CC=gcc
CFLAGS=-Wall
# TODO : Add a debug mode
all: fsh

clean:
	@rm -rf *.o build/*.o fsh

build/commands.o: src/commands.c # FIXME : use wildcards to automate build process
	@mkdir -p build
	@$(CC) $(CFLAGS) -c src/commands.c -o build/commands.o

build/fsh.o: fsh.c
	@mkdir -p build
	@$(CC) $(CFLAGS) -c fsh.c -o build/fsh.o

fsh: build/commands.o build/fsh.o
	@$(CC) $(CFLAGS) -o fsh $^ -lreadline