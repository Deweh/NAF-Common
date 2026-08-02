#include "GLTFExport.h"
#include "Settings/Settings.h"
#include "zstr.hpp"

namespace Serialization
{
	namespace
	{
		constexpr size_t Vec4Size = sizeof(float) * 4;
		constexpr size_t Vec3Size = sizeof(float) * 3;
		constexpr size_t ScalarSize = sizeof(float);

		enum BufferType
		{
			Rot,
			Time,
			Trans,
			Scale,
			Morphs
		};

		using AccessorRange = std::optional<std::pair<double, double>>;

		struct ExportUtil
		{
			void Init(const ozz::animation::Skeleton* skeleton)
			{
				asset = std::make_unique<fastgltf::Asset>();
				// Nodes carry the rest pose, so tools show a sane bind pose. Animated channels override it.
				auto joints = skeleton->joint_names();
				for (int32_t i = 0; i < static_cast<int32_t>(joints.size()); i++) {
					const auto rest = ozz::animation::GetJointLocalRestPose(*skeleton, i);
					fastgltf::Node n;
					n.name = joints[i];
					n.transform = fastgltf::TRS{
						.translation = { rest.translation.x, rest.translation.y, rest.translation.z },
						.rotation = { rest.rotation.x, rest.rotation.y, rest.rotation.z, rest.rotation.w },
						.scale = { rest.scale.x, rest.scale.y, rest.scale.z }
					};
					asset->nodes.push_back(std::move(n));
				}

				for (size_t i = 0; i < joints.size(); i++) {
					int16_t parent = skeleton->joint_parents()[i];
					if (parent != ozz::animation::Skeleton::kNoParent) {
						asset->nodes[parent].children.push_back(i);
					}
				}
			}

			size_t MakeBView(size_t bufferIdx, size_t length)
			{
				auto& bv = asset->bufferViews.emplace_back();
				bv.bufferIndex = bufferIdx;
				bv.byteLength = length;
				bv.byteOffset = 0;
				return (asset->bufferViews.size() - 1);
			}

			fastgltf::sources::Vector& MakeBuffer(size_t length)
			{
				auto& buf = asset->buffers.emplace_back();
				buf.data.emplace<fastgltf::sources::Vector>();
				auto& bVec = std::get<fastgltf::sources::Vector>(buf.data);
				bVec.mimeType = fastgltf::MimeType::GltfBuffer;
				bVec.bytes.resize(length);
				return bVec;
			}

			size_t WriteBuffer(size_t unitSize, size_t length, BufferType bufType, const std::function<void*(size_t)>& getFunc)
			{
				auto& buf = MakeBuffer(unitSize * length);
				for (size_t i = 0; i < length; i++) {
					memcpy(&buf.bytes[i * unitSize], getFunc(i), unitSize);
				}
				return DedupeLastBuffer(bufType, buf);
			}

			// a_range is for scalar accessors only. glTF requires it on animation inputs; outputs leave it out.
			size_t MakeAccessor(AccessorRange a_range, fastgltf::AccessorType type, size_t count, size_t bufferView)
			{
				auto& acc = asset->accessors.emplace_back();
				if (a_range) {
					acc.min.emplace<std::pmr::vector<double>>(1, a_range->first);
					acc.max.emplace<std::pmr::vector<double>>(1, a_range->second);
				}
				acc.type = type;
				acc.count = count;
				acc.componentType = fastgltf::ComponentType::Float;
				acc.bufferViewIndex = bufferView;
				return (asset->accessors.size() - 1);
			}

			size_t WriteAccessor(AccessorRange a_range, fastgltf::AccessorType type, size_t length, BufferType bufType, const std::function<void*(size_t)>& getFunc)
			{
				size_t unitSize = 0;
				switch (type) {
				case fastgltf::AccessorType::Scalar:
					unitSize = ScalarSize;
					break;
				case fastgltf::AccessorType::Vec3:
					unitSize = Vec3Size;
					break;
				case fastgltf::AccessorType::Vec4:
					unitSize = Vec4Size;
					break;
				}

				auto bufIdx = WriteBuffer(unitSize, length, bufType, getFunc);
				bufferMap[bufType].insert(bufIdx);
				
				//Prevent accessor duplication.
				if (auto iter = bufferAccMap.find(bufIdx); iter != bufferAccMap.end()) {
					return iter->second;
				}

				auto bvIdx = MakeBView(bufIdx, unitSize * length);
				auto accIdx = MakeAccessor(a_range, type, length, bvIdx);
				bufferAccMap[bufIdx] = accIdx;
				return accIdx;
			}

