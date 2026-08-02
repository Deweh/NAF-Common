#include "SplineMath.h"

#include "Animation/Studio/StudioMath.h"

namespace Animation::Studio
{
	namespace
	{
		constexpr size_t kSplineSamplesPerSegment = 16;

		class SplineCurve
		{
		public:
			explicit SplineCurve(const SplineInput& a_input)
			{
				const auto& points = a_input.controls;
				const size_t count = points.size();
				// Centripetal knot intervals.
				std::vector<float> intervals(count - 1);
				std::vector<float> chords(count - 1);
				for (size_t i = 0; i + 1 < count; ++i) {
					chords[i] = ozz::math::Length(points[i + 1] - points[i]);
					intervals[i] = (std::max)(std::sqrt(chords[i]), 0.001f);
				}
				// Velocities with respect to the knot parameter. The ends follow the end controls' aim.
				std::vector<ozz::math::Float3> velocities(count);
				velocities.front() = a_input.startAim * (chords.front() * a_input.tangentLength / intervals.front());
				velocities.back() = a_input.endAim * (chords.back() * a_input.tangentLength / intervals.back());
				for (size_t i = 1; i + 1 < count; ++i) {
					const float d0 = intervals[i - 1];
					const float d1 = intervals[i];
					velocities[i] = (points[i] - points[i - 1]) / d0 - (points[i + 1] - points[i - 1]) / (d0 + d1) + (points[i + 1] - points[i]) / d1;
				}

				for (size_t segment = 0; segment + 1 < count; ++segment) {
					const auto& p0 = points[segment];
					const auto& p1 = points[segment + 1];
					const auto m0 = velocities[segment] * intervals[segment];
					const auto m1 = velocities[segment + 1] * intervals[segment];
					m_controlSamples.push_back(m_points.size());
					const size_t samples = segment + 2 == count ? kSplineSamplesPerSegment + 1 : kSplineSamplesPerSegment;
					for (size_t k = 0; k < samples; ++k) {
						const float t = static_cast<float>(k) / kSplineSamplesPerSegment;
						const float t2 = t * t;
						const float t3 = t2 * t;
						m_points.push_back(p0 * (2.0f * t3 - 3.0f * t2 + 1.0f) + m0 * (t3 - 2.0f * t2 + t) + p1 * (-2.0f * t3 + 3.0f * t2) + m1 * (t3 - t2));
						const auto derivative = p0 * (6.0f * t2 - 6.0f * t) + m0 * (3.0f * t2 - 4.0f * t + 1.0f) + p1 * (-6.0f * t2 + 6.0f * t) + m1 * (3.0f * t2 - 2.0f * t);
						m_tangents.push_back(derivative);
					}
				}
				m_controlSamples.push_back(m_points.size() - 1);
				ComputeLengths();

				// Offset the curve onto the first bone, fading out so the tip still reaches the last control.
				if (!a_input.rootFollowsCurve && m_lengths.back() > 0.00001f) {
					const auto offset = a_input.root - m_points.front();
					for (size_t i = 0; i < m_points.size(); ++i) m_points[i] = m_points[i] + offset * (1.0f - m_lengths[i] / m_lengths.back());
					ComputeLengths();
				}
				// Tangents: the derivative's direction, or the chord's where the derivative vanishes.
				for (size_t i = 0; i < m_points.size(); ++i) {
					const auto chord = i + 1 < m_points.size() ? m_points[i + 1] - m_points[i] : m_points[i] - m_points[i - 1];
					m_tangents[i] = NormalizeOr(m_tangents[i], NormalizeOr(chord, a_input.startAim));
				}
				ComputeFrames(a_input);
			}

			[[nodiscard]] float GetLength() const { return m_lengths.back(); }
			[[nodiscard]] const std::vector<ozz::math::Float3>& GetPoints() const { return m_points; }
			[[nodiscard]] const std::vector<float>& GetLengths() const { return m_lengths; }

