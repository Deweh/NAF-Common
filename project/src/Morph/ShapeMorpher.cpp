#include "ShapeMorpher.h"
#include "Manager.h"

#if defined TARGET_GAME_F4
// First: it orders the D3D headers around Renderer.h.
#include "UI/DX11Renderer.h"

#include <DirectXPackedVector.h>

namespace Morph
{
	namespace
	{
		using Vertex = RE::BSGraphics::Vertex;
		using DirectX::PackedVector::HALF;
		using DirectX::PackedVector::XMConvertFloatToHalf;
		using DirectX::PackedVector::XMConvertHalfToFloat;

		// Vertices waiting this long for their range give up.
		constexpr uint32_t kMaxWaitFrames = 600;
		// Runs of changed vertices closer than this are uploaded as one.
		constexpr uint32_t kMergeGap = 32;

		void* GetRenderer()
		{
			static REL::Relocation<void*> renderer{ REL::ID(1378294) };
			return renderer.get();
		}

		// BSGraphics::Renderer::CreateTriShape: a TriShape with its own range in the geometry pool, filled from a_data
		// (asynchronously, through BSD3DResourceCreator), sharing a_indices.
		RE::BSGraphics::TriShape* CreateTriShape(uint32_t a_size, const void* a_data, uint64_t a_desc, RE::BSGraphics::IndexBuffer* a_indices)
		{
			using func_t = RE::BSGraphics::TriShape* (*)(void*, uint32_t&, const void*, uint64_t, RE::BSGraphics::IndexBuffer*);
			static REL::Relocation<func_t> func{ REL::ID(351724) };
			return func(GetRenderer(), a_size, a_data, a_desc, a_indices);
		}

		// BSGraphics::Renderer::DecRef(TriShape*)
		void DecRef(RE::BSGraphics::TriShape* a_triShape)
		{
			using func_t = void (*)(void*, RE::BSGraphics::TriShape*);
			static REL::Relocation<func_t> func{ REL::ID(1039714) };
			func(GetRenderer(), a_triShape);
		}

		// BSShaderUtil::ClearRenderPasses: drops the object's render passes, which hold their own reference to the
		// vertex range they were built against; the shader property rebuilds them on next use.
		void ClearRenderPasses(RE::NiAVObject* a_object)
		{
			using func_t = void (*)(RE::NiAVObject*);
			static REL::Relocation<func_t> func{ REL::ID(613616) };
			func(a_object);
		}

		Vec3 ReadPosition(const std::byte* a_vertex, bool a_full)
		{
			if (a_full) {
				Vec3 p;
				std::memcpy(&p, a_vertex, sizeof(p));
				return p;
			}
			HALF h[3];
			std::memcpy(h, a_vertex, sizeof(h));
			return { XMConvertHalfToFloat(h[0]), XMConvertHalfToFloat(h[1]), XMConvertHalfToFloat(h[2]) };
		}

		void WritePosition(std::byte* a_vertex, bool a_full, const Vec3& a_position)
		{
			if (a_full) {
				std::memcpy(a_vertex, &a_position, sizeof(a_position));
				return;
			}
			const HALF h[3]{ XMConvertFloatToHalf(a_position.x), XMConvertFloatToHalf(a_position.y), XMConvertFloatToHalf(a_position.z) };
			std::memcpy(a_vertex, h, sizeof(h));
		}

		// The bitangent's X is the position's fourth component.
		float ReadBitangentX(const std::byte* a_vertex, bool a_full)
		{
			if (a_full) {
				float f;
				std::memcpy(&f, a_vertex + 12, sizeof(f));
				return f;
			}
			HALF h;
			std::memcpy(&h, a_vertex + 6, sizeof(h));
			return XMConvertHalfToFloat(h);
		}

		void WriteBitangentX(std::byte* a_vertex, bool a_full, float a_value)
		{
			if (a_full) {
				std::memcpy(a_vertex + 12, &a_value, sizeof(a_value));
				return;
			}
			const HALF h = XMConvertFloatToHalf(a_value);
			std::memcpy(a_vertex + 6, &h, sizeof(h));
		}

