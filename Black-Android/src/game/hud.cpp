// 2D is drawn 640x480 and shown as wide as the output. The game narrows its Flash menus itself; what its own 2D
// layer draws gets x' = x * narrow + anchor * spare width: HUD parts by their component's anchor, the rest 4:3 in
// the middle.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <vector>

#include "app/app.h"
#include "common/aspect.h"
#include "common/options.h"
#include "game/layout.h"
#include "xdk/d3d8.h"
#include "xdk/hle.h"

using namespace kernel;
using namespace layout;

namespace {

constexpr uint32_t kAnchors = 0x2AD968;  // x, y share of the safe area per anchor id
constexpr uint32_t kAnchorSize = 8;
constexpr uint32_t kTaller = 0x2AD9BC;   // frame height the game gives the middle anchors on a widescreen
// return addresses
constexpr uint32_t kFromHud = 0x13C02F;
constexpr uint32_t kFromFlash = 0x121E82;
constexpr uint32_t kFromHost = 0xFFFFFFF0;
constexpr uint32_t kTitleFade = 0xD71A0;  // draw callback of the level title card's full screen rectangle
constexpr float kOff = -1;
constexpr float kCentre = 0.5f;

float g_anchor = kOff;
float g_width = 640;
uint32_t g_hud = 0;  // the player's HUD while its list draws
float g_inset = 0;   // 640x480 pixels a sprite's uv is pulled in by at each edge

// The game takes the screen's shape once at boot: its flag, the view and the Flash stage follow the output.
// View width: the game's 1 on 4:3 and 1.2 on 16:9, past that the 16:9 height of view is kept
void FollowOutput() {
    uint32_t display = R32(kDisplay);
    uint32_t ui = R32(kUi);
    if (!display) return;
    float k = game::Narrow();
    G<uint8_t>(display + kDisplayWidescreen) = game::Wide();
    G<float>(display + kDisplayViewWidth) = std::max(0.4f + 0.6f / k, 0.9f / k);
    G<float>(display + kDisplayAspect) = game::Aspect();
    G<float>(kStageX) = 320 * (1 / k - 1);
    if (ui) G<float>(ui + kStageScaleX) = float(int32_t(R32(display + kDisplayWidth))) / 640 * k;
}

// An emitter's points, eax: x, y pairs, each drawn at itself plus the offset at [esp+4]. Moved for the call, not
// in the vertex buffer: strips are joined there with copies of vertices. Centred, a point on a screen side stays
template <typename F>
void Emit(X86Context& ctx, uint8_t* base, uint32_t count, F emit) {
    if (g_anchor < 0) return emit(ctx, base);
    static std::vector<float> saved;
    uint32_t points = ctx.eax;
    float offset = G<float>(R32(ctx.esp + 4));
    saved.resize(count);
    for (uint32_t i = 0; i < count; i++) {
        float& x = G<float>(points + i * 8);
        float at = (saved[i] = x) + offset;
        if (g_anchor == kCentre && (at <= 0 || at >= g_width)) continue;
        x = at * game::Narrow() + g_anchor * g_width * (1 - game::Narrow()) - offset;
    }
    emit(ctx, base);
    for (uint32_t i = 0; i < count; i++) G<float>(points + i * 8) = saved[i];
}

// Upscaled, a sprite's outer pixels sample up to half a texel past its edge texels and the filter blends in what lies
// next to it in the HUD texture (thin lines); its uv is pulled in to where 480p samples
float PullIn(float from, float to, float length) {
    if (length <= 0) return from;
    return from + (to - from) / length * g_inset;
}

// corner b's u and v lie across the quad from corner a's
void InsetQuads(const float* xy, float* uv, uint32_t quads) {
    for (uint32_t q = 0; q < quads; q++, xy += 4, uv += 4) {
        float w = std::fabs(xy[2] - xy[0]);
        float h = std::fabs(xy[3] - xy[1]);
        float u = uv[0];
        float v = uv[1];
        uv[0] = PullIn(u, uv[2], w);
        uv[1] = PullIn(v, uv[3], h);
        uv[2] = PullIn(uv[2], u, w);
        uv[3] = PullIn(uv[3], v, h);
    }
}

// the point of a 4 point strip sharing point i's other coordinate, -1 if none
int Partner(const float* uv, int i, int axis) {
    int other = 1 - axis;
    for (int j = 0; j < 4; j++) {
        bool shares = uv[j * 2 + other] == uv[i * 2 + other];
        if (shares && uv[j * 2 + axis] != uv[i * 2 + axis]) return j;
    }
    return -1;
}

// a rotated sprite: each point's u moves toward the point sharing its v, and v toward the one sharing its u
void InsetStrip(const float* xy, float* uv) {
    float in[8];
    std::copy(uv, uv + 8, in);
    for (int i = 0; i < 4; i++) {
        for (int axis = 0; axis < 2; axis++) {
            int j = Partner(in, i, axis);
            if (j < 0) continue;
            float length = std::hypot(xy[j * 2] - xy[i * 2], xy[j * 2 + 1] - xy[i * 2 + 1]);
            uv[i * 2 + axis] = PullIn(in[i * 2 + axis], in[j * 2 + axis], length);
        }
    }
}

// An emitter's uv pairs at [esp+0xC], put back after the call: the game's own tables hold them
struct SavedUv {
    float* uv;
    std::vector<float> saved;
    SavedUv(X86Context& ctx, uint32_t pairs) : uv(&G<float>(R32(ctx.esp + 0xC))), saved(uv, uv + pairs * 2) {}
    ~SavedUv() { std::copy(saved.begin(), saved.end(), uv); }
};

}  // namespace

