/*
*	Xbox 360 port of RSDKv4 (Sonic 1 Forever / Sonic 2 Absolute), SDL2-lite from the Sonic 3 A.I.R. port.
*	SDL2-lite extensions that only exist on the Xbox 360.
*/

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Returns the IDirect3DDevice9* that SDL2-lite uses for presenting (created on first use, with a 1280x720 back
// buffer), or null if it couldn't be created. Declared as void* so that this header doesn't need the XDK headers.
void* SDL_X360_GetD3DDevice(void);

// Number of hardware threads SDL_X360_ParallelFor runs jobs on at the same time (including the calling thread)
int SDL_X360_GetParallelThreadCount(void);

// Runs job(index, userData) for every index in [0, count), in parallel on the Xenon's hardware threads, and returns
// when all of them are done. Index 0 runs on the calling thread. Not reentrant: only call it from one thread.
void SDL_X360_ParallelFor(int count, void (*job)(int index, void* userData), void* userData);

// Turns a path into the form the console's file system expects: "drive:\dir\file" with single backslashes and no
// "." components. Paths without a drive are relative to game:\ (the folder the XEX was started from).
// Returns the length of the result, or -1 if it doesn't fit into outputSize bytes.
int SDL_X360_NormalizePath(const char* path, char* output, int outputSize);

// Presentation interval of SDL_RenderPresent: 1 = every vertical blank (60 FPS), 2 = every second one (30 FPS),
// 0 = immediately (no vsync)
void SDL_X360_SetPresentInterval(int interval);

#ifdef __cplusplus
}
#endif
