// Linux (SDL2) platform layer for Doom.
//
// Stands in for ../baremetal/kernel.cpp and audio.cpp as ../msdos's
// dos_platform.cpp does: the same interface (bm.h, bm_audio.h, the parts of
// audio.hpp the mixer uses), so the port glue (doomgeneric_baremetal.c,
// i_sound_baremetal.c), the mixer and the OPL3 General MIDI synth from
// ../baremetal run unchanged, and the game plays the same as on DOS and
// bare metal. This file supplies:
//
//   video     an SDL window (or fullscreen), the frame scaled to fit at
//             Doom's 4:3 shape (its pixels 1.2 times taller than wide)
//   keyboard  SDL scancodes, mapped as the DOS and bare-metal builds map
//             the PC's: the keys are where they are on a US keyboard
//   mouse     relative motion while the window has it (free look)
//   sound     SDL audio, 48 kHz mono, queued from the main loop as the DOS
//             build tops up its Sound Blaster ring: no audio thread
//   network   the original drivers' protocols, built in (net_ipx.cpp,
//             net_serial.cpp): IPX as IPXSETUP.EXE, serial as SERSETUP.EXE
#include <SDL.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string>

extern "C" {
#include "doomkeys.h"
void I_Quit(void);
}
#include "bm.h"
#include "bm_audio.h"
#include "audio.hpp"
#include "mixer.hpp"
#include "music.h"
#include "net_linux.h"

static bool has_arg(int argc, char** argv, const char* a) {
    for (int i = 1; i < argc; i++) if (!strcasecmp(argv[i], a)) return true;
    return false;
}

// ---------------------------------------------------------------- time
static uint64_t t_start, t_freq;
static uint32_t now_ms() { return (uint32_t)((SDL_GetPerformanceCounter() - t_start) * 1000 / t_freq); }

// ---------------------------------------------------------------- video
static SDL_Window* win;
static SDL_Renderer* ren;
static SDL_Texture* tex;
static int tex_w, tex_h;
static uint32_t pal[256];
static bool fullscreen, vsync, have_mouse;
static int start_w, start_h;                // -width / -height
static char title[128] = "Doom";

extern "C" int SCREENWIDTH, SCREENHEIGHT;   // i_video.c: the frame's size

static int mouse_buttons;
static bool mouse_news;

// The game has the mouse (hidden, relative motion) or lets go of it (a
// pointer, for the rest of the desktop): Ctrl+F10 as in DOSBox, or leaving
// the window. A click in the window takes it back.
static void grab_mouse(bool on) {
    if (!win || on == have_mouse) return;
    have_mouse = on;
    SDL_SetRelativeMouseMode(on ? SDL_TRUE : SDL_FALSE);
    if (!on && mouse_buttons) { mouse_buttons = 0; mouse_news = true; }   // no button left held down
    char t[192];
    snprintf(t, sizeof t, on ? "%s" : "%s - mouse free: click or Ctrl+F10 to play", title);
    SDL_SetWindowTitle(win, t);
}

static void graphics_start(int w, int h) {
    if (win) return;
    // The frame as shown: w x 1.2h. The window: the biggest whole multiple
    // of 320x240 that fits comfortably, unless told.
    int sw = w, sh = h * 6 / 5;
    int ww = start_w, wh = start_h;
    if (!ww || !wh) {
        SDL_DisplayMode dm;
        int k = 3;
        if (SDL_GetDesktopDisplayMode(0, &dm) == 0)
            while (k > 1 && (sw * k > dm.w * 9 / 10 || sh * k > dm.h * 9 / 10)) k--;
        if (sw >= 640) k = k > 1 ? k / 2 : 1;       // -hires: already big
        ww = sw * k; wh = sh * k;
    }
    Uint32 flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    if (fullscreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, ww, wh, flags);
    if (!win) { fprintf(stderr, "Video: can't open a window: %s\n", SDL_GetError()); exit(1); }
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | (vsync ? SDL_RENDERER_PRESENTVSYNC : 0));
    if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
    if (!ren) { fprintf(stderr, "Video: no renderer: %s\n", SDL_GetError()); exit(1); }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    SDL_RendererInfo ri;
    if (SDL_GetRendererInfo(ren, &ri) == 0) printf("Video: SDL %s renderer, %dx%d frame\n", ri.name, w, h);
    have_mouse = false;
    grab_mouse(true);
}

