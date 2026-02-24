# ImTl

Timeline editor for ImGui

## Controls

The controls can't be remapped at the moment.


**Pan**: middle mouse <br />
**Zoom time**: ctrl + scroll <br />
**Zoom grah**: alt + scroll <br />
**Disable snapping**: shift <br />
**New key**: double click <br />
**Edit key**: right click <br />
**Multi select**: ctrl + click

## Screenshots

![Alt text](/screenshots/1.png?raw=true)
![Alt text](/screenshots/2.png?raw=true)
![Alt text](/screenshots/3.png?raw=true)

## Example code

```cpp
#include <vector>
#include "imgui_timeline.h"
#include "imgui/imgui.h"

struct Vec3
{
    float x, y, z;
};

struct Vec3Key
{
    float time;
    Vec3 value;
};

struct ColorKey
{
    float time;
    ImColor color;
};

// Helper function to draw a track for keys of type Vec3Key
void Vec3Track(const char* name, bool* graph, std::vector<Vec3Key>& keys)
{
    ImTlTrackResultFlags track_result = 0;
    if (ImTl::BeginTrack(name, &track_result, graph, ImTlTrackFlags_ChannelsRGBA))
    {
        if (track_result & ImTlTrackResultFlags_NewKey)
        {
            Vec3Key& k = keys.emplace_back();
            k.time = ImTl::GetMouseTime();
        }

        ImTlKeyResultFlags key_result = 0;
        for (int i = 0; i < keys.size(); ++i)
        {
            Vec3Key& k = keys[i];
            if (ImTl::BeginKey(&k.time, &key_result, 0, 0, 3, &k.value.x))
            {
                if (key_result & ImTlKeyResultFlags_Deleted)
                {
                    keys.erase(keys.begin() + i);
                    continue;
                }
                if (key_result & ImTlKeyResultFlags_Tooltip)
                {
                    ImGui::Text("%g | %g | %g", k.value.x, k.value.y, k.value.z);
                }
                if (key_result & ImTlKeyResultFlags_Edit)
                {
                    ImGui::DragFloat3("", &k.value.x);
                }
                ImTl::EndKey();
            }
        }
        ImTl::EndTrack();
    }
}

int main()
{
    // Initialization ...

    ImGui::CreateContext();
    ImTl::CreateContext();

    static std::vector<Vec3Key> positions;
    static std::vector<Vec3Key> rotations;
    static std::vector<Vec3Key> scales;
    for (int i = 0; i < 10; ++i)
    {
        Vec3Key& p = positions.emplace_back();
        p.time = i * 0.5f;
        p.value = {(float)i, (float)i, (float)i};

        Vec3Key& r = rotations.emplace_back();
        r.time = i * 0.5f;
        r.value = {sinf(i * 0.1f * 2.0f * 3.141592f) * 45.0f, sinf(i * 0.1f * 2.0f * 3.141592f) * 90.0f, 0.0f};

        Vec3Key& s = scales.emplace_back();
        s.time = i * 0.5f;
        s.value = {1.0f, 1.0f, 1.0f};
    }

    static std::vector<ColorKey> colors;
    colors.push_back({0.0f, {0.8f, 0.2f, 0.2f, 1.0f}});
    colors.push_back({1.0f, {0.2f, 0.8f, 0.2f, 0.5f}});
    colors.push_back({2.0f, {0.2f, 0.2f, 0.8f, 1.0f}});
    colors.push_back({3.0f, {0.2f, 0.8f, 0.8f, 1.0f}});
    colors.push_back({4.0f, {0.8f, 0.0f, 0.8f, 0.2f}});
    colors.push_back({5.0f, {0.8f, 0.8f, 0.0f, 1.0f}});

    static float begin_time = -1.0f;
    static float end_time = 6.0f;
    static float begin_graph = -2;
    static float end_graph = 2;
    static bool position_graph = false;
    static bool rotation_graph = false;
    static bool scale_graph = false;
    static bool color_graph = false;


    while (true)
    {
        // ImGui begin frame ...

        if (ImGui::Begin("test timeline"))
        {
            if (ImTl::BeginTimeline("timeline", &begin_time, &end_time, nullptr, &begin_graph, &end_graph))
            {
                if (ImTl::BeginGroup("Transform"))
                {
                    Vec3Track("Position", &position_graph, positions);
                    Vec3Track("Rotation", &rotation_graph, rotations);
                    Vec3Track("Scale", &scale_graph, scales);
                    ImTl::EndGroup();
                }

                ImTlTrackResultFlags track_result = 0;
                if (ImTl::BeginTrack("Color", &track_result, &color_graph,
                                        ImTlTrackFlags_Gradient | ImTlTrackFlags_ChannelsRGBA))
                {
                    if (track_result & ImTlTrackResultFlags_NewKey)
                    {
                        ColorKey& k = colors.emplace_back();
                        k.time = ImTl::GetMouseTime();
                        k.color = {0.0f, 0.0f, 0.0f, 1.0f};
                    }

                    for (int i = 0; i < colors.size(); ++i)
                    {
                        ColorKey& k = colors[i];
                        ImTlKeyResultFlags key_result = 0;
                        if (ImTl::BeginKey(&k.time, &key_result, 0, k.color, 4, &k.color.Value.x))
                        {
                            if (key_result & ImTlKeyResultFlags_Deleted)
                            {
                                colors.erase(colors.begin() + i);
                                continue;
                            }
                            if (key_result & ImTlKeyResultFlags_Tooltip)
                            {
                                ImGui::ColorEdit4("", &k.color.Value.x,
                                                    ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
                            }
                            if (key_result & ImTlKeyResultFlags_Edit)
                            {
                                ImGui::ColorPicker4("", &k.color.Value.x, ImGuiColorEditFlags_AlphaBar);
                            }
                            ImTl::EndKey();
                        }
                    }

                    ImTl::EndTrack();
                }

                ImTl::EndTimeline();
            }
        }
        ImGui::End();

        // ImGui end frame ...
    }
}
```