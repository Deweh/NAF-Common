#include "DX11Renderer.h"

#include <Windows.h>
#include <cmath>
#include <detours/detours.h>

#ifdef near
#undef near
#endif
#ifdef far
#undef far
#endif

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

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

		RE::NiPoint3 WorldToScreenNormalized(const RE::NiCamera& a_camera, const RE::NiPoint3& a_world)
		{
			RE::NiPoint3 result{ 0.0f, 0.0f, -1.0f };
			const float  worldDiffRotated = (((a_world.y - a_camera.world.translate.y) * a_camera.world.rotate[0][1]) +
                                           ((a_world.x - a_camera.world.translate.x) * a_camera.world.rotate[0][0]) +
                                           ((a_world.z - a_camera.world.translate.z) * a_camera.world.rotate[0][2])) -
			                             a_camera.viewFrustum.near;

			result.z = worldDiffRotated * (1.0f / (a_camera.viewFrustum.far - a_camera.viewFrustum.near));

			const float trace = (a_world.x * a_camera.worldToCam[3][0]) +
			                    (a_world.y * a_camera.worldToCam[3][1]) +
			                    ((a_world.z * a_camera.worldToCam[3][2]) + a_camera.worldToCam[3][3]);
			if (trace <= 0.00001f) {
				return result;
			}

			const float traceInv = 1.0f / trace;
			result.x = (((a_world.y * a_camera.worldToCam[0][1]) + (a_world.x * a_camera.worldToCam[0][0])) +
			            ((a_world.z * a_camera.worldToCam[0][2]) + a_camera.worldToCam[0][3])) *
			           traceInv;
			result.y = (((a_world.y * a_camera.worldToCam[1][1]) + (a_world.x * a_camera.worldToCam[1][0])) +
			            ((a_world.z * a_camera.worldToCam[1][2]) + a_camera.worldToCam[1][3])) *
			           traceInv;
			result.x = (result.x + 1.0f) * 0.5f;
			result.y = ((-result.y) + 1.0f) * 0.5f;
			return result;
		}

		float Cross2D(const ImVec2& a_a, const ImVec2& a_b, const ImVec2& a_c)
		{
			return (a_b.x - a_a.x) * (a_c.y - a_a.y) - (a_b.y - a_a.y) * (a_c.x - a_a.x);
		}

		bool PointInTriangle(const ImVec2& a_point, const ImVec2& a_a, const ImVec2& a_b, const ImVec2& a_c)
		{
			const float ab = Cross2D(a_a, a_b, a_point);
			const float bc = Cross2D(a_b, a_c, a_point);
			const float ca = Cross2D(a_c, a_a, a_point);
			const bool  hasNeg = (ab < 0.0f) || (bc < 0.0f) || (ca < 0.0f);
			const bool  hasPos = (ab > 0.0f) || (bc > 0.0f) || (ca > 0.0f);
			return !(hasNeg && hasPos);
		}

		RE::NiPoint3 NormalizeOrFallback(const RE::NiPoint3& a_vector, const RE::NiPoint3& a_fallback)
		{
			const float lengthSq = (a_vector.x * a_vector.x) + (a_vector.y * a_vector.y) + (a_vector.z * a_vector.z);
			if (lengthSq <= 0.000001f) {
				return a_fallback;
			}

			const float invLength = 1.0f / std::sqrt(lengthSq);
			return RE::NiPoint3{ a_vector.x * invLength, a_vector.y * invLength, a_vector.z * invLength };
		}

		RE::NiPoint3 Cross(const RE::NiPoint3& a_lhs, const RE::NiPoint3& a_rhs)
		{
			return RE::NiPoint3{
				(a_lhs.y * a_rhs.z) - (a_lhs.z * a_rhs.y),
				(a_lhs.z * a_rhs.x) - (a_lhs.x * a_rhs.z),
				(a_lhs.x * a_rhs.y) - (a_lhs.y * a_rhs.x)
			};
		}

		RE::NiPoint3 AddScaled(const RE::NiPoint3& a_base, const RE::NiPoint3& a_direction, float a_scale)
		{
			return RE::NiPoint3{
				a_base.x + (a_direction.x * a_scale),
				a_base.y + (a_direction.y * a_scale),
				a_base.z + (a_direction.z * a_scale)
			};
		}

		LRESULT CALLBACK WndProc(HWND a_hwnd, UINT a_msg, WPARAM a_wparam, LPARAM a_lparam)
		{
			auto* renderer = DX11Renderer::GetSingleton();
			if (renderer->m_initialized && ImGui_ImplWin32_WndProcHandler(a_hwnd, a_msg, a_wparam, a_lparam)) {
				return true;
			}

			return CallWindowProc(renderer->m_originalWndProc, a_hwnd, a_msg, a_wparam, a_lparam);
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

	DX11Renderer* DX11Renderer::GetSingleton()
	{
		static DX11Renderer singleton;
		return &singleton;
	}

	void DX11Renderer::Install()
	{
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

		DXGI_SWAP_CHAIN_DESC desc{};
		if (SUCCEEDED(m_swapChain->GetDesc(&desc))) {
			m_hwnd = desc.OutputWindow;
		}
		if (!m_hwnd) {
			auto renderer = RE::BSGraphics::RendererData::GetSingleton();
			if (renderer) {
				m_hwnd = reinterpret_cast<HWND>(renderer->renderWindow[0].hwnd);
			}
		}

		m_backBufferView = CreateBackBufferView(m_swapChain, m_d3dDevice);
		if (!m_backBufferView) {
			logger::warn("Failed to create ImGui back-buffer render target.");
			return false;
		}

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

		ImGui::StyleColorsDark();

		if (!ImGui_ImplWin32_Init(m_hwnd) || !ImGui_ImplDX11_Init(m_d3dDevice, m_d3dDeviceContext)) {
			logger::warn("Failed to initialize ImGui backends.");
			Shutdown();
			return false;
		}

		m_originalWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtr(m_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WndProc)));
		m_initialized = true;
		logger::info("Initialized DX11 ImGui renderer.");
		return true;
	}

	void DX11Renderer::Shutdown()
	{
		if (m_originalWndProc && m_hwnd) {
			SetWindowLongPtr(m_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(m_originalWndProc));
			m_originalWndProc = nullptr;
		}

		if (m_initialized) {
			ImGui_ImplDX11_Shutdown();
			ImGui_ImplWin32_Shutdown();
			ImGui::DestroyContext();
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
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		if (m_drawCallback) {
			m_drawCallback(this);
		}

		ImGui::Render();

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

	void DX11Renderer::SetDrawCallback(DrawCallback a_callback)
	{
		m_drawCallback = a_callback;
	}

	bool DX11Renderer::WorldToScreen(const RE::NiPoint3& a_world, ImVec2& a_out) const
	{
		float depth = 0.0f;
		return WorldToScreen(a_world, a_out, depth);
	}

	bool DX11Renderer::WorldToScreen(const RE::NiPoint3& a_world, ImVec2& a_out, float& a_depth) const
	{
		const auto camera = RE::Main::WorldRootCamera();
		if (!camera) {
			return false;
		}

		const auto screen = WorldToScreenNormalized(*camera, a_world);
		a_depth = screen.z;

		ImGuiIO& io = ImGui::GetIO();
		a_out = ImVec2(screen.x * io.DisplaySize.x, screen.y * io.DisplaySize.y);
		return true;
	}

	bool DX11Renderer::BuildScreenSpaceDiamond(const RE::NiPoint3& a_center, float a_size, ScreenSpaceDiamond& a_outDiamond) const
	{
		const RE::NiPoint3 start{ a_center.x, a_center.y, a_center.z + a_size };
		const RE::NiPoint3 end{ a_center.x, a_center.y, a_center.z - a_size };
		return BuildScreenSpaceDiamond(start, end, a_size, 0.0f, a_outDiamond);
	}

	bool DX11Renderer::BuildScreenSpaceDiamond(const RE::NiPoint3& a_start, const RE::NiPoint3& a_end, float a_halfWidth, float a_rollRadians, ScreenSpaceDiamond& a_outDiamond) const
	{
		a_outDiamond = {};

		const auto camera = RE::Main::WorldRootCamera();
		if (!camera) {
			return false;
		}

		const RE::NiPoint3 axis = NormalizeOrFallback(
        RE::NiPoint3{ a_end.x - a_start.x, a_end.y - a_start.y, a_end.z - a_start.z },
        RE::NiPoint3{ 0.0f, 0.0f, 1.0f });

        // Pick a world axis that is not parallel to axis
        RE::NiPoint3 reference = (std::fabs(axis.z) < 0.999f)
            ? RE::NiPoint3{ 0.0f, 0.0f, 1.0f }
            : RE::NiPoint3{ 1.0f, 0.0f, 0.0f };

        RE::NiPoint3 side = NormalizeOrFallback(Cross(reference, axis), RE::NiPoint3{ 1.0f, 0.0f, 0.0f });
        RE::NiPoint3 normal = NormalizeOrFallback(Cross(axis, side), RE::NiPoint3{ 0.0f, 1.0f, 0.0f });

		const RE::NiPoint3 center{
			(a_start.x + a_end.x) * 0.5f,
			(a_start.y + a_end.y) * 0.5f,
			(a_start.z + a_end.z) * 0.5f
		};

		float centerDepth = 0.0f;
		if (!WorldToScreen(center, a_outDiamond.center, centerDepth)) {
			return false;
		}

        const float c = std::cos(a_rollRadians);
        const float s = std::sin(a_rollRadians);

        const RE::NiPoint3 rotatedSide{
            side.x * c - normal.x * s,
            side.y * c - normal.y * s,
            side.z * c - normal.z * s
        };

        const RE::NiPoint3 rotatedNormal{
            normal.x * c + side.x * s,
            normal.y * c + side.y * s,
            normal.z * c + side.z * s
        };

        side = rotatedSide;
        normal = rotatedNormal;

		const RE::NiPoint3 right = AddScaled(center, side, a_halfWidth);
		const RE::NiPoint3 left = AddScaled(center, side, -a_halfWidth);
		const RE::NiPoint3 front = AddScaled(center, normal, a_halfWidth);
		const RE::NiPoint3 back = AddScaled(center, normal, -a_halfWidth);

		float depthSamples[6]{};
		if (!WorldToScreen(a_start, a_outDiamond.vertices[0], depthSamples[0]) ||
		    !WorldToScreen(right, a_outDiamond.vertices[1], depthSamples[1]) ||
		    !WorldToScreen(front, a_outDiamond.vertices[2], depthSamples[2]) ||
		    !WorldToScreen(left, a_outDiamond.vertices[3], depthSamples[3]) ||
		    !WorldToScreen(back, a_outDiamond.vertices[4], depthSamples[4]) ||
		    !WorldToScreen(a_end, a_outDiamond.vertices[5], depthSamples[5])) {
			return false;
		}

		a_outDiamond.depth = centerDepth;
		a_outDiamond.halfWidth = a_halfWidth;
		a_outDiamond.valid = true;
		return true;
	}

	void DX11Renderer::DrawWorldLine(const RE::NiPoint3& a_start, const RE::NiPoint3& a_end, ImU32 a_color, float a_thickness) const
	{
		ImVec2 start;
		ImVec2 end;
		if (WorldToScreen(a_start, start) && WorldToScreen(a_end, end)) {
			ImGui::GetBackgroundDrawList()->AddLine(start, end, a_color, a_thickness);
		}
	}

	void DX11Renderer::DrawWorldDiamond(const RE::NiPoint3& a_center, float a_size, ImU32 a_color, float a_thickness, float a_rollRadians) const
	{
		const RE::NiPoint3 start{ a_center.x, a_center.y, a_center.z + a_size };
		const RE::NiPoint3 end{ a_center.x, a_center.y, a_center.z - a_size };
		DrawWorldDiamond(start, end, a_size, a_color, a_thickness, a_rollRadians);
	}

	void DX11Renderer::DrawWorldDiamond(const RE::NiPoint3& a_start, const RE::NiPoint3& a_end, float a_halfWidth, ImU32 a_color, float a_thickness, float a_rollRadians) const
	{
		ScreenSpaceDiamond diamond;
		if (!BuildScreenSpaceDiamond(a_start, a_end, a_halfWidth, a_rollRadians, diamond)) {
			return;
		}

		DrawWorldDiamond(diamond, a_color, a_thickness);
	}

    void DX11Renderer::DrawWorldDiamond(const ScreenSpaceDiamond& diamond, ImU32 a_color, float a_thickness) const
	{
		auto* drawList = ImGui::GetBackgroundDrawList();
		const auto shadedColor = ImGui::ColorConvertU32ToFloat4(a_color);
		const ImU32 backFaceColor = ImGui::ColorConvertFloat4ToU32(ImVec4(shadedColor.x * 0.35f, shadedColor.y * 0.35f, shadedColor.z * 0.35f, shadedColor.w * 0.35f));
		const ImU32 frontFaceColor = ImGui::ColorConvertFloat4ToU32(ImVec4(shadedColor.x * 0.75f, shadedColor.y * 0.75f, shadedColor.z * 0.75f, shadedColor.w * 0.18f));

		// Draw far half first so the nearer half appears on top in screen-space.
		drawList->AddTriangleFilled(diamond.vertices[0], diamond.vertices[1], diamond.vertices[4], backFaceColor);
		drawList->AddTriangleFilled(diamond.vertices[0], diamond.vertices[3], diamond.vertices[4], backFaceColor);
		drawList->AddTriangleFilled(diamond.vertices[5], diamond.vertices[1], diamond.vertices[4], backFaceColor);
		drawList->AddTriangleFilled(diamond.vertices[5], diamond.vertices[3], diamond.vertices[4], backFaceColor);

		drawList->AddTriangleFilled(diamond.vertices[0], diamond.vertices[1], diamond.vertices[2], frontFaceColor);
		drawList->AddTriangleFilled(diamond.vertices[0], diamond.vertices[3], diamond.vertices[2], frontFaceColor);
		drawList->AddTriangleFilled(diamond.vertices[5], diamond.vertices[1], diamond.vertices[2], frontFaceColor);
		drawList->AddTriangleFilled(diamond.vertices[5], diamond.vertices[3], diamond.vertices[2], frontFaceColor);

		drawList->AddLine(diamond.vertices[0], diamond.vertices[3], a_color, a_thickness);
		drawList->AddLine(diamond.vertices[0], diamond.vertices[1], a_color, a_thickness);
		drawList->AddLine(diamond.vertices[0], diamond.vertices[2], a_color, a_thickness);
		drawList->AddLine(diamond.vertices[0], diamond.vertices[4], a_color, a_thickness);
		drawList->AddLine(diamond.vertices[5], diamond.vertices[3], a_color, a_thickness);
		drawList->AddLine(diamond.vertices[5], diamond.vertices[1], a_color, a_thickness);
		drawList->AddLine(diamond.vertices[5], diamond.vertices[2], a_color, a_thickness);
		drawList->AddLine(diamond.vertices[5], diamond.vertices[4], a_color, a_thickness);
		drawList->AddLine(diamond.vertices[3], diamond.vertices[2], a_color, a_thickness);
		drawList->AddLine(diamond.vertices[2], diamond.vertices[1], a_color, a_thickness);
		drawList->AddLine(diamond.vertices[1], diamond.vertices[4], a_color, a_thickness);
		drawList->AddLine(diamond.vertices[4], diamond.vertices[3], a_color, a_thickness);
	}

	bool DX11Renderer::IsScreenPointInsideDiamond(const ImVec2& a_screenPoint, const ScreenSpaceDiamond& a_diamond) const
	{
		if (!a_diamond.valid) {
			return false;
		}

		return PointInTriangle(a_screenPoint, a_diamond.vertices[0], a_diamond.vertices[1], a_diamond.vertices[2]) ||
		       PointInTriangle(a_screenPoint, a_diamond.vertices[0], a_diamond.vertices[2], a_diamond.vertices[3]) ||
		       PointInTriangle(a_screenPoint, a_diamond.vertices[5], a_diamond.vertices[1], a_diamond.vertices[2]) ||
		       PointInTriangle(a_screenPoint, a_diamond.vertices[5], a_diamond.vertices[2], a_diamond.vertices[3]);
	}

	bool DX11Renderer::IsScreenPointInsideDiamond(const ImVec2& a_screenPoint, const RE::NiPoint3& a_center, float a_size, float a_rollRadians) const
	{
		const RE::NiPoint3 start{ a_center.x, a_center.y, a_center.z + a_size };
		const RE::NiPoint3 end{ a_center.x, a_center.y, a_center.z - a_size };
		return IsScreenPointInsideDiamond(a_screenPoint, start, end, a_size, a_rollRadians);
	}

	bool DX11Renderer::IsScreenPointInsideDiamond(const ImVec2& a_screenPoint, const RE::NiPoint3& a_start, const RE::NiPoint3& a_end, float a_halfWidth, float a_rollRadians) const
	{
		ScreenSpaceDiamond diamond;
		return BuildScreenSpaceDiamond(a_start, a_end, a_halfWidth, a_rollRadians, diamond) && IsScreenPointInsideDiamond(a_screenPoint, diamond);
	}
}
