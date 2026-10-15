#pragma once
#include "Animation/Studio/SceneObjects.h"
#include "Animation/Studio/Sequence.h"

// Undo history: what a change records, and how changes group into commits. Git-like and linear. Applying a change
// is up to the project; History only keeps the record.
namespace Animation::Studio
{
	// Live values that a resample or reset overwrote, restored when the change that overwrote them is undone.
	struct DiscardedValue
	{
		PropertyRef property;
		Value value;
		bool edited{ false };
	};

	struct KeyChange
	{
		SequenceId sequence{ kNoSequence };
		PropertyRef property;
		int32_t frame{ 0 };
		std::optional<AnyKey> before;
		std::optional<AnyKey> after;
		std::vector<DiscardedValue> discarded;
	};

	struct LiveValueChange
	{
		PropertyRef property;
		Value before;
		Value after;
		bool editedBefore{ false };
	};

	struct FrameChange
	{
		int32_t before{ 0 };
		int32_t after{ 0 };
		std::vector<DiscardedValue> discarded;
	};

	struct ActiveSequenceChange
	{
		SequenceId before{ kNoSequence };
		SequenceId after{ kNoSequence };
		int32_t frameBefore{ 0 };
		int32_t frameAfter{ 0 };
		std::vector<DiscardedValue> discarded;
	};

	// An object out of the scene, with everything that goes with it.
	struct ObjectSnapshot
	{
		ObjectId id{ kNoObject };
		std::unique_ptr<SceneObject> object;  // Null while the object is in the scene.
		size_t index{ 0 };                     // Position in the project's objects.
		size_t modifierIndex{ 0 };             // Position in the owning actor's modifiers, for modifier objects.
		std::vector<std::tuple<SequenceId, PropertyRef, Track>> tracks;
		std::vector<std::pair<SequenceId, PropertyRef>> loopSmoothedProperties;
		std::vector<std::tuple<SequenceId, PropertyRef, BezierMode>> bezierModes;
	};

	struct ObjectLifetimeChange
	{
		ObjectSnapshot snapshot;
		bool existsAfter{ false };
	};

	struct SequenceLifetimeChange
	{
		SequenceId id{ kNoSequence };
		Sequence sequence;  // Only valid while the sequence is not in the project.
		size_t index{ 0 };  // Position in the project's sequences.
		bool existsAfter{ false };
	};

	struct SequenceSettingsChange
	{
		SequenceId id{ kNoSequence };
		SequenceSettings before;
		SequenceSettings after;
	};

	struct ObjectRenameChange
	{
		ObjectId id{ kNoObject };
		std::string before;
		std::string after;
	};

	struct LocatorParentChange
	{
		ObjectId id{ kNoObject };
		ParentRef before;
		ParentRef after;
	};

	// Defaults are relative to the parent, so a reparent that keeps the world transform rewrites them too.
	struct LocatorDefaultsChange
	{
		ObjectId id{ kNoObject };
		LocatorDefaults before;
		LocatorDefaults after;
	};

	struct ActorBinding
	{
		RE::NiPointer<RE::Actor> actor;
		std::shared_ptr<const OzzSkeleton> skeleton;
		std::string skeletonId;
	};

	struct ActorBindingChange
	{
		ObjectId id{ kNoObject };
		ActorBinding before;
		ActorBinding after;
	};

	struct LoopSmoothingChange
	{
		SequenceId sequence{ kNoSequence };
		PropertyRef property;
		bool before{ false };
		bool after{ false };
	};

	struct BezierModeChange
	{
		SequenceId sequence{ kNoSequence };
		PropertyRef property;
		BezierMode before{ BezierMode::kLocal };
		BezierMode after{ BezierMode::kLocal };
	};

	struct ActorPlacementChange
	{
		ObjectId id{ kNoObject };
		ScenePlacement before;
		ScenePlacement after;
	};

	struct ModifierSettingsChange
	{
		ObjectId id{ kNoObject };
		ModifierSettings before;
		ModifierSettings after;
	};

	struct BoneMaskChange
	{
		ObjectId id{ kNoObject };
		std::map<int32_t, bool> before;
		std::map<int32_t, bool> after;
	};

	using Change = std::variant<KeyChange, LiveValueChange, FrameChange, ActiveSequenceChange, ObjectLifetimeChange, SequenceLifetimeChange,
		SequenceSettingsChange, ObjectRenameChange, LocatorParentChange, ActorBindingChange, LoopSmoothingChange, ActorPlacementChange,
		ModifierSettingsChange, LocatorDefaultsChange, BoneMaskChange, BezierModeChange>;

	struct Commit
	{
		std::string description;
		std::vector<Change> changes;
	};

	class History
	{
	public:
		// ---- Transactions ----
		// Transactions nest; only the outermost one commits. Changes made outside one aren't recorded.

		void Begin(std::string a_description);
		// Closing the outermost transaction drops its no-op changes and commits what's left, if anything. Committing
		// cuts off whatever was undone, and trims the oldest commits beyond the limit.
		void End();
		[[nodiscard]] bool IsOpen() const { return m_depth > 0; }
		// The open transaction's changes so far, or null.
		[[nodiscard]] Commit* GetOpen() { return m_open ? std::addressof(*m_open) : nullptr; }
		// Drops the open transaction's changes, once the caller has reverted them. Its scopes stay open.
		void ClearOpen();
		// Adds an applied change to the open transaction, merging it into an earlier one where that's safe.
		void Push(Change a_change);

		// ---- Commits ----

		// The commit to apply backward, with the head moved before it. Null when there's nothing to undo.
		[[nodiscard]] Commit* StepBack();
		// The commit to apply forward, with the head moved past it. Null when there's nothing to redo.
		[[nodiscard]] Commit* StepForward();
		[[nodiscard]] size_t GetSize() const { return m_commits.size(); }
		[[nodiscard]] size_t GetHead() const { return m_head; }  // Number of applied commits.
		[[nodiscard]] std::string_view GetDescription(size_t a_index) const;
		void Clear();

		[[nodiscard]] bool HasUnsavedChanges() const { return !m_savedHead || *m_savedHead != m_head; }
		void MarkSaved() { m_savedHead = m_head; }

	private:
		// Where in the open transaction the latest live and key changes are, so merging a change doesn't scan the
		// transaction. Bulk key edits record thousands of changes in one.
		struct OpenIndex
		{
			std::optional<size_t> barrier;              // The latest change that's neither a key nor a live change.
			std::map<PropertyRef, size_t> lastLive;     // Latest live change per property.
			std::map<PropertyRef, size_t> lastKey;      // Latest key change per property, any sequence and frame.
			std::map<std::tuple<SequenceId, PropertyRef, int32_t>, size_t> lastKeyAt;  // Per sequence, property and frame.
		};

		// Merges a_change into an earlier change of the open transaction where that's safe.
		bool TryCoalesce(Change& a_change);
		void IndexLast();  // Adds the open transaction's last change to the index.
		static bool IsNoOp(const Change& a_change);
		static void AppendDiscarded(std::vector<DiscardedValue>& a_target, const std::vector<DiscardedValue>& a_source);

		std::vector<Commit> m_commits;
		size_t m_head{ 0 };
		std::optional<Commit> m_open;
		OpenIndex m_index;
		uint32_t m_depth{ 0 };
		std::optional<size_t> m_savedHead{ 0 };  // Empty once the saved state is trimmed away or branched off from.
		size_t m_maxCommits{ 256 };
	};
}
