#include "Events.h"
#include "logger.h"
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <vector>
#include <ctime>
#include <string>
#include <filesystem>
#include "MinHook.h"
#include <Xinput.h>
#include <shlobj.h>
#include "Prisma.h"

#pragma comment(lib, "Xinput9_1_0.lib")
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#include <d3d12.h>
#pragma comment(lib, "d3d12.lib")
#include <commctrl.h>
#pragma comment(lib, "comctl32.lib")

HWND g_hWindow = nullptr;
static XINPUT_STATE g_lastGamepadState;
static bool g_gamepadConnected = false;
static constexpr UINT_PTR SCREENSHOT_SUBCLASS_ID = 0x5353484F54;
int g_captureDelayFrames = 0;

static bool g_captureNextFrameWithUI = false;
static bool g_captureNextFrameWithoutUI = false;
static ScreenshotFormat g_pendingFormat;

typedef HRESULT(WINAPI* Present_t)(IDXGISwapChain*, UINT, UINT);
Present_t OriginalPresent = nullptr;

typedef int64_t(*RenderUI_t)(int64_t);
RenderUI_t OriginalRenderUI = nullptr;

struct CropData {
    bool active = false;
    int x = 0, y = 0, w = 0, h = 0;
    std::vector<std::pair<int, int>> lassoPoints;
} g_crop, g_pendingCrop;

bool IsPointInPolygon(int x, int y, const std::vector<std::pair<int, int>>& polygon) {
    bool inside = false;
    for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        if (((polygon[i].second > y) != (polygon[j].second > y)) &&
            (x < (polygon[j].first - polygon[i].first) * (y - polygon[i].second) / (float)(polygon[j].second - polygon[i].second + 0.000001f) + polygon[i].first)) {
            inside = !inside;
        }
    }
    return inside;
}

void TriggerScreenshotRequest(ScreenshotFormat format, bool withoutUI = false) {
    g_pendingFormat = format;
    if (withoutUI) g_captureNextFrameWithoutUI = true;
    else g_captureNextFrameWithUI = true;
}

void TriggerRegionScreenshot(bool withUI, int x, int y, int w, int h, const std::vector<std::pair<int, int>>& lassoPoints) {
    g_crop.active = true;
    g_crop.x = x; g_crop.y = y; g_crop.w = w; g_crop.h = h;
    g_crop.lassoPoints = lassoPoints;

    g_captureDelayFrames = 1;
    TriggerScreenshotRequest(Settings::imageFormat, !withUI);
}

// Sincroniza em tempo real com o JS
void UpdatePendingCrop(int x, int y, int w, int h, const std::vector<std::pair<int, int>>& points) {
    g_pendingCrop.x = x; g_pendingCrop.y = y; g_pendingCrop.w = w; g_pendingCrop.h = h;
    g_pendingCrop.lassoPoints = points;
}

// Executado quando aperta o hotkey do C++
void ApplyPendingCropAndTrigger(bool withUI) {
    g_crop.active = true;
    g_crop.x = g_pendingCrop.x; g_crop.y = g_pendingCrop.y;
    g_crop.w = g_pendingCrop.w; g_crop.h = g_pendingCrop.h;
    g_crop.lassoPoints = g_pendingCrop.lassoPoints;

    g_captureDelayFrames = 5;
    TriggerScreenshotRequest(Settings::imageFormat, !withUI);
}

void CopyFileToClipboard(const std::string& filePath) {
    if (!OpenClipboard(nullptr)) return;
    EmptyClipboard();
    int len = MultiByteToWideChar(CP_UTF8, 0, filePath.c_str(), -1, NULL, 0);
    if (len > 0) {
        std::wstring wPath(len, 0);
        MultiByteToWideChar(CP_UTF8, 0, filePath.c_str(), -1, &wPath[0], len);
        size_t dropSize = sizeof(DROPFILES) + (wPath.length() + 1) * sizeof(wchar_t);
        HGLOBAL hMem = GlobalAlloc(GHND, dropSize);
        if (hMem) {
            DROPFILES* pDrop = (DROPFILES*)GlobalLock(hMem);
            pDrop->pFiles = sizeof(DROPFILES); pDrop->fWide = TRUE;
            char* pData = (char*)pDrop + sizeof(DROPFILES);
            memcpy(pData, wPath.c_str(), wPath.length() * sizeof(wchar_t));
            GlobalUnlock(hMem);
            SetClipboardData(CF_HDROP, hMem);
        }
    }
    CloseClipboard();
}

