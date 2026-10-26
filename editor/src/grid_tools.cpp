#include "grid_tools.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <grimlock/grid_debug.hpp>
#include <grimlock/imgui_overlay.hpp>
#include <grimlock/ray.hpp>
#include <imgui.h>
#include <stdexcept>

namespace grimlock::editor {

namespace {

using ui::projectPolygon;

int projectCell(const glm::dmat4& matrix,
                const GridConfiguration& c,
                GridCell cell,
                ImVec2 offset,
                ImVec2 size,
                std::array<ImVec2, 12>& points) {
    const double x = c.origin.x + cell.x * c.cellSize, z = c.origin.z + cell.z * c.cellSize;
    const std::array<glm::dvec3, 4> vertices{{{x, c.origin.y, z},
                                              {x + c.cellSize, c.origin.y, z},
                                              {x + c.cellSize, c.origin.y, z + c.cellSize},
                                              {x, c.origin.y, z + c.cellSize}}};
    return projectPolygon(matrix, vertices, offset, size, points);
}

void paintLine(SceneDocument& document, GridCell from, GridCell to, bool walkable) {
    int x = static_cast<int>(from.x), z = static_cast<int>(from.z), endX = static_cast<int>(to.x),
        endZ = static_cast<int>(to.z);
    const int dx = std::abs(endX - x), dz = -std::abs(endZ - z), sx = x < endX ? 1 : -1, sz = z < endZ ? 1 : -1;
    int error = dx + dz;

    for (;;) {
        document.setGridWalkable({static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(z)}, walkable);

        if (x == endX && z == endZ) {
            break;
        }

        const int twice = 2 * error;

        if (twice >= dz) {
            error += dz;
            x += sx;
        }

        if (twice <= dx) {
            error += dx;
            z += sz;
        }
    }
}

} // namespace

void drawDebugGeometry(const Camera& camera, SDL_Window* window, const GridDebug* debug) {
    if (!debug ||
        (debug->triangles().empty() && debug->boxes().empty() && debug->quads().empty() && debug->lines().empty())) {
        return;
    }
    glm::ivec2 drawable{};
    SDL_GetWindowSizeInPixels(window, &drawable.x, &drawable.y);
    if (drawable.x <= 0 || drawable.y <= 0) {
        return;
    }
    const auto matrix =
        glm::dmat4{camera.projection(static_cast<float>(drawable.x) / drawable.y)} * glm::dmat4{camera.view()};
    auto* viewport = ImGui::GetMainViewport();
    auto* draw = ImGui::GetBackgroundDrawList(viewport);
    std::array<ImVec2, 12> points{};
    for (const auto& line : debug->lines()) {
        auto a = matrix * glm::dvec4{line.start, 1}, b = matrix * glm::dvec4{line.end, 1};
        bool visible = true;
        const std::array<glm::dvec4, 6> planes{
            {{1, 0, 0, 1}, {-1, 0, 0, 1}, {0, 1, 0, 1}, {0, -1, 0, 1}, {0, 0, 1, 0}, {0, 0, -1, 1}}};
        for (const auto plane : planes) {
            const double first = glm::dot(plane, a), second = glm::dot(plane, b);
            if (first < 0 && second < 0) {
                visible = false;
                break;
            }
            if ((first < 0) != (second < 0)) {
                const auto point = a + (b - a) * (first / (first - second));
                if (first < 0) {
                    a = point;
                } else {
                    b = point;
                }
            }
        }
        if (visible && a.w > 1e-8 && b.w > 1e-8) {
            const auto screen = [&](glm::dvec4 point) {
                return ImVec2{viewport->Pos.x + static_cast<float>((point.x / point.w + 1) * 0.5) * viewport->Size.x,
                              viewport->Pos.y + static_cast<float>((point.y / point.w + 1) * 0.5) * viewport->Size.y};
            };
            draw->AddLine(screen(a),
                          screen(b),
                          ImGui::ColorConvertFloat4ToU32({line.color.r, line.color.g, line.color.b, line.color.a}),
                          1.5F);
        }
    }
    for (const auto& quad : debug->quads()) {
        std::array<glm::dvec3, 4> vertices;
        std::copy(quad.vertices.begin(), quad.vertices.end(), vertices.begin());
        const int count = projectPolygon(matrix, vertices, viewport->Pos, viewport->Size, points);
        if (count) {
            const auto& color = quad.color;
            draw->AddConvexPolyFilled(points.data(),
                                      count,
                                      ImGui::ColorConvertFloat4ToU32({color.r, color.g, color.b, color.a}));
            draw->AddPolyline(points.data(),
                              count,
                              ImGui::ColorConvertFloat4ToU32({color.r, color.g, color.b, 0.55F}),
                              ImDrawFlags_Closed,
                              1.0F);
        }
    }
    for (const auto& triangle : debug->triangles()) {
        const std::array<glm::dvec3, 3> vertices{
            {glm::dvec3{triangle[0]}, glm::dvec3{triangle[1]}, glm::dvec3{triangle[2]}}};
        const int count = projectPolygon(matrix, vertices, viewport->Pos, viewport->Size, points);
        if (count) {
            draw->AddConvexPolyFilled(points.data(), count, IM_COL32(20, 170, 180, 55));
            draw->AddPolyline(points.data(), count, IM_COL32(30, 200, 220, 170), ImDrawFlags_Closed, 1.0F);
        }
    }
    for (const auto& box : debug->boxes()) {
        const auto a = glm::dvec3{box.minimum}, b = glm::dvec3{box.maximum};
        std::array<glm::dvec3, 8> corners{{{a.x, a.y, a.z},
                                           {b.x, a.y, a.z},
                                           {b.x, a.y, b.z},
                                           {a.x, a.y, b.z},
                                           {a.x, b.y, a.z},
                                           {b.x, b.y, a.z},
                                           {b.x, b.y, b.z},
                                           {a.x, b.y, b.z}}};
        const auto center = (a + b) * 0.5;
        for (auto& corner : corners) {
            corner = center + glm::dquat{box.rotation} * (corner - center);
        }
        constexpr std::array<std::array<int, 4>, 6> faces{
            {{0, 1, 2, 3}, {4, 5, 6, 7}, {0, 1, 5, 4}, {1, 2, 6, 5}, {2, 3, 7, 6}, {3, 0, 4, 7}}};
        const auto color = ImGui::ColorConvertFloat4ToU32({box.color.r, box.color.g, box.color.b, box.color.a});
        const auto outline = ImGui::ColorConvertFloat4ToU32({box.color.r, box.color.g, box.color.b, 0.8F});
        for (const auto& face : faces) {
            const std::array<glm::dvec3, 4> vertices{corners[face[0]],
                                                     corners[face[1]],
                                                     corners[face[2]],
                                                     corners[face[3]]};
            const int count = projectPolygon(matrix, vertices, viewport->Pos, viewport->Size, points);
            if (count) {
                draw->AddConvexPolyFilled(points.data(), count, color);
                draw->AddPolyline(points.data(), count, outline, ImDrawFlags_Closed, 1.0F);
            }
        }
    }
}

void GridTools::menu(Engine& engine, GridDebug* debug) {
    const auto& scene = *engine.scene();
    const auto* grid = scene.navigationGrid();

    if (!grid) {
        resetInteraction();
    }

    if (ImGui::BeginMenu("Navigation")) {
        ImGui::BeginDisabled(!grid);

        if (ImGui::MenuItem("Edit Walkability", nullptr, &editing_)) {
            lastCell_.reset();
        }
        ImGui::Separator();

        if (ImGui::MenuItem("Paint Blocked", nullptr, !paintWalkable_)) {
            paintWalkable_ = false;
            lastCell_.reset();
        }

        if (ImGui::MenuItem("Paint Walkable", nullptr, paintWalkable_)) {
            paintWalkable_ = true;
            lastCell_.reset();
        }

        ImGui::EndDisabled();
        ImGui::Separator();

        if (ImGui::MenuItem(grid ? "Grid Settings..." : "Create Grid...")) {
            const auto c = grid ? grid->configuration() : GridConfiguration{};
            dimensions_[0] = static_cast<int>(c.width);
            dimensions_[1] = static_cast<int>(c.height);
            cellSize_ = c.cellSize;
            origin_ = c.origin;
            error_.clear();
            openSettings_ = true;
        }

        if (debug) {
            debug->menu(engine);
        }

        ImGui::EndMenu();
    }

    if (editing_) {
        ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_CheckMark),
                           "GRID EDIT: %s (LMB paint)",
                           paintWalkable_ ? "Walkable" : "Blocked");
    }
}

