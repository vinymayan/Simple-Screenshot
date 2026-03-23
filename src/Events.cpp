#include "Events.h"
#include "logger.h"
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <vector>
#include <ctime>
#include <string>
#include <filesystem> // Necessário para paths robustos
#include "MinHook.h"



#include <Xinput.h>
#pragma comment(lib, "Xinput9_1_0.lib")
// Define a implementação da biblioteca de imagem APENAS aqui no .cpp
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

#include <d3d12.h>
#pragma comment(lib, "d3d12.lib")
// Variáveis Globais

HWND g_hWindow = nullptr;
static bool g_uiHidden = false;
static XINPUT_STATE g_lastGamepadState;
static bool g_gamepadConnected = false;
static constexpr UINT_PTR SCREENSHOT_SUBCLASS_ID = 0x5353484F54;

void UnmapActionByName(const char* actionName) {
    auto controlMap = RE::ControlMap::GetSingleton();
    if (!controlMap) return;

    RE::BSFixedString eventName(actionName);

    // Itera sobre todos os contextos (Gameplay, Menu, etc.)
    for (uint32_t i = 0; i < RE::UserEvents::INPUT_CONTEXT_ID::kTotal; i++) {
        auto context = controlMap->controlMap[i];
        if (!context) continue;

        // CORREÇÃO CRÍTICA:
        // Em vez de usar "auto& deviceMap : context->deviceMappings", iteramos manualmente.
        // 0 = Keyboard, 1 = Mouse, 2 = Gamepad. 
        // Paramos em 3 para evitar ler memória de VR em Skyrim SE/AE.
        for (uint32_t deviceIdx = 0; deviceIdx < 3; deviceIdx++) {

            // Acesso seguro ao array de mapeamentos
            auto& deviceMap = context->deviceMappings[deviceIdx];

            // Itera sobre os mapeamentos dentro deste dispositivo
            for (auto& mapping : deviceMap) {
                if (mapping.eventID == eventName) {
                    // Se a tecla estiver definida, desmapeia
                    if (mapping.inputKey != 0xFF) {
                        SKSE::log::info("Acao '{}' desmapeada. Contexto: {}, Dispositivo: {}", actionName, i, deviceIdx);

                        mapping.inputKey = 0xFF;      // Define como kInvalid
                        mapping.modifier = 0xFF;      // Limpa modificadores (Shift/Ctrl)
                        mapping.remappable = false;   // (Opcional) Impede que o utilizador mude no menu
                    }
                }
            }
        }
    }
}

// Sua função principal de setup
void UnmapNativeScreenshot() {
    SKSE::log::info("Iniciando remocao de atalhos nativos...");

    // Vai direto ao ponto: remove "Screenshot" e "Multi-Screenshot"
    UnmapActionByName("Screenshot");
    UnmapActionByName("Multi-Screenshot");
}

void ToggleGameUI() {
    auto ui = RE::UI::GetSingleton();
    if (ui) {
        g_uiHidden = !g_uiHidden;
        // Se g_uiHidden for true, ShowMenus deve receber false
        ui->ShowMenus(!g_uiHidden);

        if (g_uiHidden) {
            SKSE::log::info("UI Escondida");
        }
        else {
            SKSE::log::info("UI Mostrada");
        }
    }
}

