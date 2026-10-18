/* ac_video.c -- the intro movie (intro.mp4).
 *
 * The game asks for it through GLMediaPlayer.loadMovie(name): on Android that
 * starts an activity with a VideoView over the game, the game's surface is
 * paused until it ends, and then the game goes on. Here ac_java.c takes the
 * request (ac_video_request), the frame loop (ac_game.c) pauses the engine and
 * plays the movie until it ends or a button or a touch skips it, then resumes.
 *
 * FFmpeg (ffmpeg32: the MP4 demuxer, the MPEG-4 Part 2 and AAC decoders)
 * decodes it on a thread of its own; each picture is converted to RGBA (NEON)
 * and drawn over the window by gl_blit.c; the sound is mixed into the game's
 * audio output (ac_video_mix). The pictures follow a clock started at the first
 * one shown; all the sound is decoded before the first picture (the file keeps
 * each half second of sound after its half second of pictures) and each buffer
 * of it is mixed from where the clock says it will be heard.
 *
 * Adapted from aks796's pvz_touch_nx (pvz_video.c, MIT). This file is
 * compiled with -fno-short-enums, as FFmpeg is. MIT.
 */
#include <GLES/gl.h>
#include <GLES/glext.h>
#include <arm_neon.h>
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "ac.h"
#include "config.h"
#include "dcr_path.h"
#include "gl_blit.h"
#include "gl_layer.h"
#include "util.h"

#if !DCR_VIDEO /* built without FFmpeg: the game's movie is skipped */
static char g_pending[256];
void ac_video_request(const char *name) { (void)name; }
int ac_video_pending(void) { return 0; }
int ac_video_begin(void) { return 0; }
int ac_video_active(void) { return 0; }
void ac_video_skip(void) {}
void ac_video_draw(void) {}
void ac_video_end(void) {}
void ac_video_mix(int16_t *out, int frames, int out_rate, float gain) {
  (void)out, (void)frames, (void)out_rate, (void)gain;
  (void)g_pending;
}
#else

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

void dcr_window_size(int *w, int *h);
void dcr_boost_hold(int on); /* dcr_boost.c */

#define NSLOT 4 /* pictures decoded ahead */
/* the track (libfaac, muxed with no edit list) keeps the encoder's priming on
 * its timeline: the sound proper starts this many frames after the pictures' 0 */
#define AAC_PRIMING 1024

static char g_pending[256];

static struct {
  Mutex lock;
  int open, playing, finished;
  volatile int stop, skip, eof;
  uint8_t *file;
  size_t file_len;
  int64_t file_pos;
  AVFormatContext *fmt;
  AVIOContext *io;
  AVCodecContext *vdec, *adec;
  int vs, as;
  double vtb; /* the video stream's time base, in seconds */
  Thread thread;
  int thread_on;
  int w, h;
  uint8_t *rgba[NSLOT];
  double pts[NSLOT];
  volatile int ready[NSLOT];
  int shown; /* the slot on the texture; -1 none */
  double shown_pts;
  int started; /* the clock runs */
  u64 t0;
  /* sound: all of it, s16 stereo at arate */
  int16_t *pcm;
  size_t pcm_cap, pcm_frames; /* frames decoded */
  int arate;
  double atb;   /* the sound stream's time base, in seconds */
  double apts0; /* the time of the first sound frame */
  double apos;  /* next frame to play */
  int amixed;   /* apos was set from the clock */
  int resyncs;
  volatile int audio_go;
} V = {.vs = -1, .as = -1, .shown = -1};

/* ------------------------------------------------------------ the file */
static int io_read(void *opaque, uint8_t *buf, int n) {
  (void)opaque;
  const int64_t left = (int64_t)V.file_len - V.file_pos;
  if (left <= 0)
    return AVERROR_EOF;
  if (n > left)
    n = (int)left;
  memcpy(buf, V.file + V.file_pos, (size_t)n);
  V.file_pos += n;
  return n;
}

