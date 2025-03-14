#include "BlendGraphImport.h"
#include "simdjson.h"
#include "Animation/Procedural/PActorNode.h"
#include "Animation/Procedural/PFullAnimationNode.h"
#include "Animation/Procedural/PBasePoseNode.h"
#include "Animation/Procedural/PSpringBoneNode.h"
#include "Animation/Ozz.h"
#include "Settings/Settings.h"

namespace Serialization
{
	using namespace Animation::Procedural;

	namespace detail
	{
		bool BuildOutputSetRecursive(PNode* a_outputNode, std::unordered_set<PNode*>& a_outputSet, size_t a_depth)
		{
			if (a_depth > PGraph::MAX_DEPTH) {
				return false;
			}

			a_outputSet.insert(a_outputNode);
			for (uint64_t ptr : a_outputNode->inputs) {
				if (ptr == 0)
					continue;

				if (!BuildOutputSetRecursive(reinterpret_cast<PNode*>(ptr), a_outputSet, a_depth + 1)) {
					return false;
				}
			}

			return true;
		}
	}

	std::unique_ptr<Animation::Procedural::PGraph> BlendGraphImport::LoadGraph(const std::filesystem::path& a_filePath, const std::filesystem::path& a_localDir, const std::string_view a_skeleton)
	{
		std::unique_ptr<PGraph> result;
		std::shared_ptr<const Animation::OzzSkeleton> fullSkeleton = Settings::GetSkeleton(std::string{ a_skeleton });
		try {
			simdjson::ondemand::parser parser;
			auto jsonString = simdjson::padded_string::load(a_filePath.generic_string());
			simdjson::ondemand::document doc = parser.iterate(jsonString);
			if (!doc.is_alive()) {
				throw std::exception{ "Failed to parse JSON." };
			}

			uint64_t version = doc["version"];
			if (version > 1) {
				throw std::exception{ "Blend graph uses an unsupported format version." };
			}

			result = std::make_unique<PGraph>();
			auto& nodeTypes = GetRegisteredNodeTypes();

			std::unordered_map<size_t, PNode*> nodeIdMap;
			auto nodes = doc["nodes"].get_array();

			//Pass 1: Parse all node data.
			std::vector<PEvaluationResult> values;
			std::string_view stringVal;
			double numVal;
			uint64_t intVal;
			bool bVal;
			for (auto n : nodes) {
				std::string_view typeName = n["type"];
				PNode::Registration* typeInfo = nullptr;
				if (auto iter = nodeTypes.find(typeName); iter != nodeTypes.end()) {
					typeInfo = iter->second;
				} else {
					throw std::exception{ "Unknown node type." };
				}

				std::unique_ptr<PNode> currentNode = typeInfo->createFunctor();
				uint64_t id = n["id"];
				nodeIdMap[id] = currentNode.get();

				if (typeInfo == &PActorNode::_reg) {
					if (result->actorNode != 0) {
						throw std::exception{ "Blend graph contains multiple actor nodes." };
					} else {
						result->actorNode = reinterpret_cast<uint64_t>(currentNode.get());
					}
				}

				if (!typeInfo->inputs.empty()) {
					auto inputs = n["inputs"];
					for (auto& i : typeInfo->inputs) {
						uint64_t targetNode = inputs[i.name].get_array().at(0);
						//We don't have pointers to all the nodes yet, so just store the uint64 ID as a pointer until the next step.
						currentNode->inputs.push_back(targetNode);
					}
				}
				if (!typeInfo->customValues.empty()) {
					auto customVals = n["values"];
					values.clear();
					
					for (auto& v : typeInfo->customValues) {
						auto curVal = customVals[v.first];

						switch (v.second) {
						case PEvaluationType<float>:
							numVal = curVal;
							values.emplace_back(static_cast<float>(numVal));
							break;
						case PEvaluationType<RE::BSFixedString>:
							stringVal = curVal;
							values.emplace_back(RE::BSFixedString{ std::string(stringVal).c_str() });
							break;
						case PEvaluationType<uint64_t>:
							intVal = curVal;
							values.emplace_back(intVal);
							break;
						case PEvaluationType<bool>:
							bVal = curVal;
							values.emplace_back(bVal);
							break;
						}
					}
					if (!currentNode->SetCustomValues(values, fullSkeleton.get(), a_localDir)) {
						throw std::runtime_error{ std::format("Failed to process custom value(s) for '{}' node.", typeInfo->typeName) };
					}
				}

				result->nodes.emplace_back(std::move(currentNode));
			}

			if (result->actorNode == 0) {
				throw std::exception{ "Blend graph contains no actor node." };
			}

			//Pass 2: Connect inputs together.
			for (auto& n : result->nodes) {
				auto destTypeInfo = n->GetTypeInfo();
				auto destInputIter = destTypeInfo->inputs.begin();
				for (auto& i : n->inputs) {
					if (auto iter = nodeIdMap.find(i); iter != nodeIdMap.end()) {
						if (iter->second->GetTypeInfo()->output == destInputIter->evalType) {
							i = reinterpret_cast<uint64_t>(iter->second);
						} else {
							throw std::exception{ "Node connection has mismatched input/output types." };
						}
					} else if(destInputIter->optional == true) {
						i = 0;
					} else {
						throw std::exception{ "Blend graph refers to non-existant node ID." };
					}
					destInputIter++;
				}
			}
			
			//The actor node itself isn't actually needed for anything except marking the final output, so mark the final true node as the actor node.
			result->actorNode = reinterpret_cast<PNode*>(result->actorNode)->inputs[0];

			//Pass 3: Discard unneeded nodes.
			std::unordered_set<PNode*> outputSet;
			if (!detail::BuildOutputSetRecursive(reinterpret_cast<PNode*>(result->actorNode), outputSet, 0)) {
				throw std::exception{ "One or more node chains exceed the max chain depth." };
			}
			for (auto iter = result->nodes.begin(); iter != result->nodes.end();) {
				if (!outputSet.contains(iter->get())) {
					iter = result->nodes.erase(iter);
				} else {
					iter++;
				}
			}

			//Pass 4: Cache data for special nodes.
			for (auto& n : result->nodes) {
				auto destTypeInfo = n->GetTypeInfo();
				if (destTypeInfo == &PFullAnimationNode::_reg) {
					// Find the animation node with the longest duration.
					if (auto ptr = reinterpret_cast<PFullAnimationNode*>(result->loopTrackingNode);
						ptr == nullptr || ptr->anim->GetDuration() < static_cast<PFullAnimationNode*>(n.get())->anim->GetDuration()) {
						result->loopTrackingNode = reinterpret_cast<uint64_t>(n.get());
					}
				} else if (destTypeInfo == &PBasePoseNode::_reg) {
					result->needsRestPose = true;
				} else if (destTypeInfo == &PSpringBoneNode::_reg) {
					result->needsPhysSystem = true;
				}
			}

			std::vector<PNode*> sortedNodes;
			sortedNodes.reserve(result->nodes.size());

			if (!result->SortNodes(sortedNodes)) {
				throw std::exception{ "Failed to sort nodes (either due to a dependency loop or too many nodes.)" };
			}

			result->InsertCacheReleaseNodes(sortedNodes);
			result->PointersToIndexes(sortedNodes);
			result->EmplaceNodeOrder(sortedNodes);
		}
		catch (const std::exception& ex) {
			logger::warn("{}", ex.what());
			return nullptr;
		}

		return result;
	}
}