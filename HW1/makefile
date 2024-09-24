# Makefile for gtuStudentGrades program

# Compiler
CC = gcc

# Compiler flags
CFLAGS = -Wall -Wextra -Werror

# Target executable
TARGET = gtuStudentGrades

# Source files
SRC = gtuStudentGrades.c

# Object files
OBJ = $(SRC:.c=.o)

# Rule to compile the program
$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

# Rule to compile source files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@


run: $(TARGET)
	./$(TARGET)

# Rule to clean compiled files
clean:
	rm -f $(TARGET) $(OBJ)
