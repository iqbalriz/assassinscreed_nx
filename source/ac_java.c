/* ac_java.c -- the Java side of Assassin's Creed: Altair's Chronicles HD, as
 * the library sees it.
 *
 * Gameloft's Java (AssassinsCreed, GameRenderer, GLMediaPlayer, GLResLoader,
 * the billing and licence classes, the cloner's and ad SDK's extras) does not
 * run here: ac_game.c does what the activity and the renderer did. What the
 * LIBRARY calls back through JNI is small, all of it static methods, found by
 * matching the Java's method names with the strings in the library:
 *
 *   AssassinsCreed (and GLGame)  Exit, OpenBrowser, Paused, the language,
 *                                demo / billing questions, a nomedia file
 *   GLMediaPlayer                the sound: SoundPool for effects, MediaPlayer
 *                                for music and voices, and the movie player
 *
 * The answers are those of the full game on a phone with no network and no
 * store: not a demo, no billing. The sound is ac_audio.c's (a later step: until
 * then every sound call is accepted and nothing plays). A movie is skipped: the
 * game is told it is not playing. Anything else the library asks for is logged
 * once as unhandled (jni_core.c): that list is the to-do list. MIT.
 */
#include <string.h>
#include <switch.h>

#include "ac.h"
#include "config.h"
#include "dcr_config.h"
#include "jni.h"
#include "util.h"

#define S "Ljava/lang/String;"

#define H(fn) static jvalue fn(JObj *self, const jvalue *a, const JMethod *m)

static int g_quit;

int ac_quit_requested(void) { return g_quit; }
int ac_language(void) { return dcr_config()->language; }

/* AssassinsCreed.Exit(): the game's own quit */
H(h_exit) {
  debugPrintf("[java] Exit(): closing the game\n");
  g_quit = 1;
  return jv_none();
}

H(h_lang) { return jv_i(ac_language()); }

/* GetDoubleOptionText1/2/3(): an empty byte[] -- there is no double option */
H(h_no_bytes) { return jv_l(jni_array('B', 0)); }

/* GLMediaPlayer.isSoundLoaded / isSoundLoadedBig: the sound is always "loaded", so
 * the game never waits for one */
H(h_loaded) { return jv_i(1); }

/* GLMediaPlayer: playSoundBig(id, volume, loop), stop/pause/resume, volume, isMediaPlaying */
H(h_play) { ac_audio_play(a[0].i, a[1].f, a[2].z); return jv_none(); }
H(h_stop) { ac_audio_stop(a[0].i); return jv_none(); }
H(h_pause) { ac_audio_set_paused(a[0].i, 1); return jv_none(); }
H(h_resume) { ac_audio_set_paused(a[0].i, 0); return jv_none(); }
H(h_volume) { ac_audio_set_volume(a[0].i, a[1].f); return jv_none(); }
H(h_playing) { return jv_i(ac_audio_playing(a[0].i)); }
H(h_stop_all) { ac_audio_stop_all(); return jv_none(); }
H(h_no_voice) { return jv_i(-1); }

#define AC AC_CLS_GAME
#define GG AC_CLS_GLGAME
#define MP AC_CLS_MEDIA

const JMethodDef jni_method_defs[] = {
    {AC, "Exit", "()V", h_exit},
    {AC, "ReturnmCurrentLang", "()I", h_lang},
    {AC, "GetDoubleOptionText1", "()[B", h_no_bytes},
    {AC, "GetDoubleOptionText2", "()[B", h_no_bytes},
    {AC, "GetDoubleOptionText3", "()[B", h_no_bytes},
    {AC, "IsDemo", "()I", jni_h_zero},
    {AC, "IsDoubleOption", "()I", jni_h_zero},
    {AC, "canLaunchDemo", "()I", jni_h_zero},
    {AC, "LaunchBilling", "()V", jni_h_void},
    {AC, "OpenBrowser", "(" S ")V", jni_h_void},
    {AC, "Paused", "()V", jni_h_void},
    {AC, "increaseLaunchTimes", "()V", jni_h_void},
    {AC, "createNomedia", "()V", jni_h_void},
    {AC, "sendAppToBackground", "()V", jni_h_void},
    {GG, "Exit", "()V", h_exit},
    {GG, "OpenBrowser", "(" S ")V", jni_h_void},
    {MP, "isSoundLoaded", "(II)I", h_loaded},
    {MP, "isSoundLoadedBig", "(I)I", h_loaded},
    {MP, "playSoundBig", "(IFZ)V", h_play},
    {MP, "playSoundBig", "(IFZZ)V", h_play},
    {MP, "stopSoundBig", "(I)V", h_stop},
    {MP, "unloadSoundBig", "(I)V", h_stop},
    {MP, "pauseSoundBig", "(I)V", h_pause},
    {MP, "resumeSoundBig", "(I)V", h_resume},
    {MP, "setVolumeBig", "(IF)V", h_volume},
    {MP, "isMediaPlaying", "(I)I", h_playing},
    {MP, "stopAllBig", "(I)V", h_stop_all},
    {MP, "stopAllSounds", "()V", h_stop_all},
    {MP, "stopAllPool", "(I)V", h_stop_all},
    {MP, "releaseSoundPool", "()V", h_stop_all},
    {MP, "destroySoundPool", "()V", h_stop_all},
    {MP, "playVoice", "(IIIF)I", h_no_voice},
    /* every other GLMediaPlayer call: accepted; 0 / nothing playing / a movie done */
    {MP, NULL, NULL, jni_h_zero},
    {NULL, NULL, NULL, NULL},
};

const JFieldDef jni_field_defs[] = {
    {NULL, NULL, NULL, 0, NULL},
};

const char *const jni_class_supers[][2] = {
    {NULL, NULL},
};

/* The game's own dex has no optional classes the library probes for. */
const char *const jni_missing_classes[] = {
    NULL,
};

void ac_java_init(void) {
  jni_init();
  debugPrintf("[java] JNI up\n");
}
