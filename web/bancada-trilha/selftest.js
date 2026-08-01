// Auto-teste do simulador: roda o script de index.html em Node com um DOM/canvas
// falso e verifica o fluxo de grupo (criar/entrar/JOIN/ROSTER/WORLD/UPLINK), o
// modo P2P com catch-up, o desenho das telas, o toque e os achados que a pagina
// afirma sobre o firmware. Nao precisa de browser nem de hardware:
//
//   node web/bancada-trilha/selftest.js
const fs=require("fs");
const path=require("path");
const html=fs.readFileSync(path.join(__dirname,"index.html"),"utf8");
const m=html.match(/<script>([\s\S]*?)<\/script>/);
if(!m){ console.log("FAIL: bloco <script> nao encontrado"); process.exit(1); }
let src=m[1];

// ---- mocks de DOM/canvas ----
const noop=()=>{};
const ctxStub=new Proxy({},{get:(t,k)=>{
  if(k==="measureText") return ()=>({width:10});
  return noop;
}, set:()=>true});
function mkEl(){
  const el={
    addEventListener:noop, setAttribute:noop, getAttribute:()=>"false",
    classList:{add:noop,remove:noop,toggle:noop}, dataset:{}, style:{setProperty:noop},
    getContext:()=>ctxStub, getBoundingClientRect:()=>({left:0,top:0,width:1024,height:600}),
    appendChild:noop, querySelector:()=>mkEl(), querySelectorAll:()=>[],
    scrollTop:0, scrollHeight:0, clientHeight:0,
  };
  Object.defineProperty(el,"innerHTML",{get:()=>"",set:noop});
  Object.defineProperty(el,"textContent",{get:()=>"",set:noop});
  Object.defineProperty(el,"className",{get:()=>"",set:noop});
  return el;
}
global.document={querySelector:()=>mkEl(), querySelectorAll:()=>[],
  createElement:()=>mkEl(), body:mkEl(), addEventListener:noop};
global.window=global;
global.requestAnimationFrame=()=>1;
global.getComputedStyle=()=>({getPropertyValue:()=>""});

// expoe o escopo interno para inspecao
src+=`
;module.exports={
  get devs(){return devs}, radio, chan, sim, get MODE(){return MODE}, setMode:v=>{MODE=v},
  worldStep, devLoop, chanTick, runScript, roteiro, bootDevices, applyModeFlags,
  frame, get gfx(){return gfx}, handleTouch, haversine, isLeader, s16, TRAIL_N, HIST_N,
  get T(){return T}, bump:dt=>{T+=dt}, txPacket, CMD_UPLINK, ROUTE_MAX, MAXN, kpRect, KP,
  ghostLoop, get ghosts(){return ghosts}, TDMA_AIR_MS, tdmaFits, tdmaHeadroomUs, tdmaMaxNodes,
};`;

const mod={exports:{}};
new Function("module","exports","require",src)(mod,mod.exports,require);
const S=mod.exports;

const STEP=25;          // modo GRUPO/P2P
const STEP_T=5;          // modo TDMA: menor que a guarda de 15 ms (ver index.html)
function run(ms){
  for(let i=0;i<ms/STEP;i++){
    S.bump(STEP); S.runScript(); S.worldStep(STEP); S.chanTick();
    for(const d of S.devs) if(d.on) S.devLoop(d);
  }
}
let pass=0,fail=0;
const ck=(name,cond,info="")=>{ (cond?pass++:fail++);
  console.log((cond?"PASS ":"FAIL ")+name+(info?"   "+info:"")); };
const sec=t=>console.log("\n-- "+t);

/* =================== MODO GRUPO =================== */
sec("modo GRUPO: criar o grupo e entrar (roteiro pelo handleTouch)");
const D=S.devs;
ck("as 4 telas comecam na tela inicial", D.every(d=>d.g_room===0&&!d.joined));
run(1000);
ck("Tela 1 foi para o teclado depois de CRIAR GRUPO", D[0].uiPage===1&&D[0].pendingRole===1);
run(3000);
ck("Tela 1 criou o grupo 12345 como LIDER",
   D[0].g_room===12345&&S.isLeader(D[0])&&D[0].curSlot===0, `room=${D[0].g_room} role=${D[0].g_role}`);
