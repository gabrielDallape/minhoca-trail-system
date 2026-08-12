// Replay dos testes do firmware/tdma_selftest/tdma_selftest.ino usando o port JS
// do tdma_core.h que vive em index.html.
//
//   node web/bancada-trilha/tdma_replay.js
//
// Por que existe: o tdma_selftest roda no ESP32 e nao ha compilador C++ nativo
// aqui, entao as correcoes do tdma_core.h (frameBase arredondado, recuo do
// air-time, ancora sem holdover) nao poderiam ser verificadas antes de gravar.
// Este arquivo roda os MESMOS casos, com os MESMOS numeros, sobre o port - e o
// audit.js garante que port e firmware nao divergiram. Se algum caso quebrar
// aqui, o auto-teste do firmware tambem quebra.
const fs=require("fs");
const path=require("path");
const html=fs.readFileSync(path.join(__dirname,"index.html"),"utf8");
let src=html.match(/<script>([\s\S]*?)<\/script>/)[1];

const noop=()=>{};
const ctxStub=new Proxy({},{get:(t,k)=>k==="measureText"?(()=>({width:10})):noop,set:()=>true});
function mkEl(){const el={addEventListener:noop,setAttribute:noop,getAttribute:()=>"false",
 classList:{add:noop,remove:noop,toggle:noop},dataset:{},style:{setProperty:noop,display:""},
 getContext:()=>ctxStub,getBoundingClientRect:()=>({left:0,top:0,width:1024,height:600}),
 appendChild:noop,querySelector:()=>mkEl(),querySelectorAll:()=>[],scrollTop:0,scrollHeight:0,clientHeight:0};
 for(const p of ["innerHTML","textContent","className"]) Object.defineProperty(el,p,{get:()=>"",set:noop});
 return el;}
global.document={querySelector:()=>mkEl(),querySelectorAll:()=>[],createElement:()=>mkEl(),body:mkEl(),addEventListener:noop};
global.window=global; global.requestAnimationFrame=()=>1; global.getComputedStyle=()=>({getPropertyValue:()=>""});
src+=`;module.exports={tdmaInit,tdmaOnBeacon,tdmaOnPps,tdmaTick,tdmaShouldTx,tdmaUsInFrame,
  tdmaSlotAt,tdmaSlotStartUs,tdmaGuardNow,tdmaHeadroomUs,tdmaFits,tdmaMaxNodes,
  tdmaPack,tdmaUnpack,TDMA_PKT_N,TDMA_FL_FIX,TDMA_FL_ALERT,TDMA_FL_LEADER,
  tdmaFrameSecsGridSafe,TDMA_GRID_OFFSET_SEC,sim};`;
const mod={exports:{}};
new Function("module","exports","require",src)(mod,mod.exports,require);
const S=mod.exports;

// o port usa o clock que passamos por parametro; aqui ele e explicito
let NOW=0;
const shouldTx=t=>S.tdmaShouldTx(t,NOW);
const tick=t=>S.tdmaTick(t,NOW);
const usInFrame=t=>S.tdmaUsInFrame(t,NOW);

let pass=0,fail=0;
const check=(c,n)=>{ (c?pass++:fail++); console.log((c?"  PASS ":"  FAIL ")+n); };
const checkEq=(a,b,n)=>{ const ok=a===b; (ok?pass++:fail++);
  console.log((ok?"  PASS ":"  FAIL ")+n+(ok?"":`   (${a} != ${b})`)); };
const sec=t=>console.log("\n"+t);

// [1] conta do slot e capacidade
sec("[1] geometria do frame");
{
  const t=S.tdmaInit(0,8,1,15000);
  checkEq(t.slotUs,125000,"slot de 125ms com 8 slots em 1s");
  checkEq(S.tdmaHeadroomUs(t,50000),60000,"folga com airtime 50ms");
  check(S.tdmaFits(t,50000),"airtime 50ms cabe");
  check(!S.tdmaFits(t,120000),"airtime 120ms NAO cabe");
  checkEq(S.tdmaMaxNodes(t,50000),15,"caberiam 15 nos com airtime 50ms");
  const b=S.tdmaInit(0,16,1,15000);   // slot de 62,5ms
  check(!S.tdmaFits(b,50000),"airtime 50ms NAO cabe em slot de 62ms com guarda 15ms");
}

// [2] sem ancora nao transmite
sec("[2] sem ancora nao transmite");
{
  const t=S.tdmaInit(2,8,1,15000);
  let tx=false;
  for(let i=0;i<100;i++){ NOW+=10000; if(shouldTx(t)) tx=true; }
  check(!tx,"shouldTx nunca dispara sem ancora");
}

