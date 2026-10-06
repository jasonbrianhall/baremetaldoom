// Sound-effect mixer. See mixer.hpp. Built with -mgeneral-regs-only.
#include "mixer.hpp"
#include "audio.hpp"

struct Channel {
    const uint8_t* data;
    uint32_t len;            // samples
    uint32_t pos;            // 16.16 fixed point
    uint32_t step;           // 16.16 per output frame
    int vol;                 // 0..127
    volatile bool on;
};
static Channel ch_[MIXER_CHANNELS];
static volatile bool enabled;

// The main loop changes channels; the interrupt reads them.
static inline uintptr_t irq_off() {
    uintptr_t f;
    __asm__ volatile("pushf; pop %0; cli" : "=r"(f) :: "memory");
    return f;
}
static inline void irq_restore(uintptr_t f) { __asm__ volatile("push %0; popf" :: "r"(f) : "memory", "cc"); }

extern "C" {
void mixer_init(int on) { enabled = on != 0; }
int mixer_enabled(void) { return enabled; }

void mixer_start(int c, const uint8_t* samples, uint32_t len, uint32_t rate, int vol, int sep) {
    (void)sep;
    if (c < 0 || c >= MIXER_CHANNELS || !samples || !len) return;
    uintptr_t f = irq_off();
    Channel& k = ch_[c];
    k.data = samples;
    k.len = len;
    k.pos = 0;
    k.step = (uint32_t)(((uint64_t)rate << 16) / (uint32_t)audio_rate());
    k.vol = vol < 0 ? 0 : vol > 127 ? 127 : vol;
    k.on = true;
    irq_restore(f);
}
void mixer_set(int c, int vol, int sep) {
    (void)sep;
    if (c < 0 || c >= MIXER_CHANNELS) return;
    ch_[c].vol = vol < 0 ? 0 : vol > 127 ? 127 : vol;
}
void mixer_stop(int c) { if (c >= 0 && c < MIXER_CHANNELS) ch_[c].on = false; }
int mixer_playing(int c) { return c >= 0 && c < MIXER_CHANNELS && ch_[c].on; }

void mixer_tick(void) {
    if (!enabled) return;
    int n = audio_frames_wanted(audio_rate() / 240);
    static int16_t buf[1024];
    if (n > 1024) n = 1024;
    if (n <= 0) return;
    static int32_t acc[1024];
    for (int i = 0; i < n; i++) acc[i] = 0;
    for (int c = 0; c < MIXER_CHANNELS; c++) {
        Channel& k = ch_[c];
        if (!k.on) continue;
        for (int i = 0; i < n; i++) {
            uint32_t s = k.pos >> 16;
            if (s >= k.len) { k.on = false; break; }
            acc[i] += ((int)k.data[s] - 128) * k.vol * 2;
            k.pos += k.step;
        }
    }
    for (int i = 0; i < n; i++) {
        int32_t v = acc[i];
        buf[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
    audio_submit(buf, n);
}
}
