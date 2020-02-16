#include <algorithm>

#include "all.h"
#include "../3rdParty/Storm/Source/storm.h"
#include "display.h"
#include <SDL.h>

namespace dvl {

int sgdwLockCount;
BYTE *gpBuffer;
#ifdef _DEBUG
int locktbl[256];
#endif
static CCritSect sgMemCrit;
HMODULE ghDiabMod;

int refreshDelay;
SDL_Window *window;
SDL_Renderer *renderer;
SDL_Texture *texture;

/** Currently active palette */
SDL_Palette *palette;
unsigned int pal_surface_palette_version = 0;

/** 24-bit renderer texture surface */
SDL_Surface *renderer_texture_surface = nullptr;

/** 8-bit surface wrapper around #gpBuffer */
SDL_Surface *pal_surface;

#ifdef PIXEL_LIGHT
struct POINT {
	int x, y;
	POINT(){}
	POINT(int x1, int y1): x(x1), y(y1){}
};

SDL_Surface *tmp_surface;
SDL_Surface *ui_surface;
const int num_ellipses = 15;
POINT eliSizes[num_ellipses];
SDL_Texture *ellipsesTextures[num_ellipses];
int lightReady = 0;

void prepareLightColors()
{
	int orange = 0xff9900;
	int darkorange = 0xcc0000;
	int blue = 0x0000ff;
	int darkblue = 0x000099;
	int green = 0x00ff00;
	int red = 0xff0000;
	int white = 0xffffff;
	int lime = 0xbfff00;

	//others
	lightColorMap["PLAYERLIGHT"] = white;
	lightColorMap["TRAPLIGHT"] = green;
	lightColorMap["LIGHTNINGARROW"] = blue;
	lightColorMap["FIREARROW"] = darkorange;
	lightColorMap["MAGMABALL"] = green;
	lightColorMap["BLOODSTAR_RED"] = red;
	lightColorMap["BLOODSTAR_BLUE"] = darkblue;
	lightColorMap["BLOODSTAR_YELLOW"] = lime;
	lightColorMap["ACIDMISSILE"] = lime;
	lightColorMap["ACIDPUDDLE"] = lime;
	lightColorMap["DIABLODEATH"] = red;
	lightColorMap["UNIQUEMONSTER"] = green;
	lightColorMap["DEADUNIQUEMONSTER"] = white;
	lightColorMap["REDPORTAL"] = red;
	lightColorMap["STATICLIGHT"] = darkorange;

	//spells
	lightColorMap["FIREBOLT"] = darkorange;
	lightColorMap["LIGHTNING"] = blue;
	lightColorMap["FLASH"] = blue;
	lightColorMap["FIREWALL"] = red;
	lightColorMap["TOWNPORTAL"] = blue;
	lightColorMap["FIREBALL"] = orange;
	lightColorMap["GUARDIAN"] = green;
	lightColorMap["FLAMEWAVE"] = red;
	lightColorMap["NOVA"] = blue;
	lightColorMap["INFERNO"] = red;
	lightColorMap["APOCALYPSE"] = darkorange;
	lightColorMap["ELEMENTAL"] = darkorange;
	lightColorMap["CHARGEDBOLT"] = darkblue;
	lightColorMap["HOLYBOLT"] = blue;
	lightColorMap["BONESPIRIT"] = green;
}
#endif

static void dx_create_back_buffer()
{
	pal_surface = SDL_CreateRGBSurfaceWithFormat(0, BUFFER_WIDTH, BUFFER_HEIGHT, 8, SDL_PIXELFORMAT_INDEX8);
	if (pal_surface == NULL) {
		ErrSdl();
	}

	gpBuffer = (BYTE *)pal_surface->pixels;

#ifndef USE_SDL1
	// In SDL2, `pal_surface` points to the global `palette`.
	if (SDL_SetSurfacePalette(pal_surface, palette) < 0)
		ErrSdl();
#else
	// In SDL1, `pal_surface` owns its palette and we must update it every
	// time the global `palette` is changed. No need to do anything here as
	// the global `palette` doesn't have any colors set yet.
#endif

#ifdef PIXEL_LIGHT
	prepareLightColors();
	ui_surface = SDL_CreateRGBSurfaceWithFormat(0, BUFFER_WIDTH, BUFFER_HEIGHT, 8, SDL_PIXELFORMAT_INDEX8);
	if (ui_surface == NULL)
		ErrSdl();

	if (SDL_SetSurfacePalette(ui_surface, palette) < 0)
		ErrSdl();

	tmp_surface = SDL_CreateRGBSurfaceWithFormat(0, BUFFER_WIDTH, BUFFER_HEIGHT, 8, SDL_PIXELFORMAT_INDEX8);
	if (tmp_surface == NULL)
		ErrSdl();

	if (SDL_SetSurfacePalette(tmp_surface, palette) < 0)
		ErrSdl();
#endif
	pal_surface_palette_version = 1;
}

static void dx_create_primary_surface()
{
#ifndef USE_SDL1
	if (renderer) {
		int width, height;
		SDL_RenderGetLogicalSize(renderer, &width, &height);
		Uint32 format;
		if (SDL_QueryTexture(texture, &format, nullptr, nullptr, nullptr) < 0)
			ErrSdl();
		renderer_texture_surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, SDL_BITSPERPIXEL(format), format);
	}
#endif
	if (GetOutputSurface() == nullptr) {
		ErrSdl();
	}
}

