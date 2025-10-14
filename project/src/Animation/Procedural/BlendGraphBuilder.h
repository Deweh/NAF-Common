#pragma once
#include "PGraph.h"

namespace Animation::Procedural
{
    class BlendGraphBuilder
    {
    public:
        enum ERR_CODE : uint16_t
        {
            SUCCESS,
            CUSTOM_VALUES_SIZE_MISMATCH,
            CUSTOM_VALUES_TYPE_MISMATCH,
            CUSTOM_VALUES_SET_FAILURE,
            OUTPUT_NODE_INVALID,
            INPUT_NODE_INVALID,
            INPUT_INDEX_INVALID,
            INPUT_OUTPUT_TYPE_MISMATCH,
            OUTPUT_NODE_TYPE_MISMATCH,
            CHAIN_DEPTH_EXCEEDED,
            INPUT_VALIDATION_FAILED
        };

        struct OPERATION_RESULT
        {
            uint64_t nodeId = 0;
            uint64_t errorMeta = 0;
            ERR_CODE errorCode = ERR_CODE::SUCCESS;
        };

        struct BUILD_RESULT
        {
            std::unique_ptr<PGraph> graph;
            uint64_t errorMeta = 0;
            ERR_CODE errorCode = ERR_CODE::SUCCESS;
        };

        OPERATION_RESULT AddNode(const PNode::Registration* a_typeInfo, std::vector<PEvaluationResult>&& a_customValues = {}, const std::optional<uint64_t>& a_nodeId = std::nullopt);
        OPERATION_RESULT ConnectNodes(uint64_t a_outputNode, uint64_t a_inputNode, size_t a_inputIdx);
        BUILD_RESULT BuildGraph(uint64_t a_outputNode, const Animation::OzzSkeleton* a_skeleton, const std::filesystem::path& a_localDir);

    private:
        struct NodeData
        {
            const PNode::Registration* typeInfo;
            std::vector<PEvaluationResult> customValues;
            std::vector<uint64_t> inputs;
            uint64_t id;
        };

        struct BUILD_DATA
        {
            std::unordered_map<uint64_t, NodeData*> outputNodes;
            std::unordered_map<uint64_t, PNode*> realNodes;
        };

        bool BuildOutputSetRecursive(NodeData* a_outputNode, std::unordered_map<uint64_t, NodeData*>& a_outputSet, size_t a_depth);
        bool ValidateNodeInputs(const NodeData* a_node);

        std::unordered_map<uint64_t, std::unique_ptr<NodeData>> nodes;
        uint64_t nextNodeId = 1;
    };
}