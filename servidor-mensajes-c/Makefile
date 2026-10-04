CC      ?= gcc
CFLAGS  ?= -O2 -g
CFLAGS  += -std=c11 -Wall -Wextra -Wpedantic -Wshadow -pthread
LDFLAGS += -pthread

BUILD   := build
SRC     := src/protocolo.c src/lector.c src/servidor.c
OBJ     := $(SRC:src/%.c=$(BUILD)/%.o)

.PHONY: all test valgrind tsan asan clean

all: $(BUILD)/servidor-sms

$(BUILD):
	mkdir -p $@

$(BUILD)/%.o: src/%.c src/*.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/servidor-sms: $(OBJ) $(BUILD)/main.o
	$(CC) $^ -o $@ $(LDFLAGS)

$(BUILD)/test_protocolo: tests/test_protocolo.c src/protocolo.c src/lector.c src/*.h tests/minitest.h | $(BUILD)
	$(CC) $(CFLAGS) tests/test_protocolo.c src/protocolo.c src/lector.c -o $@ $(LDFLAGS)

$(BUILD)/test_servidor: tests/test_servidor.c $(SRC) src/*.h tests/minitest.h | $(BUILD)
	$(CC) $(CFLAGS) tests/test_servidor.c $(SRC) -o $@ $(LDFLAGS)

test: $(BUILD)/test_protocolo $(BUILD)/test_servidor
	./$(BUILD)/test_protocolo
	./$(BUILD)/test_servidor

# Pérdidas de memoria y accesos inválidos
valgrind: $(BUILD)/test_protocolo $(BUILD)/test_servidor
	valgrind --leak-check=full --errors-for-leak-kinds=all --error-exitcode=1 -q ./$(BUILD)/test_protocolo
	valgrind --leak-check=full --errors-for-leak-kinds=all --error-exitcode=1 -q ./$(BUILD)/test_servidor

# Condiciones de carrera entre hilos (ThreadSanitizer)
tsan:
	$(MAKE) BUILD=build/tsan CFLAGS="-O1 -g -fsanitize=thread" LDFLAGS="-fsanitize=thread" test

# Desbordes y comportamiento indefinido
asan:
	$(MAKE) BUILD=build/asan CFLAGS="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer" \
		LDFLAGS="-fsanitize=address,undefined" test

clean:
	rm -rf build
