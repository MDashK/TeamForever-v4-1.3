#include "RetroEngine.hpp"
#include "VorbisCallbacks.hpp"

int globalSFXCount = 0;
int stageSFXCount  = 0;

SFXInfo sfxList[SFX_COUNT];
char sfxNames[SFX_COUNT][0x40];

ChannelInfo sfxChannels[CHANNEL_COUNT];

void LoadGlobalSfx()
{
    FileInfo info;
    FileInfo infoStore;
    char strBuffer[0x100];
    byte fileBuffer = 0;
    int fileBuffer2 = 0;

    globalSFXCount = 0;

    if (LoadFile("Data/Game/GameConfig.bin", &info)) {
        infoStore = info;

        FileRead(&fileBuffer, 1);
        FileRead(strBuffer, fileBuffer);

        FileRead(&fileBuffer, 1);
        FileRead(strBuffer, fileBuffer);

        byte buf[3];
        for (int c = 0; c < 0x60; ++c) FileRead(buf, 3);

        // Read Obect Names
        byte objectCount = 0;
        FileRead(&objectCount, 1);
        for (byte o = 0; o < objectCount; ++o) {
            FileRead(&fileBuffer, 1);
            FileRead(&strBuffer, fileBuffer);
        }

        // Read Script Paths
        for (byte s = 0; s < objectCount; ++s) {
            FileRead(&fileBuffer, 1);
            FileRead(&strBuffer, fileBuffer);
        }

        byte varCount = 0;
        FileRead(&varCount, 1);
        for (byte v = 0; v < varCount; ++v) {
            // Read Variable Name
            FileRead(&fileBuffer, 1);
            FileRead(&strBuffer, fileBuffer);

            // Read Variable Value
            FileRead(&fileBuffer2, 4);
        }

        // Read SFX
        FileRead(&fileBuffer, 1);
        globalSFXCount = fileBuffer;
        for (byte s = 0; s < globalSFXCount; ++s) { // SFX Names
            FileRead(&fileBuffer, 1);
            FileRead(&strBuffer, fileBuffer);
            strBuffer[fileBuffer] = 0;

            SetSfxName(strBuffer, s);
        }
        for (byte s = 0; s < globalSFXCount; ++s) { // SFX Paths
            FileRead(&fileBuffer, 1);
            FileRead(&strBuffer, fileBuffer);
            strBuffer[fileBuffer] = 0;

            GetFileInfo(&infoStore);
            CloseFile();
            LoadSfx(strBuffer, s);
            SetFileInfo(&infoStore);
        }

        CloseFile();

#if RETRO_USE_MOD_LOADER
        Engine.LoadXMLSoundFX();
#endif
    }

    for (int i = 0; i < CHANNEL_COUNT; ++i) sfxChannels[i].sfxID = -1;
}

void SetSfxName(const char *sfxName, int sfxID)
{
    int sfxNameID   = 0;
    int soundNameID = 0;
    while (sfxName[sfxNameID]) {
        if (sfxName[sfxNameID] != ' ')
            sfxNames[sfxID][soundNameID++] = sfxName[sfxNameID];
        ++sfxNameID;
    }
    sfxNames[sfxID][soundNameID] = 0;
    PrintLog("Set SFX (%d) name to: %s", sfxID, sfxName);
}

#if !RETRO_USE_ORIGINAL_CODE
// in-memory ogg reader used for ogg sound effects (from the PS Vita port)
struct MemoryVorbisFile {
    byte *data;
    int size;
    int pos;
};
static size_t memReadVorbis(void *mem, size_t size, size_t nmemb, void *ptr)
{
    MemoryVorbisFile *file = (MemoryVorbisFile *)ptr;
    int n                  = (int)(size * nmemb);
    if (n > file->size - file->pos)
        n = file->size - file->pos;
    if (n > 0) {
        memcpy(mem, &file->data[file->pos], n);
        file->pos += n;
    }
    return n > 0 ? n : 0;
}
static int memSeekVorbis(void *ptr, ogg_int64_t offset, int whence)
{
    MemoryVorbisFile *file = (MemoryVorbisFile *)ptr;
    switch (whence) {
        case SEEK_SET: whence = 0; break;
        case SEEK_CUR: whence = file->pos; break;
        case SEEK_END: whence = file->size; break;
        default: break;
    }
    file->pos = (int)(whence + offset);
    return 0;
}
static long memTellVorbis(void *ptr) { return ((MemoryVorbisFile *)ptr)->pos; }
static int memCloseVorbis(void *ptr) { return 1; }
#endif

