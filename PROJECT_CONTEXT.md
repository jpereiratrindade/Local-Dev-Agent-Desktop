# PROJECT_CONTEXT.md

Projeto desktop local em C++ com SDL2, OpenGL e Dear ImGui para integrar modelos locais via Ollama.

Objetivo atual:
- evoluir de um chat com tools para um agente de codigo mais proximo de um Codex local
- usar ferramentas do workspace para criar estrutura, editar arquivos e validar resultados
- preparar skills reutilizaveis para scaffold, review, testes e geracao de projeto

Direcao arquitetural:
- fortalecer primeiro as tools nativas e o comportamento do orquestrador
- consolidar skills persistentes no workspace
- adicionar MCP depois como camada de padronizacao, nao como prerequisito inicial