void dx_init(HWND hWnd)
{
	SDL_RaiseWindow(window);
	SDL_ShowWindow(window);

	dx_create_primary_surface();
	palette_init();
	dx_create_back_buffer();
}
static void lock_buf_priv()
{
	sgMemCrit.Enter();
	if (sgdwLockCount != 0) {
		sgdwLockCount++;
		return;
	}

	gpBufEnd += (uintptr_t)(BYTE *)pal_surface->pixels;
	gpBuffer = (BYTE *)pal_surface->pixels;
	sgdwLockCount++;
}

void lock_buf(BYTE idx)
{
#ifdef _DEBUG
	locktbl[idx]++;
#endif
	lock_buf_priv();
}

static void unlock_buf_priv()
{
	if (sgdwLockCount == 0)
		app_fatal("draw main unlock error");
	if (!gpBuffer)
		app_fatal("draw consistency error");

	sgdwLockCount--;
	if (sgdwLockCount == 0) {
		gpBufEnd -= (uintptr_t)gpBuffer;
		//gpBuffer = NULL; unable to return to menu
	}
	sgMemCrit.Leave();
}

void unlock_buf(BYTE idx)
{
#ifdef _DEBUG
	if (!locktbl[idx])
		app_fatal("Draw lock underflow: 0x%x", idx);
	locktbl[idx]--;
#endif
	unlock_buf_priv();
}

void dx_cleanup()
{
	if (ghMainWnd)
		SDL_HideWindow(window);
	sgMemCrit.Enter();
	sgdwLockCount = 0;
	gpBuffer = NULL;
	sgMemCrit.Leave();

	if (pal_surface == nullptr)
		return;
	SDL_FreeSurface(pal_surface);
	pal_surface = nullptr;
	SDL_FreePalette(palette);
	SDL_FreeSurface(renderer_texture_surface);
	SDL_DestroyTexture(texture);
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
#ifdef PIXEL_LIGHT
	SDL_FreeSurface(tmp_surface);
	SDL_FreeSurface(ui_surface);
	for (int i = 0; i < num_ellipses; i++) {
		SDL_DestroyTexture(ellipsesTextures[i]);
	}
	lightReady = 0;
#endif
}

