// ImGui's frame on the Switch: the screen size and frame time, and player 1's controller for the options panels'
// navigation (no touch, keyboard or mouse here)
#include <switch.h>

#include <chrono>
#include <cmath>

#include <imgui.h>

#include "common/menu.h"
#include "host.h"

namespace ui {

void NewFrame(float width, float height) {
    ImGuiIO& io = ImGui::GetIO();
    using clock = std::chrono::steady_clock;
    static clock::time_point last = clock::now();
    auto now = clock::now();
    float dt = std::chrono::duration<float>(now - last).count();
    last = now;
    io.DeltaTime = dt > 0 ? dt : 1.0f / 60;
    io.DisplaySize = ImVec2(width, height);
    io.DisplayFramebufferScale = ImVec2(1, 1);

    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    uint64_t b = input::NavButtons();
    // both sticks pressed in together open and close the options panel, the F10 of the PC builds
    constexpr uint64_t kCombo = HidNpadButton_StickL | HidNpadButton_StickR;
    static bool combo = false;
    bool both = (b & kCombo) == kCombo;
    if (both && !combo) app::ToggleMenu();
    combo = both;
    struct Map {
        ImGuiKey key;
        uint64_t button;
    };
    // face buttons by name, as the game's own controls: A confirms, B goes back
    const Map kMap[] = {
        {ImGuiKey_GamepadStart, HidNpadButton_Plus},     {ImGuiKey_GamepadBack, HidNpadButton_Minus},
        {ImGuiKey_GamepadFaceDown, HidNpadButton_A},     {ImGuiKey_GamepadFaceRight, HidNpadButton_B},
        {ImGuiKey_GamepadFaceLeft, HidNpadButton_X},     {ImGuiKey_GamepadFaceUp, HidNpadButton_Y},
        {ImGuiKey_GamepadDpadLeft, HidNpadButton_Left},  {ImGuiKey_GamepadDpadRight, HidNpadButton_Right},
        {ImGuiKey_GamepadDpadUp, HidNpadButton_Up},      {ImGuiKey_GamepadDpadDown, HidNpadButton_Down},
        {ImGuiKey_GamepadL1, HidNpadButton_L},           {ImGuiKey_GamepadR1, HidNpadButton_R},
    };
    // the left stick is the dpad here: ImGui moves its focus only on the dpad and scrolls on the stick, so a stick
    // pushed up or down scrolled the panel instead of going to the next row
    float x = 0, y = 0;
    input::NavStick(x, y);
    constexpr float kPush = 0.5f;
    if (std::fabs(x) > std::fabs(y)) {
        if (x < -kPush) b |= HidNpadButton_Left;
        if (x > kPush) b |= HidNpadButton_Right;
    } else {
        if (y > kPush) b |= HidNpadButton_Up;  // libnx: up is positive
        if (y < -kPush) b |= HidNpadButton_Down;
    }
    for (const Map& m : kMap) io.AddKeyEvent(m.key, (b & m.button) != 0);
}

}  // namespace ui
