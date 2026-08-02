#include "GLTFImport.h"
#include "Animation/Transform.h"
#include "Settings/Settings.h"
#include "Util/String.h"
#include "zstr.hpp"
#include "simdjson.h"

template <>
struct fastgltf::ElementTraits<ozz::math::Quaternion> : fastgltf::ElementTraitsBase<ozz::math::Quaternion, AccessorType::Vec4, float>
{};

template <>
struct fastgltf::ElementTraits<ozz::math::Float3> : fastgltf::ElementTraitsBase<ozz::math::Float3, AccessorType::Vec3, float>
{};

// Column-major, as GLTF stores it.
struct GltfMat4
{
	float m[16];
};

template <>
struct fastgltf::ElementTraits<GltfMat4> : fastgltf::ElementTraitsBase<GltfMat4, AccessorType::Mat4, float>
{};

namespace Serialization
{
	void GLTFImport::LoadAnimation(AnimationInfo& info)
	{
		if (!info.targetActor) {
			info.result.error = kNoSkeleton;
			return;
		}

		auto skeleton = Settings::GetSkeleton(info.targetActor);
		if (Settings::IsDefaultSkeleton(skeleton)) {
			info.result.error = kNoSkeleton;
			return;
		}

		auto assetData = LoadGLTF(Util::String::GetDataPath() / info.fileName);
		if (!assetData) {
			info.result.error = kFailedToLoad;
			return;
		}

		auto asset = &assetData->asset;

		fastgltf::Animation* anim = nullptr;
		if (info.id.type == AnimationIdentifer::Type::kIndex && info.id.index < asset->animations.size()) {
			anim = &asset->animations[info.id.index];
		} else if (info.id.type == AnimationIdentifer::Type::kName) {
			for (auto& a : asset->animations) {
				if (a.name == info.id.name.c_str()) {
					anim = &a;
					break;
				}
			}
		}

		if (!anim) {
			info.result.error = kInvalidAnimationIdentifier;
			return;
		}

		auto runtimeAnim = CreateRuntimeAnimation(assetData.get(), anim, skeleton.get());
		if (!runtimeAnim) {
			info.result.error = kFailedToMakeClip;
			return;
		}

		info.result.anim = std::move(runtimeAnim->data);
		info.result.error = kSuccess;
	}

