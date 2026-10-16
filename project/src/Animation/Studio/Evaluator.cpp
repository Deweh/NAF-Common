#include "Evaluator.h"
#include "Util/Game.h"

#include "GameBridge.h"
#include "Skeleton.h"
#include "StudioMath.h"

namespace Animation::Studio
{
	namespace
	{
		// Passes over a group of modifiers that depend on each other in a cycle.
		constexpr uint8_t kCycleIterations = 4;
	}

	void Evaluator::Evaluate()
	{
		// Actors move in the world independently of the project, which moves world-space targets relative to them.
		// Their roots only change with the game's update or with a change to the project, so the getters that call
		// this many times per frame skip it while nothing has changed.
		if (m_rootsDirty || m_solveNeeded) {
			m_rootsDirty = false;
			m_objects.ForEach<ActorObject>([&](ActorObject& a_actor) {
				if (RefreshRootWorld(a_actor)) m_solveNeeded = true;
			});
		}
		if (m_planDirty) {
			BuildPlan();
			m_planDirty = false;
			m_solveNeeded = true;
		}
		if (!m_solveNeeded) return;
		m_solveNeeded = false;
		Solve(true);
	}

	void Evaluator::Solve(bool a_publish)
	{
		m_objects.ForEach<ActorObject>([&](ActorObject& a_actor) {
			if (!a_actor.skeleton || !a_actor.pose) return;
			a_actor.PackPose();
			for (const auto id : a_actor.modifiers) {
				if (const auto modifier = m_objects.FindModifier(id)) modifier->ApplyToPose(a_actor);
			}
			a_actor.RunLocalToModel(ozz::animation::Skeleton::kNoParent);
		});
		for (const auto& step : m_plan) {
			for (uint8_t iteration = 0; iteration < step.iterations; ++iteration) {
				for (const auto id : step.modifiers) {
					if (const auto modifier = m_objects.FindModifier(id)) modifier->Solve(*this);
				}
			}
		}
		// Publish only once every modifier has run, so the game never reads a partly solved pose.
		if (a_publish) {
			m_objects.ForEach<ActorObject>([](ActorObject& a_actor) {
				if (a_actor.skeleton && a_actor.pose) a_actor.pose->Swap();
			});
		}
		m_objects.ForEach<LocatorObject>([&](LocatorObject& a_locator) { ResolveLocatorWorld(a_locator); });
	}

	ScenePlacement Evaluator::GetHeldPlacement(const ActorObject& a_actor) const
	{
		return ToPlacement(ToMatrix(m_sceneCenter) * ToMatrix(a_actor.placement));
	}

	void Evaluator::HoldAtPlacement(const ActorObject& a_actor) const
	{
		if (a_actor.actor) HoldActor(a_actor.actor.get(), GetHeldPlacement(a_actor));
	}

	bool Evaluator::RefreshRootWorld(ActorObject& a_actor) const
	{
		const auto root = a_actor.actor ? Util::Game::Get3D(a_actor.actor.get()) : nullptr;
		if (!root) {
			const bool changed = a_actor.hasRoot;
			a_actor.hasRoot = false;
			return changed;
		}
		// The pose solved now is applied at the game's next update, which is also when the actor's 3D reaches the
		// placement it's held at. Solving against the 3D as it stands would put world-space targets (like IK
		// effectors) off by however far the actor just moved, so the root is where the actor is held. Only the
		// scale comes from the 3D.
		auto world = ToMatrix(GetHeldPlacement(a_actor));
		const auto scale = ozz::math::simd_float4::Load1(root->world.scale);
		for (size_t i = 0; i < 3; ++i) world.cols[i] = world.cols[i] * scale;
		const bool changed = !a_actor.hasRoot || std::memcmp(&world, &a_actor.rootWorld, sizeof(world)) != 0;
		a_actor.rootWorld = world;
		a_actor.hasRoot = true;
		return changed;
	}

	std::optional<ozz::math::Float4x4> Evaluator::ResolveParentWorld(const ParentRef& a_parent)
	{
		if (a_parent.object == kNoObject) return ToMatrix(m_sceneCenter);
		if (const auto actor = m_objects.Find<ActorObject>(a_parent.object)) {
			if (!actor->hasRoot) return std::nullopt;
			if (a_parent.bone < 0) return actor->rootWorld;
			if (a_parent.bone >= static_cast<int32_t>(actor->model.size())) return std::nullopt;
			return actor->rootWorld * actor->model[a_parent.bone];
		}
		if (const auto locator = m_objects.Find<LocatorObject>(a_parent.object)) return ResolveLocatorWorld(*locator);
		return std::nullopt;
	}

