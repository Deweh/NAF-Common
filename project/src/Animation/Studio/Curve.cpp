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
		void RegenerateLocalTangents(Curve<T>& a_curve, std::optional<int32_t> a_loopFrameCount)
		{
			if constexpr (!std::is_same_v<T, bool>) {
				const auto range = a_loopFrameCount ? GetLoopRange(a_curve, *a_loopFrameCount) : std::nullopt;
				for (size_t i = 0; i < a_curve.keys.size(); ++i) RegenerateKeyTangents(a_curve, range, a_loopFrameCount, i);
			}
		}

		// ---- Continuous Bezier mode ----
		// A C2 cubic spline: the velocities of a curve's Bezier keys are solved together, so acceleration matches on
		// both sides of each key. Values are worked as Float3 and solved one component at a time: floats use x, and
		// quaternions use the world-space angular velocity in QuaternionLog units, which makes the rotation system linear
		// to first order. Per component, a hold in one component (a path that keeps its height) doesn't stop the others.

		// One row of the tridiagonal system: a * m[i - 1] + b * m[i] + c * m[i + 1] = d, for velocities m. The default
		// row is fixed: it pins the velocity to d.
		struct SplineRow
		{
			float a{ 0.0f };
			float b{ 1.0f };
			float c{ 0.0f };
			float d{ 0.0f };
			bool free{ false };  // Solved, rather than fixed.
		};

		float GetComponent(const ozz::math::Float3& a_value, size_t a_component)
		{
			return a_component == 0 ? a_value.x : a_component == 1 ? a_value.y : a_value.z;
		}

		void SetComponent(ozz::math::Float3& a_value, size_t a_component, float a_new)
		{
			(a_component == 0 ? a_value.x : a_component == 1 ? a_value.y : a_value.z) = a_new;
		}

		// The change from a_start to a_end, as a vector.
		template <class T>
		ozz::math::Float3 SplineDelta(const T& a_start, const T& a_end)
		{
			if constexpr (std::is_same_v<T, ozz::math::Quaternion>) {
				const auto start = ozz::math::NormalizeSafe(a_start, ozz::math::Quaternion::identity());
				const auto end = EnsureSameHemisphere(start, ozz::math::NormalizeSafe(a_end, start));
				// The relative rotation's log is the same vector in either key's frame. World space makes it comparable
				// with the other segments'.
				return ozz::math::TransformVector(start, QuaternionLog(ozz::math::Conjugate(start) * end));
			} else if constexpr (std::is_same_v<T, float>) {
				return { a_end - a_start, 0.0f, 0.0f };
			} else {
				return a_end - a_start;
			}
		}

		// Thomas algorithm. Every row built here is diagonally dominant, so it's stable without pivoting.
		void SolveTridiagonal(std::span<const SplineRow> a_rows, std::span<float> a_out)
		{
			const size_t count = a_rows.size();
			std::vector<float> c(count);
			std::vector<float> d(count);
			c[0] = a_rows[0].c / a_rows[0].b;
			d[0] = a_rows[0].d / a_rows[0].b;
			for (size_t i = 1; i < count; ++i) {
				const float denominator = a_rows[i].b - a_rows[i].a * c[i - 1];
				c[i] = a_rows[i].c / denominator;
				d[i] = (a_rows[i].d - a_rows[i].a * d[i - 1]) / denominator;
			}
			a_out[count - 1] = d[count - 1];
			for (size_t i = count - 1; i-- > 0;) a_out[i] = d[i] - c[i] * a_out[i + 1];
		}

		// Row 0's a couples to the last velocity and the last row's c to the first. Sherman-Morrison: solve the system
		// without the corners, then correct by a rank-one update.
		void SolveCyclicTridiagonal(std::vector<SplineRow> a_rows, std::span<float> a_out)
		{
			const size_t count = a_rows.size();
			const float lowerCorner = a_rows[count - 1].c;
			const float upperCorner = a_rows[0].a;
			const float gamma = -a_rows[0].b;
			a_rows[0].a = 0.0f;
			a_rows[count - 1].c = 0.0f;
			a_rows[0].b -= gamma;
			a_rows[count - 1].b -= lowerCorner * upperCorner / gamma;

			std::vector<float> x(count);
			SolveTridiagonal(a_rows, x);
			// The correction vector u = (gamma, 0, ..., 0, lowerCorner).
			for (auto& row : a_rows) row.d = 0.0f;
			a_rows[0].d = gamma;
			a_rows[count - 1].d = lowerCorner;
			std::vector<float> z(count);
			SolveTridiagonal(a_rows, z);

			const float ratio = upperCorner / gamma;
			const float factor = (x[0] + x[count - 1] * ratio) / (1.0f + z[0] + z[count - 1] * ratio);
			for (size_t i = 0; i < count; ++i) a_out[i] = x[i] - factor * z[i];
		}

		void SolveSplineRows(std::vector<SplineRow> a_rows, bool a_cyclic, std::span<float> a_out)
		{
			const size_t count = a_rows.size();
			// An open curve's first row never has an a, and its last never a c.
			if (!a_cyclic) {
				SolveTridiagonal(a_rows, a_out);
				return;
			}

			// Move fixed velocities into their neighbors' right-hand sides, so the cycle can be cut at them.
			for (size_t i = 0; i < count; ++i) {
				auto& row = a_rows[i];
				const auto& previous = a_rows[(i + count - 1) % count];
				const auto& next = a_rows[(i + 1) % count];
				if (row.a != 0.0f && !previous.free) {
					row.d -= row.a * previous.d;
					row.a = 0.0f;
				}
				if (row.c != 0.0f && !next.free) {
					row.d -= row.c * next.d;
					row.c = 0.0f;
				}
			}
			for (size_t start = 0; start < count; ++start) {
				if (a_rows[start].a != 0.0f || a_rows[(start + count - 1) % count].c != 0.0f) continue;
				std::vector<SplineRow> rotated(count);
				for (size_t i = 0; i < count; ++i) rotated[i] = a_rows[(start + i) % count];
				std::vector<float> solved(count);
				SolveTridiagonal(rotated, solved);
				for (size_t i = 0; i < count; ++i) a_out[(start + i) % count] = solved[i];
				return;
			}
			// Every row couples to both neighbors: a rotation that keeps turning the same way all through the loop. With
			// fewer than three keys the direction always flips somewhere, which fixes a row, so this has at least three.
			if (count < 3) {
				std::ranges::fill(a_out, 0.0f);
				return;
			}
			SolveCyclicTridiagonal(std::move(a_rows), a_out);
		}

		template <class T>
		void RegenerateContinuousTangents(Curve<T>& a_curve, std::optional<int32_t> a_loopFrameCount)
		{
			if constexpr (!std::is_same_v<T, bool>) {
				// Keys away from Bezier segments, and keys a cyclic curve doesn't evaluate, keep their local tangents.
				RegenerateLocalTangents(a_curve, a_loopFrameCount);

				auto& keys = a_curve.keys;
				const auto range = a_loopFrameCount ? GetLoopRange(a_curve, *a_loopFrameCount) : std::nullopt;
				const bool cyclic = range.has_value();
				const size_t first = cyclic ? range->first : 0;
				const size_t count = cyclic ? range->second - range->first + 1 : keys.size();
				if (count == 0) return;
				const auto keyAt = [&](size_t a_index) -> Key<T>& { return keys[first + a_index]; };

				// Segment j runs from key j to key j + 1. In a cyclic curve the last one wraps to the first key, one loop later.
				struct Segment
				{
					float length;
					ozz::math::Float3 slope;
					Interpolation interpolation;
				};
				const size_t segmentCount = cyclic ? count : count - 1;
				std::vector<Segment> segments(segmentCount);
				for (size_t j = 0; j < segmentCount; ++j) {
					const auto& start = keyAt(j);
					const auto& end = keyAt((j + 1) % count);
					const auto length = static_cast<float>(end.frame - start.frame + (j + 1 == count ? *a_loopFrameCount : 0));
					segments[j] = { length, SplineDelta(start.value, end.value) / length, start.interpolation };
				}
				const auto previousSegment = [&](size_t a_key) -> const Segment* {
					return a_key > 0 ? &segments[a_key - 1] : cyclic ? &segments[count - 1] : nullptr;
				};
				const auto nextSegment = [&](size_t a_key) -> const Segment* { return a_key < segmentCount ? &segments[a_key] : nullptr; };
				const auto isBezier = [](const Segment* a_segment) { return a_segment && a_segment->interpolation == Interpolation::kBezier; };
				const auto isLinear = [](const Segment* a_segment) { return a_segment && a_segment->interpolation == Interpolation::kLinear; };

				std::vector<ozz::math::Float3> velocities(count, ozz::math::Float3::zero());
				std::vector<SplineRow> rows(count);
				std::vector<float> solved(count);
				constexpr size_t kComponents = std::is_same_v<T, float> ? 1 : 3;
				for (size_t component = 0; component < kComponents; ++component) {
					const auto slope = [&](const Segment* a_segment) { return GetComponent(a_segment->slope, component); };
					for (size_t i = 0; i < count; ++i) {
						const auto previous = previousSegment(i);
						const auto next = nextSegment(i);
						auto& row = rows[i];
						row = {};
						if (isBezier(previous) && isBezier(next)) {
							// Next to a hold (an equal neighbor), the key stays still (the row's default). An extreme stays
							// free: holding it would break the acceleration there, so the curve may overshoot it slightly.
							// Duplicating the extreme key makes a hold, for a key that must stay the extreme.
							if (slope(previous) == 0.0f || slope(next) == 0.0f) continue;
							row.a = 1.0f / previous->length;
							row.c = 1.0f / next->length;
							row.b = 2.0f * (row.a + row.c);
							row.d = 3.0f * (slope(previous) * row.a + slope(next) * row.c);
							row.free = true;
						} else if (isBezier(previous)) {
							// The end of a run: carry on into a linear segment's velocity, or end without acceleration.
							if (isLinear(next)) {
								row.d = slope(next);
							} else {
								row.a = 1.0f;
								row.b = 2.0f;
								row.d = 3.0f * slope(previous);
								row.free = true;
							}
						} else if (isBezier(next)) {
							if (isLinear(previous)) {
								row.d = slope(previous);
							} else {
								row.b = 2.0f;
								row.c = 1.0f;
								row.d = 3.0f * slope(next);
								row.free = true;
							}
						}
					}

					// Where the value runs one way through a key, a velocity against it turns the curve back between keys.
					// Hold that key still instead and solve again. Each pass fixes at least one more row, so this ends.
					for (size_t pass = 0; pass <= count; ++pass) {
						SolveSplineRows(rows, cyclic, solved);
						bool held = false;
						for (size_t i = 0; i < count; ++i) {
							if (!rows[i].free) continue;
							const auto previous = previousSegment(i);
							const auto next = nextSegment(i);
							if (isBezier(previous) && isBezier(next) && slope(previous) * slope(next) < 0.0f) continue;  // An extreme.
							if ((isBezier(previous) && solved[i] * slope(previous) < 0.0f) || (isBezier(next) && solved[i] * slope(next) < 0.0f)) {
								rows[i] = {};
								held = true;
							}
						}
						if (!held) break;
					}
					for (size_t i = 0; i < count; ++i) SetComponent(velocities[i], component, solved[i]);
				}

				// Handles at a third of each neighboring segment, as in local mode.
				for (size_t i = 0; i < count; ++i) {
					const auto previous = previousSegment(i);
					const auto next = nextSegment(i);
					if (!isBezier(previous) && !isBezier(next)) continue;
					auto& key = keyAt(i);
					const float inLength = previous ? previous->length / 3.0f : 0.0f;
					const float outLength = next ? next->length / 3.0f : 0.0f;
					if constexpr (std::is_same_v<T, ozz::math::Quaternion>) {
						const auto current = ozz::math::NormalizeSafe(key.value, ozz::math::Quaternion::identity());
						const auto velocity = ozz::math::TransformVector(ozz::math::Conjugate(current), velocities[i]);
						key.inTangent = ozz::math::NormalizeSafe(current * QuaternionExp(velocity * -inLength), current);
						key.outTangent = ozz::math::NormalizeSafe(current * QuaternionExp(velocity * outLength), current);
					} else if constexpr (std::is_same_v<T, float>) {
						key.inTangent = key.value - velocities[i].x * inLength;
						key.outTangent = key.value + velocities[i].x * outLength;
					} else {
						key.inTangent = key.value - velocities[i] * inLength;
						key.outTangent = key.value + velocities[i] * outLength;
					}
				}
			}
		}

		template <class T>
		void RegenerateCurveTangents(Curve<T>& a_curve, std::optional<int32_t> a_loopFrameCount, BezierMode a_bezierMode)
		{
			if (a_bezierMode == BezierMode::kContinuous) RegenerateContinuousTangents(a_curve, a_loopFrameCount);
			else RegenerateLocalTangents(a_curve, a_loopFrameCount);
		}

		// After one key was written or erased at a_index: the same result as RegenerateLocalTangents, for only the keys
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

	void Track::RegenerateTangents(std::optional<int32_t> a_loopFrameCount, BezierMode a_bezierMode)
	{
		std::visit([&](auto& a_curve) { RegenerateCurveTangents(a_curve, a_loopFrameCount, a_bezierMode); }, curve);
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

	void Track::WriteKey(int32_t a_frame, const std::optional<AnyKey>& a_key, std::optional<int32_t> a_loopFrameCount, BezierMode a_bezierMode)
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
			// A continuous curve's tangents all depend on each other.
			if (a_bezierMode == BezierMode::kContinuous) RegenerateContinuousTangents(a_curve, a_loopFrameCount);
			else RegenerateTangentsAround(a_curve, a_loopFrameCount, index, oldEnds);
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
