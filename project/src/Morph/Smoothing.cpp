#include "Smoothing.h"
#include "Util/String.h"
#include "Values.h"
#include "simdjson.h"

namespace Morph
{
	namespace
	{
		// A bone whose share of a group's smoothed position is below this is dropped from the group.
		constexpr float kWeightTolerance = 1e-3f;
		// While a bone's share at a group is above this, the smoothing spreads it to the group's neighbours. Below it,
		// nothing that could reach kWeightTolerance is left to spread.
		constexpr float kSpreadTolerance = 1e-7f;
		// A group's normal is untrustworthy when its triangles' area is under this fraction of the median group's, or
		// when the length of their summed normals is under this fraction of their total area (they face every which way).
		constexpr float kTinyArea = 0.01f;
		constexpr float kMinFacing = 0.5f;

		struct PositionKey
		{
			uint32_t x, y, z;
			bool operator==(const PositionKey&) const = default;
		};

		struct PositionKeyHash
		{
			// As MeshDeformer's: positions widened from half floats have their low bits zero, so the high bits are mixed
			// down (MurmurHash3's finalizer).
			size_t operator()(const PositionKey& k) const
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

		void HashBytes(uint64_t& a_hash, const void* a_data, size_t a_size)
		{
			// FNV-1a over 8-byte words, then the tail.
			const auto bytes = static_cast<const uint8_t*>(a_data);
			size_t i = 0;
			for (; i + 8 <= a_size; i += 8) {
				uint64_t word;
				std::memcpy(&word, bytes + i, 8);
				a_hash = (a_hash ^ word) * 0x100000001B3ull;
			}
			for (; i < a_size; i++) a_hash = (a_hash ^ bytes[i]) * 0x100000001B3ull;
		}

		using Float4 = std::array<float, 4>;

		struct WeightEntry
		{
			uint32_t group;
			uint32_t bone;
			Float4 value;
		};
	}

	uint64_t SmoothMesh::Hash(const Input& a_input, const SmoothSettings& a_settings)
	{
		uint64_t h = 0xCBF29CE484222325ull;
		HashBytes(h, a_input.rest.data(), a_input.rest.size() * sizeof(Vec3));
		HashBytes(h, a_input.triangles.data(), a_input.triangles.size() * sizeof(uint32_t));
		HashBytes(h, a_input.skin.data(), a_input.skin.size() * sizeof(SkinWeights));
		HashBytes(h, &a_input.boneCount, sizeof(a_input.boneCount));
		HashBytes(h, &a_settings.repeat, sizeof(a_settings.repeat));
		const uint8_t pin = a_settings.pinBoundaries;
		HashBytes(h, &pin, 1);
		return h;
	}

