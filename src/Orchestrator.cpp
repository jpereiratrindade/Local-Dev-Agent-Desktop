#include "Orchestrator.hpp"
#include "AgentSpec.hpp"
#include "PromptComposer.hpp"
#include "TurnDiffTracker.hpp"
#include <iostream>
#include <thread>
#include <filesystem>
#include <mutex>
#include <condition_variable>
#include <numeric>
#include <future>
#include <cctype>

namespace fs = std::filesystem;

namespace agent::core {

namespace {

enum class MissionIntent {
    Informational,
    CreateOrEdit,
    OtherOperational,
};

std::string toLowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool containsAny(const std::string& haystack, const std::initializer_list<const char*> needles) {
    for (const char* needle : needles) {
        if (haystack.find(needle) != std::string::npos) return true;
    }
    return false;
}

MissionIntent inferMissionIntent(const std::string& goal) {
    const std::string lower = toLowerCopy(goal);
    if (containsAny(lower, {"criar", "crie", "gerar", "gere", "editar", "edite", "alterar", "altere",
                            "modificar", "modifique", "mkdir", "diretorio", "diretório"})) {
        return MissionIntent::CreateOrEdit;
    }
    if (containsAny(lower, {"rodar", "execute", "executar", "compilar", "build", "testar", "teste"})) {
        return MissionIntent::OtherOperational;
    }
    return MissionIntent::Informational;
}

bool toolObservationSucceeded(const std::string& observation) {
    return observation.rfind("Sucesso:", 0) == 0;
}

bool verifyMutationTarget(const std::string& path) {
    if (path.empty()) return false;
    nlohmann::json args = {{"path", path}};
    std::string readBack = ToolRegistry::instance().dispatch("read_file", args);
    return readBack.rfind("Erro:", 0) != 0 && readBack.rfind("ERRO", 0) != 0;
}

bool verifyDirectoryTarget(const std::string& workspaceRoot, const std::string& path) {
    if (path.empty()) return false;
    fs::path target(path);
    if (target.is_relative()) target = fs::path(workspaceRoot) / target;
    std::error_code ec;
    return fs::exists(target, ec) && fs::is_directory(target, ec);
}

} // namespace

// P2.1: Heurística de tokens — 1 token ≈ 4 chars (conservador para português/código)
static size_t estimateTokens(const std::string& text) {
    return (text.size() + 3) / 4;
}

static size_t historyCharCount(const std::vector<agent::network::Message>& history) {
    size_t total = 0;
    for (const auto& m : history) total += m.content.size() + m.role.size();
    return total;
}

// P1.1: Compressão rolling window do histórico.
// Mantém: system[0] + user goal + últimas kKeepTailMsgs mensagens.
// Injeta nota de compressão no início para que o modelo saiba.
static void trimHistoryToFit(std::vector<agent::network::Message>& history,
                              size_t maxChars = 48000,
                              size_t kKeepTailMsgs = 6) {
    if (historyCharCount(history) <= maxChars) return;

    // Identificar: system (idx 0), goal user (idx <=2), tail
    size_t systemIdx  = history.size(); // sentinela
    size_t goalIdx    = history.size();
    for (size_t i = 0; i < history.size(); ++i) {
        if (history[i].role == "system" && systemIdx == history.size()) { systemIdx = i; }
        if (history[i].role == "user"   && goalIdx   == history.size()) { goalIdx   = i; }
    }

    size_t tailStart = history.size() > kKeepTailMsgs
                       ? history.size() - kKeepTailMsgs
                       : 0;

    std::vector<agent::network::Message> trimmed;
    for (size_t i = 0; i < history.size(); ++i) {
        if (i == systemIdx || i == goalIdx || i >= tailStart) {
            trimmed.push_back(history[i]);
        }
    }

    // Injetar nota de compressão após o system prompt
    agent::network::Message note;
    note.role    = "system";
    note.content = "[Nota interna: parte do histórico desta sessão foi comprimida "
                   "para caber no contexto. O objetivo original e os últimos turns "
                   "estão preservados. Continue a partir daqui.]";
    size_t insertPos = (systemIdx < trimmed.size()) ? systemIdx + 1 : 0;
    trimmed.insert(trimmed.begin() + static_cast<long>(insertPos), note);

    history = std::move(trimmed);
}

Orchestrator::Orchestrator(agent::network::OllamaClient* client, const std::string& workspaceRoot)
    : ollama(client), workspaceRoot(workspaceRoot) {}

void Orchestrator::setWorkspaceRoot(const std::string& root) {
    if (root.empty()) return;
    try {
        workspaceRoot = fs::weakly_canonical(fs::path(root)).string();
    } catch (...) {
        workspaceRoot = root;
    }
}

void Orchestrator::runMission(const std::vector<agent::network::Message>& contextHistory,
                              const std::string& goal, const MissionConfig& config,
                              int maxSteps, MissionCallbacks callbacks,
                              const agent::network::OllamaOptions& options) {
    stopRequested = false;
    
    std::thread([this, contextHistory, goal, config, maxSteps, callbacks, options]() {
        std::vector<agent::network::Message> threadHistory = contextHistory;
        const std::string mode = config.mode.empty() ? "MISSION" : config.mode;
        const std::string toolProfile = config.agentSpec.toolProfile.empty() ? "workspace" : config.agentSpec.toolProfile;
        const MissionIntent missionIntent = inferMissionIntent(goal);
        
        bool hasSystem = false;
        for (const auto& msg : threadHistory) {
            if (msg.role == "system") { hasSystem = true; break; }
        }
        
        if (!hasSystem) {
            std::string systemPrompt = buildSystemPrompt(config);
            threadHistory.insert(threadHistory.begin(), {"system", systemPrompt});
        }
        
        threadHistory.push_back({"user", goal});

        bool completed = false;
        bool hadToolCall = false;
        bool hadMutatingToolSuccess = false;
        bool hadVerificationSuccess = false;
        TurnDiffTracker diffTracker(workspaceRoot);
        
        int step = 0;
        for (; step < maxSteps; ++step) {
            if (stopRequested) break;

            std::string response;
            std::string reasoningOutput;
            nlohmann::json assistantToolCalls = nlohmann::json::array();
            std::vector<agent::network::ToolCall> nativeToolCalls;

            // P1.1: Comprimir histórico antes de enviar, se necessário
            trimHistoryToFit(threadHistory);

            agent::network::ChatTurnResult turn = ollama->chatWithTools(
                threadHistory,
                ToolRegistry::instance().getOpenAiToolSpecsForProfile(toolProfile),
                "",
                options);
                
            response = turn.content;
            reasoningOutput = turn.reasoning;
            assistantToolCalls = turn.rawToolCalls;
            nativeToolCalls = std::move(turn.toolCalls);

            if (!reasoningOutput.empty() && callbacks.onThought) callbacks.onThought(reasoningOutput);
            if (!response.empty() && callbacks.onMessageChunk) callbacks.onMessageChunk(response);

            if (assistantToolCalls.is_array() && !assistantToolCalls.empty()) {
                threadHistory.push_back({"assistant", response, "", "", assistantToolCalls});
            } else {
                threadHistory.push_back({"assistant", response});
            }

            if (!nativeToolCalls.empty()) {
                hadToolCall = true;
                // P3.4: Verificar se TODAS as tools são read-only para rodar em paralelo
                bool allReadOnly = true;
                for (const auto& toolCall : nativeToolCalls) {
                    if (ToolRegistry::instance().toolMutatesWorkspace(toolCall.name)) {
                        allReadOnly = false;
                        break;
                    }
                }

                if (allReadOnly && nativeToolCalls.size() > 1) {
                    if (callbacks.onAction) callbacks.onAction("Executando " + std::to_string(nativeToolCalls.size()) + " ferramentas de leitura em paralelo...");
                    
                    std::vector<std::future<std::string>> futures;
                    for (const auto& toolCall : nativeToolCalls) {
                        futures.push_back(std::async(std::launch::async, [toolCall]() {
                            return ToolRegistry::instance().dispatch(toolCall.name, toolCall.arguments);
                        }));
                    }
                    
                    for (size_t i = 0; i < nativeToolCalls.size(); ++i) {
                        std::string localObservation = futures[i].get();
                        constexpr size_t kMaxObsChars = 8000;
                        if (localObservation.size() > kMaxObsChars) {
                            localObservation = localObservation.substr(0, kMaxObsChars)
                                + "\n...[observação truncada — use read_file_slice para partes específicas]...";
                        }
                        if (callbacks.onObservation) callbacks.onObservation(localObservation);
                        threadHistory.push_back({"tool", localObservation, nativeToolCalls[i].name, nativeToolCalls[i].id});
                    }
                } else {
                    // Execução sequencial padrão (com segurança para mutações)
                    for (const auto& toolCall : nativeToolCalls) {
                        if (callbacks.onAction) {
                            callbacks.onAction(std::string("Executando: ") + toolCall.name);
                        }

                        if (ToolRegistry::instance().toolMutatesWorkspace(toolCall.name)) {
                            std::string target = toolCall.arguments.value("path", toolCall.arguments.value("target", ""));
                            if (!target.empty()) {
                                diffTracker.snapshotBefore(target);
                            }
                        }

                        // P0.4: Approval gate para operações destrutivas (delete_path)
                        if (toolCall.name == "delete_path" && callbacks.onApprovalRequired) {
                            bool approved = callbacks.onApprovalRequired(toolCall.name, toolCall.arguments);
                            if (!approved) {
                                std::string cancelObs = "Operação delete_path cancelada pelo usuário. Nenhum arquivo foi removido.";
                                if (callbacks.onObservation) callbacks.onObservation(cancelObs);
                                threadHistory.push_back({"tool", cancelObs, toolCall.name, toolCall.id});
                                continue;
                            }
                        }

                        std::string localObservation = ToolRegistry::instance().dispatch(toolCall.name, toolCall.arguments);

                        // P0.3: Truncar observações grandes para não estourar o contexto
                        constexpr size_t kMaxObsChars = 8000;
                        if (localObservation.size() > kMaxObsChars) {
                            localObservation = localObservation.substr(0, kMaxObsChars)
                                + "\n...[observação truncada — use read_file_slice para partes específicas]...";
                        }

                        if (ToolRegistry::instance().toolMutatesWorkspace(toolCall.name)) {
                            std::string target = toolCall.arguments.value("path", toolCall.arguments.value("target", ""));
                            if (!target.empty()) {
                                diffTracker.recordMutation(target);
                                if (toolObservationSucceeded(localObservation)) {
                                    hadMutatingToolSuccess = true;
                                    if (toolCall.name == "write_file" || toolCall.name == "apply_patch") {
                                        hadVerificationSuccess = verifyMutationTarget(target);
                                    } else if (toolCall.name == "make_dir") {
                                        hadVerificationSuccess = verifyDirectoryTarget(workspaceRoot, target);
                                    }
                                }
                            }
                        }

                        if (callbacks.onObservation) callbacks.onObservation(localObservation);

                        threadHistory.push_back({"tool", localObservation, toolCall.name, toolCall.id});
                    }
                }
            } else {
                bool modelSignaledStop = (turn.finishReason == "stop" || turn.finishReason == "length"
                                          || turn.finishReason == "end");
                bool explicitComplete  = response.find("TASK COMPLETE") != std::string::npos;
                if (modelSignaledStop || explicitComplete) {
                    if (missionIntent == MissionIntent::Informational) {
                        completed = true;
                    } else {
                        completed = hadMutatingToolSuccess || hadVerificationSuccess;
                    }
                    break;
                }
                if (step > 0 && response.empty()) {
                    completed = (missionIntent == MissionIntent::Informational)
                        ? true
                        : (hadMutatingToolSuccess || hadVerificationSuccess);
                    break;
                }
            }
        }

        // P3.1: Self-reflection step — verifica se o objetivo foi atingido antes do resumo
        if (completed && !stopRequested && step > 0) {
            std::vector<agent::network::Message> reflectHistory = threadHistory;
            reflectHistory.push_back({"user",
                "Em até 3 linhas objetivas: O objetivo original foi atingido? "
                "Quais etapas ficaram pendentes, se alguma?"
            });
            std::string reflection = ollama->chat(reflectHistory);
            if (!reflection.empty() && callbacks.onMessageChunk) {
                callbacks.onMessageChunk("\n\n**Autoverificação:**\n" + reflection + "\n");
            }
        }

        std::string finalPrompt = "Forneça um resumo muito rápido do que foi feito (máximo 3 linhas). Se não usou ferramentas, apenas responda à última pergunta.";
        if (diffTracker.summarize().length() > 10) {
            finalPrompt += "\n" + diffTracker.summarize();
        }

        if (callbacks.onMissionComplete) {
            callbacks.onMissionComplete(threadHistory);
        }

        std::vector<agent::network::Message> summaryHistory = threadHistory;
        summaryHistory.push_back({"user", finalPrompt});

        // P3.3: Verificar stopRequested antes de chamar callbacks finais
        if (!stopRequested) {
            std::string finalResponse = ollama->chat(summaryHistory);
            if (callbacks.onMessageChunk) callbacks.onMessageChunk("\n\n## Resumo Final\n" + finalResponse);
        }

        if (callbacks.onComplete) callbacks.onComplete(completed && !stopRequested);
    }).detach();
}

std::string Orchestrator::getModeInstructions(const std::string& mode, const std::string& profile) const {
    return ""; 
}

std::string Orchestrator::buildSystemPrompt(const MissionConfig& config) const {
    agent::core::PromptContext ctx;
    ctx.agentSpec = config.agentSpec;
    ctx.mode = config.mode;
    ctx.reasoning = config.reasoning;
    ctx.workspaceRoot = workspaceRoot;
    ctx.governance = projectGovernance;
    ctx.modeInstructions = getModeInstructions(config.mode, config.agentSpec.role);
    ctx.toolSpecs = ToolRegistry::instance().getToolSpecsForProfile(config.agentSpec.toolProfile);
    return agent::core::buildSharedAgentPrompt(ctx);
}

} // namespace agent::core
