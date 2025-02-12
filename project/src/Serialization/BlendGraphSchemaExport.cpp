#include "BlendGraphSchemaExport.h"
#include "Animation/Procedural/PDataObject.h"
#include "Animation/Procedural/PNode.h"
#include <fstream>
#include <nlohmann/json.hpp>

namespace Serialization
{
	using namespace Animation::Procedural;

	namespace detail
	{
		const char* EvaluationTypeToString(size_t a_evalType)
		{
			switch(a_evalType) {
				case PEvaluationType<float>:
					return "float";
				case PEvaluationType<Animation::PoseCache::Handle>:
					return "pose";
				case PEvaluationType<uint64_t>:
					return "uint";
				case PEvaluationType<RE::BSFixedString>:
					return "string";
				case PEvaluationType<ozz::math::Float4>:
					return "vector";
				case PEvaluationType<bool>:
					return "bool";
				case PEvaluationType<PDataObject*>:
					return "data";
				default:
					return "unknown";
			}
		}

		const char* CategoryToString(PNode::CATEGORY a_category)
		{
			switch(a_category) {
				case PNode::CATEGORY::kPoseCreators:
					return "PoseCreators";
				case PNode::CATEGORY::kPoseModifiers:
					return "PoseModifiers";
				case PNode::CATEGORY::kValueCreators:
					return "ValueCreators";
				case PNode::CATEGORY::kValueModifiers:
					return "ValueModifiers";
				case PNode::CATEGORY::kVectorCreators:
					return "VectorCreators";
				case PNode::CATEGORY::kVectorModifiers:
					return "VectorModifiers";
				case PNode::CATEGORY::kDataCreators:
					return "DataCreators";
				case PNode::CATEGORY::kActor:
					return "Actor";
				default:
					return "unknown";
			}
		}
	}

	bool BlendGraphSchemaExport::SaveSchemaJSON(const std::filesystem::path& a_filePath)
	{
		auto& typeDefs = GetRegisteredNodeTypes();

		using json_t = nlohmann::ordered_json;
		json_t j;
		json_t& nodeTypes = j["nodeTypes"];

		for(const auto& pair : typeDefs) {
			const auto def = pair.second;
			json_t& curObj = nodeTypes.emplace_back(json_t::object_t());

			curObj["typeName"] = def->typeName;
			curObj["displayName"] = def->typeDisplayName;
			curObj["category"] = detail::CategoryToString(def->nodeCategory);
			
			json_t& inputs = curObj["inputs"];
			for(const auto& inputDef : def->inputs){
				json_t& curInput = inputs.emplace_back(json_t::object_t());
				curInput["name"] = inputDef.name;
				curInput["displayName"] = inputDef.displayName;
				curInput["type"] = detail::EvaluationTypeToString(inputDef.evalType);
				curInput["optional"] = inputDef.optional;
			}

			json_t& customVals = curObj["customValues"];
			for(const auto& valDef : def->customValues){
				json_t& curVal = customVals.emplace_back(json_t::object_t());
				curVal["name"] = valDef.name;
				curVal["displayName"] = valDef.displayName;
				curVal["type"] = detail::EvaluationTypeToString(valDef.evalType);
			}

			curObj["outputType"] = detail::EvaluationTypeToString(def->output);
			curObj["outputDisplayName"] = def->outputDisplayName;
		}

		std::ofstream file(a_filePath);
		if(!file.is_open()) {
			return false;
		}

		file << j.dump(1, '	');
		return true;
	}
}