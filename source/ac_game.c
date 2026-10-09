/* ac_game.c -- plays the part of the game's Java: the activity
 * (AssassinsCreed) and its GLSurfaceView renderer (GameRenderer).
 *
 * What the Java does, in order (read out of the APK's classes.dex), and what
 * this file does for it:
 *
 *   GameRenderer.onSurfaceCreated   nativeInit(1, 1) -- the library keeps the
 *                                   JNIEnv it is given here and uses it later;
 *                                   AssassinsCreed.nativeInit(); GLMediaPlayer.init():
 *                                   nativeInit(0), the sound counts
 *   GameRenderer.onSurfaceChanged   nativeResize(800, 480) -- the game's own size
 *   GameRenderer.onDrawFrame        nativeRender(), once a frame
 *   AssassinsCreed.onTouchEvent     nativeOnTouch(pointer id, state, x, y), state
 *                                   1 down, 0 up, 2 move, per finger
 *   AssassinsCreed.onKeyDown / Up   nativeOnKeyDown / Up(Android key code)
 *
 * On Android the UI thread (input) and the GL thread (frames) run side by side;
 * here one thread does both, in the order a frame sees them: input, then the
 * frame, then the present. The library is never called from two threads at
 * once. MIT.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <switch.h>

#include "ac.h"
#include "config.h"
#include "dcr_boost.h"
#include "dcr_config.h"
#include "dcr_path.h"
#include "dcr_setup.h"
#include "error.h"
#include "gl_layer.h"
#include "jni.h"
#include "rt_applet.h"
#include "rt_pad.h"
#include "rt_window.h"
#include "util.h"
#include "watchdog.h"

#define ENV g_jni_env

/* The shared EGL layer (gl_mesa.c / gl_null.c), as plain C: both define
 * these with the same register-level signatures. */
typedef int32_t fEGLint;
void *b_eglGetDisplay(void *native);
unsigned b_eglInitialize(void *d, fEGLint *maj, fEGLint *min);
unsigned b_eglChooseConfig(void *d, const fEGLint *attrs, void **cfgs, fEGLint cap, fEGLint *num);
void *b_eglCreateWindowSurface(void *d, void *cfg, void *win, const fEGLint *attrs);
void *b_eglCreateContext(void *d, void *cfg, void *share, const fEGLint *attrs);
unsigned b_eglMakeCurrent(void *d, void *draw, void *read, void *ctx);
unsigned b_eglSwapInterval(void *d, fEGLint interval);
unsigned b_eglSwapBuffers(void *d, void *s);
unsigned b_eglDestroySurface(void *d, void *s);
unsigned b_eglDestroyContext(void *d, void *c);
unsigned b_eglTerminate(void *d);
fEGLint b_eglGetError(void);

#define EGL_NONE 0x3038
#define EGL_RED_SIZE 0x3024
#define EGL_GREEN_SIZE 0x3023
#define EGL_BLUE_SIZE 0x3022
#define EGL_DEPTH_SIZE 0x3025
#define EGL_STENCIL_SIZE 0x3026
#define EGL_SURFACE_TYPE 0x3033
#define EGL_WINDOW_BIT 0x0004
#define EGL_RENDERABLE_TYPE 0x3040
#define EGL_OPENGL_ES_BIT 0x0001
#define EGL_CONTEXT_CLIENT_VERSION 0x3098
#define EGL_OPENGL_ES_API 0x30A0

static int g_w, g_h; /* the rendering size: the game's 800x480 or the screen's */
static void *g_dpy, *g_surf, *g_ctx;
static int g_engine_up;
static void *g_cls_renderer, *g_cls_game, *g_cls_media, *g_cls_view;

/* For the watchdog: frames presented, and whether frames are expected. */
uint64_t dcr_boot_frames(void) { return dcr_gl_frames(); }

/* The runtime logs the translation of the paths this answers 1 for, and the
 * first opens (dcr_path.c, bionic_io.c): the game's own files, once asked for. */
int dcr_path_traced(const char *p) {
  if (!p)
    return 0;
  if (strstr(p, ".apk"))
    return 1;
  return dcr_config()->log_files && strstr(p, "gameloft");
}

/* ----------------------------------------------------------- touch */
/* The game is played by touch only: its HUD (the stick, the buttons) is a set
 * of screen zones, and appKeyPressed does nothing. A pad is therefore turned
 * into touches at those zones' places (800x480). Real fingers and the pad's
 * virtual ones share the game's pointer slots. */
