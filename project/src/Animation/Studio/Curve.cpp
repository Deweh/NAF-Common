#include "Curve.h"

#include "StudioMath.h"

namespace Animation::Studio
{
	namespace
	{
		// Works for float and Float3.
		template <class T>
		T BezierCubic(const T& a_start, const T& a_startOut, const T& a_endIn, const T& a_end, float a_t)
		{
			const float u = 1.0f - a_t;
			return a_start * (u * u * u) + a_startOut * (3.0f * u * u * a_t) + a_endIn * (3.0f * u * a_t * a_t) + a_end * (a_t * a_t * a_t);
		}

		ozz::math::Quaternion BezierQuaternion(const ozz::math::Quaternion& a_start, const ozz::math::Quaternion& a_startOut, const ozz::math::Quaternion& a_endIn, const ozz::math::Quaternion& a_end, float a_t)
		{
			const auto start = ozz::math::NormalizeSafe(a_start, ozz::math::Quaternion::identity());
			const auto startOut = ozz::math::NormalizeSafe(EnsureSameHemisphere(start, a_startOut), start);
			const auto end = ozz::math::NormalizeSafe(EnsureSameHemisphere(start, a_end), start);
			const auto endIn = ozz::math::NormalizeSafe(EnsureSameHemisphere(end, a_endIn), end);
			const auto q01 = SlerpShortest(start, startOut, a_t);
			const auto q12 = SlerpShortest(startOut, endIn, a_t);
			const auto q23 = SlerpShortest(endIn, end, a_t);
			return ozz::math::NormalizeSafe(SlerpShortest(SlerpShortest(q01, q12, a_t), SlerpShortest(q12, q23, a_t), a_t), start);
		}

		template <class T>
		T InterpolateKeys(const Key<T>& a_start, const Key<T>& a_end, float a_t)
		{
			if constexpr (std::is_same_v<T, bool>) {
				return a_start.value;
			} else if constexpr (std::is_same_v<T, ozz::math::Quaternion>) {
				switch (a_start.interpolation) {
				case Interpolation::kStep:
					return Normalized(a_start.value);
				case Interpolation::kBezier:
					return BezierQuaternion(a_start.value, a_start.outTangent, a_end.inTangent, a_end.value, a_t);
				default:
					return SlerpShortest(a_start.value, a_end.value, a_t);
				}
			} else {
				switch (a_start.interpolation) {
				case Interpolation::kStep:
					return a_start.value;
				case Interpolation::kBezier:
					return BezierCubic(a_start.value, a_start.outTangent, a_end.inTangent, a_end.value, a_t);
				default:
					return a_start.value + (a_end.value - a_start.value) * a_t;
				}
			}
		}

		template <class T>
		void ComputeAutoTangents(const Key<T>* a_previous, Key<T>& a_key, const Key<T>* a_next)
		{
			const float previousDuration = a_previous ? static_cast<float>(a_key.frame - a_previous->frame) : 0.0f;
			const float nextDuration = a_next ? static_cast<float>(a_next->frame - a_key.frame) : 0.0f;

			if constexpr (std::is_same_v<T, ozz::math::Quaternion>) {
				const auto current = ozz::math::NormalizeSafe(a_key.value, ozz::math::Quaternion::identity());
				const auto deltaTo = [&](const ozz::math::Quaternion& a_other) {
					return QuaternionLog(ozz::math::Conjugate(current) * EnsureSameHemisphere(current, ozz::math::NormalizeSafe(a_other, current)));
				};
				auto velocity = ozz::math::Float3::zero();
				if (a_previous && a_next) {
					// Auto-clamp rotational extrema. A shared tangent through a direction reversal overshoots
					// the keyed orientation immediately before/after the key.
					const auto previousDelta = deltaTo(a_previous->value);
					const auto nextDelta = deltaTo(a_next->value);
					const auto incomingVelocity = -previousDelta / previousDuration;
					const auto outgoingVelocity = nextDelta / nextDuration;
					const float incomingSpeed = ozz::math::Length(incomingVelocity);
					const float outgoingSpeed = ozz::math::Length(outgoingVelocity);
					if (incomingSpeed > 0.000001f && outgoingSpeed > 0.000001f && ozz::math::Dot(incomingVelocity, outgoingVelocity) > 0.0f) {
						velocity = (nextDelta - previousDelta) / (previousDuration + nextDuration);
						const float speed = ozz::math::Length(velocity);
						const float maximumSpeed = (std::min)(incomingSpeed, outgoingSpeed);
						if (speed > maximumSpeed) velocity = velocity * (maximumSpeed / speed);
					}
				} else if (a_next) {
					velocity = deltaTo(a_next->value) / nextDuration;
				} else if (a_previous) {
					velocity = -deltaTo(a_previous->value) / previousDuration;
				}
				a_key.inTangent = a_previous ? ozz::math::NormalizeSafe(current * QuaternionExp(velocity * (-previousDuration / 3.0f)), current) : current;
				a_key.outTangent = a_next ? ozz::math::NormalizeSafe(current * QuaternionExp(velocity * (nextDuration / 3.0f)), current) : current;
			} else {
				T velocity = a_key.value * 0.0f;
				if (a_previous && a_next) velocity = (a_next->value - a_previous->value) / (previousDuration + nextDuration);
				else if (a_next) velocity = (a_next->value - a_key.value) / nextDuration;
				else if (a_previous) velocity = (a_key.value - a_previous->value) / previousDuration;
				a_key.inTangent = a_previous ? a_key.value - velocity * (previousDuration / 3.0f) : a_key.value;
				a_key.outTangent = a_next ? a_key.value + velocity * (nextDuration / 3.0f) : a_key.value;
			}
		}

