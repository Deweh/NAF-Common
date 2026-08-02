#pragma once
#include "Animation/Ozz.h"

#include <expected>

namespace Serialization
{
	class GLTFExport
	{
	public:
		// level 0 keeps every key. Higher levels reduce keys with the ozz optimizer, at rising tolerances.
		static std::vector<std::byte> CreateOptimizedAsset(Animation::RawOzzAnimation* anim, const ozz::animation::Skeleton* skeleton, uint8_t level = 0);
		// CreateOptimizedAsset, written to a_path as a gzip-compressed .glb. Doesn't touch game state, so it can run on any thread
		// once Settings::GetFaceMorphs() has been initialized.
		static std::expected<void, std::string> ExportToFile(const std::filesystem::path& a_path, Animation::RawOzzAnimation* anim, const ozz::animation::Skeleton* skeleton, uint8_t level = 0);
	};
}