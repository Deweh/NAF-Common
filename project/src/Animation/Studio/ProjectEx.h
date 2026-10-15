#pragma once
#include "Animation/Studio/Evaluator.h"
#include "Animation/Studio/History.h"
#include "Animation/Studio/Modifiers/Modifier.h"
#include "Animation/Studio/ObjectStore.h"
#include "Animation/Studio/SceneObjects.h"
#include "Animation/Studio/Sequence.h"
#include "Animation/Studio/StudioTypes.h"

#include <expected>

namespace Serialization
{
	class StudioProjectExport;
	class StudioProjectImport;
}

namespace Animation::Studio
{
	// The whole project model: scene objects, sequences and tracks, sampling, evaluation and history. No UI.
	// The types it works with live in StudioTypes.h, Curve.h, Sequence.h and SceneObjects.h; the aliases below keep
	// ProjectEx::ObjectId and friends working for its callers.
	//
	// ProjectEx is the facade. The work happens in ObjectStore, History, Evaluator, GameBridge and Modifiers/, and its
	// own definitions are split by feature: ProjectEx.cpp (scene and objects), ProjectExHistory.cpp (changes and undo),
	// ProjectExTimeline.cpp (sampling, transport, sequences, properties and keys), ProjectExModifiers.cpp,
	// ProjectExTransforms.cpp (world transforms and bone mask), ProjectExLoading.cpp (file hooks and rigs) and
	// ProjectExBake.cpp.
	class ProjectEx
	{
	public:
		using ObjectId = Studio::ObjectId;
		using SequenceId = Studio::SequenceId;
		static constexpr ObjectId kNoObject = Studio::kNoObject;
		static constexpr SequenceId kNoSequence = Studio::kNoSequence;
		static constexpr uint16_t kSelf = Studio::kSelf;

		using ObjectType = Studio::ObjectType;
		using PropertyId = Studio::PropertyId;
		using Interpolation = Studio::Interpolation;
		using TangentMode = Studio::TangentMode;
		using BezierMode = Studio::BezierMode;
		using PropertyRef = Studio::PropertyRef;
		using KeyRef = Studio::KeyRef;
		using ElementRef = Studio::ElementRef;
		using TransformMask = Studio::TransformMask;
		static constexpr uint8_t kTransformTranslation = Studio::kTransformTranslation;
		static constexpr uint8_t kTransformRotation = Studio::kTransformRotation;
		static constexpr uint8_t kTransformScale = Studio::kTransformScale;
		static constexpr uint8_t kTransformAll = Studio::kTransformAll;
		using ParentRef = Studio::ParentRef;
		using ScenePlacement = Studio::ScenePlacement;
		template <class T>
		using Key = Studio::Key<T>;
		using Value = Studio::Value;
		using AnyKey = Studio::AnyKey;
		using SequenceInfo = Studio::SequenceInfo;
		using LoopSmoothingInfo = Studio::LoopSmoothingInfo;
		using IKChainSettings = Studio::IKChainSettings;
		using FingerSetup = Studio::FingerSetup;
		using HandSettings = Studio::HandSettings;
		using SplineIKSettings = Studio::SplineIKSettings;
		using IKChainInfo = Studio::IKChainInfo;
		using HandInfo = Studio::HandInfo;
		using SplineIKInfo = Studio::SplineIKInfo;
		using KeyData = Studio::KeyData;
		using WorldTransformEdit = Studio::WorldTransformEdit;
		using RigOptions = Studio::RigOptions;
		using RigResult = Studio::RigResult;

		ProjectEx();
		~ProjectEx();

		ProjectEx(const ProjectEx&) = delete;
		ProjectEx(ProjectEx&&) = delete;
		ProjectEx& operator=(const ProjectEx&) = delete;
		ProjectEx& operator=(ProjectEx&&) = delete;

		// ---- Evaluation ----

		// Call once per Present, after drawing: advances playback, then evaluates. The game shows the result at its
		// next update, so drawing first keeps overlays on the pose that's on screen.
		void Update(float a_deltaTime);
		// Brings every evaluated transform and pose buffer up to date. Cheap when nothing changed.
		void Evaluate();

