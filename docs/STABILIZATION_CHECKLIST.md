# Stabilization Checklist

## Goal

Consolidar o nucleo do agente antes de ampliar integracoes maiores.

## Completed
- [x] Refatoração Modular do UI (Decomposição do AgentUI.cpp)
- [x] Sistema de Persistência de Sessões (Histórico por projeto)

## Immediate

- [ ] Escolher framework de testes para C++ do projeto
- [ ] Criar alvo de testes no `CMakeLists.txt`
- [ ] Adicionar testes para `ToolRegistry`
- [ ] Adicionar testes para resolucao de paths e niveis de acesso
- [ ] Adicionar testes para contexto aprovado: workspace, biblioteca, web
- [ ] Adicionar testes minimos para ingestao e stats do RAG

## Refactoring

- [ ] Extrair validacao de path de `NativeTools.cpp`
- [ ] Extrair comandos de shell e utilitarios de processo
- [ ] Extrair pipeline de ingestao do RAG
- [ ] Extrair extracao de PDF/texto para modulo proprio
- [ ] Reduzir tamanho e acoplamento de `NativeTools.cpp`

## Agent Behavior

- [ ] Injetar resumo de contexto conversacional recente no modo assistido/autonomo
- [ ] Detectar documento-alvo em tarefas de continuidade
- [ ] Priorizar arquivo ja existente em perfis `writing-*`
- [ ] Mostrar melhor quando biblioteca aprovada ainda nao foi indexada
- [ ] Revisar heuristicas de autonomia por perfil com testes manuais guiados

## Observability

- [ ] Tornar erros silenciosos mais explicitos
- [ ] Melhorar mensagens de falha de tool-call
- [ ] Exibir melhor uso efetivo de contexto e fontes
- [ ] Registrar estado de sincronizacao do RAG com mais clareza

## After Stabilization

- [ ] Revisitar suporte mais amplo a MCP
- [ ] Revisitar plugins e ferramentas externas
- [ ] Avaliar evolucao de busca semantica mais sofisticada
