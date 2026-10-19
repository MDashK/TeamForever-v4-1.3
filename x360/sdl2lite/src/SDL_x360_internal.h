/*
*	Xbox 360 port of RSDKv4 (Sonic 1 Forever / Sonic 2 Absolute), SDL2-lite from the Sonic 3 A.I.R. port.
*	SDL2-lite internals, shared by the x360/sdl2lite/src implementation files.
*/

#pragma once

#include <xtl.h>
#include "SDL.h"

// Kernel debug print: shows up in xenia.log and in a devkit's debugger
extern "C" ULONG DbgPrint(const char* format, ...);

namespace sdl2lite
{
	// Formats into the thread-unsafe global error string returned by SDL_GetError
	void setError(const char* format, ...);
}
