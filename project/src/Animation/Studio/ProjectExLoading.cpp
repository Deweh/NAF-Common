#include "ProjectEx.h"

#include "GameBridge.h"
#include "StudioMath.h"

namespace Animation::Studio
{
	// ---- Core: Loading ----

	std::expected<void, std::string> ProjectEx::LoadActorBinding(ActorObject& a_slot, RE::Actor* a_actor)
	{
		if (const auto compatible = CheckActorCompatibility(a_slot.skeletonId, a_slot.gender, a_actor); !compatible) return compatible;
		// Creates the actor's graph if it has none yet, which also registers its skeleton.
		auto skeleton = GetActorSkeleton(a_actor);
		if (!skeleton) return std::unexpected("The actor does not have a usable animation skeleton.");
		a_slot.actor.reset(a_actor);
		a_slot.bindingHint = a_actor->GetFormID();
		a_slot.InitializePose(std::move(skeleton), true);
		return {};
	}

	void ProjectEx::FinishLoad(std::span<const DiscardedValue> a_liveValues)
	{
		for (auto& sequence : m_sequences) sequence.RegenerateTangents();
		if (const auto sequence = GetActiveSequence()) m_frame = std::clamp(m_frame, 0, sequence->frameCount);
		SyncPlaybackTime();
		ResetAllToDefaults();
		ResampleAll();
		for (const auto& value : a_liveValues) {
			const auto property = FindProperty(value.property);
			if (MatchesType(property, value.value.index())) SetLiveValue(property, value.value, value.edited);
		}

		ForEachObject<ActorObject>([](ActorObject& a_actor) { AttachStudioGenerator(a_actor); });
		m_history.Clear();
		m_evaluator.MarkPlanDirty();
		m_evaluator.MarkSolveNeeded();
		MarkSaved();
	}

	std::pair<ProjectEx::ParentRef, LocatorDefaults> ProjectEx::GetRigLocator(const ActorObject& a_actor, const LocatorObject& a_locator, const std::set<ObjectId>& a_rigLocators)
	{
		const auto& transform = a_locator.transform;
		LocatorDefaults result{ transform.translation.live, Normalized(transform.rotation.live), transform.scale.live };
		auto parent = a_locator.parent;
		if (parent.object == a_actor.id || a_rigLocators.contains(parent.object)) return { parent, result };

		// Scene space, or a parent the rig doesn't carry (which is left where it is, in scene space).
		auto sceneLocal = ozz::math::Float4x4::FromAffine(result.translation, result.rotation, result.scale);
		if (parent.object != kNoObject) {
			Evaluate();
			const auto parentWorld = m_evaluator.ResolveParentWorld(parent);
			const auto sceneInverse = TryInvert(ToMatrix(m_sceneCenter));
			if (parentWorld && sceneInverse) sceneLocal = *sceneInverse * *parentWorld * sceneLocal;
		}
		const auto placementInverse = TryInvert(ToMatrix(a_actor.placement));
		if (placementInverse && ozz::math::ToAffine(*placementInverse * sceneLocal, &result.translation, &result.rotation, &result.scale)) {
			result.rotation = Normalized(result.rotation);
		}
		return { ParentRef{}, result };
	}