		float UnpackByte(std::byte a_value) { return static_cast<float>(std::to_integer<uint8_t>(a_value)) / 255.0f * 2.0f - 1.0f; }

		std::byte PackByte(float a_value)
		{
			const float scaled = std::round((std::clamp(a_value, -1.0f, 1.0f) + 1.0f) * 0.5f * 255.0f);
			return static_cast<std::byte>(static_cast<uint8_t>(scaled));
		}

		// BSSkin::Instance and BSSkin::BoneData, which CommonLibF4 doesn't define (layout as in F4SE's BSSkin.h, checked
		// against BSSkin::Instance::Instance and UpdateModelBound and BSSkin::BoneData::Set).
		template <class T>
		struct ArrayView
		{
			T* data;
			uint32_t capacity;
			uint32_t pad0C;
			uint32_t size;
			uint32_t pad14;
		};
		static_assert(sizeof(ArrayView<void*>) == 0x18);

		struct SkinBoneData
		{
			struct Bone
			{
				RE::NiBound bound;          // 00
				RE::NiTransform skinToBone;  // 10
			};
			static_assert(sizeof(Bone) == 0x50);

			std::byte object[0x10];  // NiObject
			ArrayView<Bone> bones;   // 10
		};

		struct SkinInstance
		{
			std::byte object[0x10];                         // NiObject
			ArrayView<RE::NiNode*> bones;                   // 10
			ArrayView<RE::NiTransform*> worldTransforms;    // 28
			SkinBoneData* boneData;                         // 40
			RE::NiAVObject* rootNode;                       // 48
		};
		static_assert(offsetof(SkinInstance, boneData) == 0x40);

		// The game applies a transform as p' = scale * (p * rotate) + translate, with p a row vector (see
		// BSSkin::Instance::UpdateModelBound and GetWorldToSkinTransform), so the matrix acting on column vectors is
		// rotate's transpose.
		Affine ToAffine(const RE::NiTransform& a_transform)
		{
			Affine a;
			const auto& e = a_transform.rotate.entry;
			for (int r = 0; r < 3; r++) a.linear.rows[r] = Vec3{ e[0][r], e[1][r], e[2][r] } * a_transform.scale;
			a.translation = { a_transform.translate.x, a_transform.translate.y, a_transform.translate.z };
			return a;
		}

		const SkinInstance* GetSkin(RE::BSTriShape* a_shape, uint32_t a_boneCount)
		{
			auto skin = reinterpret_cast<const SkinInstance*>(a_shape->skinInstance.get());
			if (!skin || !skin->boneData || !skin->worldTransforms.data || !skin->boneData->bones.data)
				return nullptr;
			if (skin->worldTransforms.size != a_boneCount || skin->boneData->bones.size != a_boneCount)
				return nullptr;
			return skin;
		}

		// A bone's skinning transform now: skin space to its bind, then to where the bone is. The bone's world transform
		// is read through the skin's pointer to it, as the game does: with a flattened bone tree, it isn't the node's.
		bool GetBoneTransform(const SkinInstance* a_skin, uint32_t a_bone, Affine& a_out)
		{
			auto world = a_skin->worldTransforms.data[a_bone];
			if (!world)
				return false;
			a_out = ToAffine(*world) * ToAffine(a_skin->boneData->bones.data[a_bone].skinToBone);
			return true;
		}

		// BSFlattenedBoneTree, which CommonLibF4 doesn't define (from BSFlattenedBoneTree::GetOrCreateBoneNode and
		// GetBoneNode): its bones at 0x148 (count at 0x140), 0xA0 bytes each: local transform, world transform (0x40),
		// parent index (0x80), the bone's node if one was made (0x88), name (0x90). A skin on a flattened skeleton has no
		// bone nodes; its world transform pointers point at the tree's bones' world transforms.
		constexpr size_t kTreeCount = 0x140;
		constexpr size_t kTreeBones = 0x148;
		constexpr size_t kTreeStride = 0xA0;
		constexpr size_t kTreeWorld = 0x40;
		constexpr size_t kTreeName = 0x90;

