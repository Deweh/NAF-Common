#pragma once
#include "Geometry.h"

// Corrective smoothing: Blender's Corrective Smooth modifier (Length Weight, factor 1), run on actors' skinned meshes
// in real time so skinning's collapsed and crumpled creases come out as Blender shows them.
//
// Corrective Smooth smooths the skinned mesh REPEAT times, then adds back the rest mesh's detail (rest minus rest
// smoothed the same way) in local frames of the smoothed surface. Here that's split in two:
//   - Precomputed, once per mesh (SmoothMesh): the first part of the smoothing with edge lengths fixed at rest, which
//     makes it linear in the bone transforms. Each vertex's smoothed position is then sum_b M_b * psi_b, a few bones'
//     transforms times a precomputed 4-vector each, about as cheap as skinning.
//   - At runtime (the game frontend, on the GPU): a few more passes on top with the current edge lengths, as Blender
//     does, which takes out most of the difference fixed lengths make; then the rest detail, from the morphed rest
//     mesh, so every other morph goes through the smoothing as detail.
// Against Blender (700 repeats), 700 precomputed + 100 runtime passes is within about 1 unit on the hip poses tested.
//
// Smoothing works on weld groups (vertices at the same rest position, split along UV seams), so seams don't open. With
// pinned boundaries, the mesh's open edges (a body's neck, where the head mesh meets it) keep plain skinning.
//
// It applies within a region, chosen by bone (SmoothRegion): a vertex is smoothed by the share of its smoothed
// position that the region's bones carry, so the region fades out as smoothly as the smoothing itself spreads. Outside,
// it's plain skinning (plus morphs). Smoothing everything rounds off joints bent far from the rest pose, like the
// shoulders of an A-pose body with its arms down, or knees.
//
// Optionally, within the region, crumpled creases are smoothed out afterwards (auto-smoothing): where the skin is both
// squashed (less area than at rest) and sharper than at rest (edges bent further than at rest), a few rings around,
// plain smoothing passes spread the bunched vertices out and soften the fold. Detail the body has at rest isn't sharper
// than at rest, so it's left alone.
namespace Morph
{
	// Where smoothing applies.
	enum class SmoothRegion : uint8_t
	{
		kHipsAndThighs,  // Pelvis, lower spine, buttocks and upper thighs; not the knees.
		kWholeBody,
		kCustom,         // Bones and strengths from Data/NAF/Smoothing.json.
	};
	// What the precomputed part depends on: changing it rebuilds every mesh's data.
	struct SmoothSettings
	{
		uint32_t repeat = 700;       // Precomputed smoothing passes.
		bool pinBoundaries = true;   // Open edges stay where skinning puts them.

		bool operator==(const SmoothSettings&) const = default;
	};

	// One mesh's precomputed smoothing data. Immutable once built.
	class SmoothMesh
	{
	public:
		struct Input
		{
			std::vector<Vec3> rest;            // Per vertex.
			std::vector<uint32_t> triangles;   // Three vertex indices each.
			std::vector<SkinWeights> skin;     // Per vertex.
			uint32_t boneCount = 0;
		};

		// Slow (around a second for a body, spread over the CPU's cores). Any thread. Null if the input can't be used.
		static std::shared_ptr<const SmoothMesh> Build(const Input& a_input, const SmoothSettings& a_settings);
		// Identifies the input and settings, so meshes built the same way share their data.
		static uint64_t Hash(const Input& a_input, const SmoothSettings& a_settings);

		uint32_t vertexCount = 0;
		uint32_t groupCount = 0;
		uint32_t boneCount = 0;
		std::vector<uint32_t> vertexGroup;   // Each vertex's weld group.
		std::vector<uint32_t> groupVertex;   // A vertex of each group.
		std::vector<uint8_t> pinned;         // Per group.
		std::vector<Vec3> restNormal;        // Per group, from the rest mesh.
		// Per group: whether its frames turn with the smoothed surface around it rather than with its own triangles' normal.
		// Set where those triangles are tiny or face every which way (the tips of toes), where their normal is mostly
		// rounding error.
		std::vector<uint8_t> frameShaded;
		std::vector<Vec3> smoothedRest;      // Per group: the rest mesh after the precomputed passes.
		std::vector<Vec3> rest;              // Per group: its rest position.
		std::vector<float> restArea;         // Per group: a third of its triangles' area at rest.
		// Neighbours (CSR by group).
		std::vector<uint32_t> neighbourStart, neighbours;
		// Per neighbour entry (edge g-n): the third group of the triangle with g->n in its winding and of the one with
		// n->g (kNoGroup on an open edge), and the angle between those triangles' normals at rest, in degrees.
		std::vector<std::array<uint32_t, 2>> wings;
		std::vector<float> restEdgeAngle;
		static constexpr uint32_t kNoGroup = UINT32_MAX;
		// Corners (CSR by group): for each triangle around the group, the groups before and after it, in winding order.
		std::vector<uint32_t> cornerStart;
		std::vector<std::array<uint32_t, 2>> corners;  // { previous, next }
		// Smoothing weights (CSR by group): bone index and its 4-vector. Bones that contribute less than 1e-3 of a
		// group's position are dropped, and the rest rescaled to sum to 1.
		std::vector<uint32_t> weightStart, weightBone;
		std::vector<std::array<float, 4>> weights;
		float buildMilliseconds = 0.0f;
	};

