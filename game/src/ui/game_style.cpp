#include "ui/game_style.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <grimlock/color.hpp>
#include <string>
#include <string_view>

namespace grimlock::game::presentation {
namespace {
bool menuSrgbTarget = true;
}

void menuColorSpace(bool srgbTarget) {
    menuSrgbTarget = srgbTarget;
}

ImVec4 menuColor(ImVec4 color) {
    if (menuSrgbTarget) {
        color.x = srgbToLinear(color.x);
        color.y = srgbToLinear(color.y);
        color.z = srgbToLinear(color.z);
    }
    return color;
}

ImU32 menuInk(ImVec4 color) {
    return ImGui::GetColorU32(menuColor(color));
}

MenuTheme::MenuTheme() {
    const std::pair<ImGuiCol, ImVec4> colors[]{{ImGuiCol_Text, ivory},
                                               {ImGuiCol_TextDisabled, secondaryText},
                                               {ImGuiCol_WindowBg, panel},
                                               {ImGuiCol_ChildBg, {0, 0, 0, 0}},
                                               {ImGuiCol_Border, stone},
                                               {ImGuiCol_FrameBg, forest},
                                               {ImGuiCol_FrameBgHovered, secondary},
                                               {ImGuiCol_FrameBgActive, secondary},
                                               {ImGuiCol_Button, secondary},
                                               {ImGuiCol_ButtonHovered, sage},
                                               {ImGuiCol_ButtonActive, hoverGreen},
                                               {ImGuiCol_Header, secondary},
                                               {ImGuiCol_HeaderHovered, sage},
                                               {ImGuiCol_HeaderActive, hoverGreen},
                                               {ImGuiCol_CheckMark, menuGold},
                                               {ImGuiCol_SliderGrab, sage},
                                               {ImGuiCol_SliderGrabActive, hoverGreen},
                                               {ImGuiCol_Separator, stone},
                                               {ImGuiCol_NavCursor, menuGold},
                                               {ImGuiCol_CheckboxSelectedBg, secondary},
                                               {ImGuiCol_PopupBg, panel},
                                               {ImGuiCol_ScrollbarBg, forest},
                                               {ImGuiCol_ScrollbarGrab, stone},
                                               {ImGuiCol_ScrollbarGrabHovered, sage},
                                               {ImGuiCol_ScrollbarGrabActive, hoverGreen},
                                               {ImGuiCol_TextSelectedBg, secondary}};
    for (const auto& [slot, color] : colors) {
        ImGui::PushStyleColor(slot, menuColor(color));
        ++colorCount_;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4);
}

MenuTheme::~MenuTheme() {
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(colorCount_);
}

void menuHeading(const char* title, const char* subtitle) {
    ImGui::PushFont(nullptr, 30);
    ImGui::TextColored(menuColor(ivory), "%s", title);
    ImGui::PopFont();
    if (subtitle) {
        ImGui::TextDisabled("%s", subtitle);
    }
}

void menuShade(float opacity) {
    const auto* viewport = ImGui::GetMainViewport();
    auto color = forest;
    color.w = opacity;
    ImGui::GetBackgroundDrawList()->AddRectFilled(
        viewport->Pos,
        {viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y},
        menuInk(color));
}

void angularPanel(ImVec2 a, ImVec2 b, ImVec4 fill, bool border) {
    auto* draw = ImGui::GetWindowDrawList();
    const float cut = std::min(22.0F * ImGui::GetStyle().FontScaleMain, (b.y - a.y) * 0.25F);
    const ImVec2 points[]{{a.x + cut, a.y},
                          {b.x, a.y},
                          {b.x, b.y - cut},
                          {b.x - cut, b.y},
                          {a.x, b.y},
                          {a.x, a.y + cut}};
    ImVec2 shadow[6];
    for (int i = 0; i < 6; ++i) {
        shadow[i] = {points[i].x + 7, points[i].y + 9};
    }
    draw->AddConvexPolyFilled(shadow, 6, menuInk({0.065F, 0.05F, 0.10F, 0.75F}));
    draw->AddConvexPolyFilled(points, 6, menuInk(fill));
    if (border) {
        draw->AddPolyline(points, 6, menuInk(ivory), ImDrawFlags_Closed, 2.0F);
    }
}

void windowPanel() {
    const auto a = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
    angularPanel({a.x + 2, a.y + 2}, {a.x + size.x - 9, a.y + size.y - 10}, panel);
}

bool menuAction(const char* label, ImVec2 size, bool primary, bool dangerous) {
    const std::string_view caption{label};
    const auto separator = caption.find("###");
    const char* identity = separator == std::string_view::npos ? label : label + separator + 3;
    const auto visible = caption.substr(0, separator);
    ImGui::PushStyleColor(ImGuiCol_Button, {0, 0, 0, 0});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0, 0, 0, 0});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, {0, 0, 0, 0});
    ImGui::PushStyleColor(ImGuiCol_Text, {0, 0, 0, 0});
    const bool pressed = ImGui::Button(identity, size);
    ImGui::PopStyleColor(4);
    const bool hovered = ImGui::IsItemHovered() || ImGui::IsItemFocused();
    auto fill = dangerous ? danger : primary ? menuGold : ivory;
    if (hovered) {
        fill = dangerous ? red : primary ? ImVec4{1, 0.84F, 0.36F, 1} : hoverGreen;
    }
    fill.w *= ImGui::GetStyle().Alpha;
    const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    angularPanel(a, b, fill, false);
    std::string text{visible};
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    auto* font = ImGui::GetFont();
    float fontSize = std::min(25 * ImGui::GetStyle().FontScaleMain, (b.y - a.y) * 0.46F);
    const auto measured = font->CalcTextSizeA(fontSize, 10000, 0, text.c_str());
    fontSize *= std::min(1.0F, (b.x - a.x - 34) / std::max(measured.x, 1.0F));
    const auto textSize = font->CalcTextSizeA(fontSize, 10000, 0, text.c_str());
    const ImVec2 textPosition{(a.x + b.x - textSize.x) * 0.5F, (a.y + b.y - textSize.y) * 0.5F};
    auto ink = forest;
    ink.w *= ImGui::GetStyle().Alpha;
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddText(font, fontSize, textPosition, menuInk(ink), text.c_str());
    draw->AddText(font, fontSize, {textPosition.x + 0.7F, textPosition.y}, menuInk(ink), text.c_str());
    if (primary && text.starts_with("START")) {
        const float x = a.x + 15, y = (a.y + b.y) * 0.5F;
        draw->AddTriangleFilled({x, y - 7}, {x, y + 7}, {x + 10, y}, menuInk(ink));
    }
    return pressed;
}

