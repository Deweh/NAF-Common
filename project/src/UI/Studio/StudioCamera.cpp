#include "UI/Studio/StudioCamera.h"

#include "UI/Studio/StudioWidgets.h"
#include "imgui/ImGuizmo.h"

namespace UI::Studio
{
#if defined TARGET_GAME_F4
	namespace
	{
		RE::FreeCameraState* GetFreeCamera()
		{
			const auto camera = RE::PlayerCamera::GetSingleton();
			if (!camera || !camera->QCameraEquals(RE::CameraStates::kFree)) return nullptr;
			return static_cast<RE::FreeCameraState*>(camera->currentState.get());
		}

		// Only the free camera state: the game moves the camera from it at its next update, like it does for mouse look.
		// Moving the camera node here as well would change it in the middle of a frame, after that frame was rendered.
		void MoveFreeCamera(RE::FreeCameraState& a_freeCamera, const RE::NiPoint3& a_offset)
		{
			a_freeCamera.translation.x += a_offset.x;
			a_freeCamera.translation.y += a_offset.y;
			a_freeCamera.translation.z += a_offset.z;
		}
	}

	void StudioCamera::SetEnabled(bool a_enabled)
	{
		if (m_enabled == a_enabled) return;
		const auto camera = RE::PlayerCamera::GetSingleton();
		if (!camera) return;

		if (a_enabled) {
			m_activatedFreeCamera = !camera->QCameraEquals(RE::CameraStates::kFree);
			if (m_activatedFreeCamera) camera->ToggleFreeCameraMode(false);
		} else {
			camera->rotationInput = {};
			camera->translationInput = {};
			camera->zoomInput = 0.0f;
			if (m_activatedFreeCamera && camera->QCameraEquals(RE::CameraStates::kFree)) camera->ToggleFreeCameraMode(false);
			m_activatedFreeCamera = false;
			if (m_mouseLooking) ImGui::GetIO().MouseDrawCursor = true;
			m_mouseLooking = false;
		}
		m_enabled = a_enabled;
	}

	void StudioCamera::Update()
	{
		if (!m_enabled) return;
		auto& io = ImGui::GetIO();

		// Mouse look runs while the right mouse button is held, if it was pressed outside the studio windows.
		if (!m_mouseLooking) {
			if (!ImGui::IsMouseClicked(ImGuiMouseButton_Right) || io.WantCaptureMouse || ImGuizmo::IsUsing()) return;
			m_mouseLooking = true;
			GetCursorPos(&m_mouseLockPosition);
			io.MouseDrawCursor = false;
		} else if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
			m_mouseLooking = false;
			io.MouseDrawCursor = true;
			return;
		}

		POINT cursor{};
		GetCursorPos(&cursor);
		const float mouseDeltaX = static_cast<float>(cursor.x - m_mouseLockPosition.x);
		const float mouseDeltaY = static_cast<float>(cursor.y - m_mouseLockPosition.y);
		if (cursor.x != m_mouseLockPosition.x || cursor.y != m_mouseLockPosition.y) SetCursorPos(m_mouseLockPosition.x, m_mouseLockPosition.y);

		const auto camera = RE::PlayerCamera::GetSingleton();
		const auto freeCamera = GetFreeCamera();
		if (!camera || !freeCamera) return;

		constexpr float kLookSensitivity = 0.0015f;
		constexpr float kPitchLimit = 1.553343f;
		const float pitch = std::remainder(freeCamera->rotation.x, 2.0f * std::numbers::pi_v<float>);
		freeCamera->rotation.x = std::clamp(pitch + mouseDeltaY * kLookSensitivity, -kPitchLimit, kPitchLimit);
		freeCamera->rotation.y = std::remainder(freeCamera->rotation.y + mouseDeltaX * kLookSensitivity, 2.0f * std::numbers::pi_v<float>);

		if (io.MouseWheel != 0.0f) m_moveSpeed = std::clamp(m_moveSpeed * std::pow(1.2f, io.MouseWheel), 0.05f, 20.0f);

