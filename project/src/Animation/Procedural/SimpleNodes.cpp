#include "PNode.h"
#include "Util/Ozz.h"
#include "Settings/Settings.h"

namespace Animation::Procedural
{
	/*
	=============
	Node Template
	=============
	
	class P##NAME##Node : public PNodeT<P##NAME##Node>
	{
	public:
		virtual PEvaluationResult Evaluate(PNodeInstanceData* a_instanceData, PoseCache& a_poseCache, PEvaluationContext& a_evalContext) override {

		}

		virtual bool SetCustomValues(const std::span<PEvaluationResult>& a_values, const std::string_view a_skeleton) override {

			return true;
		}

		inline static Registration _reg{
			"graphTypeName",
			{
				//inputValues
				{ "input", PEvaluationType<PoseCache::Handle> },
			},
			{
				//customValues
				{ "name", PEvaluationType<RE::BSFixedString> },
			},
			PEvaluationType<retnType>,
			CreateNodeOfType<P##NAME##Node>
		};
	};
	*/

	class PLocalToModelNode : public PNodeT<PLocalToModelNode>
	{
	public:
		uint16_t parentBoneIdx;

		virtual PEvaluationResult Evaluate(PNodeInstanceData* a_instanceData, PoseCache& a_poseCache, PEvaluationContext& a_evalContext) override
		{
			using namespace ozz::math;
			const auto pose = std::get<PoseCache::Handle>(a_evalContext.results[inputs[0]]).get();
			const Float4 vec = std::get<Float4>(a_evalContext.results[inputs[1]]);

			a_evalContext.UpdateModelSpaceCache(pose, ozz::animation::Skeleton::kNoParent, parentBoneIdx);
			const SimdFloat4 modelSpace = TransformPoint(a_evalContext.modelSpaceCache[parentBoneIdx], simd_float4::Load3PtrU(&vec.x));

			Float4 result;
			StorePtrU(modelSpace, &result.x);
			return result;
		}

		virtual bool SetCustomValues(const std::span<PEvaluationResult>& a_values, const OzzSkeleton* a_skeleton, const std::filesystem::path& a_localDir) override
		{
			const RE::BSFixedString& boneName = std::get<RE::BSFixedString>(a_values[0]);
			const auto idxs = Util::Ozz::GetJointIndexes(a_skeleton->data.get(), boneName.c_str());

			if (!idxs.has_value()) {
				return false;
			}

			parentBoneIdx = idxs->at(0);

			return true;
		}

		inline static Registration _reg{
			"local_to_model",
			{
				{ "pose", PEvaluationType<PoseCache::Handle> },
				{ "vec", PEvaluationType<ozz::math::Float4> }
			},
			{
				{ "parentBone", PEvaluationType<RE::BSFixedString> },
			},
			PEvaluationType<ozz::math::Float4>,
			CreateNodeOfType<PLocalToModelNode>
		};
	};

	class PGetBoneRotationNode : public PNodeT<PGetBoneRotationNode>
	{
	public:
		bool isModelSpace;
		uint16_t boneIdx;

		virtual PEvaluationResult Evaluate(PNodeInstanceData* a_instanceData, PoseCache& a_poseCache, PEvaluationContext& a_evalContext) override
		{
			PoseCache::Handle& input = std::get<PoseCache::Handle>(a_evalContext.results[inputs[0]]);
			ozz::math::SimdQuaternion rotation;

			if (!isModelSpace) {
				rotation = Util::Ozz::GetSoATransformQuaternion(boneIdx, input.get());
			} else {
				a_evalContext.UpdateModelSpaceCache(input.get(), ozz::animation::Skeleton::kNoParent, boneIdx);
				rotation = Util::Ozz::ToNormalizedQuaternion(a_evalContext.modelSpaceCache[boneIdx]);
			}

			ozz::math::Float4 result;
			ozz::math::StorePtrU(rotation.xyzw, &result.x);
			return result;
			
		}

