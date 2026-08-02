#include "ProjectEx.h"

#include "Modifiers/Hand.h"
#include "Modifiers/IKChain.h"
#include "Modifiers/SplineIK.h"
#include "Skeleton.h"
#include "StudioMath.h"

namespace Animation::Studio
{
	namespace
	{
		// The chain's effector, as the neutral point of a_shoulder on the evaluated pose.
		std::optional<ozz::math::Float3> CaptureShoulderNeutral(ProjectEx& a_project, const ActorObject& a_actor, const IKChainObject& a_chain, int32_t a_shoulder)
		{
			const auto effector = a_project.GetWorldTransform({ a_chain.effector });
			const auto inverseRoot = TryInvert(a_actor.rootWorld);
			if (!effector || !inverseRoot || !a_actor.hasRoot) return std::nullopt;
			return ToShoulderNeutral(*a_actor.skeleton->data, a_actor.model, a_shoulder, TransformPoint(*inverseRoot, GetTranslation(*effector)));
		}
	}

	std::optional<ProjectEx::IKChainInfo> ProjectEx::GetIKChain(ObjectId a_id) const
	{
		const auto chain = FindObject<IKChainObject>(a_id);
		if (!chain) return std::nullopt;
		IKChainInfo info{ chain->actor, chain->upperBone, chain->middleBone, chain->endBone, chain->effector, chain->pole, chain->reached, chain->settings };
		const auto actor = FindObject<ActorObject>(chain->actor);
		if (chain->shoulderGuide && actor && actor->hasRoot) {
			const auto& guide = *chain->shoulderGuide;
			const auto& root = actor->rootWorld;
			info.shoulderGuide = ShoulderGuide{ TransformPoint(root, guide.shoulder), TransformPoint(root, guide.neutral), TransformPoint(root, guide.dummy) };
		}
		return info;
	}

	bool ProjectEx::SetIKChainSettings(ObjectId a_id, const IKChainSettings& a_settings)
	{
		const auto chain = FindObject<IKChainObject>(a_id);
		const auto actor = chain ? FindObject<ActorObject>(chain->actor) : nullptr;
		if (!actor || !actor->skeleton) return false;
		if (a_settings.shoulderBone >= 0 && !IsValidShoulder(*actor->skeleton->data, a_settings.shoulderBone, chain->upperBone)) return false;
		const auto& neutral = a_settings.shoulderNeutral;
		if (!std::isfinite(a_settings.shoulderMaxAngle) || !std::isfinite(neutral.x) || !std::isfinite(neutral.y) || !std::isfinite(neutral.z)) return false;
		auto settings = a_settings;
		settings.shoulderBone = (std::max)(settings.shoulderBone, -1);
		settings.shoulderMaxAngle = std::clamp(settings.shoulderMaxAngle, 0.0f, ozz::math::kPi);

		// A shoulder widens what the chain writes, which can put its own effector or pole below it.
		const auto previous = chain->settings;
		chain->settings = settings;
		const bool selfDependent = m_evaluator.HasSelfDependentModifier();
		chain->settings = previous;
		if (selfDependent) return false;

		TransactionScope transaction(*this, "Edit IK chain settings");
		Pause();
		// A new shoulder measures neutral in its own parent's space, from where the effector is now.
		if (settings.shoulderBone >= 0 && settings.shoulderBone != previous.shoulderBone) {
			if (const auto neutral = CaptureShoulderNeutral(*this, *actor, *chain, settings.shoulderBone)) settings.shoulderNeutral = *neutral;
		}
		return RecordModifierSettings(a_id, settings);
	}

	bool ProjectEx::SetIKChainShoulderNeutral(ObjectId a_id)
	{
		const auto chain = FindObject<IKChainObject>(a_id);
		const auto actor = chain ? FindObject<ActorObject>(chain->actor) : nullptr;
		if (!actor || !actor->skeleton || chain->settings.shoulderBone < 0) return false;
		TransactionScope transaction(*this, "Set shoulder neutral");
		Pause();
		const auto neutral = CaptureShoulderNeutral(*this, *actor, *chain, chain->settings.shoulderBone);
		if (!neutral) return false;
		auto settings = chain->settings;
		settings.shoulderNeutral = *neutral;
		return SetIKChainSettings(a_id, settings);
	}

