// AUDITOR: le o firmware de verdade e confere se a bancada nao divergiu dele.
//
//   node web/bancada-trilha/audit.js
//
// O selftest.js prova que a bancada se comporta como ela mesma promete. Este
// arquivo prova outra coisa, mais importante: que ela promete o que o FIRMWARE
// faz. Compara constantes do trilha_core.h, temas/paleta em RGB565, comandos do
// protocolo e todas as strings de tela do grupo_ws.ino com o que esta no
// index.html. Divergencia aqui = a bancada esta mentindo.
const fs=require("fs");
const path=require("path");
const R=p=>fs.readFileSync(path.join(__dirname,"..","..",p),"utf8");

const core=R("firmware/trilha_core.h");
const ino =R("firmware/grupo_ws/grupo_ws.ino");
const web =fs.readFileSync(path.join(__dirname,"index.html"),"utf8");

let pass=0,fail=0;
const ck=(nome,cond,info="")=>{ (cond?pass++:fail++);
  console.log((cond?"PASS ":"FAIL ")+nome+(info?"   "+info:"")); };
const sec=t=>console.log("\n-- "+t);

// ---------------------------------------------------------------- constantes
sec("constantes do trilha_core.h");
// static const <tipo> NOME = valor;
const constDo=(txt,nome)=>{
  // o tipo pode ter mais de uma palavra: "static const unsigned long SLOT_MS"
  const m=txt.match(new RegExp("static const\\s+[\\w\\s]+?\\s+"+nome+"\\s*=\\s*([^;]+);"));
  return m?m[1].trim():null;
};
const CONSTS=["MAXN","ROUTE_MAX","HIST_N","R_EARTH","STEP_M","SLOT_MS","NODE_TTL","OFFROUTE_M"];
for(const nome of CONSTS){
  const val=constDo(core,nome);
  if(val===null){ ck(nome+" existe no core", false, "nao achei no trilha_core.h"); continue; }
  const num=parseFloat(val.replace(/[fUL]+$/i,""));
  // no JS a constante tem o mesmo nome: const MAXN=8, ...
  const m=web.match(new RegExp("\\b"+nome+"\\s*=\\s*([0-9.]+)"));
  const jsNum=m?parseFloat(m[1]):NaN;
  ck(`${nome} = ${num}`, Math.abs(jsNum-num)<1e-9, `firmware=${num}  bancada=${isNaN(jsNum)?"AUSENTE":jsNum}`);
}
// TRAIL_N e definido no .ino, nao no core
{
  const m=ino.match(/#define\s+TRAIL_N\s+(\d+)/);
  const w=web.match(/\bTRAIL_N\s*=\s*(\d+)/);
  ck("TRAIL_N (do .ino)", m&&w&&m[1]===w[1], `firmware=${m&&m[1]}  bancada=${w&&w[1]}`);
}
// CYCLE_MS e derivado: MAXN*SLOT_MS
{
  const derivado=/CYCLE_MS\s*=\s*MAXN\s*\*\s*SLOT_MS/.test(web);
  ck("CYCLE_MS derivado de MAXN*SLOT_MS", derivado);
}

// ------------------------------------------------------------------ comandos
sec("comandos do protocolo");
const cmds={};
for(const m of core.matchAll(/static const\s+uint8_t\s+(CMD_\w+)\s*=\s*(0x[0-9a-fA-F]+)/g)) cmds[m[1]]=m[2].toLowerCase();
for(const m of ino.matchAll(/#define\s+(P2P_\w+)\s+(0x[0-9a-fA-F]+)/g)) cmds[m[1]]=m[2].toLowerCase();
for(const [nome,val] of Object.entries(cmds)){
  const w=web.match(new RegExp("\\b"+nome+"\\s*=\\s*(0x[0-9a-fA-F]+)"));
  ck(`${nome} = ${val}`, w&&w[1].toLowerCase()===val, `firmware=${val}  bancada=${w?w[1]:"AUSENTE"}`);
}

// -------------------------------------------------------------------- temas
sec("temas e paleta (RGB565)");
// THEMES[3] no core: cada linha tem 11 RGB16(r,g,b) + flags + nome
const blocoTemas=core.slice(core.indexOf("THEMES[3]"), core.indexOf("PALETTE[8]"));
const linhasFw=blocoTemas.split("\n").filter(l=>l.includes("RGB16("));
const blocoWeb=web.slice(web.indexOf("const THEMES=["), web.indexOf("const PALETTE="));
const linhasWeb=blocoWeb.split("\n").filter(l=>/\[\d+,\d+,\d+\]/.test(l));
ck("3 temas nos dois lados", linhasFw.length===3&&linhasWeb.length===3,
   `firmware=${linhasFw.length} bancada=${linhasWeb.length}`);
for(let i=0;i<Math.min(linhasFw.length,linhasWeb.length);i++){
  const fw=[...linhasFw[i].matchAll(/RGB16\((\d+),(\d+),(\d+)\)/g)].map(m=>m.slice(1,4).join(","));
  const wb=[...linhasWeb[i].matchAll(/\[(\d+),(\d+),(\d+)\]/g)].map(m=>m.slice(1,4).join(","));
  const nome=(linhasFw[i].match(/"(\w+)"/)||[])[1];
  ck(`tema ${nome}: ${fw.length} cores identicas`,
     fw.length===wb.length && fw.every((c,k)=>c===wb[k]),
     fw.length!==wb.length?`firmware=${fw.length} bancada=${wb.length}`:
     fw.map((c,k)=>c===wb[k]?"":`[${k}] fw(${c}) != web(${wb[k]})`).filter(Boolean).join(" "));
}
{
  const fw=(core.match(/PALETTE\[8\]\s*=\s*\{([^}]+)\}/)||[])[1];
  const wb=(web.match(/const PALETTE=\[([^\]]+)\]/)||[])[1];
  const a=fw?fw.split(",").map(s=>s.trim().toLowerCase()):[];
  const b=wb?wb.split(",").map(s=>s.trim().toLowerCase()):[];
  ck("PALETTE de 8 cores identica", a.length===8&&a.every((v,i)=>v===b[i]),
     a.length!==b.length?`firmware=${a.length} bancada=${b.length}`:
     a.map((v,i)=>v===b[i]?"":`[${i}] ${v}!=${b[i]}`).filter(Boolean).join(" "));
}
// o override de rota/rastro que o applyTheme forca em qualquer tema
for(const rgb of ["180,100,255","80,40,120","80,210,255","30,90,120"]){
  const noFw=core.includes(`RGB16(${rgb})`);
  const noWeb=web.includes(`rgb16(${rgb})`);
  ck(`override de cor RGB16(${rgb})`, noFw&&noWeb, `firmware=${noFw} bancada=${noWeb}`);
}
// cores hex soltas usadas no desenho
for(const [nome,hex] of [["C_ORANGE","0xFD20"],["C_GHOST","0x7C53"],["C_YEL","0xFE60"]]){
  const noFw=ino.includes(hex)||core.includes(hex);
  const noWeb=web.toLowerCase().includes(hex.toLowerCase());
  ck(`${nome} ${hex}`, noFw&&noWeb, `firmware=${noFw} bancada=${noWeb}`);
}
{ // casing vermelho do trecho de alerta
  const noFw=/roadSeg\([^;]*0x6800/.test(ino);
  ck("casing do alerta 0x6800", noFw && web.toLowerCase().includes("0x6800"));
}

// ------------------------------------------------------------------ strings
sec("strings de tela (todas as que o .ino desenha)");
const strsFw=new Set();
for(const l of ino.split("\n")){
  if(!/txt\(fonts|txtBig\(fonts/.test(l)) continue;
  for(const m of l.matchAll(/"([^"]*)"/g)){
    const s=m[1];
    if(!s || s.length<2) continue;
    if(s.includes("%")) continue;              // formatos vao no snprintf
    strsFw.add(s);
  }
}
const faltando=[...strsFw].filter(s=>!web.includes(s));
ck(`${strsFw.size} strings de tela presentes na bancada`, faltando.length===0,
   faltando.length?("faltam: "+faltando.map(s=>`"${s}"`).join(", ")):"");

// -------------------------------------------------------- regras numericas
sec("regras de desenho e limites");
const REGRAS=[
  ["ponte tracejada acima de 40 m", /haversine\([^)]*\)\s*>\s*40\.0/, /haversine\([^)]*\)\s*>\s*40\.0/],
  ["perda de sinal apos 4000 ms",  /worldRxMs\s*>\s*4000/,           /worldRxMs\s*>\s*4000/],
  ["pisca a cada 300 ms",          /millis\(\)\/300\)%2/,            /millis\(\)\/300\)\|0\)%2/],
  ["zoom minimo 0.5 m\/px",        /mapMPP\s*<\s*0\.5/,              /mapMPP\s*<\s*0\.5/],
  ["zoom maximo 40 m\/px",         /mapMPP\s*>\s*40/,                /mapMPP\s*>\s*40/],
  ["zoom + multiplica por 0.7",    /mapMPP\s*\*=\s*0\.7/,            /mapMPP\s*\*=\s*0\.7/],
  ["zoom - multiplica por 1.4",    /mapMPP\s*\*=\s*1\.4/,            /mapMPP\s*\*=\s*1\.4/],
  ["uplink respeita o slotDue",     /pendingUplink && \(long\)\(now-slotDue\)>=0/, /d\.pendingUplink && now>=d\.slotDue/],
  ["join a cada 1500 ms",          /lj\s*>\s*1500/,                  /d\.lj\s*>\s*1500/],
  ["roster a cada 3 ciclos",       /cyc\+\+%3\)==0/,                 /d\.cyc\+\+%3\)===0/],
  ["trail 1 Hz, catch-up 330 ms",  /behind\?330UL:1000UL/,           /behind\?330:1000/],
  ["fallback do seguidor em 2 s",  /lastP2PTx>2000/,                 /lastP2PTx>2000/],
  ["nome do carro ate 12 letras",  /L<12/,                           /nameBuf\.length<12/],
  ["codigo de 5 digitos",          /codeLen==5/,                     /codeBuf\.length===5/],
];
for(const [nome,reFw,reWeb] of REGRAS){
  const okFw=reFw.test(ino)||reFw.test(core);
  const okWeb=reWeb.test(web);
  ck(nome, okFw&&okWeb, okFw?(okWeb?"":"ausente na bancada"):"nao achei no firmware (regra mudou?)");
}

