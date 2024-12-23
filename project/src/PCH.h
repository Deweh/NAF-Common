#pragma once
#define _USE_MATH_DEFINES

// c
#include <cassert>
#include <cctype>
#include <cerrno>
#include <cfenv>
#include <cfloat>
#include <cinttypes>
#include <climits>
#include <clocale>
#include <cmath>
#include <csetjmp>
#include <csignal>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cuchar>
#include <cwchar>
#include <cwctype>

// cxx
#include <algorithm>
#include <any>
#include <array>
#include <atomic>
#include <barrier>
#include <bit>
#include <bitset>
#include <charconv>
#include <chrono>
#include <compare>
#include <complex>
#include <concepts>
#include <condition_variable>
#include <deque>
#include <exception>
#include <execution>
#include <filesystem>
#include <format>
#include <forward_list>
#include <fstream>
#include <functional>
#include <future>
#include <initializer_list>
#include <iomanip>
#include <ios>
#include <iosfwd>
#include <iostream>
#include <istream>
#include <iterator>
#include <latch>
#include <limits>
#include <locale>
#include <map>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <new>
#include <numbers>
#include <numeric>
#include <optional>
#include <ostream>
#include <queue>
#include <random>
#include <ranges>
#include <ratio>
#include <regex>
#include <scoped_allocator>
#include <semaphore>
#include <set>
#include <shared_mutex>
#include <source_location>
#include <span>
#include <sstream>
#include <stack>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <string_view>
#include <syncstream>
#include <system_error>
#include <thread>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <valarray>
#include <variant>
#include <vector>
#include <version>
#include "config.h"

#pragma warning(push)
//CLib
#if defined TARGET_GAME_F4
	#include "F4SE/F4SE.h"
	#include "RE/Fallout.h"
	#define xSE F4SE
	constexpr uint8_t FACE_MORPHS_SIZE = 54;
#elif defined TARGET_GAME_SF
	#include "SFSE/SFSE.h"
	#include "RE/Starfield.h"
	#define xSE SFSE
	constexpr uint8_t FACE_MORPHS_SIZE = 104;
#endif
#include "RE/CommonMisc.h"

#if defined TARGET_GAME_F4
	using GraphEventProcessor = RE::BShkbAnimationGraph;
	using NiSkeletonRootNode = RE::NiNode;
#elif defined TARGET_GAME_SF
	using GraphEventProcessor = void;
	using NiSkeletonRootNode = RE::BGSFadeNode;
#endif

namespace
{
	template <typename T>
	constexpr T TargetGameVal(T a_fo4Val, T a_sfVal)
	{
#if defined TARGET_GAME_F4
		return a_fo4Val;
#elif defined TARGET_GAME_SF
		return a_sfVal;
#endif
	}

	constexpr bool IsFO4()
	{
#ifdef TARGET_GAME_F4
		return true;
#else
		return false;
#endif
	}

	constexpr bool IsSF()
	{
#ifdef TARGET_GAME_SF
		return true;
#else
		return false;
#endif
	}
}

#include <spdlog/sinks/basic_file_sink.h>
#pragma warning(pop)

#define DLLEXPORT __declspec(dllexport)

namespace logger = xSE::log;
using namespace std::literals;

//Other dependencies
#include "fastgltf/core.hpp"
#include "fastgltf/types.hpp"
#include "fastgltf/tools.hpp"
#include "ozz/animation/runtime/animation.h"
#include "ozz/animation/runtime/animation_utils.h"
#include "ozz/animation/runtime/blending_job.h"
#include "ozz/animation/runtime/ik_two_bone_job.h"
#include "ozz/animation/runtime/ik_aim_job.h"
#include "ozz/animation/runtime/local_to_model_job.h"
#include "ozz/animation/runtime/sampling_job.h"
#include "ozz/animation/runtime/skeleton.h"
#include "ozz/animation/runtime/skeleton_utils.h"
#include "ozz/animation/runtime/track.h"
#include "ozz/animation/runtime/track_sampling_job.h"
#include "ozz/animation/runtime/track_triggering_job.h"
#include "ozz/base/maths/soa_transform.h"
#include "ozz/base/maths/simd_math.h"
#include "ozz/base/maths/transform.h"
#include "ozz/base/maths/soa_float4x4.h"
#include "ozz/base/maths/soa_quaternion.h"
#include "ozz/base/maths/simd_quaternion.h"
#include "ozz/base/maths/vec_float.h"
#include "ozz/animation/offline/additive_animation_builder.h"
#include "ozz/animation/offline/animation_builder.h"
#include "ozz/animation/offline/animation_optimizer.h"
#include "ozz/animation/offline/raw_animation.h"
#include "ozz/animation/offline/raw_animation_utils.h"
#include "ozz/animation/offline/raw_skeleton.h"
#include "ozz/animation/offline/raw_track.h"
#include "ozz/animation/offline/skeleton_builder.h"
#include "ozz/animation/offline/track_builder.h"
#include "ozz/animation/offline/track_optimizer.h"