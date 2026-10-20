#include "GpuSmoother.h"

#if defined TARGET_GAME_F4
// First: it orders the D3D headers around Renderer.h.
#include "UI/DX11Renderer.h"

namespace Morph
{
	namespace
	{
		// D3DCompile, from the system's d3dcompiler_47.dll (its header doesn't build alongside the game headers).
		using D3DCompileFunc = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);
		constexpr UINT kOptimizationLevel3 = 1u << 15;  // D3DCOMPILE_OPTIMIZATION_LEVEL3

		D3DCompileFunc GetCompiler()
		{
			static const D3DCompileFunc func = []() -> D3DCompileFunc {
				auto module = ::LoadLibraryW(L"d3dcompiler_47.dll");
				return module ? reinterpret_cast<D3DCompileFunc>(::GetProcAddress(module, "D3DCompile")) : nullptr;
			}();
			return func;
		}

		constexpr uint32_t kThreads = 64;
		constexpr uint32_t kSrvSlots = 29;
		constexpr uint32_t kUavSlots = 3;
		constexpr uint32_t kConstantSlots = 2;
		// Auto-smoothing's mask spreads this many rings out from where the creases are.
		constexpr uint32_t kMaskRings = 4;
		// Stretch detail compares areas averaged over this many rings. Volume preservation softens its push mask over this
		// many (after it's kept off squashed skin), and restores the volume in this many steps.
		constexpr uint32_t kStretchRings = 6;
		constexpr uint32_t kVolumeMaskRings = 3;
		constexpr uint32_t kVolumeSteps = 4;
		// A group is buttock where the buttock bones carry more than this share of its skin; the push mask starts at
		// kButtockMaskFrom of it and is whole kButtockMaskWidth above that, softened over kButtockMaskRings rings. Across
		// the middle, the sides blend over kSideBlend units either way. The measuring point is the middle of the skin
		// the pelvis bones carry more than kPelvisShare of.
		constexpr float kButtockShare = 0.25f;
		constexpr float kButtockMaskFrom = 0.1f;
		constexpr float kButtockMaskWidth = 0.25f;
		constexpr uint32_t kButtockMaskRings = 4;
		constexpr float kSideBlend = 1.5f;
		constexpr float kPelvisShare = 0.5f;
		// A group moved by morphs by more than this counts as morphed (its surroundings' frames turn wholly).
		constexpr float kMorphedDistance = 1e-5f;
		// A group whose smoothed position the region's bones carry less than this share of is out of the region; from
		// kRegionFull up, wholly in. On the body's hip region: the hip crease is in, the knees, abs and shoulders out,
		// mid-thigh and waist partly.
		constexpr float kRegionFrom = 0.3f;
		constexpr float kRegionFull = 0.65f;

