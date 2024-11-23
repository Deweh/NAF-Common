#include "General.h"
#include "String.h"

namespace Util
{
	static std::mt19937 rand_engine;
	static std::mutex rand_lock;

	bool GetJointIndexes(const ozz::animation::Skeleton* a_skeleton, const std::span<std::string_view>& a_targetNames, const std::span<int32_t>& a_indexesOut)
	{
		if (a_targetNames.size() != a_indexesOut.size())
			return false;

		for (auto& i : a_indexesOut) {
			i = -1;
		}

		const auto jointNames = a_skeleton->joint_names();
		for (auto iter = jointNames.begin(); iter != jointNames.end(); iter++) {
			for (size_t i = 0; i < a_targetNames.size(); i++) {
				if (Util::String::CaseInsensitiveCompare(a_targetNames[i], *iter)) {
					a_indexesOut[i] = std::distance(jointNames.begin(), iter);
				}
			}
		}

		for (size_t i = 0; i < a_targetNames.size(); i++) {
			if (a_indexesOut[i] < 0) {
				return false;
			}
		}
		return true;
	}

	int GetRandomInt(int a_min, int a_max)
	{
		std::unique_lock l{ rand_lock };
		std::uniform_int_distribution<int> dist(a_min, a_max);
		return dist(rand_engine);
	}

	float GetRandomFloat(float a_min, float a_max)
	{
		std::unique_lock l{ rand_lock };
		std::uniform_real_distribution<float> dist(a_min, a_max);
		return dist(rand_engine);
	}
}