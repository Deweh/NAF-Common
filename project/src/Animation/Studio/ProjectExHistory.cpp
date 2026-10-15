#include "ProjectEx.h"

#include "GameBridge.h"
#include "StudioMath.h"

namespace Animation::Studio
{
	ProjectEx::TransactionScope::TransactionScope(ProjectEx& a_project, std::string a_description) :
		m_project(a_project)
	{
		m_project.m_history.Begin(std::move(a_description));
	}

	ProjectEx::TransactionScope::~TransactionScope()
	{
		m_project.m_history.End();
	}

	// ---- Core: Apply ----

	void ProjectEx::Apply(Change& a_change, bool a_forward)
	{
		std::visit([&](auto& a_typed) { ApplyChange(a_typed, a_forward); }, a_change);
		m_evaluator.MarkSolveNeeded();
		++m_revision;
	}

	void ProjectEx::ApplyChange(KeyChange& a_change, bool a_forward)
	{
		const auto sequence = FindSequence(a_change.sequence);
		if (!sequence) return;
		const bool active = sequence->id == m_activeSequence;
		if (a_forward) {
			a_change.discarded.clear();
			if (active) CaptureEdited(a_change.property, a_change.discarded);
		}
		sequence->WriteKey(a_change.property, a_change.frame, a_forward ? a_change.after : a_change.before);
		if (active) ResampleProperty(a_change.property);
		if (!a_forward) RestoreDiscarded(a_change.discarded);
	}

	void ProjectEx::ApplyChange(LiveValueChange& a_change, bool a_forward)
	{
		SetLiveValue(FindProperty(a_change.property), a_forward ? a_change.after : a_change.before, a_forward || a_change.editedBefore);
	}

	void ProjectEx::ApplyChange(FrameChange& a_change, bool a_forward)
	{
		if (a_forward) {
			a_change.discarded.clear();
			CaptureEditedTracked(a_change.discarded);
		}
		m_frame = a_forward ? a_change.after : a_change.before;
		SyncPlaybackTime();
		ResampleAll();
		if (!a_forward) RestoreDiscarded(a_change.discarded);
	}

	void ProjectEx::ApplyChange(ActiveSequenceChange& a_change, bool a_forward)
	{
		if (a_forward) {
			a_change.discarded.clear();
			CaptureAll(a_change.discarded);
		}
		m_activeSequence = a_forward ? a_change.after : a_change.before;
		m_frame = a_forward ? a_change.frameAfter : a_change.frameBefore;
		SyncPlaybackTime();
		ResetAllToDefaults();
		ResampleAll();
		if (!a_forward) RestoreDiscarded(a_change.discarded);
	}

	void ProjectEx::ApplyChange(ObjectLifetimeChange& a_change, bool a_forward)
	{
		if (a_change.existsAfter == a_forward) {
			InsertObject(a_change.snapshot);
		} else {
			RemoveObject(a_change.snapshot);
		}
		m_evaluator.MarkPlanDirty();
	}

	void ProjectEx::ApplyChange(SequenceLifetimeChange& a_change, bool a_forward)
	{
		if (a_change.existsAfter == a_forward) {
			const auto index = (std::min)(a_change.index, m_sequences.size());
			m_sequences.insert(m_sequences.begin() + index, std::move(a_change.sequence));
		} else {
			const auto iter = std::ranges::find(m_sequences, a_change.id, &Sequence::id);
			assert(iter != m_sequences.end() && a_change.id != m_activeSequence);
			if (iter == m_sequences.end()) return;
			a_change.index = static_cast<size_t>(std::distance(m_sequences.begin(), iter));
			a_change.sequence = std::move(*iter);
			m_sequences.erase(iter);
		}
	}

	void ProjectEx::ApplyChange(SequenceSettingsChange& a_change, bool a_forward)
	{
		const auto sequence = FindSequence(a_change.id);
		if (!sequence) return;
		sequence->ApplySettings(a_forward ? a_change.after : a_change.before);
		if (sequence->id == m_activeSequence) {
			SyncPlaybackTime();
			ResampleUnedited();
		}
	}

