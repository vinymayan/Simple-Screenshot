#include "Prisma.h"
#include "PrismaUI_API.h"
#include "Events.h"

PRISMA_UI_API::IVPrismaUI1* PrismaUI = nullptr;
static PrismaView view;
static bool g_isGamePaused = false;

void SetGamePause(bool pause) {
    if (g_isGamePaused == pause) return;
    g_isGamePaused = pause;

    auto ui = RE::UI::GetSingleton();
    if (ui) {
        if (pause) {
            ui->numPausesGame++; // Congela o mundo, física, NPCs, etc.
        }
        else {
            if (ui->numPausesGame > 0) {
                ui->numPausesGame--; // Descongela e volta ao normal
            }
        }
    }
}

void Prisma::Install() {
    PrismaUI = reinterpret_cast<PRISMA_UI_API::IVPrismaUI1*>(PRISMA_UI_API::RequestPluginAPI());
}

void Prisma::Show() {

    if (!createdView) {
        createdView = true;

        #ifdef DEV_SERVER
        constexpr const char* path = "http://localhost:5175";
        #else
        constexpr const char* path = PRODUCT_NAME "/index.html";
        #endif


        view = PrismaUI->CreateView(path, [](PrismaView view) -> void { 
            PrismaUI->Focus(view, true);
            SetGamePause(true);
        });

        PrismaUI->RegisterJSListener(view, "takeScreenshot", [](const char* data) -> void {
            std::string str(data);
            int comUI = 0, x = 0, y = 0, w = 0, h = 0;
            std::vector<std::pair<int, int>> points;

            try {
                size_t pos1 = str.find('|');
                size_t pos2 = str.find('|', pos1 + 1);
                size_t pos3 = str.find('|', pos2 + 1);
                size_t pos4 = str.find('|', pos3 + 1);
                size_t pos5 = str.find('|', pos4 + 1);

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
                                int px = std::stoi(pairStr.substr(0, comma));
                                int py = std::stoi(pairStr.substr(comma + 1));
                                points.push_back({ px, py });
                            }
                            pStart = pEnd + 1;
                        }
                    }
                }
                // Agora enviamos a array de pontos do laço junto!
                TriggerRegionScreenshot(comUI != 0, x, y, w, h, points);
            }
            catch (...) {
                // Caso a formatação falhe, ignora
            }
            });

        PrismaUI->RegisterJSListener(view, "hideWindow", [](const char* data) -> void {
            PrismaUI->Hide(view);
            SetGamePause(false);
            });

        return;
    }
    
    PrismaUI->Show(view);
    PrismaUI->Focus(view, true);
    SetGamePause(true);
}

void Prisma::Hide() {
    PrismaUI->Unfocus(view);
    PrismaUI->Hide(view); 
    SetGamePause(false);
}

bool Prisma::IsHidden() { return PrismaUI->IsHidden(view); }
