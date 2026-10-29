// The Android host: SDL's window and event loop, the settings and folders, the app's lifecycle; the game is described
// by app::kGame (src/common/game_info.cpp)
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#ifdef __APPLE__
#include <SDL3/SDL_vulkan.h>
#endif
#ifdef __ANDROID__
#include <android/log.h>
#include <sys/system_properties.h>
#endif
#ifdef __APPLE__
#include <sys/sysctl.h>
#endif
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cwchar>
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

SDL_Window* g_window = nullptr;
std::atomic<bool> g_foreground{true};
std::atomic<uint32_t> g_surface_epoch{0};
std::mutex g_foreground_lock;
std::condition_variable g_foreground_cv;

std::string Utf8(const wchar_t* w) {
    std::string s;
    for (; w && *w; w++) s += *w < 0x80 ? char(*w) : '?';
    return s;
}

// the log's lines to logcat as well: stderr is read on a thread of its own
#ifdef __ANDROID__
void LogToLogcat() {
    int fds[2];
    if (pipe(fds)) return;
    dup2(fds[1], STDERR_FILENO);
    close(fds[1]);
    std::thread([fd = fds[0]] {
        char buf[1024];
        std::string line;
        for (ssize_t n; (n = read(fd, buf, sizeof(buf))) > 0;) {
            line.append(buf, size_t(n));
            for (size_t nl; (nl = line.find('\n')) != std::string::npos; line.erase(0, nl + 1))
                __android_log_write(ANDROID_LOG_INFO, "BLACK", line.substr(0, nl).c_str());
        }
    }).detach();
}
#else
// the log goes to the file and to stderr, which Xcode shows
void LogToLogcat() {}
#endif

std::string Property(const char* name) {
#ifdef __ANDROID__
    char v[PROP_VALUE_MAX] = {};
    __system_property_get(name, v);
    return v;
#elif defined(__APPLE__)
    (void)name;
    char v[256] = {};
    size_t n = sizeof v;
    return sysctlbyname("hw.machine", v, &n, nullptr, 0) == 0 ? std::string(v) : std::string();
#else
    (void)name;
    return {};
#endif
}

void SetForeground(bool on) {
    {
        std::lock_guard lock(g_foreground_lock);
        if (on && !g_foreground) g_surface_epoch++;
        g_foreground = on;
    }
    g_foreground_cv.notify_all();
    LOGI("app: %s", on ? "in front" : "in the background");
}

// SDL's display mode in landscape: the window is not up yet when the settings load
app::Mode ScreenMode() {
    if (g_window) {
        int w = 0, h = 0;
        host::WindowPixels(w, h);
        if (w > 0 && h > 0) return {uint32_t(w), uint32_t(h)};
    }
    const SDL_DisplayMode* m = SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay());
    if (!m) return {1920, 1080};
    uint32_t w = uint32_t(m->w * m->pixel_density), h = uint32_t(m->h * m->pixel_density);
    return {std::max(w, h), std::min(w, h)};
}

float RefreshRate() {
    SDL_DisplayID d = g_window ? SDL_GetDisplayForWindow(g_window) : SDL_GetPrimaryDisplay();
    const SDL_DisplayMode* m = SDL_GetCurrentDisplayMode(d);
    return m && m->refresh_rate > 1 ? m->refresh_rate : 60.0f;
}

}  // namespace

namespace host {

SDL_Window* Window() { return g_window; }
bool Foreground() { return g_foreground; }
uint32_t SurfaceEpoch() { return g_surface_epoch; }

void WaitForeground() {
    std::unique_lock lock(g_foreground_lock);
    g_foreground_cv.wait(lock, [] { return g_foreground.load(); });
}

void WindowPixels(int& w, int& h) {
    w = h = 0;
    if (g_window) SDL_GetWindowSizeInPixels(g_window, &w, &h);
}

}  // namespace host