bool IsInputDown(uint32_t keyCode) {
    int vk = 0;

    // Mapeamento manual para teclas estendidas do DirectInput que o MapVirtualKey falha
    switch (keyCode) {
    case 256: vk = VK_LBUTTON; break;
    case 257: vk = VK_RBUTTON; break;
    case 258: vk = VK_MBUTTON; break;
    case 259: vk = VK_XBUTTON1; break;
    case 260: vk = VK_XBUTTON2; break;
    case 183: vk = VK_SNAPSHOT; break;
    case 199: vk = VK_HOME; break;
    case 200: vk = VK_UP; break;
    case 201: vk = VK_PRIOR; break; // PgUp
    case 203: vk = VK_LEFT; break;
    case 205: vk = VK_RIGHT; break;
    case 207: vk = VK_END; break;
    case 208: vk = VK_DOWN; break;
    case 209: vk = VK_NEXT; break; // PgDn
    case 210: vk = VK_INSERT; break;
    case 211: vk = VK_DELETE; break;
    case 156: vk = VK_RETURN; break; // Numpad Enter (às vezes tratado como Return)
    case 157: vk = VK_RCONTROL; break;
    case 184: vk = VK_RMENU; break; // RAlt
    default:
        // Para teclas padrão (< 256), usamos o mapa do Windows
        if (keyCode < 256) {
            vk = MapVirtualKey(keyCode, MAPVK_VSC_TO_VK);
        }
        break;
    }

    // Se encontramos um VK válido, checa o estado assíncrono (funciona mesmo se a janela não tiver foco do mouse)
    if (vk != 0) {
        return (GetAsyncKeyState(vk) & 0x8000) != 0;
    }
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





// Função auxiliar interna atualizada para usar std::filesystem
std::string GetScreenshotPath(const char* ext) {
    char filename[64];
    std::time_t now = std::time(nullptr);
    std::strftime(filename, sizeof(filename), "Screenshot_%Y%m%d_%H%M%S", std::localtime(&now));

    logger::debug("Generating screenshot path. Configured folder: '{}'", Settings::screenshotPath);

    std::filesystem::path basePath(Settings::screenshotPath);

    if (Settings::screenshotPath.empty()) {
        logger::debug("Screenshot path is empty, using current directory.");
        basePath = std::filesystem::current_path();
    }

    try {
        if (!std::filesystem::exists(basePath)) {
            logger::info("Directory '{}' does not exist. Attempting to create it.", basePath.string());
            if (std::filesystem::create_directories(basePath)) {
                logger::debug("Directory created successfully.");
            }
        }
    }
    catch (const std::exception& e) {
        logger::error("Failed to create directory '{}'. Error: {}. Falling back to root.", basePath.string(), e.what());
        basePath = std::filesystem::current_path();
    }

    std::filesystem::path fullPath = basePath / (std::string(filename) + ext);
    logger::debug("Final screenshot path: '{}'", fullPath.string());

    return fullPath.string();
}


// Globais para a Máquina de Estado do Screenshot
static bool g_captureNextFrame = false;
static bool g_restoreUINextFrame = false;
static int g_waitFramesForCapture = 0;
static ScreenshotFormat g_pendingFormat;

// Pointer para o Present original da SwapChain
typedef HRESULT(WINAPI* Present_t)(IDXGISwapChain*, UINT, UINT);
Present_t OriginalPresent = nullptr;

// -----------------------------------------------------------
// 1. NOVA FUNÇÃO DE CAPTURA (Usando o SwapChain final e real)
// -----------------------------------------------------------
// -----------------------------------------------------------
// 1. NOVA FUNÇÃO DE CAPTURA (Usando o SwapChain final + UI Blending)
// -----------------------------------------------------------
void CaptureFrameFromSwapChain(IDXGISwapChain* swapChain, ScreenshotFormat format) {
    logger::info("Iniciando captura no Present (Final Frame com UI Blending)...");

    if (!swapChain) return;

    Microsoft::WRL::ComPtr<ID3D11Device> device11;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context11;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer11;
    bool captured = false;

    // --- TENTATIVA 1: DirectX 12 (Compatibilidade Upscalers) ---
    Microsoft::WRL::ComPtr<ID3D12Device> device12;
    if (SUCCEEDED(swapChain->GetDevice(__uuidof(ID3D12Device), (void**)device12.GetAddressOf()))) {
        HRESULT hr = swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)backBuffer11.GetAddressOf());
        if (SUCCEEDED(hr) && backBuffer11) {
            backBuffer11->GetDevice(device11.GetAddressOf());
            if (device11) {
                device11->GetImmediateContext(context11.GetAddressOf());
                if (context11) captured = true;
            }
        }
    }

    // --- TENTATIVA 2: DirectX 11 Nativo ---
    if (!captured) {
        HRESULT hr = swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)backBuffer11.GetAddressOf());
        if (SUCCEEDED(hr) && backBuffer11) {
            backBuffer11->GetDevice(device11.GetAddressOf());
            if (device11) {
                device11->GetImmediateContext(context11.GetAddressOf());
                if (context11) captured = true;
            }
        }
    }

    if (!captured || !backBuffer11 || !context11 || !device11) {
        logger::error("Erro critico: Falha ao obter backbuffer do SwapChain.");
        return;
    }

    // --- 1. CAPTURAR A TEXTURA DA UI (kFRAMEBUFFER) ---
    auto renderer = RE::BSGraphics::Renderer::GetSingleton();
    Microsoft::WRL::ComPtr<ID3D11Resource> uiResource;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> uiTexture11;
    bool hasUI = false;

    // Só tentamos capturar a UI se ela não estiver escondida pelo jogador
    if (renderer && !g_uiHidden) {
        auto uiRTV = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGET::kFRAMEBUFFER].RTV;
        if (uiRTV) {
            uiRTV->GetResource(uiResource.GetAddressOf());
            if (SUCCEEDED(uiResource.As(&uiTexture11))) {
                hasUI = true;
            }
        }
    }

    // --- 2. CRIAR STAGING DO BACKBUFFER (JOGO) ---
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

    // --- 3. CRIAR STAGING DA UI ---
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
            if (FAILED(context11->Map(stagingUITex.Get(), 0, D3D11_MAP_READ, 0, &mappedUI))) {
                hasUI = false; // Se falhar, avançamos só com a imagem do jogo
            }
        }
        else {
            hasUI = false;
        }
    }

    // --- 4. RECORTAR E FAZER BLENDING NA CPU ---
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

        uint8_t* rowUI = nullptr;
        if (hasUI) {
            rowUI = (uint8_t*)mappedUI.pData + ((startY + y) * mappedUI.RowPitch) + (startX * 4);
        }

        for (int x = 0; x < targetWidth; ++x) {
            int p = x * 4;

            // 4.1. Ler o Pixel do Jogo
            float gameR, gameG, gameB;
            if (isBGRA) {
                gameR = rowSrc[p + 2]; // B -> R
                gameG = rowSrc[p + 1]; // G
                gameB = rowSrc[p + 0]; // R -> B
            }
            else {
                gameR = rowSrc[p + 0];
                gameG = rowSrc[p + 1];
                gameB = rowSrc[p + 2];
            }

            // 4.2. Composição com a UI (Alpha Blending)
            if (hasUI && rowUI) {
                // A UI do Skyrim/CS num kFRAMEBUFFER costuma ser RGBA
                float uiR = rowUI[p + 0];
                float uiG = rowUI[p + 1];
                float uiB = rowUI[p + 2];
                float uiA = rowUI[p + 3] / 255.0f; // Alpha premultiplicado para mistura

                rowDst[p + 0] = (uint8_t)((uiR * uiA) + (gameR * (1.0f - uiA)));
                rowDst[p + 1] = (uint8_t)((uiG * uiA) + (gameG * (1.0f - uiA)));
                rowDst[p + 2] = (uint8_t)((uiB * uiA) + (gameB * (1.0f - uiA)));
                rowDst[p + 3] = 255; // Resultado final opaco
            }
            else {
                rowDst[p + 0] = (uint8_t)gameR;
                rowDst[p + 1] = (uint8_t)gameG;
                rowDst[p + 2] = (uint8_t)gameB;
                rowDst[p + 3] = 255;
            }
        }
    }

    // Libertar os buffers
    if (hasUI) context11->Unmap(stagingUITex.Get(), 0);
    context11->Unmap(stagingTex.Get(), 0);

    // --- 5. SALVAR NO DISCO ---
    std::string path = GetScreenshotPath(format == ScreenshotFormat::PNG ? ".png" : (format == ScreenshotFormat::JPG ? ".jpg" : ".bmp"));
    int success = 0;

    if (format == ScreenshotFormat::PNG) success = stbi_write_png(path.c_str(), targetWidth, targetHeight, 4, pixelData.data(), targetWidth * 4);
    else if (format == ScreenshotFormat::JPG) success = stbi_write_jpg(path.c_str(), targetWidth, targetHeight, 4, pixelData.data(), 90);
    else success = stbi_write_bmp(path.c_str(), targetWidth, targetHeight, 4, pixelData.data());

    if (success) logger::info("Screenshot salvo com sucesso: {}", path);
    else logger::error("Falha ao salvar o screenshot no disco: {}", path);
}

