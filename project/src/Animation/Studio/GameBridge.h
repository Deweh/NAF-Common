#pragma once
#include "Animation/Studio/StudioTypes.h"

#include <expected>

// Everything the Studio's model needs from the game and from NAF's graphs: actors, their skeletons, holding them in
// place, and handing them the Studio's pose. The rest of the model doesn't touch RE:: or GraphManager directly.
namespace Animation::Studio
{
	class ActorObject;

	// How long a detached actor takes to blend back to its own animation.
	inline constexpr float kDetachTransitionTime = 0.2f;

	// The actor's graph skeleton. Creates the actor's graph if it has none yet, which also registers its skeleton.
	[[nodiscard]] std::shared_ptr<const OzzSkeleton> GetActorSkeleton(RE::Actor* a_actor);
	[[nodiscard]] std::string GetActorSkeletonId(RE::Actor* a_actor);
	[[nodiscard]] std::string GetActorDisplayName(RE::Actor* a_actor);
	// Whether an actor meets a slot's requirements: the skeleton id (case-insensitive) and, unless kNone, the gender.
	[[nodiscard]] std::expected<void, std::string> CheckActorCompatibility(std::string_view a_skeletonId, RE::SEX a_gender, RE::Actor* a_actor);

	[[nodiscard]] ScenePlacement GetWorldPlacement(const RE::TESObjectREFR* a_reference);
	// Holds an actor at a world placement through its animation graph, without creating one.
	void HoldActor(RE::Actor* a_actor, const ScenePlacement& a_world);

	// Publishes the actor's bone mask and attaches a StudioGenerator that plays its pose buffer. No-op when unbound.
	void AttachStudioGenerator(ActorObject& a_actor);
	void DetachStudioGenerator(ActorObject& a_actor, float a_transitionTime = kDetachTransitionTime);
}
