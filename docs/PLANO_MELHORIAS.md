# Plano de melhorias — rumo ao MTS completo

> Escrito em **2026-08-23**, depois de duas rodadas de revisão do `mts_p4` (nove
> defeitos corrigidos, ver `CHANGELOG.md`) e de uma análise propositor × crítico
> com agentes. Cobre **cinco frentes**: funcionalidades novas, otimização,
> visual, confiabilidade e testes.
>
> **Regra de ouro que ordena tudo**: nada de fase nova antes de o campo validar
> a anterior. O MVP de 2–3 telas é o portão da Fase 1.

---

## Inspiração: navegação em grupo do Google Maps

O Google vem testando navegação multi-carro. As funções dele, e o análogo MTS:

| Google Maps | Análogo MTS | Estado |
|---|---|---|
| Todos os carros no mapa | mapa com carros por slot | **já existe** |
| Rota compartilhada destacada | rota roxa do líder | **feito hoje** |
| Notificação de quem desviou da rota | faixa FORA (para os outros) | **feito hoje**; falta o aviso para **mim mesmo** (F3.2) |
| Ajuste de velocidade p/ manter o grupo unido | ritmo do grupo: delta de velocidade + tendência (F3.1) | novo — `speed` já viaja no pacote |
| Ponto de encontro sugerido | waypoint de reagrupamento (F3.4) | novo |
| Condições da estrada vindas do líder | pin de perigo (buraco, pedra, vau) (F3.5) | novo |
| Compartilhamento termina ao chegar | líder encerra a trilha para todos (F3.6) | novo |
| ETA do trajeto | "alcanço o líder em X min" (F3.3) | novo — km pelo caminho ÷ minha velocidade |
| Convite para o grupo | código de 5 dígitos | **já existe** |

O que **não** copiamos: tudo que supõe internet, servidor ou rota planejada em
banco de dados. O MTS é offline por natureza — a "rota" é o rastro real do
líder, o que para trilha é mais honesto que qualquer rota planejada.

---

## Fase 0 — validar o que existe (portão de tudo)

1. **Teste de campo com 2–3 telas** (checklist já no CHANGELOG): antena antes de
   energizar, slots separados, `conflito=0`, `preso=0`, rota roxa aparecendo no
   seguidor, ALERTA acendendo do outro lado.
2. **Rodar o `tdma_selftest` numa placa** (5 min) — a matemática nova (janela
   com air-time, TTL por frame) nunca rodou no silício.
3. **Soak de 2 h ligado** — antes do conserto dos timestamps a rede morreria aos
   71,6 min; o soak é a prova de que morreu de vez.

## Fase 1 — fundação de protocolo (pré-requisito das features de rede)

1. **Registro de tipos de pacote.** Hoje o discriminador é `len>=23 + bit 0x80`
   — frágil. Criar tabela única de tipos (POSICAO, ROSTER, e os futuros
   WAYPOINT/PERIGO/ENCERRA/HISTORIA), `rosterEh()` vira `pktTipo()`, com byte de
   versão. Sem isso, qualquer pacote novo vira carro teleportando em firmware
   antigo. (~60 l + espelho na bancada)
2. **Escalonador de fala do líder.** Roster, waypoint e futuros pacotes disputam
   os mesmos frames do líder; hoje a decisão está espalhada (`falas % 8`).
   Centralizar num escalonador que **garante a cadência mínima do beacon**
   (posição ≥ 1 a cada 2 frames, senão o TTL de 2,5 s dispara). (~40 l)
3. **Seletor de slot na tela de configuração** (0..7, tocável) — mata o laptop
   na trilha. 80% do JOIN automático por 30 linhas. *(Este pode entrar antes da
   Fase 0 — é o único que melhora o próprio teste.)*

## Fase 2 — confiabilidade

1. **Watchdog** (`esp_task_wdt` 8 s alimentado no hook) + motivo do reset no
   boot. Cuidados já mapeados: iniciar depois do `setup()`, excluir o
   `while(true)` do painel-não-subiu.
