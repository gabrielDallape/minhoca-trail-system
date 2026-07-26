/*
 * ============================================================================
 *  MODO GRUPO - FASE 1: teste do radio (beacon + slots), headless (so serial).
 *  Objetivo: provar que N nos falam por RODIZIO sem colidir (hub-and-spoke).
 *
 *  TOPOLOGIA (LoRaMESH): so o LIDER (master, ID0) faz BROADCAST (dest 2047).
 *  Os seguidores (slaves) mandam UPLINK pro lider (dest 0) na SUA VEZ (slot).
 *  O beacon do lider marca o "tempo zero" -> ninguem precisa de relogio comum.
 *
 *  COMO USAR: muda NODE_ID e regrava em cada aparelho.
 *    NODE_ID 0 = LIDER  (CYD/master, ID0)   -> LoRa Serial1 RX=35 TX=22 (ESP32)
 *    NODE_ID 1..7 = SEGUIDOR (slave)         -> GIGA LoRa Serial2 (18/19)
 *  Cada carrinho precisa do PROPRIO modulo LoRa com ID unico (=NODE_ID).
 *  (Hoje so ha 2 modulos: 13680/ID0 no CYD, 13683/ID1 no GIGA -> testa com 2.)
 *
 *  O que olhar na serial:
 *    - LIDER: "rodada R | ouvi X/N seguidores: [ids]"  (X deve bater com quantos ha)
 *    - SEGUIDOR: "beacon R ouvido -> uplink no slot K"  (sem 'perdi beacon' seguido)
 * ============================================================================
 */
#define NODE_ID 0          // <<< MUDA AQUI antes de gravar cada aparelho (0=lider)

#include "LoRaMESH.h"

#if defined(ESP32)
  LoRaMESH lora(&Serial1);
  static void loraBegin(){ Serial1.begin(9600, SERIAL_8N1, 35, 22); }   // CYD: RX=IO35 TX=IO22
#else
  LoRaMESH lora(&Serial2);
  static void loraBegin(){ Serial2.begin(9600); }                        // GIGA: Serial2 (18/19)
#endif

const uint16_t BCAST   = 2047;   // broadcast do master
const uint16_t LEADER  = 0;      // ID do lider
const uint8_t  CMD_BEACON = 0x20;
const uint8_t  CMD_UPLINK = 0x21;

const int           NSLOTS   = 8;      // ate 8 nos
const unsigned long SLOT_MS  = 450;    // duracao de cada vez (1 pacote LoRa a 9600)
const unsigned long CYCLE_MS = (unsigned long)NSLOTS * SLOT_MS;  // ~3.6s
const bool IS_LEADER = (NODE_ID == 0);

// LIDER: quem ouvi nesta rodada
uint8_t  heardMask = 0;    // bit k = ouvi o no k
uint8_t  round8    = 0;
unsigned long lastBeacon = 0;

// SEGUIDOR: agendamento do meu slot
bool          pendingUplink = false;
unsigned long slotDue = 0;
uint8_t       lastBeaconRound = 255;
unsigned long lastBeaconMs = 0;

void setup(){
  Serial.begin(115200);
  loraBegin();
  delay(150);
  lora.localread();
  Serial.print("== GRUPO FASE1 == NODE_ID="); Serial.print(NODE_ID);
  Serial.print(IS_LEADER ? " (LIDER)" : " (SEGUIDOR)");
  Serial.print(" | LoRa localId="); Serial.print(lora.localId);
  Serial.print(" uid="); Serial.println(lora.localUniqueId);
  if(lora.localUniqueId==0) Serial.println("!! LoRa SEM RESPOSTA - confere fiacao/energia");
  lastBeacon = millis();
}

void sendBeacon(){
  uint8_t p[2] = { round8, (uint8_t)NSLOTS };
  lora.PrepareFrameCommand(BCAST, CMD_BEACON, p, 2);
  lora.SendPacket();
}
void sendUplink(){
  uint8_t p[2] = { (uint8_t)NODE_ID, lastBeaconRound };
  lora.PrepareFrameCommand(LEADER, CMD_UPLINK, p, 2);
  lora.SendPacket();
}

void handleRx(uint8_t cmd, uint8_t* p, uint8_t plen){
  if(IS_LEADER){
    if(cmd==CMD_UPLINK && plen>=2){
      uint8_t nid = p[0];
      if(nid>=1 && nid<NSLOTS) heardMask |= (1<<nid);
    }
  } else {
    if(cmd==CMD_BEACON && plen>=1){
      lastBeaconRound = p[0];
      lastBeaconMs = millis();
      slotDue = lastBeaconMs + (unsigned long)NODE_ID * SLOT_MS;   // minha vez
      pendingUplink = true;
      Serial.print("beacon "); Serial.print(lastBeaconRound);
      Serial.print(" ouvido -> uplink no slot "); Serial.println(NODE_ID);
    }
  }
}

void loop(){
  // RX (drena a fila com guarda)
  int guard = 0;
  uint16_t id; uint8_t cmd=0, p[64], plen=0;
  while(guard++ < 8 && lora.ReceivePacketCommand(&id, &cmd, p, &plen, 15)){
    handleRx(cmd, p, plen);
  }

  unsigned long now = millis();

  if(IS_LEADER){
    if(now - lastBeacon >= CYCLE_MS){
      // relatorio da rodada que passou
      int n=0; for(int k=1;k<NSLOTS;k++) if(heardMask&(1<<k)) n++;
      Serial.print("rodada "); Serial.print(round8);
      Serial.print(" | ouvi "); Serial.print(n); Serial.print(" seguidores: [");
      for(int k=1;k<NSLOTS;k++) if(heardMask&(1<<k)){ Serial.print(k); Serial.print(' '); }
      Serial.println("]");
      // proxima rodada
      round8++; heardMask=0; lastBeacon=now;
      sendBeacon();
    }
  } else {
    if(pendingUplink && now >= slotDue){
      sendUplink();
      pendingUplink = false;
    }
    // aviso se o beacon sumiu
    if(lastBeaconMs!=0 && now - lastBeaconMs > CYCLE_MS*2){
      Serial.println("... sem beacon (lider fora de alcance?)");
      lastBeaconMs = now;   // evita spam
    }
  }
}
