#include "ShapeMorpher.h"

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

		std::vector<Vec3> positions(vertexCount);
		for (uint32_t v = 0; v < vertexCount; v++) positions[v] = ReadPosition(base.data() + size_t(v) * stride, fullPrecision);
		std::vector<uint32_t> triangles;
		auto ib = own->indexBuffer;
		const uint32_t triCount = shape->numTriangles;
		if (normalOffset && ib && ib->data && ib->dataSize >= triCount * 3 * sizeof(uint16_t)) {
			const auto indices = static_cast<const uint16_t*>(ib->data);
			triangles.assign(indices, indices + size_t(triCount) * 3);
		}

		// Welding and the triangle tables take a few milliseconds on a body, so they're built off the main thread.
		auto promise = std::make_shared<std::promise<void>>();
		preparing = promise->get_future().share();
		std::thread([promise, deformer = deformer, positions = std::move(positions), triangles = std::move(triangles), name = std::string{ shape->name.c_str() }, hasNormals = normalOffset != 0,
						vertices = vertexCount, triCount]() mutable {
			const auto start = std::chrono::steady_clock::now();
			deformer->Init(std::move(positions), triangles);
			if (hasNormals && !deformer->HasTopology())
				logger::warn("Morphs: no usable triangles for '{}'; its normals won't follow its morphs.", name);
			logger::info("Morphs: '{}' has its own vertex range ({} vertices, {} triangles, {} weld groups; set up in {:.1f} ms).", name, vertices, triCount, deformer->GetGroupCount(),
				std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
			promise->set_value();
		}).detach();
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

		if (state == State::kShared) {
			if (active.empty())
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
		if (a_totals == applied)
			return true;
		applied = a_totals;

		deformer->Apply(active);
		const auto& moved = deformer->GetMoved();
		const auto& shaded = deformer->GetShaded();
		std::vector<uint8_t> isMoved(vertexCount, 0);
		for (uint32_t v : moved) isMoved[v] = 1;
		for (uint32_t v : deformer->GetDirty()) WriteVertex(v, isMoved[v] != 0, std::ranges::binary_search(shaded, v));
		Upload(a_context, deformer->GetDirty());
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
