#!/bin/bash
#SBATCH --exclusive
#SBATCH --nodes=1
#SBATCH --ntasks-per-node=1
#SBATCH --job-name=t1-expA
#SBATCH --output=resultados-expA-chunk-196608-job%j.txt  # <--- %j será substituído pelo Job ID
#OBS a saida do SLURM vai para  arquivo resultados-expA-chunk-196608-job%j.txt
#    e voce vai incluir isso em uma folha da PLANILHA

echo "USAGE: sbatch roda-exp-A-slurm.sh <nElements>"
echo "$0 rodando no host " `hostname` 

#arquivo com TODOS os dados de SAIDA da experiencia
SAIDA=saida-expA-job$SLURM_JOBID.txt
#OBS a saida do SLURM vai para  arquivo resultados-expA-chunk-196608-job%j.txt   # <--- %j será substituído pelo Job ID
#    e voce vai incluir isso em uma folha da PLANILHA
 
echo "$0 rodando no host " `hostname` >$SAIDA

echo "SLURM_JOB_NAME: "	$SLURM_JOB_NAME	
echo "SLURM_NODELIST: " $SLURM_NODELIST 
echo "SLURM_JOB_NODELIST: " $SLURM_JOB_NODELIST
echo "SLURM_JOB_CPUS_PER_NODE: " $SLURM_JOB_CPUS_PER_NODE

NTIMES=10
echo "nt " $NTIMES
MAX_EXECS=6                 #mude essa variavel para executar menos vezes (se necessario)
echo "MAX_EXECS " $MAX_EXECS

CHUNKSIZE=196608
echo "-------rodando a experiencia A, com 1/4 do L2, i.e. chunkSize=" $CHUNKSIZE " elementos long long" | tee -a $SAIDA
./prefixSumPth-v3-longlong 800 $CHUNKSIZE 8 | grep -e TYPE -e L2 -e L3 | tee -a $SAIDA 
echo "(tempo acumulado:" $SECONDS "segundos)" | tee -a $SAIDA 

for th in {1..8}
do
    echo "Executando $MAX_EXECS vezes com $1 elementos e $th threads:" "(tempo acumulado:" $SECONDS "segundos)" | tee -a $SAIDA 
    for j in $(seq 1 $NTIMES);
    do
        echo "-----------------------" >>$SAIDA
        if [ $j -le $MAX_EXECS ];    #limitar a MAX_EXECS execuções (por causa da fila do cluster ter APENAS 5 minutos)
        then 
          ./prefixSumPth-v3-longlong $1 $CHUNKSIZE $th | tee -a $SAIDA | grep MOPS
          #echo "(tempo acumulado:" $SECONDS "segundos)" | tee -a $SAIDA
        else
          echo "nao executado, j=" $j   | tee -a $SAIDA 
        fi  
    done
done
echo "O tempo total dessa shell foi de" $SECONDS "segundos"
echo "SLURM_JOB_NAME: "	$SLURM_JOB_NAME	
echo "SLURM_NODELIST: " $SLURM_NODELIST 
echo "SLURM_JOB_NODELIST: " $SLURM_JOB_NODELIST
echo "SLURM_JOB_CPUS_PER_NODE: " $SLURM_JOB_CPUS_PER_NODE
#imprime infos do job slurm (e.g. TEMPO até aqui no fim do job)
squeue -j $SLURM_JOBID




