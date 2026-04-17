#pragma once

#include "AgentUI.hpp"
#include <filesystem>
#include <string>
#include <vector>
#include <algorithm>
#include <chrono>

namespace fs = std::filesystem;

namespace agent::ui {

// Helpers de Perfil e Contexto
inline const char* reasoningLabel(int idx) {
    static const char* labels[] = {"low", "medium", "high"};
    if (idx < 0 || idx > 2) return "medium";
    return labels[idx];
}

inline const char* accessLabel(int idx) {
    static const char* labels[] = {"Read-only", "Workspace-write", "Full-access"};
    if (idx < 0 || idx > 2) return "Workspace-write";
    return labels[idx];
}

inline const char* profileLabel(int idx) {
    static const char* labels[] = {
        "general", "coding", "analysis", "review",
        "writing-outline", "writing-argument", "writing-chapter", "writing-review",
        "research", "research-project"
    };
    if (idx < 0 || idx > 9) return "general";
    return labels[idx];
}

inline const char* profileUiLabel(int idx) {
    static const char* labels[] = {
        "Uso geral", "Codar", "Analise", "Review",
        "Writing Outline", "Writing Argumento", "Writing Capitulo",
        "Writing Review", "Pesquisa", "Projeto Pesquisa"
    };
    if (idx < 0 || idx > 9) return "Uso geral";
    return labels[idx];
}

inline std::string profileHintFor(const std::string& profile) {
    if (profile == "coding") return "Implementar, editar, validar, corrigir build e testar";
    if (profile == "analysis") return "Sintetizar com evidencias";
    if (profile == "review") return "Apontar bugs e riscos";
    if (profile == "writing-outline") return "Estruturar tese e secoes";
    if (profile == "writing-argument") return "Articular conceitos e tensoes";
    if (profile == "writing-chapter") return "Redigir prosa de capitulo";
    if (profile == "writing-review") return "Revisar tese, coesao e rigor";
    if (profile == "research") return "Mapear referencias e hipoteses";
    if (profile == "research-project") return "Desenhar problema, objetivos, metodo, corpus e entregaveis";
    return "Equilibrio geral";
}

inline const char* contextSourceLabel(int idx) {
    static const char* labels[] = {"workspace", "workspace+library", "workspace+library+web"};
    if (idx < 0 || idx > 2) return "workspace";
    return labels[idx];
}

// Helpers de UI
inline std::string iconLabel(bool emojiEnabled, const std::string& withEmoji, const std::string& plain) {
    return emojiEnabled ? withEmoji : plain;
}

inline std::string trimLoose(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// Path Helpers
std::string normalizeRootPath(const std::string& raw);
bool hasProjectMarkers(const fs::path& root);
int projectRootScore(const fs::path& root);
std::string resolveProjectRoot(const std::string& rawRoot);

} // namespace agent::ui
