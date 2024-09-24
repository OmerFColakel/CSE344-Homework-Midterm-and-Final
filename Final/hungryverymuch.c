#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <string.h>
#include <arpa/inet.h>

typedef struct
{
    int id;
    char *serverIP;
    int serverPort;
    int x;
    int y;
} clientData;

volatile sig_atomic_t stop = 0;
int *clientSockets;
pthread_t *clients;
int numClients;

void handleSigInt(int sig)
{
    printf("\nTermination signal received: %d\n", sig);
    stop = 1;
    for (int i = 0; i < numClients; i++)
    {
        if (clientSockets[i] != 0)
        {
            // Notify server to cancel the order
            send(clientSockets[i], "CANCEL", strlen("CANCEL"), 0);
            close(clientSockets[i]);
        }
    }
    printf("Client generator terminated.\n");
    free(clientSockets);
    free(clients);
    exit(0);
}

void *clientThread(void *arg)
{
    clientData *data = (clientData *)arg;
    struct sockaddr_in serverAddr;

    // Create socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        perror("Socket creation failed");
        pthread_exit(NULL);
    }

    clientSockets[data->id] = sock;

    // Server address configuration
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(data->serverPort);
    inet_pton(AF_INET, data->serverIP, &serverAddr.sin_addr);

    // Connect to server
    printf("Client %d connecting to %s:%d\n", data->id, data->serverIP, data->serverPort);
    if (connect(sock, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
    {
        perror("Connection failed");
        close(sock);
        pthread_exit(NULL);
    }
    printf("Client %d connected\n", data->id);

    // Send x and y to server
    char buffer[128];
    sprintf(buffer, "X:%d,Y:%d", data->x, data->y);
    if (send(sock, buffer, strlen(buffer), 0) < 0)
    {
        perror("Send failed");
        close(sock);
        pthread_exit(NULL);
    }
    printf("Client %d sent: %s\n", data->id, buffer);

    // Wait for server response (order delivery notification)
    int recv_len = recv(sock, buffer, 128, 0);
    if (recv_len < 0)
    {
        perror("Receive failed");
    }
    else if (recv_len == 0)
    {
        printf("Server closed connection unexpectedly\n");
    }
    else
    {
        buffer[recv_len] = '\0';
        if (strcmp(buffer, "CANCEL") == 0)
        {
            printf("Client %d received order cancellation\n", data->id);
        }
        else
        {
            printf("%s", buffer);
        }
    }

    close(sock);
    clientSockets[data->id] = 0;
    printf("Client %d finished\n", data->id);
    free(data);
    pthread_exit(NULL);
}

int main(int argc, char *argv[])
{
    if (argc != 6)
    {
        fprintf(stderr, "Usage: %s <IP> <Port> <Number of Clients> <p> <q>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char *serverIP = argv[1];
    int serverPort = atoi(argv[2]);
    numClients = atoi(argv[3]);
    int p = atoi(argv[4]);
    int q = atoi(argv[5]);

    // Dynamic memory allocation for client sockets and threads
    clientSockets = malloc(numClients * sizeof(int));
    clients = malloc(numClients * sizeof(pthread_t));

    if (clientSockets == NULL || clients == NULL)
    {
        fprintf(stderr, "Memory allocation failed\n");
        exit(EXIT_FAILURE);
    }

    struct sigaction sa;
    sa.sa_handler = handleSigInt;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGINT, &sa, NULL) == -1)
    {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    srand(time(NULL));
    for (int i = 0; i < numClients; i++)
    {
        clientData *data = malloc(sizeof(clientData));
        if (data == NULL)
        {
            fprintf(stderr, "Memory allocation failed\n");
            exit(EXIT_FAILURE);
        }
        data->id = i;
        data->serverIP = serverIP;
        data->serverPort = serverPort;
        data->x = (rand() % p) - (p / 2);
        data->y = (rand() % q) - (q / 2);

        pthread_create(&clients[i], NULL, clientThread, (void *)data);
    }

    for (int i = 0; i < numClients; i++)
    {
        pthread_join(clients[i], NULL);
    }

    free(clientSockets);
    free(clients);

    return 0;
}
