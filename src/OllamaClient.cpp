#include "OllamaClient.hpp"
#include "httplib.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <thread>
#include <sstream>

using json = nlohmann::json;

namespace agent::network {

namespace fs = std::filesystem;

namespace {
std::mutex g_debugLogMutex;

json messageToProviderJson(const Message& msg) {
    json out = {{"role", msg.role}};
    if (!msg.content.empty()) out["content"] = msg.content;
    else if (msg.role != "assistant") out["content"] = "";
    if (!msg.name.empty()) out["name"] = msg.name;
    if (!msg.toolCallId.empty()) out["tool_call_id"] = msg.toolCallId;
    if (msg.toolCalls.is_array() && !msg.toolCalls.empty()) out["tool_calls"] = msg.toolCalls;
    return out;
}

std::string extractContentFromOpenAiMessage(const json& message) {
    if (message.contains("content")) {
        const auto& content = message["content"];
        if (content.is_string()) return content.get<std::string>();
        if (content.is_array()) {
            std::string combined;
            for (const auto& item : content) {
                if (item.is_object() && item.value("type", "") == "text" && item.contains("text") && item["text"].is_string()) {
                    if (!combined.empty()) combined += "\n";
                    combined += item["text"].get<std::string>();
                }
            }
            return combined;
        }
    }
    return "";
}

std::string extractReasoningFromOpenAiMessage(const json& message) {
    if (message.contains("reasoning_content") && message["reasoning_content"].is_string()) {
        return message["reasoning_content"].get<std::string>();
    }
    return "";
}

std::vector<ToolCall> extractToolCalls(const json& message, json& rawToolCallsOut) {
    std::vector<ToolCall> calls;
    rawToolCallsOut = json::array();
    if (!message.contains("tool_calls") || !message["tool_calls"].is_array()) return calls;
    rawToolCallsOut = message["tool_calls"];
    for (const auto& item : message["tool_calls"]) {
        if (!item.is_object() || !item.contains("function")) continue;
        ToolCall call;
        call.id = item.value("id", "");
        call.name = item["function"].value("name", "");
        std::string rawArguments = item["function"].value("arguments", "{}");
        try {
            call.arguments = json::parse(rawArguments.empty() ? "{}" : rawArguments);
        } catch (...) {
            call.arguments = json{{"_raw", rawArguments}};
        }
        calls.push_back(std::move(call));
    }
    return calls;
}

bool handleLmStudioSseEvent(const std::string& eventBlock,
                            std::function<void(const std::string&)> onChunk,
                            OllamaStreamStats& stats,
                            bool& success) {
    std::istringstream eventStream(eventBlock);
    std::string line;
    bool sawPayload = false;
    while (std::getline(eventStream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("data:", 0) != 0) continue;
        std::string payload = line.substr(5);
        if (!payload.empty() && payload.front() == ' ') payload.erase(0, 1);
        if (payload.empty()) continue;
        if (payload == "[DONE]") {
            success = true;
            continue;
        }
        sawPayload = true;
        try {
            auto j_chunk = json::parse(payload);
            if (j_chunk.contains("choices") && j_chunk["choices"].is_array() && !j_chunk["choices"].empty()) {
                const auto& choice = j_chunk["choices"][0];
                if (choice.contains("delta") && choice["delta"].contains("content") && choice["delta"]["content"].is_string()) {
                    onChunk(choice["delta"]["content"]);
                }
                if (choice.contains("message")) {
                    const std::string text = extractContentFromOpenAiMessage(choice["message"]);
                    if (!text.empty()) onChunk(text);
                }
                const std::string finishReason = choice.value("finish_reason", "");
                if (finishReason == "stop" || finishReason == "length") {
                    success = true;
                }
            }
            if (j_chunk.contains("usage")) {
                stats.prompt_tokens = j_chunk["usage"].value("prompt_tokens", 0);
                stats.completion_tokens = j_chunk["usage"].value("completion_tokens", 0);
                stats.total_duration_ms = 0.0;
            }
        } catch (...) {
        }
    }
    return sawPayload;
}

size_t findSseEventBoundary(const std::string& buffer, size_t startPos) {
    const size_t lf = buffer.find("\n\n", startPos);
    const size_t crlf = buffer.find("\r\n\r\n", startPos);
    if (lf == std::string::npos) return crlf;
    if (crlf == std::string::npos) return lf;
    return std::min(lf, crlf);
}

size_t sseEventBoundaryLength(const std::string& buffer, size_t boundaryPos) {
    if (boundaryPos == std::string::npos) return 0;
    if (buffer.compare(boundaryPos, 4, "\r\n\r\n") == 0) return 4;
    return 2;
}

std::string truncateDebugText(const std::string& text, size_t limit = 4000) {
    if (text.size() <= limit) return text;
    return text.substr(0, limit) + "\n...[truncado para debug]...";
}

std::string dumpJsonSafe(const json& value, int indent = -1) {
    try {
        return value.dump(indent, ' ', false, json::error_handler_t::replace);
    } catch (...) {
        return "{\"error\":\"json dump failed\"}";
    }
}
}

const char* OllamaClient::providerLabel(ModelProvider value) {
    return value == ModelProvider::LMStudio ? "LM Studio" : "Ollama";
}

OllamaClient::OllamaClient(const std::string& baseUrl, const std::string& model)
    : baseUrl(baseUrl), model(model) {}

void OllamaClient::logDebug(std::string_view phase, const std::string& body) const {
    if (provider != ModelProvider::LMStudio) return;
    std::lock_guard<std::mutex> lock(g_debugLogMutex);
    try {
        const fs::path logDir = fs::current_path() / ".agent" / "logs";
        fs::create_directories(logDir);
        std::ofstream out(logDir / "lmstudio_payloads.log", std::ios::app);
        if (!out.is_open()) return;
        const auto now = std::chrono::system_clock::now();
        const auto nowTime = std::chrono::system_clock::to_time_t(now);
        out << "\n=== " << std::put_time(std::localtime(&nowTime), "%F %T")
            << " | " << phase
            << " | model=" << model
            << " | base_url=" << baseUrl
            << " ===\n";
        out << truncateDebugText(body) << "\n";
    } catch (...) {
    }
}

std::string OllamaClient::chat(const std::vector<Message>& history, const OllamaOptions& options) {
    httplib::Client cli(baseUrl);
    cli.set_read_timeout(120, 0);
    
    json j_history = json::array();
    for (const auto& msg : history) {
        j_history.push_back(messageToProviderJson(msg));
    }
    
    json payload;
    std::string route;
    if (provider == ModelProvider::LMStudio) {
        route = "/v1/chat/completions";
        payload = {
            {"model", model},
            {"messages", j_history},
            {"stream", false},
            {"temperature", options.temperature},
            {"top_p", options.top_p},
            {"max_tokens", options.num_predict}
        };
    } else {
        route = "/api/chat";
        payload = {
            {"model", model},
            {"messages", j_history},
            {"stream", false},
            {"options", {
                {"num_ctx", options.num_ctx},
                {"temperature", options.temperature},
                {"num_predict", options.num_predict},
                {"top_p", options.top_p},
                {"top_k", options.top_k}
            }}
        };
    }

    if (provider == ModelProvider::LMStudio) {
        logDebug("sync-request", dumpJsonSafe(payload, 2));
    }
    auto res = cli.Post(route, dumpJsonSafe(payload), "application/json");
    if (res && res->status == 200) {
        auto response_json = json::parse(res->body);
        if (provider == ModelProvider::LMStudio) {
            logDebug("sync-response", dumpJsonSafe(response_json, 2));
        }
        if (provider == ModelProvider::LMStudio) {
            if (response_json.contains("choices") && response_json["choices"].is_array() && !response_json["choices"].empty()) {
                return extractContentFromOpenAiMessage(response_json["choices"][0]["message"]);
            }
            return "";
        }
        return response_json["message"]["content"];
    }
    
    return std::string("Error: Unable to connect to ") + providerLabel(provider);
}

ChatTurnResult OllamaClient::chatWithTools(const std::vector<Message>& history,
                                           const nlohmann::json& tools,
                                           const std::string& systemMsg,
                                           const OllamaOptions& options) {
    ChatTurnResult result;

    // ── LM Studio path (OpenAI-compatible) ────────────────────────────────────
    if (provider == ModelProvider::LMStudio) {
        httplib::Client cli(baseUrl);
        cli.set_read_timeout(120, 0);

        json j_history = json::array();
        if (!systemMsg.empty()) {
            j_history.push_back({{"role", "system"}, {"content", systemMsg}});
        }
        for (const auto& msg : history) {
            j_history.push_back(messageToProviderJson(msg));
        }

        json payload = {
            {"model", model},
            {"messages", j_history},
            {"stream", false},
            {"temperature", options.temperature},
            {"top_p", options.top_p},
            {"max_tokens", options.num_predict},
            {"tools", tools},
            {"tool_choice", "auto"}
        };

        logDebug("tool-request", dumpJsonSafe(payload, 2));
        auto res = cli.Post("/v1/chat/completions", dumpJsonSafe(payload), "application/json");
        if (!(res && res->status == 200)) {
            result.content = res
                ? std::string("Error: HTTP ") + std::to_string(res->status) + " from LM Studio\nBody: " + res->body
                : std::string("Error: Unable to connect to LM Studio");
            return result;
        }

        auto responseJson = json::parse(res->body);
        logDebug("tool-response", dumpJsonSafe(responseJson, 2));
        if (!responseJson.contains("choices") || !responseJson["choices"].is_array() || responseJson["choices"].empty()) {
            return result;
        }

        const auto& choice = responseJson["choices"][0];
        result.finishReason = choice.value("finish_reason", "");
        if (!choice.contains("message") || !choice["message"].is_object()) return result;

        const auto& message = choice["message"];
        result.content  = extractContentFromOpenAiMessage(message);
        result.reasoning = extractReasoningFromOpenAiMessage(message);
        result.toolCalls = extractToolCalls(message, result.rawToolCalls);
        return result;
    }

    // ── Ollama path — function calling nativo via /api/chat ───────────────────
    // P0.2: Ollama >=0.2 suporta tools:[]. Se o modelo não suportar, a resposta
    // não terá tool_calls e o conteúdo textual é retornado normalmente (fallback).
    {
        httplib::Client cli(baseUrl);
        cli.set_read_timeout(120, 0);

        json j_history = json::array();
        if (!systemMsg.empty()) {
            j_history.push_back({{"role", "system"}, {"content", systemMsg}});
        }
        for (const auto& msg : history) {
            j_history.push_back(messageToProviderJson(msg));
        }

        json payload = {
            {"model", model},
            {"messages", j_history},
            {"stream", false},
            {"tools", tools},
            {"options", {
                {"num_ctx",     options.num_ctx},
                {"temperature", options.temperature},
                {"num_predict", options.num_predict},
                {"top_p",       options.top_p},
                {"top_k",       options.top_k}
            }}
        };

        auto res = cli.Post("/api/chat", dumpJsonSafe(payload), "application/json");
        if (!(res && res->status == 200)) {
            // Fallback: tentar sem tools (compatibilidade com Ollama antigo)
            result.content = chat(history, options);
            result.finishReason = "stop";
            return result;
        }

        try {
            auto responseJson = json::parse(res->body);
            if (!responseJson.contains("message") || !responseJson["message"].is_object()) {
                // Resposta inesperada — fallback para texto
                result.content = chat(history, options);
                return result;
            }

            const auto& message = responseJson["message"];
            result.content = message.value("content", "");
            result.finishReason = responseJson.value("done_reason", "");
            if (result.finishReason.empty() && responseJson.value("done", false)) {
                result.finishReason = "stop";
            }

            // Extrair tool_calls do formato Ollama (idêntico ao OpenAI)
            if (message.contains("tool_calls") && message["tool_calls"].is_array()) {
                result.toolCalls = extractToolCalls(message, result.rawToolCalls);
            }
        } catch (...) {
            // JSON parse falhou — fallback
            result.content = chat(history, options);
            result.finishReason = "stop";
        }
        return result;
    }
}

void OllamaClient::chatStream(const std::vector<Message>& history, 
                             std::function<void(const std::string&)> onChunk,
                             std::function<void(bool, OllamaStreamStats)> onComplete,
                             const std::string& systemMsg,
                             const OllamaOptions& options) {
    cancelRequested = false;
    streaming = true;
    
    // Rodar em thread separada para não travar a UI
    std::thread([this, history, onChunk, onComplete, systemMsg, options]() {
        httplib::Client cli(baseUrl);
        cli.set_read_timeout(120, 0); // Aumentado para lidar com contexto maior

        json j_history = json::array();
        
        if (!systemMsg.empty()) {
            j_history.push_back({{"role", "system"}, {"content", systemMsg}});
        }

        for (const auto& msg : history) {
            j_history.push_back(messageToProviderJson(msg));
        }

        json payload;
        std::string route;
        if (provider == ModelProvider::LMStudio) {
            route = "/v1/chat/completions";
            payload = {
                {"model", model},
                {"messages", j_history},
                {"stream", true},
                {"temperature", options.temperature},
                {"top_p", options.top_p},
                {"max_tokens", options.num_predict}
            };
        } else {
            route = "/api/chat";
            payload = {
                {"model", model},
                {"messages", j_history},
                {"stream", true},
                {"options", {
                    {"num_ctx", options.num_ctx},
                    {"temperature", options.temperature},
                    {"num_predict", options.num_predict},
                    {"top_p", options.top_p},
                    {"top_k", options.top_k}
                }}
            };
        }

        if (provider == ModelProvider::LMStudio) {
            logDebug("stream-request", dumpJsonSafe(payload, 2));
        }

        bool success = false;
        OllamaStreamStats stats;
        std::string streamBuffer;
        std::string lmStudioReasoning;
        std::string lmStudioContent;
        httplib::Headers headers;
        auto res = cli.Post(route, headers, dumpJsonSafe(payload), "application/json",
            [&](const char* data, size_t data_len) {
                if (cancelRequested.load()) return false;
                streamBuffer.append(data, data_len);
                if (provider == ModelProvider::LMStudio) {
                    size_t pos = 0;
                    while (true) {
                        size_t eventEnd = findSseEventBoundary(streamBuffer, pos);
                        if (eventEnd == std::string::npos) break;
                        std::string eventBlock = streamBuffer.substr(pos, eventEnd - pos);
                        std::istringstream eventStream(eventBlock);
                        std::string line;
                        while (std::getline(eventStream, line)) {
                            if (!line.empty() && line.back() == '\r') line.pop_back();
                            if (line.rfind("data:", 0) != 0) continue;
                            std::string payloadLine = line.substr(5);
                            if (!payloadLine.empty() && payloadLine.front() == ' ') payloadLine.erase(0, 1);
                            if (payloadLine.empty() || payloadLine == "[DONE]") continue;
                            try {
                                auto eventJson = json::parse(payloadLine);
                                if (eventJson.contains("choices") && eventJson["choices"].is_array() && !eventJson["choices"].empty()) {
                                    const auto& choice = eventJson["choices"][0];
                                    if (choice.contains("delta") && choice["delta"].is_object()) {
                                        const auto& delta = choice["delta"];
                                        if (delta.contains("reasoning_content") && delta["reasoning_content"].is_string()) {
                                            lmStudioReasoning += delta["reasoning_content"].get<std::string>();
                                        }
                                        if (delta.contains("content") && delta["content"].is_string()) {
                                            lmStudioContent += delta["content"].get<std::string>();
                                        }
                                    }
                                }
                            } catch (...) {
                            }
                        }
                        handleLmStudioSseEvent(eventBlock, onChunk, stats, success);
                        pos = eventEnd + sseEventBoundaryLength(streamBuffer, eventEnd);
                    }
                    streamBuffer.erase(0, pos);
                } else {
                    size_t pos = 0;
                    while (true) {
                        size_t newline = streamBuffer.find('\n', pos);
                        if (newline == std::string::npos) break;
                        std::string line = streamBuffer.substr(pos, newline - pos);
                        pos = newline + 1;
                        if (line.empty()) continue;
                        try {
                            auto j_chunk = json::parse(line);
                            if (j_chunk.contains("message") && j_chunk["message"].contains("content")) {
                                onChunk(j_chunk["message"]["content"]);
                            }
                            if (j_chunk.value("done", false)) {
                                success = true;
                                stats.prompt_tokens = j_chunk.value("prompt_eval_count", 0);
                                stats.completion_tokens = j_chunk.value("eval_count", 0);
                                stats.total_duration_ms = j_chunk.value("total_duration", 0.0) / 1000000.0; // ns -> ms
                            }
                        } catch (...) {
                        }
                    }
                    streamBuffer.erase(0, pos);
                }
                return true;
            });

        if (provider == ModelProvider::LMStudio) {
            std::ostringstream summary;
            summary << "success=" << (success ? "true" : "false") << "\n";
            summary << "reasoning_length=" << lmStudioReasoning.size() << "\n";
            summary << "content_length=" << lmStudioContent.size() << "\n";
            summary << "\n[reasoning]\n" << truncateDebugText(lmStudioReasoning, 2000) << "\n";
            summary << "\n[content]\n" << truncateDebugText(lmStudioContent, 2000) << "\n";
            logDebug("stream-summary", summary.str());
        }

        streaming = false;
        bool ok = success && res && res->status == 200 && !cancelRequested.load();
        onComplete(ok, stats);
    }).detach();
}

void OllamaClient::requestStop() {
    cancelRequested = true;
}

std::vector<std::string> OllamaClient::listModels() {
    std::vector<std::string> models;
    httplib::Client cli(baseUrl);

    auto res = cli.Get(provider == ModelProvider::LMStudio ? "/v1/models" : "/api/tags");
    if (res && res->status == 200) {
        try {
            auto j = json::parse(res->body);
            if (provider == ModelProvider::LMStudio) {
                for (const auto& modelEntry : j["data"]) {
                    if (modelEntry.contains("id")) models.push_back(modelEntry["id"]);
                }
            } else {
                for (const auto& modelEntry : j["models"]) {
                    models.push_back(modelEntry["name"]);
                }
            }
        } catch (...) {}
    }
    
    if (models.empty()) models.push_back(provider == ModelProvider::LMStudio ? "local-model" : "qwen2.5:14b");
    return models;
}

void OllamaClient::pullModel(const std::string& modelName, 
                            std::function<void(const std::string& status, float progress)> onProgress,
                            std::function<void(bool success)> onComplete) {
    if (provider == ModelProvider::LMStudio) {
        onProgress("LM Studio nao suporta pull por esta UI", 0.0f);
        onComplete(false);
        return;
    }
    std::thread([this, modelName, onProgress, onComplete]() {
        httplib::Client cli(baseUrl);
        cli.set_read_timeout(300, 0); // Pulling models can take a long time

        json payload = {{"model", modelName}, {"stream", true}};
        
        bool success = false;
        std::string streamBuffer;
        httplib::Headers headers;
        auto res = cli.Post("/api/pull", headers, dumpJsonSafe(payload), "application/json",
            [&](const char* data, size_t data_len) {
                streamBuffer.append(data, data_len);
                size_t pos = 0;
                while (true) {
                    size_t newline = streamBuffer.find('\n', pos);
                    if (newline == std::string::npos) break;
                    std::string line = streamBuffer.substr(pos, newline - pos);
                    pos = newline + 1;
                    if (line.empty()) continue;
                    try {
                        auto j = json::parse(line);
                        std::string status = j.value("status", "");
                        float progress = 0.0f;
                        if (j.contains("total") && j["total"].get<long long>() > 0) {
                            progress = (float)j.value("completed", 0LL) / (float)j["total"].get<long long>();
                        }
                        onProgress(status, progress);
                        if (status == "success") success = true;
                    } catch (...) {}
                }
                streamBuffer.erase(0, pos);
                return true;
            });
        
        onComplete(success && res && res->status == 200);
    }).detach();
}

std::string OllamaClient::fetchVersion() {
    httplib::Client cli(baseUrl);
    auto res = cli.Get(provider == ModelProvider::LMStudio ? "/v1/models" : "/api/version");
    if (res && res->status == 200) {
        try {
            auto j = json::parse(res->body);
            if (provider == ModelProvider::LMStudio) {
                return j.contains("data") ? "openai-compatible" : "unknown";
            }
            return j.value("version", "unknown");
        } catch (...) {}
    }
    return "";
}

} // namespace agent::network