		// ---- History ----

		// Every mutator below is its own undo step unless wrapped in an edit. Edits nest.
		// For drags, call CancelEdit() and reapply the whole operation from its starting state each frame.
		void BeginEdit(std::string a_description);
		void EndEdit();
		void CancelEdit();
		[[nodiscard]] bool IsEditing() const;
		// Refused while an edit is open. While playing, playback is cancelled first (back to the frame it started from).
		bool Undo();
		bool Redo();
		[[nodiscard]] bool CanUndo() const;
		[[nodiscard]] bool CanRedo() const;
		[[nodiscard]] size_t GetHistorySize() const;
		[[nodiscard]] size_t GetHistoryHead() const;
		[[nodiscard]] std::string_view GetHistoryDescription(size_t a_index) const;
		bool JumpToHistory(size_t a_head);
		[[nodiscard]] bool HasUnsavedChanges() const;
		void MarkSaved();
		// Changes whenever the project does (undo and redo included), for callers caching what they read from it.
		// Playback doesn't count: it only moves live values.
		[[nodiscard]] uint64_t GetRevision() const { return m_revision; }

		// ---- Transport ----

		[[nodiscard]] int32_t GetFrame() const;
		[[nodiscard]] float GetSampleFrame() const;  // Fractional during playback.
		bool SetFrame(int32_t a_frame);              // Clamped to [0, frameCount].
		// Loops over a_loopRange (clamped to the sequence), or the whole sequence.
		void Play(std::optional<std::pair<int32_t, int32_t>> a_loopRange = std::nullopt);
		void Pause();
		[[nodiscard]] bool IsPlaying() const;

		// ---- Sequences ----
		// A project always has at least one sequence. frameCount is the sequence duration, so frames run [0, frameCount].

		[[nodiscard]] std::vector<SequenceInfo> GetSequences() const;
		[[nodiscard]] SequenceId GetActiveSequenceId() const;
		SequenceId AddSequence(std::string a_name, float a_frameRate, int32_t a_frameCount);
		// Copies the sequence and switches to the copy. Duplicating the active sequence also carries over its live
		// values, unkeyed edits included, so the copy shows exactly what the source did.
		SequenceId DuplicateSequence(SequenceId a_id);
		bool RemoveSequence(SequenceId a_id);
		bool SetActiveSequence(SequenceId a_id);
		bool RenameSequence(SequenceId a_id, std::string a_name);
		bool SetSequenceFrameCount(SequenceId a_id, int32_t a_frameCount);  // Keys past the end are kept.
		// Rescaling keeps the sequence's length in seconds by moving keys, the frame count and the playhead.
		bool SetSequenceFrameRate(SequenceId a_id, float a_frameRate, bool a_rescaleKeys);
		bool SetSequenceLoopSmoothing(SequenceId a_id, bool a_enabled);

		// ---- Scene ----
		// Everything is placed relative to the scene center, so a project plays the same wherever it's opened:
		// unparented locators are in scene space, and each actor is held at its placement relative to the center.

		static ozz::math::Float4x4 ToMatrix(const ScenePlacement& a_placement);
		static ScenePlacement ToPlacement(const ozz::math::Float4x4& a_matrix);  // Keeps only translation and heading.
		static ScenePlacement GetWorldPlacement(const RE::TESObjectREFR* a_reference);
		// Holds an actor at a world placement through its animation graph.
		static void HoldActor(RE::Actor* a_actor, const ScenePlacement& a_world);

		[[nodiscard]] const ScenePlacement& GetSceneCenter() const;
		// Not saved or recorded in history. Meant to be set while a project is opening, before any editing.
		void SetSceneCenter(const ScenePlacement& a_center);
		[[nodiscard]] std::optional<ScenePlacement> GetActorPlacement(ObjectId a_id) const;  // Relative to the scene center.
		bool SetActorPlacement(ObjectId a_id, const ScenePlacement& a_placement);

