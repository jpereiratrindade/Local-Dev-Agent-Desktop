#include "AgentUI_Internal.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "misc/cpp/imgui_stdlib.h"
#include "OllamaClient.hpp"
#include "Orchestrator.hpp"
#include <filesystem>
#include <iostream>
#include <fstream>
#include <sstream>

namespace agent::ui {

// Implementações do Header Interno
std::string normalizeRootPath(const std::string& raw) {
    try {
        if (raw.empty() || raw == ".") return fs::weakly_canonical(fs::current_path()).string();
        return fs::weakly_canonical(fs::path(raw)).string();
    } catch (...) { return raw.empty() ? "." : raw; }
}

bool hasProjectMarkers(const fs::path& root) {
    return fs::exists(root / ".agent") || fs::exists(root / "AGENT.md") || fs::exists(root / "CMakeLists.txt");
}

int projectRootScore(const fs::path& root) {
    int score = 0;
    if (fs::exists(root / ".agent")) score += 10;
    if (fs::exists(root / "CMakeLists.txt")) score += 5;
    return score;
}

std::string resolveProjectRoot(const std::string& rawRoot) {
    return normalizeRootPath(rawRoot); 
}

// AgentUI Core Implementation
AgentUI::AgentUI() {
    splitterPosLeft = 260.0f;
    splitterPosRight = 320.0f;
    currentModel = "qwen2.5:14b";
}

AgentUI::~AgentUI() {
    stopTelemetryLoop();
}

void AgentUI::setOllama(agent::network::OllamaClient* client) {
    this->ollama = client;
    if (ollama) {
        ollamaVersion = ollama->fetchVersion();
        availableModels = ollama->listModels();
    }
}

void AgentUI::render() {
    drawMainMenu();
    
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    float statsHeight = 85.0f;
    float menuBarHeight = ImGui::GetFrameHeight();
    float mainAreaHeight = viewport->Size.y - menuBarHeight - statsHeight - 5.0f;

    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + menuBarHeight));
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, mainAreaHeight));
    
    ImGui::Begin("MainLayoutHost", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
    if (ImGui::BeginTable("MainWorkspaceTable", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("ExplorerCol", ImGuiTableColumnFlags_WidthFixed, splitterPosLeft);
        ImGui::TableSetupColumn("ChatCol", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("InspectorCol", ImGuiTableColumnFlags_WidthFixed, splitterPosRight);

        ImGui::TableNextRow(ImGuiTableRowFlags_None, mainAreaHeight);
        ImGui::TableNextColumn(); drawFileExplorer();
        ImGui::TableNextColumn(); drawChatWindow();
        ImGui::TableNextColumn(); drawThoughtPanel();

        ImGui::EndTable();
    }
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + viewport->Size.y - statsHeight));
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, statsHeight));
    drawStatsPanel();
    drawOpenFolderPickerDialog();
    drawGovernedProjectDialog();
    drawContextPolicyDialog();
    renderModelManagerModal();
}

void AgentUI::drawMainMenu() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("Arquivo")) {
            if (ImGui::MenuItem("Nova Sessão", "Ctrl+N")) newDialogue();
            if (ImGui::MenuItem("Abrir Pasta...", "Ctrl+O")) {
                openFolderPickerRequested = true;
                if (!hasOpenProject) {
                    folderPickerCurrentDir = fs::current_path().string();
                } else {
                    folderPickerCurrentDir = currentProjectRoot;
                }
                std::snprintf(folderPickerPathBuf, sizeof(folderPickerPathBuf), "%s", folderPickerCurrentDir.c_str());
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Sair", "Alt+F4")) exitRequested = true;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Config")) {
            if (ImGui::MenuItem("Model Manager", "Ctrl+M")) modelManagerRequested = true;
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
}

void AgentUI::newDialogue() {
    history.clear();
    thoughtStream = "Nova sessão iniciada.";
}

} // namespace agent::ui
