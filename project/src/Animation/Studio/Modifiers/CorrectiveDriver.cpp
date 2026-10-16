#include "CorrectiveDriver.h"

#include "Animation/Studio/Skeleton.h"
#include "Animation/Studio/StudioMath.h"
#include "Util/Ozz.h"
#include "Util/String.h"

namespace Animation::Studio
{
	namespace
	{
		// ---- Rules ----
		// Measured on Fallout 4's human skeleton (docs/Studio.md §2.10, Corrective drivers): the twist and rib rules
		// from every vanilla human animation, the armpit rule fitted to minimize skin stretch on a body mesh.

		// Twist 1 turns by this times the upper arm's twist from rest, and twist 2 (relative to twist 1) by minus half
		// of that, so the shoulder end of the upper arm stays untwisted and the elbow end takes all of it.
		constexpr float kTwist1PerArmTwist = -0.92f;
		constexpr float kTwist2PerArmTwist = 0.46f;

		// For an arm raised d degrees above its rest elevation, twist 1 swings back toward the side by
		// kArmpitGain * d / (1 + d / kArmpitSaturation) degrees and twist 2 swings forward by kArmpitCounter of that,
		// so the bicep keeps its direction while the armpit skin it carries stays lower. Both slide toward the top
		// of the arm, in units per degree of raise, and twist 1 also slides back toward the shoulder: that opens the
		// crushed skin at the front of the armpit, letting the top of the shoulder fold instead.
		constexpr float kArmpitGain = 0.8f;
		constexpr float kArmpitSaturation = 53.75f;
		constexpr float kArmpitCounter = 0.94375f;
		constexpr float kArmpitTwist1Slide = 0.04f;
		constexpr float kArmpitTwist2Slide = 0.027f;
		constexpr float kArmpitShoulderSlide = 0.015f;

		// The rib helper turns about its local Y by kRibGain times the collarbone's raise past kRibThreshold, up to
		// kRibMaxAngle, and slides up the chest by kRibSlide units per degree it turns. Vanilla's left and right rules
		// differ (0.7x uncapped, 1.1x capped at 35 degrees); this is between them.
		constexpr float kRibGain = 0.9f;
		constexpr float kRibThreshold = 5.0f * ozz::math::kDegreeToRadian;
		constexpr float kRibMaxAngle = 35.0f * ozz::math::kDegreeToRadian;
		constexpr float kRibSlide = 0.087f * ozz::math::kRadianToDegree;  // Units per radian.

		constexpr std::array<size_t, 3> kBoneCounts{ 3, 4, 3 };

		// ---- Helpers ----

		ozz::math::Quaternion RestRotation(const ozz::animation::Skeleton& a_skeleton, int32_t a_joint)
		{
			const auto rest = a_skeleton.joint_rest_poses();
			return GetPoseRotation({ rest.data(), rest.size() }, a_joint);
		}

		ozz::math::Float3 RestTranslation(const ozz::animation::Skeleton& a_skeleton, int32_t a_joint)
		{
			const auto rest = a_skeleton.joint_rest_poses();
			return GetPoseTranslation({ rest.data(), rest.size() }, a_joint);
		}

		// The rotation's twist about the unit vector a_axis (swing-twist), in radians wrapped to +-pi.
		float TwistAbout(const ozz::math::Quaternion& a_rotation, const ozz::math::Float3& a_axis)
		{
			float twist = 2.0f * std::atan2(ozz::math::Dot(ozz::math::Float3{ a_rotation.x, a_rotation.y, a_rotation.z }, a_axis), a_rotation.w);
			if (twist > ozz::math::kPi) {
				twist -= 2.0f * ozz::math::kPi;
			} else if (twist < -ozz::math::kPi) {
				twist += 2.0f * ozz::math::kPi;
			}
			return twist;
		}

		float AngleBetween(const ozz::math::Float3& a_from, const ozz::math::Float3& a_to)
		{
			return std::acos(std::clamp(ozz::math::Dot(a_from, a_to), -1.0f, 1.0f));
		}

		// The upper arm's long axis in its own space: toward twist 2, through twist 1.
		ozz::math::Float3 GetLimbAxis(const ozz::animation::Skeleton& a_skeleton, int32_t a_twist1, int32_t a_twist2)
		{
			const auto direction = ozz::math::TransformVector(RestRotation(a_skeleton, a_twist1), RestTranslation(a_skeleton, a_twist2));
			return NormalizeOr(direction, { 1.0f, 0.0f, 0.0f });
		}

