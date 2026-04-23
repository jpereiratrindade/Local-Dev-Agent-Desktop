#include "PromptComposer.hpp"
#include <sstream>

namespace agent::core {

std::string buildSharedAgentPrompt(const PromptContext& ctx) {
    std::stringstream prompt;
    prompt << "You are an interactive CLI tool that helps users with software engineering tasks. Use the instructions below and the tools available to you to assist the user.\n\n";

    prompt << "IMPORTANT: Before you begin work, think about what the code you're editing is supposed to do based on the filenames directory structure.\n\n";

    prompt << "# Tone and style\n";
    prompt << "You should be concise, direct, and to the point.\n";
    prompt << "Remember that your output will be displayed on a command line interface.\n";
    prompt << "Output text to communicate with the user; all text you output outside of tool use is displayed to the user. Only use tools to complete tasks.\n";
    prompt << "IMPORTANT: You should NOT answer with unnecessary preamble or postamble (such as explaining your code or summarizing your action), unless the user asks you to.\n\n";

    prompt << "# Proactiveness\n";
    prompt << "You are allowed to be proactive, but only when the user asks you to do something.\n";
    prompt << "Do not add additional code explanation summary unless requested by the user. After working on a file, just stop, rather than providing an explanation of what you did.\n\n";

    // P1.3: Instruções específicas por modo
    const std::string& mode = ctx.mode;
    if (mode == "MISSION") {
        prompt << "# Task Execution Mode (MISSION)\n";
        prompt << "You are in autonomous execution mode. Use tools to complete the task fully without asking for confirmation.\n";
        prompt << "Work systematically: read relevant files, implement changes, verify with run_command when applicable.\n";
        prompt << "Prefer apply_patch over write_file when editing existing files — it is safer and more precise.\n";
        prompt << "When the task is fully complete, say TASK COMPLETE on its own line.\n\n";
    } else if (mode == "ASSIST") {
        prompt << "# Assisted Editing Mode (ASSIST)\n";
        prompt << "You are helping the user edit the active file. Focus on the active file context provided.\n";
        prompt << "When proposing file changes, prefer structured JSON envelopes:\n";
        prompt << "{\"kind\": \"replace_file\", \"target\": \"<path>\", \"content\": \"<full content>\", \"summary\": \"<short description>\"}\n";
        prompt << "This allows the user to review and apply changes safely.\n\n";
    } else {
        // CHAT / default
        prompt << "# Doing tasks\n";
        prompt << "The user will primarily request you perform software engineering tasks. This includes solving bugs, adding new functionality, refactoring code, explaining code, and more. For these tasks the following steps are recommended:\n";
        prompt << "1. Use the available tools to understand the codebase and the user's query.\n";
        prompt << "2. Implement the solution using all tools available to you.\n\n";
    }

    prompt << "# Environment\n";
    prompt << "Here is useful information about the environment you are running in:\n";
    if (!ctx.workspaceRoot.empty()) prompt << "Working directory: " << ctx.workspaceRoot << "\n";
    if (!ctx.activeFile.empty())    prompt << "Active file: " << ctx.activeFile << "\n";
    if (!ctx.distributedContextSummary.empty()) prompt << "Distributed context available: " << ctx.distributedContextSummary << "\n";
    if (!ctx.skillsSummary.empty()) prompt << "Skills available: " << ctx.skillsSummary << "\n";

    if (!ctx.distributedContextBody.empty()) {
        prompt << "\n# Context\n" << ctx.distributedContextBody << "\n";
    }

    if (!ctx.toolSpecs.empty()) {
        prompt << "\n# Tools\n" << ctx.toolSpecs << "\n";
    }

    // P1.2: Few-shot examples para Ollama (não LM Studio) em modos operacionais
    // Ajuda modelos menores a formatar tool calls corretamente.
    const bool isOllama = ctx.provider.find("LM Studio") == std::string::npos;
    const bool needsExamples = isOllama && (mode == "MISSION" || mode == "ASSIST" || mode.empty());
    if (needsExamples && !ctx.toolSpecs.empty()) {
        prompt << "\n# Tool Usage Examples\n";
        prompt << "When using a tool, emit ONLY the JSON call. Do not add prose before or after.\n";
        prompt << "Example — list directory:\n";
        prompt << "{\"name\": \"list_dir\", \"arguments\": {\"path\": \"src\"}}\n\n";
        prompt << "Example — read file:\n";
        prompt << "{\"name\": \"read_file\", \"arguments\": {\"path\": \"src/main.cpp\"}}\n\n";
        prompt << "Example — patch existing file (PREFERRED for edits):\n";
        prompt << "{\"name\": \"apply_patch\", \"arguments\": {\"path\": \"src/main.cpp\", \"search\": \"old line\\n\", \"replace\": \"new line\\n\"}}\n\n";
        prompt << "Example — write new file:\n";
        prompt << "{\"name\": \"write_file\", \"arguments\": {\"path\": \"src/hello.cpp\", \"content\": \"#include <iostream>\\nint main() { std::cout << \\\"Hello\\\\n\\\"; }\\n\"}}\n\n";
        prompt << "Example — run command:\n";
        prompt << "{\"name\": \"run_command\", \"arguments\": {\"command\": \"cmake --build build\"}}\n\n";
        prompt << "IMPORTANT: Use apply_patch instead of write_file when the file already exists. Only use write_file for new files.\n\n";
    }

    return prompt.str();
}

} // namespace agent::core