	std::optional<std::vector<ozz::math::Transform>> GLTFImport::CreateRestPoseFromSkins(const AssetData* assetData, const ozz::animation::Skeleton& skeleton)
	{
		using namespace ozz::math;
		const auto asset = &assetData->asset;
		const size_t numNodes = asset->nodes.size();

		std::vector<size_t> nodeParents(numNodes, SIZE_MAX);
		for (size_t i = 0; i < numNodes; i++) {
			for (auto c : asset->nodes[i].children) {
				if (c < numNodes) {
					nodeParents[c] = i;
				}
			}
		}

		const auto localMatrix = [&](size_t a_node) {
			if (auto trs = std::get_if<fastgltf::TRS>(&asset->nodes[a_node].transform)) {
				return Float4x4::FromAffine(
					Float3{ trs->translation[0], trs->translation[1], trs->translation[2] },
					Quaternion{ trs->rotation[0], trs->rotation[1], trs->rotation[2], trs->rotation[3] },
					Float3{ trs->scale[0], trs->scale[1], trs->scale[2] });
			}
			return Float4x4::identity();
		};

		// Global transform from the nodes' own TRS, used for the skinned mesh nodes.
		const auto nodeGlobal = [&](size_t a_node) {
			Float4x4 result = localMatrix(a_node);
			for (size_t p = nodeParents[a_node]; p != SIZE_MAX; p = nodeParents[p]) {
				result = localMatrix(p) * result;
			}
			return result;
		};

		// The inverse bind matrices are relative to the skinned mesh node, so the joints' bind globals are meshGlobal * IBM^-1.
		std::vector<std::optional<Float4x4>> globals(numNodes);
		bool anyBound = false;
		for (size_t skinIdx = 0; skinIdx < asset->skins.size(); skinIdx++) {
			const auto& skin = asset->skins[skinIdx];
			if (!skin.inverseBindMatrices.has_value() || skin.inverseBindMatrices.value() >= asset->accessors.size())
				continue;

			const auto& ibmAccessor = asset->accessors[skin.inverseBindMatrices.value()];
			if (ibmAccessor.type != fastgltf::AccessorType::Mat4 || ibmAccessor.count != skin.joints.size())
				continue;

			Float4x4 meshGlobal = Float4x4::identity();
			for (size_t i = 0; i < numNodes; i++) {
				if (asset->nodes[i].skinIndex == skinIdx) {
					meshGlobal = nodeGlobal(i);
					break;
				}
			}

			for (size_t i = 0; i < skin.joints.size(); i++) {
				const size_t jointNode = skin.joints[i];
				if (jointNode >= numNodes || globals[jointNode].has_value())
					continue;

				const auto ibm = fastgltf::getAccessorElement<GltfMat4>(*asset, ibmAccessor, i);
				Float4x4 ibmMatrix;
				for (size_t c = 0; c < 4; c++) {
					ibmMatrix.cols[c] = simd_float4::LoadPtrU(&ibm.m[c * 4]);
				}
				globals[jointNode] = meshGlobal * Invert(ibmMatrix);
				anyBound = true;
			}
		}

		if (!anyBound)
			return std::nullopt;

		// Nodes outside the skins hang off their parent's global by their own TRS.
		const std::function<const Float4x4&(size_t)> global = [&](size_t a_node) -> const Float4x4& {
			if (!globals[a_node].has_value()) {
				const size_t parent = nodeParents[a_node];
				globals[a_node] = parent == SIZE_MAX ? localMatrix(a_node) : global(parent) * localMatrix(a_node);
			}
			return *globals[a_node];
		};

		std::map<std::string, size_t> skeletonMap;
		auto skeletonJointNames = skeleton.joint_names();
		for (size_t i = 0; i < skeletonJointNames.size(); i++) {
			skeletonMap[Util::String::ToLower(skeletonJointNames[i])] = i;
		}

		const int32_t numJoints = skeleton.num_joints();
		std::vector<size_t> jointNodes(numJoints, SIZE_MAX);
		for (size_t i = 0; i < numNodes; i++) {
			std::string_view nodeName = asset->nodes[i].name.c_str();
			if (auto iter = assetData->originalNames.find(i); iter != assetData->originalNames.end()) {
				nodeName = iter->second;
			}
			if (auto iter = skeletonMap.find(Util::String::ToLower(nodeName)); iter != skeletonMap.end() && jointNodes[iter->second] == SIZE_MAX) {
				jointNodes[iter->second] = i;
			}
		}

		// Joints the file has no node for keep the skeleton's rest pose.
		const auto parents = skeleton.joint_parents();
		std::vector<Transform> restPose;
		restPose.reserve(numJoints);
		for (int32_t i = 0; i < numJoints; i++) {
			auto& t = restPose.emplace_back(ozz::animation::GetJointLocalRestPose(skeleton, i));
			const size_t node = jointNodes[i];
			if (node == SIZE_MAX)
				continue;

			// Locals are relative to the skeleton parent, falling back to the node's GLTF parent.
			const int16_t parent = parents[i];
			const size_t parentNode = parent != ozz::animation::Skeleton::kNoParent && jointNodes[parent] != SIZE_MAX ? jointNodes[parent] : nodeParents[node];
			const Float4x4 local = parentNode == SIZE_MAX ? global(node) : Invert(global(parentNode)) * global(node);

			Transform decomposed;
			if (ToAffine(local, &decomposed)) {
				t = decomposed;
			}
		}

		return restPose;
	}

