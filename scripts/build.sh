#!/usr/bin/env sh
set -eu

MODE="${1:-debug}"
BUILD_DIR="build"

print_help() {
  cat <<EOF
Uso: ./scripts/build.sh [debug|release|run|clean|help]

Comandos:
  debug    Configura e compila em Debug (padrão)
  release  Configura e compila em Release
  run      Compila em Debug e executa o AgentGUI
  clean    Remove diretórios de build locais
  help     Exibe esta ajuda
EOF
}

require_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "Erro: comando '$1' não encontrado."
    exit 1
  fi
}

build_project() {
  build_type="$1"
  require_cmd cmake
  # Evita cache de paths absolutos de libs (ex.: migração Debian -> Fedora).
  rm -f "$BUILD_DIR/CMakeCache.txt"
  rm -rf "$BUILD_DIR/CMakeFiles"
  cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$build_type"
  cmake --build "$BUILD_DIR" -j
}

case "$MODE" in
  debug)
    echo "[build] Modo Debug"
    build_project Debug
    echo "[ok] Binário: $BUILD_DIR/AgentGUI"
    ;;
  release)
    echo "[build] Modo Release"
    build_project Release
    echo "[ok] Binário: $BUILD_DIR/AgentGUI"
    ;;
  run)
    echo "[build] Compilando (Debug) e executando"
    build_project Debug
    exec "./$BUILD_DIR/AgentGUI"
    ;;
  clean)
    echo "[clean] Removendo diretórios de build"
    rm -rf "$BUILD_DIR" build-local-root
    echo "[ok] Limpeza concluída"
    ;;
  help|-h|--help)
    print_help
    ;;
  *)
    echo "Modo inválido: $MODE"
    print_help
    exit 2
    ;;
esac
