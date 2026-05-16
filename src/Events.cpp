#include "Events.h"
#include "logger.h"
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <vector>
#include <ctime>
#include <string>
#include <filesystem>
#include "MinHook.h"
#include <Xinput.h>
#include <shlobj.h>
#include "Prisma.h"
#include <cmath>

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

static DXGI_COLOR_SPACE_TYPE g_currentColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709; // Assumimos SDR padrão
static bool g_colorSpaceHooked = false;

typedef HRESULT(WINAPI* SetColorSpace1_t)(IDXGISwapChain3*, DXGI_COLOR_SPACE_TYPE);
SetColorSpace1_t OriginalSetColorSpace1 = nullptr;

// Hook no DXGI: Toda vez que o Community Shaders ativar/desativar o HDR, nós interceptamos e salvamos
HRESULT WINAPI Hooked_SetColorSpace1(IDXGISwapChain3* pSwapChain, DXGI_COLOR_SPACE_TYPE ColorSpace) {
    g_currentColorSpace = ColorSpace;
    logger::info("[DXGI] SetColorSpace1 interceptado! Novo Color Space: {}", (uint32_t)ColorSpace);
    return OriginalSetColorSpace1(pSwapChain, ColorSpace);
}

typedef HRESULT(WINAPI* Present_t)(IDXGISwapChain*, UINT, UINT);
Present_t OriginalPresent = nullptr;

typedef int64_t(*RenderUI_t)(int64_t);
RenderUI_t OriginalRenderUI = nullptr;

std::atomic<bool> g_isCapturing = false;

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
    g_isCapturing = true;
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

// Decodifica PQ (Perceptual Quantizer - SMPTE ST 2084) para Nits Reais
inline float DecodePQ(float V) {
    float V_clamp = std::max(0.0f, std::min(V, 1.0f));
    float m1 = 2610.0f / 16384.0f;
    float m2 = 2523.0f / 32.0f;
    float c1 = 3424.0f / 4096.0f;
    float c2 = 2413.0f / 128.0f;
    float c3 = 2392.0f / 128.0f;

    float V_pow = std::pow(V_clamp, 1.0f / m2);
    float num = std::max(V_pow - c1, 0.0f);
    float den = c2 - c3 * V_pow;
    float L = std::pow(num / den, 1.0f / m1);

    return L * 10000.0f; // Retorna a luminosidade exata em Nits
}

// Converte o Espaço de Cor BT.2020 (Monitor HDR) para sRGB (Monitor SDR / JPG)
inline void ConvertBT2020TosRGB(float& r, float& g, float& b) {
    float sR = 1.660491f * r - 0.587641f * g - 0.072850f * b;
    float sG = -0.124550f * r + 1.132900f * g - 0.008349f * b;
    float sB = -0.018151f * r - 0.100579f * g + 1.118730f * b;
    r = std::max(0.0f, sR);
    g = std::max(0.0f, sG);
    b = std::max(0.0f, sB);
}

// Converte Cor Linear HDR para SDR usando curva ACES e Gamma sRGB
inline float ProcessHDRtoSDR(float linearColor) {
    // Para evitar valores negativos
    linearColor = std::max(0.0f, linearColor);

    // Como o jogo (via ISHDR.hlsl) já aplica um tonemapping fílmico excelente,
    // aplicar ACES de novo aqui causa "double tonemapping" e o Hue Shift avermelhado.
    // Basta dar um clamp no paper white (1.0f) para evitar estouro no SDR.
    float mapped = std::min(linearColor, 1.0f);

    // Gamma Correction (Aproximação sRGB padrão de 2.2)
    mapped = pow(mapped, 1.0f / 2.2f);

    return mapped;
}

// Converte Half-Float (16-bits) para Float padrão (32-bits)
inline float ConvertHalfToFloat(uint16_t h) {
    uint32_t sign = (h >> 15) & 0x00000001;
    uint32_t exp = (h >> 10) & 0x0000001F;
    uint32_t mant = h & 0x000003FF;
    if (exp == 0) {
        if (mant == 0) return sign ? -0.0f : 0.0f;
        while ((mant & 0x00000400) == 0) { mant <<= 1; exp--; }
        exp++; mant &= ~0x00000400;
    }
    else if (exp == 31) {
        if (mant == 0) return sign ? -INFINITY : INFINITY;
        return NAN;
    }
    exp = exp + (127 - 15);
    mant = mant << 13;
    uint32_t fBits = (sign << 31) | (exp << 23) | mant;
    float f; memcpy(&f, &fBits, sizeof(f));
    return f;
}

