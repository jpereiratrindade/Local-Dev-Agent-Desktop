#pragma once

#include <string>
#include <functional>
#include <vector>
#include "json.hpp"

namespace agent::network {

struct Message {
    std::string role;
    std::string content;
};

class OllamaClient {
public:
    OllamaClient(const std::string& baseUrl, const std::string& model = "qwen2.5:14b");

    // Envio síncrono (bloqueante)
    std::string chat(const std::vector<Message>& history);

    // Envio assíncrono com callback para streaming e System Prompt opcional
    struct StreamStats {
        int prompt_tokens = 0;
        int completion_tokens = 0;
        double total_duration_ms = 0;
    };

    void chatStream(const std::vector<Message>& history, 
                   std::function<void(const std::string&)> onChunk,
                   std::function<void(bool, StreamStats)> onComplete,
                   const std::string& systemMsg = "");

    // Listar modelos disponíveis
    std::vector<std::string> listModels();

    // Atualizar o modelo em uso
    void setModel(const std::string& modelName) { model = modelName; }

private:
    std::string baseUrl;
    std::string model;
};

} // namespace agent::network
