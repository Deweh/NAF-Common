#include "Drivers.h"
#include "Animation/Ozz.h"
#include "Util/Ozz.h"
#include "Util/String.h"
#include "simdjson.h"

namespace Morph
{
	namespace
	{
		using Vec3 = std::array<float, 3>;

		constexpr float kDegToRad = std::numbers::pi_v<float> / 180.0f;
		constexpr float kRadToDeg = 180.0f / std::numbers::pi_v<float>;

		Vec3 Normalize(const Vec3& a)
		{
			const float len = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
			if (len < 1e-8f)
				return { 0.0f, 0.0f, 0.0f };
			return { a[0] / len, a[1] / len, a[2] / len };
		}

		float AngleBetween(const Vec3& a, const Vec3& b)
		{
			const float d = std::clamp(a[0] * b[0] + a[1] * b[1] + a[2] * b[2], -1.0f, 1.0f);
			return std::acos(d);
		}

		Vec3 ToVec3(const ozz::math::SimdFloat4& a)
		{
			Vec3 result;
			ozz::math::Store3PtrU(a, result.data());
			return result;
		}

		// Rotation about the model axes, X then Y then Z.
		Vec3 RotateEuler(const std::array<float, 3>& a_degrees, const Vec3& v)
		{
			Vec3 r = v;
			const auto rotate = [&](int a_axis, float a_angle) {
				const float c = std::cos(a_angle * kDegToRad), s = std::sin(a_angle * kDegToRad);
				const int i = (a_axis + 1) % 3, j = (a_axis + 2) % 3;
				const float x = r[i], y = r[j];
				r[i] = c * x - s * y;
				r[j] = s * x + c * y;
			};
			rotate(0, a_degrees[0]);
			rotate(1, a_degrees[1]);
			rotate(2, a_degrees[2]);
			return r;
		}

		// Model-space transforms of the needed joints, from their locals and their parents'.
		void ComputeModel(const ozz::animation::Skeleton& a_skeleton, const std::vector<bool>& a_needed, const DriverRuntime::LocalGetter& a_local, std::vector<ozz::math::Float4x4>& a_model)
		{
			const auto parents = a_skeleton.joint_parents();
			a_model.resize(parents.size());
			for (size_t i = 0; i < parents.size(); i++) {
				if (!a_needed[i])
					continue;
				const auto local = a_local(static_cast<int32_t>(i));
				const int16_t parent = parents[i];
				a_model[i] = parent >= 0 ? a_model[parent] * local : local;
			}
		}

		float Dot(const Vec3& a, const Vec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
		Vec3 Cross(const Vec3& a, const Vec3& b) { return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] }; }
		Vec3 Scale(const Vec3& a, float s) { return { a[0] * s, a[1] * s, a[2] * s }; }
		Vec3 Add(const Vec3& a, const Vec3& b) { return { a[0] + b[0], a[1] + b[1], a[2] + b[2] }; }

		// a_v turned by the shortest rotation taking unit a_from to unit a_to.
		Vec3 RotateBetween(const Vec3& a_from, const Vec3& a_to, const Vec3& a_v)
		{
			const float c = Dot(a_from, a_to);
			if (c > 0.999999f)
				return a_v;
			if (c < -0.999999f) {
				// Opposite: any half turn about an axis square to a_from will do.
				Vec3 axis = Normalize(Cross(a_from, std::abs(a_from[0]) < 0.9f ? Vec3{ 1, 0, 0 } : Vec3{ 0, 1, 0 }));
				return Add(Scale(axis, 2.0f * Dot(axis, a_v)), Scale(a_v, -1.0f));
			}
			const auto axis = Cross(a_from, a_to);
			return Add(Add(Scale(a_v, c), Cross(axis, a_v)), Scale(axis, Dot(axis, a_v) / (1.0f + c)));
		}

		// a_v turned by a_angle (radians) about unit a_axis.
		Vec3 RotateAbout(const Vec3& a_axis, float a_angle, const Vec3& a_v)
		{
			const float c = std::cos(a_angle), s = std::sin(a_angle);
			return Add(Add(Scale(a_v, c), Scale(Cross(a_axis, a_v), s)), Scale(a_axis, Dot(a_axis, a_v) * (1.0f - c)));
		}

