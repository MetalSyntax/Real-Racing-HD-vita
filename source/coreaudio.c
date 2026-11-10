/**
 * @file coreaudio.c
 * @brief iOS AudioUnit RemoteIO and AudioSession emulation on top of sceAudioOut.
 * @details Handles guest audio output (such as embedded FMOD Ex or custom mixers):
 *          creates a RemoteIO unit, configures stream format and render callbacks,
 *          and streams audio on a dedicated high-priority sceAudioOut thread.
 *          Calls the guest callback via call_guest6.
 */
#include <AL/al.h>
#include <mpg123.h>
#include <pthread.h>
#include <strings.h>
#include <AL/alc.h>
#include <psp2/audioout.h>
#include <stdio.h>
#include <psp2/kernel/threadmgr.h>
#include <stdlib.h>
#include <string.h>

#include "imports.h"
#include "log.h"
#include "darwin.h"
#include "macho.h"
#include "objc_rt.h"
#include "objc.h"

#define kAudioUnitProperty_StreamFormat 8
#define kAudioUnitProperty_SetRenderCallback 23
#define kAudioFormatFlagIsFloat (1 << 0)
#define kAudioFormatFlagIsNonInterleaved (1 << 5)
#define kAudioFormatFlagsFixed824 (24 << 7) // kLinearPCMFormatFlagsSampleFractionShift
#define FOURCC(a, b, c, d) (((a) << 24) | ((b) << 16) | ((c) << 8) | (d))

#define GRAIN 512

typedef struct {
    double mSampleRate;
    uint32_t mFormatID, mFormatFlags, mBytesPerPacket, mFramesPerPacket, mBytesPerFrame,
        mChannelsPerFrame, mBitsPerChannel, mReserved;
} asbd;

typedef struct {
    uint32_t mNumberBuffers;
    struct {
        uint32_t mNumberChannels, mDataByteSize;
        void *mData;
    } mBuffers[2];
} buffer_list;

typedef struct {
    asbd fmt;
    void *proc, *refcon;
    SceUID thread;
    volatile int running;
} audio_unit;

static int ok(void) { return 0; }

static int au_FindNext(void *prev, const void *desc) { return prev ? 0 : 1; } // single audio component

static int au_InstanceNew(void *comp, audio_unit **out) {
    audio_unit *u = calloc(1, sizeof(*u));
    // RemoteIO default: 44.1 kHz, s16 interleaved stereo.
    u->fmt = (asbd){44100, FOURCC('l', 'p', 'c', 'm'), 0xC, 4, 1, 4, 2, 16, 0};
    *out = u;
    return 0;
}

static int au_SetProperty(audio_unit *u, uint32_t id, uint32_t scope, uint32_t elem, const void *data, uint32_t size) {
    if (id == kAudioUnitProperty_StreamFormat) {
        memcpy(&u->fmt, data, sizeof(asbd));
        l_info("AudioUnit: %d Hz, %u channels, %u bits, flags 0x%X", (int)u->fmt.mSampleRate,
               (unsigned)u->fmt.mChannelsPerFrame, (unsigned)u->fmt.mBitsPerChannel, (unsigned)u->fmt.mFormatFlags);
    } else if (id == kAudioUnitProperty_SetRenderCallback) {
        u->proc = ((void *const *)data)[0];
        u->refcon = ((void *const *)data)[1];
    }
    return 0;
}

static int au_GetProperty(audio_unit *u, uint32_t id, uint32_t scope, uint32_t elem, void *data, uint32_t *size) {
    if (id == kAudioUnitProperty_StreamFormat) {
        memcpy(data, &u->fmt, sizeof(asbd));
        *size = sizeof(asbd);
        return 0;
    }
    memset(data, 0, *size);
    return 0;
}

static int16_t to_s16(const audio_unit *u, const uint8_t *p) {
    uint32_t fl = u->fmt.mFormatFlags;
    if (fl & kAudioFormatFlagIsFloat) {
        float f = *(const float *)p * 32767.0f;
        return f > 32767 ? 32767 : f < -32768 ? -32768 : (int16_t)f;
    }
    if (u->fmt.mBitsPerChannel == 32) { // 8.24 fixed-point (canonical AudioUnit) or s32
        int32_t v = *(const int32_t *)p >> ((fl & kAudioFormatFlagsFixed824) ? 9 : 16);
        return v > 32767 ? 32767 : v < -32768 ? -32768 : v;
    }
    return *(const int16_t *)p;
}

