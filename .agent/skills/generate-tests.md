# skill: generate-tests

## Quando usar
- pedido para criar testes
- pedido para cobrir comportamento existente
- pedido para reduzir regressao

## Objetivo
Adicionar testes pequenos e executaveis que cubram comportamento observavel.

## Fluxo
1. Leia os arquivos-alvo e identifique comportamento testavel.
2. Descubra framework de testes existente; se nao houver, proponha o menor caminho viavel.
3. Crie testes por cenarios, nao por detalhes internos.
4. Rode os testes ou pelo menos o build correspondente.

## Regras
- Priorize casos criticos, bordas e regressao.
- Evite snapshots ou testes vazios.
- Se a infraestrutura de testes estiver ausente, deixe isso explicito e adicione o minimo necessario quando for seguro.
