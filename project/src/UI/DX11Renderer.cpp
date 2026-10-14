#include "DX11Renderer.h"

// Only Fallout 4 renders with D3D11.
#if defined TARGET_GAME_F4
#include <detours/detours.h>

#include "imgui/imgui_impl_dx11.h"

namespace UI
{
	namespace
	{
		using Present_t = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);
		using ResizeBuffers_t = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

		Present_t       g_originalPresent{ nullptr };
		ResizeBuffers_t g_originalResizeBuffers{ nullptr };

		constexpr std::uint32_t kPresentVTableIndex = 8;
		constexpr std::uint32_t kResizeBuffersVTableIndex = 13;

		ID3D11RenderTargetView* CreateBackBufferView(IDXGISwapChain* a_swapChain, ID3D11Device* a_device)
		{
			if (!a_swapChain || !a_device) {
				return nullptr;
			}

			ID3D11Texture2D* backBuffer = nullptr;
			if (FAILED(a_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))) || !backBuffer) {
				return nullptr;
			}

			ID3D11RenderTargetView* view = nullptr;
			const HRESULT result = a_device->CreateRenderTargetView(backBuffer, nullptr, &view);
			backBuffer->Release();
			return SUCCEEDED(result) ? view : nullptr;
		}

		HRESULT __stdcall Present(IDXGISwapChain* a_swapChain, UINT a_syncInterval, UINT a_flags)
		{
			auto* renderer = DX11Renderer::GetSingleton();
			if (!renderer->m_initialized) {
				renderer->Initialize(a_swapChain);
			}

			if (renderer->m_initialized) {
				renderer->OnFrame();
			}

			return g_originalPresent(a_swapChain, a_syncInterval, a_flags);
		}

		HRESULT __stdcall ResizeBuffers(IDXGISwapChain* a_swapChain, UINT a_bufferCount, UINT a_width, UINT a_height, DXGI_FORMAT a_newFormat, UINT a_swapChainFlags)
		{
			DX11Renderer::GetSingleton()->OnResize();
			return g_originalResizeBuffers(a_swapChain, a_bufferCount, a_width, a_height, a_newFormat, a_swapChainFlags);
		}
	}

	Renderer* Renderer::GetSingleton()
	{
		return DX11Renderer::GetSingleton();
	}

	DX11Renderer* DX11Renderer::GetSingleton()
	{
		static DX11Renderer singleton;
		return &singleton;
	}

	void DX11Renderer::Install()
	{
		InstallClipCursorHook();

		if (g_originalPresent && g_originalResizeBuffers) {
			return;
		}

		auto renderer = RE::BSGraphics::RendererData::GetSingleton();
		if (!renderer || !renderer->renderWindow[0].swapChain) {
			logger::warn("DX11 renderer install deferred: swap chain is not available.");
			return;
		}

		m_swapChain = reinterpret_cast<IDXGISwapChain*>(renderer->renderWindow[0].swapChain);
		auto vtable = *reinterpret_cast<std::uintptr_t**>(m_swapChain);
		g_originalPresent = reinterpret_cast<Present_t>(vtable[kPresentVTableIndex]);
		g_originalResizeBuffers = reinterpret_cast<ResizeBuffers_t>(vtable[kResizeBuffersVTableIndex]);

		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		DetourAttach(reinterpret_cast<PVOID*>(&g_originalPresent), reinterpret_cast<PVOID>(Present));
		DetourAttach(reinterpret_cast<PVOID*>(&g_originalResizeBuffers), reinterpret_cast<PVOID>(ResizeBuffers));
		const LONG result = DetourTransactionCommit();

		if (result == NO_ERROR) {
			logger::info("Installed DX11 swap-chain hooks.");
		} else {
			logger::warn("Failed to install DX11 swap-chain hooks: {}", result);
			g_originalPresent = nullptr;
			g_originalResizeBuffers = nullptr;
		}
	}

	bool DX11Renderer::Initialize(IDXGISwapChain* a_swapChain)
	{
		if (m_initialized) {
			return true;
		}

		if (!a_swapChain) {
			return false;
		}

		m_swapChain = a_swapChain;
		if (FAILED(m_swapChain->GetDevice(IID_PPV_ARGS(&m_d3dDevice))) || !m_d3dDevice) {
			logger::warn("Failed to fetch D3D11 device from swap chain.");
			return false;
		}

		m_d3dDevice->GetImmediateContext(&m_d3dDeviceContext);
		if (!m_d3dDeviceContext) {
			logger::warn("Failed to fetch D3D11 immediate context.");
			return false;
		}

		HWND hwnd = nullptr;
		DXGI_SWAP_CHAIN_DESC desc{};
		if (SUCCEEDED(m_swapChain->GetDesc(&desc))) {
			hwnd = desc.OutputWindow;
		}
		if (!hwnd) {
			auto renderer = RE::BSGraphics::RendererData::GetSingleton();
			if (renderer) {
				hwnd = reinterpret_cast<HWND>(renderer->renderWindow[0].hwnd);
			}
		}

		m_backBufferView = CreateBackBufferView(m_swapChain, m_d3dDevice);
		if (!m_backBufferView) {
			logger::warn("Failed to create ImGui back-buffer render target.");
			return false;
		}

		if (!InitializeImGui(hwnd)) {
			Shutdown();
			return false;
		}

		if (!ImGui_ImplDX11_Init(m_d3dDevice, m_d3dDeviceContext)) {
			logger::warn("Failed to initialize ImGui backends.");
			ShutdownImGui();
			Shutdown();
			return false;
		}

		m_initialized = true;
		logger::info("Initialized DX11 ImGui renderer.");
		return true;
	}

	void DX11Renderer::Shutdown()
	{
		if (m_initialized) {
			ImGui_ImplDX11_Shutdown();
			ShutdownImGui();
		}

		if (m_backBufferView) {
			m_backBufferView->Release();
			m_backBufferView = nullptr;
		}
		if (m_d3dDeviceContext) {
			m_d3dDeviceContext->Release();
			m_d3dDeviceContext = nullptr;
		}
		if (m_d3dDevice) {
			m_d3dDevice->Release();
			m_d3dDevice = nullptr;
		}

		m_initialized = false;
	}

	void DX11Renderer::OnFrame()
	{
		if (!m_initialized) {
			return;
		}

		if (!m_backBufferView) {
			m_backBufferView = CreateBackBufferView(m_swapChain, m_d3dDevice);
			if (!m_backBufferView) {
				return;
			}
		}

		ImGui_ImplDX11_NewFrame();
		BuildFrame();

		ID3D11RenderTargetView* previousRenderTarget = nullptr;
		ID3D11DepthStencilView* previousDepthStencil = nullptr;
		m_d3dDeviceContext->OMGetRenderTargets(1, &previousRenderTarget, &previousDepthStencil);
		m_d3dDeviceContext->OMSetRenderTargets(1, &m_backBufferView, nullptr);
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
		m_d3dDeviceContext->OMSetRenderTargets(1, &previousRenderTarget, previousDepthStencil);

		if (previousRenderTarget) {
			previousRenderTarget->Release();
		}
		if (previousDepthStencil) {
			previousDepthStencil->Release();
		}
	}

	void DX11Renderer::OnResize()
	{
		if (m_backBufferView) {
			m_backBufferView->Release();
			m_backBufferView = nullptr;
		}
	}
}
#endif