static int audio_thread(SceSize args, void *argp) {
    audio_unit *u = *(audio_unit **)argp;
    int rate = (int)u->fmt.mSampleRate, ch = u->fmt.mChannelsPerFrame;
    int bytes = u->fmt.mBitsPerChannel / 8;
    int nonint = (u->fmt.mFormatFlags & kAudioFormatFlagIsNonInterleaved) && ch == 2;
    int port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, GRAIN, rate, SCE_AUDIO_OUT_MODE_STEREO);
    if (port < 0) {
        l_error("sceAudioOutOpenPort(%d Hz) = 0x%08X", rate, port);
        return 0;
    }
    uint8_t *raw = malloc(GRAIN * 2 * 4);
    int16_t out[GRAIN * 2];
    uint8_t ts[64] = {0}; // AudioTimeStamp
    uint32_t flags = 0;
    double sample_time = 0;
    buffer_list bl;
    while (u->running) {
        if (nonint)
            bl = (buffer_list){2, {{1, GRAIN * bytes, raw}, {1, GRAIN * bytes, raw + GRAIN * 4}}};
        else
            bl = (buffer_list){1, {{ch, GRAIN * ch * bytes, raw}}};
        memcpy(ts, &sample_time, 8);
        ((uint32_t *)ts)[14] = 1; // mFlags = kAudioTimeStampSampleTimeValid
        flags = 0;
        call_guest6(u->proc, (uint32_t)u->refcon, (uint32_t)&flags, (uint32_t)ts, 0, GRAIN, (uint32_t)&bl);
        sample_time += GRAIN;
        for (int i = 0; i < GRAIN; i++) {
            if (nonint) {
                out[i * 2] = to_s16(u, raw + i * bytes);
                out[i * 2 + 1] = to_s16(u, raw + GRAIN * 4 + i * bytes);
            } else {
                out[i * 2] = to_s16(u, raw + i * ch * bytes);
                out[i * 2 + 1] = ch == 2 ? to_s16(u, raw + (i * 2 + 1) * bytes) : out[i * 2];
            }
        }
        sceAudioOutOutput(port, out);
    }
    sceAudioOutReleasePort(port);
    free(raw);
    return 0;
}

static int au_Start(audio_unit *u) {
    if (u->running || !u->proc)
        return 0;
    u->running = 1;
    u->thread = sceKernelCreateThread("coreaudio", audio_thread, 0x10000100, 256 * 1024, 0, 0, NULL);
    sceKernelStartThread(u->thread, sizeof(u), &u);
    return 0;
}

static int au_Stop(audio_unit *u) {
    if (u->running) {
        u->running = 0;
        sceKernelWaitThreadEnd(u->thread, NULL, NULL);
        sceKernelDeleteThread(u->thread);
    }
    return 0;
}

static int au_Dispose(audio_unit *u) {
    au_Stop(u);
    free(u);
    return 0;
}

static int au_Render(void) { return -1; } // audio input (microphone): not supported

// AudioSession: queried by audio engines to select stream properties.
static int as_GetProperty(uint32_t id, uint32_t *size, void *data) {
    memset(data, 0, *size);
    switch (id) {
    case FOURCC('c', 'h', 's', 'r'): // CurrentHardwareSampleRate (Float64)
        *(double *)data = 44100.0;
        break;
    case FOURCC('c', 'h', 'o', 'c'): // CurrentHardwareOutputNumberChannels
        *(uint32_t *)data = 2;
        break;
    case FOURCC('c', 'h', 'b', 'd'): // CurrentHardwareIOBufferDuration (Float32)
        *(float *)data = (float)GRAIN / 44100.0f;
        break;
    case FOURCC('c', 'h', 'o', 'l'): // CurrentHardwareOutputLatency (Float32)
        *(float *)data = 0.012f;
        break;
    default:
        l_debug("AudioSessionGetProperty('%c%c%c%c') -> 0", id >> 24, (id >> 16) & 0xFF, (id >> 8) & 0xFF, id & 0xFF);
    }
    return 0;
}