		// Toward the chest's parent (down the spine), in the chest's space.
		ozz::math::Float3 GetChestDown(const ozz::animation::Skeleton& a_skeleton, int32_t a_chest)
		{
			const auto toParent = ozz::math::TransformVector(ozz::math::Conjugate(RestRotation(a_skeleton, a_chest)), -RestTranslation(a_skeleton, a_chest));
			return NormalizeOr(toParent, { -1.0f, 0.0f, 0.0f });
		}

		void PreMultiplyLocal(std::span<ozz::math::SoaTransform> a_pose, int32_t a_joint, const ozz::math::Quaternion& a_rotation)
		{
			const auto local = ToQuaternion(Util::Ozz::GetSoATransformQuaternion(a_joint, a_pose));
			Util::Ozz::ApplySoATransformQuaternion(a_joint, ToSimdQuaternion(ozz::math::NormalizeSafe(a_rotation * local, ozz::math::Quaternion::identity())), a_pose);
		}

		// Turns a bone about its own position by a model-space rotation: local' = local * (R^-1 * q * R).
		void TurnInModel(ActorObject& a_actor, int32_t a_joint, const ozz::math::Float3& a_axis, float a_angle)
		{
			const auto rotation = GetRotation(a_actor.model[a_joint]);
			const auto correction = ozz::math::Conjugate(rotation) * ozz::math::Quaternion::FromAxisAngle(a_axis, a_angle) * rotation;
			Util::Ozz::MultiplySoATransformQuaternion(a_joint, ToSimdQuaternion(ozz::math::NormalizeSafe(correction, ozz::math::Quaternion::identity())), a_actor.pose->Back());
		}

		// Moves a bone by a model-space offset, through its local translation.
		void SlideInModel(ActorObject& a_actor, const ozz::animation::Skeleton& a_skeleton, int32_t a_joint, const ozz::math::Float3& a_offset)
		{
			const int16_t parent = a_skeleton.joint_parents()[a_joint];
			if (parent == ozz::animation::Skeleton::kNoParent) return;
			const auto& parentModel = a_actor.model[parent];
			const float scale = GetScale(parentModel).x;
			if (scale <= 0.00001f) return;
			auto& pose = a_actor.pose->Back();
			const auto local = ozz::math::TransformVector(ozz::math::Conjugate(GetRotation(parentModel)), a_offset) / scale;
			SetPoseTranslation(pose, a_joint, GetPoseTranslation(pose, a_joint) + local);
		}

		// ---- Drivers ----

		void SolveArmTwist(ActorObject& a_actor, const ozz::animation::Skeleton& a_skeleton, const CorrectiveDriver& a_driver, float a_weight)
		{
			const int32_t upperArm = a_driver.bones[0];
			const int32_t twist1 = a_driver.bones[1];
			const int32_t twist2 = a_driver.bones[2];
			auto& pose = a_actor.pose->Back();

			// The upper arm's twist from rest about its long axis, measured in its rest frame.
			const auto axis = GetLimbAxis(a_skeleton, twist1, twist2);
			const auto delta = ozz::math::Conjugate(RestRotation(a_skeleton, upperArm)) * GetPoseRotation(pose, upperArm);
			const float twist = TwistAbout(delta, axis) * a_weight;
			if (std::abs(twist) <= 0.00001f) return;

			// Each turns about the long axis in its parent's space.
			PreMultiplyLocal(pose, twist1, ozz::math::Quaternion::FromAxisAngle(axis, kTwist1PerArmTwist * twist));
			const auto twist1Axis = NormalizeOr(RestTranslation(a_skeleton, twist2), axis);
			PreMultiplyLocal(pose, twist2, ozz::math::Quaternion::FromAxisAngle(twist1Axis, kTwist2PerArmTwist * twist));
			a_actor.RunLocalToModel(twist1);
		}

