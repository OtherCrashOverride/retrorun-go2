/*
retrorun-go2 - libretro frontend for the ODROID-GO Advance
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

#include "globals.h"
#include "video.h"
#include "audio.h"
#include "input.h"

#include <unistd.h>

//#include <go2/queue.h>

#include <linux/dma-buf.h>
#include <sys/ioctl.h>

#include "libretro.h"
#include <dlfcn.h>
#include <cstdarg>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <exception>
#include <getopt.h>
#include <map>
#include <vector>

#define EGL_EGLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <drm/drm_fourcc.h>
#include <sys/time.h>
#include <go2/input.h>


#define RETRO_DEVICE_ATARI_JOYSTICK RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_JOYPAD, 1)
#define RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER 56
                                           /* unsigned * --
                                            *
                                            * Allows an implementation to ask frontend preferred hardware
                                            * context to use. Core should use this information to deal
                                            * with what specific context to request with SET_HW_RENDER.
                                            *
                                            * 'data' points to an unsigned variable
                                            */

extern go2_battery_state_t batteryState;


retro_hw_context_reset_t retro_context_reset;

const char* opt_savedir = ".";
const char* opt_systemdir = ".";
float opt_aspect = 0.0f;
int opt_backlight = -1;
int opt_volume = -1;
bool opt_restart = false;
const char* arg_core = "";
const char* arg_rom = "";
bool opt_portrait = false;

typedef std::map<std::string, std::string> varmap_t ;
varmap_t variables;

struct option longopts[] = {
	{ "savedir", required_argument, NULL, 's' },
    { "systemdir", required_argument, NULL, 'd' },
    { "aspect", required_argument, NULL, 'a' },
    { "backlight", required_argument, NULL, 'b' },
    { "volume", required_argument, NULL, 'v' },
    { "restart", no_argument, NULL, 'r' },
    { "triggers", no_argument, NULL, 't' },
    { "analog", no_argument, NULL, 'n' },
    { "portrait", no_argument, NULL, 'p' },
    { 0, 0, 0, 0 }};


static struct {
	void * handle;
	bool initialized;

	void (*retro_init)(void);
	void (*retro_deinit)(void);
	unsigned (*retro_api_version)(void);
	void (*retro_get_system_info)(struct retro_system_info * info);
	void (*retro_get_system_av_info)(struct retro_system_av_info * info);
	void (*retro_set_controller_port_device)(unsigned port, unsigned device);
	void (*retro_reset)(void);
	void (*retro_run)(void);
	size_t (*retro_serialize_size)(void);
	bool (*retro_serialize)(void *data, size_t size);
	bool (*retro_unserialize)(const void *data, size_t size);
	//	void retro_cheat_reset(void);
	//	void retro_cheat_set(unsigned index, bool enabled, const char *code);
	bool (*retro_load_game)(const struct retro_game_info * game);
	//	bool retro_load_game_special(unsigned game_type, const struct retro_game_info *info, size_t num_info);
	void (*retro_unload_game)(void);
	//	unsigned retro_get_region(void);
	void* (*retro_get_memory_data)(unsigned id);
	size_t (*retro_get_memory_size)(unsigned id);
} g_retro;