			size_t DedupeLastBuffer(BufferType bufType, fastgltf::sources::Vector& cmp)
			{
				auto iter = bufferMap.find(bufType);
				if (iter == bufferMap.end()) {
					return (asset->buffers.size() - 1);
				}

				for (auto& idx : iter->second) {
					auto& vec = std::get<fastgltf::sources::Vector>(asset->buffers[idx].data);
					if (vec.bytes.size() != cmp.bytes.size())
						continue;

					if (memcmp(vec.bytes.data(), cmp.bytes.data(), cmp.bytes.size()) == 0) {
						asset->buffers.pop_back();
						return idx;
					}
				}
				return (asset->buffers.size() - 1);
			}

			void CombineBuffers()
			{
				std::vector<size_t> idxPosMap;
				idxPosMap.reserve(asset->buffers.size());

				std::vector<fastgltf::Buffer> finalVec;
				auto& finalBuf = finalVec.emplace_back();
				finalBuf.data.emplace<fastgltf::sources::Vector>();
				auto& bVec = std::get<fastgltf::sources::Vector>(finalBuf.data);
				bVec.mimeType = fastgltf::MimeType::OctetStream;

				for (auto& b : asset->buffers) {
					idxPosMap.push_back(bVec.bytes.empty() ? 0 : bVec.bytes.size());
					auto& curVec = std::get<fastgltf::sources::Vector>(b.data);
					bVec.bytes.insert(bVec.bytes.end(), curVec.bytes.begin(), curVec.bytes.end());
				}

				size_t bufSize = bVec.bytes.size();
				finalBuf.byteLength = bufSize;
				asset->buffers = std::move(finalVec);

				for (auto& acc : asset->accessors) {
					acc.byteOffset = idxPosMap[acc.bufferViewIndex.value()];
					acc.bufferViewIndex = 0;
				}

				asset->bufferViews = std::vector<fastgltf::BufferView>();
				auto& finalBV = asset->bufferViews.emplace_back();
				finalBV.bufferIndex = 0;
				finalBV.byteLength = bufSize;
				finalBV.byteOffset = 0;
			}

			std::map<size_t, size_t> bufferAccMap;
			std::map<BufferType, std::set<size_t>> bufferMap;
			std::unique_ptr<fastgltf::Asset> asset;
		};
	}