namespace app {

namespace {

const char* kModes[] = {"fullscreen", "borderless", "windowed"};
// the defaults a settings file was written under: 2 has the frame rate unlimited and vsync off, which a file from
// before takes once, its player's own choices kept from then on
constexpr int kDefaults = 2;

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
                  {"join_ip", s.join_ip},
                  {"defaults", int64_t(kDefaults)}};
    for (const Option& o : kGame.options) {
        if (o.flag) t.insert(o.key, *o.flag);
        else t.insert(o.key, int64_t(*o.value));
    }
    if (kGame.save) kGame.save(&t);
    return t;
}

// a phone renders the console's frame at about 720p unless the settings say otherwise
constexpr uint32_t kFirstRunRenderPercent = 67;

void PhoneFrameDefaults(Settings& s) {
    s.fps_cap = 0;
    s.vsync = false;
}

}  // namespace

// the settings as a phone starts with them, for a first run and the options' restore
Settings DefaultSettings() {
    Settings s;
    s.render_percent = kFirstRunRenderPercent;
    PhoneFrameDefaults(s);
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
    if (t["defaults"].value_or(int64_t(0)) < kDefaults) PhoneFrameDefaults(s);
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
    LOGR("%ls (%s), %s, build %s", kGame.title, IMAGE_XBE, date, BLACK_COMMIT);
    LOGR("Path: %s", ExeDir().c_str());
#ifdef __ANDROID__
    LOGR("Device: %s %s, Android %s (API %d)", Property("ro.product.manufacturer").c_str(),
         Property("ro.product.model").c_str(), Property("ro.build.version.release").c_str(), SDL_GetAndroidSDKVersion());
#else
    LOGR("Device: %s, %s", Property("hw.machine").c_str(), SDL_GetPlatform());
#endif
    LOGR("CPU: %s (%d threads)", Property("ro.soc.model").c_str(), SDL_GetNumLogicalCPUCores());
    LOGR("RAM: %.1f GB", SDL_GetSystemRAM() / 1024.0);
    Mode desk = Desktop();
    LOGR("Display: %ux%u, %.0f Hz", desk.width, desk.height, RefreshRate());
    LogSettings();
}

void ApplyDisplay() {}

Mode Desktop() { return ScreenMode(); }

Mode Output() {
    // the window is the screen: the resolution setting is the screen's own
    return Desktop();
}

bool Widescreen() {
    Mode m = Output();
    return m.width * 2 >= m.height * 3;  // 3:2 or wider
}

float RenderScale(uint32_t guest_height) {
    float h = Output().height * (Get().render_percent / 100.0f);
    return guest_height ? std::max(h / guest_height, 0.1f) : 1.0f;
}

// app-specific external storage: the player can reach the logs, saves and settings over USB
std::filesystem::path ExeDir() {
    static const std::filesystem::path dir = [] {
#ifdef __ANDROID__
        const char* p = nullptr;
        if (SDL_GetAndroidExternalStorageState() & SDL_ANDROID_EXTERNAL_STORAGE_WRITE) p = SDL_GetAndroidExternalStoragePath();
        if (!p) p = SDL_GetAndroidInternalStoragePath();
        return std::filesystem::path(p ? p : ".");
#else
        // iOS: the app's Documents folder, which the Files app and Finder can see. SDL's pref path is Library/Application
        // Support, out of the player's reach, so the settings, logs and game live under Documents/black instead
        std::filesystem::path d = ".";
        if (const char* p = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS)) d = p;  // SDL keeps it
        d /= "black";
        std::error_code ec;
        std::filesystem::create_directories(d, ec);
        return d;
#endif
    }();
    return dir;
}

int Instance() { return 0; }

void* Window() { return g_window; }

// no splash and no relaunch into another executable on Android
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

namespace {
std::atomic<uint32_t> g_hold_fps{0};
std::atomic<int64_t> g_hold_until{0};  // steady clock ticks
}  // namespace

void HoldFrameRate(uint32_t fps) {
    g_hold_fps = fps;
    g_hold_until = (std::chrono::steady_clock::now() + std::chrono::milliseconds(250)).time_since_epoch().count();
}

void CpuBoost(bool) {}

void Pace() {
    using clock = std::chrono::steady_clock;
    static clock::time_point next = clock::now();
    uint32_t cap = Get().fps_cap;
    auto now = clock::now();
    if (uint32_t hold = g_hold_fps; hold && now.time_since_epoch().count() < g_hold_until) cap = hold;
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
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_IsTouchScreen;
    io.BackendPlatformName = "android_sdl3";
}

