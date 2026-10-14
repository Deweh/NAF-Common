#include "DX12Renderer.h"

// Only Starfield renders with D3D12.
#if defined TARGET_GAME_SF
#include <detours/detours.h>

#include "imgui/imgui_impl_dx12.h"

namespace UI
{
	namespace
	{
		using Present_t = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
		using Present1_t = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
		using ResizeBuffers_t = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
		using ResizeBuffers1_t = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain3*, UINT, UINT, UINT, DXGI_FORMAT, UINT, const UINT*, IUnknown* const*);
		using CreateSwapChainForHwnd_t = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**);
		using ExecuteCommandLists_t = void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);

		Present_t                g_originalPresent{ nullptr };
		Present1_t               g_originalPresent1{ nullptr };
		ResizeBuffers_t          g_originalResizeBuffers{ nullptr };
		ResizeBuffers1_t         g_originalResizeBuffers1{ nullptr };
		CreateSwapChainForHwnd_t g_originalCreateSwapChainForHwnd{ nullptr };
		ExecuteCommandLists_t    g_originalExecuteCommandLists{ nullptr };

		// COM vtable slots, counting the methods inherited from each base interface.
		constexpr std::size_t kPresentIndex = 8;                  // IDXGISwapChain
		constexpr std::size_t kResizeBuffersIndex = 13;           // IDXGISwapChain
		constexpr std::size_t kPresent1Index = 22;                // IDXGISwapChain1
		constexpr std::size_t kResizeBuffers1Index = 39;          // IDXGISwapChain3
		constexpr std::size_t kCreateSwapChainForHwndIndex = 15;  // IDXGIFactory2
		constexpr std::size_t kExecuteCommandListsIndex = 10;     // ID3D12CommandQueue

		template <class T>
		void SafeRelease(T*& a_object)
		{
			if (a_object) {
				a_object->Release();
				a_object = nullptr;
			}
		}

		template <class T>
		T GetVFunc(void* a_object, std::size_t a_index)
		{
			return reinterpret_cast<T>((*reinterpret_cast<void***>(a_object))[a_index]);
		}

		HRESULT STDMETHODCALLTYPE Present(IDXGISwapChain* a_swapChain, UINT a_syncInterval, UINT a_flags)
		{
			if (!(a_flags & DXGI_PRESENT_TEST)) DX12Renderer::GetSingleton()->OnPresent(a_swapChain);
			return g_originalPresent(a_swapChain, a_syncInterval, a_flags);
		}

		HRESULT STDMETHODCALLTYPE Present1(IDXGISwapChain1* a_swapChain, UINT a_syncInterval, UINT a_flags, const DXGI_PRESENT_PARAMETERS* a_params)
		{
			if (!(a_flags & DXGI_PRESENT_TEST)) DX12Renderer::GetSingleton()->OnPresent(a_swapChain);
			return g_originalPresent1(a_swapChain, a_syncInterval, a_flags, a_params);
		}

		HRESULT STDMETHODCALLTYPE ResizeBuffers(IDXGISwapChain* a_swapChain, UINT a_bufferCount, UINT a_width, UINT a_height, DXGI_FORMAT a_format, UINT a_flags)
		{
			DX12Renderer::GetSingleton()->OnResizeBuffers(a_swapChain);
			return g_originalResizeBuffers(a_swapChain, a_bufferCount, a_width, a_height, a_format, a_flags);
		}

		HRESULT STDMETHODCALLTYPE ResizeBuffers1(IDXGISwapChain3* a_swapChain, UINT a_bufferCount, UINT a_width, UINT a_height, DXGI_FORMAT a_format, UINT a_flags, const UINT* a_nodeMasks, IUnknown* const* a_queues)
		{
			DX12Renderer::GetSingleton()->OnResizeBuffers(a_swapChain);
			return g_originalResizeBuffers1(a_swapChain, a_bufferCount, a_width, a_height, a_format, a_flags, a_nodeMasks, a_queues);
		}

		HRESULT STDMETHODCALLTYPE CreateSwapChainForHwnd(IDXGIFactory2* a_factory, IUnknown* a_device, HWND a_hwnd, const DXGI_SWAP_CHAIN_DESC1* a_desc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* a_fullscreenDesc, IDXGIOutput* a_output, IDXGISwapChain1** a_swapChain)
		{
			const HRESULT result = g_originalCreateSwapChainForHwnd(a_factory, a_device, a_hwnd, a_desc, a_fullscreenDesc, a_output, a_swapChain);
			if (SUCCEEDED(result) && a_swapChain && *a_swapChain) DX12Renderer::GetSingleton()->OnCreateSwapChain(a_device, *a_swapChain);
			return result;
		}

		void STDMETHODCALLTYPE ExecuteCommandLists(ID3D12CommandQueue* a_queue, UINT a_count, ID3D12CommandList* const* a_lists)
		{
			DX12Renderer::GetSingleton()->OnExecuteCommandLists(a_queue);
			g_originalExecuteCommandLists(a_queue, a_count, a_lists);
		}

		// Throwaway D3D12 objects, made only to read the addresses of the functions every instance shares.
		struct DummyObjects
		{
			HWND window{ nullptr };
			ID3D12Device* device{ nullptr };
			ID3D12CommandQueue* queue{ nullptr };
			IDXGIFactory2* factory{ nullptr };
			IDXGISwapChain1* swapChain{ nullptr };

			~DummyObjects()
			{
				SafeRelease(swapChain);
				SafeRelease(factory);
				SafeRelease(queue);
				SafeRelease(device);
				if (window) DestroyWindow(window);
			}

			bool Create()
			{
				static constexpr wchar_t kClassName[] = L"NAF_DX12_Dummy";
				WNDCLASSEXW windowClass{ sizeof(WNDCLASSEXW) };
				windowClass.lpfnWndProc = DefWindowProcW;
				windowClass.hInstance = GetModuleHandleW(nullptr);
				windowClass.lpszClassName = kClassName;
				RegisterClassExW(&windowClass);
				window = CreateWindowExW(0, kClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 8, 8, nullptr, nullptr, windowClass.hInstance, nullptr);
				if (!window) return false;

				if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) return false;

				D3D12_COMMAND_QUEUE_DESC queueDesc{};
				queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
				if (FAILED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)))) return false;

				if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return false;

				DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
				swapChainDesc.Width = 8;
				swapChainDesc.Height = 8;
				swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
				swapChainDesc.SampleDesc.Count = 1;
				swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
				swapChainDesc.BufferCount = 2;
				swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
				return SUCCEEDED(factory->CreateSwapChainForHwnd(queue, window, &swapChainDesc, nullptr, nullptr, &swapChain));
			}
		};
	}

	Renderer* Renderer::GetSingleton()
	{
		return DX12Renderer::GetSingleton();
	}

	DX12Renderer* DX12Renderer::GetSingleton()
	{
		static DX12Renderer singleton;
		return &singleton;
	}

	void DX12Renderer::Install()
	{
		InstallClipCursorHook();

		if (g_originalPresent) {
			return;
		}

		DummyObjects dummy;
		if (!dummy.Create()) {
			logger::warn("Failed to create D3D12 objects to find the swap-chain functions; the overlay is unavailable.");
			return;
		}

		IDXGISwapChain3* swapChain3 = nullptr;
		if (FAILED(dummy.swapChain->QueryInterface(IID_PPV_ARGS(&swapChain3)))) {
			logger::warn("IDXGISwapChain3 is unavailable; the overlay is unavailable.");
			return;
		}

		g_originalPresent = GetVFunc<Present_t>(swapChain3, kPresentIndex);
		g_originalResizeBuffers = GetVFunc<ResizeBuffers_t>(swapChain3, kResizeBuffersIndex);
		g_originalPresent1 = GetVFunc<Present1_t>(swapChain3, kPresent1Index);
		g_originalResizeBuffers1 = GetVFunc<ResizeBuffers1_t>(swapChain3, kResizeBuffers1Index);
		g_originalCreateSwapChainForHwnd = GetVFunc<CreateSwapChainForHwnd_t>(dummy.factory, kCreateSwapChainForHwndIndex);
		g_originalExecuteCommandLists = GetVFunc<ExecuteCommandLists_t>(dummy.queue, kExecuteCommandListsIndex);
		swapChain3->Release();

		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		DetourAttach(reinterpret_cast<PVOID*>(&g_originalPresent), reinterpret_cast<PVOID>(Present));
		DetourAttach(reinterpret_cast<PVOID*>(&g_originalPresent1), reinterpret_cast<PVOID>(Present1));
		DetourAttach(reinterpret_cast<PVOID*>(&g_originalResizeBuffers), reinterpret_cast<PVOID>(ResizeBuffers));
		DetourAttach(reinterpret_cast<PVOID*>(&g_originalResizeBuffers1), reinterpret_cast<PVOID>(ResizeBuffers1));
		DetourAttach(reinterpret_cast<PVOID*>(&g_originalCreateSwapChainForHwnd), reinterpret_cast<PVOID>(CreateSwapChainForHwnd));
		DetourAttach(reinterpret_cast<PVOID*>(&g_originalExecuteCommandLists), reinterpret_cast<PVOID>(ExecuteCommandLists));
		const LONG result = DetourTransactionCommit();

		if (result == NO_ERROR) {
			logger::info("Installed DX12 swap-chain hooks.");
		} else {
			logger::warn("Failed to install DX12 swap-chain hooks: {}", result);
			g_originalPresent = nullptr;
		}
	}

	void DX12Renderer::OnCreateSwapChain(IUnknown* a_device, IDXGISwapChain1* a_swapChain)
	{
		// For D3D12 the "device" a swap chain is created with is the queue it presents on.
		ID3D12CommandQueue* queue = nullptr;
		if (!a_device || FAILED(a_device->QueryInterface(IID_PPV_ARGS(&queue)))) return;

		IDXGISwapChain* swapChain = nullptr;
		if (FAILED(a_swapChain->QueryInterface(IID_PPV_ARGS(&swapChain)))) {
			queue->Release();
			return;
		}
		swapChain->Release();  // Only kept to compare against; the queue keeps its reference.

		std::scoped_lock l{ m_queueLock };
		// An entry for the same address belongs to a swap chain that's since been freed.
		std::erase_if(m_swapChainQueues, [&](auto& a_entry) {
			if (a_entry.first != swapChain) return false;
			a_entry.second->Release();
			return true;
		});
		m_swapChainQueues.emplace_back(swapChain, queue);
		m_swapChainCreated.store(true, std::memory_order_release);
	}

	void DX12Renderer::OnExecuteCommandLists(ID3D12CommandQueue* a_queue)
	{
		if (m_initialized || m_submittedQueue.load(std::memory_order_relaxed)) return;
		if (a_queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT) return;
		m_submittedQueue.store(a_queue, std::memory_order_relaxed);
	}

	ID3D12CommandQueue* DX12Renderer::FindQueue(IDXGISwapChain* a_swapChain)
	{
		{
			std::scoped_lock l{ m_queueLock };
			for (const auto& [swapChain, queue] : m_swapChainQueues) {
				if (swapChain == a_swapChain) {
					queue->AddRef();
					return queue;
				}
			}
		}

		// The swap chain was made before the hooks were installed. Starfield presents on its graphics queue.
		// TODO: verify CommonLibSF's CreationRenderer layout against the current game.
		if (const auto renderer = RE::CreationRendererPrivate::Renderer::GetSingleton()) {
			if (const auto queue = reinterpret_cast<ID3D12CommandQueue*>(renderer->GetGraphicsQueue())) {
				queue->AddRef();
				return queue;
			}
		}

		// Last resort: a direct queue the game submitted work to.
		const auto queue = m_submittedQueue.load(std::memory_order_relaxed);
		if (queue) queue->AddRef();
		return queue;
	}

	bool DX12Renderer::Initialize(IDXGISwapChain3* a_swapChain, const DXGI_SWAP_CHAIN_DESC& a_desc)
	{
		m_commandQueue = FindQueue(a_swapChain);
		if (!m_commandQueue) return false;  // Not known yet; try again next frame.

		const auto fail = [this](const char* a_reason) {
			logger::warn("Failed to initialize the DX12 ImGui renderer: {}.", a_reason);
			m_failed = true;
			return false;
		};

		if (FAILED(a_swapChain->GetDevice(IID_PPV_ARGS(&m_device)))) return fail("no device");

		m_swapChain = a_swapChain;
		m_swapChainCreated.store(false, std::memory_order_relaxed);
		m_format = a_desc.BufferDesc.Format;
		m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		if (!EnsureRtvHeap(a_desc.BufferCount)) return fail("no RTV heap");

		D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc{};
		srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		srvHeapDesc.NumDescriptors = 1;  // ImGui's font texture.
		srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		if (FAILED(m_device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_srvHeap)))) return fail("no SRV heap");

		for (auto& frame : m_frames) {
			if (FAILED(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frame.allocator)))) return fail("no command allocator");
		}
		if (FAILED(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_frames[0].allocator, nullptr, IID_PPV_ARGS(&m_commandList)))) return fail("no command list");
		m_commandList->Close();

		if (FAILED(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)))) return fail("no fence");
		m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		if (!m_fenceEvent) return fail("no fence event");

		if (!InitializeImGui(a_desc.OutputWindow)) return fail("no ImGui context");
		if (!ImGui_ImplDX12_Init(m_device, kFramesInFlight, m_format, m_srvHeap, m_srvHeap->GetCPUDescriptorHandleForHeapStart(), m_srvHeap->GetGPUDescriptorHandleForHeapStart())) {
			ShutdownImGui();
			return fail("ImGui's DX12 backend didn't start");
		}

		m_initialized = true;
		logger::info("Initialized DX12 ImGui renderer ({} back buffers, format {}).", a_desc.BufferCount, static_cast<std::uint32_t>(m_format));
		return true;
	}

	// Moves the overlay onto a new swap chain the game made for its window.
	bool DX12Renderer::BindSwapChain(IDXGISwapChain3* a_swapChain, const DXGI_SWAP_CHAIN_DESC& a_desc)
	{
		// The command allocators and ImGui's buffers are reused below, and may be on another queue from now on.
		WaitForGpu();

		const auto queue = FindQueue(a_swapChain);
		if (!queue) return false;  // Not known yet; try again next frame.
		SafeRelease(m_commandQueue);
		m_commandQueue = queue;

		if (a_desc.BufferDesc.Format != m_format) {
			m_format = a_desc.BufferDesc.Format;
			ImGui_ImplDX12_Shutdown();
			if (!ImGui_ImplDX12_Init(m_device, kFramesInFlight, m_format, m_srvHeap, m_srvHeap->GetCPUDescriptorHandleForHeapStart(), m_srvHeap->GetGPUDescriptorHandleForHeapStart())) {
				logger::warn("Failed to restart ImGui's DX12 backend for format {}; the overlay is unavailable.", static_cast<std::uint32_t>(m_format));
				m_failed = true;
				return false;
			}
		}

		m_swapChain = a_swapChain;
		m_swapChainCreated.store(false, std::memory_order_relaxed);
		logger::info("DX12 ImGui renderer moved to a new swap chain ({} back buffers, format {}).", a_desc.BufferCount, static_cast<std::uint32_t>(m_format));
		return true;
	}

	bool DX12Renderer::EnsureRtvHeap(UINT a_bufferCount)
	{
		if (m_rtvHeap && m_rtvHeap->GetDesc().NumDescriptors >= a_bufferCount) return true;
		SafeRelease(m_rtvHeap);
		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
		rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtvHeapDesc.NumDescriptors = a_bufferCount;
		return SUCCEEDED(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));
	}

	void DX12Renderer::WaitForGpu()
	{
		if (!m_commandQueue || !m_fence) return;
		const auto value = ++m_fenceValue;
		if (SUCCEEDED(m_commandQueue->Signal(m_fence, value)) && m_fence->GetCompletedValue() < value) {
			m_fence->SetEventOnCompletion(value, m_fenceEvent);
			WaitForSingleObject(m_fenceEvent, INFINITE);
		}
	}

	void DX12Renderer::OnPresent(IDXGISwapChain* a_swapChain)
	{
		if (m_failed) return;

		IDXGISwapChain3* swapChain = nullptr;
		if (FAILED(a_swapChain->QueryInterface(IID_PPV_ARGS(&swapChain)))) return;
		// Only held for this call.
		const std::unique_ptr<IDXGISwapChain3, decltype([](IDXGISwapChain3* a_object) { a_object->Release(); })> swapChainRef{ swapChain };

		DXGI_SWAP_CHAIN_DESC desc{};
		if (FAILED(swapChain->GetDesc(&desc))) return;

		if (!m_initialized) {
			if (!Initialize(swapChain, desc)) return;
		} else if (desc.OutputWindow != m_hwnd) {
			return;  // Not the game window's swap chain.
		} else if (swapChain != m_swapChain || m_swapChainCreated.load(std::memory_order_acquire)) {
			if (!BindSwapChain(swapChain, desc)) return;
		}

		if (!EnsureRtvHeap(desc.BufferCount)) return;

		const UINT backBufferIndex = swapChain->GetCurrentBackBufferIndex();
		ID3D12Resource* backBuffer = nullptr;
		if (backBufferIndex >= desc.BufferCount || FAILED(swapChain->GetBuffer(backBufferIndex, IID_PPV_ARGS(&backBuffer)))) return;
		const std::unique_ptr<ID3D12Resource, decltype([](ID3D12Resource* a_object) { a_object->Release(); })> backBufferRef{ backBuffer };

		ImGui_ImplDX12_NewFrame();
		BuildFrame();

		auto& frame = m_frames[m_frameIndex++ % kFramesInFlight];
		if (m_fence->GetCompletedValue() < frame.fenceValue) {
			m_fence->SetEventOnCompletion(frame.fenceValue, m_fenceEvent);
			WaitForSingleObject(m_fenceEvent, INFINITE);
		}

		// Render target views are read when they're bound, so the slot can be rewritten every frame.
		auto rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
		rtv.ptr += static_cast<SIZE_T>(backBufferIndex) * m_rtvDescriptorSize;
		m_device->CreateRenderTargetView(backBuffer, nullptr, rtv);

		frame.allocator->Reset();
		m_commandList->Reset(frame.allocator, nullptr);

		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = backBuffer;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		m_commandList->ResourceBarrier(1, &barrier);

		m_commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
		m_commandList->SetDescriptorHeaps(1, &m_srvHeap);
		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_commandList);

		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
		m_commandList->ResourceBarrier(1, &barrier);
		m_commandList->Close();

		ID3D12CommandList* const lists[]{ m_commandList };
		m_commandQueue->ExecuteCommandLists(1, lists);
		frame.fenceValue = ++m_fenceValue;
		m_commandQueue->Signal(m_fence, frame.fenceValue);
	}

	void DX12Renderer::OnResizeBuffers(IDXGISwapChain* a_swapChain)
	{
		if (!m_initialized) return;

		IDXGISwapChain3* swapChain3 = nullptr;
		const bool same = SUCCEEDED(a_swapChain->QueryInterface(IID_PPV_ARGS(&swapChain3))) && swapChain3 == m_swapChain;
		if (swapChain3) swapChain3->Release();
		if (!same) return;

		// The swap chain can only resize once the GPU is done with its buffers.
		WaitForGpu();
	}
}
#endif