static int64_t io_seek(void *opaque, int64_t off, int whence) {
  (void)opaque;
  if (whence == AVSEEK_SIZE)
    return (int64_t)V.file_len;
  whence &= ~AVSEEK_FORCE;
  int64_t p = whence == SEEK_SET ? off : whence == SEEK_CUR ? V.file_pos + off : (int64_t)V.file_len + off;
  if (p < 0 || p > (int64_t)V.file_len)
    return -1;
  V.file_pos = p;
  return p;
}

static uint8_t *read_file(const char *path, size_t *len) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return NULL;
  fseek(f, 0, SEEK_END);
  const long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint8_t *p = n > 0 ? malloc((size_t)n) : NULL;
  if (p && fread(p, 1, (size_t)n, f) != (size_t)n) {
    free(p);
    p = NULL;
  }
  fclose(f);
  *len = p ? (size_t)n : 0;
  return p;
}

/* where the game keeps it: the Java side's path is /sdcard/gameloft/games/
 * assassinscreed/<name>, which the game folder on the SD card holds */
static uint8_t *find_movie(const char *name, size_t *len) {
  char path[600];
  const char *base = strrchr(name, '/');
  base = base ? base + 1 : name;
  if (!strncmp(name, "/sdcard/", 8)) {
    snprintf(path, sizeof path, "%s/%s", dcr_game_root(), name + 8);
    uint8_t *p = read_file(path, len);
    if (p)
      return p;
  }
  snprintf(path, sizeof path, "%s/gameloft/games/assassinscreed/%s", dcr_game_root(), base);
  uint8_t *p = read_file(path, len);
  if (!p)
    debugPrintf("[video] %s: not found (%s)\n", name, path);
  return p;
}

/* ------------------------------------------------------------ pictures */
/* BT.601 (video range) YUV 4:2:0 -> RGBA, 16 pixels a step */
static void yuv_to_rgba(const AVFrame *f, uint8_t *out, int w, int h) {
  const int16x8_t k74 = vdupq_n_s16(74), k102 = vdupq_n_s16(102), k25 = vdupq_n_s16(25), k52 = vdupq_n_s16(52),
                  k129 = vdupq_n_s16(129), k16 = vdupq_n_s16(16), k128 = vdupq_n_s16(128);
  for (int y = 0; y < h; y++) {
    const uint8_t *py = f->data[0] + (size_t)y * f->linesize[0];
    const uint8_t *pu = f->data[1] + (size_t)(y / 2) * f->linesize[1];
    const uint8_t *pv = f->data[2] + (size_t)(y / 2) * f->linesize[2];
    uint8_t *d = out + (size_t)y * w * 4;
    int x = 0;
    for (; x + 16 <= w; x += 16) {
      const uint8x16_t yy = vld1q_u8(py + x);
      const uint8x8_t uu = vld1_u8(pu + x / 2), vv = vld1_u8(pv + x / 2);
      const uint8x8x2_t u2 = vzip_u8(uu, uu), v2 = vzip_u8(vv, vv);
      uint8x16x4_t px;
      for (int half = 0; half < 2; half++) {
        const uint8x8_t y8 = half ? vget_high_u8(yy) : vget_low_u8(yy);
        const int16x8_t c = vmulq_s16(vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(y8)), k16), k74);
        const int16x8_t d_ = vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(u2.val[half])), k128);
        const int16x8_t e = vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(v2.val[half])), k128);
        const int16x8_t r = vqaddq_s16(c, vmulq_s16(e, k102));
        const int16x8_t g = vqsubq_s16(vqsubq_s16(c, vmulq_s16(d_, k25)), vmulq_s16(e, k52));
        const int16x8_t b = vqaddq_s16(c, vmulq_s16(d_, k129));
        const uint8x8_t r8 = vqrshrun_n_s16(r, 6), g8 = vqrshrun_n_s16(g, 6), b8 = vqrshrun_n_s16(b, 6);
        if (half) {
          px.val[0] = vcombine_u8(vget_low_u8(px.val[0]), r8);
          px.val[1] = vcombine_u8(vget_low_u8(px.val[1]), g8);
          px.val[2] = vcombine_u8(vget_low_u8(px.val[2]), b8);
        } else {
          px.val[0] = vcombine_u8(r8, r8);
          px.val[1] = vcombine_u8(g8, g8);
          px.val[2] = vcombine_u8(b8, b8);
        }
      }
      px.val[3] = vdupq_n_u8(255);
      vst4q_u8(d + x * 4, px);
    }
    for (; x < w; x++) {
      const int c = (py[x] - 16) * 74, du = pu[x / 2] - 128, ev = pv[x / 2] - 128;
      int r = (c + 102 * ev + 32) >> 6, g = (c - 25 * du - 52 * ev + 32) >> 6, b = (c + 129 * du + 32) >> 6;
      d[x * 4 + 0] = (uint8_t)(r < 0 ? 0 : r > 255 ? 255 : r);
      d[x * 4 + 1] = (uint8_t)(g < 0 ? 0 : g > 255 ? 255 : g);
      d[x * 4 + 2] = (uint8_t)(b < 0 ? 0 : b > 255 ? 255 : b);
      d[x * 4 + 3] = 255;
    }
  }
}