// Descobre o tamanho real do Pixel em Bytes
inline int GetBytesPerPixel(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_UNORM:
    case DXGI_FORMAT_R16G16B16A16_UINT:
    case DXGI_FORMAT_R16G16B16A16_SNORM:
    case DXGI_FORMAT_R16G16B16A16_SINT:
        return 8;
    default:
        return 4; // R8G8B8A8, B8G8R8A8, R10G10B10A2
    }
}

// Lê os canais RGBA corretamente para os formatos modernos e vanilla do Skyrim
inline void ReadPixel(uint8_t* rowData, int x, DXGI_FORMAT format, int bpp, float& r, float& g, float& b, float& a, bool isHDR10 = false) {
    if (bpp == 8 && format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
        uint16_t* p16 = (uint16_t*)(rowData + x * 8);
        r = ConvertHalfToFloat(p16[0]);
        g = ConvertHalfToFloat(p16[1]);
        b = ConvertHalfToFloat(p16[2]);
        a = ConvertHalfToFloat(p16[3]);

        r = ProcessHDRtoSDR(r) * 255.0f;
        g = ProcessHDRtoSDR(g) * 255.0f;
        b = ProcessHDRtoSDR(b) * 255.0f;
        a = (a > 1.0f ? 1.0f : (a < 0.0f ? 0.0f : a)) * 255.0f;
    }
    else if (format == DXGI_FORMAT_R10G10B10A2_UNORM) {
        uint32_t p32 = *(uint32_t*)(rowData + x * 4);
        r = (p32 & 0x3FF) / 1023.0f;
        g = ((p32 >> 10) & 0x3FF) / 1023.0f;
        b = ((p32 >> 20) & 0x3FF) / 1023.0f;
        a = ((p32 >> 30) & 0x3) / 3.0f;

        if (isHDR10) {
            // Se HDR estiver ligado: Decodifica PQ, Converte BT.2020 e Tonemap
            r = DecodePQ(r);
            g = DecodePQ(g);
            b = DecodePQ(b);

            ConvertBT2020TosRGB(r, g, b);

            r /= 203.0f;
            g /= 203.0f;
            b /= 203.0f;

            r = ProcessHDRtoSDR(r) * 255.0f;
            g = ProcessHDRtoSDR(g) * 255.0f;
            b = ProcessHDRtoSDR(b) * 255.0f;
        }
        else {
            // Se HDR estiver desligado: Apenas escala as cores normais (SDR 10-bits) para 255
            r *= 255.0f;
            g *= 255.0f;
            b *= 255.0f;
        }

        a = a * 255.0f;
    }
    else {
        uint8_t* p8 = rowData + x * 4;
        if (format == DXGI_FORMAT_B8G8R8A8_UNORM || format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) {
            r = p8[2]; g = p8[1]; b = p8[0]; a = p8[3];
        }
        else {
            r = p8[0]; g = p8[1]; b = p8[2]; a = p8[3];
        }
    }
}


std::string GetDXGIFormatName(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_R16G16B16A16_FLOAT: return "R16G16B16A16_FLOAT";
    case DXGI_FORMAT_R10G10B10A2_UNORM: return "R10G10B10A2_UNORM";
    case DXGI_FORMAT_R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
    case DXGI_FORMAT_B8G8R8A8_UNORM: return "B8G8R8A8_UNORM";
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return "R8G8B8A8_UNORM_SRGB";
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return "B8G8R8A8_UNORM_SRGB";
    default: return "OUTRO_FORMATO_(" + std::to_string(format) + ")";
    }
}

bool IsHDRActive(IDXGISwapChain* swapChain) {
    if (Settings::colorSpaceMode == 1) return true;
    if (Settings::colorSpaceMode == 2) return false;

    // Se o nosso hook detectou o CS alterando o Color Space (Dinâmico)
    if (g_colorSpaceHooked) {
        return (g_currentColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 ||
            g_currentColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709);
    }

    // Fallback: Se não hookamos a tempo da inicialização do CS, checamos o monitor físico do Windows
    if (!swapChain) return false;
    Microsoft::WRL::ComPtr<IDXGIOutput> output;
    if (SUCCEEDED(swapChain->GetContainingOutput(&output))) {
        Microsoft::WRL::ComPtr<IDXGIOutput6> output6;
        if (SUCCEEDED(output.As(&output6))) {
            DXGI_OUTPUT_DESC1 desc1;
            if (SUCCEEDED(output6->GetDesc1(&desc1))) {
                if (desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 ||
                    desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709) {
                    return true; // Monitor em HDR, assumimos CS em HDR
                }
            }
        }
    }
    return false;
}