static void texture_for(int w, int h) {
    if (tex && w == tex_w && h == tex_h) return;
    if (tex) SDL_DestroyTexture(tex);
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
    tex_w = w; tex_h = h;
    // Doom's 4:3 shape (w : 1.2h), letterboxed in whatever the window is.
    SDL_RenderSetLogicalSize(ren, w, h * 6 / 5);
}

extern "C" void bm_set_palette(const uint32_t* rgb) {
    for (int i = 0; i < 256; i++) pal[i] = 0xFF000000u | (rgb[i] & 0xFFFFFF);
}

extern "C" void bm_draw(const uint8_t* pixels, int w, int h) {
    graphics_start(w, h);
    texture_for(w, h);
    void* dst;
    int pitch;
    if (SDL_LockTexture(tex, nullptr, &dst, &pitch) == 0) {
        for (int y = 0; y < h; y++) {
            uint32_t* d = (uint32_t*)((uint8_t*)dst + y * pitch);
            const uint8_t* s = pixels + y * w;
            for (int x = 0; x < w; x++) d[x] = pal[s[x]];
        }
        SDL_UnlockTexture(tex);
    }
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, nullptr, nullptr);
    SDL_RenderPresent(ren);
}

// ---------------------------------------------------------------- keyboard
// SDL scancodes are USB HID usages: turned into the PC's set-1 codes, then
// into Doom's keys exactly as the DOS build (dos_platform.cpp) does, so the
// key_* numbers in default.cfg mean the same keys everywhere.
static int set1(SDL_Scancode s, bool* ext) {
    *ext = false;
    if (s >= SDL_SCANCODE_A && s <= SDL_SCANCODE_Z) {
        static const uint8_t az[26] = {0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32,
                                       0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C};
        return az[s - SDL_SCANCODE_A];
    }
    if (s >= SDL_SCANCODE_1 && s <= SDL_SCANCODE_0) return 0x02 + (s - SDL_SCANCODE_1);
    if (s >= SDL_SCANCODE_F1 && s <= SDL_SCANCODE_F10) return 0x3B + (s - SDL_SCANCODE_F1);
    switch (s) {
    case SDL_SCANCODE_RETURN: return 0x1C;
    case SDL_SCANCODE_ESCAPE: return 0x01;
    case SDL_SCANCODE_BACKSPACE: return 0x0E;
    case SDL_SCANCODE_TAB: return 0x0F;
    case SDL_SCANCODE_SPACE: return 0x39;
    case SDL_SCANCODE_MINUS: return 0x0C;
    case SDL_SCANCODE_EQUALS: return 0x0D;
    case SDL_SCANCODE_LEFTBRACKET: return 0x1A;
    case SDL_SCANCODE_RIGHTBRACKET: return 0x1B;
    case SDL_SCANCODE_BACKSLASH: return 0x2B;
    case SDL_SCANCODE_NONUSBACKSLASH: return 0x2B;
    case SDL_SCANCODE_SEMICOLON: return 0x27;
    case SDL_SCANCODE_APOSTROPHE: return 0x28;
    case SDL_SCANCODE_GRAVE: return 0x29;
    case SDL_SCANCODE_COMMA: return 0x33;
    case SDL_SCANCODE_PERIOD: return 0x34;
    case SDL_SCANCODE_SLASH: return 0x35;
    case SDL_SCANCODE_CAPSLOCK: return 0x3A;
    case SDL_SCANCODE_F11: return 0x57;
    case SDL_SCANCODE_F12: return 0x58;
    case SDL_SCANCODE_SCROLLLOCK: return 0x46;
    case SDL_SCANCODE_NUMLOCKCLEAR: return 0x45;
    case SDL_SCANCODE_KP_MULTIPLY: return 0x37;
    case SDL_SCANCODE_KP_MINUS: return 0x4A;
    case SDL_SCANCODE_KP_PLUS: return 0x4E;
    case SDL_SCANCODE_KP_7: return 0x47;
    case SDL_SCANCODE_KP_8: return 0x48;
    case SDL_SCANCODE_KP_9: return 0x49;
    case SDL_SCANCODE_KP_4: return 0x4B;
    case SDL_SCANCODE_KP_5: return 0x4C;
    case SDL_SCANCODE_KP_6: return 0x4D;
    case SDL_SCANCODE_KP_1: return 0x4F;
    case SDL_SCANCODE_KP_2: return 0x50;
    case SDL_SCANCODE_KP_3: return 0x51;
    case SDL_SCANCODE_KP_0: return 0x52;
    case SDL_SCANCODE_KP_PERIOD: return 0x53;
    case SDL_SCANCODE_LCTRL: return 0x1D;
    case SDL_SCANCODE_LSHIFT: return 0x2A;
    case SDL_SCANCODE_RSHIFT: return 0x36;
    case SDL_SCANCODE_LALT: return 0x38;
    default: break;
    }
    *ext = true;
    switch (s) {
    case SDL_SCANCODE_KP_ENTER: return 0x1C;
    case SDL_SCANCODE_RCTRL: return 0x1D;
    case SDL_SCANCODE_KP_DIVIDE: return 0x35;
    case SDL_SCANCODE_RALT: return 0x38;
    case SDL_SCANCODE_HOME: return 0x47;
    case SDL_SCANCODE_UP: return 0x48;
    case SDL_SCANCODE_PAGEUP: return 0x49;
    case SDL_SCANCODE_LEFT: return 0x4B;
    case SDL_SCANCODE_RIGHT: return 0x4D;
    case SDL_SCANCODE_END: return 0x4F;
    case SDL_SCANCODE_DOWN: return 0x50;
    case SDL_SCANCODE_PAGEDOWN: return 0x51;
    case SDL_SCANCODE_INSERT: return 0x52;
    case SDL_SCANCODE_DELETE: return 0x53;
    default: break;
    }
    return 0;
}