// ---------------------------------------------------------------- AudioFile (WAV PCM + MP3 via libmpg123)
// Real Racing HD: SFX are 16-bit PCM WAV read whole (LoadAudioFileData: 'dfmt' must be 'lpcm', then
// 'bcnt' + AudioFileReadBytes -> alBufferData). Music is MP3 streamed by BackgroundTrackMgr through an
// AudioQueue ('pkub', 'pcnt', AudioFileReadPackets). MP3s are decoded here: the file reports 16-bit
// stereo LPCM (1 packet = 1 frame) and the game's QueueCallback already handles 'lpcm' (no packet
// descriptions, no magic cookie: 'mgic'/'cmap' answer kAudioFileUnsupportedPropertyError).

#define kAudioFileUnsupportedPropertyError FOURCC('p', 't', 'y', '?')
#define kAudioFileEndOfFileError -39 // eofErr

typedef struct {
    asbd fmt;
    FILE *fp;            // WAV
    uint32_t data_off, data_size;
    mpg123_handle *mh;   // MP3
    FILE *mfp;           // MP3 file, read by mpg123 through 64 KB stdio buffering
    int64_t frames, pos; // MP3 total frames / current frame
} audio_file;

static int mpg_ready;

static int wav_parse(audio_file *af) {
    char id[4];
    uint32_t size;
    if (fread(id, 1, 4, af->fp) != 4 || memcmp(id, "RIFF", 4) || fseek(af->fp, 4, SEEK_CUR) ||
        fread(id, 1, 4, af->fp) != 4 || memcmp(id, "WAVE", 4))
        return -1;
    int have_fmt = 0;
    while (fread(id, 1, 4, af->fp) == 4 && fread(&size, 4, 1, af->fp) == 1) {
        long start = ftell(af->fp);
        if (!memcmp(id, "fmt ", 4) && size >= 16) {
            uint16_t tag, ch, align, bits;
            uint32_t rate, avg;
            fread(&tag, 2, 1, af->fp);
            fread(&ch, 2, 1, af->fp);
            fread(&rate, 4, 1, af->fp);
            fread(&avg, 4, 1, af->fp);
            fread(&align, 2, 1, af->fp);
            fread(&bits, 2, 1, af->fp);
            if (tag != 1) // only Microsoft PCM (all 74 bundle WAVs, docs/audio_video.md)
                return -1;
            af->fmt = (asbd){rate, FOURCC('l', 'p', 'c', 'm'), 0xC, align, 1, align, ch, bits, 0};
            have_fmt = 1;
        } else if (!memcmp(id, "data", 4)) {
            af->data_off = (uint32_t)start;
            af->data_size = size;
            return have_fmt ? 0 : -1;
        }
        fseek(af->fp, start + size + (size & 1), SEEK_SET);
    }
    return -1;
}

// mpg123's own reader does one read() per ~400-byte frame: mpg123_scan over a 4-5 MB track took
// 2-4 s on the console, ~10 playlist tracks per race (log 014: ~40 s frozen entering a race).
static mpg123_ssize_t mp3_rd(void *h, void *buf, size_t n) { return (mpg123_ssize_t)fread(buf, 1, n, h); }
static off_t mp3_sk(void *h, off_t off, int whence) { return fseek(h, off, whence) ? -1 : ftell(h); }

static int mp3_open(audio_file *af, const char *path) {
    if (!mpg_ready) {
        mpg123_init();
        mpg_ready = 1;
    }
    int err;
    long rate;
    int ch, enc;
    af->mh = mpg123_new(NULL, &err);
    if (!af->mh)
        return -1;
    mpg123_param(af->mh, MPG123_FLAGS, MPG123_FORCE_STEREO | MPG123_QUIET, 0);
    af->mfp = fopen(path, "rb");
    if (!af->mfp)
        return -1;
    setvbuf(af->mfp, NULL, _IOFBF, 64 * 1024);
    mpg123_replace_reader_handle(af->mh, mp3_rd, mp3_sk, NULL);
    if (mpg123_open_handle(af->mh, af->mfp) != MPG123_OK || mpg123_getformat(af->mh, &rate, &ch, &enc) != MPG123_OK)
        return -1;
    mpg123_format_none(af->mh);
    mpg123_format(af->mh, rate, MPG123_STEREO, MPG123_ENC_SIGNED_16);
    mpg123_scan(af->mh); // exact frame count for 'pcnt' (the game sizes its loop/end logic from it)
    af->frames = mpg123_length(af->mh);
    if (af->frames <= 0)
        return -1;
    af->fmt = (asbd){(double)rate, FOURCC('l', 'p', 'c', 'm'), 0xC, 4, 1, 4, 2, 16, 0};
    return 0;
}