		if (!camera->cameraRoot) return;
		const float deltaTime = std::clamp(io.DeltaTime, 1.0f / 240.0f, 1.0f / 15.0f);
		const float distance = 300.0f * kScaleMultiplier * m_moveSpeed * (ImGui::IsKeyDown(ImGuiKey_LeftShift) ? 4.0f : 1.0f) * deltaTime;
		const float forwardInput = (ImGui::IsKeyDown(ImGuiKey_W) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_S) ? 1.0f : 0.0f);
		const float rightInput = (ImGui::IsKeyDown(ImGuiKey_D) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_A) ? 1.0f : 0.0f);
		const float verticalInput = (ImGui::IsKeyDown(ImGuiKey_E) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_Q) ? 1.0f : 0.0f);
		if (forwardInput == 0.0f && rightInput == 0.0f && verticalInput == 0.0f) return;

		// Camera rows: 0 is right, 1 is forward.
		const auto& rotation = camera->cameraRoot->world.rotate;
		const RE::NiPoint3 right{ rotation[0][0], rotation[0][1], rotation[0][2] };
		const RE::NiPoint3 forward{ rotation[1][0], rotation[1][1], rotation[1][2] };
		MoveFreeCamera(*freeCamera, {
			(forwardInput * forward.x + rightInput * right.x) * distance,
			(forwardInput * forward.y + rightInput * right.y) * distance,
			(forwardInput * forward.z + rightInput * right.z + verticalInput) * distance });
	}

	void StudioCamera::Focus(const ozz::math::Float3& a_target)
	{
		const auto camera = RE::PlayerCamera::GetSingleton();
		const auto freeCamera = GetFreeCamera();
		if (!camera || !freeCamera || !camera->cameraRoot) return;
		// Keep the current orientation and back away from the target along the view direction.
		const auto& world = camera->cameraRoot->world;
		const RE::NiPoint3 forward{ world.rotate[1][0], world.rotate[1][1], world.rotate[1][2] };
		const RE::NiPoint3 destination{ a_target.x - forward.x * kFocusDistance, a_target.y - forward.y * kFocusDistance, a_target.z - forward.z * kFocusDistance };
		MoveFreeCamera(*freeCamera, { destination.x - world.translate.x, destination.y - world.translate.y, destination.z - world.translate.z });
	}