// -----------------------------------------------------------
// 2. O HOOK DO PRESENT (Aqui garantimos o ReShade e afins)
// -----------------------------------------------------------
HRESULT WINAPI Hooked_Present(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
    if (g_captureNextFrame) {
        // Captura o frame exato que vai para o monitor (incluindo ReShade/Community Shaders)
        CaptureFrameFromSwapChain(pSwapChain, g_pendingFormat);
        g_captureNextFrame = false;
    }
    return OriginalPresent(pSwapChain, SyncInterval, Flags);
}

void TriggerScreenshotRequest(ScreenshotFormat format) {
    // Apenas diz ao hook para capturar o próximo frame que passar. 
    // Não mexe na UI, pois isso é responsabilidade do jogador.
    g_pendingFormat = format;
    g_captureNextFrame = true;
}

void InstallPresentHook() {
    auto renderer = RE::BSGraphics::Renderer::GetSingleton();
    if (!renderer) return;

    auto swapChain = reinterpret_cast<IDXGISwapChain*>(renderer->GetRuntimeData().renderWindows[0].swapChain);
    if (!swapChain) return;

    void** vtable = *(void***)swapChain;
    OriginalPresent = (Present_t)vtable[8]; // Índice 8 = Present

    DWORD oldProtect;
    VirtualProtect(&vtable[8], sizeof(void*), PAGE_READWRITE, &oldProtect);
    vtable[8] = Hooked_Present;
    VirtualProtect(&vtable[8], sizeof(void*), oldProtect, &oldProtect);

    logger::info("Hook de IDXGISwapChain::Present instalado com sucesso.");
}

