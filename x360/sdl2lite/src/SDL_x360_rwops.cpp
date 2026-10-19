/*
*	Xbox 360 port of RSDKv4 (Sonic 1 Forever / Sonic 2 Absolute), SDL2-lite from the Sonic 3 A.I.R. port.
*	SDL2-lite: SDL_RWops for files and memory.
*
*	Files go through the POSIX layer of RXDK's libc (open / read / lseek / write), which hands each request
*	to the kernel in one piece. Its stdio splits reads into tiny blocks, which made loading many times slower
*	on the console (USB / HDD) in the S3AIR port. Paths are normalized first (SDL_X360_NormalizePath): '/'
*	becomes '\', duplicate separators and "." components are removed, and relative paths are made relative
*	to game:\ (the folder the XEX was started from).
*/

#include "SDL_x360_internal.h"
#include "SDL_x360.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>


namespace
{
	/* ----- Files ----- */

	int getFd(SDL_RWops* context)
	{
		return (int)(intptr_t)context->hidden.unknown.data1;
	}

	Sint64 SDLCALL fileSize(SDL_RWops* context)
	{
		struct stat info;
		if (fstat(getFd(context), &info) != 0)
			return -1;
		return (Sint64)info.st_size;
	}

	Sint64 SDLCALL fileSeek(SDL_RWops* context, Sint64 offset, int whence)
	{
		static const int ORIGINS[] = { SEEK_SET, SEEK_CUR, SEEK_END };
		if (whence < RW_SEEK_SET || whence > RW_SEEK_END)
			return SDL_SetError("Unknown value for 'whence'");
		const off_t position = lseek(getFd(context), (off_t)offset, ORIGINS[whence]);
		if (position < 0)
			return SDL_Error(SDL_EFSEEK);
		return (Sint64)position;
	}

	size_t SDLCALL fileRead(SDL_RWops* context, void* ptr, size_t size, size_t maxnum)
	{
		const size_t total = size * maxnum;
		if (total == 0)
			return 0;
		size_t done = 0;
		while (done < total)
		{
			const ssize_t result = read(getFd(context), static_cast<char*>(ptr) + done, total - done);
			if (result < 0)
			{
				SDL_Error(SDL_EFREAD);
				break;
			}
			if (result == 0)
				break;		// End of file
			done += (size_t)result;
		}
		return done / size;
	}

	size_t SDLCALL fileWrite(SDL_RWops* context, const void* ptr, size_t size, size_t num)
	{
		const size_t total = size * num;
		if (total == 0)
			return 0;
		size_t done = 0;
		while (done < total)
		{
			const ssize_t result = write(getFd(context), static_cast<const char*>(ptr) + done, total - done);
			if (result <= 0)
			{
				SDL_Error(SDL_EFWRITE);
				break;
			}
			done += (size_t)result;
		}
		return done / size;
	}

	int SDLCALL fileClose(SDL_RWops* context)
	{
		int result = 0;
		if (nullptr != context)
		{
			if (close(getFd(context)) != 0)
				result = SDL_Error(SDL_EFWRITE);
			SDL_FreeRW(context);
		}
		return result;
	}


	/* ----- Memory ----- */

	Sint64 SDLCALL memSize(SDL_RWops* context)
	{
		return (Sint64)(context->hidden.mem.stop - context->hidden.mem.base);
	}

	Sint64 SDLCALL memSeek(SDL_RWops* context, Sint64 offset, int whence)
	{
		Uint8* newPosition;
		switch (whence)
		{
			case RW_SEEK_SET:  newPosition = context->hidden.mem.base + offset;  break;
			case RW_SEEK_CUR:  newPosition = context->hidden.mem.here + offset;  break;
			case RW_SEEK_END:  newPosition = context->hidden.mem.stop + offset;  break;
			default:           return SDL_SetError("Unknown value for 'whence'");
		}
		if (newPosition < context->hidden.mem.base)
			newPosition = context->hidden.mem.base;
		if (newPosition > context->hidden.mem.stop)
			newPosition = context->hidden.mem.stop;
		context->hidden.mem.here = newPosition;
		return (Sint64)(context->hidden.mem.here - context->hidden.mem.base);
	}

	size_t SDLCALL memRead(SDL_RWops* context, void* ptr, size_t size, size_t maxnum)
	{
		if (size == 0 || maxnum == 0)
			return 0;
		size_t total = size * maxnum;
		const size_t available = (size_t)(context->hidden.mem.stop - context->hidden.mem.here);
		if (total > available)
			total = available - (available % size);
		memcpy(ptr, context->hidden.mem.here, total);
		context->hidden.mem.here += total;
		return total / size;
	}

	size_t SDLCALL memWrite(SDL_RWops* context, const void* ptr, size_t size, size_t num)
	{
		if (size == 0 || num == 0)
			return 0;
		size_t total = size * num;
		const size_t available = (size_t)(context->hidden.mem.stop - context->hidden.mem.here);
		if (total > available)
			total = available - (available % size);
		memcpy(context->hidden.mem.here, ptr, total);
		context->hidden.mem.here += total;
		return total / size;
	}

	size_t SDLCALL memWriteConst(SDL_RWops* context, const void* ptr, size_t size, size_t num)
	{
		SDL_SetError("Can't write to read-only memory");
		return 0;
	}

	int SDLCALL memClose(SDL_RWops* context)
	{
		SDL_FreeRW(context);
		return 0;
	}

	bool isSeparator(char c)
	{
		return (c == '\\' || c == '/');
	}
}


