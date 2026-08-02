#include "StudioMath.h"

namespace Animation::Studio
{
	ozz::math::Float3 GetTranslation(const ozz::math::Float4x4& a_matrix)
	{
		ozz::math::Float3 result;
		ozz::math::Store3PtrU(a_matrix.cols[3], &result.x);
		return result;
	}

	ozz::math::Quaternion GetRotation(const ozz::math::Float4x4& a_matrix)
	{
		ozz::math::Float3 translation;
		ozz::math::Quaternion rotation;
		ozz::math::Float3 scale;
		if (!ozz::math::ToAffine(a_matrix, &translation, &rotation, &scale)) return ozz::math::Quaternion::identity();
		return ozz::math::NormalizeSafe(rotation, ozz::math::Quaternion::identity());
	}

	ozz::math::Float3 GetScale(const ozz::math::Float4x4& a_matrix)
	{
		ozz::math::Float3 translation;
		ozz::math::Quaternion rotation;
		ozz::math::Float3 scale;
		if (!ozz::math::ToAffine(a_matrix, &translation, &rotation, &scale)) return ozz::math::Float3::one();
		return scale;
	}

	ozz::math::Float4x4 MakeTranslation(const ozz::math::Float3& a_translation)
	{
		return ozz::math::Float4x4::Translation(ozz::math::simd_float4::Load3PtrU(&a_translation.x));
	}

	ozz::math::Float3 TransformPoint(const ozz::math::Float4x4& a_matrix, const ozz::math::Float3& a_point)
	{
		ozz::math::Float3 result;
		ozz::math::Store3PtrU(ozz::math::TransformPoint(a_matrix, ozz::math::simd_float4::Load3PtrU(&a_point.x)), &result.x);
		return result;
	}

	ozz::math::Float3 TransformDirection(const ozz::math::Float4x4& a_matrix, const ozz::math::Float3& a_direction)
	{
		ozz::math::Float3 result;
		ozz::math::Store3PtrU(ozz::math::TransformVector(a_matrix, ozz::math::simd_float4::Load3PtrU(&a_direction.x)), &result.x);
		return result;
	}

	std::optional<ozz::math::Float4x4> TryInvert(const ozz::math::Float4x4& a_matrix)
	{
		ozz::math::SimdInt4 invertible;
		const auto inverse = ozz::math::Invert(a_matrix, &invertible);
		return ozz::math::AreAllTrue1(invertible) ? std::optional{ inverse } : std::nullopt;
	}

	ozz::math::Float3 NormalizeOr(const ozz::math::Float3& a_value, const ozz::math::Float3& a_fallback)
	{
		const float length = ozz::math::Length(a_value);
		return length > 0.00001f ? a_value / length : a_fallback;
	}

	ozz::math::Float3 Orthogonalize(const ozz::math::Float3& a_vector, const ozz::math::Float3& a_axis, const ozz::math::Float3& a_fallback)
	{
		return NormalizeOr(a_vector - a_axis * ozz::math::Dot(a_vector, a_axis), a_fallback);
	}

	ozz::math::Float3 AnyPerpendicular(const ozz::math::Float3& a_axis)
	{
		const ozz::math::Float3 reference = std::abs(a_axis.z) < 0.9f ? ozz::math::Float3{ 0.0f, 0.0f, 1.0f } : ozz::math::Float3{ 1.0f, 0.0f, 0.0f };
		return NormalizeOr(ozz::math::Cross(a_axis, reference), { 0.0f, 1.0f, 0.0f });
	}

	ozz::math::Float3 RotateAbout(const ozz::math::Float3& a_vector, const ozz::math::Float3& a_axis, float a_angle)
	{
		return ozz::math::TransformVector(ozz::math::Quaternion::FromAxisAngle(a_axis, a_angle), a_vector);
	}

	float AngleAbout(const ozz::math::Float3& a_from, const ozz::math::Float3& a_to, const ozz::math::Float3& a_axis)
	{
		const auto from = a_from - a_axis * ozz::math::Dot(a_from, a_axis);
		const auto to = a_to - a_axis * ozz::math::Dot(a_to, a_axis);
		return std::atan2(ozz::math::Dot(ozz::math::Cross(from, to), a_axis), ozz::math::Dot(from, to));
	}

	float Smoothstep(float a_edge0, float a_edge1, float a_value)
	{
		if (!(a_edge1 > a_edge0)) return a_value >= a_edge1 ? 1.0f : 0.0f;
		const float t = std::clamp((a_value - a_edge0) / (a_edge1 - a_edge0), 0.0f, 1.0f);
		return t * t * (3.0f - 2.0f * t);
	}

