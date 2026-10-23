// What the Android host has and the Switch does not: touch controls, gyro aiming, a keyboard and mouse, the update
// check. Neutral answers for the game's code and the options menu, which ask for them by name.
#include <fcntl.h>
#include <unistd.h>

#include <mutex>
#include <string>

#include "common/mouse_keyboard.h"
#include "core/input.h"
#include "host.h"

// newlib has no pread: the SDK's disc image reader (kernel/disc.cpp) reads at offsets from several threads, so the
// seek and the read go together under one lock
extern "C" ssize_t pread(int fd, void* buf, size_t count, off_t offset) {
    static std::mutex lock;
    std::lock_guard<std::mutex> hold(lock);
    if (lseek(fd, offset, SEEK_SET) < 0) return -1;
    return read(fd, buf, count);
}

namespace host {
void CheckForUpdates() {}
}  // namespace host

namespace touch {
void Apply(int, input::Pad&) {}
void Draw() {}
bool Shown() { return false; }
void AddLook(float, float) {}
bool LookActive() { return false; }
bool Firing() { return false; }
void PadUsed() {}
const char* LayoutName() { return "-"; }
void StepLayout(int) {}
uint32_t ControlsOpacity() { return 0; }
void StepOpacity(int) {}
uint32_t ControlsSize() { return 0; }
void StepSize(int) {}
void EditLayout() {}
}  // namespace touch

namespace gyro {
uint32_t CurrentMode() { return kOff; }
void SetMode(uint32_t) {}
const char* ModeName() { return "OFF"; }
void NextMode() {}
float Sensitivity() { return 1.0f; }
void SetSensitivity(float) {}
bool InvertX() { return false; }
bool InvertY() { return false; }
void SetInvert(bool, bool) {}
}  // namespace gyro

// no keyboard or mouse: the controller is the only input, so no key binds, no cursor, no typed text
namespace input {
void ReleaseCursor() {}
void Keyboard(Pad&) {}
bool TakeMouse(float&, float&) { return false; }
const Action* Actions(size_t* count) {
    if (count) *count = 0;
    return nullptr;
}
std::string Binding(size_t) { return {}; }
void Bind(size_t, int, int) {}
void Unbind(size_t) {}
bool TakeDeletePress() { return false; }
void ResetBindings() {}
void LoadBindings() {}
int PressedKey() { return 0; }
bool PanelKey(int) { return false; }
bool HudKey(int) { return false; }
std::string ButtonKey(int) { return {}; }
bool TouchActive() { return false; }
std::string TouchName(int) { return {}; }
void SeenPad(const Pad&) {}
void SetMenu(bool) {}
bool KeyboardActive() { return false; }
bool CursorShown() { return false; }
bool Cursor(float&, float&, bool&) { return false; }
int TakeWheel() { return 0; }
bool LeftButton() { return false; }
void TapConfirm() {}
void SetTextEntry(bool) {}
void OnChar(unsigned) {}
std::u16string TakeChars() { return {}; }
}  // namespace input

// libnx gives newlib no thread_detach, so pthread_detach (std::thread::detach) fails with ENOSYS. The runtime's
// detached threads (the kernel clock, rendering, audio, the guest's threads) mostly live as long as the title; one
// that ends keeps its stack and handle until the process exits.
extern "C" int __syscall_thread_detach(struct __pthread_t*) { return 0; }