void CaptureFrame(ScreenshotFormat format) {
    logger::info("Iniciando processo de captura de frame (Multi-API)...");

    auto renderer = RE::BSGraphics::Renderer::GetSingleton();
    if (!renderer) {
        logger::error("Falha: Renderer singleton nulo.");
        return;
    }

    // 1. Tentar obter a RenderWindow atual como fonte primária
    auto renderWindow = renderer->GetCurrentRenderWindow();
    if (!renderWindow) {
        logger::warn("GetCurrentRenderWindow() nulo. Tentando fallback para index 0.");
        renderWindow = &renderer->GetRuntimeData().renderWindows[0];
    }

    if (!renderWindow || !renderWindow->swapChain) {
        logger::error("Erro: Nenhuma RenderWindow ou SwapChain valida encontrada.");
        return;
    }

    // Cast seguro de REX::W32 para tipos nativos do Windows
    auto* swapChain = reinterpret_cast<IDXGISwapChain*>(renderWindow->swapChain);

    Microsoft::WRL::ComPtr<ID3D11Device> device11;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context11;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer11;

    bool captured = false;
    D3D11_TEXTURE2D_DESC desc{};

    // --- TENTATIVA 1: DirectX 12 (Prioridade) ---
    Microsoft::WRL::ComPtr<ID3D12Device> device12;
    if (SUCCEEDED(swapChain->GetDevice(__uuidof(ID3D12Device), (void**)device12.GetAddressOf()))) {
        logger::info("Ambiente DX12 detectado. Tentando captura via DX12 Interop...");

        // Em mods de interop (como DLSS), o GetBuffer(ID3D11Texture2D) no swapchain DX12 
        // costuma funcionar se o driver permitir o wrapping.
        HRESULT hr = swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)backBuffer11.GetAddressOf());
        if (SUCCEEDED(hr) && backBuffer11) {
            backBuffer11->GetDevice(device11.GetAddressOf());
            if (device11) {
                device11->GetImmediateContext(context11.GetAddressOf());
                if (context11) {
                    logger::info("Captura DX12 (via interop DX11) iniciada.");
                    captured = true;
                }
            }
        }

        if (!captured) {
            logger::warn("SwapChain DX12 detectado, mas nao aceitou interop DX11. Pulando para fallbacks.");
        }
    }

    // --- TENTATIVA 2: DirectX 11 (Caso DX12 falhe ou nao exista) ---
    if (!captured) {
        HRESULT hr = swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)backBuffer11.GetAddressOf());
        if (SUCCEEDED(hr) && backBuffer11) {
            backBuffer11->GetDevice(device11.GetAddressOf());
            if (device11) {
                device11->GetImmediateContext(context11.GetAddressOf());
                if (context11) {
                    logger::debug("Captura via DX11 SwapChain bem-sucedida.");
                    captured = true;
                }
            }
        }
    }

    // --- TENTATIVA 3: Fallback via Renderer Global (GetCurrentRenderWindow) ---
    if (!captured) {
        logger::warn("Tentando fallback via dispositivo global do motor...");
        device11 = reinterpret_cast<ID3D11Device*>(RE::BSGraphics::Renderer::GetDevice());
        context11 = reinterpret_cast<ID3D11DeviceContext*>(renderer->GetRuntimeData().context);

        if (device11 && context11) {
            HRESULT hr = swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)backBuffer11.GetAddressOf());
            if (SUCCEEDED(hr)) {
                logger::info("Buffer recuperado via contexto global.");
                captured = true;
            }
        }
    }

    // Validação final de recursos
    if (!captured || !backBuffer11 || !context11 || !device11) {
        logger::error("Erro critico: Falha total ao obter recursos de renderizacao.");
        return;
    }

    // --- PROCESSAMENTO DA IMAGEM ---
    backBuffer11->GetDesc(&desc);

    D3D11_TEXTURE2D_DESC stagingDesc = desc;
    stagingDesc.BindFlags = 0;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.MiscFlags = 0;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> stagingTex;
    if (FAILED(device11->CreateTexture2D(&stagingDesc, nullptr, stagingTex.GetAddressOf()))) {
        logger::error("Falha ao criar staging texture.");
        return;
    }

    context11->CopyResource(stagingTex.Get(), backBuffer11.Get());

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(context11->Map(stagingTex.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        logger::error("Falha ao mapear staging texture.");
        return;
    }

    // Lógica de Recorte (Centralizado)
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

        if (isBGRA) {
            for (int x = 0; x < targetWidth; ++x) {
                int p = x * 4;
                rowDst[p + 0] = rowSrc[p + 2]; // B -> R
                rowDst[p + 1] = rowSrc[p + 1]; // G
                rowDst[p + 2] = rowSrc[p + 0]; // R -> B
                rowDst[p + 3] = rowSrc[p + 3]; // A
            }
        }
        else {
            memcpy(rowDst, rowSrc, targetWidth * 4);
        }
    }

    context11->Unmap(stagingTex.Get(), 0);

    // Salvar arquivo usando o path gerado
    std::string path = GetScreenshotPath(format == ScreenshotFormat::PNG ? ".png" : (format == ScreenshotFormat::JPG ? ".jpg" : ".bmp"));
    int success = 0;

    if (format == ScreenshotFormat::PNG) success = stbi_write_png(path.c_str(), targetWidth, targetHeight, 4, pixelData.data(), targetWidth * 4);
    else if (format == ScreenshotFormat::JPG) success = stbi_write_jpg(path.c_str(), targetWidth, targetHeight, 4, pixelData.data(), 90);
    else success = stbi_write_bmp(path.c_str(), targetWidth, targetHeight, 4, pixelData.data());

    if (success) {
        logger::info("Screenshot salvo com sucesso: {}", path);
    }
    else {
        logger::error("Erro ao gravar arquivo no disco.");
    }
}

