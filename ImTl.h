#pragma once

#include "imgui/imgui.h"

#if !defined(IMTL_MAX_CHANNELS)
#define IMTL_MAX_CHANNELS 4 // Max number of channels for a track
#endif

typedef int ImTlCol;
enum ImTlCol_
{
	ImTlCol_Key,
	ImTlCol_Selected,
	ImTlCol_AreaHighlight,
	ImTlCol_MouseCursor,
	ImTlCol_PlaybackCursor,
	ImTlCol_Channel0,
	ImTlCol_Channel1,
	ImTlCol_Channel2,
	ImTlCol_Channel3,
	ImTlCol_Channel4,
	ImTlCol_Channel5,
	ImTlCol_Channel6,
	ImTlCol_Channel7,
	ImTlCol_Channel8,
	ImTlCol_Channel9,
	ImTlCol_COUNT,
};

typedef int ImTlTimelineFlags;
enum ImTlTimelineFlags_
{
	ImTlTimelineFlags_None = 0,
};

typedef int ImTlGroupFlags;
enum ImTlGroupFlags_
{
	ImTlGroupFlags_None = 0,
};

typedef int ImTlTrackFlags;
enum ImTlTrackFlags_
{
	ImTlTrackFlags_None = 0,
	ImTlTrackFlags_Gradient = 1 << 0, // Use key colors to make this track a gradient
	ImTlTrackFlags_ChannelsRGBA = 1 << 1, // Force RGBA channel colors for graph view
};

typedef int ImTlTrackResultFlags;
enum ImTlTrackResultFlags_
{
	ImTlTrackResultFlags_None = 0,
	ImTlTrackResultFlags_NewKey = 1 << 0, // Add a new key to this track
};

typedef int ImTlKeyFlags;
enum ImTlKeyFlags_
{
	ImTlKeyFlags_None = 0,
};

typedef int ImTlKeyResultFlags;
enum ImTlKeyResultFlags_
{
	ImTlKeyResultFlags_None = 0,
	ImTlKeyResultFlags_Tooltip = 1 << 0, // Display this key's value
	ImTlKeyResultFlags_Edit = 1 << 1, // Display this key's edition widgets
	ImTlKeyResultFlags_TimeChanged = 1 << 2, // This key's time was changed
	ImTlKeyResultFlags_ChannelChanged = 1 << 3, // One of This key's channel was changed
	ImTlKeyResultFlags_Deleted = 1 << 4, // This key was deleted
};

namespace ImTl
{
	// Utils

	IMGUI_API void CreateContext();
	IMGUI_API ImColor& GetColor(ImTlCol color);
	IMGUI_API float GetMouseTime();

	// Timeline

	IMGUI_API bool BeginTimeline(const char* str_id, // String ID for this timeline
		float* begin_time, // Begin time view (in seconds)
		float* end_time, // End time view (in seconds)
		float* playback_time = nullptr, // Current playback time (in seconds)
		float* begin_graph = nullptr, // Begin graphview  (arbitrary unit)
		float* end_graph = nullptr, // End graphview  (arbitrary unit)
		float begin_highlight = 0.0f, // Begin highlight area (in seconds)
		float end_highlight = 0.0f, // End highlight area (in seconds)
		int num_markers = 0, // Number of markers (visual indicators)
		float* marker_times = nullptr, // Time of each marker
		ImColor* marker_colors = nullptr, // Color of each marker
		ImTlTimelineFlags flags = 0); // Timeline flags
	IMGUI_API void EndTimeline();

	// Groups

	IMGUI_API bool BeginGroup(const char* label, ImTlGroupFlags flags = 0);
	IMGUI_API void EndGroup();

	// Tracks

	IMGUI_API bool BeginTrack(const char* label, ImTlTrackResultFlags* out_flags, bool* graph_enabled = nullptr,
		ImTlTrackFlags flags = 0);
	IMGUI_API void EndTrack();

	// Keys

	IMGUI_API bool BeginKey(float* time, ImTlKeyResultFlags* out_flags, ImTlKeyFlags flags = 0,
		ImColor color = 0, // Key color for gradient tracks
		int num_channels = 0, // Number of channels for graph view
		float* channels = nullptr, // Channel values
		ImColor* channel_colors = nullptr); // Channel colors
	IMGUI_API void EndKey();
} // namespace ImTl
