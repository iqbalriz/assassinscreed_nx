/* ac_audio.c -- the game's GLMediaPlayer, played through audout.
 *
 * Gameloft's Java makes one android.media.MediaPlayer per sound id for every
 * sound of the game (music, effects and voices alike): file raw_NNNN.ogg in the
 * game folder, started with playSoundBig(id, volume, loop), and the library
 * polls isMediaPlaying(id) to know when one is over. The Java methods are
 * answered here (ac_java.c routes them): the ogg is decoded as it plays
 * (stb_vorbis, a short chunk at a time) and the runtime's mixer pump
 * (rt_audout.c) sums the playing voices, resampled to the device's 48 kHz.
 * MIT (stb_vorbis: public domain / MIT, Sean Barrett).
 */
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <switch.h>

#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis.h"

#include "ac.h"
#include "config.h"
#include "dcr_config.h"
#include "dcr_path.h"
#include "rt_audout.h"
#include "util.h"

#define MAX_VOICES 16
#define CHUNK 2048 /* frames decoded at a time */

typedef struct {
  int id; /* -1: free */
  stb_vorbis *vb;
  int ch, rate, loop, paused;
  float gain;
  double pos, step; /* in source frames, relative to buf[0]; source frames per output frame */
  int n;            /* frames in buf */
  uint32_t age;
  int16_t buf[(CHUNK + 1) * 2];
} Voice;

static Voice g_voices[MAX_VOICES];
static Mutex g_lock;
static uint32_t g_started;
static unsigned g_out_rate = 48000;
static float g_master = 1.0f;
static int g_ready;

static void voice_free(Voice *v) {
  if (v->vb)
    stb_vorbis_close(v->vb);
  v->vb = NULL;
  v->id = -1;
}

/* the next chunk of the ogg, the last frame kept in front for the interpolation */
static int refill(Voice *v) {
  const int ch = v->ch;
  int base = 0;
  if (v->n > 0) {
    memcpy(v->buf, v->buf + (size_t)(v->n - 1) * ch, sizeof(int16_t) * ch);
    base = 1;
  }
  int got = stb_vorbis_get_samples_short_interleaved(v->vb, ch, v->buf + base * ch, CHUNK * ch);
  if (got == 0 && v->loop && stb_vorbis_seek_start(v->vb))
    got = stb_vorbis_get_samples_short_interleaved(v->vb, ch, v->buf + base * ch, CHUNK * ch);
  if (got == 0)
    return 0;
  if (base)
    v->pos -= (double)(v->n - 1);
  else
    v->pos = 0;
  v->n = base + got;
  return 1;
}

static void mix(int16_t *out, int frames, void *ud) {
  (void)ud;
  static int32_t acc[RT_AUDOUT_FRAMES * 2];
  memset(acc, 0, sizeof(int32_t) * (size_t)frames * 2);
  mutexLock(&g_lock);
  for (int k = 0; k < MAX_VOICES; k++) {
    Voice *v = &g_voices[k];
    if (v->id < 0 || v->paused)
      continue;
    const float g = v->gain * g_master * 0.9f;
    for (int f = 0; f < frames; f++) {
      while ((int)v->pos + 1 >= v->n) {
        if (!refill(v)) {
          voice_free(v);
          goto next;
        }
      }
      const int i0 = (int)v->pos;
      const float t = (float)(v->pos - (double)i0);
      float l, r;
      if (v->ch == 2) {
        l = v->buf[i0 * 2] + (v->buf[i0 * 2 + 2] - v->buf[i0 * 2]) * t;
        r = v->buf[i0 * 2 + 1] + (v->buf[i0 * 2 + 3] - v->buf[i0 * 2 + 1]) * t;
      } else {
        l = r = v->buf[i0] + (v->buf[i0 + 1] - v->buf[i0]) * t;
      }
      acc[f * 2] += (int32_t)lrintf(l * g);
      acc[f * 2 + 1] += (int32_t)lrintf(r * g);
      v->pos += v->step;
    }
  next:;
  }
  mutexUnlock(&g_lock);
  for (int i = 0; i < frames * 2; i++)
    out[i] = (int16_t)(acc[i] > 32767 ? 32767 : acc[i] < -32768 ? -32768 : acc[i]);
}

