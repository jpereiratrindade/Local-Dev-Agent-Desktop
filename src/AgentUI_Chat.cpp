#include "AgentUI_Internal.hpp"
#include "imgui.h"
#include "Orchestrator.hpp"
#include <regex>
#include <fstream>
#include <sstream>
#include <thread>
#include <mutex>

namespace agent::ui {

struct ActionStep {
    std::string title;
    std::string content;
};

struct AgentMessageSections {
    std::string answer;
    std::string actions;
    std::string logs;
    std::vector<ActionStep> actionSteps;
};

static AgentMessageSections splitAgentMessage(const std::string& text) {
    AgentMessageSections sections;
    sections.answer = text;
    std::regex jsonBlockRegex("```json\\s*([\\s\\S]*?)\\s*```");
    for (std::sregex_iterator it(text.begin(), text.end(), jsonBlockRegex), end; it != end; ++it) {
        if (!sections.actions.empty()) sections.actions += "\n\n";
        const std::string block = (*it)[1].str();
        sections.actions += block;
        ActionStep step;
        step.title = "Ação " + std::to_string(sections.actionSteps.size() + 1);
        step.content = block;
        sections.actionSteps.push_back(step);
    }
    sections.answer = std::regex_replace(sections.answer, jsonBlockRegex, "");
    sections.answer = std::regex_replace(sections.answer, std::regex("\n\\s*---\\s*\n"), "\n");
    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.find("OBSERVAÇÃO") != std::string::npos ||
            line.find("AÇÃO:") != std::string::npos ||
            line.find("PASSO") != std::string::npos ||
            line.find("Missão") != std::string::npos ||
            line.find("MISSION") != std::string::npos) {
            sections.logs += line + "\n";
        }
    }
    return sections;
}

void AgentUI::renderMarkdown(const std::string& text) {
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.substr(0, 3) == "###") {
            ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "%s", line.substr(3).c_str());
        } else if (line.substr(0, 2) == "##") {
            ImGui::TextColored(ImVec4(0.0f, 0.8f, 1.0f, 1.0f), "%s", line.substr(2).c_str());
        } else if (line.substr(0, 1) == "#") {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", line.substr(1).c_str());
        } else if (line.find("`") != std::string::npos) {
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.1f, 1.0f), "%s", line.c_str());
        } else {
            ImGui::TextWrapped("%s", line.c_str());
        }
    }
}

void AgentUI::runPythonAgent(const std::string& goal, const std::string& mode) {
    if (llmBusy || !orchestrator) return;
    llmBusy = true;
    
    std::thread([this, goal, mode]() {
        try {
            thoughtStream = "Iniciada missão: " + goal + " (" + mode + ")";
            
            agent::network::OllamaOptions opts;
            opts.temperature = (reasoning == "high") ? 0.2f : 0.7f;
            
            // Injeção de governança se disponível
            std::string fullGoal = goal;
            if (!projectGovernance.empty()) {
                fullGoal = "[GOVERNANÇA ATIVA: SIGA ESTAS REGRAS]\n" + projectGovernance + "\n\n[OBJETIVO ATUAL]\n" + goal;
            }

            agent::core::Orchestrator::MissionCallbacks callbacks;
            callbacks.onMessageChunk = [this](const std::string& chunk) {
                std::lock_guard<std::mutex> lock(msgMutex);
                if (history.empty() || history.back().role != "assistant") {
                    history.push_back({"assistant", ""});
                }
                history.back().text += chunk;
                scrollToBottom = true;
            };
            callbacks.onThought = [this](const std::string& thought) {
                thoughtStream = thought;
            };
            callbacks.onComplete = [this](bool success) {
                llmBusy = false;
                thoughtStream = success ? "Missão concluída." : "Missão interrompida.";
                saveSession();
            };

            orchestrator->runMission(fullGoal, mode, 10, callbacks, opts);

            thoughtStream = "Missão concluída.";
        } catch (const std::exception& e) {
            thoughtStream = "ERRO NA MISSÃO: " + std::string(e.what());
        }
        llmBusy = false;
        saveSession();
    }).detach();
}

void AgentUI::drawChatWindow() {
    ImGui::BeginChild("ChatWindowChild", ImVec2(0, 0), true);
    ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "AGENT CHAT");
    ImGui::TextDisabled(" | Model:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150);
    if (ImGui::BeginCombo("##ModelSelector", currentModel.c_str())) {
        for (const auto& model : availableModels) {
            bool isSelected = (currentModel == model);
            if (ImGui::Selectable(model.c_str(), isSelected)) {
                currentModel = model;
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::Separator();

    float footerHeight = ImGui::GetFrameHeightWithSpacing() + 55.0f;
    if (!selectedFile.empty()) footerHeight += 25.0f;

    ImGui::BeginChild("ChatHistory", ImVec2(0, -footerHeight), false);
    {
        std::lock_guard<std::mutex> lock(msgMutex);
        for (size_t i = 0; i < history.size(); ++i) {
            const auto& msg = history[i];
            if (msg.role == "user") {
                ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "[VOCÊ]");
                ImGui::TextWrapped("%s", msg.text.c_str());
            } else {
                ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1.0f), "[AGENT]");
                AgentMessageSections sections = splitAgentMessage(msg.text);
                
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::SmallButton("Copiar")) ImGui::SetClipboardText(msg.text.c_str());
                ImGui::PopID();

                ImGui::BeginChild(("AgentMsgBlock_" + std::to_string(i)).c_str(), ImVec2(0, 200), true);
                renderMarkdown(sections.answer);
                ImGui::EndChild();
            }
            ImGui::Separator();
        }
    }
    if (scrollToBottom) { ImGui::SetScrollHereY(1.0f); scrollToBottom = false; }
    ImGui::EndChild();

    ImGui::Separator();
    
    if (!selectedFile.empty()) {
        ImGui::TextDisabled("Anexo: %s", fs::path(selectedFile).filename().string().c_str());
    }

    ImGui::PushItemWidth(-1);
    if (ImGui::InputTextWithHint("##ChatInput", "Questione ou defina missão...", inputBuf, sizeof(inputBuf), ImGuiInputTextFlags_EnterReturnsTrue)) {
        if (std::strlen(inputBuf) > 0 && !llmBusy) {
            std::string queryText = inputBuf;
            history.push_back({"user", queryText});
            std::memset(inputBuf, 0, sizeof(inputBuf));
            scrollToBottom = true;
            runPythonAgent(queryText, "AGENT");
        }
    }
    ImGui::PopItemWidth();

    if (llmBusy) {
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "Agent está pensando...");
    } else {
        if (ImGui::Button("SND (Chat)", ImVec2(100, 0))) {
            if (std::strlen(inputBuf) > 0) {
                std::string queryText = inputBuf;
                history.push_back({"user", queryText});
                std::memset(inputBuf, 0, sizeof(inputBuf));
                scrollToBottom = true;
                runPythonAgent(queryText, "AGENT");
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("MISSION (Auto)", ImVec2(120, 0))) {
             if (std::strlen(inputBuf) > 0) {
                std::string queryText = inputBuf;
                history.push_back({"user", "MISSÃO: " + queryText});
                std::memset(inputBuf, 0, sizeof(inputBuf));
                scrollToBottom = true;
                runPythonAgent(queryText, "MISSION");
            }
        }
    }

    ImGui::EndChild();
}

} // namespace agent::ui