		// See GpuSmoother in the header for the steps. Weld groups are "groups"; a vertex's group is VertexGroup.
		constexpr const char* kShaders = R"HLSL(
cbuffer Params : register(b0)
{
	uint GroupCount;
	uint VertexCount;
	uint Stride;
	uint NormalOffset;
	uint TangentOffset;
	uint FullPrecision;
	uint BoneCount;
	uint Pad;
};

cbuffer Frame : register(b1)
{
	float4 VolumePointRest;
	float4 VolumePointNow;
	float VolumeRestore;
	uint PartialCount;
	uint StretchDetail;
	uint FramePad;
};

static const uint kNoGroup = 0xFFFFFFFF;
// Stretch detail's scale is kept within these. Volume preservation's buttock push is off where the skin has under
// kKeepOffSquashed of its rest area, whole from kKeepOffSquashed + kKeepOffWidth.
static const float kStretchMin = 0.7;
static const float kStretchMax = 1.6;
static const float kKeepOffSquashed = 0.6;
static const float kKeepOffWidth = 0.4;
// Auto-smoothing: skin squashed to under 100% of its rest area counts, fully at 40%; an edge bent 8 degrees more than at
// rest starts to count, fully at 25 degrees more.
static const float kSquashFrom = 1.0;
static const float kSquashFull = 0.4;
static const float kSharperFrom = 8.0;
static const float kSharperFull = 25.0;

StructuredBuffer<uint> WeightStart : register(t0);
StructuredBuffer<uint> WeightBone : register(t1);
StructuredBuffer<float4> Weights : register(t2);
StructuredBuffer<float4> Bones : register(t3);           // Three rows per bone: linear row, translation in w.
StructuredBuffer<float4> Xin : register(t4);
StructuredBuffer<uint> NeighbourStart : register(t5);
StructuredBuffer<uint> Neighbours : register(t6);
StructuredBuffer<uint> Pinned : register(t7);
StructuredBuffer<float4> Smoothed : register(t8);
StructuredBuffer<float4> SmoothedRest : register(t9);
StructuredBuffer<uint> CornerStart : register(t10);
StructuredBuffer<uint2> Corners : register(t11);         // Previous and next group, in winding order.
StructuredBuffer<float4> Rest : register(t12);           // Morphed rest position, per group.
StructuredBuffer<float4> Result : register(t13);
StructuredBuffer<uint> VertexGroup : register(t14);
StructuredBuffer<uint4> VertexSkin : register(t15);      // Four bone bytes, then three weights (the fourth is the rest).
ByteAddressBuffer Base : register(t16);                  // The vertices as taken.
StructuredBuffer<float4> RestNormal : register(t17);     // Per group.
StructuredBuffer<uint> FrameShaded : register(t18);      // Per group (SmoothMesh::frameShaded).
StructuredBuffer<float4> Turns : register(t19);          // Per group, three rows: the smoothed surface's turn there.
StructuredBuffer<uint> GroupVertex : register(t20);
StructuredBuffer<float4> Region : register(t21);         // Per group: how much it's in the region; 1 near morphs.
StructuredBuffer<uint2> Wings : register(t22);           // Per neighbour entry (SmoothMesh::wings).
StructuredBuffer<float> RestEdgeAngle : register(t23);   // Per neighbour entry, degrees.
StructuredBuffer<float> RestArea : register(t24);        // Per group.
StructuredBuffer<float4> Mask : register(t25);           // Auto-smoothing's weight in x; or areas, or push weights.
StructuredBuffer<float4> VolumeMask : register(t26);     // Per group: left and right buttock push mask, side (0, 1 or 2).
StructuredBuffer<float4> Partials : register(t27);       // Two per thread group (VolumePartialCS).
StructuredBuffer<float4> VolumeStep : register(t28);     // The push now (left, right), then the target volumes.
RWStructuredBuffer<float4> Out : register(u0);
RWByteAddressBuffer OutVertices : register(u1);
RWStructuredBuffer<float4> TurnsOut : register(u2);

float3 SafeNormalize(float3 v) { return v * rsqrt(max(dot(v, v), 1e-24)); }

// A vertex's skinning transform, as rows (translation in w).
void SkinRows(uint4 skin, out float4 r0, out float4 r1, out float4 r2)
{
	float4 w = float4(asfloat(skin.y), asfloat(skin.z), asfloat(skin.w), 0);
	w.w = max(1.0 - w.x - w.y - w.z, 0.0);
	r0 = 0;
	r1 = 0;
	r2 = 0;
	[unroll] for (uint i = 0; i < 4; i++) {
		uint b = ((skin.x >> (8 * i)) & 0xFF) * 3;
		if (w[i] > 0) {
			r0 += Bones[b] * w[i];
			r1 += Bones[b + 1] * w[i];
			r2 += Bones[b + 2] * w[i];
		}
	}
}

// The smoothed mesh: the bones' transforms times the precomputed weights.
[numthreads(64, 1, 1)] void SmoothCS(uint3 id : SV_DispatchThreadID)
{
	uint g = id.x;
	if (g >= GroupCount)
		return;
	uint begin = WeightStart[g], end = WeightStart[g + 1];
	float3 s = 0;
	for (uint i = begin; i < end; i++) {
		uint b = WeightBone[i] * 3;
		float4 w = Weights[i];
		s += float3(dot(Bones[b], w), dot(Bones[b + 1], w), dot(Bones[b + 2], w));
	}
	Out[g] = float4(begin == end ? Rest[g].xyz : s, 1);
}

// One Corrective Smooth (Length Weight, factor 1) pass with the current edge lengths.
[numthreads(64, 1, 1)] void PassCS(uint3 id : SV_DispatchThreadID)
{
	uint g = id.x;
	if (g >= GroupCount)
		return;
	float3 x = Xin[g].xyz;
	uint begin = NeighbourStart[g], end = NeighbourStart[g + 1];
	if (Pinned[g] != 0 || begin == end) {
		Out[g] = float4(x, 1);
		return;
	}
	float3 acc = 0;
	float total = 0;
	for (uint k = begin; k < end; k++) {
		float3 d = Xin[Neighbours[k]].xyz - x;
		float l = length(d);
		acc += d * l;
		total += l;
	}
	Out[g] = float4(total > 1e-12 ? x + acc * (2.0 / (total * (end - begin))) : x, 1);
}

// A corner's frame on a smoothed surface, as Blender's calc_tangent_loop, and its angle (the corner's weight).
void CornerFrame(float3 p, float3 prev, float3 next, out float3 t, out float3 b, out float3 n, out float w)
{
	float3 toPrev = SafeNormalize(prev - p);
	float3 fromNext = SafeNormalize(p - next);
	n = SafeNormalize(cross(toPrev, fromNext));
	t = SafeNormalize(toPrev + fromNext);
	b = cross(n, t);
	w = acos(clamp(dot(fromNext, toPrev), -1.0, 1.0));
}

// The rest detail (morphed rest minus rest smoothed), carried from each corner's rest frame to its frame now, then
// blended with plain skinning of the morphed rest position by how much the group is in the region. Also keeps how the
// corners' frames turned (their average, rest to now), for frame-shaded groups' normals.
[numthreads(64, 1, 1)] void DetailCS(uint3 id : SV_DispatchThreadID)
{
	uint g = id.x;
	if (g >= GroupCount)
		return;
	float3 s = Smoothed[g].xyz;
	float3 s0 = SmoothedRest[g].xyz;
	float3 rest = Rest[g].xyz;
	float3 delta = rest - s0;
	float3 acc = 0;
	float3 q0 = 0, q1 = 0, q2 = 0;
	float total = 0;
	for (uint c = CornerStart[g]; c < CornerStart[g + 1]; c++) {
		uint2 pn = Corners[c];
		float3 t0, b0, n0, t, b, n;
		float w0, w;
		CornerFrame(s0, SmoothedRest[pn.x].xyz, SmoothedRest[pn.y].xyz, t0, b0, n0, w0);
		CornerFrame(s, Smoothed[pn.x].xyz, Smoothed[pn.y].xyz, t, b, n, w);
		float3 local = float3(dot(t0, delta), dot(b0, delta), dot(n0, delta));
		acc += (t * local.x + b * local.y + n * local.z) * w;
		q0 += (t.x * t0 + b.x * b0 + n.x * n0) * w;
		q1 += (t.y * t0 + b.y * b0 + n.y * n0) * w;
		q2 += (t.z * t0 + b.z * b0 + n.z * n0) * w;
		total += w;
	}
	// Stretch detail: the detail grown or shrunk with the surface around it (Mask: its area now and at rest).
	float scale = 1.0;
	if (StretchDetail != 0) {
		float2 areas = Mask[g].xy;
		scale = clamp(sqrt(areas.x / max(areas.y, 1e-12)), kStretchMin, kStretchMax);
	}
	float3 smoothed = s + (total > 0 ? acc / total : delta) * scale;
	float inRegion = Region[g].x;
	if (inRegion < 1.0) {
		float4 r0, r1, r2;
		SkinRows(VertexSkin[GroupVertex[g]], r0, r1, r2);
		float3 skinned = float3(dot(r0.xyz, rest) + r0.w, dot(r1.xyz, rest) + r1.w, dot(r2.xyz, rest) + r2.w);
		smoothed = lerp(skinned, smoothed, inRegion);
	}
	Out[g] = float4(smoothed, 1);
	if (total <= 0) {
		q0 = float3(1, 0, 0);
		q1 = float3(0, 1, 0);
		q2 = float3(0, 0, 1);
	}
	TurnsOut[g * 3] = float4(q0, 0);
	TurnsOut[g * 3 + 1] = float4(q1, 0);
	TurnsOut[g * 3 + 2] = float4(q2, 0);
}

// Auto-smoothing's mask: how squashed the skin is here and how much sharper than at rest its sharpest edge is, both
// needed, by how much the group is in the region.
[numthreads(64, 1, 1)] void MaskCS(uint3 id : SV_DispatchThreadID)
{
	uint g = id.x;
	if (g >= GroupCount)
		return;
	float3 p = Xin[g].xyz;
	float area = 0;
	for (uint c = CornerStart[g]; c < CornerStart[g + 1]; c++) {
		uint2 pn = Corners[c];
		area += length(cross(Xin[pn.y].xyz - p, Xin[pn.x].xyz - p));
	}
	float ratio = area / 6.0 / max(RestArea[g], 1e-12);
	float t = saturate((kSquashFrom - ratio) / (kSquashFrom - kSquashFull));
	float squashed = t * t * (3.0 - 2.0 * t);
	float sharper = 0;
	for (uint k = NeighbourStart[g]; k < NeighbourStart[g + 1]; k++) {
		uint2 wing = Wings[k];
		if (wing.x == kNoGroup || wing.y == kNoGroup)
			continue;
		float3 pn = Xin[Neighbours[k]].xyz;
		float3 n1 = SafeNormalize(cross(pn - p, Xin[wing.x].xyz - p));
		float3 n2 = SafeNormalize(cross(p - pn, Xin[wing.y].xyz - pn));
		float angle = degrees(acos(clamp(dot(n1, n2), -1.0, 1.0)));
		sharper = max(sharper, saturate((angle - RestEdgeAngle[k] - kSharperFrom) / (kSharperFull - kSharperFrom)));
	}
	Out[g] = float4(sharper * squashed * Region[g].x, 0, 0, 0);
}

// The mask spread a ring: at least half its neighbours' average.
[numthreads(64, 1, 1)] void DilateCS(uint3 id : SV_DispatchThreadID)
{
	uint g = id.x;
	if (g >= GroupCount)
		return;
	uint begin = NeighbourStart[g], end = NeighbourStart[g + 1];
	float sum = 0;
	for (uint k = begin; k < end; k++) sum += Mask[Neighbours[k]].x;
	float w = Mask[g].x;
	Out[g] = float4(begin < end ? max(w, 0.5 * sum / (end - begin)) : w, 0, 0, 0);
}

// One auto-smoothing pass: toward the neighbours' average, by half the mask.
[numthreads(64, 1, 1)] void RelaxCS(uint3 id : SV_DispatchThreadID)
{
	uint g = id.x;
	if (g >= GroupCount)
		return;
	float3 x = Xin[g].xyz;
	float w = Mask[g].x;
	uint begin = NeighbourStart[g], end = NeighbourStart[g + 1];
	if (w <= 0 || begin == end || Pinned[g] != 0) {
		Out[g] = float4(x, 1);
		return;
	}
	float3 sum = 0;
	for (uint k = begin; k < end; k++) sum += Xin[Neighbours[k]].xyz;
	Out[g] = float4(x + 0.5 * w * (sum / (end - begin) - x), 1);
}

// A group's area on a surface: a third of its triangles'.
float GroupArea(StructuredBuffer<float4> x, uint g)
{
	float3 p = x[g].xyz;
	float area = 0;
	for (uint c = CornerStart[g]; c < CornerStart[g + 1]; c++) {
		uint2 pn = Corners[c];
		area += length(cross(x[pn.y].xyz - p, x[pn.x].xyz - p));
	}
	return area / 6.0;
}

// Stretch detail: each group's area on the smoothed mesh and on the smoothed rest mesh.
[numthreads(64, 1, 1)] void StretchAreaCS(uint3 id : SV_DispatchThreadID)
{
	uint g = id.x;
	if (g >= GroupCount)
		return;
	Out[g] = float4(GroupArea(Smoothed, g), GroupArea(SmoothedRest, g), 0, 0);
}

// Mask averaged with its neighbours (all four components).
[numthreads(64, 1, 1)] void AverageCS(uint3 id : SV_DispatchThreadID)
{
	uint g = id.x;
	if (g >= GroupCount)
		return;
	uint begin = NeighbourStart[g], end = NeighbourStart[g + 1];
	float4 sum = Mask[g];
	for (uint k = begin; k < end; k++) sum += Mask[Neighbours[k]];
	Out[g] = sum / float(end - begin + 1);
}

// The buttock push weights: the static masks, by how much the group is in the region, off where the skin is squashed.
[numthreads(64, 1, 1)] void VolumeWeightCS(uint3 id : SV_DispatchThreadID)
{
	uint g = id.x;
	if (g >= GroupCount)
		return;
	float t = saturate((GroupArea(Xin, g) / max(RestArea[g], 1e-12) - kKeepOffSquashed) / kKeepOffWidth);
	Out[g] = float4(VolumeMask[g].xy * (t * t * (3.0 - 2.0 * t) * Region[g].x), 0, 0);
}

groupshared float4 SumA[64];
groupshared float4 SumB[64];

void SumGroup(uint i)
{
	GroupMemoryBarrierWithGroupSync();
	[unroll] for (uint s = 32; s > 0; s >>= 1) {
		if (i < s) {
			SumA[i] += SumA[i + s];
			SumB[i] += SumB[i + s];
		}
		GroupMemoryBarrierWithGroupSync();
	}
}

// Each thread group's sums: left and right buttock volume now and at rest (each group's triangles' tetrahedra to the
// measuring point, a third each, as every triangle is at three groups' corners), and the push weights times area.
[numthreads(64, 1, 1)] void VolumePartialCS(uint3 id : SV_DispatchThreadID, uint3 group : SV_GroupID, uint i : SV_GroupIndex)
{
	uint g = id.x;
	float4 a = 0, b = 0;
	if (g < GroupCount) {
		float3 p = Xin[g].xyz, r = Rest[g].xyz;
		float3 c = VolumePointNow.xyz, c0 = VolumePointRest.xyz;
		float area = 0, volume = 0, restVolume = 0;
		for (uint k = CornerStart[g]; k < CornerStart[g + 1]; k++) {
			uint2 pn = Corners[k];
			float3 next = Xin[pn.y].xyz, prev = Xin[pn.x].xyz;
			area += length(cross(next - p, prev - p));
			volume += dot(p - c, cross(next - c, prev - c));
			restVolume += dot(r - c0, cross(Rest[pn.y].xyz - c0, Rest[pn.x].xyz - c0));
		}
		float side = VolumeMask[g].z;
		if (side > 1.5) {
			a.y = volume / 18.0;
			b.y = restVolume / 18.0;
		} else if (side > 0.5) {
			a.x = volume / 18.0;
			b.x = restVolume / 18.0;
		}
		a.zw = Mask[g].xy * (area / 6.0);
	}
	SumA[i] = a;
	SumB[i] = b;
	SumGroup(i);
	if (i == 0) {
		Out[group.x * 2] = SumA[0];
		Out[group.x * 2 + 1] = SumB[0];
	}
}

// The totals, and the push that restores each buttock's volume to its target (set the first time each update, from the
// volume then and VolumeRestore of what it lost).
[numthreads(64, 1, 1)] void VolumeSumCS(uint i : SV_GroupIndex)
{
	float4 a = 0, b = 0;
	for (uint k = i; k < PartialCount; k += 64) {
		a += Partials[k * 2];
		b += Partials[k * 2 + 1];
	}
	SumA[i] = a;
	SumB[i] = b;
	SumGroup(i);
	if (i == 0) {
		a = SumA[0];
		b = SumB[0];
		float4 target = Out[1];
		if (target.w == 0) {
			target = float4(a.xy + VolumeRestore * max(b.xy - a.xy, 0), 0, 1);
			Out[1] = target;
		}
		Out[0] = float4(max(target.xy - a.xy, 0) / max(a.zw, 1e-12), 0, 0);
	}
}

// The push: out along the normal by the step times the group's weight, each side.
[numthreads(64, 1, 1)] void VolumePushCS(uint3 id : SV_DispatchThreadID)
{
	uint g = id.x;
	if (g >= GroupCount)
		return;
	float3 p = Xin[g].xyz;
	float amount = dot(Mask[g].xy, VolumeStep[0].xy);
	if (amount <= 0 || Pinned[g] != 0) {
		Out[g] = float4(p, 1);
		return;
	}
	float3 n = 0;
	for (uint k = CornerStart[g]; k < CornerStart[g + 1]; k++) {
		uint2 pn = Corners[k];
		n += cross(Xin[pn.y].xyz - p, Xin[pn.x].xyz - p);
	}
	Out[g] = float4(p + SafeNormalize(n) * amount, 1);
}

float UnpackByte(uint v) { return float(v & 0xFF) / 255.0 * 2.0 - 1.0; }
uint PackByte(float v) { return uint(round((clamp(v, -1.0, 1.0) + 1.0) * 0.5 * 255.0)); }
float3 UnpackDirection(uint w) { return float3(UnpackByte(w), UnpackByte(w >> 8), UnpackByte(w >> 16)); }
uint PackDirection(float3 d, float extra) { return PackByte(d.x) | (PackByte(d.y) << 8) | (PackByte(d.z) << 16) | (PackByte(extra) << 24); }

// v turned by a_amount (0 to 1) of the rotation taking unit from to unit to.
float3 RotateToward(float3 from, float3 to, float3 v, float amount)
{
	float3 axis = cross(from, to);
	float s = length(axis);
	if (s < 1e-6 || amount <= 0)
		return v;
	float angle = atan2(s, dot(from, to)) * min(amount, 1.0);
	float3 k = axis / s;
	float ca = cos(angle), sa = sin(angle);
	return v * ca + cross(k, v) * sa + k * dot(k, v) * (1.0 - ca);
}

// Each vertex: the result carried back through its skinning, its frame turned, written in the shape's format.
[numthreads(64, 1, 1)] void WriteCS(uint3 id : SV_DispatchThreadID)
{
	uint v = id.x;
	if (v >= VertexCount)
		return;
	uint g = VertexGroup[v];
	float3 p = Result[g].xyz;

	float4 r0, r1, r2;
	SkinRows(VertexSkin[v], r0, r1, r2);
	float3 a = r0.xyz, b = r1.xyz, c = r2.xyz;
	float3 c0 = cross(b, c), c1 = cross(c, a), c2 = cross(a, b);
	float det = dot(a, c0);
	float invDet = abs(det) > 1e-12 ? 1.0 / det : 0.0;
	#define SKIN(d) float3(dot(a, d), dot(b, d), dot(c, d))
	#define UNSKIN(d) ((c0 * (d).x + c1 * (d).y + c2 * (d).z) * invDet)
	float3 position = invDet != 0 ? UNSKIN(p - float3(r0.w, r1.w, r2.w)) : p;

	uint addr = v * Stride;
	float bitangentX = FullPrecision != 0 ? asfloat(Base.Load(addr + 12)) : f16tof32(Base.Load(addr + 4) >> 16);
	float amount = saturate(max(Region[g].x, Region[g].y));
	if (NormalOffset != 0 && invDet != 0 && amount > 0) {
		// Frames turn from where skinning puts the rest surface's normal to the result's; or, where the group's own
		// triangles' normal can't be trusted, with the smoothed surface around it. By as much as the group is in the
		// region, or wholly next to morphed groups; outside both, they're as authored.
		bool frameShaded = FrameShaded[g] != 0;
		float3 q0 = Turns[g * 3].xyz, q1 = Turns[g * 3 + 1].xyz, q2 = Turns[g * 3 + 2].xyz;
		float3 now = 0;
		for (uint k = CornerStart[g]; k < CornerStart[g + 1]; k++) {
			uint2 pn = Corners[k];
			now += cross(Result[pn.y].xyz - p, Result[pn.x].xyz - p);
		}
		now = SafeNormalize(now);
		float3 skinned = SafeNormalize(SKIN(RestNormal[g].xyz));
		#define POSED(d) (frameShaded ? lerp(SKIN(d), float3(dot(q0, d), dot(q1, d), dot(q2, d)), amount) : RotateToward(skinned, now, SKIN(d), amount))
		#define TURN(d) SafeNormalize(UNSKIN(POSED(d)))

		uint normalWord = Base.Load(addr + NormalOffset);
		uint tangentWord = TangentOffset != 0 ? Base.Load(addr + TangentOffset) : 0;
		float3 bitangent = float3(bitangentX, UnpackByte(normalWord >> 24), UnpackByte(tangentWord >> 24));
		float3 normal = TURN(UnpackDirection(normalWord));
		bitangent = TURN(bitangent);
		OutVertices.Store(addr + NormalOffset, PackDirection(normal, bitangent.y));
		if (TangentOffset != 0)
			OutVertices.Store(addr + TangentOffset, PackDirection(TURN(UnpackDirection(tangentWord)), bitangent.z));
		bitangentX = bitangent.x;
	} else if (NormalOffset != 0) {
		// As authored (the base may have been turned by an earlier update).
		OutVertices.Store(addr + NormalOffset, Base.Load(addr + NormalOffset));
		if (TangentOffset != 0)
			OutVertices.Store(addr + TangentOffset, Base.Load(addr + TangentOffset));
	}
	if (FullPrecision != 0) {
		OutVertices.Store4(addr, uint4(asuint(position.x), asuint(position.y), asuint(position.z), asuint(bitangentX)));
	} else {
		OutVertices.Store2(addr, uint2(f32tof16(position.x) | (f32tof16(position.y) << 16), f32tof16(position.z) | (f32tof16(bitangentX) << 16)));
	}
}
)HLSL";

		struct Shaders
		{
			ID3D11ComputeShader* smooth = nullptr;
			ID3D11ComputeShader* pass = nullptr;
			ID3D11ComputeShader* detail = nullptr;
			ID3D11ComputeShader* mask = nullptr;
			ID3D11ComputeShader* dilate = nullptr;
			ID3D11ComputeShader* relax = nullptr;
			ID3D11ComputeShader* write = nullptr;
			ID3D11ComputeShader* stretchArea = nullptr;
			ID3D11ComputeShader* average = nullptr;
			ID3D11ComputeShader* volumeWeight = nullptr;
			ID3D11ComputeShader* volumePartial = nullptr;
			ID3D11ComputeShader* volumeSum = nullptr;
			ID3D11ComputeShader* volumePush = nullptr;
			bool tried = false;
		};

		// Compiled the first time they're needed. Main thread.
		const Shaders* GetShaders(ID3D11Device* a_device)
		{
			static Shaders shaders;
			if (!shaders.tried) {
				shaders.tried = true;
				const auto d3dCompile = GetCompiler();
				if (!d3dCompile) {
					logger::error("Morphs: couldn't load d3dcompiler_47.dll; corrective smoothing can't run.");
					return nullptr;
				}
				const auto compile = [&](const char* a_entry) -> ID3D11ComputeShader* {
					ID3DBlob* code = nullptr;
					ID3DBlob* errors = nullptr;
					const HRESULT hr = d3dCompile(kShaders, std::strlen(kShaders), "NAF_Smoothing", nullptr, nullptr, a_entry, "cs_5_0", kOptimizationLevel3, 0, &code, &errors);
					if (FAILED(hr)) {
						logger::error("Morphs: couldn't compile the smoothing shader {}: {}", a_entry, errors ? static_cast<const char*>(errors->GetBufferPointer()) : "unknown error");
						if (errors)
							errors->Release();
						return nullptr;
					}
					if (errors)
						errors->Release();
					ID3D11ComputeShader* shader = nullptr;
					a_device->CreateComputeShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader);
					code->Release();
					return shader;
				};
				shaders.smooth = compile("SmoothCS");
				shaders.pass = compile("PassCS");
				shaders.detail = compile("DetailCS");
				shaders.mask = compile("MaskCS");
				shaders.dilate = compile("DilateCS");
				shaders.relax = compile("RelaxCS");
				shaders.write = compile("WriteCS");
				shaders.stretchArea = compile("StretchAreaCS");
				shaders.average = compile("AverageCS");
				shaders.volumeWeight = compile("VolumeWeightCS");
				shaders.volumePartial = compile("VolumePartialCS");
				shaders.volumeSum = compile("VolumeSumCS");
				shaders.volumePush = compile("VolumePushCS");
			}
			const auto& s = shaders;
			const bool all = s.smooth && s.pass && s.detail && s.mask && s.dilate && s.relax && s.write && s.stretchArea && s.average && s.volumeWeight && s.volumePartial &&
			                 s.volumeSum && s.volumePush;
			return all ? &shaders : nullptr;
		}

		// The game's compute state, put back after we've used the context.
		class SavedComputeState
		{
		public:
			explicit SavedComputeState(ID3D11DeviceContext* a_context) :
				context(a_context)
			{
				context->CSGetShader(&shader, nullptr, nullptr);
				context->CSGetShaderResources(0, kSrvSlots, srvs);
				context->CSGetUnorderedAccessViews(0, kUavSlots, uavs);
				context->CSGetConstantBuffers(0, kConstantSlots, constants);
			}
			~SavedComputeState()
			{
				ID3D11UnorderedAccessView* none[kUavSlots]{};
				context->CSSetUnorderedAccessViews(0, kUavSlots, none, nullptr);
				context->CSSetShader(shader, nullptr, 0);
				context->CSSetShaderResources(0, kSrvSlots, srvs);
				const UINT keep[kUavSlots]{ UINT(-1), UINT(-1), UINT(-1) };
				context->CSSetUnorderedAccessViews(0, kUavSlots, uavs, keep);
				context->CSSetConstantBuffers(0, kConstantSlots, constants);
				if (shader)
					shader->Release();
				for (auto srv : srvs) {
					if (srv)
						srv->Release();
				}
				for (auto uav : uavs) {
					if (uav)
						uav->Release();
				}
				for (auto buffer : constants) {
					if (buffer)
						buffer->Release();
				}
			}
			SavedComputeState(const SavedComputeState&) = delete;
			SavedComputeState& operator=(const SavedComputeState&) = delete;

		private:
			ID3D11DeviceContext* context;
			ID3D11ComputeShader* shader = nullptr;
			ID3D11ShaderResourceView* srvs[kSrvSlots]{};
			ID3D11UnorderedAccessView* uavs[kUavSlots]{};
			ID3D11Buffer* constants[kConstantSlots]{};
		};

		struct Params
		{
			uint32_t groupCount;
			uint32_t vertexCount;
			uint32_t stride;
			uint32_t normalOffset;
			uint32_t tangentOffset;
			uint32_t fullPrecision;
			uint32_t boneCount;
			uint32_t pad;
		};
		static_assert(sizeof(Params) == 32);

		struct FrameParams
		{
			float volumePointRest[4];
			float volumePointNow[4];
			float volumeRestore;
			uint32_t partialCount;
			uint32_t stretchDetail;
			uint32_t pad;
		};
		static_assert(sizeof(FrameParams) == 48);
	}

	void GpuSmoother::View::Release()
	{
		if (uav)
			uav->Release();
		if (srv)
			srv->Release();
		if (buffer)
			buffer->Release();
		*this = {};
	}

	namespace
	{
		ID3D11Device* GetDevice(ID3D11Buffer* a_buffer)
		{
			ID3D11Device* device = nullptr;
			a_buffer->GetDevice(&device);
			if (device)
				device->Release();  // The game holds it.
			return device;
		}

		// A raw (byte address) buffer, readable or writable by shaders, filled from a_data if given.
		bool MakeRaw(ID3D11Device* a_device, size_t a_bytes, const void* a_data, bool a_writable, GpuSmoother::View& a_out)
		{
			D3D11_BUFFER_DESC desc{};
			desc.ByteWidth = static_cast<UINT>(a_bytes);
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = a_writable ? D3D11_BIND_UNORDERED_ACCESS : D3D11_BIND_SHADER_RESOURCE;
			desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
			D3D11_SUBRESOURCE_DATA init{ a_data };
			if (FAILED(a_device->CreateBuffer(&desc, a_data ? &init : nullptr, &a_out.buffer)))
				return false;
			if (a_writable) {
				D3D11_UNORDERED_ACCESS_VIEW_DESC u{};
				u.Format = DXGI_FORMAT_R32_TYPELESS;
				u.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
				u.Buffer.NumElements = desc.ByteWidth / 4;
				u.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
				return SUCCEEDED(a_device->CreateUnorderedAccessView(a_out.buffer, &u, &a_out.uav));
			}
			D3D11_SHADER_RESOURCE_VIEW_DESC s{};
			s.Format = DXGI_FORMAT_R32_TYPELESS;
			s.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
			s.BufferEx.NumElements = desc.ByteWidth / 4;
			s.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
			return SUCCEEDED(a_device->CreateShaderResourceView(a_out.buffer, &s, &a_out.srv));
		}

		// A structured buffer of a_count elements of a_stride bytes, filled from a_data if given.
		bool MakeStructured(ID3D11Device* a_device, uint32_t a_stride, size_t a_count, const void* a_data, bool a_writable, GpuSmoother::View& a_out)
		{
			D3D11_BUFFER_DESC desc{};
			desc.ByteWidth = static_cast<UINT>((std::max<size_t>)(a_count, 1) * a_stride);
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | (a_writable ? D3D11_BIND_UNORDERED_ACCESS : 0);
			desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
			desc.StructureByteStride = a_stride;
			D3D11_SUBRESOURCE_DATA init{ a_data };
			if (FAILED(a_device->CreateBuffer(&desc, a_data && a_count ? &init : nullptr, &a_out.buffer)))
				return false;
			if (FAILED(a_device->CreateShaderResourceView(a_out.buffer, nullptr, &a_out.srv)))
				return false;
			return !a_writable || SUCCEEDED(a_device->CreateUnorderedAccessView(a_out.buffer, nullptr, &a_out.uav));
		}

		std::vector<float> ToFloat4(const std::vector<Vec3>& a_in)
		{
			std::vector<float> out(a_in.size() * 4);
			for (size_t i = 0; i < a_in.size(); i++) {
				out[i * 4] = a_in[i].x;
				out[i * 4 + 1] = a_in[i].y;
				out[i * 4 + 2] = a_in[i].z;
				out[i * 4 + 3] = 1.0f;
			}
			return out;
		}

		using SrvSlots = std::initializer_list<std::pair<uint32_t, ID3D11ShaderResourceView*>>;
		using UavSlots = std::initializer_list<std::pair<uint32_t, ID3D11UnorderedAccessView*>>;

		// Runs a_shader over a_count threads with the given views bound (by slot), then unbinds them.
		void Run(ID3D11DeviceContext* a_context, ID3D11ComputeShader* a_shader, SrvSlots a_srvs, UavSlots a_uavs, uint32_t a_count)
		{
			ID3D11ShaderResourceView* srvs[kSrvSlots]{};
			for (const auto& [slot, srv] : a_srvs) srvs[slot] = srv;
			ID3D11UnorderedAccessView* uavs[kUavSlots]{};
			for (const auto& [slot, uav] : a_uavs) uavs[slot] = uav;
			a_context->CSSetShaderResources(0, kSrvSlots, srvs);
			a_context->CSSetUnorderedAccessViews(0, kUavSlots, uavs, nullptr);
			a_context->CSSetShader(a_shader, nullptr, 0);
			a_context->Dispatch((a_count + kThreads - 1) / kThreads, 1, 1);
			ID3D11ShaderResourceView* noSrvs[kSrvSlots]{};
			a_context->CSSetShaderResources(0, kSrvSlots, noSrvs);
			ID3D11UnorderedAccessView* noUavs[kUavSlots]{};
			a_context->CSSetUnorderedAccessViews(0, kUavSlots, noUavs, nullptr);
		}
	}

	std::unique_ptr<GpuSmoother> GpuSmoother::Create(std::shared_ptr<const SmoothMesh> a_mesh, const Layout& a_layout, const std::vector<std::byte>& a_base,
		const std::vector<SkinWeights>& a_skin, ID3D11Buffer* a_target, uint32_t a_targetOffset, const char* a_name)
	{
		if (!a_mesh || !a_target)
			return nullptr;
		D3D11_BUFFER_DESC desc{};
		a_target->GetDesc(&desc);
		const uint32_t size = a_layout.stride * a_mesh->vertexCount;
		if (desc.Usage != D3D11_USAGE_DEFAULT || size_t(a_targetOffset) + size > desc.ByteWidth || a_layout.stride % 4 != 0 || a_layout.normalOffset % 4 != 0 ||
			a_layout.tangentOffset % 4 != 0 || a_base.size() < size || a_skin.size() != a_mesh->vertexCount) {
			logger::warn("Morphs: '{}' can't be smoothed on the GPU (vertex buffer usage {}, {} bytes; stride {}); it keeps plain morphs.", a_name, int(desc.Usage), desc.ByteWidth,
				a_layout.stride);
			return nullptr;
		}
		std::unique_ptr<GpuSmoother> result{ new GpuSmoother() };
		result->mesh = std::move(a_mesh);
		result->target = a_target;
		result->targetOffset = a_targetOffset;
		if (!result->Init(a_layout, a_base, a_skin, a_name))
			return nullptr;
		return result;
	}

	bool GpuSmoother::Init(const Layout& a_layout, const std::vector<std::byte>& a_base, const std::vector<SkinWeights>& a_skin, const char* a_name)
	{
		auto device = GetDevice(target);
		if (!device || !GetShaders(device))
			return false;
		layout = a_layout;
		vertexCount = mesh->vertexCount;
		const auto& m = *mesh;
		const std::vector<uint32_t> pinnedWords(m.pinned.begin(), m.pinned.end());
		const std::vector<uint32_t> frameShadedWords(m.frameShaded.begin(), m.frameShaded.end());
		std::vector<uint32_t> skinWords(size_t(vertexCount) * 4);
		for (uint32_t v = 0; v < vertexCount; v++) {
			const auto& s = a_skin[v];
			skinWords[v * 4] = uint32_t(s.bones[0]) | (uint32_t(s.bones[1]) << 8) | (uint32_t(s.bones[2]) << 16) | (uint32_t(s.bones[3]) << 24);
			for (int i = 0; i < 3; i++) skinWords[v * 4 + 1 + i] = std::bit_cast<uint32_t>(s.weights[i]);
		}
		const std::vector<float> zeros(size_t(m.groupCount) * 4, 0.0f);
		const size_t bytes = size_t(layout.stride) * vertexCount;
		const bool ok =
			MakeStructured(device, 4, m.weightStart.size(), m.weightStart.data(), false, weightStart) &&
			MakeStructured(device, 4, m.weightBone.size(), m.weightBone.data(), false, weightBone) &&
			MakeStructured(device, 16, m.weights.size(), m.weights.data(), false, weights) &&
			MakeStructured(device, 4, m.neighbourStart.size(), m.neighbourStart.data(), false, neighbourStart) &&
			MakeStructured(device, 4, m.neighbours.size(), m.neighbours.data(), false, neighbours) &&
			MakeStructured(device, 4, m.cornerStart.size(), m.cornerStart.data(), false, cornerStart) &&
			MakeStructured(device, 8, m.corners.size(), m.corners.data(), false, corners) &&
			MakeStructured(device, 4, pinnedWords.size(), pinnedWords.data(), false, pinned) &&
			MakeStructured(device, 4, frameShadedWords.size(), frameShadedWords.data(), false, frameShaded) &&
			MakeStructured(device, 16, m.groupCount, ToFloat4(m.restNormal).data(), false, restNormal) &&
			MakeStructured(device, 16, m.groupCount, ToFloat4(m.smoothedRest).data(), false, smoothedRest) &&
			MakeStructured(device, 4, m.groupVertex.size(), m.groupVertex.data(), false, groupVertex) &&
			MakeStructured(device, 8, m.wings.size(), m.wings.data(), false, wings) &&
			MakeStructured(device, 4, m.restEdgeAngle.size(), m.restEdgeAngle.data(), false, restEdgeAngle) &&
			MakeStructured(device, 4, m.restArea.size(), m.restArea.data(), false, restArea) &&
			MakeStructured(device, 4, m.vertexGroup.size(), m.vertexGroup.data(), false, vertexGroup) &&
			MakeStructured(device, 16, vertexCount, skinWords.data(), false, vertexSkin) &&
			MakeRaw(device, bytes, a_base.data(), false, base) &&
			MakeStructured(device, 16, size_t(m.boneCount) * 3, nullptr, false, bones) &&
			MakeStructured(device, 16, m.groupCount, ToFloat4(m.rest).data(), false, rest) &&
			MakeStructured(device, 16, m.groupCount, zeros.data(), false, region) &&
			MakeStructured(device, 16, m.groupCount, nullptr, true, smoothA) &&
			MakeStructured(device, 16, m.groupCount, nullptr, true, smoothB) &&
			MakeStructured(device, 16, m.groupCount, nullptr, true, result) &&
			MakeStructured(device, 16, size_t(m.groupCount) * 3, nullptr, true, turns) &&
			MakeStructured(device, 16, m.groupCount, nullptr, true, maskA) &&
			MakeStructured(device, 16, m.groupCount, nullptr, true, maskB) &&
			MakeStructured(device, 16, m.groupCount, nullptr, true, smoothedRestPassed) &&
			MakeStructured(device, 16, m.groupCount, nullptr, true, scratch) &&
			MakeStructured(device, 16, m.groupCount, zeros.data(), false, volumeMask) &&
			MakeStructured(device, 16, size_t((m.groupCount + kThreads - 1) / kThreads) * 2, nullptr, true, volumePartials) &&
			MakeStructured(device, 16, 2, nullptr, true, volumeStep) &&
			MakeRaw(device, bytes, a_base.data(), true, output);
		if (!ok) {
			logger::warn("Morphs: couldn't create the GPU buffers to smooth '{}'; it keeps plain morphs.", a_name);
			return false;
		}
		const Params params{ m.groupCount, vertexCount, layout.stride, layout.normalOffset, layout.tangentOffset, layout.fullPrecision ? 1u : 0u, m.boneCount, 0 };
		D3D11_BUFFER_DESC cdesc{};
		cdesc.ByteWidth = sizeof(Params);
		cdesc.Usage = D3D11_USAGE_DEFAULT;
		cdesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		D3D11_SUBRESOURCE_DATA cinit{ &params };
		const FrameParams frame{};
		D3D11_SUBRESOURCE_DATA finit{ &frame };
		D3D11_BUFFER_DESC fdesc = cdesc;
		fdesc.ByteWidth = sizeof(FrameParams);
		if (FAILED(device->CreateBuffer(&cdesc, &cinit, &constants)) || FAILED(device->CreateBuffer(&fdesc, &finit, &frameConstants))) {
			logger::warn("Morphs: couldn't create the GPU buffers to smooth '{}'; it keeps plain morphs.", a_name);
			return false;
		}
		skin = a_skin;
		regionWeight.assign(m.groupCount, 0.0f);
		shadeWeight.assign(m.groupCount, 0.0f);
		logger::info("Morphs: '{}' is smoothed on the GPU ({} vertices, {} weld groups).", a_name, vertexCount, m.groupCount);
		return true;
	}

	GpuSmoother::~GpuSmoother()
	{
		for (auto view : { &weightStart, &weightBone, &weights, &neighbourStart, &neighbours, &cornerStart, &corners, &pinned, &frameShaded, &restNormal, &smoothedRest, &groupVertex,
				 &wings, &restEdgeAngle, &restArea, &vertexGroup, &vertexSkin, &base, &bones, &rest, &region, &smoothA, &smoothB, &result, &turns, &maskA, &maskB,
				 &smoothedRestPassed, &scratch, &volumeMask, &volumePartials, &volumeStep, &output })
			view->Release();
		if (constants)
			constants->Release();
		if (frameConstants)
			frameConstants->Release();
	}

	void GpuSmoother::SetRestPositions(const std::vector<Vec3>& a_positions)
	{
		const auto& m = *mesh;
		if (a_positions.size() != m.vertexCount)
			return;
		pendingRest.resize(size_t(m.groupCount) * 4);
		// Groups the morphs moved, and their neighbours: their frames turn with the result wholly, as the CPU path's do.
		std::vector<uint8_t> moved(m.groupCount, 0);
		for (uint32_t g = 0; g < m.groupCount; g++) {
			const auto& p = a_positions[m.groupVertex[g]];
			pendingRest[g * 4] = p.x;
			pendingRest[g * 4 + 1] = p.y;
			pendingRest[g * 4 + 2] = p.z;
			pendingRest[g * 4 + 3] = 1.0f;
			const auto d = p - m.rest[g];
			moved[g] = Dot(d, d) > kMorphedDistance * kMorphedDistance;
		}
		for (uint32_t g = 0; g < m.groupCount; g++) {
			bool nextToMoved = moved[g];
			for (uint32_t k = m.neighbourStart[g]; k < m.neighbourStart[g + 1] && !nextToMoved; k++) nextToMoved = moved[m.neighbours[k]];
			shadeWeight[g] = nextToMoved ? 1.0f : 0.0f;
		}
		regionDirty = true;
	}

	float GpuSmoother::SetRegion(const std::vector<float>& a_boneStrengths)
	{
		const auto& m = *mesh;
		float most = 0.0f;
		for (uint32_t g = 0; g < m.groupCount; g++) {
			float w = 0.0f;
			for (uint32_t i = m.weightStart[g]; i < m.weightStart[g + 1]; i++) {
				const uint32_t bone = m.weightBone[i];
				if (bone < a_boneStrengths.size())
					w += m.weights[i][3] * a_boneStrengths[bone];
			}
			// The smoothing spreads some of every vertex onto bones around it, so even the middle of a region isn't wholly
			// its bones' (the hip crease is about 75% the hip region's). Groups mostly the region's are wholly in it.
			const float t = std::clamp((w - kRegionFrom) / (kRegionFull - kRegionFrom), 0.0f, 1.0f);
			regionWeight[g] = t * t * (3.0f - 2.0f * t);
			most = (std::max)(most, regionWeight[g]);
		}
		regionDirty = true;
		return most;
	}

	bool GpuSmoother::SetVolumeBones(const std::vector<VolumeBone>& a_bones)
	{
		const auto& m = *mesh;
		const auto role = [&](uint32_t a_bone) { return a_bone < a_bones.size() ? a_bones[a_bone] : VolumeBone::kNone; };
		const auto smoothstep = [](float a_x) {
			const float t = std::clamp(a_x, 0.0f, 1.0f);
			return t * t * (3.0f - 2.0f * t);
		};
		// Each group's skin: how much of it the buttock bones carry, and the pelvis bones.
		std::vector<float> buttock(m.groupCount, 0.0f);
		Vec3 pointSum{};
		uint32_t pointCount = 0;
		for (uint32_t g = 0; g < m.groupCount; g++) {
			const auto& s = skin[m.groupVertex[g]];
			float pelvis = 0.0f;
			for (int i = 0; i < 4; i++) {
				const auto r = role(s.bones[i]);
				buttock[g] += (r == VolumeBone::kButtock || r == VolumeBone::kPelvisRear) ? s.weights[i] : 0.0f;
				pelvis += (r == VolumeBone::kPelvis || r == VolumeBone::kPelvisRear) ? s.weights[i] : 0.0f;
			}
			if (pelvis > kPelvisShare) {
				pointSum = pointSum + m.rest[g];
				pointCount++;
			}
		}
		pelvisBone = UINT32_MAX;
		for (uint32_t b = 0; b < a_bones.size(); b++) {
			if (a_bones[b] == VolumeBone::kPelvis || (a_bones[b] == VolumeBone::kPelvisRear && pelvisBone == UINT32_MAX))
				pelvisBone = b;
		}
		// The push mask, softened over a few rings, then split by side.
		std::vector<float> push(m.groupCount), next(m.groupCount);
		for (uint32_t g = 0; g < m.groupCount; g++) push[g] = smoothstep((buttock[g] - kButtockMaskFrom) / kButtockMaskWidth);
		for (uint32_t r = 0; r < kButtockMaskRings; r++) {
			for (uint32_t g = 0; g < m.groupCount; g++) {
				float sum = push[g];
				for (uint32_t k = m.neighbourStart[g]; k < m.neighbourStart[g + 1]; k++) sum += push[m.neighbours[k]];
				next[g] = sum / float(m.neighbourStart[g + 1] - m.neighbourStart[g] + 1);
			}
			push.swap(next);
		}
		hasButtocks = false;
		pendingVolumeMask.assign(size_t(m.groupCount) * 4, 0.0f);
		if (pointCount > 0 && pelvisBone != UINT32_MAX) {
			volumePoint = pointSum * (1.0f / float(pointCount));
			for (uint32_t g = 0; g < m.groupCount; g++) {
				const float x = m.rest[g].x - volumePoint.x;
				const float right = smoothstep(0.5f + x / (2.0f * kSideBlend));
				pendingVolumeMask[g * 4] = push[g] * (1.0f - right);
				pendingVolumeMask[g * 4 + 1] = push[g] * right;
				if (buttock[g] > kButtockShare) {
					pendingVolumeMask[g * 4 + 2] = x > 0.0f ? 2.0f : 1.0f;
					hasButtocks = true;
				}
			}
		}
		return hasButtocks;
	}

	void GpuSmoother::SmoothRest(ID3D11DeviceContext* a_context, uint32_t a_passes)
	{
		// The rest mesh gets the runtime passes too, so the detail is measured against the same smoothing.
		const auto shaders = GetShaders(nullptr);
		const auto& m = *mesh;
		a_context->CopyResource(smoothedRestPassed.buffer, smoothedRest.buffer);
		View* from = &smoothedRestPassed;
		View* to = &scratch;
		for (uint32_t i = 0; i < a_passes; i++) {
			Run(a_context, shaders->pass, { { 4, from->srv }, { 5, neighbourStart.srv }, { 6, neighbours.srv }, { 7, pinned.srv } }, { { 0, to->uav } }, m.groupCount);
			std::swap(from, to);
		}
		if (from != &smoothedRestPassed)
			a_context->CopyResource(smoothedRestPassed.buffer, from->buffer);
		restPassesDone = a_passes;
	}

	void GpuSmoother::Update(ID3D11DeviceContext* a_context, const std::vector<Affine>& a_bones, const Options& a_options)
	{
		const auto shaders = GetShaders(nullptr);
		if (!shaders)
			return;
		const auto& m = *mesh;
		const uint32_t runtimePasses = (std::min)(a_options.runtimePasses, SmoothLibrary::kMaxRuntimePasses);
		const uint32_t autoPasses = (std::min)(a_options.autoPasses, SmoothLibrary::kMaxAutoSmoothPasses);
		const bool stretch = a_options.stretchDetail;
		const bool restore = a_options.volumePreservation && hasButtocks && a_options.volumeRestore > 0.0f && pelvisBone < a_bones.size();

		std::vector<float> boneRows(size_t(m.boneCount) * 12, 0.0f);
		for (uint32_t b = 0; b < m.boneCount && b < a_bones.size(); b++) {
			const auto& a = a_bones[b];
			const float t[3]{ a.translation.x, a.translation.y, a.translation.z };
			for (int r = 0; r < 3; r++) {
				const auto& row = a.linear.rows[r];
				float* out = &boneRows[(size_t(b) * 3 + r) * 4];
				out[0] = row.x;
				out[1] = row.y;
				out[2] = row.z;
				out[3] = t[r];
			}
		}
		a_context->UpdateSubresource(bones.buffer, 0, nullptr, boneRows.data(), 0, 0);
		if (!pendingRest.empty()) {
			a_context->UpdateSubresource(rest.buffer, 0, nullptr, pendingRest.data(), 0, 0);
			pendingRest.clear();
		}
		if (regionDirty) {
			std::vector<float> data(size_t(m.groupCount) * 4, 0.0f);
			for (uint32_t g = 0; g < m.groupCount; g++) {
				data[g * 4] = regionWeight[g];
				data[g * 4 + 1] = shadeWeight[g];
			}
			a_context->UpdateSubresource(region.buffer, 0, nullptr, data.data(), 0, 0);
			regionDirty = false;
		}
		if (!pendingVolumeMask.empty()) {
			a_context->UpdateSubresource(volumeMask.buffer, 0, nullptr, pendingVolumeMask.data(), 0, 0);
			pendingVolumeMask.clear();
		}
		const uint32_t partialCount = (m.groupCount + kThreads - 1) / kThreads;
		{
			const Vec3 now = restore ? a_bones[pelvisBone] * volumePoint : volumePoint;
			const FrameParams frame{ { volumePoint.x, volumePoint.y, volumePoint.z, 1.0f }, { now.x, now.y, now.z, 1.0f }, a_options.volumeRestore, partialCount,
				stretch ? 1u : 0u, 0 };
			a_context->UpdateSubresource(frameConstants, 0, nullptr, &frame, 0, 0);
		}

		{
			SavedComputeState saved{ a_context };
			ID3D11Buffer* constantBuffers[kConstantSlots]{ constants, frameConstants };
			a_context->CSSetConstantBuffers(0, kConstantSlots, constantBuffers);
			if (restPassesDone != runtimePasses)
				SmoothRest(a_context, runtimePasses);

			Run(a_context, shaders->smooth, { { 0, weightStart.srv }, { 1, weightBone.srv }, { 2, weights.srv }, { 3, bones.srv }, { 12, rest.srv } }, { { 0, smoothA.uav } },
				m.groupCount);
			View* from = &smoothA;
			View* to = &smoothB;
			for (uint32_t i = 0; i < runtimePasses; i++) {
				Run(a_context, shaders->pass, { { 4, from->srv }, { 5, neighbourStart.srv }, { 6, neighbours.srv }, { 7, pinned.srv } }, { { 0, to->uav } }, m.groupCount);
				std::swap(from, to);
			}
			// Stretch detail: the smoothed surface's area around each group, now and at rest.
			View* areas = &maskA;
			if (stretch) {
				Run(a_context, shaders->stretchArea, { { 8, from->srv }, { 9, smoothedRestPassed.srv }, { 10, cornerStart.srv }, { 11, corners.srv } }, { { 0, maskA.uav } },
					m.groupCount);
				View* spare = &maskB;
				for (uint32_t i = 0; i < kStretchRings; i++) {
					Run(a_context, shaders->average, { { 5, neighbourStart.srv }, { 6, neighbours.srv }, { 25, areas->srv } }, { { 0, spare->uav } }, m.groupCount);
					std::swap(areas, spare);
				}
			}
			Run(a_context, shaders->detail,
				{ { 3, bones.srv }, { 8, from->srv }, { 9, smoothedRestPassed.srv }, { 10, cornerStart.srv }, { 11, corners.srv }, { 12, rest.srv }, { 15, vertexSkin.srv },
					{ 20, groupVertex.srv }, { 21, region.srv }, { 25, areas->srv } },
				{ { 0, result.uav }, { 2, turns.uav } }, m.groupCount);

			if (autoPasses > 0) {
				Run(a_context, shaders->mask,
					{ { 4, result.srv }, { 5, neighbourStart.srv }, { 6, neighbours.srv }, { 10, cornerStart.srv }, { 11, corners.srv }, { 21, region.srv }, { 22, wings.srv },
						{ 23, restEdgeAngle.srv }, { 24, restArea.srv } },
					{ { 0, maskA.uav } }, m.groupCount);
				View* mask = &maskA;
				View* spare = &maskB;
				for (uint32_t i = 0; i < kMaskRings; i++) {
					Run(a_context, shaders->dilate, { { 5, neighbourStart.srv }, { 6, neighbours.srv }, { 25, mask->srv } }, { { 0, spare->uav } }, m.groupCount);
					std::swap(mask, spare);
				}
				View* positions = &result;
				for (uint32_t i = 0; i < autoPasses; i++) {
					View* next = positions == &smoothA ? &smoothB : &smoothA;
					Run(a_context, shaders->relax, { { 4, positions->srv }, { 5, neighbourStart.srv }, { 6, neighbours.srv }, { 7, pinned.srv }, { 25, mask->srv } },
						{ { 0, next->uav } }, m.groupCount);
					positions = next;
				}
				if (positions != &result)
					a_context->CopyResource(result.buffer, positions->buffer);
			}

			if (restore) {
				// The push weights, kept off squashed skin and softened; then the buttocks' volume restored in steps.
				Run(a_context, shaders->volumeWeight,
					{ { 4, result.srv }, { 10, cornerStart.srv }, { 11, corners.srv }, { 21, region.srv }, { 24, restArea.srv }, { 26, volumeMask.srv } }, { { 0, maskA.uav } },
					m.groupCount);
				View* weightsNow = &maskA;
				View* spare = &maskB;
				for (uint32_t i = 0; i < kVolumeMaskRings; i++) {
					Run(a_context, shaders->average, { { 5, neighbourStart.srv }, { 6, neighbours.srv }, { 25, weightsNow->srv } }, { { 0, spare->uav } }, m.groupCount);
					std::swap(weightsNow, spare);
				}
				const UINT zero[4]{};
				a_context->ClearUnorderedAccessViewUint(volumeStep.uav, zero);
				View* positions = &result;
				for (uint32_t i = 0; i < kVolumeSteps; i++) {
					Run(a_context, shaders->volumePartial,
						{ { 4, positions->srv }, { 10, cornerStart.srv }, { 11, corners.srv }, { 12, rest.srv }, { 25, weightsNow->srv }, { 26, volumeMask.srv } },
						{ { 0, volumePartials.uav } }, m.groupCount);
					Run(a_context, shaders->volumeSum, { { 27, volumePartials.srv } }, { { 0, volumeStep.uav } }, 1);
					View* next = positions == &smoothA ? &smoothB : &smoothA;
					Run(a_context, shaders->volumePush,
						{ { 4, positions->srv }, { 7, pinned.srv }, { 10, cornerStart.srv }, { 11, corners.srv }, { 25, weightsNow->srv }, { 28, volumeStep.srv } }, { { 0, next->uav } },
						m.groupCount);
					positions = next;
				}
				if (positions != &result)
					a_context->CopyResource(result.buffer, positions->buffer);
			}

			Run(a_context, shaders->write,
				{ { 3, bones.srv }, { 10, cornerStart.srv }, { 11, corners.srv }, { 13, result.srv }, { 14, vertexGroup.srv }, { 15, vertexSkin.srv }, { 16, base.srv },
					{ 17, restNormal.srv }, { 18, frameShaded.srv }, { 19, turns.srv }, { 21, region.srv } },
				{ { 1, output.uav } }, vertexCount);
		}

		const D3D11_BOX box{ 0, 0, 0, layout.stride * vertexCount, 1, 1 };
		a_context->CopySubresourceRegion(target, 0, targetOffset, 0, 0, output.buffer, 0, &box);
	}
}
#endif