// textured quads and solid quads, two corners each
X86_EXTERN_FUNC(__imp__sub_000D6C00);

namespace {

enum class Film { None, Shown, Menu };
Film g_film = Film::None;

Film CurrentFilm() {
    uint32_t game = R32(kGame);
    uint32_t film = game ? R32(game + kCurrentFilm) : kFilms;
    bool menu = film < kFilms && G<uint8_t>(kFilmTable + film * kFilmEntry + kFilmBehindMenu);
    return menu ? Film::Menu : Film::Shown;
}

// corners are x, y pairs: x (axis 0) or y (axis 1) scaled about the middle
void ScaleQuad(float* q, int axis, float scale) {
    float middle = (q[axis] + q[axis + 2]) / 2;
    q[axis] = middle + (q[axis] - middle) * scale;
    q[axis + 2] = middle + (q[axis + 2] - middle) * scale;
}

}  // namespace

// The film quad covers the frame, shown as wide as the output. A menu's film stays stretched, no wider than 16:9.
// Filled, a film is made taller about the middle so it fills the screen, top and bottom cut; else narrowed to 4:3
X86_FUNC(sub_000D6C00) {
    uint32_t quads = R32(ctx.esp + 8);
    if (g_film == Film::None && g_inset > 0) {
        SavedUv uv(ctx, quads * 2);
        InsetQuads(&G<float>(ctx.eax), uv.uv, quads);
        return Emit(ctx, base, quads * 2, __imp__sub_000D6C00);
    }
    if (g_film == Film::None || !game::Wide()) return Emit(ctx, base, quads * 2, __imp__sub_000D6C00);
    float* q = &G<float>(ctx.eax);
    float saved[4] = {q[0], q[1], q[2], q[3]};
    if (g_film == Film::Menu) ScaleQuad(q, 0, std::min(1.0f, 16.0f / 9.0f / game::Aspect()));
    else if (game::Opt().fmv_scale == 0) ScaleQuad(q, 1, 1 / game::Narrow());
    else ScaleQuad(q, 0, game::Narrow());
    __imp__sub_000D6C00(ctx, base);
    std::copy(saved, saved + 4, q);
}
X86_EXTERN_FUNC(__imp__sub_001217C0);
X86_FUNC(sub_001217C0) {
    g_film = CurrentFilm();
    __imp__sub_001217C0(ctx, base);
    g_film = Film::None;
}
X86_EXTERN_FUNC(__imp__sub_000D6AA0);
X86_FUNC(sub_000D6AA0) { Emit(ctx, base, R32(ctx.esp + 8) * 2, __imp__sub_000D6AA0); }
// textured strip
X86_EXTERN_FUNC(__imp__sub_000D6850);
X86_FUNC(sub_000D6850) {
    uint32_t points = R32(ctx.esp + 8);
    if (points != 4 || g_inset <= 0) return Emit(ctx, base, points, __imp__sub_000D6850);
    SavedUv uv(ctx, points);
    InsetStrip(&G<float>(ctx.eax), uv.uv);
    Emit(ctx, base, points, __imp__sub_000D6850);
}

