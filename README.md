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
- Na barra de input, ajuste:
  - `Modelo`
  - `Reasoning` (`low`, `medium`, `high`)
  - `Acesso` (`Read-only`, `Workspace-write`, `Full-access`)

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
- O agente lê `AGENT.md` e `PROJECT_CONTEXT.md` na raiz do projeto, quando existirem.
- Skills reutilizáveis podem ser colocadas em `.agent/skills/` ou `skills/` usando `.md`, `.txt` ou `.json`.
- Para geração de projetos, prefira registrar skills como `create-cmake-project`, `scaffold-api`, `generate-tests` e `review-and-fix-build`.
- O sistema trata `perfil`, `skills` e `ferramentas/contexto` como camadas complementares:
  `perfil` orienta como pensar, `skill` sugere como começar, e tools/RAG/MCP ampliam contexto e ação.
- Skills são preferenciais e flexíveis: o agente pode combiná-las, adaptá-las ou ignorá-las se o contexto real indicar caminho melhor.

## Skills de exemplo

Este repositório já inclui skills iniciais em `.agent/skills/`:

- `create-cmake-project`: scaffold mínimo compilável com CMake
- `scaffold-api`: estrutura inicial de backend/API
- `generate-tests`: criação incremental de testes executáveis
- `review-and-fix-build`: reprodução e correção de falhas de build
- `coding-execution`: execução concreta sobre código, com validação
- `writing-chapter`: elaboração de capítulo com estrutura e prosa
- `research-project`: desenho de projeto de pesquisa com rigor e flexibilidade

Essas skills entram no prompt do orquestrador automaticamente e servem como guia operacional para o modo agente.

## Licença

Este projeto está licenciado sob **GNU GPL v3.0 only**.
Consulte o arquivo [LICENSE](./LICENSE).
