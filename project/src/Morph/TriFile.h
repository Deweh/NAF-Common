#pragma once
#include "Geometry.h"

// BodySlide's TRI morph files, as LooksMenu reads them: per shape (by name), named morphs of per-vertex position offsets
// in the mesh's own space. Two layouts, told apart by the first four bytes (written as multi-character constants, so
// they read backwards on disk):
//   'TRIP' (packed, what BodySlide writes): u16 shape count; per shape a u8-length name and a u16 morph count; per morph
//          a u8-length name, a float multiplier and a u16 vertex count, then per vertex a u16 index and three s16
//          offsets, each times the multiplier.
//   'TRI\0' (full): u32 shape count; per shape a u8-length name, a u32 block size and a u32 morph count; per morph a
//          u8-length name, a u32 block size and a u32 vertex count, then per vertex a u32 index and three floats.
// A file is indexed when loaded and each morph decoded on first use, so a body's hundred sliders cost only their raw
// bytes until one is used.
namespace Morph
{
	class TriFile
	{
	public:
		struct MorphRef
		{
			size_t offset = 0;  // Of the first vertex entry.
			uint32_t count = 0;
			float multiplier = 1.0f;
		};

		struct Shape
		{
			std::unordered_map<std::string, MorphRef> morphs;  // By lowercase name.
		};

		// Null if the data isn't a TRI file; the reason is logged.
		static std::shared_ptr<TriFile> Parse(std::vector<std::byte> a_data, std::string a_name);
		// From the game's file system (loose or archived), relative to Data, e.g. "meshes\\actors\\...\\femalebody.tri".
		// Fallout 4 only for now; null elsewhere.
		static std::shared_ptr<TriFile> LoadGameFile(const std::string& a_path);
		static std::shared_ptr<TriFile> LoadDiskFile(const std::filesystem::path& a_path);

		[[nodiscard]] const Shape* FindShape(std::string_view a_lowerName) const;
		[[nodiscard]] std::shared_ptr<const MorphDeltas> GetMorph(const MorphRef& a_ref) const;
		[[nodiscard]] const std::string& GetName() const { return name; }
		[[nodiscard]] const std::unordered_map<std::string, Shape>& GetShapes() const { return shapes; }

	private:
		std::vector<std::byte> data;
		std::string name;
		bool packed = false;
		std::unordered_map<std::string, Shape> shapes;  // By lowercase name.
		mutable std::mutex cacheLock;
		mutable std::unordered_map<size_t, std::shared_ptr<const MorphDeltas>> decoded;
	};

	// Loaded TRI files: those named by meshes' BODYTRI extra data, and the extra files in Data/NAF/Morphs, which apply to
	// any shape with a matching name and enough vertices. Files load on a background thread (a body's TRI can be 10 MB),
	// so the getters never block: until a file is loaded they return nothing and set a_pending, and the caller asks again
	// on a later frame. Owned by Manager (Manager::GetTriFiles).
	class TriLibrary
	{
	public:
		using ExtraFiles = std::vector<std::shared_ptr<TriFile>>;

		// Null if not loaded yet (a_pending set) or unreadable.
		std::shared_ptr<TriFile> GetGameFile(const std::string& a_bodyTriPath, bool& a_pending);
		// Empty while loading (a_pending set).
		[[nodiscard]] ExtraFiles GetExtraFiles(bool& a_pending);
		// Drops everything, so files are re-read on next use. Loads already running finish into the old entries.
		void Clear();

	private:
		template <class T>
		static std::shared_future<T> StartLoad(std::function<T()> a_load);
		template <class T>
		static bool IsReady(const std::shared_future<T>& a_future);

		std::mutex lock;
		std::unordered_map<std::string, std::shared_future<std::shared_ptr<TriFile>>> gameFiles;  // By lowercase path.
		std::optional<std::shared_future<ExtraFiles>> extraFiles;
	};
}
