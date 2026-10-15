#include "UI/Studio/Panels/Timeline.h"

#include "UI/Studio/StudioSession.h"

#include <nlohmann/json.hpp>

namespace UI::Studio
{
	namespace
	{
		constexpr const char* kTimelineContextPopup = "##timeline_context";
		constexpr const char* kLaneContextPopup = "##lane_context";
		constexpr float kTimelineLabelWidth = 220.0f;
		constexpr float kTimelineRulerHeight = 22.0f;
		constexpr float kTimelineRowHeight = 20.0f;
		constexpr float kKeyRadius = 5.0f;
	}

	Timeline::Timeline(StudioSession& a_session) :
		m_session(a_session),
		m_project(&a_session.GetProject())
	{}

	void Timeline::Draw()
	{
		m_focused = false;
		if (!m_session.windows.timeline) return;
		if (!ImGui::Begin(kTimelineWindow, &m_session.windows.timeline)) {
			ImGui::End();
			return;
		}
		m_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

		DrawTransportBar();
		const auto sequence = GetActiveSequenceInfo(*m_project);
		const int32_t frameCount = sequence ? sequence->frameCount : 0;
		const auto& io = ImGui::GetIO();
		const ImVec2 mouse = io.MousePos;

		// ---- Ruler ----
		const ImVec2 rulerMin = ImGui::GetCursorScreenPos();
		const float width = (std::max)(ImGui::GetContentRegionAvail().x, kTimelineLabelWidth + 1.0f);
		const float areaMinX = rulerMin.x + kTimelineLabelWidth;
		const float areaMaxX = rulerMin.x + width;
		auto drawList = ImGui::GetWindowDrawList();
		ImGui::InvisibleButton("##ruler", { width, kTimelineRulerHeight });
		const bool rulerHovered = ImGui::IsItemHovered() && mouse.x >= areaMinX;

		drawList->AddRectFilled({ areaMinX, rulerMin.y }, { areaMaxX, rulerMin.y + kTimelineRulerHeight }, ImGui::GetColorU32(ImGuiCol_FrameBg));
		int32_t step = 1;
		for (const int32_t candidate : { 1, 2, 5, 10, 20, 50, 100, 200, 500, 1000 }) {
			step = candidate;
			if (candidate * m_zoom >= 40.0f) break;
		}
		drawList->PushClipRect({ areaMinX, rulerMin.y }, { areaMaxX, rulerMin.y + kTimelineRulerHeight }, true);
		if (m_loopRange) {
			drawList->AddRectFilled({ FrameToX(static_cast<float>(m_loopRange->first), areaMinX), rulerMin.y }, { FrameToX(static_cast<float>(m_loopRange->second), areaMinX), rulerMin.y + kTimelineRulerHeight }, IM_COL32(90, 160, 255, 50));
		}
		const int32_t firstTick = static_cast<int32_t>(std::floor(m_scroll / step)) * step;
		for (int32_t frame = firstTick; FrameToX(static_cast<float>(frame), areaMinX) < areaMaxX; frame += step) {
			const float x = FrameToX(static_cast<float>(frame), areaMinX);
			drawList->AddLine({ x, rulerMin.y + kTimelineRulerHeight * 0.6f }, { x, rulerMin.y + kTimelineRulerHeight }, ImGui::GetColorU32(ImGuiCol_TextDisabled));
			drawList->AddText({ x + 2.0f, rulerMin.y + 2.0f }, ImGui::GetColorU32(frame > frameCount ? ImGuiCol_TextDisabled : ImGuiCol_Text), std::to_string(frame).c_str());
		}
		const float playheadX = FrameToX(m_project->GetSampleFrame(), areaMinX);
		drawList->AddLine({ playheadX, rulerMin.y }, { playheadX, rulerMin.y + kTimelineRulerHeight }, kColorPlayhead, 2.0f);
		drawList->PopClipRect();

		// Scrubbing starts on the ruler and runs until release.
		if (rulerHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !m_session.IsInteracting()) m_session.BeginInteraction(ScrubInteraction{}, "Change frame");
		if (std::holds_alternative<ScrubInteraction>(m_session.interaction)) {
			if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) m_project->SetFrame(static_cast<int32_t>(std::lround(XToFrame(mouse.x, areaMinX))));
			else m_session.EndInteraction();
		}