	std::shared_ptr<const SmoothMesh> SmoothMesh::Build(const Input& a_input, const SmoothSettings& a_settings)
	{
		const auto start = std::chrono::steady_clock::now();
		const uint32_t vertexCount = static_cast<uint32_t>(a_input.rest.size());
		if (vertexCount == 0 || a_input.skin.size() != vertexCount || a_input.boneCount == 0 || a_input.triangles.empty() || a_input.triangles.size() % 3 != 0 ||
			!std::ranges::all_of(a_input.triangles, [&](uint32_t i) { return i < vertexCount; }))
			return nullptr;

		auto mesh = std::make_shared<SmoothMesh>();
		auto& m = *mesh;
		m.vertexCount = vertexCount;
		m.boneCount = a_input.boneCount;

		// Weld groups: vertices at the same rest position.
		{
			std::unordered_map<PositionKey, uint32_t, PositionKeyHash> groups;
			m.vertexGroup.resize(vertexCount);
			for (uint32_t v = 0; v < vertexCount; v++) {
				const auto& p = a_input.rest[v];
				const PositionKey key{ std::bit_cast<uint32_t>(p.x), std::bit_cast<uint32_t>(p.y), std::bit_cast<uint32_t>(p.z) };
				auto [iter, added] = groups.try_emplace(key, static_cast<uint32_t>(groups.size()));
				m.vertexGroup[v] = iter->second;
				if (added)
					m.groupVertex.push_back(v);
			}
			m.groupCount = static_cast<uint32_t>(groups.size());
		}
		const uint32_t groupCount = m.groupCount;
		const auto rest = [&](uint32_t g) { return a_input.rest[m.groupVertex[g]]; };

		// Triangles between groups (a triangle welded flat is dropped), their edges, and which edges are open.
		std::vector<std::array<uint32_t, 3>> triangles;
		triangles.reserve(a_input.triangles.size() / 3);
		std::unordered_map<uint64_t, uint32_t> edgeUse;
		const auto edgeKey = [](uint32_t a, uint32_t b) { return a < b ? (uint64_t(a) << 32) | b : (uint64_t(b) << 32) | a; };
		for (size_t t = 0; t < a_input.triangles.size(); t += 3) {
			const std::array<uint32_t, 3> tri{ m.vertexGroup[a_input.triangles[t]], m.vertexGroup[a_input.triangles[t + 1]], m.vertexGroup[a_input.triangles[t + 2]] };
			if (tri[0] == tri[1] || tri[1] == tri[2] || tri[0] == tri[2])
				continue;
			triangles.push_back(tri);
			for (int k = 0; k < 3; k++) edgeUse[edgeKey(tri[k], tri[(k + 1) % 3])]++;
		}
		if (triangles.empty())
			return nullptr;

		m.pinned.assign(groupCount, 0);
		m.neighbourStart.assign(groupCount + 1, 0);
		for (const auto& [key, uses] : edgeUse) {
			const uint32_t a = uint32_t(key >> 32), b = uint32_t(key);
			m.neighbourStart[a + 1]++;
			m.neighbourStart[b + 1]++;
			if (uses == 1 && a_settings.pinBoundaries)
				m.pinned[a] = m.pinned[b] = 1;
		}
		for (uint32_t g = 0; g < groupCount; g++) m.neighbourStart[g + 1] += m.neighbourStart[g];
		m.neighbours.resize(m.neighbourStart.back());
		{
			std::vector<uint32_t> fill(m.neighbourStart.begin(), m.neighbourStart.end() - 1);
			for (const auto& [key, uses] : edgeUse) {
				const uint32_t a = uint32_t(key >> 32), b = uint32_t(key);
				m.neighbours[fill[a]++] = b;
				m.neighbours[fill[b]++] = a;
			}
		}

		// Corners: at group g of triangle (g, next, previous) in winding order.
		m.cornerStart.assign(groupCount + 1, 0);
		for (const auto& tri : triangles) {
			for (int k = 0; k < 3; k++) m.cornerStart[tri[k] + 1]++;
		}
		for (uint32_t g = 0; g < groupCount; g++) m.cornerStart[g + 1] += m.cornerStart[g];
		m.corners.resize(m.cornerStart.back());
		{
			std::vector<uint32_t> fill(m.cornerStart.begin(), m.cornerStart.end() - 1);
			for (const auto& tri : triangles) {
				for (int k = 0; k < 3; k++) m.corners[fill[tri[k]]++] = { tri[(k + 2) % 3], tri[(k + 1) % 3] };
			}
		}

		// Rest normals, and where they can't be trusted: triangles around the group whose area is under 1% of a typical
		// group's, or whose normals mostly cancel out.
		m.restNormal.assign(groupCount, {});
		std::vector<float> area(groupCount, 0.0f), facing(groupCount, 0.0f);
		for (uint32_t g = 0; g < groupCount; g++) {
			Vec3 n{};
			const auto p = rest(g);
			for (uint32_t c = m.cornerStart[g]; c < m.cornerStart[g + 1]; c++) {
				const auto face = Cross(rest(m.corners[c][1]) - p, rest(m.corners[c][0]) - p);
				n += face;
				area[g] += std::sqrt(Dot(face, face));
			}
			facing[g] = area[g] > 0.0f ? std::sqrt(Dot(n, n)) / area[g] : 0.0f;
			m.restNormal[g] = Normalized(n);
		}
		{
			auto sorted = area;
			std::ranges::nth_element(sorted, sorted.begin() + sorted.size() / 2);
			const float typical = sorted[sorted.size() / 2];
			m.frameShaded.assign(groupCount, 0);
			for (uint32_t g = 0; g < groupCount; g++) m.frameShaded[g] = area[g] < typical * kTinyArea || facing[g] < kMinFacing;
		}
		// For auto-smoothing: each group's share of its triangles' area (area holds twice their area), its rest position,
		// and each edge's wing groups and the angle between its triangles at rest.
		m.rest.resize(groupCount);
		m.restArea.resize(groupCount);
		for (uint32_t g = 0; g < groupCount; g++) {
			m.rest[g] = rest(g);
			m.restArea[g] = area[g] / 6.0f;
		}
		{
			std::unordered_map<uint64_t, uint32_t> third;
			const auto directed = [](uint32_t a, uint32_t b) { return (uint64_t(a) << 32) | b; };
			for (const auto& tri : triangles) {
				for (int k = 0; k < 3; k++) third[directed(tri[k], tri[(k + 1) % 3])] = tri[(k + 2) % 3];
			}
			m.wings.resize(m.neighbours.size());
			m.restEdgeAngle.resize(m.neighbours.size());
			for (uint32_t g = 0; g < groupCount; g++) {
				for (uint32_t k = m.neighbourStart[g]; k < m.neighbourStart[g + 1]; k++) {
					const uint32_t n = m.neighbours[k];
					const auto a = third.find(directed(g, n)), b = third.find(directed(n, g));
					m.wings[k] = { a != third.end() ? a->second : kNoGroup, b != third.end() ? b->second : kNoGroup };
					m.restEdgeAngle[k] = 0.0f;
					if (a != third.end() && b != third.end()) {
						const auto n1 = Normalized(Cross(rest(n) - rest(g), rest(a->second) - rest(g)));
						const auto n2 = Normalized(Cross(rest(g) - rest(n), rest(b->second) - rest(n)));
						m.restEdgeAngle[k] = std::acos(std::clamp(Dot(n1, n2), -1.0f, 1.0f)) * (180.0f / std::numbers::pi_v<float>);
					}
				}
			}
		}

		// The smoothing pass, as Corrective Smooth's Length Weight (factor 1): each group moves by
		// 2 * sum_e(length_e * (neighbour - group)) / (sum_e(length_e) * edges), all groups at once, with lengths at rest.
		std::vector<float> lengths(m.neighbours.size());
		std::vector<float> step(groupCount, 0.0f);
		for (uint32_t g = 0; g < groupCount; g++) {
			float total = 0.0f;
			const auto p = rest(g);
			for (uint32_t k = m.neighbourStart[g]; k < m.neighbourStart[g + 1]; k++) {
				const auto d = rest(m.neighbours[k]) - p;
				lengths[k] = std::sqrt(Dot(d, d));
				total += lengths[k];
			}
			const uint32_t edges = m.neighbourStart[g + 1] - m.neighbourStart[g];
			if (total > 1e-12f && edges > 0 && !m.pinned[g])
				step[g] = 2.0f / (total * float(edges));
		}

		// Each group's skinning, from one of its vertices.
		std::vector<SkinWeights> groupSkin(groupCount);
		for (uint32_t g = 0; g < groupCount; g++) groupSkin[g] = a_input.skin[m.groupVertex[g]];

		// The precomputed passes are linear, so each bone's field (weight * [rest, 1]) is smoothed on its own, from where
		// the bone has weight outwards as far as it spreads. Bones are shared out between threads.
		std::atomic<uint32_t> nextBone{ 0 };
		std::mutex resultLock;
		std::vector<WeightEntry> results;
		const auto worker = [&]() {
			std::vector<Float4> current(groupCount, Float4{}), next(groupCount, Float4{});
			std::vector<uint8_t> listed(groupCount, 0);
			std::vector<uint32_t> list;
			std::vector<WeightEntry> local;
			for (uint32_t bone = nextBone++; bone < m.boneCount; bone = nextBone++) {
				list.clear();
				for (uint32_t g = 0; g < groupCount; g++) {
					float w = 0.0f;
					for (int i = 0; i < 4; i++) {
						if (groupSkin[g].bones[i] == bone)
							w += groupSkin[g].weights[i];
					}
					if (w <= 0.0f)
						continue;
					const auto p = rest(g);
					current[g] = { p.x * w, p.y * w, p.z * w, w };
					listed[g] = 1;
					list.push_back(g);
				}
				if (list.empty())
					continue;
				for (uint32_t it = 0; it < a_settings.repeat; it++) {
					// Groups next to where the field is still spreading join the list (at 0).
					const size_t count = list.size();
					for (size_t i = 0; i < count; i++) {
						const uint32_t g = list[i];
						if (std::abs(current[g][3]) <= kSpreadTolerance)
							continue;
						for (uint32_t k = m.neighbourStart[g]; k < m.neighbourStart[g + 1]; k++) {
							const uint32_t n = m.neighbours[k];
							if (!listed[n]) {
								listed[n] = 1;
								list.push_back(n);
							}
						}
					}
					for (uint32_t g : list) {
						const auto& c = current[g];
						Float4 acc{};
						if (step[g] > 0.0f) {
							for (uint32_t k = m.neighbourStart[g]; k < m.neighbourStart[g + 1]; k++) {
								const auto& n = current[m.neighbours[k]];
								const float l = lengths[k];
								for (int j = 0; j < 4; j++) acc[j] += (n[j] - c[j]) * l;
							}
						}
						for (int j = 0; j < 4; j++) next[g][j] = c[j] + acc[j] * step[g];
					}
					for (uint32_t g : list) current[g] = next[g];
				}
				for (uint32_t g : list) {
					if (std::abs(current[g][3]) > kWeightTolerance)
						local.push_back({ g, bone, current[g] });
					current[g] = {};
					next[g] = {};
					listed[g] = 0;
				}
			}
			std::scoped_lock l{ resultLock };
			results.insert(results.end(), local.begin(), local.end());
		};
		{
			const uint32_t threads = std::clamp(std::thread::hardware_concurrency(), 2u, 32u) - 1;
			std::vector<std::thread> pool;
			for (uint32_t i = 0; i < threads; i++) pool.emplace_back(worker);
			for (auto& t : pool) t.join();
		}

		// By group, rescaled so the kept shares sum to 1.
		m.weightStart.assign(groupCount + 1, 0);
		for (const auto& e : results) m.weightStart[e.group + 1]++;
		for (uint32_t g = 0; g < groupCount; g++) m.weightStart[g + 1] += m.weightStart[g];
		m.weightBone.resize(results.size());
		m.weights.resize(results.size());
		{
			std::vector<uint32_t> fill(m.weightStart.begin(), m.weightStart.end() - 1);
			std::ranges::sort(results, [](const WeightEntry& a, const WeightEntry& b) { return a.group != b.group ? a.group < b.group : a.bone < b.bone; });
			for (const auto& e : results) {
				const uint32_t i = fill[e.group]++;
				m.weightBone[i] = e.bone;
				m.weights[i] = e.value;
			}
		}
		m.smoothedRest.assign(groupCount, {});
		for (uint32_t g = 0; g < groupCount; g++) {
			float total = 0.0f;
			for (uint32_t i = m.weightStart[g]; i < m.weightStart[g + 1]; i++) total += m.weights[i][3];
			if (total <= 0.0f) {
				// Nothing reached it (an unweighted island): it stays put.
				m.smoothedRest[g] = rest(g);
				continue;
			}
			Vec3 s{};
			for (uint32_t i = m.weightStart[g]; i < m.weightStart[g + 1]; i++) {
				for (auto& v : m.weights[i]) v /= total;
				s += Vec3{ m.weights[i][0], m.weights[i][1], m.weights[i][2] };
			}
			m.smoothedRest[g] = s;
		}
		m.buildMilliseconds = static_cast<float>(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
		return mesh;
	}

	namespace
	{
		// Lowercase, without a trailing "_skin".
		std::string BoneKey(std::string_view a_name)
		{
			std::string key = ToLowerName(a_name);
			constexpr std::string_view kSkin = "_skin";
			if (key.size() > kSkin.size() && key.ends_with(kSkin))
				key.resize(key.size() - kSkin.size());
			return key;
		}

		// The hip region: pelvis, lower spine (the top of the hip crease), buttocks and upper thighs with their helpers.
		// Not the lower thigh bones (Thigh_Low, Thigh_Fat), which skin the knee, nor the calves.
		bool IsHipBone(std::string_view a_key)
		{
			static const std::unordered_set<std::string_view> bones{ "pelvis", "pelvis_rear", "spine1", "spine1_rear", "lbutt_01", "rbutt_01", "lleg_thigh", "rleg_thigh",
				"lleg_thigh_01_f", "rleg_thigh_01_f", "lleg_thigh_01_r", "rleg_thigh_01_r" };
			return bones.contains(a_key);
		}
	}

	void SmoothLibrary::SetRegion(SmoothRegion a_region)
	{
		if (a_region == SmoothRegion::kCustom)
			ReloadCustom();
		region.store(a_region, std::memory_order_relaxed);
		regionGeneration.fetch_add(1, std::memory_order_release);
	}

	void SmoothLibrary::ReloadCustom()
	{
		std::unordered_map<std::string, float> bones;
		const auto path = Util::String::GetDataPath() / "Smoothing.json";
		simdjson::dom::parser parser;
		simdjson::dom::element doc;
		simdjson::dom::object list;
		std::error_code ec;
		if (!std::filesystem::exists(path, ec)) {
			logger::warn("Morphs: {} doesn't exist; the custom smoothing region is empty.", path.string());
		} else if (auto error = parser.load(path.string()).get(doc); error != simdjson::SUCCESS) {
			logger::warn("Morphs: couldn't parse {}: {}", path.string(), simdjson::error_message(error));
		} else if (doc["bones"].get(list) != simdjson::SUCCESS) {
			logger::warn("Morphs: {} needs a 'bones' object (bone name: strength).", path.string());
		} else {
			for (auto field : list) {
				double strength = 0.0;
				int64_t whole = 0;
				if (field.value.get(strength) != simdjson::SUCCESS && field.value.get(whole) == simdjson::SUCCESS)
					strength = static_cast<double>(whole);
				bones[BoneKey(field.key)] = std::clamp(static_cast<float>(strength), 0.0f, 1.0f);
			}
			logger::info("Morphs: custom smoothing region: {} bones from {}.", bones.size(), path.string());
		}
		{
			std::scoped_lock l{ lock };
			customBones = std::move(bones);
		}
		regionGeneration.fetch_add(1, std::memory_order_release);
	}

	float SmoothLibrary::GetBoneStrength(std::string_view a_bone) const
	{
		const auto key = BoneKey(a_bone);
		switch (GetRegion()) {
		case SmoothRegion::kWholeBody:
			return 1.0f;
		case SmoothRegion::kCustom:
			{
				std::scoped_lock l{ lock };
				const auto iter = customBones.find(key);
				return iter != customBones.end() ? iter->second : 0.0f;
			}
		default:
			return IsHipBone(key) ? 1.0f : 0.0f;
		}
	}

	VolumeBone SmoothLibrary::GetVolumeBone(std::string_view a_bone)
	{
		const auto key = BoneKey(a_bone);
		if (key == "pelvis")
			return VolumeBone::kPelvis;
		if (key == "pelvis_rear")
			return VolumeBone::kPelvisRear;
		if (key == "lbutt_01" || key == "rbutt_01")
			return VolumeBone::kButtock;
		return VolumeBone::kNone;
	}

	void SmoothLibrary::SetSettings(const SmoothSettings& a_settings)
	{
		SmoothSettings clamped = a_settings;
		clamped.repeat = std::clamp(clamped.repeat, kMinRepeat, kMaxRepeat);
		std::scoped_lock l{ lock };
		if (clamped == settings)
			return;
		settings = clamped;
		cache.clear();
		generation.fetch_add(1, std::memory_order_release);
	}

	SmoothSettings SmoothLibrary::GetSettings() const
	{
		std::scoped_lock l{ lock };
		return settings;
	}

	SmoothLibrary::MeshFuture SmoothLibrary::Get(SmoothMesh::Input a_input)
	{
		SmoothSettings current;
		uint64_t gen;
		{
			std::scoped_lock l{ lock };
			current = settings;
			gen = generation.load(std::memory_order_acquire);
		}
		const uint64_t key = SmoothMesh::Hash(a_input, current);

		std::scoped_lock l{ lock };
		if (generation.load(std::memory_order_acquire) != gen) {
			// The settings changed while hashing: the caller asks again.
			std::promise<std::shared_ptr<const SmoothMesh>> none;
			none.set_value(nullptr);
			return none.get_future().share();
		}
		std::erase_if(cache, [](const auto& a_pair) { return !a_pair.second.building && a_pair.second.built.expired(); });
		auto& entry = cache[key];
		if (auto built = entry.built.lock()) {
			std::promise<std::shared_ptr<const SmoothMesh>> ready;
			ready.set_value(std::move(built));
			return ready.get_future().share();
		}
		if (entry.building)
			return *entry.building;

		auto promise = std::make_shared<std::promise<std::shared_ptr<const SmoothMesh>>>();
		entry.building = promise->get_future().share();
		auto future = *entry.building;
		std::thread([this, promise, input = std::move(a_input), current, key, gen]() {
			auto mesh = SmoothMesh::Build(input, current);
			if (mesh) {
				logger::info("Morphs: smoothing data for a mesh of {} vertices ({} weld groups, {} bones, {} weights, {} repeats{}) built in {:.0f} ms.", mesh->vertexCount, mesh->groupCount,
					mesh->boneCount, mesh->weights.size(), current.repeat, current.pinBoundaries ? ", open edges pinned" : "", mesh->buildMilliseconds);
			} else {
				logger::warn("Morphs: couldn't build smoothing data for a mesh of {} vertices.", input.rest.size());
			}
			{
				std::scoped_lock l{ lock };
				if (generation.load(std::memory_order_acquire) == gen) {
					if (auto iter = cache.find(key); iter != cache.end()) {
						iter->second.built = mesh;
						iter->second.building.reset();
					}
				}
			}
			promise->set_value(std::move(mesh));
		}).detach();
		return future;
	}
}
