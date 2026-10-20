#!/bin/bash
# Experiencia 0: descobrir o melhor chunkSize (L2, 1/2 L2, 1/4 L2)
# 8 milhoes de elementos, 8 threads, 6 execucoes por tamanho.
# uso: ./roda-exp-0-chunksize.sh   (ou via sbatch)

PROG=./prefixSumPth-v3-longlong
N=8000000
TH=8
RUNS=6

L2=$(getconf LEVEL2_CACHE_SIZE); [ -z "$L2" ] || [ "$L2" -le 0 ] && L2=262144
EL=$((L2 / 8))    # elementos long long que cabem no L2

for div in 1 2 4; do
    C=$((EL / div))
    echo "== chunkSize=$C (L2/$div) =="
    for i in $(seq 1 $RUNS); do
        $PROG $N $C $TH | grep MOPS
    done | tee /tmp/exp0.$$ 
    awk '{s+=$2} END {printf "media: %.3f MOPS\n", s/NR}' /tmp/exp0.$$
done
rm -f /tmp/exp0.$$
