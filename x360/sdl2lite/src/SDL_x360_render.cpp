/*
*	Xbox 360 port of RSDKv4 (Sonic 1 Forever / Sonic 2 Absolute), SDL2-lite from the Sonic 3 A.I.R. port.
*	SDL2-lite: the 2D renderer API (SDL_Renderer / SDL_Texture) on top of the D3D9 device of SDL_x360_video.cpp.
*
*	Covers what RSDKv4's software rendering path uses: one streaming texture per frame source (the RGB565 frame
*	buffer, and the YV12 video frames), clears, textured quads and presentation.
*	 - Textures are linear (D3DFMT_LIN_*) and double buffered: the CPU and the GPU are not synchronized
*	   automatically on the 360, so SDL_LockTexture waits until the GPU is done with the buffer it hands out
*	   (BlockUntilNotBusy) while the other one may still be in use by the frame being displayed.
*	 - The texture formats with GPUENDIAN_8IN16 / 8IN32 swapping take pixels as native (big endian) 16 / 32 bit
*	   values, so a ushort RGB565 or a uint32 0xAARRGGBB is uploaded as is.
*	 - Drawing goes straight to the 1280x720 back buffer; SDL_RenderPresent presents it. Upscaling uses the
*	   "sharp bilinear" pixel shader (shaders/present.hlsl).
*/

#include "SDL_x360_internal.h"
#include "SDL_x360.h"

#include <new>
#include <stdlib.h>
#include <string.h>

#include "../shaders/present_vs.h"
#include "../shaders/present_ps.h"


struct SDL_Renderer
{
	IDirect3DDevice9* mDevice = nullptr;
	SDL_Window* mWindow = nullptr;
	IDirect3DVertexDeclaration9* mVertexDeclaration = nullptr;
	IDirect3DVertexShader9* mVertexShader = nullptr;
	IDirect3DPixelShader9* mPixelShader = nullptr;
	Uint8 mDrawColor[4] = { 0, 0, 0, 255 };
	SDL_BlendMode mDrawBlendMode = SDL_BLENDMODE_NONE;
	bool mClearedThisFrame = false;
	Uint32 mFramesPresented = 0;
	double mPresentSeconds = 0.0;
};

struct SDL_Texture
{
	SDL_Renderer* mRenderer = nullptr;
	Uint32 mFormat = 0;
	int mAccess = 0;
	int mWidth = 0;
	int mHeight = 0;
	D3DFORMAT mD3DFormat = D3DFMT_UNKNOWN;
	int mBytesPerPixel = 0;
	bool mHasAlpha = false;
	bool mIsYUV = false;
	SDL_BlendMode mBlendMode = SDL_BLENDMODE_NONE;
	IDirect3DTexture9* mTextures[2] = { nullptr, nullptr };
	int mCurrent = 0;		// The buffer drawn by SDL_RenderCopy (the one most recently written)
	int mLocked = -1;		// The buffer handed out by SDL_LockTexture, or -1
};


namespace
{
	const int BACK_BUFFER_WIDTH = 1280;
	const int BACK_BUFFER_HEIGHT = 720;

	int gPresentInterval = 1;

	bool getD3DFormat(Uint32 format, D3DFORMAT& d3dFormat, int& bytesPerPixel, bool& hasAlpha, bool& isYUV)
	{
		isYUV = false;
		hasAlpha = false;
		switch (format)
		{
			case SDL_PIXELFORMAT_RGB565:	d3dFormat = D3DFMT_LIN_R5G6B5;    bytesPerPixel = 2;  return true;
			case SDL_PIXELFORMAT_ARGB1555:	d3dFormat = D3DFMT_LIN_A1R5G5B5;  bytesPerPixel = 2;  hasAlpha = true;  return true;
			case SDL_PIXELFORMAT_ARGB8888:	d3dFormat = D3DFMT_LIN_A8R8G8B8;  bytesPerPixel = 4;  hasAlpha = true;  return true;
			case SDL_PIXELFORMAT_RGB888:	d3dFormat = D3DFMT_LIN_X8R8G8B8;  bytesPerPixel = 4;  return true;
			case SDL_PIXELFORMAT_ABGR8888:	d3dFormat = D3DFMT_LIN_A8B8G8R8;  bytesPerPixel = 4;  hasAlpha = true;  return true;
			case SDL_PIXELFORMAT_BGR888:	d3dFormat = D3DFMT_LIN_X8B8G8R8;  bytesPerPixel = 4;  return true;
			case SDL_PIXELFORMAT_YV12:
			case SDL_PIXELFORMAT_IYUV:
				// Converted to RGB on the CPU in SDL_UpdateYUVTexture (only used for video playback)
				d3dFormat = D3DFMT_LIN_X8R8G8B8;
				bytesPerPixel = 4;
				isYUV = true;
				return true;
			default:
				return false;
		}
	}

