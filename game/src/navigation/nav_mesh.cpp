#include "navigation/nav_mesh.hpp"

#include <DetourAlloc.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>
#include <Recast.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <glm/geometric.hpp>
#include <stdexcept>

namespace grimlock::game {
namespace {

template <class T, auto Free> using Native = std::unique_ptr<T, decltype(Free)>;
constexpr int maximumPath = 512;

bool finite(glm::dvec3 point) {
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z) && glm::length(point) < 1e7;
}

double horizontalDistance(glm::dvec3 a, glm::dvec3 b) {
    return glm::length(glm::dvec2{a.x - b.x, a.z - b.z});
}

void check(bool success, const char* operation) {
    if (!success) {
        throw std::runtime_error(operation);
    }
}

rcConfig buildConfiguration(const NavigationGeometry& geometry, const NavMeshConfiguration& settings) {
    check(std::isfinite(settings.cellSize) && settings.cellSize >= 0.02F && std::isfinite(settings.cellHeight) &&
              settings.cellHeight >= 0.01F && std::isfinite(settings.agentRadius) && settings.agentRadius > 0 &&
              std::isfinite(settings.agentHeight) && settings.agentHeight > 0 && std::isfinite(settings.stepHeight) &&
              settings.stepHeight >= 0 && settings.stepHeight < settings.agentHeight &&
              std::isfinite(settings.maximumSlope) && settings.maximumSlope >= 0 && settings.maximumSlope < 90,
          "Invalid NavMesh configuration");
    check(!geometry.vertices.empty() && geometry.vertices.size() % 3 == 0 && !geometry.triangles.empty() &&
              geometry.triangles.size() % 3 == 0 && geometry.walkable.size() == geometry.triangles.size() / 3 &&
              geometry.vertices.size() <= 9000000,
          "Invalid or empty navigation geometry");
    for (float value : geometry.vertices) {
        check(std::isfinite(value) && std::abs(value) < 1e7F, "Navigation vertices must be finite world coordinates");
    }
    for (int index : geometry.triangles) {
        check(index >= 0 && static_cast<std::size_t>(index) < geometry.vertices.size() / 3,
              "Navigation triangle index out of range");
    }
    for (const auto& box : geometry.solidBoxes) {
        for (int axis = 0; axis < 3; ++axis) {
            check(std::isfinite(box[0][axis]) && std::isfinite(box[1][axis]) && std::abs(box[0][axis]) < 1e7F &&
                      std::abs(box[1][axis]) < 1e7F && box[0][axis] <= box[1][axis],
                  "Invalid solid geometry bounds");
        }
    }

    rcConfig config{};
    config.cs = settings.cellSize;
    config.ch = settings.cellHeight;
    const double height = std::ceil(settings.agentHeight / config.ch);
    const double radius = std::ceil(settings.agentRadius / config.cs);
    // Recast's erosion distance is an unsigned byte with two units per voxel.
    check(height >= 3 && height < 65535 && radius < 128, "NavMesh agent dimensions exceed voxel limits");
    config.walkableHeight = static_cast<int>(height);
    config.walkableRadius = static_cast<int>(radius);
    config.walkableClimb = static_cast<int>(std::floor(settings.stepHeight / config.ch));
    config.walkableSlopeAngle = settings.maximumSlope;
    config.maxEdgeLen = static_cast<int>(12 / config.cs);
    config.maxSimplificationError = 0.5F;
    config.maxVertsPerPoly = DT_VERTS_PER_POLYGON;
    config.detailSampleDist = config.cs * 6;
    config.detailSampleMaxError = config.ch;
    rcCalcBounds(geometry.vertices.data(), static_cast<int>(geometry.vertices.size() / 3), config.bmin, config.bmax);
    config.bmin[1] -= config.ch * 2;
    config.bmax[1] += settings.agentHeight + config.ch * 2;
    const double width = (double(config.bmax[0]) - config.bmin[0]) / config.cs;
    const double depth = (double(config.bmax[2]) - config.bmin[2]) / config.cs;
    check(width > 0 && depth > 0 && width < 32768 && depth < 32768 && width * depth <= 16000000 &&
              (double(config.bmax[1]) - config.bmin[1]) / config.ch < 65535,
          "Navigation geometry exceeds the single-mesh voxel budget");
    rcCalcGridSize(config.bmin, config.bmax, config.cs, &config.width, &config.height);
    return config;
}

} // namespace