		// a_local turned by a model transform's rotation (normalized, so scale doesn't matter).
		Vec3 TurnBy(const ozz::math::Float4x4& a_transform, const Vec3& a_local)
		{
			return Normalize(ToVec3(ozz::math::TransformVector(a_transform, ozz::math::simd_float4::Load(a_local[0], a_local[1], a_local[2], 0.0f))));
		}

		struct Measured
		{
			Vec3 axis, side;
		};

		// Swing (radians between the axes) and twist (radians between the side vectors once the swing is undone).
		std::pair<float, float> SwingTwist(const Measured& a, const Measured& b)
		{
			const float swing = AngleBetween(a.axis, b.axis);
			Vec3 carried = RotateBetween(b.axis, a.axis, b.side);
			carried = Normalize(Add(carried, Scale(a.axis, -Dot(carried, a.axis))));
			const Vec3 side = Normalize(Add(a.side, Scale(a.axis, -Dot(a.side, a.axis))));
			return { swing, AngleBetween(side, carried) };
		}

		float Distance(const Measured& a, const Measured& b, float a_twistWeight)
		{
			const auto [swing, twist] = SwingTwist(a, b);
			const float t = twist * a_twistWeight;
			return std::sqrt(swing * swing + t * t);
		}

		// Solves a_matrix * x = a_rhs (n x n, n x m), in place, by Gaussian elimination with partial pivoting.
		bool Solve(std::vector<double>& a_matrix, std::vector<double>& a_rhs, size_t n, size_t m)
		{
			for (size_t col = 0; col < n; col++) {
				size_t pivot = col;
				for (size_t row = col + 1; row < n; row++) {
					if (std::abs(a_matrix[row * n + col]) > std::abs(a_matrix[pivot * n + col]))
						pivot = row;
				}
				if (std::abs(a_matrix[pivot * n + col]) < 1e-12)
					return false;
				if (pivot != col) {
					for (size_t k = 0; k < n; k++) std::swap(a_matrix[col * n + k], a_matrix[pivot * n + k]);
					for (size_t k = 0; k < m; k++) std::swap(a_rhs[col * m + k], a_rhs[pivot * m + k]);
				}
				for (size_t row = 0; row < n; row++) {
					if (row == col)
						continue;
					const double f = a_matrix[row * n + col] / a_matrix[col * n + col];
					if (f == 0.0)
						continue;
					for (size_t k = col; k < n; k++) a_matrix[row * n + k] -= f * a_matrix[col * n + k];
					for (size_t k = 0; k < m; k++) a_rhs[row * m + k] -= f * a_rhs[col * m + k];
				}
			}
			for (size_t row = 0; row < n; row++) {
				for (size_t k = 0; k < m; k++) a_rhs[row * m + k] /= a_matrix[row * n + row];
			}
			return true;
		}

		float Kernel(float a_angle, float a_radius)
		{
			const float x = a_angle / a_radius;
			return std::exp(-x * x);
		}

		std::string GetString(simdjson::dom::element a_element, const char* a_key)
		{
			std::string_view value;
			if (a_element[a_key].get(value) != simdjson::SUCCESS)
				return {};
			return std::string{ value };
		}

		float GetFloat(simdjson::dom::element a_element, double a_default)
		{
			double d;
			if (a_element.get(d) == simdjson::SUCCESS)
				return static_cast<float>(d);
			int64_t i;
			if (a_element.get(i) == simdjson::SUCCESS)
				return static_cast<float>(i);
			return static_cast<float>(a_default);
		}