static void LoadSfxFromFile(char *filePath, byte sfxID)
{
    if (!audioEnabled)
        return;

    FileInfo info;
    char fullPath[0x80];

    StrCopy(fullPath, "Data/SoundFX/");
    StrAdd(fullPath, filePath);

    if (LoadFile(fullPath, &info)) {
#if !RETRO_USE_ORIGINAL_CODE
        byte type = fullPath[StrLength(fullPath) - 3];
        if (type == 'w') {
            byte *sfx = new byte[info.vfileSize];
            FileRead(sfx, info.vfileSize);
            CloseFile();

            SDL_RWops *src = SDL_RWFromMem(sfx, info.vfileSize);
            if (src == NULL) {
                PrintLog("Unable to open sfx: %s", info.fileName);
            }
            else {
                SDL_AudioSpec wav_spec;
                uint wav_length;
                byte *wav_buffer;
                SDL_AudioSpec *wav = SDL_LoadWAV_RW(src, 0, &wav_spec, &wav_buffer, &wav_length);

                SDL_RWclose(src);
                delete[] sfx;
                if (wav == NULL) {
                    PrintLog("Unable to read sfx: %s", info.fileName);
                }
                else {
                    SDL_AudioCVT convert;
                    if (SDL_BuildAudioCVT(&convert, wav->format, wav->channels, wav->freq, audioDeviceFormat.format, audioDeviceFormat.channels,
                                          audioDeviceFormat.freq)
                        > 0) {
                        convert.buf = (byte *)malloc(wav_length * convert.len_mult);
                        convert.len = wav_length;
                        memcpy(convert.buf, wav_buffer, wav_length);
                        SDL_ConvertAudio(&convert);

                        LockAudioDevice();
                        StrCopy(sfxList[sfxID].name, filePath);
                        sfxList[sfxID].buffer = (Sint16 *)convert.buf;
                        sfxList[sfxID].length = convert.len_cvt / sizeof(Sint16);
                        sfxList[sfxID].loaded = true;
                        UnlockAudioDevice();
                        SDL_FreeWAV(wav_buffer);
                    }
                    else { // this causes errors, actually
                        PrintLog("Unable to read sfx: %s (error: %s)", info.fileName, SDL_GetError());
                        sfxList[sfxID].loaded = false;
                        free(convert.buf);
                        SDL_FreeWAV(wav_buffer);
                    }
                }
            }
        }
        else if (type == 'o') {
            // ogg sfx :(
            OggVorbis_File vf;
            ov_callbacks callbacks = OV_CALLBACKS_NOCLOSE;
            vorbis_info *vinfo;
            byte *buf;
            SDL_AudioSpec spec;
            int bitstream = -1;
            long samplesize;
            long samples;
            int read, toRead;

            // The file goes into its own buffer: this used to borrow the music's stream buffers (and switch currentStreamIndex),
            // overwriting the track that was playing (fix from the PS Vita port; Sonic 2 Absolute's sound effects are all OGG)
            MemoryVorbisFile sfxFile;
            sfxFile.size = info.vfileSize;
            sfxFile.pos  = 0;
            sfxFile.data = new byte[sfxFile.size > 0 ? sfxFile.size : 1];
            FileRead(sfxFile.data, sfxFile.size);
            CloseFile();

            callbacks.read_func  = memReadVorbis;
            callbacks.seek_func  = memSeekVorbis;
            callbacks.tell_func  = memTellVorbis;
            callbacks.close_func = memCloseVorbis;

            int error = ov_open_callbacks(&sfxFile, &vf, NULL, 0, callbacks);
            if (error != 0) {
                ov_clear(&vf);
                delete[] sfxFile.data;
                PrintLog("failed to load ogg sfx!");
                return;
            }

            vinfo = ov_info(&vf, -1);

            byte *audioBuf = NULL;
            uint audioLen  = 0;
            memset(&spec, 0, sizeof(SDL_AudioSpec));

            spec.format   = AUDIO_S16;
            spec.channels = vinfo->channels;
            spec.freq     = (int)vinfo->rate;
            spec.samples  = 4096; /* buffer size */

            samples = (long)ov_pcm_total(&vf, -1);

            audioLen = spec.size = (Uint32)(samples * spec.channels * 2);
            audioBuf             = (byte *)malloc(audioLen);
            buf                  = audioBuf;
            toRead               = audioLen;

            for (read = (int)ov_read(&vf, (char *)buf, toRead, 0, 2, 1, &bitstream); read > 0;
                 read = (int)ov_read(&vf, (char *)buf, toRead, 0, 2, 1, &bitstream)) {
                if (read < 0) {
                    free(audioBuf);
                    ov_clear(&vf);
                    delete[] sfxFile.data;
                    PrintLog("failed to read ogg sfx!");
                    return;
                }
                toRead -= read;
                buf += read;
            }

            ov_clear(&vf); // clears & closes vorbis file
            delete[] sfxFile.data;

            samplesize = ((spec.format & 0xFF) / 8) * spec.channels;
            audioLen &= ~(samplesize - 1);

            SDL_AudioCVT convert;
            if (SDL_BuildAudioCVT(&convert, spec.format, spec.channels, spec.freq, audioDeviceFormat.format, audioDeviceFormat.channels,
                                  audioDeviceFormat.freq)
                > 0) {
                convert.buf = (byte *)malloc(audioLen * convert.len_mult);
                convert.len = audioLen;
                memcpy(convert.buf, audioBuf, audioLen);
                SDL_ConvertAudio(&convert);

                LockAudioDevice();
                StrCopy(sfxList[sfxID].name, filePath);
                sfxList[sfxID].buffer = (Sint16 *)convert.buf;
                sfxList[sfxID].length = convert.len_cvt / sizeof(Sint16);
                sfxList[sfxID].loaded = true;
                UnlockAudioDevice();
                free(audioBuf);
            }
            else {
                LockAudioDevice();
                StrCopy(sfxList[sfxID].name, filePath);
                sfxList[sfxID].buffer = (Sint16 *)audioBuf;
                sfxList[sfxID].length = audioLen / sizeof(Sint16);
                sfxList[sfxID].loaded = true;
                UnlockAudioDevice();
            }
        }
        else {
            CloseFile();
            PrintLog("Sfx format not supported!");
        }
#endif
    }
}
#if RETRO_SFX_CACHE
// Converted sound effects are kept between stage loads (stage sfx are released and reloaded on every load otherwise)
// (same cache as the PS Vita port)
#include <map>
#include <string>
struct CachedSfx {
    Sint16 *buffer;
    size_t length;
};
static std::map<std::string, CachedSfx> sfxCache;
static size_t sfxCacheBytes = 0;
#define SFXCACHE_BUDGET (16 * 1024 * 1024)

