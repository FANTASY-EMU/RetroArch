#ifndef JOYEMU_NDS_COMPUTE_H
#define JOYEMU_NDS_COMPUTE_H
#include <stdint.h>

/* Kept shared with the standalone Metal pixel-equivalence test. */
struct joyemu_nds_compute_params
{
   int32_t top[4];
   int32_t bottom[4];
};

static const char *joyemu_nds_compute_source =
   "#include <metal_stdlib>\n"
   "using namespace metal;\n"
   "struct Params { int4 top; int4 bottom; };\n"
   "inline bool contains(uint2 p, int4 r) {\n"
   " return r.z > 0 && r.w > 0 && int(p.x) >= r.x && int(p.y) >= r.y\n"
   " && int(p.x) - r.x < r.z && int(p.y) - r.y < r.w; }\n"
   "inline uint sample_screen(device const uint *src, uint2 p, int4 r) {\n"
   " uint x = uint(int(p.x) - r.x) * 256u / uint(r.z);\n"
   " uint y = uint(int(p.y) - r.y) * 192u / uint(r.w);\n"
   " return src[y * 256u + x]; }\n"
   "kernel void joyemu_nds_compose(device const uint *top [[buffer(0)]],\n"
   " device const uint *bottom [[buffer(1)]], constant Params& p [[buffer(2)]],\n"
   " texture2d<float, access::write> canvas [[texture(0)]], uint2 pos [[thread_position_in_grid]]) {\n"
   " if (pos.x >= canvas.get_width() || pos.y >= canvas.get_height()) return;\n"
   " uint pixel = 0;\n"
   " if (contains(pos,p.top)) pixel = sample_screen(top,pos,p.top);\n"
   " if (contains(pos,p.bottom)) pixel = sample_screen(bottom,pos,p.bottom);\n"
   " canvas.write(float4((pixel >> 16) & 255u, (pixel >> 8) & 255u,\n"
   " pixel & 255u, pixel >> 24) / 255.0f, pos);\n"
   "}\n";
#endif