		// ---- Objects ----

		[[nodiscard]] std::vector<ObjectId> GetObjects() const;  // In outliner order.
		[[nodiscard]] std::optional<ObjectType> GetObjectType(ObjectId a_id) const;
		[[nodiscard]] std::string_view GetObjectName(ObjectId a_id) const;
		bool RenameObject(ObjectId a_id, std::string a_name);
		// Also removes dependents: an actor's modifiers, and a modifier's locators. Other locators parented to a
		// removed object are reparented to scene space, keeping their world transform.
		bool RemoveObject(ObjectId a_id);

		// The actor's placement is taken from where it stands. If it's the project's first object, the scene center
		// also moves onto it.
		std::expected<ObjectId, std::string> AddActor(RE::Actor* a_actor);
		std::expected<void, std::string> BindActor(ObjectId a_id, RE::Actor* a_actor);
		void UnbindActor(ObjectId a_id);
		[[nodiscard]] RE::Actor* GetBoundActor(ObjectId a_id) const;
		// The actor most recently bound to the slot, kept while unbound and saved with the project. Only a
		// suggestion for rebinding: the form may no longer exist.
		[[nodiscard]] RE::TESFormID GetBindingHint(ObjectId a_id) const;
		// Whether an actor meets a slot's requirements. BindActor checks the same before loading the skeleton.
		static std::expected<void, std::string> CheckActorCompatibility(std::string_view a_skeletonId, RE::SEX a_gender, RE::Actor* a_actor);
		[[nodiscard]] const ozz::animation::Skeleton* GetSkeleton(ObjectId a_id) const;
		[[nodiscard]] std::string_view GetSkeletonId(ObjectId a_actor) const;  // The slot's skeleton requirement.
		[[nodiscard]] std::span<const ObjectId> GetModifiers(ObjectId a_actor) const;
		// The actor a modifier (IK chain, hand or spline IK) belongs to. kNoObject for anything else.
		[[nodiscard]] ObjectId GetModifierActor(ObjectId a_modifier) const;

		// a_world, when given, is the initial placement (translation and rotation). It's also the locator's default,
		// so resets and sequence switches return it there. Without it the locator starts at its parent's origin.
		std::expected<ObjectId, std::string> AddLocator(ParentRef a_parent, std::optional<ozz::math::Float4x4> a_world = std::nullopt);
		// Rejects parent loops, and parents that would make an IK chain depend on its own result.
		std::expected<void, std::string> SetLocatorParent(ObjectId a_id, ParentRef a_parent, bool a_keepWorldTransform);
		[[nodiscard]] std::optional<ParentRef> GetLocatorParent(ObjectId a_id) const;
		// Makes the locator's live transform its default, where resets and sequence switches return it.
		bool SetLocatorDefaultsToCurrent(ObjectId a_id);

		// The bones must form an upper -> middle -> end ancestor chain. Intervening bones may be skipped.
		// Creates the chain plus scene-space effector and pole locators placed from the current pose.
		std::expected<ObjectId, std::string> AddIKChain(ObjectId a_actor, int32_t a_upperBone, int32_t a_middleBone, int32_t a_endBone);
		[[nodiscard]] std::optional<IKChainInfo> GetIKChain(ObjectId a_id) const;
		// Rejects a shoulder bone that isn't a strict ancestor of the chain's upper bone.
		// Choosing a new shoulder bone captures its neutral point from the current effector.
		bool SetIKChainSettings(ObjectId a_id, const IKChainSettings& a_settings);
		// Makes the effector's current position, relative to the shoulder's parent, the one where the shoulder rests.
		bool SetIKChainShoulderNeutral(ObjectId a_id);

		// Finger controls for the fingers found below a_handBone.
		std::expected<ObjectId, std::string> AddHand(ObjectId a_actor, int32_t a_handBone);
		[[nodiscard]] std::optional<HandInfo> GetHand(ObjectId a_id) const;
		// Only the per-finger tuning (flip, limits, straighten) can change; the fingers and their axes are fixed at creation.
		bool SetHandSettings(ObjectId a_id, const HandSettings& a_settings);

