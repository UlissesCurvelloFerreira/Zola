CC      = gcc
TARGET  = prefixSumPth-v3-longlong
SRC     = $(TARGET).c

# -O3: otimizacao maxima; -march=native: usa as instrucoes da CPU onde COMPILA
# (compile na mesma maquina/no em que vai rodar; se o no do cluster for diferente,
#  use:  make NATIVE=)
NATIVE  = -march=native
CFLAGS  = -O3 $(NATIVE) -funroll-loops -Wall -Wextra -std=gnu11
LDFLAGS = -pthread

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

# teste rapido de corretude (com verificacao)
test: $(TARGET)
	./$(TARGET) 1000003 0      8 -v
	./$(TARGET) 1000003 196608 8 -v
	./$(TARGET) 1000003 1000   3 -v
	./$(TARGET) 7       0      8 -v

clean:
	rm -f $(TARGET) *.o

.PHONY: all test clean
