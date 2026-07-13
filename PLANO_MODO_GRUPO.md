# Modo Grupo — Plano de Trabalho

Sistema "siga o líder" por LoRa para uma **sala de até 8 carrinhos** (1 líder + seguidores), com
configuração pelo celular/telinha, mapa de navegação profissional e alerta pela estrada.
Substitui o modo 1:1 (que vira uma sala de 2). Artifact visual do plano gerado em 13/07/2026.

## Resumo
- **Hardware:** maioria **Waveshare 7" 1024×600 (ESP32-S3)**; CYD e GIGA de reserva. Tudo LovyanGFX.
- **Rádio:** LoRaMESH 915 MHz, esquema **beacon + slots**.
- **Sala:** código de **5 dígitos**, até **8** carrinhos.
- **Config:** WiFi (página web no líder) + teclado na telinha.

## Já pronto (reaproveitar)
Firmware 1:1 funcionando (GPS + LoRa + mapa heading-up + buffer de curvas cmd 0x12 + alerta bidirecional),
e a **linguagem visual profissional aprovada**: você = triângulo azul, líder = triângulo amarelo,
outros = bolinhas nas cores de cada um, paleta sóbria (sem neon), cartões discretos, estradas com contorno.

## 1. Arquitetura
Cada aparelho é um **nó** com: papel (líder/seguidor), sala (código), nome, cor e **slot** (a vez de falar).
Tudo salvo em NVS (sobrevive a desligar).
- **Líder:** cria a sala, emite o beacon (organiza o rádio), é quem todos seguem, tela limpa.
- **Seguidor:** entra pelo código, segue a rota do líder, vê os outros.
- **Sala:** o código vira filtro — cada pacote leva o código; só escuta o da própria sala.

## 2. Rádio (o coração) — beacon + slots
- **Líder apita** a cada rodada (manda a própria posição) → marca o "tempo zero" (sem relógio global).
- Cada seguidor tem um **slot** e responde num atraso fixo depois do apito → nunca colidem.
- Pacote leva: sala, id do nó, posição + **histórico rolante** (curvas, cmd 0x12 já pronto), flags (fix, alerta).
- **Alerta** com prioridade: ao apertar, marca flag e todos realçam a estrada até ele.
- **Descoberta/join:** nó novo anuncia id+nome+cor ao entrar; líder registra e repassa a lista.
- **Trade-off:** com 8 carros, cada posição atualiza a cada ~4–5s (não é tempo real). Menos carros = mais rápido.

## 3. Telas
- **Config:** Início (criar/entrar) → Entrar (teclado do código) → Nome e cor → Sala (lista no celular do líder).
- **Seguidor:** você = triângulo azul (centro), líder = triângulo amarelo, outros = bolinhas nas cores;
  rota do líder, distância, lista lateral, bússola.
- **Líder:** tela limpa (só ele + o caminho que traça); seguidores **só aparecem no alerta**.
- **Estados:** sem fix, sem sinal, fora de rota (pisca), alerta recebido (estrada vermelha), sala encerrada.
- Linguagem profissional (GPS de verdade), LovyanGFX; opcional **modo sol** (alto contraste).

## 4. Armazenamento
Papel, sala, nome, cor e slot em **NVS** (ESP32). Ao ligar, volta configurado.

## 5. Config pelo celular (WiFi)
Líder sobe WiFi próprio + **página web** (sem app, funciona em iPhone). Cria sala, vê roster (nome/cor),
ajusta papéis, aperta Iniciar. O líder é **ponte**: recebe os "entrei" pelo LoRa e mostra na página.

## 6. Fases de implementação (uma por vez, testável; dá pra testar com 3 aparelhos: CYD + 2 GIGAs)
1. **Rádio por vez (beacon + slots)** — CRÍTICA/fundação. Entrega: apito + rodízio sem colisão.
   Testar: 3 nós juntos 10 min sem cair; medir tempo de volta.
2. **Sala + papéis + memória.** Código na telinha, filtro de grupo, papel/nome/cor em NVS, descoberta.
   Testar: 3 aparelhos na mesma sala, papéis certos, sobrevive a desligar. (dep: F1)
3. **Mapa com vários nós.** Desenhar todos (azul/amarelo/bolinhas), rota, distância, lista, fora-de-rota.
   Testar: mover 3 nós e ver posições/cores certas. (dep: F1, F2)
4. **Alerta pela estrada.** Botão (só símbolo), TX prioritária, estrada vermelha seguindo o caminho até
   quem apertou (pra todos, inclusive líder). Testar: apertar num nó e ver em todos. (dep: F3)
5. **Config pelo celular (WiFi).** Página web no líder (criar/roster/cores/iniciar). Pode vir depois. (dep: F2)
6. **Polimento + teste de rua.** Modo sol, estados de erro, antena/alcance, trilha real com 3–4 carros.

## 7. Riscos
- Saturação do rádio → slots; teto 8. | Líder sai/cai → **decisão pendente**. | Alcance em trilha → antena/posição.
- FPS da Waveshare RGB → redesenhar só o que muda / buffer. | Sincronização → resolvida pelo beacon.

## 8. Decisões em aberto
1. Líder se vê azul (o "você" do aparelho) ou amarelo (o papel)?
2. Nome/cor na telinha de cada carro ou tudo pelo celular do líder?
3. Se o líder sair no meio, o que acontece?
4. Precisa de tela "trilha encerrada" / sair da sala?
5. Fase 1 num sketch de teste separado ou já no firmware atual?
