#include "RetroEngine.hpp"
#include "VorbisCallbacks.hpp"


void ProcessMusicStream(Sint32 *stream, size_t bytes_wanted)
{
    SDL_LockMutex(musicMutex);
    StreamInfo *info = &streamInfo[currentStreamIndex];

    if (!info->loaded) {
        SDL_UnlockMutex(musicMutex);
        return;
    }

    switch (musicStatus) {
        case MUSIC_READY:
        case MUSIC_PLAYING: {
#if RETRO_USING_SDL2
            if (!info->stream) {
                SDL_UnlockMutex(musicMutex);
                return;
            }
            while (musicStatus == MUSIC_PLAYING && info->stream && SDL_AudioStreamAvailable(info->stream) < bytes_wanted) {
                // We need more samples: get some
                long bytes_read = ov_read(&info->vorbisFile, (char *)info->buffer, sizeof(info->buffer), 0, 2, 1,
                                          &info->vorbBitstream);

                if (bytes_read == 0) {
                    // We've reached the end of the file
                    if (info->trackLoop) {
                        ov_pcm_seek(&info->vorbisFile, info->loopPoint);
                        continue;
                    }
                    else {
                        musicStatus = MUSIC_STOPPED;
						trackID = -1;
                        break;
                    }
                }

                if (musicStatus != MUSIC_PLAYING
                    || (info->stream && SDL_AudioStreamPut(info->stream, info->buffer, (int)bytes_read) == -1)) {
                    SDL_UnlockMutex(musicMutex);
                    return;
                }
            }

            // Now that we know there are enough samples, read them and mix them
            int bytes_done = SDL_AudioStreamGet(info->stream, info->buffer, (int)bytes_wanted);
            if (bytes_done == -1) {
                SDL_UnlockMutex(musicMutex);
                return;
            }
            if (bytes_done != 0)
                ProcessAudioMixing(stream, info->buffer, bytes_done / sizeof(Sint16), (bgmVolume * masterVolume) / MAX_VOLUME, 0);
#endif

#if RETRO_USING_SDL1
            size_t bytes_gotten = 0;
            byte *buffer        = (byte *)malloc(bytes_wanted);
            memset(buffer, 0, bytes_wanted);
            while (bytes_gotten < bytes_wanted) {
                // We need more samples: get some
                long bytes_read =
                    ov_read(&oggFilePtr->vorbisFile, (char *)oggFilePtr->buffer,
                            sizeof(oggFilePtr->buffer) > (bytes_wanted - bytes_gotten) ? (bytes_wanted - bytes_gotten) : sizeof(oggFilePtr->buffer),
                            0, 2, 1, &oggFilePtr->vorbBitstream);

                if (bytes_read == 0) {
                    // We've reached the end of the file
                    if (oggFilePtr->trackLoop) {
                        ov_pcm_seek(&oggFilePtr->vorbisFile, oggFilePtr->loopPoint);
                        continue;
                    }
                    else {
                        musicStatus = MUSIC_STOPPED;
						trackID = -1;
                        break;
                    }
                }

                if (bytes_read > 0) {
                    memcpy(buffer + bytes_gotten, oggFilePtr->buffer, bytes_read);
                    bytes_gotten += bytes_read;
                }
                else {
                    PrintLog("Music read error: vorbis error: %d", bytes_read);
                }
            }

            if (bytes_gotten > 0) {
                SDL_AudioCVT convert;
                MEM_ZERO(convert);
                int cvtResult = SDL_BuildAudioCVT(&convert, oggFilePtr->spec.format, oggFilePtr->spec.channels, oggFilePtr->spec.freq,
                                                  audioDeviceFormat.format, audioDeviceFormat.channels, audioDeviceFormat.freq);
                if (cvtResult == 0) {
                    if (convert.len_mult > 0) {
                        convert.buf = (byte *)malloc(bytes_gotten * convert.len_mult);
                        convert.len = bytes_gotten;
                        memcpy(convert.buf, buffer, bytes_gotten);
                        SDL_ConvertAudio(&convert);
                    }
                }

                if (cvtResult == 0)
                    ProcessAudioMixing(stream, (const Sint16 *)convert.buf, bytes_gotten / sizeof(Sint16), (bgmVolume * masterVolume) / MAX_VOLUME,
                                       0);

                if (convert.len > 0 && convert.buf)
                    free(convert.buf);
            }
            if (bytes_wanted > 0)
                free(buffer);
#endif

            musicPosition = ov_pcm_tell(&info->vorbisFile);
            break;
        }
        case MUSIC_STOPPED:
        case MUSIC_PAUSED:
        case MUSIC_LOADING:
            // dont play
            break;
    }
    SDL_UnlockMutex(musicMutex);
}

