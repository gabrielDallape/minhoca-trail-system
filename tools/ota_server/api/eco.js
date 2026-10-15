// ECO - as telas contam o proprio estado para ca.
//
// POR QUE ISTO EXISTE (2026-08-29): o /status que cada tela serve por WiFi so
// funciona de DENTRO PARA FORA. Medido: a tela anuncia "conectado, IP
// 192.168.2.130, /status no ar", e do PC na mesma sub-rede o ping devolve
// "destino inalcancavel" e o ARP volta 00-00-00-00-00-00. O esp_hosted nao
// entrega trafego de entrada - ja havia suspeita disso no wifi_ota.h.
//
// Sair, ela sai: e assim que o OTA baixa 1,7 MB da Vercel. Entao a telemetria
// viaja na MESMA DIRECAO que comprovadamente funciona. Uma tela no meio da
// trilha, com sinal de celular, se reporta igual - nao e so ferramenta de
// bancada.
//
// Nao ha banco de dados de proposito: o console.log cai no log de execucao da
// Vercel, que ja e consultavel e ja tem retencao. Guardar estado aqui exigiria
// KV, credencial e manutencao para uma frota de tres aparelhos.
module.exports = (req, res) => {
  const q = req.query || {};
  const quem = q.n || "?";
  // uma linha por chamada, prefixo fixo para dar para filtrar no log
  console.log(`ECO ${quem} :: ${new URLSearchParams(q).toString()}`);
  res.setHeader("Content-Type", "text/plain; charset=utf-8");
  res.setHeader("Cache-Control", "no-store");
  res.status(200).send("ok");
};