	std::optional<ozz::math::Float4x4> Evaluator::ResolveLocatorWorld(LocatorObject& a_locator)
	{
		// Parent loops are rejected when parenting, so this recursion terminates.
		const auto parentWorld = ResolveParentWorld(a_locator.parent);
		if (!parentWorld) return std::nullopt;
		const auto& transform = a_locator.transform;
		const auto local = ozz::math::Float4x4::FromAffine(transform.translation.live, Normalized(transform.rotation.live), transform.scale.live);
		a_locator.world = *parentWorld * local;
		return a_locator.world;
	}

	ActorObject* Evaluator::FindActor(ObjectId a_id)
	{
		return m_objects.Find<ActorObject>(a_id);
	}

	std::optional<ozz::math::Float4x4> Evaluator::ResolveLocatorWorld(ObjectId a_locator)
	{
		const auto locator = m_objects.Find<LocatorObject>(a_locator);
		return locator ? ResolveLocatorWorld(*locator) : std::nullopt;
	}

	bool Evaluator::DescribeModifier(ObjectId a_id, ModifierAccess& a_out) const
	{
		const auto modifier = m_objects.FindModifier(a_id);
		if (!modifier) return false;
		const auto actor = m_objects.Find<ActorObject>(modifier->actor);
		if (!actor || !actor->skeleton) return false;
		const auto& skeleton = *actor->skeleton->data;

		// Pose-time modifiers (hands) work on the local pose before the solve, so they aren't in the plan.
		const int32_t jointCount = skeleton.num_joints();
		auto writeRoots = modifier->GetWriteRoots(skeleton);
		if (writeRoots.empty() || std::ranges::any_of(writeRoots, [&](int32_t a_bone) { return a_bone < 0 || a_bone >= jointCount; })) return false;

		a_out.actor = actor->id;
		a_out.skeleton = &skeleton;
		a_out.reads.clear();
		for (const int32_t root : writeRoots) {
			if (const int16_t parent = skeleton.joint_parents()[root]; parent != ozz::animation::Skeleton::kNoParent) a_out.reads.emplace_back(actor->id, parent);
		}
		std::vector<int32_t> boneReads;
		modifier->AppendBoneReads(skeleton, boneReads);
		for (const int32_t bone : boneReads) {
			if (bone >= 0 && bone < jointCount) a_out.reads.emplace_back(actor->id, bone);
		}
		a_out.writeRoots = std::move(writeRoots);
		for (const auto locator : modifier->GetLocators()) AppendLocatorReads(locator, a_out.reads);
		return true;
	}

	void Evaluator::AppendLocatorReads(ObjectId a_locator, std::vector<std::pair<ObjectId, int32_t>>& a_reads) const
	{
		// Follows the locator's parent chain down to the bone it ultimately hangs from, if any.
		auto locator = m_objects.Find<LocatorObject>(a_locator);
		for (size_t guard = 0; locator && guard <= m_objects.size(); ++guard) {
			const auto& parent = locator->parent;
			if (parent.object == kNoObject) return;
			if (m_objects.Find<ActorObject>(parent.object)) {
				if (parent.bone >= 0) a_reads.emplace_back(parent.object, parent.bone);
				return;
			}
			locator = m_objects.Find<LocatorObject>(parent.object);
		}
	}

	bool Evaluator::Affects(const ModifierAccess& a_writer, const ModifierAccess& a_reader)
	{
		const auto parents = a_writer.skeleton->joint_parents();
		return std::ranges::any_of(a_reader.reads, [&](const auto& a_read) {
			return a_read.first == a_writer.actor && std::ranges::any_of(a_writer.writeRoots, [&](int32_t a_root) { return IsDescendantOrSelf(parents, a_read.second, a_root); });
		});
	}

	bool Evaluator::HasSelfDependentModifier() const
	{
		ModifierAccess access;
		for (const auto& object : m_objects) {
			if (DescribeModifier(object->id, access) && Affects(access, access)) return true;
		}
		return false;
	}

