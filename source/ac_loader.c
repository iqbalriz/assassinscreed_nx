/* ac_loader.c -- loading Assassin's Creed: Altair's Chronicles HD's one module,
 * libassissinscreed.so.
 *
 * The APK's lib/armeabi holds the whole game as one library (Gameloft's
 * engine: ARMv5 / Thumb code, OpenGL ES 1, 3266 exported names). Its imports
 * are few (105: libc and gl*), all served by the shims. The APK may hold more
 * (libsandhook*.so and a natives_sec_blob.dat of the cloner it went through,
 * an ad SDK): none of it is loaded, the Java never runs.
 *
 * Its natives are exported by name (Java_com_gameloft_..._GameRenderer_nativeInit
 * and so on), static, and are looked up once into g_n. MIT.
 */
#include <stdio.h>
#include <string.h>
#include <switch.h>

#include "ac.h"
#include "codespace.h"
#include "config.h"
#include "dcr_path.h"
#include "error.h"
#include "imports.h"
#include "so_util.h"
#include "util.h"

static so_module g_mod;
AcNatives g_n;

#define P "Java_com_gameloft_android_TBFV_GloftASCR_ML_"

static const struct {
  const char *sym;
  size_t off;
  int required;
} k_natives[] = {
#define NAT(s, f, req) {P s, offsetof(AcNatives, f), req}
    NAT("GameRenderer_nativeInit", RendererInit, 1),
    NAT("GameRenderer_nativeResize", RendererResize, 1),
    NAT("GameRenderer_nativeRender", RendererRender, 1),
    NAT("GameRenderer_nativeDone", RendererDone, 0),
    NAT("GameRenderer_nativePause", RendererPause, 0),
    NAT("GameRenderer_nativeResume", RendererResume, 0),
    NAT("GameGLSurfaceView_nativePause", ViewPause, 0),
    NAT("GameGLSurfaceView_nativeResume", ViewResume, 0),
    NAT("AssassinsCreed_nativeInit", GameInit, 1),
    NAT("AssassinsCreed_nativeCanInterrupt", GameCanInterrupt, 0),
    NAT("AssassinsCreed_nativeIsStateMainMenu", GameIsStateMainMenu, 0),
    NAT("AssassinsCreed_nativeOnKeyDown", GameKeyDown, 0),
    NAT("AssassinsCreed_nativeOnKeyUp", GameKeyUp, 0),
    NAT("AssassinsCreed_nativeOnTouch", GameTouch, 1),
    NAT("GLMediaPlayer_nativeInit", MediaInit, 0),
    NAT("GLMediaPlayer_nativeGetTotalSounds", MediaTotalSounds, 0),
    NAT("GLMediaPlayer_nativeGetTotalSoundsOfSameInstance", MediaTotalSoundsOfSameInstance, 0),
    NAT("GLMediaPlayer_nativeSetStopOnMusic", MediaSetStopOnMusic, 0),
#undef NAT
};

static int bind_natives(void) {
  int missing = 0;
  for (unsigned i = 0; i < sizeof k_natives / sizeof k_natives[0]; i++) {
    uintptr_t a = so_try_find_addr_rx(&g_mod, k_natives[i].sym);
    memcpy((uint8_t *)&g_n + k_natives[i].off, &a, sizeof a); /* a function pointer's slot */
    if (!a) {
      debugPrintf("[boot] %s native %s%s\n", k_natives[i].required ? "MISSING" : "no", k_natives[i].sym,
                  k_natives[i].required ? "" : " (optional)");
      missing += k_natives[i].required;
    }
  }
  return missing;
}

int ac_load_engine(void) {
  char path[512];
  snprintf(path, sizeof path, "%s/%s", dcr_game_root(), AC_LIB);
  int rc = so_load(&g_mod, path, NULL, PORT_SO_REGION_BYTES);
  if (rc < 0) {
    const char *why = rc == -1 ? "cannot open it, or it is not a 32-bit ARM ELF"
                    : rc == -2 ? "out of memory"
                    : rc == -3 ? "larger than PORT_SO_REGION_BYTES"
                    : rc == -4 ? "too many program headers" : "?";
    debugPrintf("[boot] so_load(%s) failed rc=%d: %s\n", path, rc, why);
    return -1;
  }
  so_relocate(&g_mod);
  const int missing = so_resolve(&g_mod, dcr_imports, dcr_imports_count, 1);
  debugPrintf("[boot] %s %u KB  staged %p -> %p  (%d unresolved imports)\n", g_mod.base_name,
              (unsigned)(g_mod.load_size >> 10), g_mod.load_base, g_mod.load_virtbase, missing);
  /* libgcc's __sync_* on ARM Linux call the kernel's user helpers through
   * literal pools: point any at the runtime's kuser.S. */
  so_fix_kuser_helpers(&g_mod);
  so_finalize(&g_mod);
  so_flush_caches(&g_mod);
  if (bind_natives()) {
    debugPrintf("[boot] %s is not the Assassin's Creed library this port knows\n", AC_LIB);
    return -2;
  }
  debugPrintf("[boot] engine: Assassin's Creed, mapped at %p\n", g_mod.load_virtbase);
  return 0;
}

/* Android runs a library's constructors inside System.loadLibrary. */
void ac_run_constructors(void) {
  so_execute_init_array(&g_mod);
  debugPrintf("[boot] %s constructors done\n", AC_LIB);
}
