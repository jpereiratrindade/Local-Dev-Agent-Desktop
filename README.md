# Agent GUI Local (Codex-like)

Aplicação desktop local em C++ (SDL2 + OpenGL + Dear ImGui) para interação com LLM local via Ollama, com modo autônomo estilo agente, ferramentas de workspace e histórico de conversa por projeto.

## Principais recursos

- Chat local com streaming de respostas.
- Modo autônomo com orquestrador e tool-calls.
- Ferramentas nativas: leitura/escrita de arquivo, listagem de diretório, busca textual e execução de comando no workspace.
- Explorador de arquivos, painel de inspector e telemetria.
- Persistência de sessão por projeto em `.agent/sessions/last_session.json`.
- UX com separação de resposta em abas: `Resposta`, `Ações`, `Logs`.

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

## Solução de problemas

- Se não abrir janela:
  - Verifique driver OpenGL e ambiente gráfico.
- Se não listar modelos:
  - Confirme `ollama serve` ativo em `localhost:11434`.
- Se sessão não carregar:
  - Confira permissões de escrita em `.agent/sessions/`.
- Se build quebrar por cache antigo:
  - Rode `./scripts/build.sh clean` e depois `./scripts/build.sh`.

## Notas

- O workspace ativo impacta ferramentas e contexto.
- O histórico é salvo por projeto, não global.