// iOS validates AudioFileIDs: closing one twice just returns an error. BackgroundTrackMgr::LoadTrack
// closes a queued playlist track at once but keeps its ID, and Teardown closes it again (log 013:
// heap crash in mpg123_delete entering a race). Only live handles are freed.
static audio_file *af_live[64];

static audio_file **af_slot(audio_file *af) {
    for (int i = 0; i < 64; i++)
        if (af_live[i] == af)
            return &af_live[i];
    return NULL;
}

static int af_OpenURL(id url, int perms, uint32_t hint, audio_file **out) {
    char path[256];
    const char *raw = url ? ns_cstr(url) : NULL;
    if (!raw || !*raw)
        return -1;
    darwin_path(raw, path, sizeof(path));
    audio_file *af = calloc(1, sizeof(*af));
    const char *ext = strrchr(path, '.');
    int r;
    if (ext && !strcasecmp(ext, ".mp3")) {
        r = mp3_open(af, path);
    } else {
        af->fp = fopen(path, "rb");
        r = af->fp ? wav_parse(af) : -1;
    }
    if (r < 0) {
        l_warn("[AUDIO] AudioFileOpenURL(%s): cannot open/decode", path);
        if (af->fp)
            fclose(af->fp);
        if (af->mh)
            mpg123_delete(af->mh);
        if (af->mfp)
            fclose(af->mfp);
        free(af);
        return FOURCC('w', 'h', 't', '?'); // kAudioFileUnspecifiedError
    }
    l_info("[AUDIO] AudioFileOpenURL(%s): %s %d Hz %u ch", path, af->mh ? "mp3->pcm" : "wav", (int)af->fmt.mSampleRate,
           (unsigned)af->fmt.mChannelsPerFrame);
    audio_file **slot = af_slot(NULL);
    if (slot) // ponytail: >64 open files are never freed (leak, not a crash); the game keeps ~3
        *slot = af;
    *out = af;
    return 0;
}

static int af_Close(audio_file *af) {
    audio_file **slot = af ? af_slot(af) : NULL;
    if (!slot)
        return FOURCC('f', 'i', 'l', '?'); // kAudioFileInvalidFileError
    *slot = NULL;
    if (af->fp)
        fclose(af->fp);
    if (af->mh) {
        mpg123_close(af->mh);
        mpg123_delete(af->mh);
        fclose(af->mfp);
    }
    free(af);
    return 0;
}

static uint64_t af_bytes(const audio_file *af) {
    return af->mh ? (uint64_t)af->frames * 4 : af->data_size;
}

static int af_GetPropertyInfo(audio_file *af, uint32_t prop, uint32_t *size, uint32_t *writable) {
    uint32_t s;
    switch (prop) {
    case FOURCC('d', 'f', 'm', 't'): s = sizeof(asbd); break;
    case FOURCC('b', 'c', 'n', 't'):
    case FOURCC('p', 'c', 'n', 't'): s = 8; break;
    case FOURCC('p', 'k', 'u', 'b'):
    case FOURCC('p', 's', 'z', 'e'): s = 4; break;
    default: return kAudioFileUnsupportedPropertyError; // 'mgic' magic cookie, 'cmap' channel layout
    }
    if (size)
        *size = s;
    if (writable)
        *writable = 0;
    return 0;
}

