# Compiler
CC = gcc

# Compiler flags
CFLAGS = -Wall -Wextra -Werror -lm -g

# Source files
SRC = pideshop.c hungryverymuch.c

# Object files
OBJ = $(SRC:.c=.o)

# Executables
EXEC = pideshop hungryverymuch

# Default target
all: $(EXEC)

# Build the pideshop executable
pideshop: pideshop.o
	$(CC) $(CFLAGS) -o $@ $^ -lpthread

# Build the hungryverymuch executable
hungryverymuch: hungryverymuch.o
	$(CC) $(CFLAGS) -o $@ $^ -lpthread

# Rule to build object files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean up the build
clean:
	rm -f $(OBJ) $(EXEC)
