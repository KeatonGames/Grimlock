#include "ui/game_style.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace grimlock::game::presentation {
void configure() {
    ImGui::StyleColorsDark();
    auto& s = ImGui::GetStyle();
    s.FontSizeBase = 16;
    ImFontConfig font;
    font.SizePixels = 16;
    ImGui::GetIO().Fonts->AddFontDefaultVector(&font);
    s.Colors[ImGuiCol_Text] = {0.89F, 0.92F, 0.91F, 1};
    s.Colors[ImGuiCol_TextDisabled] = muted;
    s.Colors[ImGuiCol_WindowBg] = {0.035F, 0.055F, 0.068F, 0.96F};
    s.Colors[ImGuiCol_Border] = {0.42F, 0.51F, 0.54F, 0.25F};
    s.Colors[ImGuiCol_FrameBg] = {0.06F, 0.095F, 0.11F, 1};
    s.Colors[ImGuiCol_Button] = {0.09F, 0.15F, 0.17F, 1};
    s.Colors[ImGuiCol_ButtonHovered] = {0.17F, 0.27F, 0.28F, 1};
    s.Colors[ImGuiCol_ButtonActive] = {0.25F, 0.35F, 0.32F, 1};
    s.Colors[ImGuiCol_Header] = {0.25F, 0.32F, 0.27F, 0.85F};
    s.Colors[ImGuiCol_HeaderHovered] = {0.24F, 0.34F, 0.33F, 1};
    s.Colors[ImGuiCol_HeaderActive] = {0.32F, 0.40F, 0.32F, 1};
    s.Colors[ImGuiCol_SliderGrab] = gold;
    s.Colors[ImGuiCol_CheckMark] = gold;
    s.WindowBorderSize = 1;
    s.DisabledAlpha = 0.45F;
    scale(1);
}

void scale(float value) {
    auto& s = ImGui::GetStyle();
    s.FontScaleMain = value;
    s.WindowPadding = {16 * value, 12 * value};
    s.FramePadding = {12 * value, 7 * value};
    s.ItemSpacing = {8 * value, 7 * value};
    s.WindowRounding = 12 * value;
    s.FrameRounding = 6 * value;
    s.ScrollbarSize = 12 * value;
}

void heading(const char* title, const char* subtitle) {
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.35F);
    ImGui::TextColored(gold, "%s", title);
    ImGui::PopFont();
    if (subtitle) {
        ImGui::TextDisabled("%s", subtitle);
    }
}

void icon(Icon kind, ImVec2 c, float size, ImU32 color) {
    auto* draw = ImGui::GetWindowDrawList();
    if (kind == Icon::tower) {
        draw->AddRectFilled({c.x - size * .32F, c.y - size * .25F}, {c.x + size * .32F, c.y + size * .45F}, color, 2);
        for (int i = -1; i <= 1; ++i) {
            draw->AddRectFilled({c.x + i * size * .24F - size * .09F, c.y - size * .48F},
                                {c.x + i * size * .24F + size * .09F, c.y - size * .15F},
                                color,
                                1);
        }
    } else if (kind == Icon::hero) {
        draw->AddCircleFilled({c.x, c.y - size * .22F}, size * .2F, color);
        draw->AddTriangleFilled({c.x - size * .4F, c.y + size * .4F},
                                {c.x + size * .4F, c.y + size * .4F},
                                {c.x, c.y - size * .1F},
                                color);
    } else {
        draw->AddLine({c.x - size * .3F, c.y + size * .35F}, {c.x + size * .35F, c.y - size * .4F}, color, 3);
        draw->AddLine({c.x - size * .4F, c.y + size * .05F}, {c.x, c.y + size * .4F}, color, 3);
    }
}

bool action(const char* label, ImVec2 size, bool primary) {
    const auto id = ImGui::GetID(label);
    if (primary) {
        ImGui::PushStyleColor(ImGuiCol_Button, {0.35F, 0.29F, 0.16F, 1});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.48F, 0.40F, 0.22F, 1});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, {0.59F, 0.48F, 0.26F, 1});
    }
    const bool pressed = ImGui::Button(label, size);
    if (primary) {
        ImGui::PopStyleColor(3);
    }
    auto* storage = ImGui::GetStateStorage();
    float hover = storage->GetFloat(id ^ 0xA71U);
    hover += ((ImGui::IsItemHovered() ? 1.0F : 0.0F) - hover) * (1 - std::exp(-14 * ImGui::GetIO().DeltaTime));
    storage->SetFloat(id ^ 0xA71U, hover);
    auto color = gold;
    color.w = (0.15F + hover * 0.65F) * ImGui::GetStyle().Alpha;
    ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(),
                                        ImGui::GetItemRectMax(),
                                        ImGui::ColorConvertFloat4ToU32(color),
                                        ImGui::GetStyle().FrameRounding);
    return pressed;
}

bool towerCard(const char* label, std::uint64_t cost, bool selected, ImVec2 size) {
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, {0.8F, 0.18F});
    const bool pressed = action(label, size, selected);
    ImGui::PopStyleVar();
    const auto p = ImGui::GetItemRectMin();
    const float s = ImGui::GetStyle().FontScaleMain;
    auto color = gold;
    color.w = ImGui::GetStyle().Alpha;
    icon(Icon::tower, {p.x + 20 * s, p.y + size.y * .52F}, 23 * s, ImGui::ColorConvertFloat4ToU32(color));
    char price[48];
    std::snprintf(price, sizeof(price), "%llu gold", static_cast<unsigned long long>(cost));
    ImGui::GetWindowDrawList()->AddText({p.x + 40 * s, p.y + size.y - 23 * s},
                                        ImGui::ColorConvertFloat4ToU32(color),
                                        price);
    return pressed;
}

void meter(float fraction, ImVec2 size, const char* label, ImVec4 color) {
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, color);
    ImGui::ProgressBar(std::clamp(fraction, 0.0F, 1.0F), size, label);
    ImGui::PopStyleColor();
}

void backdrop(float opacity) {
    auto* v = ImGui::GetMainViewport();
    auto* draw = ImGui::GetBackgroundDrawList(v);
    draw->AddRectFilledMultiColor(v->Pos,
                                  {v->Pos.x + v->Size.x, v->Pos.y + v->Size.y},
                                  IM_COL32(10, 22, 30, static_cast<int>(255 * opacity)),
                                  IM_COL32(16, 31, 37, static_cast<int>(255 * opacity)),
                                  IM_COL32(3, 9, 15, static_cast<int>(255 * opacity)),
                                  IM_COL32(5, 13, 19, static_cast<int>(255 * opacity)));
    for (int i = 0; i < 4; ++i) {
        const float x = v->Pos.x + v->Size.x * (0.55F + i * .15F);
        draw->AddLine({x, v->Pos.y},
                      {x - v->Size.y * .35F, v->Pos.y + v->Size.y},
                      IM_COL32(185, 158, 98, static_cast<int>(12 * opacity)),
                      1);
    }
}
} // namespace grimlock::game::presentation