run(12000);
ck("as 3 outras entraram no mesmo grupo", D.slice(1).every(d=>d.g_room===12345),
   D.slice(1).map(d=>d.g_room).join("/"));
ck("cada seguidor recebeu um slot distinto pelo ROSTER",
   D[1].joined&&D[2].joined&&D[3].joined&&
   new Set([D[1].curSlot,D[2].curSlot,D[3].curSlot]).size===3,
   `slots=${D[1].curSlot},${D[2].curSlot},${D[3].curSlot}`);
ck("o lider alocou os uids no roster",
   [1,2,3].every(k=>D[0].ruid[k]!==0), D[0].ruid.slice(0,4).join("/"));
ck("os nomes do JOIN chegaram no roster do lider",
   D[0].rname[1]==="Tela 2"&&D[0].rname[3]==="Tela 4",
   `${D[0].rname[1]} / ${D[0].rname[2]} / ${D[0].rname[3]}`);
ck("nenhum seguidor esta mais em PROCURANDO GRUPO", D.slice(1).every(d=>!d.searching));

sec("modo GRUPO: trafego de posicao e trajeto");
run(20000);
ck("os seguidores reconstruiram o trajeto do lider",
   D.slice(1).every(d=>d.routeN>15), D.slice(1).map(d=>d.routeN).join("/"));
ck("cada seguidor ve o lider no world[0]", D.slice(1).every(d=>d.world[0].active));
const dist12=S.haversine(D[0].myLat,D[0].myLon,D[1].myLat,D[1].myLon);
const dist13=S.haversine(D[0].myLat,D[0].myLon,D[2].myLat,D[2].myLon);
ck("os carros estao espacados pelo vao configurado",
   Math.abs(dist12-S.sim.gapM)<30 && Math.abs(dist13-2*S.sim.gapM)<40,
   `T2 ${dist12.toFixed(0)}m  T3 ${dist13.toFixed(0)}m  (vao ${S.sim.gapM}m)`);

sec("modo GRUPO: lockstep de fase e colisao (o argumento do TDMA)");
const colAntes=S.radio.collisions;
S.txPacket(D[1],"LEADER",S.CMD_UPLINK,new Uint8Array(13));
S.txPacket(D[2],"LEADER",S.CMD_UPLINK,new Uint8Array(13));
run(2000);
ck("dois uplinks no mesmo instante colidem e se perdem",
   S.radio.collisions>=colAntes+2, `colisoes: ${colAntes} -> ${S.radio.collisions}`);
// mede a taxa de colisao de uplink com as fases IGUAIS (o pior caso que o
// firmware permite) e depois com fase sorteada. Forcar as fases torna o teste
// determinista - na bancada o resultado varia entre 3 % e 100 % por rodada.
// conta so os UPLINK: a colisao global inclui WORLD/ROSTER e falsearia a taxa.
// Os objetos de pacote sobrevivem a entrega, entao 'bad' pode ser lido no fim.
function taxaUplink(ms){
  const vistos=new Set();
  for(let i=0;i<ms/STEP;i++){
    S.bump(STEP); S.runScript(); S.worldStep(STEP); S.chanTick();
    for(const d of S.devs) if(d.on) S.devLoop(d);
    for(const k of S.chan.inAir) if(k.cmd===S.CMD_UPLINK) vistos.add(k);
  }
  let bad=0; for(const k of vistos) if(k.bad) bad++;
  return {tx:vistos.size, col:bad, pct:Math.round(100*bad/Math.max(1,vistos.size))};
}
// o firmware agora respeita o slotDue; para medir o lockstep de antes e preciso
// desligar isso de proposito
S.sim.useSlot=false;
D.slice(1).forEach(d=>{ d.lu=S.T; });          // lockstep forcado
const ret0=S.radio.retries;
const lock=taxaUplink(30000);
ck("com as fases iguais a colisao e alta e o modulo tem de reenviar",
   lock.pct>=35 && S.radio.retries>ret0,
   `${lock.col}/${lock.tx} transmissoes colidiram = ${lock.pct} %, ${S.radio.retries-ret0} reenvios`);