// Set-1 scancodes to doomkeys.h, as dos_platform.cpp and kernel.cpp.
static int doom_key(bool ext, uint8_t code) {
    if (ext) {
        switch (code) {
        case 0x1C: return KEY_ENTER;
        case 0x1D: return KEY_RCTRL;
        case 0x35: return '/';
        case 0x38: return KEY_RALT;
        case 0x47: return KEY_HOME;
        case 0x48: return KEY_UPARROW;
        case 0x49: return KEY_PGUP;
        case 0x4B: return KEY_LEFTARROW;
        case 0x4D: return KEY_RIGHTARROW;
        case 0x4F: return KEY_END;
        case 0x50: return KEY_DOWNARROW;
        case 0x51: return KEY_PGDN;
        case 0x52: return KEY_INS;
        case 0x53: return KEY_DEL;
        }
        return 0;
    }
    switch (code) {
    case 0x01: return KEY_ESCAPE;
    case 0x0E: return KEY_BACKSPACE;
    case 0x0F: return KEY_TAB;
    case 0x1C: return KEY_ENTER;
    case 0x1D: return KEY_RCTRL;
    case 0x2A: case 0x36: return KEY_RSHIFT;
    case 0x38: return KEY_LALT;
    case 0x39: return ' ';
    case 0x3A: return KEY_CAPSLOCK;
    case 0x45: return KEY_NUMLOCK;
    case 0x46: return KEY_SCRLCK;
    case 0x57: return KEY_F11;
    case 0x58: return KEY_F12;
    case 0x37: return '*';
    case 0x4A: return '-';
    case 0x4E: return '+';
    case 0x47: return KEY_HOME;
    case 0x48: return KEY_UPARROW;
    case 0x49: return KEY_PGUP;
    case 0x4B: return KEY_LEFTARROW;
    case 0x4C: return '5';
    case 0x4D: return KEY_RIGHTARROW;
    case 0x4F: return KEY_END;
    case 0x50: return KEY_DOWNARROW;
    case 0x51: return KEY_PGDN;
    case 0x52: return KEY_INS;
    case 0x53: return KEY_DEL;
    }
    if (code >= 0x3B && code <= 0x44) return 0x80 + code;   // F1..F10
    static const char plain[] = "\0\0" "1234567890-=\0\0" "qwertyuiop[]\0\0" "asdfghjkl;'`\0\\" "zxcvbnm,./";
    if (code < sizeof plain - 1) return (unsigned char)plain[code];
    return 0;
}