		// a_startBone must be an ancestor of a_endBone, with at least one bone between them. Creates the spline plus
		// a_controlCount scene-space control locators, placed on bones of the current pose.
		std::expected<ObjectId, std::string> AddSplineIK(ObjectId a_actor, int32_t a_startBone, int32_t a_endBone, uint32_t a_controlCount);
		// The most controls AddSplineIK can place: one per distinct point on the path, since bones on top of each
		// other (zero-length offset bones) share one. Below 2 when no spline can be made.
		[[nodiscard]] size_t GetSplineIKControlLimit(ObjectId a_actor, int32_t a_startBone, int32_t a_endBone);
		[[nodiscard]] std::optional<SplineIKInfo> GetSplineIK(ObjectId a_id) const;
		bool SetSplineIKSettings(ObjectId a_id, const SplineIKSettings& a_settings);
		// The evaluated curve in world space, for display.
		[[nodiscard]] std::vector<ozz::math::Float3> GetSplineIKCurve(ObjectId a_id);

		// The modifier using a locator (an IK chain's effector or pole, or a spline's control), or kNoObject.
		[[nodiscard]] ObjectId GetLocatorOwner(ObjectId a_locator) const;
		// True for bones an enabled modifier fully owns: an IK chain's or a spline's path.
		[[nodiscard]] bool IsDrivenByModifier(ElementRef a_ref) const;
		// IsDrivenByModifier for every bone of an actor at once, indexed by bone. Empty for anything else.
		[[nodiscard]] std::vector<bool> GetDrivenBones(ObjectId a_actor) const;

		// ---- Transforms (evaluated) ----

		[[nodiscard]] std::optional<ozz::math::Float4x4> GetWorldTransform(ElementRef a_ref);
		// Records the channels in a_mask (TransformMask flags) as live edits. Rejected for bones driven by IK.
		// For an actor itself, sets its placement from the translation and heading instead.
		bool SetWorldTransform(ElementRef a_ref, const ozz::math::Float4x4& a_world, uint8_t a_mask);
		// Several at once, applied in order, so parents go before children. Re-evaluates between edits only when an
		// earlier edit may have moved what a later one is measured against.
		bool SetWorldTransforms(std::span<const WorldTransformEdit> a_edits, uint8_t a_mask);
		// Final model-space (actor root relative) bone transforms.
		[[nodiscard]] std::span<const ozz::math::Float4x4> GetModelTransforms(ObjectId a_actor);

		// ---- Bone mask ----
		// Masked bones aren't written to the game, so the Studio's pose doesn't reach them. The skeleton's default mask
		// leaves out bones the game's animation skeleton doesn't have (NIF-only bones such as physics bones); each
		// actor can override it per bone. Not keyable.

		[[nodiscard]] bool IsBoneMasked(ElementRef a_bone) const;
		// One undo step for every actor touched. Overrides that match the skeleton's default are dropped.
		bool SetBoneMasked(std::span<const ElementRef> a_bones, bool a_masked);
		bool ResetBoneMask(ObjectId a_actor);  // Back to the skeleton's default.

		// ---- Rigs ----
		// An actor's setup saved apart from any project: its pose, its modifiers with their locators, and its bone mask.
		// Applied to any actor with the same skeleton through StudioProjectExport::SaveRig and StudioProjectImport::ApplyRig.
		// RigOptions and RigResult are in StudioTypes.h.

		// ---- Properties ----

		[[nodiscard]] std::optional<Value> GetValue(const PropertyRef& a_ref) const;
		template <class T>
		[[nodiscard]] std::optional<T> GetValue(const PropertyRef& a_ref) const
		{
			const auto value = GetValue(a_ref);
			const auto typed = value ? std::get_if<T>(std::addressof(*value)) : nullptr;
			return typed ? std::optional<T>{ *typed } : std::nullopt;
		}
		bool SetValue(const PropertyRef& a_ref, const Value& a_value);  // Type-checked against the property.
		bool ResetValue(const PropertyRef& a_ref);                       // Back to the property's default.
		[[nodiscard]] bool IsEdited(const PropertyRef& a_ref) const;

