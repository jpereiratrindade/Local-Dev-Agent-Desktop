#include "Orchestrator.hpp"
#include <regex>
#include <iostream>
#include <thread>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace agent::core {
namespace {
std::string extractTag(const std::string& input, const std::string& key) {
    std::regex tagRegex("\\[" + key + "=([^\\]]+)\\]");
    std::smatch m;
    if (std::regex_search(input, m, tagRegex)) return m[1].str();
    return "";
}

std::string stripTags(const std::string& input) {
    return std::regex_replace(input, std::regex("\\[[^\\]]+\\]"), "");
}

int adjustStepsByReasoning(int base, const std::string& reasoning) {
    if (reasoning == "low") return std::max(2, std::min(base, 4));
    if (reasoning == "high") return std::max(base, 12);
    return std::max(3, std::min(base, 8));
}

bool hasCodeEvidence(const std::string& text) {
    static const std::regex evidenceRegex(
        "([A-Za-z0-9_./-]+\\.(cpp|cc|cxx|hpp|h|md|yml|yaml|txt|cmake|sh))|CMakeLists\\.txt");
    return std::regex_search(text, evidenceRegex);
}
} // namespace

Orchestrator::Orchestrator(agent::network::OllamaClient* client, const std::string& workspaceRoot)
    : ollama(client), workspaceRoot(workspaceRoot) {}

void Orchestrator::runMission(const std::string& goal, const std::string& mode, 
                             int maxSteps, MissionCallbacks callbacks) {
    stopRequested = false;
    history.clear();
    
    std::thread([this, goal, mode, maxSteps, callbacks]() {
        std::string reasoning = extractTag(goal, "reasoning");
        std::string access = extractTag(goal, "access");
        std::string profile = extractTag(goal, "profile");
        std::string currentGoal = stripTags(goal);
        const bool highReasoning = (reasoning == "high");
        int effectiveMaxSteps = adjustStepsByReasoning(maxSteps, reasoning.empty() ? "medium" : reasoning);
        std::string systemPrompt = buildSystemPrompt(mode);
        history.push_back({"system", systemPrompt});
        history.push_back({"user", "META: " + currentGoal +
                                   "\n\nParâmetros: reasoning=" + (reasoning.empty() ? "medium" : reasoning) +
                                   ", access=" + (access.empty() ? "workspace-write" : access) +
                                   ", profile=" + (profile.empty() ? "general" : profile) +
                                   "\nInicie pela menor ação verificável possível." +
                                   (highReasoning
                                       ? "\nPara reasoning=high: colete no minimo 3 evidencias concretas em arquivos/caminhos antes da conclusao."
                                       : "")});
        bool completed = false;
        int stagnationCount = 0;
        std::string lastObservation;
        int evidenceCount = 0;
        int noEvidenceSteps = 0;

        for (int step = 0; step < effectiveMaxSteps; ++step) {
            if (stopRequested) break;

            if (callbacks.onAction) callbacks.onAction("PASSO " + std::to_string(step + 1) + " / " + std::to_string(effectiveMaxSteps));
            std::string response = ollama->chat(history);
            if (hasCodeEvidence(response)) {
                evidenceCount++;
                noEvidenceSteps = 0;
            } else {
                noEvidenceSteps++;
            }
            
            // 1. Extrair Pensamento
            std::smatch thoughtMatch;
            std::regex thoughtRegex("<thought>([\\s\\S]*?)</thought>");
            if (std::regex_search(response, thoughtMatch, thoughtRegex)) {
                if (callbacks.onThought) callbacks.onThought(thoughtMatch[1].str());
            }

            // 2. Extrair Ação (Tool Call)
            std::smatch toolMatch;
            std::regex toolRegex("```json\\s*([\\s\\S]*?)\\s*```");
            std::string observation = "";
            bool taskComplete = (response.find("TASK COMPLETE") != std::string::npos);

            if (std::regex_search(response, toolMatch, toolRegex)) {
                try {
                    auto toolJson = nlohmann::json::parse(toolMatch[1].str());
                    std::string toolName = toolJson.value("tool", "");
                    nlohmann::json args = toolJson.value("args", nlohmann::json::object());

                    if (callbacks.onAction) callbacks.onAction("Executando: " + toolName);
                    observation = ToolRegistry::instance().dispatch(toolName, args);
                    if (callbacks.onObservation) callbacks.onObservation(observation);
                    if (observation == lastObservation) stagnationCount++;
                    else stagnationCount = 0;
                    lastObservation = observation;
                    if (hasCodeEvidence(observation)) {
                        evidenceCount++;
                        noEvidenceSteps = 0;
                    }
                    
                } catch (const std::exception& e) {
                    observation = "Erro ao processar JSON da ferramenta: " + std::string(e.what());
                }
            }

            // 3. Atualizar Histórico
            history.push_back({"assistant", response});
            if (!observation.empty()) {
                history.push_back({"user", "OBSERVAÇÃO: " + observation});
                currentGoal = "OBSERVAÇÃO: " + observation;
            } else if (!taskComplete) {
                currentGoal = "Continue com a próxima ação objetiva.";
                history.push_back({"user", currentGoal});
            }

            if (callbacks.onMessageChunk) callbacks.onMessageChunk(response);

            if (taskComplete) {
                if (callbacks.onAction) callbacks.onAction("MISSÃO CONCLUÍDA COM SUCESSO.");
                completed = true;
                break;
            }

            if (stagnationCount >= 2) {
                if (callbacks.onAction) callbacks.onAction("Convergência detectada (sem novidade em observações).");
                break;
            }
            if (highReasoning && noEvidenceSteps >= 2 && step >= 2) {
                if (callbacks.onAction) callbacks.onAction("Convergência de evidências detectada (sem novas evidências por 2 passos).");
                break;
            }
        }

        // Sempre fecha com uma síntese curta e estruturada, sem ferramentas.
        std::string finalPrompt =
            "Com base no histórico, produza AGORA uma resposta final curta e estruturada.\n"
            "Formato obrigatório:\n"
            "- Forças (3 a 5 bullets)\n"
            "- Riscos (3 a 5 bullets)\n"
            "- Lacunas (2 a 4 bullets)\n"
            "- Próximas ações (3 bullets)\n"
            "Regras:\n"
            "1) Máximo total de 15 bullets.\n"
            "2) Cite evidências com caminhos de arquivo sempre que possível.\n"
            "3) Não repetir texto já dito.\n"
            "4) Sem chamar ferramentas, sem JSON de tool-call.\n"
            "5) Se afirmar que verificou/criou/editou/executou algo, inclua evidência objetiva (comando + saída curta ou caminho exato).\n";
        if (profile == "coding") {
            finalPrompt += "6) Para profile=coding, inclua foco em implementacao, impacto e validacao tecnica.\n";
        } else if (profile == "review") {
            finalPrompt += "6) Para profile=review, priorize bugs/riscos/regressoes e lacunas de teste.\n";
        } else if (profile == "analysis") {
            finalPrompt += "6) Para profile=analysis, priorize síntese com evidências e trade-offs.\n";
        }
        if (highReasoning) {
            finalPrompt += "7) Para reasoning=high, inclua ao menos 3 evidências explícitas de código/arquivos.\n";
            finalPrompt += "8) Se evidências insuficientes, declare a limitação de forma objetiva.\n";
        }
        finalPrompt += "Contexto adicional: evidências detectadas no loop = " + std::to_string(evidenceCount) + ".";

        history.push_back({"user", finalPrompt});
        std::string finalResponse = ollama->chat(history);
        if (callbacks.onMessageChunk) callbacks.onMessageChunk("\n\n## Avaliação Final\n" + finalResponse);

        if (callbacks.onComplete) callbacks.onComplete(completed && !stopRequested);
    }).detach();
}

