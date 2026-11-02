#pragma once

#include <cstdint>
#include <imgui.h>

namespace grimlock::game::presentation {
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
