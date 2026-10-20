#pragma once
#include "Smoothing.h"

// Fallout 4: corrective smoothing (Smoothing.h) on the GPU, writing straight into a shape's vertex range.
#if defined TARGET_GAME_F4
struct ID3D11Buffer;
struct ID3D11DeviceContext;
struct ID3D11ShaderResourceView;
struct ID3D11UnorderedAccessView;

namespace Morph
{
	// One shape's smoothing on the GPU. Each update, with compute shaders on the game's immediate context:
	//   1. each weld group's smoothed position: sum over its bones of the bone's transform times its precomputed weight;
	//   2. the runtime passes (Corrective Smooth's Length Weight, current edge lengths), with open edges pinned;
	//   3. the rest detail added back in the smoothed surface's corner frames: the morphed rest position minus the rest
	//      mesh smoothed the same way, so every other morph comes through the smoothing as detail; then blended with
	//      plain skinning of the morphed rest position by how much the group is in the region;
	//      With stretch detail, the detail is scaled by how much the smoothed surface around it stretched (the
	//      square root of its area now over at rest, both averaged over a few rings);
	//   4. optionally, auto-smoothing: where the result is squashed and sharper than at rest (within the region), a few
	//      rings around, plain smoothing passes;
	//   4b. with volume preservation, each buttock's volume put back: summed over its groups' triangles from a point moving
	//      with the pelvis (in two dispatches, partial sums then the total, so nothing's read back), then the surface
	//      pushed out along its normals by the step that restores it, weighted by a soft mask kept off squashed skin;
	//      a few times, as the push changes the normals and areas;
	//   5. per vertex: the result carried back through the vertex's own skinning, so the game's skinning lands it there;
	//      its normal, tangent and bitangent turned from where skinning would put the rest surface's normal to the
	//      result's (by as much as the group is in the region, or wholly near morphed vertices), the same way; and all
	//      of it written in the shape's own vertex format.
	// The vertices go to a buffer of ours (which holds the rest of each vertex as it was), and that's copied into the
	// shape's range in the game's vertex buffer, on the GPU. The game's compute state is put back afterwards.
	class GpuSmoother
	{
	public:
		struct Layout
		{
			uint32_t stride = 0;
			uint32_t normalOffset = 0;   // 0 if none.
			uint32_t tangentOffset = 0;  // 0 if none.
			bool fullPrecision = false;
		};

		// a_base is the shape's vertices as taken (stride * vertex count bytes). Null if the GPU can't do it (logged).
		// Main thread, with the renderer locked.
		static std::unique_ptr<GpuSmoother> Create(std::shared_ptr<const SmoothMesh> a_mesh, const Layout& a_layout, const std::vector<std::byte>& a_base,
			const std::vector<SkinWeights>& a_skin, ID3D11Buffer* a_target, uint32_t a_targetOffset, const char* a_name);
		~GpuSmoother();
		GpuSmoother(const GpuSmoother&) = delete;
		GpuSmoother& operator=(const GpuSmoother&) = delete;

		// The morphed rest position of every vertex (rest plus every active morph); one vertex per weld group is read.
		// Takes effect at the next Update; must be set before the first.
		void SetRestPositions(const std::vector<Vec3>& a_positions);
		// How much each skin bone is in the region (0 to 1). Each group is in it by its bones' shares of its smoothed
		// position. Returns the most any group is in it. Takes effect at the next Update; must be set before the first.
		float SetRegion(const std::vector<float>& a_boneStrengths);
		// Each skin bone's part in volume preservation. Finds the buttocks (groups mostly the buttock bones', split by
		// side) and the point their volume is measured from (the middle of the skin mostly the pelvis's, moving with the
		// pelvis). Returns whether the shape has buttocks; without them, volume preservation only scales the detail.
		bool SetVolumeBones(const std::vector<VolumeBone>& a_bones);

		struct Options
		{
			uint32_t runtimePasses = 0;
			uint32_t autoPasses = 0;        // 0 skips auto-smoothing.
			bool stretchDetail = false;
			bool volumePreservation = false;
			float volumeRestore = 1.0f;     // How much of the buttocks' lost volume is put back, 0 to 1.

			bool operator==(const Options&) const = default;
		};
		// a_bones[i] is skin bone i's transform now (bind to current), relative to any frame shared by all of them.
		// Smooths and writes the shape's vertices. Main thread, with the renderer locked.
		void Update(ID3D11DeviceContext* a_context, const std::vector<Affine>& a_bones, const Options& a_options);

		[[nodiscard]] const std::shared_ptr<const SmoothMesh>& GetMesh() const { return mesh; }

		// A buffer of ours and its views.
		struct View
		{
			ID3D11Buffer* buffer = nullptr;
			ID3D11ShaderResourceView* srv = nullptr;
			ID3D11UnorderedAccessView* uav = nullptr;
			void Release();
		};

	private:
		GpuSmoother() = default;
		bool Init(const Layout& a_layout, const std::vector<std::byte>& a_base, const std::vector<SkinWeights>& a_skin, const char* a_name);
		void SmoothRest(ID3D11DeviceContext* a_context, uint32_t a_passes);

		std::shared_ptr<const SmoothMesh> mesh;
		Layout layout;
		uint32_t vertexCount = 0;
		ID3D11Buffer* target = nullptr;  // The game's; not ours.
		uint32_t targetOffset = 0;

		// Static: the mesh's data and each vertex's skinning and original bytes.
		View weightStart, weightBone, weights, neighbourStart, neighbours, cornerStart, corners, pinned, frameShaded, restNormal, smoothedRest;
		View groupVertex, wings, restEdgeAngle, restArea;
		View vertexGroup, vertexSkin, base;
		// Per update: bones, morphed rest positions and region/shading weights (per group), the smoothed mesh
		// (ping-pong), the result and how the smoothed surface turned at each group, auto-smoothing's mask (ping-pong),
		// the rest mesh after the runtime passes, and the vertices written.
		View bones, rest, region, smoothA, smoothB, result, turns, maskA, maskB, smoothedRestPassed, scratch, output;
		// Volume preservation: per group the buttock push masks and side (static), the partial and total sums, and the
		// step.
		View volumeMask, volumePartials, volumeStep;
		ID3D11Buffer* constants = nullptr;
		ID3D11Buffer* frameConstants = nullptr;  // Per update: the measuring point at rest and now, and the options.
		std::vector<float> pendingVolumeMask;    // Per group, xyzw; uploaded at the next Update.
		bool hasButtocks = false;
		uint32_t pelvisBone = 0;
		Vec3 volumePoint;                        // At rest.
		uint32_t restPassesDone = UINT32_MAX;  // The runtime passes smoothedRestPassed has had.
		std::vector<float> pendingRest;        // Per group, xyzw; uploaded at the next Update.
		std::vector<SkinWeights> skin;         // Per vertex.
		std::vector<float> regionWeight;       // Per group.
		std::vector<float> shadeWeight;        // Per group: 1 on and next to morphed groups.
		bool regionDirty = false;
	};
}
#endif
