CC = gcc
CFLAGS=-Wall -Wextra -std=gnu99 -Iinclude -g -O0 -pthread
CFLAGS+=-DDEBUG
AR = ar
ARFLAGS = rcs
SRC_PATH=./src
BIN_PATH=./bin
OBJ_PATH=./obj
LOGGER_OBJ = $(OBJ_PATH)/logger.o
LOGGER_LIB = $(BIN_PATH)/liblogger.a
SRC = $(wildcard src/*.c)
OBJ = $(patsubst src/%.c,obj/%.o,$(SRC))

all: server

run:
	./$(BIN_PATH)/server

bin:
	mkdir -p $(BIN_PATH)

obj:
	mkdir -p $(OBJ_PATH)


$(LOGGER_LIB): $(LOGGER_OBJ) | bin
	$(AR) $(ARFLAGS) $@ $^

obj/%.o: src/%.c | obj
	$(CC) $(CFLAGS) -c $< -o $@

server: $(OBJ) $(LOGGER_LIB) | bin
	$(CC) $(CFLAGS) $(OBJ) -Lbin -llogger -o $(BIN_PATH)/server

.PHONY: clean run all
clean:
	rm -rf $(BIN_PATH)
	rm -rf $(OBJ_PATH)
	rm gateway.log
