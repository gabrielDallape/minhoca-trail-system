#!/bin/bash
# Roda dentro do container kicad-fr (ver gera.ps1). Tudo da placa sai daqui.
set -e
cd /w
python3 gera_pcb.py montar > saida/log_montar.txt
xvfb-run -a java -jar /opt/freerouting.jar -de saida/placa.dsn -do saida/placa.ses -mp 60 > saida/log_freerouting.txt 2>&1
python3 gera_pcb.py finalizar
kicad-cli pcb drc --severity-error --severity-warning -o saida/drc.txt saida/placa.kicad_pcb | tail -2
kicad-cli pcb render --side top --width 900 --height 1500 -o saida/render_top.png saida/placa.kicad_pcb >/dev/null
kicad-cli pcb render --side bottom --width 900 --height 1500 -o saida/render_bot.png saida/placa.kicad_pcb >/dev/null
# ---- arquivos de fabrica
rm -rf saida/fabrica saida/gerber && mkdir -p saida/fabrica saida/gerber
kicad-cli pcb export gerbers -o saida/gerber/ --layers F.Cu,B.Cu,F.Paste,F.Silkscreen,B.Silkscreen,F.Mask,B.Mask,Edge.Cuts --subtract-soldermask saida/placa.kicad_pcb >/dev/null
kicad-cli pcb export drill -o saida/gerber/ --format excellon --excellon-units mm --drill-origin absolute saida/placa.kicad_pcb >/dev/null
kicad-cli pcb export pos -o saida/pos_kicad.csv --side front --format csv --units mm --exclude-dnp saida/placa.kicad_pcb >/dev/null
python3 jlc.py
(cd saida/gerber && python3 -c "import zipfile,glob; z=zipfile.ZipFile('../fabrica/GERBER_placa_radio_v4.zip','w',zipfile.ZIP_DEFLATED); [z.write(f) for f in sorted(glob.glob('*'))]; print('zip:', len(z.namelist()), 'arquivos')")
kicad-cli pcb export pdf -o saida/fabrica/placa_radio_v4_camadas.pdf --layers F.Cu,F.Silkscreen,Edge.Cuts --mode-separate saida/placa.kicad_pcb >/dev/null 2>&1 || true
cp saida/render_top.png saida/fabrica/vista_cima.png; cp saida/render_bot.png saida/fabrica/vista_baixo.png
python3 ligacoes.py > /dev/null
