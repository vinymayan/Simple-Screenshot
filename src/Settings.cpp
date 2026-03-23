#include "Settings.h"
#include <Windows.h>
#include <filesystem>
#include <vector>
#include <string>
#include <algorithm>
#include "rapidjson/document.h"
#include "rapidjson/filereadstream.h"
#include "rapidjson/filewritestream.h"
#include "rapidjson/writer.h"
#include "SKSEMCP/SKSEMenuFramework.hpp"

namespace ScreenshotMenu {

    const char* SETTINGS_PATH = "Data/SKSE/Plugins/Simple Screenshot.json";

    // Variáveis do Navegador
    bool show_browser = false;
    bool showing_drives = false;
    std::filesystem::path current_browser_path;
    std::vector<std::filesystem::path> current_subdirs;
    std::vector<std::string> available_drives;

    void RefreshDrives() {
        available_drives.clear();
        DWORD mask = GetLogicalDrives();
        for (char letter = 'A'; letter <= 'Z'; ++letter) {
            if (mask & 1) {
                available_drives.push_back(std::string(1, letter) + ":\\");
            }
            mask >>= 1;
        }
    }

    void RefreshDirList() {
        current_subdirs.clear();
        try {
            for (const auto& entry : std::filesystem::directory_iterator(current_browser_path)) {
                if (entry.is_directory()) {
                    current_subdirs.push_back(entry.path());
                }
            }
            std::sort(current_subdirs.begin(), current_subdirs.end());
        }
        catch (...) {}
    }

    // Renderiza Keybind com suporte a Combo e Gamepad
    void RenderKeybind(const char* label, uint32_t* key_k, uint32_t* key_m, uint32_t* combo_k, uint32_t* key_g, uint32_t* combo_g) {
        bool changed = false;
        ImGuiMCP::PushID(label);

        ImGuiMCP::Text("%s", label);

        // --- PC (Teclado + Mouse) ---
        ImGuiMCP::Text("PC:");
        ImGuiMCP::SameLine();

        // 1. Combo Key (Agora aceita Mouse também)
        uint32_t current_pc_code = (*key_k != 0) ? *key_k : *key_m;
        const char* name_mk = "[Nenhuma]";
        if (g_dx_to_name_map.count(current_pc_code)) name_mk = g_dx_to_name_map.at(current_pc_code);

        ImGuiMCP::SetNextItemWidth(150);
        if (ImGuiMCP::BeginCombo("##mk", name_mk)) {
            for (auto& [code, name] : g_dx_to_name_map) {
                if (ImGuiMCP::Selectable(name, current_pc_code == code)) {
                    if (code < 256) { *key_k = code; *key_m = 0; }
                    else { *key_m = code; *key_k = 0; }
                    changed = true;
                }
            }
            ImGuiMCP::EndCombo();
        }

        ImGuiMCP::SameLine(); ImGuiMCP::Text("+"); ImGuiMCP::SameLine();

        // 2. Combo Key (AGORA É O SEGUNDO)
        const char* name_combo_k = "[Nenhuma]";
        if (g_dx_to_name_map.count(*combo_k)) name_combo_k = g_dx_to_name_map.at(*combo_k);
        ImGuiMCP::SetNextItemWidth(150);
        if (ImGuiMCP::BeginCombo("##ck", name_combo_k)) {
            for (auto& [code, name] : g_dx_to_name_map) {
                if (ImGuiMCP::Selectable(name, *combo_k == code)) { *combo_k = code; changed = true; }
            }
            ImGuiMCP::EndCombo();
        }

        // --- GAMEPAD ---
        if (key_g) {
            ImGuiMCP::Text("Gamepad:");
            ImGuiMCP::SameLine();

            // 1. Main Key (AGORA É O PRIMEIRO)
            const char* name_g = "[Nenhuma]";
            if (g_gamepad_to_name_map.count(*key_g)) name_g = g_gamepad_to_name_map.at(*key_g);
            ImGuiMCP::SetNextItemWidth(150);
            if (ImGuiMCP::BeginCombo("##mg", name_g)) {
                for (auto& [code, name] : g_gamepad_to_name_map) {
                    if (ImGuiMCP::Selectable(name, *key_g == code)) { *key_g = code; changed = true; }
                }
                ImGuiMCP::EndCombo();
            }

            ImGuiMCP::SameLine(); ImGuiMCP::Text("+"); ImGuiMCP::SameLine();

            // 2. Combo Key (AGORA É O SEGUNDO)
            const char* name_combo_g = "[Nenhuma]";
            if (g_gamepad_to_name_map.count(*combo_g)) name_combo_g = g_gamepad_to_name_map.at(*combo_g);
            ImGuiMCP::SetNextItemWidth(150);
            if (ImGuiMCP::BeginCombo("##cg", name_combo_g)) {
                for (auto& [code, name] : g_gamepad_to_name_map) {
                    if (ImGuiMCP::Selectable(name, *combo_g == code)) { *combo_g = code; changed = true; }
                }
                ImGuiMCP::EndCombo();
            }
        }
        ImGuiMCP::PopID();
        if (changed) SaveSettings();
    }