		// ---- Keys (active sequence) ----

		[[nodiscard]] bool IsTracked(const PropertyRef& a_ref) const;
		[[nodiscard]] std::vector<PropertyRef> GetTrackedProperties() const;
		[[nodiscard]] std::vector<int32_t> GetKeyFrames(const PropertyRef& a_ref) const;
		[[nodiscard]] std::optional<AnyKey> GetKey(const KeyRef& a_ref) const;
		// Keys each property's live value at the current frame. An existing key keeps its interpolation.
		bool KeyCurrentValue(std::span<const PropertyRef> a_refs, Interpolation a_interpolation);
		bool SetKeys(std::span<const KeyData> a_keys);  // Type-checked against each property.
		bool DeleteKeys(std::span<const KeyRef> a_keys);
		// Moved keys overwrite unselected keys they land on. Returns the moved keys' new refs.
		std::optional<std::vector<KeyRef>> MoveKeys(std::span<const KeyRef> a_keys, int32_t a_delta);
		bool SetKeyInterpolation(std::span<const KeyRef> a_keys, Interpolation a_interpolation);
		// Per-property setting. Independent of the sequence-wide one, which overrides it while on.
		bool SetLoopSmoothing(std::span<const PropertyRef> a_refs, bool a_enabled);
		[[nodiscard]] LoopSmoothingInfo GetLoopSmoothing(const PropertyRef& a_ref) const;
		// Per-property setting in the active sequence. Bool properties are always Step, so they're skipped.
		bool SetBezierMode(std::span<const PropertyRef> a_refs, BezierMode a_mode);
		[[nodiscard]] BezierMode GetBezierMode(const PropertyRef& a_ref) const;
		// Edited live values of tracked properties. Sampling (playback, a frame change, baking) overwrites them.
		[[nodiscard]] size_t CountUnkeyedEdits() const;

		// ---- Baking ----
		// The active sequence sampled at a fixed rate through the normal evaluation, for one actor: its final local
		// pose (after hands and IK) at every sample. Each sample is evaluated at its exact time, independent of the
		// game's frame rate, so none are dropped. Bakes in slices, so the UI keeps running.

		struct BakeJob
		{
			ObjectId actor{ kNoObject };
			std::shared_ptr<const OzzSkeleton> skeleton;
			std::unique_ptr<RawOzzAnimation> animation;  // Filled as samples are baked.
			size_t sampleCount{ 0 };
			size_t baked{ 0 };

			// What the bake was started against. Any change to the project fails the bake.
			SequenceId sequence{ kNoSequence };
			uint64_t revision{ 0 };
			double sampleRate{ 30.0 };
		};

		// Samples run every 1 / a_sampleRate seconds from 0, plus a last one at exactly the sequence's end, even when
		// the rate doesn't divide the length.
		[[nodiscard]] static size_t GetBakeSampleCount(int32_t a_frameCount, float a_frameRate, float a_sampleRate);
		[[nodiscard]] std::expected<BakeJob, std::string> BeginBake(ObjectId a_actor, float a_sampleRate) const;
		// Bakes samples until the job is done or a_budget has passed, and returns whether it's done. Leaves the
		// project as it found it: nothing is published to the game or recorded in history. Fails if the project
		// changed since BeginBake.
		std::expected<bool, std::string> ContinueBake(BakeJob& a_job, std::chrono::microseconds a_budget);

	private:
		// Project files are read and built directly, without going through history.
		friend class Serialization::StudioProjectExport;
		friend class Serialization::StudioProjectImport;

		// ---- History ----

		// Groups every change recorded during its lifetime into one commit. Nests; only the outermost scope commits.
		class TransactionScope
		{
		public:
			TransactionScope(ProjectEx& a_project, std::string a_description);
			~TransactionScope();

