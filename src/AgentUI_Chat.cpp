#include "AgentUI_Internal.hpp"
#include "imgui.h"
#include "Orchestrator.hpp"
#include "json.hpp"
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

std::vector<std::string> extractCodeBlocks(const std::string& text) {
    std::vector<std::string> blocks;
    std::regex codeBlockRegex("```(?:[A-Za-z0-9_+#.-]+)?\\s*([\\s\\S]*?)\\s*```");
    for (std::sregex_iterator it(text.begin(), text.end(), codeBlockRegex), end; it != end; ++it) {
        blocks.push_back(trimLoose((*it)[1].str()));
    }
    return blocks;
}

std::vector<std::string> extractJsonBlocks(const std::string& text) {
    std::vector<std::string> blocks;
    std::regex jsonBlockRegex("```json\\s*([\\s\\S]*?)\\s*```");
    for (std::sregex_iterator it(text.begin(), text.end(), jsonBlockRegex), end; it != end; ++it) {
        blocks.push_back(trimLoose((*it)[1].str()));
    }
    return blocks;
}

bool looksLikeMixedExplanatoryResponse(const std::string& text) {
    std::string lower = toLowerCopy(text);
    return containsAnyLower(lower, {
        "explicação", "explicacao", "código atualizado", "codigo atualizado",
        "para testar", "compile", "agora você terá", "agora voce tera",
        "vou implementar", "se precisar", "###", "1.", "2.", "3."
    });
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

std::string AgentUI::buildSimpleDiffPreview(const std::string& oldText, const std::string& newText) const {
    std::istringstream oldStream(oldText);
    std::istringstream newStream(newText);
    std::vector<std::string> oldLines;
    std::vector<std::string> newLines;
    std::string line;
    while (std::getline(oldStream, line)) oldLines.push_back(line);
    while (std::getline(newStream, line)) newLines.push_back(line);

    std::stringstream out;
    const size_t maxLines = std::max(oldLines.size(), newLines.size());
    size_t emitted = 0;
    for (size_t i = 0; i < maxLines; ++i) {
        const bool hasOld = i < oldLines.size();
        const bool hasNew = i < newLines.size();
        const std::string oldLine = hasOld ? oldLines[i] : "";
        const std::string newLine = hasNew ? newLines[i] : "";
        if (hasOld && hasNew && oldLine == newLine) continue;
        if (hasOld) out << "- " << oldLine << "\n";
        if (hasNew) out << "+ " << newLine << "\n";
        emitted++;
        if (emitted >= 120) {
            out << "...diff truncado...\n";
            break;
        }
    }
    if (emitted == 0) out << "(sem diferencas detectadas)\n";
    return out.str();
}

std::string AgentUI::inferActiveFileForGoal(const std::string& goal) const {
    const std::string lowerGoal = toLowerCopy(goal);

    auto candidateScore = [&](const std::string& candidatePath) -> int {
        if (candidatePath.empty()) return -1;
        fs::path path(candidatePath);
        const std::string filename = toLowerCopy(path.filename().string());
        const std::string rel = hasOpenProject && !currentProjectRoot.empty()
            ? toLowerCopy(fs::relative(path, currentProjectRoot).string())
            : filename;

        int score = 0;
        if (!filename.empty() && lowerGoal.find(filename) != std::string::npos) score += 100;
        if (!rel.empty() && lowerGoal.find(rel) != std::string::npos) score += 120;
        if (!selectedFile.empty() && candidatePath == selectedFile) score += 25;
        if (!editorFilePath.empty() && candidatePath == editorFilePath) score += 35;
        if (!lastChangeTargetPath.empty() && candidatePath == lastChangeTargetPath) score += 30;

        const auto it = std::find(recentFiles.begin(), recentFiles.end(), candidatePath);
        if (it != recentFiles.end()) {
            score += std::max(5, 20 - static_cast<int>(std::distance(recentFiles.begin(), it)) * 3);
        }

        if (lowerGoal.find("este arquivo") != std::string::npos || lowerGoal.find("arquivo ativo") != std::string::npos) {
            if (!editorFilePath.empty() && candidatePath == editorFilePath) score += 60;
            else if (!selectedFile.empty() && candidatePath == selectedFile) score += 40;
        }

        if (lowerGoal.find("esse texto") != std::string::npos || lowerGoal.find("continua") != std::string::npos ||
            lowerGoal.find("continuar") != std::string::npos || lowerGoal.find("corrigir") != std::string::npos ||
            lowerGoal.find("revisar") != std::string::npos || lowerGoal.find("ajustar") != std::string::npos) {
            if (!editorFilePath.empty() && candidatePath == editorFilePath) score += 20;
        }
        return score;
    };

    std::string bestPath;
    int bestScore = -1;
    std::vector<std::string> candidates;
    if (!editorFilePath.empty()) candidates.push_back(editorFilePath);
    if (!selectedFile.empty() && selectedFile != editorFilePath) candidates.push_back(selectedFile);
    if (!lastChangeTargetPath.empty() &&
        std::find(candidates.begin(), candidates.end(), lastChangeTargetPath) == candidates.end()) {
        candidates.push_back(lastChangeTargetPath);
    }
    for (const auto& recent : recentFiles) {
        if (std::find(candidates.begin(), candidates.end(), recent) == candidates.end()) candidates.push_back(recent);
    }

    for (const auto& candidate : candidates) {
        int score = candidateScore(candidate);
        if (score > bestScore) {
            bestScore = score;
            bestPath = candidate;
        }
    }

    if (bestScore <= 0) {
        if (!editorFilePath.empty()) return editorFilePath;
        if (!selectedFile.empty()) return selectedFile;
    }
    return bestPath;
}

std::string AgentUI::inferActiveFileAmbiguityNote(const std::string& goal) const {
    const std::string lowerGoal = toLowerCopy(goal);
    struct CandidateScore { std::string path; int score; };
    std::vector<CandidateScore> scored;
    std::vector<std::string> candidates;
    if (!editorFilePath.empty()) candidates.push_back(editorFilePath);
    if (!selectedFile.empty() && selectedFile != editorFilePath) candidates.push_back(selectedFile);
    if (!lastChangeTargetPath.empty() &&
        std::find(candidates.begin(), candidates.end(), lastChangeTargetPath) == candidates.end()) {
        candidates.push_back(lastChangeTargetPath);
    }
    for (const auto& recent : recentFiles) {
        if (std::find(candidates.begin(), candidates.end(), recent) == candidates.end()) candidates.push_back(recent);
    }

    for (const auto& candidatePath : candidates) {
        fs::path path(candidatePath);
        const std::string filename = toLowerCopy(path.filename().string());
        const std::string rel = hasOpenProject && !currentProjectRoot.empty()
            ? toLowerCopy(fs::relative(path, currentProjectRoot).string())
            : filename;
        int score = 0;
        if (!filename.empty() && lowerGoal.find(filename) != std::string::npos) score += 100;
        if (!rel.empty() && lowerGoal.find(rel) != std::string::npos) score += 120;
        if (!selectedFile.empty() && candidatePath == selectedFile) score += 25;
        if (!editorFilePath.empty() && candidatePath == editorFilePath) score += 35;
        if (!lastChangeTargetPath.empty() && candidatePath == lastChangeTargetPath) score += 30;
        const auto it = std::find(recentFiles.begin(), recentFiles.end(), candidatePath);
        if (it != recentFiles.end()) score += std::max(5, 20 - static_cast<int>(std::distance(recentFiles.begin(), it)) * 3);
        if (score > 0) scored.push_back({candidatePath, score});
    }

    if (scored.size() < 2) return "";
    std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) {
        return a.score > b.score;
    });
    if (scored[0].score - scored[1].score <= 15) {
        return "Ambiguidade de arquivo: " + fs::path(scored[0].path).filename().string() +
               " ou " + fs::path(scored[1].path).filename().string();
    }
    return "";
}

