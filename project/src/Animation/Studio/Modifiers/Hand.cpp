#include "Hand.h"

#include "Animation/Studio/Skeleton.h"
#include "Animation/Studio/StudioMath.h"
#include "Util/Ozz.h"
#include "Util/String.h"

namespace Animation::Studio
{
	PropertyPtr HandObject::FindProperty(uint16_t a_element, PropertyId a_property)
	{
		if (a_element != kSelf) {
			if (a_element >= fingers.size()) return {};
			auto& finger = fingers[a_element];
			return a_property == PropertyId::kCurl ? PropertyPtr{ &finger.curl } : a_property == PropertyId::kSpread ? PropertyPtr{ &finger.spread } : PropertyPtr{};
		}
		switch (a_property) {
		case PropertyId::kCurl:
			return &curl;
		case PropertyId::kSpread:
			return &spread;
		case PropertyId::kWeight:
			return &weight;
		case PropertyId::kEnabled:
			return &enabled;
		default:
			return {};
		}
	}

	void HandObject::ForEachProperty(const PropertyVisitor& a_visitor)
	{
		a_visitor(kSelf, PropertyId::kEnabled, &enabled);
		a_visitor(kSelf, PropertyId::kWeight, &weight);
		a_visitor(kSelf, PropertyId::kCurl, &curl);
		a_visitor(kSelf, PropertyId::kSpread, &spread);
		for (size_t i = 0; i < fingers.size(); ++i) {
			a_visitor(static_cast<uint16_t>(i), PropertyId::kCurl, &fingers[i].curl);
			a_visitor(static_cast<uint16_t>(i), PropertyId::kSpread, &fingers[i].spread);
		}
	}

	void HandObject::SetSettings(const ModifierSettings& a_settings)
	{
		if (const auto typed = std::get_if<HandSettings>(&a_settings)) settings = *typed;
	}

	void HandObject::ApplyToPose(ActorObject& a_actor) const
	{
		if (!enabled.live || weight.live <= 0.0f || fingers.size() != settings.fingers.size()) return;
		auto& pose = a_actor.pose->Back();
		const auto jointCount = static_cast<int32_t>(a_actor.bones.size());
		const float clampedWeight = std::clamp(weight.live, 0.0f, 1.0f);
		for (size_t i = 0; i < fingers.size(); ++i) {
			const auto& setup = settings.fingers[i];
			const auto& values = fingers[i];
			const float fingerCurl = std::clamp(curl.live + values.curl.live, -1.0f, 1.0f);
			const float sign = setup.flip ? -1.0f : 1.0f;
			const float fingerSpread = std::clamp(spread.live + values.spread.live, -1.0f, 1.0f) * clampedWeight;
			const bool flat = !setup.straighten.empty();
			for (size_t k = 0; k < setup.bones.size() && k < setup.curlAxes.size() && k < setup.maxCurl.size(); ++k) {
				if (flat && (k >= setup.straighten.size() || k >= setup.maxExtend.size())) break;
				const int32_t bone = setup.bones[k];
				if (bone < 0 || bone >= jointCount) break;
				const float spreadAngle = k == 0 ? fingerSpread * setup.maxSpread * setup.spreadScale : 0.0f;
				if (spreadAngle != 0.0f) {
					const auto rotation = ozz::math::Quaternion::FromAxisAngle(NormalizeOr(setup.spreadAxis, { 0.0f, 0.0f, 1.0f }), spreadAngle);
					Util::Ozz::MultiplySoATransformQuaternion(bone, ToSimdQuaternion(rotation), pose);
				}
				// Curl 0 is straight: the rest bend is undone, then curl bends toward the palm or back past straight.
				// Hands from before version 5 have no straighten, so curl 0 is the rest pose there.
				const float curlAngle = flat ? clampedWeight * (setup.straighten[k] + sign * fingerCurl * (fingerCurl >= 0.0f ? setup.maxCurl[k] : setup.maxExtend[k])) :
				                               clampedWeight * sign * fingerCurl * setup.maxCurl[k];
				if (curlAngle != 0.0f) {
					const auto rotation = ozz::math::Quaternion::FromAxisAngle(NormalizeOr(setup.curlAxes[k], { 0.0f, 0.0f, 1.0f }), curlAngle);
					Util::Ozz::MultiplySoATransformQuaternion(bone, ToSimdQuaternion(rotation), pose);
				}
			}
		}
	}