		bool IsFlattenedBoneTree(const RE::NiAVObject* a_object)
		{
			static REL::Relocation<const RE::NiRTTI*> rtti{ RE::Ni_RTTI::BSFlattenedBoneTree };
			for (auto r = a_object->GetRTTI(); r; r = r->GetBaseRTTI()) {
				if (r == rtti.get())
					return true;
			}
			return false;
		}

		// The flattened bone trees in the scene graph a_shape is in.
		std::vector<const std::byte*> FindBoneTrees(RE::NiAVObject* a_shape)
		{
			std::vector<const std::byte*> trees;
			auto root = a_shape;
			while (root->parent) root = root->parent;
			std::vector<RE::NiAVObject*> stack{ root };
			while (!stack.empty()) {
				auto object = stack.back();
				stack.pop_back();
				if (!object)
					continue;
				if (IsFlattenedBoneTree(object))
					trees.push_back(reinterpret_cast<const std::byte*>(object));
				if (auto node = object->IsNode()) {
					for (auto& child : node->children) stack.push_back(child.get());
				}
			}
			return trees;
		}

		// The name of the flattened tree bone whose world transform a_world is, or empty.
		std::string FlattenedBoneName(const std::vector<const std::byte*>& a_trees, const RE::NiTransform* a_world)
		{
			const auto at = reinterpret_cast<uintptr_t>(a_world);
			for (auto tree : a_trees) {
				const auto bones = *reinterpret_cast<const uintptr_t*>(tree + kTreeBones);
				const auto count = *reinterpret_cast<const int32_t*>(tree + kTreeCount);
				if (!bones || count <= 0 || at < bones || at >= bones + size_t(count) * kTreeStride || (at - bones) % kTreeStride != kTreeWorld)
					continue;
				return reinterpret_cast<const RE::BSFixedString*>(at - kTreeWorld + kTreeName)->c_str();
			}
			return {};
		}

		bool Differs(const Affine& a, const Affine& b)
		{
			constexpr float kLinear = 1e-4f, kTranslation = 1e-3f;
			const auto apart = [](const Vec3& x, const Vec3& y, float e) { return std::abs(x.x - y.x) > e || std::abs(x.y - y.y) > e || std::abs(x.z - y.z) > e; };
			for (int r = 0; r < 3; r++) {
				if (apart(a.linear.rows[r], b.linear.rows[r], kLinear))
					return true;
			}
			return apart(a.translation, b.translation, kTranslation);
		}
	}

	ShapeMorpher::ShapeMorpher(RE::BSTriShape* a_shape, std::vector<Source> a_sources) :
		shape(a_shape), sources(std::move(a_sources))
	{
		auto desc = a_shape->vertexDesc;
		stride = desc.GetSize();
		vertexCount = a_shape->numVertices;
		fullPrecision = desc.HasFlag(Vertex::VF_FULLPREC);
		normalOffset = desc.HasFlag(Vertex::VF_NORMAL) ? desc.GetAttributeOffset(Vertex::VA_NORMAL) : 0;
		tangentOffset = desc.HasFlag(Vertex::VF_TANGENT) ? desc.GetAttributeOffset(Vertex::VA_BINORMAL) : 0;
		skinOffset = desc.HasFlag(Vertex::VF_SKINNED) ? desc.GetAttributeOffset(Vertex::VA_SKINNING) : 0;
	}

	std::string_view ShapeMorpher::GetStateName() const
	{
		switch (state) {
		case State::kShared:
			return "shared";
		case State::kPending:
			return "pending";
		case State::kPreparing:
			return "preparing";
		case State::kReady:
			return "ready";
		default:
			return "failed";
		}
	}

