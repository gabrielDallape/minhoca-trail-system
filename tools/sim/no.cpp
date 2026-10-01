// Uma tela simulada. Compilado UMA VEZ POR NO com -DNO_NS=no0, no1, ...: o mesmo
// firmware, cada copia no seu namespace, com seu relogio, radio, GPS e NVS.
#include "pre.h"
#ifndef NO_NS
#error "compile com -DNO_NS=noK"
#endif
namespace NO_NS {
#include "node_env.h"
#include "mts/app/boot.h"
#include "mts/app/sessao.h"
#include "mts/app/servico.h"
#include "node_api.h"
}
#define SIM_CAT2(a, b) a##b
#define SIM_CAT(a, b) SIM_CAT2(a, b)
NoApi SIM_CAT(api_, NO_NS) = {
  NO_NS::simApi_boot, NO_NS::simApi_religa, NO_NS::simApi_agoraG, NO_NS::simApi_passo,
  NO_NS::simApi_energiaRadio, NO_NS::simApi_gpsFix, NO_NS::simApi_criar, NO_NS::simApi_abrir,
  NO_NS::simApi_entrarLista, NO_NS::simApi_entrarCodigo, NO_NS::simApi_sair, NO_NS::simApi_retomar,
  NO_NS::simApi_obs, NO_NS::simApi_fecha
};
