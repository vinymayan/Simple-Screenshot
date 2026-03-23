#pragma once

#include <commctrl.h>
#pragma comment(lib, "comctl32.lib")

#include "Settings.h" // Incluimos Settings para reconhecer o Enum ScreenshotFormat

// Apenas declaramos que estas funções existem.
// A implementação real vai para o src/Events.cpp

void SetupInputHook();
void CaptureFrame(ScreenshotFormat format);