#define MAX_FINGERS 5 /* Android's pointer ids; the game's CTouchManager keeps a few */
static struct {
  int on, virt;
  u32 id;
  int x, y;
} g_finger[MAX_FINGERS];
static int g_touch_ready;

static void touch_init(void) {
  hidInitializeTouchScreen();
  g_touch_ready = 1;
}

/* a pad-made finger: a slot from the pool, held until it is let go */
static int vt_down(int x, int y) {
  for (int k = 0; k < MAX_FINGERS; k++)
    if (!g_finger[k].on) {
      g_finger[k].on = 1, g_finger[k].virt = 1, g_finger[k].id = 0xffff0000u + (u32)k;
      g_finger[k].x = x, g_finger[k].y = y;
      if (dcr_config()->log_input)
        debugPrintf("[input] pad touch %d down %d,%d\n", k, x, y);
      g_n.GameTouch(ENV, g_cls_game, k, 1, x, y);
      return k;
    }
  return -1;
}

static void vt_move(int slot, int x, int y) {
  if (slot < 0 || (g_finger[slot].x == x && g_finger[slot].y == y))
    return;
  g_finger[slot].x = x, g_finger[slot].y = y;
  g_n.GameTouch(ENV, g_cls_game, slot, 2, x, y);
}

static void vt_up(int slot) {
  if (slot < 0 || !g_finger[slot].on)
    return;
  if (dcr_config()->log_input)
    debugPrintf("[input] pad touch %d up %d,%d\n", slot, g_finger[slot].x, g_finger[slot].y);
  g_n.GameTouch(ENV, g_cls_game, slot, 0, g_finger[slot].x, g_finger[slot].y);
  g_finger[slot].on = 0, g_finger[slot].virt = 0;
}

/* HidTouchScreenState -> nativeOnTouch(pointer id, state, x, y), per finger
 * (AssassinsCreed.onTouchEvent): the 1280x720 touch screen mapped onto the
 * game's picture. */
static void touch_poll(void) {
  if (!g_touch_ready || !dcr_config()->touch)
    return;
  HidTouchScreenState st;
  memset(&st, 0, sizeof st);
  if (!hidGetTouchScreenStates(&st, 1))
    return;
  int seen[MAX_FINGERS] = {0};
  for (s32 i = 0; i < st.count; i++) {
    const int x = (int)((int64_t)st.touches[i].x * g_w / 1280), y = (int)((int64_t)st.touches[i].y * g_h / 720);
    int slot = -1, free_slot = -1;
    for (int k = 0; k < MAX_FINGERS; k++) {
      if (g_finger[k].on && !g_finger[k].virt && g_finger[k].id == st.touches[i].finger_id)
        slot = k;
      else if (!g_finger[k].on && free_slot < 0)
        free_slot = k;
    }
    if (slot < 0) {
      if (free_slot < 0)
        continue;
      slot = free_slot;
      g_finger[slot].on = 1, g_finger[slot].virt = 0, g_finger[slot].id = st.touches[i].finger_id;
      g_finger[slot].x = x, g_finger[slot].y = y;
      if (dcr_config()->log_input)
        debugPrintf("[input] touch %d down %d,%d\n", slot, x, y);
      g_n.GameTouch(ENV, g_cls_game, slot, 1, x, y);
    } else if (g_finger[slot].x != x || g_finger[slot].y != y) {
      g_n.GameTouch(ENV, g_cls_game, slot, 2, x, y);
      g_finger[slot].x = x, g_finger[slot].y = y;
    }
    seen[slot] = 1;
  }
  for (int k = 0; k < MAX_FINGERS; k++)
    if (g_finger[k].on && !g_finger[k].virt && !seen[k]) {
      if (dcr_config()->log_input)
        debugPrintf("[input] touch %d up %d,%d\n", k, g_finger[k].x, g_finger[k].y);
      g_n.GameTouch(ENV, g_cls_game, k, 0, g_finger[k].x, g_finger[k].y);
      g_finger[k].on = 0;
    }
}

/* ----------------------------------------------------------------- pad */
static PadState g_pad, g_pad_hh;
static int g_pad_ready, g_pad_present;

