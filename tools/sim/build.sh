#!/bin/sh
# Compila (e opcionalmente roda) o SIMULADOR. Roda DENTRO do container gcc:14 -
# o build.ps1 chama isto. No Windows desta maquina o Smart App Control bloqueia
# qualquer compilador nao assinado (MinGW: saida 0xC0E90002), entao o GCC vive
# no Docker. O repositorio entra montado em /w.
#
#   NOS=3 sh tools/sim/build.sh [cenario|todos]
set -e
cd /w
SIM=tools/sim
OUT=$SIM/out
SRC=$OUT/src
N=${NOS:-4}

rm -rf "$SRC"; mkdir -p "$SRC/mts"
cp -r firmware/mts/core firmware/mts/app firmware/mts/hal "$SRC/mts/"
cp $SIM/fake_hal/*.h "$SRC/mts/hal/"          # SO o radio e o cartao sao falsos
cp firmware/*.h "$SRC/"

FLAGS="-std=gnu++17 -O2 -g -Wall -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable \
 -I $SIM -I $SIM/fake -I $SRC -I $SRC/mts"
OBJS=""
k=0
while [ $k -lt $N ]; do
  g++ $FLAGS -DNO_NS=no$k -c $SIM/no.cpp -o $OUT/no$k.o &
  OBJS="$OBJS $OUT/no$k.o"
  k=$((k + 1))
done
g++ $FLAGS -c $SIM/ar.cpp -o $OUT/ar.o &
g++ $FLAGS -c $SIM/sim.cpp -o $OUT/sim.o &
wait
g++ -o $OUT/sim $OUT/sim.o $OUT/ar.o $OBJS
echo "simulador: $OUT/sim ($N nos)"
if [ -n "$1" ]; then
  cd $OUT && ./sim "$1"
fi