void GridTools::dialog(SceneDocument& document) {
    if (openSettings_) {
        ImGui::OpenPopup("Navigation Grid");
        openSettings_ = false;
    }

    if (!ImGui::BeginPopupModal("Navigation Grid", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::SetNextItemWidth(250);
    ImGui::InputInt2("Cells (X/Z)", dimensions_);
    ImGui::SetNextItemWidth(250);
    ImGui::InputDouble("Cell Size", &cellSize_, 0.1, 1, "%.4f");
    ImGui::SetNextItemWidth(250);
    ImGui::InputScalarN("Origin (X/Y/Z)", ImGuiDataType_Double, &origin_.x, 3, nullptr, nullptr, "%.3f");
    ImGui::TextUnformatted("Maximum 65536 cells. Existing overlapping cell states are kept.");
    ImGui::TextUnformatted("Shrinking discards cells outside the new dimensions.");

    if (!error_.empty()) {
        ImGui::PushTextWrapPos(440);
        ImGui::TextUnformatted(error_.c_str());
        ImGui::PopTextWrapPos();
    }

    if (ImGui::Button("Apply")) {
        try {
            if (dimensions_[0] <= 0 || dimensions_[1] <= 0) {
                throw std::invalid_argument("Dimensions must be positive.");
            }

            document.configureGrid(GridConfiguration{static_cast<std::uint32_t>(dimensions_[0]),
                                                     static_cast<std::uint32_t>(dimensions_[1]),
                                                     cellSize_,
                                                     origin_});
            lastCell_.reset();
            ImGui::CloseCurrentPopup();
        } catch (const std::exception& error) {
            error_ = error.what();
        }
    }

    ImGui::SameLine();

    if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void GridTools::finishFrame(Engine& engine,
                            SceneDocument& document,
                            const Camera& camera,
                            SDL_Window* window,
                            bool viewportMouse) {
    auto* grid = engine.scene()->navigationGrid();

    if (!grid || !editing_) {
        lastCell_.reset();

        return;
    }

    glm::ivec2 logical{}, drawable{};
    SDL_GetWindowSize(window, &logical.x, &logical.y);
    SDL_GetWindowSizeInPixels(window, &drawable.x, &drawable.y);

    if (logical.x <= 0 || logical.y <= 0 || drawable.x <= 0 || drawable.y <= 0) {
        lastCell_.reset();

        return;
    }

    const auto& input = engine.input();
    const auto& c = grid->configuration();
    const auto hit = [&](glm::vec2 point) -> std::optional<GridCell> {
        const auto ray = mouseRay(camera, point, logical, drawable);

        if (!ray) {
            return {};
        }

        const auto world = intersectPlaneXZ(*ray, c.origin.y);

        return world ? grid->worldToCell(*world) : std::nullopt;
    };

    const auto navigationButton = [&](MouseButton button) {
        const auto b = input.button(button);

        return b.held || b.pressed || b.released;
    };

    const bool canPaint = viewportMouse && input.focused && !navigationButton(MouseButton::right) &&
                          !navigationButton(MouseButton::middle) && input.mouseWheel == glm::vec2{0};
    const auto hover = viewportMouse && input.focused ? hit(input.mousePosition) : std::nullopt;

    if (canPaint) {
        const auto left = input.button(MouseButton::left);

        if (left.pressed && input.leftPressPosition) {
            lastCell_ = hit(*input.leftPressPosition);

            if (lastCell_) {
                document.setGridWalkable(*lastCell_, paintWalkable_);
            }
        }

        if (left.held && hover) {
            paintLine(document, lastCell_.value_or(*hover), *hover, paintWalkable_);
            lastCell_ = hover;
        }

        if (!left.held || !hover) {
            lastCell_.reset();
        }
    } else {
        lastCell_.reset();
    }

    const auto matrix =
        glm::dmat4{camera.projection(static_cast<float>(drawable.x) / drawable.y)} * glm::dmat4{camera.view()};
    auto* viewport = ImGui::GetMainViewport();
    auto* draw = ImGui::GetBackgroundDrawList(viewport);
    std::array<ImVec2, 12> points{};
    for (std::uint32_t z = 0; z < c.height; ++z) {
        for (std::uint32_t x = 0; x < c.width; ++x) {
            const GridCell cell{x, z};
            const int count = projectCell(matrix, c, cell, viewport->Pos, viewport->Size, points);

            if (!count) {
                continue;
            }

            const bool highlight = hover == cell;
            const ImU32 fill = highlight              ? IM_COL32(160, 100, 20, 90)
                               : grid->walkable(cell) ? IM_COL32(12, 65, 28, 24)
                                                      : IM_COL32(100, 18, 14, 85);
            draw->AddConvexPolyFilled(points.data(), count, fill);
            draw->AddPolyline(points.data(),
                              count,
                              highlight ? IM_COL32(150, 110, 25, 200) : IM_COL32(42, 90, 65, 85),
                              1.0F,
                              ImDrawFlags_Closed);
        }
    }
}

} // namespace grimlock::editor