		// The keys a looping curve cycles through, as an inclusive index range: those in [0, frameCount], minus the key
		// at frameCount when frame 0 also has one (they're the same point in the loop). Keys outside the range are kept
		// but not evaluated. nullopt when no key is in range.
		template <class T>
		std::optional<std::pair<size_t, size_t>> GetLoopRange(const Curve<T>& a_curve, int32_t a_frameCount)
		{
			if (a_frameCount <= 0) return std::nullopt;
			const auto& keys = a_curve.keys;
			const auto begin = std::ranges::lower_bound(keys, 0, {}, &Key<T>::frame);
			auto end = std::ranges::upper_bound(keys, a_frameCount, {}, &Key<T>::frame);
			if (begin == end) return std::nullopt;
			// Frame 0 wins over frameCount.
			if (std::prev(end) != begin && begin->frame == 0 && std::prev(end)->frame == a_frameCount) --end;
			return std::pair{ static_cast<size_t>(begin - keys.begin()), static_cast<size_t>(end - keys.begin()) - 1 };
		}

		template <class T>
		T SampleCurve(const Curve<T>& a_curve, float a_frame, std::optional<int32_t> a_loopFrameCount)
		{
			const auto& keys = a_curve.keys;
			assert(!keys.empty());

			if (const auto range = a_loopFrameCount ? GetLoopRange(a_curve, *a_loopFrameCount) : std::nullopt) {
				// Periodic over [0, frameCount). Outside the first and last keys, the curve runs along the wrap
				// segment from the last key to the first key shifted by one loop.
				const auto period = static_cast<float>(*a_loopFrameCount);
				float frame = std::fmod(a_frame, period);
				if (frame < 0.0f) frame += period;
				const auto& first = keys[range->first];
				const auto& last = keys[range->second];
				if (frame < static_cast<float>(first.frame) || frame >= static_cast<float>(last.frame)) {
					if (frame < static_cast<float>(first.frame)) frame += period;
					// Never zero: GetLoopRange never returns both a key at 0 and a key at frameCount.
					const float span = static_cast<float>(first.frame - last.frame) + period;
					return InterpolateKeys(last, first, (frame - static_cast<float>(last.frame)) / span);
				}
				const auto begin = keys.begin() + range->first;
				const auto end = keys.begin() + range->second + 1;
				const auto next = std::upper_bound(begin, end, frame, [](float a_value, const Key<T>& a_key) { return a_value < static_cast<float>(a_key.frame); });
				const auto& start = *std::prev(next);
				return InterpolateKeys(start, *next, (frame - static_cast<float>(start.frame)) / static_cast<float>(next->frame - start.frame));
			}

			if (a_frame <= static_cast<float>(keys.front().frame)) return Normalized(keys.front().value);
			if (a_frame >= static_cast<float>(keys.back().frame)) return Normalized(keys.back().value);

			const auto next = std::ranges::upper_bound(keys, a_frame, {}, [](const Key<T>& a_key) { return static_cast<float>(a_key.frame); });
			const auto& start = *std::prev(next);
			const auto& end = *next;
			return InterpolateKeys(start, end, (a_frame - static_cast<float>(start.frame)) / static_cast<float>(end.frame - start.frame));
		}

