/*
*	Xbox 360 port of RSDKv4 (Sonic 1 Forever / Sonic 2 Absolute), SDL2-lite from the Sonic 3 A.I.R. port.
*	SDL2-lite: audio format conversion (SDL_AudioCVT, SDL_AudioStream) and WAV loading (SDL_LoadWAV_RW).
*
*	Both converters work the same way: samples of any SDL format (8 / 16 / 32-bit integer or 32-bit float, in
*	either byte order) are read as floats, the channels are mapped (mono <-> stereo; more channels are mixed
*	down into the first two), the sample rate is converted with linear interpolation, and the result is written
*	in the destination format. The byte order matters here: OGG decoding and WAV files give little endian
*	samples (AUDIO_S16LSB), while the output device on the 360 takes big endian (AUDIO_S16MSB / AUDIO_S16SYS).
*/

#include "SDL_x360_internal.h"

#include <math.h>
#include <new>
#include <stdlib.h>
#include <string.h>
#include <vector>

static_assert(SDL_BYTEORDER == SDL_BIG_ENDIAN, "SDL's headers must see the Xbox 360 as big endian");


namespace
{
	bool isSupportedFormat(SDL_AudioFormat format)
	{
		switch (format)
		{
			case AUDIO_U8:  case AUDIO_S8:
			case AUDIO_U16LSB:  case AUDIO_S16LSB:  case AUDIO_U16MSB:  case AUDIO_S16MSB:
			case AUDIO_S32LSB:  case AUDIO_S32MSB:
			case AUDIO_F32LSB:  case AUDIO_F32MSB:
				return true;
			default:
				return false;
		}
	}

	inline int bytesPerSample(SDL_AudioFormat format)
	{
		return SDL_AUDIO_BITSIZE(format) / 8;
	}

	inline Uint32 readRaw(const Uint8* p, int bytes, bool bigEndian)
	{
		Uint32 value = 0;
		if (bigEndian)
		{
			for (int i = 0; i < bytes; ++i)
				value = (value << 8) | p[i];
		}
		else
		{
			for (int i = bytes - 1; i >= 0; --i)
				value = (value << 8) | p[i];
		}
		return value;
	}

	inline void writeRaw(Uint8* p, Uint32 value, int bytes, bool bigEndian)
	{
		for (int i = 0; i < bytes; ++i)
		{
			const int shift = bigEndian ? (bytes - 1 - i) * 8 : i * 8;
			p[i] = (Uint8)(value >> shift);
		}
	}

	float readSample(const Uint8* p, SDL_AudioFormat format)
	{
		const bool bigEndian = SDL_AUDIO_ISBIGENDIAN(format) != 0;
		switch (format)
		{
			case AUDIO_U8:		return ((float)p[0] - 128.0f) / 128.0f;
			case AUDIO_S8:		return (float)(Sint8)p[0] / 128.0f;
			case AUDIO_U16LSB:
			case AUDIO_U16MSB:	return ((float)readRaw(p, 2, bigEndian) - 32768.0f) / 32768.0f;
			case AUDIO_S16LSB:
			case AUDIO_S16MSB:	return (float)(Sint16)readRaw(p, 2, bigEndian) / 32768.0f;
			case AUDIO_S32LSB:
			case AUDIO_S32MSB:	return (float)((double)(Sint32)readRaw(p, 4, bigEndian) / 2147483648.0);
			case AUDIO_F32LSB:
			case AUDIO_F32MSB:
			{
				const Uint32 bits = readRaw(p, 4, bigEndian);
				float value;
				memcpy(&value, &bits, 4);
				return value;
			}
			default:			return 0.0f;
		}
	}