void Dispatch(const SDL_Event& e) {
    switch (e.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_TERMINATING:
            logx::Flush();
            kernel::Shutdown(0);
            break;
        case SDL_EVENT_WILL_ENTER_BACKGROUND:
            SetForeground(false);
            break;
        case SDL_EVENT_DID_ENTER_FOREGROUND:
            SetForeground(true);
            break;
        case SDL_EVENT_LOW_MEMORY:
            LOGW("app: Android is low on memory");
            break;
        case SDL_EVENT_SENSOR_UPDATE:
        case SDL_EVENT_GAMEPAD_SENSOR_UPDATE:
            gyro::OnEvent(e);
            return;
        case SDL_EVENT_GAMEPAD_ADDED:
        case SDL_EVENT_GAMEPAD_REMOVED:
            input::OnPadEvent(e);
            break;
        default:
            break;
    }
    if (touch::OnEvent(e)) return;
    std::lock_guard lock(app::UiLock());
    ui::OnEvent(e);
}

}  // namespace

int main(int, char**) {
    LogToLogcat();
#ifdef __APPLE__
    // a plain file before SDL: if the window or Vulkan cannot start, this is the only trace. The logger itself is
    // opened once, after SDL, so two handles never share the same log
    if (const char* home = std::getenv("HOME")) {
        std::filesystem::path dir = std::filesystem::path(home) / "Documents" / "black" / "logs";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (FILE* f = std::fopen((dir / "boot.txt").string().c_str(), "w")) {
            std::fprintf(f, "start %s\n", BLACK_COMMIT);
            std::fclose(f);
        }
    }
#endif
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
#ifdef __ANDROID__
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
#endif
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO | SDL_INIT_SENSOR)) {
        LOGE("app: SDL_Init failed: %s", SDL_GetError());
        SDL_Log("SDL_Init: %s", SDL_GetError());
        logx::Flush();
        return 1;
    }
    logx::Init((app::ExeDir() / "logs").string(), app::kGame.log);
    app::Load();
    if (app::kGame.init) app::kGame.init();
    app::LogSystem();
    LOGI("app: SDL ready");

#ifdef __APPLE__
    // MoltenVK is linked in: SDL finds its entry points in the executable and makes the Vulkan surface with them
    if (!SDL_Vulkan_LoadLibrary(nullptr)) LOGE("app: no Vulkan: %s", SDL_GetError());
#endif

    std::string title = Utf8(app::kGame.title);
    LOGI("app: opening the window");
    g_window = SDL_CreateWindow(title.c_str(), 0, 0, SDL_WINDOW_FULLSCREEN | SDL_WINDOW_VULKAN | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!g_window) {
        LOGE("app: no window: %s", SDL_GetError());
        logx::Flush();
        return 1;
    }
    LOGI("app: window up");

    app::GameFiles game;
    if (!app::FindGame(game)) {
        logx::Flush();
#ifdef __APPLE__
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Black",
                                 "The game files were not found. Put them in the app's Documents/black/game folder "
                                 "(Files app or Finder: default.xbe plus chars, data, export, language, levels, sound "
                                 "and videos), then open the app again.",
                                 g_window);
#endif
        return 1;
    }

    InitUi();
    input::Init();
    if (app::kGame.window) app::kGame.window(g_window);
    LOGI("app: booting the image");
    if (!kernel::Boot(std::move(game.xbe), IMAGE_XBE, game.dir.string(), (app::ExeDir() / "hdd").string())) {
        LOGE("app: boot failed");
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, Utf8(app::kGame.short_title).c_str(),
                                 "The game failed to start, see the log in the logs folder.", g_window);
        logx::Flush();
        return 1;
    }
    LOGI("app: booted");

    for (;;) {
        SDL_Event e;
        if (SDL_WaitEventTimeout(&e, 100)) {
            Dispatch(e);
            while (SDL_PollEvent(&e)) Dispatch(e);
        }
        touch::Pump();
    }
}
