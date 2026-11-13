#pragma once
// What the Switch host's parts share: the screen, the applet's focus and the controllers. The same interfaces the
// Android host has (vulkan.cpp, game_host.cpp and src/common/menu.cpp call them), without SDL.
#include <cstdint>

namespace input {
struct Pad;
}

namespace host {

// the default NWindow of the applet (vulkan.cpp makes its surface on it)
void* Window();
// the applet has the focus; the renderer and the game wait while it does not
bool Foreground();
// bumped when the screen changes size (docked or handheld): the renderer makes its swapchain again
uint32_t SurfaceEpoch();
// the game thread, before a present: returns once the applet has the focus again
void WaitForeground();
// the screen's size in pixels: 1280x720 handheld, 1920x1080 docked
void WindowPixels(int& w, int& h);
// the options menu's update check: nothing to ask on the Switch
void CheckForUpdates();

}  // namespace host

// controllers through libnx's HID (input.cpp)
namespace input {
// the first controller's buttons as libnx reports them (HidNpadButton_*), for ImGui's navigation (ui.cpp)
uint64_t NavButtons();
void NavStick(float& x, float& y);
}  // namespace input

// the Android touch controls: none on the Switch (stubs.cpp), here for vulkan.cpp and the options menu
namespace touch {
void Apply(int port, input::Pad& pad);
void Draw();
bool Shown();
void AddLook(float dx, float dy);
bool LookActive();
bool Firing();
void PadUsed();
const char* LayoutName();
void StepLayout(int step);
uint32_t ControlsOpacity();
void StepOpacity(int step);
uint32_t ControlsSize();
void StepSize(int step);
void EditLayout();
}  // namespace touch

// aiming by turning the phone: none on the Switch (stubs.cpp), for the options menu
namespace gyro {
enum Mode : uint32_t { kOff, kAlways, kFiring, kAiming, kModes };  // kFiring: ZR held, kAiming: ZL held
uint32_t CurrentMode();
void SetMode(uint32_t mode);
const char* ModeName();
void NextMode();
float Sensitivity();
void SetSensitivity(float s);
bool InvertX();
bool InvertY();
void SetInvert(bool x, bool y);
enum Axis : uint32_t { kTurn, kLean, kTurnAndLean, kAxes };  // what turns the view left and right
float Vertical();  // the look up and down's share of the sensitivity
void SetVertical(float v);
uint32_t Axis();
void SetAxis(uint32_t a);
float Steadiness();  // degrees a second under which a turn is scaled down, 0 none
void SetSteadiness(float s);
bool DriftFix();  // the drift learnt at rest
void SetDriftFix(bool on);
// the game's aim assist (the pull of the view to a target, the slow down near one, shots bent to one)
enum Assist : uint32_t { kAssistOn, kAssistOffWithGyro, kAssistOff, kAssists };
uint32_t AimAssist();
void SetAimAssist(uint32_t a);
}  // namespace gyro

// the handheld screen's shadow lift (screen.cpp)
namespace screen {
float HandheldGamma();  // 1 none
void SetHandheldGamma(float g);
float Gamma();  // the exponent at scan-out now: the handheld one in handheld, 1 docked
}  // namespace screen

// ImGui's frame on the Switch (ui.cpp): render thread, before ImGui::NewFrame
namespace ui {
void NewFrame(float width, float height);
}  // namespace ui
