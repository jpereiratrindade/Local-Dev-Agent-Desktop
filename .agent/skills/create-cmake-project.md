# skill: create-cmake-project

## Quando usar
- pedido para criar projeto C++
- scaffold inicial com CMake
- reorganizar um repositorio pequeno em layout `include/`, `src/`, `tests/`, `scripts/`

## Objetivo
Criar um projeto compilavel minimo, com estrutura clara, arquivos essenciais e instrucoes de build.

## Fluxo
1. Inspecione a raiz com `list_dir`.
2. Planeje a estrutura minima.
3. Crie diretorios com `make_dir`.
4. Grave `CMakeLists.txt`, fontes iniciais, headers e `README.md` se faltarem.
5. Valide com `run_command` usando build local.

## Entregaveis minimos
- `CMakeLists.txt`
- `src/main.cpp`
- `include/` se houver headers
- `README.md` com build/execucao
- `tests/` ou placeholder simples se fizer sentido

## Regras
- Nao gere complexidade antes do minimo compilavel.
- Preserve arquivos existentes e expanda sobre eles quando possivel.
- Ao final, informe a estrutura criada e o resultado da validacao.
