#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h> 
#include <sched.h>    

// --- Variáveis Globais ---
char *global_buffer;
long global_index = 0;
long buffer_size;
pthread_mutex_t mutex; // Mutex para exclusão mútua com herança de prioridade
int *sched_counts; 
int num_threads;

pthread_barrier_t start_barrier; // Barreira para sincronizar o início

// --- Estrutura para passar dados para a thread ---
typedef struct {
    char id;
    int thread_num; 
} thread_args_t;

// --- Função utilitária para converter string em política ---
int get_policy(const char *name) {
    if (strcmp(name, "SCHED_OTHER") == 0) return SCHED_OTHER;
    if (strcmp(name, "SCHED_BATCH") == 0) return SCHED_BATCH;
    if (strcmp(name, "SCHED_IDLE") == 0) return SCHED_IDLE;
    if (strcmp(name, "SCHED_FIFO") == 0) return SCHED_FIFO;
    if (strcmp(name, "SCHED_RR") == 0) return SCHED_RR;
    
    // SCHED_LOW_IDLE não é padrão POSIX, pode não estar disponível
    // Se for um requisito, SCHED_IDLE é o substituto moderno.
    if (strcmp(name, "SCHED_LOW_IDLE") == 0) return SCHED_IDLE;
    
    return SCHED_OTHER;
}

// --- Função da Thread ---
void *worker_function(void *arg) {
    thread_args_t *data = (thread_args_t *)arg;
    char id = data->id;

    // 1. Espera na barreira para que todas as threads sejam criadas
    pthread_barrier_wait(&start_barrier);

    // 2. Ciclo de trabalho (como pedido no trabalho)
    while (1) {
        // --- Início da Região Crítica ---
        pthread_mutex_lock(&mutex); 

        if (global_index >= buffer_size) {
            // Buffer cheio, libera o mutex e termina
            pthread_mutex_unlock(&mutex); 
            break;
        }

        // --- TRABALHO MÍNIMO (como na especificação) ---
        global_buffer[global_index] = id;
        global_index++;
        // --- FIM DO TRABALHO ---

        // --- Fim da Região Crítica ---
        pthread_mutex_unlock(&mutex); 
    }

    pthread_exit(NULL);
}

// --- Função de Pós-Processamento ---
void post_process_and_print() {
    printf("Buffer (primeiros 200 caracteres):\n");
    long print_limit = (buffer_size < 200) ? buffer_size : 200;
    for (long i = 0; i < print_limit; i++) {
        printf("%c", global_buffer[i]);
    }
    if (buffer_size > 200) printf("...");
    printf("\n");

    // --- Pós-processamento ---
    printf("\nPós-processamento (padrão de execução):\n");
    char last_char = '\0';
    // Aloca e zera o array de contagem
    sched_counts = (int *)calloc(num_threads, sizeof(int)); 
    if (sched_counts == NULL) {
        perror("calloc sched_counts");
        return;
    }

    for (long i = 0; i < buffer_size; i++) {
        if (global_buffer[i] != last_char) {
            // Garante que o caractere é um ID de thread válido
            if (global_buffer[i] >= 'A' && global_buffer[i] < 'A' + num_threads) {
                printf("%c", global_buffer[i]);
                last_char = global_buffer[i];
                sched_counts[last_char - 'A']++; // Contabiliza a troca
            }
        }
    }
    printf("\n");

    // --- Contagem de Escalonamentos ---
    printf("\nContagem de escalonamentos:\n");
    for (int i = 0; i < num_threads; i++) {
        printf("%c = %d\n", 'A' + i, sched_counts[i]);
    }
}

// --- Função Principal ---
int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Uso: %s <num_threads> <buffer_size_kb> [politica_1 prio_1 ...]\n", argv[0]);
        return 1;
    }

    num_threads = atoi(argv[1]);
    buffer_size = atol(argv[2]) * 1024; // Converte KB para bytes

    if (num_threads <= 0 || buffer_size <= 0 || num_threads > 26) {
        fprintf(stderr, "Valores inválidos. Max 26 threads.\n");
        return 1;
    }

    // Alocação
    global_buffer = (char *)malloc(buffer_size);
    if (global_buffer == NULL) {
        perror("malloc global_buffer");
        return 1;
    }
    
    pthread_t *threads = (pthread_t *)malloc(num_threads * sizeof(pthread_t));
    thread_args_t *args = (thread_args_t *)malloc(num_threads * sizeof(thread_args_t));

    // Inicializa atributos do Mutex para Herança de Prioridade
    pthread_mutexattr_t mta;
    pthread_mutexattr_init(&mta);
    if (pthread_mutexattr_setprotocol(&mta, PTHREAD_PRIO_INHERIT) != 0) {
        perror("pthread_mutexattr_setprotocol");
        return 1;
    }
    pthread_mutex_init(&mutex, &mta);

    // Inicializa a barreira para esperar por (num_threads + 1) -> (workers + main)
    pthread_barrier_init(&start_barrier, NULL, num_threads + 1);

    // Verifica se políticas customizadas foram passadas
    int custom_policies = (argc > 3);
    if (custom_policies && argc != 3 + (num_threads * 2)) {
        fprintf(stderr, "Erro: número incorreto de políticas e prioridades.\n");
        return 1;
    }

    // --- Loop de Criação das Threads ---
    for (int i = 0; i < num_threads; i++) {
        args[i].id = 'A' + i;
        args[i].thread_num = i;

        pthread_attr_t attr;
        struct sched_param param;
        pthread_attr_init(&attr);

        if (custom_policies) {
            const char *policy_str = argv[3 + i * 2];
            int priority = atoi(argv[3 + i * 2 + 1]);
            int policy = get_policy(policy_str);

            // IMPORTANTE: Para definir política, tem que fazer isso
            if (policy != SCHED_OTHER) {
                 pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
                 pthread_attr_setschedpolicy(&attr, policy);
                 param.sched_priority = priority;
                 pthread_attr_setschedparam(&attr, &param);
            }
            printf("Criando Thread %c (Poli: %s, Prio: %d)\n", args[i].id, policy_str, priority);

        } else {
             printf("Criando Thread %c (Padrão: SCHED_OTHER, Prio: 0)\n", args[i].id);
        }

       int rc = pthread_create(&threads[i], &attr, worker_function, &args[i]);
        if (rc != 0) {
             // Esta é a forma CORRETA de reportar o erro:
             fprintf(stderr, "ERRO: Falha ao criar Thread %c (Poli: %s, Prio: %d)\n", 
                     args[i].id, policy_str, priority);
             fprintf(stderr, "Código de erro %d: %s\n", rc, strerror(rc)); // strerror(rc) é a chave!
             
             // Se uma thread falhar, temos que abortar
             // para não causar deadlock na barreira.
             exit(1); 
        }
        pthread_attr_destroy(&attr);
    }

    // A main thread é a última a chegar na barreira
    printf("Todas as threads criadas. Liberando a barreira...\n");
    pthread_barrier_wait(&start_barrier);
    // As threads de tempo-real começarão a competir agora

    // --- Loop de Join ---
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("\n--- Execução Concluída ---\n");

    // --- Análise e Impressão ---
    post_process_and_print();

    // --- Limpeza ---
    free(global_buffer);
    free(threads);
    free(args);
    free(sched_counts);
    pthread_mutex_destroy(&mutex); 
    pthread_mutexattr_destroy(&mta); 
    pthread_barrier_destroy(&start_barrier);

    return 0;
}