static uint16_t keyq[256];                  // pressed << 8 | key
static uint8_t keyq_head, keyq_tail;
static void key_post(bool down, int k) {
    if (!k || (uint8_t)(keyq_head + 1) == keyq_tail) return;
    keyq[keyq_head++] = (uint16_t)((down ? 0x100 : 0) | (k & 0xFF));
}

// ---------------------------------------------------------------- mouse
static int mouse_dx, mouse_dy;

// ---------------------------------------------------------------- sound
// 48 kHz signed 16-bit mono into SDL's queue, kept `ahead` frames ahead of
// what it has played. mixer_tick (mixer.cpp) asks audio_frames_wanted how
// much to make, as on DOS.
static SDL_AudioDeviceID audio_dev;
static bool audio_found, audio_on;
static uint32_t audio_ahead = 48000 * 80 / 1000;    // -latency MS
static int pump_frames;

int audio_frames_wanted(int) { return pump_frames; }
void audio_submit(const int16_t* samples, int n) {
    if (audio_on) SDL_QueueAudio(audio_dev, samples, (Uint32)n * 2);
}
const char* audio_name() { return audio_on ? "SDL" : "none"; }

static void audio_pump() {
    if (!audio_on) return;
    for (int round = 0; round < 64; round++) {
        uint32_t queued = SDL_GetQueuedAudioSize(audio_dev) / 2;
        if (queued >= audio_ahead) break;
        uint32_t need = audio_ahead - queued;
        pump_frames = (int)(need > 1024 ? 1024 : need);
        mixer_tick();
    }
    pump_frames = 0;
}

static bool audio_start() {
    if (audio_on) return true;
    if (!audio_found) return false;
    SDL_ClearQueuedAudio(audio_dev);
    SDL_PauseAudioDevice(audio_dev, 0);
    audio_on = true;
    return true;
}

static void audio_stop() {
    if (!audio_on) return;
    audio_on = false;
    SDL_PauseAudioDevice(audio_dev, 1);
    SDL_ClearQueuedAudio(audio_dev);
}

static bool audio_init() {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        printf("Sound: SDL audio: %s\n", SDL_GetError());
        return false;
    }
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = audio_rate();
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 512;
    audio_dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (!audio_dev) {
        printf("Sound: no audio device: %s\n", SDL_GetError());
        return false;
    }
    audio_found = true;
    printf("Sound: SDL audio (%s), 48 kHz\n", SDL_GetCurrentAudioDriver());
    return true;
}

// bm_audio.h: the Sound Volume menu's CARD line. SDL, then OFF.
extern "C" int bm_audio_count(void) { return audio_found ? 2 : 1; }
extern "C" int bm_audio_current(void) { return audio_found && audio_on ? 0 : bm_audio_count() - 1; }
extern "C" const char* bm_audio_label(int i) { return audio_found && i == 0 ? "SDL" : "OFF"; }
extern "C" int bm_audio_select(int i) {
    mixer_init(0);
    if (audio_found && i == 0) {
        bool ok = audio_start();
        mixer_init(ok);
        return ok;
    }
    audio_stop();
    return 1;
}

