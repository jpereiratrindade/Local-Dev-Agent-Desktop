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

namespace {
bool containsAnyLower(const std::string& haystack, const std::vector<std::string>& needles) {
    for (const auto& needle : needles) {
        if (haystack.find(needle) != std::string::npos) return true;
    }
    return false;
}

std::string toLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}
} // namespace

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

std::string AgentUI::buildActiveContextBlock() const {
    std::stringstream context;
    if (hasOpenProject && !currentProjectRoot.empty()) {
        context << "Projeto atual: " << currentProjectRoot << "\n";
    }
    if (!selectedFile.empty()) {
        context << "Arquivo ativo: " << selectedFile << "\n";
        std::string content;
        if (selectedFile == editorFilePath) {
            content = editorUsesPlainText ? editorPlainTextBuffer : codeEditor.GetText();
        } else {
            try {
                std::ifstream in(selectedFile);
                if (in) {
                    std::stringstream buffer;
                    buffer << in.rdbuf();
                    content = buffer.str();
                }
            } catch (...) {}
        }
        if (!content.empty()) {
            if (content.size() > 12000) content = content.substr(0, 12000) + "\n...[conteudo truncado]...";
            context << "Conteudo atual do arquivo ativo:\n```text\n" << content << "\n```\n";
        }
    }
    if (!projectGovernance.empty()) {
        context << "Governanca local ativa.\n";
    }
    return context.str();
}

std::string AgentUI::buildChatSystemPrompt() const {
    std::stringstream prompt;
    prompt << "Voce e um assistente local de desenvolvimento e escrita orientado a execucao.\n";
    prompt << "Regras:\n";
    prompt << "1. Preserve continuidade com o historico do chat.\n";
    prompt << "2. Se houver arquivo ativo e o pedido implicar editar, continuar, revisar ou inserir conteudo, trate o arquivo ativo como alvo principal.\n";
    prompt << "3. Para tarefas editoriais, responda como coautor e use o arquivo ativo antes de explorar o resto do projeto.\n";
    prompt << "4. Para tarefas executivas e quando necessario, voce pode propor JSON de tool-call, mas so faca isso se realmente precisar agir no workspace.\n";
    prompt << "5. Seja concreto. Se o pedido for de alteracao de conteudo, entregue texto pronto para inserir ou diga claramente qual alteracao faria.\n";
    if (hasOpenProject && !currentProjectRoot.empty()) {
        prompt << "Projeto atual: " << currentProjectRoot << "\n";
    }
    if (!selectedFile.empty()) {
        prompt << "Arquivo ativo priorizado: " << selectedFile << "\n";
    }
    return prompt.str();
}

std::string AgentUI::extractWritableAssistantText(const std::string& text) const {
    AgentMessageSections sections = splitAgentMessage(text);
    std::string cleaned = trimLoose(sections.answer);
    cleaned = std::regex_replace(cleaned, std::regex("<thought>[\\s\\S]*?</thought>"), "");
    cleaned = std::regex_replace(cleaned, std::regex("TASK COMPLETE"), "");
    return trimLoose(cleaned);
}

std::string AgentUI::inferTaskMode(const std::string& goal) const {
    std::string lower = toLowerCopy(goal);
    if (containsAnyLower(lower, {"crie arquivo", "criar arquivo", "gere projeto", "scaffold", "rodar", "executar", "corrigir build", "refator", "refactor", "renomear arquivo", "mover arquivo"})) {
        return "MISSION";
    }
    if (!selectedFile.empty() && containsAnyLower(lower, {"inclua", "incluir", "continue", "continuar", "revise este texto", "reescreva", "melhore o texto", "edite", "ajuste o documento", "insira"})) {
        return "ASSIST";
    }
    return "CHAT";
}

