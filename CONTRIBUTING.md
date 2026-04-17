# Guia de Contribuição - Agent

Este projeto utiliza uma arquitetura modular em C++ para garantir manutenibilidade e performance.

## Arquitetura Modular

A UI é dividida em submódulos dentro de `src/` e `include/`:

- **Core (`AgentUI.cpp`)**: Gerencia o layout principal, o loop de renderização e coordena os componentes.
- **Chat (`AgentUI_Chat.cpp`)**: Implementa a janela de chat, processamento de Markdown e lógica de envio de mensagens.
- **Explorer (`AgentUI_Explorer.cpp`)**: Gerencia a árvore de arquivos e o editor de texto integrado.
- **Modals (`AgentUI_Modals.cpp`)**: Contém diálogos popup (Model Manager, Folder Picker, etc.).
- **Stats (`AgentUI_Stats.cpp`)**: Lógica de telemetria GPU/RAM e sincronização RAG.
- **Persistence (`AgentUI_Persistence.cpp`)**: Gestão de salvamento/carregamento de sessões JSON.

## Como Adicionar Nova Funcionalidade

1. **Definição**: Adicione os métodos/variáveis necessários em `include/AgentUI.hpp`.
2. **Implementação**: Escolha o arquivo `.cpp` correspondente à área da funcionalidade.
3. **Internal Helpers**: Se precisar de funções utilitárias compartilhadas entre módulos, coloque em `src/AgentUI_Internal.hpp`.
4. **Build**: O `CMakeLists.txt` deve ser atualizado se novos arquivos forem criados.

## Padrões de Código

- Use `snake_case` para variáveis locais e `camelCase` para membros de classe (padrão atual do projeto).
- Utilize `std::lock_guard` para acesso a recursos thread-safe (como `history` e telemetry).
- Prefira ferramentas nativas em vez de chamadas de sistema externas.

## Testes

- Execute `./scripts/build.sh debug` para validar a compilação.
- Teste a funcionalidade localmente antes de submeter.
