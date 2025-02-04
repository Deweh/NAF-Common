#pragma once

namespace Serialization
{
	class BlendGraphSchemaExport
	{
	public:
		static bool SaveSchemaJSON(const std::filesystem::path& a_filePath);
	};
}