static int af_GetProperty(audio_file *af, uint32_t prop, uint32_t *size, void *out) {
    switch (prop) {
    case FOURCC('d', 'f', 'm', 't'): memcpy(out, &af->fmt, sizeof(asbd)); *size = sizeof(asbd); return 0;
    case FOURCC('b', 'c', 'n', 't'): *(uint64_t *)out = af_bytes(af); *size = 8; return 0;
    case FOURCC('p', 'c', 'n', 't'): *(uint64_t *)out = af_bytes(af) / af->fmt.mBytesPerPacket; *size = 8; return 0;
    case FOURCC('p', 'k', 'u', 'b'):
    case FOURCC('p', 's', 'z', 'e'): *(uint32_t *)out = af->fmt.mBytesPerPacket; *size = 4; return 0;
    }
    return kAudioFileUnsupportedPropertyError;
}

// Reads bytes of LPCM: WAV straight from the data chunk, MP3 decoded from frame (start / 4).
static uint32_t af_read(audio_file *af, uint64_t start, uint32_t n, void *buf) {
    uint64_t total = af_bytes(af);
    if (start >= total)
        return 0;
    if (start + n > total)
        n = (uint32_t)(total - start);
    if (!af->mh) {
        fseek(af->fp, af->data_off + (long)start, SEEK_SET);
        return (uint32_t)fread(buf, 1, n, af->fp);
    }
    int64_t frame = (int64_t)(start / 4);
    if (frame != af->pos && mpg123_seek(af->mh, frame, SEEK_SET) >= 0)
        af->pos = frame;
    size_t done = 0;
    if (mpg123_read(af->mh, buf, n, &done) == MPG123_ERR)
        l_warn("[AUDIO] mpg123_read: %s", mpg123_strerror(af->mh));
    af->pos += done / 4;
    return (uint32_t)done;
}

static int af_ReadBytes(audio_file *af, int cache, uint32_t start_lo, uint32_t start_hi, uint32_t *n, void *buf) {
    *n = af_read(af, (uint64_t)start_hi << 32 | start_lo, *n, buf);
    return *n ? 0 : kAudioFileEndOfFileError;
}

// AudioFileReadPackets(file, useCache, &ioNumBytes, packetDescs, SInt64 startPacket, &ioNumPackets, buf):
// the 64-bit startPacket is 8-aligned on the stack after packetDescs (r3).
static int af_ReadPackets(audio_file *af, int cache, uint32_t *nbytes, void *descs, uint32_t start_lo,
                          uint32_t start_hi, uint32_t *npackets, void *buf) {
    uint32_t bpp = af->fmt.mBytesPerPacket;
    uint64_t start = ((uint64_t)start_hi << 32 | start_lo) * bpp;
    uint32_t got = af_read(af, start, *npackets * bpp, buf);
    *npackets = got / bpp;
    *nbytes = *npackets * bpp;
    return 0; // 0 packets = end of file; the game's QueueCallback closes or loops
}

// ---------------------------------------------------------------- AudioQueue output -> sceAudioOut
// Callbacks were registered with CFRunLoopGetCurrent() (main thread): the mixing thread only consumes
// buffers, and coreaudio_poll() (run loop) hands finished buffers back to the guest callback, as iOS does.

typedef struct {
    uint32_t mAudioDataBytesCapacity;
    void *mAudioData;
    uint32_t mAudioDataByteSize;
    void *mUserData;
    uint32_t mPacketDescriptionCapacity;
    void *mPacketDescriptions;
    uint32_t mPacketDescriptionCount;
} aq_buffer; // iOS AudioQueueBuffer layout

#define AQ_MAX 16
typedef struct audio_queue {
    asbd fmt;
    void *proc, *user;
    void *listen_proc, *listen_user;
    aq_buffer *fifo[AQ_MAX], *done[AQ_MAX];
    int nfifo, ndone, head_off;
    volatile int running, paused, stopping, notify_stop, dispose;
    float volume;
    SceUID thread;
    pthread_mutex_t lock;
} audio_queue;

#define AQ_RATE 48000 // SCE_AUDIO_OUT_PORT_TYPE_MAIN is 48 kHz only (OpenAL-soft holds the BGM port)
static audio_queue *queues[4];