#if !RETRO_USE_ORIGINAL_CODE
void freeMusInfo()
{
    SDL_LockMutex(musicMutex);
    int streamID = currentStreamIndex;

#if RETRO_USING_SDL2
    if (streamInfo[streamID].loaded && streamInfo[streamID].stream) {
        SDL_FreeAudioStream(streamInfo[streamID].stream);
        streamInfo[streamID].stream = NULL;
    }
#endif

    if (streamInfo[streamID].loaded)
        ov_clear(&streamInfo[streamID].vorbisFile);

#if RETRO_USING_SDL2
    streamInfo[streamID].stream = nullptr;
#endif
    streamInfo[streamID].loaded = false;

    SDL_UnlockMutex(musicMutex);
}
#endif

#if RETRO_ASYNC_MUSIC
// ---------------------------------------------------------------------------------------------
// Background music loader (from the PS Vita port)
// PlayMusic() only records a request; one loader thread reads the OGG (with ReadWholeFileThreadSafe: the engine's file
// reader is global state that the game thread uses at the same time) into the stream slot that isn't playing, and the
// slot is swapped in under musicMutex. Before, every track change started a new thread that used the global file reader
// (racing the game's own file loads), and a track requested while another was loading was dropped.
// Each request gets a generation number: a newer request, or StopMusic(), supersedes a load that's still running.
// ---------------------------------------------------------------------------------------------
struct MusicLoadRequest {
    char fileName[0x40];
    bool loop;
    uint loopPoint;
    bool resume;           // continue from the previous track's position (SwapMusicTrack), or start at startPosition
    double startPosition;  // in samples
    int generation;
};

static volatile int musicGeneration = 0;
static SDL_Thread *musicLoadThread  = NULL;
static SDL_sem *musicLoadSem        = NULL;
static SDL_mutex *musicRequestLock  = NULL;
static MusicLoadRequest pendingMusicRequest;
static bool musicRequestPending = false;

static void ClearStreamSlot(int slot)
{
    StreamInfo *strmInfo = &streamInfo[slot];
    if (strmInfo->loaded) {
#if RETRO_USING_SDL2
        if (strmInfo->stream)
            SDL_FreeAudioStream(strmInfo->stream);
#endif
        ov_clear(&strmInfo->vorbisFile);
    }
#if RETRO_USING_SDL2
    strmInfo->stream = NULL;
#endif
    strmInfo->loaded = false;
}

