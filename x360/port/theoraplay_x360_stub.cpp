/*
*	Xbox 360 port of RSDKv4 (Sonic 1 Forever / Sonic 2 Absolute).
*
*	Video playback is not available on the Xbox 360 yet: theoraplay needs libtheora and its own decoder thread,
*	and neither Sonic 1 Forever nor Sonic 2 Absolute plays videos (no .ogv files, no LoadVideo in their scripts).
*	These stand-ins make every decode fail cleanly, so RSDKv4's Video.cpp logs "Video Decoder Error!" and the
*	game carries on as if the video had been skipped.
*/

#include "theoraplay.h"

#include <stddef.h>


extern "C"
{

THEORAPLAY_Decoder* THEORAPLAY_startDecodeFile(const char* fname, const unsigned int maxframes, THEORAPLAY_VideoFormat vidfmt)
{
	return NULL;
}

THEORAPLAY_Decoder* THEORAPLAY_startDecode(THEORAPLAY_Io* io, const unsigned int maxframes, THEORAPLAY_VideoFormat vidfmt)
{
	// The decoder owns the I/O from here on, even when it fails (closes the video file)
	if (NULL != io && NULL != io->close)
		io->close(io);
	return NULL;
}

void THEORAPLAY_stopDecode(THEORAPLAY_Decoder* decoder)  {}

int THEORAPLAY_isDecoding(THEORAPLAY_Decoder* decoder)			{ return 0; }
int THEORAPLAY_decodingError(THEORAPLAY_Decoder* decoder)		{ return 1; }
int THEORAPLAY_isInitialized(THEORAPLAY_Decoder* decoder)		{ return 0; }
int THEORAPLAY_hasVideoStream(THEORAPLAY_Decoder* decoder)		{ return 0; }
int THEORAPLAY_hasAudioStream(THEORAPLAY_Decoder* decoder)		{ return 0; }
unsigned int THEORAPLAY_availableVideo(THEORAPLAY_Decoder* decoder)	{ return 0; }
unsigned int THEORAPLAY_availableAudio(THEORAPLAY_Decoder* decoder)	{ return 0; }

const THEORAPLAY_AudioPacket* THEORAPLAY_getAudio(THEORAPLAY_Decoder* decoder)	{ return NULL; }
void THEORAPLAY_freeAudio(const THEORAPLAY_AudioPacket* item)  {}

const THEORAPLAY_VideoFrame* THEORAPLAY_getVideo(THEORAPLAY_Decoder* decoder)	{ return NULL; }
void THEORAPLAY_freeVideo(const THEORAPLAY_VideoFrame* item)  {}

unsigned int THEORAPLAY_seek(THEORAPLAY_Decoder* decoder, unsigned long mspos)	{ return 0; }

}
