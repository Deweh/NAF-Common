#pragma once

// Shared by StudioProjectExport and StudioProjectImport.
//
// File layout (little-endian):
//   magic "NAFSTUDO" | u32 version | u64 JSON size | JSON | binary key data
//
// The JSON holds the project structure. Each track names a byte range of the binary data that holds its keys:
//   i32 frame | u8 interpolation | u8 tangent mode | value
// where the value is a float (4 bytes), bool (1), Float3 (12) or quaternion (16, xyzw). Auto tangents are not
// stored; they're regenerated on load.
//
// Bones are referenced by name, so a skeleton whose joints were reordered still loads. A hand's per-finger
// properties are referenced by finger index instead.
//
// Nothing is stored in world space. Actor placements and unparented locators are relative to the scene center,
// which is chosen each time the project is opened. In a rig, unparented locators are relative to its actor's placement.
namespace Serialization::StudioProjectFormat
{
	inline constexpr std::array<char, 8> kMagic{ 'N', 'A', 'F', 'S', 'T', 'U', 'D', 'O' };
	inline constexpr uint32_t kVersion = 6;
	// Version 4 only added to version 3 (hands, spline IK, IK chain shoulders), version 5 to version 4 (each finger's
	// straighten and maxExtend, which make curl 0 flat), and version 6 to version 5 (IK chain twist distribution and
	// shoulder neutral), so version 3 files still read. Version 6 also replaced the shoulder solve, so chains from older
	// files take the default shoulderWeight and drop their reach (kShoulderSchemaVersion).
	inline constexpr uint32_t kOldestReadableVersion = 3;
	inline constexpr size_t kHeaderSize = kMagic.size() + sizeof(uint32_t) + sizeof(uint64_t);
	inline constexpr size_t kKeyHeaderSize = sizeof(int32_t) + 2 * sizeof(uint8_t);

	// Rigs (.nafrig) use the same container with no key data. Their objects use the project schema of kVersion.
	inline constexpr std::array<char, 8> kRigMagic{ 'N', 'A', 'F', 'S', 'T', 'R', 'I', 'G' };
	inline constexpr uint32_t kRigVersion = 2;  // Version 2 holds project schema version 6.
	// The first project and rig versions whose chain shoulderWeight is read as saved.
	inline constexpr uint32_t kShoulderSchemaVersion = 6;
	inline constexpr uint32_t kShoulderSchemaRigVersion = 2;

	// Indexed by ProjectEx::PropertyId.
	inline constexpr std::array<std::string_view, 14> kPropertyNames{ "translation", "rotation", "scale", "weight", "soften", "twist", "enabled",
		"shoulderWeight", "curl", "spread", "stretch", "tangentLength", "orient", "twistDistribution" };
	// Indexed by ProjectEx::ObjectType.
	inline constexpr std::array<std::string_view, 5> kObjectTypeNames{ "actor", "locator", "ikChain", "hand", "splineIK" };
	// Indexed like ProjectEx::Value.
	inline constexpr std::array<std::string_view, 4> kValueTypeNames{ "float", "bool", "float3", "quaternion" };
	inline constexpr std::array<size_t, 4> kValueSizes{ sizeof(float), sizeof(uint8_t), 3 * sizeof(float), 4 * sizeof(float) };
}
