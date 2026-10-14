#pragma once
#include <Windows.h>
#include "imgui/imgui.h"
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

	// The ImGui overlay drawn over the game. Subclasses own the graphics API side: hooking the swap chain, and
	// rendering ImGui's draw data from inside Present. Everything else (input, and projecting the game world onto
	// the screen) is shared.
	class Renderer
	{
	public:
		using DrawCallback = void (*)(Renderer* a_renderer);

		HWND m_hwnd{ nullptr };
		WNDPROC m_originalWndProc{ nullptr };
		bool m_initialized{ false };
		bool m_showDemoWindow{ false };
		bool m_drawExampleDiamond{ false };
		float m_exampleDiamondSize{ 32.0f };

		// The backend for the game being built: DX11 for Fallout 4, DX12 for Starfield.
		static Renderer* GetSingleton();

		virtual ~Renderer() = default;

		virtual void Install() = 0;
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

	protected:
		void InstallClipCursorHook();
		// Creates the ImGui context and the Win32 platform backend, and subclasses the window for input.
		// Subclasses initialize their renderer backend after this succeeds.
		bool InitializeImGui(HWND a_hwnd);
		void ShutdownImGui();
		// Runs the draw callback for a new ImGui frame, after the subclass started its renderer backend's frame.
		void BuildFrame();

	private:
		DrawCallback m_drawCallback{ nullptr };
		std::atomic_bool m_mouseReleased{ false };
	};
}