	void ParseMorphChannel(const GLTFImport::AssetData* assetData, const fastgltf::Animation* anim, const fastgltf::AnimationChannel& channel, Animation::RawOzzAnimation* rawAnim)
	{
		auto asset = &assetData->asset;
		auto& targetNode = asset->nodes[channel.nodeIndex.value()];

		if (!targetNode.meshIndex.has_value())
			return;

		auto iter = assetData->morphTargets.find(targetNode.meshIndex.value());
		if (iter == assetData->morphTargets.end())
			return;

		auto& morphTargets = iter->second;

		//Create a map of GLTF morph indexes -> game morph indexes
		std::vector<size_t> morphIdxs;
		auto gameIdxs = Settings::GetFaceMorphIndexMap();
		bool hasMorphs = false;

		for (auto& mt : morphTargets) {
			if (auto iter = gameIdxs.find(mt); iter != gameIdxs.end()) {
				//If this track has been populated by a different weights channel already, skip over it.
				if (rawAnim->faceData != nullptr && !rawAnim->faceData->tracks[iter->second].keyframes.empty()) [[unlikely]] {
					morphIdxs.push_back(UINT64_MAX);
				} else {
					morphIdxs.push_back(iter->second);
					hasMorphs = true;
				}
			} else {
				morphIdxs.push_back(UINT64_MAX);
			}
		}

		if (!hasMorphs)
			return;

		//Process GLTF data
		if (channel.samplerIndex > anim->samplers.size())
			return;

		auto& sampler = anim->samplers[channel.samplerIndex];
		if (sampler.inputAccessor > asset->accessors.size() || sampler.outputAccessor > asset->accessors.size())
			return;

		auto& timeAccessor = asset->accessors[sampler.inputAccessor];
		auto& dataAccessor = asset->accessors[sampler.outputAccessor];

		if (timeAccessor.count != (dataAccessor.count / morphTargets.size()))
			return;

		if (!std::holds_alternative<std::pmr::vector<double>>(timeAccessor.max))
			return;

		auto timeMax = std::get_if<std::pmr::vector<double>>(&timeAccessor.max);
		if (!timeMax || timeMax->empty())
			return;

		float duration = static_cast<float>((*timeMax)[0]);

		if (rawAnim->faceData == nullptr) {
			rawAnim->faceData = std::make_unique<Animation::RawOzzFaceAnimation>();
			rawAnim->faceData->duration = duration;
		}

		ozz::animation::offline::RawTrackKeyframe<float> kf{};
		kf.interpolation = ozz::animation::offline::RawTrackInterpolation::kLinear;
		for (size_t i = 0; i < timeAccessor.count; i++) {
			float time = fastgltf::getAccessorElement<float>(*asset, timeAccessor, i);
			kf.ratio = (time == 0.0f ? 0.0f : std::clamp(time / duration, 0.0f, 1.0f));

			for (size_t j = 0; j < morphTargets.size(); j++) {
				auto idx = morphIdxs[j];
				if (idx == UINT64_MAX)
					continue;

				kf.value = fastgltf::getAccessorElement<float>(*asset, dataAccessor, (i * morphTargets.size()) + j);
				rawAnim->faceData->tracks[idx].keyframes.push_back(kf);
			}
		}

		for (size_t i = 0; i < morphTargets.size(); i++) {
			auto idx = morphIdxs[i];
			if (idx == UINT64_MAX)
				continue;

			bool allZero = true;
			for (auto& k : rawAnim->faceData->tracks[idx].keyframes) {
				if (k.value != 0.0f) {
					allZero = false;
					break;
				}
			}
			if (allZero) {
				rawAnim->faceData->tracks[idx].keyframes.clear();
			}
		}
	}

