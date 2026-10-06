// Doom's sound effects on bare metal: the WAD's DMX samples (8-bit unsigned,
// 11025 Hz mostly) go to the interrupt-driven mixer (mixer.cpp) and on to
// the sound card. No music: that would need an OPL (AdLib) synthesizer.
#include <string.h>
#include <stdio.h>
#include "doomtype.h"
#include "i_sound.h"
#include "w_wad.h"
#include "z_zone.h"
#include "mixer.hpp"

int use_libsamplerate = 0;
float libsamplerate_scale = 0.65f;
char* timidity_cfg_path = "";
char* gus_patch_path = "";
int gus_ram_kb = 1024;
void I_InitTimidityConfig(void) {}

static boolean use_prefix;

// A sound lump, checked and parsed once: the samples and their rate.
typedef struct { const uint8_t* data; uint32_t len, rate; } sample_t;

static boolean Init(boolean use_sfx_prefix)
{
    use_prefix = use_sfx_prefix;
    if (!mixer_enabled()) return false;
    printf("I_InitSound: sound effects through the mixer\n");
    return true;
}

static void Shutdown(void)
{
    for (int c = 0; c < MIXER_CHANNELS; c++) mixer_stop(c);
}

static int GetSfxLumpNum(sfxinfo_t* sfx)
{
    char name[16];
    if (sfx->link) sfx = sfx->link;
    snprintf(name, sizeof name, use_prefix ? "ds%s" : "%s", sfx->name);
    return W_CheckNumForName(name);
}

static sample_t* Load(sfxinfo_t* sfx)
{
    if (sfx->link) sfx = sfx->link;
    if (sfx->driver_data) return sfx->driver_data;
    if (sfx->lumpnum < 0) sfx->lumpnum = GetSfxLumpNum(sfx);
    if (sfx->lumpnum < 0) return NULL;
    // PU_STATIC: the mixer reads it from the timer interrupt, so the
    // zone must never move or free it.
    const uint8_t* d = W_CacheLumpNum(sfx->lumpnum, PU_STATIC);
    uint32_t size = W_LumpLength(sfx->lumpnum);
    // DMX: format 3, rate, sample count; the samples are padded by 16
    // bytes at each end.
    if (size < 8 || d[0] != 3 || d[1] != 0) return NULL;
    uint32_t rate = d[2] | d[3] << 8;
    uint32_t len = d[4] | d[5] << 8 | (uint32_t)d[6] << 16 | (uint32_t)d[7] << 24;
    if (len > size - 8) len = size - 8;
    sample_t* s = Z_Malloc(sizeof *s, PU_STATIC, NULL);
    if (len > 32) { s->data = d + 8 + 16; s->len = len - 32; }
    else { s->data = d + 8; s->len = len; }
    s->rate = rate ? rate : 11025;
    sfx->driver_data = s;
    return s;
}

static void UpdateSoundParams(int channel, int vol, int sep) { mixer_set(channel, vol, sep); }

static int StartSound(sfxinfo_t* sfx, int channel, int vol, int sep)
{
    if (channel < 0 || channel >= MIXER_CHANNELS) return -1;
    sample_t* s = Load(sfx);
    if (!s) return -1;
    mixer_start(channel, s->data, s->len, s->rate, vol, sep);
    return channel;
}

static void StopSound(int channel) { mixer_stop(channel); }
static boolean SoundIsPlaying(int channel) { return mixer_playing(channel) ? true : false; }
static void Update(void) {}
static void CacheSounds(sfxinfo_t* sounds, int num)
{
    for (int i = 0; i < num; i++) Load(&sounds[i]);
}

static snddevice_t devices[] = { SNDDEVICE_SB, SNDDEVICE_PAS, SNDDEVICE_GUS, SNDDEVICE_WAVEBLASTER,
                                 SNDDEVICE_SOUNDCANVAS, SNDDEVICE_AWE32 };

sound_module_t DG_sound_module = {
    devices, sizeof devices / sizeof devices[0],
    Init, Shutdown, GetSfxLumpNum, Update, UpdateSoundParams,
    StartSound, StopSound, SoundIsPlaying, CacheSounds,
};

// Music: none.
static boolean MusInit(void) { return true; }
static void MusNone(void) {}
static void MusVolume(int v) { (void)v; }
static void* MusRegister(void* data, int len) { (void)data; (void)len; return NULL; }
static void MusUnregister(void* h) { (void)h; }
static void MusPlay(void* h, boolean loop) { (void)h; (void)loop; }
static boolean MusPlaying(void) { return false; }

music_module_t DG_music_module = {
    devices, sizeof devices / sizeof devices[0],
    MusInit, MusNone, MusVolume, MusNone, MusNone,
    MusRegister, MusUnregister, MusPlay, MusNone, MusPlaying, NULL,
};
