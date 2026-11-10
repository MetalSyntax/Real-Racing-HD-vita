/**
 * @file patch.h
 * @brief Game-specific hooks, memory patches, and per-frame logic callbacks.
 */
#pragma once
#include "imports.h"

/**
 * @brief Initializes game-specific memory patches, trampolines, and binary hooks.
 */
void patch_init(void);

/**
 * @brief Per-frame callback invoked from the main loop for custom game logic.
 */
void patch_frame(void);

/**
 * @brief Adds synthetic touches for physical buttons / left stick after input_poll().
 */
void patch_input(void);


/**
 * @brief Accelerometer sample (UIAcceleration, g units, iOS device axes) synthesized from the left stick.
 */
void patch_accel(double *x, double *y, double *z);