2. **Remontagem do cartão a quente** — socket push-push sob vibração é modo de
   falha documentado. Extrair `montaCartao()` do setup, N=8 erros seguidos →
   remonta fora do caminho do quadro, teto de tentativas.
3. **Qualidade de link por carro** — `seq` viaja e ninguém lê. Contar buracos
   (atenção: o seq é compartilhado entre roster e posição — contar nos dois
   caminhos), RSSI por carro, barrinhas de 4 níveis na faixa.
4. **Marcador esmaecido** para posição velha (>2 s só contorno) + balde de idade
   no `mudou()` para a repintura disparar.

## Fase 3 — funcionalidades novas (inspiração Google + análise dos agentes)

1. **Ritmo do grupo.** Na faixa de cada carro, seta de tendência
   (aproximando/afastando, pela derivada da km na rota) e delta de velocidade
   contra o líder. É o "ajuste de velocidade" do Google sem mandar ninguém
   acelerar: mostra o fato, o motorista decide. (~60 l, dado já no ar)
2. **Aviso "VOCÊ saiu da trilha".** O FORA de hoje aparece para os *outros*;
   quem saiu não fica sabendo. Moldura âmbar + texto quando `filaForaDaTrilha`
   vale para mim, com histerese (entra >150 m, sai <100 m). (~40 l)
3. **"Alcanço o líder em X min."** Distância pelo caminho ÷ minha velocidade
   média móvel; mostrar na faixa do líder. Escala honesta: só com velocidade
   >5 km/h. (~30 l)
4. **Waypoint de reagrupamento** *(depende da Fase 1)*: líder crava "esperem
   aqui" (botão, não long-press), pacote tipo WAYPOINT com lat/lon repetido pelo
   escalonador; bandeira + distância em todas as telas. (~180 l + bancada)
5. **Pin de perigo** *(depende da Fase 1)*: qualquer carro marca perigo na
   própria posição (buraco/pedra/vau); vira POI vermelho no mapa de todos por
   30 min. É a versão off-road do "condições da estrada". (~120 l + bancada)
6. **Líder encerra a trilha** *(depende da Fase 1)*: pacote ENCERRA repetido por
   1 min; seguidores recebem oferta de sair da sessão. Espelha o "termina ao
   chegar" do Google sem automatismo perigoso (ninguém é expulso sem tocar).
7. **Odômetro + altitude** (altitude só se o bloco DEM já está em cache).
8. **Buzzer no alerta** (buzzer passivo + LEDC; codec ES8311 descartado). Só
   alerta recebido + confirmação do próprio — sem bip de perda de sinal.
9. **Tema automático por pôr do sol** (NOAA simplificada + histerese 20 min,
   NVS com 3 estados).
10. **Pan "olhar o líder"** (toque na faixa centra nele 5 s; exige centro de
    mapa separado de `g_meuLat/Lon`; esconder anéis durante o pan).

