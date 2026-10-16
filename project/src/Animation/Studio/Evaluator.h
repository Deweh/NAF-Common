#pragma once
#include "Animation/Studio/Modifiers/Modifier.h"
#include "Animation/Studio/ObjectStore.h"

namespace Animation::Studio
{
	// Which bones a modifier reads and writes. B depends on A when B reads a bone that A writes.
	struct ModifierAccess
	{
		ObjectId actor{ kNoObject };
		const ozz::animation::Skeleton* skeleton{ nullptr };
		std::vector<int32_t> writeRoots;                  // The modifier writes these bones and all their descendants.
		std::vector<std::pair<ObjectId, int32_t>> reads;  // Actor, bone.
	};

	// Turns the scene's live values into final poses: actor roots, FK, modifiers in dependency order, and locator
	// worlds. Works on bones, not whole actors: modifiers form a dependency graph by the bones they read and write,
	// and cycles are solved iteratively. Cheap to call when nothing changed.
	class Evaluator final : private ModifierContext
	{
	public:
		Evaluator(ObjectStore& a_objects, const ScenePlacement& a_sceneCenter) :
			m_objects(a_objects),
			m_sceneCenter(a_sceneCenter)
		{}

		// Something moved a root's inputs, or a live value changed.
		void MarkSolveNeeded() { m_solveNeeded = true; }
		// The game may have moved, unloaded or rescaled an actor's 3D. Once per Update.
		void MarkRootsDirty() { m_rootsDirty = true; }
		// What a modifier reads or writes may have changed: added or removed objects, reparented locators, settings.
		void MarkPlanDirty() { m_planDirty = true; }

		// Refreshes the roots, rebuilds the plan if needed, then solves and publishes if anything changed.
		void Evaluate();
		// Poses every actor from its live values, runs the modifiers and resolves locators. a_publish swaps the pose
		// buffers, handing the result to the game; baking leaves it unpublished.
		void Solve(bool a_publish);

		// World transforms as of the last solve. Locator chains resolve from live values, which are always current.
		std::optional<ozz::math::Float4x4> ResolveParentWorld(const ParentRef& a_parent);
		std::optional<ozz::math::Float4x4> ResolveLocatorWorld(LocatorObject& a_locator);

		// Where an actor is held, in world space: the scene center times its placement.
		[[nodiscard]] ScenePlacement GetHeldPlacement(const ActorObject& a_actor) const;
		void HoldAtPlacement(const ActorObject& a_actor) const;

		// False for modifiers that don't solve in model space (hands).
		bool DescribeModifier(ObjectId a_id, ModifierAccess& a_out) const;
		// Whether some modifier reads its own output.
		[[nodiscard]] bool HasSelfDependentModifier() const;
		// Whether the evaluated transforms an edit of a_target is computed from may be stale after a_edited were
		// recorded. Conservative: any edit can change a modifier's result.
		[[nodiscard]] bool IsAffectedByEdits(ElementRef a_target, std::span<const ElementRef> a_edited) const;

	private:
		// A single modifier runs once. Modifiers in a dependency cycle are grouped and solved iteratively.
		struct EvalStep
		{
			std::vector<ObjectId> modifiers;
			uint8_t iterations{ 1 };
		};

		// ModifierContext
		ActorObject* FindActor(ObjectId a_id) override;
		std::optional<ozz::math::Float4x4> ResolveLocatorWorld(ObjectId a_locator) override;

		bool RefreshRootWorld(ActorObject& a_actor) const;
		void AppendLocatorReads(ObjectId a_locator, std::vector<std::pair<ObjectId, int32_t>>& a_reads) const;
		static bool Affects(const ModifierAccess& a_writer, const ModifierAccess& a_reader);
		void BuildPlan();

		ObjectStore& m_objects;
		const ScenePlacement& m_sceneCenter;
		std::vector<EvalStep> m_plan;
		bool m_rootsDirty{ true };
		bool m_solveNeeded{ false };
		bool m_planDirty{ true };
	};
}