void UnmapActionByName(const char* actionName) {
    auto controlMap = RE::ControlMap::GetSingleton();
    if (!controlMap) return;
    RE::BSFixedString eventName(actionName);
    for (uint32_t i = 0; i < RE::UserEvents::INPUT_CONTEXT_ID::kTotal; i++) {
        auto context = controlMap->controlMap[i];
        if (!context) continue;
        for (uint32_t deviceIdx = 0; deviceIdx < 3; deviceIdx++) {
            auto& deviceMap = context->deviceMappings[deviceIdx];
            for (auto& mapping : deviceMap) {
                if (mapping.eventID == eventName && mapping.inputKey != 0xFF) {
                    mapping.inputKey = 0xFF; mapping.modifier = 0xFF; mapping.remappable = false;
                }
            }
        }
    }
}

void UnmapNativeScreenshot() {
    UnmapActionByName("Screenshot");
    UnmapActionByName("Multi-Screenshot");
}

bool IsInputDown(uint32_t keyCode) {
    int vk = 0;
    switch (keyCode) {
    case 256: vk = VK_LBUTTON; break; case 257: vk = VK_RBUTTON; break; case 258: vk = VK_MBUTTON; break;
    case 259: vk = VK_XBUTTON1; break; case 260: vk = VK_XBUTTON2; break; case 183: vk = VK_SNAPSHOT; break;
    case 199: vk = VK_HOME; break; case 200: vk = VK_UP; break; case 201: vk = VK_PRIOR; break;
    case 203: vk = VK_LEFT; break; case 205: vk = VK_RIGHT; break; case 207: vk = VK_END; break;
    case 208: vk = VK_DOWN; break; case 209: vk = VK_NEXT; break; case 210: vk = VK_INSERT; break;
    case 211: vk = VK_DELETE; break; case 156: vk = VK_RETURN; break; case 157: vk = VK_RCONTROL; break;
    case 184: vk = VK_RMENU; break;
    default: if (keyCode < 256) vk = MapVirtualKey(keyCode, MAPVK_VSC_TO_VK); break;
    }
    if (vk != 0) return (GetAsyncKeyState(vk) & 0x8000) != 0;
    return false;
}

bool CheckGamepadButton(const XINPUT_GAMEPAD& pad, uint32_t key) {
    switch (key) {
    case 266: return (pad.wButtons & XINPUT_GAMEPAD_DPAD_UP); case 267: return (pad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN);
    case 268: return (pad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT); case 269: return (pad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT);
    case 270: return (pad.wButtons & XINPUT_GAMEPAD_START); case 271: return (pad.wButtons & XINPUT_GAMEPAD_BACK);
    case 272: return (pad.wButtons & XINPUT_GAMEPAD_LEFT_THUMB); case 273: return (pad.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB);
    case 274: return (pad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER); case 275: return (pad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER);
    case 276: return (pad.wButtons & XINPUT_GAMEPAD_A); case 277: return (pad.wButtons & XINPUT_GAMEPAD_B);
    case 278: return (pad.wButtons & XINPUT_GAMEPAD_X); case 279: return (pad.wButtons & XINPUT_GAMEPAD_Y);
    case 280: return pad.bLeftTrigger > 128; case 281: return pad.bRightTrigger > 128;
    }
    return false;
}

std::string GetScreenshotPath(const char* ext) {
    char filename[64];
    std::time_t now = std::time(nullptr);
    std::strftime(filename, sizeof(filename), "Screenshot_%Y%m%d_%H%M%S", std::localtime(&now));
    std::filesystem::path basePath(Settings::screenshotPath);
    if (Settings::screenshotPath.empty()) basePath = std::filesystem::current_path();
    try { if (!std::filesystem::exists(basePath)) std::filesystem::create_directories(basePath); }
    catch (...) { basePath = std::filesystem::current_path(); }
    return (basePath / (std::string(filename) + ext)).string();
}