// ---------------------------------------------------------------- events
static void toggle_fullscreen() {
    if (!win) return;
    fullscreen = !fullscreen;
    SDL_SetWindowFullscreen(win, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}

extern "C" void bm_poll(void) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT:
            // The window's close button: Doom's own quit, so a net game
            // tells the others (d_net.c) and the config is saved.
            I_Quit();
            break;
        case SDL_KEYDOWN:
        case SDL_KEYUP: {
            bool down = e.type == SDL_KEYDOWN;
            SDL_Scancode sc = e.key.keysym.scancode;
            if (down && sc == SDL_SCANCODE_RETURN && (e.key.keysym.mod & KMOD_ALT)) { toggle_fullscreen(); break; }
            // Ctrl+F10 (as DOSBox) or Ctrl+Alt+G: let go of the mouse, or take it
            // back. The F10 / G never reach Doom (F10 is its quit key).
            static SDL_Scancode eaten = SDL_SCANCODE_UNKNOWN;
            if (!down && sc == eaten) { eaten = SDL_SCANCODE_UNKNOWN; break; }
            if (down && (e.key.keysym.mod & KMOD_CTRL)
                && (sc == SDL_SCANCODE_F10 || (sc == SDL_SCANCODE_G && (e.key.keysym.mod & KMOD_ALT)))) {
                if (!e.key.repeat) grab_mouse(!have_mouse);
                eaten = sc;
                break;
            }
            if (e.key.repeat) break;                    // Doom repeats nothing itself
            if (sc == SDL_SCANCODE_PAUSE) { if (down) { key_post(true, KEY_PAUSE); key_post(false, KEY_PAUSE); } break; }
            bool ext;
            int code = set1(sc, &ext);
            if (code) key_post(down, doom_key(ext, (uint8_t)code));
            break;
        }
        case SDL_MOUSEMOTION:
            if (!have_mouse) break;
            mouse_dx += e.motion.xrel;
            mouse_dy += e.motion.yrel;
            mouse_news = true;
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP: {
            if (!have_mouse) {                          // a click takes the mouse back
                if (e.type == SDL_MOUSEBUTTONDOWN) grab_mouse(true);
                break;
            }
            int bit = e.button.button == SDL_BUTTON_LEFT ? 1 : e.button.button == SDL_BUTTON_RIGHT ? 2
                    : e.button.button == SDL_BUTTON_MIDDLE ? 4 : 0;
            if (!bit) break;
            if (e.type == SDL_MOUSEBUTTONDOWN) mouse_buttons |= bit; else mouse_buttons &= ~bit;
            mouse_news = true;
            break;
        }
        case SDL_WINDOWEVENT:
            if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                grab_mouse(false);
                if (mouse_buttons) { mouse_buttons = 0; mouse_news = true; }
            } else if (e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
                grab_mouse(true);
            }
            break;
        }
    }
    audio_pump();
}

extern "C" int bm_key(int* pressed, unsigned char* key) {
    bm_poll();
    if (keyq_tail == keyq_head) return 0;
    uint16_t k = keyq[keyq_tail++];
    *pressed = k >> 8;
    *key = (unsigned char)k;
    return 1;
}

extern "C" int bm_mouse(int* buttons, int* dx, int* dy) {
    if (!mouse_news) return 0;
    *buttons = mouse_buttons; *dx = mouse_dx; *dy = mouse_dy;
    mouse_dx = mouse_dy = 0;
    mouse_news = false;
    return 1;
}

extern "C" uint32_t bm_ms(void) { return now_ms(); }

extern "C" void bm_sleep_ms(uint32_t ms) {
    uint32_t t0 = now_ms();
    for (;;) {
        bm_poll();
        uint32_t t = now_ms() - t0;
        if (t >= ms) break;
        SDL_Delay(ms - t > 2 ? 2 : 1);
    }
}

// ================================================================ start / exit
static void shutdown() {
    net_shutdown();
    audio_stop();
    if (win) SDL_SetRelativeMouseMode(SDL_FALSE);
    SDL_Quit();
}

static void on_signal(int sig) {
    net_shutdown();
    signal(sig, SIG_DFL);
    raise(sig);
}