			TransactionScope(const TransactionScope&) = delete;
			TransactionScope& operator=(const TransactionScope&) = delete;

		private:
			ProjectEx& m_project;
		};

		// ---- Rigs ----

		// A rig as read from its file, with bones resolved against the target actor's skeleton.
		struct RigContents
		{
			ObjectId actor{ kNoObject };  // The id the rig's objects use for its actor.
			// Locators and modifiers, with the rig's ids. Modifiers are in application order. Locators parented to
			// kNoObject are relative to the actor's placement, not the scene center.
			std::vector<std::unique_ptr<SceneObject>> objects;
			std::vector<std::pair<int32_t, LocatorDefaults>> pose;  // Bone local transforms that differ from the rest pose.
			std::map<int32_t, bool> maskOverrides;
		};

		// ---- Core: Lookup ----

		SceneObject* FindObject(ObjectId a_id) { return m_objects.Find(a_id); }
		const SceneObject* FindObject(ObjectId a_id) const { return m_objects.Find(a_id); }
		template <class T>
		T* FindObject(ObjectId a_id)
		{
			return m_objects.Find<T>(a_id);
		}
		template <class T>
		const T* FindObject(ObjectId a_id) const
		{
			return m_objects.Find<T>(a_id);
		}
		ActorObject* FindActorObject(const RE::Actor* a_actor) const { return m_objects.FindActor(a_actor); }
		PropertyPtr FindProperty(const PropertyRef& a_ref);
		PropertyPtr FindProperty(const PropertyRef& a_ref) const;
		bool IsValidParent(const ParentRef& a_parent) const { return m_objects.IsValidParent(a_parent); }
		// The actor a modifier belongs to. kNoObject for objects that aren't modifiers.
		static ObjectId GetModifierActor(const SceneObject& a_object);

		// ---- Core: Sampling ----

		Sequence* FindSequence(SequenceId a_id);
		const Sequence* FindSequence(SequenceId a_id) const;
		Sequence* GetActiveSequence();
		const Sequence* GetActiveSequence() const;
		void SyncPlaybackTime();
		void CancelPlayback();
		void ResampleProperty(const PropertyRef& a_ref);
		void ResampleAll();
		void ResampleUnedited();
		void ResetAllToDefaults();

		// ---- Core: Apply ----

		void Apply(Change& a_change, bool a_forward);
		void ApplyChange(KeyChange& a_change, bool a_forward);
		void ApplyChange(LiveValueChange& a_change, bool a_forward);
		void ApplyChange(FrameChange& a_change, bool a_forward);
		void ApplyChange(ActiveSequenceChange& a_change, bool a_forward);
		void ApplyChange(ObjectLifetimeChange& a_change, bool a_forward);
		void ApplyChange(SequenceLifetimeChange& a_change, bool a_forward);
		void ApplyChange(SequenceSettingsChange& a_change, bool a_forward);
		void ApplyChange(ObjectRenameChange& a_change, bool a_forward);
		void ApplyChange(LocatorParentChange& a_change, bool a_forward);
		void ApplyChange(LocatorDefaultsChange& a_change, bool a_forward);
		void ApplyChange(ActorBindingChange& a_change, bool a_forward);
		void ApplyChange(LoopSmoothingChange& a_change, bool a_forward);
		void ApplyChange(BezierModeChange& a_change, bool a_forward);
		void ApplyChange(ActorPlacementChange& a_change, bool a_forward);
		void ApplyChange(ModifierSettingsChange& a_change, bool a_forward);
		void ApplyChange(BoneMaskChange& a_change, bool a_forward);
		static ActorBinding GetBinding(const ActorObject& a_actor);
		void InsertObject(ObjectSnapshot& a_snapshot);
		void RemoveObject(ObjectSnapshot& a_snapshot);
		void CaptureEdited(const PropertyRef& a_ref, std::vector<DiscardedValue>& a_out);
		void CaptureEditedTracked(std::vector<DiscardedValue>& a_out);
		void CaptureAll(std::vector<DiscardedValue>& a_out);
		void RestoreDiscarded(const std::vector<DiscardedValue>& a_values);

