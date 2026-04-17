#include "NativeTools.hpp"
#include "ToolRegistry.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <array>
#include <memory>
#include <vector>
#include <regex>
#include <algorithm>
#include <unordered_map>
#include <cctype>

namespace fs = std::filesystem;

namespace agent::core {
namespace {
fs::path g_workspaceRoot = fs::current_path();
AccessLevel g_accessLevel = AccessLevel::WorkspaceWrite;
ContextSourceMode g_contextSourceMode = ContextSourceMode::WorkspaceOnly;
std::vector<fs::path> g_approvedRoots;
std::vector<std::string> g_approvedDomains;
std::vector<std::string> g_recentUsage;

std::string trim(std::string value) {
    auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

bool pathWithin(const fs::path& candidate, const fs::path& root) {
    auto candidateStr = candidate.string();
    auto rootStr = root.string();
    return candidateStr == rootStr || candidateStr.rfind(rootStr + fs::path::preferred_separator, 0) == 0;
}

bool resolveAgainstApprovedRoots(const fs::path& weak) {
    for (const auto& root : g_approvedRoots) {
        try {
            fs::path normalizedRoot = fs::weakly_canonical(root);
            if (pathWithin(weak, normalizedRoot)) return true;
        } catch (...) {
        }
    }
    return false;
}

void noteUsage(const std::string& item) {
    if (item.empty()) return;
    if (std::find(g_recentUsage.begin(), g_recentUsage.end(), item) != g_recentUsage.end()) return;
    g_recentUsage.push_back(item);
    if (g_recentUsage.size() > 16) g_recentUsage.erase(g_recentUsage.begin());
}

bool resolveWorkspacePath(const std::string& input, fs::path& resolved, std::string& error) {
    if (input.empty()) {
        error = "Path não fornecido.";
        return false;
    }
    try {
        fs::path candidate = fs::path(input);
        if (candidate.is_relative()) {
            candidate = g_workspaceRoot / candidate;
        }
        fs::path weak = fs::weakly_canonical(candidate);
        if (g_accessLevel == AccessLevel::FullAccess) {
            resolved = weak;
            return true;
        }
        fs::path root = fs::weakly_canonical(g_workspaceRoot);
        bool insideWorkspace = pathWithin(weak, root);
        bool insideApprovedLibrary = (g_contextSourceMode != ContextSourceMode::WorkspaceOnly) && resolveAgainstApprovedRoots(weak);
        if (!insideWorkspace && !insideApprovedLibrary) {
            error = "Acesso negado: path fora do workspace.";
            return false;
        }
        resolved = weak;
        return true;
    } catch (const std::exception& e) {
        error = std::string("Path inválido: ") + e.what();
        return false;
    }
}

bool isDomainApproved(const std::string& host) {
    if (g_contextSourceMode != ContextSourceMode::WorkspaceLibraryAndWeb) return false;
    std::string lower = host;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    for (const auto& approved : g_approvedDomains) {
        std::string candidate = approved;
        std::transform(candidate.begin(), candidate.end(), candidate.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (candidate.empty()) continue;
        if (lower == candidate) return true;
        if (lower.size() > candidate.size() &&
            lower.rfind("." + candidate) == lower.size() - candidate.size() - 1) {
            return true;
        }
    }
    return false;
}

fs::path ragRoot() {
    return g_workspaceRoot / ".agent" / "rag";
}

fs::path ragCacheRoot() {
    return ragRoot() / "cache";
}

std::string shellEscape(const std::string& value) {
    std::string out = "'";
    for (char c : value) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
}

std::string runShellCapture(const std::string& command) {
    std::array<char, 512> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(command.c_str(), "r"), pclose);
    if (!pipe) return "";
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

std::string sha256ForFile(const fs::path& path) {
    std::string output = trim(runShellCapture("sha256sum " + shellEscape(path.string()) + " 2>/dev/null"));
    if (output.empty()) return "";
    auto pos = output.find(' ');
    return pos == std::string::npos ? output : output.substr(0, pos);
}

std::string cleanUtf8(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(input[i]);
        if (c < 128) {
            out += static_cast<char>(c);
        } else if ((c & 0xE0) == 0xC0) { // 2 bytes
            if (i + 1 < input.size() && (static_cast<unsigned char>(input[i+1]) & 0xC0) == 0x80) {
                out += static_cast<char>(c);
                out += static_cast<char>(input[++i]);
            } else out += '?';
        } else if ((c & 0xF0) == 0xE0) { // 3 bytes
            if (i + 2 < input.size() && 
                (static_cast<unsigned char>(input[i+1]) & 0xC0) == 0x80 &&
                (static_cast<unsigned char>(input[i+2]) & 0xC0) == 0x80) {
                out += static_cast<char>(c);
                out += static_cast<char>(input[++i]);
                out += static_cast<char>(input[++i]);
            } else out += '?';
        } else if ((c & 0xF8) == 0xF0) { // 4 bytes
            if (i + 3 < input.size() && 
                (static_cast<unsigned char>(input[i+1]) & 0xC0) == 0x80 &&
                (static_cast<unsigned char>(input[i+2]) & 0xC0) == 0x80 &&
                (static_cast<unsigned char>(input[i+3]) & 0xC0) == 0x80) {
                out += static_cast<char>(c);
                out += static_cast<char>(input[++i]);
                out += static_cast<char>(input[++i]);
                out += static_cast<char>(input[++i]);
            } else out += '?';
        } else {
            out += '?';
        }
    }
    return out;
}

std::string normalizeText(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    
    for (size_t i = 0; i < input.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(input[i]);
        
        // UTF-8 Mapping for common Portuguese accents (safely)
        if (c == 0xC3 && i + 1 < input.size()) { 
            unsigned char next = static_cast<unsigned char>(input[i+1]);
            bool matched = true;
            if ((next >= 0x80 && next <= 0x85) || (next >= 0xA0 && next <= 0xA5)) out += 'a';      // àáâãäå ÀÁÂÃÄÅ
            else if ((next >= 0x88 && next <= 0x8B) || (next >= 0xA8 && next <= 0xAB)) out += 'e'; // èéêë ÈÉÊË
            else if ((next >= 0x8C && next <= 0x8F) || (next >= 0xAC && next <= 0xAF)) out += 'i'; // ìíîï ÌÍÎÏ
            else if ((next >= 0x92 && next <= 0x96) || (next >= 0xB2 && next <= 0xB6)) out += 'o'; // òóôõö ÒÓÔÕÖ
            else if ((next >= 0x99 && next <= 0x9C) || (next >= 0xB9 && next <= 0xBC)) out += 'u'; // ùúûü ÙÚÛÜ
            else if (next == 0x87 || next == 0xA7) out += 'c'; // çÇ
            else matched = false;
            
            if (matched) { 
                i++; 
                continue; 
            }
        }
        
        // Standard ASCII lower
        if (c < 128) {
            if (c >= 'A' && c <= 'Z') out += static_cast<char>(c + 32);
            else out += static_cast<char>(c);
        } else {
            // Preservar outros caracteres multibyte como estao para evitar corromper UTF-8
            out += static_cast<char>(c);
        }
    }
    return out;
}

std::vector<std::string> tokenizeQuery(const std::string& query) {
    std::string normalized = normalizeText(query);
    std::vector<std::string> terms;
    std::stringstream ss(normalized);
    std::string word;
    while (ss >> word) {
        // Remover pontuacao basica
        word.erase(std::remove_if(word.begin(), word.end(), [](unsigned char c) {
            return std::ispunct(c);
        }), word.end());
        if (word.size() >= 3) terms.push_back(word);
    }
    return terms;
}

std::string readTextFile(const fs::path& path) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) return "";
    std::stringstream buffer;
    buffer << ifs.rdbuf();
    return buffer.str();
}

std::string extractTextFromPdf(const fs::path& path, bool& usedOcr, std::string& extractor) {
    usedOcr = false;
    extractor.clear();

    std::string pdftotextCmd = "pdftotext -layout -nopgbrk " + shellEscape(path.string()) + " - 2>/dev/null";
    std::string text = runShellCapture(pdftotextCmd);
    if (!trim(text).empty()) {
        extractor = "pdftotext";
        return text;
    }

    std::string pythonFallback =
        "python3 - <<'PY'\n"
        "import sys\n"
        "try:\n"
        "    from pypdf import PdfReader\n"
        "except Exception:\n"
        "    sys.exit(0)\n"
        "path = " + shellEscape(path.string()) + "\n"
        "try:\n"
        "    reader = PdfReader(path)\n"
        "    for page in reader.pages:\n"
        "        text = page.extract_text() or ''\n"
        "        if text:\n"
        "            print(text)\n"
        "except Exception:\n"
        "    pass\n"
        "PY";
    text = runShellCapture(pythonFallback);
    if (!trim(text).empty()) {
        extractor = "pypdf";
        return text;
    }

    return "";
}

std::string extractDocumentText(const fs::path& path, bool& usedOcr, std::string& extractor, std::string& kind) {
    usedOcr = false;
    extractor.clear();
    kind = "unknown";
    std::string ext = normalizeText(path.extension().string());

    if (ext == ".pdf") {
        kind = "pdf";
        return extractTextFromPdf(path, usedOcr, extractor);
    }
    if (ext == ".txt" || ext == ".md" || ext == ".rst" || ext == ".csv") {
        kind = "text";
        extractor = "direct-read";
        return readTextFile(path);
    }
    if (ext == ".json" || ext == ".yml" || ext == ".yaml") {
        kind = "structured-text";
        extractor = "direct-read";
        return readTextFile(path);
    }
    return "";
}

std::vector<nlohmann::json> buildChunks(const std::string& text, const std::string& sourcePath) {
    std::vector<nlohmann::json> chunks;
    if (text.empty()) return chunks;

    const std::size_t chunkSize = 1200;
    const std::size_t overlap = 180;
    std::size_t start = 0;
    int idx = 0;

    auto moveBackToBoundary = [&](std::size_t pos) {
        while (pos > 0 && (static_cast<unsigned char>(text[pos]) & 0xC0) == 0x80) {
            pos--;
        }
        return pos;
    };

    while (start < text.size()) {
        std::size_t end = std::min(start + chunkSize, text.size());
        if (end < text.size()) {
            std::size_t breakPos = text.rfind('\n', end);
            if (breakPos != std::string::npos && breakPos > start + 400) {
                end = breakPos;
            } else {
                // Se nao achou quebra de linha, garante que nao corta um caractere UTF-8 ao meio
                end = moveBackToBoundary(end);
            }
        }
        
        if (end <= start) break; // Seguranca contra loops infinitos

        std::string chunkText = trim(text.substr(start, end - start));
        if (!chunkText.empty()) {
            chunks.push_back({
                {"chunk_index", idx},
                {"source", sourcePath},
                {"start_offset", static_cast<int>(start)},
                {"end_offset", static_cast<int>(end)},
                {"text", chunkText}
            });
            ++idx;
        }
        
        if (end >= text.size()) break;
        
        // Calcular proximo start com overlap, tambem respeitando UTF-8
        std::size_t nextStart = end > overlap ? end - overlap : end;
        start = moveBackToBoundary(nextStart);
        
        if (start < idx * 100) { // Pequeno hack de seguranca (nao deve acontecer com chunkSize=1200)
            if (start <= nextStart - overlap) start = end; 
        }
        if (start < 0) start = 0;
    }
    return chunks;
}

bool ensureDirectory(const fs::path& path, std::string& error) {
    try {
        if (!fs::exists(path)) fs::create_directories(path);
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

std::string ingestResolvedDocument(const fs::path& resolved, nlohmann::json* metaOut = nullptr) {
    if (!fs::exists(resolved) || !fs::is_regular_file(resolved)) {
        return "Erro: documento inexistente ou não regular: " + resolved.string();
    }

    std::string error;
    if (!ensureDirectory(ragCacheRoot(), error)) {
        return "Erro: não foi possível preparar cache RAG: " + error;
    }

    std::string hash = sha256ForFile(resolved);
    if (hash.empty()) return "Erro: não foi possível calcular sha256 do arquivo.";

    fs::path cacheDir = ragCacheRoot() / hash;
    fs::path documentTxt = cacheDir / "document.txt";
    fs::path chunksFile = cacheDir / "chunks.jsonl";
    fs::path metaFile = cacheDir / "meta.json";
    fs::path sourceFile = cacheDir / "source.json";

    if (fs::exists(metaFile) && fs::exists(documentTxt) && fs::exists(chunksFile)) {
        nlohmann::json meta;
        try {
            std::ifstream in(metaFile);
            in >> meta;
        } catch (...) {
        }
        noteUsage("RAG cache: " + resolved.string());
        if (metaOut) *metaOut = meta;
        return "Cache RAG reutilizado: " + hash + " (" + resolved.filename().string() + ")";
    }

    bool usedOcr = false;
    std::string extractor;
    std::string kind;
    std::string text = extractDocumentText(resolved, usedOcr, extractor, kind);
    if (trim(text).empty()) {
        return "Erro: não foi possível extrair texto útil de " + resolved.string() +
               ". Para PDF, instale pdftotext ou disponibilize extração via pypdf.";
    }

    std::vector<nlohmann::json> chunks = buildChunks(cleanUtf8(text), resolved.string());
    if (chunks.empty()) {
        return "Erro: extração concluída, mas nenhum chunk útil foi gerado para " + resolved.string();
    }

    if (!ensureDirectory(cacheDir, error)) {
        return "Erro: não foi possível criar diretório de cache: " + error;
    }

    {
        std::ofstream out(documentTxt);
        out << cleanUtf8(text);
    }
    {
        std::ofstream out(chunksFile);
        for (const auto& chunk : chunks) out << chunk.dump() << "\n";
    }

    nlohmann::json source = {
        {"path", resolved.string()},
        {"filename", resolved.filename().string()},
        {"sha256", hash},
        {"size_bytes", static_cast<long long>(fs::file_size(resolved))}
    };
    nlohmann::json meta = {
        {"sha256", hash},
        {"path", resolved.string()},
        {"kind", kind},
        {"extractor", extractor},
        {"used_ocr", usedOcr},
        {"chunk_count", static_cast<int>(chunks.size())},
        {"text_size", static_cast<int>(text.size())}
    };

    {
        std::ofstream out(sourceFile);
        out << source.dump(2);
    }
    {
        std::ofstream out(metaFile);
        out << meta.dump(2);
    }

    noteUsage("RAG ingest: " + resolved.string());
    if (metaOut) *metaOut = meta;
    return "Documento ingerido em cache RAG: " + hash + " (" + std::to_string(chunks.size()) + " chunks)";
}

std::vector<fs::path> listLibraryDocuments() {
    std::vector<fs::path> docs;
    for (const auto& root : g_approvedRoots) {
        try {
            if (!fs::exists(root)) continue;
            if (fs::is_regular_file(root)) {
                std::string ext = normalizeText(root.extension().string());
                if (ext == ".pdf" || ext == ".txt" || ext == ".md" || ext == ".rst") {
                    docs.push_back(root);
                }
            } else if (fs::is_directory(root)) {
                // Adicionando opcoes para pular erros de permissao e nao abortar a varredura
                for (const auto& entry : fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied)) {
                    if (!entry.is_regular_file()) continue;
                    std::string ext = normalizeText(entry.path().extension().string());
                    if (ext == ".pdf" || ext == ".txt" || ext == ".md" || ext == ".rst") {
                        docs.push_back(entry.path());
                    }
                }
            }
        } catch (...) {
        }
    }
    return docs;
}
} // namespace

// --- FILESYSTEM TOOLS ---

std::string apply_patch(const nlohmann::json& args) {
    if (!args.contains("path") || !args.contains("search") || !args.contains("replace")) {
        return "Erro: apply_patch requer 'path', 'search' e 'replace'.";
    }

    std::string pathStr = args["path"];
    std::string search = args["search"];
    std::string replace = args["replace"];

    fs::path resolved;
    std::string error;
    if (!resolveWorkspacePath(pathStr, resolved, error)) return "Erro: " + error;

    if (!fs::exists(resolved)) return "Erro: Arquivo não encontrado: " + pathStr;

    try {
        std::ifstream in(resolved, std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        in.close();

        if (search.empty()) {
             // Inserção no início se search for empty? Não, melhor exigir.
             return "Erro: 'search' não pode estar vazio.";
        }

        size_t pos = content.find(search);
        if (pos == std::string::npos) {
            return "Erro: Texto de busca não encontrado no arquivo. Verifique espaços e quebras de linha exatos.";
        }

        if (content.find(search, pos + search.length()) != std::string::npos) {
            return "Erro: Texto de busca não é único no arquivo. Forneça mais contexto (linhas vizinhas).";
        }

        content.replace(pos, search.length(), replace);

        std::ofstream out(resolved, std::ios::binary);
        out << content;
        out.close();

        noteUsage("apply_patch: " + pathStr);
        return "Sucesso: Patch aplicado em " + pathStr;
    } catch (const std::exception& e) {
        return "Erro ao aplicar patch: " + std::string(e.what());
    }
}

std::string read_file(const nlohmann::json& args) {
    std::string path = args.value("path", "");
    fs::path resolved;
    std::string error;
    if (!resolveWorkspacePath(path, resolved, error)) return "Erro: " + error;
    
    std::ifstream ifs(resolved);
    if (!ifs.is_open()) return "Erro: Não foi possível abrir " + resolved.string();
    
    std::stringstream buffer;
    buffer << ifs.rdbuf();
    return buffer.str();
}

std::string write_file(const nlohmann::json& args) {
    if (g_accessLevel == AccessLevel::ReadOnly) return "Erro: write_file bloqueado (Access: ReadOnly).";
    std::string path = args.value("path", "");
    std::string content = args.value("content", "");
    fs::path resolved;
    std::string error;
    if (!resolveWorkspacePath(path, resolved, error)) return "Erro: " + error;

    try {
        if (resolved.has_parent_path()) fs::create_directories(resolved.parent_path());
        
        std::ofstream ofs(resolved);
        if (!ofs.is_open()) return "Erro: Falha ao criar arquivo " + resolved.string();
        ofs << content;
        return "Sucesso: Arquivo " + resolved.string() + " gravado.";
    } catch (const std::exception& e) {
        return "Erro: " + std::string(e.what());
    }
}

std::string make_dir(const nlohmann::json& args) {
    if (g_accessLevel == AccessLevel::ReadOnly) return "Erro: make_dir bloqueado (Access: ReadOnly).";
    std::string path = args.value("path", "");
    if (path.empty()) return "Erro: path não fornecido.";

    fs::path resolved;
    std::string error;
    if (!resolveWorkspacePath(path, resolved, error)) return "Erro: " + error;

    try {
        fs::create_directories(resolved);
        noteUsage("MKDIR: " + resolved.string());
        return "Sucesso: Diretório " + resolved.string() + " garantido.";
    } catch (const std::exception& e) {
        return "Erro: " + std::string(e.what());
    }
}

std::string move_path(const nlohmann::json& args) {
    if (g_accessLevel == AccessLevel::ReadOnly) return "Erro: move_path bloqueado (Access: ReadOnly).";
    std::string from = args.value("from", "");
    std::string to = args.value("to", "");
    if (from.empty() || to.empty()) return "Erro: parâmetros 'from' e 'to' são obrigatórios.";

    fs::path resolvedFrom;
    fs::path resolvedTo;
    std::string error;
    if (!resolveWorkspacePath(from, resolvedFrom, error)) return "Erro: origem inválida: " + error;
    if (!resolveWorkspacePath(to, resolvedTo, error)) return "Erro: destino inválido: " + error;

    try {
        if (!fs::exists(resolvedFrom)) return "Erro: origem inexistente: " + resolvedFrom.string();
        if (resolvedTo.has_parent_path()) fs::create_directories(resolvedTo.parent_path());
        fs::rename(resolvedFrom, resolvedTo);
        noteUsage("MOVE: " + resolvedFrom.string() + " -> " + resolvedTo.string());
        return "Sucesso: Movido para " + resolvedTo.string();
    } catch (const std::exception& e) {
        return "Erro: " + std::string(e.what());
    }
}

std::string delete_path(const nlohmann::json& args) {
    if (g_accessLevel == AccessLevel::ReadOnly) return "Erro: delete_path bloqueado (Access: ReadOnly).";
    std::string path = args.value("path", "");
    bool recursive = args.value("recursive", false);
    if (path.empty()) return "Erro: path não fornecido.";

    fs::path resolved;
    std::string error;
    if (!resolveWorkspacePath(path, resolved, error)) return "Erro: " + error;

    try {
        if (!fs::exists(resolved)) return "Erro: caminho inexistente: " + resolved.string();
        std::uintmax_t removed = 0;
        if (fs::is_directory(resolved) && recursive) removed = fs::remove_all(resolved);
        else {
            if (fs::is_directory(resolved) && !fs::is_empty(resolved)) {
                return "Erro: diretório não vazio. Use recursive=true para remover.";
            }
            removed = fs::remove(resolved) ? 1 : 0;
        }
        noteUsage("DELETE: " + resolved.string());
        return "Sucesso: Remoção concluída em " + resolved.string() + " (" + std::to_string(removed) + " item(ns)).";
    } catch (const std::exception& e) {
        return "Erro: " + std::string(e.what());
    }
}

std::string list_dir(const nlohmann::json& args) {
    std::string path = args.value("path", g_workspaceRoot.string());
    fs::path resolved;
    std::string error;
    if (!resolveWorkspacePath(path, resolved, error)) return "Erro: " + error;

    try {
        std::stringstream ss;
        for (const auto& entry : fs::directory_iterator(resolved)) {
            ss << entry.path().filename().string() << (entry.is_directory() ? "/" : "") << "\n";
        }
        return ss.str();
    } catch (const std::exception& e) {
        return "Erro ao listar: " + std::string(e.what());
    }
}

// --- SYSTEM TOOLS ---

std::string run_command(const nlohmann::json& args) {
    if (g_accessLevel == AccessLevel::ReadOnly) return "Erro: run_command bloqueado (Access: ReadOnly).";
    std::string command = args.value("command", "");
    if (command.empty()) return "Erro: Comando vazio.";

    std::string cmdPrefix = (g_accessLevel == AccessLevel::FullAccess) ? "" : ("cd \"" + g_workspaceRoot.string() + "\" && ");
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen((cmdPrefix + command + " 2>&1").c_str(), "r"), pclose);
    if (!pipe) return "Erro: Falha ao executar popen()";
    
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result.empty() ? "(Comando executado sem saída)" : result;
}

std::string grep_search(const nlohmann::json& args) {
    std::string pattern = args.value("pattern", "");
    std::string path = args.value("path", ".");
    if (pattern.empty()) return "Erro: pattern não fornecido.";
    fs::path resolved;
    std::string error;
    if (!resolveWorkspacePath(path, resolved, error)) return "Erro: " + error;

    std::string command = "cd \"" + g_workspaceRoot.string() + "\" && rg -n --hidden --glob '!.git' \"" +
                          pattern + "\" \"" + resolved.string() + "\" 2>&1";

    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(command.c_str(), "r"), pclose);
    if (!pipe) return "Erro: Falha ao executar rg";
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result.empty() ? "Nenhum resultado." : result;
}