#elif defined TARGET_GAME_SF
	namespace
	{
		RE::FreeCamera::State* GetFreeCamera()
		{
			return RE::FreeCamera::GetState(RE::PlayerCamera::GetSingleton());
		}

		// From the free camera's own angles, so this doesn't depend on which camera matrix row is forward.
		RE::NiPoint3 GetForward(const RE::FreeCamera::State& a_freeCamera)
		{
			const float cosPitch = std::cos(a_freeCamera.pitch);
			return { std::sin(a_freeCamera.yaw) * cosPitch, std::cos(a_freeCamera.yaw) * cosPitch, -std::sin(a_freeCamera.pitch) };
		}

		RE::NiPoint3 GetRight(const RE::FreeCamera::State& a_freeCamera)
		{
			return { std::cos(a_freeCamera.yaw), -std::sin(a_freeCamera.yaw), 0.0f };
		}
	}

	// Mirrors `tfc` (0x1409fa6a0) without its time freeze, input context and HUD changes: the studio blocks game input itself.
	void StudioCamera::SetEnabled(bool a_enabled)
	{
		if (m_enabled == a_enabled) return;
		const auto camera = RE::PlayerCamera::GetSingleton();
		if (!camera) return;

		if (a_enabled) {
			m_activatedFreeCamera = !RE::FreeCamera::IsActive(camera) && !RE::FreeCamera::IsBlocked(camera);
			if (m_activatedFreeCamera) {
				m_cameraWasEnabled = camera->enabled;
				camera->ToggleFreeCameraMode(RE::FreeCamera::kStateIndex, false);
				camera->SetEnabled(true);
			}
		} else {
			if (m_activatedFreeCamera && RE::FreeCamera::IsActive(camera)) {
				camera->ToggleFreeCameraMode(RE::FreeCamera::kOff, false);
				camera->SetEnabled(m_cameraWasEnabled);
			}
			m_activatedFreeCamera = false;
			if (m_mouseLooking) ImGui::GetIO().MouseDrawCursor = true;
			m_mouseLooking = false;
		}
		m_enabled = a_enabled;
	}

	void StudioCamera::Update()
	{
		if (!m_enabled) return;
		auto& io = ImGui::GetIO();

		// Mouse look runs while the right mouse button is held, if it was pressed outside the studio windows.
		if (!m_mouseLooking) {
			if (!ImGui::IsMouseClicked(ImGuiMouseButton_Right) || io.WantCaptureMouse || ImGuizmo::IsUsing()) return;
			m_mouseLooking = true;
			GetCursorPos(&m_mouseLockPosition);
			io.MouseDrawCursor = false;
		} else if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
			m_mouseLooking = false;
			io.MouseDrawCursor = true;
			return;
		}

		POINT cursor{};
		GetCursorPos(&cursor);
		const float mouseDeltaX = static_cast<float>(cursor.x - m_mouseLockPosition.x);
		const float mouseDeltaY = static_cast<float>(cursor.y - m_mouseLockPosition.y);
		if (cursor.x != m_mouseLockPosition.x || cursor.y != m_mouseLockPosition.y) SetCursorPos(m_mouseLockPosition.x, m_mouseLockPosition.y);

		const auto freeCamera = GetFreeCamera();
		if (!freeCamera) return;

		// Same directions as the game's own mouse look: moving the mouse down or right increases pitch or yaw.
		constexpr float kLookSensitivity = 0.0015f;
		constexpr float kPitchLimit = 1.553343f;
		const float pitch = std::remainder(freeCamera->pitch, 2.0f * std::numbers::pi_v<float>);
		freeCamera->pitch = std::clamp(pitch + mouseDeltaY * kLookSensitivity, -kPitchLimit, kPitchLimit);
		freeCamera->yaw = std::remainder(freeCamera->yaw + mouseDeltaX * kLookSensitivity, 2.0f * std::numbers::pi_v<float>);

		if (io.MouseWheel != 0.0f) m_moveSpeed = std::clamp(m_moveSpeed * std::pow(1.2f, io.MouseWheel), 0.05f, 20.0f);

		const float deltaTime = std::clamp(io.DeltaTime, 1.0f / 240.0f, 1.0f / 15.0f);
		const float distance = 300.0f * kScaleMultiplier * m_moveSpeed * (ImGui::IsKeyDown(ImGuiKey_LeftShift) ? 4.0f : 1.0f) * deltaTime;
		const float forwardInput = (ImGui::IsKeyDown(ImGuiKey_W) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_S) ? 1.0f : 0.0f);
		const float rightInput = (ImGui::IsKeyDown(ImGuiKey_D) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_A) ? 1.0f : 0.0f);
		const float verticalInput = (ImGui::IsKeyDown(ImGuiKey_E) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(ImGuiKey_Q) ? 1.0f : 0.0f);
		if (forwardInput == 0.0f && rightInput == 0.0f && verticalInput == 0.0f) return;

		const auto forward = GetForward(*freeCamera);
		const auto right = GetRight(*freeCamera);
		freeCamera->translation.x += (forwardInput * forward.x + rightInput * right.x) * distance;
		freeCamera->translation.y += (forwardInput * forward.y + rightInput * right.y) * distance;
		freeCamera->translation.z += (forwardInput * forward.z + rightInput * right.z + verticalInput) * distance;
	}

	void StudioCamera::Focus(const ozz::math::Float3& a_target)
	{
		const auto freeCamera = GetFreeCamera();
		if (!freeCamera) return;
		// Keep the current orientation and back away from the target along the view direction.
		const auto forward = GetForward(*freeCamera);
		freeCamera->translation = { a_target.x - forward.x * kFocusDistance, a_target.y - forward.y * kFocusDistance, a_target.z - forward.z * kFocusDistance };
	}
#endif
}
