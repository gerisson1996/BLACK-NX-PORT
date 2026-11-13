// Aiming by turning the controller, as the Android host does with the phone: the six-axis sensor of the handheld
// console, the right Joy-Con or a Pro Controller turns the view through the game's mouse look (src/game/mouse.cpp,
// input::TakeMouse). Off, always on, only while ZR is held or only while ZL is held (aiming down the sights, as
// Bullet Witch's port has it); the mode, the sensitivity and the inversion are set in
// the game's options menu (src/common/menu.cpp) and kept in gyro.toml beside the game's settings.
#include <switch.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <mutex>
#include <string>

#include <toml++/toml.hpp>

#include "app/app.h"
#include "common/menu.h"
#include "common/mouse_keyboard.h"
#include "core/log.h"
#include "host.h"

namespace gyro {

namespace {

constexpr float kDegreesPerCount = 0.05f;  // src/game/mouse.cpp's kDegrees
// the sensor's drift is learnt while the controller rests: turning slower than this, gravity steady, for kRestTime.
// Low and slow, so a slow, careful aim is not taken for drift and eaten (at 4 degrees a second it was)
constexpr float kRestTurn = 1.5f;
constexpr float kRestTime = 1.0f;
constexpr float kDriftTime = 3.0f;  // the drift learnt over this many seconds of rest
constexpr float kMaxDrift = 3.0f;   // degrees a second; a controller's drift is under one
constexpr const char* kNames[kModes] = {"OFF", "ON", "FIRE", "AIM"};

std::mutex g_lock;
std::atomic<uint32_t> g_mode{kAiming};  // Cold Fear's: the gyro aims while ZL is held
float g_sensitivity = 1.5f;  // view degrees per degree the controller turns
bool g_invert_x = false;
bool g_invert_y = false;
float g_vertical = 1.0f;  // the look up and down's share of the sensitivity
uint32_t g_axis = kTurn;  // what turns the view left and right
// below this many degrees a second a turn is scaled down, not cut: the shake of a hand held still fades out and a
// slow, careful turn still moves the view, with no step where a hard dead zone would start; 0 none
float g_steady = 1.0f;
bool g_drift = true;  // the sensor's drift learnt at rest and taken off
uint32_t g_assist = kAssistOffWithGyro;
bool g_loaded = false;

// the sensors: handheld, Pro Controller (or one Joy-Con held sideways), the Joy-Con pair (left, right)
HidSixAxisSensorHandle g_handles[4];
bool g_started = false;
uint64_t g_last_sample = 0;
std::chrono::steady_clock::time_point g_last_take;
float g_up[3] = {0, 0, 1};  // the direction gravity holds the controller from, smoothed
float g_bias[3] = {};       // the sensor's drift, degrees a second, learnt at rest
float g_rest = 0;           // seconds the controller has rested
const HidSixAxisSensorHandle* g_sensor = nullptr;  // the drift and gravity are this sensor's
float g_carry_x = 0, g_carry_y = 0;
int g_logged = 0;

std::filesystem::path File() { return app::ExeDir() / "gyro.toml"; }

void Load() {
    g_loaded = true;
    try {
        toml::table t = toml::parse_file(File().string());
        // a gyro.toml from before Cold Fear's gyro: its defaults, not the old settings
        if (t["gyro_model"].value_or(std::string()) != "coldfear") return;
        g_mode = std::min<uint32_t>(t["gyro"].value_or(uint32_t(kAiming)), kModes - 1);
        g_sensitivity = float(t["gyro_sensitivity"].value_or(double(g_sensitivity)));
        g_invert_x = t["gyro_invert_x"].value_or(g_invert_x);
        g_invert_y = t["gyro_invert_y"].value_or(g_invert_y);
        g_vertical = std::clamp(float(t["gyro_vertical"].value_or(double(g_vertical))), 0.1f, 3.0f);
        g_axis = std::min<uint32_t>(t["gyro_axis"].value_or(uint32_t(kTurn)), kAxes - 1);
        g_steady = std::clamp(float(t["gyro_steadiness"].value_or(double(g_steady))), 0.0f, 10.0f);
        g_drift = t["gyro_drift_fix"].value_or(g_drift);
        g_assist = std::min<uint32_t>(t["aim_assist"].value_or(uint32_t(g_assist)), kAssists - 1);
    } catch (...) {
    }
}

void Save() {
    toml::table t{{"gyro_model", "coldfear"},
                  {"gyro", int64_t(g_mode.load())},
                  {"gyro_sensitivity", double(g_sensitivity)},
                  {"gyro_invert_x", g_invert_x},
                  {"gyro_invert_y", g_invert_y},
                  {"gyro_vertical", double(g_vertical)},
                  {"gyro_axis", int64_t(g_axis)},
                  {"gyro_steadiness", double(g_steady)},
                  {"gyro_drift_fix", g_drift},
                  {"aim_assist", int64_t(g_assist)}};
    std::ofstream(File()) << t << "\n";
}

void Start() {
    if (g_started) return;
    hidGetSixAxisSensorHandles(&g_handles[0], 1, HidNpadIdType_Handheld, HidNpadStyleTag_NpadHandheld);
    hidGetSixAxisSensorHandles(&g_handles[1], 1, HidNpadIdType_No1, HidNpadStyleTag_NpadFullKey);
    hidGetSixAxisSensorHandles(&g_handles[2], 2, HidNpadIdType_No1, HidNpadStyleTag_NpadJoyDual);
    for (const HidSixAxisSensorHandle& h : g_handles) hidStartSixAxisSensor(h);
    g_started = true;
    g_last_sample = 0;
    LOGI("gyro: sensors on");
}

void Stop() {
    if (!g_started) return;
    for (const HidSixAxisSensorHandle& h : g_handles) hidStopSixAxisSensor(h);
    g_started = false;
    LOGI("gyro: sensors off");
}

// the sensor of what player 1 plays with: the console in handheld, else the Pro Controller or the right Joy-Con
const HidSixAxisSensorHandle* Current() {
    if (hidGetNpadStyleSet(HidNpadIdType_Handheld) & HidNpadStyleTag_NpadHandheld) return &g_handles[0];
    u32 style = hidGetNpadStyleSet(HidNpadIdType_No1);
    if (style & HidNpadStyleTag_NpadFullKey) return &g_handles[1];
    if (style & HidNpadStyleTag_NpadJoyDual) return &g_handles[3];
    return nullptr;
}

float Soft(float rate) {
    float r = std::fabs(rate);
    return r < g_steady ? rate * (r / g_steady) : rate;
}

// view counts for the samples since the last call. The sensor reads in its own axes, turns a second (1 = 360
// degrees): x to the right, y away from the player, z out of the face. The turn about the world's up axis, wherever
// the controller is tilted (lying flat or the console held upright), is the turn about the direction the
// accelerometer says is up; the look up and down is the turn about x, level in either grip. Each sample is added for
// its own time, so no turn is lost or doubled when the game's frame rate moves (15 to 60 fps)
bool Take(float& dx, float& dy) {
    auto now = std::chrono::steady_clock::now();
    float wall = std::chrono::duration<float>(now - g_last_take).count();
    g_last_take = now;
    const HidSixAxisSensorHandle* h = Current();
    if (!h) return false;
    if (h != g_sensor) {  // another controller: its own drift and its own gravity
        g_sensor = h;
        g_last_sample = 0;
        g_rest = 0;
        for (float& b : g_bias) b = 0;
    }
    HidSixAxisSensorState states[16];
    size_t n = hidGetSixAxisSensorStates(*h, states, 16);
    if (!n) return false;
    std::sort(states, states + n, [](const HidSixAxisSensorState& l, const HidSixAxisSensorState& r) {
        return l.sampling_number < r.sampling_number;
    });
    bool first = !g_last_sample;
    int fresh = 0;
    for (size_t i = 0; i < n; i++) fresh += states[i].sampling_number > g_last_sample;
    if (!fresh) return false;
    float yaw = 0, pitch = 0;  // degrees turned
    for (size_t i = 0; i < n; i++) {
        const HidSixAxisSensorState& s = states[i];
        if (s.sampling_number <= g_last_sample) continue;
        g_last_sample = s.sampling_number;
        // the sample's own time, in nanoseconds; the time since the last call shared out when it reads nonsense
        float dt = float(s.delta_time) * 1e-9f;
        if (!(dt > 0.0005f && dt < 0.05f)) dt = std::clamp(wall / float(fresh), 0.0f, 0.05f);
        float w[3] = {s.angular_velocity.x * 360.0f, s.angular_velocity.y * 360.0f, s.angular_velocity.z * 360.0f};
        float a[3] = {s.acceleration.x, s.acceleration.y, s.acceleration.z};
        if (g_logged < 3) {
            g_logged++;
            LOGI("gyro: turn %.1f %.1f %.1f deg/s, gravity %.2f %.2f %.2f g, sample %.2f ms", w[0], w[1], w[2], a[0],
                 a[1], a[2], dt * 1000.0f);
        }
        float len = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
        // gravity steady: about one g, and the same direction as the filtered one (a hand moving the controller
        // tilts it)
        float along = len > 0 ? (a[0] * g_up[0] + a[1] * g_up[1] + a[2] * g_up[2]) / len : 0;
        bool steady = len > 0.95f && len < 1.05f && along > 0.999f;
        if (len > 0.5f && len < 1.5f) {
            float k = first ? 1.0f : std::min(1.0f, dt / 0.3f);
            for (int j = 0; j < 3; j++) g_up[j] += (a[j] / len - g_up[j]) * k;
            float ul = std::sqrt(g_up[0] * g_up[0] + g_up[1] * g_up[1] + g_up[2] * g_up[2]);
            for (float& u : g_up) u /= ul;
        }
        first = false;
        // at rest the sensor still reads a small turn: that is its drift, taken off every reading
        float raw = std::sqrt((w[0] - g_bias[0]) * (w[0] - g_bias[0]) + (w[1] - g_bias[1]) * (w[1] - g_bias[1]) +
                              (w[2] - g_bias[2]) * (w[2] - g_bias[2]));
        g_rest = steady && raw < kRestTurn ? g_rest + dt : 0;
        if (!g_drift) {
            for (float& b : g_bias) b = 0;
        } else if (g_rest > kRestTime) {
            float k = std::min(1.0f, dt / kDriftTime);
            for (int j = 0; j < 3; j++)
                g_bias[j] = std::clamp(g_bias[j] + (w[j] - g_bias[j]) * k, -kMaxDrift, kMaxDrift);
        }
        for (int j = 0; j < 3; j++) w[j] -= g_bias[j];
        // turning about the world's up axis, or leaning: the turn about the axis away from the player, level
        // (up crossed with the controller's right; up is the accelerometer's minus, so is this, and the signs agree)
        float turn = w[0] * g_up[0] + w[1] * g_up[1] + w[2] * g_up[2];
        float fl = std::sqrt(g_up[1] * g_up[1] + g_up[2] * g_up[2]);
        float lean = fl > 0.3f ? (w[1] * g_up[2] - w[2] * g_up[1]) / fl : 0;
        float side = g_axis == kTurn ? turn : g_axis == kLean ? lean : turn + lean;
        yaw += Soft(side) * dt;  // positive: left
        pitch += Soft(w[0]) * dt;                                              // positive: looking up
    }
    // a pause (the game not asking, a menu): no turn piled up
    if (wall <= 0 || wall > 0.25f || (!yaw && !pitch)) return false;
    float k = g_sensitivity / kDegreesPerCount;
    dx = (g_invert_x ? -yaw : yaw) * k;  // the console's up is the accelerometer's minus: a turn left reads negative
    dy = (g_invert_y ? pitch : -pitch) * k * g_vertical;
    return true;
}

// the gyro turns the view now: always, or while the mode's trigger is held
bool Held(uint32_t mode) {
    return mode == kAlways || (mode == kFiring && (input::NavButtons() & HidNpadButton_ZR)) ||
           (mode == kAiming && (input::NavButtons() & HidNpadButton_ZL));
}

}  // namespace

uint32_t CurrentMode() {
    std::lock_guard lock(g_lock);
    if (!g_loaded) Load();
    return g_mode;
}

// the game's options menu (src/common/menu.cpp) sets these; saved at once
void SetMode(uint32_t mode) {
    std::lock_guard lock(g_lock);
    if (!g_loaded) Load();
    g_mode = mode % kModes;
    LOGI("gyro: %s", kNames[g_mode]);
    Save();
}

float Sensitivity() {
    std::lock_guard lock(g_lock);
    if (!g_loaded) Load();
    return g_sensitivity;
}

void SetSensitivity(float s) {
    std::lock_guard lock(g_lock);
    if (!g_loaded) Load();
    g_sensitivity = std::clamp(s, 0.1f, 10.0f);
    Save();
}

bool InvertX() {
    std::lock_guard lock(g_lock);
    if (!g_loaded) Load();
    return g_invert_x;
}

bool InvertY() {
    std::lock_guard lock(g_lock);
    if (!g_loaded) Load();
    return g_invert_y;
}

void SetInvert(bool x, bool y) {
    std::lock_guard lock(g_lock);
    if (!g_loaded) Load();
    g_invert_x = x;
    g_invert_y = y;
    Save();
}

float Vertical() {
    std::lock_guard lock(g_lock);
    if (!g_loaded) Load();
    return g_vertical;
}

void SetVertical(float v) {
    std::lock_guard lock(g_lock);
    if (!g_loaded) Load();
    g_vertical = std::clamp(v, 0.1f, 3.0f);
    Save();
}

uint32_t Axis() {
    std::lock_guard lock(g_lock);
    if (!g_loaded) Load();
    return g_axis;
}

void SetAxis(uint32_t a) {
    std::lock_guard lock(g_lock);
    if (!g_loaded) Load();
    g_axis = a % kAxes;
    Save();
}

float Steadiness() {
    std::lock_guard lock(g_lock);
    if (!g_loaded) Load();
    return g_steady;
}

void SetSteadiness(float s) {
    std::lock_guard lock(g_lock);
    if (!g_loaded) Load();
    g_steady = std::clamp(s, 0.0f, 10.0f);
    Save();
}

bool DriftFix() {
    std::lock_guard lock(g_lock);
    if (!g_loaded) Load();
    return g_drift;
}

void SetDriftFix(bool on) {
    std::lock_guard lock(g_lock);
    if (!g_loaded) Load();
    g_drift = on;
    Save();
}

uint32_t AimAssist() {
    std::lock_guard lock(g_lock);
    if (!g_loaded) Load();
    return g_assist;
}

void SetAimAssist(uint32_t a) {
    std::lock_guard lock(g_lock);
    if (!g_loaded) Load();
    g_assist = a % kAssists;
    Save();
}

const char* ModeName() { return kNames[CurrentMode() % kModes]; }

void NextMode() { SetMode(g_mode + 1); }

}  // namespace gyro