static void put_picture(const AVFrame *f) {
  if (f->format != AV_PIX_FMT_YUV420P || f->width != V.w || f->height != V.h)
    return;
  int slot = -1;
  while (!V.stop) {
    mutexLock(&V.lock);
    for (int i = 0; i < NSLOT && slot < 0; i++)
      if (!V.ready[i] && i != V.shown)
        slot = i;
    mutexUnlock(&V.lock);
    if (slot >= 0)
      break;
    svcSleepThread(3000000ll);
  }
  if (slot < 0)
    return;
  yuv_to_rgba(f, V.rgba[slot], V.w, V.h);
  const int64_t ts = f->best_effort_timestamp != AV_NOPTS_VALUE ? f->best_effort_timestamp : f->pts;
  mutexLock(&V.lock);
  V.pts[slot] = ts == AV_NOPTS_VALUE ? 0 : (double)ts * V.vtb;
  V.ready[slot] = 1;
  mutexUnlock(&V.lock);
}

/* ------------------------------------------------------------ sound */
static void put_sound(const AVFrame *f) {
  if (!V.pcm || f->nb_samples <= 0)
    return;
  const int ch = f->ch_layout.nb_channels;
  size_t n = (size_t)f->nb_samples;
  if (!V.pcm_frames) {
    const int64_t ts = f->best_effort_timestamp != AV_NOPTS_VALUE ? f->best_effort_timestamp : f->pts;
    V.apts0 = ts == AV_NOPTS_VALUE ? 0 : (double)ts * V.atb;
  }
  mutexLock(&V.lock);
  if (V.pcm_frames + n > V.pcm_cap)
    n = V.pcm_cap - V.pcm_frames;
  int16_t *d = V.pcm + V.pcm_frames * 2;
  mutexUnlock(&V.lock);
  for (size_t i = 0; i < n; i++)
    for (int c = 0; c < 2; c++) {
      const int k = c < ch ? c : 0;
      float s;
      switch (f->format) {
      case AV_SAMPLE_FMT_FLTP: s = ((const float *)f->extended_data[k])[i]; break;
      case AV_SAMPLE_FMT_FLT: s = ((const float *)f->extended_data[0])[i * ch + k]; break;
      case AV_SAMPLE_FMT_S16P: s = ((const int16_t *)f->extended_data[k])[i] / 32768.0f; break;
      case AV_SAMPLE_FMT_S16: s = ((const int16_t *)f->extended_data[0])[i * ch + k] / 32768.0f; break;
      default: s = 0; break;
      }
      int v = (int)(s * 32767.0f);
      d[i * 2 + c] = (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
    }
  mutexLock(&V.lock);
  V.pcm_frames += n;
  mutexUnlock(&V.lock);
}

static double now_s(void) { return (double)armTicksToNs(armGetSystemTick() - V.t0) / 1e9; }

/* Into the game's output (48 kHz stereo s16), from ac_audio.c's mixer: the
 * sound due when this buffer is heard -- after the buffers queued before it
 * (2.5 on average), and the picture drawn now is seen a frame or two after its
 * draw. The first buffer starts there, the rest follow on; if they come apart
 * by more than 60 ms it starts there again. Sound not decoded is silence. */
void ac_video_mix(int16_t *out, int frames, int out_rate, float gain) {
  if (!V.audio_go || !V.pcm || V.arate <= 0)
    return;
  mutexLock(&V.lock);
  if (!V.audio_go || !V.pcm) { /* closed meanwhile */
    mutexUnlock(&V.lock);
    return;
  }
  const double step = (double)V.arate / (double)out_rate;
  const double ahead = 2.5 * frames / out_rate - 1.5 / 60.0;
  const double want = (now_s() + ahead - V.apts0) * V.arate;
  if (!V.amixed || fabs(V.apos - want) > 0.06 * V.arate) {
    if (V.amixed && V.resyncs++ < 8)
      debugPrintf("[video] sound %.0f ms off the pictures: back in step\n", (V.apos - want) * 1000.0 / V.arate);
    V.apos = want;
    V.amixed = 1;
  }
  const double have = (double)V.pcm_frames;
  for (int i = 0; i < frames; i++, V.apos += step) {
    if (V.apos < 0 || V.apos + 1 >= have)
      continue;
    const int i0 = (int)V.apos;
    const double t = V.apos - i0;
    for (int c = 0; c < 2; c++) {
      const double s = V.pcm[i0 * 2 + c] * (1 - t) + V.pcm[(i0 + 1) * 2 + c] * t;
      int v = out[i * 2 + c] + (int)(s * gain);
      out[i * 2 + c] = (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
    }
  }
  mutexUnlock(&V.lock);
}

/* ------------------------------------------------------------ decoding */
static void only_stream(int keep) {
  for (unsigned i = 0; i < V.fmt->nb_streams; i++)
    V.fmt->streams[i]->discard = (int)i == keep ? AVDISCARD_DEFAULT : AVDISCARD_ALL;
}

/* All of the sound, then back to the start for the pictures. 0, or -1: the
 * pictures cannot be read now. */
static int decode_sound(AVPacket *pkt, AVFrame *fr) {
  const u64 t = armGetSystemTick();
  only_stream(V.as);
  while (!V.stop && av_read_frame(V.fmt, pkt) >= 0) {
    if (pkt->stream_index == V.as && avcodec_send_packet(V.adec, pkt) >= 0)
      while (avcodec_receive_frame(V.adec, fr) == 0)
        put_sound(fr);
    av_packet_unref(pkt);
  }
  avcodec_send_packet(V.adec, NULL);
  while (avcodec_receive_frame(V.adec, fr) == 0)
    put_sound(fr);
  if (V.adec->codec_id == AV_CODEC_ID_AAC)
    V.apts0 -= (double)AAC_PRIMING / V.arate;
  only_stream(V.vs);
  const int r = av_seek_frame(V.fmt, V.vs, 0, AVSEEK_FLAG_BACKWARD);
  debugPrintf("[video] the sound first: %.2f s in %.0f ms (frame 0 at %.3f s); back to the start: %s\n",
              (double)V.pcm_frames / V.arate, (double)armTicksToNs(armGetSystemTick() - t) / 1e6, V.apts0,
              r >= 0 ? "ok" : av_err2str(r));
  return r >= 0 ? 0 : -1;
}

static void decode_thread(void *arg) {
  (void)arg;
  AVPacket *pkt = av_packet_alloc();
  AVFrame *fr = av_frame_alloc();
  int got_video = 0;
  if (pkt && fr && V.adec && decode_sound(pkt, fr) < 0)
    V.stop = 1;
  while (pkt && fr && !V.stop) {
    const int r = av_read_frame(V.fmt, pkt);
    if (r < 0) { /* the end: what the decoder holds */
      avcodec_send_packet(V.vdec, NULL);
      while (!V.stop && avcodec_receive_frame(V.vdec, fr) == 0)
        put_picture(fr), got_video++;
      break;
    }
    if (pkt->stream_index == V.vs && avcodec_send_packet(V.vdec, pkt) >= 0)
      while (!V.stop && avcodec_receive_frame(V.vdec, fr) == 0)
        put_picture(fr), got_video++;
    av_packet_unref(pkt);
  }
  av_frame_free(&fr);
  av_packet_free(&pkt);
  debugPrintf("[video] decoded %d pictures, %u sound frames%s\n", got_video, (unsigned)V.pcm_frames,
              V.stop ? " (stopped)" : "");
  V.eof = 1;
}

static void close_all(void) {
  V.stop = 1;
  if (V.thread_on) {
    threadWaitForExit(&V.thread);
    threadClose(&V.thread);
    V.thread_on = 0;
  }
  mutexLock(&V.lock);
  V.audio_go = 0;
  mutexUnlock(&V.lock);
  avcodec_free_context(&V.vdec);
  avcodec_free_context(&V.adec);
  if (V.fmt)
    avformat_close_input(&V.fmt);
  if (V.io) {
    av_freep(&V.io->buffer);
    avio_context_free(&V.io);
  }
  for (int i = 0; i < NSLOT; i++) {
    free(V.rgba[i]);
    V.rgba[i] = NULL;
    V.ready[i] = 0;
  }
  free(V.pcm);
  V.pcm = NULL;
  free(V.file);
  V.file = NULL;
  if (V.playing)
    dcr_boost_hold(0);
  V.open = V.playing = 0;
  V.shown = -1;
  V.vs = V.as = -1;
}

static AVCodecContext *open_decoder(AVStream *st) {
  const AVCodec *c = avcodec_find_decoder(st->codecpar->codec_id);
  AVCodecContext *ctx = c ? avcodec_alloc_context3(c) : NULL;
  if (!ctx || avcodec_parameters_to_context(ctx, st->codecpar) < 0 || avcodec_open2(ctx, c, NULL) < 0) {
    avcodec_free_context(&ctx);
    return NULL;
  }
  return ctx;
}

static int open_movie(const char *name) {
  if (V.open)
    close_all();
  mutexInit(&V.lock);
  V.stop = V.skip = V.eof = 0;
  V.finished = V.started = 0;
  V.apos = V.apts0 = 0;
  V.amixed = V.resyncs = 0;
  V.pcm_frames = 0;
  V.file = find_movie(name, &V.file_len);
  if (!V.file)
    return 0;
  V.file_pos = 0;
  av_log_set_level(AV_LOG_ERROR);
  unsigned char *buf = av_malloc(32768);
  V.io = buf ? avio_alloc_context(buf, 32768, 0, NULL, io_read, NULL, io_seek) : NULL;
  V.fmt = avformat_alloc_context();
  if (!V.io || !V.fmt) {
    close_all();
    return 0;
  }
  V.fmt->pb = V.io;
  if (avformat_open_input(&V.fmt, NULL, NULL, NULL) < 0 || avformat_find_stream_info(V.fmt, NULL) < 0) {
    debugPrintf("[video] %s: not a video FFmpeg reads here\n", name);
    close_all();
    return 0;
  }
  V.vs = av_find_best_stream(V.fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
  V.as = av_find_best_stream(V.fmt, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
  if (V.vs >= 0)
    V.vdec = open_decoder(V.fmt->streams[V.vs]);
  if (V.as >= 0)
    V.adec = open_decoder(V.fmt->streams[V.as]);
  if (!V.vdec) {
    debugPrintf("[video] %s: no picture decoder for it\n", name);
    close_all();
    return 0;
  }
  V.w = V.vdec->width;
  V.h = V.vdec->height;
  V.vtb = av_q2d(V.fmt->streams[V.vs]->time_base);
  for (int i = 0; i < NSLOT; i++)
    if (!(V.rgba[i] = memalign(64, (size_t)V.w * V.h * 4))) {
      close_all();
      return 0;
    }
  if (V.adec) {
    V.arate = V.adec->sample_rate;
    V.atb = av_q2d(V.fmt->streams[V.as]->time_base);
    const double secs = V.fmt->duration > 0 ? (double)V.fmt->duration / AV_TIME_BASE : 60.0;
    V.pcm_cap = (size_t)((secs + 2.0) * V.arate);
    V.pcm = malloc(V.pcm_cap * 4);
  }
  V.open = 1;
  debugPrintf("[video] %s: %dx%d %s, %s, %.1f s\n", name, V.w, V.h, avcodec_get_name(V.vdec->codec_id),
              V.adec ? avcodec_get_name(V.adec->codec_id) : "no sound",
              V.fmt->duration > 0 ? (double)V.fmt->duration / AV_TIME_BASE : 0.0);
  return 1;
}

/* ------------------------------------------------------------ the interface */
/* GLMediaPlayer.loadMovie(name), on the game's thread: only noted; the frame
 * loop starts it after the frame. */
void ac_video_request(const char *name) {
  snprintf(g_pending, sizeof g_pending, "%s", name ? name : "");
  debugPrintf("[video] the game asks for %s\n", g_pending);
}

int ac_video_pending(void) { return g_pending[0] != 0; }

/* Open the pending movie and start it: 1 playing, 0 not (the game goes on). */
int ac_video_begin(void) {
  char name[256];
  snprintf(name, sizeof name, "%s", g_pending);
  g_pending[0] = 0;
  if (!open_movie(name))
    return 0;
  V.stop = 0;
  /* a core of its own, as the game's threads: the decoder runs ahead */
  if (R_FAILED(threadCreate(&V.thread, decode_thread, NULL, NULL, 0x40000, 0x2C, -2)) ||
      R_FAILED(threadStart(&V.thread))) {
    debugPrintf("[video] no thread for the decoder\n");
    close_all();
    return 0;
  }
  V.thread_on = 1;
  V.playing = 1;
  dcr_boost_hold(1);
  return 1;
}

int ac_video_active(void) { return V.playing; }

void ac_video_skip(void) {
  if (V.playing && !V.skip) {
    V.skip = 1;
    debugPrintf("[video] skipped\n");
  }
}

void ac_video_end(void) {
  if (V.open)
    close_all();
}

/* ------------------------------------------------------------ drawing */
typedef void (*t_geti)(GLenum, GLint *);
typedef void (*t_bind)(GLenum, GLuint);
typedef void (*t_active)(GLenum);
typedef void (*t_gen)(GLsizei, GLuint *);
typedef void (*t_teximage)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *);
typedef void (*t_texsub)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *);
typedef void (*t_texparami)(GLenum, GLenum, GLint);
typedef void (*t_pixelstorei)(GLenum, GLint);
typedef GLenum (*t_geterror)(void);
typedef void (*t_clear)(GLbitfield);
typedef void (*t_clearcolor)(GLfloat, GLfloat, GLfloat, GLfloat);

static struct {
  t_geti GetIntegerv;
  t_bind BindTexture;
  t_active ActiveTexture;
  t_gen GenTextures;
  t_teximage TexImage2D;
  t_texsub TexSubImage2D;
  t_texparami TexParameteri;
  t_pixelstorei PixelStorei;
  t_geterror GetError;
  t_clear Clear;
  t_clearcolor ClearColor;
} G;
static GLuint g_tex;
static int g_tex_w, g_tex_h;
static int g_gl;      /* 0 not looked up, 1 ready, -1 unavailable */
static int g_method;  /* gl_blit.c's: 0 a quad, 1 glDrawTexOES */
static int g_checked; /* the picture was looked for on the screen (with g_method) */
static int g_logged;  /* the game's GL state is in the log */

#define L(name) (G.name = (void *)dcr_gl_lookup("gl" #name))
static int gl_setup(void) {
  L(GetIntegerv), L(BindTexture), L(ActiveTexture), L(GenTextures), L(TexImage2D);
  L(TexSubImage2D), L(TexParameteri), L(PixelStorei), L(GetError), L(Clear), L(ClearColor);
  if (!G.GetIntegerv || !G.BindTexture || !G.GenTextures || !G.TexImage2D || !G.TexSubImage2D || !G.TexParameteri ||
      !G.PixelStorei || !G.GetError || !G.Clear || !G.ClearColor || dcr_blit_setup("video") < 0) {
    debugPrintf("[video] the GL driver lacks a basic call: no pictures\n");
    return -1;
  }
  return 1;
}

static void finish(const char *why) {
  if (!V.playing)
    return;
  V.playing = 0;
  V.finished = 1;
  dcr_boost_hold(0);
  debugPrintf("[video] ended (%s)\n", why);
}

/* Once per way of drawing: the first picture with some light in the middle,
 * read back at five points from the screen just drawn. */
static void check_on_screen(int vw, int vh, GLenum err) {
  const uint8_t *mid = V.rgba[V.shown] + ((size_t)(V.h / 2) * V.w + V.w / 2) * 4;
  if (mid[0] < 64 && mid[1] < 64 && mid[2] < 64)
    return; /* too dark to tell (the fade-in): a later one */
  g_checked = 1;
  static const int k_at[5][2] = {{2, 2}, {1, 1}, {3, 1}, {1, 3}, {3, 3}}; /* quarters of the picture */
  char got_s[5][16], want_s[5][16];
  int worst = 0;
  for (int i = 0; i < 5; i++) {
    const int px = V.w * k_at[i][0] / 4, py = V.h * k_at[i][1] / 4;
    const uint8_t *want = V.rgba[V.shown] + ((size_t)py * V.w + px) * 4;
    uint8_t got[4];
    dcr_blit_read(vw * k_at[i][0] / 4, vh * k_at[i][1] / 4, got);
    const int d = abs(got[0] - want[0]) + abs(got[1] - want[1]) + abs(got[2] - want[2]);
    if (d > worst)
      worst = d;
    snprintf(got_s[i], sizeof got_s[i], "%d,%d,%d", got[0], got[1], got[2]);
    snprintf(want_s[i], sizeof want_s[i], "%d,%d,%d", want[0], want[1], want[2]);
  }
  const int ok = worst <= 60;
  debugPrintf("[video] picture on the screen (%s): %s -- screen %s %s %s %s %s, picture %s %s %s %s %s; GL error 0x%x\n",
              g_method ? "glDrawTexOES" : "quad", ok ? "yes" : "NO", got_s[0], got_s[1], got_s[2], got_s[3],
              got_s[4], want_s[0], want_s[1], want_s[2], want_s[3], want_s[4], (unsigned)err);
  if (!ok) {
    dcr_blit_log_state("video");
    if (g_method == 0) {
      g_method = 1;
      g_checked = 0;
      debugPrintf("[video] drawing it with glDrawTexOES instead\n");
    }
  }
}

/* Once a frame while it plays, on the engine's thread, before the swap: the
 * picture due, over the whole window. Ends the movie when it is over or
 * skipped. */
void ac_video_draw(void) {
  if (!V.playing)
    return;
  if (V.skip) {
    finish("skipped");
    return;
  }
  if (!g_gl)
    g_gl = gl_setup();
  if (g_gl < 0) {
    finish("no GL");
    return;
  }
  if (!g_logged) {
    g_logged = 1;
    dcr_blit_log_state("video");
  }

  /* the picture due: the newest one ready whose time has come (the clock
   * starts with the first) */
  mutexLock(&V.lock);
  int due = -1;
  if (!V.started) {
    double first = 1e9;
    for (int i = 0; i < NSLOT; i++)
      if (V.ready[i] && V.pts[i] < first)
        first = V.pts[i], due = i;
    if (due >= 0) {
      V.started = 1;
      V.t0 = armGetSystemTick() - armNsToTicks((u64)(first * 1e9));
      V.audio_go = 1;
    }
  } else {
    const double t = now_s();
    for (int i = 0; i < NSLOT; i++)
      if (V.ready[i] && V.pts[i] <= t && (due < 0 || V.pts[i] > V.pts[due]))
        due = i;
  }
  const int nothing = !V.started && due < 0 && V.eof;
  int any_left = 0;
  if (due >= 0) {
    for (int i = 0; i < NSLOT; i++) /* the ones it passed are done with */
      if (V.ready[i] && i != due && V.pts[i] < V.pts[due])
        V.ready[i] = 0;
    if (V.shown >= 0 && V.shown != due)
      V.ready[V.shown] = 0;
  }
  for (int i = 0; i < NSLOT; i++)
    any_left |= V.ready[i] && i != due && i != V.shown;
  mutexUnlock(&V.lock);
  if (nothing) {
    finish("no pictures");
    return;
  }

  if (due >= 0 && due != V.shown) { /* into the texture */
    GLint bound = 0, align = 4, active = GL_TEXTURE0;
    if (G.ActiveTexture) {
      G.GetIntegerv(GL_ACTIVE_TEXTURE, &active);
      G.ActiveTexture(GL_TEXTURE0);
    }
    G.GetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
    G.GetIntegerv(GL_UNPACK_ALIGNMENT, &align);
    while (G.GetError() != GL_NO_ERROR)
      ;
    if (!g_tex) {
      G.GenTextures(1, &g_tex);
      G.BindTexture(GL_TEXTURE_2D, g_tex);
      G.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      G.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      G.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      G.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    G.BindTexture(GL_TEXTURE_2D, g_tex);
    G.PixelStorei(GL_UNPACK_ALIGNMENT, 4);
    if (g_tex_w != V.w || g_tex_h != V.h) {
      G.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, V.w, V.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, V.rgba[due]);
      const GLenum err = G.GetError();
      debugPrintf("[video] texture %dx%d (texture %u): GL error 0x%x\n", V.w, V.h, g_tex, (unsigned)err);
      g_tex_w = V.w, g_tex_h = V.h;
    } else {
      G.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, V.w, V.h, GL_RGBA, GL_UNSIGNED_BYTE, V.rgba[due]);
    }
    G.PixelStorei(GL_UNPACK_ALIGNMENT, align);
    G.BindTexture(GL_TEXTURE_2D, (GLuint)bound);
    if (G.ActiveTexture)
      G.ActiveTexture((GLenum)active);
    if (V.shown < 0 || g_method == 1)
      dcr_blit_crop(g_tex, V.w, V.h);
    V.shown = due;
    V.shown_pts = V.pts[due];
  }
  /* the game is paused behind it: a black frame under the first picture */
  G.ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  G.Clear(GL_COLOR_BUFFER_BIT);
  if (V.shown >= 0) {
    int vw, vh;
    dcr_window_size(&vw, &vh);
    const GLenum err = dcr_blit(g_tex, 0, 0, vw, vh, 0, g_method);
    if (!g_checked)
      check_on_screen(vw, vh, err);
  }

  /* the end: everything decoded and shown, and the last picture's time
   * (a frame) past */
  if (V.eof && !any_left && V.started && now_s() > V.shown_pts + 0.1)
    finish("the end");
}

#endif /* DCR_VIDEO */
