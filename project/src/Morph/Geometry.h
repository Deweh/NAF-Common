#pragma once

// Game-agnostic mesh deformation for morphs: summing morph offsets over a mesh's rest positions, and working out how
// much the surface turned at each vertex so a game's own normals, tangents and bitangents can be turned with it. A game
// frontend reads its vertex format into this and writes the result back (on Fallout 4, Morph/ShapeMorpher).
namespace Morph
{
	struct Vec3
	{
		float x = 0.0f, y = 0.0f, z = 0.0f;

		Vec3 operator+(const Vec3& a) const { return { x + a.x, y + a.y, z + a.z }; }
		Vec3 operator-(const Vec3& a) const { return { x - a.x, y - a.y, z - a.z }; }
		Vec3 operator*(float s) const { return { x * s, y * s, z * s }; }
		Vec3& operator+=(const Vec3& a)
		{
			x += a.x;
			y += a.y;
			z += a.z;
			return *this;
		}
	};

	[[nodiscard]] inline Vec3 Cross(const Vec3& a, const Vec3& b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
	[[nodiscard]] inline float Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
	[[nodiscard]] Vec3 Normalized(const Vec3& a);

	// One morph's offsets, from a TRI file.
	struct MorphDeltas
	{
		std::vector<uint32_t> indices;
		std::vector<Vec3> offsets;
		uint32_t maxIndex = 0;
	};

	class MeshDeformer
	{
	public:
		using ActiveMorphs = std::vector<std::pair<std::shared_ptr<const MorphDeltas>, float>>;

		// a_triangles is three vertex indices per triangle. Without triangles, positions still move but no frames turn.
		// Vertices at exactly the same rest position (copies along UV seams) are welded, so their frames turn together.
		void Init(std::vector<Vec3> a_restPositions, const std::vector<uint32_t>& a_triangles);

		// Sums a_active (offset times value) onto the rest positions. Afterwards:
		//   GetDirty():  every vertex whose output differs from the last call, sorted. Each should be rewritten from
		//                rest, then given its new position if moved and its turned frame if shaded.
		//   GetMoved():  vertices away from rest, with GetPosition().
		//   GetShaded(): vertices whose surface turned, sorted; TurnFrame() turns a rest normal, tangent or bitangent.
		void Apply(const ActiveMorphs& a_active);

		[[nodiscard]] const std::vector<uint32_t>& GetDirty() const { return dirty; }
		[[nodiscard]] const std::vector<uint32_t>& GetMoved() const { return moved; }
		[[nodiscard]] const std::vector<uint32_t>& GetShaded() const { return shaded; }
		[[nodiscard]] const Vec3& GetPosition(uint32_t a_vertex) const { return current[a_vertex]; }
		[[nodiscard]] Vec3 TurnFrame(uint32_t a_vertex, const Vec3& a_restDirection) const;
		[[nodiscard]] bool HasTopology() const { return hasTopology; }
		[[nodiscard]] uint32_t GetVertexCount() const { return static_cast<uint32_t>(rest.size()); }
		[[nodiscard]] uint32_t GetGroupCount() const { return hasTopology ? static_cast<uint32_t>(groupStart.size() - 1) : 0; }

	private:
		Vec3 GroupNormal(uint32_t a_group, const std::vector<Vec3>& a_positions) const;

		std::vector<Vec3> rest;
		std::vector<Vec3> current;

		// Topology: triangles, each vertex's triangles (CSR), weld groups and their members (CSR), rest group normals,
		// and each group's normal now.
		std::vector<uint32_t> triangles;
		std::vector<uint32_t> vertexTriStart, vertexTris;
		std::vector<uint32_t> group;
		std::vector<uint32_t> groupStart, groupMembers;
		std::vector<Vec3> restGroupNormal;
		std::vector<Vec3> currentGroupNormal;
		bool hasTopology = false;

		// Scratch.
		std::vector<Vec3> accum;
		std::vector<uint32_t> vertexMark, groupMark;
		uint32_t vertexStamp = 0, groupStamp = 0;

		std::vector<uint32_t> moved, shaded, dirty;
	};
}
