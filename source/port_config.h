/* port_config.h -- Assassin's Creed: Altair's Chronicles HD's settings for the
 * android32 runtime.
 *
 * Macros only: the runtime's C files, its assembly and the launcher all read
 * this (runtime/source/rt_settings.h). What each setting does is next to its
 * default in the runtime; runtime/docs/ lists them all. MIT.
 */
#ifndef PORT_CONFIG_H
#define PORT_CONFIG_H

/* ------------------------------------------------------------------ the game */
#define PORT_TITLE    "Assassin's Creed: Altair's Chronicles HD"
#define PORT_NAME     "assassinscreed_nx"
#define PORT_PACKAGE  "com.gameloft.android.TBFV.GloftASCR.ML"
#define PORT_BANNER   "assassinscreed_nx: Assassin's Creed: Altair's Chronicles HD (Gameloft, armeabi)"

/* The game's library is in lib/armeabi/ of the APK (ARMv5 code, which the
 * Switch's AArch32 mode runs), not in armeabi-v7a. */
#define PORT_ABI_DIR "lib/armeabi/"
/* libassissinscreed.so is 1 MB on disk; each module is mapped into its own
 * region (a larger one is refused by so_load, -3), its bss included */
#define PORT_SO_REGION_BYTES (48u * 1024 * 1024)

/* The APK, by what is in it: the game's library. Nothing else is needed or
 * looked at: an APK that went through a cloner or a protector (extra
 * libsandhook*.so, a natives_sec_blob.dat, ad SDKs) is as good as the
 * original, because only this library is loaded -- the Java does not run. */
#define PORT_APK_DESC "Assassin's Creed: Altair's Chronicles HD (lib/armeabi/libassissinscreed.so)"
#define PORT_APK_ROLES                                                                       \
  {.what = "the game",                                                                       \
   .need = (const char *const[]){"lib/armeabi/libassissinscreed.so", NULL},                  \
   .flags = RT_APK_HIGHEST_VERSION}

/* The game reads and writes /sdcard/gameloft/games/assassinscreed/: the player's
 * own "gameloft" folder, in the game folder on the SD card. */
#define RT_PATH_SD_SUBDIRS "gameloft"
#define RT_PATH_EXTRA_DIRS "gameloft", "gameloft/games", "gameloft/games/assassinscreed"

/* ------------------------------------------------------------------ launcher */
#define PORT_LAUNCHER_START_NOTE "(the first start unpacks the game's library from the APK)"
#define PORT_LAUNCHER_BYLINE     "by Iqbalriz (the Switch port); the game by Gameloft"

/* ------------------------------------------------------------------ frames, input */
#define RT_BOOST_WATCH_THREAD 1  /* boost the long (loading) frames */
#define RT_PAD_MAX_PLAYERS    1

#endif
