#include "ProjectEx.h"

namespace Animation::Studio
{
	// ---- Core: Sampling ----

	Sequence* ProjectEx::FindSequence(SequenceId a_id)
	{
		const auto iter = std::ranges::find(m_sequences, a_id, &Sequence::id);
		return iter != m_sequences.end() ? std::addressof(*iter) : nullptr;
	}

	const Sequence* ProjectEx::FindSequence(SequenceId a_id) const
	{
		const auto iter = std::ranges::find(m_sequences, a_id, &Sequence::id);
		return iter != m_sequences.end() ? std::addressof(*iter) : nullptr;
	}

	Sequence* ProjectEx::GetActiveSequence()
	{
		return FindSequence(m_activeSequence);
	}

	const Sequence* ProjectEx::GetActiveSequence() const
	{
		return FindSequence(m_activeSequence);
	}

	float ProjectEx::GetSampleFrame() const
	{
		if (m_bakeFrame) return *m_bakeFrame;
		if (m_playing) {
			if (const auto sequence = GetActiveSequence()) return static_cast<float>(m_playbackTime * sequence->frameRate);
		}
		return static_cast<float>(m_frame);
	}

	void ProjectEx::SyncPlaybackTime()
	{
		const auto sequence = GetActiveSequence();
		m_playbackTime = sequence && sequence->frameRate > 0.0f ? m_frame / static_cast<double>(sequence->frameRate) : 0.0;
	}

	void ProjectEx::CancelPlayback()
	{
		if (!m_playing) return;
		m_playing = false;
		auto change = std::move(*m_playbackChange);
		m_playbackChange.reset();
		// Back to the starting frame, restoring the edits that playback discarded.
		ApplyChange(change, false);
		m_evaluator.MarkSolveNeeded();
	}

	void ProjectEx::ResampleProperty(const PropertyRef& a_ref)
	{
		const auto sequence = GetActiveSequence();
		if (!sequence) return;
		const auto track = sequence->FindTrack(a_ref);
		if (!track) return;
		SetLiveValue(FindProperty(a_ref), sequence->Sample(a_ref, *track, GetSampleFrame()), false);
	}

	void ProjectEx::ResampleAll()
	{
		const auto sequence = GetActiveSequence();
		if (!sequence) return;
		const float frame = GetSampleFrame();
		for (const auto& [ref, track] : sequence->tracks) {
			SetLiveValue(FindProperty(ref), sequence->Sample(ref, track, frame), false);
		}
	}

	void ProjectEx::ResampleUnedited()
	{
		// For changes that reshape curves without moving the playhead: pending edits are kept, so there's
		// nothing to capture, and the change stays exactly reversible.
		const auto sequence = GetActiveSequence();
		if (!sequence) return;
		const float frame = GetSampleFrame();
		for (const auto& [ref, track] : sequence->tracks) {
			const auto property = FindProperty(ref);
			if (!Studio::IsEdited(property)) SetLiveValue(property, sequence->Sample(ref, track, frame), false);
		}
	}

	void ProjectEx::ResetAllToDefaults()
	{
		for (auto& object : m_objects) {
			object->ForEachProperty([](uint16_t, PropertyId, PropertyPtr a_ptr) { ResetLiveValue(a_ptr); });
		}
	}

	// ---- Core: Playback ----

	void ProjectEx::AdvancePlayback(float a_deltaTime)
	{
		const auto sequence = GetActiveSequence();
		if (!sequence || !(sequence->frameRate > 0.0f)) return;
		const double frameRate = sequence->frameRate;
		const double start = m_loopRange.first / frameRate;
		const double end = m_loopRange.second / frameRate;

		m_playbackTime += a_deltaTime;
		if (m_playbackTime < start || m_playbackTime >= end) {
			const double length = end - start;
			m_playbackTime = length > 0.0 ? start + std::fmod((std::max)(m_playbackTime - start, 0.0), length) : start;
		}
		m_frame = std::clamp(static_cast<int32_t>(std::floor(m_playbackTime * frameRate)), 0, sequence->frameCount);
		ResampleAll();
		m_evaluator.MarkSolveNeeded();
	}

	// ---- Transport ----

	int32_t ProjectEx::GetFrame() const
	{
		return m_frame;
	}

