#pragma once
#include "Geometry.h"
#include "GpuSmoother.h"
#include "Smoothing.h"
#include "TriFile.h"
#include "Values.h"

// Fallout 4's geometry pool and vertex format.
#if defined TARGET_GAME_F4
struct ID3D11DeviceContext;

namespace Morph
{
	// Applies morphs to one actor's copy of one mesh shape, in real time.
	//
	// The first time a morph on the shape is non-zero, the shape gets its own vertex range in the game's geometry pool:
	// a new renderer TriShape copied from its vertices (sharing its index buffer), its render passes cleared so they're
	// rebuilt against the new range, and the shared TriShape released. Every actor wearing the mesh otherwise shares one
	// range. The range is created asynchronously; morphs wait until it's on the GPU, then until the MeshDeformer has
	// built the mesh's topology on a background thread.
	//
	// After that, the MeshDeformer works out the new positions and how far the surface turned; this writes positions,
	// turns the mesh's own normals, tangents and bitangents, and uploads just the vertices that changed with
	// UpdateSubresource. The CPU copy of the vertices stays unmorphed and is the base every update starts from, so the
	// base includes any LooksMenu morphs baked in when the mesh was built.
	//
	// For a skinned shape, the deformer also gets each vertex's bone weights and, every apply, the skin's bone
	// transforms, so it can shade crease areas on the posed mesh (MeshDeformer::SetPose). Shading there then follows
	// the pose: while it does, Apply re-shades those vertices whenever their bones move, even if the morphs didn't.
	//
	// While corrective smoothing is on (SmoothLibrary), a skinned shape is taken over even without morphs, and once its
	// mesh's smoothing data is built, the GPU does everything instead (GpuSmoother): the morphs go in as the morphed rest
	// positions, and the smoothed, morphed, re-shaded vertices are written straight into the range. That follows the
	// pose, so it's redone whenever the bones move. Until the data is ready, or if the GPU can't do it, the shape is
	// morphed on the CPU as above; switching back rewrites the whole range from the CPU copy.
	class ShapeMorpher
	{
	public:
		struct Source
		{
			std::shared_ptr<TriFile> file;
			const TriFile::Shape* shape = nullptr;
		};

		ShapeMorpher(RE::BSTriShape* a_shape, std::vector<Source> a_sources);
		ShapeMorpher(const ShapeMorpher&) = delete;
		ShapeMorpher& operator=(const ShapeMorpher&) = delete;

		// Applies the totals (lowercase names, sorted), or re-shades for the current pose if they didn't change.
		// Returns false while waiting for the vertex range or the topology, so the caller tries again next frame. Main
		// thread, with the renderer locked.
		bool Apply(const Values& a_totals, ID3D11DeviceContext* a_context);
		// Whether the shape's shading follows the pose now, so Apply should be called every frame.
		[[nodiscard]] bool IsPoseDependent() const { return state == State::kReady && (smoothed || deformer->IsPoseDependent()); }
		// Whether the GPU smoothed it at the last Apply.
		[[nodiscard]] bool IsSmoothed() const { return smoothed; }
		// Whether the shape is still the one this was made for: still attached, and drawing from our range if we took it.
		[[nodiscard]] bool IsValid() const;
		// After morph files are re-read: the next Apply re-applies everything from the new data.
		void SetSources(std::vector<Source> a_sources);

		[[nodiscard]] RE::BSTriShape* GetShape() const { return shape.get(); }
		void ListMorphs(std::vector<std::string>& a_out) const;
		[[nodiscard]] size_t GetMovedCount() const { return state == State::kReady ? deformer->GetMoved().size() : 0; }
		[[nodiscard]] bool IsTakenOver() const { return own != nullptr; }
		// "shared", "pending", "ready" or "failed", and how many of the last totals this shape has.
		[[nodiscard]] std::string_view GetStateName() const;
		[[nodiscard]] size_t GetActiveCount() const { return activeCount; }

	private:
		enum class State : uint8_t
		{
			kShared,   // Still drawing from the shared range.
			kPending,    // Our range is being created.
			kPreparing,  // The deformer is being set up on a background thread.
			kReady,
			kFailed
		};

		bool TakeOver();
		bool IsRangeReady() const;
		bool Prepare();  // Reads the base vertices and starts setting up the deformer, once the range is ready.
		std::vector<SkinWeights> ReadSkin();
		// Reads the pose bones' transforms into pose. Returns false if they can't be read; a_changed is whether they
		// moved since the last read.
		bool ReadPose(bool& a_changed);
		void WriteAndUpload(ID3D11DeviceContext* a_context);
		void WriteVertex(uint32_t a_vertex, bool a_moved, bool a_shaded);
		void Upload(ID3D11DeviceContext* a_context, const std::vector<uint32_t>& a_vertices);
		// Whether corrective smoothing is on and the shape can take it (skinned, with normals).
		[[nodiscard]] bool WantsSmoothing() const;
		// Smooths on the GPU. False if it can't yet (data building) or at all; the CPU path runs then.
		bool ApplySmoothed(const MeshDeformer::ActiveMorphs& a_active, const Values& a_totals, ID3D11DeviceContext* a_context);
		// Every skin bone's transform, relative to the first. a_changed: whether any moved since the last read.
		bool ReadBones(std::vector<Affine>& a_out, bool& a_changed);

		RE::NiPointer<RE::BSTriShape> shape;
		std::vector<Source> sources;
		RE::BSGraphics::TriShape* own = nullptr;
		State state = State::kShared;
		uint32_t waitFrames = 0;
		size_t activeCount = 0;

		// Vertex layout.
		uint32_t stride = 0;
		uint32_t vertexCount = 0;
		uint32_t normalOffset = 0;   // 0 if none.
		uint32_t tangentOffset = 0;  // 0 if none.
		uint32_t skinOffset = 0;     // 0 if not skinned.
		bool fullPrecision = false;

		std::vector<std::byte> base;  // The vertices as they were when we took the shape.
		std::vector<std::byte> work;  // As last uploaded.
		// Shared with the thread that sets it up, which may outlive this.
		std::shared_ptr<MeshDeformer> deformer = std::make_shared<MeshDeformer>();
		std::shared_future<void> preparing;
		Values applied;
		uint32_t boneCount = 0;   // In the skin, if its weights were read.
		std::vector<Affine> pose;  // As last read, relative to the first pose bone.
		bool fullUpload = false;   // The next upload rewrites the whole range (after the GPU wrote it).

		// Corrective smoothing.
		std::vector<Vec3> restPositions;
		std::vector<uint32_t> triangles;
		std::vector<SkinWeights> skinWeights;
		std::vector<std::string> boneNames;      // The skin's bones, by index.
		SmoothLibrary::MeshFuture smoothData;
		uint64_t smoothGeneration = UINT64_MAX;  // Of the settings smoothData and gpu are for.
		uint64_t smoothFailed = UINT64_MAX;      // The settings generation smoothing failed for.
		std::unique_ptr<GpuSmoother> gpu;
		bool smoothed = false;                   // The GPU wrote the range last.
		Values smoothedTotals;                   // The morphs in the GPU's rest positions.
		std::vector<Affine> smoothedBones;       // As last given to the GPU.
		uint32_t smoothedPasses = UINT32_MAX;
		uint32_t smoothedAutoPasses = UINT32_MAX;
		uint64_t regionGeneration = UINT64_MAX;  // Of the region gpu has.
		bool inRegion = false;                   // Whether any of the shape is in it.
	};
}
#endif