struct NavMesh::Data {
    Native<dtNavMesh, dtFreeNavMesh> mesh{dtAllocNavMesh(), dtFreeNavMesh};
    Native<dtNavMeshQuery, dtFreeNavMeshQuery> query{dtAllocNavMeshQuery(), dtFreeNavMeshQuery};
    dtQueryFilter filter;
};

NavMesh::NavMesh() = default;
NavMesh::~NavMesh() = default;

void NavMesh::replaceWith(NavMesh&& candidate) noexcept {
    data_ = std::move(candidate.data_);
    configuration_ = candidate.configuration_;
    statistics_ = candidate.statistics_;
    status_ = std::move(candidate.status_);
    triangles_ = std::move(candidate.triangles_);
    ++revision_;
}

bool NavMesh::ready() const noexcept {
    return data_ != nullptr;
}

void NavMesh::reset() {
    data_.reset();
    triangles_.clear();
    statistics_ = {};
    status_ = "No navigation geometry";
    ++revision_;
}

bool NavMesh::build(const NavigationGeometry& geometry, NavMeshConfiguration configuration) {
    reset();
    configuration_ = configuration;
    const auto start = std::chrono::steady_clock::now();
    try {
        const auto config = buildConfiguration(geometry, configuration);
        rcContext context;
        Native<rcHeightfield, rcFreeHeightField> solid{rcAllocHeightfield(), rcFreeHeightField};
        Native<rcCompactHeightfield, rcFreeCompactHeightfield> compact{rcAllocCompactHeightfield(),
                                                                       rcFreeCompactHeightfield};
        Native<rcContourSet, rcFreeContourSet> contours{rcAllocContourSet(), rcFreeContourSet};
        Native<rcPolyMesh, rcFreePolyMesh> polygons{rcAllocPolyMesh(), rcFreePolyMesh};
        Native<rcPolyMeshDetail, rcFreePolyMeshDetail> detail{rcAllocPolyMeshDetail(), rcFreePolyMeshDetail};
        check(solid && compact && contours && polygons && detail, "Cannot allocate Recast build data");
        check(rcCreateHeightfield(&context,
                                  *solid,
                                  config.width,
                                  config.height,
                                  config.bmin,
                                  config.bmax,
                                  config.cs,
                                  config.ch),
              "Cannot create heightfield");
        const int vertexCount = static_cast<int>(geometry.vertices.size() / 3);
        const int triangleCount = static_cast<int>(geometry.triangles.size() / 3);
        std::vector<unsigned char> areas(triangleCount, RC_NULL_AREA);
        rcMarkWalkableTriangles(&context,
                                config.walkableSlopeAngle,
                                geometry.vertices.data(),
                                vertexCount,
                                geometry.triangles.data(),
                                triangleCount,
                                areas.data());
        for (int i = 0; i < triangleCount; ++i) {
            if (!geometry.walkable[i]) {
                areas[i] = RC_NULL_AREA;
            }
        }
        check(rcRasterizeTriangles(&context,
                                   geometry.vertices.data(),
                                   vertexCount,
                                   geometry.triangles.data(),
                                   areas.data(),
                                   triangleCount,
                                   *solid,
                                   config.walkableClimb),
              "Cannot rasterize geometry");
        // Do not promote low blocking surfaces to walkable: authored wall tops stay obstacles.
        rcFilterLedgeSpans(&context, config.walkableHeight, config.walkableClimb, *solid);
        rcFilterWalkableLowHeightSpans(&context, config.walkableHeight, *solid);
        check(rcBuildCompactHeightfield(&context, config.walkableHeight, config.walkableClimb, *solid, *compact),
              "Cannot compact heightfield");
        solid.reset();
        // A triangle soup describes the shell of a solid box. Remove buried
        // floor spans inside that volume; they must not become walkable rooms.
        for (const auto& box : geometry.solidBoxes) {
            rcMarkBoxArea(&context, &box[0].x, &box[1].x, RC_NULL_AREA, *compact);
        }
        check(rcErodeWalkableArea(&context, config.walkableRadius, *compact), "Cannot erode agent clearance");
        check(rcBuildDistanceField(&context, *compact) && rcBuildRegions(&context, *compact, 0, 0, 0),
              "Cannot partition walkable regions");
        check(rcBuildContours(&context, *compact, config.maxSimplificationError, config.maxEdgeLen, *contours),
              "Cannot build contours");
        check(rcBuildPolyMesh(&context, *contours, config.maxVertsPerPoly, *polygons) && polygons->npolys > 0,
              "No walkable polygons for this geometry and agent configuration");
        check(rcBuildPolyMeshDetail(&context,
                                    *polygons,
                                    *compact,
                                    config.detailSampleDist,
                                    config.detailSampleMaxError,
                                    *detail),
              "Cannot build detail mesh");
        for (int i = 0; i < polygons->npolys; ++i) {
            polygons->flags[i] = 1;
        }
        dtNavMeshCreateParams params{};
        params.verts = polygons->verts;
        params.vertCount = polygons->nverts;
        params.polys = polygons->polys;
        params.polyAreas = polygons->areas;
        params.polyFlags = polygons->flags;
        params.polyCount = polygons->npolys;
        params.nvp = polygons->nvp;
        params.detailMeshes = detail->meshes;
        params.detailVerts = detail->verts;
        params.detailVertsCount = detail->nverts;
        params.detailTris = detail->tris;
        params.detailTriCount = detail->ntris;
        std::copy_n(polygons->bmin, 3, params.bmin);
        std::copy_n(polygons->bmax, 3, params.bmax);
        params.walkableHeight = configuration.agentHeight;
        params.walkableRadius = configuration.agentRadius;
        params.walkableClimb = configuration.stepHeight;
        params.cs = config.cs;
        params.ch = config.ch;
        params.buildBvTree = true;
        unsigned char* bytes = nullptr;
        int byteCount = 0;
        check(dtCreateNavMeshData(&params, &bytes, &byteCount), "Cannot create Detour mesh data");
        Native<unsigned char, dtFree> ownedBytes{bytes, dtFree};
        auto data = std::make_unique<Data>();
        check(data->mesh && data->query, "Cannot allocate Detour resources");
        check(dtStatusSucceed(data->mesh->init(bytes, byteCount, DT_TILE_FREE_DATA)), "Cannot initialize Detour mesh");
        ownedBytes.release();
        check(dtStatusSucceed(data->query->init(data->mesh.get(), 4096)), "Cannot initialize Detour queries");
        data_ = std::move(data);
        statistics_.triangles = geometry.walkable.size();
        statistics_.polygons = polygons->npolys;
        for (int i = 0; i < polygons->npolys; ++i) {
            const auto* polygon = polygons->polys + i * polygons->nvp * 2;
            std::vector<glm::vec3> vertices;
            for (int j = 0; j < polygons->nvp && polygon[j] != RC_MESH_NULL_IDX; ++j) {
                const auto* vertex = polygons->verts + polygon[j] * 3;
                vertices.push_back({polygons->bmin[0] + vertex[0] * config.cs,
                                    polygons->bmin[1] + vertex[1] * config.ch,
                                    polygons->bmin[2] + vertex[2] * config.cs});
            }
            for (std::size_t j = 2; j < vertices.size(); ++j) {
                triangles_.push_back({vertices[0], vertices[j - 1], vertices[j]});
            }
        }
        status_ = "Ready";
    } catch (const std::exception& error) {
        data_.reset();
        triangles_.clear();
        status_ = error.what();
    }
    statistics_.buildMilliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return ready();
}

