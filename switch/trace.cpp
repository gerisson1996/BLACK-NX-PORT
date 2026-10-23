// Diagnostics for the sound library's start on the Switch: its class lookup (0x81860), its object creation (0x81B70)
// and the call that walks a class's list (0x82290), and the game's file calls. The first lines of each go to the log.
//
// Off by default: they write to the card and slow the game. Uncomment the next line to turn them on.
// #define BLACK_TRACE
#ifdef BLACK_TRACE
#include <atomic>

#include "xdk/hle.h"

using namespace kernel;

namespace {

std::atomic<int> g_lines{0};
constexpr int kMaxLines = 400;

bool More() { return g_lines.fetch_add(1) < kMaxLines; }

uint32_t Arg(X86Context& ctx, int i) { return R32(ctx.esp + 4 + uint32_t(i) * 4); }

}  // namespace

X86_EXTERN_FUNC(__imp__sub_00081860);
X86_FUNC(sub_00081860) {
    uint32_t ret = R32(ctx.esp), a0 = Arg(ctx, 0), a1 = Arg(ctx, 1), a2 = Arg(ctx, 2), a3 = Arg(ctx, 3);
    __imp__sub_00081860(ctx, base);
    // 0xB7F90 and 0xBDE51 look up the same class many times over
    if (ret != 0xB7F90 && ret != 0xBDE51 && More())
        LOGI("trace: lookup(%08X, guid %08X, %08X, %08X) from %08X -> %08X, +4C %08X", a0, a1, a2, a3, ret, ctx.eax,
             ctx.eax && ctx.eax < 0xF0000000u ? R32(ctx.eax + 0x4C) : 0);
}

X86_EXTERN_FUNC(__imp__sub_00081B70);
X86_FUNC(sub_00081B70) {
    uint32_t ret = R32(ctx.esp), a0 = Arg(ctx, 0), a1 = Arg(ctx, 1), a2 = Arg(ctx, 2), a3 = Arg(ctx, 3),
             a4 = Arg(ctx, 4), a5 = Arg(ctx, 5);
    __imp__sub_00081B70(ctx, base);
    if (More())
        LOGI("trace: create(%08X, flags %X, %08X, %08X, %08X, tag %X) from %08X -> %08X, +4C %08X", a0, a1, a2, a3, a4,
             a5, ret, ctx.eax, ctx.eax && ctx.eax < 0xF0000000u ? R32(ctx.eax + 0x4C) : 0);
}

X86_EXTERN_FUNC(__imp__sub_00082290);
X86_FUNC(sub_00082290) {
    uint32_t x = Arg(ctx, 0), obj = x ? R32(x) : 0;
    if (More())
        LOGI("trace: walk(%08X, %08X, %X) from %08X: object %08X, +4C %08X", x, Arg(ctx, 1), Arg(ctx, 2), R32(ctx.esp),
             obj, obj ? R32(obj + 0x4C) : 0);
    logx::Flush();
    __imp__sub_00082290(ctx, base);
}

// the generic constructor (class, ...) the output device comes from, 0x8C090 -> 0x830D0
X86_EXTERN_FUNC(__imp__sub_000830D0);
X86_FUNC(sub_000830D0) {
    uint32_t ret = R32(ctx.esp), a0 = Arg(ctx, 0), a1 = Arg(ctx, 1), a2 = Arg(ctx, 2), a3 = Arg(ctx, 3),
             a4 = Arg(ctx, 4), a5 = Arg(ctx, 5);
    __imp__sub_000830D0(ctx, base);
    if (More())
        LOGI("trace: construct(class %08X, %08X, %08X, %08X, %08X, %08X) from %08X -> %08X", a0, a1, a2, a3, a4, a5, ret,
             ctx.eax);
}

// the library's allocators (size, tag) and (pool, tag): a failure is what matters
X86_EXTERN_FUNC(__imp__sub_00086590);
X86_FUNC(sub_00086590) {
    uint32_t ret = R32(ctx.esp), a0 = Arg(ctx, 0), a1 = Arg(ctx, 1);
    __imp__sub_00086590(ctx, base);
    if (!ctx.eax && More()) LOGW("trace: alloc(%X bytes, tag %X) from %08X failed", a0, a1, ret);
}

X86_EXTERN_FUNC(__imp__sub_00086120);
X86_FUNC(sub_00086120) {
    uint32_t ret = R32(ctx.esp), a0 = Arg(ctx, 0), a1 = Arg(ctx, 1);
    __imp__sub_00086120(ctx, base);
    if (!ctx.eax && More()) LOGW("trace: pool alloc(%08X, tag %X) from %08X failed", a0, a1, ret);
}

// the sound output device (0, config, 2, 0, 0), 0xBDD4E: zero here leaves the device handle empty
X86_EXTERN_FUNC(__imp__sub_0008C090);
X86_FUNC(sub_0008C090) {
    uint32_t ret = R32(ctx.esp), cfg = Arg(ctx, 1);
    if (More())
        LOGI("trace: output device(%08X, config %08X [%08X %08X %08X %08X %08X %08X %08X %08X], %X) from %08X", Arg(ctx, 0),
             cfg, R32(cfg), R32(cfg + 4), R32(cfg + 8), R32(cfg + 12), R32(cfg + 16), R32(cfg + 20), R32(cfg + 24),
             R32(cfg + 28), Arg(ctx, 2), ret);
    __imp__sub_0008C090(ctx, base);
    if (More()) LOGI("trace: output device -> %08X", ctx.eax);
}

// the game's file calls the effects image goes through, 0x8C570: open, seek, tell, read
X86_EXTERN_FUNC(__imp__sub_0008AEB0);
X86_FUNC(sub_0008AEB0) {
    uint32_t ret = R32(ctx.esp), name = Arg(ctx, 0), mode = Arg(ctx, 1);
    __imp__sub_0008AEB0(ctx, base);
    if (More())
        LOGI("trace: fopen(%s, %s) from %08X -> %08X", name ? ReadCString(name).c_str() : "-",
             mode ? ReadCString(mode).c_str() : "-", ret, ctx.eax);
}

X86_EXTERN_FUNC(__imp__sub_000918D0);
X86_FUNC(sub_000918D0) {
    uint32_t ret = R32(ctx.esp), f = Arg(ctx, 0), off = Arg(ctx, 1), whence = Arg(ctx, 2);
    __imp__sub_000918D0(ctx, base);
    if (More()) LOGI("trace: fseek(%08X, %X, %u) from %08X -> %08X", f, off, whence, ret, ctx.eax);
}

X86_EXTERN_FUNC(__imp__sub_00091970);
X86_FUNC(sub_00091970) {
    uint32_t ret = R32(ctx.esp), f = Arg(ctx, 0);
    __imp__sub_00091970(ctx, base);
    if (More()) LOGI("trace: ftell(%08X) from %08X -> %08X", f, ret, ctx.eax);
}

X86_EXTERN_FUNC(__imp__sub_00091870);
X86_FUNC(sub_00091870) {
    uint32_t ret = R32(ctx.esp), buf = Arg(ctx, 0), size = Arg(ctx, 1), count = Arg(ctx, 2), f = Arg(ctx, 3);
    __imp__sub_00091870(ctx, base);
    if (More()) LOGI("trace: fread(%08X, %X, %X, %08X) from %08X -> %08X", buf, size, count, f, ret, ctx.eax);
}

#endif  // BLACK_TRACE