11. **Atualização de frota pela internet** *(pedido em 2026-08-24; horizonte
    longo)*: hoje o OTA é rede local (PC e tela no mesmo WiFi). A visão: as
    telas conectadas em qualquer WiFi do mundo consultam um servidor ("há
    firmware novo?"), baixam por HTTPS e se atualizam — e um painel web lista a
    frota inteira (quem está online, versão de cada uma, subir para todas de
    uma vez). Exige: servidor de release, assinatura do binário (uma tela não
    pode aceitar firmware de qualquer um), rollback se o novo não bootar
    (a partição dupla já dá isso de graça) e política de "não atualizar em
    movimento". Pré-requisito natural: o WiFi do C6 se provar estável no uso
    de garagem por algumas semanas.

**Adiados com gatilho** (não entram neste plano até o gatilho acontecer):
JOIN automático (→ PPS fiado + 3 nós validados), catch-up de rota sob demanda
(→ PPS em todos), blackbox no cartão (→ `sd_bench` de escrita medido), ppm do
cristal (→ primeira bancada com PPS). **Mortos**: zoom AUTO, frame 2 s/16 slots
(até existir o 9º carro).

## Fase 4 — otimização (guiada pelos cronômetros que já existem)

O quadro imprime `relevo/vias/trajeto/carros + push` a cada 16 repintadas.
Otimizar **só o que o número acusar**, nesta ordem de suspeita:

1. **Fundo em sprite separado (cache de cena).** O fundo (relevo + vias) só muda
   quando o carro anda ou o zoom troca; o overlay (carros, rastro, HUD) muda a
   cada pacote. Desenhar o fundo num segundo sprite e, por quadro, blit fundo +
   overlay. Custa +1,2 MB de PSRAM (sobram ~30) e pode derrubar o quadro de
   ~73 ms para ~25 ms. **Medir antes e depois.**
2. **`rotaProjeta`/`trechoMaisPerto` incrementais.** Hoje O(n) por carro por
   quadro (1400 pts). Começar da última posição encontrada e expandir janela;
   full-scan só quando a distância mínima crescer demais. O(1) amortizado.
3. **SPI do rádio 2 → 8 MHz.** O SX1262 aceita 16; a leitura de pacote e o
   getIrqFlags encurtam. Ganho pequeno, custo zero — conferir com osciloscópio
   de software (contadores) que nada regrediu.
4. **`mudou()` mais barato**: sair do hash float para inteiros já quantizados.
   Micro; só se o loop principal aparecer no perfil.

Não-otimizações deliberadas: nada de FreeRTOS task para o rádio (o hook em
linha única de execução é decisão registrada — double lido pela metade vira
coordenada absurda); nada de mexer no PCLK/painel (custou caro achar o estável).

## Fase 5 — visual

1. **HUD consolidado**: velocidade atual + odômetro + altitude num cluster único
   (canto inferior esquerdo, acima da escala), fonte grande, tema-aware.
2. **Hierarquia do mapa ao sol**: revisar contraste dos rótulos de carro sobre
   fundo de cartão (halo hoje é 1 px; testar 2 px ao sol real).
3. **Faixas com estado composto**: cor do carro + barrinhas de link + tendência
   + distância — sem virar árvore de natal: máximo 2 elementos além do nome.
4. **Animação zero**: decisão mantida — nada de fade/slide; repintura seca.

## Fase 6 — testes (cada feature de rede paga o espelho)

1. Regra permanente: **feature que toca protocolo/tempo = firmware + espelho na
   bancada + caso no `tdma_replay` + checagem no `audit.js`** (custo ~2×,
   contado no orçamento de cada item acima).
2. **`vec_selftest`/`tile_selftest` seguem no CI manual** (rodar antes de cada
   gravação de campo).
3. **Roteiro de campo padronizado** (criar `docs/ROTEIRO_CAMPO.md`): partida a
   frio, fix, criar grupo, entrar, rota aparecendo, alerta ida-e-volta,
   sombra de sinal (desligar 30 s), religar no meio (RETOMAR), soak.
4. **Captura de serial em campo**: celular OTG na CH343 + app de terminal — o
   log de 5 s já imprime tudo; é a blackbox de custo zero até o `sd_bench` de
   escrita liberar a de verdade.

---

## Ordem de execução sugerida

```
Fase 1.3 (seletor de slot)  ─┐  antes do campo
Fase 0 (campo + selftest)   ─┘  ← PORTÃO
Fase 2 inteira (confiabilidade)         ~2 dias de bancada
Fase 1.1 + 1.2 (protocolo)              ~1 dia
Fase 3.1–3.3 (ritmo, self-FORA, ETA)    ~1 dia   ← só UI/dados locais
Fase 3.4–3.6 (waypoint, perigo, encerra)~2 dias  ← protocolo novo + bancada
Fase 4.1–4.2 (otimização medida)        ~1 dia
Fase 3.7–3.10 + Fase 5 (polish)         ~2 dias
```