	void writeSample(Uint8* p, SDL_AudioFormat format, float value)
	{
		const bool bigEndian = SDL_AUDIO_ISBIGENDIAN(format) != 0;
		if (!SDL_AUDIO_ISFLOAT(format))
			value = (value < -1.0f) ? -1.0f : (value > 1.0f) ? 1.0f : value;
		switch (format)
		{
			case AUDIO_U8:		p[0] = (Uint8)(int)floorf(value * 127.0f + 128.5f);  break;
			case AUDIO_S8:		p[0] = (Uint8)(Sint8)(int)floorf(value * 127.0f + 0.5f);  break;
			case AUDIO_U16LSB:
			case AUDIO_U16MSB:	writeRaw(p, (Uint32)(int)floorf(value * 32767.0f + 32768.5f), 2, bigEndian);  break;
			case AUDIO_S16LSB:
			case AUDIO_S16MSB:	writeRaw(p, (Uint32)(Uint16)(Sint16)(int)floorf(value * 32767.0f + 0.5f), 2, bigEndian);  break;
			case AUDIO_S32LSB:
			case AUDIO_S32MSB:	writeRaw(p, (Uint32)(Sint32)floor((double)value * 2147483647.0 + 0.5), 4, bigEndian);  break;
			case AUDIO_F32LSB:
			case AUDIO_F32MSB:
			{
				Uint32 bits;
				memcpy(&bits, &value, 4);
				writeRaw(p, bits, 4, bigEndian);
				break;
			}
			default:			break;
		}
	}

	// Reads frames of the source format into floats with the destination channel count
	void decodeFrames(const Uint8* source, int frames, SDL_AudioFormat format, int sourceChannels, int destinationChannels, float* output)
	{
		const int sampleBytes = bytesPerSample(format);
		const int frameBytes = sampleBytes * sourceChannels;
		for (int f = 0; f < frames; ++f)
		{
			const Uint8* frame = source + (size_t)f * frameBytes;
			float* out = output + (size_t)f * destinationChannels;
			if (sourceChannels == destinationChannels)
			{
				for (int c = 0; c < sourceChannels; ++c)
					out[c] = readSample(frame + c * sampleBytes, format);
			}
			else if (destinationChannels == 1)
			{
				float sum = 0.0f;
				for (int c = 0; c < sourceChannels; ++c)
					sum += readSample(frame + c * sampleBytes, format);
				out[0] = sum / (float)sourceChannels;
			}
			else if (sourceChannels == 1)
			{
				const float value = readSample(frame, format);
				for (int c = 0; c < destinationChannels; ++c)
					out[c] = value;
			}
			else
			{
				// Different multi-channel layouts: keep the first channels, silence the rest
				for (int c = 0; c < destinationChannels; ++c)
					out[c] = (c < sourceChannels) ? readSample(frame + c * sampleBytes, format) : 0.0f;
			}
		}
	}

	void encodeFrames(const float* input, int frames, int channels, SDL_AudioFormat format, Uint8* destination)
	{
		const int sampleBytes = bytesPerSample(format);
		const size_t samples = (size_t)frames * channels;
		for (size_t i = 0; i < samples; ++i)
			writeSample(destination + i * sampleBytes, format, input[i]);
	}

	// Linear interpolation resampling of a whole buffer
	int resample(const float* input, int inputFrames, int channels, int sourceRate, int destinationRate, std::vector<float>& output)
	{
		if (sourceRate == destinationRate || inputFrames <= 0)
		{
			output.assign(input, input + (size_t)inputFrames * channels);
			return inputFrames;
		}
		const double step = (double)sourceRate / (double)destinationRate;
		const int outputFrames = (int)((double)inputFrames / step);
		output.resize((size_t)outputFrames * channels);
		for (int f = 0; f < outputFrames; ++f)
		{
			const double position = f * step;
			int index = (int)position;
			const float fraction = (float)(position - index);
			if (index >= inputFrames - 1)
				index = inputFrames - 1;
			const int next = (index + 1 < inputFrames) ? index + 1 : index;
			for (int c = 0; c < channels; ++c)
			{
				const float a = input[(size_t)index * channels + c];
				const float b = input[(size_t)next * channels + c];
				output[(size_t)f * channels + c] = a + (b - a) * fraction;
			}
		}
		return outputFrames;
	}

	// The parameters of an SDL_AudioCVT, kept in fields SDL2-lite doesn't use otherwise
	struct CvtParameters
	{
		int mSourceChannels;
		int mDestinationChannels;
		int mSourceRate;
		int mDestinationRate;
	};