void configure() {
    ImGui::StyleColorsDark();
    auto& s = ImGui::GetStyle();
    s.FontSizeBase = 16;
    ImFontConfig font;
    font.SizePixels = 16;
    ImGui::GetIO().Fonts->AddFontDefaultVector(&font);
    s.Colors[ImGuiCol_Text] = menuColor(ivory);
    s.Colors[ImGuiCol_TextDisabled] = menuColor({0.68F, 0.63F, 0.62F, 1});
    s.Colors[ImGuiCol_WindowBg] = menuColor(panel);
    s.Colors[ImGuiCol_Border] = menuColor(ivory);
    s.Colors[ImGuiCol_FrameBg] = menuColor(secondary);
    s.Colors[ImGuiCol_Button] = menuColor(ivory);
    s.Colors[ImGuiCol_ButtonHovered] = menuColor(menuGold);
    s.Colors[ImGuiCol_ButtonActive] = menuColor(hoverGreen);
    s.Colors[ImGuiCol_Header] = menuColor(secondary);
    s.Colors[ImGuiCol_HeaderHovered] = menuColor(secondary);
    s.Colors[ImGuiCol_HeaderActive] = menuColor(secondary);
    s.Colors[ImGuiCol_SliderGrab] = menuColor(menuGold);
    s.Colors[ImGuiCol_CheckMark] = menuColor(menuGold);
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
    s.WindowRounding = 0;
    s.FrameRounding = 0;
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
    return menuAction(label, size, primary);
}

bool towerCard(const char* label, std::uint64_t cost, bool selected, ImVec2 size) {
    const std::string identity = std::string{"###"} + label;
    const bool pressed = action(identity.c_str(), size, selected);
    const auto p = ImGui::GetItemRectMin();
    auto* draw = ImGui::GetWindowDrawList();
    const float scale = ImGui::GetStyle().FontScaleMain;
    const ImVec2 c{p.x + size.x * 0.5F, p.y + 24 * scale};
    const auto ink = menuInk(forest);
    if (std::string_view{label}.find("Archer") != std::string_view::npos) {
        draw->PathArcTo({c.x - 10 * scale, c.y}, 17 * scale, -1.3F, 1.3F, 20);
        draw->PathStroke(ink, 0, 3 * scale);
        draw->AddLine({c.x - 5 * scale, c.y - 16 * scale}, {c.x - 5 * scale, c.y + 16 * scale}, ink, 1.5F * scale);
        draw->AddLine({c.x - 13 * scale, c.y + 12 * scale}, {c.x + 13 * scale, c.y - 12 * scale}, ink, 2 * scale);
        draw->AddTriangleFilled({c.x + 15 * scale, c.y - 15 * scale},
                                {c.x + 6 * scale, c.y - 12 * scale},
                                {c.x + 13 * scale, c.y - 6 * scale},
                                ink);
    } else {
        draw->AddTriangleFilled({c.x - 15 * scale, c.y + 10 * scale},
                                {c.x + 12 * scale, c.y + 10 * scale},
                                {c.x - 2 * scale, c.y - 17 * scale},
                                ink);
        draw->AddEllipseFilled({c.x, c.y + 11 * scale}, {20 * scale, 5 * scale}, ink);
        draw->AddLine({c.x - 8 * scale, c.y + 5 * scale},
                      {c.x + 7 * scale, c.y + 5 * scale},
                      menuInk(menuGold),
                      2 * scale);
    }
    char price[32];
    std::snprintf(price, sizeof(price), "%llu", static_cast<unsigned long long>(cost));
    const auto text = ImGui::CalcTextSize(price);
    draw->AddText({p.x + (size.x - text.x) * 0.5F, p.y + size.y - 24 * scale}, ink, price);
    return pressed;
}

void meter(float fraction, ImVec2 size, const char* label, ImVec4 color) {
    if (size.x <= 0) {
        size.x = ImGui::GetContentRegionAvail().x;
    }
    const auto a = ImGui::GetCursorScreenPos();
    ImGui::Dummy(size);
    const ImVec2 b{a.x + size.x, a.y + size.y};
    angularPanel(a, b, secondary, false);
    const float filled = std::clamp(fraction, 0.0F, 1.0F);
    if (filled > 0) {
        angularPanel(a,
                     {a.x + size.x * filled, b.y},
                     (color.x == green.x && color.y == green.y) ? ivory : color,
                     false);
    }
    const auto text = ImGui::CalcTextSize(label);
    ImGui::GetWindowDrawList()->AddText({b.x - text.x - 12, a.y + (size.y - text.y) * 0.5F}, menuInk(forest), label);
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