	void ProjectEx::ApplyChange(ObjectRenameChange& a_change, bool a_forward)
	{
		if (const auto object = FindObject(a_change.id)) object->name = a_forward ? a_change.after : a_change.before;
	}

	void ProjectEx::ApplyChange(LocatorParentChange& a_change, bool a_forward)
	{
		if (const auto locator = FindObject<LocatorObject>(a_change.id)) locator->parent = a_forward ? a_change.after : a_change.before;
		m_evaluator.MarkPlanDirty();
	}

	void ProjectEx::ApplyChange(LocatorDefaultsChange& a_change, bool a_forward)
	{
		const auto locator = FindObject<LocatorObject>(a_change.id);
		if (!locator) return;
		const auto& defaults = a_forward ? a_change.after : a_change.before;
		locator->transform.translation.defaultValue = defaults.translation;
		locator->transform.rotation.defaultValue = defaults.rotation;
		locator->transform.scale.defaultValue = defaults.scale;
	}

	void ProjectEx::ApplyChange(ActorBindingChange& a_change, bool a_forward)
	{
		const auto actor = FindObject<ActorObject>(a_change.id);
		if (!actor) return;
		const auto& binding = a_forward ? a_change.after : a_change.before;
		DetachStudioGenerator(*actor);
		actor->actor = binding.actor;
		if (binding.actor) actor->bindingHint = binding.actor->GetFormID();
		actor->skeletonId = binding.skeletonId;
		if (binding.skeleton && binding.skeleton != actor->skeleton) actor->InitializePose(binding.skeleton, false);
		AttachStudioGenerator(*actor);
	}

	void ProjectEx::ApplyChange(ActorPlacementChange& a_change, bool a_forward)
	{
		// Held now so the actor moves at the same game update as the pose solved for it; Update() keeps holding it.
		const auto actor = FindObject<ActorObject>(a_change.id);
		if (!actor) return;
		actor->placement = a_forward ? a_change.after : a_change.before;
		m_evaluator.HoldAtPlacement(*actor);
	}

	void ProjectEx::ApplyChange(LoopSmoothingChange& a_change, bool a_forward)
	{
		const auto sequence = FindSequence(a_change.sequence);
		if (!sequence) return;
		if (a_forward ? a_change.after : a_change.before) sequence->loopSmoothedProperties.insert(a_change.property);
		else sequence->loopSmoothedProperties.erase(a_change.property);

		const auto track = sequence->FindTrack(a_change.property);
		if (!track) return;
		sequence->RegenerateTangents(a_change.property);
		if (sequence->id == m_activeSequence) {
			const auto property = FindProperty(a_change.property);
			if (!Studio::IsEdited(property)) SetLiveValue(property, sequence->Sample(a_change.property, *track, GetSampleFrame()), false);
		}
	}

	void ProjectEx::ApplyChange(BezierModeChange& a_change, bool a_forward)
	{
		const auto sequence = FindSequence(a_change.sequence);
		if (!sequence) return;
		const auto mode = a_forward ? a_change.after : a_change.before;
		if (mode != BezierMode::kLocal) sequence->bezierModes.insert_or_assign(a_change.property, mode);
		else sequence->bezierModes.erase(a_change.property);

		const auto track = sequence->FindTrack(a_change.property);
		if (!track) return;
		sequence->RegenerateTangents(a_change.property);
		if (sequence->id == m_activeSequence) {
			const auto property = FindProperty(a_change.property);
			if (!Studio::IsEdited(property)) SetLiveValue(property, sequence->Sample(a_change.property, *track, GetSampleFrame()), false);
		}
	}

	void ProjectEx::ApplyChange(ModifierSettingsChange& a_change, bool a_forward)
	{
		if (const auto modifier = m_objects.FindModifier(a_change.id)) modifier->SetSettings(a_forward ? a_change.after : a_change.before);
		// Settings can change what a modifier writes, such as a chain's shoulder bone.
		m_evaluator.MarkPlanDirty();
	}

	void ProjectEx::ApplyChange(BoneMaskChange& a_change, bool a_forward)
	{
		const auto actor = FindObject<ActorObject>(a_change.id);
		if (!actor) return;
		actor->maskOverrides = a_forward ? a_change.after : a_change.before;
		actor->PublishBoneMask();
	}

