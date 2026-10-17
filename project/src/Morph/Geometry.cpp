#include "Geometry.h"

namespace Morph
{
	namespace
	{
		// Restarts a stamp-marked scratch array when its stamp wraps.
		uint32_t NextStamp(uint32_t& a_stamp, std::vector<uint32_t>& a_marks)
		{
			if (++a_stamp == 0) {
				std::ranges::fill(a_marks, 0u);
				a_stamp = 1;
			}
			return a_stamp;
		}

		// a_v turned by the shortest rotation taking unit a_from to unit a_to.
		Vec3 RotateBetween(const Vec3& a_from, const Vec3& a_to, const Vec3& a_v)
		{
			const float c = Dot(a_from, a_to);
			if (c > 0.999999f || c < -0.9999f)
				return a_v;
			const auto axis = Cross(a_from, a_to);
			return a_v * c + Cross(axis, a_v) + axis * (Dot(axis, a_v) / (1.0f + c));
		}
	}

	Vec3 Normalized(const Vec3& a)
	{
		const float length = std::sqrt(Dot(a, a));
		return length > 1e-12f ? a * (1.0f / length) : a;
	}

	void MeshDeformer::Init(std::vector<Vec3> a_restPositions, const std::vector<uint32_t>& a_triangles)
	{
		rest = std::move(a_restPositions);
		current = rest;
		const uint32_t vertexCount = static_cast<uint32_t>(rest.size());
		accum.assign(vertexCount, {});
		vertexMark.assign(vertexCount, 0);
		moved.clear();
		shaded.clear();
		dirty.clear();
		hasTopology = false;

		if (a_triangles.empty() || a_triangles.size() % 3 != 0 || !std::ranges::all_of(a_triangles, [&](uint32_t i) { return i < vertexCount; }))
			return;
		triangles = a_triangles;
		const uint32_t triCount = static_cast<uint32_t>(triangles.size() / 3);

		vertexTriStart.assign(vertexCount + 1, 0);
		for (auto i : triangles) vertexTriStart[i + 1]++;
		for (uint32_t v = 0; v < vertexCount; v++) vertexTriStart[v + 1] += vertexTriStart[v];
		vertexTris.resize(triangles.size());
		std::vector<uint32_t> fill(vertexTriStart.begin(), vertexTriStart.end() - 1);
		for (uint32_t t = 0; t < triCount; t++) {
			for (int k = 0; k < 3; k++) vertexTris[fill[triangles[t * 3 + k]]++] = t;
		}

		// Weld vertices at the same position: UV seams split the mesh, but the surface is continuous there.
		struct Key
		{
			uint32_t x, y, z;
			bool operator==(const Key&) const = default;
		};
		struct KeyHash
		{
			size_t operator()(const Key& k) const { return (size_t(k.x) * 73856093) ^ (size_t(k.y) * 19349663) ^ (size_t(k.z) * 83492791); }
		};
		std::unordered_map<Key, uint32_t, KeyHash> groups;
		group.resize(vertexCount);
		for (uint32_t v = 0; v < vertexCount; v++) {
			const auto& p = rest[v];
			const Key key{ std::bit_cast<uint32_t>(p.x), std::bit_cast<uint32_t>(p.y), std::bit_cast<uint32_t>(p.z) };
			group[v] = groups.try_emplace(key, static_cast<uint32_t>(groups.size())).first->second;
		}
		const uint32_t groupCount = static_cast<uint32_t>(groups.size());
		groupStart.assign(groupCount + 1, 0);
		for (auto g : group) groupStart[g + 1]++;
		for (uint32_t g = 0; g < groupCount; g++) groupStart[g + 1] += groupStart[g];
		groupMembers.resize(vertexCount);
		fill.assign(groupStart.begin(), groupStart.end() - 1);
		for (uint32_t v = 0; v < vertexCount; v++) groupMembers[fill[group[v]]++] = v;

		restGroupNormal.assign(groupCount, {});
		for (uint32_t t = 0; t < triCount; t++) {
			const auto& a = rest[triangles[t * 3]];
			const auto n = Cross(rest[triangles[t * 3 + 1]] - a, rest[triangles[t * 3 + 2]] - a);
			for (int k = 0; k < 3; k++) restGroupNormal[group[triangles[t * 3 + k]]] += n;
		}
		for (auto& n : restGroupNormal) n = Normalized(n);
		currentGroupNormal = restGroupNormal;
		groupMark.assign(groupCount, 0);
		hasTopology = true;
	}