		// A key's auto tangents depend only on its neighbors' frames and values and, in a loop, on whether it's an end
		// of the loop range (and then on the other end).
		template <class T>
		void RegenerateKeyTangents(Curve<T>& a_curve, std::optional<std::pair<size_t, size_t>> a_range, std::optional<int32_t> a_loopFrameCount, size_t a_index)
		{
			auto& keys = a_curve.keys;
			if (keys[a_index].tangentMode != TangentMode::kAuto) return;
			const Key<T>* previous = a_index > 0 ? &keys[a_index - 1] : nullptr;
			const Key<T>* next = a_index + 1 < keys.size() ? &keys[a_index + 1] : nullptr;

			// Looping: the first and last keys in the loop take their missing neighbor from the other end, shifted by
			// one loop length. A lone key is its own neighbor on both sides.
			Key<T> previousGhost;
			Key<T> nextGhost;
			if (a_range && a_index >= a_range->first && a_index <= a_range->second) {
				if (a_index == a_range->first) {
					previousGhost = keys[a_range->second];
					previousGhost.frame -= *a_loopFrameCount;
					previous = &previousGhost;
				}
				if (a_index == a_range->second) {
					nextGhost = keys[a_range->first];
					nextGhost.frame += *a_loopFrameCount;
					next = &nextGhost;
				}
			}
			ComputeAutoTangents(previous, keys[a_index], next);
		}

		template <class T>
		void RegenerateCurveTangents(Curve<T>& a_curve, std::optional<int32_t> a_loopFrameCount)
		{
			if constexpr (!std::is_same_v<T, bool>) {
				const auto range = a_loopFrameCount ? GetLoopRange(a_curve, *a_loopFrameCount) : std::nullopt;
				for (size_t i = 0; i < a_curve.keys.size(); ++i) RegenerateKeyTangents(a_curve, range, a_loopFrameCount, i);
			}
		}

		// After one key was written or erased at a_index: the same result as RegenerateCurveTangents, for only the keys
		// whose inputs can have changed. Those are its neighbors, and the loop range's ends before and after (a_oldEnds,
		// by frame), since a range end's tangent depends on the other end. Rewriting a whole curve one key at a time
		// was quadratic in its length.
		template <class T>
		void RegenerateTangentsAround(Curve<T>& a_curve, std::optional<int32_t> a_loopFrameCount, size_t a_index, std::optional<std::pair<int32_t, int32_t>> a_oldEnds)
		{
			if constexpr (!std::is_same_v<T, bool>) {
				auto& keys = a_curve.keys;
				const auto range = a_loopFrameCount ? GetLoopRange(a_curve, *a_loopFrameCount) : std::nullopt;
				std::array<size_t, 7> indices;
				size_t count = 0;
				const auto add = [&](size_t a_candidate) {
					if (a_candidate < keys.size() && std::find(indices.begin(), indices.begin() + count, a_candidate) == indices.begin() + count) indices[count++] = a_candidate;
				};
				if (a_index > 0) add(a_index - 1);
				add(a_index);
				add(a_index + 1);
				if (range) {
					add(range->first);
					add(range->second);
				}
				if (a_oldEnds) {
					for (const int32_t frame : { a_oldEnds->first, a_oldEnds->second }) {
						const auto iter = std::ranges::lower_bound(keys, frame, {}, &Key<T>::frame);
						if (iter != keys.end() && iter->frame == frame) add(static_cast<size_t>(iter - keys.begin()));
					}
				}
				for (size_t i = 0; i < count; ++i) RegenerateKeyTangents(a_curve, range, a_loopFrameCount, indices[i]);
			}
		}

