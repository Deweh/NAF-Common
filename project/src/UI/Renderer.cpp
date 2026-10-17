#include "Renderer.h"

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
		using ClipCursor_t = BOOL(WINAPI*)(const RECT*);

		ClipCursor_t g_originalClipCursor{ nullptr };

		RE::NiCamera* GetWorldCamera()
		{
#if defined TARGET_GAME_F4
			return RE::Main::WorldRootCamera();
#elif defined TARGET_GAME_SF
			const auto worldRoot = RE::GetWorldRoot();
			return worldRoot ? worldRoot->camera.get() : nullptr;
#endif
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

		// Geometry nearer to the camera plane than this is clipped away instead of projected.
		constexpr float kNearClipW = 0.1f;

		// Diamond faces, back half first so the front half draws over it, and its edges. Indices into
		// ScreenSpaceDiamond::vertices: start, right, front, left, back, end.
		constexpr std::array<std::array<uint8_t, 3>, 8> kDiamondFaces{ { { 0, 1, 4 }, { 0, 3, 4 }, { 5, 1, 4 }, { 5, 3, 4 },
			{ 0, 1, 2 }, { 0, 3, 2 }, { 5, 1, 2 }, { 5, 3, 2 } } };
		constexpr std::array<std::array<uint8_t, 2>, 12> kDiamondEdges{ { { 0, 3 }, { 0, 1 }, { 0, 2 }, { 0, 4 }, { 5, 3 }, { 5, 1 },
			{ 5, 2 }, { 5, 4 }, { 3, 2 }, { 2, 1 }, { 1, 4 }, { 4, 3 } } };

		// Laid out like NiCamera::worldToCam: row r dotted with (x, y, z, 1) is clip component r (x, y, z, w).
		struct WorldToClip
		{
			float m[4][4];
		};

#if defined TARGET_GAME_F4
		// The view-projection the renderer drew a_camera's view with this frame. The NiCamera may have been
		// updated for the next frame by the time Present runs, so its worldToCam can't be relied on to match the image.
		std::optional<WorldToClip> GetRenderedWorldToClip(const RE::NiCamera& a_camera)
		{
			// The renderer state's address is only known for the pre-NG runtime.
			if (!REL::Module::IsF4()) return std::nullopt;
			static REL::Relocation<RE::BSGraphics::State*> state{ REL::ID(600795) };

			const RE::BSGraphics::CameraStateData* data = nullptr;
			for (const auto& entry : state->cameraDataCache) {
				if (entry.referenceCamera == &a_camera) {
					data = &entry;
					break;
				}
			}
			if (!data && state->cameraState.referenceCamera == &a_camera) data = &state->cameraState;
			if (!data) return std::nullopt;

			// Unjittered, so the overlay doesn't shimmer with TAA. The renderer works relative to posAdjust.
			float source[4][4];
			for (size_t row = 0; row < 4; ++row) _mm_storeu_ps(source[row], data->camViewData.viewProjUnjittered[row]);
			const auto& adjust = data->posAdjust;

			// The matrix's orientation isn't documented, so take the reading whose depth (w) row matches the
			// NiCamera's: it points along the camera's forward axis and puts a point ahead of the camera at that
			// distance. Neither matching means the data isn't what's expected, and the caller falls back.
			const RE::NiPoint3 forward{ a_camera.world.rotate[0][0], a_camera.world.rotate[0][1], a_camera.world.rotate[0][2] };
			constexpr float kProbeDistance = 100.0f;
			const RE::NiPoint3 probe{
				a_camera.world.translate.x + forward.x * kProbeDistance,
				a_camera.world.translate.y + forward.y * kProbeDistance,
				a_camera.world.translate.z + forward.z * kProbeDistance
			};
			for (const bool rowVector : { true, false }) {
				WorldToClip result;
				for (size_t r = 0; r < 4; ++r) {
					for (size_t c = 0; c < 4; ++c) result.m[r][c] = rowVector ? source[c][r] : source[r][c];
					result.m[r][3] -= result.m[r][0] * adjust.x + result.m[r][1] * adjust.y + result.m[r][2] * adjust.z;
				}
				const auto& w = result.m[3];
				const float axisLength = std::sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
				if (axisLength <= 0.000001f) continue;
				const float alignment = (w[0] * forward.x + w[1] * forward.y + w[2] * forward.z) / axisLength;
				// Loose enough for a frame of fast camera movement between the render and now.
				const float depth = w[0] * probe.x + w[1] * probe.y + w[2] * probe.z + w[3];
				if (alignment > 0.9f && std::abs(depth - kProbeDistance) < 2000.0f) return result;
			}
			return std::nullopt;
		}
#endif

		WorldToClip ComputeWorldToClip(const RE::NiCamera& a_camera)
		{
#if defined TARGET_GAME_F4
			static std::optional<bool> usingRendered;
			const auto rendered = GetRenderedWorldToClip(a_camera);
			if (usingRendered != rendered.has_value()) {
				usingRendered = rendered.has_value();
				logger::info("Viewport overlay projection: {}", rendered ? "renderer camera state" : "NiCamera::worldToCam (fallback)");
			}
			if (rendered) return *rendered;
#endif
			WorldToClip result;
			std::memcpy(result.m, a_camera.worldToCam, sizeof(result.m));
			return result;
		}

		// The camera doesn't move within a frame, and the overlay projects hundreds of points per frame, so the
		// lookup (camera state search, transpose and probe) runs once per ImGui frame and camera.
		WorldToClip GetWorldToClip(const RE::NiCamera& a_camera)
		{
			struct Cache
			{
				int frame{ -1 };
				const RE::NiCamera* camera{ nullptr };
				WorldToClip value;
			};
			static Cache cache;
			const int frame = ImGui::GetFrameCount();
			if (cache.frame != frame || cache.camera != &a_camera) cache = { frame, &a_camera, ComputeWorldToClip(a_camera) };
			return cache.value;
		}

		ClipVertex ToClip(const WorldToClip& a_worldToClip, const RE::NiPoint3& a_world)
		{
			const auto& m = a_worldToClip.m;
			return {
				(a_world.x * m[0][0]) + (a_world.y * m[0][1]) + (a_world.z * m[0][2]) + m[0][3],
				(a_world.x * m[1][0]) + (a_world.y * m[1][1]) + (a_world.z * m[1][2]) + m[1][3],
				(a_world.x * m[3][0]) + (a_world.y * m[3][1]) + (a_world.z * m[3][2]) + m[3][3]
			};
		}

		// Only valid for vertices in front of the near plane.
		ImVec2 ToScreen(const ClipVertex& a_vertex)
		{
			const ImGuiIO& io = ImGui::GetIO();
			const float x = ((a_vertex.x / a_vertex.w) + 1.0f) * 0.5f;
			const float y = (1.0f - (a_vertex.y / a_vertex.w)) * 0.5f;
			return ImVec2(x * io.DisplaySize.x, y * io.DisplaySize.y);
		}

		ClipVertex IntersectNearPlane(const ClipVertex& a_inside, const ClipVertex& a_outside)
		{
			const float t = (kNearClipW - a_inside.w) / (a_outside.w - a_inside.w);
			return { a_inside.x + ((a_outside.x - a_inside.x) * t), a_inside.y + ((a_outside.y - a_inside.y) * t), kNearClipW };
		}

		// Sutherland-Hodgman against the near plane, then projected. One plane cuts a triangle into at most 4 vertices.
		size_t ClipTriangle(const ClipVertex& a_a, const ClipVertex& a_b, const ClipVertex& a_c, std::array<ImVec2, 4>& a_out)
		{
			const std::array<ClipVertex, 3> input{ a_a, a_b, a_c };
			size_t count = 0;
			for (size_t i = 0; i < input.size(); ++i) {
				const auto& current = input[i];
				const auto& next = input[(i + 1) % input.size()];
				const bool currentInside = current.w >= kNearClipW;
				const bool nextInside = next.w >= kNearClipW;
				if (currentInside) a_out[count++] = ToScreen(current);
				if (currentInside != nextInside) a_out[count++] = ToScreen(currentInside ? IntersectNearPlane(current, next) : IntersectNearPlane(next, current));
			}
			return count;
		}

		bool ClipSegment(const ClipVertex& a_start, const ClipVertex& a_end, ImVec2& a_outStart, ImVec2& a_outEnd)
		{
			const bool startInside = a_start.w >= kNearClipW;
			const bool endInside = a_end.w >= kNearClipW;
			if (!startInside && !endInside) return false;
			a_outStart = ToScreen(startInside ? a_start : IntersectNearPlane(a_end, a_start));
			a_outEnd = ToScreen(endInside ? a_end : IntersectNearPlane(a_start, a_end));
			return true;
		}

		bool PointInConvexPolygon(const ImVec2& a_point, std::span<const ImVec2> a_polygon)
		{
			for (size_t i = 1; i + 1 < a_polygon.size(); ++i) {
				if (PointInTriangle(a_point, a_polygon[0], a_polygon[i], a_polygon[i + 1])) return true;
			}
			return false;
		}

		bool IsGameInputMessage(UINT a_msg)
		{
			return a_msg == WM_INPUT ||
			       (a_msg >= WM_KEYFIRST && a_msg <= WM_KEYLAST) ||
			       (a_msg >= WM_MOUSEFIRST && a_msg <= WM_MOUSELAST);
		}

		// Whether the message releases a key or mouse button. Releases still reach the game, so one held down when the
		// overlay took the input (like the key that opened it) isn't stuck down in the game.
		bool IsReleaseMessage(UINT a_msg, LPARAM a_lparam)
		{
			switch (a_msg) {
			case WM_KEYUP:
			case WM_SYSKEYUP:
			case WM_LBUTTONUP:
			case WM_RBUTTONUP:
			case WM_MBUTTONUP:
			case WM_XBUTTONUP:
				return true;
			case WM_INPUT:
				{
					RAWINPUT input;
					UINT size = sizeof(input);
					if (GetRawInputData(reinterpret_cast<HRAWINPUT>(a_lparam), RID_INPUT, &input, &size, sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1))
						return false;
					if (input.header.dwType == RIM_TYPEKEYBOARD)
						return (input.data.keyboard.Flags & RI_KEY_BREAK) != 0;
					if (input.header.dwType == RIM_TYPEMOUSE) {
						constexpr USHORT kButtonUps = RI_MOUSE_BUTTON_1_UP | RI_MOUSE_BUTTON_2_UP | RI_MOUSE_BUTTON_3_UP | RI_MOUSE_BUTTON_4_UP | RI_MOUSE_BUTTON_5_UP;
						return (input.data.mouse.usButtonFlags & kButtonUps) != 0;
					}
					return false;
				}
			default:
				return false;
			}
		}

		BOOL WINAPI ClipCursorHook(const RECT* a_rect)
		{
			auto* renderer = Renderer::GetSingleton();
			if (!renderer->IsMouseReleased()) {
				return g_originalClipCursor(a_rect);
			}

			return g_originalClipCursor(nullptr);

			HWND hwnd = renderer->m_hwnd;
			if (!hwnd) {
#if defined TARGET_GAME_F4
				if (const auto gameRenderer = RE::BSGraphics::RendererData::GetSingleton(); gameRenderer) {
					hwnd = reinterpret_cast<HWND>(gameRenderer->renderWindow[0].hwnd);
				}
#endif
			}

			RECT clientRect{};
			POINT topLeft{};
			POINT bottomRight{};
			if (hwnd && GetClientRect(hwnd, &clientRect)) {
				topLeft = POINT{ clientRect.left, clientRect.top };
				bottomRight = POINT{ clientRect.right, clientRect.bottom };
				if (ClientToScreen(hwnd, &topLeft) && ClientToScreen(hwnd, &bottomRight)) {
					const RECT windowClip{ topLeft.x, topLeft.y, bottomRight.x, bottomRight.y };
					return g_originalClipCursor(&windowClip);
				}
			}

			// If the window is temporarily unavailable, removing the clip is safer than
			// allowing the game to restore its smaller center-screen rectangle.
			return g_originalClipCursor(nullptr);
		}

		LRESULT CALLBACK WndProc(HWND a_hwnd, UINT a_msg, WPARAM a_wparam, LPARAM a_lparam)
		{
			auto* renderer = Renderer::GetSingleton();
			if (renderer->m_initialized && ImGui_ImplWin32_WndProcHandler(a_hwnd, a_msg, a_wparam, a_lparam)) {
				return true;
			}

			// While the overlay has the mouse, the game doesn't get the window's keyboard and mouse messages. Both games
			// read keyboard and mouse from these: Fallout 4's WinProc feeds WM_INPUT to its input devices
			// (BSPCKeyboardDevice::ProcessWinPCRawInput). Gamepads aren't affected.
			if (renderer->IsMouseReleased() && IsGameInputMessage(a_msg) && !IsReleaseMessage(a_msg, a_lparam)) {
				return a_msg == WM_INPUT ? DefWindowProc(a_hwnd, a_msg, a_wparam, a_lparam) : 0;
			}

			return CallWindowProc(renderer->m_originalWndProc, a_hwnd, a_msg, a_wparam, a_lparam);
		}

	}

	void Renderer::InstallClipCursorHook()
	{
		if (g_originalClipCursor) {
			return;
		}

#if defined TARGET_GAME_F4
		g_originalClipCursor = reinterpret_cast<ClipCursor_t>(REL::PatchIAT(ClipCursorHook, "USER32.dll", "ClipCursor"));
#elif defined TARGET_GAME_SF
		// This is the import table slot, not the function it points to.
		if (const auto slot = static_cast<ClipCursor_t*>(REX::FModule::GetExecutingModule().GetImportFunctionPointer("ClipCursor", "USER32.dll"))) {
			g_originalClipCursor = *slot;
			REL::WriteSafeData(slot, &ClipCursorHook);
		}
#endif
		if (g_originalClipCursor) {
			logger::info("Installed ClipCursor IAT hook.");
		} else {
			logger::warn("Failed to install ClipCursor IAT hook.");
		}
	}

	bool Renderer::InitializeImGui(HWND a_hwnd)
	{
		m_hwnd = a_hwnd;

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

		ImGui::StyleColorsDark();

		if (!ImGui_ImplWin32_Init(m_hwnd)) {
			logger::warn("Failed to initialize the ImGui Win32 backend.");
			ImGui::DestroyContext();
			return false;
		}

		m_originalWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtr(m_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WndProc)));
		return true;
	}

	void Renderer::ShutdownImGui()
	{
		if (m_originalWndProc && m_hwnd) {
			SetWindowLongPtr(m_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(m_originalWndProc));
			m_originalWndProc = nullptr;
		}

		if (ImGui::GetCurrentContext()) {
			ImGui_ImplWin32_Shutdown();
			ImGui::DestroyContext();
		}
	}

	void Renderer::BuildFrame()
	{
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		if (m_drawCallback) {
			m_drawCallback(this);
		}

		ImGui::Render();
	}

	void Renderer::SetDrawCallback(DrawCallback a_callback)
	{
		m_drawCallback = a_callback;
	}

	void Renderer::SetMouseReleased(bool a_release)
	{
		m_mouseReleased.store(a_release, std::memory_order_release);

		ShowCursor(a_release ? TRUE : FALSE);

		if (a_release && g_originalClipCursor) {
			ClipCursorHook(nullptr);
		}
	}

	bool Renderer::IsMouseReleased() const
	{
		return m_mouseReleased.load(std::memory_order_acquire);
	}

	bool Renderer::WorldToScreen(const RE::NiPoint3& a_world, ImVec2& a_out) const
	{
		float depth = 0.0f;
		return WorldToScreen(a_world, a_out, depth);
	}

	bool Renderer::WorldToScreen(const RE::NiPoint3& a_world, ImVec2& a_out, float& a_depth) const
	{
		const auto camera = GetWorldCamera();
		if (!camera) {
			return false;
		}

		a_depth = WorldToScreenNormalized(*camera, a_world).z;
		const auto clip = ToClip(GetWorldToClip(*camera), a_world);
		if (clip.w < kNearClipW) return false;  // Behind the camera; projecting would mirror it on screen.
		a_out = ToScreen(clip);
		return true;
	}

	bool Renderer::GetGizmoCameraMatrices(ozz::math::Float4x4& a_view, ozz::math::Float4x4& a_projection, bool& a_orthographic) const
	{
		const auto camera = GetWorldCamera();
		if (!camera) return false;

		const auto gameCameraWorld = *reinterpret_cast<const ozz::math::Float4x4*>(std::addressof(camera->world));

		// Fallout camera space is forward/right/up, while ImGuizmo expects
		// right/up/forward. Present the same camera with the axes reordered so
		// ImGuizmo's screen-size calculation uses the actual camera-right axis.
		ozz::math::Float4x4 cameraWorld;
		cameraWorld.cols[0] = gameCameraWorld.cols[1];
		cameraWorld.cols[1] = gameCameraWorld.cols[2];
		cameraWorld.cols[2] = gameCameraWorld.cols[0];
		cameraWorld.cols[3] = gameCameraWorld.cols[3];
		ozz::math::SimdInt4 invertible;
		a_view = ozz::math::Invert(cameraWorld, &invertible);
		if (!ozz::math::AreAllTrue1(invertible)) return false;

		const auto source = GetWorldToClip(*camera);
		ozz::math::Float4x4 worldToClip;
		for (size_t column = 0; column < 4; ++column) {
			float values[4];
			for (size_t row = 0; row < 4; ++row) values[row] = source.m[row][column];
			worldToClip.cols[column] = ozz::math::simd_float4::LoadPtrU(values);
		}
		// The world-to-clip transform is complete on its own. Factor out the reordered view to obtain its matching
		// projection; together they reproduce it exactly, even if the camera has moved since the render.
		a_projection = worldToClip * cameraWorld;
		a_orthographic = camera->viewFrustum.ortho;
		return true;
	}

	bool Renderer::BuildScreenSpaceDiamond(const RE::NiPoint3& a_center, float a_size, ScreenSpaceDiamond& a_outDiamond) const
	{
		const RE::NiPoint3 start{ a_center.x, a_center.y, a_center.z + a_size };
		const RE::NiPoint3 end{ a_center.x, a_center.y, a_center.z - a_size };
		return BuildScreenSpaceDiamond(start, end, a_size, 0.0f, a_outDiamond);
	}

	bool Renderer::BuildScreenSpaceDiamond(const RE::NiPoint3& a_start, const RE::NiPoint3& a_end, float a_halfWidth, float a_rollRadians, ScreenSpaceDiamond& a_outDiamond) const
	{
		a_outDiamond = {};

		const auto camera = GetWorldCamera();
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

		const auto worldToClip = GetWorldToClip(*camera);
		const RE::NiPoint3 points[6]{ a_start, right, front, left, back, a_end };
		for (size_t i = 0; i < std::size(points); ++i) {
			a_outDiamond.vertices[i] = ToClip(worldToClip, points[i]);
			a_outDiamond.valid |= a_outDiamond.vertices[i].w >= kNearClipW;
		}
		a_outDiamond.depth = WorldToScreenNormalized(*camera, center).z;
		a_outDiamond.halfWidth = a_halfWidth;
		return a_outDiamond.valid;
	}

	void Renderer::DrawWorldLine(const RE::NiPoint3& a_start, const RE::NiPoint3& a_end, ImU32 a_color, float a_thickness) const
	{
		const auto camera = GetWorldCamera();
		if (!camera) {
			return;
		}

		ImVec2 start;
		ImVec2 end;
		const auto worldToClip = GetWorldToClip(*camera);
		if (ClipSegment(ToClip(worldToClip, a_start), ToClip(worldToClip, a_end), start, end)) {
			ImGui::GetBackgroundDrawList()->AddLine(start, end, a_color, a_thickness);
		}
	}

	void Renderer::DrawWorldDiamond(const RE::NiPoint3& a_center, float a_size, ImU32 a_color, float a_thickness, float a_rollRadians) const
	{
		const RE::NiPoint3 start{ a_center.x, a_center.y, a_center.z + a_size };
		const RE::NiPoint3 end{ a_center.x, a_center.y, a_center.z - a_size };
		DrawWorldDiamond(start, end, a_size, a_color, a_thickness, a_rollRadians);
	}

	void Renderer::DrawWorldDiamond(const RE::NiPoint3& a_start, const RE::NiPoint3& a_end, float a_halfWidth, ImU32 a_color, float a_thickness, float a_rollRadians) const
	{
		ScreenSpaceDiamond diamond;
		if (!BuildScreenSpaceDiamond(a_start, a_end, a_halfWidth, a_rollRadians, diamond)) {
			return;
		}

		DrawWorldDiamond(diamond, a_color, a_thickness);
	}

    void Renderer::DrawWorldDiamond(const ScreenSpaceDiamond& diamond, ImU32 a_color, float a_thickness) const
	{
		if (!diamond.valid) {
			return;
		}

		auto* drawList = ImGui::GetBackgroundDrawList();
		const auto shadedColor = ImGui::ColorConvertU32ToFloat4(a_color);
		const ImU32 backFaceColor = ImGui::ColorConvertFloat4ToU32(ImVec4(shadedColor.x * 0.35f, shadedColor.y * 0.35f, shadedColor.z * 0.35f, shadedColor.w * 0.35f));
		const ImU32 frontFaceColor = ImGui::ColorConvertFloat4ToU32(ImVec4(shadedColor.x * 0.75f, shadedColor.y * 0.75f, shadedColor.z * 0.75f, shadedColor.w * 0.18f));

		// Faces and edges are clipped at the near plane, so a diamond reaching behind the camera is cut off cleanly.
		std::array<ImVec2, 4> polygon;
		for (size_t face = 0; face < kDiamondFaces.size(); ++face) {
			const auto& indices = kDiamondFaces[face];
			const size_t count = ClipTriangle(diamond.vertices[indices[0]], diamond.vertices[indices[1]], diamond.vertices[indices[2]], polygon);
			if (count >= 3) drawList->AddConvexPolyFilled(polygon.data(), static_cast<int>(count), face < 4 ? backFaceColor : frontFaceColor);
		}
		for (const auto& edge : kDiamondEdges) {
			ImVec2 start;
			ImVec2 end;
			if (ClipSegment(diamond.vertices[edge[0]], diamond.vertices[edge[1]], start, end)) drawList->AddLine(start, end, a_color, a_thickness);
		}
	}

	bool Renderer::IsScreenPointInsideDiamond(const ImVec2& a_screenPoint, const ScreenSpaceDiamond& a_diamond) const
	{
		if (!a_diamond.valid) {
			return false;
		}

		// With every vertex in front of the camera, the outline lies within the projected vertices' bounds, so most
		// diamonds are rejected without clipping their faces.
		if (std::ranges::all_of(a_diamond.vertices, [](const ClipVertex& a_vertex) { return a_vertex.w >= kNearClipW; })) {
			ImVec2 min{ FLT_MAX, FLT_MAX };
			ImVec2 max{ -FLT_MAX, -FLT_MAX };
			for (const auto& vertex : a_diamond.vertices) {
				const auto screen = ToScreen(vertex);
				min = { (std::min)(min.x, screen.x), (std::min)(min.y, screen.y) };
				max = { (std::max)(max.x, screen.x), (std::max)(max.y, screen.y) };
			}
			if (a_screenPoint.x < min.x || a_screenPoint.x > max.x || a_screenPoint.y < min.y || a_screenPoint.y > max.y) return false;
		}

		// Any face covers the point; together they make up the diamond's whole visible outline.
		std::array<ImVec2, 4> polygon;
		for (const auto& indices : kDiamondFaces) {
			const size_t count = ClipTriangle(a_diamond.vertices[indices[0]], a_diamond.vertices[indices[1]], a_diamond.vertices[indices[2]], polygon);
			if (count >= 3 && PointInConvexPolygon(a_screenPoint, std::span{ polygon.data(), count })) return true;
		}
		return false;
	}

	bool Renderer::IsScreenPointInsideDiamond(const ImVec2& a_screenPoint, const RE::NiPoint3& a_center, float a_size, float a_rollRadians) const
	{
		const RE::NiPoint3 start{ a_center.x, a_center.y, a_center.z + a_size };
		const RE::NiPoint3 end{ a_center.x, a_center.y, a_center.z - a_size };
		return IsScreenPointInsideDiamond(a_screenPoint, start, end, a_size, a_rollRadians);
	}

	bool Renderer::IsScreenPointInsideDiamond(const ImVec2& a_screenPoint, const RE::NiPoint3& a_start, const RE::NiPoint3& a_end, float a_halfWidth, float a_rollRadians) const
	{
		ScreenSpaceDiamond diamond;
		return BuildScreenSpaceDiamond(a_start, a_end, a_halfWidth, a_rollRadians, diamond) && IsScreenPointInsideDiamond(a_screenPoint, diamond);
	}
}
