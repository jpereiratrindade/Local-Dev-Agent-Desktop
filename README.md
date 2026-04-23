# Agent GUI Local (Codex-like)

Aplicação desktop local em C++ (SDL2 + OpenGL + Dear ImGui) para interação com LLM local via Ollama, com modo autônomo estilo agente, ferramentas de workspace e histórico de conversa por projeto.

## Principais recursos

- Chat local com streaming de respostas.
- Modo autônomo com orquestrador e tool-calls.
- Interrupção manual de geração (`Interromper`) em streaming e missão autônoma.
- Controles inline na caixa de pergunta: `Modelo`, `Reasoning`, `Acesso`.
- Níveis de acesso de ferramentas: `Read-only`, `Workspace-write`, `Full-access`.
- Ferramentas nativas: leitura/escrita de arquivo, listagem de diretório, busca textual e execução de comando.
- Ferramentas nativas de workspace para agir como agente de código: criar diretórios, mover/renomear caminhos, remover artefatos e validar via shell.
- Execução autônoma de múltiplas ações por missão, incluindo envelopes `create_file`/`replace_file`, comandos shell no workspace ativo e observações visíveis no histórico.
- Seletor de pasta interno (nativo da UI), com navegação, criação de pasta e fallback para seletor do sistema.
- Histórico de diálogos por projeto com sidebar `DIALOGOS` e menu `Diálogos Recentes`.
- Persistência de sessão por projeto em `.agent/sessions/*.json` + `last_session.json`.
- UX com separação de resposta em abas: `Resposta`, `Ações`, `Logs`.
- Ícones com emoji quando fonte suportada, fallback automático para ASCII.
- Explorer de arquivos, painel de inspector e telemetria.

## Estrutura do projeto

```text
.
├── CMakeLists.txt
├── include/
├── src/
│   ├── AgentUI.cpp (Core & Layout)
│   ├── AgentUI_Chat.cpp (Chat & Markdown)
│   ├── AgentUI_Explorer.cpp (Explorer & Editor)
│   ├── AgentUI_Modals.cpp (Diálogos & Model Manager)
│   ├── AgentUI_Stats.cpp (Telemetria & RAG Sync)
│   ├── AgentUI_Persistence.cpp (Sessões & Histórico)
│   ├── Orchestrator.cpp
│   ├── OllamaClient.cpp
│   └── NativeTools.cpp
├── third_party/
│   └── vendor/
├── scripts/
│   └── build.sh
├── LICENSE
└── .agent/
```

## Requisitos

- Linux
- CMake >= 3.20
- Compilador C++ com suporte a C++20 (g++/clang++)
- SDL2 (headers + libs)
- OpenGL
- Ollama rodando localmente em `http://localhost:11434`

No Fedora:

```bash
sudo dnf install cmake gcc-c++ SDL2-devel mesa-libGL-devel
# opcional para seletor externo:
sudo dnf install zenity
```

No Ubuntu/Debian:

```bash
sudo apt-get update
sudo apt-get install -y cmake g++ libsdl2-dev libgl1-mesa-dev
```

## Build rápido

```bash
./scripts/build.sh
```

Build debug:

```bash
./scripts/build.sh debug
```

Build release:

```bash
./scripts/build.sh release
```

Build e execução:

```bash
./scripts/build.sh run
```

Limpar diretórios de build:

```bash
./scripts/build.sh clean
```

