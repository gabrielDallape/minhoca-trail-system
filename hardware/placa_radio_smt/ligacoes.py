"""Lista de ligacoes LIDA DA PLACA PRONTA (nao da tabela do gera_pcb.py).

Serve de conferencia independente: se uma rede aqui nao bater com o
docs/MONTAGEM.md, o erro esta na tabela PECAS."""
import collections
import pcbnew

b = pcbnew.LoadBoard("/w/saida/placa.kicad_pcb")
redes = collections.defaultdict(list)
for f in b.GetFootprints():
    for p in f.Pads():
        if p.GetNetname() and p.GetNumber() != "MP":
            redes[p.GetNetname()].append("%s.%s" % (f.GetReference(), p.GetNumber()))
with open("/w/saida/fabrica/ligacoes.txt", "w") as fp:
    for n in sorted(redes):
        fp.write("%-8s %s\n" % (n, "  ".join(sorted(redes[n], key=lambda s: (s.split(".")[0], int(s.split(".")[1]) if s.split(".")[1].isdigit() else 0)))))
print(open("/w/saida/fabrica/ligacoes.txt").read())