void CaptureFrameFromSwapChain(IDXGISwapChain* swapChain, ScreenshotFormat format, bool withoutUI) {
    if (!swapChain) return;
    Microsoft::WRL::ComPtr<ID3D11Device> device11;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context11;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer11;
    bool captured = false; bool isDX12Interop = false;
    Microsoft::WRL::ComPtr<ID3D12Device> device12;
    if (SUCCEEDED(swapChain->GetDevice(__uuidof(ID3D12Device), (void**)device12.GetAddressOf()))) {
        isDX12Interop = true;
        if (SUCCEEDED(swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)backBuffer11.GetAddressOf())) && backBuffer11) {
            backBuffer11->GetDevice(device11.GetAddressOf());
            if (device11) { device11->GetImmediateContext(context11.GetAddressOf()); if (context11) captured = true; }
        }
    }
    if (!captured) {
        if (SUCCEEDED(swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)backBuffer11.GetAddressOf())) && backBuffer11) {
            backBuffer11->GetDevice(device11.GetAddressOf());
            if (device11) { device11->GetImmediateContext(context11.GetAddressOf()); if (context11) captured = true; }
        }
    }
    if (!captured || !backBuffer11 || !context11 || !device11) return;

    auto renderer = RE::BSGraphics::Renderer::GetSingleton();
    Microsoft::WRL::ComPtr<ID3D11Resource> uiResource;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> uiTexture11;
    bool hasUI = false;
    if (renderer && !withoutUI && isDX12Interop) {
        auto uiRTV = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kFRAMEBUFFER].RTV;
        if (uiRTV) {
            uiRTV->GetResource(uiResource.GetAddressOf());
            if (SUCCEEDED(uiResource.As(&uiTexture11))) hasUI = true;
        }
    }

    D3D11_TEXTURE2D_DESC desc{}; backBuffer11->GetDesc(&desc);
    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.BindFlags = 0; stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.Usage = D3D11_USAGE_STAGING; stagingDesc.MiscFlags = 0;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> stagingTex;
    if (FAILED(device11->CreateTexture2D(&stagingDesc, nullptr, stagingTex.GetAddressOf()))) return;
    context11->CopyResource(stagingTex.Get(), backBuffer11.Get());

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(context11->Map(stagingTex.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> stagingUITex;
    D3D11_MAPPED_SUBRESOURCE mappedUI = { 0 };
    if (hasUI) {
        D3D11_TEXTURE2D_DESC uiDesc{}; uiTexture11->GetDesc(&uiDesc);
        D3D11_TEXTURE2D_DESC stagingUIDesc = uiDesc;
        stagingUIDesc.BindFlags = 0; stagingUIDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingUIDesc.Usage = D3D11_USAGE_STAGING; stagingUIDesc.MiscFlags = 0;
        if (SUCCEEDED(device11->CreateTexture2D(&stagingUIDesc, nullptr, stagingUITex.GetAddressOf()))) {
            context11->CopyResource(stagingUITex.Get(), uiTexture11.Get());
            if (FAILED(context11->Map(stagingUITex.Get(), 0, D3D11_MAP_READ, 0, &mappedUI))) hasUI = false;
        }
        else hasUI = false;
    }

    int windowWidth = desc.Width; int windowHeight = desc.Height;
    int targetWidth = windowWidth; int targetHeight = windowHeight;
    int startX = 0; int startY = 0;

    if (g_crop.active) {
        startX = g_crop.x; startY = g_crop.y; targetWidth = g_crop.w; targetHeight = g_crop.h;
        if (startX < 0) startX = 0; if (startY < 0) startY = 0;
        if (startX + targetWidth > windowWidth) targetWidth = windowWidth - startX;
        if (startY + targetHeight > windowHeight) targetHeight = windowHeight - startY;
        g_crop.active = false;
    }
    else {
        targetWidth = windowWidth; targetHeight = windowHeight;
        startX = 0; startY = 0;
    }

    std::vector<uint8_t> pixelData(targetWidth * targetHeight * 4);
    uint8_t* src = (uint8_t*)mapped.pData;
    uint8_t* dst = pixelData.data();
    bool isBGRA = (desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM || desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB);
    bool useLasso = !g_crop.lassoPoints.empty();

    for (int y = 0; y < targetHeight; ++y) {
        uint8_t* rowSrc = src + ((startY + y) * mapped.RowPitch) + (startX * 4);
        uint8_t* rowDst = dst + (y * targetWidth * 4);
        uint8_t* rowUI = hasUI ? (uint8_t*)mappedUI.pData + ((startY + y) * mappedUI.RowPitch) + (startX * 4) : nullptr;
        for (int x = 0; x < targetWidth; ++x) {
            int p = x * 4;
            if (useLasso) {
                if (!IsPointInPolygon(startX + x, startY + y, g_crop.lassoPoints)) {
                    rowDst[p + 0] = 0; rowDst[p + 1] = 0; rowDst[p + 2] = 0; rowDst[p + 3] = 0;
                    continue;
                }
            }
            float gameR, gameG, gameB;
            if (isBGRA) { gameR = rowSrc[p + 2]; gameG = rowSrc[p + 1]; gameB = rowSrc[p + 0]; }
            else { gameR = rowSrc[p + 0]; gameG = rowSrc[p + 1]; gameB = rowSrc[p + 2]; }

            if (hasUI && rowUI) {
                float uiR = rowUI[p + 0]; float uiG = rowUI[p + 1]; float uiB = rowUI[p + 2];
                float uiA = rowUI[p + 3] / 255.0f;
                rowDst[p + 0] = (uint8_t)((uiR * uiA) + (gameR * (1.0f - uiA)));
                rowDst[p + 1] = (uint8_t)((uiG * uiA) + (gameG * (1.0f - uiA)));
                rowDst[p + 2] = (uint8_t)((uiB * uiA) + (gameB * (1.0f - uiA)));
                rowDst[p + 3] = 255;
            }
            else {
                rowDst[p + 0] = (uint8_t)gameR; rowDst[p + 1] = (uint8_t)gameG;
                rowDst[p + 2] = (uint8_t)gameB; rowDst[p + 3] = 255;
            }
        }
    }

    if (hasUI) context11->Unmap(stagingUITex.Get(), 0);
    context11->Unmap(stagingTex.Get(), 0);

    std::string path = GetScreenshotPath(format == ScreenshotFormat::PNG ? ".png" : (format == ScreenshotFormat::JPG ? ".jpg" : ".bmp"));
    int success = 0;
    if (format == ScreenshotFormat::PNG) success = stbi_write_png(path.c_str(), targetWidth, targetHeight, 4, pixelData.data(), targetWidth * 4);
    else if (format == ScreenshotFormat::JPG) success = stbi_write_jpg(path.c_str(), targetWidth, targetHeight, 4, pixelData.data(), 90);
    else success = stbi_write_bmp(path.c_str(), targetWidth, targetHeight, 4, pixelData.data());

    if (success) { CopyFileToClipboard(path); }
}

int64_t Hooked_RenderUI(int64_t gMenuManager) {
    if (g_captureNextFrameWithoutUI) {
        if (g_captureDelayFrames > 0) g_captureDelayFrames--;
        else {
            auto renderer = RE::BSGraphics::Renderer::GetSingleton();
            if (renderer && renderer->GetRuntimeData().renderWindows[0].swapChain) {
                CaptureFrameFromSwapChain(reinterpret_cast<IDXGISwapChain*>(renderer->GetRuntimeData().renderWindows[0].swapChain), g_pendingFormat, true);
            }
            g_captureNextFrameWithoutUI = false;
        }
    }
    return OriginalRenderUI(gMenuManager);
}

HRESULT WINAPI Hooked_Present(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
    if (g_captureNextFrameWithUI) {
        if (g_captureDelayFrames > 0) g_captureDelayFrames--;
        else {
            CaptureFrameFromSwapChain(pSwapChain, g_pendingFormat, false);
            g_captureNextFrameWithUI = false;
        }
    }
    return OriginalPresent(pSwapChain, SyncInterval, Flags);
}

void InstallHooks() {
    auto renderer = RE::BSGraphics::Renderer::GetSingleton();
    if (!renderer) return;
    auto swapChain = reinterpret_cast<IDXGISwapChain*>(renderer->GetRuntimeData().renderWindows[0].swapChain);
    if (swapChain) {
        void** vtable = *(void***)swapChain;
        OriginalPresent = (Present_t)vtable[8];
        DWORD oldProtect; VirtualProtect(&vtable[8], sizeof(void*), PAGE_READWRITE, &oldProtect);
        vtable[8] = Hooked_Present; VirtualProtect(&vtable[8], sizeof(void*), oldProtect, &oldProtect);
    }
    SKSE::AllocTrampoline(14);
    auto& trampoline = SKSE::GetTrampoline();
    OriginalRenderUI = reinterpret_cast<RenderUI_t>(trampoline.write_call<5>(
        REL::RelocationID(35556, 36555).address() + REL::Relocate(0x3ab, 0x371), (uintptr_t)Hooked_RenderUI));
}

// Variáveis para rastrear o estado das teclas e evitar que 1 clique dispare 10 fotos
static bool g_wasCapUIPressedAsync = false;
static bool g_wasCapNoUIPressedAsync = false;

void PollAsyncInputs() {
    // --- 1. TECLADO/MOUSE ASYNC (Ignora o roubo de foco do Windows) ---
    if (!Prisma::IsHidden()) {
        bool capUI_k = Settings::captureWithUIKey_k != 0 && IsInputDown(Settings::captureWithUIKey_k);
        bool capUI_m = Settings::captureWithUIKey_m != 0 && IsInputDown(Settings::captureWithUIKey_m);
        bool capUI_pressed = capUI_k || capUI_m;

        if (capUI_pressed && !g_wasCapUIPressedAsync) {
            bool comboHeld = (Settings::captureWithUICombo_k == 0) || IsInputDown(Settings::captureWithUICombo_k);
            if (comboHeld) {
                Prisma::Hide();
                ApplyPendingCropAndTrigger(true);
            }
        }
        g_wasCapUIPressedAsync = capUI_pressed;

        bool capNoUI_k = Settings::captureNoUIKey_k != 0 && IsInputDown(Settings::captureNoUIKey_k);
        bool capNoUI_m = Settings::captureNoUIKey_m != 0 && IsInputDown(Settings::captureNoUIKey_m);
        bool capNoUI_pressed = capNoUI_k || capNoUI_m;

        if (capNoUI_pressed && !g_wasCapNoUIPressedAsync) {
            bool comboHeld = (Settings::captureNoUICombo_k == 0) || IsInputDown(Settings::captureNoUICombo_k);
            if (comboHeld) {
                Prisma::Hide();
                ApplyPendingCropAndTrigger(false);
            }
        }
        g_wasCapNoUIPressedAsync = capNoUI_pressed;
    }

    // --- 2. GAMEPAD (Sua lógica original mantida aqui dentro) ---
    XINPUT_STATE currentState; ZeroMemory(&currentState, sizeof(XINPUT_STATE));
    if (XInputGetState(0, &currentState) != ERROR_SUCCESS) { g_gamepadConnected = false; return; }
    if (!g_gamepadConnected) { g_gamepadConnected = true; g_lastGamepadState = currentState; return; }
    if (currentState.dwPacketNumber == g_lastGamepadState.dwPacketNumber) return;

    auto& pad = currentState.Gamepad; auto& oldPad = g_lastGamepadState.Gamepad;
    auto IsPressed = [&](uint32_t key) -> bool { return key != 0 && CheckGamepadButton(pad, key) && !CheckGamepadButton(oldPad, key); };
    auto IsHeld = [&](uint32_t key) -> bool { return key == 0 || CheckGamepadButton(pad, key); };

    bool triggerOpen = false;
    if (IsPressed(Settings::openModeKey_g)) { if (IsHeld(Settings::openModeCombo_g)) triggerOpen = true; }
    else if (IsPressed(Settings::openModeCombo_g)) { if (IsHeld(Settings::openModeKey_g)) triggerOpen = true; }
    if (triggerOpen) {
        if (Prisma::IsHidden()) Prisma::Show();
        else Prisma::Hide();
    }

    bool triggerCapUI = false;
    if (IsPressed(Settings::captureWithUIKey_g)) { if (IsHeld(Settings::captureWithUICombo_g)) triggerCapUI = true; }
    else if (IsPressed(Settings::captureWithUICombo_g)) { if (IsHeld(Settings::captureWithUIKey_g)) triggerCapUI = true; }
    if (triggerCapUI && !Prisma::IsHidden()) {
        Prisma::Hide();
        ApplyPendingCropAndTrigger(true);
    }

    bool triggerCapNoUI = false;
    if (IsPressed(Settings::captureNoUIKey_g)) { if (IsHeld(Settings::captureNoUICombo_g)) triggerCapNoUI = true; }
    else if (IsPressed(Settings::captureNoUICombo_g)) { if (IsHeld(Settings::captureNoUIKey_g)) triggerCapNoUI = true; }
    if (triggerCapNoUI && !Prisma::IsHidden()) {
        Prisma::Hide();
        ApplyPendingCropAndTrigger(false);
    }
    g_lastGamepadState = currentState;
}

LRESULT CALLBACK MySubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    if (msg == WM_TIMER && wParam == 0x5353) PollAsyncInputs();
    uint32_t pressedKey = 0; bool isKeyboard = false;
    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) {
        pressedKey = MapVirtualKey(static_cast<UINT>(wParam), MAPVK_VK_TO_VSC);
        if ((lParam >> 24) & 1) pressedKey += 128;
        isKeyboard = true;
    }
    else if (msg == WM_KEYUP) {
        if (wParam == VK_SNAPSHOT) pressedKey = 183;
        isKeyboard = true;
    }
    else if (msg == WM_LBUTTONDOWN) pressedKey = 256;
    else if (msg == WM_RBUTTONDOWN) pressedKey = 257;
    else if (msg == WM_MBUTTONDOWN) pressedKey = 258;
    else if (msg == WM_XBUTTONDOWN) pressedKey = (GET_XBUTTON_WPARAM(wParam) == XBUTTON1) ? 259 : 260;
    else return DefSubclassProc(hwnd, msg, wParam, lParam);

    if (pressedKey != 0) {
        bool blockInput = false;

        bool isOpen_Trigger = (isKeyboard && Settings::openModeKey_k != 0 && pressedKey == Settings::openModeKey_k) ||
            (!isKeyboard && Settings::openModeKey_m != 0 && pressedKey == Settings::openModeKey_m);
        if (isOpen_Trigger) {
            bool comboHeld = (Settings::openModeCombo_k == 0) || IsInputDown(Settings::openModeCombo_k);
            if (comboHeld) {
                if (Prisma::IsHidden()) Prisma::Show();
                else Prisma::Hide();
                blockInput = true;
            }
        }

        bool isCapUI_Trigger = (isKeyboard && Settings::captureWithUIKey_k != 0 && pressedKey == Settings::captureWithUIKey_k) ||
            (!isKeyboard && Settings::captureWithUIKey_m != 0 && pressedKey == Settings::captureWithUIKey_m);
        if (isCapUI_Trigger && !Prisma::IsHidden()) {
            bool comboHeld = (Settings::captureWithUICombo_k == 0) || IsInputDown(Settings::captureWithUICombo_k);
            if (comboHeld) {
                Prisma::Hide();
                ApplyPendingCropAndTrigger(true);
                blockInput = true;
            }
        }

        bool isCapNoUI_Trigger = (isKeyboard && Settings::captureNoUIKey_k != 0 && pressedKey == Settings::captureNoUIKey_k) ||
            (!isKeyboard && Settings::captureNoUIKey_m != 0 && pressedKey == Settings::captureNoUIKey_m);
        if (isCapNoUI_Trigger && !Prisma::IsHidden()) {
            bool comboHeld = (Settings::captureNoUICombo_k == 0) || IsInputDown(Settings::captureNoUICombo_k);
            if (comboHeld) {
                Prisma::Hide();
                ApplyPendingCropAndTrigger(false);
                blockInput = true;
            }
        }
        if (blockInput) return 0;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

void SetupInputHook() {
    auto taskInterface = SKSE::GetTaskInterface();
    if (taskInterface) {
        taskInterface->AddTask([]() {
            INITCOMMONCONTROLSEX icex; icex.dwSize = sizeof(INITCOMMONCONTROLSEX); icex.dwICC = ICC_WIN95_CLASSES;
            InitCommonControlsEx(&icex);
            auto renderer = RE::BSGraphics::Renderer::GetSingleton();
            if (renderer) g_hWindow = (HWND)renderer->GetRuntimeData().renderWindows[0].hWnd;
            if (!g_hWindow) g_hWindow = FindWindowA(nullptr, "Skyrim Special Edition");
            if (g_hWindow) {
                DWORD windowPid = 0; GetWindowThreadProcessId(g_hWindow, &windowPid);
                if (windowPid != GetCurrentProcessId()) return;
                if (SetWindowSubclass(g_hWindow, MySubclassProc, SCREENSHOT_SUBCLASS_ID, 0)) {
                    SetTimer(g_hWindow, 0x5353, 16, nullptr);
                    UnmapNativeScreenshot(); InstallHooks();
                }
            }
            });
    }
}