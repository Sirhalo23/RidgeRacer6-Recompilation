// Ultrawide (or any other aspect ratio).
//
// 3D view: the game always renders a 1280x720 frame. To fill a wider screen
// without touching its frame buffers, the projection is given the screen's real
// aspect ratio, so a wider field of view is squeezed into the 16:9 frame, and
// the presenter then stretches that frame across the whole screen
// (present_letterbox = false). Geometry comes out with correct proportions.
// The aspect hooks are attached in rr6_recomp_manifest.toml.
//
// 2D layer (HUD, menus): that stretch would make everything 2D too wide, so 2D
// is narrowed again by the same factor. All 2D goes through eleven primitive
// routines (called by the display-list executor sub_82143F88): seven take
// int16 pixel coordinates, four take float ones. Every one of them writes its
// vertices, already in clip space with X first, into a buffer obtained from
// BeginVertices (sub_82253E00) and then calls EndVertices (sub_82254080).
// So each of the eleven is wrapped to mark "inside a 2D primitive", and at
// EndVertices the X coordinates of that draw are rewritten.
//
// Edge layout (rr6_hud_edges): 2D is not drawn immediately; it is recorded into
// display lists and executed later. The race HUD is recorded by the sprite-group
// routine sub_82178648, menus by a separate layout engine and text renderer.
// So the edge shift is applied to recorded commands, and only inside
// sub_82178648: menu text is drawn one glyph per command and must not be pulled
// apart. Everything one sub_82178648 call records is treated as one widget and
// moved by the same amount (so a logo stays next to its caption); only a call
// that covers both sides of the screen falls back to moving each command on
// its own. The shift moves an element sideways in the game's own 1280-wide
// pixel space, far enough that after the narrowing above it lands at the
// screen edge.

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <vector>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/ppc/context.h>

#include "generated/default/rr6_recomp_init.h"

REXCVAR_DEFINE_DOUBLE(rr6_aspect_ratio, 0.0, "RR6",
                      "Aspect ratio of the 3D view (width / height), e.g. 2.3889 for 3440x1440. "
                      "0 = original 16:9. Use together with present_letterbox = false.");

REXCVAR_DEFINE_BOOL(rr6_hud_fix, true, "RR6",
                    "When rr6_aspect_ratio widens the view, draw menus and the HUD with their "
                    "original proportions instead of stretched.");

REXCVAR_DEFINE_BOOL(rr6_hud_edges, false, "RR6",
                    "With rr6_hud_fix: move 2D elements that sit near the left or right of the "
                    "16:9 picture out to the real screen edges (experimental; off = keep "
                    "everything in the central 16:9 area).");

REXCVAR_DEFINE_BOOL(rr6_hud_stats, false, "RR6",
                    "Diagnostic: every five seconds, log how many 2D commands were recorded "
                    "and how many the edge layout moved.");

