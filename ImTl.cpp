#include "imgui_timeline.h"

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include <math.h>
#include <stdint.h>

namespace ImTl
{
	// State

	struct TimelineKey
	{
		float x_pos; // x coordinate in pixels
		uint32_t color; // Color for track gradient
		ImGuiID id;
		ImTlKeyFlags flags;
		float channels[IMTL_MAX_CHANNELS]; // y coordinate in pixels
		ImColor channel_colors[IMTL_MAX_CHANNELS];
		ImGuiID channel_ids[IMTL_MAX_CHANNELS];
	};

	// Reset each time a timeline begins
	struct GlobalTimelineState
	{
		// Timeline
		ImTlTimelineFlags timeline_flags;

		// Groups
		ImTlGroupFlags timeline_group_flags;
		uint32_t timeline_group_index;

		// Tracks
		ImGuiID track_id; // Current track ID
		ImTlTrackFlags timeline_track_flags; // Current track flags
		ImTlKeyResultFlags track_result_flags; // Current track result flags
		uint32_t timeline_track_index; // Index of the track in the timeline
		int track_channels; // Number of channels in the track
		float track_height; // Track height in pixels
		float track_center_y; // Center y position of the track in pixels
		bool track_graph; // Whether to draw the graph for this track

		// Keys
		ImTlKeyFlags timeline_key_flags; // Current key flags
		ImTlKeyResultFlags key_result_flags; // Current key result flags
		uint32_t track_key_index; // Key index in the current track

		// Time / Pixels
		ImVec2 pixels_per_unit;
		ImVec2 begin_unit; // x = time | y = graph
		ImVec2 end_unit; // x = time | y = graph
		ImVec2 begin_pixel; // pixel at begin_unit
		ImVec2 end_pixel; // pixel at unit_unit
		ImVec2 mouse_unit; // x = time | y = graph
		ImVec2 mouse_pixel; // x = time | y = graph

		char buf[256]; // Local buffer for text formatting
	};

	// Persistent state, individual for each timeline
	struct TimelineState
	{
		ImDrawList* drawlist; // Current window's drawlist
		ImVector<ImGuiID> selected_keys; // Currently selected keys
		ImVector<TimelineKey> track_keys; // Keys in the current track
		ImRect tracks_rect; // Rect of the tracks area
		ImRect box_select_rect; // Rect of the box selection
		ImVec2 drag_begin; // Begin position of a drag operation
		ImVec2 drag_delta; // Delta of the current drag operation
		ImVec2 drag_init_delta; // Delta between the key and mouse when drag operation started
		ImGuiID hovered_track_id = 0; // ID of the currently hovered track
		int hovered_channel = -1; // Index of the currently hovered channel (-1 for none)
		bool box_select_active = false; // Is box selection active
		bool drag_left = false; // Is mouse dragging with left click
		bool key_held = false; // Key is being held this frame
		bool key_held_prev = false; // Key is being held in the previous frame
		bool any_key_hovered = false; // A key is hovered this frame
		bool any_key_hovered_prev = false; // A key is hovered in the previous frame
		bool graph_enabled = false; // Graph view is active
		bool playback_held = false; // Holding the playback cursor
	};

	ImPool<TimelineState> timelines;
	TimelineState* tl = nullptr;
	GlobalTimelineState gtl;

	// Utils

	static ImColor timeline_colors[ImTlCol_COUNT] = {};
	void CreateContext()
	{
		timeline_colors[ImTlCol_Key] = ImGui::GetStyleColorVec4(ImGuiCol_Text);
		timeline_colors[ImTlCol_Selected] = IM_COL32(243, 192, 103, 255);
		timeline_colors[ImTlCol_AreaHighlight] = IM_COL32(255, 255, 255, 5);
		timeline_colors[ImTlCol_MouseCursor] = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
		timeline_colors[ImTlCol_PlaybackCursor] = IM_COL32(255, 0, 0, 255);
		timeline_colors[ImTlCol_Channel0] = IM_COL32(246, 54, 82, 255); // R
		timeline_colors[ImTlCol_Channel1] = IM_COL32(112, 164, 28, 255); // G
		timeline_colors[ImTlCol_Channel2] = IM_COL32(47, 132, 227, 255); // B
		timeline_colors[ImTlCol_Channel3] = IM_COL32(255, 255, 255, 255); // A
		timeline_colors[ImTlCol_Channel4] = IM_COL32(201, 102, 20, 255);
		timeline_colors[ImTlCol_Channel5] = IM_COL32(157, 78, 181, 255);
		timeline_colors[ImTlCol_Channel6] = IM_COL32(201, 68, 168, 255);
		timeline_colors[ImTlCol_Channel7] = IM_COL32(154, 186, 110, 255);
		timeline_colors[ImTlCol_Channel8] = IM_COL32(176, 56, 100, 255);
		timeline_colors[ImTlCol_Channel9] = IM_COL32(76, 192, 194, 255);
	}

	ImColor& GetColor(ImTlCol color)
	{
		return timeline_colors[color];
	}

	float GetMouseTime()
	{
		IM_ASSERT(tl && "Not inside a timeline");
		return gtl.mouse_unit.x;
	}

	static bool IsMouseHoveringLine(ImVec2 const& A, ImVec2 const& B)
	{
		bool hovered = false;
		if (ImGui::IsWindowHovered())
		{
			ImVec2 v = ImLineClosestPoint(A, B, ImGui::GetMousePos()) - ImGui::GetMousePos();
			hovered |= v.x * v.x + v.y * v.y < 16;
		}
		return hovered;
	}

	static ImVec2 UnitAtPixel(ImVec2 pixel)
	{
		return gtl.begin_unit + (pixel - gtl.begin_pixel) / gtl.pixels_per_unit;
	}

	static ImVec2 PixelAtUnit(ImVec2 unit)
	{
		return gtl.begin_pixel + (unit - gtl.begin_unit) * gtl.pixels_per_unit;
	}