void AgentUI::runPythonAgent(const std::string& goal, const std::string& mode) {
    if (llmBusy || !orchestrator) return;
    llmBusy = true;
    
    std::thread([this, goal, mode]() {
        try {
            thoughtStream = "Iniciada missão: " + goal + " (" + mode + ")";
            
            agent::network::OllamaOptions opts;
            opts.temperature = (reasoning == "high") ? 0.2f : 0.7f;

            ollama->setModel(currentModel); // Sincroniza o modelo selecionado

            const std::string resolvedMode = (mode == "AUTO") ? inferTaskMode(goal) : mode;
            const bool useMissionLoop = (resolvedMode == "MISSION");

            if (!useMissionLoop) {
                std::vector<agent::network::Message> threadHistory;
                threadHistory.push_back({"system", buildChatSystemPrompt()});
                {
                    std::lock_guard<std::mutex> lock(msgMutex);
                    for (const auto& msg : history) {
                        threadHistory.push_back({msg.role, msg.text});
                    }
                }
                const std::string activeContext = buildActiveContextBlock();
                if (!activeContext.empty()) {
                    threadHistory.push_back({"system", activeContext});
                }

                ollama->chatStream(threadHistory,
                    [this](const std::string& chunk) {
                        std::lock_guard<std::mutex> lock(msgMutex);
                        if (history.empty() || history.back().role != "assistant") {
                            history.push_back({"assistant", ""});
                        }
                        history.back().text += chunk;
                        scrollToBottom = true;
                    },
                    [this](bool ok, agent::network::OllamaStreamStats stats) {
                        {
                            std::lock_guard<std::mutex> lock(msgMutex);
                            totalPromptTokens = stats.prompt_tokens;
                            totalCompletionTokens = stats.completion_tokens;
                            tokensPerSec = (stats.total_duration_ms > 0.0)
                                ? static_cast<float>(stats.completion_tokens / (stats.total_duration_ms / 1000.0))
                                : 0.0f;
                            tokenRateMs = (stats.completion_tokens > 0)
                                ? static_cast<float>(stats.total_duration_ms / stats.completion_tokens)
                                : 0.0f;
                        }
                        llmBusy = false;
                        thoughtStream = ok ? "Resposta concluída." : "Resposta interrompida.";
                        saveSession();
                    },
                    "", opts);
                return;
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
            callbacks.onAction = [this](const std::string& action) {
                thoughtStream = "Ação: " + action;
            };
            callbacks.onObservation = [this](const std::string& obs) {
                thoughtStream = "Observação recebida (" + std::to_string(obs.size()) + " bytes)";
            };
            callbacks.onComplete = [this](bool success) {
                llmBusy = false; // Reset ONLY when background thread completes
                thoughtStream = success ? "Missão concluída." : "Missão interrompida.";
                saveSession();
            };
            callbacks.onStreamStats = [this](const agent::network::OllamaStreamStats& stats) {
                std::lock_guard<std::mutex> lock(telemetryMutex);
                totalPromptTokens = stats.prompt_tokens;
                totalCompletionTokens = stats.completion_tokens;
                tokensPerSec = (stats.total_duration_ms > 0.0)
                    ? static_cast<float>(stats.completion_tokens / (stats.total_duration_ms / 1000.0))
                    : 0.0f;
                tokenRateMs = (stats.completion_tokens > 0)
                    ? static_cast<float>(stats.total_duration_ms / stats.completion_tokens)
                    : 0.0f;
            };

            std::string fullGoal = goal;
            const std::string activeContext = buildActiveContextBlock();
            if (!activeContext.empty()) {
                fullGoal += "\n\n[CONTEXTO ATIVO]\n" + activeContext;
            }
            if (!projectGovernance.empty()) {
                fullGoal = "[GOVERNANÇA ATIVA: SIGA ESTAS REGRAS]\n" + projectGovernance + "\n\n[OBJETIVO ATUAL]\n" + fullGoal;
            }

            orchestrator->runMission(fullGoal, "MISSION", 10, callbacks, opts);
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(msgMutex);
            thoughtStream = "ERRO NA MISSÃO: " + std::string(e.what());
            llmBusy = false;
        }
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
    const std::string inferredMode = inferTaskMode(inputBuf);
    ImGui::TextDisabled("Modo sugerido: %s", inferredMode == "MISSION" ? "Missao" : "Chat assistido");
    if (!selectedFile.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("| Arquivo alvo: %s", fs::path(selectedFile).filename().string().c_str());
    }

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
                if (!editorFilePath.empty()) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Aplicar")) {
                        const std::string writable = extractWritableAssistantText(msg.text);
                        if (applyTextToActiveFile(writable, false)) {
                            thoughtStream = "Resposta aplicada ao arquivo ativo.";
                        } else {
                            thoughtStream = "ERRO: nao foi possivel aplicar ao arquivo ativo.";
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Aplicar+Salvar")) {
                        const std::string writable = extractWritableAssistantText(msg.text);
                        if (applyTextToActiveFile(writable, true)) {
                            thoughtStream = "Resposta aplicada e salva no arquivo ativo.";
                        } else {
                            thoughtStream = "ERRO: nao foi possivel aplicar e salvar.";
                        }
                    }
                }
                ImGui::PopID();

                ImGui::BeginChild(("AgentMsgBlock_" + std::to_string(i)).c_str(), ImVec2(0, 300), true);
                if (ImGui::BeginTabBar("AgentMessageTabs")) {
                    if (ImGui::BeginTabItem("Resposta")) {
                        renderMarkdown(sections.answer);
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Ações")) {
                        if (sections.actionSteps.empty()) {
                            ImGui::TextDisabled("Nenhuma ferramenta foi utilizada nesta resposta.");
                        } else {
                            for (const auto& step : sections.actionSteps) {
                                if (ImGui::CollapsingHeader(step.title.c_str())) {
                                    ImGui::TextWrapped("%s", step.content.c_str());
                                }
                            }
                        }
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Logs")) {
                        if (sections.logs.empty()) {
                            ImGui::TextDisabled("Nenhum log técnico disponível.");
                        } else {
                            ImGui::TextUnformatted(sections.logs.c_str());
                        }
                        ImGui::EndTabItem();
                    }
                    ImGui::EndTabBar();
                }
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
            {
                std::lock_guard<std::mutex> lock(msgMutex);
                history.push_back({"user", queryText});
            }
            std::memset(inputBuf, 0, sizeof(inputBuf));
            scrollToBottom = true;
            runPythonAgent(queryText, "AUTO");
        }
    }
    ImGui::PopItemWidth();

    if (llmBusy) {
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "Agent está pensando...");
        ImGui::SameLine();
        if (ImGui::SmallButton("PARAR")) {
            if (ollama) ollama->requestStop();
            if (orchestrator) orchestrator->stopMission();
            thoughtStream = "Parada forçada pelo usuário.";
        }
    } else {
        if (ImGui::Button("SND (Chat)", ImVec2(100, 0))) {
            if (std::strlen(inputBuf) > 0) {
                std::string queryText = inputBuf;
                history.push_back({"user", queryText});
                std::memset(inputBuf, 0, sizeof(inputBuf));
                scrollToBottom = true;
                runPythonAgent(queryText, "CHAT");
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