void dx_reinit()
{
#ifdef USE_SDL1
	window = SDL_SetVideoMode(0, 0, 0, window->flags ^ SDL_FULLSCREEN);
	if (window == NULL) {
		ErrSdl();
	}
#else
	Uint32 flags = 0;
	if (!fullscreen) {
		flags = renderer ? SDL_WINDOW_FULLSCREEN_DESKTOP : SDL_WINDOW_FULLSCREEN;
	}
	if (SDL_SetWindowFullscreen(window, flags)) {
		ErrSdl();
	}
#endif
	fullscreen = !fullscreen;
	force_redraw = 255;
}

void CreatePalette()
{
	palette = SDL_AllocPalette(256);
	if (palette == NULL) {
		ErrSdl();
	}
}

void BltFast(SDL_Rect *src_rect, SDL_Rect *dst_rect)
{
	Blit(pal_surface, src_rect, dst_rect);
}

void Blit(SDL_Surface *src, SDL_Rect *src_rect, SDL_Rect *dst_rect)
{
	SDL_Surface *dst = GetOutputSurface();
#ifndef USE_SDL1
	if (SDL_BlitSurface(src, src_rect, dst, dst_rect) < 0)
			ErrSdl();
		return;
#else
	if (!OutputRequiresScaling()) {
		if (SDL_BlitSurface(src, src_rect, dst, dst_rect) < 0)
			ErrSdl();
		return;
	}

	SDL_Rect scaled_dst_rect;
	if (dst_rect != nullptr) {
		scaled_dst_rect = *dst_rect;
		ScaleOutputRect(&scaled_dst_rect);
		dst_rect = &scaled_dst_rect;
	}

	// Same pixel format: We can call BlitScaled directly.
	if (SDLBackport_PixelFormatFormatEq(src->format, dst->format)) {
		if (SDL_BlitScaled(src, src_rect, dst, dst_rect) < 0)
			ErrSdl();
		return;
	}

	// If the surface has a color key, we must stretch first and can then call BlitSurface.
	if (SDL_HasColorKey(src)) {
		SDL_Surface *stretched = SDL_CreateRGBSurface(SDL_SWSURFACE, dst_rect->w, dst_rect->h, src->format->BitsPerPixel,
		    src->format->Rmask, src->format->Gmask, src->format->BitsPerPixel, src->format->Amask);
		SDL_SetColorKey(stretched, SDL_SRCCOLORKEY, src->format->colorkey);
		if (src->format->palette != nullptr)
			SDL_SetPalette(stretched, SDL_LOGPAL, src->format->palette->colors, 0, src->format->palette->ncolors);
		SDL_Rect stretched_rect = { 0, 0, dst_rect->w, dst_rect->h };
		if (SDL_SoftStretch(src, src_rect, stretched, &stretched_rect) < 0
		    || SDL_BlitSurface(stretched, &stretched_rect, dst, dst_rect) < 0) {
			SDL_FreeSurface(stretched);
			ErrSdl();
		}
		SDL_FreeSurface(stretched);
		return;
	}

	// A surface with a non-output pixel format but without a color key needs scaling.
	// We can convert the format and then call BlitScaled.
	SDL_Surface *converted = SDL_ConvertSurface(src, dst->format, 0);
	if (SDL_BlitScaled(converted, src_rect, dst, dst_rect) < 0) {
		SDL_FreeSurface(converted);
		ErrSdl();
	}
	SDL_FreeSurface(converted);
#endif
}

/**
 * @brief Limit FPS to avoid high CPU load, use when v-sync isn't available
 */
void LimitFrameRate()
{
	static uint32_t frameDeadline;
	uint32_t tc = SDL_GetTicks() * 1000;
	uint32_t v = 0;
	if (frameDeadline > tc) {
		v = tc % refreshDelay;
		SDL_Delay(v / 1000 + 1); // ceil
	}
	frameDeadline = tc + v + refreshDelay;
}

#ifdef PIXEL_LIGHT
int width, height;
Uint32 format;

