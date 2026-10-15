#pragma once
#include "Animation/Studio/Curve.h"

namespace Animation::Studio
{
	// The settings a SequenceSettingsChange swaps.
	struct SequenceSettings
	{
		std::string name;
		float frameRate{ 30.0f };
		int32_t frameCount{ 30 };
		bool loopSmoothing{ false };

		bool operator==(const SequenceSettings&) const = default;
	};

	// A set of tracks on one timeline. frameCount is the duration, so frames run [0, frameCount].
	struct Sequence
	{
		SequenceId id{ kNoSequence };
		std::string name;
		float frameRate{ 30.0f };
		int32_t frameCount{ 30 };
		bool loopSmoothing{ false };
		std::map<PropertyRef, Track> tracks;
		// Kept apart from tracks, which are erased with their last key; moving or rescaling every key of a track
		// removes and rewrites them, and must not lose the setting.
		std::set<PropertyRef> loopSmoothedProperties;
		// Kept apart from tracks for the same reason. Only properties that aren't Local.
		std::map<PropertyRef, BezierMode> bezierModes;

		[[nodiscard]] SequenceSettings GetSettings() const;
		void ApplySettings(const SequenceSettings& a_settings);  // Regenerates every track's tangents.

		// frameCount when a_ref's curve is cyclic (for the property or the whole sequence), otherwise nullopt.
		[[nodiscard]] std::optional<int32_t> GetLoopFrameCount(const PropertyRef& a_ref) const;
		[[nodiscard]] BezierMode GetBezierMode(const PropertyRef& a_ref) const;
		[[nodiscard]] Track* FindTrack(const PropertyRef& a_ref);
		[[nodiscard]] const Track* FindTrack(const PropertyRef& a_ref) const;
		[[nodiscard]] Value Sample(const PropertyRef& a_ref, const Track& a_track, float a_frame) const;
		[[nodiscard]] std::optional<AnyKey> GetKey(const PropertyRef& a_ref, int32_t a_frame) const;
		// Writes or erases (a_key empty) one key. Creates the track for a property's first key and removes it with its last.
		void WriteKey(const PropertyRef& a_ref, int32_t a_frame, const std::optional<AnyKey>& a_key);
		// After a change to a_ref's cyclic setting or Bezier mode, or to the sequence's cyclic setting.
		void RegenerateTangents(const PropertyRef& a_ref);
		void RegenerateTangents();
	};
}