std::optional<NavigationPoint> NavMesh::project(glm::dvec3 position, double horizontalExtent) const {
    if (!data_ || !finite(position) || !std::isfinite(horizontalExtent) || horizontalExtent <= 0 ||
        horizontalExtent > 1000) {
        return {};
    }
    const glm::vec3 point{position};
    const float extents[]{static_cast<float>(horizontalExtent),
                          configuration_.agentHeight + configuration_.stepHeight,
                          static_cast<float>(horizontalExtent)};
    glm::vec3 nearest{0};
    dtPolyRef polygon = 0;
    const auto status = data_->query->findNearestPoly(&point.x, extents, &data_->filter, &polygon, &nearest.x);
    return dtStatusSucceed(status) && polygon ? std::optional{NavigationPoint{nearest, polygon}} : std::nullopt;
}

bool NavMesh::contains(glm::dvec3 position, double tolerance) const {
    if (!std::isfinite(tolerance) || tolerance <= 0) {
        return false;
    }
    const auto nearest = project(position, tolerance);
    return nearest && horizontalDistance(nearest->position, position) <= tolerance &&
           std::abs(nearest->position.y - position.y) <= configuration_.stepHeight + configuration_.cellHeight * 2;
}

bool NavMesh::findPath(glm::dvec3 start, glm::dvec3 destination, NavigationPath& path) const {
    path.polygons.clear();
    path.valid = false;
    path.revision = revision_;
    ++statistics_.pathQueries;
    const auto from = project(start, 0.05), to = project(destination);
    if (!from || !to || !contains(start, 0.05)) {
        return false;
    }
    const glm::vec3 a{from->position}, b{to->position};
    std::array<dtPolyRef, maximumPath> polygons{};
    int count = 0;
    const auto status =
        data_->query
            ->findPath(from->polygon, to->polygon, &a.x, &b.x, &data_->filter, polygons.data(), &count, maximumPath);
    if (dtStatusFailed(status) || dtStatusDetail(status, DT_PARTIAL_RESULT | DT_BUFFER_TOO_SMALL | DT_OUT_OF_NODES) ||
        count == 0 || polygons[count - 1] != to->polygon) {
        return false;
    }
    path.polygons.assign(polygons.begin(), polygons.begin() + count);
    path.destination = to->position;
    path.valid = true;
    return true;
}

