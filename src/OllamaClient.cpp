#include "OllamaClient.hpp"
#include "httplib.h"
#include <iostream>
#include <thread>
#include <sstream>

using json = nlohmann::json;

namespace agent::network {

OllamaClient::OllamaClient(const std::string& baseUrl, const std::string& model)
    : baseUrl(baseUrl), model(model) {}

std::string OllamaClient::chat(const std::vector<Message>& history) {
    httplib::Client cli(baseUrl);
    
    json j_history = json::array();
    for (const auto& msg : history) {
        j_history.push_back({{"role", msg.role}, {"content", msg.content}});
    }

    json payload = {
        {"model", model},
        {"messages", j_history},
        {"stream", false}
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
                             std::function<void(bool, StreamStats)> onComplete,
                             const std::string& systemMsg) {
    cancelRequested = false;
    streaming = true;
    
    // Rodar em thread separada para não travar a UI
    std::thread([this, history, onChunk, onComplete, systemMsg]() {
        httplib::Client cli(baseUrl);
        cli.set_read_timeout(60, 0);

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
            {"stream", true}
        };

        bool success = false;
        StreamStats stats;
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

} // namespace agent::network