		std::optional<Driver> ParseDriver(simdjson::dom::element a_json, const std::filesystem::path& a_file)
		{
			Driver d;
			d.name = GetString(a_json, "name");
			d.bone = GetString(a_json, "bone");
			d.tip = GetString(a_json, "tip");
			d.reference = GetString(a_json, "reference");
			if (d.bone.empty() || d.tip.empty() || d.reference.empty()) {
				logger::warn("Correctives: driver '{}' in {} needs 'bone', 'tip' and 'reference'; skipping it.", d.name, a_file.filename().string());
				return std::nullopt;
			}
			if (d.name.empty())
				d.name = d.bone;
			simdjson::dom::element radius;
			if (a_json["radius"].get(radius) == simdjson::SUCCESS)
				d.radius = std::max(GetFloat(radius, 60.0), 1.0f);
			simdjson::dom::element twist;
			if (a_json["twist"].get(twist) == simdjson::SUCCESS)
				d.twistWeight = std::max(GetFloat(twist, 1.0), 0.0f);
			simdjson::dom::array range;
			if (a_json["range"].get(range) == simdjson::SUCCESS && range.size() == 2) {
				d.minimum = GetFloat(range.at(0).value(), 0.0);
				d.maximum = GetFloat(range.at(1).value(), 1.0);
			}

			simdjson::dom::array poses;
			if (a_json["poses"].get(poses) != simdjson::SUCCESS) {
				logger::warn("Correctives: driver '{}' in {} has no 'poses'; skipping it.", d.name, a_file.filename().string());
				return std::nullopt;
			}
			std::vector<std::vector<std::pair<std::string, float>>> poseMorphs;
			for (auto pose : poses) {
				auto& p = d.poses.emplace_back();
				p.name = GetString(pose, "name");
				simdjson::dom::array rotation;
				if (pose["rotation"].get(rotation) == simdjson::SUCCESS) {
					for (size_t i = 0; i < 3 && i < rotation.size(); i++) p.rotation[i] = GetFloat(rotation.at(i).value(), 0.0);
				}
				simdjson::dom::element poseTwist;
				if (pose["twist"].get(poseTwist) == simdjson::SUCCESS)
					p.twist = GetFloat(poseTwist, 0.0);
				auto& morphs = poseMorphs.emplace_back();
				simdjson::dom::object morphObject;
				if (pose["morphs"].get(morphObject) == simdjson::SUCCESS) {
					for (auto field : morphObject) {
						auto name = ToLowerName(field.key);
						morphs.emplace_back(name, GetFloat(field.value, 0.0));
						if (!std::ranges::contains(d.morphs, name))
							d.morphs.push_back(name);
					}
				}
			}
			for (size_t i = 0; i < d.poses.size(); i++) {
				d.poses[i].weights.assign(d.morphs.size(), 0.0f);
				for (auto& [name, weight] : poseMorphs[i]) {
					d.poses[i].weights[std::ranges::find(d.morphs, name) - d.morphs.begin()] = weight;
				}
			}
			if (d.morphs.empty()) {
				logger::warn("Correctives: driver '{}' in {} drives no morphs; skipping it.", d.name, a_file.filename().string());
				return std::nullopt;
			}
			return d;
		}
	}

	void DriverLibrary::Load()
	{
		std::map<std::string, std::vector<Driver>, std::less<>> parsed;
		const auto dir = Util::String::GetDataPath() / "Correctives";
		std::error_code ec;
		if (std::filesystem::is_directory(dir, ec)) {
			for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
				if (!entry.is_regular_file() || Util::String::ToLower(entry.path().extension().string()) != ".json")
					continue;
				simdjson::dom::parser parser;
				simdjson::dom::element doc;
				if (auto error = parser.load(entry.path().string()).get(doc); error != simdjson::SUCCESS) {
					logger::warn("Correctives: couldn't parse {}: {}", entry.path().filename().string(), simdjson::error_message(error));
					continue;
				}
				std::vector<std::string> skeletons;
				if (auto one = GetString(doc, "skeleton"); !one.empty())
					skeletons.push_back(ToLowerName(one));
				simdjson::dom::array many;
				if (doc["skeletons"].get(many) == simdjson::SUCCESS) {
					for (auto s : many) {
						std::string_view name;
						if (s.get(name) == simdjson::SUCCESS)
							skeletons.push_back(ToLowerName(name));
					}
				}
				simdjson::dom::array drivers;
				if (skeletons.empty() || doc["drivers"].get(drivers) != simdjson::SUCCESS) {
					logger::warn("Correctives: {} needs 'skeleton' and 'drivers'.", entry.path().filename().string());
					continue;
				}
				size_t count = 0;
				for (auto d : drivers) {
					auto driver = ParseDriver(d, entry.path());
					if (!driver)
						continue;
					for (auto& s : skeletons) parsed[s].push_back(*driver);
					count++;
				}
				logger::info("Correctives: loaded {} drivers from {}.", count, entry.path().filename().string());
			}
		}

		std::unique_lock l{ lock };
		bySkeleton.clear();
		for (auto& [name, drivers] : parsed) {
			bySkeleton.emplace(name, std::make_shared<const std::vector<Driver>>(std::move(drivers)));
		}
		loaded = true;
		generation.fetch_add(1, std::memory_order_release);
	}

	std::shared_ptr<const std::vector<Driver>> DriverLibrary::Get(std::string_view a_skeleton)
	{
		{
			std::shared_lock l{ lock };
			if (loaded) {
				auto iter = bySkeleton.find(ToLowerName(a_skeleton));
				return iter != bySkeleton.end() ? iter->second : nullptr;
			}
		}
		Load();
		return Get(a_skeleton);
	}

	std::shared_ptr<DriverRuntime> DriverRuntime::Build(const Animation::OzzSkeleton& a_skeleton, std::shared_ptr<const std::vector<Driver>> a_drivers, uint64_t a_generation)
	{
		auto result = std::make_shared<DriverRuntime>();
		result->generation = a_generation;
		result->drivers = a_drivers;
		result->skeleton = a_skeleton.data.get();
		if (!a_drivers || !result->skeleton)
			return result;

		const auto& skeleton = *result->skeleton;
		const auto names = skeleton.joint_names();
		const auto parents = skeleton.joint_parents();
		const auto find = [&](const std::string& a_name) -> int32_t {
			for (size_t i = 0; i < names.size(); i++) {
				if (Util::String::CaseInsensitiveCompare(names[i], a_name))
					return static_cast<int32_t>(i);
			}
			return -1;
		};

		// Rest pose model transforms.
		const auto restSoa = skeleton.joint_rest_poses();
		std::vector<ozz::math::SoaTransform> soa(restSoa.begin(), restSoa.end());
		auto& restLocal = result->restLocal;
		restLocal.resize(skeleton.num_joints());
		Util::Ozz::UnpackSoaTransforms(soa, restLocal, &skeleton);
		std::vector<bool> all(skeleton.num_joints(), true);
		std::vector<ozz::math::Float4x4> restModel;
		ComputeModel(skeleton, all, [&](int32_t i) { return restLocal[i]; }, restModel);

		result->needed.assign(skeleton.num_joints(), false);
		for (const auto& d : *a_drivers) {
			Resolved r{ .driver = &d, .bone = find(d.bone), .tip = find(d.tip), .reference = find(d.reference) };
			if (r.bone < 0 || r.tip < 0 || r.reference < 0) {
				logger::warn("Correctives: skeleton '{}' lacks a bone of driver '{}' ({}, {}, {}); skipping it.", a_skeleton.name, d.name, d.bone, d.tip, d.reference);
				continue;
			}
			for (int32_t j : { r.bone, r.tip, r.reference }) {
				for (int32_t k = j; k >= 0 && !result->needed[k]; k = parents[k]) result->needed[k] = true;
			}

			// The bone's axis (towards its tip) and a vector square to it, in the bone's own frame at rest.
			const auto& restBone = restModel[r.bone];
			const auto inverseRestBone = ozz::math::Invert(restBone);
			const auto inverseRefRest = ozz::math::Invert(restModel[r.reference]);
			r.localAxis = Normalize(ToVec3(ozz::math::TransformVector(inverseRestBone, restModel[r.tip].cols[3] - restBone.cols[3])));
			{
				int best = 0;
				for (int k = 1; k < 3; k++) {
					if (std::abs(r.localAxis[k]) < std::abs(r.localAxis[best]))
						best = k;
				}
				Vec3 e{ 0.0f, 0.0f, 0.0f };
				e[best] = 1.0f;
				r.localSide = Normalize(Add(e, Scale(r.localAxis, -Dot(e, r.localAxis))));
			}

			// Rest, then each pose: the bone at rest turned by the pose's rotation (about model axes) and twisted about
			// its own axis, in the reference's rest frame.
			const auto inRefRest = [&](const Vec3& a_model) {
				return Normalize(ToVec3(ozz::math::TransformVector(inverseRefRest, ozz::math::simd_float4::Load(a_model[0], a_model[1], a_model[2], 0.0f))));
			};
			const Vec3 restAxis = TurnBy(restBone, r.localAxis), restSide = TurnBy(restBone, r.localSide);
			r.centers.push_back({ inRefRest(restAxis), inRefRest(restSide) });
			for (const auto& pose : d.poses) {
				const Vec3 axis = Normalize(RotateEuler(pose.rotation, restAxis));
				Vec3 side = Normalize(RotateEuler(pose.rotation, restSide));
				if (pose.twist != 0.0f)
					side = RotateAbout(axis, pose.twist * kDegToRad, side);
				r.centers.push_back({ inRefRest(axis), inRefRest(side) });
			}

			// Interpolation weights: Phi * C = W, where W says how much of each pose each center is (the rest pose's row
			// all zero, then the identity), so evaluating gives each pose's share rather than the morphs directly.
			const size_t n = r.centers.size(), m = d.poses.size();
			const float radius = d.radius * kDegToRad;
			std::vector<double> phi(n * n), w(n * m, 0.0);
			for (size_t i = 0; i < n; i++) {
				for (size_t j = 0; j < n; j++) phi[i * n + j] = Kernel(Distance({ r.centers[i].axis, r.centers[i].side }, { r.centers[j].axis, r.centers[j].side }, d.twistWeight), radius) + (i == j ? 1e-6 : 0.0);
			}
			for (size_t i = 1; i < n; i++) w[i * m + (i - 1)] = 1.0;
			if (!Solve(phi, w, n, m)) {
				logger::warn("Correctives: driver '{}' has poses too close together to tell apart; skipping it.", d.name);
				continue;
			}
			r.coefficients.resize(w.size());
			std::ranges::transform(w, r.coefficients.begin(), [](double a_value) { return static_cast<float>(a_value); });
			result->resolved.push_back(std::move(r));
		}
		return result;
	}

	void DriverRuntime::Evaluate(const LocalGetter& a_local, Values& a_out, std::vector<DriverDebug>* a_debug) const
	{
		if (resolved.empty())
			return;
		thread_local std::vector<ozz::math::Float4x4> model;
		ComputeModel(*skeleton, needed, a_local, model);

		std::map<std::string, float, std::less<>> totals;
		for (auto& [name, value] : a_out) totals[name] += value;
		if (a_debug)
			a_debug->clear();

		std::vector<float> kernel;
		for (const auto& r : resolved) {
			const auto& d = *r.driver;
			const auto inverseRef = ozz::math::Invert(model[r.reference]);
			const auto inRef = [&](const Vec3& a_local) {
				const auto turned = ozz::math::TransformVector(model[r.bone], ozz::math::simd_float4::Load(a_local[0], a_local[1], a_local[2], 0.0f));
				return Normalize(ToVec3(ozz::math::TransformVector(inverseRef, turned)));
			};
			const Measured now{ inRef(r.localAxis), inRef(r.localSide) };
			const float radius = d.radius * kDegToRad;
			kernel.resize(r.centers.size());
			for (size_t i = 0; i < r.centers.size(); i++) kernel[i] = Kernel(Distance(now, { r.centers[i].axis, r.centers[i].side }, d.twistWeight), radius);

			DriverDebug* debug = a_debug ? &a_debug->emplace_back() : nullptr;
			if (debug) {
				debug->name = d.name;
				debug->valid = true;
				const auto [swing, twist] = SwingTwist(now, { r.centers[0].axis, r.centers[0].side });
				debug->swingFromRest = swing * kRadToDeg;
				debug->twistFromRest = twist * kRadToDeg;
				for (size_t i = 1; i < r.centers.size(); i++) debug->poseDistances.push_back(Distance(now, { r.centers[i].axis, r.centers[i].side }, d.twistWeight) * kRadToDeg);
			}
			// Each pose's share. Shares below 0 are dropped, and shares totalling more than 1 are scaled down to 1: between
			// two poses the interpolation overshoots (about 0.55 each halfway), which would add up to more than either
			// pose's correction. At a pose its share is still exactly 1.
			const size_t poses = d.poses.size();
			thread_local std::vector<float> shares;
			shares.assign(poses, 0.0f);
			float total = 0.0f;
			for (size_t p = 0; p < poses; p++) {
				float share = 0.0f;
				for (size_t i = 0; i < r.centers.size(); i++) share += kernel[i] * r.coefficients[i * poses + p];
				shares[p] = std::max(share, 0.0f);
				total += shares[p];
			}
			if (total > 1.0f) {
				for (auto& share : shares) share /= total;
			}

			const size_t m = d.morphs.size();
			for (size_t k = 0; k < m; k++) {
				float value = 0.0f;
				for (size_t p = 0; p < poses; p++) value += shares[p] * d.poses[p].weights[k];
				value = std::clamp(value, d.minimum, d.maximum);
				totals[d.morphs[k]] += value;
				if (debug)
					debug->outputs.emplace_back(d.morphs[k], value);
			}
		}
		a_out.assign(totals.begin(), totals.end());
	}
}
