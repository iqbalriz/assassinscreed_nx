/* config.h -- Assassin's Creed: Altair's Chronicles HD's own constants (the
 * runtime's settings are in port_config.h).
 *
 * com.gameloft.android.TBFV.GloftASCR.ML, armeabi: libassissinscreed.so is the
 * whole game (OpenGL ES 1, 800x480). Its Java is Gameloft's launcher; what the
 * library asks of it is small -- the sound (GLMediaPlayer) and a few activity
 * methods -- and is done here. MIT.
 */
#ifndef AC_CONFIG_H
#define AC_CONFIG_H

#include "rt_settings.h"

#define AC_LIB "libassissinscreed.so"
#define AC_PACKAGE PORT_PACKAGE

/* The Java classes whose natives the library exports */
#define AC_PKG "com/gameloft/android/TBFV/GloftASCR/ML/"
#define AC_CLS_GAME     AC_PKG "AssassinsCreed"
#define AC_CLS_RENDERER AC_PKG "GameRenderer"
#define AC_CLS_VIEW     AC_PKG "GameGLSurfaceView"
#define AC_CLS_MEDIA    AC_PKG "GLMediaPlayer"
#define AC_CLS_GLGAME   AC_PKG "GLGame"

/* The game's own picture size: GameRenderer.onSurfaceChanged hands the library
 * 800 x 480 whatever the screen is (nativeResize(800, 480)). */
#define AC_GAME_W 800
#define AC_GAME_H 480

/* Where the player's files are, as the game names them (dcr_path.c maps it) */
#define AC_DATA_DIR "/sdcard/gameloft/games/assassinscreed"

#endif /* AC_CONFIG_H */
