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
    return fs::exists(root / ".agent")
        || fs::exists(root / "AGENT.md")
        || fs::exists(root / "AGENTS.md")
        || fs::exists(root / "PROJECT_CONTEXT.md")
        || fs::exists(root / "CMakeLists.txt")
        || fs::exists(root / "pyproject.toml")
        || fs::exists(root / "package.json")
        || fs::exists(root / "ai-governance");
}

int projectRootScore(const fs::path& root) {
    int score = 0;
    if (fs::exists(root / ".agent")) score += 10;
    if (fs::exists(root / "CMakeLists.txt")) score += 5;
    if (fs::exists(root / ".agent" / "sessions" / "last_session.json")) score += 18;
    if (fs::exists(root / ".agent" / "rag")) score += 12;
    if (fs::exists(root / ".agent" / "context_policy.json")) score += 10;
    if (fs::exists(root / "ai-governance")) score += 10;
    if (fs::exists(root / "AGENT.md") || fs::exists(root / "AGENTS.md")) score += 8;
    if (fs::exists(root / "PROJECT_CONTEXT.md")) score += 7;
    if (fs::exists(root / "pyproject.toml") || fs::exists(root / "package.json")) score += 4;
    return score;
}

std::string resolveProjectRoot(const std::string& rawRoot) {
    fs::path base = normalizeRootPath(rawRoot);
    fs::path best = base;
    int bestScore = projectRootScore(base);

    auto consider = [&](const fs::path& candidate) {
        if (!fs::exists(candidate) || !fs::is_directory(candidate)) return;
        if (!hasProjectMarkers(candidate)) return;
        int score = projectRootScore(candidate);
        if (score > bestScore) {
            bestScore = score;
            best = candidate;
        }
    };

    try {
        consider(base);
        for (const auto& entry : fs::directory_iterator(base)) {
            if (!entry.is_directory()) continue;
            consider(entry.path());
            try {
                for (const auto& nested : fs::directory_iterator(entry.path())) {
                    if (nested.is_directory()) consider(nested.path());
                }
            } catch (...) {}
        }
    } catch (...) {}

    return normalizeRootPath(best.string());
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
        ImGui::Separator();
        ImGui::TextDisabled("Projeto:");
        ImGui::SameLine();
        if (hasOpenProject && !currentProjectRoot.empty()) {
            ImGui::Text("%s", fs::path(currentProjectRoot).filename().string().c_str());
        } else {
            ImGui::TextDisabled("(nenhum)");
        }
        ImGui::EndMainMenuBar();
    }
}

void AgentUI::newDialogue() {
    history.clear();
    if (orchestrator) orchestrator->clearHistory();
    thoughtStream = "Nova sessão iniciada.";
}

} // namespace agent::ui