    void SaveSettings() {
        rapidjson::Document doc;
        doc.SetObject();
        auto& allocator = doc.GetAllocator();

        doc.AddMember("screenshotKey_k", Settings::screenshotKey_k, allocator);
        doc.AddMember("screenshotKey_m", Settings::screenshotKey_m, allocator);
        doc.AddMember("comboKey_k", Settings::comboKey_k, allocator);

        doc.AddMember("screenshotKey_g", Settings::screenshotKey_g, allocator);
        doc.AddMember("comboKey_g", Settings::comboKey_g, allocator);

        doc.AddMember("toggleUIKey_k", Settings::toggleUIKey_k, allocator);
        doc.AddMember("toggleUIKey_m", Settings::toggleUIKey_m, allocator); 
        doc.AddMember("toggleUIComboKey_k", Settings::toggleUIComboKey_k, allocator);
        doc.AddMember("toggleUIKey_g", Settings::toggleUIKey_g, allocator);
        doc.AddMember("toggleUIComboKey_g", Settings::toggleUIComboKey_g, allocator); 
        doc.AddMember("useCustomResolution", Settings::useCustomResolution, allocator);
        doc.AddMember("customWidth", Settings::customWidth, allocator);
        doc.AddMember("customHeight", Settings::customHeight, allocator);
        doc.AddMember("imageFormat", static_cast<int>(Settings::imageFormat), allocator);
        doc.AddMember("screenshotPath", rapidjson::StringRef(Settings::screenshotPath.c_str()), allocator);

        FILE* fp = nullptr;
        fopen_s(&fp, SETTINGS_PATH, "wb");
        if (fp) {
            char writeBuffer[65536];
            rapidjson::FileWriteStream os(fp, writeBuffer, sizeof(writeBuffer));
            rapidjson::Writer<rapidjson::FileWriteStream> writer(os);
            doc.Accept(writer);
            fclose(fp);
        }
    }

    void LoadSettings() {
		logger::debug("Loading settings from {}", SETTINGS_PATH);
        FILE* fp = nullptr;
        fopen_s(&fp, SETTINGS_PATH, "rb");
        if (fp) {
            char readBuffer[65536];
            rapidjson::FileReadStream is(fp, readBuffer, sizeof(readBuffer));
            rapidjson::Document doc;
            doc.ParseStream(is);
            fclose(fp);

            if (doc.IsObject()) {
                if (doc.HasMember("screenshotKey_k")) Settings::screenshotKey_k = doc["screenshotKey_k"].GetInt();
                if (doc.HasMember("screenshotKey_m")) Settings::screenshotKey_m = doc["screenshotKey_m"].GetInt();
                if (doc.HasMember("comboKey_k")) Settings::comboKey_k = doc["comboKey_k"].GetInt();

                if (doc.HasMember("screenshotKey_g")) Settings::screenshotKey_g = doc["screenshotKey_g"].GetInt();
                if (doc.HasMember("comboKey_g")) Settings::comboKey_g = doc["comboKey_g"].GetInt();

                if (doc.HasMember("toggleUIKey_k")) Settings::toggleUIKey_k = doc["toggleUIKey_k"].GetInt();
                if (doc.HasMember("toggleUIKey_m")) Settings::toggleUIKey_m = doc["toggleUIKey_m"].GetInt();
                if (doc.HasMember("toggleUIComboKey_k")) Settings::toggleUIComboKey_k = doc["toggleUIComboKey_k"].GetInt();
                if (doc.HasMember("toggleUIKey_g")) Settings::toggleUIKey_g = doc["toggleUIKey_g"].GetInt();
                if (doc.HasMember("toggleUIComboKey_g")) Settings::toggleUIComboKey_g = doc["toggleUIComboKey_g"].GetInt();
                if (doc.HasMember("useCustomResolution")) Settings::useCustomResolution = doc["useCustomResolution"].GetBool();
                if (doc.HasMember("customWidth")) Settings::customWidth = doc["customWidth"].GetInt();
                if (doc.HasMember("customHeight")) Settings::customHeight = doc["customHeight"].GetInt();
                if (doc.HasMember("imageFormat")) Settings::imageFormat = static_cast<ScreenshotFormat>(doc["imageFormat"].GetInt());
                if (doc.HasMember("screenshotPath")) Settings::screenshotPath = doc["screenshotPath"].GetString();
            }
        }
    }

