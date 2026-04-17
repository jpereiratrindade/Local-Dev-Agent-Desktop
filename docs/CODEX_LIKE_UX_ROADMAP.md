# Codex-like UX Roadmap

## Objective

Aproximar a experiencia do app ao fluxo de Codex/Copilot em ambiente local, sem sacrificar continuidade, confiabilidade e controle.

## Principle

Antes de perseguir aparencia de autocomplete, priorizar:

- contexto correto
- aplicacao segura de alteracoes
- continuidade conversacional
- integracao clara entre chat, editor e tools

## Phase 0: Infrastructure

- [x] Refatoração Modular do UI (Separação em submódulos especializados)
- [x] Persistência de sessões por projeto

## Phase 1

Foco: ganho alto com risco baixo.

- slash commands como `/fix`, `/test`, `/explain`, `/refactor`, `/doc`
- deteccao automatica de arquivo ativo
- prompts baseados em selecao ativa
- visualizacao de diff antes de aplicar mudancas
- fluxo explicito de `aplicar patch` com confirmacao

## Phase 2

Foco: fluidez de navegacao.

- quick open de arquivos no estilo `Ctrl+P`
- atalhos para focar explorer, chat e arquivo ativo
- navegacao mais rapida entre arquivos relevantes do projeto

## Phase 3

Foco: experiencia inline.

- sugestoes inline no editor
- ghost text em tempo real
- preview incremental durante streaming

## Phase 4

Foco: refinamento do editor e integracoes mais profundas.

- evolucao do editor embutido
- acoes contextuais mais ricas sobre blocos de codigo
- integracao mais ampla com MCP e outras tools externas

## Priority Order

1. contexto automatico e selecao ativa
2. slash commands
3. diff/apply patch
4. quick open e atalhos de foco
5. inline suggestions e ghost text

## Why This Order

- melhora comportamento real antes de cosmetica
- reduz chance de parecer Copilot sem entregar confiabilidade
- aproveita os diferenciais do projeto: skills, perfis, RAG local e governanca