std::string read_file_slice(const nlohmann::json& args) {
    std::string path = args.value("path", "");
    int fromLine = args.value("from_line", 1);
    int toLine = args.value("to_line", 200);
    if (fromLine < 1) fromLine = 1;
    if (toLine < fromLine) return "Erro: intervalo inválido.";

    fs::path resolved;
    std::string error;
    if (!resolveWorkspacePath(path, resolved, error)) return "Erro: " + error;

    std::ifstream ifs(resolved);
    if (!ifs.is_open()) return "Erro: Não foi possível abrir " + resolved.string();

    std::stringstream out;
    std::string line;
    int idx = 0;
    while (std::getline(ifs, line)) {
        ++idx;
        if (idx < fromLine) continue;
        if (idx > toLine) break;
        out << idx << ": " << line << "\n";
    }
    return out.str().empty() ? "Sem conteúdo no intervalo solicitado." : out.str();
}

std::string fetch_url(const nlohmann::json& args) {
    std::string url = trim(args.value("url", ""));
    if (url.empty()) return "Erro: url não fornecida.";
    if (g_contextSourceMode != ContextSourceMode::WorkspaceLibraryAndWeb) {
        return "Erro: fetch_url bloqueado pela política de contexto atual.";
    }

    std::smatch match;
    std::regex urlRegex(R"(^https?://([^/]+)(/.*)?$)", std::regex::icase);
    if (!std::regex_match(url, match, urlRegex)) {
        return "Erro: URL inválida ou não suportada.";
    }
    std::string host = match[1].str();
    if (!isDomainApproved(host)) {
        return "Erro: domínio não aprovado pela política: " + host;
    }

    std::string command = "curl -L --max-time 20 --silent --show-error \"" + url + "\"";
    std::array<char, 256> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(command.c_str(), "r"), pclose);
    if (!pipe) return "Erro: Falha ao executar curl.";
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
        if (result.size() > 120000) break;
    }
    if (result.empty()) return "Erro: Sem conteúdo retornado de " + url;
    noteUsage("WEB: " + host);
    return result;
}