	// Each source bone may sit in its rest pose with different local axes than the matching target bone (e.g. Blender
	// re-orients bones to point at their children). Joint positions are assumed to line up at rest.
	//
	// The target follows the source's world-space motion: T_world(t) = S_world(t) * C, with C = S_world_rest^-1 * T_world_rest.
	// In local space that becomes T_local(t) = C_parent^-1 * S_local(t) * C. The delta from rest, which is what the
	// animator authored, gets conjugated into the target bone's axes instead of being applied along the source's axes.
	// Blender's Copy Transforms constraint in Local Owner Orientation space does the same thing.
	bool GLTFImport::RetargetAnimation(ozz::animation::offline::RawAnimation* anim, const std::vector<ozz::math::Transform>& sourceRestPose, const std::vector<ozz::math::Transform>& targetRestPose, const ozz::animation::Skeleton& skeleton)
	{
		const size_t numJoints = anim->num_tracks();
		if (sourceRestPose.size() != numJoints || targetRestPose.size() != numJoints || static_cast<size_t>(skeleton.num_joints()) != numJoints)
			return false;

		using namespace ozz::math;
		const auto parents = skeleton.joint_parents();

		// ozz orders joints so that parents always come before their children.
		std::vector<Quaternion> sourceWorld(numJoints);
		std::vector<Quaternion> targetWorld(numJoints);
		std::vector<Quaternion> offsets(numJoints);
		for (size_t i = 0; i < numJoints; i++) {
			const auto sourceLocal = NormalizeSafe(sourceRestPose[i].rotation, Quaternion::identity());
			const auto targetLocal = NormalizeSafe(targetRestPose[i].rotation, Quaternion::identity());
			const int16_t parent = parents[i];
			if (parent == ozz::animation::Skeleton::kNoParent) {
				sourceWorld[i] = sourceLocal;
				targetWorld[i] = targetLocal;
			} else {
				sourceWorld[i] = sourceWorld[parent] * sourceLocal;
				targetWorld[i] = targetWorld[parent] * targetLocal;
			}
			offsets[i] = Conjugate(sourceWorld[i]) * targetWorld[i];
		}

		const auto safeDivide = [](float a_num, float a_den) {
			return std::abs(a_den) > 1e-6f ? a_num / a_den : 1.0f;
		};

		for (size_t i = 0; i < numJoints; i++) {
			auto& track = anim->tracks[i];
			const int16_t parent = parents[i];
			const Quaternion parentOffsetInv = parent == ozz::animation::Skeleton::kNoParent ? Quaternion::identity() : Conjugate(offsets[parent]);
			const Quaternion& offset = offsets[i];
			const auto& sourceRest = sourceRestPose[i];
			const auto& targetRest = targetRestPose[i];

			for (auto& rot : track.rotations) {
				rot.value = Normalize(parentOffsetInv * NormalizeSafe(rot.value, Quaternion::identity()) * offset);
			}
			// Keep the target's bone lengths and only carry over the motion, expressed in the target parent's axes.
			for (auto& trans : track.translations) {
				trans.value = targetRest.translation + TransformVector(parentOffsetInv, trans.value - sourceRest.translation);
			}
			for (auto& scale : track.scales) {
				scale.value = {
					targetRest.scale.x * safeDivide(scale.value.x, sourceRest.scale.x),
					targetRest.scale.y * safeDivide(scale.value.y, sourceRest.scale.y),
					targetRest.scale.z * safeDivide(scale.value.z, sourceRest.scale.z)
				};
			}
		}

		return true;
	}

