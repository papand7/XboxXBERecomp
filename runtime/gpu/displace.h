#pragma once
// Geometry refinement (displace.cpp; the Vulkan renderer draws it): a draw of the scenery cut into more triangles and
// pushed in by a smooth height made from its kind's detail picture, so the ground has real lumps and hollows
// (GraphicsStyle::displacement: grass, snow, rock and dirt facing up) and rock and concrete walls are uneven
// (GraphicsStyle::wall_displacement). The draw's own edges and creases stay where they were, so it still meets what is
// beside it; the baked light in its vertex colour is shaded by the new slopes (the game's lighting would not show them)
#include <cstdint>
#include <vector>

#include "gpu/vertex.h"

namespace gpu {

struct DisplaceParams {
    uint8_t kind = 0;          // DetailKind: whose height
    float depth = 0.08f;       // how far the lowest point of a surface facing up is pushed in, in the draw's units
    float side_depth = 0.0f;   // the same for one facing sideways or down (0: those are left alone)
    float tile = 4.0f;         // the height's broad lumps repeat, in the draw's units
    float edge = 0.6f;         // the draw cut until its typical edge is about this long (at most `levels` times)
    int levels = 3;            // each a triangle cut in four
    bool push = true;          // false: cut only (a test)
    int color_reg = -1;        // the input register holding the baked light (its rgb shaded by the bumps), -1 none
    float shade = 1.0f;        // how strongly the bumps' slopes and hollows shade that colour
    float facing = 0;          // the winding's outside (+1, -1) as an earlier pass of the same mesh found it, 0 find it
    bool eye_ok = false;       // eye: the camera in the draw's space (the side it is seen from is the outside; not
    float eye[3] = {};         // part of Key: a surface is seen from one side)
    uint64_t Key() const;      // the fields hashed (not the padding)
};

struct DisplaceResult {
    int levels = 0;       // cuts made
    int normal_reg = -1;  // the input register found holding the vertex normals (turned with the surface), -1 none
    bool up = false;      // the draw mostly faces up
    bool retry = false;   // refused for now (the camera did not tell its outside): asked again later
    float facing = 0;     // the outside found (DisplaceParams::facing for the mesh's other passes)
};

// a converted draw (Assembled::vertices, a triangle list, register 0 its place) cut and pushed in, in place; false
// (a left as it was) for one that is not refined: no triangles, too small, not facing up without side_depth, or no
// way to tell its outside. The mesh's own edges and creases stay where they were, so it still meets what is beside it
bool DisplaceMesh(Assembled& a, const DisplaceParams& p, DisplaceResult* out = nullptr);

}  // namespace gpu