/* The HUD's places in the game's 800x480 picture (read off its screens). */
#define JOY_X 109 /* the stick's centre (CInputJoystick) */
#define JOY_Y 383
#define JOY_R 55 /* how far a full stick pushes the thumb */
static const struct {
  u64 button;
  int x, y;
  const char *name;
} k_buttons[] = {
    {HidNpadButton_X, 729, 298, "run/jump"},
    {HidNpadButton_A, 723, 388, "light attack"},
    {HidNpadButton_B, 619, 430, "sword"},
    {HidNpadButton_Y, 739, 196, "action (above run)"},
    {HidNpadButton_L, 259, 430, "guard"},
    {HidNpadButton_ZL, 150, 103, "weapon/hand (top left)"},
    {HidNpadButton_R, 695, 45, "scroll (top right)"},
    {HidNpadButton_Minus, 31, 25, "pause"},
};
#define NBTN ((int)(sizeof k_buttons / sizeof k_buttons[0]))
static int g_btn_slot[NBTN];
static int g_joy_slot = -1;

/* the main menu's three buttons, for the D-pad */
static const int k_menu_y[3] = {228, 317, 391};
#define MENU_X 542

static float g_stick[2][2];
static u64 g_buttons;
static float g_ptr_x, g_ptr_y;
static u64 g_ptr_seen, g_ptr_tick;
static int g_ptr_slot = -1, g_ptr_init, g_in_menu, g_menu_sel = -1;
#define PTR_HIDE_NS 4000000000ull
#define PTR_SPEED 700.0f /* pixels a second at full stick, at 1280x720 */

static void pointer_update(u64 pressed) {
  const u64 now = armGetSystemTick();
  if (!g_ptr_init) {
    g_ptr_init = 1;
    g_ptr_x = (float)g_w / 2, g_ptr_y = (float)g_h / 2;
    g_ptr_tick = now;
  }
  const float dt = (float)armTicksToNs(now - g_ptr_tick) / 1e9f;
  g_ptr_tick = now;
  const float sx = g_stick[1][0], sy = -g_stick[1][1]; /* the pad's y is up */
  const int moving = sqrtf(sx * sx + sy * sy) > 0.2f;
  if (moving) {
    g_ptr_x += sx * PTR_SPEED * dt * (float)g_w / 1280.0f;
    g_ptr_y += sy * PTR_SPEED * dt * (float)g_h / 720.0f;
    if (g_ptr_x < 0) g_ptr_x = 0;
    if (g_ptr_y < 0) g_ptr_y = 0;
    if (g_ptr_x > (float)(g_w - 1)) g_ptr_x = (float)(g_w - 1);
    if (g_ptr_y > (float)(g_h - 1)) g_ptr_y = (float)(g_h - 1);
    g_ptr_seen = now;
  }
  /* in the main menu the D-pad picks a button, and A taps (as the pad's A has no
   * button of its own there) */
  if (g_in_menu) {
    int d = 0;
    if (pressed & HidNpadButton_Down) d = 1;
    if (pressed & HidNpadButton_Up) d = -1;
    if (d) {
      g_menu_sel = g_menu_sel < 0 ? (d > 0 ? 0 : 2) : (g_menu_sel + d + 3) % 3;
      g_ptr_x = (float)MENU_X * (float)g_w / AC_GAME_W, g_ptr_y = (float)k_menu_y[g_menu_sel] * (float)g_h / AC_GAME_H;
      g_ptr_seen = now;
    }
  }
  const int click = !!(g_buttons & HidNpadButton_ZR) || (g_in_menu && (g_buttons & HidNpadButton_A));
  const int px = (int)g_ptr_x, py = (int)g_ptr_y;
  if (click && g_ptr_slot < 0) {
    g_ptr_seen = now;
    g_ptr_slot = vt_down(px, py);
  } else if (!click && g_ptr_slot >= 0) {
    g_ptr_seen = now;
    vt_up(g_ptr_slot);
    g_ptr_slot = -1;
  } else if (g_ptr_slot >= 0) {
    g_ptr_seen = now;
    vt_move(g_ptr_slot, px, py);
  }
}

