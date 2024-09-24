#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <math.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>

#define MAX_ORDERS 1000
#define SHOVEL_COUNT 3
#define MAX_cookThreads 100
#define MAX_DELIVERY_THREADS 100

typedef struct
{
    int orderID;
    int x;
    int y;
    int clientSocket;
    int status; // Status of the order: 0 - pending, 1 - cooking, 2 - ready for delivery, 3 - delivered
} orderStruct;

pthread_mutex_t orderQueueMutex = PTHREAD_MUTEX_INITIALIZER; //  Mutex for order queue
pthread_cond_t isOrderAvailable = PTHREAD_COND_INITIALIZER;  // Condition variable for order availability
pthread_cond_t isDeliveryReady = PTHREAD_COND_INITIALIZER;   // Condition variable for delivery readiness

orderStruct orderQueue[MAX_ORDERS];    // Order queue for pending orders
orderStruct deliveryQueue[MAX_ORDERS]; // Order queue for orders ready for delivery
int orderCount = 0;                    // Number of orders in the pending queue
int deliveryCount = 0;                 // Number of orders in the delivery queue

pthread_mutex_t shovelMutex = PTHREAD_MUTEX_INITIALIZER;     // Mutex for shovels
pthread_cond_t isShovelAvailable = PTHREAD_COND_INITIALIZER; // Condition variable for shovel availability
int shovels = SHOVEL_COUNT;                                  // Number of available shovels

volatile sig_atomic_t stop = 0; // Flag to indicate termination
int serverSocket;               // Server socket
int k;                          // Delivery constant
int cookThreadPoolSize;         // Number of cook threads
int deliveryPoolSize;           // Number of delivery threads
int orderCounter = 1;           // Starting order ID counter

pthread_t *cookThreads;     // Array to store cook threads
pthread_t *deliveryThreads; // Array to store delivery threads

int logFile;                                    // Log file descriptor
int deliveredCount[MAX_DELIVERY_THREADS] = {0}; // Array to store delivered order count for each delivery thread

// Function to handle logging
void serverLog(const char *message)
{
    pthread_mutex_lock(&orderQueueMutex);
    int writtenBytes = write(logFile, message, strlen(message));
    if (writtenBytes < 0)
    {
        perror("Failed to write to log file");
    }
    pthread_mutex_unlock(&orderQueueMutex);
}

void handleSigInt(int sig)
{
    // Print a termination message with signal number
    printf("\nTermination signal received: %d\n", sig);

    // Print the most delivered orders by a delivery thread
    int maxDelivered = 0;
    int i;
    for (i = 0; i < deliveryPoolSize; i++)
    {
        if (deliveredCount[i] > maxDelivered)
        {
            maxDelivered = deliveredCount[i];
        }
    }
    printf("Most delivered orders by a delivery thread: %d by the %dth thread\n", maxDelivered, i);

    // Set the stop flag to indicate termination
    pthread_mutex_lock(&orderQueueMutex);
    stop = 1;
    pthread_mutex_unlock(&orderQueueMutex);

    // Close the server socket
    close(serverSocket);

    // Print a termination message
    printf("\nServer terminated.\n");

    serverLog("Server terminated.\n");

    // Close log file
    close(logFile);

    // Exit the process
    exit(0);
}

double calculateDistance(int x1, int y1, int x2, int y2)
{
    return sqrt(pow(x2 - x1, 2) + pow(y2 - y1, 2));
}