		virtual bool SetCustomValues(const std::span<PEvaluationResult>& a_values, const OzzSkeleton* a_skeleton, const std::filesystem::path& a_localDir) override
		{
			const RE::BSFixedString& boneName = std::get<RE::BSFixedString>(a_values[0]);
			isModelSpace = std::get<bool>(a_values[1]);

			const auto idxs = Util::Ozz::GetJointIndexes(a_skeleton->data.get(), boneName.c_str());

			if (!idxs.has_value()) {
				return false;
			}

			boneIdx = idxs->at(0);

			return true;
		}

		inline static Registration _reg{
			"get_bone_rot",
			{
				{ "pose", PEvaluationType<PoseCache::Handle> }
			},
			{
				{ "bone", PEvaluationType<RE::BSFixedString> },
				{ "is_ms", PEvaluationType<bool> }
			},
			PEvaluationType<ozz::math::Float4>,
			CreateNodeOfType<PGetBoneRotationNode>
		};
	};

	class PSetBoneRotationNode : public PNodeT<PSetBoneRotationNode>
	{
	public:
		bool isModelSpace;
		uint16_t boneIdx;
		uint16_t parentIdx;

		virtual PEvaluationResult Evaluate(PNodeInstanceData* a_instanceData, PoseCache& a_poseCache, PEvaluationContext& a_evalContext) override
		{
			PoseCache::Handle& input = GetRequiredInput<PoseCache::Handle>(0, a_evalContext);
			ozz::math::Float4& rot = GetRequiredInput<ozz::math::Float4>(1, a_evalContext);
			PoseCache::Handle output = a_poseCache.acquire_handle();
			auto inputSpan = input.get();
			auto outputSpan = output.get();
			std::copy(inputSpan.begin(), inputSpan.end(), outputSpan.begin());

			const ozz::math::SimdQuaternion inputQuat{ .xyzw = ozz::math::simd_float4::LoadPtrU(&rot.x) };
			if (!isModelSpace) {
				Util::Ozz::ApplySoATransformQuaternion(boneIdx, inputQuat, outputSpan);
			} else {
				a_evalContext.UpdateModelSpaceCache(input.get(), ozz::animation::Skeleton::kNoParent, boneIdx);
				const ozz::math::SimdQuaternion parentQuat = Util::Ozz::ToNormalizedQuaternion(a_evalContext.modelSpaceCache[parentIdx]);
				Util::Ozz::ApplySoATransformQuaternion(boneIdx, Conjugate(parentQuat) * inputQuat, outputSpan);
			}

			return output;
		}

		virtual bool SetCustomValues(const std::span<PEvaluationResult>& a_values, const OzzSkeleton* a_skeleton, const std::filesystem::path& a_localDir) override
		{
			const RE::BSFixedString& boneName = std::get<RE::BSFixedString>(a_values[0]);
			isModelSpace = std::get<bool>(a_values[1]);

			const auto idxs = Util::Ozz::GetJointIndexes(a_skeleton->data.get(), boneName.c_str());

			if (!idxs.has_value()) {
				return false;
			}

			boneIdx = idxs->at(0);

			int16_t prnt = a_skeleton->data->joint_parents()[boneIdx];
			if (prnt == ozz::animation::Skeleton::kNoParent) {
				return false;
			}

			parentIdx = prnt;
			return true;
		}

		inline static Registration _reg{
			"set_bone_rot",
			{
				{ "pose", PEvaluationType<PoseCache::Handle> },
				{ "rot", PEvaluationType<ozz::math::Float4> }
			},
			{
				{ "bone", PEvaluationType<RE::BSFixedString> },
				{ "is_ms", PEvaluationType<bool> }
			},
			PEvaluationType<PoseCache::Handle>,
			CreateNodeOfType<PSetBoneRotationNode>
		};
	};

	class PGetBonePositionNode : public PNodeT<PGetBonePositionNode>
	{
	public:
		bool isModelSpace;
		uint16_t boneIdx;