	bool ProjectEx::SetFrame(int32_t a_frame)
	{
		const auto sequence = GetActiveSequence();
		if (!sequence) return false;
		TransactionScope transaction(*this, "Change frame");
		Pause();
		a_frame = std::clamp(a_frame, 0, sequence->frameCount);
		if (a_frame == m_frame) return false;
		Record(FrameChange{ m_frame, a_frame, {} });
		return true;
	}

	void ProjectEx::Play(std::optional<std::pair<int32_t, int32_t>> a_loopRange)
	{
		const auto sequence = GetActiveSequence();
		if (!sequence) return;
		m_loopRange = { 0, sequence->frameCount };
		if (a_loopRange) {
			const int32_t start = std::clamp(a_loopRange->first, 0, sequence->frameCount);
			const int32_t end = std::clamp(a_loopRange->second, 0, sequence->frameCount);
			if (start < end) m_loopRange = { start, end };
		}
		if (m_playing) return;

		m_playbackChange = FrameChange{ m_frame, m_frame, {} };
		CaptureEditedTracked(m_playbackChange->discarded);
		SyncPlaybackTime();
		m_playing = true;
	}

	void ProjectEx::Pause()
	{
		if (!m_playing) return;
		const auto sequence = GetActiveSequence();
		const int32_t frame = std::clamp(static_cast<int32_t>(std::lround(GetSampleFrame())), 0, sequence ? sequence->frameCount : 0);
		m_playing = false;
		m_frame = frame;
		SyncPlaybackTime();
		ResampleAll();
		m_evaluator.MarkSolveNeeded();

		// The frame change is already live; it only needs to enter history.
		auto change = std::move(*m_playbackChange);
		m_playbackChange.reset();
		change.after = frame;
		TransactionScope transaction(*this, "Playback");
		m_history.Push(std::move(change));
	}

	bool ProjectEx::IsPlaying() const
	{
		return m_playing;
	}

	// ---- Sequences ----

	std::vector<ProjectEx::SequenceInfo> ProjectEx::GetSequences() const
	{
		std::vector<SequenceInfo> result;
		result.reserve(m_sequences.size());
		for (const auto& sequence : m_sequences) {
			result.push_back({ sequence.id, sequence.name, sequence.frameRate, sequence.frameCount, sequence.loopSmoothing });
		}
		return result;
	}

	ProjectEx::SequenceId ProjectEx::GetActiveSequenceId() const
	{
		return m_activeSequence;
	}

	ProjectEx::SequenceId ProjectEx::AddSequence(std::string a_name, float a_frameRate, int32_t a_frameCount)
	{
		if (!(a_frameRate > 0.0f) || a_frameCount < 1) return kNoSequence;
		const SequenceId id = m_nextSequenceId++;
		TransactionScope transaction(*this, "Add sequence");
		Record(SequenceLifetimeChange{
			.id = id,
			.sequence = Sequence{ .id = id, .name = std::move(a_name), .frameRate = a_frameRate, .frameCount = a_frameCount },
			.index = m_sequences.size(),
			.existsAfter = true });
		return id;
	}

	ProjectEx::SequenceId ProjectEx::DuplicateSequence(SequenceId a_id)
	{
		const auto source = FindSequence(a_id);
		if (!source) return kNoSequence;
		Sequence copy = *source;
		copy.id = m_nextSequenceId++;
		copy.name += " Copy";
		const SequenceId id = copy.id;
		const auto index = static_cast<size_t>(std::distance(m_sequences.data(), source)) + 1;
		TransactionScope transaction(*this, "Duplicate sequence");
		Pause();
		// Live values belong to the active sequence, so only its copy can take them. The switch resets and resamples;
		// whatever then differs from the source is an unkeyed edit in the copy as well.
		std::vector<DiscardedValue> live;
		if (a_id == m_activeSequence) CaptureAll(live);
		Record(SequenceLifetimeChange{ .id = id, .sequence = std::move(copy), .index = index, .existsAfter = true });
		SetActiveSequence(id);
		for (const auto& value : live) {
			if (GetLiveValue(FindProperty(value.property)) != value.value) RecordLiveValue(value.property, value.value);
		}
		return id;
	}