// o ponto que corrige a versao anterior desta bancada: com ACK, a entrega
// acontece. O sintoma do descompasso e trafego e latencia, nao carro perdido.
ck("mesmo em lockstep o ACK do modulo entrega: o lider ve os 3 seguidores",
   [1,2,3].every(k=>D[0].world[k].active&&S.T-D[0].world[k].lastMs<15000),
   [1,2,3].map(k=>`${k}:${((S.T-D[0].world[k].lastMs)/1000).toFixed(1)}s`).join(" "));
// o conserto: respeitar o slotDue que o parseRx ja calcula. Os primeiros uplinks
// depois de ligar saem juntos (todos tinham pendingUplink pendente e slotDue no
// passado) - transiente da troca de modo, nao do esquema: mede depois dele.
S.sim.useSlot=true; run(5000);
const slot=taxaUplink(30000);
ck("usando o slotDue a colisao de uplink vai a zero",
   slot.col===0, `${slot.col} colisoes em ${slot.tx} uplinks = ${slot.pct} %`);
run(20000);
ck("e o lider volta a ver os 3 seguidores com dado fresco",
   [1,2,3].every(k=>D[0].world[k].active&&S.T-D[0].world[k].lastMs<15000),
   [1,2,3].map(k=>`${k}:${((S.T-D[0].world[k].lastMs)/1000).toFixed(1)}s`).join(" "));
S.sim.useSlot=false;

sec("achados que a pagina afirma sobre o firmware");
// Cenario controlado: todos veem os 4 nos ativos e recentes. Isso isola a REGRA
// de desenho (peer = primeiro no ativo com slot != o meu) do sorteio do radio.
function cenario(quemAlerta){
  for(const d of D){
    d.myAlert=(d.curSlot===quemAlerta && !(S.isLeader(d)&&quemAlerta!==0));
    for(let k=0;k<4;k++){ const w=d.world[k];
      w.active=true; w.fix=true; w.lastMs=S.T; w.alert=(k===quemAlerta); }
  }
  D.forEach((d,i)=>S.frame(d,S.gfx[i]));
  return D.map(d=>d._flags.alertOn?"1":"0").join("");
}
// CORRIGIDO: o alerta de qualquer carro acende em todo mundo. Antes o drawMap
// olhava so o primeiro peer ativo e o alerta de um slot alto nao acendia em
// ninguem - com o fixAlerta desligado da para ver como era.
const a3=cenario(3);
ck("o alerta do slot 3 acende em todas as telas", a3==="1111", "alertOn: "+a3);
S.sim.fixAlerta=false;
const antigo=cenario(3);
ck("ACHADO (como era): sem a correcao, o alerta do slot 3 so acendia nele",
   antigo==="0001", "alertOn: "+antigo);
S.sim.fixAlerta=true;
const a1=cenario(1);
ck("o alerta do slot 1 tambem acende em todos", a1==="1111", "alertOn: "+a1);
D.forEach(d=>{ d.myAlert=false; }); run(3000);
D[2].stray=true; run(6000); S.frame(D[2],S.gfx[2]);
ck("desviar da trilha levanta FORA DO TRAJETO no seguidor",
   D[2]._flags.offRoute===true, `myBest=${(D[2]._flags.myBest||0).toFixed(0)}m`);