		virtual PEvaluationResult Evaluate(PNodeInstanceData* a_instanceData, PoseCache& a_poseCache, PEvaluationContext& a_evalContext) override
		{
			PoseCache::Handle& input = std::get<PoseCache::Handle>(a_evalContext.results[inputs[0]]);
			ozz::math::SimdFloat4 position;

			if (!isModelSpace) {
				position = Util::Ozz::GetSoATransformTranslation(boneIdx, input.get());
			} else {
				a_evalContext.UpdateModelSpaceCache(input.get(), ozz::animation::Skeleton::kNoParent, boneIdx);
				position = ozz::math::SetW(a_evalContext.modelSpaceCache[boneIdx].cols[3], ozz::math::simd_float4::zero());
			}

			ozz::math::Float4 result;
			ozz::math::StorePtrU(position, &result.x);
			return result;
			
		}

		virtual bool SetCustomValues(const std::span<PEvaluationResult>& a_values, const OzzSkeleton* a_skeleton, const std::filesystem::path& a_localDir) override
		{
			const RE::BSFixedString& boneName = std::get<RE::BSFixedString>(a_values[0]);
			isModelSpace = std::get<bool>(a_values[1]);

			const auto idxs = Util::Ozz::GetJointIndexes(a_skeleton->data.get(), boneName.c_str());

			if (!idxs.has_value()) {
				return false;
			}

			boneIdx = idxs->at(0);

			return true;
		}

		inline static Registration _reg{
			"get_bone_pos",
			{
				{ "input", PEvaluationType<PoseCache::Handle> }
			},
			{
				{ "bone", PEvaluationType<RE::BSFixedString> },
				{ "ms", PEvaluationType<bool> }
			},
			PEvaluationType<ozz::math::Float4>,
			CreateNodeOfType<PGetBonePositionNode>
		};
	};

	class PSetBonePositionNode : public PNodeT<PSetBonePositionNode>
	{
	public:
		bool isModelSpace;
		uint16_t boneIdx;
		uint16_t parentIdx;

		virtual PEvaluationResult Evaluate(PNodeInstanceData* a_instanceData, PoseCache& a_poseCache, PEvaluationContext& a_evalContext) override
		{
			PoseCache::Handle& input = GetRequiredInput<PoseCache::Handle>(0, a_evalContext);
			ozz::math::Float4& pos = GetRequiredInput<ozz::math::Float4>(1, a_evalContext);
			PoseCache::Handle output = a_poseCache.acquire_handle();
			auto inputSpan = input.get();
			auto outputSpan = output.get();
			std::copy(inputSpan.begin(), inputSpan.end(), outputSpan.begin());

			const ozz::math::SimdFloat4 inputPos = ozz::math::simd_float4::Load3PtrU(&pos.x);
			if (!isModelSpace) {
				Util::Ozz::ApplySoATransformTranslation(boneIdx, inputPos, outputSpan);
			} else {
				a_evalContext.UpdateModelSpaceCache(input.get(), ozz::animation::Skeleton::kNoParent, boneIdx);
				const ozz::math::Float4x4 parentInv = ozz::math::Invert(a_evalContext.modelSpaceCache[parentIdx]);
				Util::Ozz::ApplySoATransformTranslation(boneIdx, ozz::math::TransformPoint(parentInv, inputPos), outputSpan);
			}

			return output;
		}

		virtual bool SetCustomValues(const std::span<PEvaluationResult>& a_values, const OzzSkeleton* a_skeleton, const std::filesystem::path& a_localDir) override
		{
			const RE::BSFixedString& boneName = std::get<RE::BSFixedString>(a_values[0]);
			isModelSpace = std::get<bool>(a_values[1]);

			const auto idxs = Util::Ozz::GetJointIndexes(a_skeleton->data.get(), boneName.c_str());

			if (!idxs.has_value()) {
				return false;
			}

			boneIdx = idxs->at(0);

			int16_t prnt = a_skeleton->data->joint_parents()[boneIdx];
			if (prnt == ozz::animation::Skeleton::kNoParent) {
				return false;
			}

			parentIdx = prnt;
			return true;
		}

		inline static Registration _reg{
			"set_bone_pos",
			{
				{ "pose", PEvaluationType<PoseCache::Handle> },
				{ "position", PEvaluationType<ozz::math::Float4> }
			},
			{
				{ "bone", PEvaluationType<RE::BSFixedString> },
				{ "is_ms", PEvaluationType<bool> }
			},
			PEvaluationType<PoseCache::Handle>,
			CreateNodeOfType<PSetBonePositionNode>
		};
	};