	Vec3 MeshDeformer::GroupNormal(uint32_t a_group, const std::vector<Vec3>& a_positions) const
	{
		Vec3 n{};
		for (uint32_t m = groupStart[a_group]; m < groupStart[a_group + 1]; m++) {
			const uint32_t member = groupMembers[m];
			for (uint32_t t = vertexTriStart[member]; t < vertexTriStart[member + 1]; t++) {
				const uint32_t tri = vertexTris[t];
				const auto& a = a_positions[triangles[tri * 3]];
				n += Cross(a_positions[triangles[tri * 3 + 1]] - a, a_positions[triangles[tri * 3 + 2]] - a);
			}
		}
		return Normalized(n);
	}

	void MeshDeformer::Apply(const ActiveMorphs& a_active)
	{
		// Everything that changed last time is rewritten from rest.
		dirty.assign(moved.begin(), moved.end());
		dirty.insert(dirty.end(), shaded.begin(), shaded.end());

		// Sum the offsets of the vertices the active morphs move.
		const uint32_t stamp = NextStamp(vertexStamp, vertexMark);
		std::vector<uint32_t> nowMoved;
		for (const auto& [deltas, value] : a_active) {
			for (size_t i = 0; i < deltas->indices.size(); i++) {
				const uint32_t v = deltas->indices[i];
				if (vertexMark[v] != stamp) {
					vertexMark[v] = stamp;
					accum[v] = {};
					nowMoved.push_back(v);
				}
				accum[v] += deltas->offsets[i] * value;
			}
		}
		for (uint32_t v : moved) {
			if (vertexMark[v] != stamp)
				current[v] = rest[v];
		}
		for (uint32_t v : nowMoved) current[v] = rest[v] + accum[v];
		moved = std::move(nowMoved);

		// The weld groups whose normal can change: every group sharing a triangle with a moved vertex.
		shaded.clear();
		if (hasTopology) {
			const uint32_t gstamp = NextStamp(groupStamp, groupMark);
			std::vector<uint32_t> ring;
			for (uint32_t v : moved) {
				const uint32_t g = group[v];
				for (uint32_t m = groupStart[g]; m < groupStart[g + 1]; m++) {
					const uint32_t member = groupMembers[m];
					for (uint32_t t = vertexTriStart[member]; t < vertexTriStart[member + 1]; t++) {
						const uint32_t tri = vertexTris[t];
						for (int k = 0; k < 3; k++) {
							const uint32_t g2 = group[triangles[tri * 3 + k]];
							if (groupMark[g2] != gstamp) {
								groupMark[g2] = gstamp;
								ring.push_back(g2);
							}
						}
					}
				}
			}
			for (uint32_t g : ring) {
				currentGroupNormal[g] = GroupNormal(g, current);
				for (uint32_t m = groupStart[g]; m < groupStart[g + 1]; m++) shaded.push_back(groupMembers[m]);
			}
			std::ranges::sort(shaded);
		}

		dirty.insert(dirty.end(), moved.begin(), moved.end());
		dirty.insert(dirty.end(), shaded.begin(), shaded.end());
		std::ranges::sort(dirty);
		dirty.erase(std::unique(dirty.begin(), dirty.end()), dirty.end());
	}

	Vec3 MeshDeformer::TurnFrame(uint32_t a_vertex, const Vec3& a_restDirection) const
	{
		if (!hasTopology)
			return a_restDirection;
		const uint32_t g = group[a_vertex];
		return Normalized(RotateBetween(restGroupNormal[g], currentGroupNormal[g], a_restDirection));
	}
}