	ozz::math::Quaternion ToQuaternion(const ozz::math::SimdQuaternion& a_value)
	{
		ozz::math::Quaternion result;
		ozz::math::StorePtrU(a_value.xyzw, &result.x);
		return result;
	}

	ozz::math::SimdQuaternion ToSimdQuaternion(const ozz::math::Quaternion& a_value)
	{
		return { ozz::math::simd_float4::LoadPtrU(&a_value.x) };
	}

	ozz::math::Quaternion RotationBetweenFrames(const ozz::math::Float3& a_fromAim, const ozz::math::Float3& a_fromUp, const ozz::math::Float3& a_toAim, const ozz::math::Float3& a_toUp)
	{
		const auto basis = [](const ozz::math::Float3& a_aim, const ozz::math::Float3& a_up) {
			const auto aim = NormalizeOr(a_aim, { 1.0f, 0.0f, 0.0f });
			const auto up = Orthogonalize(a_up, aim, AnyPerpendicular(aim));
			const auto side = ozz::math::Cross(aim, up);
			ozz::math::Float4x4 result;
			result.cols[0] = ozz::math::simd_float4::Load(aim.x, aim.y, aim.z, 0.0f);
			result.cols[1] = ozz::math::simd_float4::Load(up.x, up.y, up.z, 0.0f);
			result.cols[2] = ozz::math::simd_float4::Load(side.x, side.y, side.z, 0.0f);
			result.cols[3] = ozz::math::simd_float4::w_axis();
			return result;
		};
		return GetRotation(basis(a_toAim, a_toUp) * ozz::math::Transpose(basis(a_fromAim, a_fromUp)));
	}

	ozz::math::Quaternion EnsureSameHemisphere(const ozz::math::Quaternion& a_reference, const ozz::math::Quaternion& a_value)
	{
		return ozz::math::Dot(a_reference, a_value) < 0.0f ? -a_value : a_value;
	}

	ozz::math::Quaternion SlerpShortest(const ozz::math::Quaternion& a_start, const ozz::math::Quaternion& a_end, float a_t)
	{
		const auto start = ozz::math::NormalizeSafe(a_start, ozz::math::Quaternion::identity());
		const auto end = ozz::math::NormalizeSafe(EnsureSameHemisphere(start, a_end), start);
		const float cosine = std::clamp(ozz::math::Dot(start, end), -1.0f, 1.0f);

		float startWeight = 1.0f - a_t;
		float endWeight = a_t;
		// Nearly parallel: a normalized lerp is accurate here and avoids dividing by a vanishing sine.
		if (cosine < 0.9995f) {
			const float angle = std::acos(cosine);
			const float sine = std::sin(angle);
			startWeight = std::sin((1.0f - a_t) * angle) / sine;
			endWeight = std::sin(a_t * angle) / sine;
		}
		const ozz::math::Quaternion result{
			start.x * startWeight + end.x * endWeight,
			start.y * startWeight + end.y * endWeight,
			start.z * startWeight + end.z * endWeight,
			start.w * startWeight + end.w * endWeight
		};
		return ozz::math::NormalizeSafe(result, start);
	}

	ozz::math::Quaternion QuaternionExp(const ozz::math::Float3& a_value)
	{
		const float angle = ozz::math::Length(a_value);
		if (angle <= 0.000001f) return { a_value.x, a_value.y, a_value.z, 1.0f };
		const float scale = std::sin(angle) / angle;
		return { a_value.x * scale, a_value.y * scale, a_value.z * scale, std::cos(angle) };
	}

	ozz::math::Float3 QuaternionLog(const ozz::math::Quaternion& a_value)
	{
		const auto q = ozz::math::NormalizeSafe(a_value, ozz::math::Quaternion::identity());
		const float angle = std::acos(std::clamp(q.w, -1.0f, 1.0f));
		const float sine = std::sin(angle);
		if (std::abs(sine) <= 0.000001f) return ozz::math::Float3::zero();
		const float scale = angle / sine;
		return { q.x * scale, q.y * scale, q.z * scale };
	}

	// ---- Scene placement ----

	ozz::math::Float4x4 ToMatrix(const ScenePlacement& a_placement)
	{
		const float sine = std::sin(a_placement.heading);
		const float cosine = std::cos(a_placement.heading);
		ozz::math::Float4x4 result;
		result.cols[0] = ozz::math::simd_float4::Load(cosine, -sine, 0.0f, 0.0f);
		result.cols[1] = ozz::math::simd_float4::Load(sine, cosine, 0.0f, 0.0f);
		result.cols[2] = ozz::math::simd_float4::z_axis();
		result.cols[3] = ozz::math::simd_float4::Load(a_placement.translation.x, a_placement.translation.y, a_placement.translation.z, 1.0f);
		return result;
	}

