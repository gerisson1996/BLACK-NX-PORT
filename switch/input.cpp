// controllers through libnx's HID: the handheld Joy-Cons or a pair/Pro Controller as player 1, players 2-4 on their
// own pads. Buttons by name, as the earlier xboxrecomp port mapped them: A is the Xbox A (confirm), B is B, X is X,
// Y is Y; R and L are the black and white buttons, ZL and ZR the triggers.
#include "core/input.h"

#include <switch.h>

#include <algorithm>
#include <cmath>
#include <mutex>

#include "app/app.h"
#include "common/options.h"
#include "core/log.h"
#include "host.h"

namespace input {

namespace {

std::mutex g_lock;
PadState g_pads[4];
HidVibrationDeviceHandle g_vib[4][2];
bool g_vib_ok[4];

int16_t Axis(float v) { return int16_t(std::clamp(v, -1.0f, 1.0f) * 32767.0f); }

// as the Android host: a radial dead zone, then the travel past it times the sensitivity
void Shape(int16_t& x, int16_t& y, uint32_t deadzone, float sensitivity) {
    if (!deadzone && sensitivity == 1.0f) return;
    constexpr float kFull = 32767.0f;
    float fx = float(x) / kFull, fy = float(y) / kFull;
    float m = std::sqrt(fx * fx + fy * fy);
    float dz = std::min(float(deadzone), 90.0f) / 100.0f;
    if (m <= dz) {
        x = y = 0;
        return;
    }
    float k = (m - dz) / (1.0f - dz) * sensitivity / m;
    x = Axis(fx * k);
    y = Axis(fy * k);
}

void Read(PadState& s, Pad& p) {
    padUpdate(&s);
    if (!padIsConnected(&s)) return;
    p.connected = true;
    u64 b = padGetButtons(&s);
    // dpad up/down/left/right, start, back, left thumb, right thumb
    constexpr u64 kButtons[8] = {HidNpadButton_Up,   HidNpadButton_Down,  HidNpadButton_Left,     HidNpadButton_Right,
                                 HidNpadButton_Plus, HidNpadButton_Minus, HidNpadButton_StickL, HidNpadButton_StickR};
    for (int i = 0; i < 8; i++)
        if (b & kButtons[i]) p.buttons |= uint16_t(1u << i);
    // A B X Y black white left trigger right trigger
    constexpr u64 kAnalog[8] = {HidNpadButton_A, HidNpadButton_B, HidNpadButton_X,  HidNpadButton_Y,
                                HidNpadButton_R, HidNpadButton_L, HidNpadButton_ZL, HidNpadButton_ZR};
    for (int i = 0; i < 8; i++) p.analog[i] = (b & kAnalog[i]) ? 0xFF : 0;
    // libnx's sticks are +-32767 with up positive, as the console's
    HidAnalogStickState l = padGetStickPos(&s, 0), r = padGetStickPos(&s, 1);
    p.lx = int16_t(std::clamp(l.x, -32767, 32767));
    p.ly = int16_t(std::clamp(l.y, -32767, 32767));
    p.rx = int16_t(std::clamp(r.x, -32767, 32767));
    p.ry = int16_t(std::clamp(r.y, -32767, 32767));
}

}  // namespace

void Init() {
    padConfigureInput(4, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&g_pads[0]);  // player 1 and the handheld Joy-Cons
    padInitialize(&g_pads[1], HidNpadIdType_No2);
    padInitialize(&g_pads[2], HidNpadIdType_No3);
    padInitialize(&g_pads[3], HidNpadIdType_No4);
    const HidNpadIdType ids[4] = {HidNpadIdType_No1, HidNpadIdType_No2, HidNpadIdType_No3, HidNpadIdType_No4};
    for (int i = 0; i < 4; i++)
        g_vib_ok[i] = R_SUCCEEDED(hidInitializeVibrationDevices(g_vib[i], 2, ids[i], HidNpadStyleTag_NpadFullKey));
    LOGI("input: libnx HID, 4 ports");
}

Pad Poll(int port) {
    Pad p;
    if (port < 0 || port > 3) return p;
    {
        std::lock_guard lock(g_lock);
        Read(g_pads[port], p);
    }
    const game::Options& o = game::Opt();
    Shape(p.lx, p.ly, o.left_deadzone, o.left_sensitivity);
    Shape(p.rx, p.ry, o.right_deadzone, o.right_sensitivity);
    if (app::kGame.pad) app::kGame.pad(port, p);
    return p;
}

// the motors run until set again
void Rumble(int port, uint16_t left, uint16_t right) {
    if (app::kGame.rumble) app::kGame.rumble(port, left, right);
    if (port < 0 || port > 3 || !g_vib_ok[port]) return;
    HidVibrationValue v[2] = {};
    float amp[2] = {left / 65535.0f, right / 65535.0f};
    for (int i = 0; i < 2; i++) {
        v[i].amp_low = amp[i];
        v[i].freq_low = 160.0f;
        v[i].amp_high = amp[i];
        v[i].freq_high = 320.0f;
    }
    hidSendVibrationValues(g_vib[port], v, 2);
}

uint64_t NavButtons() {
    std::lock_guard lock(g_lock);
    return padIsConnected(&g_pads[0]) ? padGetButtons(&g_pads[0]) : 0;
}

void NavStick(float& x, float& y) {
    std::lock_guard lock(g_lock);
    HidAnalogStickState l = padGetStickPos(&g_pads[0], 0);
    x = l.x / 32767.0f;
    y = l.y / 32767.0f;
}

}  // namespace input
