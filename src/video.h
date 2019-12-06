#pragma once

#include <stdint.h>
#include <stdlib.h>


extern uint32_t color_format;
extern bool isOpenGL;
extern int GLContextMajor;
extern int GLContextMinor;
extern int hasStencil;
extern bool screenshot_requested;

void video_configure(const struct retro_game_geometry* geom);
void video_deinit();
uintptr_t core_video_get_current_framebuffer();
void core_video_refresh(const void * data, unsigned width, unsigned height, size_t pitch);