	std::expected<ModifierBuild, std::string> BuildHand(const ActorObject& a_actor, int32_t a_handBone, ObjectId& a_nextId)
	{
		const auto& skeleton = *a_actor.skeleton->data;
		auto settings = DetectHand(skeleton, a_handBone);
		if (!settings) return std::unexpected(std::move(settings.error()));

		auto hand = std::make_unique<HandObject>();
		hand->id = a_nextId++;
		hand->name = std::format("{} Fingers", skeleton.joint_names()[a_handBone]);
		hand->actor = a_actor.id;
		hand->handBone = a_handBone;
		hand->settings = std::move(*settings);
		hand->fingers.resize(hand->settings.fingers.size());
		// Curl 0 is flat, so new hands start relaxed instead, near most skeletons' rest pose.
		for (size_t i = 0; i < hand->fingers.size(); ++i) {
			const float relaxed = kRelaxedHandPreset.GetCurl(i, hand->fingers.size());
			hand->fingers[i].curl = { relaxed, relaxed };
		}

		ModifierBuild result;
		result.modifier = std::move(hand);
		return result;
	}

	bool IsValidHand(const ozz::animation::Skeleton& a_skeleton, int32_t a_handBone, const HandSettings& a_settings)
	{
		const int32_t jointCount = a_skeleton.num_joints();
		if (a_handBone < 0 || a_handBone >= jointCount || a_settings.fingers.empty()) return false;
		const auto parents = a_skeleton.joint_parents();
		return std::ranges::all_of(a_settings.fingers, [&](const FingerSetup& a_finger) {
			const size_t count = a_finger.bones.size();
			if (count == 0 || a_finger.curlAxes.size() != count || a_finger.maxCurl.size() != count) return false;
			if (a_finger.straighten.size() != a_finger.maxExtend.size() || (!a_finger.straighten.empty() && a_finger.straighten.size() != count)) return false;
			// A chain hanging directly from the hand.
			int32_t parent = a_handBone;
			for (const int32_t bone : a_finger.bones) {
				if (bone < 0 || bone >= jointCount || parents[bone] != parent) return false;
				parent = bone;
			}
			return true;
		});
	}