static int aq_thread(SceSize args, void *argp) {
    audio_queue *q = *(audio_queue **)argp;
    int port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, GRAIN, AQ_RATE, SCE_AUDIO_OUT_MODE_STEREO);
    if (port < 0) {
        l_error("[AUDIO] AudioQueue: sceAudioOutOpenPort = 0x%08X", port);
        return 0;
    }
    l_info("[AUDIO] AudioQueue thread: %d Hz %u ch -> port %d (48 kHz)", (int)q->fmt.mSampleRate,
           (unsigned)q->fmt.mChannelsPerFrame, port);
    int16_t out[GRAIN * 2];
    double step = q->fmt.mSampleRate / AQ_RATE, frac = 0; // linear resampler 44.1/48 kHz -> 48 kHz
    int prev[2] = {0, 0}, cur[2] = {0, 0};
    int ch = q->fmt.mChannelsPerFrame == 1 ? 1 : 2;
    while (q->running) {
        int vol = (int)(q->volume * SCE_AUDIO_VOLUME_0DB);
        int vols[2] = {vol, vol};
        sceAudioOutSetVolume(port, SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, vols);
        pthread_mutex_lock(&q->lock);
        for (int i = 0; i < GRAIN; i++) {
            if (q->paused || !q->nfifo) { // underrun / paused: silence, position kept
                out[i * 2] = out[i * 2 + 1] = 0;
                continue;
            }
            for (frac += step; frac >= 1.0 && q->nfifo; frac -= 1.0) {
                aq_buffer *b = q->fifo[0];
                const int16_t *smp = (const int16_t *)((uint8_t *)b->mAudioData + q->head_off);
                prev[0] = cur[0];
                prev[1] = cur[1];
                cur[0] = smp[0];
                cur[1] = smp[ch - 1];
                q->head_off += ch * 2;
                if (q->head_off >= (int)b->mAudioDataByteSize) { // buffer played: back to the guest
                    memmove(q->fifo, q->fifo + 1, --q->nfifo * sizeof(q->fifo[0]));
                    q->head_off = 0;
                    if (q->ndone < AQ_MAX)
                        q->done[q->ndone++] = b;
                }
            }
            if (frac > 1.0) // ran dry mid-step
                frac = 1.0;
            out[i * 2] = (int16_t)(prev[0] + (cur[0] - prev[0]) * frac);
            out[i * 2 + 1] = (int16_t)(prev[1] + (cur[1] - prev[1]) * frac);
        }
        if (q->stopping && !q->nfifo) { // AudioQueueStop(inImmediate = false): drained
            q->stopping = 0;
            q->running = 0;
            q->notify_stop = 1;
        }
        pthread_mutex_unlock(&q->lock);
        sceAudioOutOutput(port, out);
    }
    sceAudioOutReleasePort(port);
    return 0;
}

static int aq_NewOutput(const asbd *fmt, void *proc, void *user, void *runloop, void *mode, uint32_t flags,
                        audio_queue **out) {
    if (fmt->mFormatID != FOURCC('l', 'p', 'c', 'm') || fmt->mBitsPerChannel != 16) {
        l_error("[AUDIO] AudioQueueNewOutput: unsupported format %08X / %u bits", (unsigned)fmt->mFormatID,
                (unsigned)fmt->mBitsPerChannel);
        return FOURCC('f', 'm', 't', '?'); // kAudioQueueErr_InvalidFormat-ish: game prints and keeps going
    }
    audio_queue *q = calloc(1, sizeof(*q));
    q->fmt = *fmt;
    q->proc = proc;
    q->user = user;
    q->volume = 1.0f;
    pthread_mutex_init(&q->lock, NULL);
    for (int i = 0; i < 4; i++)
        if (!queues[i]) {
            queues[i] = q;
            break;
        }
    *out = q;
    return 0;
}

static int aq_AllocateBuffer(audio_queue *q, uint32_t size, aq_buffer **out) {
    aq_buffer *b = calloc(1, sizeof(*b) + size);
    b->mAudioDataBytesCapacity = size;
    b->mAudioData = b + 1;
    *out = b;
    return 0;
}

static int aq_FreeBuffer(audio_queue *q, aq_buffer *b) {
    pthread_mutex_lock(&q->lock);
    for (int i = 0; i < q->ndone; i++)
        if (q->done[i] == b)
            memmove(q->done + i, q->done + i + 1, (--q->ndone - i) * sizeof(b));
    pthread_mutex_unlock(&q->lock);
    free(b);
    return 0;
}