namespace {

constexpr double kOriginalAspect = 16.0 / 9.0;

double TargetAspect() {
  const double wanted = REXCVAR_GET(rr6_aspect_ratio);
  return (wanted >= 1.0 && wanted <= 6.0) ? wanted : kOriginalAspect;
}

// ---- 2D layer ---------------------------------------------------------------

struct VertexCapture {
  uint32_t address = 0;  // guest address of the vertex buffer being filled
  uint32_t count = 0;
  uint32_t stride = 0;
};

thread_local int tl_2d_depth = 0;
thread_local VertexCapture tl_capture;

inline float LoadFloat(uint8_t* base, uint32_t address) {
  PPCRegister value{};
  value.u32 = REX_LOAD_U32(address);
  return value.f32;
}

inline void StoreFloat(uint8_t* base, uint32_t address, float f) {
  PPCRegister value{};
  value.f32 = f;
  REX_STORE_U32(address, value.u32);
}

// How far toward a screen edge an element is pushed, from where its centre sits
// in the 16:9 picture: 0 in the middle, -1 / +1 near the left / right, with a
// linear ramp between so nothing jumps while it moves.
double EdgePull(double centre) {
  constexpr double kInner = 0.20;  // |centre| below this: stays centred
  constexpr double kOuter = 0.45;  // |centre| above this: fully at the edge
  const double a = std::fabs(centre);
  if (a <= kInner) {
    return 0.0;
  }
  const double t = a >= kOuter ? 1.0 : (a - kInner) / (kOuter - kInner);
  return centre < 0 ? -t : t;
}

void Remap2DDraw(uint8_t* base, const VertexCapture& draw) {
  if (!REXCVAR_GET(rr6_hud_fix)) {
    return;
  }
  const double target = TargetAspect();
  if (target <= kOriginalAspect) {
    return;
  }
  if (draw.count == 0 || draw.count > 16384 || draw.stride < 8 || draw.stride > 256) {
    return;
  }
  float min_x = 0, max_x = 0;
  for (uint32_t i = 0; i < draw.count; ++i) {
    const float x = LoadFloat(base, draw.address + i * draw.stride);
    if (!std::isfinite(x)) {
      return;
    }
    if (i == 0 || x < min_x) min_x = x;
    if (i == 0 || x > max_x) max_x = x;
  }
  // Spans the whole picture (fades, full-screen backgrounds): leave it covering
  // the whole screen.
  if (min_x <= -0.97f && max_x >= 0.97f) {
    return;
  }
  const float k = static_cast<float>(kOriginalAspect / target);
  for (uint32_t i = 0; i < draw.count; ++i) {
    const uint32_t address = draw.address + i * draw.stride;
    StoreFloat(base, address, LoadFloat(base, address) * k);
  }
}

// ---- edge layout, applied to display-list commands as they are recorded ------

struct RecordedCommand {
  uint32_t first = 0;         // guest address of the first vertex (float x, y pairs)
  uint32_t vertex_count = 0;
  float min_x = 0, max_x = 0;
};

thread_local int tl_sprite_group_depth = 0;
thread_local uint32_t tl_last_payload = 0;  // payload of the command just allocated
thread_local std::vector<RecordedCommand> tl_group;  // commands of the current sprite group

// Statistics for the log: which recorders run inside / outside a sprite group.
std::atomic<uint32_t> g_in_group[4];   // opcodes 0x0E, 0x0F, 0x11, 0x12
std::atomic<uint32_t> g_outside[4];
std::atomic<uint32_t> g_shifted{0};         // commands moved
std::atomic<uint32_t> g_groups_whole{0};    // sprite groups moved as one widget
std::atomic<uint32_t> g_groups_split{0};    // sprite groups too wide: moved command by command
std::atomic<int64_t> g_last_report_ms{0};

void CountCommand(int slot) {
  (tl_sprite_group_depth > 0 ? g_in_group[slot] : g_outside[slot]).fetch_add(1);
  if (!REXCVAR_GET(rr6_hud_stats)) {
    return;
  }
  const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now().time_since_epoch())
                          .count();
  int64_t last = g_last_report_ms.load();
  if (now - last >= 5000 && g_last_report_ms.compare_exchange_strong(last, now)) {
    REXLOG_INFO("[hud] 5s of 2D commands - in sprite group: quads {} colourquads {} op11 {} fans {} "
                "(shifted {}, groups whole {} split {}); outside: quads {} colourquads {} op11 {} "
                "fans {}",
                g_in_group[0].exchange(0), g_in_group[1].exchange(0), g_in_group[2].exchange(0),
                g_in_group[3].exchange(0), g_shifted.exchange(0), g_groups_whole.exchange(0),
                g_groups_split.exchange(0), g_outside[0].exchange(0), g_outside[1].exchange(0),
                g_outside[2].exchange(0), g_outside[3].exchange(0));
  }
}

constexpr double kHalfWidth = 640.0;       // the game's 2D space is 1280 x 720
constexpr double kWideFraction = 0.6;      // wider than this share of the picture: not a side widget

bool EdgeLayoutActive() {
  return REXCVAR_GET(rr6_hud_fix) && REXCVAR_GET(rr6_hud_edges) && TargetAspect() > kOriginalAspect;
}

// Sideways shift (in the game's pixels) for something spanning min_x..max_x:
// zero for wide or central things, up to the full distance near an edge. After
// narrowing by k = original/target, a shift of (1/k - 1) * half-width moves an
// element from the edge of the 16:9 area to the edge of the screen.
float EdgeShift(float min_x, float max_x) {
  if (max_x - min_x > kWideFraction * 2 * kHalfWidth) {
    return 0.0f;  // bar, fade, backdrop: leave centred
  }
  const double centre = (0.5 * (double(min_x) + double(max_x)) - kHalfWidth) / kHalfWidth;
  return static_cast<float>(EdgePull(centre) * (TargetAspect() / kOriginalAspect - 1.0) * kHalfWidth);
}

void ShiftVertices(uint8_t* base, const RecordedCommand& command, float shift) {
  if (shift == 0.0f) {
    return;
  }
  for (uint32_t i = 0; i < command.vertex_count; ++i) {
    const uint32_t address = command.first + 8 * i;
    StoreFloat(base, address, LoadFloat(base, address) + shift);
  }
  g_shifted.fetch_add(1);
}

// Notes a just-recorded command (float x,y pairs at payload+0xC) of the current
// sprite group; the group is laid out when it is complete.
void NoteRecordedCommand(uint8_t* base, uint32_t payload, uint32_t vertex_count) {
  if (tl_sprite_group_depth <= 0 || payload == 0 || vertex_count == 0 || vertex_count > 4096 ||
      !EdgeLayoutActive()) {
    return;
  }
  RecordedCommand command;
  command.first = payload + 0xC;
  command.vertex_count = vertex_count;
  for (uint32_t i = 0; i < vertex_count; ++i) {
    const float x = LoadFloat(base, command.first + 8 * i);
    if (!std::isfinite(x)) {
      return;
    }
    if (i == 0 || x < command.min_x) command.min_x = x;
    if (i == 0 || x > command.max_x) command.max_x = x;
  }
  tl_group.push_back(command);
}

