#pragma once
#include "Animation/Studio/SceneObjects.h"

#include <expected>

namespace Animation::Studio
{
	// What a modifier can reach while it solves.
	class ModifierContext
	{
	public:
		virtual ~ModifierContext() = default;
		[[nodiscard]] virtual ActorObject* FindActor(ObjectId a_id) = 0;
		// A locator's world transform from its current live values. Nullopt when it doesn't resolve, such as when it
		// hangs from an unbound or unloaded actor.
		[[nodiscard]] virtual std::optional<ozz::math::Float4x4> ResolveLocatorWorld(ObjectId a_locator) = 0;
	};

	// An object that changes its actor's pose after FK: IK chains, hands and spline IK. Each actor lists its modifiers
	// in application order. Corrections live only in the pose buffer and the model matrices, never in live values, so
	// they don't accumulate.
	//
	// A new modifier type derives from this, adds an ObjectType, and gets a ModifierSettings alternative if it has
	// settings that aren't keyable. A model-space modifier (one that reads the posed skeleton or locators) implements
	// GetWriteRoots and Solve, and the eval plan orders it among the others. A local-space, additive one implements
	// ApplyToPose instead.
	class ModifierObject : public SceneObject
	{
	public:
		using SceneObject::AsModifier;
		[[nodiscard]] ModifierObject* AsModifier() override { return this; }

		// The locators this modifier owns (an IK chain's effector and pole, a spline's controls). They're removed with
		// it, and removing one of them removes the modifier.
		[[nodiscard]] virtual std::vector<ObjectId> GetLocators() const { return {}; }
		// Called per object per frame by the outliner, so overrides should avoid building GetLocators' vector.
		[[nodiscard]] virtual bool UsesLocator(ObjectId a_locator) const { return std::ranges::contains(GetLocators(), a_locator); }
		// Points the modifier at a_actor and maps each of its locators, as applying a rig does.
		virtual void Remap(ObjectId a_actor, [[maybe_unused]] const std::function<ObjectId(ObjectId)>& a_locator) { actor = a_actor; }

		// Pose-time: adjusts the actor's local pose (its back buffer) before LocalToModel, so IK and locators see the
		// result without an eval plan entry.
		virtual void ApplyToPose(ActorObject&) const {}
		// Model-space: the bones this modifier writes, each along with all its descendants, or none if it doesn't
		// solve in model space. Such modifiers join the eval plan, reading each write root's parent.
		[[nodiscard]] virtual std::vector<int32_t> GetWriteRoots(const ozz::animation::Skeleton&) const { return {}; }
		// Bones of its own actor it reads besides its write roots' parents and its locators, such as a driver's inputs.
		virtual void AppendBoneReads(const ozz::animation::Skeleton&, [[maybe_unused]] std::vector<int32_t>& a_bones) const {}
		// Runs in the eval plan, after LocalToModel. Updates the pose's back buffer and the model matrices.
		virtual void Solve(ModifierContext&) {}
		// Bones this modifier fully owns while it's enabled. Edits to them are rejected. Additive modifiers own none.
		[[nodiscard]] virtual bool DrivesBone(ozz::span<const int16_t>, int32_t) const { return false; }

		[[nodiscard]] virtual ModifierSettings GetSettings() const = 0;
		// Settings for another modifier type are ignored.
		virtual void SetSettings(const ModifierSettings& a_settings) = 0;

		ObjectId actor{ kNoObject };
	};

	// What creating a modifier makes: its locators, in insertion order, then the modifier.
	struct ModifierBuild
	{
		std::vector<std::unique_ptr<LocatorObject>> locators;
		std::unique_ptr<ModifierObject> modifier;
	};

	// The object as a modifier, or null.
	[[nodiscard]] inline ModifierObject* AsModifier(SceneObject* a_object)
	{
		return a_object ? a_object->AsModifier() : nullptr;
	}

	[[nodiscard]] inline const ModifierObject* AsModifier(const SceneObject* a_object)
	{
		return a_object ? a_object->AsModifier() : nullptr;
	}
}
