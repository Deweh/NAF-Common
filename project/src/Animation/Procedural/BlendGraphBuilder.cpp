#include "BlendGraphBuilder.h"
#include "PFullAnimationNode.h"
#include "PBasePoseNode.h"
#include "PSpringBoneNode.h"
#include "PGraph.h"

namespace Animation::Procedural
{
	BlendGraphBuilder::OPERATION_RESULT BlendGraphBuilder::AddNode(const PNode::Registration* a_typeInfo, std::vector<PEvaluationResult>&& a_customValues, const std::optional<uint64_t>& a_nodeId)
	{
        OPERATION_RESULT result{};

        if(a_typeInfo->customValues.size() != a_customValues.size()) {
            result.errorCode = ERR_CODE::CUSTOM_VALUES_SIZE_MISMATCH;
            return result;
        }

        for(size_t i = 0; i < a_typeInfo->customValues.size(); i++) {
            if(a_typeInfo->customValues[i].evalType != a_customValues[i].index()) {
                result.errorCode = ERR_CODE::CUSTOM_VALUES_TYPE_MISMATCH;
                result.errorMeta = i;
                return result;
            }
        }

        if(a_nodeId.has_value() && nodes.find(a_nodeId.value()) == nodes.end()) {
            result.nodeId = a_nodeId.value();
        } else {
            result.nodeId = nextNodeId++;
            while(nodes.find(result.nodeId) != nodes.end()) {
                result.nodeId = nextNodeId++;
            }
        }

        std::unique_ptr<NodeData> newNode = std::make_unique<NodeData>();
        newNode->typeInfo = a_typeInfo;
        newNode->customValues = std::move(a_customValues);
        newNode->inputs = std::vector<uint64_t>(a_typeInfo->inputs.size(), UINT64_MAX);
        newNode->id = result.nodeId;

        nodes[result.nodeId] = std::move(newNode);
        return result;
	}

	BlendGraphBuilder::OPERATION_RESULT BlendGraphBuilder::ConnectNodes(uint64_t a_outputNode, uint64_t a_inputNode, size_t a_inputIdx)
	{
		OPERATION_RESULT result{};
        
        auto outputNodeIter = nodes.find(a_outputNode);
        if(outputNodeIter == nodes.end()) {
            result.errorCode = ERR_CODE::OUTPUT_NODE_INVALID;
            return result;
        }

        auto inputNodeIter = nodes.find(a_inputNode);
        if(inputNodeIter == nodes.end()) {
            result.errorCode = ERR_CODE::INPUT_NODE_INVALID;
            return result;
        }

        if(a_inputIdx >= inputNodeIter->second->inputs.size()) {
            result.errorCode = ERR_CODE::INPUT_INDEX_INVALID;
            return result;
        }

        if(outputNodeIter->second->typeInfo->output != inputNodeIter->second->typeInfo->inputs[a_inputIdx].evalType) {
            result.errorCode = ERR_CODE::INPUT_OUTPUT_TYPE_MISMATCH;
            return result;
        }

        result.errorMeta = inputNodeIter->second->inputs[a_inputIdx];
        inputNodeIter->second->inputs[a_inputIdx] = a_outputNode;
        return result;
	}

