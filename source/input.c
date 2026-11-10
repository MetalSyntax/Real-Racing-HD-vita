/**
 * @file input.c
 * @brief Input subsystem mapping PS Vita front touch and buttons to iOS touches.
 * @details Captures front capacitive multitouch and analog stick / button state.
 *          Maps hardware coordinates into the guest game's virtual coordinate space.
 */
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <string.h>

#include "gles.h"
#include "input.h"
#include "log.h"
#include "macho.h"

touch_point touches[SCE_TOUCH_MAX_REPORT];
int touch_count = 0;
SceCtrlData pad;

static touch_point prev_touches[SCE_TOUCH_MAX_REPORT];
static int prev_count = 0;

void input_init(void) {
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    memset(touches, 0, sizeof(touches));
    memset(prev_touches, 0, sizeof(prev_touches));
    touch_count = 0;
    prev_count = 0;
    l_info("Input subsystem initialized (front touch + analog sticks).");
}

void input_poll(void) {
    sceCtrlPeekBufferPositive(0, &pad, 1);

    SceTouchData t;
    if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &t, 1) < 1)
        t.reportNum = 0;

    memcpy(prev_touches, touches, sizeof(touches));
    prev_count = touch_count;

    touch_count = (int)t.reportNum;
    if (touch_count > SCE_TOUCH_MAX_REPORT)
        touch_count = SCE_TOUCH_MAX_REPORT;

    int n = 0;
    for (int i = 0; i < touch_count; i++) {
        float vx, vy;
        int x = t.report[i].x / 2, y = t.report[i].y / 2; // 1920x1088 panel -> 960x544 screen
        if (!gles_screen_to_view(x, y, &vx, &vy))
            continue; // black side bar
        touches[n] = (touch_point){t.report[i].id, x, y, (int)vx, (int)vy, 1};
        n++;
    }
    touch_count = n;
}
