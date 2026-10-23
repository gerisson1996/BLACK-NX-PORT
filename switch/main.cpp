// The Switch host: the applet's loop, the settings and folders under sdmc:/switch/black, the screen size; the game is
// described by app::kGame (src/common/game_info.cpp). The Android host's main.cpp with libnx in place of SDL.
#include <switch.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <ctime>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>

#include <imgui.h>
#include <toml++/toml.hpp>

#include "app/app.h"
#include "build_info.h"
#include "core/input.h"
#include "core/log.h"
#include "host.h"
#include "image_symbols.h"
#include "kernel/kernel.h"

namespace {

constexpr const char* kDir = "sdmc:/switch/black";

std::atomic<uint32_t> g_surface_epoch{0};
AppletOperationMode g_op_mode = AppletOperationMode_Handheld;

app::Mode ScreenMode() {
    return g_op_mode == AppletOperationMode_Console ? app::Mode{1920, 1080} : app::Mode{1280, 720};
}

}  // namespace

namespace host {

void* Window() { return nwindowGetDefault(); }
bool Foreground() { return true; }
uint32_t SurfaceEpoch() { return g_surface_epoch; }
void WaitForeground() {}

void WindowPixels(int& w, int& h) {
    app::Mode m = ScreenMode();
    w = int(m.width);
    h = int(m.height);
}

}  // namespace host

namespace app {

namespace {

const char* kModes[] = {"fullscreen", "borderless", "windowed"};

std::filesystem::path File() { return ExeDir() / kGame.settings; }

toml::table Table() {
    const Settings& s = Get();
    toml::table t{{"display_mode", kModes[uint32_t(s.display_mode)]},
                  {"width", int64_t(s.width)},
                  {"height", int64_t(s.height)},
                  {"render_percent", int64_t(s.render_percent)},
                  {"fps_cap", int64_t(s.fps_cap)},
                  {"vsync", s.vsync},
                  {"anisotropic_filtering", int64_t(s.aniso)},
                  {"join_ip", s.join_ip}};
    for (const Option& o : kGame.options) {
        if (o.flag) t.insert(o.key, *o.flag);
        else t.insert(o.key, int64_t(*o.value));
    }
    if (kGame.save) kGame.save(&t);
    return t;
}

}  // namespace

// the Switch starts at the console's own pace: 30 frames a second, vsync on, the frame at the screen's height
Settings DefaultSettings() {
    Settings s;
    s.render_percent = 100;
    s.fps_cap = 30;
    s.vsync = true;
    return s;
}

Settings& Get() {
    static Settings s = DefaultSettings();
    return s;
}

void Load() {
    Settings& s = Get();
    toml::table t;
    try {
        t = toml::parse_file(File().string());
    } catch (...) {
        return;
    }
    std::string mode = t["display_mode"].value_or(std::string(kModes[uint32_t(s.display_mode)]));
    for (uint32_t i = 0; i < 3; i++)
        if (mode == kModes[i]) s.display_mode = DisplayMode(i);
    s.width = t["width"].value_or(s.width);
    s.height = t["height"].value_or(s.height);
    s.render_percent = t["render_percent"].value_or(s.render_percent);
    s.fps_cap = t["fps_cap"].value_or(s.fps_cap);
    s.vsync = t["vsync"].value_or(s.vsync);
    s.aniso = t["anisotropic_filtering"].value_or(s.aniso);
    s.join_ip = t["join_ip"].value_or(s.join_ip);
    for (const Option& o : kGame.options) {
        if (o.flag) *o.flag = t[o.key].value_or(*o.flag);
        else *o.value = t[o.key].value_or(*o.value);
    }
    if (kGame.load) kGame.load(&t);
}

void Save() {
    std::ofstream f(File());
    f << Table() << "\n";
}

void LogSettings() {
    std::ostringstream text;
    text << Table();
    std::istringstream lines(text.str());
    LOGR("Settings:");
    for (std::string line; std::getline(lines, line);)
        if (!line.empty()) LOGR("  %s", line.c_str());
}

void LogSystem() {
    std::time_t now = std::time(nullptr);
    char date[32] = {};
    std::strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    LOGR("%ls (%s), %s, build %s, Switch", kGame.title, IMAGE_XBE, date, BLACK_COMMIT);
    LOGR("Path: %s", ExeDir().c_str());
    u64 total = 0, used = 0;
    svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    LOGR("Applet: %s, memory %llu MB (%llu MB used)",
         appletGetAppletType() == AppletType_Application ? "application (full memory)" : "applet",
         (unsigned long long)(total >> 20), (unsigned long long)(used >> 20));
    Mode desk = Desktop();
    LOGR("Display: %ux%u (%s)", desk.width, desk.height, g_op_mode == AppletOperationMode_Console ? "docked" : "handheld");
    LogSettings();
}

void ApplyDisplay() {}

Mode Desktop() { return ScreenMode(); }

Mode Output() { return Desktop(); }

bool Widescreen() {
    Mode m = Output();
    return m.width * 2 >= m.height * 3;  // 3:2 or wider
}

float RenderScale(uint32_t guest_height) {
    float h = Output().height * (Get().render_percent / 100.0f);
    return guest_height ? std::max(h / guest_height, 0.1f) : 1.0f;
}

std::filesystem::path ExeDir() { return std::filesystem::path(kDir); }

int Instance() { return 0; }

void* Window() { return nwindowGetDefault(); }

// no splash and no relaunch into another executable on the Switch
bool OpenSplash() { return false; }
void SplashFirstFrame() {}
void CloseSplash() {}
void OpenGameWindow(bool) {}
bool StartHandover() { return false; }
bool HandingOver() { return false; }
void FinishHandover() {}

std::recursive_mutex& UiLock() {
    static std::recursive_mutex m;
    return m;
}

void Pace() {
    using clock = std::chrono::steady_clock;
    static clock::time_point next = clock::now();
    uint32_t cap = Get().fps_cap;
    auto now = clock::now();
    if (!cap) {
        next = now;
        return;
    }
    auto period = std::chrono::nanoseconds(1000000000ull / cap);
    next += period;
    if (next < now - period) next = now;
    while (clock::now() + std::chrono::milliseconds(2) < next) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    while (clock::now() < next) std::this_thread::yield();
}

}  // namespace app