	void releaseTextures(SDL_Texture* texture)
	{
		for (int i = 0; i < 2; ++i)
		{
			if (nullptr != texture->mTextures[i])
			{
				texture->mTextures[i]->BlockUntilNotBusy();
				texture->mTextures[i]->Release();
				texture->mTextures[i] = nullptr;
			}
		}
	}

	void clearIfNeeded(SDL_Renderer* renderer)
	{
		// The back buffer has undefined content after a present: make sure every frame starts from a clear,
		// even if the application only draws over part of it
		if (!renderer->mClearedThisFrame)
		{
			renderer->mDevice->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
			renderer->mClearedThisFrame = true;
		}
	}

	inline Uint8 clampByte(int value)
	{
		return (Uint8)((value < 0) ? 0 : (value > 255) ? 255 : value);
	}
}


extern "C"
{

void SDL_X360_SetPresentInterval(int interval)
{
	gPresentInterval = (interval < 0) ? 0 : (interval > 2) ? 2 : interval;
}


/* ----- Renderer ----- */

SDL_Renderer* SDL_CreateRenderer(SDL_Window* window, int index, Uint32 flags)
{
	IDirect3DDevice9* device = static_cast<IDirect3DDevice9*>(SDL_X360_GetD3DDevice());
	if (nullptr == device)
		return nullptr;		// The error was set by the device creation

	SDL_Renderer* renderer = new (std::nothrow) SDL_Renderer();
	if (nullptr == renderer)
	{
		SDL_OutOfMemory();
		return nullptr;
	}
	renderer->mDevice = device;
	renderer->mWindow = window;

	const D3DVERTEXELEMENT9 vertexElements[] =
	{
		{ 0, 0, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
		{ 0, 8, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
		D3DDECL_END()
	};
	if (FAILED(device->CreateVertexDeclaration(vertexElements, &renderer->mVertexDeclaration)) ||
		FAILED(device->CreateVertexShader(g_presentVertexShader, &renderer->mVertexShader)) ||
		FAILED(device->CreatePixelShader(g_presentPixelShader, &renderer->mPixelShader)))
	{
		SDL_SetError("Creating the renderer shaders failed");
		SDL_DestroyRenderer(renderer);
		return nullptr;
	}

	DbgPrint("SDL2-lite: renderer created (back buffer %dx%d)\n", BACK_BUFFER_WIDTH, BACK_BUFFER_HEIGHT);
	return renderer;
}

void SDL_DestroyRenderer(SDL_Renderer* renderer)
{
	if (nullptr == renderer)
		return;
	if (nullptr != renderer->mPixelShader)
		renderer->mPixelShader->Release();
	if (nullptr != renderer->mVertexShader)
		renderer->mVertexShader->Release();
	if (nullptr != renderer->mVertexDeclaration)
		renderer->mVertexDeclaration->Release();
	delete renderer;
}

int SDL_GetRendererOutputSize(SDL_Renderer* renderer, int* w, int* h)
{
	if (nullptr != w)
		*w = BACK_BUFFER_WIDTH;
	if (nullptr != h)
		*h = BACK_BUFFER_HEIGHT;
	return 0;
}

int SDL_SetRenderDrawColor(SDL_Renderer* renderer, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
	if (nullptr == renderer)
		return SDL_InvalidParamError("renderer");
	renderer->mDrawColor[0] = r;
	renderer->mDrawColor[1] = g;
	renderer->mDrawColor[2] = b;
	renderer->mDrawColor[3] = a;
	return 0;
}

int SDL_SetRenderDrawBlendMode(SDL_Renderer* renderer, SDL_BlendMode blendMode)
{
	if (nullptr == renderer)
		return SDL_InvalidParamError("renderer");
	renderer->mDrawBlendMode = blendMode;
	return 0;
}

int SDL_RenderClear(SDL_Renderer* renderer)
{
	if (nullptr == renderer)
		return SDL_InvalidParamError("renderer");
	const Uint8* c = renderer->mDrawColor;
	renderer->mDevice->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(c[3], c[0], c[1], c[2]), 1.0f, 0);
	renderer->mClearedThisFrame = true;
	return 0;
}

int SDL_RenderCopy(SDL_Renderer* renderer, SDL_Texture* texture, const SDL_Rect* srcrect, const SDL_Rect* dstrect)
{
	if (nullptr == renderer || nullptr == texture)
		return SDL_InvalidParamError((nullptr == renderer) ? "renderer" : "texture");
	IDirect3DTexture9* d3dTexture = texture->mTextures[texture->mCurrent];
	if (nullptr == d3dTexture)
		return SDL_SetError("Texture has no content");

	IDirect3DDevice9* device = renderer->mDevice;
	clearIfNeeded(renderer);

	SDL_Rect source = { 0, 0, texture->mWidth, texture->mHeight };
	if (nullptr != srcrect)
		source = *srcrect;
	SDL_Rect destination = { 0, 0, BACK_BUFFER_WIDTH, BACK_BUFFER_HEIGHT };
	if (nullptr != dstrect)
		destination = *dstrect;
	if (source.w <= 0 || source.h <= 0 || destination.w <= 0 || destination.h <= 0)
		return 0;

	// Pixel rectangle -> normalized device coordinates (with D3DRS_HALFPIXELOFFSET, pixel centers are at .5 like in OpenGL)
	const float left   = (float)destination.x / BACK_BUFFER_WIDTH * 2.0f - 1.0f;
	const float right  = (float)(destination.x + destination.w) / BACK_BUFFER_WIDTH * 2.0f - 1.0f;
	const float top    = 1.0f - (float)destination.y / BACK_BUFFER_HEIGHT * 2.0f;
	const float bottom = 1.0f - (float)(destination.y + destination.h) / BACK_BUFFER_HEIGHT * 2.0f;
	const float u0 = (float)source.x / texture->mWidth;
	const float u1 = (float)(source.x + source.w) / texture->mWidth;
	const float v0 = (float)source.y / texture->mHeight;
	const float v1 = (float)(source.y + source.h) / texture->mHeight;

	struct Vertex { float x, y, u, v; };
	const Vertex quad[4] =
	{
		{ left,  top,    u0, v0 },
		{ right, top,    u1, v0 },
		{ right, bottom, u1, v1 },
		{ left,  bottom, u0, v1 },
	};

	device->SetRenderState(D3DRS_HALFPIXELOFFSET, TRUE);
	device->SetRenderState(D3DRS_ZENABLE, FALSE);
	device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	const bool blend = (texture->mBlendMode == SDL_BLENDMODE_BLEND) && texture->mHasAlpha;
	device->SetRenderState(D3DRS_ALPHABLENDENABLE, blend ? TRUE : FALSE);
	if (blend)
	{
		device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
		device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
		device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
	}

	device->SetVertexDeclaration(renderer->mVertexDeclaration);
	device->SetVertexShader(renderer->mVertexShader);
	device->SetPixelShader(renderer->mPixelShader);
	const float textureSizeAndScale[4] = { (float)texture->mWidth, (float)texture->mHeight, (float)destination.w / (float)source.w, (float)destination.h / (float)source.h };
	device->SetPixelShaderConstantF(0, textureSizeAndScale, 1);
	device->SetTexture(0, d3dTexture);
	device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
	device->DrawPrimitiveUP(D3DPT_QUADLIST, 1, quad, sizeof(Vertex));
	device->SetTexture(0, nullptr);
	return 0;
}

void SDL_RenderPresent(SDL_Renderer* renderer)
{
	if (nullptr == renderer)
		return;
	clearIfNeeded(renderer);

	LARGE_INTEGER timeStart;
	QueryPerformanceCounter(&timeStart);

	static const DWORD INTERVALS[3] = { D3DPRESENT_INTERVAL_IMMEDIATE, D3DPRESENT_INTERVAL_ONE, D3DPRESENT_INTERVAL_TWO };
	renderer->mDevice->SetRenderState(D3DRS_PRESENTINTERVAL, INTERVALS[gPresentInterval]);
	renderer->mDevice->Present(nullptr, nullptr, nullptr, nullptr);
	renderer->mClearedThisFrame = false;

	LARGE_INTEGER timeEnd;
	LARGE_INTEGER frequency;
	QueryPerformanceCounter(&timeEnd);
	QueryPerformanceFrequency(&frequency);
	renderer->mPresentSeconds += (double)(timeEnd.QuadPart - timeStart.QuadPart) / (double)frequency.QuadPart;

	++renderer->mFramesPresented;
	if (renderer->mFramesPresented == 1 || (renderer->mFramesPresented % 3600) == 0)
	{
		const double framesMeasured = (renderer->mFramesPresented == 1) ? 1.0 : 3600.0;
		DbgPrint("SDL2-lite: presented frame %u, avg %.2f ms per present\n", (unsigned)renderer->mFramesPresented, renderer->mPresentSeconds * 1000.0 / framesMeasured);
		renderer->mPresentSeconds = 0.0;
	}
}


/* ----- Textures ----- */

SDL_Texture* SDL_CreateTexture(SDL_Renderer* renderer, Uint32 format, int access, int w, int h)
{
	if (nullptr == renderer)
	{
		SDL_InvalidParamError("renderer");
		return nullptr;
	}
	if (w <= 0 || h <= 0)
	{
		SDL_SetError("Texture dimensions can't be 0");
		return nullptr;
	}

	D3DFORMAT d3dFormat;
	int bytesPerPixel;
	bool hasAlpha;
	bool isYUV;
	if (!getD3DFormat(format, d3dFormat, bytesPerPixel, hasAlpha, isYUV))
	{
		SDL_SetError("Unsupported texture format 0x%08X", (unsigned)format);
		return nullptr;
	}

	SDL_Texture* texture = new (std::nothrow) SDL_Texture();
	if (nullptr == texture)
	{
		SDL_OutOfMemory();
		return nullptr;
	}
	texture->mRenderer = renderer;
	texture->mFormat = format;
	texture->mAccess = access;
	texture->mWidth = w;
	texture->mHeight = h;
	texture->mD3DFormat = d3dFormat;
	texture->mBytesPerPixel = bytesPerPixel;
	texture->mHasAlpha = hasAlpha;
	texture->mIsYUV = isYUV;

	for (int i = 0; i < 2; ++i)
	{
		if (FAILED(renderer->mDevice->CreateTexture(w, h, 1, 0, d3dFormat, 0, &texture->mTextures[i], nullptr)))
		{
			SDL_SetError("CreateTexture %dx%d (format 0x%08X) failed", w, h, (unsigned)format);
			releaseTextures(texture);
			delete texture;
			return nullptr;
		}
	}

	// Start from black, so that a texture drawn before its first update doesn't show garbage
	for (int i = 0; i < 2; ++i)
	{
		D3DLOCKED_RECT lockedRect;
		if (SUCCEEDED(texture->mTextures[i]->LockRect(0, &lockedRect, nullptr, 0)))
		{
			memset(lockedRect.pBits, 0, (size_t)lockedRect.Pitch * (size_t)h);
			texture->mTextures[i]->UnlockRect(0);
		}
	}

	DbgPrint("SDL2-lite: texture %dx%d created (format 0x%08X)\n", w, h, (unsigned)format);
	return texture;
}

void SDL_DestroyTexture(SDL_Texture* texture)
{
	if (nullptr == texture)
		return;
	if (texture->mLocked >= 0)
		texture->mTextures[texture->mLocked]->UnlockRect(0);
	releaseTextures(texture);
	delete texture;
}

int SDL_QueryTexture(SDL_Texture* texture, Uint32* format, int* access, int* w, int* h)
{
	if (nullptr == texture)
		return SDL_InvalidParamError("texture");
	if (nullptr != format)  *format = texture->mFormat;
	if (nullptr != access)  *access = texture->mAccess;
	if (nullptr != w)       *w = texture->mWidth;
	if (nullptr != h)       *h = texture->mHeight;
	return 0;
}

int SDL_SetTextureBlendMode(SDL_Texture* texture, SDL_BlendMode blendMode)
{
	if (nullptr == texture)
		return SDL_InvalidParamError("texture");
	texture->mBlendMode = blendMode;
	return 0;
}

int SDL_LockTexture(SDL_Texture* texture, const SDL_Rect* rect, void** pixels, int* pitch)
{
	if (nullptr == texture || nullptr == pixels || nullptr == pitch)
		return SDL_InvalidParamError("texture");
	if (texture->mLocked >= 0)
		return SDL_SetError("Texture is already locked");
	if (texture->mIsYUV)
		return SDL_SetError("Locking YUV textures is not supported (use SDL_UpdateYUVTexture)");

	// Hand out the buffer that isn't the most recent one; wait until the GPU no longer reads it
	const int index = 1 - texture->mCurrent;
	IDirect3DTexture9* d3dTexture = texture->mTextures[index];
	d3dTexture->BlockUntilNotBusy();
	D3DLOCKED_RECT lockedRect;
	if (FAILED(d3dTexture->LockRect(0, &lockedRect, nullptr, 0)))
		return SDL_SetError("LockRect failed");

	Uint8* bits = static_cast<Uint8*>(lockedRect.pBits);
	if (nullptr != rect)
		bits += (size_t)rect->y * lockedRect.Pitch + (size_t)rect->x * texture->mBytesPerPixel;
	*pixels = bits;
	*pitch = lockedRect.Pitch;
	texture->mLocked = index;
	return 0;
}

void SDL_UnlockTexture(SDL_Texture* texture)
{
	if (nullptr == texture || texture->mLocked < 0)
		return;
	texture->mTextures[texture->mLocked]->UnlockRect(0);
	texture->mCurrent = texture->mLocked;
	texture->mLocked = -1;
}

int SDL_UpdateTexture(SDL_Texture* texture, const SDL_Rect* rect, const void* pixels, int pitch)
{
	if (nullptr == texture || nullptr == pixels)
		return SDL_InvalidParamError("texture");

	// A partial update has to keep the rest of the texture: start from a copy of the current content
	const bool partial = (nullptr != rect) && (rect->x != 0 || rect->y != 0 || rect->w != texture->mWidth || rect->h != texture->mHeight);
	SDL_Rect area = { 0, 0, texture->mWidth, texture->mHeight };
	if (nullptr != rect)
		area = *rect;

	void* destination;
	int destinationPitch;
	if (SDL_LockTexture(texture, nullptr, &destination, &destinationPitch) != 0)
		return -1;

	if (partial)
	{
		D3DLOCKED_RECT current;
		IDirect3DTexture9* currentTexture = texture->mTextures[1 - texture->mLocked];
		if (SUCCEEDED(currentTexture->LockRect(0, &current, nullptr, D3DLOCK_READONLY)))
		{
			memcpy(destination, current.pBits, (size_t)current.Pitch * (size_t)texture->mHeight);
			currentTexture->UnlockRect(0);
		}
	}

	const size_t rowBytes = (size_t)area.w * texture->mBytesPerPixel;
	Uint8* destinationRow = static_cast<Uint8*>(destination) + (size_t)area.y * destinationPitch + (size_t)area.x * texture->mBytesPerPixel;
	const Uint8* sourceRow = static_cast<const Uint8*>(pixels);
	for (int y = 0; y < area.h; ++y)
	{
		memcpy(destinationRow, sourceRow, rowBytes);
		destinationRow += destinationPitch;
		sourceRow += pitch;
	}
	SDL_UnlockTexture(texture);
	return 0;
}

int SDL_UpdateYUVTexture(SDL_Texture* texture, const SDL_Rect* rect, const Uint8* Yplane, int Ypitch, const Uint8* Uplane, int Upitch, const Uint8* Vplane, int Vpitch)
{
	if (nullptr == texture || !texture->mIsYUV || nullptr == Yplane || nullptr == Uplane || nullptr == Vplane)
		return SDL_InvalidParamError("texture");

	SDL_Rect area = { 0, 0, texture->mWidth, texture->mHeight };
	if (nullptr != rect)
		area = *rect;

	const int index = 1 - texture->mCurrent;
	IDirect3DTexture9* d3dTexture = texture->mTextures[index];
	d3dTexture->BlockUntilNotBusy();
	D3DLOCKED_RECT lockedRect;
	if (FAILED(d3dTexture->LockRect(0, &lockedRect, nullptr, 0)))
		return SDL_SetError("LockRect failed");

	// BT.601 limited range YUV 4:2:0 -> X8R8G8B8 (native uint32 0xFFRRGGBB)
	for (int y = 0; y < area.h; ++y)
	{
		Uint32* destination = reinterpret_cast<Uint32*>(static_cast<Uint8*>(lockedRect.pBits) + (size_t)(area.y + y) * lockedRect.Pitch) + area.x;
		const Uint8* rowY = Yplane + (size_t)y * Ypitch;
		const Uint8* rowU = Uplane + (size_t)(y / 2) * Upitch;
		const Uint8* rowV = Vplane + (size_t)(y / 2) * Vpitch;
		for (int x = 0; x < area.w; ++x)
		{
			const int c = ((int)rowY[x] - 16) * 298;
			const int d = (int)rowU[x / 2] - 128;
			const int e = (int)rowV[x / 2] - 128;
			const Uint8 r = clampByte((c + 409 * e + 128) >> 8);
			const Uint8 g = clampByte((c - 100 * d - 208 * e + 128) >> 8);
			const Uint8 b = clampByte((c + 516 * d + 128) >> 8);
			destination[x] = 0xFF000000u | ((Uint32)r << 16) | ((Uint32)g << 8) | b;
		}
	}
	d3dTexture->UnlockRect(0);
	texture->mCurrent = index;
	return 0;
}

}
