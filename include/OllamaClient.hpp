#pragma once

#include <string>
#include <functional>
#include <vector>
#include <atomic>
#include "json.hpp"

namespace agent::network {

struct Message {
    std::string role;
    std::string content;
};

// Parâmetros de execução do modelo
struct OllamaOptions {
    int num_ctx = 8192;           // Tamanho da janela de contexto
    float temperature = 0.1f;    // Criatividade/Estabilidade
    int num_predict = 4096;      // Limite de tokens na resposta
    float top_p = 0.9f;
    int top_k = 40;
};

struct OllamaStreamStats {
    int prompt_tokens = 0;
    int completion_tokens = 0;
    double total_duration_ms = 0;
};

class OllamaClient {
public:
    OllamaClient(const std::string& baseUrl, const std::string& model = "qwen2.5:14b");

    // Envio síncrono (bloqueante)
    std::string chat(const std::vector<Message>& history, const OllamaOptions& options = OllamaOptions());

    void chatStream(const std::vector<Message>& history, 
                   std::function<void(const std::string&)> onChunk,
                   std::function<void(bool, OllamaStreamStats)> onComplete,
                   const std::string& systemMsg = "",
                   const OllamaOptions& options = OllamaOptions());

    void requestStop();
    bool isStreaming() const { return streaming.load(); }

    // Listar modelos disponíveis
    std::vector<std::string> listModels();

    // Baixar novo modelo
    void pullModel(const std::string& modelName, 
                  std::function<void(const std::string& status, float progress)> onProgress,
                  std::function<void(bool success)> onComplete);

    // Verificar versão do servidor
    std::string fetchVersion();

    // Atualizar o modelo em uso
    void setModel(const std::string& modelName) { model = modelName; }

private:
    std::string baseUrl;
    std::string model;
    std::atomic<bool> cancelRequested{false};
    std::atomic<bool> streaming{false};
};

} // namespace agent::network