static int aq_EnqueueBuffer(audio_queue *q, aq_buffer *b, uint32_t ndesc, const void *descs) {
    if (!b->mAudioDataByteSize) // iOS rejects empty buffers (kAudioQueueErr_BufferEmpty)
        return -66686;
    pthread_mutex_lock(&q->lock);
    if (q->nfifo < AQ_MAX)
        q->fifo[q->nfifo++] = b;
    pthread_mutex_unlock(&q->lock);
    return 0;
}

static int aq_Start(audio_queue *q, const void *start_time) {
    q->paused = 0;
    if (q->running)
        return 0;
    q->running = 1;
    q->stopping = 0;
    q->thread = sceKernelCreateThread("audioqueue", aq_thread, 0x10000100, 64 * 1024, 0, 0, NULL);
    sceKernelStartThread(q->thread, sizeof(q), &q);
    q->notify_stop = 2; // 2 = report "running" to the property listener on the next poll
    return 0;
}

static int aq_Pause(audio_queue *q) {
    q->paused = 1;
    return 0;
}

static void aq_join(audio_queue *q) {
    if (q->thread > 0) {
        sceKernelWaitThreadEnd(q->thread, NULL, NULL);
        sceKernelDeleteThread(q->thread);
        q->thread = 0;
    }
}

static int aq_Stop(audio_queue *q, int immediate) {
    if (!q->running)
        return 0;
    if (!immediate) {
        q->stopping = 1;
        return 0;
    }
    q->running = 0;
    aq_join(q);
    pthread_mutex_lock(&q->lock);
    q->nfifo = 0;
    q->head_off = 0;
    pthread_mutex_unlock(&q->lock);
    q->notify_stop = 1;
    return 0;
}

static int aq_Dispose(audio_queue *q, int immediate) {
    q->running = 0;
    aq_join(q);
    for (int i = 0; i < 4; i++)
        if (queues[i] == q)
            queues[i] = NULL;
    pthread_mutex_destroy(&q->lock);
    free(q); // ponytail: buffers belong to the queue on iOS; the game frees them itself before disposing
    return 0;
}

static int aq_Prime(audio_queue *q, uint32_t frames, uint32_t *prepared) {
    if (prepared)
        *prepared = 0;
    return 0;
}

SOFTFP static int aq_SetParameter(audio_queue *q, uint32_t param, float value) {
    if (param == 1) // kAudioQueueParam_Volume
        q->volume = value < 0 ? 0 : value > 1 ? 1 : value;
    return 0;
}

static int aq_GetProperty(audio_queue *q, uint32_t prop, void *data, uint32_t *size) {
    if (prop == FOURCC('a', 'q', 'r', 'n')) { // kAudioQueueProperty_IsRunning
        *(uint32_t *)data = q->running;
        *size = 4;
        return 0;
    }
    return kAudioFileUnsupportedPropertyError;
}

static int aq_SetProperty(audio_queue *q, uint32_t prop, const void *data, uint32_t size) { return 0; }

static int aq_AddPropertyListener(audio_queue *q, uint32_t prop, void *proc, void *user) {
    if (prop == FOURCC('a', 'q', 'r', 'n')) {
        q->listen_proc = proc;
        q->listen_user = user;
    }
    return 0;
}

// Run loop (main thread): finished buffers -> guest output callback; IsRunning changes -> listener.
void coreaudio_poll(void) {
    for (int i = 0; i < 4; i++) {
        audio_queue *q = queues[i];
        if (!q)
            continue;
        if (q->stopping == 0 && q->notify_stop == 1 && q->thread)
            aq_join(q); // drained stop: the mixing thread has already left its loop
        aq_buffer *done[AQ_MAX];
        pthread_mutex_lock(&q->lock);
        int n = q->ndone;
        memcpy(done, q->done, n * sizeof(done[0]));
        q->ndone = 0;
        pthread_mutex_unlock(&q->lock);
        for (int j = 0; j < n && queues[i] == q; j++)
            call_guest(q->proc, (uint32_t)q->user, (uint32_t)q, (uint32_t)done[j], 0);
        if (queues[i] == q && q->notify_stop && q->listen_proc) {
            q->notify_stop = 0;
            call_guest(q->listen_proc, (uint32_t)q->listen_user, (uint32_t)q, FOURCC('a', 'q', 'r', 'n'), 0);
        } else if (queues[i] == q) {
            q->notify_stop = 0;
        }
    }
}