int ac_audio_init(void) {
  mutexInit(&g_lock);
  for (int i = 0; i < MAX_VOICES; i++)
    g_voices[i].id = -1;
  g_master = (float)dcr_config()->volume / 100.0f;
  if (rt_audout_open() != 0)
    return -1;
  g_out_rate = rt_audout_rate();
  if (rt_audout_pump_start(mix, NULL, 0x2A, -2) != 0) {
    debugPrintf("[audio] mixer thread -- no sound\n");
    return -1;
  }
  g_ready = 1;
  debugPrintf("[audio] %d voices, mixed at %u Hz, volume %d%%\n", MAX_VOICES, g_out_rate, dcr_config()->volume);
  return 0;
}

void ac_audio_shutdown(void) {
  if (!g_ready)
    return;
  g_ready = 0;
  rt_audout_pump_stop();
  rt_audout_close();
  mutexLock(&g_lock);
  for (int i = 0; i < MAX_VOICES; i++)
    if (g_voices[i].id >= 0)
      voice_free(&g_voices[i]);
  mutexUnlock(&g_lock);
}

void ac_audio_pause(int paused) {
  if (g_ready)
    rt_audout_pause(paused);
}

static Voice *find(int id) {
  for (int i = 0; i < MAX_VOICES; i++)
    if (g_voices[i].id == id)
      return &g_voices[i];
  return NULL;
}

void ac_audio_play(int id, float volume, int loop) {
  if (!g_ready || id < 0)
    return;
  char real[DCR_PATH_MAX], path[DCR_PATH_MAX + 32];
  snprintf(path, sizeof path, "%s/raw_%04d.ogg", dcr_translate_path(AC_DATA_DIR, real, sizeof real), id);
  int err = 0;
  stb_vorbis *vb = stb_vorbis_open_filename(path, &err, NULL);
  if (dcr_config()->log_input)
    debugPrintf("[audio] play %d vol %.2f%s: %s\n", id, (double)volume, loop ? " loop" : "", vb ? "ok" : "cannot open");
  if (!vb)
    return;
  const stb_vorbis_info info = stb_vorbis_get_info(vb);
  if (info.channels < 1 || info.channels > 2) {
    stb_vorbis_close(vb);
    return;
  }
  mutexLock(&g_lock);
  Voice *v = find(id);
  if (!v) {
    for (int i = 0; i < MAX_VOICES && !v; i++)
      if (g_voices[i].id < 0)
        v = &g_voices[i];
  }
  if (!v) { /* all busy: the oldest goes */
    v = &g_voices[0];
    for (int i = 1; i < MAX_VOICES; i++)
      if ((int32_t)(g_voices[i].age - v->age) < 0)
        v = &g_voices[i];
  }
  voice_free(v);
  v->id = id;
  v->vb = vb;
  v->ch = info.channels;
  v->rate = (int)info.sample_rate;
  v->loop = loop;
  v->paused = 0;
  v->gain = volume < 0 ? 0 : volume;
  v->pos = 0;
  v->n = 0;
  v->step = (double)v->rate / (double)g_out_rate;
  v->age = ++g_started;
  mutexUnlock(&g_lock);
}

void ac_audio_stop(int id) {
  if (!g_ready)
    return;
  mutexLock(&g_lock);
  Voice *v = find(id);
  if (v)
    voice_free(v);
  mutexUnlock(&g_lock);
}

void ac_audio_stop_all(void) {
  if (!g_ready)
    return;
  mutexLock(&g_lock);
  for (int i = 0; i < MAX_VOICES; i++)
    if (g_voices[i].id >= 0)
      voice_free(&g_voices[i]);
  mutexUnlock(&g_lock);
}

void ac_audio_set_paused(int id, int paused) {
  if (!g_ready)
    return;
  mutexLock(&g_lock);
  Voice *v = find(id);
  if (v)
    v->paused = paused;
  mutexUnlock(&g_lock);
}

void ac_audio_set_volume(int id, float volume) {
  if (!g_ready)
    return;
  mutexLock(&g_lock);
  Voice *v = find(id);
  if (v)
    v->gain = volume < 0 ? 0 : volume;
  mutexUnlock(&g_lock);
}

/* MediaPlayer.isPlaying(): started and not over (or paused) */
int ac_audio_playing(int id) {
  if (!g_ready)
    return 0;
  mutexLock(&g_lock);
  const Voice *v = find(id);
  const int on = v && !v->paused;
  mutexUnlock(&g_lock);
  return on;
}