bool NavMesh::visible(glm::dvec3 start, glm::dvec3 destination) const {
    const auto from = project(start, 0.03);
    const auto to = project(destination, 0.03);
    if (!from || !to || !contains(destination, 0.03)) {
        return false;
    }
    const glm::vec3 a{from->position}, b{destination};
    float fraction = 0;
    glm::vec3 normal{0};
    std::array<dtPolyRef, maximumPath> visited{};
    int count = 0;
    const auto status = data_->query->raycast(from->polygon,
                                              &a.x,
                                              &b.x,
                                              &data_->filter,
                                              &fraction,
                                              &normal.x,
                                              visited.data(),
                                              &count,
                                              maximumPath);
    if (dtStatusFailed(status) || dtStatusDetail(status, DT_BUFFER_TOO_SMALL) || fraction < 1 || count == 0) {
        return false;
    }
    // Shared contour edges may resolve to either adjacent polygon. Compare their surface points,
    // while rejecting the same X/Z location on a disconnected floor above or below the ray.
    glm::vec3 endpoint;
    return dtStatusSucceed(data_->query->closestPointOnPoly(visited[count - 1], &b.x, &endpoint.x, nullptr)) &&
           horizontalDistance(endpoint, to->position) <= 0.03 &&
           std::abs(endpoint.y - to->position.y) <= configuration_.cellHeight * 2;
}