			// Beyond the ends, the curve continues along its end tangent.
			[[nodiscard]] ozz::math::Float3 PointAt(float a_arc) const
			{
				if (a_arc >= GetLength()) return m_points.back() + m_tangents.back() * (a_arc - GetLength());
				const auto [index, t] = Locate(a_arc);
				return Lerp(m_points[index], m_points[index + 1], t);
			}

			[[nodiscard]] ozz::math::Float3 TangentAt(float a_arc) const
			{
				if (a_arc >= GetLength()) return m_tangents.back();
				const auto [index, t] = Locate(a_arc);
				return NormalizeOr(Lerp(m_tangents[index], m_tangents[index + 1], t), m_tangents[index]);
			}

			// Perpendicular to TangentAt.
			[[nodiscard]] ozz::math::Float3 UpAt(float a_arc) const
			{
				const auto tangent = TangentAt(a_arc);
				if (a_arc >= GetLength()) return Orthogonalize(m_ups.back(), tangent, AnyPerpendicular(tangent));
				const auto [index, t] = Locate(a_arc);
				return Orthogonalize(Lerp(m_ups[index], m_ups[index + 1], t), tangent, AnyPerpendicular(tangent));
			}

		private:
			static ozz::math::Float3 Lerp(const ozz::math::Float3& a_from, const ozz::math::Float3& a_to, float a_t)
			{
				return a_from + (a_to - a_from) * a_t;
			}

			void ComputeLengths()
			{
				m_lengths.assign(m_points.size(), 0.0f);
				for (size_t i = 1; i < m_points.size(); ++i) m_lengths[i] = m_lengths[i - 1] + ozz::math::Length(m_points[i] - m_points[i - 1]);
			}

			// The sample interval holding a_arc (clamped to the curve), and the position within it.
			[[nodiscard]] std::pair<size_t, float> Locate(float a_arc) const
			{
				const float arc = std::clamp(a_arc, 0.0f, GetLength());
				const auto next = std::ranges::upper_bound(m_lengths, arc);
				const size_t index = std::clamp<size_t>(static_cast<size_t>(std::distance(m_lengths.begin(), next)), 1, m_lengths.size() - 1) - 1;
				const float span = m_lengths[index + 1] - m_lengths[index];
				return { index, span > 0.00001f ? (arc - m_lengths[index]) / span : 0.0f };
			}

