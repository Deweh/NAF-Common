#pragma once
#include <Windows.h>
#include "d3d11.h"
#include "dxgi.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx11.h"
#include "imgui/imgui_impl_win32.h"

#ifdef near
#undef near
#endif
#ifdef far
#undef far
#endif

namespace RE
{
	class NiPoint3;
}

struct ImVec2;

namespace UI
{
	// A projected point before the perspective divide. Kept in this form so geometry crossing the camera's
	// near plane can be clipped there; dividing first would mirror points behind the camera across the screen.
	struct ClipVertex
	{
		float x{ 0.0f };
		float y{ 0.0f };
		float w{ 0.0f };
	};

	struct ScreenSpaceDiamond
	{
		ClipVertex vertices[6]{};  // Start, right, front, left, back, end.
		float depth{ 0.0f };
		float halfWidth{ 0.0f };
		bool valid{ false };       // At least partly in front of the camera.
	};

	class DX11Renderer
	{
	public:
		using DrawCallback = void (*)(DX11Renderer* a_renderer);

		ID3D11Device* m_d3dDevice{ nullptr };
		ID3D11DeviceContext* m_d3dDeviceContext{ nullptr };
		IDXGISwapChain* m_swapChain{ nullptr };
		ID3D11RenderTargetView* m_backBufferView{ nullptr };
		HWND m_hwnd{ nullptr };
		WNDPROC m_originalWndProc{ nullptr };
		bool m_initialized{ false };
		bool m_showDemoWindow{ false };
		bool m_drawExampleDiamond{ false };
		float m_exampleDiamondSize{ 32.0f };

		static DX11Renderer* GetSingleton();

		void Install();
		bool Initialize(IDXGISwapChain* a_swapChain);
		void Shutdown();
		void OnFrame();
		void OnResize();
		void SetDrawCallback(DrawCallback a_callback);
		void SetMouseReleased(bool a_release);
		[[nodiscard]] bool IsMouseReleased() const;

		[[nodiscard]] bool WorldToScreen(const RE::NiPoint3& a_world, ImVec2& a_out) const;
		[[nodiscard]] bool WorldToScreen(const RE::NiPoint3& a_world, ImVec2& a_out, float& a_depth) const;
		[[nodiscard]] bool GetGizmoCameraMatrices(ozz::math::Float4x4& a_view, ozz::math::Float4x4& a_projection, bool& a_orthographic) const;
		[[nodiscard]] bool BuildScreenSpaceDiamond(const RE::NiPoint3& a_center, float a_size, ScreenSpaceDiamond& a_outDiamond) const;
		[[nodiscard]] bool BuildScreenSpaceDiamond(const RE::NiPoint3& a_start, const RE::NiPoint3& a_end, float a_halfWidth, float a_rollRadians, ScreenSpaceDiamond& a_outDiamond) const;
		void DrawWorldLine(const RE::NiPoint3& a_start, const RE::NiPoint3& a_end, ImU32 a_color, float a_thickness = 1.0f) const;
		void DrawWorldDiamond(const RE::NiPoint3& a_center, float a_size, ImU32 a_color, float a_thickness = 1.0f, float a_rollRadians = 0.0f) const;
		void DrawWorldDiamond(const RE::NiPoint3& a_start, const RE::NiPoint3& a_end, float a_halfWidth, ImU32 a_color, float a_thickness = 1.0f, float a_rollRadians = 0.0f) const;
		void DrawWorldDiamond(const ScreenSpaceDiamond& a_diamond, ImU32 a_color, float a_thickness = 1.0f) const;

		[[nodiscard]] bool IsScreenPointInsideDiamond(const ImVec2& a_screenPoint, const ScreenSpaceDiamond& a_diamond) const;
		[[nodiscard]] bool IsScreenPointInsideDiamond(const ImVec2& a_screenPoint, const RE::NiPoint3& a_center, float a_size, float a_rollRadians) const;
		[[nodiscard]] bool IsScreenPointInsideDiamond(const ImVec2& a_screenPoint, const RE::NiPoint3& a_start, const RE::NiPoint3& a_end, float a_halfWidth, float a_rollRadians) const;

	private:
		DrawCallback m_drawCallback{ nullptr };
		std::atomic_bool m_mouseReleased{ false };
	};
}