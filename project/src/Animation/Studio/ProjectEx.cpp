#include "ProjectEx.h"

#include "GameBridge.h"
#include "StudioMath.h"

namespace Animation::Studio
{
	ProjectEx::ProjectEx()
	{
		m_sequences.push_back(Sequence{ .id = m_nextSequenceId++, .name = "Sequence 1" });
		m_activeSequence = m_sequences.front().id;
	}

	ProjectEx::~ProjectEx()
	{
		for (auto& object : m_objects) {
			if (object->GetType() == ObjectType::kActor) {
				DetachStudioGenerator(static_cast<ActorObject&>(*object));
			}
		}
	}

	// ---- Core: Lookup ----

	PropertyPtr ProjectEx::FindProperty(const PropertyRef& a_ref)
	{
		const auto object = FindObject(a_ref.object);
		return object ? object->FindProperty(a_ref.element, a_ref.property) : PropertyPtr{};
	}

	PropertyPtr ProjectEx::FindProperty(const PropertyRef& a_ref) const
	{
		// SceneObject property access is non-const. Callers of this overload only read through the result.
		return const_cast<ProjectEx*>(this)->FindProperty(a_ref);
	}

	ProjectEx::ObjectId ProjectEx::GetModifierActor(const SceneObject& a_object)
	{
		const auto modifier = a_object.AsModifier();
		return modifier ? modifier->actor : kNoObject;
	}

	// ---- Evaluation ----

	void ProjectEx::Update(float a_deltaTime)
	{
		// Every frame, since the game can move an actor too (a cell change resets the graph's root).
		ForEachObject<ActorObject>([&](ActorObject& a_actor) { m_evaluator.HoldAtPlacement(a_actor); });
		if (m_playing) AdvancePlayback(a_deltaTime);
		m_evaluator.MarkRootsDirty();
		Evaluate();
	}

	void ProjectEx::Evaluate()
	{
		m_evaluator.Evaluate();
	}

	// ---- Scene ----

	ozz::math::Float4x4 ProjectEx::ToMatrix(const ScenePlacement& a_placement)
	{
		return Studio::ToMatrix(a_placement);
	}

	ProjectEx::ScenePlacement ProjectEx::ToPlacement(const ozz::math::Float4x4& a_matrix)
	{
		return Studio::ToPlacement(a_matrix);
	}

	ProjectEx::ScenePlacement ProjectEx::GetWorldPlacement(const RE::TESObjectREFR* a_reference)
	{
		return Studio::GetWorldPlacement(a_reference);
	}

	void ProjectEx::HoldActor(RE::Actor* a_actor, const ScenePlacement& a_world)
	{
		Studio::HoldActor(a_actor, a_world);
	}

	const ProjectEx::ScenePlacement& ProjectEx::GetSceneCenter() const
	{
		return m_sceneCenter;
	}

	void ProjectEx::SetSceneCenter(const ScenePlacement& a_center)
	{
		if (m_sceneCenter == a_center) return;
		m_sceneCenter = a_center;
		m_evaluator.MarkSolveNeeded();
		// Now rather than at the next Update(), so the actors move at the same game update as the pose solved for them.
		ForEachObject<ActorObject>([&](ActorObject& a_actor) { m_evaluator.HoldAtPlacement(a_actor); });
	}

	std::optional<ProjectEx::ScenePlacement> ProjectEx::GetActorPlacement(ObjectId a_id) const
	{
		const auto actor = FindObject<ActorObject>(a_id);
		return actor ? std::optional{ actor->placement } : std::nullopt;
	}

	bool ProjectEx::SetActorPlacement(ObjectId a_id, const ScenePlacement& a_placement)
	{
		const auto actor = FindObject<ActorObject>(a_id);
		if (!actor || actor->placement == a_placement) return false;
		TransactionScope transaction(*this, "Move actor");
		Record(ActorPlacementChange{ a_id, actor->placement, a_placement });
		return true;
	}

	// ---- Objects ----

	std::vector<ProjectEx::ObjectId> ProjectEx::GetObjects() const
	{
		std::vector<ObjectId> result;
		result.reserve(m_objects.size());
		for (const auto& object : m_objects) result.push_back(object->id);
		return result;
	}

	std::optional<ProjectEx::ObjectType> ProjectEx::GetObjectType(ObjectId a_id) const
	{
		const auto object = FindObject(a_id);
		return object ? std::optional{ object->GetType() } : std::nullopt;
	}

	std::string_view ProjectEx::GetObjectName(ObjectId a_id) const
	{
		const auto object = FindObject(a_id);
		return object ? std::string_view{ object->name } : std::string_view{};
	}

	bool ProjectEx::RenameObject(ObjectId a_id, std::string a_name)
	{
		const auto object = FindObject(a_id);
		if (!object || object->name == a_name) return false;
		TransactionScope transaction(*this, "Rename");
		Record(ObjectRenameChange{ a_id, object->name, std::move(a_name) });
		return true;
	}