void PutPixel32_nolock(SDL_Surface *surface, int x, int y, Uint32 color)
{
	Uint8 *pixel = (Uint8 *)surface->pixels;
	pixel += (y * surface->pitch) + (x * sizeof(Uint32));
	*((Uint32 *)pixel) = color;
}

POINT gameToScreen(int targetRow, int targetCol)
{
	int playerRow = plr[myplr].WorldX;
	int playerCol = plr[myplr].WorldY;
	int sx = TILE_SIZE * (targetRow - playerRow) + TILE_SIZE * (playerCol - targetCol) + SCREEN_WIDTH / 2;
	if (ScrollInfo._sdir == SDIR_E)
		sx -= TILE_SIZE;
	else if (ScrollInfo._sdir == SDIR_W)
		sx += TILE_SIZE;

	int sy = TILE_SIZE * (targetCol - playerCol) + sx / 2;
	if (ScrollInfo._sdir == SDIR_W)
		sy -= TILE_SIZE;
	sy += TILE_SIZE / 2;

	return POINT(sx, sy);
}

int mergeChannel(int a, int b, float amount)
{
	float result = (a * amount) + (b * (1 - amount));
	return (int)result;
}

Uint32 blendColors(Uint32 c1, Uint32 c2, float howmuch)
{
	int r = mergeChannel(c1 & 0x0000FF, c2 & 0x0000FF, howmuch);
	int g = mergeChannel((c1 & 0x00FF00) >> 8, (c2 & 0x00FF00) >> 8, howmuch);
	int b = mergeChannel((c1 & 0xFF0000) >> 16, (c2 & 0xFF0000) >> 16, howmuch);
	return r + (g << 8) + (b << 16);
}

void drawRadius(int lid, int row, int col, int radius, int color, int xoff, int yoff)
{
	xoff = 0;
	yoff = 0;
	bool isMis = false;

	if (lid != -1) {
		for (int i = 0; i < nummissiles; i++) {
			MissileStruct *mis = &missile[missileactive[i]];
			if (mis->_mlid == lid) {
				xoff = mis->_mixoff;
				yoff = mis->_miyoff;
				isMis = true;
				break;
			}
		}
		if (!isMis) {
			for (int i = 0; i < nummonsters; i++) {
				MonsterStruct *mon = &monster[monstactive[i]];
				if (mon->mlid == lid) {
					xoff = mon->_mxoff;
					yoff = mon->_myoff;
					if (abs(mon->_mx - row) >= 2 || abs(mon->_my - col) >= 2) {
						row = mon->_mx;
						col = mon->_my;
					}
					break;
				}
			}
		}
	}

	if (lid != plr[myplr]._plid) {
		xoff -= plr[myplr]._pxoff;
		yoff -= plr[myplr]._pyoff;
	}

	POINT pos = gameToScreen(row, col);
	int sx = pos.x;
	int sy = pos.y;

	sx += xoff;
	sy += yoff;

	int width = eliSizes[radius - 1].x;
	int height = eliSizes[radius - 1].y;
	int srcx = width / 2;
	int srcy = height / 2;
	int offsetx = sx - srcx;
	int offsety = sy - srcy;
	if (offsetx > (SCREEN_WIDTH + width) || offsety > (SCREEN_HEIGHT + height) || offsetx < (-width) || offsety < (-height)) {
		return;
	}

	SDL_Rect rect;
	rect.x = offsetx;
	rect.y = offsety;
	rect.w = width;
	rect.h = height;

	Uint8 r = (color & 0xFF0000) >> 16;
	Uint8 g = (color & 0x00FF00) >> 8;
	Uint8 b = (color & 0x0000FF);
	if (SDL_SetTextureColorMod(ellipsesTextures[radius - 1], r, g, b) < 0)
		ErrSdl();
	if (SDL_RenderCopy(renderer, ellipsesTextures[radius - 1], NULL, &rect) < 0)
		ErrSdl();
}

