#pragma once
#include "Animation/Studio/SharedPose.h"
#include "Animation/Studio/StudioTypes.h"

namespace Animation::Studio
{
	// ---- Properties ----

	// Every animatable property holds its default, its live value, and whether the live value was edited by hand.
	// Sampling overwrites live values; edits that aren't keyed are lost when it does.
	template <class T>
	struct Property
	{
		T defaultValue{};
		T live{};
		bool edited{ false };  // Manually changed since it was last sampled or reset.
	};

	using PropertyPtr = std::variant<std::monostate, Property<float>*, Property<bool>*, Property<ozz::math::Float3>*, Property<ozz::math::Quaternion>*>;
	using PropertyVisitor = std::function<void(uint16_t a_element, PropertyId a_property, PropertyPtr a_ptr)>;

	// a_valueIndex is a Value alternative index.
	[[nodiscard]] bool MatchesType(PropertyPtr a_property, size_t a_valueIndex);
	[[nodiscard]] Value GetLiveValue(PropertyPtr a_property);
	[[nodiscard]] Value GetDefaultValue(PropertyPtr a_property);
	[[nodiscard]] bool IsEdited(PropertyPtr a_property);
	void SetLiveValue(PropertyPtr a_property, const Value& a_value, bool a_edited);  // Ignored on a type mismatch.
	void ResetLiveValue(PropertyPtr a_property);

	struct TransformProperties
	{
		Property<ozz::math::Float3> translation{ ozz::math::Float3::zero(), ozz::math::Float3::zero() };
		Property<ozz::math::Quaternion> rotation{ ozz::math::Quaternion::identity(), ozz::math::Quaternion::identity() };
		Property<ozz::math::Float3> scale{ ozz::math::Float3::one(), ozz::math::Float3::one() };

		[[nodiscard]] PropertyPtr Find(PropertyId a_property);
		void ForEach(uint16_t a_element, const PropertyVisitor& a_visitor);
		[[nodiscard]] LocatorDefaults GetDefaults() const;
		[[nodiscard]] LocatorDefaults GetLive() const;            // Rotation normalized.
		[[nodiscard]] ozz::math::Float4x4 GetLiveMatrix() const;  // Rotation normalized.
	};

	// ---- Objects ----

	class ModifierObject;

	class SceneObject
	{
	public:
		virtual ~SceneObject() = default;
		[[nodiscard]] virtual ObjectType GetType() const = 0;
		[[nodiscard]] virtual PropertyPtr FindProperty(uint16_t a_element, PropertyId a_property) = 0;
		virtual void ForEachProperty(const PropertyVisitor& a_visitor) = 0;
		[[nodiscard]] virtual ModifierObject* AsModifier() { return nullptr; }
		[[nodiscard]] const ModifierObject* AsModifier() const { return const_cast<SceneObject*>(this)->AsModifier(); }

		ObjectId id{ kNoObject };
		std::string name;
	};

	class ActorObject final : public SceneObject
	{
	public:
		static constexpr ObjectType kType = ObjectType::kActor;
		[[nodiscard]] ObjectType GetType() const override { return kType; }
		[[nodiscard]] PropertyPtr FindProperty(uint16_t a_element, PropertyId a_property) override;
		void ForEachProperty(const PropertyVisitor& a_visitor) override;

		// Sets the skeleton and a fresh pose buffer at its rest pose. a_resetBones also resets the bones' values.
		void InitializePose(std::shared_ptr<const OzzSkeleton> a_skeleton, bool a_resetBones);
		// Live bone values into the pose's back buffer.
		void PackPose();
		// Model transforms from the back buffer, for a_from and its descendants (or everything, from kNoParent).
		void RunLocalToModel(int32_t a_from);
		// The override, or else the skeleton's default.
		[[nodiscard]] bool IsBoneMasked(int32_t a_bone) const;
		// Hands the effective bone mask to the StudioGenerator through the pose buffer.
		void PublishBoneMask();

		// Slot requirements. An unbound slot only accepts actors that satisfy these.
		std::string skeletonId;
		RE::SEX gender{ RE::SEX::kNone };

		// Binding. Null for an unbound slot.
		RE::NiPointer<RE::Actor> actor;
		std::shared_ptr<const OzzSkeleton> skeleton;
		RE::TESFormID bindingHint{ 0 };  // Last bound actor.
		ScenePlacement placement;        // Relative to the scene center.

		std::vector<TransformProperties> bones;  // Indexed by skeleton joint. Defaults are the rest pose.
		std::vector<ObjectId> modifiers;          // Pose operators applied after FK, in order.
		std::map<int32_t, bool> maskOverrides;    // Bone -> masked, where it differs from the skeleton's default.

		// Evaluation output.
		std::vector<ozz::math::Float4x4> model;  // Final model-space transforms.
		std::shared_ptr<SharedPose> pose;        // Shared with the actor's StudioGenerator.
		ozz::math::Float4x4 rootWorld{ ozz::math::Float4x4::identity() };
		bool hasRoot{ false };  // Bound and loaded, so rootWorld is valid.
	};

	class LocatorObject final : public SceneObject
	{
	public:
		static constexpr ObjectType kType = ObjectType::kLocator;
		[[nodiscard]] ObjectType GetType() const override { return kType; }
		[[nodiscard]] PropertyPtr FindProperty(uint16_t a_element, PropertyId a_property) override;
		void ForEachProperty(const PropertyVisitor& a_visitor) override;

		TransformProperties transform;  // Relative to parent.
		ParentRef parent;

		// Evaluation output.
		ozz::math::Float4x4 world{ ozz::math::Float4x4::identity() };
	};

	// The object if it has type T, otherwise null.
	template <class T>
	[[nodiscard]] T* ObjectCast(SceneObject* a_object)
	{
		return a_object && a_object->GetType() == T::kType ? static_cast<T*>(a_object) : nullptr;
	}

	template <class T>
	[[nodiscard]] const T* ObjectCast(const SceneObject* a_object)
	{
		return a_object && a_object->GetType() == T::kType ? static_cast<const T*>(a_object) : nullptr;
	}
}
