#pragma once
#include "Animation/Studio/Modifiers/Modifier.h"

namespace Animation::Studio
{
	// A two-bone IK chain from upperBone to endBone, reaching for its effector locator and bending toward its pole.
	// An optional shoulder (a strict ancestor of the upper bone) follows the effector's offset from a neutral point.
	// The end bone follows the effector's rotation, with its twist spread down the lower limb.
	class IKChainObject final : public ModifierObject
	{
	public:
		static constexpr ObjectType kType = ObjectType::kIKChain;
		[[nodiscard]] ObjectType GetType() const override { return kType; }
		[[nodiscard]] PropertyPtr FindProperty(uint16_t a_element, PropertyId a_property) override;
		void ForEachProperty(const PropertyVisitor& a_visitor) override;

		[[nodiscard]] std::vector<ObjectId> GetLocators() const override { return { effector, pole }; }
		[[nodiscard]] bool UsesLocator(ObjectId a_locator) const override { return a_locator == effector || a_locator == pole; }
		void Remap(ObjectId a_actor, const std::function<ObjectId(ObjectId)>& a_locator) override;
		// The upper bone, or the shoulder when it has a valid one.
		[[nodiscard]] int32_t GetWriteRoot(const ozz::animation::Skeleton& a_skeleton) const override;
		void Solve(ModifierContext& a_context) override;
		// The path from the upper bone to the end bone, while enabled.
		[[nodiscard]] bool DrivesBone(ozz::span<const int16_t> a_parents, int32_t a_bone) const override;
		[[nodiscard]] ModifierSettings GetSettings() const override { return settings; }
		void SetSettings(const ModifierSettings& a_settings) override;

		int32_t upperBone{ -1 };
		int32_t middleBone{ -1 };
		int32_t endBone{ -1 };
		ozz::math::Float3 middleAxis{ 0.0f, 0.0f, 1.0f };
		ObjectId effector{ kNoObject };  // Locator.
		ObjectId pole{ kNoObject };      // Locator.
		IKChainSettings settings;

		Property<float> weight{ 1.0f, 1.0f };
		Property<float> soften{ 0.95f, 0.95f };
		Property<float> twist{ 0.0f, 0.0f };
		Property<bool> enabled{ true, true };
		Property<float> shoulderWeight{ 0.5f, 0.5f };
		// How much of the end bone's twist about the lower limb is spread over the bones between the middle and end
		// bones, each by how far along the lower limb it sits. 0 leaves it all on the end bone.
		Property<float> twistDistribution{ 1.0f, 1.0f };

		// Evaluation output.
		bool reached{ true };
		// In model space: the shoulder, its neutral point and the point it tracked. Nullopt without a shoulder.
		std::optional<ShoulderGuide> shoulderGuide;

	private:
		// Swings the shoulder toward a_targetModel before the two-bone solve, which then starts from the moved limb.
		void SolveShoulder(ActorObject& a_actor, const ozz::math::Float3& a_targetModel);
		// Turns the bones between the middle and end bones about the lower limb by their share of the twist from the
		// end bone's model rotation to a_targetModel. Leaves the end bone's model transform unchanged.
		void DistributeTwist(ActorObject& a_actor, const ozz::math::Quaternion& a_targetModel, float a_weight) const;
	};

	// The bones must form an upper -> middle -> end ancestor chain. Intervening bones may be skipped.
	[[nodiscard]] bool IsValidChain(const ozz::animation::Skeleton& a_skeleton, int32_t a_upperBone, int32_t a_middleBone, int32_t a_endBone);
	// A shoulder is a strict ancestor of the upper bone.
	[[nodiscard]] bool IsValidShoulder(const ozz::animation::Skeleton& a_skeleton, int32_t a_shoulder, int32_t a_upperBone);
	// The upper bone's parent, when it carries little besides the limb: a clavicle does, a pelvis (with the spine
	// below it) doesn't. -1 otherwise.
	[[nodiscard]] int32_t DetectShoulder(const ozz::animation::Skeleton& a_skeleton, int32_t a_upperBone);
	// a_targetModel in the space of a_shoulder's parent, on a_model: the shoulder's neutral point for that target.
	[[nodiscard]] ozz::math::Float3 ToShoulderNeutral(const ozz::animation::Skeleton& a_skeleton, std::span<const ozz::math::Float4x4> a_model,
		int32_t a_shoulder, const ozz::math::Float3& a_targetModel);
	// The neutral point with the end bone at rest. For chains saved without one.
	[[nodiscard]] ozz::math::Float3 GetRestShoulderNeutral(const ozz::animation::Skeleton& a_skeleton, int32_t a_shoulder, int32_t a_endBone);

	// A chain on a_actor's current pose, plus its effector (at the end bone) and pole locators, in scene space.
	// a_sceneFromModel maps the actor's model space into scene space. Each new object takes the next id from a_nextId.
	[[nodiscard]] std::expected<ModifierBuild, std::string> BuildIKChain(const ActorObject& a_actor, const ozz::math::Float4x4& a_sceneFromModel,
		int32_t a_upperBone, int32_t a_middleBone, int32_t a_endBone, ObjectId& a_nextId);
}
