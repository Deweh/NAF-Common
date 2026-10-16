#pragma once
#include "Animation/Studio/Modifiers/Modifier.h"

namespace Animation::Studio
{
	// Bones from the start of the path to its end follow a Catmull-Rom curve through the control locators.
	class SplineIKObject final : public ModifierObject
	{
	public:
		static constexpr ObjectType kType = ObjectType::kSplineIK;
		[[nodiscard]] ObjectType GetType() const override { return kType; }
		[[nodiscard]] PropertyPtr FindProperty(uint16_t a_element, PropertyId a_property) override;
		void ForEachProperty(const PropertyVisitor& a_visitor) override;

		[[nodiscard]] std::vector<ObjectId> GetLocators() const override { return controls; }
		[[nodiscard]] bool UsesLocator(ObjectId a_locator) const override { return std::ranges::contains(controls, a_locator); }
		void Remap(ObjectId a_actor, const std::function<ObjectId(ObjectId)>& a_locator) override;
		// The start bone.
		[[nodiscard]] std::vector<int32_t> GetWriteRoots(const ozz::animation::Skeleton& a_skeleton) const override;
		void Solve(ModifierContext& a_context) override;
		// The path, while enabled.
		[[nodiscard]] bool DrivesBone(ozz::span<const int16_t> a_parents, int32_t a_bone) const override;
		[[nodiscard]] ModifierSettings GetSettings() const override { return settings; }
		void SetSettings(const ModifierSettings& a_settings) override;

		struct ControlAxes
		{
			ozz::math::Float3 aim{ 1.0f, 0.0f, 0.0f };  // In the control's local space.
			ozz::math::Float3 up{ 0.0f, 0.0f, 1.0f };

			bool operator==(const ControlAxes&) const = default;
		};

		std::vector<int32_t> bones;              // The path, start to end. Each is the parent of the next.
		std::vector<ozz::math::Float3> aimAxes;  // Per bone, in its local space: toward the next bone.
		std::vector<ozz::math::Float3> upAxes;   // Per bone, in its local space. Calibrated at creation.
		std::vector<ObjectId> controls;          // Locators, start to end.
		std::vector<ControlAxes> controlAxes;    // Parallel to controls.
		SplineIKSettings settings;

		Property<float> weight{ 1.0f, 1.0f };
		Property<float> twist{ 0.0f, 0.0f };
		Property<float> stretch{ 0.0f, 0.0f };
		Property<float> tangentLength{ 1.0f, 1.0f };
		// 1 turns the bones to follow the curve; 0 keeps their rotation and moves them onto it instead.
		Property<float> orient{ 1.0f, 1.0f };
		Property<bool> enabled{ true, true };

		// Evaluation output.
		std::vector<ozz::math::Float3> curve;  // Model space samples.
	};

	// A spline along a_path (start to end, at least 3 bones) on a_actor's current pose, plus a_controlCount control
	// locators in scene space, on bones spread evenly along the path. a_controlCount is clamped to what the path
	// allows. a_sceneFromModel maps the actor's model space into scene space. Each new object takes the next id from
	// a_nextId.
	[[nodiscard]] std::expected<ModifierBuild, std::string> BuildSplineIK(const ActorObject& a_actor, const ozz::math::Float4x4& a_sceneFromModel,
		const std::vector<int32_t>& a_path, uint32_t a_controlCount, ObjectId& a_nextId);
	// The most controls a spline along a_path can have on a_actor's current pose: one per distinct point, since bones
	// on top of each other (zero-length offset bones) share one.
	[[nodiscard]] size_t GetSplineIKControlLimit(const ActorObject& a_actor, const std::vector<int32_t>& a_path);
}