	void Evaluator::BuildPlan()
	{
		m_plan.clear();
		std::vector<ObjectId> ids;
		std::vector<ModifierAccess> access;
		m_objects.ForEach<ActorObject>([&](ActorObject& a_actor) {
			for (const auto id : a_actor.modifiers) {
				ModifierAccess info;
				if (!DescribeModifier(id, info)) continue;
				ids.push_back(id);
				access.push_back(std::move(info));
			}
		});

		const size_t count = ids.size();
		std::vector<std::vector<size_t>> edges(count);
		for (size_t writer = 0; writer < count; ++writer) {
			for (size_t reader = 0; reader < count; ++reader) {
				if (writer != reader && Affects(access[writer], access[reader])) edges[writer].push_back(reader);
			}
		}

		// Tarjan's algorithm. Components are emitted in reverse topological order.
		std::vector<int32_t> index(count, -1);
		std::vector<int32_t> lowLink(count, 0);
		std::vector<bool> onStack(count, false);
		std::vector<size_t> stack;
		std::vector<std::vector<size_t>> components;
		int32_t nextIndex = 0;
		const auto connect = [&](this auto&& a_self, size_t a_node) -> void {
			index[a_node] = lowLink[a_node] = nextIndex++;
			stack.push_back(a_node);
			onStack[a_node] = true;
			for (const size_t next : edges[a_node]) {
				if (index[next] < 0) {
					a_self(next);
					lowLink[a_node] = (std::min)(lowLink[a_node], lowLink[next]);
				} else if (onStack[next]) {
					lowLink[a_node] = (std::min)(lowLink[a_node], index[next]);
				}
			}
			if (lowLink[a_node] != index[a_node]) return;
			auto& component = components.emplace_back();
			size_t member;
			do {
				member = stack.back();
				stack.pop_back();
				onStack[member] = false;
				component.push_back(member);
			} while (member != a_node);
		};
		for (size_t node = 0; node < count; ++node) {
			if (index[node] < 0) connect(node);
		}

		for (auto& component : components | std::views::reverse) {
			std::ranges::sort(component);  // Keep actor/modifier order within a cycle group.
			EvalStep step;
			for (const size_t member : component) step.modifiers.push_back(ids[member]);
			step.iterations = component.size() > 1 ? kCycleIterations : 1;
			m_plan.push_back(std::move(step));
		}
	}

	bool Evaluator::IsAffectedByEdits(ElementRef a_target, std::span<const ElementRef> a_edited) const
	{
		// The evaluated transform the edit reads: a bone's own (its parent's, plus the additive offset on itself), or
		// the bone or actor root a locator ultimately hangs from. An actor's placement reads nothing evaluated.
		ObjectId actorId = kNoObject;
		int32_t bone = -1;
		if (m_objects.Find<ActorObject>(a_target.object)) {
			if (a_target.element == kSelf) return false;
			actorId = a_target.object;
			bone = a_target.element;
		} else if (const auto locator = m_objects.Find<LocatorObject>(a_target.object)) {
			// Locator chains are resolved from live values, which are always current.
			auto parent = locator->parent;
			for (size_t guard = 0; guard <= m_objects.size(); ++guard) {
				const auto parentLocator = m_objects.Find<LocatorObject>(parent.object);
				if (!parentLocator) break;
				parent = parentLocator->parent;
			}
			if (!m_objects.Find<ActorObject>(parent.object)) return false;
			actorId = parent.object;
			bone = parent.bone;
		} else {
			return false;
		}

		// The actor's root world follows its placement.
		if (std::ranges::contains(a_edited, ElementRef{ actorId, kSelf })) return true;
		if (bone < 0) return false;

		const auto actor = m_objects.Find<ActorObject>(actorId);
		if (!actor->skeleton) return true;
		const auto parents = actor->skeleton->data->joint_parents();
		// Forward kinematics: an edited ancestor.
		for (const auto& edited : a_edited) {
			if (edited.object == actorId && edited.element != kSelf && IsDescendantOrSelf(parents, bone, edited.element)) return true;
		}
		// A modifier writing this bone, whose result any edit may have changed through its inputs.
		ModifierAccess access;
		for (const auto id : actor->modifiers) {
			if (!DescribeModifier(id, access)) continue;
			if (std::ranges::any_of(access.writeRoots, [&](int32_t a_root) { return IsDescendantOrSelf(parents, bone, a_root); })) return true;
		}
		return false;
	}
}
