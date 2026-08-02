#pragma once
#include "Animation/Studio/StudioTypes.h"

// Vector, matrix and quaternion helpers shared by the Studio's model and UI. Pure: no game or project state.
// Don't add helpers named like ozz::math functions (Cross, Dot, Lerp...): ADL finds both.
namespace Animation::Studio
{
	[[nodiscard]] ozz::math::Float3 GetTranslation(const ozz::math::Float4x4& a_matrix);
	[[nodiscard]] ozz::math::Quaternion GetRotation(const ozz::math::Float4x4& a_matrix);  // Identity if the matrix isn't affine.
	[[nodiscard]] ozz::math::Float3 GetScale(const ozz::math::Float4x4& a_matrix);         // One if the matrix isn't affine.
	[[nodiscard]] ozz::math::Float4x4 MakeTranslation(const ozz::math::Float3& a_translation);
	[[nodiscard]] ozz::math::Float3 TransformPoint(const ozz::math::Float4x4& a_matrix, const ozz::math::Float3& a_point);
	[[nodiscard]] ozz::math::Float3 TransformDirection(const ozz::math::Float4x4& a_matrix, const ozz::math::Float3& a_direction);
	[[nodiscard]] std::optional<ozz::math::Float4x4> TryInvert(const ozz::math::Float4x4& a_matrix);

	[[nodiscard]] ozz::math::Float3 NormalizeOr(const ozz::math::Float3& a_value, const ozz::math::Float3& a_fallback);
	// The part of a_vector perpendicular to the unit vector a_axis, normalized.
	[[nodiscard]] ozz::math::Float3 Orthogonalize(const ozz::math::Float3& a_vector, const ozz::math::Float3& a_axis, const ozz::math::Float3& a_fallback);
	// Any unit vector perpendicular to the unit vector a_axis.
	[[nodiscard]] ozz::math::Float3 AnyPerpendicular(const ozz::math::Float3& a_axis);
	// Rotates a_vector about the unit vector a_axis.
	[[nodiscard]] ozz::math::Float3 RotateAbout(const ozz::math::Float3& a_vector, const ozz::math::Float3& a_axis, float a_angle);
	// The signed angle about the unit vector a_axis that turns a_from toward a_to, both projected across it.
	[[nodiscard]] float AngleAbout(const ozz::math::Float3& a_from, const ozz::math::Float3& a_to, const ozz::math::Float3& a_axis);
	[[nodiscard]] float Smoothstep(float a_edge0, float a_edge1, float a_value);

	[[nodiscard]] ozz::math::Quaternion ToQuaternion(const ozz::math::SimdQuaternion& a_value);
	[[nodiscard]] ozz::math::SimdQuaternion ToSimdQuaternion(const ozz::math::Quaternion& a_value);
	// The rotation taking a_fromAim to a_toAim and a_fromUp (projected) to a_toUp (projected).
	[[nodiscard]] ozz::math::Quaternion RotationBetweenFrames(const ozz::math::Float3& a_fromAim, const ozz::math::Float3& a_fromUp, const ozz::math::Float3& a_toAim, const ozz::math::Float3& a_toUp);
	[[nodiscard]] ozz::math::Quaternion EnsureSameHemisphere(const ozz::math::Quaternion& a_reference, const ozz::math::Quaternion& a_value);
	// Not ozz::math::SLerp: it returns its first argument unchanged when the inputs are within ~5 degrees, which turns
	// small interpolations (and Bezier control point blending) into a step.
	[[nodiscard]] ozz::math::Quaternion SlerpShortest(const ozz::math::Quaternion& a_start, const ozz::math::Quaternion& a_end, float a_t);
	[[nodiscard]] ozz::math::Quaternion QuaternionExp(const ozz::math::Float3& a_value);
	[[nodiscard]] ozz::math::Float3 QuaternionLog(const ozz::math::Quaternion& a_value);

	// Quaternions are normalized; other values pass through.
	template <class T>
	[[nodiscard]] T Normalized(const T& a_value)
	{
		if constexpr (std::is_same_v<T, ozz::math::Quaternion>) {
			return ozz::math::NormalizeSafe(a_value, ozz::math::Quaternion::identity());
		} else {
			return a_value;
		}
	}

	// ---- Scene placement ----

	// Heading h turns +Y (forward) to (sin h, cos h), like the game's angle.z.
	[[nodiscard]] ozz::math::Float4x4 ToMatrix(const ScenePlacement& a_placement);
	[[nodiscard]] ScenePlacement ToPlacement(const ozz::math::Float4x4& a_matrix);  // Keeps only translation and heading.
	[[nodiscard]] ozz::math::Float4x4 ToMatrix(const LocatorDefaults& a_transform);   // Rotation normalized.
	// Nullopt if the matrix isn't affine. Rotation normalized.
	[[nodiscard]] std::optional<LocatorDefaults> ToTransform(const ozz::math::Float4x4& a_matrix);

	// ---- SoA poses ----
	// One joint of an SoA pose. Not Util::Ozz::ApplySoATransformTranslation: it transposes four rows from
	// translation.x, so its fourth row is rotation.x, which the vector's w overwrites.

	[[nodiscard]] ozz::math::Float3 GetPoseTranslation(std::span<const ozz::math::SoaTransform> a_pose, int32_t a_joint);
	[[nodiscard]] ozz::math::Quaternion GetPoseRotation(std::span<const ozz::math::SoaTransform> a_pose, int32_t a_joint);
	void SetPoseTranslation(std::span<ozz::math::SoaTransform> a_pose, int32_t a_joint, const ozz::math::Float3& a_value);

	struct JointTransform
	{
		ozz::math::Float3 translation;
		ozz::math::Quaternion rotation;
		ozz::math::Float3 scale;
	};

	[[nodiscard]] std::vector<JointTransform> UnpackSoaTransforms(std::span<const ozz::math::SoaTransform> a_soa, size_t a_count);
}