		template <class T>
		std::optional<std::pair<int32_t, int32_t>> GetLoopEndFrames(const Curve<T>& a_curve, std::optional<int32_t> a_loopFrameCount)
		{
			const auto range = a_loopFrameCount ? GetLoopRange(a_curve, *a_loopFrameCount) : std::nullopt;
			if (!range) return std::nullopt;
			return std::pair{ a_curve.keys[range->first].frame, a_curve.keys[range->second].frame };
		}
	}

	Value Track::Sample(float a_frame, std::optional<int32_t> a_loopFrameCount) const
	{
		return std::visit([&](const auto& a_curve) -> Value { return SampleCurve(a_curve, a_frame, a_loopFrameCount); }, curve);
	}

	void Track::RegenerateTangents(std::optional<int32_t> a_loopFrameCount)
	{
		std::visit([&](auto& a_curve) { RegenerateCurveTangents(a_curve, a_loopFrameCount); }, curve);
	}

	bool Track::HasLoopRange(int32_t a_frameCount) const
	{
		return std::visit([&](const auto& a_curve) { return GetLoopRange(a_curve, a_frameCount).has_value(); }, curve);
	}

	bool Track::IsEmpty() const
	{
		return std::visit([](const auto& a_curve) { return a_curve.keys.empty(); }, curve);
	}

	std::vector<AnyKey> Track::GetKeys() const
	{
		return std::visit([](const auto& a_curve) { return std::vector<AnyKey>(a_curve.keys.begin(), a_curve.keys.end()); }, curve);
	}

	std::vector<int32_t> Track::GetKeyFrames() const
	{
		return std::visit([](const auto& a_curve) {
			std::vector<int32_t> frames;
			frames.reserve(a_curve.keys.size());
			for (const auto& key : a_curve.keys) frames.push_back(key.frame);
			return frames;
		}, curve);
	}

	std::optional<AnyKey> Track::GetKey(int32_t a_frame) const
	{
		return std::visit([&](const auto& a_curve) -> std::optional<AnyKey> {
			using KeyT = typename std::remove_cvref_t<decltype(a_curve.keys)>::value_type;
			const auto iter = std::ranges::lower_bound(a_curve.keys, a_frame, {}, &KeyT::frame);
			if (iter == a_curve.keys.end() || iter->frame != a_frame) return std::nullopt;
			return *iter;
		}, curve);
	}

	void Track::WriteKey(int32_t a_frame, const std::optional<AnyKey>& a_key, std::optional<int32_t> a_loopFrameCount)
	{
		std::visit([&](auto& a_curve) {
			using KeyT = typename decltype(a_curve.keys)::value_type;
			auto& keys = a_curve.keys;
			const auto oldEnds = GetLoopEndFrames(a_curve, a_loopFrameCount);
			const auto iter = std::ranges::lower_bound(keys, a_frame, {}, &KeyT::frame);
			const auto index = static_cast<size_t>(iter - keys.begin());
			const bool exists = iter != keys.end() && iter->frame == a_frame;
			if (!a_key) {
				if (!exists) return;
				keys.erase(iter);
			} else {
				const auto newKey = std::get_if<KeyT>(std::addressof(*a_key));
				if (!newKey) return;
				auto key = *newKey;
				key.frame = a_frame;
				if (exists) *iter = key;
				else keys.insert(iter, key);
			}
			RegenerateTangentsAround(a_curve, a_loopFrameCount, index, oldEnds);
		}, curve);
	}

	Track MakeEmptyTrack(const AnyKey& a_key)
	{
		return { std::visit([](const auto& a_typed) -> AnyCurve { return Curve<decltype(a_typed.value)>{}; }, a_key) };
	}

	AnyKey MakeKey(const Value& a_value, int32_t a_frame, Interpolation a_interpolation)
	{
		return std::visit([&](const auto& a_typed) -> AnyKey {
			using T = std::remove_cvref_t<decltype(a_typed)>;
			Key<T> key;
			key.frame = a_frame;
			key.interpolation = std::is_same_v<T, bool> ? Interpolation::kStep : a_interpolation;
			key.value = a_typed;
			key.inTangent = a_typed;
			key.outTangent = a_typed;
			return key;
		}, a_value);
	}

	int32_t GetKeyFrame(const AnyKey& a_key)
	{
		return std::visit([](const auto& a_typed) { return a_typed.frame; }, a_key);
	}
}