D[2].stray=false; run(6000);
// vao permanente: o WORLD leva no maximo HIST_N pontos, entao um corte de 2
// ciclos (7,2 s) perde mais pontos do que um WORLD consegue repor. Sem catch-up
// o deficit de route[] nunca volta - medir por routeN e determinista.
// O vao se mede como o firmware o desenha: par de pontos consecutivos do route[]
// com mais de 40 m entre eles (o limiar da ponte tracejada em drawMap). Contar
// pontos nao serve: o seguidor atrasado engole 12 por WORLD e o total se aproxima
// sem que os pontos do meio voltem.
function buracos(d){
  let n=0;
  for(let k=1;k<d.routeN;k++){
    const a=d.route[(d.routeHead-d.routeN+k-1+S.ROUTE_MAX)%S.ROUTE_MAX];
    const b=d.route[(d.routeHead-d.routeN+k+S.ROUTE_MAX)%S.ROUTE_MAX];
    if(S.haversine(a.lat,a.lon,b.lat,b.lon)>40) n++;
  }
  return n;
}
// 20 s de corte a 32 km/h = ~178 m de trilha perdida; o WORLD repoe 60 m
// (HIST_N * STEP_M), entao sobra buraco de ~118 m > 40 m do tracejado.
const bAntes=buracos(D[1]);
S.radio.down=true; run(20000); S.radio.down=false; run(15000);
const bMeio=buracos(D[1]);
run(20000);
const bFim=buracos(D[1]);
ck("ACHADO: um corte de 20 s abre um vao na rota do seguidor",
   bMeio>bAntes, `buracos >40 m: ${bAntes} -> ${bMeio}`);
ck("ACHADO: o vao NAO fecha depois (sem catch-up no modo grupo)",
   bFim>=bMeio, `buracos: ${bMeio} -> ${bFim} apos 20 s de link limpo`);

sec("achado do carro zumbi (so aparece com 3+ nos)");
// A Tela 4 desaparece de vez (sem energia, fora de alcance). Nada no firmware
// zera world[].active: o lider segue empacotando active=1 e o parseRx do
// seguidor refresca lastMs a cada WORLD -> o carro morto nunca expira lá.
S.sim.useSlot=true; run(8000);                 // sem lockstep, todos reportando
const t4=D[3]; t4.on=false;                    // a Tela 4 sai do ar
run(40000);                                    // muito mais que NODE_TTL (15 s)
const pos1={lat:D[1].world[3].lat,lon:D[1].world[3].lon};
const idadeNoLider=(S.T-D[0].world[3].lastMs)/1000;
const idadeNoSeg  =(S.T-D[1].world[3].lastMs)/1000;
ck("ACHADO: o lider percebe (idade do slot 3 passa do TTL de 15 s)",
   idadeNoLider>15, `idade no lider = ${idadeNoLider.toFixed(1)} s`);
// CORRIGIDO: o lider expira quem parou de reportar e para de anunciar active=1,
// entao o carro morto tambem sai do mapa dos seguidores
ck("o carro que saiu do ar sai do mapa dos seguidores tambem",
   D[1].world[3].active===false, `active=${D[1].world[3].active} idade=${idadeNoSeg.toFixed(1)} s`);
S.sim.fixZumbi=false;
const t4b=D[3]; t4b.on=true; run(8000); t4b.on=false; run(40000);
ck("ACHADO (como era): sem a correcao o carro morto ficava 'fresco' no seguidor",
   D[1].world[3].active===true && (S.T-D[1].world[3].lastMs)/1000<4.5,
   `active=${D[1].world[3].active} idade=${((S.T-D[1].world[3].lastMs)/1000).toFixed(1)} s`);
S.sim.fixZumbi=true;
t4.on=true; S.sim.useSlot=false; run(5000);

sec("modo GRUPO: telas e toque");
try{
  D.forEach((d,i)=>S.frame(d,S.gfx[i]));
  D[1].myFix=false; S.frame(D[1],S.gfx[1]); D[1].myFix=true;   // PROCURANDO GPS
  const t=D[3], g0=t.g_room;
  t.g_room=0; t.uiPage=0; S.frame(t,S.gfx[3]);                 // home
  t.uiPage=1; S.frame(t,S.gfx[3]);                             // teclado
  t.editName=true; S.frame(t,S.gfx[3]);                        // ajustes
  t.editName=false; t.g_room=g0; t.searching=true; S.frame(t,S.gfx[3]);  // procurando grupo
  t.searching=false; t.uiPage=0;
  ck("todas as telas desenham sem excecao", true);
}catch(e){ ck("todas as telas desenham sem excecao", false, e.message); }

