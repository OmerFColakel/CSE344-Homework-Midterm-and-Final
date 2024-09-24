# Compiler
CC = gcc

# Compiler flags
CFLAGS = -Wall -Wextra -Werror

# client.c and server.c
CLIENT = client.c
SERVER = server.c

# Output files
CLIENT_OUT = NehosClient
SERVER_OUT = neHosServer

# Libraries
LIBS = -lrt -lpthread

# Build
build:
	$(CC) $(SERVER) $(LIBS) -o $(SERVER_OUT)
	$(CC) $(CLIENT) $(LIBS) -o $(CLIENT_OUT)

# Clean
clean:
	rm -f $(SERVER_OUT) $(CLIENT_OUT)
