/* ac_main.c -- Assassin's Creed: Altair's Chronicles HD's part of the boot: the
 * runtime's main() (runtime/source/main.c) does the rest -- the log,
 * config.ini, the NRO self-update, the APK found by what it holds -- and calls
 * these.
 *
 * The first launch (runtime/source/dcr_setup.c, from the plan below):
 * libassissinscreed.so out of lib/armeabi/ (PORT_ABI_DIR) and classes.txt (the
 * Java class names the dex files define), made again whenever the APK changes
 * (.setup stamps). The game's data is the player's own folder, gameloft/, in
 * the game folder. The bar, in permille:
 *     0- 200  (the APK found and checked: the runtime's main())
 *   200- 800  the library unpacked (by bytes written)
 *   800- 950  the Java class list
 *        1000 the game starts
 * MIT.
 */
#include "ac.h"
#include "config.h"
#include "dcr_path.h"
#include "dcr_setup.h"
#include "error.h"
#include "rt_boot.h"
#include "util.h"

static const char *const k_libs[] = {AC_LIB};

const RtSetupPlan port_setup_plan = {
    .libs = k_libs,
    .nlibs = 1,
    .libs_what = "Unpacking the game's library",
    .apk_requirement = "This port needs Assassin's Creed: Altair's Chronicles HD\n"
                       "(com.gameloft.android.TBFV.GloftASCR.ML): the APK of your own copy.",
    .libs_p0 = 200,
    .libs_p1 = 800,
    .classes_p0 = 800,
    .classes_p1 = 950,
};

/* From the APK to the game's first code: the library and classes.txt (again
 * when the APK changed), then the module loaded, relocated, resolved against
 * the shims and mapped as code. */
int port_load(const char *apk) {
  dcr_setup_from_apk(apk);
  if (ac_load_engine() != 0)
    fatal_error("Could not load the game engine from %s/" AC_LIB ".\n\n"
                "It is unpacked from the APK (lib/armeabi/) on launch: delete\n" AC_LIB
                " and .setup there to unpack it again. See debug.log.",
                dcr_game_root());
  return 0;
}

/* System.loadLibrary: the library's constructors; then the activity. */
void port_run(void) {
  ac_run_constructors();
  ac_game_run();
}

/* For the error screens. */
const char *port_apk_help(void) {
  return "Copy the APK of your own Assassin's Creed: Altair's Chronicles HD\n"
         "(armeabi) into /switch/" PORT_NAME ". Any file name ending in .apk works.\n"
         "The game's data is the \"gameloft\" folder: /switch/" PORT_NAME "/gameloft/games/assassinscreed.";
}