	std::expected<ProjectEx::ObjectId, std::string> ProjectEx::AddHand(ObjectId a_actor, int32_t a_handBone)
	{
		const auto actor = FindObject<ActorObject>(a_actor);
		if (!actor || !actor->skeleton) return std::unexpected("The actor has no skeleton.");
		auto build = BuildHand(*actor, a_handBone, m_nextObjectId);
		if (!build) return std::unexpected(std::move(build.error()));

		TransactionScope transaction(*this, "Add finger controls");
		Pause();
		return RecordModifierBuild(*actor, std::move(*build));
	}

	ProjectEx::ObjectId ProjectEx::RecordModifierBuild(const ActorObject& a_actor, ModifierBuild a_build)
	{
		for (auto& locator : a_build.locators) {
			const ObjectId id = locator->id;
			Record(ObjectLifetimeChange{ ObjectSnapshot{ .id = id, .object = std::move(locator), .index = m_objects.size() }, true });
		}
		const ObjectId id = a_build.modifier->id;
		Record(ObjectLifetimeChange{ ObjectSnapshot{ .id = id, .object = std::move(a_build.modifier), .index = m_objects.size(), .modifierIndex = a_actor.modifiers.size() }, true });
		return id;
	}

	std::optional<ProjectEx::HandInfo> ProjectEx::GetHand(ObjectId a_id) const
	{
		const auto hand = FindObject<HandObject>(a_id);
		if (!hand) return std::nullopt;
		return HandInfo{ hand->actor, hand->handBone, hand->settings };
	}

	bool ProjectEx::SetHandSettings(ObjectId a_id, const HandSettings& a_settings)
	{
		const auto hand = FindObject<HandObject>(a_id);
		if (!hand || a_settings.fingers.size() != hand->settings.fingers.size()) return false;
		// Only the tuning changes. The fingers and their axes stay as detected.
		auto settings = hand->settings;
		for (size_t i = 0; i < settings.fingers.size(); ++i) {
			auto& finger = settings.fingers[i];
			const auto& requested = a_settings.fingers[i];
			if (requested.maxCurl.size() != finger.maxCurl.size() || requested.straighten.size() != finger.straighten.size() || requested.maxExtend.size() != finger.maxExtend.size()) return false;
			for (size_t k = 0; k < finger.maxCurl.size(); ++k) {
				if (!std::isfinite(requested.maxCurl[k])) return false;
				finger.maxCurl[k] = std::clamp(requested.maxCurl[k], 0.0f, ozz::math::kPi);
			}
			for (size_t k = 0; k < finger.straighten.size(); ++k) {
				if (!std::isfinite(requested.straighten[k]) || !std::isfinite(requested.maxExtend[k])) return false;
				finger.straighten[k] = std::clamp(requested.straighten[k], -ozz::math::kPi * 0.5f, ozz::math::kPi * 0.5f);
				finger.maxExtend[k] = std::clamp(requested.maxExtend[k], 0.0f, ozz::math::kPi * 0.5f);
			}
			if (!std::isfinite(requested.maxSpread)) return false;
			finger.maxSpread = std::clamp(requested.maxSpread, 0.0f, ozz::math::kPi * 0.5f);
			finger.flip = requested.flip;
		}
		TransactionScope transaction(*this, "Edit finger setup");
		Pause();
		return RecordModifierSettings(a_id, std::move(settings));
	}

	std::expected<ProjectEx::ObjectId, std::string> ProjectEx::AddSplineIK(ObjectId a_actor, int32_t a_startBone, int32_t a_endBone, uint32_t a_controlCount)
	{
		const auto actor = FindObject<ActorObject>(a_actor);
		if (!actor || !actor->skeleton) return std::unexpected("The actor has no skeleton.");
		const auto path = GetBonePath(*actor->skeleton->data, a_startBone, a_endBone);
		if (path.size() < 3) return std::unexpected("The start bone must be an ancestor of the end bone, with at least one bone between them.");

		TransactionScope transaction(*this, "Add spline IK");
		Pause();
		Evaluate();
		if (!actor->hasRoot) return std::unexpected("The actor must be bound and loaded.");
		auto build = BuildSplineIK(*actor, *TryInvert(ToMatrix(m_sceneCenter)) * actor->rootWorld, path, a_controlCount, m_nextObjectId);
		if (!build) return std::unexpected(std::move(build.error()));
		return RecordModifierBuild(*actor, std::move(*build));
	}