void CaptureFrameFromSwapChain(ID3D11Texture2D* sourceTex, ScreenshotFormat format, bool withoutUI, const char* callerContext) {
    std::shared_ptr<void> unlocker(nullptr, [](void*) { g_isCapturing = false; });

    logger::info("==================================================");
    logger::info("[CAPTURE] Iniciando captura disparada por: {}", callerContext);

    if (!sourceTex) {
        logger::error("[CAPTURE] ERRO: sourceTex eh nulo!");
        return;
    }

    D3D11_TEXTURE2D_DESC desc{};
    sourceTex->GetDesc(&desc);

    auto renderer = RE::BSGraphics::Renderer::GetSingleton();
    IDXGISwapChain* pSwapChain = renderer ? reinterpret_cast<IDXGISwapChain*>(renderer->GetRuntimeData().renderWindows[0].swapChain) : nullptr;

    bool isHDRSpace = IsHDRActive(pSwapChain);

    Microsoft::WRL::ComPtr<ID3D11Device> device11;
    sourceTex->GetDevice(device11.GetAddressOf());
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context11;
    if (device11) device11->GetImmediateContext(context11.GetAddressOf());

    if (!device11 || !context11) {
        logger::error("[CAPTURE] ERRO: Falha ao obter device ou context do DirectX.");
        return;
    }

    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.MiscFlags = 0;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> stagingTex;
    if (FAILED(device11->CreateTexture2D(&stagingDesc, nullptr, stagingTex.GetAddressOf()))) return;

    context11->CopyResource(stagingTex.Get(), sourceTex);

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(context11->Map(stagingTex.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return;

    // 1. Cópia EXTREMAMENTE RÁPIDA (Memcpy) para a memória RAM. Libera o jogo imediatamente!
    size_t dataSize = mapped.RowPitch * desc.Height;
    std::vector<uint8_t> rawBuffer(dataSize);
    memcpy(rawBuffer.data(), mapped.pData, dataSize);

    context11->Unmap(stagingTex.Get(), 0);

    // 2. Salva o estado atual do crop para enviar para a thread separada
    CropData currentCrop = g_crop;
    g_crop.active = false; // Reseta globalmente para a próxima captura não bugar

    logger::info("[CAPTURE] Frame mapeado e copiado com sucesso. Despachando para processamento assincrono...");

    // 3. Processamento pesado (Matemática HDR, Lasso e HD) transferido 100% para a thread separada!
    std::thread([format, desc, withoutUI, isHDRSpace, rowPitch = mapped.RowPitch,
        rawBuffer = std::move(rawBuffer), currentCrop, unlocker]() mutable {

            int windowWidth = desc.Width; int windowHeight = desc.Height;
            int targetWidth = windowWidth; int targetHeight = windowHeight;
            int startX = 0; int startY = 0;

            if (currentCrop.active) {
                startX = currentCrop.x; startY = currentCrop.y;
                targetWidth = currentCrop.w; targetHeight = currentCrop.h;
                if (startX < 0) startX = 0; if (startY < 0) startY = 0;
                if (startX + targetWidth > windowWidth) targetWidth = windowWidth - startX;
                if (startY + targetHeight > windowHeight) targetHeight = windowHeight - startY;
            }

            std::vector<uint8_t> pixelData(targetWidth * targetHeight * 4);
            uint8_t* src = rawBuffer.data();
            uint8_t* dst = pixelData.data();

            bool isBGRA = (desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM || desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB);
            int gameBpp = (desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT) ? 8 : 4;
            bool useLasso = !currentCrop.lassoPoints.empty();

            for (int y = 0; y < targetHeight; ++y) {
                uint8_t* rowSrc = src + ((startY + y) * rowPitch) + (startX * gameBpp);
                uint8_t* rowDst = dst + (y * targetWidth * 4);

                for (int x = 0; x < targetWidth; ++x) {
                    int p = x * 4;

                    // Se usar Lasso, testa a colisão na thread secundária (zero impacto no FPS do jogo)
                    if (useLasso && !IsPointInPolygon(startX + x, startY + y, currentCrop.lassoPoints)) {
                        rowDst[p + 0] = 0; rowDst[p + 1] = 0; rowDst[p + 2] = 0; rowDst[p + 3] = 0;
                        continue;
                    }

                    float gameR = 0.0f, gameG = 0.0f, gameB = 0.0f;

                    if (desc.Format == DXGI_FORMAT_R10G10B10A2_UNORM) {
                        uint32_t p32 = *(uint32_t*)(rowSrc + x * 4);
                        float r = (p32 & 0x3FF) / 1023.0f;
                        float g = ((p32 >> 10) & 0x3FF) / 1023.0f;
                        float b = ((p32 >> 20) & 0x3FF) / 1023.0f;

                        if (withoutUI) {
                            gameR = ProcessHDRtoSDR(r) * 255.0f;
                            gameG = ProcessHDRtoSDR(g) * 255.0f;
                            gameB = ProcessHDRtoSDR(b) * 255.0f;
                        }
                        else {
                            if (isHDRSpace) {
                                r = DecodePQ(r); g = DecodePQ(g); b = DecodePQ(b);
                                ConvertBT2020TosRGB(r, g, b);
                                r /= 203.0f; g /= 203.0f; b /= 203.0f;
                                gameR = ProcessHDRtoSDR(r) * 255.0f;
                                gameG = ProcessHDRtoSDR(g) * 255.0f;
                                gameB = ProcessHDRtoSDR(b) * 255.0f;
                            }
                            else {
                                gameR = r * 255.0f; gameG = g * 255.0f; gameB = b * 255.0f;
                            }
                        }
                    }
                    else if (desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT) {
                        uint16_t* p16 = (uint16_t*)(rowSrc + x * 8);
                        float r = ConvertHalfToFloat(p16[0]);
                        float g = ConvertHalfToFloat(p16[1]);
                        float b = ConvertHalfToFloat(p16[2]);

                        if (withoutUI) {
                            // Captura pura sem UI: geralmente já é scRGB linear
                            gameR = ProcessHDRtoSDR(r) * 255.0f;
                            gameG = ProcessHDRtoSDR(g) * 255.0f;
                            gameB = ProcessHDRtoSDR(b) * 255.0f;
                        }
                        else {
                            if (isHDRSpace) {
                                // Faltava fazer o Decode do HDR aqui também!
                                r = DecodePQ(r); g = DecodePQ(g); b = DecodePQ(b);
                                ConvertBT2020TosRGB(r, g, b);
                                r /= 203.0f; g /= 203.0f; b /= 203.0f;
                                gameR = ProcessHDRtoSDR(r) * 255.0f;
                                gameG = ProcessHDRtoSDR(g) * 255.0f;
                                gameB = ProcessHDRtoSDR(b) * 255.0f;
                            }
                            else {
                                gameR = (r > 1.0f ? 1.0f : (r < 0.0f ? 0.0f : r)) * 255.0f;
                                gameG = (g > 1.0f ? 1.0f : (g < 0.0f ? 0.0f : g)) * 255.0f;
                                gameB = (b > 1.0f ? 1.0f : (b < 0.0f ? 0.0f : b)) * 255.0f;
                            }
                        }
                    }
                    else {
                        if (isBGRA) { gameR = rowSrc[x * 4 + 2]; gameG = rowSrc[x * 4 + 1]; gameB = rowSrc[x * 4 + 0]; }
                        else { gameR = rowSrc[x * 4 + 0]; gameG = rowSrc[x * 4 + 1]; gameB = rowSrc[x * 4 + 2]; }
                    }

                    rowDst[p + 0] = (uint8_t)(gameR > 255.0f ? 255.0f : (gameR < 0.0f ? 0.0f : gameR));
                    rowDst[p + 1] = (uint8_t)(gameG > 255.0f ? 255.0f : (gameG < 0.0f ? 0.0f : gameG));
                    rowDst[p + 2] = (uint8_t)(gameB > 255.0f ? 255.0f : (gameB < 0.0f ? 0.0f : gameB));
                    rowDst[p + 3] = 255;
                }
            }

            std::string path = GetScreenshotPath(format == ScreenshotFormat::PNG ? ".png" : (format == ScreenshotFormat::JPG ? ".jpg" : ".bmp"));
            int success = 0;

            if (format == ScreenshotFormat::PNG) success = stbi_write_png(path.c_str(), targetWidth, targetHeight, 4, pixelData.data(), targetWidth * 4);
            else if (format == ScreenshotFormat::JPG) success = stbi_write_jpg(path.c_str(), targetWidth, targetHeight, 4, pixelData.data(), 90);
            else success = stbi_write_bmp(path.c_str(), targetWidth, targetHeight, 4, pixelData.data());

            if (success) { CopyFileToClipboard(path); logger::info("[CAPTURE] SUCESSO: {}", path); }
            else { logger::error("[CAPTURE] ERRO: Falha ao salvar a imagem."); }

        }).detach();
}

int64_t Hooked_RenderUI(int64_t gMenuManager) {
    if (g_captureNextFrameWithoutUI) {
        if (g_captureDelayFrames > 0) {
            g_captureDelayFrames--;
            return OriginalRenderUI(gMenuManager);
        }

        auto renderer = RE::BSGraphics::Renderer::GetSingleton();
        IDXGISwapChain* swapChain = renderer ? reinterpret_cast<IDXGISwapChain*>(renderer->GetRuntimeData().renderWindows[0].swapChain) : nullptr;

        // Verifica se o HDR do Community Shaders está ativo
        bool isHDRSpace = IsHDRActive(swapChain);

        if (isHDRSpace) {
            // ==========================================
            // MODO HDR (Community Shaders Ativo)
            // ==========================================
            // 1. Deixamos a UI renderizar. Isto força o CS a limpar a uiTexture antiga.
            int64_t result = OriginalRenderUI(gMenuManager);

            // 2. O RTV atual do jogo agora aponta para a uiTexture do CS.
            // A cena 3D está guardada em segurança. Limpamos a UI com preto transparente!
            if (renderer) {
                auto rtv = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kFRAMEBUFFER].RTV;
                if (rtv) {
                    Microsoft::WRL::ComPtr<ID3D11Device> device;
                    rtv->GetDevice(&device);
                    if (device) {
                        Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
                        device->GetImmediateContext(&context);
                        float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                        context->ClearRenderTargetView(rtv, clearColor);
                    }
                }
            }

            logger::info("[CAPTURE] HDR Ativo: UI apagada da VRAM. Delegando para Hooked_Present...");

            // 3. O SwapChain final terá a cena HDR pura, já codificada. Capturamos no Present!
            g_captureNextFrameWithUI = true;
            g_captureNextFrameWithoutUI = false;

            return result;
        }
        else {
            // ==========================================
            // MODO VANILLA (SDR)
            // ==========================================
            // A cena 3D pura está no kFRAMEBUFFER agora. Temos de capturar ANTES de renderizar a UI.
            if (renderer) {
                ID3D11Texture2D* sceneTex = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kFRAMEBUFFER].texture;
                if (sceneTex) CaptureFrameFromSwapChain(sceneTex, g_pendingFormat, true, "Hooked_RenderUI (SDR Vanilla)");
            }

            g_captureNextFrameWithoutUI = false;

            // Depois de capturarmos, deixamos a UI ser renderizada normalmente para o ecrã
            return OriginalRenderUI(gMenuManager);
        }
    }

    return OriginalRenderUI(gMenuManager);
}