static void pad_poll(void) {
  if (!g_pad_ready)
    return;
  padUpdate(&g_pad);
  padUpdate(&g_pad_hh);
  PadState *src = padIsConnected(&g_pad) ? &g_pad : &g_pad_hh;
  g_pad_present = padIsConnected(&g_pad) || padIsConnected(&g_pad_hh);
  u64 buttons = 0;
  float sticks[4] = {0, 0, 0, 0};
  if (g_pad_present)
    buttons = rt_pad_read(src, sticks);
  g_stick[0][0] = sticks[0], g_stick[0][1] = sticks[1];
  g_stick[1][0] = sticks[2], g_stick[1][1] = sticks[3];
  const u64 pressed = buttons & ~g_buttons;
  g_buttons = buttons;

  if (g_n.GameIsStateMainMenu) {
    const int m = g_n.GameIsStateMainMenu(ENV, g_cls_game) != 0;
    if (m != g_in_menu) {
      g_in_menu = m;
      g_menu_sel = -1;
      if (dcr_config()->log_input)
        debugPrintf("[input] main menu %s\n", m ? "on" : "off");
    }
  }

  /* the buttons: a finger held on the HUD's button while the pad's is held
   * (not in the main menu, where A taps the cursor) */
  for (int i = 0; i < NBTN; i++) {
    const int down = !g_in_menu && !!(buttons & k_buttons[i].button);
    if (down && g_btn_slot[i] == 0) {
      const int sl = vt_down(k_buttons[i].x, k_buttons[i].y);
      g_btn_slot[i] = sl < 0 ? 0 : sl + 1; /* 0 = none */
    } else if (!down && g_btn_slot[i] > 0) {
      vt_up(g_btn_slot[i] - 1);
      g_btn_slot[i] = 0;
    }
  }

  /* the left stick: a finger on the HUD's stick, moved from its centre */
  const float lx = g_stick[0][0], ly = -g_stick[0][1];
  const int pushed = sqrtf(lx * lx + ly * ly) > 0.25f;
  const int tx = JOY_X + (int)(lx * JOY_R), ty = JOY_Y + (int)(ly * JOY_R);
  if (pushed && g_joy_slot < 0 && !g_in_menu) {
    g_joy_slot = vt_down(JOY_X, JOY_Y);
    vt_move(g_joy_slot, tx, ty);
  } else if (pushed && g_joy_slot >= 0) {
    vt_move(g_joy_slot, tx, ty);
  } else if (!pushed && g_joy_slot >= 0) {
    vt_up(g_joy_slot);
    g_joy_slot = -1;
  }

  pointer_update(pressed);
}

/* the cursor: an arrow over the engine's frame, drawn with OpenGL ES 1 calls
 * that leave the engine's state as they found it */
static struct {
  void (*GetIntegerv)(unsigned, int *);
  void (*GetFloatv)(unsigned, float *);
  unsigned char (*IsEnabled)(unsigned);
  void (*Enable)(unsigned);
  void (*Disable)(unsigned);
  void (*EnableClientState)(unsigned);
  void (*DisableClientState)(unsigned);
  void (*ClientActiveTexture)(unsigned);
  void (*MatrixMode)(unsigned);
  void (*PushMatrix)(void);
  void (*PopMatrix)(void);
  void (*LoadMatrixf)(const float *);
  void (*LoadIdentity)(void);
  void (*Color4f)(float, float, float, float);
  void (*BlendFunc)(unsigned, unsigned);
  void (*BindBuffer)(unsigned, unsigned);
  void (*VertexPointer)(int, unsigned, int, const void *);
  void (*DrawArrays)(unsigned, int, int);
  int ok;
} g_gl;

static void pointer_gl_init(void) {
  if (g_gl.ok)
    return;
#define L(n) g_gl.n = (void *)dcr_gl_lookup("gl" #n)
  L(GetIntegerv); L(GetFloatv); L(IsEnabled); L(Enable); L(Disable); L(EnableClientState);
  L(DisableClientState); L(ClientActiveTexture); L(MatrixMode); L(PushMatrix); L(PopMatrix);
  L(LoadMatrixf); L(LoadIdentity); L(Color4f); L(BlendFunc); L(BindBuffer); L(VertexPointer); L(DrawArrays);
#undef L
  const int have = g_gl.GetIntegerv && g_gl.GetFloatv && g_gl.IsEnabled && g_gl.Enable && g_gl.Disable &&
            g_gl.EnableClientState && g_gl.DisableClientState && g_gl.ClientActiveTexture && g_gl.MatrixMode &&
            g_gl.PushMatrix && g_gl.PopMatrix && g_gl.LoadMatrixf && g_gl.LoadIdentity && g_gl.Color4f &&
            g_gl.BlendFunc && g_gl.BindBuffer && g_gl.VertexPointer && g_gl.DrawArrays;
  g_gl.ok = have ? 1 : -1;
  if (!have)
    debugPrintf("[game] the pointer cursor cannot be drawn: a GL function is missing\n");
}

