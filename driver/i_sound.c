#include "engine.h"
#include <limits.h>

static SDL_AudioDeviceID device;
static SDL_AudioSpec format;
static Uint8 *sample;
static Uint32 length, cursor;
static int volume = 10;

static void audio(void *unused, Uint8 *stream, int bytes) {
    (void)unused;
    memset(stream, format.silence, bytes);
    Uint32 available = length - cursor;
    if (available > (Uint32)bytes) available = bytes;
    if (available) SDL_MixAudioFormat(stream, sample + cursor, format.format, available,
                                     volume * SDL_MIX_MAXVOLUME / 10);
    cursor += available;
}

void I_SetVolumes(int sound, int music) {
    (void)music; /* Retail CD audio is not part of the installed data files. */
    if (device) SDL_LockAudioDevice(device);
    volume = sound;
    if (device) SDL_UnlockAudioDevice(device);
}

void I_PlaySound(const char *path) {
    SDL_AudioSpec source;
    Uint8 *data;
    Uint32 size;
    if (!SDL_LoadWAV(path, &source, &data, &size)) return;
    if (!device && !SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        source.callback = audio;
        source.userdata = NULL;
        device = SDL_OpenAudioDevice(NULL, 0, &source, &format, 0);
    }
    if (!device) { SDL_FreeWAV(data); return; }
    SDL_AudioCVT convert;
    if (SDL_BuildAudioCVT(&convert, source.format, source.channels, source.freq,
                         format.format, format.channels, format.freq) < 0 ||
        size > INT_MAX / (unsigned)convert.len_mult) {
        SDL_FreeWAV(data);
        return;
    }
    convert.buf = malloc((size_t)size * convert.len_mult);
    if (!convert.buf) { SDL_FreeWAV(data); return; }
    memcpy(convert.buf, data, size);
    SDL_FreeWAV(data);
    convert.len = size;
    if (convert.needed) {
        if (SDL_ConvertAudio(&convert)) { free(convert.buf); return; }
    } else convert.len_cvt = convert.len;
    SDL_LockAudioDevice(device);
    free(sample);
    sample = convert.buf;
    length = convert.len_cvt;
    cursor = 0;
    SDL_UnlockAudioDevice(device);
    SDL_PauseAudioDevice(device, 0);
}

void I_ShutdownSound(void) {
    if (device) SDL_CloseAudioDevice(device);
    device = 0;
    free(sample);
    sample = NULL;
    length = cursor = 0;
}