S.frame(D[0],S.gfx[0]);
const zoom=D[0].mapMPP;
S.handleTouch(D[0],D[0].abX,D[0].abY);
ck("toque no FAB liga o alerta", D[0].myAlert===true);
S.handleTouch(D[0],D[0].abX,D[0].abY);
S.handleTouch(D[0],D[0].zmX+30,D[0].zmY+30);
ck("toque no + aproxima", D[0].mapMPP<zoom, `${zoom} -> ${D[0].mapMPP}`);
S.handleTouch(D[0],D[0].exX+10,D[0].exY+10);
ck("SAIR DO GRUPO volta para a tela inicial", D[0].g_room===0&&!D[0].joined);

/* =================== MODO P2P =================== */
sec("modo P2P: par fixo, trajeto com catch-up por ack");
S.setMode("P2P"); S.bootDevices(); S.roteiro();
ck("so 2 telas ficam ativas no P2P",
   S.devs.filter(d=>d.on).length===2, S.devs.map(d=>d.on?"on":"off").join("/"));
run(30000);
const P=S.devs;
ck("o seguidor acompanha o trajeto do lider",
   Math.abs(P[0].routeSeq-P[1].lastRouteSeq)<=2, `lider=${P[0].routeSeq} seguidor=${P[1].lastRouteSeq}`);
ck("o lider conhece o ack do seguidor", P[0].peerAckKnown&&P[0].peerAck>0, `ack=${P[0].peerAck}`);
const antes=P[1].lastRouteSeq;
S.radio.down=true; run(30000);
ck("com sinal cortado o seguidor para de receber (tolera 1 em voo)",
   P[1].lastRouteSeq-antes<=1, `${antes} -> ${P[1].lastRouteSeq}`);
ck("o ack ficou para tras alem de TRAIL_N",
   S.s16(P[0].peerAck-P[0].routeSeq)<-S.TRAIL_N, `atraso=${S.s16(P[0].peerAck-P[0].routeSeq)}`);
S.radio.down=false; run(25000);
ck("o catch-up recuperou e convergiu",
   P[1].lastRouteSeq>antes+20 && Math.abs(P[0].routeSeq-P[1].lastRouteSeq)<=3,
   `lider=${P[0].routeSeq} seguidor=${P[1].lastRouteSeq}`);

/* ============ PROVA: e firmware ou e a bancada? ============
   Repete os achados de logica com RADIO IDEAL - entrega instantanea, sem
   colisao, sem perda. O que continuar errado nao pode ser culpa do modelo de
   rede: esta no codigo. Foi assim que se descobriu que a versao anterior desta
   bancada exagerava o efeito da colisao (modelava o LoRaMESH como radio cru,
   sem o ACK que o modulo tem). */
sec("PROVA com radio ideal: os achados sao do firmware, nao do modelo de radio");
S.setMode("GRUPO"); S.bootDevices(); S.radio.ideal=true; S.roteiro();
run(30000);
const I=S.devs;
ck("com radio ideal os 4 entram e o lider ve todos com dado fresco",
   [1,2,3].every(k=>I[0].world[k].active&&S.T-I[0].world[k].lastMs<2000)
   && S.radio.collisions===0 && S.radio.lost===0,
   `colisoes=${S.radio.collisions} perdas=${S.radio.lost} idades=`+
   [1,2,3].map(k=>((S.T-I[0].world[k].lastMs)/1000).toFixed(1)+"s").join("/"));
// 1. alerta: a regra do peer unico nao depende do radio
for(const d of I){
  d.myAlert=(d.curSlot===3 && !S.isLeader(d));
  for(let k=0;k<4;k++){ const w=d.world[k];
    w.active=true; w.fix=true; w.lastMs=S.T; w.alert=(k===3); }
}
I.forEach((d,i)=>S.frame(d,S.gfx[i]));
const aI=I.map(d=>d._flags.alertOn?"1":"0").join("");
ck("PROVA: com radio ideal o alerta corrigido acende em todos", aI==="1111", "alertOn: "+aI);
S.sim.fixAlerta=false;
I.forEach((d,i)=>S.frame(d,S.gfx[i]));
const aIold=I.map(d=>d._flags.alertOn?"1":"0").join("");
ck("PROVA: e o bug antigo tambem aparecia com radio ideal (era logica, nao radio)",
   aIold==="0001", "alertOn: "+aIold);
