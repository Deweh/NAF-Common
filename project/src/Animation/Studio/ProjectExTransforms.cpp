#include "ProjectEx.h"

#include "StudioMath.h"

namespace Animation::Studio
{
	// ---- Transforms ----

	std::optional<ozz::math::Float4x4> ProjectEx::GetWorldTransform(ElementRef a_ref)
	{
		Evaluate();
		if (const auto actor = FindObject<ActorObject>(a_ref.object)) {
			if (!actor->hasRoot) return std::nullopt;
			if (a_ref.element == kSelf) return actor->rootWorld;
			if (a_ref.element >= actor->model.size()) return std::nullopt;
			return actor->rootWorld * actor->model[a_ref.element];
		}
		if (const auto locator = FindObject<LocatorObject>(a_ref.object); locator && a_ref.element == kSelf) return m_evaluator.ResolveLocatorWorld(*locator);
		return std::nullopt;
	}

	bool ProjectEx::SetWorldTransform(ElementRef a_ref, const ozz::math::Float4x4& a_world, uint8_t a_mask)
	{
		if (IsDrivenByModifier(a_ref)) return false;
		const WorldTransformEdit edit{ a_ref, a_world };
		return SetWorldTransforms(std::span{ &edit, 1 }, a_mask);
	}

	bool ProjectEx::SetWorldTransforms(std::span<const WorldTransformEdit> a_edits, uint8_t a_mask)
	{
		TransactionScope transaction(*this, "Transform");
		Pause();
		Evaluate();

		// Each edit is measured against the evaluated transforms, so re-evaluate only when the edits recorded since
		// the last evaluation may have moved them. A full evaluation per edit made multi-selection drags scale badly.
		std::vector<ElementRef> edited;
		bool changed = false;
		for (const auto& edit : a_edits) {
			if (!edited.empty() && m_evaluator.IsAffectedByEdits(edit.element, edited)) {
				Evaluate();
				edited.clear();
			}
			if (RecordWorldTransform(edit.element, edit.world, a_mask)) {
				edited.push_back(edit.element);
				changed = true;
			}
		}
		return changed;
	}

	bool ProjectEx::RecordWorldTransform(ElementRef a_ref, const ozz::math::Float4x4& a_world, uint8_t a_mask)
	{
		if (IsDrivenByModifier(a_ref)) return false;

		std::optional<ozz::math::Float4x4> parentWorld;
		if (const auto placed = FindObject<ActorObject>(a_ref.object); placed && a_ref.element == kSelf) {
			const auto sceneInverse = TryInvert(ToMatrix(m_sceneCenter));
			if (!sceneInverse) return false;
			const auto moved = ToPlacement(*sceneInverse * a_world);
			auto placement = placed->placement;
			if (a_mask & kTransformTranslation) placement.translation = moved.translation;
			if (a_mask & kTransformRotation) placement.heading = moved.heading;
			if (placement == placed->placement) return false;
			Record(ActorPlacementChange{ a_ref.object, placed->placement, placement });
			return true;
		} else if (const auto actor = FindObject<ActorObject>(a_ref.object)) {
			// Bones no modifier drives keep their local transform through the solve, so the final parent is the
			// right space. Additive modifiers (finger curl, auto-shoulder, corrective drivers) still rotate them, by
			// an offset that stays out of the edit: live = desired * inverse(inverse(live) * posed). Corrective
			// drivers also slide some, by a translation offset kept out the same way: live = desired - (posed - live).
			if (!actor->hasRoot || !actor->skeleton || !actor->pose || a_ref.element >= actor->model.size()) return false;
			const int16_t parent = actor->skeleton->data->joint_parents()[a_ref.element];
			const auto boneParentWorld = parent == ozz::animation::Skeleton::kNoParent ? actor->rootWorld : actor->rootWorld * actor->model[parent];
			const auto parentInverse = TryInvert(boneParentWorld);
			if (!parentInverse) return false;
			auto local = *parentInverse * a_world;
			const auto& bone = actor->bones[a_ref.element];
			const auto live = Normalized(bone.rotation.live);
			// The front buffer: once a solve is published, the back buffer holds an older pose.
			const auto& front = actor->pose->Front();
			const auto posed = GetPoseRotation(front, a_ref.element);
			const auto offset = ozz::math::Conjugate(live) * EnsureSameHemisphere(live, posed);
			const auto slide = GetPoseTranslation(front, a_ref.element) - bone.translation.live;
			const bool rotated = (a_mask & kTransformRotation) && std::abs(offset.w) < 0.999999f;
			const bool slid = (a_mask & kTransformTranslation) && ozz::math::Length(slide) > 0.00001f;
			ozz::math::Float3 translation;
			ozz::math::Quaternion rotation;
			ozz::math::Float3 scale;
			if ((rotated || slid) && ozz::math::ToAffine(local, &translation, &rotation, &scale)) {
				if (rotated) rotation = Normalized(rotation * ozz::math::Conjugate(offset));
				if (slid) translation = translation - slide;
				local = ozz::math::Float4x4::FromAffine(translation, rotation, scale);
			}
			return RecordTransform(a_ref, local, a_mask);
		} else if (const auto locator = FindObject<LocatorObject>(a_ref.object); locator && a_ref.element == kSelf) {
			parentWorld = m_evaluator.ResolveParentWorld(locator->parent);
		}
		const auto parentInverse = parentWorld ? TryInvert(*parentWorld) : std::nullopt;
		return parentInverse && RecordTransform(a_ref, *parentInverse * a_world, a_mask);
	}

	std::span<const ozz::math::Float4x4> ProjectEx::GetModelTransforms(ObjectId a_actor)
	{
		Evaluate();
		const auto actor = FindObject<ActorObject>(a_actor);
		return actor ? std::span<const ozz::math::Float4x4>{ actor->model } : std::span<const ozz::math::Float4x4>{};
	}

	// ---- Bone mask ----

	bool ProjectEx::IsBoneMasked(ElementRef a_bone) const
	{
		const auto actor = FindObject<ActorObject>(a_bone.object);
		return actor && actor->IsBoneMasked(a_bone.element);
	}

	bool ProjectEx::SetBoneMasked(std::span<const ElementRef> a_bones, bool a_masked)
	{
		std::map<ObjectId, std::map<int32_t, bool>> changed;
		for (const auto& bone : a_bones) {
			const auto actor = FindObject<ActorObject>(bone.object);
			if (!actor || !actor->skeleton || bone.element >= actor->skeleton->defaultBoneMask.size()) continue;
			auto& after = changed.try_emplace(bone.object, actor->maskOverrides).first->second;
			if (a_masked == !actor->skeleton->defaultBoneMask[bone.element]) after.erase(bone.element);
			else after[bone.element] = a_masked;
		}
		std::erase_if(changed, [&](const auto& a_entry) { return FindObject<ActorObject>(a_entry.first)->maskOverrides == a_entry.second; });
		if (changed.empty()) return false;

		TransactionScope transaction(*this, a_masked ? "Mask bones" : "Unmask bones");
		Pause();
		for (auto& [id, after] : changed) Record(BoneMaskChange{ id, FindObject<ActorObject>(id)->maskOverrides, std::move(after) });
		return true;
	}

	bool ProjectEx::ResetBoneMask(ObjectId a_actor)
	{
		const auto actor = FindObject<ActorObject>(a_actor);
		if (!actor || actor->maskOverrides.empty()) return false;
		TransactionScope transaction(*this, "Reset bone mask");
		Pause();
		Record(BoneMaskChange{ a_actor, actor->maskOverrides, {} });
		return true;
	}
}