void PollGamepad() {
    XINPUT_STATE currentState;
    ZeroMemory(&currentState, sizeof(XINPUT_STATE));

    // Pega o estado do controle 0
    if (XInputGetState(0, &currentState) != ERROR_SUCCESS) {
        g_gamepadConnected = false;
        return;
    }

    // Se conectou agora, só sincroniza
    if (!g_gamepadConnected) {
        g_gamepadConnected = true;
        g_lastGamepadState = currentState;
        return;
    }

    // Só processa se o pacote mudou (otimização do XInput)
    if (currentState.dwPacketNumber == g_lastGamepadState.dwPacketNumber) {
        return;
    }

    auto& pad = currentState.Gamepad;
    auto& oldPad = g_lastGamepadState.Gamepad;

    // Função Lambda para verificar "Pressed" (Estava solto, agora apertou)
    auto IsPressed = [&](uint32_t key) -> bool {
        if (key == 0) return false;
        bool now = CheckGamepadButton(pad, key);
        bool before = CheckGamepadButton(oldPad, key);
        return now && !before;
        };

    // Função Lambda para verificar "Held" (Está segurado)
    auto IsHeld = [&](uint32_t key) -> bool {
        if (key == 0) return true; // Se não tem combo, conta como segurado
        return CheckGamepadButton(pad, key);
        };

    // --- LÓGICA DE SCREENSHOT (GAMEPAD) ---
    bool triggerSS = false;

    // Caso 1: Apertou Gatilho (e Combo está segurado)
    if (IsPressed(Settings::screenshotKey_g)) {
        if (IsHeld(Settings::comboKey_g)) triggerSS = true;
    }
    // Caso 2: Apertou Combo (e Gatilho está segurado)
    else if (IsPressed(Settings::comboKey_g)) {
        if (IsHeld(Settings::screenshotKey_g)) triggerSS = true;
    }

    if (triggerSS) TriggerScreenshotRequest(Settings::imageFormat);

    // --- LÓGICA UI (GAMEPAD) ---
    bool triggerUI = false;

    // Caso 1: Apertou Gatilho UI
    if (IsPressed(Settings::toggleUIKey_g)) {
        if (IsHeld(Settings::toggleUIComboKey_g)) triggerUI = true;
    }
    // Caso 2: Apertou Combo UI
    else if (IsPressed(Settings::toggleUIComboKey_g)) {
        if (IsHeld(Settings::toggleUIKey_g)) triggerUI = true;
    }

    if (triggerUI) ToggleGameUI();

    // Atualiza estado anterior
    g_lastGamepadState = currentState;
}

