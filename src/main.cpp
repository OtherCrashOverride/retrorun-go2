#include "video.h"

#include <unistd.h>



#include <go2/queue.h>
#include <go2/audio.h>
#include <go2/input.h>


#include <linux/dma-buf.h>
#include <sys/ioctl.h>

#include "libretro.h"
#include <dlfcn.h>
#include <cstdarg>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <exception>

#define EGL_EGLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <drm/drm_fourcc.h>


#define DEFAULT_WIDTH (480)
#define DEFAULT_HEIGHT (320)




go2_audio_t* audio;

static go2_gamepad_t gamepadState;
go2_input_t* input;


retro_hw_context_reset_t retro_context_reset;


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
	//	void *retro_get_memory_data(unsigned id);
	//	size_t retro_get_memory_size(unsigned id);
    
} g_retro;

#define load_sym(V, S) do {\
        if (!((*(void**)&V) = dlsym(g_retro.handle, #S))) \
        { \
            printf("[noarch] Failed to load symbol '" #S "'': %s", dlerror()); \
            abort(); \
        } \
	} while (0)
#define load_retro_sym(S) load_sym(g_retro.S, S)




static void audio_init(int freq)
{
    // Note: audio stutters in OpenAL unless the buffer frequency at upload
    // is the same as during creation.
    audio = go2_audio_create(freq);

    printf("audio_init: freq=%d\n", freq);
}

static void audio_deinit()
{

}

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

	if (level == RETRO_LOG_ERROR)
		exit(EXIT_FAILURE);
}



static const char* test_value = "UNIBIOS";

static __eglMustCastToProperFunctionPointerType get_proc_address(const char* sym)
{
    __eglMustCastToProperFunctionPointerType result = eglGetProcAddress(sym);
    printf("get_proc_address: sym='%s', result=%p\n", sym, (void*)result);

    return result;
}

static bool core_environment(unsigned cmd, void* data)
{
	bool* bval;

	switch (cmd)
    {
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
                switch (fmt)
                {   
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
        case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
            * (const char * * ) data = ".";
            return true;

        case RETRO_ENVIRONMENT_SET_HW_RENDER:
            {
                retro_hw_render_callback* hw = (retro_hw_render_callback*)data;

                printf("RETRO_ENVIRONMENT_SET_HW_RENDER\n");
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

        case RETRO_ENVIRONMENT_GET_VARIABLE:
        {
            retro_variable* var = (retro_variable*)data;
            if (strcmp(var->key, "fbneo-neogeo-mode") == 0)
            {
                printf("fbneo-neogeo-mode=%s\n", test_value);
                var->value = test_value;
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
            return false;
        }
        default:
            core_log(RETRO_LOG_DEBUG, "Unhandled env #%u", cmd);
            return false;
	}

	return true;
}





static void core_input_poll(void)
{
	go2_input_read(input, &gamepadState);
}

static int16_t core_input_state(unsigned port, unsigned device, unsigned index, unsigned id)
{
    int16_t result;

    if (port || index || device != RETRO_DEVICE_JOYPAD)
            return 0;

#if 1
    // Map thumbstick to dpad
    const float TRIM = 0.35f;
    
    if (gamepadState.thumb.y < -TRIM) gamepadState.dpad.up = ButtonState_Pressed;
    if (gamepadState.thumb.y > TRIM) gamepadState.dpad.down = ButtonState_Pressed;
    if (gamepadState.thumb.x < -TRIM) gamepadState.dpad.left = ButtonState_Pressed;
    if (gamepadState.thumb.x > TRIM) gamepadState.dpad.right = ButtonState_Pressed;
#endif

/*
#define RETRO_DEVICE_ID_JOYPAD_B        0
#define RETRO_DEVICE_ID_JOYPAD_Y        1
#define RETRO_DEVICE_ID_JOYPAD_SELECT   2
#define RETRO_DEVICE_ID_JOYPAD_START    3
#define RETRO_DEVICE_ID_JOYPAD_UP       4
#define RETRO_DEVICE_ID_JOYPAD_DOWN     5
#define RETRO_DEVICE_ID_JOYPAD_LEFT     6
#define RETRO_DEVICE_ID_JOYPAD_RIGHT    7
#define RETRO_DEVICE_ID_JOYPAD_A        8
#define RETRO_DEVICE_ID_JOYPAD_X        9
#define RETRO_DEVICE_ID_JOYPAD_L       10
#define RETRO_DEVICE_ID_JOYPAD_R       11
#define RETRO_DEVICE_ID_JOYPAD_L2      12
#define RETRO_DEVICE_ID_JOYPAD_R2      13
#define RETRO_DEVICE_ID_JOYPAD_L3      14
#define RETRO_DEVICE_ID_JOYPAD_R3      15
*/

    switch (id)
    {
    case RETRO_DEVICE_ID_JOYPAD_B:
        return gamepadState.buttons.b;
        break;
    
    case RETRO_DEVICE_ID_JOYPAD_Y:
        return gamepadState.buttons.y;
        break;

    case RETRO_DEVICE_ID_JOYPAD_SELECT:
        return gamepadState.buttons.f3;
        break;

    case RETRO_DEVICE_ID_JOYPAD_START:
        return gamepadState.buttons.f4;
        break;

    case RETRO_DEVICE_ID_JOYPAD_UP:
        return gamepadState.dpad.up;
        break;

    case RETRO_DEVICE_ID_JOYPAD_DOWN:
        return gamepadState.dpad.down;
        break;

    case RETRO_DEVICE_ID_JOYPAD_LEFT:
        return gamepadState.dpad.left;
        break;

    case RETRO_DEVICE_ID_JOYPAD_RIGHT:
        return gamepadState.dpad.right;
        break;

    case RETRO_DEVICE_ID_JOYPAD_A:
        return gamepadState.buttons.a;
        break;

    case RETRO_DEVICE_ID_JOYPAD_X:
        return gamepadState.buttons.x;
        break;

    case RETRO_DEVICE_ID_JOYPAD_L:
        return gamepadState.buttons.top_left;
        break;

    case RETRO_DEVICE_ID_JOYPAD_R:
        return gamepadState.buttons.top_right;
        break;

    default:
        result = 0;
        break;
    }

    return result;
}

static void core_audio_sample(int16_t left, int16_t right)
{
	// (void)left;
	// (void)right;
}

static size_t core_audio_sample_batch(const int16_t * data, size_t frames)
{
	go2_audio_submit(audio, (const short*)data, frames);
	return 0;
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

	puts("Core loaded");

    g_retro.retro_set_controller_port_device(0, RETRO_DEVICE_JOYPAD);
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
		printf("The core failed to load the content.");
        abort();
    }

	g_retro.retro_get_system_av_info( & av);
	video_configure(&av.geometry);
	audio_init(av.timing.sample_rate);

	return;

	libc_error:
		printf("Failed to load content '%s'", filename);
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

static int LoadState(const char* saveName)
{
    FILE* file = fopen(saveName, "rb");
	if (!file)
		return 0;

	fseek(file, 0, SEEK_END);
	long size = ftell(file);
	rewind(file);

    if (size < 1) return 0;

    void* ptr = malloc(size);
    if (!ptr) abort();

    size_t count = fread(ptr, 1, size, file);
    if (size != count)
    {
        free(ptr);
        abort();
    }

    fclose(file);

    g_retro.retro_unserialize(ptr, size);
    free(ptr);
}

static int SaveState(const char* saveName)
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

int main(int argc, char *argv[])
{
    //printf("argc=%d, argv=%p\n", argc, argv);

#if 0
    // queue test
    go2_queue_t* queue = go2_queue_create(10);
    for (int i = 0; i < 10; ++i)
    {
        go2_queue_push(queue, (void*)i);
    }

    for (int i = 0; i < 10; ++i)
    {
        void* val = go2_queue_pop(queue);
        printf("queue[%d]=%p\n", i, val);
    }

    return 0;
#endif



    input = go2_input_create();


#if 1
    // Init
	if (argc < 3)
    {
		printf("Usage: %s <core> <game>", argv[0]);
        exit(1);
    }

	core_load(argv[1]);
    core_load_game(argv[2]);
    
#endif
    const char* fileName = FileNameFromPath(argv[2]);
    
    char* saveName = (char*)malloc(strlen(fileName) + 4 + 1);
    strcpy(saveName, fileName);
    strcat(saveName, ".sav");

    printf("saveName='%s'\n", saveName);
    LoadState(saveName);


    printf("Entering render loop.\n");

    bool isRunning = true;
    while(isRunning)
    {                
        if (gamepadState.buttons.f1)
            isRunning = false;


        g_retro.retro_run();
    }

    SaveState(saveName);

    return 0;
}
