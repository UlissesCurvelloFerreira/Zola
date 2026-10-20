// ci1316 - Programacao Paralela - Trab 1: Soma de Prefixos com PThreads
//
// uso: ./prefixSumPth-v3-longlong <nTotalElements> <chunkSize> <nThreads> [-v]
//   (mesma ordem usada nos scripts SLURM do professor)
//   chunkSize == 0  -> cada thread recebe UM unico bloco (sem reuso de cache)
//   -v              -> verifica o resultado sequencialmente ao final
//
// ALGORITMO (pool de threads + chunks + 1 vetor PartialSum com 1 elemento/thread)
// -------------------------------------------------------------------------
// O vetor eh dividido em chunks de C elementos. Os chunks sao processados em
// "rodadas": na rodada r, a thread t cuida do chunk de indice r*T + t.
//
//  Fase 1 (cada thread, no PROPRIO chunk): soma de prefixos local in place;
//          a soma total do chunk vai para PartialSum[t].
//  --- BARREIRA DO ALGORITMO (barrier "algoBarrier") ---
//  Fase 2 (cada thread): offset = carry + sum(PartialSum[0..t-1]);
//          soma o offset no proprio chunk (ainda quente no cache, pois acabou
//          de ser escrito na fase 1 e C eh pequeno, ex. 1/4 do L2);
//          carry += sum(PartialSum[0..T-1])  (cada thread mantem seu carry,
//          calculado redundantemente -> nao precisa de uma 2a barreira).
//
// PartialSum eh duplo (indices pares/impares de rodada) para que uma thread
// rapida que ja iniciou a rodada r+1 nao sobrescreva valores que uma lenta
// ainda esta lendo da rodada r. Assim basta 1 barreira por rodada.
//
// Barreiras do pool (poolBarrier): usada na entrada (libera o trabalho) e na
// saida (garante que TODAS as threads terminaram a fase 2 antes da thread 0
// retornar). Sem joins no caminho medido.
// Com chunkSize == 0 ha uma unica rodada => 1 barreira do algoritmo + 2 do pool.

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <time.h>

typedef long long TYPE;

#ifndef NTIMES
#define NTIMES 100
#endif
#define CACHE_LINE  64
#define MAX_THREADS 256

// ---------------------------------------------------------------- estado global
typedef struct {
    volatile TYPE v;
    char pad[CACHE_LINE - sizeof(TYPE)];   // evita false sharing
} PaddedSum;

static PaddedSum PartialSum[2][MAX_THREADS] __attribute__((aligned(CACHE_LINE)));

typedef struct { int id; int core; } ThreadArg;

static pthread_t         poolThreads[MAX_THREADS];
static ThreadArg         targs[MAX_THREADS];
static pthread_barrier_t poolBarrier;   // barreira do POOL
static pthread_barrier_t algoBarrier;   // barreira do algoritmo de soma de prefixos
static int               poolSize = 0;
static int               poolQuit = 0;

// "tarefa" corrente, escrita pela thread 0 ANTES da barreira de entrada
static TYPE *gVec;
static long  gN, gChunk;

// ---------------------------------------------------------------- tempo
static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

// ---------------------------------------------------------------- trabalho
static void prefixWork(int id)
{
    const int  T = poolSize;
    const long C = gChunk, N = gN;
    TYPE *restrict v = gVec;

    const long nChunks = (N + C - 1) / C;
    const long nRounds = (nChunks + T - 1) / T;
    TYPE carry = 0;

    for (long r = 0; r < nRounds; r++) {
        PaddedSum *ps = PartialSum[r & 1];
        long start = (r * T + id) * C;
        long end   = start + C;
        if (end > N) end = N;
        const int has = (start < N);

        // fase 1: prefixos locais in place
        TYPE sum = 0;
        if (has)
            for (long i = start; i < end; i++) { sum += v[i]; v[i] = sum; }
        ps[id].v = sum;

        pthread_barrier_wait(&algoBarrier);

        // fase 2: offset deste chunk
        TYPE off = carry, total = 0;
        for (int j = 0; j < T; j++) {
            TYPE s = ps[j].v;
            if (j < id) off += s;
            total += s;
        }
        carry += total;

        if (has && off != 0)
            for (long i = start; i < end; i++) v[i] += off;
    }
}

static void *workerMain(void *p)
{
    ThreadArg *a = (ThreadArg *)p;
    for (;;) {
        pthread_barrier_wait(&poolBarrier);      // espera trabalho
        if (poolQuit) break;
        prefixWork(a->id);
        pthread_barrier_wait(&poolBarrier);      // avisa termino
    }
    return NULL;
}

// ---------------------------------------------------------------- pool
static void initPool(int nThreads)
{
    int ncores = (int)sysconf(_SC_NPROCESSORS_ONLN);
    poolSize = nThreads;
    poolQuit = 0;
    pthread_barrier_init(&poolBarrier, NULL, nThreads);
    pthread_barrier_init(&algoBarrier, NULL, nThreads);

    // thread 0 = chamadora: fixa no core 0
    targs[0].id = 0; targs[0].core = 0;
    cpu_set_t cs; CPU_ZERO(&cs); CPU_SET(0, &cs);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cs);

    for (int i = 1; i < nThreads; i++) {
        targs[i].id = i;
        targs[i].core = i % ncores;     // 0..3 em cores fisicos distintos; 4..7 nos irmaos HT
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        cpu_set_t cpuset; CPU_ZERO(&cpuset); CPU_SET(targs[i].core, &cpuset);
        pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpuset);
        pthread_create(&poolThreads[i], &attr, workerMain, &targs[i]);
        pthread_attr_destroy(&attr);
    }
}

