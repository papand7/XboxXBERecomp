#pragma once
// Ground displacement (displace.cpp, GraphicsStyle::displacement; the Vulkan renderer draws it): a draw of the
// scenery's ground (a texture ground detail finds grass, snow, rock or dirt in) cut into more triangles and pushed
// into the ground by its kind's height, so the ground has real lumps and hollows, in the ray traced scene too
#include <cstdint>
#include <vector>

#include "gpu/vertex.h"

namespace gpu {

struct DisplaceParams {
    uint8_t kind = 0;      // DetailKind: whose height
    float depth = 0.08f;   // how far the lowest point is pushed in, in the draw's units
    float tile = 2.0f;     // the height's repeat, in the draw's units
    int levels = 2;        // each a triangle cut in four
    bool push = true;      // false: cut only (a test)
};

// a converted draw (Assembled::vertices, a triangle list, register 0 its place) cut and pushed in, in place; false
// (a left as it was) for one that is not ground: no triangles, too many, or not facing up. The mesh's own edges stay
// where they were, so it still meets what is beside it
bool DisplaceMesh(Assembled& a, const DisplaceParams& p);

}  // namespace gpu
