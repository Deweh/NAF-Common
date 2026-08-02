#pragma once
#include "Animation/Studio/StudioTypes.h"

// Keyframe curves: storage, sampling and auto tangents. Pure.
//
// a_loopFrameCount, where taken, is the sequence's frame count when the curve is cyclic ("loop smoothing"), and
// nullopt otherwise. A cyclic curve is periodic over [0, frameCount), so keys don't need to sit on the boundary.
namespace Animation::Studio
{
	template <class T>
	struct Curve
	{
		std::vector<Key<T>> keys;  // Sorted by frame, unique frames.
	};

	using AnyCurve = std::variant<Curve<float>, Curve<bool>, Curve<ozz::math::Float3>, Curve<ozz::math::Quaternion>>;

	// A property is tracked by a sequence only while its track exists. Removing a track's last key removes the track.
	struct Track
	{
		AnyCurve curve;

		// Before the first key or after the last, the value clamps, unless the curve is cyclic.
		[[nodiscard]] Value Sample(float a_frame, std::optional<int32_t> a_loopFrameCount) const;
		void RegenerateTangents(std::optional<int32_t> a_loopFrameCount);
		// Whether a cyclic curve has a key in [0, frameCount], which it needs to be applied.
		[[nodiscard]] bool HasLoopRange(int32_t a_frameCount) const;

		[[nodiscard]] bool IsEmpty() const;
		[[nodiscard]] std::vector<AnyKey> GetKeys() const;
		[[nodiscard]] std::vector<int32_t> GetKeyFrames() const;
		[[nodiscard]] std::optional<AnyKey> GetKey(int32_t a_frame) const;
		// Writes a_key at a_frame (whatever frame the key holds), or erases the key there when a_key is empty, then
		// regenerates tangents. A key of the wrong type is ignored.
		void WriteKey(int32_t a_frame, const std::optional<AnyKey>& a_key, std::optional<int32_t> a_loopFrameCount);
	};

	// A track of a_key's type, without keys.
	[[nodiscard]] Track MakeEmptyTrack(const AnyKey& a_key);
	// Bool keys are always Step.
	[[nodiscard]] AnyKey MakeKey(const Value& a_value, int32_t a_frame, Interpolation a_interpolation);
	[[nodiscard]] int32_t GetKeyFrame(const AnyKey& a_key);
}