	void ShapeMorpher::SetSources(std::vector<Source> a_sources)
	{
		sources = std::move(a_sources);
		applied.clear();
		applied.emplace_back("", 0.0f);  // Never equal to real totals.
	}

	void ShapeMorpher::ListMorphs(std::vector<std::string>& a_out) const
	{
		for (const auto& source : sources) {
			for (const auto& [name, ref] : source.shape->morphs) {
				if (!std::ranges::contains(a_out, name))
					a_out.push_back(name);
			}
		}
	}

	bool ShapeMorpher::IsValid() const
	{
		if (!shape || !shape->parent)
			return false;
		return state == State::kShared || state == State::kFailed || shape->rendererData == own;
	}

	bool ShapeMorpher::TakeOver()
	{
		auto shared = static_cast<RE::BSGraphics::TriShape*>(shape->rendererData);
		auto vertexBuffer = shared ? shared->vertexBuffer : nullptr;
		const uint32_t size = stride * vertexCount;
		if (!vertexBuffer || !vertexBuffer->data || vertexBuffer->dataSize < size) {
			logger::warn("Morphs: '{}' has no CPU copy of its vertices; it can't be morphed.", shape->name.c_str());
			return false;
		}
		own = CreateTriShape(size, vertexBuffer->data, shape->vertexDesc.desc, shared->indexBuffer);
		if (!own || !own->vertexBuffer) {
			logger::warn("Morphs: couldn't create a vertex range for '{}'.", shape->name.c_str());
			own = nullptr;
			return false;
		}
		shape->rendererData = own;
		ClearRenderPasses(shape.get());
		DecRef(shared);
		return true;
	}

	bool ShapeMorpher::IsRangeReady() const
	{
		auto vb = own->vertexBuffer;
		if (!vb || !vb->buffer || vb->pendingCopy)
			return false;
		return *reinterpret_cast<const volatile uint32_t*>(&vb->pendingRequests) == 0;
	}

