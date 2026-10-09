/* dcr_config.h -- the user's settings, from <game folder>/config.ini (dcr_config.c). */
#ifndef DCR_USER_CONFIG_H
#define DCR_USER_CONFIG_H

typedef struct {
  int language;      /* [game] language: the game's own number (ac_language) */
  int frame_limit;   /* [game] frame_limit: 20 fps like the Java side */
  int volume;        /* [sound] volume, 0..100 */
  int render_screen; /* [graphics] render_size: 0 = the game's 800x480, 1 = the screen's size */
  int touch;         /* [controls] touch_screen */
  int res_w, res_h;  /* [display] resolution */
  int boost;         /* [performance] boost_cpu_when_loading */
  int gl_selftest;   /* [debug] gl_selftest */
  int boot_log;      /* [debug] boot_log_on_screen */
  int log_jni;       /* [debug] log_java_calls */
  int log_input;     /* [debug] log_input */
  int log_files;     /* [debug] log_file_access */
  int gl_trace;      /* [debug] gl_trace */
} DcrConfig;

/* Read config.ini (writing it with the defaults, or adding missing options,
 * first). Early in main(); the defaults hold until then. */
void dcr_config_load(void);
const DcrConfig *dcr_config(void);

#endif
