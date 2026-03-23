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
#include "SKSE/Trampoline.h"
#include "REL/Relocation.h"

#pragma comment(lib, "Xinput9_1_0.lib")
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#include <d3d12.h>
#pragma comment(lib, "d3d12.lib")

HWND g_hWindow = nullptr;
static XINPUT_STATE g_lastGamepadState;
static bool g_gamepadConnected = false;
static constexpr UINT_PTR SCREENSHOT_SUBCLASS_ID = 0x5353484F54;

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
                if (mapping.eventID == eventName) {
                    if (mapping.inputKey != 0xFF) {
                        SKSE::log::info("Acao '{}' desmapeada. Contexto: {}, Dispositivo: {}", actionName, i, deviceIdx);
                        mapping.inputKey = 0xFF;
                        mapping.modifier = 0xFF;
                        mapping.remappable = false;
                    }
                }
            }
        }
    }
}

void UnmapNativeScreenshot() {
    SKSE::log::info("Iniciando remocao de atalhos nativos...");
    UnmapActionByName("Screenshot");
    UnmapActionByName("Multi-Screenshot");
}

bool IsInputDown(uint32_t keyCode) {
    int vk = 0;
    switch (keyCode) {
    case 256: vk = VK_LBUTTON; break;
    case 257: vk = VK_RBUTTON; break;
    case 258: vk = VK_MBUTTON; break;
    case 259: vk = VK_XBUTTON1; break;
    case 260: vk = VK_XBUTTON2; break;
    case 183: vk = VK_SNAPSHOT; break;
    case 199: vk = VK_HOME; break;
    case 200: vk = VK_UP; break;
    case 201: vk = VK_PRIOR; break;
    case 203: vk = VK_LEFT; break;
    case 205: vk = VK_RIGHT; break;
    case 207: vk = VK_END; break;
    case 208: vk = VK_DOWN; break;
    case 209: vk = VK_NEXT; break;
    case 210: vk = VK_INSERT; break;
    case 211: vk = VK_DELETE; break;
    case 156: vk = VK_RETURN; break;
    case 157: vk = VK_RCONTROL; break;
    case 184: vk = VK_RMENU; break;
    default:
        if (keyCode < 256) vk = MapVirtualKey(keyCode, MAPVK_VSC_TO_VK);
        break;
    }

    if (vk != 0) return (GetAsyncKeyState(vk) & 0x8000) != 0;
    return false;
}

bool CheckGamepadButton(const XINPUT_GAMEPAD& pad, uint32_t key) {
    switch (key) {
    case 266: return (pad.wButtons & XINPUT_GAMEPAD_DPAD_UP);
    case 267: return (pad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN);
    case 268: return (pad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT);
    case 269: return (pad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT);
    case 270: return (pad.wButtons & XINPUT_GAMEPAD_START);
    case 271: return (pad.wButtons & XINPUT_GAMEPAD_BACK);
    case 272: return (pad.wButtons & XINPUT_GAMEPAD_LEFT_THUMB);
    case 273: return (pad.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB);
    case 274: return (pad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER);
    case 275: return (pad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER);
    case 276: return (pad.wButtons & XINPUT_GAMEPAD_A);
    case 277: return (pad.wButtons & XINPUT_GAMEPAD_B);
    case 278: return (pad.wButtons & XINPUT_GAMEPAD_X);
    case 279: return (pad.wButtons & XINPUT_GAMEPAD_Y);
    case 280: return pad.bLeftTrigger > 128;
    case 281: return pad.bRightTrigger > 128;
    }
    return false;
}

std::string GetScreenshotPath(const char* ext) {
    char filename[64];
    std::time_t now = std::time(nullptr);
    std::strftime(filename, sizeof(filename), "Screenshot_%Y%m%d_%H%M%S", std::localtime(&now));

    std::filesystem::path basePath(Settings::screenshotPath);
    if (Settings::screenshotPath.empty()) basePath = std::filesystem::current_path();

    try {
        if (!std::filesystem::exists(basePath)) std::filesystem::create_directories(basePath);
    }
    catch (const std::exception& e) {
        basePath = std::filesystem::current_path();
    }

    return (basePath / (std::string(filename) + ext)).string();
}