LRESULT CALLBACK MySubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    // 1. TIMER: Poll do Gamepad (Executa a cada ~16ms)
    if (msg == WM_TIMER && wParam == 0x5353) {
        PollGamepad();
    }

    uint32_t pressedKey = 0;
    bool isKeyboard = false;

    // 2. IDENTIFICAÇÃO DA ENTRADA (Teclado ou Mouse)
    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) {
        pressedKey = MapVirtualKey(static_cast<UINT>(wParam), MAPVK_VK_TO_VSC);

        // Verifica o bit 24 do lParam para teclas estendidas (ex: Numpad Enter, RAlt)
        if ((lParam >> 24) & 1) {
            pressedKey += 128;
        }
        
        isKeyboard = true;
    }
    else if (msg == WM_KEYDOWN || msg == WM_KEYUP) {
        if (wParam == VK_SNAPSHOT) {
            pressedKey = 183;
        }
        isKeyboard = true;
    }
    // Mouse
    else if (msg == WM_LBUTTONDOWN) { pressedKey = 256; }
    else if (msg == WM_RBUTTONDOWN) { pressedKey = 257; }
    else if (msg == WM_MBUTTONDOWN) { pressedKey = 258; }
    else if (msg == WM_XBUTTONDOWN) {
        pressedKey = (GET_XBUTTON_WPARAM(wParam) == XBUTTON1) ? 259 : 260;
    }
    else {
        return DefSubclassProc(hwnd, msg, wParam, lParam);
    }

    // 3. LÓGICA DE DISPARO (Trigger)
    if (pressedKey != 0) {
		logger::debug("Tecla pressionada detectada: KeyID={}, Tipo={}", pressedKey, isKeyboard ? "Teclado" : "Mouse");
        bool blockInput = false;

        // --- LÓGICA DE SCREENSHOT ---
        bool isSS_Trigger = (isKeyboard && Settings::screenshotKey_k != 0 && pressedKey == Settings::screenshotKey_k) ||
            (!isKeyboard && Settings::screenshotKey_m != 0 && pressedKey == Settings::screenshotKey_m);

        if (isSS_Trigger) {
            // Verifica se a tecla de combo (ex: Shift/Ctrl) está pressionada
            bool comboHeld = (Settings::comboKey_k == 0) || IsInputDown(Settings::comboKey_k);
            if (comboHeld) {
                TriggerScreenshotRequest(Settings::imageFormat);
                RE::SendHUDMessage::ShowHUDMessage("Screenshot captured!");
                blockInput = true;
            }
        }

        // --- LÓGICA DE UI (Esconder Menus) ---
        bool isUI_Trigger = (isKeyboard && Settings::toggleUIKey_k != 0 && pressedKey == Settings::toggleUIKey_k) ||
            (!isKeyboard && Settings::toggleUIKey_m != 0 && pressedKey == Settings::toggleUIKey_m);

        if (isUI_Trigger) {
            bool comboHeld = (Settings::toggleUIComboKey_k == 0) || IsInputDown(Settings::toggleUIComboKey_k);
            if (comboHeld) {
                ToggleGameUI();
                blockInput = true;
            }
        }

        // Se a tecla foi usada pelo mod, retornamos 0 para o Windows/Skyrim não processar (bloqueia o input)
        if (blockInput) {
            logger::debug("Input bloqueado pelo Mod: KeyID={}", pressedKey);
            return 0;
        }
    }

    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

