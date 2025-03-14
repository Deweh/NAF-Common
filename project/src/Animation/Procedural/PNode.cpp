#include "PNode.h"

namespace Animation::Procedural
{
	size_t PNodeInstanceData::GetSizeBytes()
	{
		return 0;
	}

	void PEvaluationContext::UpdateModelSpaceCache(const std::span<ozz::math::SoaTransform>& a_localPose, int a_from, int a_to)
	{
		ozz::animation::LocalToModelJob l2mJob;
		l2mJob.skeleton = skeleton;
		l2mJob.input = ozz::make_span(a_localPose);
		l2mJob.output = ozz::make_span(modelSpaceCache);
		l2mJob.from = a_from;
		l2mJob.to = a_to;
		l2mJob.Run();
	}

	size_t PEvaluationContext::GetSizeBytes() const
	{
		size_t result = sizeof(PEvaluationContext) + std::span(nodeInstances).size_bytes() +
		                std::span(results).size_bytes() + std::span(syncMap).size_bytes();

		for (auto& inst : nodeInstances) {
			if (inst) {
				result += inst->GetSizeBytes();
			}
		}

		return result;
	}

	std::unique_ptr<PNodeInstanceData> PNode::CreateInstanceData()
	{
		return nullptr;
	}

	void PNode::AdvanceTime(PNodeInstanceData* a_instanceData, float a_deltaTime)
	{
	}

	void PNode::Synchronize(PNodeInstanceData* a_instanceData, PNodeInstanceData* a_ownerInstance, float a_correctionDelta)
	{
	}

	bool PNode::SetCustomValues(const std::span<PEvaluationResult>& a_values, const OzzSkeleton* a_skeleton, const std::filesystem::path& a_localDir)
	{
		return true;
	}

	PNode::Registration* PNode::GetTypeInfo()
	{
		return nullptr;
	}

	size_t PNode::GetSizeBytes()
	{
		return 0;
	}

	PNode::Registration::Registration(const char* a_typeName,
		const std::vector<InputConnection>& a_inputs,
		const std::vector<std::pair<const char*, size_t>>& a_customValues,
		size_t a_output,
		CreationFunctor a_createFunctor,
		const char* a_typeDisplayName,
		CATEGORY a_category,
		const char* a_outputDisplayName) :
		typeName(a_typeName),
		inputs(a_inputs),
		customValues(a_customValues),
		output(a_output),
		createFunctor(a_createFunctor),
		typeDisplayName(a_typeDisplayName),
		nodeCategory(a_category),
		outputDisplayName(a_outputDisplayName)
	{
		GetRegisteredNodeTypes()[{ a_typeName }] = this;
	}

	std::unordered_map<std::string_view, PNode::Registration*>& GetRegisteredNodeTypes()
	{
		static std::unordered_map<std::string_view, PNode::Registration*> instance;
		return instance;
	}

	PNode::InputConnection::InputConnection(const char* a_name, size_t a_evalType, bool a_optional, const char* a_displayName) :
		name(a_name), evalType(a_evalType), optional(a_optional), displayName(a_displayName)
	{
	}
}