		void SolveArmpit(ActorObject& a_actor, const ozz::animation::Skeleton& a_skeleton, const CorrectiveDriver& a_driver, float a_weight)
		{
			const int32_t chest = a_driver.bones[0];
			const int32_t upperArm = a_driver.bones[1];
			const int32_t twist1 = a_driver.bones[2];
			const int32_t twist2 = a_driver.bones[3];
			const auto& model = a_actor.model;

			// Elevation is the arm's angle from straight down the spine, in the chest's space. The rest elevation
			// comes from the rest rotations between the chest and the upper arm.
			const auto limbAxis = GetLimbAxis(a_skeleton, twist1, twist2);
			const auto downInChest = GetChestDown(a_skeleton, chest);
			auto restArm = limbAxis;
			const auto path = GetBonePath(a_skeleton, chest, upperArm);
			for (auto joint = path.rbegin(); joint != path.rend() && *joint != chest; ++joint) {
				restArm = ozz::math::TransformVector(RestRotation(a_skeleton, *joint), restArm);
			}
			const float restElevation = AngleBetween(NormalizeOr(restArm, limbAxis), downInChest);

			const auto chestRotation = GetRotation(model[chest]);
			const auto down = ozz::math::TransformVector(chestRotation, downInChest);
			const auto arm = NormalizeOr(ozz::math::TransformVector(GetRotation(model[upperArm]), limbAxis), down);
			const float raise = (std::max)(0.0f, AngleBetween(arm, down) - restElevation) * ozz::math::kRadianToDegree;
			if (raise <= 0.0001f) return;
			const float swing = kArmpitGain * raise / (1.0f + raise / kArmpitSaturation) * ozz::math::kDegreeToRadian * a_weight;

			// Back toward down, in the plane of the arm and the spine. Straight overhead that plane is undefined, so
			// it falls back to the rest arm's plane.
			auto axis = ozz::math::Cross(arm, down);
			if (ozz::math::Length(axis) < 0.05f) axis = ozz::math::Cross(ozz::math::TransformVector(chestRotation, restArm), down);
			axis = NormalizeOr(axis, AnyPerpendicular(arm));
			const auto towardTop = Orthogonalize(-down, arm, AnyPerpendicular(arm));

			TurnInModel(a_actor, twist1, axis, swing);
			a_actor.RunLocalToModel(twist1);
			TurnInModel(a_actor, twist2, axis, -kArmpitCounter * swing);
			a_actor.RunLocalToModel(twist2);
			const auto towardShoulder = arm * (-kArmpitShoulderSlide * raise * a_weight);
			SlideInModel(a_actor, a_skeleton, twist1, towardTop * (kArmpitTwist1Slide * raise * a_weight) + towardShoulder);
			SlideInModel(a_actor, a_skeleton, twist2, towardTop * (kArmpitTwist2Slide * raise * a_weight));
			a_actor.RunLocalToModel(twist1);
		}

		void SolveRibHelper(ActorObject& a_actor, const ozz::animation::Skeleton& a_skeleton, const CorrectiveDriver& a_driver, float a_weight)
		{
			const int32_t chest = a_driver.bones[0];
			const int32_t collarbone = a_driver.bones[1];
			const int32_t rib = a_driver.bones[2];
			auto& pose = a_actor.pose->Back();
			const float side = a_driver.mirrored ? -1.0f : 1.0f;

			// The collarbone's raise: its turn from rest about its local Y, as a rotation vector component. The right
			// collarbone raises about -Y.
			auto delta = ozz::math::Conjugate(RestRotation(a_skeleton, collarbone)) * GetPoseRotation(pose, collarbone);
			if (delta.w < 0.0f) delta = -delta;
			const float sine = ozz::math::Length(ozz::math::Float3{ delta.x, delta.y, delta.z });
			if (sine <= 0.000001f) return;
			const float raise = side * 2.0f * std::atan2(sine, delta.w) * delta.y / sine;
			const float angle = (std::min)(kRibGain * (std::max)(0.0f, raise - kRibThreshold), kRibMaxAngle) * a_weight;
			if (angle <= 0.00001f) return;

			Util::Ozz::MultiplySoATransformQuaternion(rib, ToSimdQuaternion(ozz::math::Quaternion::FromAxisAngle({ 0.0f, 1.0f, 0.0f }, side * angle)), pose);
			const auto up = -GetChestDown(a_skeleton, chest);
			SetPoseTranslation(pose, rib, GetPoseTranslation(pose, rib) + up * (kRibSlide * angle));
			a_actor.RunLocalToModel(rib);
		}

#ifdef TARGET_GAME_F4
		struct HumanSide
		{
			const char* upperArm;
			const char* twist1;
			const char* twist2;
			const char* collarbone;
			const char* ribHelper;
			bool mirrored;
		};