	ActorBinding ProjectEx::GetBinding(const ActorObject& a_actor)
	{
		return { a_actor.actor, a_actor.skeleton, a_actor.skeletonId };
	}

	void ProjectEx::InsertObject(ObjectSnapshot& a_snapshot)
	{
		assert(a_snapshot.object && a_snapshot.object->id == a_snapshot.id);
		if (!a_snapshot.object) return;

		for (auto& [sequenceId, ref, track] : a_snapshot.tracks) {
			if (const auto sequence = FindSequence(sequenceId)) sequence->tracks.insert_or_assign(ref, std::move(track));
		}
		a_snapshot.tracks.clear();
		for (const auto& [sequenceId, ref] : a_snapshot.loopSmoothedProperties) {
			if (const auto sequence = FindSequence(sequenceId)) sequence->loopSmoothedProperties.insert(ref);
		}
		a_snapshot.loopSmoothedProperties.clear();
		for (const auto& [sequenceId, ref, mode] : a_snapshot.bezierModes) {
			if (const auto sequence = FindSequence(sequenceId)) sequence->bezierModes.insert_or_assign(ref, mode);
		}
		a_snapshot.bezierModes.clear();

		auto& object = *a_snapshot.object;
		switch (object.GetType()) {
		case ObjectType::kActor:
			{
				auto& actor = static_cast<ActorObject&>(object);
				assert(!actor.actor || !FindActorObject(actor.actor.get()));
				AttachStudioGenerator(actor);
				break;
			}
		default:
			if (const auto actor = FindObject<ActorObject>(GetModifierActor(object))) {
				const auto index = (std::min)(a_snapshot.modifierIndex, actor->modifiers.size());
				actor->modifiers.insert(actor->modifiers.begin() + index, object.id);
			}
			break;
		}

		m_objects.Insert(a_snapshot.index, std::move(a_snapshot.object));
		object.ForEachProperty([&](uint16_t a_element, PropertyId a_property, PropertyPtr) {
			ResampleProperty({ object.id, a_element, a_property });
		});
	}

	void ProjectEx::RemoveObject(ObjectSnapshot& a_snapshot)
	{
		const auto found = FindObject(a_snapshot.id);
		assert(found);
		if (!found) return;

		// Tracks are ordered by object first, so each sequence's tracks for this object form one contiguous range.
		a_snapshot.tracks.clear();
		a_snapshot.loopSmoothedProperties.clear();
		a_snapshot.bezierModes.clear();
		const PropertyRef rangeStart{ a_snapshot.id, 0, PropertyId::kTranslation };
		const PropertyRef rangeEnd{ a_snapshot.id + 1, 0, PropertyId::kTranslation };
		for (auto& sequence : m_sequences) {
			const auto first = sequence.tracks.lower_bound(rangeStart);
			const auto last = sequence.tracks.lower_bound(rangeEnd);
			for (auto track = first; track != last; ++track) {
				a_snapshot.tracks.emplace_back(sequence.id, track->first, std::move(track->second));
			}
			sequence.tracks.erase(first, last);

			const auto firstSmoothed = sequence.loopSmoothedProperties.lower_bound(rangeStart);
			const auto lastSmoothed = sequence.loopSmoothedProperties.lower_bound(rangeEnd);
			for (auto smoothed = firstSmoothed; smoothed != lastSmoothed; ++smoothed) {
				a_snapshot.loopSmoothedProperties.emplace_back(sequence.id, *smoothed);
			}
			sequence.loopSmoothedProperties.erase(firstSmoothed, lastSmoothed);

			const auto firstMode = sequence.bezierModes.lower_bound(rangeStart);
			const auto lastMode = sequence.bezierModes.lower_bound(rangeEnd);
			for (auto mode = firstMode; mode != lastMode; ++mode) {
				a_snapshot.bezierModes.emplace_back(sequence.id, mode->first, mode->second);
			}
			sequence.bezierModes.erase(firstMode, lastMode);
		}

		auto& object = *found;
		switch (object.GetType()) {
		case ObjectType::kActor:
			DetachStudioGenerator(static_cast<ActorObject&>(object));
			break;
		default:
			if (const auto actor = FindObject<ActorObject>(GetModifierActor(object))) {
				const auto modifier = std::ranges::find(actor->modifiers, object.id);
				a_snapshot.modifierIndex = static_cast<size_t>(std::distance(actor->modifiers.begin(), modifier));
				if (modifier != actor->modifiers.end()) actor->modifiers.erase(modifier);
			}
			break;
		}

		a_snapshot.object = m_objects.Extract(a_snapshot.id, a_snapshot.index);
	}