// [3] a janela de TX cai dentro do meu slot
sec("[3] a janela de TX cai dentro do meu slot");
{
  const ID=3, t=S.tdmaInit(ID,8,1,15000);
  NOW=1000000; S.tdmaOnBeacon(t,NOW);
  let firstTx=0;
  for(let step=0;step<1000;step++){
    NOW=1000000+step*1000;
    if(shouldTx(t)){ firstTx=step*1000; break; }
  }
  check(firstTx>=382500&&firstTx<=492500,`primeiro TX dentro da janela do slot 3 (foi ${firstTx})`);
  check(firstTx>=S.tdmaSlotStartUs(t,ID),"TX nao vaza para o slot anterior");
  check(firstTx<S.tdmaSlotStartUs(t,ID)+t.slotUs,"TX nao vaza para o slot seguinte");
}

// [4] exatamente 1 TX por frame
sec("[4] exatamente 1 TX por frame");
{
  const t=S.tdmaInit(1,8,1,15000);
  NOW=0; S.tdmaOnBeacon(t,0);
  let txs=0;
  for(let ms=0;ms<10000;ms++){
    NOW=ms*1000;
    if(ms>0&&ms%1000===0) S.tdmaOnBeacon(t,NOW);
    if(shouldTx(t)) txs++;
  }
  checkEq(txs,10,"10 TX em 10 frames");
  checkEq(t.txCount,10,"contador interno txCount");
  const u=S.tdmaInit(0,8,1,15000);
  NOW=0; S.tdmaOnBeacon(u,0);
  NOW=20000;
  const a=shouldTx(u), b=shouldTx(u);
  check(a&&!b,"segunda chamada no mesmo frame retorna false");
}

// [5] janela perdida e contabilizada
sec("[5] janela perdida e contabilizada");
{
  const t=S.tdmaInit(1,8,1,15000);
  NOW=0; S.tdmaOnBeacon(t,0);
  NOW=300000;
  check(!shouldTx(t),"nao transmite fora da janela");
  checkEq(t.missedTx,1,"missedTx registrou a janela perdida");
  NOW=350000;
  check(!shouldTx(t),"nao transmite atrasado no mesmo frame");
}

// [6] aritmetica de 64 bits + 1 hora continua
sec("[6] aritmetica longa (onde micros() estouraria)");
{
  const t=S.tdmaInit(5,8,1,15000);
  const base=10000*1000000;
  NOW=base; S.tdmaOnBeacon(t,base);
  NOW=base+700000;
  const p=usInFrame(t);
  check(!!p,"usInFrame ok depois de 10.000s");
  checkEq(p.usInFrame,700000,"posicao no frame correta apos overflow de 32 bits");
  const h=S.tdmaInit(2,8,1,15000);
  NOW=base; S.tdmaOnBeacon(h,base);
  for(let ms=0;ms<3600*1000;ms+=5){
    NOW=base+ms*1000;
    if(ms>0&&ms%1000===0) S.tdmaOnBeacon(h,NOW);
    shouldTx(h);
  }
  checkEq(h.txCount,3600,"3600 TX em 1 hora (1/frame de 1s)");
  checkEq(h.missedTx,0,"nenhuma janela perdida em 1 hora");
}

// [7] holdover
sec("[7] holdover quando a ancora para de chegar");
{
  const t=S.tdmaInit(1,8,1,15000);
  NOW=0; S.tdmaOnBeacon(t,0); tick(t);
  check(!t.holdover,"sem holdover logo depois da ancora");
  checkEq(S.tdmaGuardNow(t),15000,"guarda normal");
  NOW=1400000; tick(t);
  check(!t.holdover,"1,4s sem ancora: ainda nao e holdover");
  NOW=2000000; tick(t);
  check(t.holdover,"2s sem ancora: entrou em holdover");
  checkEq(S.tdmaGuardNow(t),45000,"guarda triplicada no holdover");
  let txs=0;
  for(let ms=2000;ms<5000;ms++){ NOW=ms*1000; if(shouldTx(t)) txs++; }
  check(txs>=2,"segue transmitindo em holdover (nao para a rede)");
}

// [8] PPS usa o segundo UTC
sec("[8] ancora por PPS usa o segundo UTC (frame de 3s)");
{
  const t=S.tdmaInit(0,50,3,15000);
  NOW=5000000; S.tdmaOnPps(t,NOW,7);
  const p=usInFrame(t);
  check(!!p,"usInFrame com PPS");
  checkEq(p.usInFrame,1000000,"7 % 3 = 1 -> 1s dentro do frame de 3s");
  checkEq(p.frameIdx,2,"frameIdx = 7/3 = 2");
}

