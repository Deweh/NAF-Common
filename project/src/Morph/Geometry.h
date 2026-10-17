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

	// A 3x3 matrix, by rows.
	struct Mat3
	{
		Vec3 rows[3]{ { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } };

		Vec3 operator*(const Vec3& a) const { return { Dot(rows[0], a), Dot(rows[1], a), Dot(rows[2], a) }; }
		Mat3 operator*(const Mat3& a) const;
		Mat3 operator*(float s) const { return { { rows[0] * s, rows[1] * s, rows[2] * s } }; }
		Mat3& operator+=(const Mat3& a)
		{
			for (int i = 0; i < 3; i++) rows[i] += a.rows[i];
			return *this;
		}
		// The identity if singular.
		[[nodiscard]] Mat3 Inverse() const;
	};

	// p' = linear * p + translation.
	struct Affine
	{
		Mat3 linear;
		Vec3 translation;

		Vec3 operator*(const Vec3& a) const { return linear * a + translation; }
		Affine operator*(const Affine& a) const { return { linear * a.linear, linear * a.translation + translation }; }
		[[nodiscard]] Affine Inverse() const;
	};

	// A vertex's skinning: up to four bones, by index into the pose given to MeshDeformer::SetPose, with weights summing
	// to 1 (unused slots have weight 0).
	struct SkinWeights
	{
		uint8_t bones[4]{};
		float weights[4]{};
	};

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
		// a_skin (one per vertex, or empty) is how the game skins the mesh; see SetPose.
		void Init(std::vector<Vec3> a_restPositions, const std::vector<uint32_t>& a_triangles, std::vector<SkinWeights> a_skin = {});

		// Sums a_active (offset times value) onto the rest positions. Afterwards:
		//   GetDirty():  every vertex whose output differs from the last call, sorted. Each should be rewritten from
		//                rest, then given its new position if moved and its turned frame if shaded.
		//   GetMoved():  vertices away from rest, with GetPosition().
		//   GetShaded(): vertices whose surface turned, sorted; TurnFrame() turns a rest normal, tangent or bitangent.
		void Apply(const ActiveMorphs& a_active);

		// Posed shading. A morph is stored on the unposed mesh, but a corrective is sculpted on the posed one, where
		// skinning has squashed (often folded) a crease; unfolding it there can fold the unposed mesh over itself, and a
		// surface turn measured unposed then points normals inward. Where a vertex's surroundings are skinned by one
		// bone the two agree, but where skinning blends bones they don't. So with a pose set, groups in blended areas
		// turn their frames from where plain skinning would put them toward the posed, morphed surface, by as much as
		// the morphs there are on, carried back through each vertex's skinning so the game's skinning lands them there.
		// Elsewhere, and without a pose, the turn is measured unposed.
		//
		// a_bones[i] is bone i's skinning transform now (bind to current), in any frame shared by all bones; only the
		// bones in GetPoseBones() are read. Takes effect at the next Apply or Reshade.
		void SetPose(const std::vector<Affine>& a_bones);
		// The bones posed shading reads: those skinning vertices around blended groups. Empty without skinning.
		[[nodiscard]] const std::vector<uint8_t>& GetPoseBones() const { return poseBones; }
		// Whether the last Apply or Reshade shaded any group on the posed mesh, so the output changes with the pose.
		[[nodiscard]] bool IsPoseDependent() const { return !posedGroups.empty(); }
		// After a new pose with the same morphs: re-measures the posed groups only. GetDirty() is then their vertices.
		void Reshade();

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
		bool CanPose() const;
		void Skin(uint32_t a_vertex, uint32_t a_stamp);
		void ShadePosed(uint32_t a_group, float a_strength, uint32_t a_stamp);
		void ClearPosed();

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
		std::vector<float> strength;  // The sum of the values of the morphs moving each moved vertex.
		std::vector<uint32_t> vertexMark, groupMark;
		uint32_t vertexStamp = 0, groupStamp = 0;

		std::vector<uint32_t> moved, shaded, dirty;

		// Posed shading: each vertex's skinning, groups whose surroundings blend bones, the bones those read, the pose,
		// the groups shaded posed last time and each of their vertices' frame turn (in unposed space).
		std::vector<SkinWeights> skin;
		std::vector<uint8_t> groupBlended;
		std::vector<uint8_t> poseBones;
		std::vector<Affine> pose;
		std::vector<uint32_t> posedGroups;
		std::vector<float> posedStrength;
		std::vector<Mat3> posedTurn;
		std::vector<uint8_t> usesPosedTurn;
		// Scratch: each vertex's skinning transform and current position skinned, valid where marked.
		std::vector<Affine> skinned;
		std::vector<Vec3> posedCurrent;
		std::vector<uint32_t> skinMark;
		uint32_t skinStamp = 0;
	};
}