// ------------------------------------------------------- TDMA (Fase 4)
sec("constantes do tdma_core.h e do tdma_test.ino");
const tdmaCore=R("firmware/tdma_core.h");
const tdmaIno =R("firmware/tdma_test/tdma_test.ino");
// #define no core
for(const nome of ["TDMA_PKT_N","TDMA_FL_FIX","TDMA_FL_ALERT","TDMA_FL_LEADER"]){
  const m=tdmaCore.match(new RegExp("#define\\s+"+nome+"\\s+(\\S+)"));
  const w=web.match(new RegExp("\\b"+nome+"\\s*=\\s*(\\S+?)[,;\\s]"));
  const iguais=m&&w&&parseInt(m[1])===parseInt(w[1]);
  ck(`${nome} = ${m?m[1]:"?"}`, iguais, `firmware=${m&&m[1]}  bancada=${w&&w[1]}`);
}
// static const no core
for(const nome of ["TDMA_ANCHOR_TTL_US","TDMA_HOLDOVER_GUARD"]){
  const val=constDo(tdmaCore,nome);
  const num=val?parseFloat(val.replace(/[fUL]+$/i,"")):NaN;
  const w=web.match(new RegExp("\\b"+nome+"\\s*=\\s*([0-9]+)"));
  ck(`${nome} = ${num}`, w&&parseFloat(w[1])===num, `firmware=${num}  bancada=${w&&w[1]}`);
}
// #define no sketch de teste -> na bancada com prefixo TDMA_
for(const [fw,bank] of [["N_SLOTS","TDMA_N_SLOTS"],["FRAME_SECS","TDMA_FRAME_SECS"],
                        ["GUARD_US","TDMA_GUARD_US"],["ROOM","TDMA_ROOM"]]){
  const m=tdmaIno.match(new RegExp("#define\\s+"+fw+"\\s+(\\S+)"));
  const w=web.match(new RegExp("\\b"+bank+"\\s*=\\s*(\\S+?)[,;\\s]"));
  const a=m?parseInt(m[1]):NaN, b=w?parseInt(w[1]):NaN;
  ck(`${fw} = ${a}`, a===b, `firmware=${a}  bancada=${b}`);
}
// parametros de RF que definem o air-time
for(const [fw,bank] of [["RF_SF","TDMA_SF"],["RF_BW","TDMA_BW_KHZ"],["RF_PRE","TDMA_PRE"]]){
  const m=tdmaIno.match(new RegExp("#define\\s+"+fw+"\\s+([0-9.]+)"));
  const w=web.match(new RegExp("\\b"+bank+"\\s*=\\s*([0-9.]+)"));
  ck(`${fw} = ${m&&m[1]}`, m&&w&&parseFloat(m[1])===parseFloat(w[1]),
     `firmware=${m&&m[1]}  bancada=${w&&w[1]}`);
}
sec("correcoes do TDMA presentes no firmware (e espelhadas na bancada)");
// frameBase: arredondamento com minimo 1, no lugar da divisao inteira
{
  const fwOk=/\(d \+ t\.frameUs \/ 2\) \/ t\.frameUs/.test(tdmaCore) && /n \? n : 1/.test(tdmaCore);
  const webOk=/Math\.round\(\(rxUs-t\.anchorUs\)\/t\.frameUs\)/.test(web);
  ck("frameBase arredonda com minimo 1 (nao trunca)", fwOk&&webOk,
     `firmware=${fwOk} bancada=${webOk}`);
  const bugAntigo=/frameBase\s*\+=\s*\(uint32_t\)\(\(rxUs\s*-\s*t\.anchorUs\)\s*\/\s*t\.frameUs\)/.test(tdmaCore);
  ck("a divisao inteira antiga nao ficou no firmware", !bugAntigo);
}
// ancora do beacon: desconta o air-time (rxDone = fim do pacote) e o sketch informa o valor medido
{
  const fwRecua=/if \(t\.airtimeUs && edge > t\.airtimeUs\) edge -= t\.airtimeUs;/.test(tdmaCore);
  const fwInforma=/tdmaSetAirtime\(tdma, at\)/.test(tdmaIno);
  const webRecua=/if\(t\.airtimeUs && edge>t\.airtimeUs\) edge-=t\.airtimeUs;/.test(web);
  ck("a ancora do beacon desconta o air-time", fwRecua&&fwInforma&&webRecua,
     `core recua=${fwRecua}  sketch informa=${fwInforma}  bancada=${webRecua}`);
}
// a ancora do beacon so vale quando NAO ha PPS
{
  const fwOk=/tdma\.sync\s*!=\s*TDMA_SYNC_PPS\)\s*tdmaOnBeacon/.test(tdmaIno);
  ck("o beacon nao sobrescreve o PPS (fim da mistura de ancoras)", fwOk,
     fwOk?"":"o sketch voltou a ancorar sem checar a fonte de tempo");
}
// quem ancora a rede nao entra em holdover
{
  const fwOk=/isAnchor && t\.sync == TDMA_SYNC_BEACON/.test(tdmaCore);
  const webOk=/t\.isAnchor && t\.sync===TDMA_SYNC_BEACON/.test(web);
  ck("o no ancora nao e marcado em holdover", fwOk&&webOk, `firmware=${fwOk} bancada=${webOk}`);
}
// a janela de TX recua o limite superior pelo air-time (o pacote inteiro cabe no slot)
{
  const fwOk=/hi = start \+ t\.slotUs - half - t\.airtimeUs/.test(tdmaCore);
  const webOk=/hi=start\+t\.slotUs-half-t\.airtimeUs/.test(web);
  ck("a janela de TX desconta o air-time do fim do slot", fwOk&&webOk,
     `firmware=${fwOk} bancada=${webOk}`);
}

