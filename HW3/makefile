# Compiler
CC = gcc

# Compiler flags
CFLAGS = -Wall -Wextra -Werror

# client.c and server.c
CFILE = main.c

# Output files
OUTFILE = main

# Libraries
LIBS = -lrt -lpthread

# Build
build:
	$(CC) $(CFILE) $(LIBS) -o $(OUTFILE)

# Clean
clean:
	rm -f $(OUTFILE)
