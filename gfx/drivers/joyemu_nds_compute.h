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
/* Rotates each screen rect of an already composed canvas about its own centre.
 * Pixels outside every screen keep the source; vacated screen pixels turn black.
 * Later rects draw over earlier ones, matching the compose order. */
struct joyemu_screen_rotate_params
{
   int32_t rect[2][4];
   float cos_sin[2][4];
};

static const char *joyemu_screen_rotate_source =
   "#include <metal_stdlib>\n"
   "using namespace metal;\n"
   "struct Params { int4 rect[2]; float4 cs[2]; };\n"
   "inline bool inside(float2 p, int4 r) {\n"
   " return r.z > 0 && r.w > 0 && p.x >= float(r.x) && p.y >= float(r.y)\n"
   " && p.x < float(r.x + r.z) && p.y < float(r.y + r.w); }\n"
   "kernel void joyemu_screen_rotate(texture2d<float, access::read> src [[texture(0)]],\n"
   " texture2d<float, access::write> dst [[texture(1)]], constant Params& p [[buffer(0)]],\n"
   " uint2 pos [[thread_position_in_grid]]) {\n"
   " if (pos.x >= dst.get_width() || pos.y >= dst.get_height()) return;\n"
   " float2 c = float2(pos) + 0.5f;\n"
   " bool covered = inside(c, p.rect[0]) || inside(c, p.rect[1]);\n"
   " float4 color = covered ? float4(0.0f, 0.0f, 0.0f, 1.0f) : src.read(pos);\n"
   " for (int i = 0; i < 2; i++) {\n"
   "  int4 r = p.rect[i];\n"
   "  if (r.z <= 0 || r.w <= 0) continue;\n"
   "  float2 centre = float2(r.xy) + float2(r.zw) * 0.5f;\n"
   "  float2 d = c - centre; float cs = p.cs[i].x, sn = p.cs[i].y;\n"
   "  float2 l = centre + float2(d.x * cs + d.y * sn, -d.x * sn + d.y * cs);\n"
   "  if (inside(l, r)) color = src.read(uint2(clamp(l, float2(0.0f), float2(src.get_width() - 1, src.get_height() - 1))));\n"
   " }\n"
   " dst.write(color, pos);\n"
   "}\n";
#endif
