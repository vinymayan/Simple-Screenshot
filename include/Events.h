#pragma once

#include "Settings.h"


void SetupInputHook();
void CaptureFrame(ScreenshotFormat format);
void TriggerRegionScreenshot(bool withUI, int x, int y, int w, int h, const std::vector<std::pair<int, int>>& lassoPoints);
