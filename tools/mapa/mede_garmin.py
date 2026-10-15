# Mede TUDO que um mapa de GPS off-road (estilo Garmin topo) carrega, para o
# orcamento do cartao sair de numero medido e nao de chute.
#
# Nao gera nada. Le o extrato uma vez e conta pontos por camada, porque o que
# custa byte no cartao e tempo de desenho na tela e o NUMERO DE PONTOS - nao o
# numero de feicoes, e muito menos o tamanho do .pbf.
import sys, osmium, collections

MB = 1e6


def cls_via(t):
    hw = t.get('highway')
    if hw is None:
        return None
    if hw in ('track', 'path', 'bridleway', 'unclassified'):
        return 'via/trilha'
    if hw in ('motorway', 'trunk', 'primary', 'secondary', 'tertiary',
              'motorway_link', 'trunk_link', 'primary_link',
              'secondary_link', 'tertiary_link'):
        return 'via/estrada'
    if hw in ('residential', 'service', 'living_street'):
        return 'via/rua'
    if hw in ('footway', 'cycleway', 'steps', 'pedestrian'):
        return 'via/pedestre'
    return 'via/outra'


def cls_way(t):
    v = cls_via(t)
    if v:
        return v
    wl = t.get('waterway')
    if wl in ('river', 'canal'):
        return 'agua/rio'
    if wl in ('stream', 'ditch', 'drain'):
        return 'agua/corrego'
    if t.get('natural') == 'water' or t.get('landuse') in ('reservoir', 'basin'):
        return 'agua/lago'
    if t.get('natural') in ('wood', 'scrub', 'grassland', 'heath', 'wetland'):
        return 'solo/mata'
    if t.get('landuse') in ('forest', 'farmland', 'meadow', 'orchard', 'vineyard'):
        return 'solo/uso'
    if t.get('boundary') in ('protected_area', 'national_park', 'aboriginal_lands'):
        return 'limite/area protegida'
    if t.get('boundary') == 'administrative' and t.get('admin_level') in ('4', '6', '8'):
        return 'limite/administrativo'
    if t.get('railway'):
        return 'ferrovia'
    if t.get('aeroway') in ('runway', 'taxiway', 'aerodrome'):
        return 'pista de pouso'
    if t.get('barrier'):
        return 'obstaculo'
    return None


# POIs: exatamente o que salva a viagem, nao um catalogo do OSM inteiro
POI_NO = {
    'amenity': {'fuel': 'combustivel', 'drinking_water': 'agua potavel',
                'hospital': 'hospital', 'clinic': 'hospital', 'pharmacy': 'farmacia',
                'restaurant': 'comida', 'cafe': 'comida', 'police': 'policia',
                'shelter': 'abrigo', 'toilets': 'banheiro'},
    'tourism': {'camp_site': 'camping', 'alpine_hut': 'abrigo', 'wilderness_hut': 'abrigo',
                'viewpoint': 'mirante', 'hotel': 'pousada', 'guest_house': 'pousada',
                'attraction': 'atracao', 'picnic_site': 'parada'},
    'shop': {'car_repair': 'mecanico', 'tyres': 'borracharia', 'convenience': 'mercado',
             'supermarket': 'mercado'},
    'natural': {'peak': 'pico', 'saddle': 'garganta', 'spring': 'nascente',
                'cave_entrance': 'caverna', 'volcano': 'pico'},
    'waterway': {'waterfall': 'cachoeira'},
    'barrier': {'gate': 'porteira', 'lift_gate': 'cancela', 'cattle_grid': 'mata-burro',
                'block': 'bloqueio', 'bollard': 'bloqueio'},
    'ford': {'yes': 'vau', 'stepping_stones': 'vau'},
    'man_made': {'water_well': 'poco', 'tower': 'torre'},
}


class Conta(osmium.SimpleHandler):
    def __init__(self):
        super().__init__()
        self.wv = collections.Counter()   # feicoes por camada
        self.wp = collections.Counter()   # pontos por camada
        self.poi = collections.Counter()
        self.poi_nome_bytes = 0
        self.rel = collections.Counter()

    def way(self, w):
        c = cls_way(w.tags)
        if c is None:
            return
        self.wv[c] += 1
        self.wp[c] += len(w.nodes)

    def node(self, n):
        for chave, mapa in POI_NO.items():
            v = n.tags.get(chave)
            if v is not None and v in mapa:
                self.poi[mapa[v]] += 1
                nome = n.tags.get('name')
                if nome:
                    self.poi_nome_bytes += len(nome.encode('utf-8')) + 1
                return

    def relation(self, r):
        c = cls_way(r.tags)
        if c is not None:
            self.rel[c] += 1


c = Conta()
c.apply_file(sys.argv[1])

print(f"\narquivo: {sys.argv[1]}\n")
print(f"{'camada':<24} {'feicoes':>10} {'pontos':>12} {'@4B':>10}")
print("-" * 60)
tp = 0
for k in sorted(c.wp, key=lambda x: -c.wp[x]):
    tp += c.wp[k]
    print(f"{k:<24} {c.wv[k]:>10,} {c.wp[k]:>12,} {c.wp[k]*4/MB:>8.1f}MB")
print("-" * 60)
print(f"{'TOTAL linhas/areas':<24} {sum(c.wv.values()):>10,} {tp:>12,} {tp*4/MB:>8.1f}MB")

print(f"\n{'POI':<24} {'quantos':>10}")
print("-" * 36)
for k, v in c.poi.most_common():
    print(f"{k:<24} {v:>10,}")
tpoi = sum(c.poi.values())
# 9 B por POI: 4+4 de coordenada em int32, 1 de classe. Nome vai a parte.
print("-" * 36)
print(f"{'TOTAL POI':<24} {tpoi:>10,}   -> {tpoi*9/MB:.2f} MB + {c.poi_nome_bytes/MB:.2f} MB de nomes")

print("\nrelacoes (multipoligonos: lagos e areas grandes, precisam de montagem):")
for k, v in c.rel.most_common(8):
    print(f"  {k:<24} {v:>8,}")