static void pointer_draw(void) {
  const u64 now = armGetSystemTick();
  if (!g_pad_present || !g_ptr_seen || armTicksToNs(now - g_ptr_seen) >= PTR_HIDE_NS)
    return;
  pointer_gl_init();
  if (g_gl.ok != 1)
    return;
  static const unsigned caps[] = {0x0DE1 /*TEXTURE_2D*/, 0x0B71 /*DEPTH_TEST*/, 0x0B90 /*STENCIL_TEST*/,
                                  0x0B44 /*CULL_FACE*/,  0x0BE2 /*BLEND*/,      0x0C11 /*SCISSOR_TEST*/,
                                  0x0B50 /*LIGHTING*/,   0x0B60 /*FOG*/,        0x0BC0 /*ALPHA_TEST*/};
  static const unsigned arrays[] = {0x8074 /*VERTEX*/, 0x8075 /*NORMAL*/, 0x8076 /*COLOR*/, 0x8078 /*TEXCOORD*/};
  unsigned char cap_on[sizeof caps / sizeof caps[0]], arr_on[sizeof arrays / sizeof arrays[0]];
  int matrix_mode = 0, client_tex = 0x84C0, blend_src = 1, blend_dst = 0, array_buf = 0;
  float color[4] = {1, 1, 1, 1};
  g_gl.GetIntegerv(0x0BA0 /*MATRIX_MODE*/, &matrix_mode);
  g_gl.GetIntegerv(0x84E1 /*CLIENT_ACTIVE_TEXTURE*/, &client_tex);
  g_gl.GetIntegerv(0x0BE1 /*BLEND_SRC*/, &blend_src);
  g_gl.GetIntegerv(0x0BE0 /*BLEND_DST*/, &blend_dst);
  g_gl.GetIntegerv(0x8894 /*ARRAY_BUFFER_BINDING*/, &array_buf);
  g_gl.GetFloatv(0x0B00 /*CURRENT_COLOR*/, color);
  for (unsigned i = 0; i < sizeof caps / sizeof caps[0]; i++)
    cap_on[i] = g_gl.IsEnabled(caps[i]);
  g_gl.ClientActiveTexture(0x84C0);
  for (unsigned i = 0; i < sizeof arrays / sizeof arrays[0]; i++)
    arr_on[i] = g_gl.IsEnabled(arrays[i]);

  for (unsigned i = 0; i < sizeof caps / sizeof caps[0]; i++)
    g_gl.Disable(caps[i]);
  g_gl.Enable(0x0BE2 /*BLEND*/);
  g_gl.BlendFunc(0x0302 /*SRC_ALPHA*/, 0x0303 /*ONE_MINUS_SRC_ALPHA*/);
  g_gl.DisableClientState(0x8075);
  g_gl.DisableClientState(0x8076);
  g_gl.DisableClientState(0x8078);
  g_gl.EnableClientState(0x8074);
  g_gl.BindBuffer(0x8892 /*ARRAY_BUFFER*/, 0);
  const float W = (float)g_w, H = (float)g_h;
  const float proj[16] = {2.0f / W, 0, 0, 0, 0, -2.0f / H, 0, 0, 0, 0, -1, 0, -1, 1, 0, 1};
  g_gl.MatrixMode(0x1701 /*PROJECTION*/);
  g_gl.PushMatrix();
  g_gl.LoadMatrixf(proj);
  g_gl.MatrixMode(0x1700 /*MODELVIEW*/);
  g_gl.PushMatrix();
  g_gl.LoadIdentity();
  const float s = H / 720.0f * (g_ptr_slot >= 0 ? 0.85f : 1.0f);
  const float x = g_ptr_x, y = g_ptr_y;
  const float fade = armTicksToNs(now - g_ptr_seen) > 3000000000ull
                         ? 1.0f - (float)(armTicksToNs(now - g_ptr_seen) - 3000000000ull) / 1e9f : 1.0f;
  const float out[6] = {x, y, x, y + 32 * s, x + 22 * s, y + 23 * s};
  const float in[6] = {x + 2.5f * s, y + 6.5f * s, x + 2.5f * s, y + 25 * s, x + 17 * s, y + 21 * s};
  g_gl.Color4f(0.0f, 0.0f, 0.0f, 0.9f * fade);
  g_gl.VertexPointer(2, 0x1406 /*FLOAT*/, 0, out);
  g_gl.DrawArrays(0x0004 /*TRIANGLES*/, 0, 3);
  g_gl.Color4f(1.0f, 1.0f, 1.0f, 0.95f * fade);
  g_gl.VertexPointer(2, 0x1406, 0, in);
  g_gl.DrawArrays(0x0004, 0, 3);

  g_gl.PopMatrix();
  g_gl.MatrixMode(0x1701);
  g_gl.PopMatrix();
  g_gl.MatrixMode((unsigned)matrix_mode);
  for (unsigned i = 0; i < sizeof caps / sizeof caps[0]; i++)
    cap_on[i] ? g_gl.Enable(caps[i]) : g_gl.Disable(caps[i]);
  for (unsigned i = 0; i < sizeof arrays / sizeof arrays[0]; i++)
    arr_on[i] ? g_gl.EnableClientState(arrays[i]) : g_gl.DisableClientState(arrays[i]);
  g_gl.ClientActiveTexture((unsigned)client_tex);
  g_gl.BlendFunc((unsigned)blend_src, (unsigned)blend_dst);
  g_gl.BindBuffer(0x8892, (unsigned)array_buf);
  g_gl.Color4f(color[0], color[1], color[2], color[3]);
}

