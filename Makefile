CC=gcc
CFLAGS=-Wall -Iinclude
# TODO : Add a debug mode

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