void lightLoop()
{
	for (int i = 0; i < numlights; i++) {
		int lid = lightactive[i];
		drawRadius(lid, LightList[lid]._lx, LightList[lid]._ly, LightList[lid]._lradius, LightList[lid]._lcolor, LightList[lid]._xoff, LightList[lid]._yoff);
	}

	for (unsigned int i = 0; i < staticLights[currlevel + setlvlnum * (32 * setlevel)].size(); i++) {
		LightListStruct *it = &staticLights[currlevel + setlvlnum * (32 * setlevel)][i];
		drawRadius(-1, it->_lx, it->_ly, it->_lradius, it->_lcolor, 0, 0);
	}
}

POINT predrawEllipse(SDL_Surface* eli, int radius, bool test, int width, int height)
{
	int sx = width / 2;
	int sy = height / 2;
	int hey = radius * 16;
	int maxx = 0, maxy = 0;
	for (int x = 0; x < width; x++) {
		for (int y = 0; y < height; y++) {
			//if (x >= 0 && x < SCREEN_WIDTH && y >= 0 && y < SCREEN_HEIGHT) {
			float howmuch;
			float diffx = sx - x;
			float diffy = sy - y;
			float sa = diffx / 32;
			float a = sa * sa;
			float sb = diffy / 16;
			float b = sb * sb;
			float c = hey;
			float ab = a + b;
			if (ab <= c) {
				howmuch = cbrt(ab / c);
				if (test) {
					if (diffx > maxx) {
						maxx = diffx;
					}
					if (diffy > maxy) {
						maxy = diffy;
					}
				} else {
					PutPixel32_nolock(eli, x, y, blendColors(0x000000, 0xFFFFFF, howmuch));
				}
			}
			//}
		}
	}
	return POINT(maxx * 2, maxy * 2);
}

void prepareLight()
{
	SDL_RenderGetLogicalSize(renderer, &width, &height);
	if (SDL_QueryTexture(texture, &format, nullptr, nullptr, nullptr) < 0)
		ErrSdl();
	for (int i = 0; i < num_ellipses; i++) {
		eliSizes[i] = predrawEllipse(NULL, i + 1, true, 2048, 2048);
		SDL_Surface* tmpEllipse = SDL_CreateRGBSurfaceWithFormat(0, eliSizes[i].x, eliSizes[i].y, SDL_BITSPERPIXEL(format), format);
		if (tmpEllipse == NULL)
			ErrSdl();
		if (SDL_FillRect(tmpEllipse, NULL, SDL_MapRGB(tmpEllipse->format, 0, 0, 0)) < 0)
			ErrSdl();
		predrawEllipse(tmpEllipse, i + 1, false, eliSizes[i].x, eliSizes[i].y);
		ellipsesTextures[i] = SDL_CreateTextureFromSurface(renderer, tmpEllipse);
		if (ellipsesTextures[i] == NULL)
			ErrSdl();
		SDL_FreeSurface(tmpEllipse);
		if (SDL_SetTextureBlendMode(ellipsesTextures[i], SDL_BLENDMODE_ADD) < 0)
			ErrSdl();
	}
}

#endif
void RenderPresent()
{
	SDL_Surface *surface = GetOutputSurface();
	assert(!SDL_MUSTLOCK(surface));

	if (!gbActive) {
		LimitFrameRate();
		return;
	}

#ifndef USE_SDL1
	if (renderer) {
#ifdef PIXEL_LIGHT
		Uint8 red_r = 255;
		Uint8 red_g = 100;
		Uint8 red_b = 55;
		if (testvar3 != 0 && leveltype != DTYPE_TOWN && (redrawLights == 1 || (testvar1 == 1 && redrawLights != -1))) {
			if (lightReady != 1) {
				lightReady = 1;
				prepareLight();
			}
			SDL_BlendMode bm = SDL_BLENDMODE_NONE;
			switch (testvar5) {
			case 1:
				bm = SDL_BLENDMODE_BLEND;
				break;
			case 2:
				bm = SDL_BLENDMODE_ADD;
				break;
			case 3:
				bm = SDL_BLENDMODE_MOD;
				break;
			}
			if (SDL_SetTextureBlendMode(texture, bm) < 0)
				ErrSdl();
		} else {
			if (SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE) < 0)
				ErrSdl();
		}
#endif

		if (SDL_UpdateTexture(texture, NULL, surface->pixels, surface->pitch) < 0) //pitch is 2560
			ErrSdl();

		// Clear buffer to avoid artifacts in case the window was resized
		if (SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255) < 0) // TODO only do this if window was resized
			ErrSdl();

		if (SDL_RenderClear(renderer) < 0)
			ErrSdl();

