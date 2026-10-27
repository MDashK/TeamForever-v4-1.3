#ifndef AUDIO_H
#define AUDIO_H

#include "Music.hpp"
#include "SFX.hpp"

#define MAX_VOLUME (100)
#define MIX_BUFFER_SAMPLES (256)

#if RETRO_USING_SDL2

// SDL_LockAudio() only locks the legacy device id 1, but the engine opens its device with SDL_OpenAudioDevice (ids start at 2),
// so it never actually excluded the audio thread: sounds and music could be changed or freed while being mixed
// (fix from the PS Vita port)
extern SDL_AudioDeviceID audioDevice;
#define LockAudioDevice()   SDL_LockAudioDevice(audioDevice)
#define UnlockAudioDevice() SDL_UnlockAudioDevice(audioDevice)

#elif RETRO_USING_SDL1

#define LockAudioDevice()   SDL_LockAudio()
#define UnlockAudioDevice() SDL_UnlockAudio()

#else
#define LockAudioDevice()   ;
#define UnlockAudioDevice() ;
#endif

extern int masterVolume;
extern int sfxVolume;
extern int bgmVolume;
extern bool audioEnabled;

#if RETRO_USING_SDL1 || RETRO_USING_SDL2
extern SDL_mutex *musicMutex;
extern SDL_AudioSpec audioDeviceFormat;
#endif

int InitAudioPlayback();

#if RETRO_USING_SDL1 || RETRO_USING_SDL2
#if !RETRO_USE_ORIGINAL_CODE
// These functions did exist, but with different signatures
void ProcessAudioPlayback(void *data, Uint8 *stream, int len);
void ProcessAudioMixing(Sint32 *dst, const Sint16 *src, int len, int volume, sbyte pan);
#endif

#else
void ProcessAudioPlayback() {}
void ProcessAudioMixing() {}

#endif

inline void SetMusicVolume(int volume)
{
    if (volume < 0)
        volume = 0;
    if (volume > MAX_VOLUME)
        volume = MAX_VOLUME;

    masterVolume = volume;
}

inline void SetGameVolumes(int bgmVol, int sfxVol)
{
    bgmVolume = bgmVol;
    sfxVolume = sfxVol;

    if (bgmVolume < 0)
        bgmVolume = 0;
    if (bgmVolume > MAX_VOLUME)
        bgmVolume = MAX_VOLUME;

    if (sfxVolume < 0)
        sfxVolume = 0;
    if (sfxVolume > MAX_VOLUME)
        sfxVolume = MAX_VOLUME;
}

#if RETRO_USING_SDL2
extern SDL_AudioDeviceID audioDevice;
#endif

inline void ReleaseAudioDevice()
{
#if RETRO_USING_SDL2
    // Stop the audio callback first: it mixes the music stream and the SFX buffers released below, and locks
    // musicMutex. Left running, it used freed memory on exit (a freeze or crash on the Xbox 360)
    if (audioEnabled) {
        SDL_CloseAudioDevice(audioDevice);
        audioEnabled = false;
    }
#endif

    StopMusic(true);
    StopAllSfx();
    ReleaseStageSfx();
    ReleaseGlobalSfx();

#if !RETRO_ASYNC_MUSIC
    // (with the music loader thread, the mutex is kept: the loader may still be finishing a load, and the process is ending)
    if (musicMutex)
        SDL_DestroyMutex(musicMutex);
    musicMutex = NULL;
#endif
}

#endif // !AUDIO_H
