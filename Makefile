CC=gcc
CFLAGS=-Wall -Iinclude
# TODO : Add a debug mode

objects := $(patsubst src/%.c,build/%.o,$(wildcard src/*.c))

all: fsh

clean:
	rm -rf *.o build/*.o fsh

build/%.o: src/%.c
	mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

fsh: $(objects)
	$(CC) $(CFLAGS) -o fsh $^ -lreadline