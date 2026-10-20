#include "grid_tools.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <grimlock/grid_debug.hpp>
#include <grimlock/ray.hpp>
#include <imgui.h>
#include <stdexcept>

namespace grimlock::editor {

namespace {

// Clip in Vulkan homogeneous coordinates before perspective division, including the near plane.
int projectCell(const glm::dmat4& matrix,
                const GridConfiguration& c,
                GridCell cell,
                ImVec2 offset,
                ImVec2 size,
                std::array<ImVec2, 12>& points) {
    const double x = c.origin.x + cell.x * c.cellSize, z = c.origin.z + cell.z * c.cellSize;
    std::array<glm::dvec4, 12> polygon{}, clipped{};
    polygon[0] = matrix * glm::dvec4{x, c.origin.y, z, 1};
    polygon[1] = matrix * glm::dvec4{x + c.cellSize, c.origin.y, z, 1};
    polygon[2] = matrix * glm::dvec4{x + c.cellSize, c.origin.y, z + c.cellSize, 1};
    polygon[3] = matrix * glm::dvec4{x, c.origin.y, z + c.cellSize, 1};
    int count = 4;

    const auto distance = [](glm::dvec4 p, int plane) {
        switch (plane) {
        case 0:
            return p.w + p.x;
        case 1:
            return p.w - p.x;
        case 2:
            return p.w + p.y;
        case 3:
            return p.w - p.y;
        case 4:
            return p.z;
        default:
            return p.w - p.z;
        }
    };

    for (int plane = 0; plane < 6 && count; ++plane) {
        int output = 0;
        auto previous = polygon[count - 1];
        double before = distance(previous, plane);

        for (int i = 0; i < count; ++i) {
            const auto current = polygon[i];
            const double after = distance(current, plane);

            if ((before >= 0) != (after >= 0)) {
                clipped[output++] = previous + (current - previous) * (before / (before - after));
            }

            if (after >= 0) {
                clipped[output++] = current;
            }

            previous = current;
            before = after;
        }

        polygon = clipped;
        count = output;
    }

    if (count < 3) {
        return 0;
    }

    for (int i = 0; i < count; ++i) {
        const auto p = polygon[i];

        if (!std::isfinite(p.w) || p.w <= 0) {
            return 0;
        }

        points[i] = {offset.x + static_cast<float>((p.x / p.w + 1) * 0.5) * size.x,
                     offset.y + static_cast<float>((p.y / p.w + 1) * 0.5) * size.y};
    }

    float area = 0;

    for (int i = 0; i < count; ++i) {
        const auto a = points[i], b = points[(i + 1) % count];
        area += a.x * b.y - b.x * a.y;
    }

    if (!std::isfinite(area) || std::abs(area) < 0.1F) {
        return 0;
    }

    if (area < 0) {
        std::reverse(points.begin(), points.begin() + count);
    }

    return count;
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

void GridTools::menu(Engine& engine, GridDebug* debug) {
    const auto& scene = *engine.scene();
    const auto* grid = scene.navigationGrid();

    if (!grid) {
        resetInteraction();
    }

    if (ImGui::BeginMenu("Navigation")) {
        ImGui::BeginDisabled(!grid);

        if (ImGui::MenuItem("Show Grid", nullptr, &visible_) && !visible_) {
            resetInteraction();
        }

        if (ImGui::MenuItem("Edit Walkability", nullptr, &editing_)) {
            visible_ = true;
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
            visible_ = true;
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
                            bool viewportMouse,
                            GridDebug* debug) {
    auto* grid = engine.scene()->navigationGrid();

    if (!grid || !visible_) {
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

    const bool canPaint = editing_ && viewportMouse && input.focused && !navigationButton(MouseButton::right) &&
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

    const auto debugView = debug ? debug->view(engine) : GridDebugView{};
    double maximumCost = 1;
    if (debugView.showCosts) {
        for (const auto cost : debugView.costs) {
            if (std::isfinite(cost)) {
                maximumCost = std::max(maximumCost, cost);
            }
        }
    }
    const auto matrix =
        glm::dmat4{camera.projection(static_cast<float>(drawable.x) / drawable.y)} * glm::dmat4{camera.view()};
    auto* viewport = ImGui::GetMainViewport();
    auto* draw = ImGui::GetBackgroundDrawList(viewport);
    std::array<ImVec2, 12> points{};
    const auto project = [&](glm::dvec3 world) -> std::optional<ImVec2> {
        const auto p = matrix * glm::dvec4{world, 1};
        if (!std::isfinite(p.w) || p.w <= 0 || std::abs(p.x) > p.w || std::abs(p.y) > p.w || p.z < 0 || p.z > p.w) {
            return {};
        }
        return ImVec2{viewport->Pos.x + static_cast<float>((p.x / p.w + 1) * 0.5) * viewport->Size.x,
                      viewport->Pos.y + static_cast<float>((p.y / p.w + 1) * 0.5) * viewport->Size.y};
    };

    for (std::uint32_t z = 0; z < c.height; ++z) {
        for (std::uint32_t x = 0; x < c.width; ++x) {
            const GridCell cell{x, z};
            const int count = projectCell(matrix, c, cell, viewport->Pos, viewport->Size, points);

            if (!count) {
                continue;
            }

            const bool highlight = editing_ && hover == cell;
            ImU32 fill = highlight              ? IM_COL32(160, 100, 20, 90)
                         : grid->walkable(cell) ? IM_COL32(12, 65, 28, 24)
                                                : IM_COL32(100, 18, 14, 85);
            const auto index = *grid->index(cell);
            if (!highlight && grid->walkable(cell) && debugView.showCosts && index < debugView.costs.size()) {
                const double cost = debugView.costs[index];
                const float ratio = static_cast<float>(cost / maximumCost);
                fill = std::isfinite(cost)
                           ? ImGui::ColorConvertFloat4ToU32({0.65F * ratio, 0.45F * (1 - ratio), 0.08F, 0.35F})
                           : IM_COL32(70, 70, 80, 100);
            }
            draw->AddConvexPolyFilled(points.data(), count, fill);
            draw->AddPolyline(points.data(),
                              count,
                              highlight ? IM_COL32(150, 110, 25, 200) : IM_COL32(42, 90, 65, 85),
                              1.0F,
                              ImDrawFlags_Closed);

            const auto center = *grid->cellCenter(cell);
            const auto point = project(center);
            if (!point) {
                continue;
            }
            if (debugView.destination == cell) {
                draw->AddCircle(*point, 7, IM_COL32(240, 200, 30, 255), 16, 2);
                draw->AddCircleFilled(*point, 2, IM_COL32(240, 200, 30, 255));
            }
            if (debugView.showDirections && index < debugView.directions.size()) {
                const auto direction = debugView.directions[index];
                const auto end =
                    project(center + glm::dvec3{direction.x * c.cellSize * 0.32, 0, direction.y * c.cellSize * 0.32});
                if (direction == glm::vec2{0} || !end) {
                    continue;
                }
                const glm::vec2 offset{end->x - point->x, end->y - point->y};
                const float length = glm::length(offset);
                if (length < 2) {
                    continue;
                }
                const auto back = offset * (std::min(5.0F, length * 0.45F) / length);
                const glm::vec2 side{-back.y * 0.6F, back.x * 0.6F};
                const auto color = IM_COL32(120, 195, 225, 230);
                draw->AddLine(*point, *end, color, 1.4F);
                draw->AddTriangleFilled(*end,
                                        {end->x - back.x + side.x, end->y - back.y + side.y},
                                        {end->x - back.x - side.x, end->y - back.y - side.y},
                                        color);
            }
        }
    }
}

} // namespace grimlock::editor