namespace {

void InitUi() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.BackendPlatformName = "switch_libnx";
}

}  // namespace

// libnx calls this for any CPU exception: the report goes to the log, then the process ends, so the log's last lines
// say where. pc - main + main's ELF address (aarch64-none-elf-nm black.elf) is the ELF address.
int main(int, char**);

extern "C" {
alignas(16) u8 __nx_exception_stack[0x4000];
u64 __nx_exception_stack_size = sizeof(__nx_exception_stack);

void __libnx_exception_handler(ThreadExceptionDump* ctx) {
    uintptr_t base = reinterpret_cast<uintptr_t>(kernel::g_base);
    uint64_t far = ctx->far.x;
    LOGE("crash: exception %u at pc %016llX lr %016llX sp %016llX, address %016llX", ctx->error_desc,
         (unsigned long long)ctx->pc.x, (unsigned long long)ctx->lr.x, (unsigned long long)ctx->sp.x,
         (unsigned long long)far);
    LOGE("crash: main at %p; guest base %p%s", reinterpret_cast<void*>(&main), kernel::g_base,
         base && far >= base && far < base + (1ull << 32) ? "" : " (address outside guest memory)");
    if (base && far >= base && far < base + (1ull << 32)) LOGE("crash: guest address %08X", uint32_t(far - base));
    for (int i = 0; i < 29; i += 4)
        LOGE("crash: x%-2d %016llX %016llX %016llX %016llX", i, (unsigned long long)ctx->cpu_gprs[i].x,
             (unsigned long long)ctx->cpu_gprs[i + 1].x, (unsigned long long)ctx->cpu_gprs[i + 2].x,
             (unsigned long long)ctx->cpu_gprs[i + 3].x);
    // on a guest thread: its registers and the return addresses on its stack place the guest code
    if (kernel::Thread* t = kernel::Current(); t && t->ctx) {
        LOGE("crash: on guest thread %X", t->id);
        kernel::LogGuestStack(*t->ctx, 256);
    }
    logx::Flush();
    svcExitProcess();
}
}