#define load_sym(V, S) do {\
        if (!((*(void**)&V) = dlsym(g_retro.handle, #S))) \
        { \
            printf("[noarch] Failed to load symbol '" #S "'': %s", dlerror()); \
            abort(); \
        } \
	} while (0)

#define load_retro_sym(S) load_sym(g_retro.S, S)


static void core_log(enum retro_log_level level, const char* fmt, ...)
{
	char buffer[4096] = {
		0
	};
	
    static const char * levelstr[] = {
		"dbg",
		"inf",
		"wrn",
		"err"
	};
	
    va_list va;

	va_start(va, fmt);
	vsnprintf(buffer, sizeof(buffer), fmt, va);
	va_end(va);

	if (level == 0)
		return;

	fprintf(stdout, "[%s] %s", levelstr[level], buffer);
	fflush(stdout);

#if 0
	if (level == RETRO_LOG_ERROR)
		exit(EXIT_FAILURE);
#endif
}

static __eglMustCastToProperFunctionPointerType get_proc_address(const char* sym)
{
    __eglMustCastToProperFunctionPointerType result = eglGetProcAddress(sym);
    //printf("get_proc_address: sym='%s', result=%p\n", sym, (void*)result);

    return result;
}

static bool core_environment(unsigned cmd, void* data)
{
	bool* bval;

	switch (cmd)
    {
        case RETRO_ENVIRONMENT_GET_FASTFORWARDING:
            bval = (bool*)data;
            *bval = false;
            return true;

        case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        {
            struct retro_log_callback * cb = (struct retro_log_callback * ) data;
            cb->log = core_log;
            break;
        }

        case RETRO_ENVIRONMENT_GET_CAN_DUPE:
            bval = (bool*)data;
            *bval = true;
            break;

        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        {
            const enum retro_pixel_format fmt = *(enum retro_pixel_format *)data;
            printf("RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: fmt=0x%x\n", (int)fmt);

            switch (fmt)
            {
            case RETRO_PIXEL_FORMAT_0RGB1555:
                color_format = DRM_FORMAT_RGBA5551;
                break;
                
            case RETRO_PIXEL_FORMAT_RGB565:
                color_format = DRM_FORMAT_RGB565;
                break;
            
            case RETRO_PIXEL_FORMAT_XRGB8888:
                color_format = DRM_FORMAT_XRGB8888;
                break;

            default:
                return false;
            }

            return true;
        }

        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
            *(const char**)data = opt_systemdir;
            return true;

        case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
            *(const char**)data = opt_savedir;
            return true;

        case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER:
        {
            unsigned int* preferred = (unsigned int*)data;
            *preferred = RETRO_HW_CONTEXT_OPENGLES3;
            return true;
        }

        case RETRO_ENVIRONMENT_SET_HW_RENDER:
        {
            retro_hw_render_callback* hw = (retro_hw_render_callback*)data;

            printf("RETRO_ENVIRONMENT_SET_HW_RENDER: context_type=%d\n", hw->context_type);

            if (hw->context_type != RETRO_HW_CONTEXT_OPENGLES_VERSION &&
                hw->context_type != RETRO_HW_CONTEXT_OPENGLES3 &&
                hw->context_type != RETRO_HW_CONTEXT_OPENGLES2)
            {
                return false;
            }

            
            isOpenGL = true;
            GLContextMajor = hw->version_major;
            GLContextMinor = hw->version_minor;
            retro_context_reset = hw->context_reset;

            hw->get_current_framebuffer = core_video_get_current_framebuffer;
            hw->get_proc_address = (retro_hw_get_proc_address_t)get_proc_address;

            printf("HWRENDER: context_type=%d, major=%d, minor=%d\n",
                hw->context_type, GLContextMajor, GLContextMinor);

            return true;
        }

        case RETRO_ENVIRONMENT_SET_VARIABLES:
        {
            retro_variable* var = (retro_variable*)data;
            while (var->key != NULL)
            {
                std::string key = var->key;

                const char* start = strchr(var->value, ';');
                start += 2;

                std::string value;
                while(*start != '|' && *start != 0)
                {
                    value += *start;
                    ++start;
                }

                variables[key] = value;
                printf("SET_VAR: %s=%s\n", key.c_str(), value.c_str());
                ++var;
            }

            break;
        }

        case RETRO_ENVIRONMENT_GET_VARIABLE:
        {
            retro_variable* var = (retro_variable*)data;
            printf("GET_VAR: %s\n", var->key);

            if (strcmp(var->key, "fbneo-neogeo-mode") == 0)
            {
                var->value = "UNIBIOS";
                return true;
            }
            else if (strcmp(var->key, "atari800_resolution") == 0)
            {
                var->value = "336x240";
                return true;
            }
            else if (strcmp(var->key, "atari800_system") == 0)
            {
                var->value = "5200";
                return true;
            }
            else if (strcmp(var->key, "mgba_sgb_borders") == 0)
            {
                var->value = "OFF";
                return true;
            }
            else if (strcmp(var->key, "mame2003-plus_skip_disclaimer") == 0)
            {
                var->value = "enabled";
                return true;
            }
            // else if (strcmp(var->key, "mame2003-plus_frameskip") == 0)
            // {
            //     var->value = "1";
            //     return true;
            // }
            else if (strcmp(var->key, "duckstation_GPU.Renderer") == 0)
            {
                var->value = "Software";
                return true;
            }
            // else if (strcmp(var->key, "duckstation_CPU.ExecutionMode") == 0)
            // {
            //     var->value = "Recompiler";
            //     return true;
            // }
            else if (strcmp(var->key, "reicast_threaded_rendering") == 0)
            {
                var->value = "enabled";
                return true;
            }
            else if (strcmp(var->key, "reicast_internal_resolution") == 0)
            {
                var->value = "320x240";
                return true;
            }            
            else
            {
                varmap_t::iterator iter = variables.find(var->key);
                if (iter != variables.end())
                {
                    var->value = iter->second.c_str();
                    printf("ENV_VAR (default): %s=%s\n", var->key, var->value);

                    return true;
                }

            }

            return false;
        }

        case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
        {
            unsigned int* options_version = (unsigned int*)data;
            *options_version = 1;
            return true;
        }

        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
        {
            const struct retro_core_option_definition* options = ((const struct retro_core_option_definition *)data);
            int i = 0;
            while (options[i].key != 0)
            {
                std::string key = options[i].key;
                std::string value = options[i].default_value;

                variables[key] = value;

                printf("OPTION: key=%s, value=%s\n", key.c_str(), value.c_str());
                ++i;
            }

            return true;
        }

        default:
            core_log(RETRO_LOG_DEBUG, "Unhandled env #%u", cmd);
            return false;
	}

	return true;
}

static void core_load(const char* sofile)
{
	void (*set_environment)(retro_environment_t) = NULL;
	void (*set_video_refresh)(retro_video_refresh_t) = NULL;
	void (*set_input_poll)(retro_input_poll_t) = NULL;
	void (*set_input_state)(retro_input_state_t) = NULL;
	void (*set_audio_sample)(retro_audio_sample_t) = NULL;
	void (*set_audio_sample_batch)(retro_audio_sample_batch_t) = NULL;

	memset(&g_retro, 0, sizeof(g_retro));
	g_retro.handle = dlopen(sofile, RTLD_LAZY);

	if (!g_retro.handle)
    {        
		printf("Failed to load core: %s\n", dlerror());
        throw std::exception();
    }

	dlerror();

	load_retro_sym(retro_init);
	load_retro_sym(retro_deinit);
	load_retro_sym(retro_api_version);
	load_retro_sym(retro_get_system_info);
	load_retro_sym(retro_get_system_av_info);
	load_retro_sym(retro_set_controller_port_device);
	load_retro_sym(retro_reset);
	load_retro_sym(retro_run);
	load_retro_sym(retro_load_game);
	load_retro_sym(retro_unload_game);
	load_retro_sym(retro_serialize_size);
	load_retro_sym(retro_serialize);
	load_retro_sym(retro_unserialize);
    load_retro_sym(retro_get_memory_data);
    load_retro_sym(retro_get_memory_size);

	load_sym(set_environment, retro_set_environment);
	load_sym(set_video_refresh, retro_set_video_refresh);
	load_sym(set_input_poll, retro_set_input_poll);
	load_sym(set_input_state, retro_set_input_state);
	load_sym(set_audio_sample, retro_set_audio_sample);
	load_sym(set_audio_sample_batch, retro_set_audio_sample_batch);

	set_environment(core_environment);
	set_video_refresh(core_video_refresh);
	set_input_poll(core_input_poll);
	set_input_state(core_input_state);
	set_audio_sample(core_audio_sample);
	set_audio_sample_batch(core_audio_sample_batch);

	g_retro.retro_init();
	g_retro.initialized = true;

	printf("Core loaded\n");

    //g_retro.retro_set_controller_port_device(0, RETRO_DEVICE_JOYPAD);
    g_retro.retro_set_controller_port_device(0, RETRO_DEVICE_ANALOG);

    struct retro_system_info system = {
		0, 0, 0, false, false
	};
    g_retro.retro_get_system_info(&system);
    printf("core_load: library_name='%s'\n", system.library_name);

    if (strcmp(system.library_name, "Atari800") == 0)
    {
        Retrorun_Core = RETRORUN_CORE_ATARI800;
        g_retro.retro_set_controller_port_device(0, RETRO_DEVICE_ATARI_JOYSTICK);
    }
}

static void core_load_game(const char * filename)
{
	struct retro_system_timing timing = {
		60.0f, 10000.0f
	};
	struct retro_game_geometry geom = {
		100, 100, 100, 100, 1.0f
	};
	struct retro_system_av_info av = {
		geom, timing
	};
	struct retro_system_info system = {
		0, 0, 0, false, false
	};
	struct retro_game_info info = {
		filename,
		0,
		0,
		NULL
	};
	FILE * file = fopen(filename, "rb");

	if (!file)
		goto libc_error;

	fseek(file, 0, SEEK_END);
	info.size = ftell(file);
	rewind(file);

	g_retro.retro_get_system_info( & system);

	if (!system.need_fullpath) {
		info.data = malloc(info.size);

		if (!info.data || !fread((void * ) info.data, info.size, 1, file))
			goto libc_error;
	}

	if (!g_retro.retro_load_game( & info))
    {
		printf("The core failed to load the content.\n");
        abort();
    }

	g_retro.retro_get_system_av_info( & av);
	video_configure(&av.geometry);
	audio_init(av.timing.sample_rate);

	return;

	libc_error:
		printf("Failed to load content '%s'\n", filename);
        abort();
}

static void core_unload()
{
	if (g_retro.initialized)
		g_retro.retro_deinit();

	if (g_retro.handle)
		dlclose(g_retro.handle);
}

static const char* FileNameFromPath(const char* fullpath)
{
    // Find last slash
    const char* ptr = strrchr(fullpath,'/');
    if (!ptr)
    {
        ptr = fullpath;
    }
    else
    {
        ++ptr;
    } 

    return ptr;   
}

static char* PathCombine(const char* path, const char* filename)
{
    int len = strlen(path);
    int total_len = len + strlen(filename);

    char* result = NULL;

    if (path[len-1] != '/')
    {
        ++total_len;
        result = (char*)calloc(total_len + 1, 1);
        strcpy(result, path);
        strcat(result, "/");
        strcat(result, filename);
    }
    else
    {
        result = (char*)calloc(total_len + 1, 1);
        strcpy(result, path);
        strcat(result, filename);
    }
    
    return result;
}

static int LoadState(const char* saveName)
{
    FILE* file = fopen(saveName, "rb");
	if (!file)
		return -1;

	fseek(file, 0, SEEK_END);
	long size = ftell(file);
	rewind(file);

    if (size < 1) return -1;

    void* ptr = malloc(size);
    if (!ptr) abort();

    size_t count = fread(ptr, 1, size, file);
    if ((size_t)size != count)
    {
        free(ptr);
        abort();
    }

    fclose(file);

    g_retro.retro_unserialize(ptr, size);
    free(ptr);

    return 0;
}

static int LoadSram(const char* saveName)
{
    FILE* file = fopen(saveName, "rb");
	if (!file)
		return -1;

	fseek(file, 0, SEEK_END);
	long size = ftell(file);
	rewind(file);

    size_t sramSize = g_retro.retro_get_memory_size(0);
    if (size < 1) return -1;
    if (size != sramSize)
    {
        printf("LoadSram: File size mismatch (%d != %d)\n", size, sramSize);
        return -1;
    }

    void* ptr = g_retro.retro_get_memory_data(0);
    if (!ptr) abort();

    size_t count = fread(ptr, 1, size, file);
    if ((size_t)size != count)
    {
        abort();
    }

    fclose(file);

    return 0;
}

static void SaveState(const char* saveName)
{
    size_t size = g_retro.retro_serialize_size();
    
    void* ptr = malloc(size);
    if (!ptr) abort();

    g_retro.retro_serialize(ptr, size);

    FILE* file = fopen(saveName, "wb");
	if (!file)
    {
        free(ptr);
		abort();
    }

    size_t count = fwrite(ptr, 1, size, file);
    if (count != size)
    {
        free(ptr);
        abort();
    }

    fclose(file);
    free(ptr);
}

static void SaveSram(const char* saveName)
{
    size_t size = g_retro.retro_get_memory_size(0);
    if (size < 1) return;
    
    void* ptr = g_retro.retro_get_memory_data(0);
    if (!ptr) abort();

 
    FILE* file = fopen(saveName, "wb");
	if (!file)
    {
		abort();
    }

    size_t count = fwrite(ptr, 1, size, file);
    if (count != size)
    {
        abort();
    }

    fclose(file);
}

int main(int argc, char *argv[])
{
    //printf("argc=%d, argv=%p\n", argc, argv);


    // Init
#if 0

	if (argc < 3)
    {
		printf("Usage: %s <core> <game>", argv[0]);
        exit(1);
    }

	core_load(argv[1]);
    core_load_game(argv[2]);

#else

    int c;
    int option_index = 0;

	while ((c = getopt_long(argc, argv, "s:d:a:b:v:rtn", longopts, &option_index)) != -1)
	{
		switch (c)
		{
			case 's':
				opt_savedir = optarg;
				break;

			case 'd':
				opt_systemdir = optarg;
				break;

			case 'a':
				opt_aspect = atof(optarg);
				break;

            case 'b':
                opt_backlight = atoi(optarg);
                break;
            
            case 'v':
                opt_volume = atoi(optarg);
                break;

            case 'r':
                opt_restart = true;
                break;

            case 't':
                opt_triggers = true;
                break;

            case 'n':
                Retrorun_UseAnalogStick = true;
                break;

            case 'p':
                opt_portrait = true;
                break;
                
			default:
				printf("Unknown option. '%s'\n", longopts[option_index].name);
                exit(EXIT_FAILURE);
		}
	}

    printf("opt_save='%s', opt_systemdir='%s', opt_aspect=%f\n", opt_savedir, opt_systemdir, opt_aspect);


    int remaining_args = argc - optind;
    int remaining_index = optind;
    printf("remaining_args=%d\n", remaining_args);

    if (remaining_args < 2)
    {
		printf("Usage: %s [-s savedir] [-d systemdir] [-a aspect] core rom\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    //return 0;
    if (optind < argc)
    {
        printf ("non-option ARGV-elements: ");
        while (optind < argc)
            printf ("%s ", argv[optind++]);
        putchar ('\n');
    }

    arg_core = argv[remaining_index++];
    arg_rom = argv[remaining_index++];

	core_load(arg_core);
    core_load_game(arg_rom);

#endif

    // Overrides
    printf("Checking overrides.\n");

    input_gamepad_read();
    
    go2_input_state_t* gamepadState =input_gampad_current_get();
    if (go2_input_state_button_get(gamepadState, Go2InputButton_F1) == ButtonState_Pressed)
    {
        printf("Forcing restart due to button press (F1).\n");
        opt_restart = true;
    }
   

    // State
    const char* fileName = FileNameFromPath(arg_rom);
    
    char* saveName = (char*)malloc(strlen(fileName) + 4 + 1);
    strcpy(saveName, fileName);
    strcat(saveName, ".sav");

    char* savePath = PathCombine(opt_savedir, saveName);
    printf("savePath='%s'\n", savePath);
    
    char* sramName = (char*)malloc(strlen(fileName) + 4 + 1);
    strcpy(sramName, fileName);
    strcat(sramName, ".srm");

    char* sramPath = PathCombine(opt_savedir, sramName);
    printf("sramPath='%s'\n", sramPath);


    if (opt_restart)
    {
        printf("Restarting.\n");
    }
    else
    {
        printf("Loading.\n");
        LoadState(savePath);
    }

    LoadSram(sramPath);


    printf("Entering render loop.\n");

    const char* batteryStateDesc[] = { "UNK", "DSC", "CHG", "FUL" };

    struct timeval startTime;
    struct timeval endTime;
    double elapsed = 0;
    int totalFrames = 0;
    bool isRunning = true;
    while(isRunning)
    {
        gettimeofday(&startTime, NULL);

        if (input_exit_requested)
            isRunning = false;

        g_retro.retro_run();
        
        gettimeofday(&endTime, NULL);
        ++totalFrames;

        double seconds = (endTime.tv_sec - startTime.tv_sec);
	    double milliseconds = ((double)(endTime.tv_usec - startTime.tv_usec)) / 1000000.0;

        elapsed += seconds + milliseconds;

        if (elapsed >= 1.0)
        {
            int fps = (int)(totalFrames / elapsed);
            printf("FPS: %i, BATT: %d [%s]\n", fps, batteryState.level, batteryStateDesc[batteryState.status]);

            totalFrames = 0;
            elapsed = 0;
        }
    }

    SaveSram(sramPath);
    free(sramPath);
    free(sramName);

    SaveState(savePath);
    free(savePath);
    free(saveName);

    return 0;
}
