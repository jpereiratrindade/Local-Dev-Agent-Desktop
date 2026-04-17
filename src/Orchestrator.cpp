#include "Orchestrator.hpp"
#include <regex>
#include <iostream>
#include <thread>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <mutex>
#include <condition_variable>

namespace fs = std::filesystem;

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

bool isWritingProfile(const std::string& profile) {
    return profile.rfind("writing-", 0) == 0;
}

int adjustStepsByProfile(int base, const std::string& profile, const std::string& reasoning) {
    int adjusted = base;
    if (profile == "general") adjusted = std::min(base, 6);
    else if (profile == "coding") adjusted = std::max(base, 10);
    else if (profile == "analysis") adjusted = std::min(base, 6);
    else if (profile == "review") adjusted = std::min(base, 5);
    else if (profile == "research") adjusted = std::min(base, 6);
    else if (profile == "research-project") adjusted = std::min(std::max(base, 5), 8);
    else if (profile == "writing-outline") adjusted = 3;
    else if (profile == "writing-argument") adjusted = 4;
    else if (profile == "writing-chapter") adjusted = 5;
    else if (profile == "writing-review") adjusted = 4;

    if (reasoning == "high" && isWritingProfile(profile)) adjusted = std::min(std::max(adjusted, 4), 6);
    return std::max(2, adjusted);
}

std::string profileInstructions(const std::string& profile) {
    if (profile == "coding") return "PERFIL COGNITIVO: CODING. Priorize implementacao, edicao concreta, impacto tecnico, validacao, correcao de build e proximos passos verificaveis. Nao reduza a tarefa a explicacao se puder agir com ferramentas.";
    if (profile == "analysis") return "PERFIL COGNITIVO: ANALYSIS. Priorize sintese baseada em evidencias, trade-offs e conclusoes proporcionais ao que foi observado.";
    if (profile == "review") return "PERFIL COGNITIVO: REVIEW. Priorize bugs, riscos, regressao comportamental e lacunas de teste.";
    if (profile == "research") return "PERFIL COGNITIVO: RESEARCH. Priorize mapa conceitual, hipoteses, referencias do workspace e perguntas de investigacao.";
    if (profile == "research-project") return "PERFIL COGNITIVO: RESEARCH PROJECT. Trate a tarefa como elaboracao de projeto de pesquisa. Priorize problema, justificativa, objetivos, hipoteses, metodologia, corpus, evidencias, referencias e entregaveis. Use estrutura para orientar, sem engessar a formulacao conceitual.";
    if (profile == "writing-outline") return "PERFIL COGNITIVO: WRITING OUTLINE. Trate a tarefa como construcao de estrutura argumentativa. Nao transforme a missao em exploracao mecanica do repositorio. Procure primeiro README, outline, chapters, notes, references ou arquivos explicitamente relacionados ao texto.";
    if (profile == "writing-argument") return "PERFIL COGNITIVO: WRITING ARGUMENT. Trate a tarefa como elaboracao conceitual. Extraia tese, tensoes teoricas, definicoes e encadeamento. Evite listar diretorios repetidamente apos o primeiro panorama.";
    if (profile == "writing-chapter") return "PERFIL COGNITIVO: WRITING CHAPTER. Gere prosa articulada, com secoes e paragrafos, nao apenas listas. Use o workspace para ancoragem, mas responda como coautor do texto.";
    if (profile == "writing-review") return "PERFIL COGNITIVO: WRITING REVIEW. Avalie clareza de tese, precisao conceitual, coesao, referencias e lacunas argumentativas.";
    return "PERFIL COGNITIVO: GENERAL. Mantenha equilibrio entre utilidade, evidencias e objetividade.";
}

bool hasCodeEvidence(const std::string& text) {
    static const std::regex evidenceRegex(
        "([A-Za-z0-9_./-]+\\.(cpp|cc|cxx|hpp|h|md|yml|yaml|txt|cmake|sh))|CMakeLists\\.txt");
    return std::regex_search(text, evidenceRegex);
}