static void LoadMusicAsync(const MusicLoadRequest *request)
{
    // only this thread changes currentStreamIndex, so the other slot is ours: the audio thread only reads the current one
    SDL_LockMutex(musicMutex);
    int slot = 1 - currentStreamIndex;
    SDL_UnlockMutex(musicMutex);
    ClearStreamSlot(slot);

    StreamInfo *strmInfo = &streamInfo[slot];
    StreamFile *musFile  = &streamFile[slot];
    bool success         = false;
    Uint32 loadStart     = SDL_GetTicks();

    int size = ReadWholeFileThreadSafe(request->fileName, musFile->buffer, MUSBUFFER_SIZE);
    if (size > 0) {
        musFile->fileSize = size;
        musFile->filePos  = 0;

        ov_callbacks callbacks;
        callbacks.read_func  = readVorbis;
        callbacks.seek_func  = seekVorbis;
        callbacks.tell_func  = tellVorbis;
        callbacks.close_func = closeVorbis;

        int error = ov_open_callbacks(musFile, &strmInfo->vorbisFile, NULL, 0, callbacks);
        if (error == 0) {
            strmInfo->loaded        = true;
            strmInfo->vorbBitstream = -1;
            strmInfo->vorbisFile.vi = ov_info(&strmInfo->vorbisFile, -1);

            unsigned long long samples = (unsigned long long)ov_pcm_total(&strmInfo->vorbisFile, -1);
#if RETRO_USING_SDL2
            strmInfo->stream = SDL_NewAudioStream(AUDIO_S16, strmInfo->vorbisFile.vi->channels, (int)strmInfo->vorbisFile.vi->rate,
                                                  audioDeviceFormat.format, audioDeviceFormat.channels, audioDeviceFormat.freq);
            if (!strmInfo->stream)
                PrintLog("Failed to create stream: %s", SDL_GetError());
            success = strmInfo->stream != NULL;
#else
            success = true;
#endif

            if (request->resume && samples)
                ov_pcm_seek(&strmInfo->vorbisFile, (ogg_int64_t)fmod(request->startPosition, (double)samples));

            strmInfo->trackLoop = request->loop;
            strmInfo->loopPoint = request->loopPoint;
        }
        else {
            PrintLog("Failed to load vorbis '%s'! error: %d", request->fileName, error);
        }
    }
    else {
        PrintLog("Couldn't load music file '%s'", request->fileName);
    }

    bool current = false;
    SDL_LockMutex(musicMutex);
    if (request->generation == musicGeneration) { // nothing newer was requested (or stopped) in the meantime
        current = true;
        if (success) {
            currentStreamIndex = slot;
            musicPosition      = 0;
            if (musicStatus == MUSIC_LOADING)
                musicStatus = MUSIC_PLAYING;
        }
        else {
            musicStatus = MUSIC_STOPPED;
            trackID     = -1;
        }
    }
    SDL_UnlockMutex(musicMutex);

    if (!current || !success)
        ClearStreamSlot(slot); // superseded or failed: nothing will play it

    PrintLog("music: %s '%s' in %d ms%s", success ? "loaded" : "failed", request->fileName, (int)(SDL_GetTicks() - loadStart),
             current ? "" : " (superseded)");
}

static int MusicLoadThread(void *)
{
    while (true) {
        SDL_SemWait(musicLoadSem);

        MusicLoadRequest request;
        SDL_LockMutex(musicRequestLock);
        bool pending        = musicRequestPending;
        request             = pendingMusicRequest;
        musicRequestPending = false;
        SDL_UnlockMutex(musicRequestLock);

        if (pending)
            LoadMusicAsync(&request);
    }
    return 0;
}

void InitMusicLoader()
{
    if (musicLoadThread)
        return;
    musicLoadSem     = SDL_CreateSemaphore(0);
    musicRequestLock = SDL_CreateMutex();
    musicLoadThread  = SDL_CreateThreadWithStackSize(MusicLoadThread, "MusicLoader", 0x40000, NULL);
    if (!musicLoadThread)
        PrintLog("Failed to create music loader thread: %s", SDL_GetError());
}
#endif