HRESULT WINAPI Hooked_Present(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
    if (g_captureNextFrameWithUI) {
        if (g_captureDelayFrames > 0) {
            g_captureDelayFrames--;
        }
        else {
            Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
            bool captured = false;

            // Checa FrameGen (DX12) - Abordagem importada do Screenshot.h
            Microsoft::WRL::ComPtr<ID3D12Device> device12;
            if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D12Device), (void**)device12.GetAddressOf()))) {
                if (SUCCEEDED(pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)backBuffer.GetAddressOf())) && backBuffer) {
                    captured = true;
                }
            }

            // Fallback Vanilla (DX11)
            if (!captured) {
                if (SUCCEEDED(pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)backBuffer.GetAddressOf())) && backBuffer) {
                    captured = true;
                }
            }

            if (captured && backBuffer) {
                logger::info("[HOOK] Disparando captura COM UI diretamente do SwapChain Final");
                CaptureFrameFromSwapChain(backBuffer.Get(), g_pendingFormat, false, "Hooked_Present");
            }
            else {
                logger::error("[HOOK] Falha ao obter o backbuffer no Hooked_Present");
            }

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
        DWORD oldProtect;
        VirtualProtect(&vtable[8], sizeof(void*), PAGE_READWRITE, &oldProtect);
        vtable[8] = Hooked_Present;
        VirtualProtect(&vtable[8], sizeof(void*), oldProtect, &oldProtect);

        // --- HOOK NO IDXGISwapChain3::SetColorSpace1 (Índice 38 na Tabela Virtual) ---
        // Permite interceptar as mudanças em tempo real feitas pelo Community Shaders
        Microsoft::WRL::ComPtr<IDXGISwapChain3> swapChain3;
        if (SUCCEEDED(swapChain->QueryInterface(IID_PPV_ARGS(&swapChain3)))) {
            void** vtable3 = *(void***)swapChain3.Get();
            OriginalSetColorSpace1 = (SetColorSpace1_t)vtable3[38];
            VirtualProtect(&vtable3[38], sizeof(void*), PAGE_READWRITE, &oldProtect);
            vtable3[38] = Hooked_SetColorSpace1;
            VirtualProtect(&vtable3[38], sizeof(void*), oldProtect, &oldProtect);
            g_colorSpaceHooked = true;
            logger::info("[DXGI] Hook em SetColorSpace1 (VTable 38) instalado com sucesso.");
        }
    }
    auto& trampoline = SKSE::GetTrampoline();
    OriginalRenderUI = reinterpret_cast<RenderUI_t>(trampoline.write_call<5>(
        REL::RelocationID(35556, 36555).address() + REL::Relocate(0x3ab, 0x371), (uintptr_t)Hooked_RenderUI));
}