	std::unique_ptr<Animation::RawOzzAnimation> GLTFImport::CreateRawAnimation(const AssetData* assetData, const fastgltf::Animation* anim, const Animation::OzzSkeleton* skeleton)
	{
		auto asset = &assetData->asset;

		//Create a map of GLTF node indexes -> skeleton indexes
		std::vector<size_t> skeletonIdxs;
		skeletonIdxs.reserve(asset->nodes.size());

		std::map<std::string, size_t> skeletonMap;
		auto skeletonJointNames = skeleton->data->joint_names();
		for (size_t i = 0; i < skeletonJointNames.size(); i++) {
			skeletonMap[Util::String::ToLower(skeletonJointNames[i])] = i;
		}

		std::vector<ozz::math::Transform> bindPose;
		int32_t numJoints = skeleton->data->num_joints();
		bindPose.reserve(numJoints);

		std::vector<bool> hasNode(numJoints, false);

		//Save skeleton bind pose.
		for (int32_t i = 0; i < numJoints; i++) {
			if constexpr (IsFO4()) {
				bindPose.push_back(ozz::animation::GetJointLocalRestPose(*skeleton->data, i));
			} else {
				bindPose.push_back(ozz::math::Transform::identity());
			}
		}

		for (auto nIter = asset->nodes.begin(); nIter != asset->nodes.end(); nIter++) {
			const auto& n = *nIter;
			std::string_view nodeName = n.name.c_str();

			if (auto iter = assetData->originalNames.find(std::distance(asset->nodes.begin(), nIter)); iter != assetData->originalNames.end()) {
				nodeName = iter->second;
			}

			if (auto iter = skeletonMap.find(Util::String::ToLower(nodeName)); iter != skeletonMap.end()) {
				skeletonIdxs.push_back(iter->second);
				if (std::holds_alternative<fastgltf::TRS>(n.transform)) {
					hasNode[iter->second] = true;
					auto& trs = std::get<fastgltf::TRS>(n.transform);
					auto& b = bindPose[iter->second];
					b.rotation = {
						trs.rotation[0],
						trs.rotation[1],
						trs.rotation[2],
						trs.rotation[3]
					};
					b.translation = {
						trs.translation[0],
						trs.translation[1],
						trs.translation[2]
					};
					b.scale = {
						trs.scale[0],
						trs.scale[1],
						trs.scale[2]
					};
				}
			} else {
				skeletonIdxs.push_back(UINT64_MAX);
			}
		}

		//Create the raw animation
		auto rawAnimResult = std::make_unique<Animation::RawOzzAnimation>();
		rawAnimResult->data = ozz::make_unique<ozz::animation::offline::RawAnimation>();
		const auto animIndex = static_cast<size_t>(anim - asset->animations.data());
		if (auto bones = assetData->unmaskedBones.find(animIndex); bones != assetData->unmaskedBones.end()) {
			rawAnimResult->unmaskedBones = bones->second;
		}
		if (auto bones = assetData->maskedBones.find(animIndex); bones != assetData->maskedBones.end()) {
			rawAnimResult->maskedBones = bones->second;
		}
		auto& animResult = rawAnimResult->data;
		animResult->duration = 0.001f;
		animResult->tracks.resize(skeletonMap.size());

		//Process GLTF data
		std::vector<float> times;
		for (auto& c : anim->channels) {
			times.clear();
			if (!c.nodeIndex.has_value() || c.nodeIndex > asset->nodes.size())
				continue;

			if (c.path == fastgltf::AnimationPath::Weights) {
				ParseMorphChannel(assetData, anim, c, rawAnimResult.get());
				continue;
			}

			auto idx = skeletonIdxs[c.nodeIndex.value()];
			if (idx == UINT64_MAX)
				continue;

			auto& rTl = animResult->tracks[idx].rotations;
			auto& pTl = animResult->tracks[idx].translations;
			auto& sTl = animResult->tracks[idx].scales;

			if (c.samplerIndex > anim->samplers.size())
				continue;

			auto& sampler = anim->samplers[c.samplerIndex];
			if (sampler.inputAccessor > asset->accessors.size() || sampler.outputAccessor > asset->accessors.size())
				continue;

			auto& timeAccessor = asset->accessors[sampler.inputAccessor];
			auto& dataAccessor = asset->accessors[sampler.outputAccessor];

			if (timeAccessor.count != dataAccessor.count)
				continue;

			ozz::animation::offline::RawAnimation::RotationKey r;
			ozz::animation::offline::RawAnimation::TranslationKey p;
			ozz::animation::offline::RawAnimation::ScaleKey s;
			for (size_t i = 0; i < timeAccessor.count; i++) {
				float t = fastgltf::getAccessorElement<float>(*asset, timeAccessor, i);
				if (t > animResult->duration)
					animResult->duration = t;

				switch (c.path) {
				case fastgltf::AnimationPath::Rotation:
					r.value = fastgltf::getAccessorElement<ozz::math::Quaternion>(*asset, dataAccessor, i);
					r.time = t;
					rTl.push_back(r);
					break;
				case fastgltf::AnimationPath::Translation:
					p.value = fastgltf::getAccessorElement<ozz::math::Float3>(*asset, dataAccessor, i);
					p.time = t;
					pTl.push_back(p);
					break;
				case fastgltf::AnimationPath::Scale:
					s.value = fastgltf::getAccessorElement<ozz::math::Float3>(*asset, dataAccessor, i);
					s.time = t;
					sTl.push_back(s);
				}
			}
		}

		//If the file has a skin, its bind pose is the rest pose of the rig the keys were authored against (e.g. a Blender armature, whose bones
		//are oriented differently), and the keys get retargeted onto the game skeleton below. Unkeyed channels are filled in the source rig's
		//space first so they go through the same retarget. Joints the file has no node for take the source rest pose, which retargets to exactly
		//the skeleton's rest pose.
		const auto sourceRestPose = CreateRestPoseFromSkins(assetData, *skeleton->data);
		const bool retarget = sourceRestPose.has_value() && animResult->tracks.size() == static_cast<size_t>(numJoints);
		if (retarget) {
			for (int32_t i = 0; i < numJoints; i++) {
				if (!hasNode[i]) {
					bindPose[i] = (*sourceRestPose)[i];
				}
			}
		}

		for (size_t i = 0; i < skeletonMap.size(); i++) {
			auto& rTl = animResult->tracks[i].rotations;
			auto& pTl = animResult->tracks[i].translations;
			auto& sTl = animResult->tracks[i].scales;
			auto& b = bindPose[i];
			if (rTl.empty()) {
				ozz::animation::offline::RawAnimation::RotationKey r;
				r.time = 0.0001f;
				r.value = b.rotation;
				rTl.push_back(r); 
			}
			if (pTl.empty()) {
				ozz::animation::offline::RawAnimation::TranslationKey p;
				p.time = 0.0001f;
				p.value = b.translation;
				pTl.push_back(p);
			}
			if (sTl.empty()) {
				ozz::animation::offline::RawAnimation::ScaleKey s;
				s.time = 0.0001f;
				s.value = b.scale;
				sTl.push_back(s);
			}
		}

		if (retarget) {
			std::vector<ozz::math::Transform> targetRestPose;
			targetRestPose.reserve(numJoints);
			for (int32_t i = 0; i < numJoints; i++) {
				targetRestPose.push_back(ozz::animation::GetJointLocalRestPose(*skeleton->data, i));
			}
			RetargetAnimation(animResult.get(), *sourceRestPose, targetRestPose, *skeleton->data);
		}

		if (rawAnimResult->faceData != nullptr) {
			auto faceResult = rawAnimResult->faceData.get();
			ozz::animation::offline::RawTrackKeyframe<float> kf{};
			kf.interpolation = ozz::animation::offline::RawTrackInterpolation::kLinear;
			kf.ratio = 0.0f;
			kf.value = 0.0f;
			for (auto& t : faceResult->tracks) {
				if (t.keyframes.empty()) {
					t.keyframes.push_back(kf);
				}
			}
		}

		return rawAnimResult;
	}

