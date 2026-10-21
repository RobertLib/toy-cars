#ifndef TOYCARS_SFX_H
#define TOYCARS_SFX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SFX_RATE 44100
#define SFX_VOICES 48
#define SFX_BLOCK 1024
/* Playback gain 1 brings every sound to this loudness (LUFS); the director's
   levels set the mix against the music at -26 LUFS. */
#define SFX_LOUDNESS -20.f

/* Recorded sound effects. Engine, tire, surface and weather sounds are seamless
   loops; the long ambience beds stream from their files instead of being decoded. */
typedef enum {
    SFX_ENGINE_IDLE,
    SFX_ENGINE_LOW,
    SFX_ENGINE_MID,
    SFX_ENGINE_HIGH,
    SFX_SKID,
    SFX_DIRT,
    SFX_SAND,
    SFX_SNOW,
    SFX_WADE,
    SFX_WIND,
    SFX_HELICOPTER,
    SFX_AMBIENCE_COUNTRY,
    SFX_AMBIENCE_BEACH,
    SFX_AMBIENCE_WINTER,
    SFX_AMBIENCE_NIGHT,
    SFX_RAIN,
    SFX_GUSTS,
    SFX_BEEP,
    SFX_GO,
    SFX_FUEL,
    SFX_BOOST,
    SFX_TAKEOFF,
    SFX_LANDING,
    SFX_CAR_HIT,
    SFX_CONE,
    SFX_TIRE,
    SFX_FENCE,
    SFX_SPLASH,
    SFX_SPUTTER,
    SFX_GEAR,
    SFX_BACKFIRE,
    SFX_BELL,
    SFX_CHEER,
    SFX_ROAR,
    SFX_RECOVER,
    SFX_CROSSING_BELL,
    SFX_STEAM_WHISTLE,
    SFX_TRAIN_HORN,
    SFX_STEAM_TRAIN,
    SFX_TRAIN,
    SFX_LAND_TRAIN,
    SFX_LAND_TRAIN_HORN,
    SFX_UI_MOVE,
    SFX_UI_SELECT,
    SFX_COUNT
} SfxSound;

typedef struct {
    const char *file;
    float loudness; // Integrated loudness in LUFS (ITU-R BS.1770), measured from the file
    bool loop, stream;
} SfxInfo;

/* Returns a whole asset allocated by SDL (released with SDL_free), or NULL. */
typedef void *(*SfxLoader)(const char *name, size_t *size);
typedef struct stb_vorbis stb_vorbis;

typedef struct {
    int16_t *pcm;
    int frames, channels;
} SfxSample;

typedef struct {
    int sound; // -1 while free
    uint32_t serial;
    double position; // Source frame
    float gain, pitch, pan, cutoff, target_gain, target_pitch, target_pan, target_cutoff;
    float low[2], last[2];
    // A streamed bed decodes ahead into its own buffer.
    stb_vorbis *vorbis;
    void *data;
    float *block;
    int block_frames, block_used;
} SfxVoice;

/* Owned by the audio producer. Levels are linear gains relative to the
   loudness-normalized sound, pitch is a playback-rate ratio, pan runs from
   -1 (left) to 1 (right) and cutoff is a one-pole low-pass coefficient (1 = open). */
typedef struct {
    SfxSample samples[SFX_COUNT];
    SfxVoice voices[SFX_VOICES];
    SfxLoader load;
    uint32_t serial;
    float tail[2];
    bool initialized;
} Sfx;

void sfx_init(Sfx *s, SfxLoader load);
void sfx_destroy(Sfx *s);
/* Starts a one-shot and returns its handle (0 when it could not start). */
uint32_t sfx_play(Sfx *s, int sound, float gain, float pitch, float pan);
/* Drives a continuous sound held in *handle: it starts when gain rises above
   zero, follows the new targets over the next rendered block and is released
   once it has faded to silence. */
void sfx_hold(Sfx *s, uint32_t *handle, int sound, float gain, float pitch, float pan,
              float cutoff);
void sfx_stop_all(Sfx *s);
/* Adds the mix to an interleaved stereo buffer. */
void sfx_render(Sfx *s, float *stereo, int frames);
bool sfx_active(const Sfx *s, uint32_t handle);
int sfx_voices(const Sfx *s);
const SfxInfo *sfx_info(int sound);
/* One-pole coefficient for a cutoff frequency at SFX_RATE. */
float sfx_cutoff(float hz);

#endif
