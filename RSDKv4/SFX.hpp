#ifndef SFX_H
#define SFX_H

#include "RetroEngine.hpp"

#define SFX_COUNT   (0x100)
#if !RETRO_USE_ORIGINAL_CODE
#define CHANNEL_COUNT (0x10) // 4 in the original, 16 for convenience
#else
#define CHANNEL_COUNT (0x4)
#endif

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

struct SFXInfo {
    char name[0x40];
    Sint16 *buffer;
    size_t length;
    bool loaded;
};

struct ChannelInfo {
    size_t sampleLength;
    Sint16 *samplePtr;
    int sfxID;
    byte loopSFX;
    sbyte pan;
};

extern int globalSFXCount;
extern int stageSFXCount;

extern SFXInfo sfxList[SFX_COUNT];
extern char sfxNames[SFX_COUNT][0x40];

extern ChannelInfo sfxChannels[CHANNEL_COUNT];

void LoadGlobalSfx();
void LoadSfx(char *filePath, byte sfxID);

// keep converted sound effects between stage loads (Xbox 360, as in the PS Vita port: loading is slow there)
#define RETRO_SFX_CACHE (RETRO_PLATFORM == RETRO_XBOX_360 && RETRO_USING_SDL2 && !RETRO_USE_ORIGINAL_CODE)
#if RETRO_SFX_CACHE
void ClearSfxCache();
#endif
void PlaySfx(int sfx, bool loop);
void StopSfx(int sfx);
void SetSfxAttributes(int sfx, int loopCount, sbyte pan);
void SetSfxName(const char *sfxName, int sfxID);
void StopAllSfx();
void ReleaseGlobalSfx();
void ReleaseStageSfx();

#if !RETRO_USE_ORIGINAL_CODE
// Helper Funcs
inline bool PlaySfxByName(const char *sfx, sbyte loopCnt)
{
    char buffer[0x40];
    int pos = 0;
    while (*sfx) {
        if (*sfx != ' ')
            buffer[pos++] = *sfx;
        sfx++;
    }
    buffer[pos] = 0;

    for (int s = 0; s < globalSFXCount + stageSFXCount; ++s) {
        if (StrComp(sfxNames[s], buffer)) {
            PlaySfx(s, loopCnt);
            return true;
        }
    }
    return false;
}
inline bool StopSFXByName(const char *sfx)
{
    char buffer[0x40];
    int pos = 0;
    while (*sfx) {
        if (*sfx != ' ')
            buffer[pos++] = *sfx;
        sfx++;
    }
    buffer[pos] = 0;

    for (int s = 0; s < globalSFXCount + stageSFXCount; ++s) {
        if (StrComp(sfxNames[s], buffer)) {
            StopSfx(s);
            return true;
        }
    }
    return false;
}
#endif

#endif // !SFX_H
