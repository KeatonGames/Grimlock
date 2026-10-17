#pragma once

#include "grimlock/assets/model_data.hpp"

namespace grimlock::assets {
// MikkTSpace output is per face corner: split only vertices with incompatible
// tangent frames. Invalid/degenerate UV faces keep W=0 for geometric-normal fallback.
void generateTangents(MeshData& mesh, const TextureReference& coordinates);
}