			void ComputeFrames(const SplineInput& a_input)
			{
				// A rotation-minimizing frame by double reflection (Wang et al. 2008), starting from the first control's up.
				const size_t count = m_points.size();
				m_ups.resize(count);
				m_ups[0] = Orthogonalize(a_input.controlUps.front(), m_tangents[0], AnyPerpendicular(m_tangents[0]));
				for (size_t i = 0; i + 1 < count; ++i) {
					const auto v1 = m_points[i + 1] - m_points[i];
					const float c1 = ozz::math::Dot(v1, v1);
					if (c1 <= 0.0000001f) {
						m_ups[i + 1] = Orthogonalize(m_ups[i], m_tangents[i + 1], AnyPerpendicular(m_tangents[i + 1]));
						continue;
					}
					const auto reflectedUp = m_ups[i] - v1 * (2.0f / c1 * ozz::math::Dot(v1, m_ups[i]));
					const auto reflectedTangent = m_tangents[i] - v1 * (2.0f / c1 * ozz::math::Dot(v1, m_tangents[i]));
					const auto v2 = m_tangents[i + 1] - reflectedTangent;
					const float c2 = ozz::math::Dot(v2, v2);
					const auto up = c2 <= 0.0000001f ? reflectedUp : reflectedUp - v2 * (2.0f / c2 * ozz::math::Dot(v2, reflectedUp));
					m_ups[i + 1] = Orthogonalize(up, m_tangents[i + 1], AnyPerpendicular(m_tangents[i + 1]));
				}

				// Roll toward each later control's up, interpolated by arc length between controls, plus the twist
				// growing from root to tip.
				std::vector<float> rolls(m_controlSamples.size(), 0.0f);
				for (size_t k = 1; k < m_controlSamples.size(); ++k) {
					const size_t sample = m_controlSamples[k];
					const auto& tangent = m_tangents[sample];
					const auto target = Orthogonalize(a_input.controlUps[k], tangent, m_ups[sample]);
					float roll = std::atan2(ozz::math::Dot(ozz::math::Cross(m_ups[sample], target), tangent), ozz::math::Dot(m_ups[sample], target));
					// Unwrapped against the previous control, so roll doesn't jump by a full turn between them.
					while (roll - rolls[k - 1] > ozz::math::kPi) roll -= 2.0f * ozz::math::kPi;
					while (roll - rolls[k - 1] < -ozz::math::kPi) roll += 2.0f * ozz::math::kPi;
					rolls[k] = roll;
				}
				const float length = GetLength();
				for (size_t k = 0; k + 1 < m_controlSamples.size(); ++k) {
					const size_t first = m_controlSamples[k];
					const size_t last = m_controlSamples[k + 1];
					const float span = m_lengths[last] - m_lengths[first];
					for (size_t i = first; i < last || (k + 2 == m_controlSamples.size() && i == last); ++i) {
						const float t = span > 0.00001f ? (m_lengths[i] - m_lengths[first]) / span : 0.0f;
						const float twist = length > 0.00001f ? a_input.twist * m_lengths[i] / length : 0.0f;
						m_ups[i] = RotateAbout(m_ups[i], m_tangents[i], rolls[k] + (rolls[k + 1] - rolls[k]) * t + twist);
					}
				}
			}

			std::vector<ozz::math::Float3> m_points;
			std::vector<ozz::math::Float3> m_tangents;  // Unit.
			std::vector<ozz::math::Float3> m_ups;       // Unit, perpendicular to the tangents.
			std::vector<float> m_lengths;               // Cumulative arc length.
			std::vector<size_t> m_controlSamples;       // The sample at each control.
		};
	}

	float GetSplineCoincidentDistance(std::span<const float> a_lengths)
	{
		const float chain = a_lengths.empty() ? 0.0f : std::accumulate(a_lengths.begin() + 1, a_lengths.end(), 0.0f);
		return (std::max)(chain * kSplineCoincidentFraction, kSplineCoincidentMinimum);
	}

	std::vector<std::pair<size_t, size_t>> GetSplineAimSegments(std::span<const float> a_lengths)
	{
		const size_t count = a_lengths.size();
		const float threshold = GetSplineCoincidentDistance(a_lengths);
		std::vector<std::pair<size_t, size_t>> result(count);
		for (size_t j = 0; j < count; ++j) {
			result[j] = { j, j };
			float distance = 0.0f;
			for (size_t k = j + 1; k < count && result[j].second == j; ++k) {
				distance += a_lengths[k];
				if (distance > threshold) result[j] = { j, k };
			}
			if (result[j].second != j) continue;
			distance = 0.0f;
			for (size_t i = j; i > 0; --i) {
				distance += a_lengths[i];
				if (distance > threshold) {
					result[j] = { i - 1, j };
					break;
				}
			}
		}
		return result;
	}

	std::vector<size_t> GetSplineControlCandidates(std::span<const float> a_lengths)
	{
		const float threshold = GetSplineCoincidentDistance(a_lengths);
		std::vector<size_t> result{ 0 };
		float distance = 0.0f;
		for (size_t j = 1; j < a_lengths.size(); ++j) {
			distance += a_lengths[j];
			if (distance > threshold) {
				result.push_back(j);
				distance = 0.0f;
			}
		}
		if (result.size() > 1) result.back() = a_lengths.size() - 1;
		return result;
	}