	std::expected<HandSettings, std::string> DetectHand(const ozz::animation::Skeleton& a_skeleton, int32_t a_handBone)
	{
		const auto& skeleton = a_skeleton;
		const int32_t jointCount = skeleton.num_joints();
		if (a_handBone < 0 || a_handBone >= jointCount) return std::unexpected("The hand bone is not valid.");
		const auto names = skeleton.joint_names();
		const auto children = GetChildren(skeleton);

		// Each child of the hand starts a finger, followed down while it doesn't branch. Single bones are
		// weapon and attach nodes, not fingers.
		std::vector<std::vector<int32_t>> chains;
		for (const int32_t child : children[a_handBone]) {
			std::vector<int32_t> chain{ child };
			while (children[chain.back()].size() == 1) chain.push_back(children[chain.back()].front());
			if (chain.size() >= 2) chains.push_back(std::move(chain));
		}
		const auto isNamedFinger = [&](const std::vector<int32_t>& a_chain) {
			const auto name = Util::String::ToLower(names[a_chain.front()]);
			return name.contains("finger") || name.contains("thumb");
		};
		if (std::ranges::any_of(chains, isNamedFinger)) std::erase_if(chains, [&](const auto& a_chain) { return !isNamedFinger(a_chain); });
		if (chains.empty()) return std::unexpected("No fingers were found below this bone.");

		// Axes come from the rest pose, so they don't depend on how the hand is posed right now.
		const auto restModel = GetRestModel(skeleton);
		if (!restModel) return std::unexpected("The skeleton's rest pose could not be evaluated.");
		const auto& rest = *restModel;
		const auto position = [&](int32_t a_bone) { return GetTranslation(rest[a_bone]); };
		const auto toLocal = [&](int32_t a_bone, const ozz::math::Float3& a_direction) {
			const auto inverse = TryInvert(rest[a_bone]);
			return NormalizeOr(inverse ? TransformDirection(*inverse, a_direction) : a_direction, { 0.0f, 0.0f, 1.0f });
		};

		// The thumb is the finger whose base direction strays furthest from the others'. The rest are ordered
		// from it, index to pinky.
		const auto hand = position(a_handBone);
		std::optional<size_t> thumb;
		if (chains.size() >= 3) {
			auto mean = ozz::math::Float3::zero();
			for (const auto& chain : chains) mean = mean + NormalizeOr(position(chain.front()) - hand, mean);
			mean = NormalizeOr(mean, { 1.0f, 0.0f, 0.0f });
			float lowest = 2.0f;
			for (size_t i = 0; i < chains.size(); ++i) {
				const float alignment = ozz::math::Dot(NormalizeOr(position(chains[i].front()) - hand, mean), mean);
				if (alignment < lowest) {
					lowest = alignment;
					thumb = i;
				}
			}
		}
		if (thumb) {
			std::rotate(chains.begin(), chains.begin() + static_cast<std::ptrdiff_t>(*thumb), chains.begin() + static_cast<std::ptrdiff_t>(*thumb) + 1);
			const auto thumbBase = position(chains.front().front());
			std::stable_sort(chains.begin() + 1, chains.end(), [&](const auto& a_left, const auto& a_right) {
				return ozz::math::Length(position(a_left.front()) - thumbBase) < ozz::math::Length(position(a_right.front()) - thumbBase);
			});
		}
		const size_t firstFinger = thumb ? 1 : 0;

		// Palm normal from the index and pinky bases. Its sign depends on the side: actors face +Y, so the
		// actor's left hand is on -X. Assumes palms down in the rest pose; Flip covers skeletons where that's wrong.
		const auto fingerDirection = [&](const std::vector<int32_t>& a_chain) { return NormalizeOr(position(a_chain[1]) - position(a_chain[0]), { 1.0f, 0.0f, 0.0f }); };
		ozz::math::Float3 normal;
		if (chains.size() - firstFinger >= 2) {
			normal = ozz::math::Cross(position(chains[firstFinger].front()) - hand, position(chains.back().front()) - hand);
		} else {
			normal = ozz::math::Cross(fingerDirection(chains.front()), { 0.0f, 0.0f, 1.0f });
		}
		normal = NormalizeOr(normal, AnyPerpendicular(fingerDirection(chains.front())));
		if (hand.x < 0.0f) normal = -normal;

		auto middle = ozz::math::Float3::zero();
		for (size_t i = firstFinger; i < chains.size(); ++i) middle = middle + position(chains[i].front());
		middle = middle / static_cast<float>(chains.size() - firstFinger);
		std::vector<float> sides(chains.size());
		float widest = 0.0f;
		for (size_t i = 0; i < chains.size(); ++i) {
			sides[i] = ozz::math::Dot(ozz::math::Cross(normal, fingerDirection(chains[i])), position(chains[i].front()) - middle);
			if (i >= firstFinger) widest = (std::max)(widest, std::abs(sides[i]));
		}

		const auto restLocal = UnpackSoaTransforms({ skeleton.joint_rest_poses().data(), skeleton.joint_rest_poses().size() }, static_cast<size_t>(jointCount));
		// Toward the next bone, or the tip's continuation of its parent.
		const auto boneDirection = [&](const std::vector<int32_t>& a_chain, size_t a_index) {
			return a_index + 1 < a_chain.size() ? position(a_chain[a_index + 1]) - position(a_chain[a_index]) : position(a_chain[a_index]) - position(a_chain[a_index - 1]);
		};

		HandSettings result;
		for (size_t i = 0; i < chains.size(); ++i) {
			const auto& chain = chains[i];
			const bool isThumb = thumb && i == 0;
			const size_t count = chain.size();

			// The skeleton's own hinges: a finger joint's rest rotation is a bend about its hinge, so its axis is
			// the curl axis, whatever the hand's orientation. Model space, signed so the rest bend is a curl.
			std::vector<std::optional<ozz::math::Float3>> hinges(count);
			std::vector<float> restBends(count, 0.0f);
			size_t dominant = count;
			for (size_t k = 1; k < count; ++k) {
				auto rotation = ozz::math::NormalizeSafe(restLocal[chain[k]].rotation, ozz::math::Quaternion::identity());
				if (rotation.w < 0.0f) rotation = -rotation;
				const ozz::math::Float3 vector{ rotation.x, rotation.y, rotation.z };
				const float sine = ozz::math::Length(vector);
				const float angle = 2.0f * std::atan2(sine, rotation.w);
				if (angle < 8.0f * ozz::math::kDegreeToRadian) continue;
				// The rotation's axis is the same in the joint's frame and its parent's.
				hinges[k] = NormalizeOr(TransformDirection(rest[chain[k]], vector / sine), normal);
				restBends[k] = angle;
				if (dominant == count || angle > restBends[dominant]) dominant = k;
			}
			// A joint bent backward at rest would otherwise curl backward; the most bent joint sets the direction.
			for (size_t k = 0; k < count && dominant < count; ++k) {
				if (hinges[k] && ozz::math::Dot(*hinges[k], *hinges[dominant]) < 0.0f) {
					hinges[k] = -*hinges[k];
				}
			}

			FingerSetup setup;
			setup.bones = chain;
			for (size_t k = 0; k < count; ++k) {
				// A joint without a measurable bend (the base, or a straight joint) uses the nearest joint's hinge,
				// so the finger curls in one plane.
				std::optional<ozz::math::Float3> hinge;
				for (size_t distance = 0; distance < count && !hinge; ++distance) {
					if (k + distance < count && hinges[k + distance]) hinge = hinges[k + distance];
					else if (distance <= k && hinges[k - distance]) hinge = hinges[k - distance];
				}
				ozz::math::Float3 axis;
				if (hinge) {
					axis = toLocal(chain[k], *hinge);
				} else {
					// A finger straight at rest: estimate from the palm, then snap to the bone's nearest local axis
					// across it, which is how skeletons are normally authored.
					const auto estimate = toLocal(chain[k], NormalizeOr(ozz::math::Cross(boneDirection(chain, k), normal), AnyPerpendicular(normal)));
					const auto along = toLocal(chain[k], boneDirection(chain, k));
					const std::array<ozz::math::Float3, 3> basis{ ozz::math::Float3{ 1.0f, 0.0f, 0.0f }, ozz::math::Float3{ 0.0f, 1.0f, 0.0f }, ozz::math::Float3{ 0.0f, 0.0f, 1.0f } };
					float best = -1.0f;
					axis = estimate;
					for (const auto& candidate : basis) {
						if (std::abs(ozz::math::Dot(candidate, along)) > 0.7f) continue;
						const float alignment = std::abs(ozz::math::Dot(candidate, estimate));
						if (alignment > best) {
							best = alignment;
							axis = ozz::math::Dot(candidate, estimate) < 0.0f ? -candidate : candidate;
						}
					}
				}
				setup.curlAxes.push_back(axis);

				// The limits are from straight, which is where curl 0 puts the finger.
				static constexpr std::array<float, 3> kFingerCurl{ 90.0f, 100.0f, 70.0f };
				static constexpr std::array<float, 3> kThumbCurl{ 35.0f, 45.0f, 60.0f };
				static constexpr std::array<float, 3> kFingerExtend{ 20.0f, 5.0f, 10.0f };
				static constexpr std::array<float, 3> kThumbExtend{ 5.0f, 10.0f, 20.0f };
				const auto& limits = isThumb ? kThumbCurl : kFingerCurl;
				const auto& extends = isThumb ? kThumbExtend : kFingerExtend;
				setup.maxCurl.push_back(limits[(std::min)(k, limits.size() - 1)] * ozz::math::kDegreeToRadian);
				setup.maxExtend.push_back(extends[(std::min)(k, extends.size() - 1)] * ozz::math::kDegreeToRadian);
			}

			// Straight is each bone in line with the one before it, and the base in the palm's plane. Measured about
			// each curl axis, so straightening stays in the plane the finger curls in. The thumb's base sits out of
			// the palm in any skeleton, so it keeps its rest angle.
			for (size_t k = 0; k < count; ++k) {
				const auto axis = NormalizeOr(TransformDirection(rest[chain[k]], setup.curlAxes[k]), normal);
				float angle = 0.0f;
				if (k == 0) {
					const auto palm = normal - axis * ozz::math::Dot(normal, axis);
					if (!isThumb && ozz::math::Length(palm) > 0.1f) {
						const auto unitPalm = ozz::math::Normalize(palm);
						const auto direction = position(chain[1]) - position(chain[0]);
						angle = AngleAbout(direction, direction - unitPalm * ozz::math::Dot(direction, unitPalm), axis);
					}
				} else {
					// The tip has no child to aim at. It points along the same local axis as its parent's bone.
					const auto direction = k + 1 < count ? position(chain[k + 1]) - position(chain[k]) : TransformDirection(rest[chain[k]], restLocal[chain[k]].translation);
					angle = AngleAbout(direction, position(chain[k]) - position(chain[k - 1]), axis);
				}
				setup.straighten.push_back(std::clamp(angle, -ozz::math::kPi * 0.5f, ozz::math::kPi * 0.5f));
			}

			// Spread swings the base across its curl plane: about the axis perpendicular to the bone and its curl
			// axis, signed to agree with the palm normal that spreadScale is measured against.
			const auto alongLocal = toLocal(chain.front(), boneDirection(chain, 0));
			auto spreadAxis = NormalizeOr(ozz::math::Cross(alongLocal, setup.curlAxes.front()), toLocal(chain.front(), normal));
			if (ozz::math::Dot(TransformDirection(rest[chain.front()], spreadAxis), normal) < 0.0f) spreadAxis = -spreadAxis;
			setup.spreadAxis = spreadAxis;
			// Positive spread fans away from the middle of the hand; the middle finger barely moves.
			setup.spreadScale = isThumb ? (sides[i] < 0.0f ? -1.0f : 1.0f) : widest > 0.00001f ? sides[i] / widest : 0.0f;
			result.fingers.push_back(std::move(setup));
		}
		return result;
	}
}
