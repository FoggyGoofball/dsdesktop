/*============================================================================
 * pc_host/src/audio_enc.cpp
 *
 * Audio capture and encoding.
 *
 * Windows builds use WASAPI loopback on the default render endpoint,
 * downsample to 16 kHz mono, and package unsigned 8-bit PCM chunks for
 * transport to the DS.
 *
 * Non-Windows builds still require a platform backend before --audio can
 * be enabled.
 *==========================================================================*/
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <algorithm>

#include "../../common/protocol.h"
#include "../include/audio_enc.h"

static uint16_t s_audio_seq = 0;
static int s_audio_ready = 0;

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <ks.h>
#include <ksmedia.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")

#define AUDIO_OUT_RATE        16000u
#define AUDIO_CHUNK_MIN       256
#define AUDIO_CHUNK_MAX       1024
#define AUDIO_RING_CAPACITY   32768

static IMMDeviceEnumerator *s_enumerator = nullptr;
static IMMDevice           *s_device = nullptr;
static IAudioClient        *s_audio_client = nullptr;
static IAudioCaptureClient *s_capture_client = nullptr;
static WAVEFORMATEX        *s_mix_format = nullptr;
static int                  s_com_inited = 0;
static uint8_t              s_pcm_ring[AUDIO_RING_CAPACITY];
static int                  s_ring_head = 0;
static int                  s_ring_tail = 0;
static uint32_t             s_resample_accum = 0;

static void safe_release(IUnknown *p)
{
    if (p) p->Release();
}

static int ring_available(void)
{
    if (s_ring_head >= s_ring_tail)
        return s_ring_head - s_ring_tail;
    return AUDIO_RING_CAPACITY - s_ring_tail + s_ring_head;
}

static void ring_push(uint8_t sample)
{
    int next = (s_ring_head + 1) % AUDIO_RING_CAPACITY;
    if (next == s_ring_tail)
        s_ring_tail = (s_ring_tail + 1) % AUDIO_RING_CAPACITY; /* drop oldest */
    s_pcm_ring[s_ring_head] = sample;
    s_ring_head = next;
}

static int ring_pop_block(uint8_t *dst, int count)
{
    int avail = ring_available();
    if (count > avail) count = avail;
    for (int i = 0; i < count; i++) {
        dst[i] = s_pcm_ring[s_ring_tail];
        s_ring_tail = (s_ring_tail + 1) % AUDIO_RING_CAPACITY;
    }
    return count;
}

static int is_float_format(const WAVEFORMATEX *fmt)
{
    if (fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
        return 1;
    if (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        const WAVEFORMATEXTENSIBLE *ex = (const WAVEFORMATEXTENSIBLE *)fmt;
        return IsEqualGUID(ex->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
    }
    return 0;
}

static int is_pcm_format(const WAVEFORMATEX *fmt)
{
    if (fmt->wFormatTag == WAVE_FORMAT_PCM)
        return 1;
    if (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        const WAVEFORMATEXTENSIBLE *ex = (const WAVEFORMATEXTENSIBLE *)fmt;
        return IsEqualGUID(ex->SubFormat, KSDATAFORMAT_SUBTYPE_PCM);
    }
    return 0;
}

static float read_sample_as_float(const uint8_t *frame, int channel)
{
    const int channels = s_mix_format->nChannels;
    const int bits = s_mix_format->wBitsPerSample;

    if (is_float_format(s_mix_format) && bits == 32) {
        const float *samples = (const float *)frame;
        return samples[channel < channels ? channel : 0];
    }

    if (is_pcm_format(s_mix_format) && bits == 16) {
        const int16_t *samples = (const int16_t *)frame;
        return (float)samples[channel < channels ? channel : 0] / 32768.0f;
    }

    if (is_pcm_format(s_mix_format) && bits == 32) {
        const int32_t *samples = (const int32_t *)frame;
        return (float)samples[channel < channels ? channel : 0] / 2147483648.0f;
    }

    return 0.0f;
}

static void process_capture_frames(const uint8_t *data, UINT32 frames, DWORD flags)
{
    const int channels = s_mix_format->nChannels > 0 ? s_mix_format->nChannels : 1;
    const uint32_t input_rate = s_mix_format->nSamplesPerSec;
    const int block_align = s_mix_format->nBlockAlign;

    for (UINT32 i = 0; i < frames; i++) {
        float mono = 0.0f;
        if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && data) {
            if (channels == 1) {
                mono = read_sample_as_float(data + (i * block_align), 0);
            } else {
                float a = read_sample_as_float(data + (i * block_align), 0);
                float b = read_sample_as_float(data + (i * block_align), 1);
                mono = 0.5f * (a + b);
            }
        }

        if (mono < -1.0f) mono = -1.0f;
        if (mono >  1.0f) mono =  1.0f;
        uint8_t pcm8 = (uint8_t)(mono * 127.0f + 128.0f);

        s_resample_accum += AUDIO_OUT_RATE;
        while (s_resample_accum >= input_rate) {
            ring_push(pcm8);
            s_resample_accum -= input_rate;
        }
    }
}

static void pump_capture(void)
{
    if (!s_capture_client)
        return;

    UINT32 packet_frames = 0;
    while (SUCCEEDED(s_capture_client->GetNextPacketSize(&packet_frames)) &&
           packet_frames > 0) {
        BYTE *data = nullptr;
        UINT32 frames = 0;
        DWORD flags = 0;

        HRESULT hr = s_capture_client->GetBuffer(&data, &frames, &flags, NULL, NULL);
        if (FAILED(hr))
            break;

        process_capture_frames(data, frames, flags);
        s_capture_client->ReleaseBuffer(frames);
        packet_frames = 0;
    }
}

int audio_enc_init(void)
{
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (SUCCEEDED(hr))
        s_com_inited = 1;
    else if (hr != RPC_E_CHANGED_MODE)
        return -1;

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
                          __uuidof(IMMDeviceEnumerator), (void **)&s_enumerator);
    if (FAILED(hr)) return -1;

    hr = s_enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &s_device);
    if (FAILED(hr)) return -1;

    hr = s_device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL,
                            (void **)&s_audio_client);
    if (FAILED(hr)) return -1;

    hr = s_audio_client->GetMixFormat(&s_mix_format);
    if (FAILED(hr) || !s_mix_format) return -1;

    if (!(is_float_format(s_mix_format) || is_pcm_format(s_mix_format))) {
        fprintf(stderr, "Audio capture unavailable: unsupported WASAPI mix format.\n");
        return -1;
    }

    hr = s_audio_client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                    AUDCLNT_STREAMFLAGS_LOOPBACK,
                                    0, 0, s_mix_format, NULL);
    if (FAILED(hr)) return -1;

    hr = s_audio_client->GetService(__uuidof(IAudioCaptureClient),
                                    (void **)&s_capture_client);
    if (FAILED(hr)) return -1;

    hr = s_audio_client->Start();
    if (FAILED(hr)) return -1;

    s_ring_head = s_ring_tail = 0;
    s_resample_accum = 0;
    s_audio_seq = 0;
    s_audio_ready = 1;

    printf("  Audio capture: WASAPI loopback (%u Hz mix -> %u Hz mono PCM8)\n",
           (unsigned)s_mix_format->nSamplesPerSec,
           (unsigned)AUDIO_OUT_RATE);
    return 0;
}

