#pragma once
// Before Renderer.h, which undefines the "far" these headers use.
#include <d3d12.h>
#include <dxgi1_4.h>

#include "UI/Renderer.h"

namespace UI
{
	// Starfield's renderer. The game's swap chain isn't mapped, so the D3D12 and DXGI functions are hooked
	// directly; the swap chain is picked up the first time it presents, and again whenever the game replaces it.
	class DX12Renderer : public Renderer
	{
	public:
		static DX12Renderer* GetSingleton();

		// Best called while the plugin loads, before the game creates its swap chain, so the queue the swap chain
		// presents on is known for certain. Later works too: the queue is then taken from the game's submissions.
		void Install() override;

		void OnCreateSwapChain(IUnknown* a_device, IDXGISwapChain1* a_swapChain);
		void OnExecuteCommandLists(ID3D12CommandQueue* a_queue);
		void OnPresent(IDXGISwapChain* a_swapChain);
		void OnResizeBuffers(IDXGISwapChain* a_swapChain);

	private:
		// The command allocators and ImGui's vertex buffers are reused after this many frames. Each frame waits
		// for the GPU to finish with them first.
		static constexpr std::uint32_t kFramesInFlight = 3;

		struct FrameContext
		{
			ID3D12CommandAllocator* allocator{ nullptr };
			std::uint64_t fenceValue{ 0 };
		};

		bool Initialize(IDXGISwapChain3* a_swapChain, const DXGI_SWAP_CHAIN_DESC& a_desc);
		bool BindSwapChain(IDXGISwapChain3* a_swapChain, const DXGI_SWAP_CHAIN_DESC& a_desc);
		bool EnsureRtvHeap(UINT a_bufferCount);
		void WaitForGpu();
		ID3D12CommandQueue* FindQueue(IDXGISwapChain* a_swapChain);

		std::mutex m_queueLock;
		std::vector<std::pair<IDXGISwapChain*, ID3D12CommandQueue*>> m_swapChainQueues;
		std::atomic<ID3D12CommandQueue*> m_submittedQueue{ nullptr };
		std::atomic_bool m_failed{ false };
		std::atomic_bool m_swapChainCreated{ false };  // A swap chain was made since the last bind; it may reuse an old address.

		ID3D12Device* m_device{ nullptr };
		ID3D12CommandQueue* m_commandQueue{ nullptr };
		// Not referenced, and neither are its buffers: DXGI won't let the game make a new swap chain for its window while
		// the old one is still alive, which it does when e.g. frame generation is toggled.
		IDXGISwapChain3* m_swapChain{ nullptr };
		DXGI_FORMAT m_format{ DXGI_FORMAT_UNKNOWN };

		ID3D12DescriptorHeap* m_rtvHeap{ nullptr };
		ID3D12DescriptorHeap* m_srvHeap{ nullptr };
		std::uint32_t m_rtvDescriptorSize{ 0 };

		FrameContext m_frames[kFramesInFlight]{};
		std::uint64_t m_frameIndex{ 0 };
		ID3D12GraphicsCommandList* m_commandList{ nullptr };
		ID3D12Fence* m_fence{ nullptr };
		HANDLE m_fenceEvent{ nullptr };
		std::uint64_t m_fenceValue{ 0 };
	};
}
