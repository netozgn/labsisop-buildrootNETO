#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#define BUFFER_LENGTH 512

int main() {
    int fd;
    char receive[BUFFER_LENGTH];
    char stringToSend[BUFFER_LENGTH];

    printf("Starting XTEA device test...\n");

    fd = open("/dev/simple_driver_desafio", O_RDWR);
    if (fd < 0) {
        perror("Failed to open the device...");
        return errno;
    }

    // Exemplo de comando pronto (16 bytes / 32 hex)
    snprintf(stringToSend, BUFFER_LENGTH,
         "enc aabbccddeeff00112233445566778899"); // só operação + dados

    printf("Enviando comando: %s\n", stringToSend);

    if (write(fd, stringToSend, strlen(stringToSend)) < 0) {
        perror("Erro ao escrever no device");
        close(fd);
        return 1;
    }

    int ret = read(fd, receive, BUFFER_LENGTH - 1);
    if (ret < 0) {
        perror("Erro ao ler do device");
        close(fd);
        return 1;
    }

    receive[ret] = '\0';
    printf("Resultado recebido: [%s]\n", receive);

    close(fd);
    return 0;
}