void audio_enc_shutdown(void)
{
    s_audio_ready = 0;

    if (s_audio_client)
        s_audio_client->Stop();

    safe_release((IUnknown *)s_capture_client); s_capture_client = nullptr;
    safe_release((IUnknown *)s_audio_client); s_audio_client = nullptr;
    safe_release((IUnknown *)s_device); s_device = nullptr;
    safe_release((IUnknown *)s_enumerator); s_enumerator = nullptr;

    if (s_mix_format) {
        CoTaskMemFree(s_mix_format);
        s_mix_format = nullptr;
    }

    if (s_com_inited) {
        CoUninitialize();
        s_com_inited = 0;
    }
}

int audio_enc_capture(uint8_t *out_buf, int out_cap)
{
    if (!s_audio_ready) return 0;

    pump_capture();

    int header_bytes = (int)sizeof(dsrd_header_t) + (int)sizeof(dsrd_audio_hdr_t);
    int max_payload = out_cap - header_bytes;
    if (max_payload < AUDIO_CHUNK_MIN)
        return 0;

    int avail = ring_available();
    if (avail < AUDIO_CHUNK_MIN)
        return 0;

    int chunk_size = (avail < AUDIO_CHUNK_MAX) ? avail : AUDIO_CHUNK_MAX;
    if (chunk_size > max_payload)
        chunk_size = max_payload;

    dsrd_header_t *hdr = (dsrd_header_t *)out_buf;
    dsrd_header_init(hdr, PKT_AUDIO_CHUNK, s_audio_seq++,
                     (uint16_t)(sizeof(dsrd_audio_hdr_t) + chunk_size),
                     0 /* PC */, DSRD_FLAG_AUDIO);

    dsrd_audio_hdr_t *ah = (dsrd_audio_hdr_t *)(out_buf + sizeof(dsrd_header_t));
    ah->sample_count = (uint16_t)chunk_size;
    ah->format       = DSRD_AUDIO_FMT_PCM8;

    uint8_t *samples = out_buf + sizeof(dsrd_header_t) + sizeof(dsrd_audio_hdr_t);
    ring_pop_block(samples, chunk_size);
    return header_bytes + chunk_size;
}

#else
int audio_enc_init(void)
{
    fprintf(stderr, "Audio capture unavailable in this build: PulseAudio/PipeWire backend is not included.\n");
    s_audio_ready = 0;
    s_audio_seq = 0;
    return -1;
}

void audio_enc_shutdown(void)
{
    s_audio_ready = 0;
}

int audio_enc_capture(uint8_t *out_buf, int out_cap)
{
    (void)out_buf;
    (void)out_cap;
    (void)s_audio_seq;
    return 0;
}
#endif
