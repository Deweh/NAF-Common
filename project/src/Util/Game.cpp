#include "Util/Game.h"

#include "Settings/Impl.h"
#include "Util/String.h"

namespace Util::Game
{
	RE::NiAVObject* Get3D(RE::TESObjectREFR* a_ref)
	{
		if (!a_ref) return nullptr;
#if defined TARGET_GAME_F4
		return a_ref->Get3D();
#elif defined TARGET_GAME_SF
		const auto loadedData = a_ref->loadedData.LockRead();
		return *loadedData ? (*loadedData)->data3D.get() : nullptr;
#endif
	}

#if defined TARGET_GAME_F4
	std::optional<PluginFormID> GetPluginFormID(RE::TESFormID a_formId)
	{
		const auto form = a_formId != 0 ? RE::TESForm::GetFormByID(a_formId) : nullptr;
		const auto file = form && (a_formId >> 24) != 0xFF ? form->GetFile(0) : nullptr;
		if (!file) return std::nullopt;
		return PluginFormID{ std::string(file->GetFilename()), (a_formId >> 24) == 0xFE ? a_formId & 0xFFF : a_formId & 0xFFFFFF };
	}

	RE::TESFormID ResolvePluginFormID(RE::TESFormID a_localFormId, std::string_view a_plugin)
	{
		const auto dataHandler = RE::TESDataHandler::GetSingleton();
		const auto form = dataHandler ? dataHandler->LookupForm(a_localFormId, a_plugin) : nullptr;
		return form ? form->GetFormID() : 0;
	}
#elif defined TARGET_GAME_SF
	namespace
	{
		// Starfield packs full plugins into the top byte, medium ones under 0xFD and small ones under 0xFE.
		RE::TESFormID GetLocalFormIdMask(const RE::TESFile& a_file)
		{
			if (a_file.fileFlags.any(RE::TESFile::Flags::kSmall)) return 0xFFF;
			if (a_file.fileFlags.any(RE::TESFile::Flags::kMedium)) return 0xFFFF;
			return 0xFFFFFF;
		}

		RE::TESFormID GetFileFormIdPrefix(const RE::TESFile& a_file)
		{
			const auto& index = a_file.fileIndex;
			if (a_file.fileFlags.any(RE::TESFile::Flags::kSmall)) return 0xFE000000 | (static_cast<RE::TESFormID>(index.smallIndex) << 12);
			if (a_file.fileFlags.any(RE::TESFile::Flags::kMedium)) return 0xFD000000 | (static_cast<RE::TESFormID>(index.mediumIndex) << 16);
			return static_cast<RE::TESFormID>(index.fullIndex) << 24;
		}

		template <class F>
		const RE::TESFile* FindFile(F&& a_predicate)
		{
			const auto dataHandler = RE::TESDataHandler::GetSingleton();
			if (!dataHandler) return nullptr;
			for (const auto file : dataHandler->files) {
				if (file && a_predicate(*file)) return file;
			}
			return nullptr;
		}
	}

	std::optional<PluginFormID> GetPluginFormID(RE::TESFormID a_formId)
	{
		if (a_formId == 0 || (a_formId >> 24) == 0xFF) return std::nullopt;
		const auto file = FindFile([&](const RE::TESFile& a_file) {
			return (a_formId & ~GetLocalFormIdMask(a_file)) == GetFileFormIdPrefix(a_file);
		});
		if (!file) return std::nullopt;
		return PluginFormID{ std::string(file->fileName), a_formId & GetLocalFormIdMask(*file) };
	}

	RE::TESFormID ResolvePluginFormID(RE::TESFormID a_localFormId, std::string_view a_plugin)
	{
		const auto file = FindFile([&](const RE::TESFile& a_file) {
			return Util::String::CaseInsensitiveCompare(a_file.fileName, a_plugin);
		});
		if (!file) return 0;
		return GetFileFormIdPrefix(*file) | (a_localFormId & GetLocalFormIdMask(*file));
	}
#endif

	RE::TESObjectREFR* GetConsoleSelectedRef()
	{
#if defined TARGET_GAME_F4
		return RE::Console::GetPickRef().get().get();
#elif defined TARGET_GAME_SF
		// Starfield's console isn't mapped by CommonLibSF; the plugin supplies this.
		const auto func = Settings::GetImplFunctions().GetConsoleSelectedRef;
		return func ? func() : nullptr;
#endif
	}
}
