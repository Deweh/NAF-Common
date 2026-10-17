#include "TriFile.h"
#include "Values.h"
#include "Util/String.h"

namespace Morph
{
	namespace
	{
		class Reader
		{
		public:
			explicit Reader(const std::vector<std::byte>& a_data) :
				data(a_data) {}

			template <class T>
			bool Read(T& a_out)
			{
				if (pos + sizeof(T) > data.size())
					return false;
				std::memcpy(&a_out, data.data() + pos, sizeof(T));
				pos += sizeof(T);
				return true;
			}

			bool ReadName(std::string& a_out)
			{
				uint8_t length;
				if (!Read(length) || pos + length > data.size())
					return false;
				a_out.assign(reinterpret_cast<const char*>(data.data() + pos), length);
				pos += length;
				return true;
			}

			bool Skip(size_t a_bytes)
			{
				if (pos + a_bytes > data.size())
					return false;
				pos += a_bytes;
				return true;
			}

			size_t pos = 0;

		private:
			const std::vector<std::byte>& data;
		};

		constexpr uint32_t kPackedMagic = 0x54524950;  // The multi-character constant 'TRIP': the bytes "PIRT" on disk.
		constexpr uint32_t kFullMagic = 0x54524900;    // 'TRI\0': the bytes "\0IRT".
		constexpr size_t kPackedEntry = sizeof(uint16_t) + 3 * sizeof(int16_t);
		constexpr size_t kFullEntry = sizeof(uint32_t) + 3 * sizeof(float);
	}

	std::shared_ptr<TriFile> TriFile::Parse(std::vector<std::byte> a_data, std::string a_name)
	{
		auto result = std::make_shared<TriFile>();
		result->data = std::move(a_data);
		result->name = std::move(a_name);
		Reader r{ result->data };

		const auto fail = [&](std::string_view a_what) -> std::shared_ptr<TriFile> {
			logger::warn("Morphs: {} isn't a readable TRI file ({} at byte {}).", result->name, a_what, r.pos);
			return nullptr;
		};

		uint32_t magic;
		if (!r.Read(magic))
			return fail("no header");
		if (magic == kPackedMagic) {
			result->packed = true;
		} else if (magic != kFullMagic) {
			return fail("unknown header");
		}

		uint32_t shapeCount = 0;
		if (result->packed) {
			uint16_t count;
			if (!r.Read(count))
				return fail("no shape count");
			shapeCount = count;
		} else if (!r.Read(shapeCount)) {
			return fail("no shape count");
		}

		for (uint32_t s = 0; s < shapeCount; s++) {
			std::string shapeName;
			if (!r.ReadName(shapeName))
				return fail("truncated shape name");
			uint32_t morphCount = 0;
			if (result->packed) {
				uint16_t count;
				if (!r.Read(count))
					return fail("no morph count");
				morphCount = count;
			} else {
				uint32_t blockSize;
				if (!r.Read(blockSize) || !r.Read(morphCount))
					return fail("no morph count");
			}

			auto& shape = result->shapes[ToLowerName(shapeName)];
			for (uint32_t m = 0; m < morphCount; m++) {
				std::string morphName;
				if (!r.ReadName(morphName))
					return fail("truncated morph name");
				MorphRef ref;
				if (result->packed) {
					uint16_t count;
					if (!r.Read(ref.multiplier) || !r.Read(count))
						return fail("truncated morph header");
					ref.count = count;
				} else {
					uint32_t blockSize;
					if (!r.Read(blockSize) || !r.Read(ref.count))
						return fail("truncated morph header");
				}
				ref.offset = r.pos;
				if (!r.Skip(size_t(ref.count) * (result->packed ? kPackedEntry : kFullEntry)))
					return fail("truncated morph data");
				shape.morphs[ToLowerName(morphName)] = ref;
			}
		}
		return result;
	}

	std::shared_ptr<TriFile> TriFile::LoadGameFile(const std::string& a_path)
	{
#if defined TARGET_GAME_F4
		RE::BSResourceNiBinaryStream stream{ a_path };
		if (!stream.good()) {
			logger::warn("Morphs: couldn't open {}.", a_path);
			return nullptr;
		}
		std::vector<std::byte> data;
		std::array<std::byte, 64 * 1024> chunk;
		while (true) {
			const size_t read = stream.DoRead(chunk.data(), chunk.size());
			if (read == 0)
				break;
			data.insert(data.end(), chunk.begin(), chunk.begin() + read);
		}
		return Parse(std::move(data), a_path);
#else
		logger::warn("Morphs: reading {} through the game's file system isn't supported on this game.", a_path);
		return nullptr;
#endif
	}

