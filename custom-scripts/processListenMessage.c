#define _GNU_SOURCE
#include <unistd.h>
#include <sys/syscall.h>
#include <stdio.h>

#define SYS_listenMessage 387

int main(int argc, char **argv) {
    if (argc <= 1) {
        printf("Uso: %s <mensagem>\n", argv[0]);
        return -1;
    }

    char *msg = argv[1];
    printf("Chamando syscall com: \"%s\"\n", msg);
    long ret = syscall(SYS_listenMessage, msg);
    printf("Syscall retornou: %ld\n", ret);

    return ret;
}