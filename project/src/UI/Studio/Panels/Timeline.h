#pragma once

#include "UI/Studio/StudioWidgets.h"

#include <nlohmann/json_fwd.hpp>

namespace UI::Studio
{
	class StudioSession;

	// The ruler, a lane per selected element and per property, and key editing. Custom drawn.
	class Timeline
	{
	public:
		// Built from the selection. A group lane summarizes every property of its element.
		struct Lane
		{
			std::string label;
			std::vector<ProjectEx::PropertyRef> properties;
			std::optional<ProjectEx::ElementRef> group;
			std::map<int32_t, std::vector<ProjectEx::KeyRef>> keys;  // Frame -> keys drawn as one diamond.
		};

		explicit Timeline(StudioSession& a_session);
		void Draw();

		// Cached until the project, the selection or the collapsed groups change.
		[[nodiscard]] std::shared_ptr<const std::vector<Lane>> BuildLanes() const;
		// Routes context-dependent actions (delete, select all) to keys while the timeline has focus.
		[[nodiscard]] bool IsFocused() const { return m_focused; }
		// Passed to Play; also where First and Last Frame go.
		[[nodiscard]] const std::optional<std::pair<int32_t, int32_t>>& GetLoopRange() const { return m_loopRange; }

		// Its part of the "editor" section: zoom, scroll, loop range and collapsed lanes.
		void WriteSettings(nlohmann::json& a_editor) const;
		void ReadSettings(const nlohmann::json& a_editor);

	private:
		void DrawTransportBar();
		void DrawContextMenu();
		[[nodiscard]] float FrameToX(float a_frame, float a_areaMinX) const;
		[[nodiscard]] float XToFrame(float a_x, float a_areaMinX) const;

		StudioSession& m_session;
		ProjectEx* m_project;
		bool m_focused{ false };
		float m_zoom{ 8.0f };    // Pixels per frame.
		float m_scroll{ 0.0f };  // First visible frame.
		std::optional<std::pair<int32_t, int32_t>> m_loopRange;
		std::set<ProjectEx::ElementRef> m_collapsedGroups;
		std::vector<ProjectEx::PropertyRef> m_contextLaneProperties;  // The lane whose label was right-clicked.

		// BuildLanes cache.
		mutable std::shared_ptr<const std::vector<Lane>> m_lanes;
		mutable uint64_t m_lanesRevision{ 0 };
		mutable std::vector<ProjectEx::ElementRef> m_lanesSelection;
		mutable std::set<ProjectEx::ElementRef> m_lanesCollapsed;
	};
}
