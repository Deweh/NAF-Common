#include "Sequence.h"

namespace Animation::Studio
{
	SequenceSettings Sequence::GetSettings() const
	{
		return { name, frameRate, frameCount, loopSmoothing };
	}

	void Sequence::ApplySettings(const SequenceSettings& a_settings)
	{
		name = a_settings.name;
		frameRate = a_settings.frameRate;
		frameCount = a_settings.frameCount;
		loopSmoothing = a_settings.loopSmoothing;
		// Loop smoothing depends on both the length and the sequence-wide setting.
		RegenerateTangents();
	}

	std::optional<int32_t> Sequence::GetLoopFrameCount(const PropertyRef& a_ref) const
	{
		const bool enabled = loopSmoothing || loopSmoothedProperties.contains(a_ref);
		return enabled ? std::optional{ frameCount } : std::nullopt;
	}

	BezierMode Sequence::GetBezierMode(const PropertyRef& a_ref) const
	{
		const auto iter = bezierModes.find(a_ref);
		return iter != bezierModes.end() ? iter->second : BezierMode::kLocal;
	}

	Track* Sequence::FindTrack(const PropertyRef& a_ref)
	{
		const auto iter = tracks.find(a_ref);
		return iter != tracks.end() ? std::addressof(iter->second) : nullptr;
	}

	const Track* Sequence::FindTrack(const PropertyRef& a_ref) const
	{
		const auto iter = tracks.find(a_ref);
		return iter != tracks.end() ? std::addressof(iter->second) : nullptr;
	}

	Value Sequence::Sample(const PropertyRef& a_ref, const Track& a_track, float a_frame) const
	{
		return a_track.Sample(a_frame, GetLoopFrameCount(a_ref));
	}

	std::optional<AnyKey> Sequence::GetKey(const PropertyRef& a_ref, int32_t a_frame) const
	{
		const auto track = FindTrack(a_ref);
		return track ? track->GetKey(a_frame) : std::nullopt;
	}

	void Sequence::WriteKey(const PropertyRef& a_ref, int32_t a_frame, const std::optional<AnyKey>& a_key)
	{
		auto track = tracks.find(a_ref);
		if (!a_key) {
			if (track == tracks.end()) return;
			track->second.WriteKey(a_frame, std::nullopt, GetLoopFrameCount(a_ref), GetBezierMode(a_ref));
			if (track->second.IsEmpty()) tracks.erase(track);
			return;
		}
		if (track == tracks.end()) track = tracks.emplace(a_ref, MakeEmptyTrack(*a_key)).first;
		track->second.WriteKey(a_frame, a_key, GetLoopFrameCount(a_ref), GetBezierMode(a_ref));
	}

	void Sequence::RegenerateTangents(const PropertyRef& a_ref)
	{
		if (const auto track = FindTrack(a_ref)) track->RegenerateTangents(GetLoopFrameCount(a_ref), GetBezierMode(a_ref));
	}

	void Sequence::RegenerateTangents()
	{
		for (auto& [ref, track] : tracks) track.RegenerateTangents(GetLoopFrameCount(ref), GetBezierMode(ref));
	}
}