S.sim.fixAlerta=true;
I.forEach(d=>{ d.myAlert=false; }); run(3000);
// 2. zumbi: o refresh de lastMs no parseRx nao depende do radio
const z=I[3]; z.on=false; run(40000);
const zL=(S.T-I[0].world[3].lastMs)/1000;
ck("PROVA: com radio ideal o zumbi tambem esta corrigido",
   !I[1].world[3].active, `idade no lider ${zL.toFixed(0)}s, active no seguidor=${I[1].world[3].active}`);
S.sim.fixZumbi=false;
const z2=I[3]; z2.on=true; run(8000); z2.on=false; run(40000);
ck("PROVA: sem a correcao o zumbi aparecia mesmo com radio ideal",
   I[1].world[3].active===true,
   `active=${I[1].world[3].active} idade=${((S.T-I[1].world[3].lastMs)/1000).toFixed(1)}s`);
S.sim.fixZumbi=true;
z.on=true; run(4000);
// 3. o vao da rota: o WORLD nao carrega o que falta, mesmo com radio perfeito
const bA=buracos(I[1]);
S.radio.ideal=false; S.radio.down=true; run(20000);       // so um corte pode abrir o vao
S.radio.down=false; S.radio.ideal=true; run(20000);        // volta com radio PERFEITO
ck("PROVA: com radio perfeito depois do corte, o vao NAO fecha",
   buracos(I[1])>bA, `buracos >40 m: ${bA} -> ${buracos(I[1])}`);
S.radio.ideal=false;

/* ============ ROBUSTEZ: cliques aleatorios nas 4 telas ============
   Pega o que eu nao imagino: sequencia de toques que leva a estado impossivel,
   excecao no desenho ou tela travada. O firmware nao pode quebrar porque alguem
   apertou coisas fora de ordem - e a bancada tambem nao. */
sec("robustez: 600 toques aleatorios + redesenho");
S.setMode("GRUPO"); S.bootDevices(); S.radio.ideal=false; S.roteiro(); run(20000);
let excecoes=0, desenhos=0;
for(let n=0;n<600;n++){
  const d=S.devs[n%4];
  try{ S.handleTouch(d, Math.floor(Math.random()*1024), Math.floor(Math.random()*600)); }
  catch(e){ excecoes++; if(excecoes<3) console.log("   excecao no toque:", e.message); }
  if(n%25===0){
    run(300);
    try{ S.devs.forEach((x,i)=>{ if(x.on) S.frame(x,S.gfx[i]); }); desenhos++; }
    catch(e){ excecoes++; if(excecoes<3) console.log("   excecao no desenho:", e.message); }
  }
}
ck("600 toques aleatorios sem excecao", excecoes===0, `${excecoes} excecoes, ${desenhos} redesenhos`);
// nenhum estado impossivel: quem tem grupo tem papel coerente, quem nao tem esta na home
const incoerentes=S.devs.filter(d=>{
  if(d.g_room===0) return d.joined||d.searching;
  if(S.isLeader(d)) return d.curSlot!==0;
  return d.joined && (d.curSlot<1||d.curSlot>=S.MAXN);
});
ck("nenhuma tela em estado impossivel", incoerentes.length===0,
   incoerentes.map(d=>`${d.label}: room=${d.g_room} role=${d.g_role} slot=${d.curSlot} joined=${d.joined}`).join(" | "));
// e a simulacao continua viva depois da bagunca
S.bootDevices(); S.roteiro(); run(30000);
ck("depois da bagunca, um novo grupo se forma normalmente",
   S.devs.slice(1).every(d=>d.joined) && S.devs[0].g_room===12345,
   `grupo=${S.devs[0].g_room} slots=`+S.devs.slice(1).map(d=>d.curSlot).join(","));

