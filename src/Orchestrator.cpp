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
    if (profile == "writing-chapter") return "PERFIL COGNITIVO: WRITING CHAPTER. Gere prosa articulada, com secoes e paragrafos, nao apenas listas. Use o workspace para ancoragem, mas responda como coautor do texto. Se houver arquivo ativo ou capitulo existente, continue e edite esse artefato antes de expandir a exploracao.";
    if (profile == "writing-review") return "PERFIL COGNITIVO: WRITING REVIEW. Avalie clareza de tese, precisao conceitual, coesao, referencias e lacunas argumentativas.";
    return "PERFIL COGNITIVO: GENERAL. Mantenha equilibrio entre utilidade, evidencias e objetividade.";
}

bool hasCodeEvidence(const std::string& text) {
    static const std::regex evidenceRegex(
        "([A-Za-z0-9_./-]+\\.(cpp|cc|cxx|hpp|h|md|yml|yaml|txt|cmake|sh))|CMakeLists\\.txt");
    return std::regex_search(text, evidenceRegex);
}

bool translateChangeEnvelopeToTool(const nlohmann::json& envelope, std::string& toolName, nlohmann::json& args) {
    if (!toolName.empty()) return false;
    if (!envelope.is_object() || !envelope.contains("kind")) return false;

    const std::string kind = envelope.value("kind", "");
    const std::string target = envelope.value("target", envelope.value("path", ""));
    if (target.empty()) return false;

    if (kind == "create_file" || kind == "replace_file") {
        std::string content = envelope.value("content", "");
        fs::path targetPath(target);
        if (targetPath.filename() == "Makefile") {
            size_t pos = 0;
            while ((pos = content.find("\\n", pos)) != std::string::npos) {
                content.replace(pos, 2, "\n");
                pos += 1;
            }
            pos = 0;
            while ((pos = content.find("\\t", pos)) != std::string::npos) {
                content.replace(pos, 2, "\t");
                pos += 1;
            }
        }
        toolName = "write_file";
        args = {
            {"path", target},
            {"content", content}
        };
        return true;
    }

    return false;
}

bool shouldSuppressRepeatedTool(const std::string& toolName) {
    return toolName == "list_dir" ||
           toolName == "read_file" ||
           toolName == "read_file_slice" ||
           toolName == "grep_search" ||
           toolName == "search_library" ||
           toolName == "rag_cache_status";
}

bool shouldPrioritizeActiveFile(const std::string& goal, const std::string& profile) {
    std::string lower = goal;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (!(profile == "coding" || isWritingProfile(profile) || profile == "research-project")) return false;
    return lower.find("arquivo ativo") != std::string::npos ||
           lower.find("contexto ativo") != std::string::npos ||
           lower.find("continue") != std::string::npos ||
           lower.find("continuar") != std::string::npos ||
           lower.find("inclua") != std::string::npos ||
           lower.find("incluir") != std::string::npos ||
           lower.find("reescreva") != std::string::npos ||
           lower.find("revise") != std::string::npos ||
           lower.find("edite") != std::string::npos;
}