## Build manual (sem script)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
```

Executar:

```bash
./build/AgentGUI
```

## Teste CLI com LM Studio

Para validar o comportamento do servidor local do LM Studio fora da UI, use:

```bash
python3 scripts/test_lmstudio_cli.py
```

Opções úteis:

```bash
python3 scripts/test_lmstudio_cli.py --model qwen3-14b-claude-4.5-opus-high-reasoning-distill
python3 scripts/test_lmstudio_cli.py --prompt "Liste apenas os nomes dos arquivos do projeto."
python3 scripts/test_lmstudio_cli.py --dump-events
```

O script testa:

- `GET /v1/models`
- `POST /v1/chat/completions` com `stream=false`
- `POST /v1/chat/completions` com `stream=true`

Isso ajuda a distinguir:

- problema no servidor/modelo do LM Studio;
- problema no formato SSE recebido;
- problema específico da UI do app.

Quando o provider ativo for `LM Studio`, o app também grava debug local em:

```text
.agent/logs/lmstudio_payloads.log
```

Esse arquivo registra payload enviado, resposta síncrona e resumo do stream
(`reasoning_content` vs `content`) para comparação com outros clientes.

## Ollama

Inicie o serviço antes de abrir a UI:

```bash
ollama serve
```

A aplicação detecta modelos locais no startup e exibe sugestões de vocação no menu `Preferências`.

## Uso rápido da UI

- `Arquivo > Abrir Pasta...` abre o seletor interno de pastas.
- `Arquivo > Novo Diálogo` cria nova sessão (`session_YYYYMMDD_HHMMSS.json`).
- Clique em um item da sidebar `DIALOGOS` para restaurar conversa anterior.
- Na barra superior do chat, ajuste:
  - `Modelo`
  - `Reasoning` (`low`, `medium`, `high`)
  - `Acesso` (`Read-only`, `Workspace-write`, `Full-access`)
  - `Contexto` (`workspace`, `workspace+library`, `workspace+library+web`)
- Use `MISSION (Auto)` para pedidos que precisam alterar arquivos, criar estrutura ou validar com shell.
- Em modo missão, o chat mostra `AÇÃO` e `OBSERVAÇÃO` para cada tool executada, incluindo saída de comandos como `make`.

## Solução de problemas

- Se não abrir janela:
  - Verifique driver OpenGL e ambiente gráfico.
- Se não listar modelos:
  - Confirme `ollama serve` ativo em `localhost:11434`.
- Se o seletor externo não abrir:
  - Use o seletor interno da UI (padrão).
  - Opcionalmente instale `zenity`/`kdialog`.
- Se sessão não carregar:
  - Confira permissões de escrita em `.agent/sessions/`.
- Se build quebrar por cache antigo:
  - Rode `./scripts/build.sh clean` e depois `./scripts/build.sh`.

## Notas

- O workspace ativo impacta ferramentas e contexto.
- O histórico é salvo por projeto, não global.
- O agente mantém compatibilidade com `AGENT.md`/`AGENTS.md` e `PROJECT_CONTEXT.md`, mas também descobre contexto distribuído como `stack-context.md`, `coding-standards.md`, `architecture-context.md` e `memory/decisions-log.md`.
- Skills reutilizáveis podem ser colocadas em `.agent/skills/` ou `skills/` usando `.md`, `.txt` ou `.json`.
- Para geração de projetos, prefira registrar skills como `create-cmake-project`, `scaffold-api`, `generate-tests` e `review-and-fix-build`.
- O sistema trata `perfil`, `skills` e `ferramentas/contexto` como camadas complementares:
  `perfil` orienta como pensar, `skill` sugere como começar, e tools/RAG/MCP ampliam contexto e ação.
- Skills são preferenciais e flexíveis: o agente pode combiná-las, adaptá-las ou ignorá-las se o contexto real indicar caminho melhor.
- Skills não são mais despejadas integralmente no prompt base: o orquestrador injeta os nomes disponíveis e incentiva leitura progressiva via `read_file` apenas quando necessário.
- Quando houver `memory/decisions-log.md` ou `.agent/memory/decisions-log.md`, o agente pode registrar decisões arquiteturais relevantes como memória persistente do projeto.
- O chat agora aceita atalhos como `/fix`, `/explain`, `/test`, `/refactor` e `/doc` para acelerar pedidos recorrentes sobre o arquivo em foco.
- A UI pode inferir o arquivo alvo pelo nome citado no prompt mesmo antes de ele estar aberto no editor.
- A revisão de mudanças usa diff contra o conteúdo real do arquivo de destino: se o arquivo não estiver aberto no editor, a comparação é feita contra o conteúdo atual no workspace.
- O orquestrador executa todos os blocos JSON de uma resposta, não apenas o primeiro. Isso permite sequências como `make_dir`, `write_file`, `write_file`, `run_command`.
- Envelopes estruturados de mudança (`kind=create_file` ou `kind=replace_file`) são convertidos em `write_file` durante missões autônomas.
- `run_command` executa no workspace aberto por padrão, mesmo em `Full-access`; use `cwd` nos argumentos da tool quando precisar de outro diretório aprovado.
- Em distros onde OpenGL fica em `/usr/lib/x86_64-linux-gnu`, o CMake ajusta automaticamente o cache para evitar links quebrados para `/usr/lib64`.

## Skills de exemplo

Este repositório já inclui skills iniciais em `.agent/skills/`:

- `create-cmake-project`: scaffold mínimo compilável com CMake
- `scaffold-api`: estrutura inicial de backend/API
- `generate-tests`: criação incremental de testes executáveis
- `review-and-fix-build`: reprodução e correção de falhas de build
- `coding-execution`: execução concreta sobre código, com validação
- `writing-chapter`: elaboração de capítulo com estrutura e prosa
- `research-project`: desenho de projeto de pesquisa com rigor e flexibilidade

Essas skills ficam disponíveis ao orquestrador como catálogo inicial e servem como guia operacional para o modo agente. Os detalhes podem ser carregados sob demanda, reduzindo desperdício de contexto.

## Estabilizacao

Antes de ampliar integracoes maiores, o projeto passa a priorizar estabilizacao do nucleo.

- Decisao arquitetural: [docs/ADR-0001-stabilize-core-before-expanding-integrations.md](./docs/ADR-0001-stabilize-core-before-expanding-integrations.md)
- Execucao adaptativa e artefatos persistentes: [docs/ADR-0002-adaptive-execution-and-persistent-artifacts.md](./docs/ADR-0002-adaptive-execution-and-persistent-artifacts.md)
- Checklist pratica: [docs/STABILIZATION_CHECKLIST.md](./docs/STABILIZATION_CHECKLIST.md)

## Roadmap de UX

Para aproximar a experiencia do app a um fluxo mais `Codex-like`, a ordem atual de implementacao prioriza contexto e aplicacao segura antes de autocomplete visual.

- Roadmap: [docs/CODEX_LIKE_UX_ROADMAP.md](./docs/CODEX_LIKE_UX_ROADMAP.md)

## Licença

Este projeto está licenciado sob **GNU GPL v3.0 only**.
Consulte o arquivo [LICENSE](./LICENSE).
