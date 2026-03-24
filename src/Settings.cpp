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
#include "Prisma.h"

namespace ScreenshotMenu {

    const char* SETTINGS_PATH = "Data/SKSE/Plugins/Simple Screenshot.json";

    bool show_browser = false;
    bool showing_drives = false;
    std::filesystem::path current_browser_path;
    std::vector<std::filesystem::path> current_subdirs;
    std::vector<std::string> available_drives;

    void RefreshDrives() {
        available_drives.clear();
        DWORD mask = GetLogicalDrives();
        for (char letter = 'A'; letter <= 'Z'; ++letter) {
            if (mask & 1) available_drives.push_back(std::string(1, letter) + ":\\");
            mask >>= 1;
        }
    }

    void RefreshDirList() {
        current_subdirs.clear();
        try {
            for (const auto& entry : std::filesystem::directory_iterator(current_browser_path)) {
                if (entry.is_directory()) current_subdirs.push_back(entry.path());
            }
            std::sort(current_subdirs.begin(), current_subdirs.end());
        }
        catch (...) {}
    }

    void RenderKeybind(const char* label, uint32_t* key_k, uint32_t* key_m, uint32_t* combo_k, uint32_t* key_g, uint32_t* combo_g) {
        bool changed = false;
        ImGuiMCP::PushID(label);
        ImGuiMCP::Text("%s", label);
        ImGuiMCP::Text("PC:"); ImGuiMCP::SameLine();

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

        const char* name_combo_k = "[Nenhuma]";
        if (g_dx_to_name_map.count(*combo_k)) name_combo_k = g_dx_to_name_map.at(*combo_k);
        ImGuiMCP::SetNextItemWidth(150);
        if (ImGuiMCP::BeginCombo("##ck", name_combo_k)) {
            for (auto& [code, name] : g_dx_to_name_map) {
                if (ImGuiMCP::Selectable(name, *combo_k == code)) { *combo_k = code; changed = true; }
            }
            ImGuiMCP::EndCombo();
        }

        if (key_g) {
            ImGuiMCP::Text("Gamepad:"); ImGuiMCP::SameLine();
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
        if (changed) {
            SaveSettings();
            Prisma::UpdateKeybindsUI();
        }
    }

    void SaveSettings() {
        rapidjson::Document doc;
        doc.SetObject();
        auto& allocator = doc.GetAllocator();

        doc.AddMember("openModeKey_k", Settings::openModeKey_k, allocator);
        doc.AddMember("openModeKey_m", Settings::openModeKey_m, allocator);
        doc.AddMember("openModeCombo_k", Settings::openModeCombo_k, allocator);
        doc.AddMember("openModeKey_g", Settings::openModeKey_g, allocator);
        doc.AddMember("openModeCombo_g", Settings::openModeCombo_g, allocator);

        doc.AddMember("captureWithUIKey_k", Settings::captureWithUIKey_k, allocator);
        doc.AddMember("captureWithUIKey_m", Settings::captureWithUIKey_m, allocator);
        doc.AddMember("captureWithUICombo_k", Settings::captureWithUICombo_k, allocator);
        doc.AddMember("captureWithUIKey_g", Settings::captureWithUIKey_g, allocator);
        doc.AddMember("captureWithUICombo_g", Settings::captureWithUICombo_g, allocator);

        doc.AddMember("captureNoUIKey_k", Settings::captureNoUIKey_k, allocator);
        doc.AddMember("captureNoUIKey_m", Settings::captureNoUIKey_m, allocator);
        doc.AddMember("captureNoUICombo_k", Settings::captureNoUICombo_k, allocator);
        doc.AddMember("captureNoUIKey_g", Settings::captureNoUIKey_g, allocator);
        doc.AddMember("captureNoUICombo_g", Settings::captureNoUICombo_g, allocator);

        doc.AddMember("useCustomResolution", Settings::useCustomResolution, allocator);
        doc.AddMember("customWidth", Settings::customWidth, allocator);
        doc.AddMember("customHeight", Settings::customHeight, allocator);
        doc.AddMember("defaultRatio", rapidjson::StringRef(Settings::defaultRatio.c_str()), allocator);
        doc.AddMember("defaultCustomRatioW", Settings::defaultCustomRatioW, allocator);
        doc.AddMember("defaultCustomRatioH", Settings::defaultCustomRatioH, allocator);
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
        FILE* fp = nullptr;
        fopen_s(&fp, SETTINGS_PATH, "rb");
        if (fp) {
            char readBuffer[65536];
            rapidjson::FileReadStream is(fp, readBuffer, sizeof(readBuffer));
            rapidjson::Document doc;
            doc.ParseStream(is);
            fclose(fp);

            if (doc.IsObject()) {
                if (doc.HasMember("openModeKey_k")) Settings::openModeKey_k = doc["openModeKey_k"].GetInt();
                if (doc.HasMember("openModeKey_m")) Settings::openModeKey_m = doc["openModeKey_m"].GetInt();
                if (doc.HasMember("openModeCombo_k")) Settings::openModeCombo_k = doc["openModeCombo_k"].GetInt();
                if (doc.HasMember("openModeKey_g")) Settings::openModeKey_g = doc["openModeKey_g"].GetInt();
                if (doc.HasMember("openModeCombo_g")) Settings::openModeCombo_g = doc["openModeCombo_g"].GetInt();

                if (doc.HasMember("captureWithUIKey_k")) Settings::captureWithUIKey_k = doc["captureWithUIKey_k"].GetInt();
                if (doc.HasMember("captureWithUIKey_m")) Settings::captureWithUIKey_m = doc["captureWithUIKey_m"].GetInt();
                if (doc.HasMember("captureWithUICombo_k")) Settings::captureWithUICombo_k = doc["captureWithUICombo_k"].GetInt();
                if (doc.HasMember("captureWithUIKey_g")) Settings::captureWithUIKey_g = doc["captureWithUIKey_g"].GetInt();
                if (doc.HasMember("captureWithUICombo_g")) Settings::captureWithUICombo_g = doc["captureWithUICombo_g"].GetInt();

                if (doc.HasMember("captureNoUIKey_k")) Settings::captureNoUIKey_k = doc["captureNoUIKey_k"].GetInt();
                if (doc.HasMember("captureNoUIKey_m")) Settings::captureNoUIKey_m = doc["captureNoUIKey_m"].GetInt();
                if (doc.HasMember("captureNoUICombo_k")) Settings::captureNoUICombo_k = doc["captureNoUICombo_k"].GetInt();
                if (doc.HasMember("captureNoUIKey_g")) Settings::captureNoUIKey_g = doc["captureNoUIKey_g"].GetInt();
                if (doc.HasMember("captureNoUICombo_g")) Settings::captureNoUICombo_g = doc["captureNoUICombo_g"].GetInt();

                if (doc.HasMember("useCustomResolution")) Settings::useCustomResolution = doc["useCustomResolution"].GetBool();
                if (doc.HasMember("customWidth")) Settings::customWidth = doc["customWidth"].GetInt();
                if (doc.HasMember("customHeight")) Settings::customHeight = doc["customHeight"].GetInt();
                if (doc.HasMember("defaultRatio")) Settings::defaultRatio = doc["defaultRatio"].GetString(); 
                if (doc.HasMember("defaultCustomRatioW")) Settings::defaultCustomRatioW = doc["defaultCustomRatioW"].GetInt();
                if (doc.HasMember("defaultCustomRatioH")) Settings::defaultCustomRatioH = doc["defaultCustomRatioH"].GetInt();
                if (doc.HasMember("imageFormat")) Settings::imageFormat = static_cast<ScreenshotFormat>(doc["imageFormat"].GetInt());
                if (doc.HasMember("screenshotPath")) Settings::screenshotPath = doc["screenshotPath"].GetString();
            }
        }
    }

    void __stdcall Render() {
        bool changed = false;
        ImGuiMCP::Text("Simple Screenshot Settings");
        ImGuiMCP::Separator();

        RenderKeybind("Open Screenshot Mode", &Settings::openModeKey_k, &Settings::openModeKey_m, &Settings::openModeCombo_k, &Settings::openModeKey_g, &Settings::openModeCombo_g);
        ImGuiMCP::Spacing();
        RenderKeybind("Capture with UI", &Settings::captureWithUIKey_k, &Settings::captureWithUIKey_m, &Settings::captureWithUICombo_k, &Settings::captureWithUIKey_g, &Settings::captureWithUICombo_g);
        ImGuiMCP::Spacing();
        RenderKeybind("Capture without UI", &Settings::captureNoUIKey_k, &Settings::captureNoUIKey_m, &Settings::captureNoUICombo_k, &Settings::captureNoUIKey_g, &Settings::captureNoUICombo_g);

        ImGuiMCP::Separator();
        const char* formats[] = { "BMP", "PNG", "JPG" };
        int current_fmt = static_cast<int>(Settings::imageFormat);
        if (ImGuiMCP::Combo("File format", &current_fmt, formats, 3)) {
            Settings::imageFormat = static_cast<ScreenshotFormat>(current_fmt);
            changed = true;
        }

        ImGuiMCP::Separator();
        ImGuiMCP::Text("Default Resolution:");

        const char* ratio_options[] = { "Free", "1:1", "3:2", "4:3", "16:9", "Custom" };
        const char* ratio_values[] = { "custom", "1:1", "3:2", "4:3", "16:9", "custom-input" };
        int current_ratio_idx = 0;
        for (int i = 0; i < 6; ++i) { // Agora são 6 opções
            if (Settings::defaultRatio == ratio_values[i]) { current_ratio_idx = i; break; }
        }
        if (ImGuiMCP::Combo("Default Ratio", &current_ratio_idx, ratio_options, 6)) {
            Settings::defaultRatio = ratio_values[current_ratio_idx];
            changed = true;
        }

        // Se o Ratio for Custom, mostra os campos para digitar W e H
        if (Settings::defaultRatio == "custom-input") {
            ImGuiMCP::Indent();
            if (ImGuiMCP::InputInt("Ratio Width", &Settings::defaultCustomRatioW)) {
                if (Settings::defaultCustomRatioW < 1) Settings::defaultCustomRatioW = 1;
                changed = true;
            }
            if (ImGuiMCP::InputInt("Ratio Height", &Settings::defaultCustomRatioH)) {
                if (Settings::defaultCustomRatioH < 1) Settings::defaultCustomRatioH = 1;
                changed = true;
            }
            ImGuiMCP::Unindent();
        }
        if (ImGuiMCP::Checkbox("Use Custom Resolution", &Settings::useCustomResolution)) changed = true;

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
            ImGuiMCP::TextColored(ImGuiMCP::ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Will be centered in Screenshot Mode.");
            ImGuiMCP::Unindent();
        }

        ImGuiMCP::Spacing();
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
                if (showing_drives) {}
                else if (current_browser_path.has_parent_path() && current_browser_path != current_browser_path.root_path()) {
                    current_browser_path = current_browser_path.parent_path();
                    RefreshDirList();
                }
                else {
                    showing_drives = true; RefreshDrives();
                }
            }

            if (!showing_drives) {
                ImGuiMCP::SameLine();
                if (ImGuiMCP::Button("Select folder")) {
                    Settings::screenshotPath = current_browser_path.string();
                    strcpy_s(pathBuffer, Settings::screenshotPath.c_str());
                    changed = true; show_browser = false;
                }
            }

            ImGuiMCP::Separator();
            if (ImGuiMCP::BeginChild("Files", ImGuiMCP::ImVec2(0, 200), true, 0)) {
                if (showing_drives) {
                    for (const auto& d : available_drives) {
                        if (ImGuiMCP::Selectable(d.c_str(), false)) { current_browser_path = d; showing_drives = false; RefreshDirList(); }
                    }
                }
                else {
                    for (const auto& d : current_subdirs) {
                        if (ImGuiMCP::Selectable(d.filename().string().c_str(), false)) { current_browser_path = d; RefreshDirList(); }
                    }
                }
                ImGuiMCP::EndChild();
            }
            ImGuiMCP::Unindent();
        }
        if (changed) {
            SaveSettings();
            Prisma::UpdateKeybindsUI();
        }
    }

    void Register() {
        if (SKSEMenuFramework::IsInstalled()) {
            SKSEMenuFramework::SetSection("Simple Screenshot");
            SKSEMenuFramework::AddSectionItem("Settings", Render);
        }
    }
}