// Begin. Left alone: the Flash player's, the host's, a smaller target's
X86_EXTERN_FUNC(__imp__sub_000D99A0);
X86_FUNC(sub_000D99A0) {
    uint32_t from = R32(ctx.esp);
    uint32_t display = R32(kDisplay);
    FollowOutput();
    __imp__sub_000D99A0(ctx, base);
    std::lock_guard<std::recursive_mutex> lock(d3d::Lock());
    uint32_t width = d3d::State().viewport.w;
    bool screen = display && width == R32(display) && from != kFromFlash && from != kFromHost;
    g_anchor = screen && game::Wide() ? kCentre : kOff;
    g_width = float(width);
    bool native = from != kFromFlash && from != kFromHost;
    g_inset = native ? std::max(0.0f, 0.5f - 0.5f / app::RenderScale(480)) : 0;
}

// End
X86_EXTERN_FUNC(__imp__sub_000D3740);
X86_FUNC(sub_000D3740) {
    g_anchor = kOff;
    g_inset = 0;
    __imp__sub_000D3740(ctx, base);
}

// HUD and its components
constexpr uint32_t kHudList = 0x40;
constexpr uint32_t kHudComponents = 0xA0;
constexpr uint32_t kComponentFrame = 4;
constexpr uint32_t kComponentNext = 8;
constexpr uint32_t kComponentAnchor = 0x10;
// list item and its frame
constexpr uint32_t kItemFrame = 0x40;
constexpr uint32_t kItemCallback = 0x44;
constexpr uint32_t kFrameHeight = 0xC;

// List draw, ecx the list
X86_EXTERN_FUNC(__imp__sub_000DA4B0);
X86_FUNC(sub_000DA4B0) {
    bool hud = R32(ctx.esp) == kFromHud;
    g_hud = hud ? ctx.ecx - kHudList : 0;
    __imp__sub_000DA4B0(ctx, base);
    g_hud = 0;
}

namespace {

// when the player's HUD last drew an item, hidden by the player or not: the game shows it while the player is in the
// level, a hint's pause too, and not over a cutscene or a loading screen
std::atomic<int64_t> g_hud_ms{0};

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}  // namespace

namespace game {
// native/touch.cpp: the touch controls stay up over a hint's pause, which stops the look as a cutscene does
bool HudShown() { return NowMs() - g_hud_ms.load(std::memory_order_relaxed) < 250; }
}  // namespace game

// A list's item before it draws, ebx. Hidden, the HUD draws only the title card's fade: false skips the callback
bool HudItem(X86Context& ctx, uint8_t*) {
    if (g_hud && R32(ctx.ebx + kItemCallback) != kTitleFade) g_hud_ms.store(NowMs(), std::memory_order_relaxed);
    if (g_hud && game::g_hide_hud && R32(ctx.ebx + kItemCallback) != kTitleFade) return false;
    uint32_t frame = R32(ctx.ebx + kItemFrame);
    float anchor = kCentre;
    for (uint32_t c = g_hud ? R32(g_hud + kHudComponents) : 0; c; c = R32(c + kComponentNext)) {
        if (R32(c + kComponentFrame) != frame) continue;
        anchor = G<float>(kAnchors + R32(c + kComponentAnchor) * kAnchorSize);
        if (float& height = G<float>(frame + kFrameHeight); height == G<float>(kTaller)) height = 1;
    }
    if (g_anchor != kOff) g_anchor = anchor;
    return true;
}