#undef F
#define F(name, fn) {"_" #name, (uintptr_t)(fn)}

// ---------------------------------------------------------------- OpenAL (openal-soft from VitaSDK)

// Apple extension: buffer data stays owned by the app. openal-soft copies it, which is equivalent
// as long as the game does not rewrite the memory afterwards.
static void al_BufferDataStatic(ALuint buf, ALenum fmt, const ALvoid *data, ALsizei size, ALsizei freq) {
    alBufferData(buf, fmt, data, size, freq);
}

// Extensions are returned from our own import table so guest calls enter through ml_align_call.
static void *al_GetProcAddress(ALCdevice *dev, const ALCchar *name) {
    char n[64];
    snprintf(n, sizeof(n), "_%s", name);
    void *p = (void *)macho_resolve(n);
    l_info("alcGetProcAddress(%s) -> %p", name, p);
    return p;
}

#undef D
#define D(name) {"_" #name, (uintptr_t)(name)}

const import_entry coreaudio_imports[] = {
    D(alDeleteBuffers), D(alDeleteSources), D(alDistanceModel), D(alGenBuffers), D(alGenSources),
    D(alGetError), D(alGetSourcei), D(alListenerfv), D(alSource3f), D(alSourcePause), D(alSourcePlay),
    D(alSourceQueueBuffers), D(alSourceStop), D(alSourceUnqueueBuffers), D(alSourcef), D(alSourcefv),
    D(alSourcei), D(alcCloseDevice), D(alcCreateContext), D(alcDestroyContext),
    D(alcMakeContextCurrent), D(alcOpenDevice), D(alcProcessContext), D(alcSuspendContext),
    F(alcGetProcAddress, al_GetProcAddress), F(alBufferDataStatic, al_BufferDataStatic),
    F(AudioComponentFindNext, au_FindNext), F(AudioComponentInstanceNew, au_InstanceNew),
    F(AudioComponentInstanceDispose, au_Dispose), F(AudioUnitSetProperty, au_SetProperty),
    F(AudioUnitGetProperty, au_GetProperty), F(AudioUnitInitialize, ok), F(AudioUnitUninitialize, ok),
    F(AudioOutputUnitStart, au_Start), F(AudioOutputUnitStop, au_Stop), F(AudioUnitRender, au_Render),
    F(AudioSessionInitialize, ok), F(AudioSessionSetActive, ok), F(AudioSessionSetProperty, ok),
    F(AudioSessionGetProperty, as_GetProperty), F(AudioSessionAddPropertyListener, ok),
    // imported via __nl_symbol_ptr (function pointer): requires code stub, not data
    F(AudioSessionRemovePropertyListenerWithUserData, ok),
    // Real Racing HD: OpenAL extras, AudioFile (WAV + MP3) and AudioQueue
    D(alBufferData), D(alIsBuffer), D(alListener3i), D(alListenerf), D(alListeneriv), D(alcGetContextsDevice),
    F(AudioFileOpenURL, af_OpenURL), F(AudioFileClose, af_Close), F(AudioFileGetPropertyInfo, af_GetPropertyInfo),
    F(AudioFileGetProperty, af_GetProperty), F(AudioFileReadBytes, af_ReadBytes),
    F(AudioFileReadPackets, af_ReadPackets), F(AudioQueueNewOutput, aq_NewOutput),
    F(AudioQueueAllocateBuffer, aq_AllocateBuffer), F(AudioQueueFreeBuffer, aq_FreeBuffer),
    F(AudioQueueEnqueueBuffer, aq_EnqueueBuffer), F(AudioQueueStart, aq_Start), F(AudioQueuePause, aq_Pause),
    F(AudioQueueStop, aq_Stop), F(AudioQueueDispose, aq_Dispose), F(AudioQueuePrime, aq_Prime),
    F(AudioQueueSetParameter, aq_SetParameter), F(AudioQueueGetProperty, aq_GetProperty),
    F(AudioQueueSetProperty, aq_SetProperty), F(AudioQueueAddPropertyListener, aq_AddPropertyListener),
};
const int coreaudio_imports_count = sizeof(coreaudio_imports) / sizeof(coreaudio_imports[0]);