namespace input {

// the game's aim assist is off while this holds (src/game/mouse.cpp). Off only while the gyro turned the view was
// not enough: with the trigger let go the pull came back and dragged the aim to the nearest enemy between two gyro
// turns. Now off whenever the gyro is on, or always, as set
bool GyroAiming() {
    uint32_t a = gyro::AimAssist();
    if (a == gyro::kAssistOff) return true;
    if (a == gyro::kAssistOffWithGyro) return gyro::CurrentMode() != gyro::kOff;
    return gyro::Held(gyro::CurrentMode());
}

// the game's mouse look, each tick in play: the gyro's turn as mouse counts, whole ones, the rest carried to the next
bool TakeMouse(float& dx, float& dy) {
    using namespace gyro;
    std::lock_guard lock(g_lock);
    if (!g_loaded) Load();
    if (g_mode == kOff) {
        Stop();
        return false;
    }
    Start();
    float x = 0, y = 0;
    if (!Take(x, y) || app::MenuOpen()) return false;
    if (!Held(g_mode)) {
        g_carry_x = g_carry_y = 0;
        return false;
    }
    g_carry_x += x;
    g_carry_y += y;
    float ix = std::trunc(g_carry_x), iy = std::trunc(g_carry_y);
    g_carry_x -= ix;
    g_carry_y -= iy;
    if (!ix && !iy) return false;
    dx = ix;
    dy = iy;
    return true;
}

}  // namespace input
