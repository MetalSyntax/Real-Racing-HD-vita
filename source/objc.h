/**
 * @file objc.h
 * @brief Objective-C selector dispatch runtime and on-screen keyboard IME bridge.
 */
#pragma once
#include <stdint.h>

void objc_init(void);
void uikit_init(void);
/** Run loop: hands finished AudioQueue buffers / IsRunning changes to the guest (coreaudio.c). */
void coreaudio_poll(void);
/** Current UIInterfaceOrientation (3 = LandscapeRight, 4 = LandscapeLeft) set by the game. */
uint32_t uikit_orientation(void);
void ime_poll(void);
void ime_open(int type, void (*done)(const char *utf8));
int ime_active(void);