// Variáveis para rastrear o estado das teclas e evitar que 1 clique dispare 10 fotos
static bool g_wasCapUIPressedAsync = false;
static bool g_wasCapNoUIPressedAsync = false;
static bool g_wasOpenPressedAsync = false;

void PrimeAsyncInputs() {
    g_wasCapUIPressedAsync = true;
    g_wasCapNoUIPressedAsync = true;
    g_wasOpenPressedAsync = true;
}

void PollAsyncInputs() {
    // --- 1. TECLADO/MOUSE ASYNC (Ignora o roubo de foco do Windows) ---
    if (!Prisma::IsHidden()) {
        bool capUI_pressed = (Settings::captureWithUIKey_k != 0 && IsInputDown(Settings::captureWithUIKey_k)) ||
            (Settings::captureWithUIKey_m != 0 && IsInputDown(Settings::captureWithUIKey_m));

        bool capNoUI_pressed = (Settings::captureNoUIKey_k != 0 && IsInputDown(Settings::captureNoUIKey_k)) ||
            (Settings::captureNoUIKey_m != 0 && IsInputDown(Settings::captureNoUIKey_m));

        bool open_pressed = (Settings::openModeKey_k != 0 && IsInputDown(Settings::openModeKey_k)) ||
            (Settings::openModeKey_m != 0 && IsInputDown(Settings::openModeKey_m));

        // HIERARQUIA: 1º Com UI, 2º Sem UI, 3º Apenas Fechar
        if (capUI_pressed && !g_wasCapUIPressedAsync) {
            if (Settings::captureWithUICombo_k == 0 || IsInputDown(Settings::captureWithUICombo_k)) {
                Prisma::Hide(); ApplyPendingCropAndTrigger(true);
            }
        }
        else if (capNoUI_pressed && !g_wasCapNoUIPressedAsync) {
            if (Settings::captureNoUICombo_k == 0 || IsInputDown(Settings::captureNoUICombo_k)) {
                Prisma::Hide(); ApplyPendingCropAndTrigger(false);
            }
        }
        else if (open_pressed && !g_wasOpenPressedAsync) {
            if (Settings::openModeCombo_k == 0 || IsInputDown(Settings::openModeCombo_k)) {
                Prisma::Hide();
            }
        }

        g_wasCapUIPressedAsync = capUI_pressed;
        g_wasCapNoUIPressedAsync = capNoUI_pressed;
        g_wasOpenPressedAsync = open_pressed;
    }

    // --- 2. GAMEPAD ---
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

    bool triggerCapUI = false;
    if (IsPressed(Settings::captureWithUIKey_g)) { if (IsHeld(Settings::captureWithUICombo_g)) triggerCapUI = true; }
    else if (IsPressed(Settings::captureWithUICombo_g)) { if (IsHeld(Settings::captureWithUIKey_g)) triggerCapUI = true; }

    bool triggerCapNoUI = false;
    if (IsPressed(Settings::captureNoUIKey_g)) { if (IsHeld(Settings::captureNoUICombo_g)) triggerCapNoUI = true; }
    else if (IsPressed(Settings::captureNoUICombo_g)) { if (IsHeld(Settings::captureNoUIKey_g)) triggerCapNoUI = true; }

    if (!Prisma::IsHidden()) {
        if (triggerCapUI) { Prisma::Hide(); ApplyPendingCropAndTrigger(true); }
        else if (triggerCapNoUI) { Prisma::Hide(); ApplyPendingCropAndTrigger(false); }
        else if (triggerOpen) { Prisma::Hide(); }
    }
    else {
        if (triggerOpen) Prisma::Show();
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

        bool isCapUI_Trigger = (isKeyboard && Settings::captureWithUIKey_k != 0 && pressedKey == Settings::captureWithUIKey_k) ||
            (!isKeyboard && Settings::captureWithUIKey_m != 0 && pressedKey == Settings::captureWithUIKey_m);

        bool isCapNoUI_Trigger = (isKeyboard && Settings::captureNoUIKey_k != 0 && pressedKey == Settings::captureNoUIKey_k) ||
            (!isKeyboard && Settings::captureNoUIKey_m != 0 && pressedKey == Settings::captureNoUIKey_m);

        if (!Prisma::IsHidden()) {
            // HIERARQUIA: Captura sobrepõe o botão de Fechar (se forem iguais)
            if (isCapUI_Trigger && ((Settings::captureWithUICombo_k == 0) || IsInputDown(Settings::captureWithUICombo_k))) {
                Prisma::Hide(); ApplyPendingCropAndTrigger(true); blockInput = true;
            }
            else if (isCapNoUI_Trigger && ((Settings::captureNoUICombo_k == 0) || IsInputDown(Settings::captureNoUICombo_k))) {
                Prisma::Hide(); ApplyPendingCropAndTrigger(false); blockInput = true;
            }
            else if (isOpen_Trigger && ((Settings::openModeCombo_k == 0) || IsInputDown(Settings::openModeCombo_k))) {
                Prisma::Hide(); blockInput = true;
            }
        }
        else {
            // Se o menu está fechado, o botão de abrir funciona normalmente
            if (isOpen_Trigger && ((Settings::openModeCombo_k == 0) || IsInputDown(Settings::openModeCombo_k))) {
                Prisma::Show(); blockInput = true;
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
			logger::info("Setting up input hook...");
            INITCOMMONCONTROLSEX icex; icex.dwSize = sizeof(INITCOMMONCONTROLSEX); icex.dwICC = ICC_WIN95_CLASSES;
            InitCommonControlsEx(&icex);
            auto renderer = RE::BSGraphics::Renderer::GetSingleton();
            if (renderer) g_hWindow = (HWND)renderer->GetRuntimeData().renderWindows[0].hWnd;
			logger::info("Initial window handle: {}", fmt::ptr(g_hWindow));
            if (!g_hWindow) g_hWindow = FindWindowA(nullptr, "Skyrim Special Edition");
            if (g_hWindow) {
				logger::info("Found window handle: {}", fmt::ptr(g_hWindow));
                DWORD windowPid = 0; GetWindowThreadProcessId(g_hWindow, &windowPid);
                if (windowPid != GetCurrentProcessId()) return;
				logger::info("Window belongs to current process. Installing subclass...");
                if (SetWindowSubclass(g_hWindow, MySubclassProc, SCREENSHOT_SUBCLASS_ID, 0)) {
					logger::info("Subclass installed successfully. Starting async input polling...");
                    SetTimer(g_hWindow, 0x5353, 16, nullptr);
                    logger::info("Async input polling started.");
                    UnmapNativeScreenshot(); 
					logger::info("Unmap called successfully.");
                    InstallHooks();
					logger::info("Input hook installed successfully.");
                }
            }
            });
    }
}