	bool ProjectEx::RemoveObject(ObjectId a_id)
	{
		if (!FindObject(a_id)) return false;

		// Gather everything that can't exist without the removed object.
		std::vector<ObjectId> removing{ a_id };
		const auto isRemoving = [&](ObjectId a_candidate) { return std::ranges::contains(removing, a_candidate); };
		const auto add = [&](ObjectId a_candidate) {
			if (a_candidate != kNoObject && !isRemoving(a_candidate) && FindObject(a_candidate)) removing.push_back(a_candidate);
		};
		for (size_t i = 0; i < removing.size(); ++i) {
			const auto object = FindObject(removing[i]);
			if (const auto actor = ObjectCast<ActorObject>(object)) {
				for (const auto modifier : actor->modifiers) add(modifier);
			} else if (const auto modifier = object->AsModifier()) {
				for (const auto locator : modifier->GetLocators()) add(locator);
			} else if (object->GetType() == ObjectType::kLocator) {
				add(GetLocatorOwner(object->id));
			}
		}

		TransactionScope transaction(*this, "Remove");
		Pause();

		// Surviving locators hanging from a removed object move to world space, staying where they are.
		ForEachObject<LocatorObject>([&](LocatorObject& a_locator) {
			if (!isRemoving(a_locator.id) && isRemoving(a_locator.parent.object)) RecordReparent(a_locator, {}, true);
		});

		// Modifiers go before their locators and actor, so undo restores those first. Each removal applies as it's
		// recorded, so objects taken out by an earlier pass are no longer found.
		const auto removeAll = [&](auto a_filter) {
			for (const auto id : removing) {
				const auto object = FindObject(id);
				if (object && a_filter(*object)) Record(ObjectLifetimeChange{ ObjectSnapshot{ .id = id }, false });
			}
		};
		const auto isLocator = [](const SceneObject& a_object) { return a_object.GetType() == ObjectType::kLocator; };
		removeAll([](const SceneObject& a_object) { return a_object.AsModifier() != nullptr; });
		removeAll(isLocator);
		removeAll([&](const SceneObject& a_object) { return !a_object.AsModifier() && !isLocator(a_object); });
		return true;
	}

	std::expected<ProjectEx::ObjectId, std::string> ProjectEx::AddActor(RE::Actor* a_actor)
	{
		if (!a_actor) return std::unexpected("No actor was supplied.");
		if (FindActorObject(a_actor)) return std::unexpected("That actor is already in the project.");
		auto skeleton = GetActorSkeleton(a_actor);
		if (!skeleton) return std::unexpected("The actor does not have a usable animation skeleton.");

		auto actor = std::make_unique<ActorObject>();
		actor->id = m_nextObjectId++;
		actor->name = GetActorDisplayName(a_actor);
		actor->skeletonId = GetActorSkeletonId(a_actor);
		if (const auto npc = a_actor->GetNPC()) actor->gender = npc->GetSex();
		actor->actor.reset(a_actor);
		actor->bindingHint = a_actor->GetFormID();
		actor->InitializePose(std::move(skeleton), true);

		// The project's first object ever: nothing has been placed in scene space yet, so the center can move onto it.
		const auto world = GetWorldPlacement(a_actor);
		if (actor->id == 1) m_sceneCenter = world;
		if (const auto sceneInverse = TryInvert(ToMatrix(m_sceneCenter))) actor->placement = ToPlacement(*sceneInverse * ToMatrix(world));

		const ObjectId id = actor->id;
		TransactionScope transaction(*this, "Add actor");
		Record(ObjectLifetimeChange{ ObjectSnapshot{ .id = id, .object = std::move(actor), .index = m_objects.size() }, true });
		return id;
	}

	std::expected<void, std::string> ProjectEx::BindActor(ObjectId a_id, RE::Actor* a_actor)
	{
		const auto slot = FindObject<ActorObject>(a_id);
		if (!slot) return std::unexpected("Actor slot was not found.");
		if (!a_actor) return std::unexpected("No actor was supplied.");
		if (slot->actor.get() == a_actor) return {};
		if (FindActorObject(a_actor)) return std::unexpected("That actor is already in the project.");

		if (const auto compatible = CheckActorCompatibility(slot->skeletonId, slot->gender, a_actor); !compatible) return compatible;
		const std::string skeletonId = GetActorSkeletonId(a_actor);
		auto skeleton = GetActorSkeleton(a_actor);
		if (!skeleton) return std::unexpected("The actor does not have a usable animation skeleton.");
		if (static_cast<size_t>(skeleton->data->num_joints()) != slot->bones.size()) {
			return std::unexpected("The actor skeleton has a different joint layout.");
		}

		TransactionScope transaction(*this, "Bind actor");
		Record(ActorBindingChange{ a_id, GetBinding(*slot), ActorBinding{ RE::NiPointer<RE::Actor>(a_actor), std::move(skeleton), skeletonId } });
		return {};
	}

	void ProjectEx::UnbindActor(ObjectId a_id)
	{
		const auto slot = FindObject<ActorObject>(a_id);
		if (!slot || !slot->actor) return;
		auto binding = GetBinding(*slot);
		binding.actor.reset();
		TransactionScope transaction(*this, "Unbind actor");
		Record(ActorBindingChange{ a_id, GetBinding(*slot), std::move(binding) });
	}