std::string ingest_document(const nlohmann::json& args) {
    if (g_contextSourceMode == ContextSourceMode::WorkspaceOnly) {
        return "Erro: ingest_document requer Workspace + Biblioteca ou modo superior.";
    }
    std::string path = args.value("path", "");
    fs::path resolved;
    std::string error;
    if (!resolveWorkspacePath(path, resolved, error)) return "Erro: " + error;
    return ingestResolvedDocument(resolved);
}

std::string search_library(const nlohmann::json& args) {
    if (g_contextSourceMode == ContextSourceMode::WorkspaceOnly) {
        return "Erro: search_library bloqueado pela política de contexto atual.";
    }
    std::string query = trim(args.value("query", ""));
    int limit = std::max(1, std::min(args.value("limit", 5), 12));
    if (query.empty()) return "Erro: query não fornecida.";

    auto docs = listLibraryDocuments();
    if (docs.empty()) return "Erro: nenhuma biblioteca local aprovada com documentos suportados foi encontrada.";

    std::vector<std::string> terms = tokenizeQuery(query);
    if (terms.empty()) return "Erro: query sem termos úteis para busca.";

    struct Hit {
        int score = 0;
        nlohmann::json chunk;
        nlohmann::json meta;
    };
    std::vector<Hit> hits;

    for (const auto& doc : docs) {
        nlohmann::json meta;
        ingestResolvedDocument(doc, &meta);
        if (!meta.is_object()) continue;
        std::string hash = meta.value("sha256", "");
        if (hash.empty()) continue;
        fs::path chunksFile = ragCacheRoot() / hash / "chunks.jsonl";
        std::ifstream in(chunksFile);
        if (!in.is_open()) continue;
        std::string line;
        int lineNum = 0;
        while (std::getline(in, line)) {
            lineNum++;
            if (line.empty()) continue;
            try {
                auto chunk = nlohmann::json::parse(line);
                std::string text = normalizeText(chunk.value("text", ""));
                int score = 0;
                for (const auto& term : terms) {
                    std::size_t pos = text.find(term);
                    while (pos != std::string::npos) {
                        ++score;
                        pos = text.find(term, pos + term.size());
                    }
                }
                if (score > 0) {
                    hits.push_back({score, chunk, meta});
                }
            } catch (...) {
            }
        }
    }

    if (hits.empty()) return "Nenhum resultado relevante na biblioteca local para: " + query;

    std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
        return a.score > b.score;
    });

    std::stringstream out;
    int emitted = 0;
    for (const auto& hit : hits) {
        std::string path = hit.meta.is_object() ? hit.meta.value("path", "") : "doc-desconhecido";
        noteUsage("BIBLIOTECA: " + path);
        out << "[" << hit.score << "] " << path << "\n";
        out << "chunk=" << hit.chunk.value("chunk_index", -1)
            << " extractor=" << hit.meta.value("extractor", "") << "\n";
        std::string text = hit.chunk.value("text", "");
        if (text.size() > 700) text = text.substr(0, 700) + "...";
        out << text << "\n\n";
        if (++emitted >= limit) break;
    }
    return out.str();
}