	BlendGraphBuilder::BUILD_RESULT BlendGraphBuilder::BuildGraph(uint64_t a_outputNode, const Animation::OzzSkeleton* a_skeleton, const std::filesystem::path& a_localDir)
	{
		BUILD_RESULT result{};
        BUILD_DATA data{};

        auto outputNodeIter = nodes.find(a_outputNode);
        if(outputNodeIter == nodes.end()) {
            result.errorCode = ERR_CODE::OUTPUT_NODE_INVALID;
            return result;
        }

        if(outputNodeIter->second->typeInfo->output != PEvaluationType<PoseCache::Handle>) {
            result.errorCode = ERR_CODE::OUTPUT_NODE_TYPE_MISMATCH;
            return result;
        }

        if(!BuildOutputSetRecursive(outputNodeIter->second.get(), data.outputNodes, 0)) {
            result.errorCode = ERR_CODE::CHAIN_DEPTH_EXCEEDED;
            return result;
        }

        result.graph = std::make_unique<PGraph>();

        // Pass 1: Create nodes & set custom values.
        for(auto& [nodeId, nodeData] : data.outputNodes) {
            if(!ValidateNodeInputs(nodeData)) {
                result.errorCode = ERR_CODE::INPUT_VALIDATION_FAILED;
                result.errorMeta = nodeId;
                return result;
            }

            const PNode::Registration* currentType = nodeData->typeInfo;
            std::unique_ptr<PNode> currentRealNode = currentType->createFunctor();
            currentRealNode->inputs.reserve(currentType->inputs.size());
            currentRealNode->inputs.resize(currentType->inputs.size(), 0);
            if(!nodeData->customValues.empty() && !currentRealNode->SetCustomValues(nodeData->customValues, a_skeleton, a_localDir)) {
                result.errorCode = ERR_CODE::CUSTOM_VALUES_SET_FAILURE;
                result.errorMeta = nodeId;
                return result;
            }

            data.realNodes[nodeId] = currentRealNode.get();
            result.graph->nodes.emplace_back(std::move(currentRealNode));
        }

        // Set actor (aka graph output) node.
        result.graph->actorNode = reinterpret_cast<uint64_t>(data.realNodes.find(a_outputNode)->second);

        // Pass 2: Connect inputs together.
        for(auto& [nodeId, realNode] : data.realNodes) {
            for(size_t i = 0; i < realNode->inputs.size(); i++) {
                uint64_t inputNode = data.outputNodes[nodeId]->inputs[i];
                if(inputNode != UINT64_MAX) {
                     realNode->inputs[i] = reinterpret_cast<uint64_t>(data.realNodes.find(inputNode)->second);
                }
            }
        }

        //Pass 3: Cache data for special nodes.
        for (auto& n : result.graph->nodes) {
            auto destTypeInfo = n->GetTypeInfo();
            if (destTypeInfo == &PFullAnimationNode::_reg) {
                // Find the animation node with the longest duration.
                if (auto ptr = reinterpret_cast<PFullAnimationNode*>(result.graph->loopTrackingNode);
                    ptr == nullptr || ptr->anim->GetDuration() < static_cast<PFullAnimationNode*>(n.get())->anim->GetDuration()) {
                    result.graph->loopTrackingNode = reinterpret_cast<uint64_t>(n.get());
                }
            } else if (destTypeInfo == &PBasePoseNode::_reg) {
                result.graph->needsRestPose = true;
            } else if (destTypeInfo == &PSpringBoneNode::_reg) {
                result.graph->needsPhysSystem = true;
            }
        }

        std::vector<PNode*> sortedNodes;
        sortedNodes.reserve(result.graph->nodes.size());

        if (!result.graph->SortNodes(sortedNodes)) {
            result.errorCode = ERR_CODE::CHAIN_DEPTH_EXCEEDED;
            return result;
        }

        result.graph->InsertCacheReleaseNodes(sortedNodes);
        result.graph->PointersToIndexes(sortedNodes);
        result.graph->EmplaceNodeOrder(sortedNodes);

        return result;
	}

	bool BlendGraphBuilder::BuildOutputSetRecursive(NodeData* a_outputNode, std::unordered_map<uint64_t, NodeData*>& a_outputSet, size_t a_depth)
	{
        if (a_depth > PGraph::MAX_DEPTH) {
				return false;
			}

			a_outputSet[a_outputNode->id] = a_outputNode;
			for (uint64_t inputNode : a_outputNode->inputs) {
				if (inputNode == UINT64_MAX)
					continue;

				if (!BuildOutputSetRecursive(nodes.find(inputNode)->second.get(), a_outputSet, a_depth + 1)) {
					return false;
				}
			}

			return true;
	}

	bool BlendGraphBuilder::ValidateNodeInputs(const NodeData* a_node)
	{
		for(size_t i = 0; i < a_node->typeInfo->inputs.size(); i++) {
            if(a_node->inputs[i] == UINT64_MAX){
                if(!a_node->typeInfo->inputs[i].optional) {
                    return false;
                }
            } else {
                auto inputNodeIter = nodes.find(a_node->inputs[i]);
                if(inputNodeIter == nodes.end()) {
                    return false;
                }
            }
        }

        return true;
	}
}