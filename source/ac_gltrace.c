/* ac_gltrace.c -- which GL call raised which error, and shader build results.
 *
 * Bring-up tool ([debug] gl_trace in config.ini). The runtime hands every gl*
 * function the engine looks up to port_gl_wrap() (gl_layer.h); here each is
 * wrapped, the real one called with the same arguments (all of them: a GL
 * function takes at most 9 words, in registers and on the stack, and returns
 * one in r0, floats as bits in core registers on this ABI), and glGetError()
 * asked right after. An error is logged with the function's name and its
 * arguments, so "unsupported function called" says WHICH one. After a
 * glCompileShader / glLinkProgram that failed, the driver's info log is
 * logged. A summary of the functions that raised errors goes to debug.log
 * with the 10-second report. MIT.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "dcr_config.h"
#include "gl_layer.h"
#include "ac.h"
#include "util.h"

#define AC_GW_SLOTS 256
#define AC_GW_ERR_LOG 200 /* errors logged with their arguments */

typedef uintptr_t (*gl_fn)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t,
                           uintptr_t, uintptr_t, uintptr_t);

static struct {
  const char *name;
  gl_fn real;
  unsigned long calls, errors;
  unsigned first_error;
} g_slot[AC_GW_SLOTS];
static int g_nslots;
static gl_fn g_get_error, g_get_shaderiv, g_get_shader_log, g_get_programiv, g_get_program_log;
static unsigned g_err_logged;
static unsigned g_build_logged;

static uintptr_t gw_call(int i, uintptr_t a0, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5,
                         uintptr_t a6, uintptr_t a7, uintptr_t a8, uintptr_t a9) {
  const uintptr_t r = g_slot[i].real(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9);
  g_slot[i].calls++;
  if (!g_get_error)
    return r;
  const unsigned e = (unsigned)g_get_error(0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
  if (e) {
    g_slot[i].errors++;
    if (!g_slot[i].first_error)
      g_slot[i].first_error = e;
    if (g_err_logged++ < AC_GW_ERR_LOG)
      debugPrintf("[gltrace] %s(%#x, %#x, %#x, %#x, %#x, %#x) -> error 0x%x\n", g_slot[i].name, (unsigned)a0,
                  (unsigned)a1, (unsigned)a2, (unsigned)a3, (unsigned)a4, (unsigned)a5, e);
  }
  /* a shader or program that did not build: the driver says why */
  if (g_build_logged < 12) {
    char log[512];
    int ok = 1;
    unsigned len = 0;
    if (!strcmp(g_slot[i].name, "glCompileShader") && g_get_shaderiv && g_get_shader_log) {
      int st = 1;
      g_get_shaderiv(a0, 0x8B81 /* COMPILE_STATUS */, (uintptr_t)&st, 0, 0, 0, 0, 0, 0, 0);
      if (!st) {
        ok = 0;
        log[0] = 0;
        g_get_shader_log(a0, sizeof log - 1, (uintptr_t)&len, (uintptr_t)log, 0, 0, 0, 0, 0, 0);
      }
    } else if (!strcmp(g_slot[i].name, "glLinkProgram") && g_get_programiv && g_get_program_log) {
      int st = 1;
      g_get_programiv(a0, 0x8B82 /* LINK_STATUS */, (uintptr_t)&st, 0, 0, 0, 0, 0, 0, 0);
      if (!st) {
        ok = 0;
        log[0] = 0;
        g_get_program_log(a0, sizeof log - 1, (uintptr_t)&len, (uintptr_t)log, 0, 0, 0, 0, 0, 0);
      }
    }
    if (!ok) {
      g_build_logged++;
      log[sizeof log - 1] = 0;
      debugPrintf("[gltrace] %s(%u) FAILED: %s\n", g_slot[i].name, (unsigned)a0, log);
    }
  }
  return r;
}

#include "ac_gltrace_slots.inc"

/* CALLBACK (gl_layer.h): a gl* function the engine looked up. */
uintptr_t port_gl_wrap(const char *name, uintptr_t real) {
  if (!real || !dcr_config()->gl_trace || strncmp(name, "gl", 2) || !strcmp(name, "glGetError"))
    return 0;
  for (int i = 0; i < g_nslots; i++)
    if (!strcmp(g_slot[i].name, name))
      return k_gw[i];
  if (g_nslots >= AC_GW_SLOTS)
    return 0;
  if (!g_get_error) { /* the helpers, looked up (unwrapped) once */
    g_get_error = (gl_fn)dcr_gl_lookup("glGetError");
    g_get_shaderiv = (gl_fn)dcr_gl_lookup("glGetShaderiv");
    g_get_shader_log = (gl_fn)dcr_gl_lookup("glGetShaderInfoLog");
    g_get_programiv = (gl_fn)dcr_gl_lookup("glGetProgramiv");
    g_get_program_log = (gl_fn)dcr_gl_lookup("glGetProgramInfoLog");
  }
  const int i = g_nslots++;
  g_slot[i].name = strdup(name);
  g_slot[i].real = (gl_fn)real;
  return k_gw[i];
}

/* The functions that raised errors, once, and then at the 10-second report. */
void ac_gltrace_report(void) {
  if (!dcr_config()->gl_trace)
    return;
  static int told;
  int n = 0;
  for (int i = 0; i < g_nslots; i++)
    if (g_slot[i].errors) {
      n++;
      if (told < 2)
        debugPrintf("[gltrace]   %-28s %lu error(s) of %lu call(s), first 0x%x\n", g_slot[i].name,
                    g_slot[i].errors, g_slot[i].calls, g_slot[i].first_error);
    }
  if (told < 2)
    debugPrintf("[gltrace] %d GL function(s) raised errors; %d wrapped\n", n, g_nslots);
  told++;
}
