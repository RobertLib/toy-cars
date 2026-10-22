#ifndef TOYCARS_MUSIC_H
#define TOYCARS_MUSIC_H

#include <stdbool.h>
#include <stddef.h>

#define MUSIC_RATE 44100
#define MUSIC_SONGS 6  // Race songs, each with a faster final-lap version
#define MUSIC_TRACKS 10 // Circuits; each alternates between two of the songs
#define MUSIC_DECKS 3
#define MUSIC_BLOCK 1024

typedef enum { MUSIC_MENU, MUSIC_COUNTDOWN, MUSIC_RACE, MUSIC_PAUSE, MUSIC_RESULTS } MusicScene;
typedef enum { MUSIC_BEST, MUSIC_GOOD, MUSIC_BAD } MusicFinish;
typedef struct {
    MusicScene scene;
    int track;
    MusicFinish finish;
    bool final_lap, enabled;
} MusicContext;

/* The menu, two songs per environment, their faster final-lap versions, the
   final-lap jingle, and a finish jingle leading into a loop for each result. */
enum {
    MUSIC_CUE_MENU,
    MUSIC_CUE_RACE,
    MUSIC_CUE_CLIMAX = MUSIC_CUE_RACE + MUSIC_SONGS,
    MUSIC_CUE_FINAL_LAP = MUSIC_CUE_CLIMAX + MUSIC_SONGS,
    MUSIC_CUE_FINISH,
    MUSIC_CUE_FINISH_LOOP = MUSIC_CUE_FINISH + 3,
    MUSIC_CUES = MUSIC_CUE_FINISH_LOOP + 3
};

typedef struct {
    const char *file, *title;
    float loudness; // Integrated loudness in LUFS (ITU-R BS.1770), measured from the file
    bool loop;
} MusicCue;

/* Returns a whole asset allocated by SDL (released with SDL_free), or NULL. */
typedef void *(*MusicLoader)(const char *name, size_t *size);
typedef struct stb_vorbis stb_vorbis;

typedef struct {
    stb_vorbis *vorbis;
    void *data;
    int cue, then;
    float gain, target, step, last[2], carry[2];
    bool seam;
} MusicDeck;

/* Owned by the audio producer. A file is read and its decoder opened only when
   its cue starts; a deck releases both once it has faded out or finished. */
typedef struct {
    MusicDeck decks[MUSIC_DECKS];
    MusicLoader load;
    MusicContext context;
    float block[MUSIC_BLOCK * 2], volume, cutoff, low[2][2];
    int program, song, song_track, visits[MUSIC_TRACKS];
    MusicFinish finish;
    bool initialized;
} Music;

void music_init(Music *m, MusicLoader load);
void music_destroy(Music *m);
void music_context(Music *m, MusicContext context);
void music_render(Music *m, float *stereo, int frames);
/* The cue leading the mix, or -1 while music is silent between cues. */
int music_playing(const Music *m);
const MusicCue *music_cue(int cue);
/* The race song a circuit plays on a given visit; visits alternate two songs. */
int music_track_song(int track, int visit);

#endif
