#include "AgentUI_Internal.hpp"
#include "imgui.h"
#include "Orchestrator.hpp"
#include <thread>
#include <fstream>
#include "json.hpp"

namespace agent::ui {

void AgentUI::startTelemetry() {
    stopTelemetry = false;
    std::thread([this]() { this->telemetryLoop(); }).detach();
}

void AgentUI::stopTelemetryLoop() {
    stopTelemetry = true;
}

void AgentUI::telemetryLoop() {
    while (!stopTelemetry) {
        {
            float bestLoad = 0.0f;
            float bestVram = 0.0f;
            std::string bestCard = "N/A";

            for (int i = 0; i < 4; ++i) {
                std::string base = "/sys/class/drm/card" + std::to_string(i) + "/device/";
                std::ifstream v(base + "mem_info_vram_used");
                if (v.is_open()) {
                    long long val; v >> val;
                    float vUsedGb = (float)val / 1024.0f / 1024.0f / 1024.0f;
                    
                    float currentLoad = 0;
                    std::ifstream f(base + "gpu_busy_percent");
                    if (f.is_open()) f >> currentLoad;

                    if (currentLoad > bestLoad || (currentLoad == bestLoad && vUsedGb > bestVram)) {
                        bestLoad = currentLoad;
                        bestVram = vUsedGb;
                        bestCard = "card" + std::to_string(i);
                    }
                }
            }

            {
                std::lock_guard<std::mutex> lock(telemetryMutex);
                gpuLoad = bestLoad;
                vramUsed = bestVram;
                activeGpuName = bestCard;

                std::ifstream m("/proc/meminfo");
                std::string line;
                while (std::getline(m, line)) {
                    if (line.find("MemTotal:") == 0) vramTotal = (float)std::stol(line.substr(9)) / 1024.0f / 1024.0f;
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

void AgentUI::drawStatsPanel() {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.08f, 0.1f, 1.0f));
    ImGui::BeginChild("StatsArea", ImVec2(0, 0), true);
    
    std::lock_guard<std::mutex> lock(telemetryMutex);
    ImGui::TextColored(ImVec4(0.0f, 0.8f, 1.0f, 1.0f), "SYSTEM TELEMETRY v4.1 (AMD-SENSE)");
    ImGui::Separator();
    
    ImGui::Columns(4, "StatsColumns", false);
    ImGui::Text("GPU: %s", activeGpuName.c_str());
    ImGui::Text("LOAD: %.1f%%", gpuLoad);
    ImGui::NextColumn();
    ImGui::Text("VRAM: %.2f GB", vramUsed);
    ImGui::Text("HOST RAM: %.1f GB", vramTotal);
    ImGui::NextColumn();
    ImGui::Text("TOKENS IN: %d", totalPromptTokens);
    ImGui::Text("TOKENS OUT: %d", totalCompletionTokens);
    ImGui::NextColumn();
    ImGui::Text("MS/TOK: %.1f ms", tokenRateMs);
    ImGui::Text("TOK/S: %.2f", tokensPerSec);

    ImGui::Columns(1);
    ImGui::Separator();
    ImGui::Text("Projeto: %s", hasOpenProject ? currentProjectRoot.c_str() : "(nenhum)");
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::Text("Modelo: %s", currentModel.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::Text("Arquivo: %s", selectedFile.empty() ? "(nenhum)" : fs::path(selectedFile).filename().string().c_str());
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void AgentUI::drawThoughtPanel() {
    ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "PENSAMENTO & CONTEXTO");
    ImGui::Separator();
    
    ImVec4 thoughtColor = ImVec4(0.6f, 0.6f, 0.7f, 1.0f);
    if (thoughtStream.find("ERRO") != std::string::npos) thoughtColor = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
    else if (thoughtStream.find("SUCCESS") != std::string::npos || thoughtStream.find("Concluido") != std::string::npos) 
        thoughtColor = ImVec4(0.4f, 1.0f, 0.4f, 1.0f);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.12f, 0.14f, 1.0f));
    ImGui::BeginChild("ThinkingProcess", ImVec2(0, 0), true);
    ImGui::TextWrapped("%s", thoughtStream.c_str());
    
    if (!projectGovernance.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "[GOVERNANCE ACTIVE]");
        ImGui::Separator();
    }

    if (autonomousMode) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "[MODO AUTÔNOMO ATIVO]");
        if (ImGui::Button("PARA MISSÃO", ImVec2(-1, 0))) {
            if (orchestrator) orchestrator->stopMission();
            thoughtStream = "Parada solicitada pelo usuário.";
        }
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void AgentUI::triggerRagSync() {
    if (ragIndexingBusy) return;
    ragIndexingBusy = true;
    ragIndexingProgress = 0.0f;
    ragIndexingStatusMsg = "Iniciando varredura...";

    std::thread([this]() {
        try {
            std::vector<std::string> roots = agent::core::getNativeToolApprovedRoots();
            std::vector<std::filesystem::path> allFiles;
            std::vector<std::string> missingRoots;
            
            for (const auto& rootStr : roots) {
                try {
                    std::filesystem::path root(rootStr);
                    if (!std::filesystem::exists(root)) {
                        missingRoots.push_back(rootStr);
                        continue;
                    }
                    for (const auto& entry : std::filesystem::recursive_directory_iterator(root, std::filesystem::directory_options::skip_permission_denied)) {
                        if (entry.is_regular_file()) {
                            std::string ext = entry.path().extension().string();
                            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return std::tolower(c); });
                            if (ext == ".pdf" || ext == ".txt" || ext == ".md" || ext == ".rst") {
                                allFiles.push_back(entry.path());
                            }
                        }
                    }
                } catch (...) {
                    missingRoots.push_back(rootStr);
                }
            }

            if (!allFiles.empty()) {
                int successCount = 0;
                int errorCount = 0;
                for (size_t i = 0; i < allFiles.size(); ++i) {
                    try {
                        ragIndexingStatusMsg = "Ingerindo [" + std::to_string(i+1) + "/" + std::to_string(allFiles.size()) + "]: " + allFiles[i].filename().string();
                        std::string res = agent::core::ingest_file_direct(allFiles[i].string());
                        if (res.rfind("Erro", 0) == 0) errorCount++;
                        else successCount++;
                    } catch (...) { errorCount++; }
                    ragIndexingProgress = static_cast<float>(i + 1) / static_cast<float>(allFiles.size());
                    if (i % 5 == 0) ragStats = agent::core::getNativeToolRagStats();
                }
                ragIndexingStatusMsg = "Concluído! Sucesso: " + std::to_string(successCount) + " | Erros: " + std::to_string(errorCount);
            } else {
                ragIndexingStatusMsg = "Nenhum arquivo encontrado.";
            }
            ragStats = agent::core::getNativeToolRagStats();
            std::this_thread::sleep_for(std::chrono::seconds(5));
        } catch (...) {
            ragIndexingStatusMsg = "Erro no sync.";
        }
        ragIndexingBusy = false;
    }).detach();
}

} // namespace agent::ui