// Moves what the sprite group that just ended recorded.
void LayOutSpriteGroup(uint8_t* base) {
  if (tl_group.empty()) {
    return;
  }
  float min_x = tl_group[0].min_x, max_x = tl_group[0].max_x;
  for (const RecordedCommand& command : tl_group) {
    if (command.min_x < min_x) min_x = command.min_x;
    if (command.max_x > max_x) max_x = command.max_x;
  }
  if (max_x - min_x <= kWideFraction * 2 * kHalfWidth) {
    // One widget: everything moves together.
    const float shift = EdgeShift(min_x, max_x);
    for (const RecordedCommand& command : tl_group) {
      ShiftVertices(base, command, shift);
    }
    g_groups_whole.fetch_add(1);
  } else {
    // Covers both sides of the picture: each command goes to its own side.
    for (const RecordedCommand& command : tl_group) {
      ShiftVertices(base, command, EdgeShift(command.min_x, command.max_x));
    }
    g_groups_split.fetch_add(1);
  }
  tl_group.clear();
}

}  // namespace

// ---- 3D view: mid-asm hooks (see manifest) ----------------------------------

// Replaces the 16:9 aspect the game just loaded.
void RR6_AspectHook(PPCRegister& value) {
  value.f64 = TargetAspect();
}

// Split screen uses twice the single-screen aspect; keep that relationship.
void RR6_SplitAspectHook(PPCRegister& value) {
  value.f64 = value.f64 * (TargetAspect() / kOriginalAspect);
}

// ---- 2D layer: function hooks -----------------------------------------------

#define RR6_2D_PRIMITIVE(name)    \
  REX_HOOK_RAW(name) {            \
    ++tl_2d_depth;                \
    __imp__##name(ctx, base);     \
    --tl_2d_depth;                \
  }

// int16 pixel coordinates
RR6_2D_PRIMITIVE(sub_8221CED8)  // line strip (minimap outline)
RR6_2D_PRIMITIVE(sub_8221D0B0)  // quad
RR6_2D_PRIMITIVE(sub_8221D2E8)  // quad
RR6_2D_PRIMITIVE(sub_8221D5B0)  // quad
RR6_2D_PRIMITIVE(sub_8221D8A8)  // quad
RR6_2D_PRIMITIVE(sub_8221DB98)  // triangle strip
RR6_2D_PRIMITIVE(sub_8221DD70)  // triangle strip
// float pixel coordinates (race HUD)
RR6_2D_PRIMITIVE(sub_8221E9C8)  // triangle fan
RR6_2D_PRIMITIVE(sub_8221FAE8)  // quads
RR6_2D_PRIMITIVE(sub_8221FEF0)  // quad
RR6_2D_PRIMITIVE(sub_822201E0)  // quad

// BeginVertices(device, primitive_type, vertex_count, vertex_stride) -> buffer
REX_HOOK_RAW(sub_82253E00) {
  const uint32_t count = ctx.r5.u32;
  const uint32_t stride = ctx.r6.u32;
  __imp__sub_82253E00(ctx, base);
  if (tl_2d_depth > 0) {
    tl_capture = VertexCapture{ctx.r3.u32, count, stride};
  }
}

// EndVertices(device)
REX_HOOK_RAW(sub_82254080) {
  if (tl_2d_depth > 0 && tl_capture.address != 0) {
    Remap2DDraw(base, tl_capture);
  }
  tl_capture = VertexCapture{};
  __imp__sub_82254080(ctx, base);
}

// ---- edge layout: record-time hooks -------------------------------------------

// Sprite-group draw (race HUD widgets). Calls can nest; the outermost one is
// the widget.
REX_HOOK_RAW(sub_82178648) {
  if (++tl_sprite_group_depth == 1) {
    tl_group.clear();
  }
  __imp__sub_82178648(ctx, base);
  if (--tl_sprite_group_depth == 0) {
    LayOutSpriteGroup(base);
  }
}

// Display-list command allocation: (list, opcode) -> payload pointer.
REX_HOOK_RAW(sub_82143D38) {
  __imp__sub_82143D38(ctx, base);
  tl_last_payload = ctx.r3.u32;
}

// Recorder for opcode 0x0E: N textured quads. payload[0] = N, vertices at +0xC.
REX_HOOK_RAW(sub_82145EF8) {
  tl_last_payload = 0;
  __imp__sub_82145EF8(ctx, base);
  CountCommand(0);
  if (tl_last_payload != 0) {
    NoteRecordedCommand(base, tl_last_payload, 4 * REX_LOAD_U32(tl_last_payload));
  }
}

// Recorder for opcode 0x12: triangle fan. payload[0] = triangle count, vertices at +0xC.
REX_HOOK_RAW(sub_82146358) {
  tl_last_payload = 0;
  __imp__sub_82146358(ctx, base);
  CountCommand(3);
  if (tl_last_payload != 0) {
    NoteRecordedCommand(base, tl_last_payload, REX_LOAD_U32(tl_last_payload) + 2);
  }
}

// Recorders for opcodes 0x0F and 0x11: only counted for now.
REX_HOOK_RAW(sub_82145FD0) {
  __imp__sub_82145FD0(ctx, base);
  CountCommand(1);
}

REX_HOOK_RAW(sub_82146288) {
  __imp__sub_82146288(ctx, base);
  CountCommand(2);
}
