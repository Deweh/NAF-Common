#pragma once

#include "UI/Renderer.h"

namespace UI::Studio
{
	// The studio camera: the game's free camera, with mouse look on the right mouse button and WASD / Q / E movement.
	// It restores the previous camera state only if it changed it.
	class StudioCamera
	{
	public:
		void SetEnabled(bool a_enabled);
		[[nodiscard]] bool IsEnabled() const { return m_enabled; }
		// While the right mouse button is held after a press outside the windows. Keybinds and picking are off meanwhile.
		[[nodiscard]] bool IsMouseLooking() const { return m_mouseLooking; }
		[[nodiscard]] float& MoveSpeed() { return m_moveSpeed; }

		// Once per frame, before drawing.
		void Update();
		// Keeps the orientation and backs away from a_target along the view direction.
		void Focus(const ozz::math::Float3& a_target);

	private:
		bool m_enabled{ false };
		bool m_activatedFreeCamera{ false };  // Only restore the camera state the studio changed.
		bool m_cameraWasEnabled{ true };      // SF: the camera's enabled flag before the studio turned the free camera on, as `tfc` keeps it.
		bool m_mouseLooking{ false };
		POINT m_mouseLockPosition{};
		float m_moveSpeed{ 1.0f };
	};
}
