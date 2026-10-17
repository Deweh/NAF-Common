#pragma once
#include "Values.h"

namespace Animation
{
	struct OzzSkeleton;
}

// Pose-space corrective drivers: morph values driven by how a bone is turned relative to another, like Unreal's Pose
// Driver or Maya's pose interpolator. Defined in Data/NAF/Correctives/*.json:
//
//   {
//     "skeleton": "HumanRace",               (or "skeletons": [...]; race EditorIDs, as NAF names skeletons)
//     "drivers": [ {
//       "name": "Left hip",
//       "bone": "LLeg_Thigh",                the bone whose rotation drives the morphs
//       "tip": "LLeg_Calf",                  the bone's axis runs from it to this bone; twist is about that axis
//       "reference": "Pelvis",               measured in this bone's space, so moving the reference doesn't count
//       "radius": 60,                        degrees; how far a pose's influence reaches (default 60)
//       "twist": 1.0,                        how much twist counts against swing (default 1; 0 ignores twist)
//       "range": [0, 1],                     the outputs are clamped to this (default [0, 1])
//       "poses": [ {
//         "name": "Side split",
//         "rotation": [0, 80, 0],            degrees, about the character's X (right), Y (forward) and Z (up) axes,
//                                            applied to the bone at rest in that order (default [0, 0, 0])
//         "twist": 0,                        degrees, then turned about its own axis (default 0)
//         "morphs": { "NAF_SideSplit_L": 1.0 }
//       } ]
//     } ]
//   }
//
// A pose is a full orientation of the bone in the reference's space. The distance between two orientations combines
// swing (the angle between the bone's axes) and twist (the turn about the axis that's left), as
// sqrt(swing^2 + (twist weight * twist)^2). The rest pose is an implicit pose with every morph at 0. Between poses,
// values are interpolated with Gaussian radial basis functions of that distance, so they're exact at each pose and fade
// out away from all of them. Where several drivers drive the same morph, their values add up.
namespace Morph
{
	struct DriverPose
	{
		std::string name;
		std::array<float, 3> rotation{ 0.0f, 0.0f, 0.0f };
		float twist = 0.0f;
		std::vector<float> weights;  // Parallel to Driver::morphs.
	};

	struct Driver
	{
		std::string name;
		std::string bone;
		std::string tip;
		std::string reference;
		float radius = 60.0f;
		float twistWeight = 1.0f;
		float minimum = 0.0f;
		float maximum = 1.0f;
		std::vector<std::string> morphs;  // Lowercase.
		std::vector<DriverPose> poses;
	};

	// The drivers read from Data/NAF/Correctives, by skeleton. Owned by Manager (Manager::GetDrivers).
	class DriverLibrary
	{
	public:
		// Reads every file in Data/NAF/Correctives. Bumps the generation.
		void Load();
		// The drivers for a skeleton (by name, case-insensitive), or null if none. Loads on first use.
		[[nodiscard]] std::shared_ptr<const std::vector<Driver>> Get(std::string_view a_skeleton);
		[[nodiscard]] uint64_t GetGeneration() const { return generation.load(std::memory_order_acquire); }

	private:
		std::shared_mutex lock;
		std::map<std::string, std::shared_ptr<const std::vector<Driver>>, std::less<>> bySkeleton;
		std::atomic<uint64_t> generation{ 0 };
		bool loaded = false;
	};

	// One driver's state at its last evaluation, for the console.
	struct DriverDebug
	{
		std::string name;
		bool valid = false;             // False if a bone wasn't found.
		float swingFromRest = 0.0f;     // Degrees.
		float twistFromRest = 0.0f;     // Degrees.
		std::vector<float> poseDistances;  // Degrees (swing and weighted twist combined) to each pose.
		Values outputs;
	};

	// A skeleton's drivers resolved against it: joint indices, each pose's direction and the interpolation weights.
	class DriverRuntime
	{
	public:
		using LocalGetter = std::function<ozz::math::Float4x4(int32_t)>;

		static std::shared_ptr<DriverRuntime> Build(const Animation::OzzSkeleton& a_skeleton, std::shared_ptr<const std::vector<Driver>> a_drivers, uint64_t a_generation);

		// a_local returns a joint's current local transform. Adds each driven morph's value to a_out (summed per name).
		void Evaluate(const LocalGetter& a_local, Values& a_out, std::vector<DriverDebug>* a_debug = nullptr) const;
		[[nodiscard]] uint64_t GetGeneration() const { return generation; }
		[[nodiscard]] bool Empty() const { return resolved.empty(); }
		[[nodiscard]] const ozz::animation::Skeleton* GetSkeleton() const { return skeleton; }
		// A joint's rest local transform, for joints the actor's model doesn't have.
		[[nodiscard]] const ozz::math::Float4x4& GetRestLocal(int32_t a_joint) const { return restLocal[a_joint]; }

	private:
		// An orientation of the bone in the reference's space: its axis and a unit vector square to it, which turns with
		// twist.
		struct Orientation
		{
			std::array<float, 3> axis;
			std::array<float, 3> side;
		};

		struct Resolved
		{
			const Driver* driver = nullptr;
			int32_t bone = -1;
			int32_t tip = -1;
			int32_t reference = -1;
			std::array<float, 3> localAxis;      // The axis and side vector in the bone's own frame.
			std::array<float, 3> localSide;
			std::vector<Orientation> centers;    // 0 is rest, then each pose.
			std::vector<float> coefficients;     // centers x morphs, row-major.
		};

		const ozz::animation::Skeleton* skeleton = nullptr;
		std::shared_ptr<const std::vector<Driver>> drivers;
		std::vector<Resolved> resolved;
		std::vector<bool> needed;  // Joints whose model transform an evaluation needs.
		std::vector<ozz::math::Float4x4> restLocal;
		uint64_t generation = 0;
	};
}