	ProjectEx::RigResult ProjectEx::ApplyRig(ObjectId a_actor, RigContents a_rig, const RigOptions& a_options)
	{
		RigResult result;
		const auto actor = FindObject<ActorObject>(a_actor);
		if (!actor || !actor->skeleton) return result;

		// The modifiers being applied, and the locators they use. Other locators are left out.
		std::map<ObjectId, const LocatorObject*> rigLocators;
		std::set<ObjectId> used;
		for (const auto& object : a_rig.objects) {
			if (object->GetType() == ObjectType::kLocator) rigLocators.emplace(object->id, static_cast<const LocatorObject*>(object.get()));
			if (!a_options.Applies(object->GetType())) continue;
			if (const auto modifier = object->AsModifier()) {
				const auto locators = modifier->GetLocators();
				used.insert(locators.begin(), locators.end());
			}
		}

		// A locator hanging from one that's left out takes that one's parent, with both transforms combined.
		std::map<ObjectId, std::pair<ParentRef, ozz::math::Float4x4>> placements;
		for (const auto id : used) {
			const auto& locator = *rigLocators.at(id);
			auto parent = locator.parent;
			auto local = locator.transform.GetLiveMatrix();
			while (parent.object != kNoObject && parent.object != a_rig.actor && !used.contains(parent.object)) {
				const auto& skipped = *rigLocators.at(parent.object);
				local = skipped.transform.GetLiveMatrix() * local;
				parent = skipped.parent;
			}
			placements.emplace(id, std::pair{ parent, local });
		}
		rigLocators.clear();
		std::erase_if(a_rig.objects, [&](const auto& a_object) {
			return a_object->GetType() == ObjectType::kLocator ? !used.contains(a_object->id) : !a_options.Applies(a_object->GetType());
		});

		// New ids. The rig's actor becomes the target actor.
		std::map<ObjectId, ObjectId> ids{ { kNoObject, kNoObject }, { a_rig.actor, a_actor } };
		for (const auto& object : a_rig.objects) ids.emplace(object->id, m_nextObjectId++);
		const auto placement = ToMatrix(actor->placement);
		for (auto& object : a_rig.objects) {
			if (const auto locator = ObjectCast<LocatorObject>(object.get())) {
				auto [parent, local] = placements.at(locator->id);
				// The rig keeps unparented locators relative to the actor.
				if (parent.object == kNoObject) local = placement * local;
				parent.object = ids.at(parent.object);
				locator->parent = parent;
				ozz::math::Float3 translation;
				ozz::math::Quaternion rotation;
				ozz::math::Float3 scale;
				if (ozz::math::ToAffine(local, &translation, &rotation, &scale)) {
					// Defaults too, so resets and sequence switches return the locator where the rig put it.
					locator->transform.translation = { translation, translation };
					locator->transform.rotation = { Normalized(rotation), Normalized(rotation) };
					locator->transform.scale = { scale, scale };
				}
			} else if (const auto modifier = object->AsModifier()) {
				modifier->Remap(a_actor, [&](ObjectId a_locator) { return ids.at(a_locator); });
			}
			object->id = ids.at(object->id);
		}

		TransactionScope transaction(*this, "Apply rig");
		Pause();

		if (a_options.replaceModifiers) {
			std::vector<ObjectId> replaced;
			for (const auto id : actor->modifiers) {
				if (const auto object = FindObject(id); object && a_options.Applies(object->GetType())) replaced.push_back(id);
			}
			for (const auto id : replaced) RemoveObject(id);
		}

		if (a_options.pose) {
			// Bones the rig doesn't list are at rest in it.
			std::vector<const LocatorDefaults*> pose(actor->bones.size(), nullptr);
			for (const auto& [bone, transform] : a_rig.pose) {
				if (bone >= 0 && bone < static_cast<int32_t>(pose.size())) pose[bone] = &transform;
			}
			for (size_t bone = 0; bone < pose.size(); ++bone) {
				auto& properties = actor->bones[bone];
				const std::array<std::pair<PropertyId, Value>, 3> values{ {
					{ PropertyId::kTranslation, pose[bone] ? pose[bone]->translation : properties.translation.defaultValue },
					{ PropertyId::kRotation, pose[bone] ? pose[bone]->rotation : properties.rotation.defaultValue },
					{ PropertyId::kScale, pose[bone] ? pose[bone]->scale : properties.scale.defaultValue },
				} };
				for (const auto& [property, value] : values) {
					const PropertyRef ref{ a_actor, static_cast<uint16_t>(bone), property };
					// Unchanged values are skipped, so they aren't marked as edits or keyed.
					if (!(GetLiveValue(FindProperty(ref)) == value) && RecordLiveValue(ref, value)) result.poseChanges.push_back(ref);
				}
			}
		}

		if (a_options.boneMask) {
			std::map<int32_t, bool> mask;
			const auto& defaults = actor->skeleton->defaultBoneMask;
			for (const auto& [bone, masked] : a_rig.maskOverrides) {
				if (bone >= 0 && bone < static_cast<int32_t>(defaults.size()) && masked != !defaults[bone]) mask[bone] = masked;
			}
			if (mask != actor->maskOverrides) Record(BoneMaskChange{ a_actor, actor->maskOverrides, std::move(mask) });
		}

		std::vector<ObjectId> inserted;
		for (auto& object : a_rig.objects) {
			const ObjectId id = object->id;
			// An actor has at most one, which the rig's doesn't replace unless replaceModifiers removed it.
			if (object->GetType() == ObjectType::kCorrectiveDriver && FindCorrectiveDriver(a_actor) != kNoObject) {
				result.warnings.push_back(std::format("\"{}\" was skipped: the actor already has corrective drivers.", object->name));
				continue;
			}
			const bool modifier = object->GetType() != ObjectType::kLocator;
			Record(ObjectLifetimeChange{ ObjectSnapshot{ .id = id, .object = std::move(object), .index = m_objects.size(), .modifierIndex = modifier ? actor->modifiers.size() : 0 }, true });
			inserted.push_back(id);
		}

		// Only possible when a locator took a left-out locator's parent. Moving it to scene space breaks the loop.
		for (const auto id : inserted) {
			if (!m_evaluator.HasSelfDependentModifier()) break;
			const auto locator = FindObject<LocatorObject>(id);
			if (!locator || locator->parent.object == kNoObject) continue;
			RecordReparent(*locator, {}, true);
			result.warnings.push_back(std::format("Locator \"{}\" was moved to scene space: its parent made a modifier depend on itself.", locator->name));
		}
		return result;
	}
}
