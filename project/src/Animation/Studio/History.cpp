#include "History.h"

namespace Animation::Studio
{
	void History::Begin(std::string a_description)
	{
		if (m_depth++ == 0) {
			m_open = Commit{ std::move(a_description), {} };
			m_index = {};
		}
	}

	void History::End()
	{
		assert(m_depth > 0 && m_open);
		if (m_depth == 0 || --m_depth > 0) return;

		auto commit = std::move(*m_open);
		m_open.reset();
		m_index = {};
		std::erase_if(commit.changes, IsNoOp);
		if (commit.changes.empty()) return;

		m_commits.erase(m_commits.begin() + m_head, m_commits.end());
		if (m_savedHead && *m_savedHead > m_head) m_savedHead.reset();
		m_commits.push_back(std::move(commit));
		++m_head;

		if (m_commits.size() > m_maxCommits) {
			const size_t excess = m_commits.size() - m_maxCommits;
			m_commits.erase(m_commits.begin(), m_commits.begin() + excess);
			m_head -= excess;
			if (m_savedHead) {
				if (*m_savedHead < excess) m_savedHead.reset();
				else *m_savedHead -= excess;
			}
		}
	}

	void History::Push(Change a_change)
	{
		assert(m_open);
		if (!m_open) return;
		auto& changes = m_open->changes;
		if (TryCoalesce(a_change)) return;
		changes.push_back(std::move(a_change));
		IndexLast();
	}

	Commit* History::StepBack()
	{
		return m_head > 0 ? std::addressof(m_commits[--m_head]) : nullptr;
	}

	Commit* History::StepForward()
	{
		return m_head < m_commits.size() ? std::addressof(m_commits[m_head++]) : nullptr;
	}

	std::string_view History::GetDescription(size_t a_index) const
	{
		return a_index < m_commits.size() ? std::string_view{ m_commits[a_index].description } : std::string_view{};
	}

	void History::Clear()
	{
		m_commits.clear();
		m_head = 0;
	}

	bool History::TryCoalesce(Change& a_change)
	{
		auto& changes = m_open->changes;

		// Frame changes only merge with an immediately preceding frame change. Anything in between
		// (e.g. a live edit) would be restored in the wrong order on undo.
		if (const auto frame = std::get_if<FrameChange>(&a_change)) {
			const auto previous = changes.empty() ? nullptr : std::get_if<FrameChange>(&changes.back());
			if (!previous) return false;
			previous->after = frame->after;
			AppendDiscarded(previous->discarded, frame->discarded);
			return true;
		}

		// Live and key changes merge into the most recent change with the same target, as long as nothing in between
		// could have affected that property: a key change to it (for a live change), a live change to it (for a key
		// change), or any other kind of change. The index finds what scanning back from the end would.
		const auto latest = [](const auto& a_map, const auto& a_key) -> std::optional<size_t> {
			const auto iter = a_map.find(a_key);
			return iter != a_map.end() ? std::optional{ iter->second } : std::nullopt;
		};
		const auto after = [](std::optional<size_t> a_index, std::optional<size_t> a_other) { return !a_other || *a_index > *a_other; };

		if (const auto live = std::get_if<LiveValueChange>(&a_change)) {
			const auto candidate = latest(m_index.lastLive, live->property);
			if (!candidate || !after(candidate, m_index.barrier) || !after(candidate, latest(m_index.lastKey, live->property))) return false;
			std::get<LiveValueChange>(changes[*candidate]).after = live->after;
			return true;
		}

		if (const auto key = std::get_if<KeyChange>(&a_change)) {
			const auto candidate = latest(m_index.lastKeyAt, std::tuple{ key->sequence, key->property, key->frame });
			if (!candidate || !after(candidate, m_index.barrier) || !after(candidate, latest(m_index.lastLive, key->property))) return false;
			auto& previous = std::get<KeyChange>(changes[*candidate]);
			previous.after = std::move(key->after);
			AppendDiscarded(previous.discarded, key->discarded);
			return true;
		}

		return false;
	}

	void History::IndexLast()
	{
		const size_t index = m_open->changes.size() - 1;
		const auto& change = m_open->changes.back();
		if (const auto live = std::get_if<LiveValueChange>(&change)) {
			m_index.lastLive[live->property] = index;
		} else if (const auto key = std::get_if<KeyChange>(&change)) {
			m_index.lastKey[key->property] = index;
			m_index.lastKeyAt[{ key->sequence, key->property, key->frame }] = index;
		} else {
			m_index.barrier = index;
		}
	}

	void History::ClearOpen()
	{
		if (!m_open) return;
		m_open->changes.clear();
		m_index = {};
	}

	bool History::IsNoOp(const Change& a_change)
	{
		if (const auto key = std::get_if<KeyChange>(&a_change)) return key->before == key->after && key->discarded.empty();
		if (const auto live = std::get_if<LiveValueChange>(&a_change)) return live->before == live->after && live->editedBefore;
		if (const auto frame = std::get_if<FrameChange>(&a_change)) return frame->before == frame->after && frame->discarded.empty();
		if (const auto settings = std::get_if<SequenceSettingsChange>(&a_change)) return settings->before == settings->after;
		if (const auto rename = std::get_if<ObjectRenameChange>(&a_change)) return rename->before == rename->after;
		if (const auto parent = std::get_if<LocatorParentChange>(&a_change)) return parent->before == parent->after;
		if (const auto defaults = std::get_if<LocatorDefaultsChange>(&a_change)) return defaults->before == defaults->after;
		if (const auto loop = std::get_if<LoopSmoothingChange>(&a_change)) return loop->before == loop->after;
		if (const auto placement = std::get_if<ActorPlacementChange>(&a_change)) return placement->before == placement->after;
		if (const auto settings = std::get_if<ModifierSettingsChange>(&a_change)) return settings->before == settings->after;
		if (const auto mask = std::get_if<BoneMaskChange>(&a_change)) return mask->before == mask->after;
		if (const auto binding = std::get_if<ActorBindingChange>(&a_change)) {
			return binding->before.actor.get() == binding->after.actor.get() && binding->before.skeleton == binding->after.skeleton &&
			       binding->before.skeletonId == binding->after.skeletonId;
		}
		return false;
	}

	void History::AppendDiscarded(std::vector<DiscardedValue>& a_target, const std::vector<DiscardedValue>& a_source)
	{
		// The earliest captured value of a property is the one undo must restore.
		for (const auto& value : a_source) {
			if (std::ranges::find(a_target, value.property, &DiscardedValue::property) == a_target.end()) a_target.push_back(value);
		}
	}
}