static void finishPool(void)
{
    if (!poolSize) return;
    poolQuit = 1;
    pthread_barrier_wait(&poolBarrier);
    for (int i = 1; i < poolSize; i++) pthread_join(poolThreads[i], NULL);
    pthread_barrier_destroy(&poolBarrier);
    pthread_barrier_destroy(&algoBarrier);
    poolSize = 0;
}

void ParallelPrefixSumPth(volatile TYPE *Vec, long nTotalElmts,
                          long chunkSize, int nThreads)
{
    if (poolSize != nThreads) { finishPool(); initPool(nThreads); }

    long C = chunkSize;
    if (C <= 0) C = (nTotalElmts + nThreads - 1) / nThreads;   // 1 chunk por thread
    if (C < 1)  C = 1;

    gVec = (TYPE *)Vec;     // cast: o qualificador volatile impediria otimizacoes
    gN = nTotalElmts;       // (a sincronizacao eh garantida pelas barreiras)
    gChunk = C;

    pthread_barrier_wait(&poolBarrier);   // libera o pool
    prefixWork(0);                        // thread 0 tambem trabalha
    pthread_barrier_wait(&poolBarrier);   // todas terminaram a fase 2
}

// ---------------------------------------------------------------- verificacao
void verifyPrefixSum(const TYPE *InputVec, volatile TYPE *prefixSumVec, long nTotalElmts)
{
    TYPE acc = 0;
    long errors = 0;
    for (long i = 0; i < nTotalElmts; i++) {
        acc += InputVec[i];
        if (prefixSumVec[i] != acc) {
            if (errors < 10)
                fprintf(stderr, "ERRO em [%ld]: esperado %lld, obtido %lld\n",
                        i, acc, prefixSumVec[i]);
            errors++;
        }
    }
    if (errors) printf("Verificacao: FALHOU (%ld erros)\n", errors);
    else        printf("Verificacao: OK\n");
}

// ---------------------------------------------------------------- main
static void printCaches(void)
{
    long l2 = sysconf(_SC_LEVEL2_CACHE_SIZE);
    long l3 = sysconf(_SC_LEVEL3_CACHE_SIZE);
    printf("TYPE = long long (%zu bytes)\n", sizeof(TYPE));
    printf("L2 cache = %ld bytes = %ld elementos (1/2 L2 = %ld, 1/4 L2 = %ld)\n",
           l2, l2 / (long)sizeof(TYPE), l2 / 2 / (long)sizeof(TYPE), l2 / 4 / (long)sizeof(TYPE));
    printf("L3 cache = %ld bytes = %ld elementos\n", l3, l3 / (long)sizeof(TYPE));
}

int main(int argc, char *argv[])
{
    if (argc < 4) {
        fprintf(stderr, "usage: %s <nTotalElements> <chunkSize> <nThreads> [-v]\n", argv[0]);
        return 1;
    }
    long nTotalElements = atol(argv[1]);
    long chunkSize      = atol(argv[2]);
    int  nThreads       = atoi(argv[3]);
    int  verify = 0;
    for (int i = 4; i < argc; i++) if (!strcmp(argv[i], "-v")) verify = 1;

    if (nTotalElements <= 0 || nThreads < 1 || nThreads > MAX_THREADS) {
        fprintf(stderr, "parametros invalidos\n");
        return 1;
    }
    if (chunkSize < 0) chunkSize = 0;

    printCaches();

    TYPE *InitialVector, *Vector;
    if (posix_memalign((void **)&InitialVector, CACHE_LINE, nTotalElements * sizeof(TYPE)) ||
        posix_memalign((void **)&Vector,        CACHE_LINE, nTotalElements * sizeof(TYPE))) {
        fprintf(stderr, "falha de alocacao\n");
        return 1;
    }

    for (long i = 0; i < nTotalElements; i++) {
        int r = rand();
        InitialVector[i] = (r % 1000) - 500;
    }

    initPool(nThreads);   // criacao das threads fora da medida

    // aquecimento (nao medido)
    memcpy(Vector, InitialVector, nTotalElements * sizeof(TYPE));
    ParallelPrefixSumPth(Vector, nTotalElements, chunkSize, nThreads);

    double copyTime = 0.0;
    double t0 = now();
    for (int k = 0; k < NTIMES; k++) {
        double c0 = now();
        memcpy(Vector, InitialVector, nTotalElements * sizeof(TYPE));
        copyTime += now() - c0;
        ParallelPrefixSumPth(Vector, nTotalElements, chunkSize, nThreads);
    }
    double total = (now() - t0) - copyTime;     // desconta os memcpy
    double perCall = total / NTIMES;
    double mops = (double)nTotalElements / perCall / 1e6;

    printf("nThreads=%d nTotalElements=%ld chunkSize=%ld NTIMES=%d\n",
           nThreads, nTotalElements, chunkSize, NTIMES);
    printf("Tempo por chamada: %.6f s (total %.6f s, memcpy descontado %.6f s)\n",
           perCall, total, copyTime);
    printf("Vazao: %.3f MOPS\n", mops);

    if (verify) verifyPrefixSum(InitialVector, Vector, nTotalElements);

    finishPool();
    free(InitialVector);
    free(Vector);
    return 0;
}