	std::unique_ptr<Animation::OzzAnimation> GLTFImport::CreateRuntimeAnimation(const AssetData* assetData, const fastgltf::Animation* anim, const Animation::OzzSkeleton* skeleton)
	{
		auto result = std::make_unique<Animation::OzzAnimation>();

		auto animResult = CreateRawAnimation(assetData, anim, skeleton);
		ozz::animation::offline::AnimationBuilder builder;
		result->data = builder(*animResult->data);

		if (!result->data) {
			return nullptr;
		}

		if (animResult->faceData.get() != nullptr) {
			ozz::animation::offline::TrackBuilder trackBuilder;
			result->faceData = std::make_unique<Animation::OzzFaceAnimation>();
			result->faceData->duration = animResult->faceData->duration;
			for (size_t i = 0; i < animResult->faceData->tracks.size(); i++) {
				auto t = trackBuilder(animResult->faceData->tracks[i]);
				if (!t) {
					return nullptr;
				}
				result->faceData->tracks[i] = std::move(*t);
			}
		}

		// Names the skeleton doesn't have are ignored. The mask stays empty (the default) unless one changes it.
		if (!animResult->unmaskedBones.empty() || !animResult->maskedBones.empty()) {
			auto mask = skeleton->defaultBoneMask;
			const auto jointNames = skeleton->data->joint_names();
			bool changed = false;
			const auto apply = [&](const std::vector<std::string>& a_names, bool a_written) {
				for (const auto& name : a_names) {
					for (size_t i = 0; i < jointNames.size() && i < mask.size(); i++) {
						if (Util::String::CaseInsensitiveCompare(name, jointNames[i])) {
							changed |= mask[i] != a_written;
							mask[i] = a_written;
							break;
						}
					}
				}
			};
			apply(animResult->unmaskedBones, true);
			apply(animResult->maskedBones, false);
			if (changed) {
				result->boneMask = std::move(mask);
			}
		}

		return result;
	}

