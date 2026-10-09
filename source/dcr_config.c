/* dcr_config.c -- Assassin's Creed: Altair's Chronicles HD's settings:
 * config.ini's options, on the runtime's INI engine (runtime/source/rt_cfg.c).
 *
 * Written whole on the first start, the options a newer build adds appended at
 * the end. Booleans take true/false, yes/no, on/off, 1/0. Read once at
 * start-up: changes apply the next time the game starts. MIT.
 */
#include <switch.h>

#include "dcr_config.h"
#include "rt_cfg.h"
#include "util.h"

static DcrConfig g_cfg = {
    .language = 0,
    .frame_limit = 1,
    .volume = 100,
    .render_screen = 0,
    .touch = 1,
    .res_w = 1280,
    .res_h = 720,
    .boost = 1,
};

const DcrConfig *dcr_config(void) { return &g_cfg; }

static const CfgOpt k_opts[] = {
    {"game", "language", "en",
     "The game's language: en, de, fr, it, es, pt, pt-br or zh. The text is in\n"
     "# the game's own files (UK.bar, DE.bar, ...): the ones in your gameloft folder.",
     CFG_CHOICE, "en,de,fr,it,es,pt,pt-br,zh", &g_cfg.language},
    {"game", "frame_limit", "true",
     "Run at the game's own 20 frames a second. Off: as fast as the Switch can draw,\n"
     "# and the game runs far too fast.",
     CFG_BOOL, NULL, &g_cfg.frame_limit},
    {"sound", "volume", "100", "Sound volume, 0 to 100 (the console's volume applies too).",
     CFG_INT, NULL, &g_cfg.volume, 0, 100},
    {"graphics", "render_size", "800x480",
     "The size the game draws at. 800x480 is the game's own, the Switch scales it to\n"
     "# the screen. screen: the Switch's rendering size (see [display]); the game is\n"
     "# made for 800x480, so its picture and touch may not fit.",
     CFG_CHOICE, "800x480,screen", &g_cfg.render_screen},
    {"controls", "touch_screen", "true",
     "The touch screen plays the game (handheld mode).", CFG_BOOL, NULL, &g_cfg.touch},
    CFG_ROW_RESOLUTION("auto",
                       "Rendering resolution: 720, 1080 or auto (1080 if docked when the game\n"
                       "# starts), used when render_size = screen. The Switch scales the result."),
    CFG_ROW_BOOST("CPU at 1785 MHz while the game starts (until its first picture).", &g_cfg.boost),
    CFG_ROW_GL_SELFTEST(&g_cfg.gl_selftest),
    CFG_ROW_BOOT_LOG("Show the start-up log on screen at every launch. Off: the log appears only\n"
                     "# while something is being set up (first launch, a new APK or NRO).",
                     &g_cfg.boot_log),
    CFG_ROW_LOG_JNI("Write every Java method the game calls to debug.log (for bug reports).", &g_cfg.log_jni),
    {"debug", "log_input", "false",
     "Write the game's touches and keys to debug.log (for bug reports).",
     CFG_BOOL, NULL, &g_cfg.log_input},
    {"debug", "log_file_access", "false",
     "Write the game's file openings to debug.log, with the SD card path they became\n"
     "# (for bug reports; makes the log long).",
     CFG_BOOL, NULL, &g_cfg.log_files},
    {"debug", "gl_trace", "false",
     "Check every OpenGL call for errors and write the failing ones (their name and\n"
     "# arguments) to debug.log (for bug reports; it slows the game a little).",
     CFG_BOOL, NULL, &g_cfg.gl_trace},
    /* [config] version = 1: the engine's row, last (CfgTable.version) */
};

static void apply(void) {
  const RtConfig *rt = rt_config(); /* the resolution: rt_cfg.c sets the window to it */
  g_cfg.res_w = rt->res_w;
  g_cfg.res_h = rt->res_h;
  const int docked = appletGetOperationMode() == AppletOperationMode_Console;
  debugPrintf("[config] %dx%d (%s, %s); language %s, render size %s, touch %s, CPU boost %s\n", g_cfg.res_w,
              g_cfg.res_h, rt_config_get("display", "resolution"), docked ? "docked" : "handheld",
              rt_config_get("game", "language"), g_cfg.render_screen ? "screen" : "800x480",
              g_cfg.touch ? "on" : "off", g_cfg.boost ? "on" : "off");
}

static const CfgTable k_table = {
    .opts = k_opts,
    .nopts = CFG_COUNT(k_opts),
    .version = 1,
    .apply = apply,
};

void dcr_config_load(void) { rt_config_load(&k_table); }