	std::vector<std::byte> GLTFExport::CreateOptimizedAsset(Animation::RawOzzAnimation* anim, const ozz::animation::Skeleton* skeleton, uint8_t level)
	{
		if (level > 0) {
			float toleranceLevel = 1e-5;

			switch (level) {
			case 1:
				toleranceLevel = 0.0f;
				break;
			case 2:
				toleranceLevel = 1e-7f;
				break;
			case 3:
				toleranceLevel = 1e-6f;
				break;
			}

			ozz::unique_ptr<ozz::animation::offline::RawAnimation> tempAnim = ozz::make_unique<ozz::animation::offline::RawAnimation>();
			ozz::animation::offline::AnimationOptimizer optimizer;
			optimizer.setting.distance = 1.0f;
			optimizer.setting.tolerance = toleranceLevel;
			// On failure the output is left empty, so keep every key instead.
			if (optimizer(*anim->data, *skeleton, tempAnim.get())) {
				anim->data = std::move(tempAnim);
			}
		}

		// An empty track means identity to ozz, and the optimizer empties tracks that are constant identity. glTF reads
		// an empty or missing channel as the node's own TRS, which is the rest pose, so write the identity explicitly.
		for (auto& trck : anim->data->tracks) {
			if (trck.translations.empty()) {
				trck.translations.push_back({ 0.0f, ozz::math::Float3::zero() });
			}
			if (trck.rotations.empty()) {
				trck.rotations.push_back({ 0.0f, ozz::math::Quaternion::identity() });
			}
			if (trck.scales.empty()) {
				trck.scales.push_back({ 0.0f, ozz::math::Float3::one() });
			}
		}

		ExportUtil util;
		auto& asset = util.asset;
		util.Init(skeleton);

		auto& assetAnim = asset->animations.emplace_back();
		assetAnim.name = "Animation";

		const auto DedupeSampler = [&](const fastgltf::AnimationSampler& smplr) -> size_t {
			for (size_t i = 0; i < (assetAnim.samplers.size() - 1); i++) {
				auto& s = assetAnim.samplers[i];
				if (s.inputAccessor == smplr.inputAccessor && s.outputAccessor == smplr.outputAccessor) {
					assetAnim.samplers.pop_back();
					return i;
				}
			}
			return (assetAnim.samplers.size() - 1);
		};

		for (size_t i = 0; i < anim->data->tracks.size(); i++) {
			auto& trck = anim->data->tracks[i];

			auto& rotSmplr = assetAnim.samplers.emplace_back();
			rotSmplr.interpolation = fastgltf::AnimationInterpolation::Linear;
			rotSmplr.inputAccessor = util.WriteAccessor(
				std::pair<double, double>{ trck.rotations.front().time, trck.rotations.back().time },
				fastgltf::AccessorType::Scalar,
				trck.rotations.size(),
				BufferType::Time,
				[&](size_t i) { return &trck.rotations[i].time; }
			);
			rotSmplr.outputAccessor = util.WriteAccessor(
				std::nullopt,
				fastgltf::AccessorType::Vec4,
				trck.rotations.size(),
				BufferType::Rot,
				[&](size_t i) { return &trck.rotations[i].value; }
			);

			auto& rotChnl = assetAnim.channels.emplace_back();
			rotChnl.nodeIndex = i;
			rotChnl.path = fastgltf::AnimationPath::Rotation;
			rotChnl.samplerIndex = DedupeSampler(rotSmplr);

			auto& transSmplr = assetAnim.samplers.emplace_back();
			transSmplr.interpolation = fastgltf::AnimationInterpolation::Linear;
			transSmplr.inputAccessor = util.WriteAccessor(
				std::pair<double, double>{ trck.translations.front().time, trck.translations.back().time },
				fastgltf::AccessorType::Scalar,
				trck.translations.size(),
				BufferType::Time,
				[&](size_t i) { return &trck.translations[i].time; }
			);
			transSmplr.outputAccessor = util.WriteAccessor(
				std::nullopt,
				fastgltf::AccessorType::Vec3,
				trck.translations.size(),
				BufferType::Trans,
				[&](size_t i) { return &trck.translations[i].value; }
			);

			auto& transChnl = assetAnim.channels.emplace_back();
			transChnl.nodeIndex = i;
			transChnl.path = fastgltf::AnimationPath::Translation;
			transChnl.samplerIndex = DedupeSampler(transSmplr);

			auto& scaleSmplr = assetAnim.samplers.emplace_back();
			scaleSmplr.interpolation = fastgltf::AnimationInterpolation::Linear;
			scaleSmplr.inputAccessor = util.WriteAccessor(
				std::pair<double, double>{ trck.scales.front().time, trck.scales.back().time },
				fastgltf::AccessorType::Scalar,
				trck.scales.size(),
				BufferType::Time,
				[&](size_t i) { return &trck.scales[i].time; }
			);
			scaleSmplr.outputAccessor = util.WriteAccessor(
				std::nullopt,
				fastgltf::AccessorType::Vec3,
				trck.scales.size(),
				BufferType::Scale,
				[&](size_t i) { return &trck.scales[i].value; }
			);

			auto& scaleChnl = assetAnim.channels.emplace_back();
			scaleChnl.nodeIndex = i;
			scaleChnl.path = fastgltf::AnimationPath::Scale;
			scaleChnl.samplerIndex = DedupeSampler(scaleSmplr);
		}

		std::vector<std::string> morphTargets = Settings::GetFaceMorphs();
		std::vector<ozz::animation::offline::RawFloatTrack*> tracksView;
		bool hasFaceAnim = false;

		if (anim->faceData != nullptr) {
			auto targetIter = morphTargets.begin();
			auto trackIter = anim->faceData->tracks.begin();
			while (targetIter != morphTargets.end() && trackIter != anim->faceData->tracks.end()) {
				if (trackIter->keyframes.size() == 1 && trackIter->keyframes[0].value == 0.0f) {
					targetIter = morphTargets.erase(targetIter);
				} else {
					targetIter++;
					tracksView.push_back(&(*trackIter));
				}
				trackIter++;
			}

			if (!tracksView.empty()) {
				hasFaceAnim = true;
			}
		}

		if (hasFaceAnim) {
			auto& n = asset->nodes.emplace_back();
			n.name = "_MorphTarget_";
			n.meshIndex = 0;

			auto& mesh = asset->meshes.emplace_back();
			mesh.name = "_MorphTarget_";
			auto& mPrim = mesh.primitives.emplace_back();
			auto& a = mPrim.attributes.emplace_back();
			a.first = "POSITION";
			a.second = 0;

			size_t timeSize = 1;
			ozz::animation::offline::RawFloatTrack* timeTrack = tracksView[0];
			for (auto& t : tracksView) {
				if (t->keyframes.size() > 1) {
					timeSize = t->keyframes.size();
					timeTrack = t;
					break;
				}
			}

			auto& smplr = assetAnim.samplers.emplace_back();
			smplr.interpolation = fastgltf::AnimationInterpolation::Linear;
			smplr.inputAccessor = util.WriteAccessor(
				std::pair<double, double>{ 0.0, anim->faceData->duration },
				fastgltf::AccessorType::Scalar,
				timeSize,
				BufferType::Time,
				[&](size_t i) {
					auto& r = timeTrack->keyframes[i].ratio;
					r *= anim->faceData->duration;
					return &r;
				}
			);

			std::vector<float> combinedWeights;
			combinedWeights.reserve(tracksView.size() * timeSize);

			for (size_t i = 0; i < timeSize; i++) {
				for (size_t j = 0; j < tracksView.size(); j++) {
					auto& kfs = tracksView[j]->keyframes;
					combinedWeights.push_back(i >= kfs.size() ? kfs.back().value : kfs[i].value);
				}
			}

			smplr.outputAccessor = util.WriteAccessor(
				std::nullopt,
				fastgltf::AccessorType::Scalar,
				combinedWeights.size(),
				BufferType::Morphs,
				[&](size_t i) { return &combinedWeights[i]; }
			);

			auto& chnl = assetAnim.channels.emplace_back();
			chnl.nodeIndex = (asset->nodes.size() - 1);
			chnl.path = fastgltf::AnimationPath::Weights;
			chnl.samplerIndex = (assetAnim.samplers.size() - 1);
		}

		util.CombineBuffers();

		struct Extras
		{
			const std::vector<std::string>* morphTargets;
			const Animation::RawOzzAnimation* anim;
		} extras{ &morphTargets, anim };

		fastgltf::Exporter exp;
		exp.setUserPointer(&extras);
		exp.setExtrasWriteCallback([](std::size_t objectIndex, fastgltf::Category objectType, void* userPointer) -> std::optional<std::string> {
			const auto& extras = *static_cast<Extras*>(userPointer);
			if (objectType == fastgltf::Category::Animations) {
				std::string result;
				const auto writeBones = [&](std::string_view a_key, const std::vector<std::string>& a_bones) {
					if (a_bones.empty())
						return;

					result += result.empty() ? "{" : ",";
					result += "\"" + std::string(a_key) + "\":[";
					for (size_t i = 0; i < a_bones.size(); i++) {
						result += "\"" + fastgltf::escapeString(a_bones[i]) + "\"";
						if ((i + 1) < a_bones.size()) {
							result += ",";
						}
					}
					result += "]";
				};
				writeBones("unmasked_bones", extras.anim->unmaskedBones);
				writeBones("masked_bones", extras.anim->maskedBones);
				if (result.empty())
					return std::nullopt;

				return result + "}";
			}

			if (objectType != fastgltf::Category::Meshes)
				return std::nullopt;

			auto& morphNames = *extras.morphTargets;
			std::string result = R"({"targetNames":[)";
			for (size_t i = 0; i < morphNames.size(); i++) {
				result += "\"" + morphNames[i] + "\"";
				if ((i + 1) < morphNames.size()) {
					result += ",";
				}
			}
			result += "]}";
			return result;
		});

		auto result = exp.writeGltfBinary(*util.asset.get(), fastgltf::ExportOptions::None);
		if (result.error() != fastgltf::Error::None) {
			return {};
		}

		return std::move(result.get().output);
	}