		constexpr std::array<HumanSide, 2> kHumanSides{ {
			{ "LArm_UpperArm", "LArm_UpperTwist1", "LArm_UpperTwist2", "LArm_Collarbone", "L_RibHelper", false },
			{ "RArm_UpperArm", "RArm_UpperTwist1", "RArm_UpperTwist2", "RArm_Collarbone", "R_RibHelper", true },
		} };
		constexpr const char* kHumanChest = "Chest";
#endif
	}

	PropertyPtr CorrectiveDriverObject::FindProperty(uint16_t a_element, PropertyId a_property)
	{
		if (a_element != kSelf) {
			if (a_element >= driverWeights.size() || a_property != PropertyId::kWeight) return {};
			return &driverWeights[a_element];
		}
		switch (a_property) {
		case PropertyId::kWeight:
			return &weight;
		case PropertyId::kEnabled:
			return &enabled;
		default:
			return {};
		}
	}

	void CorrectiveDriverObject::ForEachProperty(const PropertyVisitor& a_visitor)
	{
		a_visitor(kSelf, PropertyId::kEnabled, &enabled);
		a_visitor(kSelf, PropertyId::kWeight, &weight);
		for (size_t i = 0; i < driverWeights.size(); ++i) a_visitor(static_cast<uint16_t>(i), PropertyId::kWeight, &driverWeights[i]);
	}

	std::vector<int32_t> CorrectiveDriverObject::GetWriteRoots(const ozz::animation::Skeleton& a_skeleton) const
	{
		std::vector<int32_t> result;
		for (const auto& driver : settings.drivers) {
			if (!IsValidCorrectiveDriver(a_skeleton, driver)) continue;
			// Twist 1 for the arm drivers (twist 2 is below it), the rib helper for its driver.
			const int32_t root = driver.kind == CorrectiveDriverKind::kArmTwist ? driver.bones[1] : driver.bones[2];
			if (!std::ranges::contains(result, root)) result.push_back(root);
		}
		return result;
	}

	void CorrectiveDriverObject::AppendBoneReads(const ozz::animation::Skeleton& a_skeleton, std::vector<int32_t>& a_bones) const
	{
		for (const auto& driver : settings.drivers) {
			if (!IsValidCorrectiveDriver(a_skeleton, driver)) continue;
			// Arm twist: the upper arm (also twist 1's parent). Armpit: the chest and upper arm. Rib helper: the chest
			// (also its parent) and the collarbone.
			a_bones.push_back(driver.bones[0]);
			if (driver.kind != CorrectiveDriverKind::kArmTwist) a_bones.push_back(driver.bones[1]);
		}
	}

	void CorrectiveDriverObject::SetSettings(const ModifierSettings& a_settings)
	{
		if (const auto typed = std::get_if<CorrectiveDriverSettings>(&a_settings)) settings = *typed;
	}

	void CorrectiveDriverObject::Solve(ModifierContext& a_context)
	{
		if (!enabled.live || weight.live <= 0.0f || driverWeights.size() != settings.drivers.size()) return;
		const auto owner = a_context.FindActor(actor);
		if (!owner || !owner->skeleton || !owner->pose) return;
		const auto& skeleton = *owner->skeleton->data;
		const float objectWeight = std::clamp(weight.live, 0.0f, 1.0f);
		for (size_t i = 0; i < settings.drivers.size(); ++i) {
			const auto& driver = settings.drivers[i];
			const float driverWeight = objectWeight * std::clamp(driverWeights[i].live, 0.0f, kMaxCorrectiveDriverWeight);
			if (driverWeight <= 0.0f || !IsValidCorrectiveDriver(skeleton, driver)) continue;
			switch (driver.kind) {
			case CorrectiveDriverKind::kArmTwist:
				SolveArmTwist(*owner, skeleton, driver, driverWeight);
				break;
			case CorrectiveDriverKind::kArmpit:
				SolveArmpit(*owner, skeleton, driver, driverWeight);
				break;
			case CorrectiveDriverKind::kRibHelper:
				SolveRibHelper(*owner, skeleton, driver, driverWeight);
				break;
			}
		}
	}

