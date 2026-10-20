mix_full:
	$(BIN_PATH)/client temperature 11 127.0.0.1 1234 500  --glue &
	$(BIN_PATH)/client temperature 10 127.0.0.1 1234 50 --fragment &
	$(BIN_PATH)/client humidity 21 127.0.0.1 1235 50 --fragment &
	$(BIN_PATH)/client door 40 127.0.0.1 1236 1000  --glue &
	$(BIN_PATH)/client motion 30 127.0.0.1 1236 500 --fragment &
	$(BIN_PATH)/client door 40 127.0.0.1 1236 200 --fragment &
	$(BIN_PATH)/client smoke_gas 50 127.0.0.1 1237 1000  --glue &
	$(BIN_PATH)/client smoke_gas 50 127.0.0.1 1237 300 --fragment &
	$(BIN_PATH)/client temperature 11 127.0.0.1 1234 500 &
	$(BIN_PATH)/client humidity 22 127.0.0.1 1235 400 &
	$(BIN_PATH)/client motion 30 127.0.0.1 1236 1000  --glue &
	$(BIN_PATH)/client motion 30 127.0.0.1 1236 1000 &
	$(BIN_PATH)/client door 40 127.0.0.1 1236 1000 &
	$(BIN_PATH)/client humidity 22 127.0.0.1 1235 400  --glue &
	$(BIN_PATH)/client smoke_gas 50 127.0.0.1 1237 1000 &
	wait

test_corrupt:
	$(BIN_PATH)/client humidity 22 127.0.0.1 1235 50 --corrupt &
	$(BIN_PATH)/client motion 30 127.0.0.1 1236 500 --corrupt &
	$(BIN_PATH)/client door 40 127.0.0.1 1236 200 --corrupt &
	$(BIN_PATH)/client smoke_gas 50 127.0.0.1 1237 300 --corrupt &

gw_fail:
	$(BIN_PATH)/gateway 8080 &
	$(BIN_PATH)/gateway 1234 &
	$(BIN_PATH)/gateway 12 124 421 &

tbp:
	$(BIN_PATH)/client temperature 11 127.0.0.1 1234 1000  &
	$(BIN_PATH)/client temperature 11 127.0.0.1 1234 500  --glue &
	$(BIN_PATH)/client temperature 10 127.0.0.1 1234 50 --fragment &

hlp:
	$(BIN_PATH)/client humidity 22 127.0.0.1 1235 1000 &
	$(BIN_PATH)/client humidity 22 127.0.0.1 1235 400  --glue &
	$(BIN_PATH)/client humidity 21 127.0.0.1 1235 200 --fragment &

door:
	$(BIN_PATH)/client door 40 127.0.0.1 1236 1000  --glue &
	$(BIN_PATH)/client door 40 127.0.0.1 1236 200 --fragment &
	$(BIN_PATH)/client door 40 127.0.0.1 1236 1000 &

motion:
	$(BIN_PATH)/client motion 30 127.0.0.1 1236 500 --fragment &
	$(BIN_PATH)/client motion 30 127.0.0.1 1236 1000  --glue &
	$(BIN_PATH)/client motion 30 127.0.0.1 1236 1000 &

sgp:
	$(BIN_PATH)/client smoke_gas 50 127.0.0.1 1237 1000  --glue &
	$(BIN_PATH)/client smoke_gas 50 127.0.0.1 1237 300 --fragment &
	$(BIN_PATH)/client smoke_gas 50 127.0.0.1 1237 1000 &