	std::shared_ptr<TriFile> TriFile::LoadDiskFile(const std::filesystem::path& a_path)
	{
		std::ifstream file{ a_path, std::ios::binary | std::ios::ate };
		if (!file) {
			logger::warn("Morphs: couldn't open {}.", a_path.string());
			return nullptr;
		}
		std::vector<std::byte> data(static_cast<size_t>(file.tellg()));
		file.seekg(0);
		file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
		return Parse(std::move(data), a_path.filename().string());
	}

	const TriFile::Shape* TriFile::FindShape(std::string_view a_lowerName) const
	{
		auto iter = shapes.find(std::string{ a_lowerName });
		return iter != shapes.end() ? &iter->second : nullptr;
	}

	std::shared_ptr<const MorphDeltas> TriFile::GetMorph(const MorphRef& a_ref) const
	{
		std::scoped_lock l{ cacheLock };
		auto& entry = decoded[a_ref.offset];
		if (entry)
			return entry;

		auto deltas = std::make_shared<MorphDeltas>();
		deltas->indices.reserve(a_ref.count);
		deltas->offsets.reserve(a_ref.count);
		const std::byte* p = data.data() + a_ref.offset;
		for (uint32_t i = 0; i < a_ref.count; i++) {
			uint32_t index;
			Vec3 offset;
			if (packed) {
				uint16_t index16;
				std::array<int16_t, 3> v;
				std::memcpy(&index16, p, sizeof(index16));
				std::memcpy(v.data(), p + sizeof(index16), sizeof(v));
				p += kPackedEntry;
				index = index16;
				offset = { v[0] * a_ref.multiplier, v[1] * a_ref.multiplier, v[2] * a_ref.multiplier };
			} else {
				std::memcpy(&index, p, sizeof(index));
				std::memcpy(&offset, p + sizeof(index), sizeof(float) * 3);
				p += kFullEntry;
			}
			deltas->indices.push_back(index);
			deltas->offsets.push_back(offset);
			deltas->maxIndex = (std::max)(deltas->maxIndex, index);
		}
		entry = std::move(deltas);
		return entry;
	}

	template <class T>
	std::shared_future<T> TriLibrary::StartLoad(std::function<T()> a_load)
	{
		// A detached thread rather than std::async, whose future would block when the library drops it mid-load.
		auto promise = std::make_shared<std::promise<T>>();
		std::shared_future<T> future = promise->get_future().share();
		std::thread([promise, load = std::move(a_load)]() {
			try {
				promise->set_value(load());
			} catch (...) {
				promise->set_value(T{});
			}
		}).detach();
		return future;
	}

	template <class T>
	bool TriLibrary::IsReady(const std::shared_future<T>& a_future)
	{
		return a_future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
	}

	std::shared_ptr<TriFile> TriLibrary::GetGameFile(const std::string& a_bodyTriPath, bool& a_pending)
	{
		// BODYTRI paths are relative to Data/Meshes.
		std::string path = ToLowerName(a_bodyTriPath);
		std::ranges::replace(path, '/', '\\');
		if (!path.starts_with("meshes\\"))
			path = "meshes\\" + path;

		std::shared_future<std::shared_ptr<TriFile>> future;
		{
			std::scoped_lock l{ lock };
			auto iter = gameFiles.find(path);
			if (iter == gameFiles.end()) {
				iter = gameFiles.emplace(path, StartLoad<std::shared_ptr<TriFile>>([path]() {
					const auto start = std::chrono::steady_clock::now();
					auto file = TriFile::LoadGameFile(path);
					if (file)
						logger::info("Morphs: loaded {} ({} shapes) in {:.1f} ms.", path, file->GetShapes().size(), std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
					return file;
				})).first;
			}
			future = iter->second;
		}
		if (!IsReady(future)) {
			a_pending = true;
			return nullptr;
		}
		return future.get();
	}

	TriLibrary::ExtraFiles TriLibrary::GetExtraFiles(bool& a_pending)
	{
		std::shared_future<ExtraFiles> future;
		{
			std::scoped_lock l{ lock };
			if (!extraFiles) {
				extraFiles = StartLoad<ExtraFiles>([]() {
					ExtraFiles files;
					const auto dir = Util::String::GetDataPath() / "Morphs";
					std::error_code ec;
					if (!std::filesystem::is_directory(dir, ec))
						return files;
					for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
						if (!entry.is_regular_file() || Util::String::ToLower(entry.path().extension().string()) != ".tri")
							continue;
						if (auto file = TriFile::LoadDiskFile(entry.path())) {
							logger::info("Morphs: loaded {} ({} shapes).", entry.path().filename().string(), file->GetShapes().size());
							files.push_back(std::move(file));
						}
					}
					return files;
				});
			}
			future = *extraFiles;
		}
		if (!IsReady(future)) {
			a_pending = true;
			return {};
		}
		return future.get();
	}

	void TriLibrary::Clear()
	{
		std::scoped_lock l{ lock };
		gameFiles.clear();
		extraFiles.reset();
	}
}