	std::unique_ptr<GLTFImport::AssetData> GLTFImport::LoadGLTF(const std::filesystem::path& fileName)
	{
		try {
			zstr::ifstream file(fileName.generic_string(), std::ios::binary);

			if (file.bad())
				return nullptr;

			std::vector<uint8_t> buffer;
			std::istreambuf_iterator<char> fileEnd;

			for (std::istreambuf_iterator<char> iter(file); iter != fileEnd; iter++) {
				buffer.push_back(*iter);
			}

			size_t baseBufferSize = buffer.size();
			size_t paddedBufferSize = baseBufferSize + fastgltf::getGltfBufferPadding();
			buffer.resize(paddedBufferSize);

			fastgltf::GltfDataBuffer data;
			data.fromByteView(buffer.data(), baseBufferSize, paddedBufferSize);

			fastgltf::Parser parser;
			auto gltfOptions =
				fastgltf::Options::LoadGLBBuffers |
				fastgltf::Options::DecomposeNodeMatrices;

			auto gltfCategories =
				fastgltf::Category::Animations |
				fastgltf::Category::Nodes |
				fastgltf::Category::Meshes |
				fastgltf::Category::Buffers |
				fastgltf::Category::BufferViews |
				fastgltf::Category::Samplers |
				fastgltf::Category::Accessors |
				fastgltf::Category::Skins;

			auto assetData = std::make_unique<AssetData>();
			parser.setUserPointer(assetData.get());
			parser.setExtrasParseCallback([](simdjson::dom::object* extras, std::size_t objectIndex, fastgltf::Category objectType, void* userPointer) {
				auto assetData = static_cast<AssetData*>(userPointer);
				if (objectType == fastgltf::Category::Meshes) {
					auto arr = (*extras)["targetNames"].get_array();
					if (arr.error() != simdjson::error_code::SUCCESS) {
						return;
					}

					std::string_view name;
					auto& target = assetData->morphTargets[objectIndex];
					for (auto ele : arr) {
						if (ele.get_string().get(name) == simdjson::error_code::SUCCESS) {
							target.emplace_back(name);
						} else {
							assetData->morphTargets.erase(objectIndex);
							return;
						}
					}
				} else if (objectType == fastgltf::Category::Nodes) {
					auto str = (*extras)["original_name"].get_string();
					if (str.error() != simdjson::error_code::SUCCESS) {
						return;
					}

					assetData->originalNames[objectIndex] = str.value();
				} else if (objectType == fastgltf::Category::Animations) {
					const auto readBones = [&](std::string_view a_key, std::map<size_t, std::vector<std::string>>& a_out) {
						auto arr = (*extras)[a_key].get_array();
						if (arr.error() != simdjson::error_code::SUCCESS) {
							return;
						}

						std::string_view name;
						auto& bones = a_out[objectIndex];
						for (auto ele : arr) {
							if (ele.get_string().get(name) == simdjson::error_code::SUCCESS) {
								bones.emplace_back(name);
							}
						}
					};
					readBones("unmasked_bones", assetData->unmaskedBones);
					readBones("masked_bones", assetData->maskedBones);
				}
			});

			auto gltf = parser.loadGltf(&data, fileName.parent_path(), gltfOptions, gltfCategories);
			if (auto err = gltf.error(); err != fastgltf::Error::None) {
				return nullptr;
			}

			assetData->asset = std::move(gltf.get());
			return assetData;
		} catch (const std::exception&) {
			return nullptr;
		}
	}
}