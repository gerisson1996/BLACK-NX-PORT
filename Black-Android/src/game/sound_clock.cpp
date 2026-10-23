// The sound library's millisecond clock steps the time stamp counter since its last call: a focus pause is no time
#include "common/options.h"
#include "xdk/hle.h"

using namespace kernel;

namespace {

constexpr uint32_t kSoundClockLastTsc = 0x2C02A4;  // low half of the counter at the clock's last step, 0x82266

uint32_t g_pauses_seen = 0;

}  // namespace

// the clock step 0x82210: clock 0x2C02A0 += counter ticks since kSoundClockLastTsc as ms
X86_EXTERN_FUNC(__imp__sub_00082210);
X86_FUNC(sub_00082210) {
    uint32_t pauses = game::g_focus_pauses;
    if (game::g_focus_paused || pauses != g_pauses_seen) {
        W32(kSoundClockLastTsc, uint32_t(x86_rdtsc()));
        g_pauses_seen = pauses;
    }
    __imp__sub_00082210(ctx, base);
}
