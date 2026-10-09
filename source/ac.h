/* ac.h -- the Assassin's Creed side of the port: the pieces that stand in for
 * the game's Java (the activity, its GLSurfaceView renderer, the sound player),
 * talking to each other. MIT. */
#ifndef AC_H
#define AC_H
#include <stddef.h>
#include <stdint.h>

#include "jni.h"

/* ------------------------------------------------------------ the library */
/* The natives libassissinscreed.so exports. Every one is STATIC: (env, class,
 * args...). NULL when the library lacks one. Signatures from the APK's
 * classes.dex. ac_loader.c fills this in. */
typedef struct {
  /* GameRenderer */
  void (*RendererInit)(void *env, void *cls, jint a, jint b);
  void (*RendererResize)(void *env, void *cls, jint w, jint h);
  void (*RendererRender)(void *env, void *cls);
  void (*RendererDone)(void *env, void *cls);
  void (*RendererPause)(void *env, void *cls, jint a);
  void (*RendererResume)(void *env, void *cls, jint a);
  /* GameGLSurfaceView */
  void (*ViewPause)(void *env, void *cls, jint a);
  void (*ViewResume)(void *env, void *cls, jint a);
  /* AssassinsCreed */
  void (*GameInit)(void *env, void *cls);
  jint (*GameCanInterrupt)(void *env, void *cls);
  jint (*GameIsStateMainMenu)(void *env, void *cls);
  void (*GameKeyDown)(void *env, void *cls, jint code);
  void (*GameKeyUp)(void *env, void *cls, jint code);
  /* pointer id, state (1 down, 0 up, 2 move), x, y: in the game's 800 x 480 */
  void (*GameTouch)(void *env, void *cls, jint id, jint state, jint x, jint y);
  /* GLMediaPlayer */
  void (*MediaInit)(void *env, void *cls, jint a);
  jint (*MediaTotalSounds)(void *env, void *cls);
  jint (*MediaTotalSoundsOfSameInstance)(void *env, void *cls);
  void (*MediaSetStopOnMusic)(void *env, void *cls, jint on);
} AcNatives;

extern AcNatives g_n;

int ac_load_engine(void);        /* load, relocate, resolve, map the module; 0 on success */
void ac_run_constructors(void);

/* ---------------------------------------------------------- the Java side */
void ac_java_init(void);
/* ac_audio.c: GLMediaPlayer's sounds, through audout */
int ac_audio_init(void);
void ac_audio_shutdown(void);
void ac_audio_pause(int paused);
void ac_audio_play(int id, float volume, int loop);
void ac_audio_stop(int id);
void ac_audio_stop_all(void);
void ac_audio_set_paused(int id, int paused);
void ac_audio_set_volume(int id, float volume);
int ac_audio_playing(int id);

int ac_quit_requested(void);     /* the game asked for AssassinsCreed.Exit() */
int ac_language(void);           /* ReturnmCurrentLang(): 0 en, 1 de, 2 fr, 3 it, 4 es, 5 pt, 6 pt-BR, 7 zh, 8 ja, 9 ko */

/* ------------------------------------------------------------- the game */
int ac_game_run(void);

/* ac_gltrace.c: the summary of the GL functions that raised errors */
void ac_gltrace_report(void);

#endif /* AC_H */