	bool ProjectEx::RemoveSequence(SequenceId a_id)
	{
		const auto iter = std::ranges::find(m_sequences, a_id, &Sequence::id);
		if (iter == m_sequences.end() || m_sequences.size() <= 1) return false;
		TransactionScope transaction(*this, "Remove sequence");
		if (a_id == m_activeSequence) {
			const auto neighbor = std::next(iter) != m_sequences.end() ? std::next(iter) : std::prev(iter);
			SetActiveSequence(neighbor->id);
		}
		Record(SequenceLifetimeChange{ .id = a_id, .existsAfter = false });
		return true;
	}

	bool ProjectEx::SetActiveSequence(SequenceId a_id)
	{
		const auto sequence = FindSequence(a_id);
		if (!sequence || a_id == m_activeSequence) return false;
		TransactionScope transaction(*this, "Switch sequence");
		Pause();
		Record(ActiveSequenceChange{
			.before = m_activeSequence,
			.after = a_id,
			.frameBefore = m_frame,
			.frameAfter = std::clamp(m_frame, 0, sequence->frameCount) });
		return true;
	}

	bool ProjectEx::RenameSequence(SequenceId a_id, std::string a_name)
	{
		const auto sequence = FindSequence(a_id);
		if (!sequence || sequence->name == a_name) return false;
		auto settings = sequence->GetSettings();
		settings.name = std::move(a_name);
		TransactionScope transaction(*this, "Rename sequence");
		Record(SequenceSettingsChange{ a_id, sequence->GetSettings(), std::move(settings) });
		return true;
	}

	bool ProjectEx::SetSequenceFrameCount(SequenceId a_id, int32_t a_frameCount)
	{
		const auto sequence = FindSequence(a_id);
		if (!sequence || a_frameCount < 1 || sequence->frameCount == a_frameCount) return false;
		const bool active = a_id == m_activeSequence;
		auto settings = sequence->GetSettings();
		settings.frameCount = a_frameCount;
		TransactionScope transaction(*this, "Change sequence length");
		if (active) Pause();
		Record(SequenceSettingsChange{ a_id, sequence->GetSettings(), std::move(settings) });
		if (active && m_frame > a_frameCount) Record(FrameChange{ m_frame, a_frameCount, {} });
		return true;
	}

	bool ProjectEx::SetSequenceFrameRate(SequenceId a_id, float a_frameRate, bool a_rescaleKeys)
	{
		const auto sequence = FindSequence(a_id);
		if (!sequence || !(a_frameRate > 0.0f) || sequence->frameRate == a_frameRate) return false;
		const bool active = a_id == m_activeSequence;
		const double scale = static_cast<double>(a_frameRate) / sequence->frameRate;
		const auto rescale = [&](int32_t a_frame) { return static_cast<int32_t>(std::lround(a_frame * scale)); };

		auto settings = sequence->GetSettings();
		settings.frameRate = a_frameRate;
		TransactionScope transaction(*this, "Change frame rate");
		if (active) Pause();

		if (a_rescaleKeys) {
			settings.frameCount = (std::max)(1, rescale(sequence->frameCount));
			std::vector<std::pair<PropertyRef, std::vector<AnyKey>>> tracks;
			for (const auto& [ref, track] : sequence->tracks) tracks.emplace_back(ref, track.GetKeys());
			// Remove every key before writing any, so keys moving onto each other's old frames can't collide.
			for (const auto& [ref, keys] : tracks) {
				for (const auto& key : keys) RecordKeyWrite(*sequence, ref, GetKeyFrame(key), std::nullopt);
			}
			for (auto& [ref, keys] : tracks) {
				for (auto& key : keys) RecordKeyWrite(*sequence, ref, rescale(GetKeyFrame(key)), std::move(key));
			}
		}

		Record(SequenceSettingsChange{ a_id, sequence->GetSettings(), settings });
		if (active) {
			const int32_t frame = std::clamp(a_rescaleKeys ? rescale(m_frame) : m_frame, 0, settings.frameCount);
			if (frame != m_frame) Record(FrameChange{ m_frame, frame, {} });
		}
		return true;
	}