	size_t ProjectEx::GetSplineIKControlLimit(ObjectId a_actor, int32_t a_startBone, int32_t a_endBone)
	{
		const auto actor = FindObject<ActorObject>(a_actor);
		if (!actor || !actor->skeleton) return 0;
		const auto path = GetBonePath(*actor->skeleton->data, a_startBone, a_endBone);
		if (path.size() < 3) return 0;
		Evaluate();
		return Studio::GetSplineIKControlLimit(*actor, path);
	}

	std::optional<ProjectEx::SplineIKInfo> ProjectEx::GetSplineIK(ObjectId a_id) const
	{
		const auto spline = FindObject<SplineIKObject>(a_id);
		if (!spline) return std::nullopt;
		return SplineIKInfo{ spline->actor, spline->bones, spline->controls, spline->settings };
	}

	bool ProjectEx::SetSplineIKSettings(ObjectId a_id, const SplineIKSettings& a_settings)
	{
		if (!FindObject<SplineIKObject>(a_id)) return false;
		TransactionScope transaction(*this, "Edit spline IK settings");
		Pause();
		return RecordModifierSettings(a_id, a_settings);
	}

	std::vector<ozz::math::Float3> ProjectEx::GetSplineIKCurve(ObjectId a_id)
	{
		Evaluate();
		const auto spline = FindObject<SplineIKObject>(a_id);
		const auto actor = spline ? FindObject<ActorObject>(spline->actor) : nullptr;
		if (!actor || !actor->hasRoot) return {};
		std::vector<ozz::math::Float3> result;
		result.reserve(spline->curve.size());
		for (const auto& point : spline->curve) result.push_back(TransformPoint(actor->rootWorld, point));
		return result;
	}

	ProjectEx::ObjectId ProjectEx::GetLocatorOwner(ObjectId a_locator) const
	{
		return m_objects.GetLocatorOwner(a_locator);
	}

	std::expected<ProjectEx::ObjectId, std::string> ProjectEx::AddIKChain(ObjectId a_actor, int32_t a_upperBone, int32_t a_middleBone, int32_t a_endBone)
	{
		const auto actor = FindObject<ActorObject>(a_actor);
		if (!actor || !actor->skeleton) return std::unexpected("The actor has no skeleton.");
		if (!IsValidChain(*actor->skeleton->data, a_upperBone, a_middleBone, a_endBone)) {
			return std::unexpected("The selected bones must form an upper -> middle -> end ancestor chain.");
		}

		TransactionScope transaction(*this, "Add IK chain");
		Pause();
		Evaluate();
		if (!actor->hasRoot) return std::unexpected("The actor must be bound and loaded.");
		// The locators go in scene space.
		auto build = BuildIKChain(*actor, *TryInvert(ToMatrix(m_sceneCenter)) * actor->rootWorld, a_upperBone, a_middleBone, a_endBone, m_nextObjectId);
		if (!build) return std::unexpected(std::move(build.error()));
		return RecordModifierBuild(*actor, std::move(*build));
	}

	bool ProjectEx::IsDrivenByModifier(ElementRef a_ref) const
	{
		const auto actor = FindObject<ActorObject>(a_ref.object);
		if (!actor || !actor->skeleton || a_ref.element >= actor->bones.size()) return false;
		const auto parents = actor->skeleton->data->joint_parents();
		return std::ranges::any_of(actor->modifiers, [&](ObjectId a_id) {
			const auto modifier = m_objects.FindModifier(a_id);
			return modifier && modifier->DrivesBone(parents, a_ref.element);
		});
	}

	std::vector<bool> ProjectEx::GetDrivenBones(ObjectId a_actor) const
	{
		const auto actor = FindObject<ActorObject>(a_actor);
		if (!actor || !actor->skeleton) return {};
		const auto parents = actor->skeleton->data->joint_parents();
		std::vector<bool> result(actor->bones.size(), false);
		for (const auto id : actor->modifiers) {
			const auto modifier = m_objects.FindModifier(id);
			if (!modifier) continue;
			for (size_t bone = 0; bone < result.size(); ++bone) {
				if (!result[bone] && modifier->DrivesBone(parents, static_cast<int32_t>(bone))) result[bone] = true;
			}
		}
		return result;
	}
}
