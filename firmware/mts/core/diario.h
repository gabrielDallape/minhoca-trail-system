#pragma once
// ============================================================================
//  DIARIO - o FORMATO do diario de bordo (CSV por tipo de linha)
// ============================================================================
//
// Quem abrir o CSV daqui a tres meses precisa entender sem abrir o firmware:
// o cabecalho descreve so o que sai, coluna por coluna. As strings moram aqui
// (core) para o teste no PC conferir que cada tipo de linha tem cabecalho e que
// as colunas da linha E batem com o que app/registro.h escreve.
//
// Tipos de linha (primeira coluna):
//   E  estado, 1 Hz          R  pacote recebido      T  transmissao (air-time MEDIDO)
//   N  ruido de fundo        G  evento do GPS        !  evento / aviso
//   C  caixa-preta do boot anterior (so apos reset violento)
//   V  pacote de versao de protocolo diferente (aparelho incompativel no ar)
//
// ms = millis desde o boot; utc = segundo do dia (0 = sem hora do GPS).

#define DIARIO_CAB_0 "# MTS fw v%d proto v%d - uma linha por evento, primeira coluna = tipo; ms desde o boot; utc = segundo do dia (0 = sem hora do GPS)"
#define DIARIO_CAB_E "# E,ms,utc,lat,lon,slot,sync(0 nada 1 beacon 2 pps),ancora,tx,perdi,preso,rx,ruim,conflito,rssiUlt,pwr,fix,sats,holdover,lacoPiorUs,lacoLimiteUs,seqPerdidos,ruidoUlt,ruidoMed,txAirUltUs,txAirMaxUs,rotaN,rotaPrefixo,catchups,relogioVoltou,pwrConfig,seguro,flushes,flushPiorUs,logDescartadas,versaoErrada,orcNegados,orcEstouros,sdErros,carrosAtivos"
#define DIARIO_CAB_R "# R,ms,utc,slotOrigem,seq,bytes,rssi,snr,freqErrHz,crcOk,perdidosDesdeUltimo,tipo(pos/roster/crc/sala/tamanho/versao)[,flags,pontosTrilha,pontosNovosNaRota]"
#define DIARIO_CAB_T "# T,ms,utc,seq,tipo(P/R),bytes,airMedidoUs,usNoQuadroAoIniciar,catchup,pontos"
#define DIARIO_CAB_N "# N,ms,utc,rssiInstantaneo(ruido de fundo, sem pacote),slotVazioOndeMediu"
#define DIARIO_CAB_G "# G,ms,evento,sats,vista,ppsRun,ppsCnt,sync    !,ms,evento,...    C,ms,caixa-preta do boot anterior    V,ms,utc,versaoOuvida,slotDeclarado,bytes,rssi"

// Quantas colunas a linha E tem (contadas no cabecalho acima): o registro.h
// tem um static_assert contra este numero para o cabecalho nunca mentir.
#define DIARIO_E_COLUNAS 40
