#include "String.h"

namespace Util
{
	namespace detail
	{
		inline bool CaseInsensitiveCharCompare(char c1, char c2)
		{
			return std::tolower(static_cast<unsigned char>(c1)) ==
			       std::tolower(static_cast<unsigned char>(c2));
		}
	}

	const std::filesystem::path& String::GetGamePath()
	{
		static std::optional<std::filesystem::path> BasePath = std::nullopt;

		if (!BasePath) {
#if defined TARGET_GAME_F4
			std::filesystem::path p = REL::Module::get().filePath();
#elif defined TARGET_GAME_SF
			std::filesystem::path p = REX::W32::GetProcPath(nullptr);
#endif
			BasePath = p.parent_path();
		}

		return *BasePath;
	}

	const std::filesystem::path& String::GetDataPath()
	{
		static std::optional<std::filesystem::path> DataPath = std::nullopt;

		if (!DataPath) {
			DataPath = GetGamePath() / "Data" / "NAF";
		}

		return *DataPath;
	}

	const std::filesystem::path& String::GetPluginPath()
	{
		static std::optional<std::filesystem::path> PluginPath = std::nullopt;

		if (!PluginPath) {
			PluginPath = GetGamePath() / "Data" / "SFSE" / "Plugins";
		}

		return *PluginPath;
	}

	std::string String::ToLower(const std::string_view s)
	{
		std::string result(s);
		std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return std::tolower(c); });
		return result;
	}

	std::string_view String::TransformToLower(std::string& s)
	{
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
		return s;
	}

	bool String::CaseInsensitiveCompare(const std::string_view& s1, const std::string_view& s2)
	{
		return std::equal(s1.begin(), s1.end(),
			s2.begin(), s2.end(),
			[](char a, char b) {
				return tolower(a) == tolower(b);
			});
	}

	std::vector<std::string_view> String::Split(const std::string_view& s, const std::string_view& delimiter, const std::optional<char>& escapeChar)
	{
		std::vector<std::string_view> substrings;
		size_t start = 0;
		size_t end = 0;
		bool escaped = false;

		while (end < s.length()) {
			if (escapeChar.has_value() && s[end] == escapeChar) {
				escaped = !escaped;
			} else if (!escaped && s.substr(end, delimiter.length()) == delimiter) {
				substrings.push_back(s.substr(start, end - start));
				start = end + delimiter.length();
				end = start - 1;
			}
			end++;
		}

		if (start < s.length()) {
			substrings.push_back(s.substr(start));
		}

		if (escapeChar.has_value()) {
			for (auto& substring : substrings) {
				if (substring.size() > 1 && substring.front() == escapeChar.value() && substring.back() == escapeChar.value()) {
					substring.remove_prefix(1);
					substring.remove_suffix(1);
				}
			}
		}

		return substrings;
	}

	std::optional<uint32_t> String::HexToUInt(const std::string_view& s)
	{
		uint32_t value;
		const char* start = s.data();
		std::from_chars_result result = std::from_chars(start, start + s.size(), value, 16);

		if (result.ec != std::errc::invalid_argument && result.ec != std::errc::result_out_of_range) {
			return value;
		} else {
			return std::nullopt;
		}
	}

	std::optional<int32_t> String::StrToInt(const std::string_view& s)
	{
		int32_t value;
		const char* start = s.data();
		std::from_chars_result result = std::from_chars(start, start + s.size(), value);

		if (result.ec != std::errc::invalid_argument && result.ec != std::errc::result_out_of_range) {
			return value;
		} else {
			return std::nullopt;
		}
	}

	std::optional<float> String::StrToFloat(const std::string_view& s)
	{
		float value;
		const char* start = s.data();
		std::from_chars_result result = std::from_chars(start, start + s.size(), value);

		if (result.ec != std::errc::invalid_argument && result.ec != std::errc::result_out_of_range) {
			return value;
		} else {
			return std::nullopt;
		}
	}
}