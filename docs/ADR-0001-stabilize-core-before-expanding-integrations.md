# ADR-0001: Stabilize Core Before Expanding Integrations

## Status
Accepted

## Context

O projeto evoluiu rapidamente de uma GUI local para Ollama para uma base de agente com:

- perfis cognitivos
- skills persistentes
- tools nativas de workspace
- controle de acesso
- contexto local, biblioteca e web aprovada
- cache local de RAG

Essa fundacao e promissora, mas a complexidade aumentou em areas sensiveis:

- `src/NativeTools.cpp` concentra responsabilidades demais
- o comportamento entre chat assistido e modo autonomo ainda precisa ser consolidado
- faltam testes automatizados para regras criticas de acesso, contexto e tools
- integracoes futuras como MCP podem ampliar ainda mais a superficie de falha

## Decision

Antes de expandir significativamente em novas integracoes e features, o projeto deve priorizar estabilizacao do nucleo.

As prioridades imediatas passam a ser:

1. testes automatizados para componentes criticos
2. refatoracao de `NativeTools.cpp` em modulos coesos
3. melhoria de continuidade de contexto entre chat assistido, historico e missao
4. observabilidade e tratamento de erros mais claros

Integracoes maiores, como MCP mais amplo, plugin system ou banco vetorial mais sofisticado, continuam desejaveis, mas entram depois da consolidacao dessa base.

## Consequences

### Positivas

- reduz risco de regressao silenciosa
- melhora previsibilidade do agente em tarefas reais
- facilita manutencao do codigo com crescimento de funcionalidade
- cria base mais segura para MCP, plugins e evolucao do RAG

### Trade-offs

- desacelera entrega de features novas no curto prazo
- exige investimento em infraestrutura de testes e refatoracao antes de expansao visivel

## Priority Areas

### 1. Tests

- `ToolRegistry`
- regras de acesso e resolucao de paths
- comportamento de contexto aprovado
- RAG basico: ingestao, stats e busca

### 2. Refactoring

- separar validacao de paths
- separar extracao de texto/PDF
- separar ingestao/chunking/cache do RAG
- reduzir acoplamento shell onde for viavel

### 3. Agent Continuity

- preservar melhor contexto conversacional recente
- detectar arquivo-alvo em tarefas de continuidade
- reduzir exploracao mecanica em perfis de escrita e pesquisa

### 4. Observability

- tornar falhas mais visiveis
- evitar `catch (...)` silenciosos quando isso esconder problema importante
- melhorar diagnostico de contexto, rag e tool use