	ScenePlacement ToPlacement(const ozz::math::Float4x4& a_matrix)
	{
		float forward[4];
		ozz::math::StorePtrU(a_matrix.cols[1], forward);
		const bool level = std::abs(forward[0]) < 0.000001f && std::abs(forward[1]) < 0.000001f;
		return { GetTranslation(a_matrix), level ? 0.0f : std::atan2(forward[0], forward[1]) };
	}

	ozz::math::Float4x4 ToMatrix(const LocatorDefaults& a_transform)
	{
		return ozz::math::Float4x4::FromAffine(a_transform.translation, Normalized(a_transform.rotation), a_transform.scale);
	}

	std::optional<LocatorDefaults> ToTransform(const ozz::math::Float4x4& a_matrix)
	{
		LocatorDefaults result;
		if (!ozz::math::ToAffine(a_matrix, &result.translation, &result.rotation, &result.scale)) return std::nullopt;
		result.rotation = Normalized(result.rotation);
		return result;
	}

	// ---- SoA poses ----

	ozz::math::Float3 GetPoseTranslation(std::span<const ozz::math::SoaTransform> a_pose, int32_t a_joint)
	{
		const auto& soa = a_pose[a_joint / 4];
		const int32_t lane = a_joint & 3;
		float x[4], y[4], z[4];
		ozz::math::StorePtrU(soa.translation.x, x);
		ozz::math::StorePtrU(soa.translation.y, y);
		ozz::math::StorePtrU(soa.translation.z, z);
		return { x[lane], y[lane], z[lane] };
	}

	ozz::math::Quaternion GetPoseRotation(std::span<const ozz::math::SoaTransform> a_pose, int32_t a_joint)
	{
		const auto& soa = a_pose[a_joint / 4];
		const int32_t lane = a_joint & 3;
		float x[4], y[4], z[4], w[4];
		ozz::math::StorePtrU(soa.rotation.x, x);
		ozz::math::StorePtrU(soa.rotation.y, y);
		ozz::math::StorePtrU(soa.rotation.z, z);
		ozz::math::StorePtrU(soa.rotation.w, w);
		return { x[lane], y[lane], z[lane], w[lane] };
	}

	void SetPoseTranslation(std::span<ozz::math::SoaTransform> a_pose, int32_t a_joint, const ozz::math::Float3& a_value)
	{
		auto& soa = a_pose[a_joint / 4];
		const int32_t lane = a_joint & 3;
		float x[4], y[4], z[4];
		ozz::math::StorePtrU(soa.translation.x, x);
		ozz::math::StorePtrU(soa.translation.y, y);
		ozz::math::StorePtrU(soa.translation.z, z);
		x[lane] = a_value.x;
		y[lane] = a_value.y;
		z[lane] = a_value.z;
		soa.translation = ozz::math::SoaFloat3::Load(ozz::math::simd_float4::LoadPtrU(x), ozz::math::simd_float4::LoadPtrU(y), ozz::math::simd_float4::LoadPtrU(z));
	}

	std::vector<JointTransform> UnpackSoaTransforms(std::span<const ozz::math::SoaTransform> a_soa, size_t a_count)
	{
		std::vector<JointTransform> result(a_count);
		for (size_t i = 0; i < a_soa.size(); ++i) {
			float tx[4], ty[4], tz[4], rx[4], ry[4], rz[4], rw[4], sx[4], sy[4], sz[4];
			ozz::math::StorePtrU(a_soa[i].translation.x, tx);
			ozz::math::StorePtrU(a_soa[i].translation.y, ty);
			ozz::math::StorePtrU(a_soa[i].translation.z, tz);
			ozz::math::StorePtrU(a_soa[i].rotation.x, rx);
			ozz::math::StorePtrU(a_soa[i].rotation.y, ry);
			ozz::math::StorePtrU(a_soa[i].rotation.z, rz);
			ozz::math::StorePtrU(a_soa[i].rotation.w, rw);
			ozz::math::StorePtrU(a_soa[i].scale.x, sx);
			ozz::math::StorePtrU(a_soa[i].scale.y, sy);
			ozz::math::StorePtrU(a_soa[i].scale.z, sz);
			for (size_t lane = 0; lane < 4 && i * 4 + lane < a_count; ++lane) {
				result[i * 4 + lane] = {
					{ tx[lane], ty[lane], tz[lane] },
					{ rx[lane], ry[lane], rz[lane], rw[lane] },
					{ sx[lane], sy[lane], sz[lane] }
				};
			}
		}
		return result;
	}
}