// Doom (d_iwad.c, as built here) looks for the IWAD in the current
// directory only. As a Linux Doom: DOOMWADDIR, DOOMWADPATH and the usual
// places too, handed over as -iwad.
static std::string find_iwad() {
    static const char* names[] = {"doom2.wad", "plutonia.wad", "tnt.wad", "doom.wad", "doom1.wad",
                                  "freedoom2.wad", "freedoom1.wad"};
    std::string dirs[16];
    int nd = 0;
    dirs[nd++] = ".";
    if (const char* e = getenv("DOOMWADDIR")) dirs[nd++] = e;
    if (const char* e = getenv("DOOMWADPATH")) {
        std::string p = e;
        size_t a = 0;
        while (a <= p.size() && nd < 12) {
            size_t b = p.find(':', a);
            if (b == std::string::npos) b = p.size();
            if (b > a) dirs[nd++] = p.substr(a, b - a);
            a = b + 1;
        }
    }
    if (const char* h = getenv("HOME")) dirs[nd++] = std::string(h) + "/.local/share/games/doom";
    dirs[nd++] = "/usr/local/share/games/doom";
    dirs[nd++] = "/usr/share/games/doom";
    for (const char* n : names)
        for (int d = 0; d < nd; d++) {
            for (int upper = 0; upper < 2; upper++) {
                std::string f = n;
                if (upper) for (auto& c : f) c = (char)toupper((unsigned char)c);
                std::string path = dirs[d] + "/" + f;
                struct stat st;
                if (stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode)) return d == 0 ? std::string() : path;
            }
        }
    return std::string();
}

extern "C" void doomgeneric_Create(int argc, char** argv);
extern "C" void doomgeneric_Tick(void);

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IOLBF, 0);
    printf("Doom for Linux (doomgeneric, SDL2)\n");

    // The network first, as IPXSETUP / SERSETUP ran before DOOM.EXE: find
    // the other players (Ctrl+C gives up), then start the game.
    signal(SIGINT, on_signal); signal(SIGTERM, on_signal);
    signal(SIGSEGV, on_signal); signal(SIGABRT, on_signal);
    signal(SIGPIPE, SIG_IGN);
    if (!net_setup(argc, argv)) return 1;

    // The mouse in the game's units (as DOS's mickeys and bare metal's PS/2
    // counts), not shrunk by the window's scale: SDL by default divides
    // relative motion by how much the 320x240 picture is blown up, so a 3x
    // window turned at a third of the speed.
    SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_SCALING, "0");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }
    t_freq = SDL_GetPerformanceFrequency();
    t_start = SDL_GetPerformanceCounter();
    atexit(shutdown);

    fullscreen = has_arg(argc, argv, "-fullscreen");
    if (has_arg(argc, argv, "-window")) fullscreen = false;
    vsync = !has_arg(argc, argv, "-novsync");
    for (int i = 1; i + 1 < argc; i++) {
        if (!strcasecmp(argv[i], "-width")) start_w = atoi(argv[i + 1]);
        if (!strcasecmp(argv[i], "-height")) start_h = atoi(argv[i + 1]);
        if (!strcasecmp(argv[i], "-geometry")) sscanf(argv[i + 1], "%dx%d", &start_w, &start_h);
    }
    if (start_w && !start_h) start_h = start_w * 3 / 4;
    if (start_h && !start_w) start_w = start_h * 4 / 3;

    // Sound latency: -latency MS (default 80).
    for (int i = 1; i + 1 < argc; i++)
        if (!strcasecmp(argv[i], "-latency")) {
            int v = atoi(argv[i + 1]);
            if (v >= 20 && v <= 600) audio_ahead = (uint32_t)(48000 * v / 1000);
        }
    bool want_sound = !has_arg(argc, argv, "-nosound") && !(has_arg(argc, argv, "-nosfx") && has_arg(argc, argv, "-nomusic"));
    bool sound = want_sound && audio_init() && audio_start();
    mixer_init(sound);
    music_init();

    static char* args[512];
    int n = 0;
    for (int i = 0; i < argc && n < 500; i++) args[n++] = argv[i];
    if (!has_arg(argc, argv, "-mb")) { args[n++] = (char*)"-mb"; args[n++] = (char*)"64"; }
    static std::string iwad;
    if (!has_arg(argc, argv, "-iwad")) {
        iwad = find_iwad();
        if (!iwad.empty()) {
            printf("IWAD: %s\n", iwad.c_str());
            args[n++] = (char*)"-iwad";
            args[n++] = (char*)iwad.c_str();
        }
    }
    args[n] = nullptr;

    doomgeneric_Create(n, args);
    for (;;) doomgeneric_Tick();
}
