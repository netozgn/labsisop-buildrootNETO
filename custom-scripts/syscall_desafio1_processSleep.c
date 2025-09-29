#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <string.h>

#define __NR_listSleepProcesses 386
// 386 i386	listSleepProcesses    listSleepProcesses
// em syscall_32.tbl


/*
DESAFIO:
Adicione uma nova chamada de sistema para retornar uma lista com todos os processos em 
estado de sleep. Um processo fica em estado de sleep quando ele precisa de recursos que 
não estão disponíveis no momento. Quando em estado de sleep, o processo não utiliza CPU. 
Para descobrir se um processo esta nesse estado, verifique se o estado dele é TASK_RUNNING 
(em execução), TASK_INTERRUPTIBLE (processo do usuário em sleep) ou TASK_UNINTERRUPTIBLE
 (processo do kernel / chamada de sistema em sleep).*/

int main() {
    char buf[2048];
    int ret;

    ret = syscall(__NR_listSleepProcesses, buf, sizeof(buf));
    if (ret < 0) {
        perror("syscall");
        return 1;
    }

    buf[ret] = '\0';
    printf("Processos em sleep:\n%s\n", buf);

    return 0;
}