/* --------------------------------------------------------- lifecycle */
/* The runtime's applet lifecycle (rt_applet.c) calls these from the frame
 * loop's rt_applet_poll(): what the activity's onPause / onWindowFocusChanged
 * did (it asks the library whether it can be interrupted, and stops the
 * sounds). Held keys let go. */
void port_focus_lost(void) {
  if (!g_engine_up)
    return;
  /* GameGLSurfaceView.onWindowFocusChanged(false): nativePause(3) */
  if (g_n.ViewPause)
    g_n.ViewPause(ENV, g_cls_view, 3);
  ac_audio_pause(1);
  for (int k = 0; k < MAX_FINGERS; k++)
    if (g_finger[k].on) {
      g_n.GameTouch(ENV, g_cls_game, k, 0, g_finger[k].x, g_finger[k].y);
      g_finger[k].on = 0, g_finger[k].virt = 0;
    }
  memset(g_btn_slot, 0, sizeof g_btn_slot);
  g_joy_slot = g_ptr_slot = -1;
}

void port_focus_gained(void) {
  ac_audio_pause(0);
  if (g_engine_up && g_n.ViewResume)
    g_n.ViewResume(ENV, g_cls_view, 3); /* onWindowFocusChanged(true): nativeResume(3) */
}

/* HOME and sleep freeze the whole process; the runtime's clocks find each
 * freeze: what Android does around it, onPause then onResume. */
void port_process_frozen(unsigned count) {
  debugPrintf("[game] the process was held (HOME menu or sleep; freeze %u)\n", count);
  port_focus_lost();
  port_focus_gained();
}

/* ---------------------------------------------------------------- EGL */
static int choose_config(void *dpy, void **cfg) {
  /* the best first: RGB8 with 24-bit depth and stencil, then less */
  static const struct { int depth, stencil; } tries[] = {{24, 8}, {16, 8}, {24, 0}, {16, 0}, {0, 0}};
  for (unsigned t = 0; t < sizeof tries / sizeof tries[0]; t++) {
    const fEGLint attrs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RED_SIZE, 8,
                             EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_DEPTH_SIZE, tries[t].depth,
                             EGL_STENCIL_SIZE, tries[t].stencil, EGL_NONE};
    fEGLint n = 0;
    if (b_eglChooseConfig(dpy, attrs, cfg, 1, &n) && n >= 1) {
      debugPrintf("[game] EGL config: RGB8, depth %d, stencil %d\n", tries[t].depth, tries[t].stencil);
      return 0;
    }
  }
  return -1;
}

/* The game draws with fixed-function OpenGL ES 1 (glMatrixMode, glFrustumf,
 * glVertexPointer, ...): an ES 1 context. */
static void egl_up(void) {
  if (!dcr_config()->render_screen)
    dcr_window_set_size(AC_GAME_W, AC_GAME_H); /* before the window surface */
  dcr_window_size(&g_w, &g_h);
  g_dpy = b_eglGetDisplay(NULL);
  fEGLint maj = 0, min = 0;
  if (!g_dpy || !b_eglInitialize(g_dpy, &maj, &min))
    fatal_error("The graphics driver did not start (eglInitialize 0x%x).", (unsigned)b_eglGetError());
  unsigned (*bind_api)(unsigned) = (unsigned (*)(unsigned))dcr_gl_lookup("eglBindAPI");
  if (bind_api)
    bind_api(EGL_OPENGL_ES_API);
  void *cfg = NULL;
  if (choose_config(g_dpy, &cfg))
    fatal_error("No OpenGL ES 1 window configuration (0x%x).", (unsigned)b_eglGetError());
  const fEGLint ctx_attrs[] = {EGL_CONTEXT_CLIENT_VERSION, 1, EGL_NONE};
  g_ctx = b_eglCreateContext(g_dpy, cfg, NULL, ctx_attrs);
  g_surf = b_eglCreateWindowSurface(g_dpy, cfg, nwindowGetDefault(), NULL);
  if (!g_surf || !g_ctx || !b_eglMakeCurrent(g_dpy, g_surf, g_surf, g_ctx))
    fatal_error("Could not create the OpenGL ES 1 context (surface %p, context %p, 0x%x).", g_surf, g_ctx,
                (unsigned)b_eglGetError());
  b_eglSwapInterval(g_dpy, 1);
  debugPrintf("[game] EGL %d.%d: OpenGL ES 1 on the window, %dx%d\n", (int)maj, (int)min, g_w, g_h);
}

