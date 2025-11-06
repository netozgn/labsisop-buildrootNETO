# Nome do executável
TARGET := teste

# Arquivo-fonte (na mesma pasta)
SRC := teste.c

# Caminho até o diretório raiz do Buildroot (3 níveis acima)
BUILDROOT_DIR := ../../..

# Compilador cruzado (gerado pelo Buildroot)
COMPILER := $(BUILDROOT_DIR)/output/host/bin/i686-buildroot-linux-gnu-gcc

# Diretório onde o binário será instalado no target
TARGET_DIR := $(BUILDROOT_DIR)/output/target/bin

# Flags de compilação
CFLAGS := -Wall -static -lpthread

# --- Regras ---

# Regra padrão: compila e copia o binário para o target
all: $(TARGET)
	@echo "Copiando $(TARGET) para $(TARGET_DIR)..."
	cp $(TARGET) $(TARGET_DIR)
	@echo "Concluído."

# Regra de compilação
$(TARGET): $(SRC)
	@echo "Cross-compilando $(SRC)..."
	$(COMPILER) -o $(TARGET) $(SRC) $(CFLAGS)

# Limpeza
clean:
	@echo "Limpando..."
	rm -f $(TARGET)

.PHONY: all clean
