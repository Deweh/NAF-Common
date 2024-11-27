#pragma once

#ifndef M_PI
#	define M_PI 3.14159265358979323846
#endif

namespace Util
{
	inline static constexpr float DEGREE_TO_RADIAN{ M_PI / 180.0f };
	inline static constexpr float RADIAN_TO_DEGREE{ 180.0f / M_PI };

	float NormalizeSpan(float begin, float end, float val);
}