	void ProjectEx::CaptureEdited(const PropertyRef& a_ref, std::vector<DiscardedValue>& a_out)
	{
		const auto property = FindProperty(a_ref);
		if (Studio::IsEdited(property)) a_out.push_back({ a_ref, GetLiveValue(property), true });
	}

	void ProjectEx::CaptureEditedTracked(std::vector<DiscardedValue>& a_out)
	{
		const auto sequence = GetActiveSequence();
		if (!sequence) return;
		for (const auto& ref : sequence->tracks | std::views::keys) {
			CaptureEdited(ref, a_out);
		}
	}

	void ProjectEx::CaptureAll(std::vector<DiscardedValue>& a_out)
	{
		for (auto& object : m_objects) {
			object->ForEachProperty([&](uint16_t a_element, PropertyId a_property, PropertyPtr a_ptr) {
				a_out.push_back({ { object->id, a_element, a_property }, GetLiveValue(a_ptr), Studio::IsEdited(a_ptr) });
			});
		}
	}

	void ProjectEx::RestoreDiscarded(const std::vector<DiscardedValue>& a_values)
	{
		for (const auto& discarded : a_values) {
			SetLiveValue(FindProperty(discarded.property), discarded.value, discarded.edited);
		}
	}

	// ---- Core: History ----

	void ProjectEx::Record(Change a_change)
	{
		assert(m_history.IsOpen());
		Apply(a_change, true);
		m_history.Push(std::move(a_change));
	}

	bool ProjectEx::RecordKeyWrite(Sequence& a_sequence, const PropertyRef& a_ref, int32_t a_frame, std::optional<AnyKey> a_key)
	{
		auto before = a_sequence.GetKey(a_ref, a_frame);
		if (before == a_key) return false;
		Record(KeyChange{ .sequence = a_sequence.id, .property = a_ref, .frame = a_frame, .before = std::move(before), .after = std::move(a_key) });
		return true;
	}

	bool ProjectEx::RecordLiveValue(const PropertyRef& a_ref, const Value& a_value)
	{
		const auto property = FindProperty(a_ref);
		if (!MatchesType(property, a_value.index())) return false;
		auto before = GetLiveValue(property);
		const bool edited = Studio::IsEdited(property);
		if (edited && before == a_value) return false;
		Record(LiveValueChange{ a_ref, std::move(before), a_value, edited });
		return true;
	}

	bool ProjectEx::RecordTransform(ElementRef a_ref, const ozz::math::Float4x4& a_local, uint8_t a_mask)
	{
		ozz::math::Float3 translation;
		ozz::math::Quaternion rotation;
		ozz::math::Float3 scale;
		if (!ozz::math::ToAffine(a_local, &translation, &rotation, &scale)) return false;
		bool changed = false;
		if (a_mask & kTransformTranslation) changed |= RecordLiveValue({ a_ref.object, a_ref.element, PropertyId::kTranslation }, translation);
		if (a_mask & kTransformRotation) changed |= RecordLiveValue({ a_ref.object, a_ref.element, PropertyId::kRotation }, Normalized(rotation));
		if (a_mask & kTransformScale) changed |= RecordLiveValue({ a_ref.object, a_ref.element, PropertyId::kScale }, scale);
		return changed;
	}