void *cookThread()
{
    while (stop == 0)
    {
        orderStruct order;

        pthread_mutex_lock(&orderQueueMutex);
        while (orderCount == 0 && stop == 0)
        {
            pthread_cond_wait(&isOrderAvailable, &orderQueueMutex);
        }
        if (stop)
        {
            pthread_mutex_unlock(&orderQueueMutex);
            break;
        }

        order = orderQueue[--orderCount];
        pthread_mutex_unlock(&orderQueueMutex);

        // Check order status before proceeding
        if (order.status != 0)
        {
            printf("Order %d is not in pending state.\n", order.orderID);
            continue;
        }

        // Mark order as cooking
        order.status = 1;

        // Prepare the pide
        int preparingTime = rand() % 5 + 1;
        printf("Cook is preparing order %d. Cooking time: %d\n", order.orderID, preparingTime);
        sleep(preparingTime);

        // Acquire a shovel
        pthread_mutex_lock(&shovelMutex);
        while (shovels == 0)
        {
            pthread_cond_wait(&isShovelAvailable, &shovelMutex);
        }
        shovels--;
        pthread_mutex_unlock(&shovelMutex);

        // Simulate putting pide in the oven (using a shovel)
        printf("Cook is putting order %d into the oven.\n", order.orderID);
        int cookingTime = preparingTime / 2;
        sleep(cookingTime);

        // Release the shovel
        pthread_mutex_lock(&shovelMutex);
        shovels++;
        pthread_mutex_unlock(&shovelMutex);
        pthread_cond_signal(&isShovelAvailable);

        // Mark order as ready for delivery
        pthread_mutex_lock(&orderQueueMutex);
        order.status = 2;                       // Ready for delivery
        deliveryQueue[deliveryCount++] = order; // Move to delivery queue
        pthread_mutex_unlock(&orderQueueMutex);

        // Log order state change
        char logMsg[128];
        snprintf(logMsg, sizeof(logMsg), "Order %d is ready for delivery.\n", order.orderID);
        serverLog(logMsg);

        printf("Order %d is ready for delivery.\n", order.orderID);
        pthread_cond_signal(&isDeliveryReady);
    }
    return NULL;
}

void *deliveryThread(void *arg)
{
    int threadIndex = *(int *)arg;
    free(arg);

    while (stop == 0)
    {
        orderStruct order;

        pthread_mutex_lock(&orderQueueMutex);
        while (deliveryCount == 0 && stop == 0)
        {
            pthread_cond_wait(&isDeliveryReady, &orderQueueMutex);
        }
        if (stop)
        {
            pthread_mutex_unlock(&orderQueueMutex);
            break;
        }

        order = deliveryQueue[--deliveryCount];
        pthread_mutex_unlock(&orderQueueMutex);

        // Simulate delivery time
        // calculate distance between the restaurant and the delivery location
        double distance = calculateDistance(0, 0, order.x, order.y);
        int deliveryTime = distance / k;
        printf("Delivery thread %d is delivering order %d. Delivery time: %d\n", threadIndex, order.orderID, deliveryTime);
        sleep(deliveryTime);

        // Notify client about delivery
        char deliveryMessage[128];
        snprintf(deliveryMessage, sizeof(deliveryMessage), "Order %d delivered to (%d, %d).\n", order.orderID, order.x, order.y);
        send(order.clientSocket, deliveryMessage, strlen(deliveryMessage), 0);

        // Log delivery
        snprintf(deliveryMessage, sizeof(deliveryMessage), "Order %d delivered to (%d, %d).\n", order.orderID, order.x, order.y);
        serverLog(deliveryMessage);

        // Increment delivery count for this thread
        deliveredCount[threadIndex]++;

        // Print delivery count for this thread
        printf("Delivery thread %d delivered %d orders.\n", threadIndex, deliveredCount[threadIndex]);
    }

    return NULL;
}