#ifdef PIXEL_LIGHT
		if (testvar3 != 0 && leveltype != DTYPE_TOWN && (redrawLights == 1 || (testvar1 == 1 && redrawLights != -1))) {
			lightLoop();
		}
		if (drawRed) {
			if (SDL_SetTextureColorMod(texture, red_r, red_g, red_b) < 0)
				ErrSdl();
		}

#endif
		if (SDL_RenderCopy(renderer, texture, NULL, NULL) < 0)
			ErrSdl();
#ifdef PIXEL_LIGHT
		int tmpRed = drawRed;
		if (drawRed) {
			if (SDL_SetTextureColorMod(texture, 255, 255, 255) < 0)
				ErrSdl();
			drawRed = false;
		}
		if (testvar3 != 0 && leveltype != DTYPE_TOWN && (redrawLights == 1 || (testvar1 == 1 && redrawLights != -1))) {
			//Setting the color key here because it might change each frame during fadein/fadeout which modify palette
			if (SDL_SetColorKey(ui_surface, SDL_TRUE, PALETTE_TRANSPARENT_COLOR) < 0)
				ErrSdl();
			// Convert from 8-bit to 24-bit
			SDL_Surface *tmp = SDL_ConvertSurface(ui_surface, renderer_texture_surface->format, 0);
			if (tmp == NULL)
				ErrSdl();
			SDL_Texture *ui_texture = SDL_CreateTextureFromSurface(renderer, tmp);
			if (ui_texture == NULL)
				ErrSdl();
			if (SDL_SetTextureBlendMode(ui_texture, SDL_BLENDMODE_BLEND) < 0)
				ErrSdl();
			SDL_Rect rect;
			rect.x = BORDER_LEFT;
			rect.y = BORDER_TOP;
			rect.w = SCREEN_WIDTH;
			rect.h = SCREEN_HEIGHT;
			if (tmpRed) {
				if (SDL_SetTextureColorMod(ui_texture, red_r, red_g, red_b) < 0)
					ErrSdl();
			}
			if (SDL_RenderCopy(renderer, ui_texture, &rect, NULL) > 0)
				ErrSdl();
			if (tmpRed) {
				if (SDL_SetTextureColorMod(ui_texture, 255, 255, 255) < 0)
					ErrSdl();
			}
			if (SDL_SetColorKey(ui_surface, SDL_FALSE, PALETTE_TRANSPARENT_COLOR) < 0)
				ErrSdl();
			SDL_DestroyTexture(ui_texture);
			SDL_FreeSurface(tmp);
			if (testvar1 != 1)
				redrawLights = 0;
		}
#endif
		SDL_RenderPresent(renderer);
	} else {
		if (SDL_UpdateWindowSurface(window) < 0)
			ErrSdl();
		LimitFrameRate();
	}
#else
	if (SDL_Flip(surface) < 0)
		ErrSdl();

	LimitFrameRate();
#endif
}

void PaletteGetEntries(DWORD dwNumEntries, SDL_Color *lpEntries)
{
	for (DWORD i = 0; i < dwNumEntries; i++) {
		lpEntries[i] = system_palette[i];
	}
}
} // namespace dvl