	class PFixedVectorNode : public PNodeT<PFixedVectorNode>
	{
	public:
		ozz::math::Float4 vec;

		virtual PEvaluationResult Evaluate(PNodeInstanceData* a_instanceData, PoseCache& a_poseCache, PEvaluationContext& a_evalContext) override
		{
			return vec;
		}

		virtual bool SetCustomValues(const std::span<PEvaluationResult>& a_values, const OzzSkeleton* a_skeleton, const std::filesystem::path& a_localDir) override
		{
			vec.x = std::get<float>(a_values[0]);
			vec.y = std::get<float>(a_values[1]);
			vec.z = std::get<float>(a_values[2]);
			vec.w = std::get<float>(a_values[3]);
			return true;
		}

		inline static Registration _reg{
			"fixed_vec",
			{
			},
			{
				{ "x", PEvaluationType<float> },
				{ "y", PEvaluationType<float> },
				{ "z", PEvaluationType<float> },
				{ "w", PEvaluationType<float> },
			},
			PEvaluationType<ozz::math::Float4>,
			CreateNodeOfType<PFixedVectorNode>
		};
	};

	class PMakeVectorNode : public PNodeT<PMakeVectorNode>
	{
	public:
		ozz::math::Float4 vec;

		virtual PEvaluationResult Evaluate(PNodeInstanceData* a_instanceData, PoseCache& a_poseCache, PEvaluationContext& a_evalContext) override
		{
			return ozz::math::Float4{
				std::get<float>(a_evalContext.results[inputs[0]]),
				std::get<float>(a_evalContext.results[inputs[1]]),
				std::get<float>(a_evalContext.results[inputs[2]]),
				std::get<float>(a_evalContext.results[inputs[3]]),
			};
		}

		inline static Registration _reg{
			"make_vec",
			{
				{ "x", PEvaluationType<float> },
				{ "y", PEvaluationType<float> },
				{ "z", PEvaluationType<float> },
				{ "w", PEvaluationType<float> },
			},
			{
			},
			PEvaluationType<ozz::math::Float4>,
			CreateNodeOfType<PMakeVectorNode>
		};
	};

	class PAddVectorsNode : public PNodeT<PAddVectorsNode>
	{
	public:
		virtual PEvaluationResult Evaluate(PNodeInstanceData* a_instanceData, PoseCache& a_poseCache, PEvaluationContext& a_evalContext) override
		{
			return std::get<ozz::math::Float4>(a_evalContext.results[inputs[0]]) +
			       std::get<ozz::math::Float4>(a_evalContext.results[inputs[1]]);
		}

		inline static Registration _reg{
			"add_vecs",
			{
				{ "1", PEvaluationType<ozz::math::Float4> },
				{ "2", PEvaluationType<ozz::math::Float4> },
			},
			{
			},
			PEvaluationType<ozz::math::Float4>,
			CreateNodeOfType<PAddVectorsNode>
		};
	};

	class PSubtractVectorsNode : public PNodeT<PSubtractVectorsNode>
	{
	public:
		virtual PEvaluationResult Evaluate(PNodeInstanceData* a_instanceData, PoseCache& a_poseCache, PEvaluationContext& a_evalContext) override
		{
			return std::get<ozz::math::Float4>(a_evalContext.results[inputs[0]]) -
			       std::get<ozz::math::Float4>(a_evalContext.results[inputs[1]]);
		}

		inline static Registration _reg{
			"sub_vecs",
			{
				{ "1", PEvaluationType<ozz::math::Float4> },
				{ "2", PEvaluationType<ozz::math::Float4> },
			},
			{},
			PEvaluationType<ozz::math::Float4>,
			CreateNodeOfType<PSubtractVectorsNode>
		};
	};