	SplineResult SolveSpline(const SplineInput& a_input)
	{
		const SplineCurve curve(a_input);
		const auto& points = curve.GetPoints();
		const auto& sampleLengths = curve.GetLengths();
		const size_t boneCount = a_input.lengths.size();

		// Each bone at its rest distance (chord, not arc) from the previous one, searched down the samples.
		std::vector<ozz::math::Float3> positions(boneCount);
		std::vector<float> arcs(boneCount, 0.0f);
		positions[0] = a_input.rootFollowsCurve ? points.front() : a_input.root;
		size_t next = 1;
		auto previousPoint = positions[0];
		float previousArc = 0.0f;
		for (size_t j = 1; j < boneCount; ++j) {
			const auto& center = positions[j - 1];
			const float length = a_input.lengths[j];
			bool found = false;
			for (; next < points.size(); ++next) {
				if (ozz::math::Length(points[next] - center) < length) {
					previousPoint = points[next];
					previousArc = sampleLengths[next];
					continue;
				}
				// Where the segment previousPoint -> points[next] leaves the sphere about center: the larger root.
				const auto direction = points[next] - previousPoint;
				const auto offset = previousPoint - center;
				const float a = ozz::math::Dot(direction, direction);
				const float b = 2.0f * ozz::math::Dot(offset, direction);
				const float c = ozz::math::Dot(offset, offset) - length * length;
				const float discriminant = b * b - 4.0f * a * c;
				const float u = a > 0.0000001f && discriminant >= 0.0f ? std::clamp((-b + std::sqrt(discriminant)) / (2.0f * a), 0.0f, 1.0f) : 1.0f;
				positions[j] = previousPoint + direction * u;
				arcs[j] = previousArc + std::sqrt(a) * u;
				previousPoint = positions[j];
				previousArc = arcs[j];
				found = true;
				break;
			}
			if (!found) {
				// Past the end: continue along the end tangent.
				const auto tangent = curve.TangentAt(curve.GetLength());
				const auto offset = points.back() - center;
				const float b = 2.0f * ozz::math::Dot(offset, tangent);
				const float c = ozz::math::Dot(offset, offset) - length * length;
				const float w = (std::max)((-b + std::sqrt((std::max)(b * b - 4.0f * c, 0.0f))) * 0.5f, 0.0f);
				positions[j] = points.back() + tangent * w;
				arcs[j] = curve.GetLength() + w;
				previousPoint = positions[j];
				previousArc = arcs[j];
			}
		}

		// Stretch: blend toward bones spaced proportionally along the whole curve.
		const float stretch = std::clamp(a_input.stretch, 0.0f, 1.0f);
		if (stretch > 0.0f) {
			const float chainLength = std::accumulate(a_input.lengths.begin() + 1, a_input.lengths.end(), 0.0f);
			float offset = 0.0f;
			for (size_t j = 1; j < boneCount && chainLength > 0.00001f; ++j) {
				offset += a_input.lengths[j];
				const float arc = curve.GetLength() * offset / chainLength;
				positions[j] = positions[j] + (curve.PointAt(arc) - positions[j]) * stretch;
				arcs[j] = arcs[j] + (arc - arcs[j]) * stretch;
			}
		}

		SplineResult result;
		result.positions = positions;
		result.aims.resize(boneCount);
		result.ups.resize(boneCount);
		// Bones that share a point aim past it, so they all turn as one and keep their rest relationship.
		const auto segments = GetSplineAimSegments(a_input.lengths);
		for (size_t j = 0; j < boneCount; ++j) {
			const auto tangent = curve.TangentAt(arcs[j]);
			const size_t target = segments[j].second;
			result.aims[j] = target > j ? NormalizeOr(positions[target] - positions[j], tangent) : tangent;
			result.ups[j] = Orthogonalize(curve.UpAt(arcs[j]), result.aims[j], AnyPerpendicular(result.aims[j]));
		}
		result.curve = points;
		return result;
	}
}
