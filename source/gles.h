/**
 * @file gles.h
 * @brief OpenGL ES 1.1 / 2.0 graphics pipeline over vitaGL with SoftFP calling convention.
 */
#pragma once
#include <stdint.h>

typedef enum {
    DISPLAY_MODE_LANDSCAPE_FULL = 0,    /**< iPad 4:3 picture stretched to 960x544. */
    DISPLAY_MODE_ORIGINAL_CENTERED = 1, /**< 4:3 kept: 725x544 centered, black side bars [Default]. */
} display_mode_t;

/**
 * @brief Maps a Vita screen point (0..959, 0..543) to EAGLView points (768x1024 portrait UIKit space).
 * @return 0 if the point is on a black side bar.
 */
int gles_screen_to_view(int sx, int sy, float *vx, float *vy);

/** @brief Turns the landscape picture (and touch mapping) 180 degrees more (controls.txt ROTATE_180). */
void gles_set_rotate180(int on);

/** @brief Multiplier for glPolygonOffsetx units (16-bit iPad depth -> DF32M; controls.txt DEPTH_BIAS). */
void gles_set_depth_bias_scale(float s);

/** @brief Maps an iPad landscape point (0..1024 x 0..768, as the player sees it) to EAGLView points. */
void gles_landscape_to_view(float lx, float ly, float *vx, float *vy);

/**
 * @brief Initializes the OpenGL ES pipeline and off-screen virtual FBO.
 */
void gles_init(void);

/**
 * @brief Presents current frame: rotates the portrait iPad FBO to landscape and scales it to 960x544.
 */
void gles_present(void);

/**
 * @brief Boot load bar: re-presents the last frame if the GL thread has not presented for 200 ms
 *        (the game loads synchronously behind the splash). No-op from other threads.
 */

/**
 * @brief Returns the total number of draw calls dispatched this session.
 */
uint32_t gles_get_draw_calls(void);

/**
 * @brief Marks the currently bound renderbuffer as the CAEAGLLayer drawable
 *        ([EAGLContext renderbufferStorage:fromDrawable:]).
 */
void gles_bind_drawable(void);

/**
 * @brief Gets current display presentation mode.
 */
display_mode_t gles_get_display_mode(void);

/**
 * @brief Sets current display presentation mode.
 */
void gles_set_display_mode(display_mode_t mode);

/**
 * @brief Cycles through available display presentation modes.
 */
void gles_cycle_display_mode(void);

