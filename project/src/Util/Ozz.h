#pragma once
#include "Util/String.h"
#include "Animation/Transform.h"

namespace Util::Ozz
{
	struct SimdTransform
	{
		ozz::math::SimdFloat4 translation;
		ozz::math::SimdQuaternion rotation;
		ozz::math::SimdFloat4 scale;
	};

	struct InterpSoaFloat3
	{
		ozz::math::SimdFloat4 ratio[2];
		ozz::math::SoaFloat3 value[2];
	};

	struct InterpSoaQuaternion
	{
		ozz::math::SimdFloat4 ratio[2];
		ozz::math::SoaQuaternion value[2];
	};

	inline size_t CalcContextSizeBytes(int a_numSoaTracks)
	{
		const size_t max_tracks = a_numSoaTracks * 4;
		const size_t num_outdated = (a_numSoaTracks + 7) / 8;
		const size_t size =
			sizeof(ozz::animation::SamplingJob::Context) +
			sizeof(InterpSoaFloat3) * a_numSoaTracks +
			sizeof(InterpSoaQuaternion) * a_numSoaTracks +
			sizeof(InterpSoaFloat3) * a_numSoaTracks +
			sizeof(uint32_t) * max_tracks * 3 +
			sizeof(uint8_t) * 3 * num_outdated;
		return size;
	}

	// This is a reduced version of the LocalToModelJob to simply convert SoaTransforms to Float4x4s without doing any additional math.
	inline void UnpackSoaTransforms(const std::span<ozz::math::SoaTransform>& a_input, const std::span<ozz::math::Float4x4>& a_output, const ozz::animation::Skeleton* a_skeleton)
	{
		const int end = a_skeleton->num_joints();
		if (a_input.size() != a_skeleton->num_soa_joints() || a_output.size() != end) {
			return;
		}

		for (int i = 0, process = i < end; process;) {
			const ozz::math::SoaTransform& transform = a_input[i / 4];
			const ozz::math::SoaFloat4x4 local_soa_matrices = ozz::math::SoaFloat4x4::FromAffine(
				transform.translation, transform.rotation, transform.scale);

			ozz::math::Float4x4 local_aos_matrices[4];
			ozz::math::Transpose16x16(&local_soa_matrices.cols[0].x,
				local_aos_matrices->cols);

			for (const int soa_end = (i + 4) & ~3; i < soa_end && process; ++i, process = i < end) {
				a_output[i] = local_aos_matrices[i & 3];
			}
		}
	}

	inline void PackSoaTransforms(const std::span<ozz::math::Float4x4>& a_input, const std::span<ozz::math::SoaTransform>& a_output, const ozz::animation::Skeleton* a_skeleton)
	{
		const int end = a_skeleton->num_joints();
		if (a_input.size() != end || a_output.size() != a_skeleton->num_soa_joints()) {
			return;
		}

		for (size_t i = 0, k = 0; i < end; i += 4, k++) {
			ozz::math::SimdFloat4 translations[4];
			ozz::math::SimdFloat4 rotations[4];
			ozz::math::SimdFloat4 scales[4];

			size_t remaining = std::min(4ui64, end - i);
			for (int j = 0; j < remaining; j++) {
				ozz::math::ToAffine(a_input[i + j], &translations[j], &rotations[j], &scales[j]);
			}

			auto& out = a_output[k];
			ozz::math::Transpose4x3(translations, &out.translation.x);
			ozz::math::Transpose4x4(rotations, &out.rotation.x);
			ozz::math::Transpose4x3(scales, &out.scale.x);
		}
	}

	inline void PackSoaTransforms(const std::span<Animation::Transform>& a_input, const std::span<ozz::math::SoaTransform>& a_output, const ozz::animation::Skeleton* a_skeleton)
	{
		const int end = a_skeleton->num_joints();
		if (a_input.size() != end || a_output.size() != a_skeleton->num_soa_joints()) {
			return;
		}

		const ozz::math::SimdFloat4 one = ozz::math::simd_float4::one();

		for (size_t i = 0, k = 0; i < end; i += 4, k++) {
			ozz::math::SimdFloat4 translations[4];
			ozz::math::SimdFloat4 rotations[4];

			size_t remaining = std::min(4ui64, end - i);
			for (int j = 0; j < remaining; j++) {
				const Animation::Transform& cur = a_input[i + j];
				translations[j] = ozz::math::simd_float4::Load3PtrU(&cur.translate.x);
				rotations[j] = ozz::math::simd_float4::Load(cur.rotate.x, cur.rotate.y, cur.rotate.z, cur.rotate.w);
			}

			auto& out = a_output[k];
			ozz::math::Transpose4x3(translations, &out.translation.x);
			ozz::math::Transpose4x4(rotations, &out.rotation.x);
			out.scale = ozz::math::SoaFloat3::Load(one, one, one);
		}
	}

