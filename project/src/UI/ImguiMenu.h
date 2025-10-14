#pragma once
#include "UI/DX11Renderer.h"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "imgui/ImGuizmo.h"

namespace UI
{
	class ImguiMenu
	{
	public:
		static ImguiMenu* GetSingleton();
		static void Install(DX11Renderer* a_renderer);

		void Attach(DX11Renderer* a_renderer);
		void SetOpen(bool a_open);
		[[nodiscard]] bool IsOpen() const;
		void Draw();

	private:
		static void OnFrameThunk(DX11Renderer* a_renderer);

        void DrawStudioTab();

		DX11Renderer* m_renderer{ nullptr };
		bool m_isOpen{ false };
	};
}