	bool ProjectEx::SetSequenceLoopSmoothing(SequenceId a_id, bool a_enabled)
	{
		const auto sequence = FindSequence(a_id);
		if (!sequence || sequence->loopSmoothing == a_enabled) return false;
		auto settings = sequence->GetSettings();
		settings.loopSmoothing = a_enabled;
		TransactionScope transaction(*this, a_enabled ? "Enable sequence loop smoothing" : "Disable sequence loop smoothing");
		Record(SequenceSettingsChange{ a_id, sequence->GetSettings(), std::move(settings) });
		return true;
	}

	// ---- Properties ----

	std::optional<ProjectEx::Value> ProjectEx::GetValue(const PropertyRef& a_ref) const
	{
		const auto property = FindProperty(a_ref);
		if (std::holds_alternative<std::monostate>(property)) return std::nullopt;
		return GetLiveValue(property);
	}

	bool ProjectEx::SetValue(const PropertyRef& a_ref, const Value& a_value)
	{
		if (!MatchesType(FindProperty(a_ref), a_value.index())) return false;
		TransactionScope transaction(*this, "Edit value");
		Pause();
		return RecordLiveValue(a_ref, a_value);
	}

	bool ProjectEx::ResetValue(const PropertyRef& a_ref)
	{
		const auto property = FindProperty(a_ref);
		if (std::holds_alternative<std::monostate>(property)) return false;
		return SetValue(a_ref, GetDefaultValue(property));
	}

	bool ProjectEx::IsEdited(const PropertyRef& a_ref) const
	{
		return Studio::IsEdited(FindProperty(a_ref));
	}

	// ---- Keys ----

	bool ProjectEx::IsTracked(const PropertyRef& a_ref) const
	{
		const auto sequence = GetActiveSequence();
		return sequence && sequence->tracks.contains(a_ref);
	}

	std::vector<ProjectEx::PropertyRef> ProjectEx::GetTrackedProperties() const
	{
		const auto sequence = GetActiveSequence();
		if (!sequence) return {};
		const auto keys = sequence->tracks | std::views::keys;
		return std::vector<PropertyRef>(keys.begin(), keys.end());
	}

	std::vector<int32_t> ProjectEx::GetKeyFrames(const PropertyRef& a_ref) const
	{
		const auto sequence = GetActiveSequence();
		if (!sequence) return {};
		const auto track = sequence->FindTrack(a_ref);
		return track ? track->GetKeyFrames() : std::vector<int32_t>{};
	}

	std::optional<ProjectEx::AnyKey> ProjectEx::GetKey(const KeyRef& a_ref) const
	{
		const auto sequence = GetActiveSequence();
		return sequence ? sequence->GetKey(a_ref.property, a_ref.frame) : std::nullopt;
	}

	bool ProjectEx::KeyCurrentValue(std::span<const PropertyRef> a_refs, Interpolation a_interpolation)
	{
		const auto sequence = GetActiveSequence();
		if (!sequence) return false;
		TransactionScope transaction(*this, "Place keyframe");
		Pause();
		bool changed = false;
		for (const auto& ref : a_refs) {
			const auto property = FindProperty(ref);
			if (std::holds_alternative<std::monostate>(property)) continue;
			const auto live = GetLiveValue(property);
			auto key = MakeKey(live, m_frame, a_interpolation);
			if (auto existing = sequence->GetKey(ref, m_frame); existing && existing->index() == key.index()) {
				key = std::move(*existing);
				std::visit([&](auto& a_key) { a_key.value = std::get<decltype(a_key.value)>(live); }, key);
			}
			changed |= RecordKeyWrite(*sequence, ref, m_frame, std::move(key));
		}
		return changed;
	}

	bool ProjectEx::SetKeys(std::span<const KeyData> a_keys)
	{
		const auto sequence = GetActiveSequence();
		if (!sequence) return false;
		TransactionScope transaction(*this, "Set keyframes");
		bool changed = false;
		for (const auto& data : a_keys) {
			const int32_t frame = GetKeyFrame(data.key);
			if (frame < 0 || !MatchesType(FindProperty(data.property), data.key.index())) continue;
			changed |= RecordKeyWrite(*sequence, data.property, frame, data.key);
		}
		return changed;
	}