// [9] pacote de 16 bytes
sec("[9] pacote de 16 bytes vai e volta");
{
  const k={room:12345,slot:5,seq:200,flags:S.TDMA_FL_FIX|S.TDMA_FL_LEADER,
           lat:-23.5505123,lon:-46.6333456,heading:271,speed:87};
  const p=S.tdmaPack(k);
  checkEq(p.length,S.TDMA_PKT_N,"16 bytes exatos");
  const q=S.tdmaUnpack(p);
  checkEq(q.room,12345,"room");
  checkEq(q.slot,5,"slot");
  checkEq(q.seq,200,"seq");
  check(Math.abs(q.lat-k.lat)<1e-6,"lat com resolucao de 1e-7");
  check(Math.abs(q.lon-k.lon)<1e-6,"lon com resolucao de 1e-7");
  check(Math.abs(q.heading-270)<=2,"heading com resolucao de 2 graus");
  checkEq(q.speed,87,"speed");
}

// [10] as CORRECOES desta rodada (nao existiam no tdma_selftest)
sec("[10] correcoes: frameBase, recuo do air-time, ancora sem holdover");
{
  // frameBase: no com cristal mais lento re-ancorando a cada 999,96ms locais
  const t=S.tdmaInit(1,8,1,15000);
  NOW=0; S.tdmaOnBeacon(t,0);
  let txs=0, prox=999960;
  for(let us=0;us<60*1000000;us+=1000){
    NOW=us;
    if(us>=prox){ S.tdmaOnBeacon(t,NOW); prox+=999960; }   // -40 ppm
    if(shouldTx(t)) txs++;
  }
  check(txs>=55,`cristal 40ppm mais lento segue transmitindo (${txs} TX em 60 frames)`);

  // recuo do air-time: inerte sem tdmaSetAirtime, ativo com ele
  const a=S.tdmaInit(1,8,1,15000);
  NOW=500000; S.tdmaOnBeacon(a,500000);
  checkEq(a.anchorUs,500000,"sem airtimeUs a ancora nao recua (compatibilidade)");
  const b=S.tdmaInit(1,8,1,15000);
  b.airtimeUs=51500;
  NOW=500000; S.tdmaOnBeacon(b,500000);
  checkEq(b.anchorUs,500000-51500,"com airtimeUs a ancora recua o pacote inteiro");

  // quem ancora nao entra em holdover
  const anc=S.tdmaInit(0,8,1,15000);
  NOW=0; S.tdmaOnBeacon(anc,0);
  NOW=10000000; tick(anc);
  check(!anc.holdover,"o no 0 (ancora) nao entra em holdover depois de 10s");
  checkEq(S.tdmaGuardNow(anc),15000,"e a guarda dele fica normal");
  const seg=S.tdmaInit(1,8,1,15000);
  NOW=0; S.tdmaOnBeacon(seg,0);
  NOW=10000000; tick(seg);
  check(seg.holdover,"um seguidor sem ancora ainda entra em holdover");
}

sec("[11] grade de tempo GPS x UTC (18s) e frameSecs sensivel");
{
  check( S.tdmaFrameSecsGridSafe(1), "frameSecs 1 e imune");
  check( S.tdmaFrameSecsGridSafe(3), "frameSecs 3 e imune");
  check(!S.tdmaFrameSecsGridSafe(4), "frameSecs 4 e SENSIVEL a grade");
  check(!S.tdmaFrameSecsGridSafe(5), "frameSecs 5 e SENSIVEL a grade");
  check( S.tdmaFrameSecsGridSafe(6), "frameSecs 6 e imune");

  const a=S.tdmaInit(0,8,1,15000);
  check(!a.gridSensitive,"frame de 1s nao levanta gridSensitive");
  const b=S.tdmaInit(0,50,4,20000);
  check(b.gridSensitive,"frame de 4s (50 nos em SF7) levanta gridSensitive");

  // a prova do estrago: mesma borda de PPS, um no rotulando em UTC e outro na
  // grade GPS (+18). Com frameSecs=4 eles discordam de 2s.
  const utc=1000, gps=1000+S.TDMA_GRID_OFFSET_SEC;
  NOW=5000000;
  const u4=S.tdmaInit(0,8,4,15000), g4=S.tdmaInit(0,8,4,15000);
  S.tdmaOnPps(u4,NOW,utc); S.tdmaOnPps(g4,NOW,gps);
  checkEq(Math.abs(usInFrame(g4).usInFrame-usInFrame(u4).usInFrame),2000000,
    "frame 4s: nos em grades diferentes ficam 2s deslocados");

  const u6=S.tdmaInit(0,8,6,15000), g6=S.tdmaInit(0,8,6,15000);
  S.tdmaOnPps(u6,NOW,utc); S.tdmaOnPps(g6,NOW,gps);
  checkEq(Math.abs(usInFrame(g6).usInFrame-usInFrame(u6).usInFrame),0,
    "frame 6s: mesmas grades diferentes, ZERO deslocamento");
}

console.log(`\n${pass} PASS, ${fail} FAIL`);
process.exit(fail?1:0);