void ClearSfxCache()
{
    for (auto &entry : sfxCache) free(entry.second.buffer);
    sfxCache.clear();
    sfxCacheBytes = 0;
}
#endif

void LoadSfx(char *filePath, byte sfxID)
{
#if RETRO_SFX_CACHE
    if (audioEnabled) {
        auto it = sfxCache.find(filePath);
        if (it != sfxCache.end()) {
            Sint16 *buffer = (Sint16 *)malloc(it->second.length * sizeof(Sint16));
            if (buffer) {
                memcpy(buffer, it->second.buffer, it->second.length * sizeof(Sint16));
                LockAudioDevice();
                StrCopy(sfxList[sfxID].name, filePath);
                sfxList[sfxID].buffer = buffer;
                sfxList[sfxID].length = it->second.length;
                sfxList[sfxID].loaded = true;
                UnlockAudioDevice();
                return;
            }
        }
    }
#endif

    LoadSfxFromFile(filePath, sfxID);

#if RETRO_SFX_CACHE
    if (audioEnabled && sfxList[sfxID].loaded && sfxList[sfxID].buffer && StrComp(sfxList[sfxID].name, filePath)
        && sfxCache.find(filePath) == sfxCache.end()) {
        size_t bytes = sfxList[sfxID].length * sizeof(Sint16);
        if (sfxCacheBytes + bytes <= SFXCACHE_BUDGET) {
            Sint16 *copy = (Sint16 *)malloc(bytes);
            if (copy) {
                memcpy(copy, sfxList[sfxID].buffer, bytes);
                sfxCache[filePath] = { copy, sfxList[sfxID].length };
                sfxCacheBytes += bytes;
            }
        }
    }
#endif
}
void PlaySfx(int sfx, bool loop)
{
    LockAudioDevice();
    int sfxChannelID = -1;
    for (int c = 0; c < CHANNEL_COUNT; ++c) {
        if (sfxChannels[c].sfxID == sfx || sfxChannels[c].sfxID == -1) {
            sfxChannelID = c;
            break;
        }
    }

    ChannelInfo *sfxInfo  = &sfxChannels[sfxChannelID];
    sfxInfo->sfxID        = sfx;
    sfxInfo->samplePtr    = sfxList[sfx].buffer;
    sfxInfo->sampleLength = sfxList[sfx].length;
    sfxInfo->loopSFX      = loop;
    sfxInfo->pan          = 0;
    UnlockAudioDevice();
}
void SetSfxAttributes(int sfx, int loopCount, sbyte pan)
{
    LockAudioDevice();
    int sfxChannel = -1;
    for (int i = 0; i < CHANNEL_COUNT; ++i) {
        if (sfxChannels[i].sfxID == sfx) {
            sfxChannel = i;
            break;
        }
    }
    if (sfxChannel == -1) {
        UnlockAudioDevice();
        return; // wasn't found
    }

    ChannelInfo *sfxInfo = &sfxChannels[sfxChannel];
    sfxInfo->loopSFX     = loopCount == -1 ? sfxInfo->loopSFX : loopCount;
    sfxInfo->pan         = pan;
    sfxInfo->sfxID       = sfx;
    UnlockAudioDevice();
}