	RE::Actor* ProjectEx::GetBoundActor(ObjectId a_id) const
	{
		const auto actor = FindObject<ActorObject>(a_id);
		return actor ? actor->actor.get() : nullptr;
	}

	RE::TESFormID ProjectEx::GetBindingHint(ObjectId a_id) const
	{
		const auto actor = FindObject<ActorObject>(a_id);
		return actor ? actor->bindingHint : 0;
	}

	std::expected<void, std::string> ProjectEx::CheckActorCompatibility(std::string_view a_skeletonId, RE::SEX a_gender, RE::Actor* a_actor)
	{
		return Studio::CheckActorCompatibility(a_skeletonId, a_gender, a_actor);
	}

	const ozz::animation::Skeleton* ProjectEx::GetSkeleton(ObjectId a_id) const
	{
		const auto actor = FindObject<ActorObject>(a_id);
		return actor && actor->skeleton ? actor->skeleton->data.get() : nullptr;
	}

	std::string_view ProjectEx::GetSkeletonId(ObjectId a_actor) const
	{
		const auto actor = FindObject<ActorObject>(a_actor);
		return actor ? std::string_view{ actor->skeletonId } : std::string_view{};
	}

	std::span<const ProjectEx::ObjectId> ProjectEx::GetModifiers(ObjectId a_actor) const
	{
		const auto actor = FindObject<ActorObject>(a_actor);
		return actor ? std::span<const ObjectId>{ actor->modifiers } : std::span<const ObjectId>{};
	}

	ProjectEx::ObjectId ProjectEx::GetModifierActor(ObjectId a_modifier) const
	{
		const auto object = FindObject(a_modifier);
		return object ? GetModifierActor(*object) : kNoObject;
	}

	std::expected<ProjectEx::ObjectId, std::string> ProjectEx::AddLocator(ParentRef a_parent, std::optional<ozz::math::Float4x4> a_world)
	{
		if (!IsValidParent(a_parent)) return std::unexpected("The locator parent is not valid.");
		auto locator = std::make_unique<LocatorObject>();
		locator->id = m_nextObjectId++;
		locator->name = std::format("Locator {}", locator->id);
		locator->parent = a_parent;
		if (a_world) {
			Evaluate();
			const auto parentWorld = m_evaluator.ResolveParentWorld(a_parent);
			const auto parentInverse = parentWorld ? TryInvert(*parentWorld) : std::nullopt;
			ozz::math::Float3 translation;
			ozz::math::Quaternion rotation;
			ozz::math::Float3 scale;
			if (parentInverse && ozz::math::ToAffine(*parentInverse * *a_world, &translation, &rotation, &scale)) {
				// The initial placement doubles as the default, so switching sequences doesn't send it to the parent's origin.
				locator->transform.translation = { translation, translation };
				locator->transform.rotation = { Normalized(rotation), Normalized(rotation) };
			}
		}

		const ObjectId id = locator->id;
		TransactionScope transaction(*this, "Add locator");
		Record(ObjectLifetimeChange{ ObjectSnapshot{ .id = id, .object = std::move(locator), .index = m_objects.size() }, true });
		return id;
	}

	std::expected<void, std::string> ProjectEx::SetLocatorParent(ObjectId a_id, ParentRef a_parent, bool a_keepWorldTransform)
	{
		const auto locator = FindObject<LocatorObject>(a_id);
		if (!locator) return std::unexpected("The locator was not found.");
		if (!IsValidParent(a_parent)) return std::unexpected("The parent is not valid.");
		if (locator->parent == a_parent) return {};
		if (WouldCreateLocatorLoop(a_id, a_parent)) return std::unexpected("A locator cannot be parented to itself or its own children.");

		const auto previous = locator->parent;
		locator->parent = a_parent;
		const bool selfDependent = m_evaluator.HasSelfDependentModifier();
		locator->parent = previous;
		if (selfDependent) return std::unexpected("This would make an IK chain or spline depend on its own result.");

		TransactionScope transaction(*this, "Reparent locator");
		Pause();
		RecordReparent(*locator, a_parent, a_keepWorldTransform);
		return {};
	}

	std::optional<ProjectEx::ParentRef> ProjectEx::GetLocatorParent(ObjectId a_id) const
	{
		const auto locator = FindObject<LocatorObject>(a_id);
		return locator ? std::optional{ locator->parent } : std::nullopt;
	}

	bool ProjectEx::SetLocatorDefaultsToCurrent(ObjectId a_id)
	{
		const auto locator = FindObject<LocatorObject>(a_id);
		if (!locator) return false;
		TransactionScope transaction(*this, "Set locator default");
		Pause();
		const auto& transform = locator->transform;
		LocatorDefaults after{ transform.translation.live, Normalized(transform.rotation.live), transform.scale.live };
		const auto before = locator->transform.GetDefaults();
		if (before == after) return false;
		Record(LocatorDefaultsChange{ a_id, before, after });
		return true;
	}
}