	class PDivideVectorsNode : public PNodeT<PDivideVectorsNode>
	{
	public:
		virtual PEvaluationResult Evaluate(PNodeInstanceData* a_instanceData, PoseCache& a_poseCache, PEvaluationContext& a_evalContext) override
		{
			return std::get<ozz::math::Float4>(a_evalContext.results[inputs[0]]) /
			       std::get<ozz::math::Float4>(a_evalContext.results[inputs[1]]);
		}

		inline static Registration _reg{
			"div_vecs",
			{
				{ "1", PEvaluationType<ozz::math::Float4> },
				{ "2", PEvaluationType<ozz::math::Float4> },
			},
			{},
			PEvaluationType<ozz::math::Float4>,
			CreateNodeOfType<PDivideVectorsNode>
		};
	};

	class PMultiplyVectorsNode : public PNodeT<PMultiplyVectorsNode>
	{
	public:
		virtual PEvaluationResult Evaluate(PNodeInstanceData* a_instanceData, PoseCache& a_poseCache, PEvaluationContext& a_evalContext) override
		{
			return std::get<ozz::math::Float4>(a_evalContext.results[inputs[0]]) *
			       std::get<ozz::math::Float4>(a_evalContext.results[inputs[1]]);
		}

		inline static Registration _reg{
			"mult_vecs",
			{
				{ "1", PEvaluationType<ozz::math::Float4> },
				{ "2", PEvaluationType<ozz::math::Float4> },
			},
			{},
			PEvaluationType<ozz::math::Float4>,
			CreateNodeOfType<PMultiplyVectorsNode>
		};
	};

	class PAddRotationVectorsNode : public PNodeT<PAddRotationVectorsNode>
	{
	public:
		virtual PEvaluationResult Evaluate(PNodeInstanceData* a_instanceData, PoseCache& a_poseCache, PEvaluationContext& a_evalContext) override
		{
			const auto& v1 = std::get<ozz::math::Float4>(a_evalContext.results[inputs[0]]);
			const auto& v2 = std::get<ozz::math::Float4>(a_evalContext.results[inputs[1]]);

			ozz::math::SimdQuaternion q1, q2;
			q1.xyzw = ozz::math::simd_float4::LoadPtrU(&v1.x);
			q2.xyzw = ozz::math::simd_float4::LoadPtrU(&v2.x);
			const ozz::math::SimdQuaternion q3 = q1 * q2;
			ozz::math::Float4 result;
			ozz::math::StorePtrU(q3.xyzw, &result.x);
			return result;
		}

		inline static Registration _reg{
			"add_rot_vecs",
			{
				{ "1", PEvaluationType<ozz::math::Float4> },
				{ "2", PEvaluationType<ozz::math::Float4> },
			},
			{},
			PEvaluationType<ozz::math::Float4>,
			CreateNodeOfType<PAddRotationVectorsNode>
		};
	};

	class PSubtractRotationVectorsNode : public PNodeT<PSubtractRotationVectorsNode>
	{
	public:
		virtual PEvaluationResult Evaluate(PNodeInstanceData* a_instanceData, PoseCache& a_poseCache, PEvaluationContext& a_evalContext) override
		{
			const auto& v1 = std::get<ozz::math::Float4>(a_evalContext.results[inputs[0]]);
			const auto& v2 = std::get<ozz::math::Float4>(a_evalContext.results[inputs[1]]);

			ozz::math::SimdQuaternion q1, q2;
			q1.xyzw = ozz::math::simd_float4::LoadPtrU(&v1.x);
			q2.xyzw = ozz::math::simd_float4::LoadPtrU(&v2.x);
			q2 = ozz::math::Conjugate(q2);
			const ozz::math::SimdQuaternion q3 = q1 * q2;
			ozz::math::Float4 result;
			ozz::math::StorePtrU(q3.xyzw, &result.x);
			return result;
		}

		inline static Registration _reg{
			"sub_rot_vecs",
			{
				{ "1", PEvaluationType<ozz::math::Float4> },
				{ "2", PEvaluationType<ozz::math::Float4> },
			},
			{},
			PEvaluationType<ozz::math::Float4>,
			CreateNodeOfType<PSubtractRotationVectorsNode>
		};
	};
}