sec("correcoes do modo grupo presentes no firmware");
{
  const fwOk=/world\[k\]\.active=false;/.test(ino) && /now-world\[k\]\.lastMs>NODE_TTL/.test(ino);
  const webOk=/sim\.fixZumbi/.test(web);
  ck("o firmware expira quem parou de reportar (fim do carro zumbi)", fwOk&&webOk,
     `firmware=${fwOk} bancada=${webOk}`);
}
{
  const fwOk=/pendingUplink && \(long\)\(now-slotDue\)>=0/.test(ino);
  ck("o uplink respeita o slotDue (fim do lockstep)", fwOk);
}
{
  const fwOk=/if\(world\[k\]\.alert && peer<0\) peer=k;/.test(ino);
  const webOk=/sim\.fixAlerta/.test(web);
  ck("o alerta procura quem esta em alerta, nao o primeiro peer", fwOk&&webOk,
     `firmware=${fwOk} bancada=${webOk}`);
}
{
  const fwOk=/rcolor\[slot\]=\(col>0&&col<8\)\?col:\(uint8_t\)slot/.test(core);
  ck("a cor do carro cai no slot quando o JOIN manda 0", fwOk);
}
{
  const fwOk=/for\(int pass=0;pass<MAXN;pass\+\+\)/.test(ino);
  ck("os rotulos do mapa desviam para nao se sobrepor", fwOk);
}

