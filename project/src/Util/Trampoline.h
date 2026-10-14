#pragma once

namespace Util::Trampoline
{
#if defined TARGET_GAME_F4
	using TrampolineT = F4SE::Trampoline;
#elif defined TARGET_GAME_SF
	using TrampolineT = REL::Trampoline;
#endif

	struct DetourHook
	{
		uintptr_t* resultPtr;
		uintptr_t hookPtr;
		size_t relID;
		const char* name;
	};

	void AddDetourHook(const DetourHook& hook);
	void AddHook(size_t bytesAlloc, const std::function<void(TrampolineT&, uintptr_t)>& func);
	void ProcessHooks();
}