// -----------------------------------------------------------
// GLOBAIS E HOOKS
// -----------------------------------------------------------
static bool g_captureNextFrameWithUI = false;
static bool g_captureNextFrameWithoutUI = false;
static ScreenshotFormat g_pendingFormat;

typedef HRESULT(WINAPI* Present_t)(IDXGISwapChain*, UINT, UINT);
Present_t OriginalPresent = nullptr;

typedef int64_t(*RenderUI_t)(int64_t);
RenderUI_t OriginalRenderUI = nullptr;

// -----------------------------------------------------------
// CORE DA CAPTURA (Trata os casos de DX11 / DX12)
// -----------------------------------------------------------
void CaptureFrameFromSwapChain(IDXGISwapChain* swapChain, ScreenshotFormat format, bool withoutUI) {
    if (!swapChain) return;

    Microsoft::WRL::ComPtr<ID3D11Device> device11;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context11;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer11;
    bool captured = false;

    // Verificar se estamos no ambiente DX12 FrameGen (Community Shaders / Upscalers)
    bool isDX12Interop = false;
    Microsoft::WRL::ComPtr<ID3D12Device> device12;
    if (SUCCEEDED(swapChain->GetDevice(__uuidof(ID3D12Device), (void**)device12.GetAddressOf()))) {
        isDX12Interop = true;
        if (SUCCEEDED(swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)backBuffer11.GetAddressOf())) && backBuffer11) {
            backBuffer11->GetDevice(device11.GetAddressOf());
            if (device11) {
                device11->GetImmediateContext(context11.GetAddressOf());
                if (context11) captured = true;
            }
        }
    }

    if (!captured) {
        if (SUCCEEDED(swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)backBuffer11.GetAddressOf())) && backBuffer11) {
            backBuffer11->GetDevice(device11.GetAddressOf());
            if (device11) {
                device11->GetImmediateContext(context11.GetAddressOf());
                if (context11) captured = true;
            }
        }
    }

    if (!captured || !backBuffer11 || !context11 || !device11) return;

    // --- 1. CAPTURAR A TEXTURA DA UI (kFRAMEBUFFER) ---
    // Apenas capturamos a UI se NÃO pedirmos clean screenshot E se estivermos num proxy FrameGen (DX12)
    // Em DX11 Vanilla, a UI já vem desenhada no backbuffer!
    auto renderer = RE::BSGraphics::Renderer::GetSingleton();
    Microsoft::WRL::ComPtr<ID3D11Resource> uiResource;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> uiTexture11;
    bool hasUI = false;

    if (renderer && !withoutUI && isDX12Interop) {
        auto uiRTV = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kFRAMEBUFFER].RTV;
        if (uiRTV) {
            uiRTV->GetResource(uiResource.GetAddressOf());
            if (SUCCEEDED(uiResource.As(&uiTexture11))) {
                hasUI = true;
            }
        }
    }

    // --- 2. CRIAR STAGING DO JOGO ---
    D3D11_TEXTURE2D_DESC desc{};
    backBuffer11->GetDesc(&desc);

    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.MiscFlags = 0;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> stagingTex;
    if (FAILED(device11->CreateTexture2D(&stagingDesc, nullptr, stagingTex.GetAddressOf()))) return;
    context11->CopyResource(stagingTex.Get(), backBuffer11.Get());

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(context11->Map(stagingTex.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return;

    // --- 3. CRIAR STAGING DA UI (Se existir proxy DX12) ---
    Microsoft::WRL::ComPtr<ID3D11Texture2D> stagingUITex;
    D3D11_MAPPED_SUBRESOURCE mappedUI = { 0 };

    if (hasUI) {
        D3D11_TEXTURE2D_DESC uiDesc{};
        uiTexture11->GetDesc(&uiDesc);
        D3D11_TEXTURE2D_DESC stagingUIDesc = uiDesc;
        stagingUIDesc.BindFlags = 0;
        stagingUIDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingUIDesc.Usage = D3D11_USAGE_STAGING;
        stagingUIDesc.MiscFlags = 0;

        if (SUCCEEDED(device11->CreateTexture2D(&stagingUIDesc, nullptr, stagingUITex.GetAddressOf()))) {
            context11->CopyResource(stagingUITex.Get(), uiTexture11.Get());
            if (FAILED(context11->Map(stagingUITex.Get(), 0, D3D11_MAP_READ, 0, &mappedUI))) hasUI = false;
        }
        else {
            hasUI = false;
        }
    }

    // --- 4. RECORTAR E BLENDING ---
    int windowWidth = desc.Width;
    int windowHeight = desc.Height;
    int targetWidth = Settings::useCustomResolution ? (std::min)((int)Settings::customWidth, windowWidth) : windowWidth;
    int targetHeight = Settings::useCustomResolution ? (std::min)((int)Settings::customHeight, windowHeight) : windowHeight;

    int startX = (windowWidth - targetWidth) / 2;
    int startY = (windowHeight - targetHeight) / 2;

    std::vector<uint8_t> pixelData(targetWidth * targetHeight * 4);
    uint8_t* src = (uint8_t*)mapped.pData;
    uint8_t* dst = pixelData.data();

    bool isBGRA = (desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM || desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB);

    for (int y = 0; y < targetHeight; ++y) {
        uint8_t* rowSrc = src + ((startY + y) * mapped.RowPitch) + (startX * 4);
        uint8_t* rowDst = dst + (y * targetWidth * 4);
        uint8_t* rowUI = hasUI ? (uint8_t*)mappedUI.pData + ((startY + y) * mappedUI.RowPitch) + (startX * 4) : nullptr;

        for (int x = 0; x < targetWidth; ++x) {
            int p = x * 4;
            float gameR, gameG, gameB;

            if (isBGRA) {
                gameR = rowSrc[p + 2]; gameG = rowSrc[p + 1]; gameB = rowSrc[p + 0];
            }
            else {
                gameR = rowSrc[p + 0]; gameG = rowSrc[p + 1]; gameB = rowSrc[p + 2];
            }

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

    // --- 5. SALVAR ---
    std::string path = GetScreenshotPath(format == ScreenshotFormat::PNG ? ".png" : (format == ScreenshotFormat::JPG ? ".jpg" : ".bmp"));
    int success = 0;

    if (format == ScreenshotFormat::PNG) success = stbi_write_png(path.c_str(), targetWidth, targetHeight, 4, pixelData.data(), targetWidth * 4);
    else if (format == ScreenshotFormat::JPG) success = stbi_write_jpg(path.c_str(), targetWidth, targetHeight, 4, pixelData.data(), 90);
    else success = stbi_write_bmp(path.c_str(), targetWidth, targetHeight, 4, pixelData.data());

    if (success) logger::info("Screenshot salvo com sucesso: {}", path);
}

// -----------------------------------------------------------
// GESTÃO DE HOOKS
// -----------------------------------------------------------
void TriggerScreenshotRequest(ScreenshotFormat format, bool withoutUI = false) {
    g_pendingFormat = format;
    if (withoutUI) {
        g_captureNextFrameWithoutUI = true;
    }
    else {
        g_captureNextFrameWithUI = true;
    }
}

// Intercepta a chamada que desenha a UI. Excelente para fotos LIMPAS!
int64_t Hooked_RenderUI(int64_t gMenuManager) {
    if (g_captureNextFrameWithoutUI) {
        auto renderer = RE::BSGraphics::Renderer::GetSingleton();
        if (renderer && renderer->GetRuntimeData().renderWindows[0].swapChain) {
            auto swapChain = reinterpret_cast<IDXGISwapChain*>(renderer->GetRuntimeData().renderWindows[0].swapChain);
            CaptureFrameFromSwapChain(swapChain, g_pendingFormat, true);
        }
        g_captureNextFrameWithoutUI = false;
    }
    return OriginalRenderUI(gMenuManager);
}

// Intercepta o final do frame. Excelente para fotos com UI incluída!
HRESULT WINAPI Hooked_Present(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
    if (g_captureNextFrameWithUI) {
        CaptureFrameFromSwapChain(pSwapChain, g_pendingFormat, false);
        g_captureNextFrameWithUI = false;
    }
    return OriginalPresent(pSwapChain, SyncInterval, Flags);
}

void InstallHooks() {
    auto renderer = RE::BSGraphics::Renderer::GetSingleton();
    if (!renderer) return;

    // 1. Hook de IDXGISwapChain::Present (Para fotos COM UI)
    auto swapChain = reinterpret_cast<IDXGISwapChain*>(renderer->GetRuntimeData().renderWindows[0].swapChain);
    if (swapChain) {
        void** vtable = *(void***)swapChain;
        OriginalPresent = (Present_t)vtable[8];

        DWORD oldProtect;
        VirtualProtect(&vtable[8], sizeof(void*), PAGE_READWRITE, &oldProtect);
        vtable[8] = Hooked_Present;
        VirtualProtect(&vtable[8], sizeof(void*), oldProtect, &oldProtect);
        logger::info("Hook de IDXGISwapChain::Present instalado com sucesso.");
    }

    // 2. Hook de RenderUI (Para fotos SEM UI)
    // Usamos o Trampoline do SKSE para intercetar o "call" do MenuManager::Render
    SKSE::AllocTrampoline(14);
    auto& trampoline = SKSE::GetTrampoline();
    OriginalRenderUI = reinterpret_cast<RenderUI_t>(trampoline.write_call<5>(
        REL::RelocationID(35556, 36555).address() + REL::Relocate(0x3ab, 0x371),
        (uintptr_t)Hooked_RenderUI
    ));
    logger::info("Hook de RenderUI instalado com sucesso.");
}

void PollGamepad() {
    XINPUT_STATE currentState;
    ZeroMemory(&currentState, sizeof(XINPUT_STATE));

    if (XInputGetState(0, &currentState) != ERROR_SUCCESS) {
        g_gamepadConnected = false;
        return;
    }
    if (!g_gamepadConnected) {
        g_gamepadConnected = true;
        g_lastGamepadState = currentState;
        return;
    }
    if (currentState.dwPacketNumber == g_lastGamepadState.dwPacketNumber) return;

    auto& pad = currentState.Gamepad;
    auto& oldPad = g_lastGamepadState.Gamepad;

    auto IsPressed = [&](uint32_t key) -> bool {
        if (key == 0) return false;
        return CheckGamepadButton(pad, key) && !CheckGamepadButton(oldPad, key);
        };
    auto IsHeld = [&](uint32_t key) -> bool {
        if (key == 0) return true;
        return CheckGamepadButton(pad, key);
        };

    // --- LÓGICA DE SCREENSHOT NORMAL (COM UI) ---
    bool triggerSS = false;
    if (IsPressed(Settings::screenshotKey_g)) {
        if (IsHeld(Settings::comboKey_g)) triggerSS = true;
    }
    else if (IsPressed(Settings::comboKey_g)) {
        if (IsHeld(Settings::screenshotKey_g)) triggerSS = true;
    }
    if (triggerSS) TriggerScreenshotRequest(Settings::imageFormat, false);

    // --- LÓGICA SCREENSHOT SEM UI ---
    bool triggerSSNoUI = false;
    if (IsPressed(Settings::toggleUIKey_g)) {
        if (IsHeld(Settings::toggleUIComboKey_g)) triggerSSNoUI = true;
    }
    else if (IsPressed(Settings::toggleUIComboKey_g)) {
        if (IsHeld(Settings::toggleUIKey_g)) triggerSSNoUI = true;
    }
    if (triggerSSNoUI) TriggerScreenshotRequest(Settings::imageFormat, true);

    g_lastGamepadState = currentState;
}

LRESULT CALLBACK MySubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    if (msg == WM_TIMER && wParam == 0x5353) {
        PollGamepad();
    }

    uint32_t pressedKey = 0;
    bool isKeyboard = false;

    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) {
        pressedKey = MapVirtualKey(static_cast<UINT>(wParam), MAPVK_VK_TO_VSC);
        if ((lParam >> 24) & 1) pressedKey += 128;
        isKeyboard = true;
    }
    else if (msg == WM_KEYUP) {
        if (wParam == VK_SNAPSHOT) pressedKey = 183;
        isKeyboard = true;
    }
    else if (msg == WM_LBUTTONDOWN) { pressedKey = 256; }
    else if (msg == WM_RBUTTONDOWN) { pressedKey = 257; }
    else if (msg == WM_MBUTTONDOWN) { pressedKey = 258; }
    else if (msg == WM_XBUTTONDOWN) {
        pressedKey = (GET_XBUTTON_WPARAM(wParam) == XBUTTON1) ? 259 : 260;
    }
    else {
        return DefSubclassProc(hwnd, msg, wParam, lParam);
    }

    if (pressedKey != 0) {
        bool blockInput = false;

        bool isSS_Trigger = (isKeyboard && Settings::screenshotKey_k != 0 && pressedKey == Settings::screenshotKey_k) ||
            (!isKeyboard && Settings::screenshotKey_m != 0 && pressedKey == Settings::screenshotKey_m);

        if (isSS_Trigger) {
            bool comboHeld = (Settings::comboKey_k == 0) || IsInputDown(Settings::comboKey_k);
            if (comboHeld) {
                TriggerScreenshotRequest(Settings::imageFormat, false);
                RE::SendHUDMessage::ShowHUDMessage("Screenshot captured!");
                blockInput = true;
            }
        }

        bool isSSNoUI_Trigger = (isKeyboard && Settings::toggleUIKey_k != 0 && pressedKey == Settings::toggleUIKey_k) ||
            (!isKeyboard && Settings::toggleUIKey_m != 0 && pressedKey == Settings::toggleUIKey_m);

        if (isSSNoUI_Trigger) {
            bool comboHeld = (Settings::toggleUIComboKey_k == 0) || IsInputDown(Settings::toggleUIComboKey_k);
            if (comboHeld) {
                TriggerScreenshotRequest(Settings::imageFormat, true);
                RE::SendHUDMessage::ShowHUDMessage("Screenshot (No UI) captured!");
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
            INITCOMMONCONTROLSEX icex;
            icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
            icex.dwICC = ICC_WIN95_CLASSES;
            InitCommonControlsEx(&icex);

            auto renderer = RE::BSGraphics::Renderer::GetSingleton();
            if (renderer) g_hWindow = (HWND)renderer->GetRuntimeData().renderWindows[0].hWnd;
            if (!g_hWindow) g_hWindow = FindWindowA(nullptr, "Skyrim Special Edition");

            if (g_hWindow) {
                DWORD windowPid = 0;
                GetWindowThreadProcessId(g_hWindow, &windowPid);
                DWORD currentPid = GetCurrentProcessId();

                if (windowPid != currentPid) return;

                if (SetWindowSubclass(g_hWindow, MySubclassProc, SCREENSHOT_SUBCLASS_ID, 0)) {
                    SetTimer(g_hWindow, 0x5353, 16, nullptr);
                    UnmapNativeScreenshot();

                    // Instalar ambos os Hooks
                    InstallHooks();
                }
            }});
    }
}