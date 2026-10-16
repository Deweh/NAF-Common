#pragma once
#include "Animation/Studio/Modifiers/Modifier.h"

namespace Animation::Studio
{
	// Drives a skeleton's corrective (helper) bones from the bones around them, the way the game's own animations move
	// them. Each driver turns or slides its bones on top of the pose after IK, so they stay editable: what they add is
	// kept out of edits like the auto-shoulder's offset. Properties: enabled and weight on kSelf, and element i is
	// driver i's weight.
	class CorrectiveDriverObject final : public ModifierObject
	{
	public:
		static constexpr ObjectType kType = ObjectType::kCorrectiveDriver;
		[[nodiscard]] ObjectType GetType() const override { return kType; }
		[[nodiscard]] PropertyPtr FindProperty(uint16_t a_element, PropertyId a_property) override;
		void ForEachProperty(const PropertyVisitor& a_visitor) override;

		// The bones each driver moves: the upper arm twist bones and the rib helpers.
		[[nodiscard]] std::vector<int32_t> GetWriteRoots(const ozz::animation::Skeleton& a_skeleton) const override;
		// The bones the drivers measure: the chest, collarbones and upper arms, after any IK that moves them.
		void AppendBoneReads(const ozz::animation::Skeleton& a_skeleton, std::vector<int32_t>& a_bones) const override;
		void Solve(ModifierContext& a_context) override;
		[[nodiscard]] ModifierSettings GetSettings() const override { return settings; }
		void SetSettings(const ModifierSettings& a_settings) override;

		CorrectiveDriverSettings settings;
		Property<float> weight{ 1.0f, 1.0f };
		Property<bool> enabled{ true, true };
		std::vector<Property<float>> driverWeights;  // Parallel to settings.drivers. 0 to 2.
	};

	inline constexpr float kMaxCorrectiveDriverWeight = 2.0f;

	// The bones a driver of a_kind names, in order.
	//   kArmTwist:  upper arm, twist 1 (child of the upper arm), twist 2 (child of twist 1)
	//   kArmpit:    chest (an ancestor of the upper arm), upper arm, twist 1, twist 2
	//   kRibHelper: chest (an ancestor of the collarbone), collarbone, rib helper (child of the chest)
	[[nodiscard]] size_t GetCorrectiveDriverBoneCount(CorrectiveDriverKind a_kind);
	// "Left Arm Twist", for the UI.
	[[nodiscard]] std::string GetCorrectiveDriverName(const CorrectiveDriver& a_driver);
	[[nodiscard]] bool IsValidCorrectiveDriver(const ozz::animation::Skeleton& a_skeleton, const CorrectiveDriver& a_driver);
	// Every driver the skeleton has bones for. Fails when it has none, or on games without drivers yet. Takes its id
	// from a_nextId.
	[[nodiscard]] std::expected<ModifierBuild, std::string> BuildCorrectiveDriver(const ActorObject& a_actor, ObjectId& a_nextId);
}
