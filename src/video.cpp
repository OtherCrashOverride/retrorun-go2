/*
retrorun-gou - libretro frontend for the ODROID-GO Advance
Copyright (C) 2020  OtherCrashOverride

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include "video.h"

#include "libretro.h"
#include "globals.h"

#include <stdlib.h>
#include <stdio.h>
#include <exception>
#include <string.h>

#include <gou/display.h>
#include <gou/context3d.h>

#define EGL_EGLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <drm/drm_fourcc.h>

#define FBO_DIRECT 1
#define ALIGN(val, align)	(((val) + (align) - 1) & ~((align) - 1))

extern float opt_aspect;
extern int opt_backlight;

gou_display_t* display;
gou_surface_t* surface;
gou_surface_t* display_surface;
gou_context3d_t* context3D;
float aspect_ratio;
uint32_t color_format;

bool isOpenGL = false;
int GLContextMajor = 0;
int GLContextMinor = 0;
GLuint fbo;
int hasStencil = false;
bool screenshot_requested = false;
int prevBacklight;

extern retro_hw_context_reset_t retro_context_reset;


void video_configure(const struct retro_game_geometry* geom)
{
	printf("video_configure: base_width=%d, base_height=%d, max_width=%d, max_height=%d, aspect_ratio=%f\n",
        geom->base_width, geom->base_height,
        geom->max_width, geom->max_height,
        geom->aspect_ratio);

    
    display = gou_display_create();
    gou_display_background_color_set(display, 0xff080808);  // ABGR


    if (opt_backlight > -1)
    {
        //gou_display_backlight_set(display, (uint32_t)opt_backlight);
    }
    else
    {
        //opt_backlight = gou_display_backlight_get(display);
    }
    prevBacklight = opt_backlight;    


    aspect_ratio = opt_aspect == 0.0f ? geom->aspect_ratio : opt_aspect;

    if (isOpenGL)
    {        
        gou_context3d_attributes_t attr;
        attr.major = 3;
        attr.minor = 2;
        attr.red_bits = 8;
        attr.green_bits = 8;
        attr.blue_bits = 8;
        attr.alpha_bits = 0;
        attr.depth_bits = 24;
        attr.stencil_bits = 8;

        //context3D = gou_context_create(display, geom->base_width, geom->base_height, &attr);
        context3D = gou_context3d_create(display, geom->max_width, geom->max_height, &attr);
        gou_context3d_make_current(context3D);


        retro_context_reset();
    }
    else
    {
        if (surface) abort();

        int aw = ALIGN(geom->max_width, 32);
        int ah = ALIGN(geom->max_height, 32);
        printf ("video_configure: aw=%d, ah=%d\n", aw, ah);

        if (color_format == DRM_FORMAT_RGBA5551)
        {
            surface = gou_surface_create(display, aw, ah, DRM_FORMAT_RGB565);
        }
        else
        {
            surface = gou_surface_create(display, aw, ah, color_format);
        }

        if (!surface)
        {
            printf("gou_surface_create failed.\n");
            throw std::exception();
        }
        

        
        //printf("video_configure: rect=%d, %d, %d, %d\n", y, x, h, w);
    }
}

void video_deinit()
{

}


uintptr_t core_video_get_current_framebuffer()
{
    //printf("core_video_get_current_framebuffer\n");
    return gou_context3d_fbo_get(context3D);
}

void core_video_refresh(const void * data, unsigned width, unsigned height, size_t pitch)
{
    //printf("core_video_refresh: data=%p, width=%d, height=%d, pitch=%d\n", data, width, height, pitch);

    if (opt_backlight != prevBacklight)
    {
        //gou_display_backlight_set(display, (uint32_t)opt_backlight);
        prevBacklight = opt_backlight;

        //printf("Backlight = %d\n", opt_backlight);
    }


    int x;
    int y;
    int w;
    int h;
    //gou_rotation_t rotation = opt_portrait ? gou_ROTATION_DEGREES_180 : gou_ROTATION_DEGREES_270;

    if (aspect_ratio >= 1.0f)
    {
        h = gou_display_height_get(display);
        
        w = h * aspect_ratio;
        w = (w > gou_display_width_get(display)) ? gou_display_width_get(display) : w;

        x = (gou_display_width_get(display) / 2) - (w / 2);
        y = 0;

        //printf("x=%d, y=%d, w=%d, h=%d\n", x, y, w, h);
    }
    else
    {
        x = 0;
        y = 0;
        w = gou_display_width_get(display);
        h = gou_display_height_get(display);
    }


    if (isOpenGL)
    {
        if (data != RETRO_HW_FRAME_BUFFER_VALID) return;
        
        // Swap
        //gou_context3d_swap_buffers(context3D);

        gou_surface_t* gles_surface = gou_context3d_surface_lock(context3D);

        int ss_w = gou_surface_width_get(gles_surface);
        int ss_h = gou_surface_height_get(gles_surface);

        gou_display_present(display,
            gles_surface,
            0, ss_h - height, width, height, false, true,
            y, x, h, w);

        gou_context3d_surface_unlock(context3D, gles_surface);
    }
    else
    {
        if (!data) return;

        uint8_t* src = (uint8_t*)data;
        uint8_t* dst = (uint8_t*)gou_surface_map(surface);
        int bpp = gou_drm_format_get_bpp(gou_surface_format_get(surface)) / 8;

        int yy = height;
        while(yy > 0)
        {
            if (color_format == DRM_FORMAT_RGBA5551)
            {
                // uint16_t* src2 = (uint16_t*)src;
                // uint16_t* dst2 = (uint16_t*)dst;

                uint32_t* src2 = (uint32_t*)src;
                uint32_t* dst2 = (uint32_t*)dst;

                for (int x = 0; x < width / 2; ++x)
                {
                    // uint16_t pixel = src2[x];
                    // pixel = (pixel << 1) & (~0x1f) | pixel & 0x1f;
                    // dst2[x] = pixel;

                    uint32_t pixel = src2[x];
                    pixel = ((pixel << 1) & (~0x3f003f)) | (pixel & 0x1f001f);
                    dst2[x] = pixel;
                }
            }
            else
            {
                memcpy(dst, src, width * bpp);
            }
            
            src += pitch;
            dst += gou_surface_stride_get(surface);
            
            --yy;
        }

        // if (screenshot_requested)
        // {
        //     printf("Screenshot.\n");

        //     int ss_w = gou_surface_width_get(surface);
        //     int ss_h = gou_surface_height_get(surface);
        //     gou_surface_t* screenshot = gou_surface_create(display, ss_w, ss_h, DRM_FORMAT_RGB888);
        //     if (!screenshot)
        //     {
        //         printf("gou_surface_create failed.\n");
        //         throw std::exception();
        //     }

        //     gou_surface_blit(surface, 0, 0, ss_w, ss_h,
        //                      screenshot, 0, 0, ss_w, ss_h,
        //                      gou_ROTATION_DEGREES_0);

        //     gou_surface_save_as_png(screenshot, "ScreenShot.png");

        //     gou_surface_destroy(screenshot);

        //     screenshot_requested = false;
        // }

        gou_display_present(display,
            surface,
            0, 0, width, height, false, false,
            x, y, w, h);
    }
}
