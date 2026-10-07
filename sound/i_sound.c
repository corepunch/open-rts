/* Platform sound: an SDL audio callback mixing samples in software, as the
 * SDL ports of Doom's i_sound.c do. Samples are converted once at load to
 * signed 16-bit mono at the device rate, so mixing is a scaled add. */
#include "engine.h"

#define NUMCHANNELS 32
/* DC.EXE sets an 11025 Hz primary buffer; mixing at twice that resamples its
 * 8000..22050 Hz samples with less aliasing. */
#define MIXRATE 22050

struct sfxsample_s {
    int16_t *data;
    uint32_t length; /* Frames. */
};

typedef struct {
    const sfxsample_t *sample;
    uint32_t position;
    int left, right; /* 0..256. */
    bool loop;
    uint16_t generation;
} channel_t;

static SDL_AudioDeviceID device;
static SDL_AudioSpec spec;
static channel_t channels[NUMCHANNELS];
static int32_t *mixbuffer;
static int mixframes;

static void mix(void *userdata, Uint8 *stream, int bytes) {
    (void)userdata;
    int frames = bytes / (int)(sizeof(int16_t) * 2);
    if (frames > mixframes) frames = mixframes;
    memset(mixbuffer, 0, (size_t)frames * 2 * sizeof(*mixbuffer));
    for (int c = 0; c < NUMCHANNELS; ++c) {
        channel_t *channel = &channels[c];
        if (!channel->sample) continue;
        const int16_t *data = channel->sample->data;
        uint32_t length = channel->sample->length;
        for (int i = 0; i < frames; ++i) {
            if (channel->position >= length) {
                if (!channel->loop || !length) { channel->sample = NULL; break; }
                channel->position = 0;
            }
            int32_t value = data[channel->position++];
            mixbuffer[i * 2] += value * channel->left;
            mixbuffer[i * 2 + 1] += value * channel->right;
        }
    }
    int16_t *out = (int16_t *)stream;
    for (int i = 0; i < frames * 2; ++i) {
        int32_t value = mixbuffer[i] >> 8;
        out[i] = (int16_t)(value > 32767 ? 32767 : value < -32768 ? -32768 : value);
    }
    if (frames * (int)(sizeof(int16_t) * 2) < bytes)
        memset(stream + frames * sizeof(int16_t) * 2, 0,
               (size_t)bytes - (size_t)frames * sizeof(int16_t) * 2);
}

bool I_InitSound(void) {
    if (device) return true;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "warning: no audio: %s\n", SDL_GetError());
        return false;
    }
    SDL_AudioSpec want = {
        .freq = MIXRATE, .format = AUDIO_S16SYS, .channels = 2, .samples = 512,
        .callback = mix,
    };
    device = SDL_OpenAudioDevice(NULL, 0, &want, &spec, 0);
    if (!device) {
        fprintf(stderr, "warning: no audio device: %s\n", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }
    mixframes = spec.samples * 4;
    mixbuffer = calloc((size_t)mixframes * 2, sizeof(*mixbuffer));
    if (!mixbuffer) {
        SDL_CloseAudioDevice(device);
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        device = 0;
        return false;
    }
    SDL_PauseAudioDevice(device, 0);
    return true;
}

void I_ShutdownSound(void) {
    if (!device) return;
    SDL_CloseAudioDevice(device);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    device = 0;
    memset(channels, 0, sizeof(channels));
    free(mixbuffer);
    mixbuffer = NULL;
}

