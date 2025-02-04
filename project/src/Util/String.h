#pragma once

namespace Util
{
	class String
	{
	public:
		struct CaseInsensitiveLess
		{
			inline bool compare(const std::string_view& a_lhs, const std::string_view& a_rhs) const
			{
				return std::lexicographical_compare(a_lhs.begin(), a_lhs.end(), a_rhs.begin(), a_rhs.end(),
					[](const unsigned char a_lhs, const unsigned char a_rhs) {
						return std::tolower(a_lhs) < std::tolower(a_rhs);
					});
			}

			inline bool operator()(const std::string& a_lhs, const std::string& a_rhs) const
			{
				return compare(a_lhs, a_rhs);
			}

			inline bool operator()(const std::string_view& a_lhs, const std::string_view& a_rhs) const
			{
				return compare(a_lhs, a_rhs);
			}
		};

		static const std::filesystem::path& GetGamePath();
		static const std::filesystem::path& GetDataPath();
		static const std::filesystem::path& GetPluginPath();
		static std::string ToLower(const std::string_view s);
		static std::string_view TransformToLower(std::string& s);
		static bool CaseInsensitiveCompare(const std::string_view& s1, const std::string_view& s2);
		static std::vector<std::string_view> Split(const std::string_view& s, const std::string_view& delimiter, const std::optional<char>& escapeChar = std::nullopt);
		static std::optional<uint32_t> HexToUInt(const std::string_view& s);
		static std::optional<int32_t> StrToInt(const std::string_view& s);
		static std::optional<float> StrToFloat(const std::string_view& s);
	};
}