// ------------------------------------------------------------ divergencias
sec("divergencias declaradas (a bancada NAO deve inventar comportamento)");
// SAIR DO GRUPO: o firmware nao limpa route[] nem world[]; a bancada tambem nao pode
{
  const trechoFw=ino.slice(ino.indexOf("if(tx>=exX"), ino.indexOf("else if(tx>=zmX"));
  const limpaNoFw=/routeN=0|routeHead=0/.test(trechoFw);
  const trechoWeb=web.slice(web.indexOf("if(tx>=d.exX"), web.indexOf("if(tx>=d.zmX"));
  const limpaNoWeb=/routeN=0|routeHead=0/.test(trechoWeb);
  ck("SAIR limpa route[] nos dois (ou em nenhum)", limpaNoFw===limpaNoWeb,
     `firmware limpa=${limpaNoFw}  bancada limpa=${limpaNoWeb}`);
}
// world[].color inicial: o setup() usa rcolor[k]?rcolor[k]:k, nao zero
{
  const okFw=/world\[k\]\.color\s*=\s*rcolor\[k\]\s*\?\s*rcolor\[k\]\s*:\s*k/.test(ino);
  const okWeb=/color:k/.test(web);
  ck("world[].color inicial nao e zero", okFw&&okWeb, `firmware=${okFw} bancada=${okWeb}`);
}
// nada no firmware zera active: a bancada nao pode zerar por conta propria
{
  const zeraFw=/world\[\w+\]\.active\s*=\s*false/.test(ino);
  const zeraWeb=/world\[k\]\.active=false/.test(web);
  ck("firmware e bancada expiram world[].active do mesmo jeito", zeraFw===zeraWeb,
     `firmware zera=${zeraFw}  bancada zera=${zeraWeb}`);
}

console.log(`\n${pass} PASS, ${fail} FAIL`);
process.exit(fail?1:0);
