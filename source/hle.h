/**
 * @file hle.h
 * @brief Canonical storage path constants for PS Vita high-level emulation.
 */
#pragma once

#ifndef DATA_PATH
#define DATA_PATH "ux0:data/realracinghd/"
#endif

#define BUNDLE_PATH DATA_PATH "game_app/"
#define DOCS_PATH   DATA_PATH "saves/"
#define LOGS_PATH   DATA_PATH "logs/"

// Identidad del bundle iOS (Info.plist de game_app/)
#define EXE_NAME     "RealRacingHD"
#define EXE_PATH     BUNDLE_PATH EXE_NAME
#define BUNDLE_ID    "com.firemint.RealRacingHD"
#define APP_DELEGATE "RealRacingAppDelegate"
#define BUNDLE_VERSION "1.04"
#define BUNDLE_BUILD "06241337" // Info.plist fmBuildNumber

// Device the game believes it runs on: Real Racing HD is iPad-only (UIDeviceFamily 2, MinimumOSVersion 3.2).
// iOS 3.2 keeps GameCenter (iOS 4.1+) and multitasking code paths off.
#define DEVICE_MACHINE "iPad1,1"
#define DEVICE_MODEL   "iPad"
#define IOS_VERSION    "3.2"
#define VIEW_W 768.0f // UIScreen bounds in points (portrait, as iOS 3.2 reports them)
#define VIEW_H 1024.0f
// Info.plist UISupportedInterfaceOrientations[0] = UIInterfaceOrientationLandscapeLeft (4)
#define DEFAULT_ORIENTATION 4