std::string readOptionalFile(const fs::path& path) {
    std::ifstream in(path);
    if (!in) return "";
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
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

std::string truncateForPrompt(const std::string& text, std::size_t limit) {
    if (text.size() <= limit) return text;
    return text.substr(0, limit) + "\n...[conteudo truncado para preservar contexto]...";
}

struct ContextArtifact {
    std::string label;
    fs::path path;
    std::string content;
};

std::vector<ContextArtifact> discoverContextArtifacts(const std::string& workspaceRoot) {
    const fs::path root(workspaceRoot);
    const std::vector<std::pair<std::string, std::vector<fs::path>>> candidates = {
        {"governance", {root / "AGENT.md", root / "AGENTS.md"}},
        {"project-context", {root / "PROJECT_CONTEXT.md"}},
        {"stack-context", {root / "stack-context.md", root / ".agent" / "context" / "stack-context.md"}},
        {"coding-standards", {root / "coding-standards.md", root / ".agent" / "context" / "coding-standards.md"}},
        {"architecture-context", {root / "architecture-context.md", root / ".agent" / "context" / "architecture-context.md"}},
        {"decisions-log", {root / "memory" / "decisions-log.md", root / ".agent" / "memory" / "decisions-log.md"}}
    };

    std::vector<ContextArtifact> artifacts;
    for (const auto& candidate : candidates) {
        for (const auto& path : candidate.second) {
            std::string content = readOptionalFile(path);
            if (content.empty()) continue;
            artifacts.push_back({candidate.first, path, content});
            break;
        }
    }
    return artifacts;
}

std::string summarizeContextArtifactNames(const std::string& workspaceRoot) {
    auto artifacts = discoverContextArtifacts(workspaceRoot);
    if (artifacts.empty()) return "(nenhum)";

    std::stringstream out;
    for (size_t i = 0; i < artifacts.size(); ++i) {
        if (i) out << ", ";
        out << artifacts[i].label << " -> " << artifacts[i].path.lexically_relative(fs::path(workspaceRoot)).string();
    }
    return out.str();
}

std::string buildDistributedContextPrompt(const std::string& workspaceRoot) {
    auto artifacts = discoverContextArtifacts(workspaceRoot);
    std::stringstream out;

    std::string governance = "Atue como um engenheiro de software autonomo focado em execucao técnica.";
    std::string projectContext = "Workspace local sem manifestos especificos. Explore para entender.";

    for (const auto& artifact : artifacts) {
        if (artifact.label == "governance") governance = artifact.content;
        if (artifact.label == "project-context") projectContext = artifact.content;
    }

    out << governance << "\n\n";
    out << "## CONTEXTO DO PROJETO\n" << projectContext << "\n\n";

    bool hasDistributedArtifacts = false;
    for (const auto& artifact : artifacts) {
        if (artifact.label == "governance" || artifact.label == "project-context") continue;
        hasDistributedArtifacts = true;
        out << "## " << artifact.label << "\n";
        out << "Arquivo: " << artifact.path.lexically_relative(fs::path(workspaceRoot)).string() << "\n";
        out << truncateForPrompt(artifact.content, artifact.label == "decisions-log" ? 1200 : 1800) << "\n\n";
    }

    if (!hasDistributedArtifacts) {
        out << "## CONTEXTO DISTRIBUIDO\nNenhum arquivo adicional de stack/coding/architecture/memory foi encontrado.\n\n";
    }

    return out.str();
}
} // namespace

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
        const bool activeFilePriority = shouldPrioritizeActiveFile(currentGoal, profile);
        
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
                                       "\nContextos disponíveis: " + summarizeContextArtifactNames(workspaceRoot) +
                                       "\n" + profileInstructions(profile) +
                                       "\nSe alguma skill combinar com a tarefa, use-a como guia flexivel de arranque, nao como trilho obrigatorio." +
                                       "\nSe algum contexto distribuido combinar com a tarefa, use 'read_file' para aprofundar apenas no arquivo necessario." +
                                       "\nPerfis definem postura cognitiva; skills sugerem fluxo; ferramentas e contexto permitem improviso responsavel." +
                                       "\nInicie pela menor ação verificável possível." +
                                       (activeFilePriority
                                            ? "\nPrioridade adicional: ha contexto/arquivo ativo relevante. Antes de explorar o repositorio, leia e trabalhe primeiro sobre esse artefato."
                                            : "") +
                                       (activeFilePriority
                                            ? "\nSe a melhor saida for uma proposta de alteracao de arquivo, prefira um bloco ```json``` com kind, target, summary e content em vez de prosa misturada."
                                            : "") +
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
        std::string lastToolSignature;
        int repeatedToolSignatureCount = 0;
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
                [&](bool ok, agent::network::OllamaStreamStats stats) {
                    {
                        std::lock_guard<std::mutex> lock(streamMutex);
                        streamDone = true;
                    }
                    if (callbacks.onStreamStats) callbacks.onStreamStats(stats);
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

            for (std::sregex_iterator it(response.begin(), response.end(), toolRegex), end; it != end; ++it) {
                try {
                    auto toolJson = nlohmann::json::parse((*it)[1].str());
                    std::string toolName = toolJson.value("tool", "");
                    nlohmann::json args = toolJson.value("args", nlohmann::json::object());
                    const bool translatedEnvelope = translateChangeEnvelopeToTool(toolJson, toolName, args);
                    std::string toolSignature = toolName + ":" + args.dump();

                    if (callbacks.onAction) {
                        callbacks.onAction(std::string("Executando: ") + toolName +
                                           (translatedEnvelope ? " (envelope de mudança)" : ""));
                    }

                    if (!lastToolSignature.empty() && toolSignature == lastToolSignature) {
                        repeatedToolSignatureCount++;
                    } else {
                        repeatedToolSignatureCount = 0;
                    }
                    lastToolSignature = toolSignature;

                    std::string localObservation;
                    if (toolName.empty()) {
                        localObservation =
                            "JSON recebido não é uma tool-call executável. Use {\"tool\":\"write_file\","
                            "\"args\":{\"path\":\"...\",\"content\":\"...\"}} ou um envelope create_file/replace_file completo.";
                    } else if (repeatedToolSignatureCount >= 1 && shouldSuppressRepeatedTool(toolName)) {
                        localObservation =
                            "Repetição detectada: a mesma tool-call foi solicitada novamente sem avanço. "
                            "Não repita inspeção idêntica. Prossiga para ação concreta de execução "
                            "(ex.: make_dir/write_file/apply_patch/run_command) para materializar o objetivo.";
                    } else {
                        localObservation = ToolRegistry::instance().dispatch(toolName, args);
                    }
                    observation += localObservation + "\n";

                    if (callbacks.onObservation) callbacks.onObservation(localObservation);
                    if (localObservation == lastObservation) stagnationCount++;
                    else stagnationCount = 0;
                    lastObservation = localObservation;
                    if (hasCodeEvidence(localObservation)) {
                        evidenceCount++;
                        noEvidenceSteps = 0;
                    }
                    
                } catch (const std::exception& e) {
                    observation += "Erro ao processar JSON da ferramenta: " + std::string(e.what()) + "\n";
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

    return "## AGENT NATIVO CODEX\n\n" +
           buildDistributedContextPrompt(workspaceRoot) +
           "## MODO DE OPERACAO: " + mode + "\n" + modeInstr + "\n\n" +
           "## FERRAMENTAS DISPONÍVEIS\n" + specs + "\n\n" +
           "## CONTEXTO OPERACIONAL\n" +
           "- Workspace raiz: " + workspaceRoot + "\n" +
           "- Bibliotecas aprovadas: " + rootsSummary + "\n" +
           "- Dominios aprovados: " + domainsSummary + "\n" +
           "- Contextos distribuidos encontrados: " + summarizeContextArtifactNames(workspaceRoot) + "\n\n" +
           "## REGRAS DE OURO:\n" +
           "1. Pense antes de agir com <thought>...</thought>.\n" +
           "2. Use blocos ```json ... ``` para ferramentas: {\"tool\": \"...\", \"args\": {...}}.\n" +
           "3. Skills disponiveis (use 'read_file' se precisar de detalhes): " + summarizeSkillNames(workspaceRoot) + "\n" +
           "4. Contextos distribuidos (use 'read_file' para aprofundar so no que for util): " + summarizeContextArtifactNames(workspaceRoot) + "\n" +
           "5. Se tomar uma decisao arquitetural relevante e existir 'memory/decisions-log.md' (ou equivalente em '.agent/memory/'), atualize esse arquivo com 'write_file' antes de concluir.\n" +
           "6. Se a tarefa estiver pronta, finalize obrigatoriamente com 'TASK COMPLETE'.\n" +
           "7. Nao invente informacoes. Se nao souber, use as ferramentas para descobrir.\n" +
           "8. Evite repetir a mesma tool-call com os mesmos argumentos. Após uma inspeção suficiente, execute a ação de materialização no workspace.\n" +
           "9. Para tarefas de documentacao, Doxygen, build ou teste, so conclua depois de verificar o artefato esperado com ferramenta objetiva (ex.: Doxyfile, docs/html/index.html, executavel, saida de teste ou make).\n" +
           "10. Nunca execute sudo, pkexec ou su. Se faltar dependencia do sistema, reporte o comando sugerido ao usuario e conclua com status claro de bloqueio externo.\n" +
           "11. Em tarefas Doxygen, trate como pacote minimo: criar/ajustar Doxyfile, documentar os fontes principais com comentarios Doxygen (ex.: /** ... */ ou /// em src/main.cpp), adicionar alvo 'doc' no Makefile quando houver Makefile, executar 'make doc' ou 'doxygen Doxyfile', e verificar 'docs/html/index.html'.\n" +
           "12. Se a tarefa pedir inserir, adicionar, alterar ou documentar texto em arquivo existente, leia o arquivo, aplique a edicao com apply_patch ou write_file, releia o arquivo e confirme que o trecho esperado esta presente antes de concluir.";
}

} // namespace agent::core