	void ProjectEx::RecordReparent(LocatorObject& a_locator, ParentRef a_parent, bool a_keepWorldTransform)
	{
		Evaluate();
		const auto world = m_evaluator.ResolveLocatorWorld(a_locator);
		// The defaults are carried over like the live values, measured against the parents as they stand now.
		// Otherwise a reset or sequence switch would apply them in the new parent's space.
		const auto defaults = a_locator.transform.GetDefaults();
		const auto oldParentWorld = m_evaluator.ResolveParentWorld(a_locator.parent);
		const auto defaultWorld = oldParentWorld ?
		                              std::optional{ *oldParentWorld * ozz::math::Float4x4::FromAffine(defaults.translation, Normalized(defaults.rotation), defaults.scale) } :
		                              std::nullopt;
		Record(LocatorParentChange{ a_locator.id, a_locator.parent, a_parent });
		if (!a_keepWorldTransform || !world) return;

		Evaluate();
		const auto parentWorld = m_evaluator.ResolveParentWorld(a_parent);
		const auto parentInverse = parentWorld ? TryInvert(*parentWorld) : std::nullopt;
		if (!parentInverse) return;
		RecordTransform({ a_locator.id, kSelf }, *parentInverse * *world, kTransformAll);

		LocatorDefaults after;
		if (defaultWorld && ozz::math::ToAffine(*parentInverse * *defaultWorld, &after.translation, &after.rotation, &after.scale)) {
			after.rotation = Normalized(after.rotation);
			Record(LocatorDefaultsChange{ a_locator.id, defaults, after });
		}
	}

	bool ProjectEx::RecordModifierSettings(ObjectId a_id, ModifierSettings a_settings)
	{
		const auto modifier = m_objects.FindModifier(a_id);
		if (!modifier) return false;
		auto before = modifier->GetSettings();
		if (before.index() != a_settings.index() || before == a_settings) return false;
		Record(ModifierSettingsChange{ a_id, std::move(before), std::move(a_settings) });
		return true;
	}

	void ProjectEx::ApplyCommit(Commit& a_commit, bool a_forward)
	{
		if (a_forward) {
			for (auto& change : a_commit.changes) Apply(change, true);
		} else {
			for (auto& change : a_commit.changes | std::views::reverse) Apply(change, false);
		}
	}

	// ---- History ----

	void ProjectEx::BeginEdit(std::string a_description)
	{
		m_history.Begin(std::move(a_description));
	}

	void ProjectEx::EndEdit()
	{
		m_history.End();
	}

	void ProjectEx::CancelEdit()
	{
		const auto open = m_history.GetOpen();
		if (!open) return;
		// Scopes stay open; the outermost EndEdit drops the now-empty commit.
		ApplyCommit(*open, false);
		m_history.ClearOpen();
	}

	bool ProjectEx::IsEditing() const
	{
		return m_history.IsOpen();
	}

	bool ProjectEx::Undo()
	{
		if (IsEditing()) return false;
		CancelPlayback();
		const auto commit = m_history.StepBack();
		if (!commit) return false;
		ApplyCommit(*commit, false);
		return true;
	}

	bool ProjectEx::Redo()
	{
		if (IsEditing()) return false;
		CancelPlayback();
		const auto commit = m_history.StepForward();
		if (!commit) return false;
		ApplyCommit(*commit, true);
		return true;
	}

	bool ProjectEx::CanUndo() const
	{
		return !IsEditing() && m_history.GetHead() > 0;
	}

	bool ProjectEx::CanRedo() const
	{
		return !IsEditing() && m_history.GetHead() < m_history.GetSize();
	}

	size_t ProjectEx::GetHistorySize() const
	{
		return m_history.GetSize();
	}

	size_t ProjectEx::GetHistoryHead() const
	{
		return m_history.GetHead();
	}

	std::string_view ProjectEx::GetHistoryDescription(size_t a_index) const
	{
		return m_history.GetDescription(a_index);
	}

	bool ProjectEx::JumpToHistory(size_t a_head)
	{
		if (IsEditing() || a_head > m_history.GetSize()) return false;
		CancelPlayback();
		while (m_history.GetHead() > a_head) ApplyCommit(*m_history.StepBack(), false);
		while (m_history.GetHead() < a_head) ApplyCommit(*m_history.StepForward(), true);
		return true;
	}

	bool ProjectEx::HasUnsavedChanges() const
	{
		return m_history.HasUnsavedChanges();
	}

	void ProjectEx::MarkSaved()
	{
		m_history.MarkSaved();
	}
}
