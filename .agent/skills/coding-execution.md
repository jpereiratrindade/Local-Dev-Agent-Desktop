# skill: coding-execution

## Quando usar
- pedido para implementar funcionalidade
- editar codigo existente
- corrigir bug, build ou integracao
- criar estrutura real de arquivos para software

## Funcao
Guiar execucao pratica sobre codigo sem reduzir a tarefa a explicacoes abstratas.

## Fluxo sugerido
1. Inspecione apenas os arquivos e entradas realmente relevantes.
2. Monte um plano curto de implementacao.
3. Execute com ferramentas do workspace.
4. Valide com build, teste, leitura objetiva ou comando apropriado.
5. Resuma o que mudou, impacto e evidencias.

## Edicao de arquivos existentes
- Antes de alterar um arquivo existente, leia o trecho relevante.
- Aplique a mudanca com `apply_patch` quando for uma edicao localizada ou `write_file` quando reescrever o arquivo inteiro for mais seguro.
- Depois da edicao, releia o arquivo e confirme que o trecho esperado esta presente.
- So conclua depois de validar a edicao e, quando aplicavel, rodar build/teste.

## Flexibilidade
- Esta skill e um ponto de partida, nao uma receita fechada.
- Se o projeto pedir outro caminho, adapte a abordagem.
- Combine com outras skills quando isso ajudar a entrega.
