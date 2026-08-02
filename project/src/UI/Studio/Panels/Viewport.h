#pragma once

#include "UI/Studio/StudioWidgets.h"

#include <nlohmann/json_fwd.hpp>

namespace UI::Studio
{
	class StudioSession;
	struct GizmoTarget;

	// The overlay on the game view: bones, locators, IK lines and the scene center, with picking and the gizmo.
	class Viewport
	{
	public:
		struct ViewOptions
		{
			bool bones{ true };
			bool locators{ true };
			bool ikLines{ true };  // Lines from each chain to its effector and pole.
			bool sceneCenter{ true };
		};

		explicit Viewport(StudioSession& a_session);
		void Draw(DX11Renderer* a_renderer);

		// What the gizmo would move: bones not driven by IK, locators, and actors (their placement). Stops at a_limit.
		[[nodiscard]] std::vector<GizmoTarget> CollectGizmoTargets(size_t a_limit = SIZE_MAX);

		// Its part of the "editor" section.
		void WriteSettings(nlohmann::json& a_editor) const;
		void ReadSettings(const nlohmann::json& a_editor);

		ViewOptions view;

	private:
		void DrawGizmo(DX11Renderer* a_renderer);

		StudioSession& m_session;
		ProjectEx* m_project;
		std::optional<ProjectEx::ElementRef> m_hovered;  // Hit test result for this frame.
	};
}