static sfxsample_t *load_sample(SDL_RWops *source) {
    if (!source) return NULL;
    SDL_AudioSpec wav;
    Uint8 *bytes = NULL;
    Uint32 size = 0;
    if (!SDL_LoadWAV_RW(source, 1, &wav, &bytes, &size)) return NULL;
    SDL_AudioCVT cvt;
    if (SDL_BuildAudioCVT(&cvt, wav.format, wav.channels, wav.freq,
                          AUDIO_S16SYS, 1, spec.freq) < 0) {
        SDL_FreeWAV(bytes);
        return NULL;
    }
    if (!size || size > INT32_MAX / (unsigned)(cvt.len_mult > 0 ? cvt.len_mult : 1)) {
        SDL_FreeWAV(bytes);
        return NULL;
    }
    cvt.len = (int)size;
    cvt.buf = malloc((size_t)size * (size_t)(cvt.len_mult > 0 ? cvt.len_mult : 1));
    sfxsample_t *sample = calloc(1, sizeof(*sample));
    if (!cvt.buf || !sample) {
        free(cvt.buf);
        free(sample);
        SDL_FreeWAV(bytes);
        return NULL;
    }
    memcpy(cvt.buf, bytes, size);
    SDL_FreeWAV(bytes);
    if (cvt.needed && SDL_ConvertAudio(&cvt) != 0) {
        free(cvt.buf);
        free(sample);
        return NULL;
    }
    sample->data = (int16_t *)cvt.buf;
    sample->length = (uint32_t)(cvt.needed ? cvt.len_cvt : cvt.len) / sizeof(int16_t);
    return sample;
}

sfxsample_t *I_LoadSample(const char *path) {
    if (!device) return NULL;
    sfxsample_t *sample = load_sample(SDL_RWFromFile(path, "rb"));
    if (!sample) fprintf(stderr, "warning: sound %s: %s\n", path, SDL_GetError());
    return sample;
}

sfxsample_t *I_LoadSampleMemory(const void *bytes, size_t size) {
    if (!device || !bytes || !size || size > INT32_MAX) return NULL;
    return load_sample(SDL_RWFromConstMem(bytes, (int)size));
}

void I_FreeSample(sfxsample_t *sample) {
    if (!sample) return;
    if (device) {
        /* The callback may be reading it; detach first. */
        SDL_LockAudioDevice(device);
        for (int c = 0; c < NUMCHANNELS; ++c)
            if (channels[c].sample == sample) channels[c].sample = NULL;
        SDL_UnlockAudioDevice(device);
    }
    free(sample->data);
    free(sample);
}

/* A handle is the channel and its generation, so a stale one never touches
 * the sound now reusing the channel. */
static channel_t *channel_for(int handle) {
    if (handle <= 0) return NULL;
    int index = (handle & 0xff) - 1;
    if (index < 0 || index >= NUMCHANNELS) return NULL;
    channel_t *channel = &channels[index];
    return channel->generation == (uint16_t)(handle >> 8) ? channel : NULL;
}

static int clamp_volume(int volume) {
    return volume < 0 ? 0 : volume > 256 ? 256 : volume;
}

int I_StartSound(const sfxsample_t *sample, int left, int right, bool loop) {
    if (!device || !sample || !sample->length) return 0;
    SDL_LockAudioDevice(device);
    int index = -1;
    for (int c = 0; c < NUMCHANNELS && index < 0; ++c)
        if (!channels[c].sample) index = c;
    int handle = 0;
    if (index >= 0) {
        channel_t *channel = &channels[index];
        channel->sample = sample;
        channel->position = 0;
        channel->left = clamp_volume(left);
        channel->right = clamp_volume(right);
        channel->loop = loop;
        channel->generation = (uint16_t)((channel->generation + 1) & 0x7fff);
        handle = (channel->generation << 8) | (index + 1);
    }
    SDL_UnlockAudioDevice(device);
    return handle;
}

void I_UpdateSoundParams(int handle, int left, int right) {
    if (!device) return;
    SDL_LockAudioDevice(device);
    channel_t *channel = channel_for(handle);
    if (channel && channel->sample) {
        channel->left = clamp_volume(left);
        channel->right = clamp_volume(right);
    }
    SDL_UnlockAudioDevice(device);
}

void I_StopSound(int handle) {
    if (!device) return;
    SDL_LockAudioDevice(device);
    channel_t *channel = channel_for(handle);
    if (channel) channel->sample = NULL;
    SDL_UnlockAudioDevice(device);
}

bool I_SoundIsPlaying(int handle) {
    if (!device) return false;
    SDL_LockAudioDevice(device);
    channel_t *channel = channel_for(handle);
    bool playing = channel && channel->sample;
    SDL_UnlockAudioDevice(device);
    return playing;
}