	size_t GetCorrectiveDriverBoneCount(CorrectiveDriverKind a_kind)
	{
		const auto index = static_cast<size_t>(a_kind);
		return index < kBoneCounts.size() ? kBoneCounts[index] : 0;
	}

	std::string GetCorrectiveDriverName(const CorrectiveDriver& a_driver)
	{
		const char* side = a_driver.mirrored ? "Right" : "Left";
		switch (a_driver.kind) {
		case CorrectiveDriverKind::kArmTwist:
			return std::format("{} Arm Twist", side);
		case CorrectiveDriverKind::kArmpit:
			return std::format("{} Armpit", side);
		case CorrectiveDriverKind::kRibHelper:
			return std::format("{} Rib Helper", side);
		}
		return "Driver";
	}

	bool IsValidCorrectiveDriver(const ozz::animation::Skeleton& a_skeleton, const CorrectiveDriver& a_driver)
	{
		const int32_t jointCount = a_skeleton.num_joints();
		if (a_driver.bones.size() != GetCorrectiveDriverBoneCount(a_driver.kind)) return false;
		if (std::ranges::any_of(a_driver.bones, [&](int32_t a_bone) { return a_bone < 0 || a_bone >= jointCount; })) return false;
		const auto parents = a_skeleton.joint_parents();
		const auto& bones = a_driver.bones;
		const auto strictAncestor = [&](int32_t a_ancestor, int32_t a_bone) { return a_ancestor != a_bone && IsDescendantOrSelf(parents, a_bone, a_ancestor); };
		switch (a_driver.kind) {
		case CorrectiveDriverKind::kArmTwist:
			return parents[bones[1]] == bones[0] && parents[bones[2]] == bones[1];
		case CorrectiveDriverKind::kArmpit:
			return strictAncestor(bones[0], bones[1]) && parents[bones[2]] == bones[1] && parents[bones[3]] == bones[2];
		case CorrectiveDriverKind::kRibHelper:
			return strictAncestor(bones[0], bones[1]) && parents[bones[2]] == bones[0];
		}
		return false;
	}

	std::expected<ModifierBuild, std::string> BuildCorrectiveDriver(const ActorObject& a_actor, ObjectId& a_nextId)
	{
#ifdef TARGET_GAME_F4
		const auto& skeleton = *a_actor.skeleton->data;
		const auto names = skeleton.joint_names();
		const auto find = [&](const char* a_name) -> int32_t {
			for (int32_t joint = 0; joint < skeleton.num_joints(); ++joint) {
				if (Util::String::CaseInsensitiveCompare(names[joint], a_name)) return joint;
			}
			return -1;
		};

		// Twists before the armpit, which swings the twist bones as the twist driver left them.
		CorrectiveDriverSettings settings;
		const int32_t chest = find(kHumanChest);
		for (const auto kind : { CorrectiveDriverKind::kArmTwist, CorrectiveDriverKind::kArmpit, CorrectiveDriverKind::kRibHelper }) {
			for (const auto& side : kHumanSides) {
				CorrectiveDriver driver{ .kind = kind, .mirrored = side.mirrored };
				switch (kind) {
				case CorrectiveDriverKind::kArmTwist:
					driver.bones = { find(side.upperArm), find(side.twist1), find(side.twist2) };
					break;
				case CorrectiveDriverKind::kArmpit:
					driver.bones = { chest, find(side.upperArm), find(side.twist1), find(side.twist2) };
					break;
				case CorrectiveDriverKind::kRibHelper:
					driver.bones = { chest, find(side.collarbone), find(side.ribHelper) };
					break;
				}
				if (IsValidCorrectiveDriver(skeleton, driver)) settings.drivers.push_back(std::move(driver));
			}
		}
		if (settings.drivers.empty()) return std::unexpected("This skeleton has none of the Fallout 4 human corrective bones.");

		auto object = std::make_unique<CorrectiveDriverObject>();
		object->id = a_nextId++;
		object->name = "Corrective Drivers";
		object->actor = a_actor.id;
		object->driverWeights.resize(settings.drivers.size(), Property<float>{ 1.0f, 1.0f });
		object->settings = std::move(settings);

		ModifierBuild result;
		result.modifier = std::move(object);
		return result;
#else
		(void)a_actor;
		(void)a_nextId;
		return std::unexpected("Corrective drivers aren't available for this game yet.");
#endif
	}
}
