CC = gcc
CFLAGS=-Wall -Wextra -std=gnu99 -Iinclude -g -O0 -pthread
AR = ar
ARFLAGS = rcs
SRC_PATH=./src
BIN_PATH=./bin
OBJ_PATH=./obj
LIB_PATH=./lib
SRC = $(wildcard src/*.c)
OBJ = $(patsubst src/%.c,obj/%.o,$(SRC))

AR_RAW_NAME+=connection_manager
AR_RAW_NAME+=endecode_helper
AR_RAW_NAME+=logger
AR_RAW_NAME+=share_buffer
AR_LINKER= $(patsubst %, -l%, $(AR_RAW_NAME))
AR_OBJ= $(patsubst %, $(OBJ_PATH)/%.o, $(AR_RAW_NAME))
AR_LIB_NAME=$(patsubst %, $(LIB_PATH)/lib%.a, $(AR_RAW_NAME))
AR_LINKER_PATH+=-L$(LIB_PATH)

all: gateway client

gw:
	$(BIN_PATH)/gateway 1234

cl:
	$(BIN_PATH)/client temperature 10 127.0.0.1 1234 1000
	$(BIN_PATH)/client humidity 21 127.0.0.1 1235 1000
	$(BIN_PATH)/client motion 30 127.0.0.1 1236 1000
	$(BIN_PATH)/client door 40 127.0.0.1 1236 1000
	$(BIN_PATH)/client smoke_gas 50 127.0.0.1 1237 1000

bin:
	mkdir -p $(BIN_PATH)

obj:
	mkdir -p $(OBJ_PATH)

lib:
	mkdir -p $(LIB_PATH)


obj/%.o: $(SRC_PATH)/%.c | obj
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -c $< -o $@

$(AR_LIB_NAME): $(AR_OBJ) | lib
	$(AR) $(ARFLAGS) $@ $^

client: $(OBJ_PATH)/client.o  $(AR_LIB_NAME) | bin obj
	$(CC) $(CFLAGS) $< $(AR_LINKER_PATH) $(AR_LINKER) -o $(BIN_PATH)/$@

gateway: $(OBJ_PATH)/gateway.o  $(AR_LIB_NAME) | bin obj
	$(CC) $(CFLAGS) $< $(AR_LINKER_PATH) $(AR_LINKER) -o $(BIN_PATH)/$@

.PHONY: clean run all
clean:
	rm -rf $(BIN_PATH)
	rm -rf $(OBJ_PATH)
	rm -rf $(LIB_PATH)
	rm gateway.log
