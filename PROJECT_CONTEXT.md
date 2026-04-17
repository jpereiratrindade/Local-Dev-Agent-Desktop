# PROJECT_CONTEXT.md

Projeto desktop local em C++ com SDL2, OpenGL e Dear ImGui para integrar modelos locais via Ollama.

Objetivo atual:
- evoluir de um chat com tools para um agente de codigo mais proximo de um Codex local
- usar ferramentas do workspace para criar estrutura, editar arquivos e validar resultados
- preparar skills reutilizaveis para scaffold, review, testes e geracao de projeto

Direcao arquitetural:
- Fortalecer primeiro as tools nativas e o orquestrador (Modularizacao Concluida ✅)
- Consolidar a arquitetura modular do UI
- Adicionar MCP depois como camada de padronizacao, nao como prerequisito inicial

Prioridade atual:
- Melhorar a gestão de modelos e prompt engineering (LLM Boost)
- Adicionar testes automatizados para componentes criticos
- Refatorar gradualmente `NativeTools.cpp`
- Melhorar continuidade de contexto entre chat assistido, historico e missoes
- Reduzir o gap entre resposta no chat e artefato persistente no workspace

Roadmap de UX (Status: Em andamento):
- [x] Arquitetura UI Modular
- [x] Persistencia de sessao e historico por projeto
- [x] Chat assistido com priorizacao do arquivo ativo
- [ ] Slash commands como `/fix`, `/test`, `/explain`
- [ ] Visualizacao de diff antes de aplicar mudancas
- [ ] Melhorias na gestao de modelos locais via UI

Direcao operacional adicional:
- tratar `intencao`, `planejamento`, `execucao`, `verificacao` e `iteracao` como capacidades adaptativas, nao pipeline fixo
- privilegiar artefato persistente quando houver autorizacao e alvo suficiente
- manter autonomia graduada e rastreavel sobre arquivos, pastas e conteudo salvo