		// ---- Lanes ----
		const auto lanePointer = BuildLanes();  // Kept alive for the whole draw.
		const auto& lanes = *lanePointer;
		ImGui::BeginChild("##timeline_lanes", { 0.0f, 0.0f }, false, ImGuiWindowFlags_NoScrollWithMouse);
		drawList = ImGui::GetWindowDrawList();
		const ImVec2 bodyMin = ImGui::GetCursorScreenPos();
		const ImVec2 clipMin = ImGui::GetWindowPos();
		const ImVec2 clipMax{ clipMin.x + ImGui::GetWindowWidth(), clipMin.y + ImGui::GetWindowHeight() };
		const bool bodyHovered = ImGui::IsWindowHovered();
		const bool inKeyArea = bodyHovered && mouse.x >= areaMinX && mouse.x < areaMaxX;

		std::optional<std::pair<size_t, int32_t>> hoveredKey;  // Lane, frame.
		for (size_t laneIndex = 0; laneIndex < lanes.size(); ++laneIndex) {
			const auto& lane = lanes[laneIndex];
			const float rowMinY = bodyMin.y + laneIndex * kTimelineRowHeight;
			const float rowMaxY = rowMinY + kTimelineRowHeight;
			if (rowMaxY < clipMin.y || rowMinY > clipMax.y) continue;
			const float rowCenterY = (rowMinY + rowMaxY) * 0.5f;

			// Row background and label.
			const bool selectedGroup = lane.group && m_session.IsSelected(*lane.group);
			drawList->AddRectFilled({ bodyMin.x, rowMinY }, { areaMaxX, rowMaxY },
				ImGui::GetColorU32(lane.group ? ImGuiCol_TableHeaderBg : (laneIndex % 2 ? ImGuiCol_TableRowBgAlt : ImGuiCol_TableRowBg)));
			if (lane.group) {
				const bool collapsed = m_collapsedGroups.contains(*lane.group);
				const float arrowX = bodyMin.x + 6.0f;
				if (collapsed) {
					drawList->AddTriangleFilled({ arrowX, rowCenterY - 4.0f }, { arrowX + 6.0f, rowCenterY }, { arrowX, rowCenterY + 4.0f }, ImGui::GetColorU32(ImGuiCol_Text));
				} else {
					drawList->AddTriangleFilled({ arrowX - 1.0f, rowCenterY - 3.0f }, { arrowX + 7.0f, rowCenterY - 3.0f }, { arrowX + 3.0f, rowCenterY + 3.0f }, ImGui::GetColorU32(ImGuiCol_Text));
				}
			}
			drawList->PushClipRect({ bodyMin.x, rowMinY }, { areaMinX - 4.0f, rowMaxY }, true);
			drawList->AddText({ bodyMin.x + (lane.group ? 18.0f : 30.0f), rowMinY + 3.0f }, selectedGroup ? kColorSelected : ImGui::GetColorU32(ImGuiCol_Text), lane.label.c_str());
			drawList->PopClipRect();

			// Label clicks: the arrow collapses, the rest selects the element.
			if (lane.group && bodyHovered && mouse.y >= rowMinY && mouse.y < rowMaxY && mouse.x < areaMinX && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !m_session.IsInteracting()) {
				if (mouse.x < bodyMin.x + 16.0f) {
					if (!m_collapsedGroups.erase(*lane.group)) m_collapsedGroups.insert(*lane.group);
				} else {
					m_session.Select(*lane.group, m_session.GetClickSelectMode());
				}
			}
			if (bodyHovered && mouse.y >= rowMinY && mouse.y < rowMaxY && mouse.x < areaMinX && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !m_session.IsInteracting()) {
				m_contextLaneProperties = lane.properties;
				ImGui::OpenPopup(kLaneContextPopup);
			}

			// Keys.
			drawList->PushClipRect({ areaMinX, rowMinY }, { areaMaxX, rowMaxY }, true);
			if (frameCount > 0) {
				const float endX = FrameToX(static_cast<float>(frameCount), areaMinX);
				if (endX < areaMaxX) drawList->AddRectFilled({ endX, rowMinY }, { areaMaxX, rowMaxY }, IM_COL32(0, 0, 0, 60));
			}
			for (const auto& [frame, refs] : lane.keys) {
				const float x = FrameToX(static_cast<float>(frame), areaMinX);
				if (x < areaMinX - kKeyRadius || x > areaMaxX + kKeyRadius) continue;
				const bool selected = std::ranges::all_of(refs, [&](const auto& a_ref) { return m_session.selectedKeys.contains(a_ref); });
				const bool hovered = inKeyArea && std::abs(mouse.x - x) <= kKeyRadius + 1.0f && mouse.y >= rowMinY && mouse.y < rowMaxY;
				if (hovered) hoveredKey = std::pair{ laneIndex, frame };
				DrawDiamond(drawList, { x, rowCenterY }, lane.group ? kKeyRadius - 1.0f : kKeyRadius, selected ? kColorSelected : hovered ? kColorHovered : kColorKey, IM_COL32(20, 20, 20, 255));
			}
			drawList->PopClipRect();
		}
		ImGui::Dummy({ width, (std::max)(static_cast<float>(lanes.size()) * kTimelineRowHeight, 1.0f) });