/* ============ ACHADO: sair do grupo nao limpa nada ============ */
sec("SAIR DO GRUPO: nao deixa mais lixo do grupo anterior");
const A=S.devs[1];
const ptsAntes=A.routeN;
S.frame(A,S.gfx[1]);
S.handleTouch(A,A.exX+10,A.exY+10);                 // SAIR DO GRUPO
ck("CORRIGIDO: sair limpa o route[] do grupo anterior",
   A.routeN===0 && A.g_room===0, `route[] ${ptsAntes} -> ${A.routeN}, room=${A.g_room}`);
ck("CORRIGIDO: e limpa world[].active (nenhum carro fantasma fica marcado)",
   A.world.every(w=>!w.active));
// reentrar em OUTRO grupo: o traçado velho continua e o novo e emendado nele
A.pendingRole=0; A.uiPage=1; A.codeBuf="";
for(const ch of "54321") S.handleTouch(A,...(()=>{const r=S.kpRect(S.KP.indexOf(ch));return [r.x+r.w/2|0,r.y+r.h/2|0];})());
S.handleTouch(A,...(()=>{const r=S.kpRect(S.KP.indexOf("OK"));return [r.x+r.w/2|0,r.y+r.h/2|0];})());
ck("CORRIGIDO: entrando em outro grupo, a tela comeca limpa",
   A.g_room===54321 && A.routeN===0, `grupo=${A.g_room} route[]=${A.routeN} pts`);

/* ============ MODO TDMA (Fase 4) ============
   Roda o tdma_core.h + tdma_test.ino portados, com clock proprio por no. */
sec("TDMA: capacidade e conta do slot");
S.setMode("TDMA"); Object.assign(S.sim,{slots:8,frameSecs:1,ghosts:0,sync:"beacon",fixFrameBase:false});
Object.assign(S.radio,{ideal:false,loss:0,down:false});
S.bootDevices();
const airUs=S.TDMA_AIR_MS*1000, t0=S.devs[0].tdma;
ck("air-time de 16 B em SF7/BW125 fica na faixa que o plano estima (40-60 ms)",
   S.TDMA_AIR_MS>40 && S.TDMA_AIR_MS<60, S.TDMA_AIR_MS.toFixed(1)+" ms");
ck("o pacote cabe no slot de 125 ms", S.tdmaFits(t0,airUs),
   `folga=${(S.tdmaHeadroomUs(t0,airUs)/1000).toFixed(0)} ms`);
ck("cabem 15 nos num frame de 1 s", S.tdmaMaxNodes(t0,airUs)===15,
   S.tdmaMaxNodes(t0,airUs)+" nos");

sec("TDMA: o bug do frameBase (divisao inteira) - fase 2, beacon");
function txPorNo(ppms,sync,fix){
  S.setMode("TDMA");
  Object.assign(S.sim,{slots:8,frameSecs:1,ghosts:0,sync,fixFrameBase:!!fix});
  Object.assign(S.radio,{ideal:false,loss:0,down:false});
  S.bootDevices();
  S.devs.forEach((d,i)=>{ d.ppm=ppms[i]; d.clkUs=1000000; });
  runTdma(60000);
  return S.devs.map(d=>d.tdma.txCount);
}
function runTdma(ms){
  for(let i=0;i<ms/STEP_T;i++){
    S.bump(STEP_T);
    for(const n of S.devs) if(n.on) n.clkUs+=STEP_T*1000*(1+n.ppm/1e6);
    for(const g of S.ghosts) g.clkUs+=STEP_T*1000*(1+g.ppm/1e6);
    S.worldStep(STEP_T); S.chanTick();
    for(const d of S.devs) if(d.on) S.devLoop(d);
    for(const g of S.ghosts) S.ghostLoop(g);
  }
}
const lentos=txPorNo([0,-30,-20,-10],"beacon");
ck("ACHADO: no beacon, cristal mais lento que a ancora = no calado",
   lentos[0]>=55 && lentos.slice(1).every(v=>v<=3), "tx = "+lentos.join(" / "));
const rapidos=txPorNo([0,30,20,10],"beacon");
ck("ACHADO (contraste): cristal mais rapido funciona - por isso passa com 2 nos",
   rapidos.every(v=>v>=55), "tx = "+rapidos.join(" / "));