	CvtParameters getCvtParameters(const SDL_AudioCVT* cvt)
	{
		CvtParameters parameters;
		const Uint32 packed = (Uint32)cvt->filter_index;
		parameters.mSourceChannels = (int)(packed & 0xFF);
		parameters.mDestinationChannels = (int)((packed >> 8) & 0xFF);
		parameters.mSourceRate = (int)(intptr_t)cvt->filters[0];
		parameters.mDestinationRate = (int)(intptr_t)cvt->filters[1];
		return parameters;
	}
}


struct _SDL_AudioStream
{
	SDL_AudioFormat mSourceFormat;
	int mSourceChannels;
	int mSourceRate;
	SDL_AudioFormat mDestinationFormat;
	int mDestinationChannels;
	int mDestinationRate;

	std::vector<Uint8> mPartialFrame;		// Input bytes that don't make up a whole frame yet
	std::vector<float> mInput;				// Decoded input frames (destination channel count, source rate)
	size_t mInputStart = 0;					// First frame of mInput still needed
	double mPosition = 0.0;					// Resampling position, in frames relative to mInputStart
	bool mFlushed = false;
	std::vector<float> mScratch;

	double getStep() const  { return (double)mSourceRate / (double)mDestinationRate; }

	size_t getInputFrames() const  { return mInput.size() / mDestinationChannels - mInputStart; }

	// Output frames that can be produced from the input so far
	int getAvailableFrames() const
	{
		const size_t inputFrames = getInputFrames();
		if (inputFrames == 0)
			return 0;
		if (mSourceRate == mDestinationRate)
			return (int)inputFrames;
		// Interpolation needs the next frame, unless the stream was flushed (then the last frame is repeated)
		const double lastUsable = (double)inputFrames - (mFlushed ? 0.0 : 1.0);
		if (lastUsable <= mPosition)
			return 0;
		return (int)ceil((lastUsable - mPosition) / getStep());
	}

	void compact()
	{
		// Drop consumed frames once they make up a good part of the buffer
		if (mInputStart > 4096 && mInputStart * 2 > getInputFrames())
		{
			mInput.erase(mInput.begin(), mInput.begin() + mInputStart * mDestinationChannels);
			mInputStart = 0;
		}
	}
};