std::string rag_cache_status(const nlohmann::json&) {
    fs::path root = ragCacheRoot();
    if (!fs::exists(root)) return "Cache RAG ainda não inicializado.";
    int docs = 0;
    for (const auto& entry : fs::directory_iterator(root)) {
        if (entry.is_directory()) ++docs;
    }
    std::stringstream out;
    out << "Cache RAG: " << docs << " documento(s) em " << root.string() << "\n";
    auto roots = getNativeToolApprovedRoots();
    out << "Bibliotecas aprovadas:\n";
    if (roots.empty()) out << "- (nenhuma)\n";
    else for (const auto& rootPath : roots) out << "- " << rootPath << "\n";
    return out.str();
}

void registerNativeTools(const std::string& workspaceRoot) {
    try {
        g_workspaceRoot = fs::weakly_canonical(fs::path(workspaceRoot.empty() ? "." : workspaceRoot));
    } catch (...) {
        g_workspaceRoot = fs::current_path();
    }

    auto& reg = ToolRegistry::instance();

    reg.registerTool("read_file", "Lê o conteúdo de um arquivo.", {"path"}, read_file);
    reg.registerTool("read_file_slice", "Lê intervalo de linhas de um arquivo.", {"path", "from_line", "to_line"}, read_file_slice);
    reg.registerTool("apply_patch", "Realiza edições cirúrgicas em um arquivo existente usando busca e substituição de blocos únicos de texto. Esta é a ferramenta PREFERENCIAL para alterar arquivos grandes sem corrompê-los.", {"path", "search", "replace"}, apply_patch);
    reg.registerTool("write_file", "Grava conteúdo total em um arquivo. Use preferencialmente para arquivos NOVOS.", {"path", "content"}, write_file);
    reg.registerTool("make_dir", "Cria um diretório e seus pais se necessário.", {"path"}, make_dir);
    reg.registerTool("move_path", "Move ou renomeia arquivo/diretório dentro do escopo permitido.", {"from", "to"}, move_path);
    reg.registerTool("delete_path", "Remove arquivo ou diretório. Para diretório não vazio, use recursive=true.", {"path", "recursive"}, delete_path);
    reg.registerTool("list_dir", "Lista arquivos de um diretório.", {"path"}, list_dir);
    reg.registerTool("grep_search", "Busca padrão textual com ripgrep no workspace.", {"pattern", "path"}, grep_search);
    reg.registerTool("run_command", "Executa um comando no shell e retorna a saída.", {"command"}, run_command);
    reg.registerTool("fetch_url", "Busca conteúdo HTTP(S) bruto de um domínio aprovado pela política de contexto.", {"url"}, fetch_url);
    reg.registerTool("ingest_document", "Ingere documento da biblioteca/workspace para o cache RAG local por hash.", {"path"}, ingest_document);
    reg.registerTool("search_library", "Pesquisa SEMANTICA e LEXICAL no CONTEÚDO de milhares de arquivos das bibliotecas de referência aprovadas de uma só vez. Use OBRIGATORIAMENTE esta ferramenta para responder perguntas sobre o que os documentos dizem ou para encontrar informações específicas neles.", {"query", "limit"}, search_library);
    reg.registerTool("rag_cache_status", "Mostra status do cache RAG local e bibliotecas aprovadas.", {}, rag_cache_status);
}

