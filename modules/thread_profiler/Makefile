
TARGET := thread_profiler

SRC := thread_profiler.c

BUILDROOT_DIR := ../..

COMPILER := $(BUILDROOT_DIR)/output/host/bin/i686-buildroot-linux-gnu-gcc

TARGET_DIR := $(BUILDROOT_DIR)/output/target/bin

# -lpthread: Essencial para linkar a biblioteca Pthreads (threads, semáforos)
# -static:   Recomendado para Buildroot. Inclui todas as bibliotecas
#            no executável, para que ele rode no target sem
#            precisar de arquivos .so extras.
# -Wall:     Ativa todos os warnings (boas práticas)
CFLAGS := -Wall -static -lpthread

# --- Regras ---

# Regra 'all' (padrão): Compila o programa e o instala.
# Esta é a regra que será executada se você apenas digitar 'make'.
all: $(TARGET)
	@echo "Copiando $(TARGET) para $(TARGET_DIR)..."
	cp $(TARGET) $(TARGET_DIR)
	@echo "Concluído."

# Regra para compilar o programa
$(TARGET): $(SRC)
	@echo "Cross-compilando $(SRC)..."
	$(COMPILER) -o $(TARGET) $(SRC) $(CFLAGS)

# Regra 'clean': Limpa os arquivos gerados
clean:
	@echo "Limpando..."
	rm -f $(TARGET)

# Declara que 'all' e 'clean' não são nomes de arquivos
.PHONY: all clean