static void egl_down(void) {
  if (!g_dpy)
    return;
  b_eglMakeCurrent(g_dpy, NULL, NULL, NULL);
  if (g_ctx)
    b_eglDestroyContext(g_dpy, g_ctx);
  if (g_surf)
    b_eglDestroySurface(g_dpy, g_surf);
  b_eglTerminate(g_dpy);
  g_dpy = g_surf = g_ctx = NULL;
}

/* ---------------------------------------------------------------- report */
static void report(void) {
  static u64 last_tick;
  static unsigned long last_frames;
  const u64 tick = armGetSystemTick();
  const unsigned long frames = (unsigned long)dcr_gl_frames();
  const double fps = last_tick ? (double)(frames - last_frames) * 1e9 / (double)armTicksToNs(tick - last_tick) : 0.0;
  last_tick = tick;
  last_frames = frames;
  char rate[24] = "";
  if (fps > 0.0)
    snprintf(rate, sizeof rate, " (%.1f fps)", fps);
  debugPrintf("[game] %lu frames%s, %d Java objects\n", frames, rate, jni_live_objects());
  dcr_boost_report();
  ac_gltrace_report();
}

/* ------------------------------------------------------------- language */
/* The game keeps its language in the options file (offset 0x530), not in Java:
 * a header "N=Z ", the body size and the body's CRC32 (from offset 12). Set it
 * from config.ini, the CRC redone, so the file the game reads agrees. */
#define OPT_LANG_OFFSET 0x530

static uint32_t crc32_of(const uint8_t *p, size_t n) {
  uint32_t c = 0xffffffffu;
  while (n--) {
    c ^= *p++;
    for (int k = 0; k < 8; k++)
      c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1u)));
  }
  return ~c;
}

static void patch_language(const char *name) {
  char real[DCR_PATH_MAX], path[DCR_PATH_MAX + 40];
  snprintf(path, sizeof path, "%s/%s", dcr_translate_path(AC_DATA_DIR, real, sizeof real), name);
  FILE *f = fopen(path, "rb");
  if (!f)
    return;
  static uint8_t buf[8192];
  const size_t n = fread(buf, 1, sizeof buf, f);
  fclose(f);
  if (n <= OPT_LANG_OFFSET || n == sizeof buf || memcmp(buf, "N=Z ", 4) != 0)
    return;
  const int want = ac_language();
  if (buf[OPT_LANG_OFFSET] == want)
    return;
  debugPrintf("[game] %s: language %d -> %d\n", name, buf[OPT_LANG_OFFSET], want);
  buf[OPT_LANG_OFFSET] = (uint8_t)want;
  const uint32_t crc = crc32_of(buf + 12, n - 12);
  memcpy(buf + 8, &crc, 4);
  f = fopen(path, "wb");
  if (!f)
    return;
  fwrite(buf, 1, n, f);
  fclose(f);
}

