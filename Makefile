CC=gcc
CFLAGS=-Wall -Iinclude

ifeq ($(DEBUG), 1)
	CFLAGS += -g -DDEBUG
endif

objects := $(patsubst src/%.c,build/%.o,$(wildcard src/*.c))

all: fsh

.PHONY: clean
clean:
	rm -rf build fsh

build:
	mkdir build

build/%.o: src/%.c build
	$(CC) $(CFLAGS) -c $< -o $@

fsh: $(objects)
	$(CC) $(CFLAGS) -o fsh $^ -lreadline

debug:
	$(MAKE) DEBUG=1