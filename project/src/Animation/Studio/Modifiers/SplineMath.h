#pragma once

// Spline IK's curve and bone placement, in model space. Pure.
namespace Animation::Studio
{
	// Bones closer together than this, as a fraction of the chain's length, share one point on the curve: for
	// example zero-length _OFFSET bones between the bones that shape the chain. The floor covers chains of
	// almost nothing but such bones.
	inline constexpr float kSplineCoincidentFraction = 0.01f;
	inline constexpr float kSplineCoincidentMinimum = 0.0001f;

	// a_lengths, here and below: per bone, the distance from the previous bone on the path. a_lengths[0] is unused.
	[[nodiscard]] float GetSplineCoincidentDistance(std::span<const float> a_lengths);
	// The two bones whose positions give each bone's aim, {from, to}. A bone aims at the first later bone that's
	// apart from it, skipping bones that share its point. Bones at the end with none (the tip, and any bones on top
	// of it) aim along the direction from the last earlier bone that's apart, so from < bone. Both indices are the
	// bone itself when the whole chain shares one point.
	[[nodiscard]] std::vector<std::pair<size_t, size_t>> GetSplineAimSegments(std::span<const float> a_lengths);
	// The bones a control can sit on, one per point on the curve: the start bone, the first bone of each later
	// point, and the end bone in place of the first bone of the last point, so the last control lands on it.
	[[nodiscard]] std::vector<size_t> GetSplineControlCandidates(std::span<const float> a_lengths);

	struct SplineInput
	{
		std::vector<ozz::math::Float3> controls;    // Positions, model space.
		ozz::math::Float3 startAim;                 // Unit directions, model space.
		ozz::math::Float3 endAim;
		std::vector<ozz::math::Float3> controlUps;  // Unit directions, model space. One per control.
		std::vector<float> lengths;                 // Per bone: the distance from the previous bone. lengths[0] is unused.
		ozz::math::Float3 root;                     // The first bone's FK position.
		bool rootFollowsCurve{ false };
		float tangentLength{ 1.0f };
		float twist{ 0.0f };
		float stretch{ 0.0f };
	};

	struct SplineResult
	{
		std::vector<ozz::math::Float3> positions;  // Per bone.
		std::vector<ozz::math::Float3> aims;       // Per bone: toward the next bone; the tip along the curve.
		std::vector<ozz::math::Float3> ups;        // Per bone, perpendicular to its aim.
		std::vector<ozz::math::Float3> curve;      // Samples.
	};

	// Centripetal Catmull-Rom through the controls, each bone placed at its rest distance from the previous one.
	[[nodiscard]] SplineResult SolveSpline(const SplineInput& a_input);
}
