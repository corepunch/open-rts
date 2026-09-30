#ifndef __I_VIDEO__
#define __I_VIDEO__

#include "v_video.h"

#include <stdbool.h>

typedef struct app_s app_t;

bool I_InitGraphics(app_t *app, int window_w, int window_h, bool hidden, bool software);
void I_ShutdownGraphics(void);
void I_FinishUpdate(void);
bool I_SaveScreenshot(const char *path);
void I_SetScaleMode(bool linear);

#endif