void setNativeToolAccessLevel(AccessLevel level) {
    g_accessLevel = level;
}

AccessLevel getNativeToolAccessLevel() {
    return g_accessLevel;
}

std::string getNativeToolAccessLevelName() {
    if (g_accessLevel == AccessLevel::ReadOnly) return "ReadOnly";
    if (g_accessLevel == AccessLevel::FullAccess) return "FullAccess";
    return "WorkspaceWrite";
}

void setNativeToolContextSourceMode(ContextSourceMode mode) {
    g_contextSourceMode = mode;
}

ContextSourceMode getNativeToolContextSourceMode() {
    return g_contextSourceMode;
}

std::string getNativeToolContextSourceModeName() {
    if (g_contextSourceMode == ContextSourceMode::WorkspaceAndLibrary) return "WorkspaceAndLibrary";
    if (g_contextSourceMode == ContextSourceMode::WorkspaceLibraryAndWeb) return "WorkspaceLibraryAndWeb";
    return "WorkspaceOnly";
}

void setNativeToolApprovedRoots(const std::vector<std::string>& roots) {
    g_approvedRoots.clear();
    for (const auto& root : roots) {
        if (root.empty()) continue;
        try {
            g_approvedRoots.push_back(fs::weakly_canonical(fs::path(root)));
        } catch (...) {
        }
    }
}

