.RECIPEPREFIX = >

CC = gcc
CFLAGS = -std=c11 -D_GNU_SOURCE -Wall -Wextra -O0 -g -Iinclude
LDLIBS = -pthread -lrt
SRC = $(wildcard src/*.c)
HDR = $(wildcard include/*.h)
BIN = kitchen
IPC_KEY = 0x4b495443
SHM_FILE = /dev/shm/cse325_g4_kitchen

all: $(BIN)

$(BIN): $(SRC) $(HDR)
> $(CC) $(CFLAGS) $(SRC) -o $(BIN) $(LDLIBS)

run: $(BIN)
> ./$(BIN) --mode=ordered --seconds=3 --verbose

test: $(BIN)
> @fail=0; \
> for m in ordered room sysv monitor; do \
>   out=$$(./$(BIN) --mode=$$m --seconds=2) || fail=1; \
>   echo "$$out" | grep '^CHECK' | sed "s/^/[$$m] /"; \
> done; \
> exit $$fail

clean:
> rm -f $(BIN) src/*.o
> @ipcrm -S $(IPC_KEY) 2>/dev/null || true
> rm -f $(SHM_FILE)

.PHONY: all run test clean
