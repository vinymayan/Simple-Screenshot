#pragma once
#include "SKSEMCP/SKSEMenuFramework.hpp"
#include <string>
#include <map>

// Enum para os formatos
enum class ScreenshotFormat : int {
    BMP = 0,
    PNG = 1,
    JPG = 2
};

namespace Settings {
    // Teclado
    inline uint32_t screenshotKey_k = 183; // Default 'Z'
    inline uint32_t screenshotKey_m = 0; // Default 'Z'
    inline uint32_t comboKey_k = 0;       // Tecla Combo (Ex: Shift). 0 = Desativado

    // Gamepad
    inline uint32_t screenshotKey_g = 0;  // Botão Screenshot
    inline uint32_t comboKey_g = 0;       // Botão Combo

    // Esconder UI
    inline uint32_t toggleUIKey_k = 56;
    inline uint32_t toggleUIKey_m = 0;
    inline uint32_t toggleUIKey_g = 0;
    inline uint32_t toggleUIComboKey_k = 47;
    inline uint32_t toggleUIComboKey_g = 0;

    // Configurações
    inline std::string screenshotPath = "Screenshots";
    inline ScreenshotFormat imageFormat = ScreenshotFormat::PNG;
    inline bool useCustomResolution = false;
    inline int customWidth = 1920;
    inline int customHeight = 1080;
}

namespace ScreenshotMenu {
    void RenderKeybind(const char* label, uint32_t* key_k, uint32_t* key_m, uint32_t* combo_k, uint32_t* key_g, uint32_t* combo_g);
    void Register();
    void LoadSettings();
    void SaveSettings();
    void Render();
    
}

// Mapa de Teclas (Reutilizando do BFCO para o Menu ficar bonito)
const std::map<int, const char*> g_gamepad_to_name_map = {
    {0, "[Nenhuma]"},
    {266, "DPad Up"}, {267, "DPad Down"}, {268, "DPad Left"}, {269, "DPad Right"},
    {270, "Start"}, {271, "Back"}, {272, "L3"}, {273, "R3"},
    {274, "LB/L1"}, {275, "RB/R1"}, {276, "A/X"}, {277, "B/O"}, {278, "X/Square"}, {279, "Y/Triangle"},
    {280, "LT/L2"}, {281, "RT/R2"}
};


// MAPA 2: Converte o Scan Code do DirectX para um Nome (o que você precisa exibir) - O seu mapa original.
const std::map<int, const char*> g_dx_to_name_map = {
    {0, "[Nenhuma]"},
    {1, "Escape"},
    {2, "1"},
    {3, "2"},
    {4, "3"},
    {5, "4"},
    {6, "5"},
    {7, "6"},
    {8, "7"},
    {9, "8"},
    {10, "9"},
    {11, "0"},
    {12, "-"},
    {13, "="},
    {14, "Backspace"},
    {15, "Tab"},
    {16, "Q"},
    {17, "W"},
    {18, "E"},
    {19, "R"},
    {20, "T"},
    {21, "Y"},
    {22, "U"},
    {23, "I"},
    {24, "O"},
    {25, "P"},
    {28, "Enter"},
    {29, "Left Ctrl"},
    {30, "A"},
    {31, "S"},
    {32, "D"},
    {33, "F"},
    {34, "G"},
    {35, "H"},
    {36, "J"},
    {37, "K"},
    {38, "L"},
    {39, ";"},
    {42, "Left Shift"},
    {43, "\\"},
    {44, "Z"},
    {45, "X"},
    {46, "C"},
    {47, "V"},
    {48, "B"},
    {49, "N"},
    {50, "M"},
    {51, ","},
    {52, "."},
    {53, "/"},
    {54, "Right Shift"},
    {56, "Left Alt"},
    {57, "Spacebar"},
    {59, "F1"},
    {60, "F2"},
    {61, "F3"},
    {62, "F4"},
    {63, "F5"},
    {64, "F6"},
    {65, "F7"},
    {66, "F8"},
    {67, "F9"},
    {68, "F10"},
    {87, "F11"},
    {88, "F12"},
    {156, "Keypad Enter"},
    {157, "Right Ctrl"},
    {184, "Right Alt"},
    {199, "Home"},
    {200, "Up Arrow"},
    {201, "PgUp"},
    {203, "Left Arrow"},
    {205, "Right Arrow"},
    {207, "End"},
    {208, "Down Arrow"},
    {209, "PgDown"},
    {210, "Insert"},
    {183, "PrintScreen"},
    {211, "Delete"},
    //{256, "Left Click"},
    //{257, "Right Click"},
    {258, "Middle Mouse Button"},
    {259, "Mouse 4"},
    {260, "Mouse 5"},
    {261, "Mouse 6"},
    {262, "Mouse 7"},
    {263, "Mouse 8"},
    {55, "Keypad *"},
    {181, "Keypad /"},
    {74, "Keypad -"},  // <-- ADICIONADO
    {78, "Keypad +"},  // <-- ADICIONADO
    {73, "Keypad 9"},  // <-- ADICIONADO
    {72, "Keypad 8"},  // <-- ADICIONADO
    {71, "Keypad 7"},  // <-- ADICIONADO
    {77, "Keypad 6"},  // <-- ADICIONADO
    {76, "Keypad 5"},  // <-- ADICIONADO
    {75, "Keypad 4"},  // <-- ADICIONADO
    {81, "Keypad 3"},  // <-- ADICIONADO
    {80, "Keypad 2"},  // <-- ADICIONADO
    {79, "Keypad 1"},  // <-- ADICIONADO
    {82, "Keypad 0"},  // <-- ADICIONADO
    {83, "Keypad ."},  // <-- ADICIONADO
    //{261, "Scroll Up"},
    //{262, "Scroll Down"}
    // Adicionei a key 0 para o caso "Nenhuma" para simplificar.
};