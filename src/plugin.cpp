#include "logger.h"
#include "Events.h"
#include "Prisma.h"
#include "Hooks.h"

void OnMessage(SKSE::MessagingInterface::Message* message) {
    if (message->type == SKSE::MessagingInterface::kDataLoaded) {
        SetupInputHook();
        ScreenshotMenu::LoadSettings(); // Carrega config salva
        ScreenshotMenu::Register();     // Registra o menu
        Hooks::Install();
        Prisma::Install();
    }
    if (message->type == SKSE::MessagingInterface::kNewGame || message->type == SKSE::MessagingInterface::kPostLoadGame) {
        // Post-load
    }
}

SKSEPluginLoad(const SKSE::LoadInterface *skse) {

    SetupLog();
    logger::info("Plugin loaded");
    SKSE::Init(skse);
    SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
    
    return true;
}