		// Playhead through the lanes.
		drawList->AddLine({ playheadX, clipMin.y }, { playheadX, clipMax.y }, kColorPlayhead, 1.0f);

		// ---- Input ----
		if (bodyHovered && !m_session.IsInteracting()) {
			if (io.MouseWheel != 0.0f) {
				if (io.KeyCtrl) {
					// Zoom around the mouse.
					const float anchor = XToFrame(mouse.x, areaMinX);
					m_zoom = std::clamp(m_zoom * std::pow(1.15f, io.MouseWheel), 1.0f, 80.0f);
					m_scroll = anchor - (mouse.x - areaMinX) / m_zoom;
				} else if (io.KeyShift) {
					m_scroll -= io.MouseWheel * 40.0f / m_zoom;
				} else {
					ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseWheel * kTimelineRowHeight * 3.0f);
				}
			}
			if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) m_scroll -= io.MouseDelta.x / m_zoom;

			if (inKeyArea && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
				if (hoveredKey) {
					const auto& refs = lanes[hoveredKey->first].keys.at(hoveredKey->second);
					const bool selected = std::ranges::all_of(refs, [&](const auto& a_ref) { return m_session.selectedKeys.contains(a_ref); });
					switch (m_session.GetClickSelectMode()) {
					case SelectMode::kReplace:
						if (!selected) m_session.selectedKeys = { refs.begin(), refs.end() };
						break;
					case SelectMode::kAdd:
						m_session.selectedKeys.insert(refs.begin(), refs.end());
						break;
					case SelectMode::kToggle:
						for (const auto& ref : refs) {
							if (selected) m_session.selectedKeys.erase(ref);
							else m_session.selectedKeys.insert(ref);
						}
						break;
					}
					// A click without movement produces an empty edit, which is dropped.
					if (!m_session.selectedKeys.empty()) m_session.BeginInteraction(KeyDragInteraction{ { m_session.selectedKeys.begin(), m_session.selectedKeys.end() }, mouse.x, 0 }, "Move keyframes");
				} else {
					m_session.BeginInteraction(BoxSelectInteraction{ mouse, io.KeyCtrl || io.KeyShift }, "Select keyframes");
				}
			}

			if (inKeyArea && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
				if (hoveredKey) {
					const auto& refs = lanes[hoveredKey->first].keys.at(hoveredKey->second);
					if (!std::ranges::all_of(refs, [&](const auto& a_ref) { return m_session.selectedKeys.contains(a_ref); })) m_session.selectedKeys = { refs.begin(), refs.end() };
				}
				ImGui::OpenPopup(kTimelineContextPopup);
			}
		}

		if (const auto drag = std::get_if<KeyDragInteraction>(&m_session.interaction)) {
			if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
				const auto delta = static_cast<int32_t>(std::lround((mouse.x - drag->startMouseX) / m_zoom));
				if (delta != drag->delta) {
					// Rebuild the move from the original keys, so keys passed over along the way survive.
					m_project->CancelEdit();
					if (const auto moved = m_project->MoveKeys(drag->original, delta)) m_session.selectedKeys = { moved->begin(), moved->end() };
					drag->delta = delta;
				}
			} else {
				m_session.EndInteraction();
			}
		}

		if (const auto box = std::get_if<BoxSelectInteraction>(&m_session.interaction)) {
			const ImVec2 boxMin{ (std::min)(box->start.x, mouse.x), (std::min)(box->start.y, mouse.y) };
			const ImVec2 boxMax{ (std::max)(box->start.x, mouse.x), (std::max)(box->start.y, mouse.y) };
			drawList->AddRectFilled(boxMin, boxMax, IM_COL32(110, 220, 255, 40));
			drawList->AddRect(boxMin, boxMax, kColorHovered);
			if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
				if (!box->additive) m_session.selectedKeys.clear();
				const float firstFrame = XToFrame(boxMin.x, areaMinX);
				const float lastFrame = XToFrame(boxMax.x, areaMinX);
				for (size_t laneIndex = 0; laneIndex < lanes.size(); ++laneIndex) {
					const float rowCenterY = bodyMin.y + (laneIndex + 0.5f) * kTimelineRowHeight;
					if (rowCenterY < boxMin.y || rowCenterY > boxMax.y) continue;
					for (const auto& [frame, refs] : lanes[laneIndex].keys) {
						if (frame >= firstFrame && frame <= lastFrame) m_session.selectedKeys.insert(refs.begin(), refs.end());
					}
				}
				m_session.EndInteraction();
			}
		}

		DrawContextMenu();
		ImGui::EndChild();
		ImGui::End();
	}

	void Timeline::DrawTransportBar()
	{
		const auto playShortcut = GetShortcutText(Action::kPlayPause);
		if (ImGui::Button(m_project->IsPlaying() ? "Pause" : "Play", { 60.0f, 0.0f })) m_session.ExecuteAction(Action::kPlayPause);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", playShortcut.c_str());
		ImGui::SameLine();
		int frame = m_project->GetFrame();
		ImGui::SetNextItemWidth(90.0f);
		if (ImGui::InputInt("##frame", &frame, 1, 10, ImGuiInputTextFlags_EnterReturnsTrue)) m_project->SetFrame(frame);
		if (const auto sequence = GetActiveSequenceInfo(*m_project)) {
			ImGui::SameLine();
			ImGui::TextDisabled("/ %d", sequence->frameCount);
		}
		if (m_loopRange) {
			ImGui::SameLine();
			ImGui::TextDisabled("| Loop %d - %d", m_loopRange->first, m_loopRange->second);
		}
		ImGui::SameLine();
		ImGui::TextDisabled("(?)");
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip(
				"Ruler: click or drag to scrub\n"
				"Keys: click to select, drag to move, drag empty space to box select\n"
				"Ctrl/Shift+click: toggle/add to selection\n"
				"Mouse wheel: scroll lanes  |  Shift+wheel or middle drag: pan  |  Ctrl+wheel: zoom\n"
				"Right-click: keyframe options  |  Escape: cancel a drag");
		}
	}

	std::shared_ptr<const std::vector<Timeline::Lane>> Timeline::BuildLanes() const
	{
		// Rebuilding reads every lane's keys, which for a whole actor is every bone's tracks. Only what the project, the
		// selection and the collapsed groups say goes into the lanes, so they're reused until one of those changes.
		const auto revision = m_project->GetRevision();
		if (m_lanes && m_lanesRevision == revision && m_lanesSelection == m_session.selection && m_lanesCollapsed == m_collapsedGroups) return m_lanes;

		auto lanes = std::make_shared<std::vector<Lane>>();
		const auto collectKeys = [&](Lane& a_lane) {
			for (const auto& property : a_lane.properties) {
				for (const auto frame : m_project->GetKeyFrames(property)) a_lane.keys[frame].push_back({ property, frame });
			}
		};

		for (const auto& element : m_session.selection) {
			Lane group{ m_session.GetElementLabel(element), m_session.GetElementProperties(element), element };
			collectKeys(group);
			lanes->push_back(std::move(group));

			// A whole actor stays a single summary lane; it would otherwise expand to every bone channel.
			const bool wholeActor = element.element == ProjectEx::kSelf && m_project->GetObjectType(element.object) == ProjectEx::ObjectType::kActor;
			if (wholeActor || m_collapsedGroups.contains(element)) continue;
			for (const auto& property : m_session.GetElementProperties(element)) {
				Lane lane{ GetPropertyLabel(*m_project, property), { property } };
				collectKeys(lane);
				lanes->push_back(std::move(lane));
			}
		}

		m_lanes = std::move(lanes);
		m_lanesRevision = revision;
		m_lanesSelection = m_session.selection;
		m_lanesCollapsed = m_collapsedGroups;
		return m_lanes;
	}

	void Timeline::DrawContextMenu()
	{
		const auto loopSmoothingMenu = [&](std::span<const ProjectEx::PropertyRef> a_properties) {
			if (!ImGui::BeginMenu("Cyclic", !a_properties.empty())) return;
			if (ImGui::MenuItem("On")) m_project->SetLoopSmoothing(a_properties, true);
			if (ImGui::MenuItem("Off")) m_project->SetLoopSmoothing(a_properties, false);
			ImGui::EndMenu();
		};

		if (ImGui::BeginPopup(kLaneContextPopup)) {
			loopSmoothingMenu(m_contextLaneProperties);
			m_session.DrawBezierModeMenu(m_contextLaneProperties);
			ImGui::EndPopup();
		}

		if (!ImGui::BeginPopup(kTimelineContextPopup)) return;
		const bool hasKeys = !m_session.selectedKeys.empty();
		std::vector<ProjectEx::PropertyRef> keyProperties;
		for (const auto& key : m_session.selectedKeys) {
			if (keyProperties.empty() || keyProperties.back() != key.property) keyProperties.push_back(key.property);
		}
		loopSmoothingMenu(keyProperties);
		m_session.DrawBezierModeMenu(keyProperties);
		if (ImGui::BeginMenu("Interpolation", hasKeys)) {
			for (const auto interpolation : { ProjectEx::Interpolation::kLinear, ProjectEx::Interpolation::kBezier, ProjectEx::Interpolation::kStep }) {
				if (ImGui::MenuItem(GetInterpolationName(interpolation))) {
					const std::vector<ProjectEx::KeyRef> keys{ m_session.selectedKeys.begin(), m_session.selectedKeys.end() };
					m_project->SetKeyInterpolation(keys, interpolation);
				}
			}
			ImGui::EndMenu();
		}
		m_session.ActionMenuItem(Action::kCopyKeys);
		m_session.ActionMenuItem(Action::kPasteKeys);
		m_session.ActionMenuItem(Action::kDeleteSelection, "Delete Keyframes");
		ImGui::Separator();

		const int32_t frame = m_project->GetFrame();
		const auto sequence = GetActiveSequenceInfo(*m_project);
		const int32_t frameCount = sequence ? sequence->frameCount : 0;
		if (ImGui::MenuItem("Set Loop Start at Playhead", nullptr, false, frame < (m_loopRange ? m_loopRange->second : frameCount))) {
			m_loopRange = std::pair{ frame, m_loopRange ? m_loopRange->second : frameCount };
		}
		if (ImGui::MenuItem("Set Loop End at Playhead", nullptr, false, frame > (m_loopRange ? m_loopRange->first : 0))) {
			m_loopRange = std::pair{ m_loopRange ? m_loopRange->first : 0, frame };
		}
		if (ImGui::MenuItem("Clear Loop Range", nullptr, false, m_loopRange.has_value())) m_loopRange.reset();
		ImGui::EndPopup();
	}

	float Timeline::FrameToX(float a_frame, float a_areaMinX) const
	{
		return a_areaMinX + (a_frame - m_scroll) * m_zoom;
	}

	float Timeline::XToFrame(float a_x, float a_areaMinX) const
	{
		return m_scroll + (a_x - a_areaMinX) / m_zoom;
	}

	void Timeline::WriteSettings(nlohmann::json& a_editor) const
	{
		a_editor["timelineZoom"] = m_zoom;
		a_editor["timelineScroll"] = m_scroll;
		if (m_loopRange) a_editor["loopRange"] = { m_loopRange->first, m_loopRange->second };
		auto& collapsed = a_editor["collapsedLanes"] = nlohmann::json::array();
		for (const auto& group : m_collapsedGroups) {
			if (auto item = m_session.ElementToJson(group); !item.is_null()) collapsed.push_back(std::move(item));
		}
	}

	void Timeline::ReadSettings(const nlohmann::json& a_editor)
	{
		if (const float zoom = a_editor.value("timelineZoom", m_zoom); std::isfinite(zoom)) m_zoom = std::clamp(zoom, 1.0f, 80.0f);
		if (const float scroll = a_editor.value("timelineScroll", m_scroll); std::isfinite(scroll)) m_scroll = scroll;
		if (const auto range = a_editor.find("loopRange"); range != a_editor.end() && range->is_array() && range->size() == 2) {
			const auto start = range->at(0).get<int32_t>();
			const auto end = range->at(1).get<int32_t>();
			if (start >= 0 && start < end) m_loopRange = std::pair{ start, end };
		}
		if (const auto collapsed = a_editor.find("collapsedLanes"); collapsed != a_editor.end() && collapsed->is_array()) {
			for (const auto& item : *collapsed) {
				if (const auto element = m_session.ElementFromJson(item)) m_collapsedGroups.insert(*element);
			}
		}
	}
}
