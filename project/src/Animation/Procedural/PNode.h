#pragma once
#include "Animation/PoseCache.h"
#include "Animation/Ozz.h"
#include "Util/General.h"
#include "Physics/Constraint.h"

namespace Physics
{
	class ModelSpaceSystem;
}

namespace Animation::Procedural
{
	struct PDataObject;
	class PNode;
	class PGraph;
	struct PVariableInstance;
	using PEvaluationResult = std::variant<float, PoseCache::Handle, uint64_t, RE::BSFixedString, ozz::math::Float4, bool, PDataObject*>;

	template <typename T>
	inline constexpr std::size_t PEvaluationType = variant_index<T, PEvaluationResult>::value;

	struct PNodeInstanceData
	{
		virtual size_t GetSizeBytes();
		virtual ~PNodeInstanceData() = default;
	};

	template <typename T>
	struct PNodeInstanceDataT : public PNodeInstanceData
	{
		virtual size_t GetSizeBytes() override
		{
			return sizeof(T);
		}
	};

	struct PEvaluationContext
	{
		struct SyncData
		{
			PNode* node;
			PNodeInstanceData* selfInstData;
			PNodeInstanceData* ownerInstData;
		};

		std::vector<std::unique_ptr<PNodeInstanceData>> nodeInstances;
		std::vector<PEvaluationResult> results;
		std::unordered_map<std::string_view, PVariableInstance*> variableMap;
		PEvaluationContext* lastSyncOwner = nullptr;
		std::vector<SyncData> syncMap;

		PoseCache::Handle* restPose = nullptr;
		const ozz::animation::Skeleton* skeleton = nullptr;
		std::span<ozz::math::Float4x4> modelSpaceCache;
		Physics::ModelSpaceSystem* physSystem = nullptr;

		void UpdateModelSpaceCache(const std::span<ozz::math::SoaTransform>& a_localPose,
			int a_from = ozz::animation::Skeleton::kNoParent,
			int a_to = ozz::animation::Skeleton::kMaxJoints);

		size_t GetSizeBytes() const;
	};

	class PNode
	{
	public:
		struct InputConnection
		{
			InputConnection(const char* a_name, size_t a_evalType, bool a_optional = false);

			const char* name;
			size_t evalType;
			bool optional;
		};

		struct Registration
		{
			typedef std::unique_ptr<PNode> (*CreationFunctor)();

			Registration(const char* a_typeName,
				const std::vector<InputConnection>& a_inputs,
				const std::vector<std::pair<const char*, size_t>>& a_customValues,
				size_t a_output,
				CreationFunctor a_createFunctor);

			const char* typeName;
			std::vector<InputConnection> inputs;
			std::vector<std::pair<const char*, size_t>> customValues;
			size_t output;
			CreationFunctor createFunctor;
		};

		uint64_t syncId = UINT64_MAX;
		std::vector<uint64_t> inputs;

		virtual std::unique_ptr<PNodeInstanceData> CreateInstanceData();
		virtual PEvaluationResult Evaluate(PNodeInstanceData* a_instanceData, PoseCache& a_poseCache, PEvaluationContext& a_evalContext) = 0;
		virtual void AdvanceTime(PNodeInstanceData* a_instanceData, float a_deltaTime);
		virtual void Synchronize(PNodeInstanceData* a_instanceData, PNodeInstanceData* a_ownerInstance, float a_correctionDelta);
		virtual bool SetCustomValues(const std::span<PEvaluationResult>& a_values, const OzzSkeleton* a_skeleton, const std::filesystem::path& a_localDir);
		virtual Registration* GetTypeInfo();
		virtual size_t GetSizeBytes();
		virtual ~PNode() = default;

		template <typename T>
		static std::unique_ptr<PNode> CreateNodeOfType()
		{
			return std::make_unique<T>();
		}

	protected:
		template <typename T>
		inline T& GetRequiredInput(size_t a_idx, PEvaluationContext& a_evalContext)
		{
			return std::get<T>(a_evalContext.results[inputs[a_idx]]);
		}

		template <typename T>
		inline T GetOptionalInput(size_t a_idx, const T& a_default, PEvaluationContext& a_evalContext)
		{
			size_t resIdx = inputs[a_idx];
			if (resIdx == UINT64_MAX) {
				return a_default;
			} else {
				return GetRequiredInput<T>(a_idx, a_evalContext);
			}
		}
	};

	std::unordered_map<std::string_view, PNode::Registration*>& GetRegisteredNodeTypes();

	template <typename T>
	class PNodeT : public PNode
	{
	public:
		virtual Registration* GetTypeInfo() override
		{
			return &T::_reg;
		}

		virtual size_t GetSizeBytes() override
		{
			return sizeof(T);
		}
	};

	template <typename T>
	bool IsNodeOfType(PNode* a_node)
	{
		return a_node->GetTypeInfo() == &T::_reg;
	}
}