std::string readOptionalFile(const fs::path& path) {
    std::ifstream in(path);
    if (!in) return "";
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::string loadProjectSkills(const std::string& workspaceRoot) {
    std::vector<fs::path> skillRoots = {
        fs::path(workspaceRoot) / ".agent" / "skills",
        fs::path(workspaceRoot) / "skills"
    };
    std::vector<fs::path> skillFiles;

    for (const auto& root : skillRoots) {
        try {
            if (!fs::exists(root) || !fs::is_directory(root)) continue;
            for (const auto& entry : fs::directory_iterator(root)) {
                if (!entry.is_regular_file()) continue;
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });
                if (ext == ".md" || ext == ".txt" || ext == ".json") {
                    skillFiles.push_back(entry.path());
                }
            }
        } catch (...) {
        }
    }

    std::sort(skillFiles.begin(), skillFiles.end());
    if (skillFiles.empty()) return "";

    std::stringstream out;
    out << "## SKILLS DO PROJETO\n";
    for (const auto& skillFile : skillFiles) {
        std::string content = readOptionalFile(skillFile);
        if (content.empty()) continue;
        out << "\n### " << skillFile.filename().string() << "\n";
        out << content << "\n";
    }
    return out.str();
}

std::string summarizeSkillNames(const std::string& workspaceRoot) {
    std::vector<fs::path> skillRoots = {
        fs::path(workspaceRoot) / ".agent" / "skills",
        fs::path(workspaceRoot) / "skills"
    };
    std::vector<std::string> names;
    for (const auto& root : skillRoots) {
        try {
            if (!fs::exists(root) || !fs::is_directory(root)) continue;
            for (const auto& entry : fs::directory_iterator(root)) {
                if (!entry.is_regular_file()) continue;
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });
                if (ext == ".md" || ext == ".txt" || ext == ".json") {
                    names.push_back(entry.path().stem().string());
                }
            }
        } catch (...) {
        }
    }
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    if (names.empty()) return "(nenhuma)";

    std::stringstream out;
    for (size_t i = 0; i < names.size(); ++i) {
        if (i) out << ", ";
        out << names[i];
    }
    return out.str();
}
} // namespace

Orchestrator::Orchestrator(agent::network::OllamaClient* client, const std::string& workspaceRoot)
    : ollama(client), workspaceRoot(workspaceRoot) {}