extern "C"
{

/* ----- SDL_AudioCVT ----- */

int SDL_BuildAudioCVT(SDL_AudioCVT* cvt, SDL_AudioFormat src_format, Uint8 src_channels, int src_rate, SDL_AudioFormat dst_format, Uint8 dst_channels, int dst_rate)
{
	if (nullptr == cvt)
		return SDL_InvalidParamError("cvt");
	memset(cvt, 0, sizeof(SDL_AudioCVT));
	if (!isSupportedFormat(src_format) || !isSupportedFormat(dst_format))
		return SDL_SetError("Unsupported audio format (0x%04X -> 0x%04X)", (unsigned)src_format, (unsigned)dst_format);
	if (src_channels == 0 || dst_channels == 0 || src_rate <= 0 || dst_rate <= 0)
		return SDL_SetError("Invalid audio conversion parameters");

	cvt->src_format = src_format;
	cvt->dst_format = dst_format;
	cvt->filter_index = (int)(src_channels | (dst_channels << 8));
	cvt->filters[0] = (SDL_AudioFilter)(intptr_t)src_rate;
	cvt->filters[1] = (SDL_AudioFilter)(intptr_t)dst_rate;
	cvt->rate_incr = (double)dst_rate / (double)src_rate;

	const double sourceFrameBytes = (double)bytesPerSample(src_format) * src_channels;
	const double destinationFrameBytes = (double)bytesPerSample(dst_format) * dst_channels;
	cvt->len_ratio = (destinationFrameBytes / sourceFrameBytes) * cvt->rate_incr;
	cvt->len_mult = (int)ceil(cvt->len_ratio) + 1;
	if (cvt->len_mult < 1)
		cvt->len_mult = 1;

	cvt->needed = (src_format != dst_format || src_channels != dst_channels || src_rate != dst_rate) ? 1 : 0;
	return cvt->needed;
}

int SDL_ConvertAudio(SDL_AudioCVT* cvt)
{
	if (nullptr == cvt || nullptr == cvt->buf)
		return SDL_InvalidParamError("cvt");
	if (!cvt->needed)
	{
		cvt->len_cvt = cvt->len;
		return 0;
	}

	const CvtParameters parameters = getCvtParameters(cvt);
	const int sourceFrameBytes = bytesPerSample(cvt->src_format) * parameters.mSourceChannels;
	const int inputFrames = cvt->len / sourceFrameBytes;

	std::vector<float> decoded((size_t)inputFrames * parameters.mDestinationChannels);
	decodeFrames(cvt->buf, inputFrames, cvt->src_format, parameters.mSourceChannels, parameters.mDestinationChannels, decoded.data());
	std::vector<float> resampled;
	const int outputFrames = resample(decoded.data(), inputFrames, parameters.mDestinationChannels, parameters.mSourceRate, parameters.mDestinationRate, resampled);

	// The caller's buffer holds len * len_mult bytes
	const int destinationFrameBytes = bytesPerSample(cvt->dst_format) * parameters.mDestinationChannels;
	int framesToWrite = outputFrames;
	const int capacityFrames = (cvt->len * cvt->len_mult) / destinationFrameBytes;
	if (framesToWrite > capacityFrames)
		framesToWrite = capacityFrames;
	encodeFrames(resampled.data(), framesToWrite, parameters.mDestinationChannels, cvt->dst_format, cvt->buf);
	cvt->len_cvt = framesToWrite * destinationFrameBytes;
	return 0;
}


/* ----- SDL_AudioStream ----- */

SDL_AudioStream* SDL_NewAudioStream(const SDL_AudioFormat src_format, const Uint8 src_channels, const int src_rate, const SDL_AudioFormat dst_format, const Uint8 dst_channels, const int dst_rate)
{
	if (!isSupportedFormat(src_format) || !isSupportedFormat(dst_format))
	{
		SDL_SetError("Unsupported audio format (0x%04X -> 0x%04X)", (unsigned)src_format, (unsigned)dst_format);
		return nullptr;
	}
	if (src_channels == 0 || dst_channels == 0 || src_rate <= 0 || dst_rate <= 0)
	{
		SDL_SetError("Invalid audio stream parameters");
		return nullptr;
	}

	SDL_AudioStream* stream = new (std::nothrow) SDL_AudioStream();
	if (nullptr == stream)
	{
		SDL_OutOfMemory();
		return nullptr;
	}
	stream->mSourceFormat = src_format;
	stream->mSourceChannels = src_channels;
	stream->mSourceRate = src_rate;
	stream->mDestinationFormat = dst_format;
	stream->mDestinationChannels = dst_channels;
	stream->mDestinationRate = dst_rate;
	return stream;
}

int SDL_AudioStreamPut(SDL_AudioStream* stream, const void* buf, int len)
{
	if (nullptr == stream)
		return SDL_InvalidParamError("stream");
	if (nullptr == buf || len < 0)
		return SDL_InvalidParamError("buf");
	if (len == 0)
		return 0;

	const int frameBytes = bytesPerSample(stream->mSourceFormat) * stream->mSourceChannels;
	const Uint8* bytes = static_cast<const Uint8*>(buf);
	stream->mFlushed = false;

	// Complete a frame left over from the previous call first
	if (!stream->mPartialFrame.empty())
	{
		const int missing = frameBytes - (int)stream->mPartialFrame.size();
		const int take = (len < missing) ? len : missing;
		stream->mPartialFrame.insert(stream->mPartialFrame.end(), bytes, bytes + take);
		bytes += take;
		len -= take;
		if ((int)stream->mPartialFrame.size() == frameBytes)
		{
			const size_t oldSize = stream->mInput.size();
			stream->mInput.resize(oldSize + stream->mDestinationChannels);
			decodeFrames(stream->mPartialFrame.data(), 1, stream->mSourceFormat, stream->mSourceChannels, stream->mDestinationChannels, &stream->mInput[oldSize]);
			stream->mPartialFrame.clear();
		}
	}

	const int frames = len / frameBytes;
	if (frames > 0)
	{
		const size_t oldSize = stream->mInput.size();
		stream->mInput.resize(oldSize + (size_t)frames * stream->mDestinationChannels);
		decodeFrames(bytes, frames, stream->mSourceFormat, stream->mSourceChannels, stream->mDestinationChannels, &stream->mInput[oldSize]);
	}
	const int remainder = len - frames * frameBytes;
	if (remainder > 0)
		stream->mPartialFrame.assign(bytes + frames * frameBytes, bytes + len);
	return 0;
}

int SDL_AudioStreamGet(SDL_AudioStream* stream, void* buf, int len)
{
	if (nullptr == stream)
		return SDL_InvalidParamError("stream");
	if (nullptr == buf || len < 0)
		return SDL_InvalidParamError("buf");

	const int channels = stream->mDestinationChannels;
	const int frameBytes = bytesPerSample(stream->mDestinationFormat) * channels;
	int frames = len / frameBytes;
	const int available = stream->getAvailableFrames();
	if (frames > available)
		frames = available;
	if (frames <= 0)
		return 0;

	const float* input = stream->mInput.data() + stream->mInputStart * channels;
	const size_t inputFrames = stream->getInputFrames();
	Uint8* output = static_cast<Uint8*>(buf);

	if (stream->mSourceRate == stream->mDestinationRate)
	{
		encodeFrames(input, frames, channels, stream->mDestinationFormat, output);
		stream->mInputStart += frames;
	}
	else
	{
		const double step = stream->getStep();
		stream->mScratch.resize((size_t)frames * channels);
		double position = stream->mPosition;
		for (int f = 0; f < frames; ++f)
		{
			size_t index = (size_t)position;
			const float fraction = (float)(position - (double)index);
			if (index >= inputFrames)
				index = inputFrames - 1;
			const size_t next = (index + 1 < inputFrames) ? index + 1 : index;
			for (int c = 0; c < channels; ++c)
			{
				const float a = input[index * channels + c];
				const float b = input[next * channels + c];
				stream->mScratch[(size_t)f * channels + c] = a + (b - a) * fraction;
			}
			position += step;
		}
		encodeFrames(stream->mScratch.data(), frames, channels, stream->mDestinationFormat, output);

		// Keep the fractional position relative to the first frame still needed
		size_t consumed = (size_t)position;
		if (consumed > inputFrames)
			consumed = inputFrames;
		stream->mInputStart += consumed;
		stream->mPosition = position - (double)consumed;
	}

	if (stream->getInputFrames() == 0)
	{
		stream->mInput.clear();
		stream->mInputStart = 0;
	}
	else
	{
		stream->compact();
	}
	return frames * frameBytes;
}

int SDL_AudioStreamAvailable(SDL_AudioStream* stream)
{
	if (nullptr == stream)
		return 0;
	return stream->getAvailableFrames() * bytesPerSample(stream->mDestinationFormat) * stream->mDestinationChannels;
}

int SDL_AudioStreamFlush(SDL_AudioStream* stream)
{
	if (nullptr == stream)
		return SDL_InvalidParamError("stream");
	stream->mPartialFrame.clear();
	stream->mFlushed = true;
	return 0;
}

void SDL_AudioStreamClear(SDL_AudioStream* stream)
{
	if (nullptr == stream)
		return;
	stream->mPartialFrame.clear();
	stream->mInput.clear();
	stream->mInputStart = 0;
	stream->mPosition = 0.0;
	stream->mFlushed = false;
}

void SDL_FreeAudioStream(SDL_AudioStream* stream)
{
	delete stream;
}


/* ----- WAV files ----- */

SDL_AudioSpec* SDL_LoadWAV_RW(SDL_RWops* src, int freesrc, SDL_AudioSpec* spec, Uint8** audio_buf, Uint32* audio_len)
{
	SDL_AudioSpec* result = nullptr;
	if (nullptr == src || nullptr == spec || nullptr == audio_buf || nullptr == audio_len)
	{
		SDL_InvalidParamError("src");
		if (freesrc && nullptr != src)
			SDL_RWclose(src);
		return nullptr;
	}
	*audio_buf = nullptr;
	*audio_len = 0;

	// Read the whole file, then walk its RIFF chunks
	std::vector<Uint8> file;
	{
		Uint8 chunk[4096];
		size_t got;
		while ((got = SDL_RWread(src, chunk, 1, sizeof(chunk))) > 0)
			file.insert(file.end(), chunk, chunk + got);
	}
	if (freesrc)
		SDL_RWclose(src);

	const size_t size = file.size();
	const Uint8* data = file.data();
	if (size < 12 || memcmp(data, "RIFF", 4) != 0 || memcmp(data + 8, "WAVE", 4) != 0)
	{
		SDL_SetError("Not a RIFF / WAVE file");
		return nullptr;
	}

	Uint16 formatTag = 0;
	Uint16 channels = 0;
	Uint32 rate = 0;
	Uint16 bits = 0;
	const Uint8* samples = nullptr;
	Uint32 samplesLength = 0;

	size_t offset = 12;
	while (offset + 8 <= size)
	{
		const Uint8* chunk = data + offset;
		const Uint32 chunkLength = readRaw(chunk + 4, 4, false);
		const size_t bodyOffset = offset + 8;
		const size_t available = size - bodyOffset;
		const Uint32 bodyLength = (chunkLength > available) ? (Uint32)available : chunkLength;
		if (memcmp(chunk, "fmt ", 4) == 0 && bodyLength >= 16)
		{
			const Uint8* body = data + bodyOffset;
			formatTag = (Uint16)readRaw(body, 2, false);
			channels  = (Uint16)readRaw(body + 2, 2, false);
			rate      = readRaw(body + 4, 4, false);
			bits      = (Uint16)readRaw(body + 14, 2, false);
			if (formatTag == 0xFFFE && bodyLength >= 26)		// WAVE_FORMAT_EXTENSIBLE: the sub-format GUID starts with the format tag
				formatTag = (Uint16)readRaw(body + 24, 2, false);
		}
		else if (memcmp(chunk, "data", 4) == 0)
		{
			samples = data + bodyOffset;
			samplesLength = bodyLength;
		}
		offset = bodyOffset + chunkLength + (chunkLength & 1);		// Chunks are word aligned
	}

	if (nullptr == samples || channels == 0 || rate == 0)
	{
		SDL_SetError("WAVE file without format or data");
		return nullptr;
	}

	SDL_AudioFormat format = 0;
	int outputBytesPerSample = 0;
	bool convert24 = false;
	if (formatTag == 1)		// PCM
	{
		switch (bits)
		{
			case 8:   format = AUDIO_U8;      outputBytesPerSample = 1;  break;
			case 16:  format = AUDIO_S16LSB;  outputBytesPerSample = 2;  break;
			case 24:  format = AUDIO_S16LSB;  outputBytesPerSample = 2;  convert24 = true;  break;
			case 32:  format = AUDIO_S32LSB;  outputBytesPerSample = 4;  break;
			default:  break;
		}
	}
	else if (formatTag == 3 && bits == 32)		// IEEE float
	{
		format = AUDIO_F32LSB;
		outputBytesPerSample = 4;
	}
	if (format == 0)
	{
		SDL_SetError("Unsupported WAVE format (tag %u, %u bits)", (unsigned)formatTag, (unsigned)bits);
		return nullptr;
	}

	const Uint32 inputBytesPerFrame = (Uint32)channels * (bits / 8);
	const Uint32 frames = samplesLength / inputBytesPerFrame;
	const Uint32 outputLength = frames * channels * outputBytesPerSample;
	Uint8* buffer = static_cast<Uint8*>(malloc(outputLength ? outputLength : 1));
	if (nullptr == buffer)
	{
		SDL_OutOfMemory();
		return nullptr;
	}
	if (convert24)
	{
		// 24-bit little endian -> 16-bit little endian (keep the two high bytes)
		const Uint32 count = frames * channels;
		for (Uint32 i = 0; i < count; ++i)
		{
			buffer[i * 2 + 0] = samples[i * 3 + 1];
			buffer[i * 2 + 1] = samples[i * 3 + 2];
		}
	}
	else
	{
		memcpy(buffer, samples, outputLength);
	}

	memset(spec, 0, sizeof(SDL_AudioSpec));
	spec->format = format;
	spec->channels = (Uint8)channels;
	spec->freq = (int)rate;
	spec->samples = 4096;
	spec->size = outputLength;
	*audio_buf = buffer;
	*audio_len = outputLength;
	result = spec;
	return result;
}

void SDL_FreeWAV(Uint8* audio_buf)
{
	free(audio_buf);
}

}