std::optional<glm::dvec3> NavMesh::steering(glm::dvec3 position, NavigationPath& path, double* remaining) const {
    if (!data_ || !path.valid || path.revision != revision_ || path.polygons.empty() ||
        path.polygons.size() > maximumPath || !finite(path.destination)) {
        return {};
    }
    const auto from = project(position, 0.03);
    if (!from || !contains(position, 0.03)) {
        path.valid = false;
        return {};
    }
    if (visible(position, path.destination)) {
        if (remaining) {
            *remaining = horizontalDistance(position, path.destination);
        }
        return path.destination;
    }
    const auto current = std::find(path.polygons.begin(), path.polygons.end(), from->polygon);
    if (current == path.polygons.end()) {
        path.valid = false;
        return {};
    }
    path.polygons.erase(path.polygons.begin(), current);
    std::array<float, maximumPath * 3> points{};
    int count = 0;
    const glm::vec3 a{from->position}, b{path.destination};
    const auto status = data_->query->findStraightPath(&a.x,
                                                       &b.x,
                                                       path.polygons.data(),
                                                       static_cast<int>(path.polygons.size()),
                                                       points.data(),
                                                       nullptr,
                                                       nullptr,
                                                       &count,
                                                       maximumPath);
    if (dtStatusFailed(status) || count < 2) {
        path.valid = false;
        return {};
    }
    if (remaining) {
        *remaining = 0;
        glm::dvec3 previous = position;
        for (int i = 1; i < count; ++i) {
            const glm::dvec3 point{points[i * 3], points[i * 3 + 1], points[i * 3 + 2]};
            *remaining += horizontalDistance(previous, point);
            previous = point;
        }
    }
    // Anticipate a nearby funnel corner. Surface-constrained movement still
    // clips against the boundary; exact floating-point corner hits are not required.
    const glm::dvec3 first{points[3], points[4], points[5]};
    const int index = count > 2 && horizontalDistance(position, first) < configuration_.agentRadius * 0.75 ? 2 : 1;
    return glm::dvec3{points[index * 3], points[index * 3 + 1], points[index * 3 + 2]};
}

glm::dvec3 NavMesh::move(glm::dvec3 position, glm::dvec2 displacement) const {
    const auto target = position + glm::dvec3{displacement.x, 0, displacement.y};
    const auto from = project(position, 0.03);
    if (!from || !contains(position, 0.03) || !finite(target)) {
        return position;
    }
    const glm::vec3 a{from->position}, b{target};
    glm::vec3 result{a};
    std::array<dtPolyRef, 64> visited{};
    int count = 0;
    const auto status = data_->query->moveAlongSurface(from->polygon,
                                                       &a.x,
                                                       &b.x,
                                                       &data_->filter,
                                                       &result.x,
                                                       visited.data(),
                                                       &count,
                                                       static_cast<int>(visited.size()));
    if (dtStatusFailed(status) || dtStatusDetail(status, DT_BUFFER_TOO_SMALL) || count == 0) {
        return position;
    }
    float height = result.y;
    if (dtStatusFailed(data_->query->getPolyHeight(visited[count - 1], &result.x, &height))) {
        // A boundary result may round just outside its polygon. Clamp to that
        // same polygon instead of freezing at a valid contour edge.
        glm::vec3 nearest{result};
        if (dtStatusFailed(data_->query->closestPointOnPoly(visited[count - 1], &result.x, &nearest.x, nullptr))) {
            return position;
        }
        result = nearest;
        height = nearest.y;
    }
    result.y = height;
    return result;
}

std::vector<glm::vec3> NavMesh::corners(glm::dvec3 position, const NavigationPath& path) const {
    std::vector<glm::vec3> result;
    if (!data_ || !path.valid || path.revision != revision_ || path.polygons.empty() || !finite(position) ||
        path.polygons.size() > maximumPath || !finite(path.destination)) {
        return result;
    }
    const glm::vec3 a{position}, b{path.destination};
    std::array<float, maximumPath * 3> points{};
    int count = 0;
    if (dtStatusSucceed(data_->query->findStraightPath(&a.x,
                                                       &b.x,
                                                       path.polygons.data(),
                                                       static_cast<int>(path.polygons.size()),
                                                       points.data(),
                                                       nullptr,
                                                       nullptr,
                                                       &count,
                                                       maximumPath))) {
        result.reserve(count);
        for (int i = 0; i < count; ++i) {
            result.emplace_back(points[i * 3], points[i * 3 + 1], points[i * 3 + 2]);
        }
    }
    return result;
}

} // namespace grimlock::game