void Orchestrator::runMission(const std::string& goal, const std::string& mode, 
                             int maxSteps, MissionCallbacks callbacks,
                             const agent::network::OllamaOptions& options) {
    stopRequested = false;
    
    std::thread([this, goal, mode, maxSteps, callbacks, options]() {
        bool isFirstTurn = history.empty();
        std::string reasoning = extractTag(goal, "reasoning");
        std::string access = extractTag(goal, "access");
        std::string profile = extractTag(goal, "profile");
        std::string context = extractTag(goal, "context");
        if (profile.empty()) profile = "general";
        if (reasoning.empty()) reasoning = "medium";
        if (context.empty()) context = "workspace";
        std::string currentGoal = stripTags(goal);
        
        if (isFirstTurn) {
            std::string systemPrompt = buildSystemPrompt(mode, profile, reasoning);
            std::string availableSkills = summarizeSkillNames(workspaceRoot);
            history.push_back({"system", systemPrompt});
            history.push_back({"user", "META: " + currentGoal +
                                       "\n\nParâmetros: reasoning=" + reasoning +
                                       ", access=" + (access.empty() ? "workspace-write" : access) +
                                       ", profile=" + profile +
                                       ", context=" + context +
                                       "\nSkills disponíveis: " + availableSkills +
                                       "\n" + profileInstructions(profile) +
                                       "\nSe alguma skill combinar com a tarefa, use-a como guia flexivel de arranque, nao como trilho obrigatorio." +
                                       "\nPerfis definem postura cognitiva; skills sugerem fluxo; ferramentas e contexto permitem improviso responsavel." +
                                       "\nInicie pela menor ação verificável possível." +
                                       "\nRegra crítica de evidência: NÃO conclua ausência/lacuna de conteúdo sem 2 evidências diretas "
                                       "(ex.: list_dir + read_file, ou erro objetivo de acesso)." +
                                       ((reasoning == "high")
                                           ? "\nPara reasoning=high: colete no minimo 3 evidencias concretas em arquivos/caminhos antes da conclusao."
                                           : "")});
        } else {
            history.push_back({"user", currentGoal});
        }

        const bool highReasoning = (reasoning == "high");
        int effectiveMaxSteps = adjustStepsByReasoning(maxSteps, reasoning);
        effectiveMaxSteps = adjustStepsByProfile(effectiveMaxSteps, profile, reasoning);
        
        bool completed = false;
        int stagnationCount = 0;
        std::string lastObservation;
        int evidenceCount = 0;
        int noEvidenceSteps = 0;

        for (int step = 0; step < effectiveMaxSteps; ++step) {
            if (stopRequested) break;

            // Chamada de chat com streaming para feedback em tempo real
            std::string response;
            std::mutex streamMutex;
            std::condition_variable cv;
            bool streamDone = false;
            
            ollama->chatStream(history, 
                [&](const std::string& chunk) {
                    {
                        std::lock_guard<std::mutex> lock(streamMutex);
                        response += chunk;
                        
                        // Detecção simples de loop de repetição (hallucinação)
                        if (response.size() > 500) {
                            std::string tail = response.substr(response.size() - 200);
                            std::string pattern = tail.substr(tail.size() - 50);
                            int count = 0;
                            size_t pos = tail.find(pattern);
                            while (pos != std::string::npos) {
                                count++;
                                pos = tail.find(pattern, pos + 1);
                            }
                            if (count > 3) {
                                ollama->requestStop(); // Força parada do stream
                            }
                        }
                    }
                    if (callbacks.onMessageChunk) callbacks.onMessageChunk(chunk);
                },
                [&](bool ok, agent::network::OllamaStreamStats) {
                    {
                        std::lock_guard<std::mutex> lock(streamMutex);
                        streamDone = true;
                    }
                    cv.notify_one();
                },
                "", options);

            // Aguardar a conclusão do stream
            std::unique_lock<std::mutex> lock(streamMutex);
            cv.wait(lock, [&]{ return streamDone; });

            if (response.empty()) response = "Erro: Sem resposta do LLM.";
            
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
            "Com base no histórico, produza AGORA uma resposta final curta e estruturada.\n";
        if (isWritingProfile(profile)) {
            finalPrompt +=
                "Formato obrigatório:\n"
                "- Diagnóstico do material (2 a 4 bullets)\n"
                "- Tese ou direção argumentativa proposta (2 a 4 bullets)\n"
                "- Próximo bloco de escrita sugerido (1 parágrafo curto ou 3 bullets)\n"
                "- Fontes/lacunas de evidência (2 a 3 bullets)\n"
                "Regras:\n"
                "1) Não use tom de automação de repositório; responda como apoio editorial e conceitual.\n"
                "2) Se houver arquivos de texto/chapter/outline, cite-os explicitamente.\n"
                "3) Não invente referências. Se faltarem fontes, diga \"evidência insuficiente\".\n"
                "4) Evite repetir listagens de diretório ou narrar passos mecânicos.\n"
                "5) Sem chamar ferramentas, sem JSON de tool-call.\n";
        } else {
            finalPrompt +=
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
                "5) Se afirmar que verificou/criou/editou/executou algo, inclua evidência objetiva (comando + saída curta ou caminho exato).\n"
                "6) Proibido afirmar ausência/lacuna sem no mínimo 2 evidências diretas no histórico. "
                "Se não houver evidência suficiente, declare explicitamente \"evidência insuficiente\".\n";
        }
        if (profile == "coding") {
            finalPrompt += "7) Para profile=coding, inclua foco em implementacao, impacto e validacao tecnica.\n";
        } else if (profile == "review") {
            finalPrompt += "7) Para profile=review, priorize bugs/riscos/regressoes e lacunas de teste.\n";
        } else if (profile == "analysis") {
            finalPrompt += "7) Para profile=analysis, priorize síntese com evidências e trade-offs.\n";
        } else if (profile == "research-project") {
            finalPrompt += "7) Para profile=research-project, explicite problema, objetivos, metodo, corpus, referencias e lacunas de evidência.\n";
        } else if (profile == "writing-outline") {
            finalPrompt += "7) Para profile=writing-outline, entregue uma estrutura de argumento e nao apenas observacoes soltas.\n";
        } else if (profile == "writing-argument") {
            finalPrompt += "7) Para profile=writing-argument, explicite tese, tensoes e articulacao conceitual.\n";
        } else if (profile == "writing-chapter") {
            finalPrompt += "7) Para profile=writing-chapter, inclua um pequeno trecho de redacao exemplar em prosa.\n";
        } else if (profile == "writing-review") {
            finalPrompt += "7) Para profile=writing-review, priorize clareza de tese, coesao e precisao conceitual.\n";
        }
        if (highReasoning) {
            finalPrompt += "8) Para reasoning=high, inclua ao menos 3 evidências explícitas de código/arquivos.\n";
            finalPrompt += "9) Se evidências insuficientes, declare a limitação de forma objetiva.\n";
        }
        finalPrompt += "Contexto adicional: evidências detectadas no loop = " + std::to_string(evidenceCount) + ".";

        history.push_back({"user", finalPrompt});
        std::string finalResponse = ollama->chat(history);
        if (callbacks.onMessageChunk) callbacks.onMessageChunk("\n\n## Avaliação Final\n" + finalResponse);

        if (callbacks.onComplete) callbacks.onComplete(completed && !stopRequested);
    }).detach();
}

std::string Orchestrator::getModeInstructions(const std::string& mode, const std::string& profile) {
    if (mode == "AGENT") {
        if (isWritingProfile(profile)) {
            return "FOCO EM ELABORACAO. Use ferramentas para ler apenas o necessario do workspace textual e depois sintetize cedo. Nao trate a tarefa como varredura mecanica do repositorio.";
        }
        if (profile == "research-project") {
            return "FOCO EM CONCEPCAO DE PROJETO. Use ferramentas para mapear material relevante, mas preserve liberdade para formular problema, metodo e estrutura de pesquisa sem reduzir a tarefa a checklist.";
        }
        if (profile == "research") {
            return "FOCO EM INVESTIGACAO ESTRUTURADA. Use ferramentas para mapear evidencias do workspace e converter isso em hipoteses e direcoes.";
        }
        return "FOCO EM EXECUCAO. Use ferramentas para investigar o repo, editar arquivos e rodar comandos. Nao faca perguntas se puder encontrar a resposta via 'read_file'.";
    }
    if (mode == "DEBUG") return "FOCO EM CORRECAO. Analise logs, reproduza o erro e aplique patches. Teste apos cada mudanca.";
    if (mode == "REVIEW") return "FOCO EM ANALISE. Examine o codigo buscando bugs ou desalinhamentos com o ADR.";
    return "FOCO EM ASSISTÊNCIA TÉCNICA.";
}

std::string Orchestrator::buildSystemPrompt(const std::string& mode, const std::string& profile, const std::string& reasoning) {
    std::string specs = ToolRegistry::instance().getToolSpecs();
    std::string modeInstr = getModeInstructions(mode, profile);
    std::vector<std::string> approvedRoots = getNativeToolApprovedRoots();
    std::vector<std::string> approvedDomains = getNativeToolApprovedDomains();
    std::string rootsSummary = approvedRoots.empty() ? "(nenhuma)" : approvedRoots.front();
    std::string domainsSummary = approvedDomains.empty() ? "(nenhum)" : approvedDomains.front();
    for (size_t i = 1; i < approvedRoots.size(); ++i) rootsSummary += ", " + approvedRoots[i];
    for (size_t i = 1; i < approvedDomains.size(); ++i) domainsSummary += ", " + approvedDomains[i];
    
    std::string agentMd = readOptionalFile(fs::path(workspaceRoot) / "AGENT.md");
    if (agentMd.empty()) {
        agentMd = "Atue como um engenheiro de software autonomo focado em execucao técnica.";
    }
    std::string contextMd = readOptionalFile(fs::path(workspaceRoot) / "PROJECT_CONTEXT.md");
    if (contextMd.empty()) {
        contextMd = "Workspace local sem manifestos especificos. Explore para entender.";
    }

    return "## AGENT NATIVO CODEX\n\n" +
           agentMd + "\n\n" +
           "## CONTEXTO DO PROJETO\n" + contextMd + "\n\n" +
           "## MODO DE OPERACAO: " + mode + "\n" + modeInstr + "\n\n" +
           "## FERRAMENTAS DISPONÍVEIS\n" + specs + "\n\n" +
           "## REGRAS DE OURO:\n" +
           "1. Pense antes de agir com <thought>...</thought>.\n" +
           "2. Use blocos ```json ... ``` para ferramentas: {\"tool\": \"...\", \"args\": {...}}.\n" +
           "3. Skills disponiveis (use 'read_file' se precisar de detalhes): " + summarizeSkillNames(workspaceRoot) + "\n" +
           "4. Se a tarefa estiver pronta, finalize obrigatoriamente com 'TASK COMPLETE'.\n" +
           "5. Nao invente informacoes. Se nao souber, use as ferramentas para descobrir.";
}

} // namespace agent::core
