#pragma once

#include <cstdint>
#include <imgui.h>

namespace grimlock::game::presentation {
inline constexpr ImVec4 forest{35 / 255.0F, 30 / 255.0F, 48 / 255.0F, 1};
inline constexpr ImVec4 panel{42 / 255.0F, 36 / 255.0F, 55 / 255.0F, 0.98F};
inline constexpr ImVec4 secondary{64 / 255.0F, 55 / 255.0F, 76 / 255.0F, 1};
inline constexpr ImVec4 sage{243 / 255.0F, 216 / 255.0F, 160 / 255.0F, 1};
inline constexpr ImVec4 hoverGreen{255 / 255.0F, 232 / 255.0F, 185 / 255.0F, 1};
inline constexpr ImVec4 menuGold{255 / 255.0F, 199 / 255.0F, 65 / 255.0F, 1};
inline constexpr ImVec4 ivory{249 / 255.0F, 226 / 255.0F, 181 / 255.0F, 1};
inline constexpr ImVec4 secondaryText{183 / 255.0F, 198 / 255.0F, 178 / 255.0F, 1};
inline constexpr ImVec4 stone{89 / 255.0F, 102 / 255.0F, 90 / 255.0F, 1};
inline constexpr ImVec4 danger{184 / 255.0F, 108 / 255.0F, 96 / 255.0F, 1};
void menuColorSpace(bool srgbTarget);
ImVec4 menuColor(ImVec4 color);
ImU32 menuInk(ImVec4 color);

class MenuTheme final {
public:
    MenuTheme();
    ~MenuTheme();
    MenuTheme(const MenuTheme&) = delete;
    MenuTheme& operator=(const MenuTheme&) = delete;

private:
    int colorCount_ = 0;
};

bool menuAction(const char* label, ImVec2 size, bool primary = false, bool dangerous = false);
void angularPanel(ImVec2 minimum, ImVec2 maximum, ImVec4 fill, bool border = true);
void windowPanel();
void menuHeading(const char* title, const char* subtitle);
void menuShade(float opacity);
inline constexpr ImVec4 gold{0.88F, 0.73F, 0.42F, 1};
inline constexpr ImVec4 muted{0.56F, 0.64F, 0.69F, 1};
inline constexpr ImVec4 green{0.40F, 0.78F, 0.65F, 1};
inline constexpr ImVec4 red{0.92F, 0.43F, 0.42F, 1};
enum class Icon {
    tower,
    hero,
    sword
};
void configure();
void scale(float value);
void heading(const char* title, const char* subtitle = nullptr);
void icon(Icon kind, ImVec2 center, float size, ImU32 color);
bool action(const char* label, ImVec2 size = {}, bool primary = false);
bool towerCard(const char* label, std::uint64_t cost, bool selected, ImVec2 size);
void meter(float fraction, ImVec2 size, const char* label, ImVec4 color = green);
void backdrop(float opacity = 1);
} // namespace grimlock::game::presentation
