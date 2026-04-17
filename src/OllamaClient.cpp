#include "OllamaClient.hpp"
#include "httplib.h"
#include <iostream>
#include <thread>
#include <sstream>

using json = nlohmann::json;

namespace agent::network {

OllamaClient::OllamaClient(const std::string& baseUrl, const std::string& model)
    : baseUrl(baseUrl), model(model) {}

std::string OllamaClient::chat(const std::vector<Message>& history, const OllamaOptions& options) {
    httplib::Client cli(baseUrl);
    cli.set_read_timeout(120, 0);
    
    json j_history = json::array();
    for (const auto& msg : history) {
        j_history.push_back({{"role", msg.role}, {"content", msg.content}});
    }
    
    json payload = {
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

    auto res = cli.Post("/api/chat", payload.dump(), "application/json");
    if (res && res->status == 200) {
        auto response_json = json::parse(res->body);
        return response_json["message"]["content"];
    }
    
    return "Error: Unable to connect to Ollama";
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
            j_history.push_back({{"role", msg.role}, {"content", msg.content}});
        }

        json payload = {
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

        bool success = false;
        OllamaStreamStats stats;
        std::string streamBuffer;
        httplib::Headers headers;
        auto res = cli.Post("/api/chat", headers, payload.dump(), "application/json",
            [&](const char* data, size_t data_len) {
                if (cancelRequested.load()) return false;
                streamBuffer.append(data, data_len);
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
                return true;
            });

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
    
    auto res = cli.Get("/api/tags");
    if (res && res->status == 200) {
        try {
            auto j = json::parse(res->body);
            for (const auto& model : j["models"]) {
                models.push_back(model["name"]);
            }
        } catch (...) {}
    }
    
    if (models.empty()) models.push_back("qwen2.5:14b"); // Fallback
    return models;
}

void OllamaClient::pullModel(const std::string& modelName, 
                            std::function<void(const std::string& status, float progress)> onProgress,
                            std::function<void(bool success)> onComplete) {
    std::thread([this, modelName, onProgress, onComplete]() {
        httplib::Client cli(baseUrl);
        cli.set_read_timeout(300, 0); // Pulling models can take a long time

        json payload = {{"model", modelName}, {"stream", true}};
        
        bool success = false;
        std::string streamBuffer;
        httplib::Headers headers;
        auto res = cli.Post("/api/pull", headers, payload.dump(), "application/json",
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
    auto res = cli.Get("/api/version");
    if (res && res->status == 200) {
        try {
            auto j = json::parse(res->body);
            return j.value("version", "unknown");
        } catch (...) {}
    }
    return "";
}

} // namespace agent::network
