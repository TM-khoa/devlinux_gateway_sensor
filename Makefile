CC = gcc
CFLAGS=-Wall -Wextra -std=gnu99 -Iinclude -g -O0
SRC_PATH=./src
BIN_PATH=./bin

all: server

run:
	./$(BIN_PATH)/server

create_bin_path:
	mkdir -p $(BIN_PATH)

debug: create_bin_path
	$(CC) $(CFLAGS) -DDEBUG $(SRC_PATH)/server.c -o $(BIN_PATH)/server

server: create_bin_path
	$(CC) $(CFLAGS) $(SRC_PATH)/server.c -o $(BIN_PATH)/server

.PHONY: clean run all
clean:
	rm -rf $(BIN_PATH)