std::string AgentUI::buildActiveContextBlock() const {
    std::stringstream context;
    if (hasOpenProject && !currentProjectRoot.empty()) {
        context << "Projeto atual: " << currentProjectRoot << "\n";
    }
    const std::string activeFile = inferActiveFileForGoal(history.empty() ? "" : history.back().text);
    if (!activeFile.empty()) {
        context << "Arquivo ativo inferido: " << activeFile << "\n";
        const std::string ambiguity = inferActiveFileAmbiguityNote(history.empty() ? "" : history.back().text);
        if (!ambiguity.empty()) context << ambiguity << "\n";
        std::string content;
        if (activeFile == editorFilePath) {
            content = editorUsesPlainText ? editorPlainTextBuffer : codeEditor.GetText();
        } else {
            try {
                std::ifstream in(activeFile);
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
    prompt << "6. Se estiver propondo criar ou substituir um arquivo, prefira incluir um bloco ```json``` com {\"kind\":\"replace_file|create_file\",\"target\":\"...\",\"summary\":\"...\",\"content\":\"...\"}.\n";
    prompt << "7. Nao misture instrucoes de teste e explicacao dentro do campo content.\n";
    if (hasOpenProject && !currentProjectRoot.empty()) {
        prompt << "Projeto atual: " << currentProjectRoot << "\n";
    }
    const std::string inferredFile = inferActiveFileForGoal(history.empty() ? "" : history.back().text);
    if (!inferredFile.empty()) {
        prompt << "Arquivo ativo inferido/priorizado: " << inferredFile << "\n";
    }
    return prompt.str();
}

bool AgentUI::buildChangeProposalFromAssistantText(const std::string& text, ChangeProposal& proposal) const {
    proposal = {};
    proposal.kind = editorFilePath.empty() ? "create_file" : "replace_file";
    proposal.targetPath = !editorFilePath.empty() ? editorFilePath : selectedFile;

    for (const auto& block : extractJsonBlocks(text)) {
        try {
            auto j = nlohmann::json::parse(block);
            if (!j.is_object()) continue;
            if (!j.contains("content")) continue;
            proposal.kind = j.value("kind", proposal.kind);
            proposal.targetPath = j.value("target", proposal.targetPath);
            proposal.summary = j.value("summary", "Proposta estruturada recebida do agente.");
            proposal.content = j.value("content", std::string{});
            proposal.directlyApplicable = !proposal.content.empty() &&
                (proposal.kind == "replace_file" || proposal.kind == "create_file");
            if (proposal.directlyApplicable) return true;
        } catch (...) {
        }
    }

    AgentMessageSections sections = splitAgentMessage(text);
    std::string cleaned = trimLoose(sections.answer);
    cleaned = std::regex_replace(cleaned, std::regex("<thought>[\\s\\S]*?</thought>"), "");
    cleaned = std::regex_replace(cleaned, std::regex("TASK COMPLETE"), "");
    cleaned = trimLoose(cleaned);

    const auto codeBlocks = extractCodeBlocks(cleaned);
    if (codeBlocks.size() == 1) {
        proposal.content = codeBlocks.front();
        proposal.summary = "Bloco de codigo unico extraido da resposta do agente.";
        proposal.directlyApplicable = !proposal.content.empty();
        return proposal.directlyApplicable;
    }

    if (codeBlocks.empty() && !looksLikeMixedExplanatoryResponse(cleaned)) {
        proposal.content = cleaned;
        proposal.summary = "Resposta textual direta tratada como substituicao de arquivo.";
        proposal.directlyApplicable = !proposal.content.empty();
        return proposal.directlyApplicable;
    }

    proposal.summary = "Resposta ambigua: mistura explicacao e conteudo. Aplicacao automatica bloqueada.";
    proposal.directlyApplicable = false;
    return false;
}

std::string AgentUI::inferTaskMode(const std::string& goal) const {
    std::string lower = toLowerCopy(goal);
    if (containsAnyLower(lower, {"crie arquivo", "criar arquivo", "gere projeto", "scaffold", "rodar", "executar", "corrigir build", "refator", "refactor", "renomear arquivo", "mover arquivo"})) {
        return "MISSION";
    }
    if (!inferActiveFileForGoal(goal).empty() && containsAnyLower(lower, {"inclua", "incluir", "continue", "continuar", "revise este texto", "reescreva", "melhore o texto", "edite", "ajuste o documento", "insira", "corrija", "corrigir"})) {
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
    const std::string inferredActiveFile = inferActiveFileForGoal(inputBuf);
    const std::string ambiguityNote = inferActiveFileAmbiguityNote(inputBuf);
    ImGui::TextDisabled("Modo sugerido: %s", inferredMode == "MISSION" ? "Missao" : "Chat assistido");
    if (!inferredActiveFile.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("| Arquivo inferido: %s", fs::path(inferredActiveFile).filename().string().c_str());
    }
    if (!ambiguityNote.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), "%s", ambiguityNote.c_str());
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
                    ChangeProposal proposal;
                    const bool hasProposal = buildChangeProposalFromAssistantText(msg.text, proposal);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Propor Mudanca")) {
                        if (hasProposal) {
                            pendingChangeProposal = proposal;
                            const std::string currentText = editorUsesPlainText ? editorPlainTextBuffer : codeEditor.GetText();
                            pendingChangeDiff = buildSimpleDiffPreview(currentText, proposal.content);
                            std::snprintf(pendingChangeTargetBuf, sizeof(pendingChangeTargetBuf), "%s", pendingChangeProposal.targetPath.c_str());
                            changeProposalVisible = true;
                            thoughtStream = "Mudanca pronta para revisao.";
                        } else {
                            thoughtStream = proposal.summary.empty()
                                ? "Resposta sem proposta de mudanca segura."
                                : proposal.summary;
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
    
    if (!inferredActiveFile.empty()) {
        ImGui::TextDisabled("Arquivo em foco: %s", fs::path(inferredActiveFile).filename().string().c_str());
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
