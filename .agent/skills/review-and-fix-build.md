# skill: review-and-fix-build

## Quando usar
- build quebrando
- erros de compilacao
- regressao apos alteracoes

## Objetivo
Reproduzir falha, localizar causa, corrigir com mudancas pequenas e validar novamente.

## Fluxo
1. Reproduza com `run_command`.
2. Leia apenas os arquivos implicados pelo erro.
3. Corrija a menor causa raiz provavel.
4. Rode novamente o build.
5. Resuma erro inicial, causa e validacao final.

## Regras
- Nao faça refatoracao ampla durante correcoes de build.
- Se houver multiplas falhas, ataque pela primeira causa real.
- Cite a saida relevante do compilador ou comando ao concluir.
