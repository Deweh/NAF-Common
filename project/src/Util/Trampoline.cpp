#include "Trampoline.h"
#include <Windows.h>
#include "detours/detours.h"

namespace Util::Trampoline
{
	struct HookData
	{
		size_t totalAlloc = 0;
		std::vector<std::function<void(xSE::Trampoline&, uintptr_t)>> hookFuncs;
		std::vector<DetourHook> detourHooks;
	};

	 std::unique_ptr<HookData>& GetTempData()
	{
		static std::unique_ptr<HookData> tempData{ std::make_unique<HookData>() };
		return tempData;
	}

	void AddDetourHook(const DetourHook& hook)
	{
		auto& tempData = GetTempData();
		tempData->detourHooks.push_back(hook);
	}

	void AddHook(size_t bytesAlloc, const std::function<void(xSE::Trampoline&, uintptr_t)>& func)
	{
		auto& tempData = GetTempData();
		tempData->totalAlloc += bytesAlloc;
		tempData->hookFuncs.push_back(func);
	}

	void ProcessHooks()
	{
		auto& tempData = GetTempData();
		uintptr_t baseAddr = REL::Module::get().base();

		if (!tempData->detourHooks.empty()) {
			DetourTransactionBegin();
			DetourUpdateThread(GetCurrentThread());
			for (auto& h : tempData->detourHooks) {
				uintptr_t addr = REL::Relocation<uintptr_t>{ REL::ID(h.relID) }.address();
				(*h.resultPtr) = addr;
				if (DetourAttach(reinterpret_cast<PVOID*>(h.resultPtr), reinterpret_cast<PVOID>(h.hookPtr)) == NO_ERROR) {
					logger::info("Applied {} detour hook at {:X}", h.name, addr - baseAddr);
				} else {
					logger::warn("Failed to apply {} detour hook.", h.name);
				}
			}
			DetourTransactionCommit();
		}

		if (tempData->totalAlloc > 0) {
			xSE::AllocTrampoline(tempData->totalAlloc);
		}
		auto& t = xSE::GetTrampoline();
		for (auto& f : tempData->hookFuncs) {
			f(t, baseAddr);
		}
		tempData.reset();
	}
}