const corrigido=txPorNo([0,-30,-20,-10],"beacon",true);
ck("a correcao (arredondar, minimo 1) devolve o TX a todos",
   corrigido.every(v=>v>=55), "tx = "+corrigido.join(" / "));
const comPps=txPorNo([0,-40,25,-15],"pps");
ck("com PPS o problema nao existe (a borda e simultanea)",
   comPps.every(v=>v>=55), "tx = "+comPps.join(" / "));
const misto=txPorNo([0,-40,25,-15],"misto");
ck("ACHADO: somar PPS ao beacon (sem apagar a linha) cala nos",
   misto.slice(1).some(v=>v<=3), "tx = "+misto.join(" / "));

sec("TDMA: alinhamento e colisao");
S.setMode("TDMA"); Object.assign(S.sim,{slots:8,frameSecs:1,ghosts:4,sync:"pps",fixFrameBase:false});
S.bootDevices(); runTdma(60000);
const crcPps=S.devs.reduce((s,d)=>s+d.rxCRC,0);
const errPps=S.devs.reduce((s,d)=>s+d.wrongSlot,0);
ck("com PPS e 8 nos: zero colisao", crcPps===0, `rxCRC=${crcPps}`);
ck("com PPS e 8 nos: praticamente nenhum slot errado", errPps<=8, `slotErrado=${errPps}`);
let vistos=0; for(let k=0;k<8;k++) if(k!==1&&S.devs[1].world[k].active) vistos++;
ck("cada tela remonta a rede toda sozinha (sem lider reencaminhando)", vistos>=6,
   `a Tela 2 ve ${vistos} vizinhos`);
const arPps=100*S.radio.airMs/S.T;
ck("8 nos num frame de 1 s ocupam ~41 % do ar", arPps>35&&arPps<48, arPps.toFixed(1)+" %");
S.sim.ghosts=0;

sec("TDMA: MAXN=8 limita a tela, nao a rede");
S.setMode("TDMA"); Object.assign(S.sim,{slots:16,frameSecs:2,ghosts:12,sync:"pps"});
S.bootDevices(); runTdma(40000);
const fora=S.devs.reduce((s,d)=>s+(d.foraDoWorld||0),0);
ck("ACHADO: com 16 slots, os pacotes de slot >= MAXN sao descartados", fora>0,
   `${fora} pacotes fora do world[] (MAXN=${S.MAXN})`);
S.sim.ghosts=0; S.sim.slots=8; S.sim.frameSecs=1;

sec("TDMA: resolucao do trajeto ditada pelo frame");
// cada caso tem de partir de estado limpo - inclusive o radio, senao herda
// perda/corte de um teste anterior e a medida vira ruido
function casoTrajeto(kmh,frame,ms){
  S.setMode("TDMA");
  Object.assign(S.sim,{slots:8,frameSecs:frame,ghosts:0,sync:"pps",fixFrameBase:false,leadKmh:kmh});
  Object.assign(S.radio,{ideal:false,loss:0,down:false});
  S.bootDevices(); runTdma(ms);
  return {vaos:buracos(S.devs[1]), pts:S.devs[1].routeN, ptsLider:S.devs[0].routeN};
}
const r60=casoTrajeto(60,3,60000);
ck("ACHADO: a 60 km/h com frame de 3 s o trajeto vira tracejado",
   r60.vaos>10, `${r60.vaos} vaos >40 m (v*frame = 50 m)`);
const r32=casoTrajeto(32,1,60000);
ck("a 32 km/h com frame de 1 s o trajeto fica continuo",
   r32.vaos===0, `${r32.vaos} vaos (v*frame = 8,9 m)`);
ck("ACHADO: o seguidor ve o trajeto muito mais grosso que o lider",
   r32.ptsLider>r32.pts*1.3,
   `lider ${r32.ptsLider} pts (STEP_M=5 m) vs seguidor ${r32.pts} pts (1 por frame)`);
S.sim.leadKmh=32; S.sim.frameSecs=1;

console.log(`\n${pass} PASS, ${fail} FAIL`);
process.exit(fail?1:0);
