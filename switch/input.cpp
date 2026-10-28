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

// the motors for each way a player can hold the controller: a Pro Controller or a Joy-Con pair (left, right), one
// Joy-Con held sideways, and for player 1 the Joy-Cons on the console
struct Motors {
    HidNpadIdType id;
    HidNpadStyleTag style;
    int count;
    HidVibrationDeviceHandle handles[2];
    bool ok;
};
Motors g_vib[4][5];
constexpr HidNpadStyleTag kVibStyles[5] = {HidNpadStyleTag_NpadFullKey, HidNpadStyleTag_NpadJoyDual,
                                           HidNpadStyleTag_NpadJoyLeft, HidNpadStyleTag_NpadJoyRight,
                                           HidNpadStyleTag_NpadHandheld};

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
    int motors = 0;
    for (int i = 0; i < 4; i++)
        for (int s = 0; s < 5; s++) {
            Motors& m = g_vib[i][s];
            m.style = kVibStyles[s];
            if (m.style == HidNpadStyleTag_NpadHandheld && i) continue;  // only player 1 plays on the console
            m.id = m.style == HidNpadStyleTag_NpadHandheld ? HidNpadIdType_Handheld : ids[i];
            m.count = (m.style == HidNpadStyleTag_NpadJoyLeft || m.style == HidNpadStyleTag_NpadJoyRight) ? 1 : 2;
            m.ok = R_SUCCEEDED(hidInitializeVibrationDevices(m.handles, m.count, m.id, m.style));
            motors += m.ok;
        }
    LOGI("input: rumble on %d controller grips", motors);
    LOGI("input: libnx HID, 4 ports, gyro aiming %s (gyro.toml)", gyro::ModeName());
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

// the motors run until set again. The Xbox pad's left motor is the heavy one, the right the light buzz: on the
// Switch the left side shakes low and the right side high; one Joy-Con alone takes both
void Rumble(int port, uint16_t left, uint16_t right) {
    if (app::kGame.rumble) app::kGame.rumble(port, left, right);
    if (port < 0 || port > 3) return;
    float heavy = left / 65535.0f, light = right / 65535.0f;
    HidVibrationValue pair[2] = {}, single[1] = {};
    pair[0] = {.amp_low = heavy, .freq_low = 160.0f, .amp_high = heavy * 0.5f, .freq_high = 320.0f};
    pair[1] = {.amp_low = light * 0.5f, .freq_low = 160.0f, .amp_high = light, .freq_high = 320.0f};
    single[0] = {.amp_low = heavy, .freq_low = 160.0f, .amp_high = light, .freq_high = 320.0f};
    for (Motors& m : g_vib[port]) {
        if (!m.ok || !(hidGetNpadStyleSet(m.id) & m.style)) continue;  // only the grip held now
        hidSendVibrationValues(m.handles, m.count == 2 ? pair : single, m.count);
    }
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