void SetupInputHook() {

	logger::debug("Configurando Hook de WndProc...");

    auto taskInterface = SKSE::GetTaskInterface();
    if (taskInterface) {
        // Agenda a execução para o próximo frame disponível na thread principal
        taskInterface->AddTask([]() {
            INITCOMMONCONTROLSEX icex;
            icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
            icex.dwICC = ICC_WIN95_CLASSES; // Ou outros flags dependendo da necessidade
            InitCommonControlsEx(&icex);

            auto renderer = RE::BSGraphics::Renderer::GetSingleton();
            if (renderer) g_hWindow = (HWND)renderer->GetRuntimeData().renderWindows[0].hWnd;
			logger::debug("HWND obtido do Renderer: {}", (void*)g_hWindow);
            if (!g_hWindow) 
			{
				logger::debug("Tentando FindWindowA para Skyrim Special Edition...");
                g_hWindow = FindWindowA(nullptr, "Skyrim Special Edition"); }

            if (g_hWindow) {
                DWORD windowPid = 0;
                GetWindowThreadProcessId(g_hWindow, &windowPid);
                DWORD currentPid = GetCurrentProcessId();

                if (windowPid != currentPid) {
                    SKSE::log::error("ERRO: HWND encontrado pertence ao PID {}, mas o plugin está no PID {}. Janela incorreta!", windowPid, currentPid);
                    return;
                }

                char windowTitle[256];
                GetWindowTextA(g_hWindow, windowTitle, sizeof(windowTitle));
                char className[256];
                GetClassNameA(g_hWindow, className, sizeof(className));

                SKSE::log::debug("Instalando Subclass na janela: '{}' | Classe: '{}' | HWND: {:p}", windowTitle, className, (void*)g_hWindow);
                // Substituição do SetWindowLongPtr pelo SetWindowSubclass
                if (SetWindowSubclass(g_hWindow, MySubclassProc, SCREENSHOT_SUBCLASS_ID, 0)) {
                    SetTimer(g_hWindow, 0x5353, 16, nullptr);
                    UnmapNativeScreenshot();

                    // AQUI: Instala o Hook do DirectX
                    InstallPresentHook();

                    SKSE::log::info("WndProc Subclass instalado com sucesso.");
                }
                else {
                    DWORD error = GetLastError();
                    SKSE::log::error("Falha ao instalar SetWindowSubclass. Erro Win32: {}", error);
                }
            }});
    }
}