	inline void ApplySoATransformTranslation(int32_t a_index, const ozz::math::SimdFloat4& a_trans, const std::span<ozz::math::SoaTransform>& a_transforms)
	{
		ozz::math::SoaTransform& soa_transform_ref = a_transforms[a_index / 4];
		ozz::math::SimdFloat4 aos_trans[4];
		ozz::math::Transpose4x4(&soa_transform_ref.translation.x, aos_trans);

		ozz::math::SimdFloat4& aos_trans_ref = aos_trans[a_index & 3];
		aos_trans_ref = a_trans;

		ozz::math::Transpose4x4(aos_trans, &soa_transform_ref.translation.x);
	}

	inline void ApplySoATransformQuaternion(int32_t a_index, const ozz::math::SimdQuaternion& a_quat, const std::span<ozz::math::SoaTransform>& a_transforms)
	{
		ozz::math::SoaTransform& soa_transform_ref = a_transforms[a_index / 4];
		ozz::math::SimdQuaternion aos_quats[4];
		ozz::math::Transpose4x4(&soa_transform_ref.rotation.x, &aos_quats->xyzw);

		aos_quats[a_index & 3] = a_quat;

		ozz::math::Transpose4x4(&aos_quats->xyzw, &soa_transform_ref.rotation.x);
	}

	inline void MultiplySoATransformQuaternion(int32_t a_index, const ozz::math::SimdQuaternion& a_quat, const std::span<ozz::math::SoaTransform>& a_transforms)
	{
		ozz::math::SoaTransform& soa_transform_ref = a_transforms[a_index / 4];
		ozz::math::SimdQuaternion aos_quats[4];
		ozz::math::Transpose4x4(&soa_transform_ref.rotation.x, &aos_quats->xyzw);

		ozz::math::SimdQuaternion& aos_quat_ref = aos_quats[a_index & 3];
		aos_quat_ref = aos_quat_ref * a_quat;

		ozz::math::Transpose4x4(&aos_quats->xyzw, &soa_transform_ref.rotation.x);
	}

	inline ozz::math::SimdQuaternion GetSoATransformQuaternion(int32_t a_index, const std::span<ozz::math::SoaTransform>& a_transforms)
	{
		ozz::math::SoaTransform& soa_transform_ref = a_transforms[a_index / 4];
		ozz::math::SimdQuaternion aos_quats[4];
		ozz::math::Transpose4x4(&soa_transform_ref.rotation.x, &aos_quats->xyzw);

		return aos_quats[a_index & 3];
	}

	inline ozz::math::SimdFloat4 GetSoATransformTranslation(int32_t a_index, const std::span<ozz::math::SoaTransform>& a_transforms)
	{
		ozz::math::SoaTransform& soa_transform_ref = a_transforms[a_index / 4];
		ozz::math::SimdFloat4 aos_trans[4];
		ozz::math::Transpose4x4(&soa_transform_ref.translation.x, aos_trans);

		return aos_trans[a_index & 3];
	}

	inline ozz::math::SimdQuaternion ToNormalizedQuaternion(const ozz::math::Float4x4& a_transform)
	{
		SimdTransform affineTransform;
		ozz::math::ToAffine(a_transform, &affineTransform.translation, &affineTransform.rotation.xyzw, &affineTransform.scale);
		return affineTransform.rotation;
	}

	inline ozz::math::Float4x4 GetParentTransform(uint16_t a_targetNode,
		const std::span<ozz::math::Float4x4>& a_modelSpace,
		const ozz::animation::Skeleton* a_skeleton)
	{
		int16_t parent = a_skeleton->joint_parents()[a_targetNode];
		if (parent == ozz::animation::Skeleton::kNoParent)
			return ozz::math::Float4x4::identity();

		return a_modelSpace[parent];
	}