	bool ProjectEx::DeleteKeys(std::span<const KeyRef> a_keys)
	{
		const auto sequence = GetActiveSequence();
		if (!sequence) return false;
		TransactionScope transaction(*this, "Delete keyframes");
		bool changed = false;
		for (const auto& ref : a_keys) {
			changed |= RecordKeyWrite(*sequence, ref.property, ref.frame, std::nullopt);
		}
		return changed;
	}

	std::optional<std::vector<ProjectEx::KeyRef>> ProjectEx::MoveKeys(std::span<const KeyRef> a_keys, int32_t a_delta)
	{
		const auto sequence = GetActiveSequence();
		if (!sequence) return std::nullopt;

		std::vector<KeyRef> refs(a_keys.begin(), a_keys.end());
		std::ranges::sort(refs);
		refs.erase(std::ranges::unique(refs).begin(), refs.end());

		std::vector<std::pair<KeyRef, AnyKey>> moving;
		for (const auto& ref : refs) {
			if (auto key = sequence->GetKey(ref.property, ref.frame)) moving.emplace_back(ref, std::move(*key));
		}
		if (moving.empty()) return std::nullopt;

		const int32_t earliest = (std::ranges::min)(moving | std::views::transform([](const auto& a_entry) { return a_entry.first.frame; }));
		a_delta = (std::max)(a_delta, -earliest);

		std::vector<KeyRef> result;
		result.reserve(moving.size());
		TransactionScope transaction(*this, "Move keyframes");
		// Remove every moving key before writing any, so moved keys only overwrite keys that aren't moving.
		if (a_delta != 0) {
			for (const auto& [ref, key] : moving) RecordKeyWrite(*sequence, ref.property, ref.frame, std::nullopt);
		}
		for (auto& [ref, key] : moving) {
			const KeyRef moved{ ref.property, ref.frame + a_delta };
			if (a_delta != 0) RecordKeyWrite(*sequence, moved.property, moved.frame, std::move(key));
			result.push_back(moved);
		}
		return result;
	}

	bool ProjectEx::SetKeyInterpolation(std::span<const KeyRef> a_keys, Interpolation a_interpolation)
	{
		const auto sequence = GetActiveSequence();
		if (!sequence) return false;
		TransactionScope transaction(*this, "Set interpolation");
		bool changed = false;
		for (const auto& ref : a_keys) {
			auto key = sequence->GetKey(ref.property, ref.frame);
			if (!key) continue;
			std::visit([&](auto& a_key) {
				if constexpr (!std::is_same_v<decltype(a_key.value), bool>) a_key.interpolation = a_interpolation;
			}, *key);
			changed |= RecordKeyWrite(*sequence, ref.property, ref.frame, std::move(key));
		}
		return changed;
	}

	bool ProjectEx::SetLoopSmoothing(std::span<const PropertyRef> a_refs, bool a_enabled)
	{
		const auto sequence = GetActiveSequence();
		if (!sequence) return false;
		TransactionScope transaction(*this, a_enabled ? "Enable cyclic" : "Disable cyclic");
		bool changed = false;
		for (const auto& ref : a_refs) {
			if (std::holds_alternative<std::monostate>(FindProperty(ref))) continue;
			const bool current = sequence->loopSmoothedProperties.contains(ref);
			if (current == a_enabled) continue;
			Record(LoopSmoothingChange{ sequence->id, ref, current, a_enabled });
			changed = true;
		}
		return changed;
	}

	ProjectEx::LoopSmoothingInfo ProjectEx::GetLoopSmoothing(const PropertyRef& a_ref) const
	{
		const auto sequence = GetActiveSequence();
		if (!sequence) return {};
		LoopSmoothingInfo info{ sequence->loopSmoothedProperties.contains(a_ref), sequence->loopSmoothing };
		if (const auto track = sequence->tracks.find(a_ref); track != sequence->tracks.end() && (info.property || info.sequence)) {
			info.applied = track->second.HasLoopRange(sequence->frameCount);
		}
		return info;
	}

	size_t ProjectEx::CountUnkeyedEdits() const
	{
		const auto sequence = GetActiveSequence();
		if (!sequence) return 0;
		return static_cast<size_t>(std::ranges::count_if(sequence->tracks | std::views::keys, [&](const PropertyRef& a_ref) { return Studio::IsEdited(FindProperty(a_ref)); }));
	}
}
