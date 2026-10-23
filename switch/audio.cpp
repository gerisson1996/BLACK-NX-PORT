// mixer output through libnx's audio renderer (audren): a thread renders 10 ms blocks of the game's 5.1 mix, folds
// them to stereo as the Android host does, and queues them as 16 bit wave buffers on one voice
#include <malloc.h>
#include <switch.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <thread>
#include <vector>

#include "audio/output.h"
#include "core/log.h"

namespace audio {

namespace {

constexpr float kCentreFold = 0.707f;
constexpr uint32_t kBlock = kRate / 100;  // frames a wave buffer holds: 10 ms
constexpr int kBuffers = 6;               // 60 ms queued at most

std::atomic<uint32_t> g_mode{kOutputAuto};
std::atomic<bool> g_paused{false};

AudioDriver g_drv;
AudioDriverWaveBuf g_wave[kBuffers];
int16_t* g_pool = nullptr;  // kBuffers stereo blocks, the renderer's memory pool

// L = FL + kCentreFold C + BL and R = FR + kCentreFold C + BR without the sub; mono hears the average of the two
void Fold(const float* in, int16_t* out, bool mono) {
    for (uint32_t i = 0; i < kBlock; i++) {
        const float* f = in + size_t(i) * kChannels;
        float c = f[Centre] * kCentreFold;
        float l = f[FrontLeft] + c + f[BackLeft];
        float r = f[FrontRight] + c + f[BackRight];
        if (mono) l = r = (l + r) * 0.5f;
        out[i * 2] = int16_t(std::clamp(l, -1.0f, 1.0f) * 32767.0f);
        out[i * 2 + 1] = int16_t(std::clamp(r, -1.0f, 1.0f) * 32767.0f);
    }
}

void Run(std::function<void(float*, uint32_t)> render) {
    std::vector<float> mix(size_t(kBlock) * kChannels);
    for (;;) {
        for (int i = 0; i < kBuffers; i++) {
            AudioDriverWaveBuf& w = g_wave[i];
            if (w.state != AudioDriverWaveBufState_Free && w.state != AudioDriverWaveBufState_Done) continue;
            int16_t* pcm = g_pool + size_t(i) * kBlock * 2;
            if (g_paused) {
                std::memset(pcm, 0, size_t(kBlock) * 2 * sizeof(int16_t));
            } else {
                render(mix.data(), kBlock);
                Fold(mix.data(), pcm, g_mode == kOutputMono);
            }
            armDCacheFlush(pcm, size_t(kBlock) * 2 * sizeof(int16_t));
            audrvVoiceAddWaveBuf(&g_drv, 0, &w);
        }
        if (!audrvVoiceIsPlaying(&g_drv, 0)) audrvVoiceStart(&g_drv, 0);
        audrvUpdate(&g_drv);
        audrenWaitFrame();
    }
}

}  // namespace

// stereo on the Switch's speakers and TV: a 5.1 mix only when the player asks for it
bool IsSurround() { return g_mode == kOutputSurround; }

bool IsMono() { return g_mode == kOutputMono; }

void SetOutputMode(uint32_t mode) {
    if (g_mode.exchange(mode) != mode) LOGI("audio: output %u", mode);
}

void SetPaused(bool paused) { g_paused = paused; }

void StartOutput(std::function<void(float*, uint32_t)> render) {
    static const AudioRendererConfig cfg = {
        .output_rate = AudioRendererOutputRate_48kHz,
        .num_voices = 4,
        .num_effects = 0,
        .num_sinks = 1,
        .num_mix_objs = 1,
        .num_mix_buffers = 2,
    };
    Result rc = audrenInitialize(&cfg);
    if (R_SUCCEEDED(rc)) rc = audrvCreate(&g_drv, &cfg, 2);
    if (R_FAILED(rc)) {
        LOGE("audio: audio renderer unavailable (0x%X), no sound", rc);
        return;
    }
    size_t pool = (size_t(kBlock) * 2 * sizeof(int16_t) * kBuffers + AUDREN_MEMPOOL_ALIGNMENT - 1) &
                  ~size_t(AUDREN_MEMPOOL_ALIGNMENT - 1);
    g_pool = static_cast<int16_t*>(memalign(AUDREN_MEMPOOL_ALIGNMENT, pool));
    std::memset(g_pool, 0, pool);
    armDCacheFlush(g_pool, pool);
    int id = audrvMemPoolAdd(&g_drv, g_pool, pool);
    audrvMemPoolAttach(&g_drv, id);
    static const u8 kSink[2] = {0, 1};
    audrvDeviceSinkAdd(&g_drv, AUDREN_DEFAULT_DEVICE_NAME, 2, kSink);
    audrvUpdate(&g_drv);
    audrenStartAudioRenderer();
    audrvVoiceInit(&g_drv, 0, 2, PcmFormat_Int16, kRate);
    audrvVoiceSetDestinationMix(&g_drv, 0, AUDREN_FINAL_MIX_ID);
    audrvVoiceSetMixFactor(&g_drv, 0, 1.0f, 0, 0);
    audrvVoiceSetMixFactor(&g_drv, 0, 0.0f, 0, 1);
    audrvVoiceSetMixFactor(&g_drv, 0, 0.0f, 1, 0);
    audrvVoiceSetMixFactor(&g_drv, 0, 1.0f, 1, 1);
    for (int i = 0; i < kBuffers; i++) {
        g_wave[i] = {};
        g_wave[i].data_raw = g_pool;
        g_wave[i].size = pool;
        g_wave[i].start_sample_offset = int32_t(size_t(i) * kBlock);
        g_wave[i].end_sample_offset = int32_t(size_t(i + 1) * kBlock);
    }
    std::thread(Run, std::move(render)).detach();
    LOGI("audio: %s output through the audio renderer", IsSurround() ? "5.1 folded to stereo" : "stereo");
}

}  // namespace audio