	static bool IsKeySelected(ImGuiID id, uint32_t* index = nullptr)
	{
		int found_index = tl->selected_keys.find_index(id);
		if (found_index != -1 && index)
			*index = found_index;
		return found_index != -1;
	}

	static void SingleSelectKey(ImGuiID id)
	{
		tl->selected_keys.resize(0);
		tl->selected_keys.push_back(id);
	}

	static void SelectKey(ImGuiID id)
	{
		if (!IsKeySelected(id))
			tl->selected_keys.push_back(id);
	}

	static void UnselectKey(ImGuiID id)
	{
		tl->selected_keys.find_erase_unsorted(id);
	}

	static void RightAlign(float size)
	{
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImMax(0.0f, ImGui::GetContentRegionAvail().x - size));
	}

	// Timeline

	static void DrawMarker(float time, ImColor color, const ImRect& cell_rect)
	{
		float pixel = PixelAtUnit({ time, 0 }).x;
		ImVec2 cursor_min = { pixel - 1, cell_rect.Max.y - ImGui::GetFrameHeight() };
		ImVec2 cursor_max = { pixel + 1, cell_rect.Max.y };
		tl->drawlist->AddRectFilled(cursor_min, cursor_max, color);
		color.Value.w *= 0.1f;
		tl->drawlist->AddRectFilled({ cursor_min.x + 1, cursor_max.y }, { cursor_min.x + 2, tl->tracks_rect.Max.y }, color);
	}

	static void DrawTimeGraduations(float unit, float scale, bool draw_text, const ImRect& cell_rect, float* playback_time,
		float begin_highlight, float end_highlight, int num_markers, float* marker_times,
		ImColor* marker_colors)
	{
		ImVec2 frame_padding = ImGui::GetStyle().FramePadding;
		float grad_height = ImGui::GetFrameHeight() * scale;
		float begin_time = floorf(gtl.begin_unit.x / unit) * unit;
		float pixel_step = PixelAtUnit({ unit, 0 }).x - PixelAtUnit({ 0.0f, 0 }).x;
		if (pixel_step <= 1.0f)
			return;

		ImGui::PushClipRect({ tl->tracks_rect.Min.x, cell_rect.Min.y }, tl->tracks_rect.Max, false);

		// Highlight
		if (begin_highlight != end_highlight)
		{
			ImRect highlight_rect;
			highlight_rect.Min.x = PixelAtUnit({ begin_highlight, 0 }).x;
			highlight_rect.Max.x = PixelAtUnit({ end_highlight, 0 }).x;
			highlight_rect.Min.y = cell_rect.Min.y;
			highlight_rect.Max.y = tl->tracks_rect.Max.y;
			tl->drawlist->AddRectFilled(highlight_rect.Min, highlight_rect.Max, timeline_colors[ImTlCol_AreaHighlight]);
		}

		// Graduations
		for (float pixel = PixelAtUnit({ begin_time, 0 }).x; pixel <= gtl.end_pixel.x; pixel += pixel_step)
		{
			if (pixel < gtl.begin_pixel.x)
				continue;
			ImVec2 grad_min = { pixel - 1, cell_rect.Max.y - grad_height };
			ImVec2 grad_max = { pixel + 1, cell_rect.Max.y };
			ImColor color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
			color.Value.w *= scale;
			tl->drawlist->AddRectFilled(grad_min, grad_max, color);
			color.Value.w *= 0.025f;
			tl->drawlist->AddRectFilled({ grad_min.x + 1, grad_max.y }, { grad_min.x + 2, tl->tracks_rect.Max.y }, color);

			if (draw_text)
			{
				float time = UnitAtPixel({ pixel, 0 }).x;
				if (fabsf(time) < 0.0001f) // Avoid switching between 0.0 and -0.0
					time = 0.0f;
				if (unit >= 1.0f)
					snprintf(gtl.buf, sizeof(gtl.buf), "%.0f", time);
				else
					snprintf(gtl.buf, sizeof(gtl.buf), "%.*f", (int)log10f(unit) * -1, time);
				float text_width = ImGui::CalcTextSize(gtl.buf).x;
				ImVec2 text_pos;
				text_pos.x = pixel - text_width * 0.5f;
				float min_pos = gtl.begin_pixel.x + frame_padding.x * 2.0f;
				float max_pos = gtl.end_pixel.x - text_width - frame_padding.x * 2.0f;
				text_pos.x = ImClamp(text_pos.x, min_pos, max_pos);
				text_pos.y = cell_rect.Min.y + frame_padding.y + ImGui::GetStyle().ItemSpacing.y;
				ImRect text_rect;
				text_rect.Min = text_pos;
				text_rect.Max = text_pos + ImVec2{ text_width, ImGui::GetFontSize() };
				tl->drawlist->AddText(text_pos, ImGui::GetColorU32(ImGuiCol_Text), gtl.buf);
			}
		}

		// Markers
		for (int i = 0; i < num_markers; ++i)
		{
			DrawMarker(marker_times[i], marker_colors[i], cell_rect);
		}

		// Playback time
		if (playback_time)
		{
			DrawMarker(*playback_time, timeline_colors[ImTlCol_PlaybackCursor], cell_rect);
		}

		// Mouse
		if (ImGui::TableGetHoveredColumn() == 1 && draw_text)
		{
			// Text + frame
			float mouse_unit = unit * 0.1f;
			if (mouse_unit >= 1.0f)
				snprintf(gtl.buf, sizeof(gtl.buf), "%.0f", gtl.mouse_unit.x);
			else
				snprintf(gtl.buf, sizeof(gtl.buf), "%.*f", (int)log10f(mouse_unit) * -1, gtl.mouse_unit.x);
			float mouse_text_width = ImGui::CalcTextSize(gtl.buf).x;
			ImRect mouse_text_rect;
			mouse_text_rect.Min.x = gtl.mouse_pixel.x - mouse_text_width * 0.5f;
			float min_pos = gtl.begin_pixel.x + frame_padding.x * 2.0f;
			float max_pos = gtl.end_pixel.x - mouse_text_width - frame_padding.x * 2.0f;
			mouse_text_rect.Min.x = ImClamp(mouse_text_rect.Min.x, min_pos, max_pos);
			mouse_text_rect.Min.y = cell_rect.Min.y + frame_padding.y + ImGui::GetStyle().ItemSpacing.y;
			mouse_text_rect.Max = mouse_text_rect.Min + ImVec2{ mouse_text_width, ImGui::GetFontSize() };
			ImRect mouse_frame_rect = mouse_text_rect;
			mouse_frame_rect.Min -= frame_padding;
			mouse_frame_rect.Max += frame_padding;
			tl->drawlist->AddRectFilled(mouse_frame_rect.Min, mouse_frame_rect.Max, ImGui::GetColorU32(ImGuiCol_FrameBg));
			tl->drawlist->AddText(mouse_text_rect.Min, ImGui::GetColorU32(ImGuiCol_Text), gtl.buf);
			DrawMarker(gtl.mouse_unit.x, timeline_colors[ImTlCol_MouseCursor], cell_rect);
		}

		ImGui::PopClipRect();
	}

	static void DrawGraphGraduations(float unit, float scale, bool draw_text, const ImRect& cell_rect)
	{
		ImGui::PushClipRect(cell_rect.Min, cell_rect.Max, false);
		ImVec2 frame_padding = ImGui::GetStyle().FramePadding;
		float grad_width = ImGui::GetFrameHeight() * scale;
		float begin_graph = floorf(gtl.begin_unit.y / unit) * unit;
		float pixel_step = PixelAtUnit({ 0, unit }).y - PixelAtUnit({ 0, 0 }).y;
		if (fabsf(pixel_step) <= 1.0f)
		{
			ImGui::PopClipRect();
			return;
		}

		// Offset the graduation based on text size
		float grad_offset = frame_padding.x * 2.0f;
		{
			float value = ImMax(fabsf(gtl.begin_unit.y), fabsf(gtl.end_unit.y));
			int nchars = 0;
			if ((draw_text ? unit * 0.1f : unit) >= 1.0f)
				nchars = snprintf(nullptr, 0, "%.0f", fabsf(value));
			else
				nchars = snprintf(nullptr, 0, "%.*f", (int)(log10f((draw_text ? unit * 0.1f : unit))) * -1, fabsf(value));
			snprintf(gtl.buf, sizeof(gtl.buf), "-%.*d", nchars, 0);
			grad_offset += ImGui::CalcTextSize(gtl.buf).x;
		}

		for (float pixel = PixelAtUnit({ 0, begin_graph }).y; pixel >= gtl.end_pixel.y; pixel += pixel_step)
		{
			if (pixel > gtl.begin_pixel.y)
				continue;
			ImVec2 grad_min = { cell_rect.Min.x + grad_offset, pixel - 1 };
			ImVec2 grad_max = { cell_rect.Min.x + grad_offset + grad_width, pixel + 1 };
			ImColor color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
			color.Value.w *= scale;
			tl->drawlist->AddRectFilled(grad_min, grad_max, color);

			color.Value.w *= 0.025f;
			tl->drawlist->AddRectFilled({ grad_max.x, grad_min.y + 1 }, { cell_rect.Max.x, grad_min.y + 2 }, color);

			if (draw_text)
			{
				float value = UnitAtPixel({ 0, pixel }).y;
				if (fabsf(value) < 0.0001f) // Avoid switching between 0.0 and -0.0
					value = 0.0f;
				if (unit >= 1.0f)
					snprintf(gtl.buf, sizeof(gtl.buf), "%.0f", value);
				else
					snprintf(gtl.buf, sizeof(gtl.buf), "%.*f", (int)log10f(unit) * -1, value);
				ImVec2 text_pos;
				text_pos.x = cell_rect.Min.x + frame_padding.x;
				text_pos.y = pixel - ImGui::GetFontSize() * 0.5f;
				float min_pos = gtl.end_pixel.y + frame_padding.y * 2.0f;
				float max_pos = gtl.begin_pixel.y - ImGui::GetFontSize() - frame_padding.y * 2.0f;
				text_pos.y = ImClamp(text_pos.y, min_pos, max_pos);
				tl->drawlist->AddText(text_pos, ImGui::GetColorU32(ImGuiCol_Text), gtl.buf);
			}
		}

		if (ImGui::TableGetHoveredColumn() == 1 && draw_text)
		{
			// Text + frame
			float mouse_unit = unit * 0.1f;
			if (mouse_unit >= 1.0f)
				snprintf(gtl.buf, sizeof(gtl.buf), "%.0f", gtl.mouse_unit.y);
			else
				snprintf(gtl.buf, sizeof(gtl.buf), "%.*f", (int)log10f(mouse_unit) * -1, gtl.mouse_unit.y);
			float mouse_text_width = ImGui::CalcTextSize(gtl.buf).x;
			ImRect mouse_text_rect;
			mouse_text_rect.Min.x = gtl.begin_pixel.x + frame_padding.x;
			mouse_text_rect.Min.y = gtl.mouse_pixel.y - ImGui::GetFontSize() * 0.5f;
			float min_pos = gtl.end_pixel.y + frame_padding.y * 2.0f;
			float max_pos = gtl.begin_pixel.y - ImGui::GetFontSize() - frame_padding.y * 2.0f;
			mouse_text_rect.Min.y = ImClamp(mouse_text_rect.Min.y, min_pos, max_pos);
			mouse_text_rect.Max = mouse_text_rect.Min + ImVec2{ mouse_text_width, ImGui::GetFontSize() };
			tl->drawlist->AddRectFilled(mouse_text_rect.Min - frame_padding, mouse_text_rect.Max + frame_padding,
				ImGui::GetColorU32(ImGuiCol_FrameBg));
			tl->drawlist->AddText(mouse_text_rect.Min, ImGui::GetColorU32(ImGuiCol_Text), gtl.buf);

			// Cursor
			float cursor_width = ImGui::GetFrameHeight();
			ImVec2 cursor_min = { cell_rect.Min.x + grad_offset, gtl.mouse_pixel.y - 1 };
			ImVec2 cursor_max = { cell_rect.Min.x + grad_offset + cursor_width, gtl.mouse_pixel.y + 1 };
			ImColor color = timeline_colors[ImTlCol_MouseCursor];
			tl->drawlist->AddRectFilled(cursor_min, cursor_max, color);
			color.Value.w *= 0.1f;
			tl->drawlist->AddRectFilled({ cursor_max.x, cursor_min.y + 1 }, { cell_rect.Max.x, cursor_min.y + 2 }, color);
		}
		ImGui::PopClipRect();
	}

	bool BeginTimeline(const char* str_id, float* begin_time, float* end_time, float* playback_time, float* begin_graph,
		float* end_graph, float begin_highlight, float end_highlight, int num_markers, float* marker_times,
		ImColor* marker_colors, ImTlTimelineFlags flags)
	{
		IM_ASSERT(begin_time && end_time);
		IM_ASSERT(*begin_time < *end_time);
		IM_ASSERT(tl == nullptr && "EndTimeline must be called before another BeginTimeLine");

		tl = timelines.GetOrAddByKey(ImGui::GetID(str_id));
		ImGuiTableFlags table_flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
		if (tl->graph_enabled)
			table_flags |= ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV;
		else
			table_flags |= ImGuiTableFlags_Borders;
		if (!ImGui::BeginTable(str_id, 2, table_flags))
		{
			tl = nullptr;
			return false;
		}

		gtl.timeline_flags = flags;
		tl->drawlist = ImGui::GetWindowDrawList();

		ImGui::SetKeyOwner(ImGuiMod_Alt, ImGui::GetCurrentWindow()->ID);

		bool graph_available = begin_graph && end_graph;
		float avail_width = ImGui::GetContentRegionAvail().x;

		// Setup columns
		ImGui::TableSetupColumn("Names", ImGuiTableColumnFlags_WidthFixed, avail_width * 0.25f);
		ImGui::TableSetupColumn("Values");

		// Setup header row
		ImRect header_border_rect;
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::GetColorU32(ImGuiCol_TableHeaderBg));
		ImGui::AlignTextToFramePadding();
		ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), "(?)");
		ImGui::SetItemTooltip("Pan: middle mouse\n"
			"Zoom time: ctrl + scroll\n"
			"Zoom grah: alt + scroll\n"
			"No snapping: shift\n"
			"New key: double click\n"
			"Edit key: right click\n"
			"Multi select: ctrl + click\n");
		if (graph_available)
		{
			if (ImGui::Checkbox("Graph", &tl->graph_enabled))
				tl->selected_keys.resize(0);
		}
		else
		{
			ImGui::Dummy({ 0, ImGui::GetFrameHeight() });
		}
		ImGui::TableSetColumnIndex(1);
		ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::GetColorU32(ImGuiCol_TableHeaderBg));

		bool hovering_column_1 = ImGui::TableGetHoveredColumn() == 1;
		ImVec2 mouse_pos = ImGui::GetMousePos();

		ImRect cell_rect = ImGui::TableGetCellBgRect(ImGui::GetCurrentTable(), 1);
		tl->tracks_rect.Min = ImVec2{ cell_rect.Min.x, cell_rect.Max.y };
		ImGui::PushClipRect(cell_rect.Min, cell_rect.Max, true);
		tl->drawlist->AddRect(cell_rect.Min, cell_rect.Max, IM_COL32(0, 0, 0, 1), 0, 0, 0);

		gtl.begin_unit.x = *begin_time;
		gtl.end_unit.x = *end_time;
		if (graph_available)
		{
			gtl.begin_unit.y = *begin_graph;
			gtl.end_unit.y = *end_graph;
		}

		// Flip Y axis because graph Y increases upwards
		gtl.begin_pixel = ImVec2{ tl->tracks_rect.Min.x, tl->tracks_rect.Max.y };
		gtl.end_pixel = ImVec2{ tl->tracks_rect.Max.x, tl->tracks_rect.Min.y };

		ImVec2 pixels = gtl.end_pixel - gtl.begin_pixel;
		ImVec2 units = gtl.end_unit - gtl.begin_unit;
		gtl.pixels_per_unit = pixels / units;

		gtl.mouse_pixel = mouse_pos;
		gtl.mouse_unit = UnitAtPixel(gtl.mouse_pixel);

		// Pan
		if (hovering_column_1 && ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
		{
			ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Middle) / gtl.pixels_per_unit;
			ImGui::ResetMouseDragDelta(ImGuiMouseButton_Middle);
			if (tl->graph_enabled)
			{
				// Graph enabled, pan X & Y axis
				gtl.begin_unit -= delta;
				gtl.end_unit -= delta;
			}
			else
			{
				// Graph disabled, only pan X axis
				gtl.begin_unit.x -= delta.x;
				gtl.end_unit.x -= delta.x;
			}
			*begin_time = gtl.begin_unit.x;
			*end_time = gtl.end_unit.x;
			if (graph_available)
			{
				*begin_graph = gtl.begin_unit.y;
				*end_graph = gtl.end_unit.y;
			}
			gtl.mouse_unit = UnitAtPixel(gtl.mouse_pixel);
		}

		// Zoom
		if (hovering_column_1 && ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::GetIO().MouseWheel != 0.0f)
		{
			// Time
			float new_time = units.x * expf(-0.1f * ImGui::GetIO().MouseWheel);
			float offset = (gtl.mouse_pixel.x - cell_rect.GetCenter().x) / cell_rect.GetWidth();
			gtl.begin_unit.x -= (new_time - units.x) * (0.5f + offset);
			gtl.end_unit.x += (new_time - units.x) * (0.5f - offset);
			*begin_time = gtl.begin_unit.x;
			*end_time = gtl.end_unit.x;
			units.x = new_time;
		}
		if (graph_available && hovering_column_1 && ImGui::IsKeyDown(ImGuiKey_LeftAlt) && ImGui::GetIO().MouseWheel != 0.0f)
		{
			// Graph
			float new_graph = units.y * expf(-0.1f * ImGui::GetIO().MouseWheel);
			float offset = (gtl.mouse_pixel.y - tl->tracks_rect.GetCenter().y) / tl->tracks_rect.GetHeight();
			gtl.begin_unit.y -= (new_graph - units.y) * (0.5f - offset);
			gtl.end_unit.y += (new_graph - units.y) * (0.5f + offset);
			*begin_graph = gtl.begin_unit.y;
			*end_graph = gtl.end_unit.y;
			units.y = new_graph;
		}
		gtl.pixels_per_unit = pixels / units;
		gtl.mouse_unit = UnitAtPixel(gtl.mouse_pixel);

		// Playback time
		if (ImGui::TableGetHoveredRow() == 0 && ImGui::TableGetHoveredColumn() == 1
			&& ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			tl->playback_held = true;
		else if (ImGui::IsMouseDown(ImGuiMouseButton_Left) == false)
			tl->playback_held = false;
		if (tl->playback_held && playback_time)
			*playback_time = gtl.mouse_unit.x;

		// Draw graduations / markers / highlights / mouse cursor
		{
			float graduation_small = powf(10.0f, floorf(log10f(units.x) - 1.0f));
			DrawTimeGraduations(graduation_small, 0.5f, false, cell_rect, nullptr, 0, 0, 0, nullptr, nullptr);

			if (ImGui::IsKeyDown(ImGuiKey_LeftShift) == false)
			{
				// Snap mouse to small graduation
				gtl.mouse_unit.x = roundf(gtl.mouse_unit.x / graduation_small) * graduation_small;
				gtl.mouse_pixel.x = PixelAtUnit({ gtl.mouse_unit.x, 0 }).x;
			}

			float graduation_big = powf(10.0f, floorf(log10f(units.x)));
			DrawTimeGraduations(graduation_big, 1.0f, true, cell_rect, playback_time, begin_highlight, end_highlight,
				num_markers, marker_times, marker_colors);
		}
		if (tl->graph_enabled)
		{
			float graduation_small = powf(10.0f, floorf(log10f(units.y) - 1.0f));
			DrawGraphGraduations(graduation_small, 0.5f, false, tl->tracks_rect);

			if (ImGui::IsKeyDown(ImGuiKey_LeftShift) == false)
			{
				// Snap mouse to small graduation
				gtl.mouse_unit.y = roundf(gtl.mouse_unit.y / graduation_small) * graduation_small;
				gtl.mouse_pixel.y = PixelAtUnit({ 0, gtl.mouse_unit.y }).y;
			}

			float graduation_big = powf(10.0f, floorf(log10f(units.y)));
			DrawGraphGraduations(graduation_big, 1.0f, true, tl->tracks_rect);
		}

		// Box select
		if (tl->box_select_active && tl->drag_left && tl->key_held_prev == false
			&& tl->tracks_rect.Contains(tl->drag_begin))
		{
			ImRect& box_rect = tl->box_select_rect;
			box_rect.Min = { ImMin(tl->drag_begin.x, mouse_pos.x), ImMin(tl->drag_begin.y, mouse_pos.y) };
			box_rect.Min = { ImMax(box_rect.Min.x, tl->tracks_rect.Min.x), ImMax(box_rect.Min.y, tl->tracks_rect.Min.y) };
			box_rect.Max = { ImMax(tl->drag_begin.x, mouse_pos.x), ImMax(tl->drag_begin.y, mouse_pos.y) };
			box_rect.Max = { ImMin(box_rect.Max.x, tl->tracks_rect.Max.x), ImMin(box_rect.Max.y, tl->tracks_rect.Max.y) };
			ImColor color = timeline_colors[ImTlCol_Selected];
			ImGui::GetForegroundDrawList()->AddRect(box_rect.Min, box_rect.Max, color);
			color.Value.w = 0.12f;
			ImGui::GetForegroundDrawList()->AddRectFilled(box_rect.Min, box_rect.Max, color);
		}
		else
		{
			tl->box_select_rect.Min = { FLT_MAX, FLT_MAX };
			tl->box_select_rect.Max = { FLT_MIN, FLT_MIN };
		}

		ImGui::PopClipRect();

		return true;
	}

	void EndTimeline()
	{
		// Invisible button to prevent dragging window when try to box select
		ImGui::PushClipRect(ImGui::GetWindowPos(), ImGui::GetWindowPos() + ImGui::GetWindowSize(), false);
		ImGuiID invisible_button_id = ImGui::GetID("btn");
		if (ImGui::ItemAdd(tl->tracks_rect, invisible_button_id))
			ImGui::ButtonBehavior(tl->tracks_rect, invisible_button_id, nullptr, nullptr);
		bool hovering_tracks_rect = ImGui::IsItemHovered();
		ImGui::PopClipRect();

		ImGui::EndTable();

		// Tracks rect
		tl->tracks_rect.Max = ImGui::GetItemRectMax();

		if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hovering_tracks_rect && tl->any_key_hovered == false)
			tl->box_select_active = true;
		else if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
			tl->box_select_active = false;

		// Unselect all keys when clicking on empty space
		if (ImGui::IsMouseHoveringRect(tl->tracks_rect.Min, tl->tracks_rect.Max)
			&& ImGui::IsMouseClicked(ImGuiMouseButton_Left) && tl->key_held_prev == false && tl->any_key_hovered == false)
			tl->selected_keys.resize(0);

		tl->key_held_prev = tl->key_held;
		tl->key_held = false;
		tl->any_key_hovered_prev = tl->any_key_hovered;
		tl->any_key_hovered = false;
		tl->drag_delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
		tl->drag_begin = ImGui::GetMousePos() - tl->drag_delta;
		if (ImGui::IsKeyDown(ImGuiKey_LeftShift) == false)
			tl->drag_delta = gtl.mouse_pixel - tl->drag_begin;
		tl->drag_left = ImGui::IsMouseDragging(ImGuiMouseButton_Left);
		memset(&gtl, 0, sizeof(gtl));
		tl = nullptr;
	}

	// Groups

	bool BeginGroup(const char* label, ImTlGroupFlags flags)
	{
		IM_ASSERT(tl && "Not inside a timeline");

		gtl.timeline_group_flags = flags;
		gtl.timeline_group_index++;

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGuiTableBgTarget bg_target = tl->graph_enabled ? ImGuiTableBgTarget_CellBg : ImGuiTableBgTarget_RowBg0;
		ImGui::TableSetBgColor(bg_target, ImGui::GetColorU32(ImGuiCol_TableHeaderBg));

		return ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_SpanAvailWidth);
	}

	void EndGroup()
	{
		ImGui::TreePop();
	}

	// Tracks

	bool BeginTrack(const char* label, ImTlTrackResultFlags* out_flags, bool* graph_enabled, ImTlTrackFlags flags)
	{
		gtl.timeline_track_flags = flags;
		gtl.track_result_flags = 0;
		gtl.timeline_track_index++;
		gtl.track_channels = -1;
		gtl.track_graph = false;
		gtl.track_id = ImGui::GetID(label);
		ImGui::PushID(label);

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::AlignTextToFramePadding();
		if (tl->graph_enabled && (!graph_enabled || !*graph_enabled))
			ImGui::BeginDisabled();
		ImGui::Text("%.*s", ImGui::FindRenderedTextEnd(label) - label, label);
		if (tl->graph_enabled && (!graph_enabled || !*graph_enabled))
			ImGui::EndDisabled();
		if (tl->graph_enabled && graph_enabled)
		{
			ImGui::SameLine();
			RightAlign(ImGui::GetFrameHeight());
			ImGui::Checkbox("", graph_enabled);
			gtl.track_graph = *graph_enabled;
		}
		ImGui::TableSetColumnIndex(1);
		ImRect cell_rect = ImGui::TableGetCellBgRect(ImGui::GetCurrentTable(), 1);
		if (tl->graph_enabled)
		{
			ImGui::PushClipRect(tl->tracks_rect.Min, tl->tracks_rect.Max, false);
			tl->drawlist->AddRect(tl->tracks_rect.Min, tl->tracks_rect.Max, IM_COL32(0, 0, 0, 1), 0, 0, 0);
		}
		else
		{
			ImGui::PushClipRect(cell_rect.Min, cell_rect.Max, true);
			tl->drawlist->AddRect(cell_rect.Min, cell_rect.Max, IM_COL32(0, 0, 0, 1), 0, 0, 0);
		}
		gtl.track_height = cell_rect.GetHeight();
		gtl.track_center_y = cell_rect.GetCenter().y;

		bool hovering_row = ImGui::TableGetHoveredRow() == ImGui::GetCurrentTable()->CurrentRow;
		bool hovering_cell = hovering_row && ImGui::IsMouseHoveringRect(cell_rect.Min, cell_rect.Max);
		bool hovering_track = hovering_cell || tl->hovered_track_id == gtl.track_id;
		bool double_clicked = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
		if (double_clicked && hovering_track && !tl->any_key_hovered_prev)
			gtl.track_result_flags |= ImTlTrackResultFlags_NewKey;

		*out_flags = gtl.track_result_flags;
		return gtl.track_id != 0;
	}

	void EndTrack()
	{
		if (tl->graph_enabled || (gtl.timeline_track_flags & ImTlTrackFlags_Gradient))
		{
			// Sort keys
			ImQsort(tl->track_keys.begin(), tl->track_keys.Size, sizeof(TimelineKey), [](const void* a, const void* b) {
				const TimelineKey* key_a = (const TimelineKey*)a;
				const TimelineKey* key_b = (const TimelineKey*)b;
				if (key_a->x_pos == key_b->x_pos)
					return 0;
				return key_a->x_pos < key_b->x_pos ? -1 : 1;
				});
		}

		if (!tl->graph_enabled && (gtl.timeline_track_flags & ImTlTrackFlags_Gradient) && tl->track_keys.size() > 1)
		{
			// Gradient
			TimelineKey& first_key = tl->track_keys[0];
			TimelineKey& last_key = tl->track_keys[tl->track_keys.size() - 1];

			ImRect gradient_rect;
			gradient_rect.Min = { first_key.x_pos, first_key.channels[0] - gtl.track_height * 0.5f };
			gradient_rect.Max = { last_key.x_pos, last_key.channels[0] + gtl.track_height * 0.5f };

			// Alpha checkerboard
			float square_side = gtl.track_height * 0.25f;
			ImVec2 square_size = { square_side, square_side };
			ImGui::PushClipRect(gradient_rect.Min, gradient_rect.Max, true);
			for (uint32_t y = 0; y < uint32_t(gtl.track_height / square_side) + 1; ++y)
			{
				for (uint32_t x = 0; x < uint32_t(gradient_rect.GetWidth() / square_side) + 1; ++x)
				{
					uint32_t color = (x + y) % 2 ? IM_COL32(204, 204, 204, 255) : IM_COL32(128, 128, 128, 255);
					ImVec2 square_min = gradient_rect.Min + ImVec2(x * square_side, y * square_side);
					ImGui::GetWindowDrawList()->AddRectFilled(square_min, square_min + square_size, color);
				}
			}
			ImGui::PopClipRect();

			// Colors
			for (int i = 1; i < tl->track_keys.size(); ++i)
			{
				TimelineKey const& a = tl->track_keys[i - 1];
				TimelineKey const& b = tl->track_keys[i];
				ImColor a_color = a.color;
				ImColor b_color = b.color;

				ImVec2 p_min = ImVec2{ a.x_pos, a.channels[0] };
				ImVec2 p_max = ImVec2{ b.x_pos, b.channels[0] } + ImVec2{ 0, gtl.track_height * 0.5f };
				tl->drawlist->AddRectFilledMultiColor(p_min, p_max, a_color, b_color, b_color, a_color);

				a_color.Value.w = 1.0f;
				b_color.Value.w = 1.0f;
				p_min = ImVec2{ a.x_pos, a.channels[0] } - ImVec2{ 0, gtl.track_height * 0.5f };
				p_max = ImVec2{ b.x_pos, b.channels[0] };
				tl->drawlist->AddRectFilledMultiColor(p_min, p_max, a_color, b_color, b_color, a_color);
			}
		}

		float key_outer_size = gtl.track_height * 0.35f;
		float key_inner_size = gtl.track_height * 0.25f;

		if (tl->graph_enabled && gtl.track_graph)
		{
			// Draw graph lines
			bool hovered_track = tl->hovered_track_id == gtl.track_id;
			ImGuiID new_hovered_track = 0;
			int new_hovered_channel = -1;
			for (int i_key = 1; i_key < tl->track_keys.Size; ++i_key)
			{
				TimelineKey& prev_key = tl->track_keys[i_key - 1];
				TimelineKey& key = tl->track_keys[i_key];
				for (int i_channel = 0; i_channel < gtl.track_channels; ++i_channel)
				{
					ImVec2 channel_center = { key.x_pos, key.channels[i_channel] };
					ImVec2 prev_channel_center = { prev_key.x_pos, prev_key.channels[i_channel] };

					bool hovered_channel = hovered_track && tl->hovered_channel == i_channel;
					ImColor color = key.channel_colors[i_channel];
					if (hovered_channel)
						color = timeline_colors[ImTlCol_Selected];
					else if (gtl.timeline_track_flags & ImTlTrackFlags_ChannelsRGBA)
						color = timeline_colors[ImTlCol_Channel0 + (i_channel % 4)];

					tl->drawlist->AddLine(prev_channel_center, channel_center, color);
					if (IsMouseHoveringLine(prev_channel_center, channel_center))
					{
						new_hovered_track = gtl.track_id;
						new_hovered_channel = i_channel;
					}
				}
			}

			if (new_hovered_track != 0)
			{
				tl->hovered_track_id = new_hovered_track;
				tl->hovered_channel = new_hovered_channel;
			}
			else
			{
				// Reset hovered track / channel, nothing is hovered this frame
				tl->hovered_track_id = 0;
				tl->hovered_channel = -1;
			}
		}

		if (!tl->graph_enabled || gtl.track_graph)
		{
			// Draw keys
			int num_channels = gtl.track_channels != -1 ? gtl.track_channels : 1;
			for (int i_key = 0; i_key < tl->track_keys.Size; ++i_key)
			{
				TimelineKey& key = tl->track_keys[i_key];
				for (int i_channel = 0; i_channel < num_channels; ++i_channel)
				{
					ImVec2 channel_center = { key.x_pos, key.channels[i_channel] };

					// Draw outer rect
					ImRect outer_rect;
					outer_rect.Min = channel_center - ImVec2{ key_outer_size, key_outer_size };
					outer_rect.Max = channel_center + ImVec2{ key_outer_size, key_outer_size };
					uint32_t outer_color = IM_COL32(0, 0, 0, 255);
					tl->drawlist->AddQuadFilled({ outer_rect.Min.x, key.channels[i_channel] }, { key.x_pos, outer_rect.Min.y },
						{ outer_rect.Max.x, key.channels[i_channel] }, { key.x_pos, outer_rect.Max.y },
						outer_color);

					// Draw inner rect
					ImRect inner_rect;
					inner_rect.Min = channel_center - ImVec2{ key_inner_size, key_inner_size };
					inner_rect.Max = channel_center + ImVec2{ key_inner_size, key_inner_size };
					uint32_t inner_color =
						timeline_colors[IsKeySelected(key.channel_ids[i_channel]) ? ImTlCol_Selected : ImTlCol_Key];
					tl->drawlist->AddQuadFilled({ inner_rect.Min.x, key.channels[i_channel] }, { key.x_pos, inner_rect.Min.y },
						{ inner_rect.Max.x, key.channels[i_channel] }, { key.x_pos, inner_rect.Max.y },
						inner_color);
				}
			}
		}

		// Reset state
		ImGui::PopClipRect();
		ImGui::PopID();
		gtl.track_id = 0;
		gtl.track_key_index = 0;
		tl->track_keys.resize(0);
	}

	// Keys

	static ImColor GetChannelColor(ImGuiID track_id, int channel)
	{
		constexpr int num_channel_colors = ImTlCol_COUNT - ImTlCol_Channel0;
		ImTlCol color_index = (ImTlCol)(ImTlCol_Channel0 + ((track_id + channel) % num_channel_colors));
		return timeline_colors[color_index];
	}

	bool BeginKey(float* time, ImTlKeyResultFlags* out_flags, ImTlKeyFlags flags, ImColor color, int num_channels,
		float* channels, ImColor* channel_colors)
	{
		IM_ASSERT(gtl.track_id != 0 && "Not inside a track");
		IM_ASSERT(num_channels <= IMTL_MAX_CHANNELS);
		IM_ASSERT((gtl.track_channels == num_channels || gtl.track_channels == -1)
			&& "All keys in track must have the same number of channels");

		gtl.timeline_key_flags = flags;
		gtl.key_result_flags = 0;
		gtl.track_key_index++;

		// Helpers
		float key_outer_size = gtl.track_height * 0.35f;
		bool ctrl = ImGui::IsKeyDown(ImGuiKey_LeftCtrl);
		ImGuiID key_id = ImGui::GetID(gtl.track_key_index);
		TimelineKey key = { PixelAtUnit({*time, 0}).x, color, key_id };
		key.flags = flags;

		if (tl->graph_enabled)
		{
			gtl.track_channels = num_channels;
			for (int i = 0; i < num_channels; ++i)
			{
				key.channels[i] = PixelAtUnit({ 0, channels[i] }).y;
				key.channel_colors[i] = channel_colors ? channel_colors[i] : GetChannelColor(gtl.track_id, i);
			}
		}
		else
		{
			// When graph is disabled use channel 0 as a "fake" channel
			key.channels[0] = gtl.track_center_y;
			num_channels = 1;
		}

		bool key_selected = false;
		bool channel_selected[IMTL_MAX_CHANNELS];
		bool channel_drag[IMTL_MAX_CHANNELS];
		ImGui::PushID(gtl.track_key_index);
		for (int i = 0; i < num_channels; ++i)
		{
			// Per channel information
			key.channel_ids[i] = ImGui::GetID(i);
			channel_selected[i] = IsKeySelected(key.channel_ids[i]);
			key_selected |= channel_selected[i];
			channel_drag[i] = tl->key_held_prev && tl->drag_left && channel_selected[i] && ctrl == false;
		}
		ImGui::PopID();
		bool key_drag = tl->key_held_prev && tl->drag_left && key_selected && ctrl == false;

		// Local copies are needed to prevent updating the time once per channel
		float init_time = *time;
		float init_x_pos = key.x_pos;

		for (int i = 0; i < num_channels; ++i)
		{
			// Channel rect
			ImRect channel_rect;
			channel_rect.Min = ImVec2{ key.x_pos, key.channels[i] } - ImVec2{ key_outer_size, key_outer_size };
			channel_rect.Max = ImVec2{ key.x_pos, key.channels[i] } + ImVec2{ key_outer_size, key_outer_size };

			// Button behavior
			bool hovered = false, held = false, pressed = false;
			if (ImGui::ItemAdd(channel_rect, key.channel_ids[i]))
			{
				pressed = ImGui::ButtonBehavior(channel_rect, key.channel_ids[i], &hovered, &held,
					ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
				tl->key_held |= held;
				tl->any_key_hovered |= hovered;
				if (!tl->key_held_prev && held)
					tl->drag_init_delta = ImGui::GetMousePos() - channel_rect.GetCenter();
			}

			if (key_drag)
			{
				// Change visualization time / channel while dragging
				key.x_pos = init_x_pos + tl->drag_delta.x + tl->drag_init_delta.x;
				if (tl->graph_enabled && channel_drag[i])
					key.channels[i] += tl->drag_delta.y + tl->drag_init_delta.y;

				if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
				{
					// Change time / channel for real on release
					ImVec2 drag_delta = UnitAtPixel(gtl.begin_pixel + tl->drag_delta) - UnitAtPixel(gtl.begin_pixel);
					ImVec2 init_drag_delta =
						UnitAtPixel(gtl.begin_pixel + tl->drag_init_delta) - UnitAtPixel(gtl.begin_pixel);
					if (drag_delta.x != 0.0f)
					{
						*time = init_time + drag_delta.x + init_drag_delta.x;
						gtl.key_result_flags |= ImTlKeyResultFlags_TimeChanged;
					}
					if (drag_delta.y != 0.0f && tl->graph_enabled && channel_drag[i])
					{
						channels[i] += drag_delta.y + init_drag_delta.y;
						gtl.key_result_flags |= ImTlKeyResultFlags_ChannelChanged;
					}
				}
			}
			else
			{
				// Key select / edit
				if (pressed && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
				{
					// Key edit
					ImGui::OpenPopup(key_id);
				}
				else if (hovered)
				{
					// Key tooltip
					if (ImGui::BeginTooltip())
						gtl.key_result_flags |= ImTlKeyResultFlags_Tooltip;
					ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

					// Key select
					if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
					{
						if (ctrl)
						{
							// Multi selection
							if (channel_selected[i])
								UnselectKey(key.channel_ids[i]);
							else
								SelectKey(key.channel_ids[i]);
							channel_selected[i] = !channel_selected[i];
						}
						else if (channel_selected[i] == false)
						{
							// Single selection
							SingleSelectKey(key.channel_ids[i]);
							channel_selected[i] = true;
						}
					}
					else if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
					{
						if (channel_selected[i] && ctrl == false && tl->drag_left == false)
						{
							// Single selection
							SingleSelectKey(key.channel_ids[i]);
							channel_selected[i] = true;
						}
					}
				}
				else if (tl->box_select_active)
				{
					// Box selection
					if (!channel_selected[i] && tl->box_select_rect.Overlaps(channel_rect))
					{
						SelectKey(key.channel_ids[i]);
						channel_selected[i] = true;
					}
					else if (channel_selected[i] && !tl->box_select_rect.Overlaps(channel_rect))
					{
						UnselectKey(key.channel_ids[i]);
						channel_selected[i] = false;
					}
				}
			}
		}

		if (ImGui::IsKeyPressed(ImGuiKey_Delete) && key_selected)
		{
			// Delete key
			gtl.key_result_flags |= ImTlKeyResultFlags_Deleted;
			for (int i = 0; i < num_channels; ++i)
				UnselectKey(key.channel_ids[i]);
			key_selected = false;
		}
		else
		{
			// Add key to track
			tl->track_keys.push_back(key);
		}

		// Try to begin edition popup
		if (ImGui::BeginPopupEx(
			key_id, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings))
		{
			gtl.key_result_flags |= ImTlKeyResultFlags_Edit;
		}

		*out_flags = gtl.key_result_flags;
		return gtl.key_result_flags;
	}

	void EndKey()
	{
		if (gtl.key_result_flags & ImTlKeyResultFlags_Edit)
			ImGui::EndPopup();
		else if (gtl.key_result_flags & ImTlKeyResultFlags_Tooltip)
			ImGui::EndTooltip();
		gtl.key_result_flags = 0;
	}
} // namespace ImTl