	std::expected<void, std::string> GLTFExport::ExportToFile(const std::filesystem::path& a_path, Animation::RawOzzAnimation* anim, const ozz::animation::Skeleton* skeleton, uint8_t level)
	{
		const auto bytes = CreateOptimizedAsset(anim, skeleton, level);
		if (bytes.empty()) return std::unexpected("Could not encode the animation.");

		std::error_code ec;
		if (a_path.has_parent_path()) {
			std::filesystem::create_directories(a_path.parent_path(), ec);
			if (ec) return std::unexpected("Could not create the folder: " + ec.message());
		}

		// Write beside the target, then replace it in one step, so it's never left half-written.
		auto temporary = a_path;
		temporary += ".tmp";
		{
			// Gzip-compressed; GLTFImport reads both compressed and plain files. zstr::ofstream only takes a narrow path,
			// so it wraps a std::ofstream, which takes the wide one.
			std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
			if (!file) return std::unexpected("Could not open the file for writing.");
			bool written = true;
			try {
				zstr::ostream compressed(file);
				compressed.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
				// Finishes the gzip stream. The destructor would too, but swallows errors.
				compressed.flush();
			} catch (const std::exception&) {
				written = false;
			}
			file.close();
			if (!written || !file) {
				std::filesystem::remove(temporary, ec);
				return std::unexpected("Failed while writing the file.");
			}
		}
		std::filesystem::rename(temporary, a_path, ec);
		if (ec) {
			const auto message = ec.message();
			std::filesystem::remove(temporary, ec);
			return std::unexpected("Could not replace the file: " + message);
		}
		return {};
	}
}