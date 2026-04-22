# ADR-0002: Adaptive Execution and Persistent Artifacts

## Status
Accepted

## Context

O projeto evoluiu de uma interface conversacional com tools para um agente local que precisa:

- reduzir o gap entre intencao do usuario e implementacao no workspace
- preservar criatividade do modelo sem impor um pipeline fixo demais
- editar, criar e manter artefatos persistentes quando houver autorizacao e contexto suficiente
- distinguir melhor entre conversa assistida, execucao operacional e verificacao

Os testes recentes mostraram um problema recorrente: o LLM frequentemente respondia bem no chat, mas deixava de materializar o resultado no arquivo ou artefato esperado.

## Decision

O sistema passa a adotar uma arquitetura de execucao adaptativa com foco em artefatos persistentes.

Isso significa:

1. As camadas `intencao`, `planejamento`, `execucao`, `verificacao` e `iteracao` passam a ser tratadas como capacidades do sistema, nao como pipeline obrigatorio para toda tarefa.
2. O app deve escolher o fluxo mais adequado conforme o tipo de pedido:
   - continuidade, revisao e escrita sobre arquivo ativo: `chat assistido`
   - criacao de estrutura, comandos e alteracoes operacionais: `mission`
3. Quando houver arquivo ativo e o pedido implicar edicao, esse arquivo deve ser tratado como alvo prioritario.
4. Quando houver autorizacao e alvo suficiente, o agente deve privilegiar a criacao ou edicao de conteudo permanente no workspace, e nao apenas resposta no chat.
5. A autonomia deve ser graduada e rastreavel:
   - escopo de acesso conhecido
   - artefato-alvo identificavel
   - resultado salvo ou explicitamente pronto para salvar
   - historico e observacoes preservados
6. Em modo `mission`, a saida estruturada do modelo deve ser tratada como plano executavel quando possivel:
   - blocos JSON com `tool` sao executados em sequencia
   - envelopes `create_file` e `replace_file` sao materializados como `write_file`
   - comandos shell rodam no workspace ativo por padrao, mesmo quando o acesso esta em `Full-access`
   - observacoes de tool devem aparecer no historico da UI para que o usuario consiga auditar o que ocorreu

## Consequences

### Positivas

- reduz o desvio entre conversa e acao real
- melhora continuidade em tarefas editoriais e de codigo
- preserva criatividade sem engessar o modelo em um fluxo unico
- torna a autonomia mais util para arquivos, scaffolds e documentos persistentes
- reduz falsos positivos onde o chat afirmava ter criado/compilado algo sem evidencia operacional visivel

### Trade-offs

- aumenta a importancia de boas heuristicas de escolha de modo
- exige observabilidade melhor sobre o que foi aplicado, salvo ou apenas sugerido
- pode introduzir novos riscos se a aplicacao em arquivo nao tiver diff ou validacao suficiente
- executar varios blocos JSON por resposta aumenta a necessidade de bons limites de acesso e logs claros

## Operational Guidance

- priorizar arquivo ativo quando o pedido contiver verbos como `continuar`, `incluir`, `reescrever`, `revisar`, `editar`
- usar tools e mission loop quando o pedido implicar criar estrutura, rodar validacoes ou agir em varios artefatos
- manter possibilidade de aplicacao explicita da resposta ao arquivo ativo pela UI
- evoluir para diffs e aplicacao assistida antes de ampliar autonomia irrestrita
- para tarefas de codigo com validacao, preferir sequencias completas: criar/editar artefatos, executar comando, observar saida e so entao concluir
- em `Full-access`, liberar escopo de paths nao deve remover o diretorio de trabalho padrao do projeto; `run_command` deve continuar usando o workspace salvo como `cwd` inicial