std::string Orchestrator::getModeInstructions(const std::string& mode) {
    if (mode == "AGENT") return "FOCO EM EXECUÇÃO. Use ferramentas para investigar o repo, editar arquivos e rodar comandos. Não faça perguntas se puder encontrar a resposta via 'read_file'.";
    if (mode == "DEBUG") return "FOCO EM CORREÇÃO. Analise logs, reproduza o erro e aplique patches. Teste após cada mudança.";
    if (mode == "REVIEW") return "FOCO EM ANÁLISE. Examine o código buscando bugs ou desalinhamentos com o ADR.";
    return "FOCO EM ASSISTÊNCIA TÉCNICA.";
}

std::string Orchestrator::buildSystemPrompt(const std::string& mode) {
    std::string specs = ToolRegistry::instance().getToolSpecs();
    std::string modeInstr = getModeInstructions(mode);
    
    // Tenta carregar contexto adicional se existir
    std::string agentMd = "";
    std::string contextMd = "";
    
    std::ifstream f1(workspaceRoot + "/AGENT.md");
    if (f1) { std::stringstream ss; ss << f1.rdbuf(); agentMd = ss.str(); }
    
    std::ifstream f2(workspaceRoot + "/PROJECT_CONTEXT.md");
    if (f2) { std::stringstream ss; ss << f2.rdbuf(); contextMd = ss.str(); }

    return "## AGENT NATIVO CODEX - PROTOCOLO DE AUTONOMIA\n\n" +
           agentMd + "\n\n" +
           "## CONTEXTO DO PROJETO\n" + contextMd + "\n\n" +
           "## MODO DE OPERAÇÃO: " + mode + "\n" + modeInstr + "\n\n" +
           "## FERRAMENTAS DISPONÍVEIS\n" + specs + "\n" +
           "## REGRAS DE OURO:\n" +
           "1. Pense antes de agir com <thought>...</thought>.\n" +
           "2. Para agir, use OBRIGATORIAMENTE o bloco ```json ... ``` com {\"tool\": \"...\", \"args\": {...}}.\n" +
           "3. PRIORIZE AÇÃO SOBRE EXPLICAÇÃO. Se a meta é 'analisar', comece com 'list_dir'.\n" +
           "4. Se a tarefa estiver pronta, finalize com 'TASK COMPLETE'.";
}

} // namespace agent::core