void *managerThread(void *arg)
{
    int clientSocket = *(int *)arg;
    free(arg);

    char buffer[128];
    int len = recv(clientSocket, buffer, sizeof(buffer), 0);
    if (len > 0)
    {

        buffer[len] = '\0';
        int x, y;
        sscanf(buffer, "X:%d,Y:%d", &x, &y);

        orderStruct order = {.orderID = orderCounter++, .x = x, .y = y, .clientSocket = clientSocket, .status = 0};
        printf("Received order %d: x=%d, y=%d\n", order.orderID, x, y);

        pthread_mutex_lock(&orderQueueMutex);
        orderQueue[orderCount++] = order;
        pthread_mutex_unlock(&orderQueueMutex);

        pthread_cond_signal(&isOrderAvailable);

        // Log order reception
        char logMsg[128];
        snprintf(logMsg, sizeof(logMsg), "Received order %d: x=%d, y=%d\n", order.orderID, x, y);
        serverLog(logMsg);

        // Print connection message and current client count
        snprintf(logMsg, sizeof(logMsg), "Client connected. Current number of clients: %d\n", orderCount);
        serverLog(logMsg);
    }
    else
    {
        close(clientSocket);
        printf("Failed to receive data from client\n");
    }

    pthread_exit(NULL);
}

int main(int argc, char *argv[])
{
    if (argc != 5)
    {
        fprintf(stderr, "Usage: %s <Port> <Cook Thread Pool Size> <Delivery Pool Size> <k>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int port = atoi(argv[1]);
    cookThreadPoolSize = atoi(argv[2]);
    deliveryPoolSize = atoi(argv[3]);
    k = atoi(argv[4]);

    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    struct sigaction action;
    action.sa_handler = handleSigInt;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    sigaction(SIGINT, &action, NULL);

    // Create socket
    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    logFile = open("serverLog.txt", O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (logFile < 0)
    {
        perror("Failed to open log file");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(serverSocket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Bind failed");
        close(serverSocket);
        exit(EXIT_FAILURE);
    }

    if (listen(serverSocket, 10) < 0)
    {
        perror("Listen failed");
        close(serverSocket);
        exit(EXIT_FAILURE);
    }

    // Print server IP address
    char hostbuffer[256];
    char *IPbuffer;
    struct hostent *host_entry;
    gethostname(hostbuffer, sizeof(hostbuffer));
    host_entry = gethostbyname(hostbuffer);
    IPbuffer = inet_ntoa(*((struct in_addr *)host_entry->h_addr_list[0]));
    printf("Server is running on IP: %s, Port: %d\n", IPbuffer, port);

    cookThreads = malloc(cookThreadPoolSize * sizeof(pthread_t));
    deliveryThreads = malloc(deliveryPoolSize * sizeof(pthread_t));

    if (cookThreads == NULL || deliveryThreads == NULL)
    {
        fprintf(stderr, "Memory allocation failed\n");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < cookThreadPoolSize; i++)
    {
        pthread_create(&cookThreads[i], NULL, cookThread, NULL);
    }

    for (int i = 0; i < deliveryPoolSize; i++)
    {
        int *threadIndex = malloc(sizeof(int));
        *threadIndex = i;
        pthread_create(&deliveryThreads[i], NULL, deliveryThread, threadIndex);
    }

    while (stop == 0)
    {
        int *clientSocket = malloc(sizeof(int));
        *clientSocket = accept(serverSocket, (struct sockaddr *)&client_addr, &client_len);
        if (*clientSocket < 0)
        {
            perror("Accept failed");
            free(clientSocket);
            continue;
        }

        printf("Client connected\n");

        pthread_t manager;
        pthread_create(&manager, NULL, managerThread, clientSocket);
        pthread_detach(manager);

        pthread_cond_signal(&isOrderAvailable); // Signal order availability

        // Log client connection
        char logMsg[128];
        snprintf(logMsg, sizeof(logMsg), "Client connected from %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        serverLog(logMsg);
    }

    for (int i = 0; i < cookThreadPoolSize; i++)
    {
        pthread_join(cookThreads[i], NULL);
    }

    for (int i = 0; i < deliveryPoolSize; i++)
    {
        pthread_join(deliveryThreads[i], NULL);
    }

    free(cookThreads);
    free(deliveryThreads);

    close(serverSocket);
    close(logFile);
    return 0;
}
