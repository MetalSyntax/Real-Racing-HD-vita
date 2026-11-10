/**
 * @file patch.c
 * @brief Real Racing HD: physical controls, tilt steering, persistent loader settings.
 * @details No binary patches are needed so far (none applied before console run #1). Controls:
 *          - left stick X / D-pad left-right -> accelerometer tilt (the game steers with
 *            -[EAGLView accelerometer:didAccelerate:]),
 *          - buttons -> synthetic touches at configurable iPad landscape points (1024x768),
 *          - SELECT -> display mode (4:3 centered / stretched), saved in saves/vita_settings.txt.
 *          Everything is read from ux0:data/realracinghd/controls.txt; a missing file = defaults.
 */
#include <math.h>
#include <psp2/ctrl.h>
#include <vitaGL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "darwin.h"
#include "gles.h"
#include "hle.h"
#include "imports.h"
#include "input.h"
#include "log.h"
#include "objc.h"
#include "patch.h"

#define CONTROLS_PATH DATA_PATH "controls.txt"
#define SETTINGS_PATH DOCS_PATH "vita_settings.txt"

// A button bound to a touch point, in iPad landscape points as the player sees the screen.
typedef struct {
    const char *name;
    uint32_t mask;
    float x, y; // < 0 = unbound
} button_touch;

// ponytail: default points are guesses from the iPad layout (gas bottom-right, brake bottom-left,
// pause top-right); calibrate on console run #1 and edit controls.txt, no rebuild needed.
static button_touch buttons[] = {
    {"CROSS", SCE_CTRL_CROSS, 900, 650},   // accelerate
    {"R", SCE_CTRL_RTRIGGER, 900, 650},    // accelerate
    {"SQUARE", SCE_CTRL_SQUARE, 124, 650}, // brake
    {"L", SCE_CTRL_LTRIGGER, 124, 650},    // brake
    {"START", SCE_CTRL_START, 980, 40},    // pause
    {"CIRCLE", SCE_CTRL_CIRCLE, 60, 40},   // back
    {"TRIANGLE", SCE_CTRL_TRIANGLE, -1, -1},
    {"UP", SCE_CTRL_UP, -1, -1},
    {"DOWN", SCE_CTRL_DOWN, -1, -1},
};
#define NBUTTONS (int)(sizeof(buttons) / sizeof(buttons[0]))

static int deadzone = 10;        // left stick, of 128
static float steer_max_deg = 40; // full stick = this tilt
static int steer_invert = 0;
static float tilt_back = 0.5f;   // gravity share along the landscape "down" axis at rest (~30 deg back)

static void save_settings(void) {
    FILE *f = fopen(SETTINGS_PATH, "w");
    if (!f)
        return;
    fprintf(f, "display_mode=%d\n", (int)gles_get_display_mode());
    fclose(f);
}

static void load_settings(void) {
    FILE *f = fopen(SETTINGS_PATH, "r");
    int mode;
    if (f) {
        if (fscanf(f, "display_mode=%d", &mode) == 1 && (mode == 0 || mode == 1))
            gles_set_display_mode((display_mode_t)mode);
        fclose(f);
    }
}

// controls.txt: one "KEY = value" per line, '#' comments. Buttons take "x,y" (landscape points) or "none".
static void load_controls(void) {
    FILE *f = fopen(CONTROLS_PATH, "r");
    if (!f) {
        l_info("[INPUT] %s not found: default controls", CONTROLS_PATH);
        return;
    }
    char line[128], key[32], val[64];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || sscanf(line, " %31[A-Za-z_] = %63[^\n#]", key, val) != 2)
            continue;
        float x, y;
        if (!strcasecmp(key, "DEADZONE"))
            deadzone = atoi(val);
        else if (!strcasecmp(key, "STEER_MAX_DEG"))
            steer_max_deg = strtof(val, NULL);
        else if (!strcasecmp(key, "STEER_INVERT"))
            steer_invert = atoi(val);
        else if (!strcasecmp(key, "TILT_BACK"))
            tilt_back = strtof(val, NULL);
        else if (!strcasecmp(key, "ROTATE_180"))
            gles_set_rotate180(atoi(val));
        else if (!strcasecmp(key, "DEPTH_BIAS"))
            gles_set_depth_bias_scale(strtof(val, NULL));
        else
            for (int i = 0; i < NBUTTONS; i++)
                if (!strcasecmp(key, buttons[i].name)) {
                    if (sscanf(val, "%f , %f", &x, &y) == 2)
                        buttons[i].x = x, buttons[i].y = y;
                    else
                        buttons[i].x = buttons[i].y = -1;
                }
    }
    fclose(f);
    if (deadzone < 0 || deadzone > 100)
        deadzone = 10;
    if (tilt_back < 0 || tilt_back > 0.95f)
        tilt_back = 0.5f;
    l_info("[INPUT] controls.txt: deadzone %d, steer %.0f deg%s", deadzone, steer_max_deg,
           steer_invert ? " (inverted)" : "");
}

const import_entry patch_imports[] = {
    {"", 0, 0}, // no guest function is replaced (keeps the table non-empty for imports.c)
};
const int patch_imports_count = 0;

void patch_init(void) {
    load_controls();
    load_settings();
    l_info("Game patches: none (Real Racing HD runs unpatched)");
}

void patch_frame(void) {}

// Stick X (-1..1) after the dead zone; the D-pad gives full lock.
static float steer_value(void) {
    if (pad.buttons & SCE_CTRL_LEFT)
        return -1;
    if (pad.buttons & SCE_CTRL_RIGHT)
        return 1;
    int v = (int)pad.lx - 128;
    if (abs(v) <= deadzone)
        return 0;
    float s = (float)(v > 0 ? v - deadzone : v + deadzone) / (127 - deadzone);
    return s > 1 ? 1 : s < -1 ? -1 : s;
}

// Gravity in the landscape frame: tilt_back along "down", steering turns it toward left/right (wheel).
// Converted to iOS device axes (x = portrait right, y = portrait up) for the current orientation:
// 3 (home right): landscape down = -x, right = -y; 4 (home left): down = +x, right = +y.
void patch_accel(double *x, double *y, double *z) {
    float s = steer_value() * (steer_invert ? 1 : -1); // log 014: the unflipped sign steered the wrong way
    float a = s * steer_max_deg * (float)M_PI / 180.0f;
    float down = tilt_back * cosf(a), right = tilt_back * sinf(a);
    float sign = uikit_orientation() == 3 ? -1.0f : 1.0f;
    *x = sign * down;
    *y = sign * right;
    *z = -sqrtf(1.0f - tilt_back * tilt_back);
}

static void add_touch(int id, float lx, float ly) {
    if (touch_count >= SCE_TOUCH_MAX_REPORT)
        return;
    float vx, vy;
    gles_landscape_to_view(lx, ly, &vx, &vy);
    touch_point *t = &touches[touch_count++];
    memset(t, 0, sizeof(*t));
    t->id = id;
    t->gx = (int)vx;
    t->gy = (int)vy;
    t->down = 1;
}

void patch_input(void) {
    static uint32_t prev;
    if ((pad.buttons & SCE_CTRL_SELECT) && !(prev & SCE_CTRL_SELECT)) {
        gles_cycle_display_mode();
        save_settings();
    }
    prev = pad.buttons;
    // Synthetic touch ids 200+ never collide with panel report ids.
    for (int i = 0; i < NBUTTONS; i++)
        if ((pad.buttons & buttons[i].mask) && buttons[i].x >= 0)
            add_touch(200 + i, buttons[i].x, buttons[i].y);
}