    void __stdcall Render() {
        bool changed = false;
        ImGuiMCP::Text("Simple Screenshot Settings");
        ImGuiMCP::Separator();

        // 1. Tecla Screenshot
        RenderKeybind("Screenshot", &Settings::screenshotKey_k, &Settings::screenshotKey_m, &Settings::comboKey_k,
            &Settings::screenshotKey_g, &Settings::comboKey_g);

        ImGuiMCP::Spacing();

        RenderKeybind("Screenshot (No UI)", &Settings::toggleUIKey_k, &Settings::toggleUIKey_m, &Settings::toggleUIComboKey_k,
            &Settings::toggleUIKey_g, &Settings::toggleUIComboKey_g);

        ImGuiMCP::Separator();

        // 3. Formato
        const char* formats[] = { "BMP", "PNG", "JPG" };
        int current_fmt = static_cast<int>(Settings::imageFormat);
        if (ImGuiMCP::Combo("File format", &current_fmt, formats, 3)) {
            Settings::imageFormat = static_cast<ScreenshotFormat>(current_fmt);
            changed = true;
        }

        ImGuiMCP::Separator();
        ImGuiMCP::Text("Resolution Settings:");

        if (ImGuiMCP::Checkbox("Use Custom Resolution", &Settings::useCustomResolution)) {
            changed = true;
        }

        if (Settings::useCustomResolution) {
            ImGuiMCP::Indent();
            if (ImGuiMCP::InputInt("Width", &Settings::customWidth)) {
                if (Settings::customWidth < 1) Settings::customWidth = 1;
                changed = true;
            }
            if (ImGuiMCP::InputInt("Height", &Settings::customHeight)) {
                if (Settings::customHeight < 1) Settings::customHeight = 1;
                changed = true;
            }
            ImGuiMCP::TextColored(ImGuiMCP::ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Will be centered and clamped to window size.");
            ImGuiMCP::Unindent();
        }

        ImGuiMCP::Spacing();

        // 4. Navegador
        ImGuiMCP::Text("Save Folder:");
        static char pathBuffer[512];
        static bool initBuffer = true;
        if (initBuffer) { strcpy_s(pathBuffer, Settings::screenshotPath.c_str()); initBuffer = false; }

        if (ImGuiMCP::InputText("##PathInput", pathBuffer, sizeof(pathBuffer))) {
            Settings::screenshotPath = std::string(pathBuffer);
            changed = true;
        }
        ImGuiMCP::SameLine();
        if (ImGuiMCP::Button(show_browser ? "Close" : "Search")) {
            show_browser = !show_browser;
            if (show_browser) {
                std::error_code ec;
                current_browser_path = std::filesystem::absolute(std::filesystem::path(Settings::screenshotPath));
                if (!std::filesystem::exists(current_browser_path, ec)) current_browser_path = std::filesystem::current_path();
                showing_drives = false;
                RefreshDirList();
            }
        }

        if (show_browser) {
            ImGuiMCP::Indent();
            ImGuiMCP::TextColored(ImGuiMCP::ImVec4(1, 0.8f, 0.2f, 1), showing_drives ? "Select disk:" : "Navegate in:");
            if (!showing_drives) ImGuiMCP::TextWrapped("%s", current_browser_path.string().c_str());

            if (ImGuiMCP::Button("Back")) {
                if (showing_drives) { /* Nada */ }
                else if (current_browser_path.has_parent_path() && current_browser_path != current_browser_path.root_path()) {
                    current_browser_path = current_browser_path.parent_path();
                    RefreshDirList();
                }
                else {
                    // Chegou na raiz (Ex: C:\), mostra os drives
                    showing_drives = true;
                    RefreshDrives();
                }
            }

            if (!showing_drives) {
                ImGuiMCP::SameLine();
                if (ImGuiMCP::Button("Select folder")) {
                    Settings::screenshotPath = current_browser_path.string();
                    strcpy_s(pathBuffer, Settings::screenshotPath.c_str());
                    changed = true;
                    show_browser = false;
                }
            }

            ImGuiMCP::Separator();
            if (ImGuiMCP::BeginChild("Files", ImGuiMCP::ImVec2(0, 200), true, 0)) {
                if (showing_drives) {
                    for (const auto& d : available_drives) {
                        if (ImGuiMCP::Selectable(d.c_str(), false)) {
                            current_browser_path = d;
                            showing_drives = false;
                            RefreshDirList();
                        }
                    }
                }
                else {
                    for (const auto& d : current_subdirs) {
                        if (ImGuiMCP::Selectable(d.filename().string().c_str(), false)) {
                            current_browser_path = d;
                            RefreshDirList();
                        }
                    }
                }
                ImGuiMCP::EndChild();
            }
            ImGuiMCP::Unindent();
        }

        if (changed) SaveSettings();
    }

    void Register() {
        if (SKSEMenuFramework::IsInstalled()) {
            SKSEMenuFramework::SetSection("Simple Screenshot");
            SKSEMenuFramework::AddSectionItem("Settings", Render);
        }
    }
}