	bool ShapeMorpher::Prepare()
	{
		auto vb = own->vertexBuffer;
		const uint32_t size = stride * vertexCount;
		D3D11_BUFFER_DESC desc{};
		reinterpret_cast<ID3D11Buffer*>(vb->buffer)->GetDesc(&desc);
		if (!vb->data || vb->dataSize < size || size_t(vb->dataOffset) + size > desc.ByteWidth) {
			logger::warn("Morphs: the vertex range of '{}' isn't laid out as expected; not morphing it.", shape->name.c_str());
			return false;
		}
		const auto data = static_cast<const std::byte*>(vb->data);
		base.assign(data, data + size);
		work = base;

		restPositions.resize(vertexCount);
		for (uint32_t v = 0; v < vertexCount; v++) restPositions[v] = ReadPosition(base.data() + size_t(v) * stride, fullPrecision);
		triangles.clear();
		auto ib = own->indexBuffer;
		const uint32_t triCount = shape->numTriangles;
		if (normalOffset && ib && ib->data && ib->dataSize >= triCount * 3 * sizeof(uint16_t)) {
			const auto indices = static_cast<const uint16_t*>(ib->data);
			triangles.assign(indices, indices + size_t(triCount) * 3);
		}

		skinWeights = normalOffset ? ReadSkin() : std::vector<SkinWeights>{};

		// Welding and the triangle tables take a few milliseconds on a body, so they're built off the main thread.
		auto promise = std::make_shared<std::promise<void>>();
		preparing = promise->get_future().share();
		std::thread([promise, deformer = deformer, positions = restPositions, triangles = triangles, skin = skinWeights, name = std::string{ shape->name.c_str() },
						hasNormals = normalOffset != 0, vertices = vertexCount, triCount]() mutable {
			const auto start = std::chrono::steady_clock::now();
			deformer->Init(std::move(positions), triangles, std::move(skin));
			if (hasNormals && !deformer->HasTopology())
				logger::warn("Morphs: no usable triangles for '{}'; its normals won't follow its morphs.", name);
			logger::info("Morphs: '{}' has its own vertex range ({} vertices, {} triangles, {} weld groups, {} bones for posed shading; set up in {:.1f} ms).", name, vertices, triCount,
				deformer->GetGroupCount(), deformer->GetPoseBones().size(), std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
			promise->set_value();
		}).detach();
		return true;
	}

	std::vector<SkinWeights> ShapeMorpher::ReadSkin()
	{
		auto raw = reinterpret_cast<const SkinInstance*>(shape->skinInstance.get());
		if (!skinOffset || !raw || !raw->boneData)
			return {};
		const uint32_t bones = raw->worldTransforms.size;
		if (bones == 0 || bones > 256 || !GetSkin(shape.get(), bones)) {
			logger::warn("Morphs: the skin of '{}' isn't laid out as expected; its creases will be shaded unposed.", shape->name.c_str());
			return {};
		}

		// Four half-float weights, then four bone indices.
		std::vector<SkinWeights> skin(vertexCount);
		for (uint32_t v = 0; v < vertexCount; v++) {
			const std::byte* data = base.data() + size_t(v) * stride + skinOffset;
			HALF h[4];
			std::memcpy(h, data, sizeof(h));
			auto& w = skin[v];
			float total = 0.0f;
			for (int i = 0; i < 4; i++) {
				w.bones[i] = std::to_integer<uint8_t>(data[8 + i]);
				w.weights[i] = (std::max)(XMConvertHalfToFloat(h[i]), 0.0f);
				if (w.bones[i] >= bones)
					w.weights[i] = 0.0f;
				total += w.weights[i];
			}
			if (total <= 0.0f)
				return {};
			for (auto& weight : w.weights) weight /= total;
		}

		// A check that skinning is read the way the game does it: the skinned mesh should sit inside its bound.
		std::vector<Affine> transforms(bones);
		for (uint32_t b = 0; b < bones; b++) {
			if (!GetBoneTransform(raw, b, transforms[b]))
				transforms[b] = {};
		}
		Vec3 centre{};
		for (uint32_t v = 0; v < vertexCount; v++) {
			const auto p = ReadPosition(base.data() + size_t(v) * stride, fullPrecision);
			Vec3 skinned{};
			for (int i = 0; i < 4; i++) skinned += (transforms[skin[v].bones[i]] * p) * skin[v].weights[i];
			centre += skinned * (1.0f / vertexCount);
		}
		const auto& bound = shape->worldBound;
		const auto offset = centre - Vec3{ bound.center.x, bound.center.y, bound.center.z };
		logger::info("Morphs: '{}' skins with {} bones; skinned centre is {:.1f} from its world bound's centre (radius {:.1f}).", shape->name.c_str(), bones,
			std::sqrt(Dot(offset, offset)), bound.fRadius);
		boneCount = bones;
		// The bones' names, for picking the smoothing region: from the bone nodes, or on a flattened skeleton (which has
		// none), from the tree bones their world transforms are.
		boneNames.assign(bones, {});
		std::optional<std::vector<const std::byte*>> trees;
		uint32_t named = 0;
		for (uint32_t b = 0; b < bones; b++) {
			if (raw->bones.data && raw->bones.size == bones && raw->bones.data[b])
				boneNames[b] = raw->bones.data[b]->name.c_str();
			if (boneNames[b].empty()) {
				if (!trees)
					trees = FindBoneTrees(shape.get());
				boneNames[b] = FlattenedBoneName(*trees, raw->worldTransforms.data[b]);
			}
			named += !boneNames[b].empty();
		}
		if (named < bones)
			logger::warn("Morphs: only {} of the {} bones skinning '{}' have names; the rest are never in a smoothing region.", named, bones, shape->name.c_str());
		return skin;
	}

	bool ShapeMorpher::ReadPose(bool& a_changed)
	{
		a_changed = false;
		const auto& bones = deformer->GetPoseBones();
		auto skin = bones.empty() ? nullptr : GetSkin(shape.get(), boneCount);
		if (!skin)
			return false;
		// Relative to the first pose bone, so moving the whole actor isn't a change.
		std::vector<Affine> next(boneCount);
		Affine reference;
		if (!GetBoneTransform(skin, bones.front(), reference))
			return false;
		const auto toReference = reference.Inverse();
		for (uint8_t b : bones) {
			Affine transform;
			if (!GetBoneTransform(skin, b, transform))
				return false;
			next[b] = toReference * transform;
		}
		a_changed = pose.size() != next.size() || std::ranges::any_of(bones, [&](uint8_t b) { return Differs(pose[b], next[b]); });
		if (a_changed)
			pose = std::move(next);
		return true;
	}

	bool ShapeMorpher::Apply(const Values& a_totals, ID3D11DeviceContext* a_context)
	{
		if (!IsValid() || state == State::kFailed)
			return true;

		// The active morphs this shape has, first source first.
		MeshDeformer::ActiveMorphs active;
		for (const auto& [name, value] : a_totals) {
			for (const auto& source : sources) {
				auto iter = source.shape->morphs.find(name);
				if (iter == source.shape->morphs.end())
					continue;
				auto deltas = source.file->GetMorph(iter->second);
				if (deltas->maxIndex < vertexCount)
					active.emplace_back(std::move(deltas), value);
				else
					logger::warn("Morphs: '{}' in {} moves vertex {}, but '{}' has {}; ignoring it.", name, source.file->GetName(), deltas->maxIndex, shape->name.c_str(), vertexCount);
				break;
			}
		}
		activeCount = active.size();
		const bool wantsSmoothing = WantsSmoothing();

		if (state == State::kShared) {
			if (active.empty() && !wantsSmoothing)
				return true;
			if (!TakeOver()) {
				state = State::kFailed;
				return true;
			}
			state = State::kPending;
			waitFrames = 0;
		}
		if (state == State::kPending) {
			if (!IsRangeReady()) {
				if (++waitFrames > kMaxWaitFrames) {
					logger::warn("Morphs: the vertex range of '{}' wasn't ready after {} frames; giving up on it.", shape->name.c_str(), kMaxWaitFrames);
					state = State::kFailed;
					return true;
				}
				return false;
			}
			if (!Prepare()) {
				state = State::kFailed;
				return true;
			}
			state = State::kPreparing;
		}
		if (state == State::kPreparing) {
			if (preparing.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
				return false;
			state = State::kReady;
			applied.clear();
		}
		if (wantsSmoothing && ApplySmoothed(active, a_totals, a_context))
			return true;
		if (smoothed) {
			// The GPU wrote the whole range: the CPU path starts over and rewrites all of it.
			smoothed = false;
			applied.clear();
			applied.emplace_back("", 0.0f);
			fullUpload = true;
		}
		// While the smoothing data is being built, the caller keeps coming back, so it's used once it's ready.
		const bool waiting = wantsSmoothing && !gpu && smoothData.valid();
		bool poseChanged = false;
		const bool posed = ReadPose(poseChanged);
		if (a_totals == applied) {
			// Same morphs: only posed shading can change, when the bones moved.
			if (posed && poseChanged && deformer->IsPoseDependent()) {
				deformer->SetPose(pose);
				deformer->Reshade();
				WriteAndUpload(a_context);
			}
			return !waiting;
		}
		applied = a_totals;

		deformer->SetPose(posed ? pose : std::vector<Affine>{});
		deformer->Apply(active);
		WriteAndUpload(a_context);
		return !waiting;
	}

	void ShapeMorpher::WriteAndUpload(ID3D11DeviceContext* a_context)
	{
		const auto& moved = deformer->GetMoved();
		const auto& shaded = deformer->GetShaded();
		std::vector<uint8_t> isMoved(vertexCount, 0);
		for (uint32_t v : moved) isMoved[v] = 1;
		for (uint32_t v : deformer->GetDirty()) WriteVertex(v, isMoved[v] != 0, std::ranges::binary_search(shaded, v));
		if (fullUpload) {
			fullUpload = false;
			std::vector<uint32_t> all(vertexCount);
			std::iota(all.begin(), all.end(), 0u);
			Upload(a_context, all);
			return;
		}
		Upload(a_context, deformer->GetDirty());
	}

	bool ShapeMorpher::WantsSmoothing() const
	{
		return skinOffset && normalOffset && shape->skinInstance.get() && Manager::GetSingleton()->GetSmoothing().GetEnabled();
	}

	bool ShapeMorpher::ReadBones(std::vector<Affine>& a_out, bool& a_changed)
	{
		a_changed = false;
		auto skin = boneCount ? GetSkin(shape.get(), boneCount) : nullptr;
		if (!skin)
			return false;
		// Relative to the first bone, so moving the whole actor isn't a change and positions stay small.
		a_out.resize(boneCount);
		Affine reference;
		if (!GetBoneTransform(skin, 0, reference))
			return false;
		const auto toReference = reference.Inverse();
		for (uint32_t b = 0; b < boneCount; b++) {
			Affine transform;
			if (!GetBoneTransform(skin, b, transform))
				return false;
			a_out[b] = toReference * transform;
		}
		a_changed = smoothedBones.size() != a_out.size();
		for (uint32_t b = 0; b < boneCount && !a_changed; b++) a_changed = Differs(smoothedBones[b], a_out[b]);
		return true;
	}

	bool ShapeMorpher::ApplySmoothed(const MeshDeformer::ActiveMorphs& a_active, const Values& a_totals, ID3D11DeviceContext* a_context)
	{
		if (skinWeights.empty() || triangles.empty())
			return false;
		auto& library = Manager::GetSingleton()->GetSmoothing();
		const uint64_t generation = library.GetGeneration();
		if (generation != smoothGeneration) {
			// New settings: new data.
			gpu.reset();
			smoothData = {};
			smoothGeneration = generation;
		}
		if (smoothFailed == generation)
			return false;
		if (!gpu) {
			if (!smoothData.valid())
				smoothData = library.Get({ restPositions, triangles, skinWeights, boneCount });
			if (smoothData.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
				return false;
			auto data = smoothData.get();
			if (data) {
				const GpuSmoother::Layout layout{ stride, normalOffset, tangentOffset, fullPrecision };
				auto vb = own->vertexBuffer;
				gpu = GpuSmoother::Create(std::move(data), layout, base, skinWeights, reinterpret_cast<ID3D11Buffer*>(vb->buffer), vb->dataOffset, shape->name.c_str());
			}
			if (!gpu) {
				smoothFailed = generation;
				smoothData = {};
				return false;
			}
			smoothedTotals.clear();
			smoothedTotals.emplace_back("", 0.0f);  // Never equal to real totals.
			regionGeneration = UINT64_MAX;
		}

		// The region: how much each skin bone is in it. A shape wholly outside it keeps the CPU path.
		bool update = false;
		if (const uint64_t regionNow = library.GetRegionGeneration(); regionNow != regionGeneration) {
			std::vector<float> strengths(boneCount);
			uint32_t regionBones = 0;
			for (uint32_t b = 0; b < boneCount && b < boneNames.size(); b++) {
				strengths[b] = library.GetBoneStrength(boneNames[b]);
				regionBones += strengths[b] > 0.0f;
			}
			inRegion = gpu->SetRegion(strengths) > 1e-3f;
			logger::info("Morphs: '{}' has {} of its {} bones in the smoothing region; {}.", shape->name.c_str(), regionBones, boneCount,
				inRegion ? "it's smoothed" : "none of it is smoothed");
			regionGeneration = regionNow;
			update = true;
		}
		if (!inRegion)
			return false;

		std::vector<Affine> bones;
		bool bonesChanged = false;
		if (!ReadBones(bones, bonesChanged))
			return false;
		update = update || !smoothed || bonesChanged;
		if (a_totals != smoothedTotals) {
			// The morphed rest mesh.
			auto positions = restPositions;
			for (const auto& [deltas, value] : a_active) {
				for (size_t i = 0; i < deltas->indices.size(); i++) positions[deltas->indices[i]] += deltas->offsets[i] * value;
			}
			gpu->SetRestPositions(positions);
			smoothedTotals = a_totals;
			update = true;
		}
		const uint32_t passes = library.GetRuntimePasses();
		const uint32_t autoPasses = library.GetAutoSmooth() ? library.GetAutoSmoothPasses() : 0;
		if (passes != smoothedPasses || autoPasses != smoothedAutoPasses)
			update = true;
		if (update) {
			gpu->Update(a_context, bones, passes, autoPasses);
			smoothedBones = std::move(bones);
			smoothedPasses = passes;
			smoothedAutoPasses = autoPasses;
		}
		smoothed = true;
		return true;
	}

	void ShapeMorpher::WriteVertex(uint32_t a_vertex, bool a_moved, bool a_shaded)
	{
		const std::byte* src = base.data() + size_t(a_vertex) * stride;
		std::byte* dst = work.data() + size_t(a_vertex) * stride;
		std::memcpy(dst, src, stride);
		if (a_moved)
			WritePosition(dst, fullPrecision, deformer->GetPosition(a_vertex));
		if (!a_shaded || !normalOffset)
			return;

		const std::byte* nb = src + normalOffset;
		const auto normal = deformer->TurnFrame(a_vertex, { UnpackByte(nb[0]), UnpackByte(nb[1]), UnpackByte(nb[2]) });
		Vec3 bitangent{ ReadBitangentX(src, fullPrecision), UnpackByte(nb[3]), 0.0f };
		if (tangentOffset) {
			const std::byte* tb = src + tangentOffset;
			const auto tangent = deformer->TurnFrame(a_vertex, { UnpackByte(tb[0]), UnpackByte(tb[1]), UnpackByte(tb[2]) });
			bitangent.z = UnpackByte(tb[3]);
			std::byte* tw = dst + tangentOffset;
			tw[0] = PackByte(tangent.x);
			tw[1] = PackByte(tangent.y);
			tw[2] = PackByte(tangent.z);
		}
		bitangent = deformer->TurnFrame(a_vertex, bitangent);
		std::byte* nw = dst + normalOffset;
		nw[0] = PackByte(normal.x);
		nw[1] = PackByte(normal.y);
		nw[2] = PackByte(normal.z);
		nw[3] = PackByte(bitangent.y);
		if (tangentOffset)
			dst[tangentOffset + 3] = PackByte(bitangent.z);
		WriteBitangentX(dst, fullPrecision, bitangent.x);
	}

	void ShapeMorpher::Upload(ID3D11DeviceContext* a_context, const std::vector<uint32_t>& a_vertices)
	{
		if (a_vertices.empty())
			return;
		auto vb = own->vertexBuffer;
		auto buffer = reinterpret_cast<ID3D11Buffer*>(vb->buffer);
		const auto upload = [&](uint32_t a_first, uint32_t a_last) {
			const uint32_t begin = a_first * stride;
			const uint32_t end = (a_last + 1) * stride;
			const D3D11_BOX box{ vb->dataOffset + begin, 0, 0, vb->dataOffset + end, 1, 1 };
			a_context->UpdateSubresource(buffer, 0, &box, work.data() + begin, 0, 0);
		};
		// a_vertices is sorted.
		uint32_t first = a_vertices.front(), last = first;
		for (uint32_t v : a_vertices) {
			if (v <= last + kMergeGap) {
				last = (std::max)(last, v);
				continue;
			}
			upload(first, last);
			first = last = v;
		}
		upload(first, last);
	}
}
#endif