// the driver (Mesa) ends the process with abort() or a failed assert, not a CPU exception: say where first
extern "C" [[noreturn]] void abort() {
    LOGE("crash: abort() called from %p (main at %p)", __builtin_return_address(0), reinterpret_cast<void*>(&main));
    logx::Flush();
    std::fflush(stderr);
    svcExitProcess();
    __builtin_unreachable();
}

extern "C" [[noreturn]] void __assert_func(const char* file, int line, const char* func, const char* expr) {
    LOGE("crash: assert %s failed in %s, %s:%d", expr, func ? func : "?", file, line);
    abort();
}

namespace {

// a message to the player before the game runs: the console's error applet, then the title ends
void Fatal(const char* text) {
    LOGE("%s", text);
    logx::Flush();
    ErrorApplicationConfig c;
    errorApplicationCreate(&c, text, nullptr);
    errorApplicationShow(&c);
}

}  // namespace

int main(int, char**) {
    // an uncaught C++ exception or std::abort ends the process without a CPU exception: say so in the log first
    std::set_terminate([] {
        try {
            if (auto e = std::current_exception()) std::rethrow_exception(e);
            LOGE("crash: std::terminate (abort, no exception)");
        } catch (const std::system_error& e) {
            LOGE("crash: uncaught std::system_error %d: %s", e.code().value(), e.what());
        } catch (const std::exception& e) {
            LOGE("crash: uncaught exception: %s", e.what());
        } catch (...) {
            LOGE("crash: uncaught exception of an unknown type");
        }
        logx::Flush();
        svcExitProcess();
    });
    appletLockExit();
    g_op_mode = appletGetOperationMode();
    std::filesystem::create_directories(std::filesystem::path(kDir) / "logs");
    // what the driver prints goes to stderr: a file beside the logs, written as it comes
    std::freopen((std::string(kDir) + "/logs/stderr.log").c_str(), "w", stderr);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    logx::Init((app::ExeDir() / "logs").string(), app::kGame.log);
    app::Load();
    if (app::kGame.init) app::kGame.init();
    app::LogSystem();

    app::GameFiles game;
    if (!app::FindGame(game)) {
        Fatal("Black: the game files were not found or are another release. Put the files of your disc in "
              "sdmc:/switch/black/game (default.xbe with its folders).");
        appletUnlockExit();
        return 1;
    }

    LOGI("app: ImGui");
    InitUi();
    LOGI("app: controllers");
    input::Init();
    LOGI("app: game window hook");
    if (app::kGame.window) app::kGame.window(nwindowGetDefault());
    LOGI("app: booting the image");
    if (!kernel::Boot(std::move(game.xbe), IMAGE_XBE, game.dir.string(), (app::ExeDir() / "hdd").string())) {
        Fatal("Black: the game failed to start, see the log in sdmc:/switch/black/logs.");
        appletUnlockExit();
        return 1;
    }

    // the applet's messages (HOME, docking, exit) on this thread; the game runs on its own
    auto alive = std::chrono::steady_clock::now();
    while (appletMainLoop()) {
        // a line every 5 seconds: a log that stops with these still coming is a hang, not an exit
        if (std::chrono::steady_clock::now() - alive > std::chrono::seconds(5)) {
            alive = std::chrono::steady_clock::now();
            LOGI("app: alive");
        }
        AppletOperationMode m = appletGetOperationMode();
        if (m != g_op_mode) {
            g_op_mode = m;
            g_surface_epoch++;
            LOGI("app: %s", m == AppletOperationMode_Console ? "docked" : "handheld");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    logx::Flush();
    kernel::Shutdown(0);
    appletUnlockExit();
    return 0;
}
