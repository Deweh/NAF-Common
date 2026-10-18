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

		// The rotation taking unit a_from toward unit a_to, by a_amount (0 to 1) of the angle between them.
		Mat3 RotationToward(const Vec3& a_from, const Vec3& a_to, float a_amount)
		{
			const auto axis = Cross(a_from, a_to);
			const float s = std::sqrt(Dot(axis, axis));
			const float c = Dot(a_from, a_to);
			if (s < 1e-6f || a_amount <= 0.0f)
				return {};
			const float angle = std::atan2(s, c) * (std::min)(a_amount, 1.0f);
			const auto k = axis * (1.0f / s);
			const float ca = std::cos(angle), sa = std::sin(angle), t = 1.0f - ca;
			return { { { ca + k.x * k.x * t, -k.z * sa + k.x * k.y * t, k.y * sa + k.x * k.z * t },
				{ k.z * sa + k.y * k.x * t, ca + k.y * k.y * t, -k.x * sa + k.y * k.z * t },
				{ -k.y * sa + k.z * k.x * t, k.x * sa + k.z * k.y * t, ca + k.z * k.z * t } } };
		}

		// A vertex counts as skinned by one bone at this weight or more.
		constexpr float kRigidWeight = 0.99f;
		constexpr uint16_t kBlended = 0xFFFF;
	}

	Mat3 Mat3::operator*(const Mat3& a) const
	{
		Mat3 r;
		for (int i = 0; i < 3; i++) {
			const auto& row = rows[i];
			r.rows[i] = a.rows[0] * row.x + a.rows[1] * row.y + a.rows[2] * row.z;
		}
		return r;
	}

	Mat3 Mat3::Inverse() const
	{
		const auto& [a, b, c] = rows;
		const auto c0 = Cross(b, c), c1 = Cross(c, a), c2 = Cross(a, b);
		const float det = Dot(a, c0);
		if (std::abs(det) < 1e-12f)
			return {};
		const float inv = 1.0f / det;
		// The inverse's columns are the cross products, over the determinant.
		return { { { c0.x * inv, c1.x * inv, c2.x * inv }, { c0.y * inv, c1.y * inv, c2.y * inv }, { c0.z * inv, c1.z * inv, c2.z * inv } } };
	}

	Affine Affine::Inverse() const
	{
		const auto inv = linear.Inverse();
		return { inv, inv * (translation * -1.0f) };
	}

	Vec3 Normalized(const Vec3& a)
	{
		const float length = std::sqrt(Dot(a, a));
		return length > 1e-12f ? a * (1.0f / length) : a;
	}

	void MeshDeformer::Init(std::vector<Vec3> a_restPositions, const std::vector<uint32_t>& a_triangles, std::vector<SkinWeights> a_skin)
	{
		rest = std::move(a_restPositions);
		current = rest;
		const uint32_t vertexCount = static_cast<uint32_t>(rest.size());
		accum.assign(vertexCount, {});
		strength.assign(vertexCount, 0.0f);
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
			// Positions widened from half floats have their low 13 bits zero, and the map picks buckets by the low bits,
			// so the high bits are mixed down (MurmurHash3's finalizer).
			size_t operator()(const Key& k) const
			{
				uint64_t h = (uint64_t(k.x) * 0x9E3779B97F4A7C15ull) ^ (uint64_t(k.y) * 0xC2B2AE3D27D4EB4Full) ^ k.z;
				h ^= h >> 33;
				h *= 0xFF51AFD7ED558CCDull;
				h ^= h >> 33;
				h *= 0xC4CEB93FE1A85EC5ull;
				h ^= h >> 33;
				return h;
			}
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

		// Posed shading: find the groups whose surroundings (every vertex of their triangles) aren't all skinned by one
		// bone, and the bones skinning those surroundings.
		if (a_skin.size() != vertexCount)
			return;
		skin = std::move(a_skin);
		std::vector<uint16_t> rigidBone(vertexCount, kBlended);
		for (uint32_t v = 0; v < vertexCount; v++) {
			for (int i = 0; i < 4; i++) {
				if (skin[v].weights[i] >= kRigidWeight)
					rigidBone[v] = skin[v].bones[i];
			}
		}
		groupBlended.assign(groupCount, 0);
		std::array<bool, 256> used{};
		for (uint32_t g = 0; g < groupCount; g++) {
			uint32_t bone = UINT32_MAX;
			bool blended = false;
			for (uint32_t m = groupStart[g]; m < groupStart[g + 1] && !blended; m++) {
				const uint32_t member = groupMembers[m];
				for (uint32_t t = vertexTriStart[member]; t < vertexTriStart[member + 1] && !blended; t++) {
					for (int k = 0; k < 3; k++) {
						const uint16_t b = rigidBone[triangles[vertexTris[t] * 3 + k]];
						if (b == kBlended || (bone != UINT32_MAX && b != bone))
							blended = true;
						bone = b;
					}
				}
			}
			if (!blended)
				continue;
			groupBlended[g] = 1;
			for (uint32_t m = groupStart[g]; m < groupStart[g + 1]; m++) {
				const uint32_t member = groupMembers[m];
				for (uint32_t t = vertexTriStart[member]; t < vertexTriStart[member + 1]; t++) {
					for (int k = 0; k < 3; k++) {
						const auto& w = skin[triangles[vertexTris[t] * 3 + k]];
						for (int i = 0; i < 4; i++) {
							if (w.weights[i] > 0.0f)
								used[w.bones[i]] = true;
						}
					}
				}
			}
		}
		for (uint32_t b = 0; b < used.size(); b++) {
			if (used[b])
				poseBones.push_back(static_cast<uint8_t>(b));
		}
		posedTurn.resize(vertexCount);
		usesPosedTurn.assign(vertexCount, 0);
		skinned.resize(vertexCount);
		posedCurrent.resize(vertexCount);
		skinMark.assign(vertexCount, 0);
	}

	void MeshDeformer::SetPose(const std::vector<Affine>& a_bones)
	{
		pose = a_bones;
	}

	bool MeshDeformer::CanPose() const
	{
		return hasTopology && !poseBones.empty() && pose.size() > poseBones.back();
	}

	void MeshDeformer::Skin(uint32_t a_vertex, uint32_t a_stamp)
	{
		if (skinMark[a_vertex] == a_stamp)
			return;
		skinMark[a_vertex] = a_stamp;
		Affine a{ Mat3{ { {}, {}, {} } }, {} };
		const auto& w = skin[a_vertex];
		for (int i = 0; i < 4; i++) {
			if (w.weights[i] <= 0.0f)
				continue;
			const auto& bone = pose[w.bones[i]];
			a.linear += bone.linear * w.weights[i];
			a.translation += bone.translation * w.weights[i];
		}
		skinned[a_vertex] = a;
		posedCurrent[a_vertex] = a * current[a_vertex];
	}

	void MeshDeformer::ShadePosed(uint32_t a_group, float a_strength, uint32_t a_stamp)
	{
		for (uint32_t m = groupStart[a_group]; m < groupStart[a_group + 1]; m++) {
			const uint32_t member = groupMembers[m];
			for (uint32_t t = vertexTriStart[member]; t < vertexTriStart[member + 1]; t++) {
				for (int k = 0; k < 3; k++) Skin(triangles[vertexTris[t] * 3 + k], a_stamp);
			}
		}
		// Plain skinning doesn't turn a crease's frames the way it bends its surface (that's why it folds), so the turn
		// isn't measured from the posed rest surface: it goes from where the game would skin the rest normal to the
		// posed, morphed surface, by as much as the morphs here are on. At full strength the result is the sculpt's own
		// surface; at 0, plain skinning. It's carried back through each vertex's skinning, since the game skins the
		// written frame by the same transform.
		const auto& linear = skinned[groupMembers[groupStart[a_group]]].linear;
		const auto skinnedNormal = Normalized(linear * restGroupNormal[a_group]);
		const auto turn = RotationToward(skinnedNormal, GroupNormal(a_group, posedCurrent), a_strength);
		for (uint32_t m = groupStart[a_group]; m < groupStart[a_group + 1]; m++) {
			const uint32_t member = groupMembers[m];
			const auto& linear = skinned[member].linear;
			posedTurn[member] = linear.Inverse() * turn * linear;
			usesPosedTurn[member] = 1;
		}
	}

	void MeshDeformer::ClearPosed()
	{
		for (uint32_t g : posedGroups) {
			for (uint32_t m = groupStart[g]; m < groupStart[g + 1]; m++) usesPosedTurn[groupMembers[m]] = 0;
		}
		posedGroups.clear();
		posedStrength.clear();
	}

	void MeshDeformer::Reshade()
	{
		dirty.clear();
		if (posedGroups.empty() || !CanPose())
			return;
		const uint32_t stamp = NextStamp(skinStamp, skinMark);
		for (size_t i = 0; i < posedGroups.size(); i++) {
			const uint32_t g = posedGroups[i];
			ShadePosed(g, posedStrength[i], stamp);
			for (uint32_t m = groupStart[g]; m < groupStart[g + 1]; m++) dirty.push_back(groupMembers[m]);
		}
		std::ranges::sort(dirty);
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
					strength[v] = 0.0f;
					nowMoved.push_back(v);
				}
				accum[v] += deltas->offsets[i] * value;
				strength[v] += std::abs(value);
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
			// Groups in blended areas are shaded on the posed mesh when there's a pose (see SetPose).
			ClearPosed();
			const bool canPose = CanPose();
			const uint32_t poseStamp = canPose ? NextStamp(skinStamp, skinMark) : 0;
			for (uint32_t g : ring) {
				if (canPose && groupBlended[g]) {
					// How much the morphs here are on: the most on any vertex of the group's triangles.
					float s = 0.0f;
					for (uint32_t m = groupStart[g]; m < groupStart[g + 1]; m++) {
						const uint32_t member = groupMembers[m];
						for (uint32_t t = vertexTriStart[member]; t < vertexTriStart[member + 1]; t++) {
							for (int k = 0; k < 3; k++) {
								const uint32_t v = triangles[vertexTris[t] * 3 + k];
								if (vertexMark[v] == stamp)
									s = (std::max)(s, strength[v]);
							}
						}
					}
					ShadePosed(g, s, poseStamp);
					posedGroups.push_back(g);
					posedStrength.push_back(s);
				} else {
					currentGroupNormal[g] = GroupNormal(g, current);
				}
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
		if (!usesPosedTurn.empty() && usesPosedTurn[a_vertex])
			return Normalized(posedTurn[a_vertex] * a_restDirection);
		const uint32_t g = group[a_vertex];
		return Normalized(RotateBetween(restGroupNormal[g], currentGroupNormal[g], a_restDirection));
	}
}
