# Editing Flow Migration Plan

## Objective

Substituir o fluxo atual de `Aplicar` / `Aplicar+Salvar` por um modelo de mudanca estruturada, revisavel e rastreavel.

O objetivo nao e colar resposta do LLM no arquivo, e sim transformar a intencao do usuario em uma alteracao de workspace com melhor controle.

## Why Change

O fluxo atual tem um problema de origem: ele opera sobre a resposta textual do modelo, nao sobre a mudanca em si.

Isso causa erros como:

- prose explicativa sendo gravada em arquivo de codigo
- markdown misturado com conteudo final
- instrucoes de teste indo para o workspace
- edicao ambigua sem diff nem contrato claro

## Design Principle

Nao aplicar resposta.

Aplicar mudanca.

O LLM pode continuar respondendo em linguagem natural, mas a camada de edicao deve trabalhar com uma representacao intermediaria propria para artefatos.

## Target Model

### 1. Change Proposal

O sistema passa a tratar alteracoes como propostas estruturadas, por exemplo:

- `replace_file`
- `create_file`
- `append_to_file`
- `create_directory`
- `rename_path`
- `delete_path`

Cada proposta deve carregar:

- alvo
- tipo de operacao
- conteudo proposto
- justificativa curta
- origem da proposta

### 2. Review Surface

Antes de persistir, a UI deve mostrar uma superficie de revisao:

- arquivo alvo
- tipo de operacao
- diff ou preview
- acoes possiveis

Acoes desejadas:

- `Aceitar`
- `Aceitar e salvar`
- `Editar antes`
- `Cancelar`

### 3. Persistence Rules

A mudanca so deve ser salva diretamente quando:

- o alvo estiver claro
- o conteudo estiver estruturado
- a operacao for segura para aplicacao direta

Caso contrario:

- abrir preview
- ou exigir mediacao do usuario

## Migration Stages

### Stage 1. Remove reliance on raw response apply

Objetivo:

- reduzir protagonismo de `Aplicar` / `Aplicar+Salvar`
- impedir gravacao cega de resposta mista

Mudancas:

- marcar `Aplicar` atual como transitorio
- desabilitar `Aplicar+Salvar` quando a resposta nao for claramente estruturada
- detectar respostas com prose + codigo e bloquear aplicacao direta

### Stage 2. Introduce structured change envelope

Objetivo:

- criar um contrato interno para mudancas de arquivo

Estrutura sugerida:

```json
{
  "kind": "replace_file",
  "target": "src/main.cpp",
  "content": "...",
  "summary": "Atualiza o fluxo principal para aguardar input temporizado"
}
```

Primeiros tipos suportados:

- `replace_file`
- `create_file`
- `create_directory`

### Stage 3. Add review panel

Objetivo:

- mostrar a mudanca como mudanca, nao como resposta de chat

UI sugerida:

- painel lateral ou modal de revisao
- preview do conteudo novo
- diff simplificado quando houver arquivo existente

### Stage 4. Route agent output through change proposal path

Objetivo:

- pedidos de edicao deixam de depender de prose do chat

Fluxo:

1. usuario pede alteracao
2. sistema identifica alvo e modo
3. modelo responde com proposta de mudanca estruturada
4. UI mostra revisao
5. usuario aceita ou ajusta

### Stage 5. Partial patch support

Objetivo:

- evoluir de substituicao total para alteracoes locais

Isso pode vir depois, com:

- diff parcial
- apply patch
- merge em blocos

## Where the Real Difficulty Is

O desafio principal nao esta em salvar arquivo.

Ele esta em:

1. identificar corretamente o artefato-alvo
2. separar explicacao de conteudo aplicavel
3. decidir quando aplicar, quando revisar e quando pedir input
4. representar a mudanca de forma consistente para UI e tools
5. manter rastreabilidade da alteracao

## How This Relates to Codex

O `codex-main` sugere um caminho melhor:

- diff como objeto de primeira classe
- request-user-input como parte do fluxo
- aprovacao e execucao como estados distintos
- menos dependencia de “colar resposta”, mais foco em revisao de mudanca

Nao precisamos copiar a arquitetura inteira, mas podemos seguir o principio:

> o sistema deve revisar e aplicar mudancas estruturadas, nao despejar respostas do modelo em arquivos.

## Recommended Next Implementation Order

1. bloquear aplicacao cega de resposta mista
2. criar contrato interno `ChangeProposal`
3. exibir preview/revisao para `replace_file` e `create_file`
4. integrar chat assistido e mission loop com esse contrato
5. adicionar diff parcial e patch depois