void StopSfx(int sfx)
{
    for (int i = 0; i < CHANNEL_COUNT; ++i) {
        if (sfxChannels[i].sfxID == sfx) {
            MEM_ZERO(sfxChannels[i]);
            sfxChannels[i].sfxID = -1;
        }
    }
}

void StopAllSfx()
{
#if !RETRO_USE_ORIGINAL_CODE
    LockAudioDevice();
#endif

    for (int i = 0; i < CHANNEL_COUNT; ++i) sfxChannels[i].sfxID = -1;

#if !RETRO_USE_ORIGINAL_CODE
    UnlockAudioDevice();
#endif
}
void ReleaseGlobalSfx()
{
    for (int i = globalSFXCount - 1; i >= 0; --i) {
        if (sfxList[i].loaded) {
            StrCopy(sfxList[i].name, "");
            StrCopy(sfxNames[i], "");
            if (sfxList[i].buffer)
                free(sfxList[i].buffer);

            sfxList[i].buffer = NULL;
            sfxList[i].length = 0;
            sfxList[i].loaded = false;
        }
    }

    globalSFXCount = 0;
}
void ReleaseStageSfx()
{
    for (int i = (stageSFXCount + globalSFXCount) - 1; i >= globalSFXCount; --i) {
        if (sfxList[i].loaded) {
            StrCopy(sfxList[i].name, "");
            StrCopy(sfxNames[i], "");
            if (sfxList[i].buffer)
                free(sfxList[i].buffer);

            sfxList[i].buffer = NULL;
            sfxList[i].length = 0;
            sfxList[i].loaded = false;
        }
    }

    stageSFXCount = 0;
}
