#pragma once

// Game queries whose API differs between Fallout 4 and Starfield.
namespace Util::Game
{
	[[nodiscard]] RE::NiAVObject* Get3D(RE::TESObjectREFR* a_ref);

	template <class T = RE::TESForm>
	[[nodiscard]] T* LookupForm(RE::TESFormID a_formId)
	{
#if defined TARGET_GAME_F4
		return RE::TESForm::GetFormByID<T>(a_formId);
#elif defined TARGET_GAME_SF
		return RE::TESForm::LookupByID<T>(a_formId);
#endif
	}

	// A form ID relative to the plugin that defines it, so it survives load order changes.
	struct PluginFormID
	{
		std::string plugin;
		RE::TESFormID localFormId{ 0 };
	};

	// Nothing for forms created at runtime.
	[[nodiscard]] std::optional<PluginFormID> GetPluginFormID(RE::TESFormID a_formId);
	// 0 if the plugin isn't loaded.
	[[nodiscard]] RE::TESFormID ResolvePluginFormID(RE::TESFormID a_localFormId, std::string_view a_plugin);

	[[nodiscard]] RE::TESObjectREFR* GetConsoleSelectedRef();
}