	inline void ApplyMSRotationToLocal(const ozz::math::SimdQuaternion& a_rotation,
		uint16_t a_targetNode,
		const std::span<ozz::math::SoaTransform>& a_localSpace,
		const std::span<ozz::math::Float4x4>& a_modelSpace,
		const ozz::animation::Skeleton* a_skeleton)
	{
		const ozz::math::Float4x4 parentTransform = GetParentTransform(a_targetNode, a_modelSpace, a_skeleton);
		const ozz::math::SimdQuaternion parentRotation = ToNormalizedQuaternion(parentTransform);

		ApplySoATransformQuaternion(a_targetNode,
			ozz::math::Conjugate(parentRotation) * a_rotation,
			a_localSpace);
	}

	inline ozz::math::Float4x4& CastNiToOzz(RE::NiTransform& a_matrix)
	{
		return *reinterpret_cast<ozz::math::Float4x4*>(&a_matrix);
	}

	inline RE::NiTransform& CastOzzToNi(ozz::math::Float4x4& a_matrix)
	{
		return *reinterpret_cast<RE::NiTransform*>(&a_matrix);
	}

	template <typename... Args>
	inline std::optional<std::array<uint16_t, sizeof...(Args)>> GetJointIndexes(const ozz::animation::Skeleton* a_skeleton, Args... a_targetNames)
	{
		constexpr size_t numArgs = sizeof...(Args);
		std::array<std::string_view, numArgs> strings = { a_targetNames... };
		std::array<uint16_t, sizeof...(Args)> result;

		for (size_t i = 0; i < numArgs; i++) {
			result[i] = std::numeric_limits<uint16_t>::max();
		}

		const auto jointNames = a_skeleton->joint_names();
		for (auto iter = jointNames.begin(); iter != jointNames.end(); iter++) {
			for (size_t i = 0; i < numArgs; i++) {
				if (Util::String::CaseInsensitiveCompare(strings[i], *iter)) {
					result[i] = std::distance(jointNames.begin(), iter);
				}
			}
		}

		for (size_t i = 0; i < numArgs; i++) {
			if (result[i] == std::numeric_limits<uint16_t>::max()) {
				return std::nullopt;
			}
		}
		return result;
	}

	struct BoneAxes
	{
		ozz::math::SimdFloat4 twist;
		ozz::math::SimdFloat4 forward;
		ozz::math::SimdFloat4 up;
	};

	// Assumes the twist axis is along the bone's length.
	inline BoneAxes GetBoneAxes(const ozz::math::SimdFloat4& a_boneStart, const ozz::math::SimdFloat4& a_boneEnd)
	{
		using namespace ozz::math;
		BoneAxes result;
		result.twist = a_boneEnd - a_boneStart;
		result.forward = Cross3(result.twist, simd_float4::Load(0.0f, 0.0f, 1.0f, 0.0f));
		result.up = Cross3(result.twist, result.forward);
	}

#ifdef TARGET_GAME_F4
	inline RE::hkQsTransformf ToHkTransform(const ozz::math::Transform& a_transform)
	{
		return RE::hkQsTransformf{
			.translation = {
				a_transform.translation.x,
				a_transform.translation.y,
				a_transform.translation.z
			},
			.rotation = {
				a_transform.rotation.x,
				a_transform.rotation.y,
				a_transform.rotation.z,
				a_transform.rotation.w
			},
			.scale = {
				a_transform.scale.x,
				a_transform.scale.y,
				a_transform.scale.z
			}
		};
	}

	inline ozz::math::Transform FromHkTransform(const RE::hkQsTransformf& a_transform)
	{
		return ozz::math::Transform{
			.translation = {
				a_transform.translation.x,
				a_transform.translation.y,
				a_transform.translation.z
			},
			.rotation = {
				a_transform.rotation.x,
				a_transform.rotation.y,
				a_transform.rotation.z,
				a_transform.rotation.w
			},
			.scale = {
				a_transform.scale.x,
				a_transform.scale.y,
				a_transform.scale.z
			}
		};
	}
#endif
}