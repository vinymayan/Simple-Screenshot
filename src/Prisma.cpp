#include "Prisma.h"
#include "PrismaUI_API.h"
#include "Events.h"

// Acesso à função do Events.cpp para salvar as coordendas fantasmas
extern void UpdatePendingCrop(int x, int y, int w, int h, const std::vector<std::pair<int, int>>& points);
extern void PrimeAsyncInputs();
extern std::atomic<bool> g_isCapturing;


std::string EscapeJSString(const std::string& str) {
    std::string result;
    for (char c : str) {
        if (c == '\\') result += "\\\\";
        else if (c == '"') result += "\\\"";
        else result += c;
    }
    return result;
}

std::string FormatKeybind(uint32_t k, uint32_t m, uint32_t ck, uint32_t g, uint32_t cg) {
    std::string pc;
    uint32_t main_pc = k != 0 ? k : m;
    if (ck != 0 && g_dx_to_name_map.count(ck)) pc += std::string(g_dx_to_name_map.at(ck)) + " + ";
    if (main_pc != 0 && g_dx_to_name_map.count(main_pc)) pc += g_dx_to_name_map.at(main_pc);

    std::string pad;
    if (cg != 0 && g_gamepad_to_name_map.count(cg)) pad += std::string(g_gamepad_to_name_map.at(cg)) + " + ";
    if (g != 0 && g_gamepad_to_name_map.count(g)) pad += g_gamepad_to_name_map.at(g);

    std::string result;
    if (!pc.empty()) result += pc;
    if (!pc.empty() && !pad.empty()) result += " / ";
    if (!pad.empty()) result += pad;

    // RETORNA PROTEGIDO PARA O JS
    return EscapeJSString(result);
}

PRISMA_UI_API::IVPrismaUI1* PrismaUI = nullptr;
static PrismaView view;
static bool g_isGamePaused = false;


void Prisma::Install() {
    PrismaUI = reinterpret_cast<PRISMA_UI_API::IVPrismaUI1*>(PRISMA_UI_API::RequestPluginAPI());
}

// Helper para evitar código duplicado
void ParseCropData(const std::string& str, int& comUI, int& x, int& y, int& w, int& h, std::vector<std::pair<int, int>>& points) {
    try {
        size_t pos1 = str.find('|'); size_t pos2 = str.find('|', pos1 + 1);
        size_t pos3 = str.find('|', pos2 + 1); size_t pos4 = str.find('|', pos3 + 1); size_t pos5 = str.find('|', pos4 + 1);

        if (pos1 != std::string::npos && pos5 != std::string::npos) {
            comUI = std::stoi(str.substr(0, pos1));
            x = std::stoi(str.substr(pos1 + 1, pos2 - pos1 - 1));
            y = std::stoi(str.substr(pos2 + 1, pos3 - pos2 - 1));
            w = std::stoi(str.substr(pos3 + 1, pos4 - pos3 - 1));
            h = std::stoi(str.substr(pos4 + 1, pos5 - pos4 - 1));

            std::string ptsStr = str.substr(pos5 + 1);
            if (!ptsStr.empty()) {
                size_t pStart = 0;
                while (pStart < ptsStr.length()) {
                    size_t pEnd = ptsStr.find(';', pStart);
                    if (pEnd == std::string::npos) pEnd = ptsStr.length();
                    std::string pairStr = ptsStr.substr(pStart, pEnd - pStart);
                    size_t comma = pairStr.find(',');
                    if (comma != std::string::npos) {
                        points.push_back({ std::stoi(pairStr.substr(0, comma)), std::stoi(pairStr.substr(comma + 1)) });
                    }
                    pStart = pEnd + 1;
                }
            }
        }
    }
    catch (...) {}
}

void Prisma::Show() {
    if (g_isCapturing) return;
    if (!createdView) {
        createdView = true;
        std::string withUIKeys = FormatKeybind(Settings::captureWithUIKey_k, Settings::captureWithUIKey_m, Settings::captureWithUICombo_k, Settings::captureWithUIKey_g, Settings::captureWithUICombo_g);
        std::string noUIKeys = FormatKeybind(Settings::captureNoUIKey_k, Settings::captureNoUIKey_m, Settings::captureNoUICombo_k, Settings::captureNoUIKey_g, Settings::captureNoUICombo_g);

        std::string pathStr;
        #ifdef DEV_SERVER
                pathStr = "http://localhost:5175";
        #else
                pathStr = std::string(PRODUCT_NAME) + "/index.html";
        #endif

        pathStr += "#" + std::to_string(Settings::customWidth) + "|" + std::to_string(Settings::customHeight) + "|" + (Settings::useCustomResolution ? "1" : "0") + "|" + withUIKeys + "|" + noUIKeys + "|" + Settings::defaultRatio + "|" + std::to_string(Settings::defaultCustomRatioW) + "|" + std::to_string(Settings::defaultCustomRatioH);

        static std::string staticPath;
        staticPath = pathStr;

        view = PrismaUI->CreateView(staticPath.c_str(), [](PrismaView view) -> void {
            PrismaUI->Focus(view, true);
            });

        PrismaUI->RegisterJSListener(view, "takeScreenshot", [](const char* data) -> void {
            int comUI = 0, x = 0, y = 0, w = 0, h = 0;
            std::vector<std::pair<int, int>> points;
            ParseCropData(std::string(data), comUI, x, y, w, h, points);
            TriggerRegionScreenshot(comUI != 0, x, y, w, h, points);
            });


        PrismaUI->RegisterJSListener(view, "syncCropData", [](const char* data) -> void {
            int comUI = 0, x = 0, y = 0, w = 0, h = 0;
            std::vector<std::pair<int, int>> points;
            ParseCropData(std::string(data), comUI, x, y, w, h, points);
            UpdatePendingCrop(x, y, w, h, points);
            });

        PrismaUI->RegisterJSListener(view, "requestKeybinds", [](const char* data) -> void {
            UpdateKeybindsUI();
            });

        PrismaUI->RegisterJSListener(view, "hideWindow", [](const char* data) -> void {
            PrismaUI->Hide(view);
            });
        return;
    }
    PrismaUI->Show(view);
    PrismaUI->Focus(view, true);
    
}

void Prisma::Hide() {

    PrismaUI->Unfocus(view);
    PrismaUI->Hide(view);
}

bool Prisma::IsHidden() { return PrismaUI->IsHidden(view); }

void Prisma::UpdateKeybindsUI() {
    if (!createdView) return;

    std::string withUIKeys = FormatKeybind(Settings::captureWithUIKey_k, Settings::captureWithUIKey_m, Settings::captureWithUICombo_k, Settings::captureWithUIKey_g, Settings::captureWithUICombo_g);
    std::string noUIKeys = FormatKeybind(Settings::captureNoUIKey_k, Settings::captureNoUIKey_m, Settings::captureNoUICombo_k, Settings::captureNoUIKey_g, Settings::captureNoUICombo_g);

    std::string jsCommand = "setTimeout(function() { if (window.updateKeybinds) window.updateKeybinds(\"" + withUIKeys + "\", \"" + noUIKeys + "\"); }, 50);";

    PrismaUI->Invoke(view, jsCommand.c_str());
}