std::vector<std::string> getNativeToolApprovedRoots() {
    std::vector<std::string> out;
    for (const auto& root : g_approvedRoots) out.push_back(root.string());
    return out;
}

void setNativeToolApprovedDomains(const std::vector<std::string>& domains) {
    g_approvedDomains.clear();
    for (auto domain : domains) {
        domain = trim(domain);
        if (!domain.empty()) g_approvedDomains.push_back(domain);
    }
}

std::vector<std::string> getNativeToolApprovedDomains() {
    return g_approvedDomains;
}

std::string getNativeToolUsageSummary() {
    if (g_recentUsage.empty()) return "";
    std::stringstream out;
    for (size_t i = 0; i < g_recentUsage.size(); ++i) {
        out << "- " << g_recentUsage[i];
        if (i + 1 < g_recentUsage.size()) out << "\n";
    }
    return out.str();
}

void clearNativeToolUsageSummary() {
    g_recentUsage.clear();
}

RagStats getNativeToolRagStats() {
    RagStats stats;
    fs::path root = ragCacheRoot();
    if (!fs::exists(root)) return stats;

    try {
        for (const auto& entry : fs::directory_iterator(root)) {
            if (!entry.is_directory()) continue;
            stats.docCount++;
            
            fs::path metaFile = entry.path() / "meta.json";
            if (fs::exists(metaFile)) {
                try {
                    std::ifstream in(metaFile);
                    nlohmann::json meta;
                    in >> meta;
                    stats.indexedPaths.push_back(meta.value("path", ""));
                } catch (...) {}
            }

            for (const auto& subEntry : fs::recursive_directory_iterator(entry.path())) {
                if (subEntry.is_regular_file()) {
                    stats.cacheSizeBytes += fs::file_size(subEntry.path());
                }
            }
        }
    } catch (...) {}
    return stats;
}

std::string ingest_file_direct(const std::string& pathStr) {
    fs::path resolved;
    std::string error;
    if (!resolveWorkspacePath(pathStr, resolved, error)) return "Erro: " + error;
    return ingestResolvedDocument(resolved);
}

} // namespace agent::core