/* ----------------------------------------------------------------- run */
int ac_game_run(void) {
  egl_up();
  ac_java_init();

  /* a pad, handheld Joy-Cons included, and the touch screen */
  rt_pad_setup(1, 1);
  rt_pad_slot(&g_pad, 0);
  rt_pad_slot(&g_pad_hh, RT_PAD_HANDHELD);
  g_pad_ready = 1;
  touch_init();

  {
    char real[DCR_PATH_MAX], probe[DCR_PATH_MAX + 16];
    snprintf(probe, sizeof probe, "%s/data.bar", dcr_translate_path(AC_DATA_DIR, real, sizeof real));
    struct stat sb;
    if (stat(probe, &sb) != 0)
      debugPrintf("[game] WARNING: no game data at %s -- copy the \"gameloft\" folder to %s\n", real, dcr_game_root());
    else
      debugPrintf("[game] game data: %s\n", real);
  }

  ac_audio_init();

  /* the classes the natives belong to: the static natives get them as "this" */
  g_cls_renderer = jni_class(AC_CLS_RENDERER)->obj;
  g_cls_game = jni_class(AC_CLS_GAME)->obj;
  g_cls_media = jni_class(AC_CLS_MEDIA)->obj;
  g_cls_view = jni_class(AC_CLS_VIEW)->obj;

  patch_language("options.sav");
  patch_language("options_Interrupt.sav");

  /* ---- GameRenderer.onSurfaceCreated ---- */
  debugPrintf("[game] GameRenderer.nativeInit(1, 1)\n");
  g_n.RendererInit(ENV, g_cls_renderer, 1, 1);
  debugPrintf("[game] AssassinsCreed.nativeInit()\n");
  g_n.GameInit(ENV, g_cls_game);
  /* GLMediaPlayer.init(): nativeInit(0), then the sound counts */
  if (g_n.MediaInit) {
    g_n.MediaInit(ENV, g_cls_media, 0);
    const jint total = g_n.MediaTotalSounds ? g_n.MediaTotalSounds(ENV, g_cls_media) : 0;
    const jint same = g_n.MediaTotalSoundsOfSameInstance ? g_n.MediaTotalSoundsOfSameInstance(ENV, g_cls_media) : 0;
    debugPrintf("[game] GLMediaPlayer: %d sounds, %d of the same instance\n", (int)total, (int)same);
  }
  /* ---- GameRenderer.onSurfaceChanged ---- */
  const int gw = dcr_config()->render_screen ? g_w : AC_GAME_W, gh = dcr_config()->render_screen ? g_h : AC_GAME_H;
  g_n.RendererResize(ENV, g_cls_renderer, gw, gh);
  /* The window gets the focus: GameGLSurfaceView.onWindowFocusChanged(true) ->
   * nativeResume(3). The library starts paused: until this, nativeRender()
   * returns at once and draws nothing (a black screen, 350 000 frames a second). */
  if (g_n.ViewResume)
    g_n.ViewResume(ENV, g_cls_view, 3);
  g_engine_up = 1;
  debugPrintf("[game] the engine is up (%dx%d), resumed\n", gw, gh);
  log_flush_ring();

  dcr_watchdog_start();

  u64 last_report = armGetSystemTick();
  int first = 1;
  unsigned long quiet_at = 0;
  while (!ac_quit_requested() && !rt_exit_requested() && appletMainLoop()) {
    rt_applet_poll(); /* focus, freezes: port_focus_lost/gained, port_process_frozen */
    if (!rt_focused()) {
      svcSleepThread(50000000ll);
      continue;
    }
    pad_poll();
    touch_poll();
    const u64 frame_start = armGetSystemTick();
    g_n.RendererRender(ENV, g_cls_renderer); /* onDrawFrame */
    pointer_draw();
    b_eglSwapBuffers(g_dpy, g_surf);
    /* The Java side waits until 50 ms have passed since the frame began (20 fps);
     * without it the game runs at the Switch's 60 and is three times too fast. */
    if (dcr_config()->frame_limit) {
      const s64 left = 50000000ll - (s64)armTicksToNs(armGetSystemTick() - frame_start);
      if (left > 1000000)
        svcSleepThread(left);
    }

    const unsigned long frames = (unsigned long)dcr_gl_frames();
    if (first) {
      first = 0;
      dcr_boost_launch_end();
      debugPrintf("[game] first frame presented\n");
      quiet_at = frames + 180;
    }
    /* From ~3 s after the first picture the log goes to a RAM ring (util.c),
     * written out every 10 s and by the watchdog. */
    if (quiet_at && frames >= quiet_at) {
      quiet_at = 0;
      log_set_quiet(1);
    }
    const u64 now = armGetSystemTick();
    if (armTicksToNs(now - last_report) >= 10000000000ull) {
      last_report = now;
      report();
      log_flush_ring();
    }
  }

  /* ---- onPause, onDestroy ---- */
  debugPrintf("[game] leaving (%s)\n", ac_quit_requested() ? "the game quit" : "closed from the system");
  log_set_quiet(0);
  rt_applet_stop();
  if (g_n.RendererDone)
    g_n.RendererDone(ENV, g_cls_renderer);
  ac_audio_shutdown();
  egl_down();
  debugPrintf("[game] closed\n");
  log_flush_ring();
  (void)g_engine_up;
  return 0;
}