extern "C"
{

int SDL_X360_NormalizePath(const char* path, char* output, int outputSize)
{
	if (nullptr == path || nullptr == output || outputSize < 8)
		return -1;

	// A drive is everything up to the first ':' before any separator ("game:", "hdd1:", ...)
	int length = 0;
	const char* rest = path;
	const char* colon = strchr(path, ':');
	const char* firstSeparator = strpbrk(path, "\\/");
	if (nullptr != colon && (nullptr == firstSeparator || colon < firstSeparator))
	{
		const int driveLength = (int)(colon - path) + 1;
		if (driveLength + 2 > outputSize)
			return -1;
		memcpy(output, path, (size_t)driveLength);
		length = driveLength;
		rest = colon + 1;
	}
	else
	{
		memcpy(output, "game:", 5);
		length = 5;
	}
	output[length++] = '\\';

	// The remaining components, with single backslashes and without "." components
	while (*rest)
	{
		while (isSeparator(*rest))
			++rest;
		if (*rest == 0)
			break;
		const char* end = rest;
		while (*end && !isSeparator(*end))
			++end;
		const int componentLength = (int)(end - rest);
		if (!(componentLength == 1 && rest[0] == '.'))
		{
			if (output[length - 1] != '\\')
				output[length++] = '\\';
			if (length + componentLength + 1 > outputSize)
				return -1;
			memcpy(&output[length], rest, (size_t)componentLength);
			length += componentLength;
		}
		rest = end;
	}
	output[length] = 0;
	return length;
}

SDL_RWops* SDL_AllocRW(void)
{
	SDL_RWops* context = static_cast<SDL_RWops*>(calloc(1, sizeof(SDL_RWops)));
	if (nullptr == context)
		SDL_OutOfMemory();
	else
		context->type = SDL_RWOPS_UNKNOWN;
	return context;
}

void SDL_FreeRW(SDL_RWops* area)
{
	free(area);
}

SDL_RWops* SDL_RWFromFile(const char* file, const char* mode)
{
	if (nullptr == file || nullptr == mode || file[0] == 0)
	{
		SDL_SetError("SDL_RWFromFile(): No file or no mode specified");
		return nullptr;
	}

	char path[512];
	if (SDL_X360_NormalizePath(file, path, (int)sizeof(path)) < 0)
	{
		SDL_SetError("Path too long: %s", file);
		return nullptr;
	}

	// The C modes: "r" read, "w" write (create / truncate), "a" append (create), "+" read and write
	const bool plus = (nullptr != strchr(mode, '+'));
	int flags = 0;
	switch (mode[0])
	{
		case 'r':  flags = plus ? O_RDWR : O_RDONLY;  break;
		case 'w':  flags = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC;  break;
		case 'a':  flags = (plus ? O_RDWR : O_WRONLY) | O_CREAT;  break;
		default:
			SDL_SetError("SDL_RWFromFile(): Unknown mode '%s'", mode);
			return nullptr;
	}

	const int fd = open(path, flags, 0666);
	if (fd < 0)
	{
		SDL_SetError("Couldn't open %s", path);
		return nullptr;
	}
	if (mode[0] == 'a')
		lseek(fd, 0, SEEK_END);

	SDL_RWops* context = SDL_AllocRW();
	if (nullptr == context)
	{
		close(fd);
		return nullptr;
	}
	context->size  = fileSize;
	context->seek  = fileSeek;
	context->read  = fileRead;
	context->write = fileWrite;
	context->close = fileClose;
	context->type = SDL_RWOPS_STDFILE;
	context->hidden.unknown.data1 = (void*)(intptr_t)fd;
	return context;
}

SDL_RWops* SDL_RWFromMem(void* mem, int size)
{
	if (nullptr == mem || size <= 0)
	{
		SDL_InvalidParamError(nullptr == mem ? "mem" : "size");
		return nullptr;
	}
	SDL_RWops* context = SDL_AllocRW();
	if (nullptr == context)
		return nullptr;
	context->size  = memSize;
	context->seek  = memSeek;
	context->read  = memRead;
	context->write = memWrite;
	context->close = memClose;
	context->hidden.mem.base = static_cast<Uint8*>(mem);
	context->hidden.mem.here = context->hidden.mem.base;
	context->hidden.mem.stop = context->hidden.mem.base + size;
	context->type = SDL_RWOPS_MEMORY;
	return context;
}

SDL_RWops* SDL_RWFromConstMem(const void* mem, int size)
{
	SDL_RWops* context = SDL_RWFromMem(const_cast<void*>(mem), size);
	if (nullptr != context)
	{
		context->write = memWriteConst;
		context->type = SDL_RWOPS_MEMORY_RO;
	}
	return context;
}

Sint64 SDL_RWsize(SDL_RWops* context)
{
	return context->size(context);
}

Sint64 SDL_RWseek(SDL_RWops* context, Sint64 offset, int whence)
{
	return context->seek(context, offset, whence);
}

Sint64 SDL_RWtell(SDL_RWops* context)
{
	return context->seek(context, 0, RW_SEEK_CUR);
}

size_t SDL_RWread(SDL_RWops* context, void* ptr, size_t size, size_t maxnum)
{
	return context->read(context, ptr, size, maxnum);
}

size_t SDL_RWwrite(SDL_RWops* context, const void* ptr, size_t size, size_t num)
{
	return context->write(context, ptr, size, num);
}

int SDL_RWclose(SDL_RWops* context)
{
	return context->close(context);
}

}
