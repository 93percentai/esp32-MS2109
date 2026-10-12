#pragma once
#include "sdkconfig.h"
/* A new profile selection also migrates existing 640x480/60-default builds. */
#if CONFIG_CAPTURE_PROFILE_HD
#define CAPTURE_WIDTH 1280U
#define CAPTURE_HEIGHT 720U
#define CAPTURE_FPS 30U
#elif CONFIG_CAPTURE_PROFILE_VGA
#define CAPTURE_WIDTH 640U
#define CAPTURE_HEIGHT 480U
#define CAPTURE_FPS 30U
#else
#define CAPTURE_WIDTH CONFIG_CAPTURE_WIDTH
#define CAPTURE_HEIGHT CONFIG_CAPTURE_HEIGHT
#define CAPTURE_FPS CONFIG_CAPTURE_FPS
#endif