void LoadMusic(void *userdata)
{
    // Synchronous load (the original path; with RETRO_ASYNC_MUSIC only used if the loader thread couldn't start).
    // It prepares the "back buffer" and then swaps it with the active one.

    SDL_LockMutex(musicMutex);
    int activeStream = currentStreamIndex;
    int backBuffer   = 1 - activeStream;
    SDL_UnlockMutex(musicMutex);

    // Clean up the back buffer before using it
    if (streamInfo[backBuffer].loaded) {
#if RETRO_USING_SDL2
        if (streamInfo[backBuffer].stream) {
            SDL_FreeAudioStream(streamInfo[backBuffer].stream);
            streamInfo[backBuffer].stream = NULL;
        }
#endif
        ov_clear(&streamInfo[backBuffer].vorbisFile);
        streamInfo[backBuffer].loaded = false;
    }

    FileInfo info;
    if (LoadFile(musicTracks[currentMusicTrack].fileName, &info)) {
        StreamInfo *strmInfo = &streamInfo[backBuffer];

        StreamFile *musFile = &streamFile[backBuffer];
        musFile->filePos    = 0;
        musFile->fileSize   = info.vfileSize;
        if (info.vfileSize > MUSBUFFER_SIZE)
            musFile->fileSize = MUSBUFFER_SIZE;

        FileRead(musFile->buffer, musFile->fileSize);
        CloseFile();

        unsigned long long samples = 0;
        ov_callbacks callbacks;

        callbacks.read_func  = readVorbis;
        callbacks.seek_func  = seekVorbis;
        callbacks.tell_func  = tellVorbis;
        callbacks.close_func = closeVorbis;

        int error = ov_open_callbacks(musFile, &strmInfo->vorbisFile, NULL, 0, callbacks);
        if (error == 0) {
            strmInfo->vorbBitstream = -1;
            strmInfo->vorbisFile.vi = ov_info(&strmInfo->vorbisFile, -1);

            samples = (unsigned long long)ov_pcm_total(&strmInfo->vorbisFile, -1);

#if RETRO_USING_SDL2
            strmInfo->stream = SDL_NewAudioStream(AUDIO_S16, strmInfo->vorbisFile.vi->channels, (int)strmInfo->vorbisFile.vi->rate,
                                                  audioDeviceFormat.format, audioDeviceFormat.channels, audioDeviceFormat.freq);
            if (!strmInfo->stream)
                PrintLog("Failed to create stream: %s", SDL_GetError());
#endif

#if RETRO_USING_SDL1
            playbackInfo->spec.format   = AUDIO_S16;
            playbackInfo->spec.channels = playbackInfo->vorbisFile.vi->channels;
            playbackInfo->spec.freq     = (int)playbackInfo->vorbisFile.vi->rate;
#endif

            if (musicStartPos) {
                if (streamInfo[activeStream].loaded) {
                    uint oldPos   = (uint)ov_pcm_tell(&streamInfo[activeStream].vorbisFile);
                    float newPos  = oldPos * ((float)musicRatio * 0.0001); // 8,000 == 0.8, 10,000 == 1.0 (ratio / 10,000)
                    ov_pcm_seek(&strmInfo->vorbisFile, fmod(newPos, samples));
                }
                else {
                    ov_pcm_seek(&strmInfo->vorbisFile, musicStartPos);
                }
            }
            musicStartPos = 0;

            SDL_LockMutex(musicMutex);
            musicStatus         = MUSIC_PLAYING;
            masterVolume        = MAX_VOLUME;
            trackID             = currentMusicTrack;
            strmInfo->trackLoop = musicTracks[currentMusicTrack].trackLoop;
            strmInfo->loopPoint = musicTracks[currentMusicTrack].loopPoint;
            strmInfo->loaded    = true;
            currentStreamIndex = backBuffer;
            currentMusicTrack   = -1;
            musicPosition       = 0;
            SDL_UnlockMutex(musicMutex);
        }
        else {
            musicStatus = MUSIC_STOPPED;
			trackID = -1;
            PrintLog("Failed to load vorbis! error: %d", error);
            switch (error) {
                default: PrintLog("Vorbis open error: Unknown (%d)", error); break;
                case OV_EREAD: PrintLog("Vorbis open error: A read from media returned an error"); break;
                case OV_ENOTVORBIS: PrintLog("Vorbis open error: Bitstream does not contain any Vorbis data"); break;
                case OV_EVERSION: PrintLog("Vorbis open error: Vorbis version mismatch"); break;
                case OV_EBADHEADER: PrintLog("Vorbis open error: Invalid Vorbis bitstream header"); break;
                case OV_EFAULT: PrintLog("Vorbis open error: Internal logic fault; indicates a bug or heap / stack corruption"); break;
            }
        }
    }
    else {
        musicStatus = MUSIC_STOPPED;
		trackID = -1;
    }
}

void SetMusicTrack(const char *filePath, byte trackID, bool loop, uint loopPoint)
{
    LockAudioDevice();
    TrackInfo *track = &musicTracks[trackID];
    StrCopy(track->fileName, "Data/Music/");
    StrAdd(track->fileName, filePath);
    track->trackLoop = loop;
    track->loopPoint = loopPoint;
    UnlockAudioDevice();
}

