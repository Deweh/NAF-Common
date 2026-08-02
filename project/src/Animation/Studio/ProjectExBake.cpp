#include "ProjectEx.h"

#include "StudioMath.h"

namespace Animation::Studio
{
	// ---- Baking ----

	size_t ProjectEx::GetBakeSampleCount(int32_t a_frameCount, float a_frameRate, float a_sampleRate)
	{
		if (!(a_frameRate > 0.0f) || !(a_sampleRate > 0.0f) || !std::isfinite(a_sampleRate)) return 0;
		// The epsilon keeps float error from adding a sample just before the end.
		const double duration = a_frameCount / static_cast<double>(a_frameRate);
		return static_cast<size_t>((std::max)(std::ceil(duration * a_sampleRate - 1e-4), 1.0)) + 1;
	}

	std::expected<ProjectEx::BakeJob, std::string> ProjectEx::BeginBake(ObjectId a_actor, float a_sampleRate) const
	{
		constexpr size_t kMaxSamples = 100000;

		const auto sequence = GetActiveSequence();
		if (!sequence || !(sequence->frameRate > 0.0f)) return std::unexpected("There's no sequence to bake.");
		const auto actor = FindObject<ActorObject>(a_actor);
		if (!actor) return std::unexpected("That actor isn't in the project.");
		if (!actor->actor || !actor->skeleton || !actor->pose) return std::unexpected(std::format("{} must be bound to an actor.", actor->name));
		if (!std::isfinite(a_sampleRate) || !(a_sampleRate > 0.0f)) return std::unexpected("The sample rate must be above 0.");

		const double duration = sequence->frameCount / static_cast<double>(sequence->frameRate);
		const size_t sampleCount = GetBakeSampleCount(sequence->frameCount, sequence->frameRate, a_sampleRate);
		if (sampleCount > kMaxSamples) return std::unexpected(std::format("That would be {} samples; the most is {}.", sampleCount, kMaxSamples));

		BakeJob job;
		job.actor = a_actor;
		job.skeleton = actor->skeleton;
		job.sampleCount = sampleCount;
		job.sequence = sequence->id;
		job.revision = m_revision;
		job.sampleRate = a_sampleRate;
		job.animation = std::make_unique<RawOzzAnimation>();
		auto& raw = job.animation->data = ozz::make_unique<ozz::animation::offline::RawAnimation>();
		raw->duration = static_cast<float>(duration);
		raw->name = sequence->name.c_str();
		raw->tracks.resize(actor->skeleton->data->num_joints());
		for (auto& track : raw->tracks) {
			track.translations.reserve(job.sampleCount);
			track.rotations.reserve(job.sampleCount);
			track.scales.reserve(job.sampleCount);
		}
		// Overrides only hold bones that differ from the skeleton's default mask.
		const auto names = actor->skeleton->data->joint_names();
		for (const auto& [bone, masked] : actor->maskOverrides) {
			if (bone < 0 || bone >= static_cast<int32_t>(names.size())) continue;
			(masked ? job.animation->maskedBones : job.animation->unmaskedBones).emplace_back(names[bone]);
		}
		return job;
	}

	std::expected<bool, std::string> ProjectEx::ContinueBake(BakeJob& a_job, std::chrono::microseconds a_budget)
	{
		if (a_job.baked >= a_job.sampleCount) return true;
		const auto sequence = GetActiveSequence();
		if (IsEditing() || m_revision != a_job.revision || !sequence || sequence->id != a_job.sequence) {
			return std::unexpected("The project changed while baking.");
		}
		const auto actor = FindObject<ActorObject>(a_job.actor);
		if (!actor || !actor->pose || actor->skeleton != a_job.skeleton) return std::unexpected("The actor was unbound while baking.");

		// Brings the roots up to date. They stay put while sampling: nothing below moves an actor's placement.
		Evaluate();
		if (!actor->hasRoot) return std::unexpected(std::format("{} isn't loaded.", actor->name));

		// Sampling overwrites live values, so they're put back afterwards. The frame and playback time aren't
		// touched: m_bakeFrame stands in for them.
		std::vector<DiscardedValue> liveValues;
		CaptureAll(liveValues);
		const auto deadline = std::chrono::steady_clock::now() + a_budget;
		const double duration = sequence->frameCount / static_cast<double>(sequence->frameRate);
		do {
			const bool last = a_job.baked + 1 == a_job.sampleCount;
			const double time = last ? duration : a_job.baked / a_job.sampleRate;
			m_bakeFrame = last ? static_cast<float>(sequence->frameCount) : static_cast<float>(time * sequence->frameRate);
			ResampleAll();
			m_evaluator.Solve(false);
			AppendBakeSample(a_job, *actor, static_cast<float>(time));
			++a_job.baked;
		} while (a_job.baked < a_job.sampleCount && std::chrono::steady_clock::now() < deadline);
		m_bakeFrame.reset();
		RestoreDiscarded(liveValues);
		m_evaluator.MarkSolveNeeded();
		return a_job.baked >= a_job.sampleCount;
	}

	void ProjectEx::AppendBakeSample(BakeJob& a_job, const ActorObject& a_actor, float a_time)
	{
		auto& tracks = a_job.animation->data->tracks;
		const auto& pose = a_actor.pose->Back();
		for (size_t soa = 0; soa < pose.size(); ++soa) {
			float t[3][4], r[4][4], s[3][4];
			const auto& transform = pose[soa];
			ozz::math::StorePtrU(transform.translation.x, t[0]), ozz::math::StorePtrU(transform.translation.y, t[1]), ozz::math::StorePtrU(transform.translation.z, t[2]);
			ozz::math::StorePtrU(transform.rotation.x, r[0]), ozz::math::StorePtrU(transform.rotation.y, r[1]), ozz::math::StorePtrU(transform.rotation.z, r[2]), ozz::math::StorePtrU(transform.rotation.w, r[3]);
			ozz::math::StorePtrU(transform.scale.x, s[0]), ozz::math::StorePtrU(transform.scale.y, s[1]), ozz::math::StorePtrU(transform.scale.z, s[2]);
			for (size_t lane = 0; lane < 4; ++lane) {
				const size_t joint = soa * 4 + lane;
				if (joint >= tracks.size()) break;
				auto& track = tracks[joint];
				auto rotation = Normalized(ozz::math::Quaternion{ r[0][lane], r[1][lane], r[2][lane], r[3][lane] });
				// Keep each key in the previous one's hemisphere, so interpolating between samples takes the short way.
				if (!track.rotations.empty()) {
					const auto& previous = track.rotations.back().value;
					if (previous.x * rotation.x + previous.y * rotation.y + previous.z * rotation.z + previous.w * rotation.w < 0.0f) rotation = -rotation;
				}
				track.translations.push_back({ a_time, { t[0][lane], t[1][lane], t[2][lane] } });
				track.rotations.push_back({ a_time, rotation });
				track.scales.push_back({ a_time, { s[0][lane], s[1][lane], s[2][lane] } });
			}
		}
	}
}