	// The smoothing settings, owned by Manager, and the cache of meshes' precomputed data.
	class SmoothLibrary
	{
	public:
		using MeshFuture = std::shared_future<std::shared_ptr<const SmoothMesh>>;

		// Whether actors' meshes are smoothed (off by default). Not saved.
		void SetEnabled(bool a_enabled) { enabled.store(a_enabled, std::memory_order_relaxed); }
		[[nodiscard]] bool GetEnabled() const { return enabled.load(std::memory_order_relaxed); }
		// Passes run at runtime on top of the precomputed ones, with current edge lengths. 0 skips them.
		void SetRuntimePasses(uint32_t a_passes) { runtimePasses.store((std::min)(a_passes, kMaxRuntimePasses), std::memory_order_relaxed); }
		[[nodiscard]] uint32_t GetRuntimePasses() const { return runtimePasses.load(std::memory_order_relaxed); }
		// Changing these drops every mesh's data, so each is rebuilt.
		void SetSettings(const SmoothSettings& a_settings);
		[[nodiscard]] SmoothSettings GetSettings() const;
		// Bumped by SetSettings.
		[[nodiscard]] uint64_t GetGeneration() const { return generation.load(std::memory_order_acquire); }

		// Where smoothing applies (hips and thighs by default). Custom reads Data/NAF/Smoothing.json, again on
		// ReloadCustom.
		void SetRegion(SmoothRegion a_region);
		[[nodiscard]] SmoothRegion GetRegion() const { return region.load(std::memory_order_relaxed); }
		void ReloadCustom();
		// How much a skin bone (by name, case-insensitive, with or without "_skin") is in the region, 0 to 1.
		[[nodiscard]] float GetBoneStrength(std::string_view a_bone) const;
		// Bumped when the region changes.
		[[nodiscard]] uint64_t GetRegionGeneration() const { return regionGeneration.load(std::memory_order_acquire); }

		// Auto-smoothing of crumpled creases within the region (off by default), and its passes.
		void SetAutoSmooth(bool a_enabled) { autoSmooth.store(a_enabled, std::memory_order_relaxed); }
		[[nodiscard]] bool GetAutoSmooth() const { return autoSmooth.load(std::memory_order_relaxed); }
		void SetAutoSmoothPasses(uint32_t a_passes) { autoSmoothPasses.store((std::min)(a_passes, kMaxAutoSmoothPasses), std::memory_order_relaxed); }
		[[nodiscard]] uint32_t GetAutoSmoothPasses() const { return autoSmoothPasses.load(std::memory_order_relaxed); }

		// The mesh's data, built on a background thread the first time it's asked for; meshes with the same input and
		// settings share it. Holders keep it alive; the cache doesn't.
		MeshFuture Get(SmoothMesh::Input a_input);

		static constexpr uint32_t kMaxRuntimePasses = 400;
		static constexpr uint32_t kMinRepeat = 1;
		static constexpr uint32_t kMaxRepeat = 5000;
		static constexpr uint32_t kMaxAutoSmoothPasses = 200;

	private:
		std::atomic<bool> enabled{ false };
		std::atomic<uint32_t> runtimePasses{ 100 };
		std::atomic<uint64_t> generation{ 0 };
		std::atomic<SmoothRegion> region{ SmoothRegion::kHipsAndThighs };
		std::atomic<uint64_t> regionGeneration{ 0 };
		std::atomic<bool> autoSmooth{ false };
		std::atomic<uint32_t> autoSmoothPasses{ 60 };
		// Custom region: bone name (lowercase, without "_skin") to strength.
		std::unordered_map<std::string, float> customBones;
		mutable std::mutex lock;
		SmoothSettings settings;
		// Meshes being built, and built meshes while anyone holds them.
		struct Entry
		{
			std::optional<MeshFuture> building;
			std::weak_ptr<const SmoothMesh> built;
		};
		std::unordered_map<uint64_t, Entry> cache;
	};
}