void SwapMusicTrack(const char *filePath, byte trackID, uint loopPoint, uint ratio)
{
    if (StrLength(filePath) <= 0) {
        StopMusic(true);
    }
    else {
        LockAudioDevice();
        TrackInfo *track = &musicTracks[trackID];
        StrCopy(track->fileName, "Data/Music/");
        StrAdd(track->fileName, filePath);
        track->trackLoop = true;
        track->loopPoint = loopPoint;
        musicRatio       = ratio;
        UnlockAudioDevice();
        PlayMusic(trackID, 1, true);
    }
}

bool PlayMusic(int track, int musStartPos, bool async)
{
    if (!audioEnabled)
        return false;

    if (track < 0 || track >= TRACK_COUNT) {
        StopMusic(true);
        currentMusicTrack = -1;
        return false;
    }
    if (!musicTracks[track].fileName[0])
        return false;

#if RETRO_ASYNC_MUSIC
    if (musicLoadThread) {
        MusicLoadRequest request;
        StrCopy(request.fileName, musicTracks[track].fileName);
        request.loop      = musicTracks[track].trackLoop;
        request.loopPoint = musicTracks[track].loopPoint;

        SDL_LockMutex(musicMutex);
        // musStartPos != 0: continue from the playing track's position scaled by musicRatio (SwapMusicTrack: 8,000 == 0.8,
        // 10,000 == 1.0), or start at musStartPos if nothing is playing, as the synchronous load did
        request.resume = musStartPos != 0;
        if (streamInfo[currentStreamIndex].loaded)
            request.startPosition = musicPosition * ((double)musicRatio * 0.0001);
        else
            request.startPosition = musStartPos;
        request.generation = ++musicGeneration;
        // the old track goes quiet straight away, the new one starts as soon as it's loaded.
        // everything the scripts can see is updated now, as the old synchronous load did
        musicStatus       = MUSIC_LOADING;
        trackID           = track;
        masterVolume      = MAX_VOLUME;
        musicStartPos     = 0;
        currentMusicTrack = -1;
        SDL_UnlockMutex(musicMutex);

        SDL_LockMutex(musicRequestLock);
        pendingMusicRequest = request; // a newer request simply replaces one that hasn't started yet
        musicRequestPending = true;
        SDL_UnlockMutex(musicRequestLock);
        SDL_SemPost(musicLoadSem);
        return true;
    }
#endif

    if (musicStatus != MUSIC_LOADING) {
#if RETRO_PLATFORM == RETRO_PS3
        if (async || musicStatus == MUSIC_PLAYING || musicStatus == MUSIC_PAUSED) {
            musicStartPos     = musStartPos;
            currentMusicTrack = track;
            musicStatus       = MUSIC_LOADING;
            SDL_Thread *thread = SDL_CreateThread((SDL_ThreadFunction)LoadMusic, "LoadMusic", NULL);
            if (thread) {
                SDL_DetachThread(thread);
                return true;
            }
        }
#endif
        // synchronous load on the calling thread (no device lock: the file is read while it would be held, which would
        // stall the audio thread; LoadMusic swaps the streams under musicMutex)
        musicStartPos     = musStartPos;
        currentMusicTrack = track;
        musicStatus       = MUSIC_LOADING;
        LoadMusic(NULL);
        return true;
    }
    PrintLog("WARNING music tried to play while music was loading!");
    return false;
}

void StopMusic(bool setStatus)
{
#if RETRO_ASYNC_MUSIC
    SDL_LockMutex(musicMutex);
    ++musicGeneration; // cancels any track that's still loading
    SDL_UnlockMutex(musicMutex);
#endif
    if (setStatus) {
        musicStatus = MUSIC_STOPPED;
		trackID = -1;
	}
    musicPosition = 0;

#if !RETRO_USE_ORIGINAL_CODE
    LockAudioDevice();
    freeMusInfo();
    UnlockAudioDevice();
#endif
}

bool PauseSound()
{
    if (musicStatus == MUSIC_PLAYING) {
        musicStatus = MUSIC_PAUSED;
        return true;
    }
    return false;
}

void ResumeSound()
{
    if (musicStatus == MUSIC_PAUSED)
        musicStatus = MUSIC_PLAYING;
}
