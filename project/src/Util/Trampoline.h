#pragma once

namespace Util::Trampoline
{
	struct DetourHook
	{
		uintptr_t* resultPtr;
		uintptr_t hookPtr;
		size_t relID;
		const char* name;
	};

	void AddDetourHook(const DetourHook& hook);
	void AddHook(size_t bytesAlloc, const std::function<void(xSE::Trampoline&, uintptr_t)>& func);
	void ProcessHooks();
}