		// ---- Core: History ----

		// Applies a change and adds it to the open edit.
		void Record(Change a_change);
		bool RecordKeyWrite(Sequence& a_sequence, const PropertyRef& a_ref, int32_t a_frame, std::optional<AnyKey> a_key);
		bool RecordLiveValue(const PropertyRef& a_ref, const Value& a_value);
		bool RecordTransform(ElementRef a_ref, const ozz::math::Float4x4& a_local, uint8_t a_mask);
		// SetWorldTransform without its transaction or evaluation: reads the evaluated transforms as they stand.
		bool RecordWorldTransform(ElementRef a_ref, const ozz::math::Float4x4& a_world, uint8_t a_mask);
		void RecordReparent(LocatorObject& a_locator, ParentRef a_parent, bool a_keepWorldTransform);
		bool RecordModifierSettings(ObjectId a_id, ModifierSettings a_settings);
		// Adds a new modifier's locators, then the modifier at the end of a_actor's modifiers. Returns the modifier's id.
		ObjectId RecordModifierBuild(const ActorObject& a_actor, ModifierBuild a_build);

		// ---- Core: Evaluation ----

		template <class T, class F>
		void ForEachObject(F&& a_func)
		{
			m_objects.ForEach<T>(std::forward<F>(a_func));
		}
		void AdvancePlayback(float a_deltaTime);
		static void AppendBakeSample(BakeJob& a_job, const ActorObject& a_actor, float a_time);
		bool HasSelfDependentModifier() const { return m_evaluator.HasSelfDependentModifier(); }
		bool WouldCreateLocatorLoop(ObjectId a_locator, const ParentRef& a_parent) const { return m_objects.WouldCreateLocatorLoop(a_locator, a_parent); }

		// ---- Core: Loading ----

		// Binds a slot being loaded and sets its bones to the actor skeleton's rest pose.
		static std::expected<void, std::string> LoadActorBinding(ActorObject& a_slot, RE::Actor* a_actor);
		// Called once every object and sequence is in place: generates tangents, samples the active sequence at
		// the current frame, then applies the saved live values on top. Leaves an empty, saved history.
		void FinishLoad(std::span<const DiscardedValue> a_liveValues);
		// How a_actor's rig stores one of its modifiers' locators: the parent is kept when it's the actor or another rig
		// locator and is otherwise scene space, measured from the actor's placement. The transform is the live one.
		std::pair<ParentRef, LocatorDefaults> GetRigLocator(const ActorObject& a_actor, const LocatorObject& a_locator, const std::set<ObjectId>& a_rigLocators);
		// One undo step. Creates the rig's objects with new ids, then sets the pose and the bone mask.
		RigResult ApplyRig(ObjectId a_actor, RigContents a_rig, const RigOptions& a_options);
		void ApplyCommit(Commit& a_commit, bool a_forward);

		// Scene
		ObjectStore m_objects;
		ScenePlacement m_sceneCenter;
		ObjectId m_nextObjectId{ 1 };

		// Sequences
		std::vector<Sequence> m_sequences;
		SequenceId m_activeSequence{ kNoSequence };
		SequenceId m_nextSequenceId{ 1 };

		// Transport
		int32_t m_frame{ 0 };
		double m_playbackTime{ 0.0 };                 // Seconds. Allows sampling between frames during playback.
		std::optional<FrameChange> m_playbackChange;  // Captured on play, committed as one step on pause.
		std::pair<int32_t, int32_t> m_loopRange{ 0, 0 };
		bool m_playing{ false };
		std::optional<float> m_bakeFrame;  // Overrides the sample frame while a bake samples.

		// History
		History m_history;
		uint64_t m_revision{ 0 };  // Counts applied changes, undo and redo included.

		// Evaluation. Declared after the scene, which it refers to.
		Evaluator m_evaluator{ m_objects, m_sceneCenter };
	};
}
