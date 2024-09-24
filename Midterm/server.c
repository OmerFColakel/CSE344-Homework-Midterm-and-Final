#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <dirent.h>
#include <sys/wait.h>

enum RequestType // type of request
{
    ERROR = -1,
    TRY_CONNECT = 1,
    CONNECT = 2,
    HELP = 3,
    LIST = 4,
    READFROM = 5,
    WRITETO = 6,
    UPLOAD = 7,
    DOWNLOAD = 8,
    ARCHSERVER = 9,
    KILLSERVER = 10,
    QUIT = 11
};

typedef struct Request // request structure
{
    enum RequestType requestType; // type of request
    char request[256];            // request message
} Request;

typedef struct ClientQueueElement // client queue element
{
    int clientPID;
    struct ClientQueueElement *next;
} ClientQueueElement;

typedef struct ClientQueue // client queue
{
    ClientQueueElement *head;
    ClientQueueElement *tail;
    int size;
} ClientQueue;

pid_t globalClientPID = -1; // the pid of the client. parent process has -1
int globalNumOfClients = 0; // number of clients
int globalMaxClients = 0;   // maximum number of clients
ClientQueue *clientQueue;   // client queue

int printMessage(char *message, int length);                                                                                             // print message
void printUsage();                                                                                                                       // print usage
int createLoggerFile(char *dirName);                                                                                                     // create logger file
char *createWorkingDirectory(char *dirName);                                                                                             // create working directory
sem_t *createSemaphore(int serverID, char *semaphoreName);                                                                               // create with
void handleClientRequest(int serverFIFORead, int logFile, sem_t *serverSemaphore, sem_t *serverFileIOSemaphore, sem_t *loggerSemaphore); // handle client's connection request
int handleClient(int clientPID, sem_t *serverFileIOSemaphore, int logFile, sem_t *loggerSemaphore, pid_t parentID);                      // handle connected client
int handleHelp(Request request, int clientFIFORead, char *clientFIFOWR);                                                                 // handle help request
int handleList(Request request, int clientFIFORead, char *clientFIFOWR);                                                                 // handle list request
int handleReadFrom(Request request, int clientFIFORead, char *clientFIFOWR);                                                             // handle read from request
int handleWriteTo(Request request, int clientFIFORead, char *clientFIFOWR);                                                              // handle write to request
int handleUpload(Request request, int clientFIFORead, char *clientFIFOWR);                                                               // handle upload request
int handleDownload(Request request, int clientFIFORead, char *clientFIFOWR);                                                             // handle download request
int handleArchServer(Request request, int clientFIFORead, char *clientFIFOWR);                                                           // handle archive server request
void writeToLogger(int logFile, char *message, sem_t *loggerSemaphore);                                                                  // write to logger
void killClient();                                                                                                                       // kill client
int handleExit();                                                                                                                        // handle exit
void sigTermHandler(int signo);                                                                                                          // signal handler for SIGTERM
void sigIntHandler(int signo);                                                                                                           // signal handler for SIGINT
void sigChldHandler(int signo);                                                                                                          // signal handler for SIGCHLD
void initClientQueue();                                                                                                                  // initialize client queue
void initClientQueueElement(ClientQueueElement *element, int clientPID);                                                                 // initialize client queue element
void enqueueClient(int clientPID);                                                                                                       // enqueue client
int dequeueClient();                                                                                                                     // dequeue client
void printQueue();                                                                                                                       // print queue
void unlinkServerFIFO();                                                                                                                 // unlink server FIFO

int main(int argc, char *argv[])
{

    if (argc != 3)
    {
        printUsage();
        return EXIT_FAILURE;
    }

    struct sigaction act;
    act.sa_handler = sigIntHandler;
    sigaction(SIGINT, &act, NULL);

    act.sa_handler = sigTermHandler;
    sigaction(SIGTERM, &act, NULL);

    act.sa_handler = sigChldHandler;
    sigaction(SIGCHLD, &act, NULL);

    globalMaxClients = atoi(argv[2]);
    if (globalMaxClients <= 0)
    {
        printMessage("Invalid number of clients\n", 26);
        return EXIT_FAILURE;
    }

    char *dirName = argv[1];

    int serverID = getpid();
    char *serverIDString = malloc(256 * sizeof(char));
    sprintf(serverIDString, "Server is running with ID: %d\n", serverID);
    printMessage(serverIDString, strlen(serverIDString));
    free(serverIDString);
    sem_t *serverSemaphore = createSemaphore(serverID, "serverSemaphore");
    if (serverSemaphore < 0)
    {
        return EXIT_FAILURE;
    }

    sem_t *serverFileIOSemaphore = createSemaphore(serverID, "serverFileIOSemaphore");
    if (serverFileIOSemaphore < 0)
    {
        return EXIT_FAILURE;
    }

    char *serverfifo = malloc(256 * sizeof(char));
    sprintf(serverfifo, "/tmp/serverFIFO%d", serverID);
    if (mkfifo(serverfifo, 0666) < 0)
    {
        perror("Error creating server FIFO");
        return EXIT_FAILURE;
    }
    printMessage("Server FIFO created\n", 20);

    int serverFIFORead = open(serverfifo, O_RDWR);
    if (serverFIFORead < 0)
    {
        perror("Error opening server FIFO");
        return EXIT_FAILURE;
    }

    char *cwd = createWorkingDirectory(dirName);
    if (cwd == NULL)
    {
        return EXIT_FAILURE;
    }
    chdir(cwd);

    int logFile = createLoggerFile(cwd);
    if (logFile < 0)
    {
        return EXIT_FAILURE;
    }
    char *serverMessage = malloc(256 * sizeof(char));
    sprintf(serverMessage, "Server is running with ID: %d\n", serverID);
    writeToLogger(logFile, serverMessage, serverSemaphore);

    sem_t *loggerSemaphore = createSemaphore(serverID, "loggerSemaphore");
    if (loggerSemaphore < 0)
    {
        return EXIT_FAILURE;
    }

    initClientQueue();

    printMessage("Waiting for clients...\n", 23);
    while (1)
    {

        handleClientRequest(serverFIFORead, logFile, serverSemaphore, serverFileIOSemaphore, loggerSemaphore);
    }
    serverMessage = malloc(256 * sizeof(char));
    sprintf(serverMessage, "Server with ID %d is shutting down\n", serverID);
    writeToLogger(logFile, serverMessage, loggerSemaphore);
    close(logFile);
    close(serverFIFORead);
    unlinkServerFIFO();
    return EXIT_SUCCESS;
}

void handleClientRequest(int serverFIFORead, int logFile, sem_t *serverSemaphore, sem_t *serverFileIOSemaphore, sem_t *loggerSemaphore)
{
    Request request;
    int bytesRead = read(serverFIFORead, &request, sizeof(Request));
    if (bytesRead < 0)
    {
        if (errno == EINTR)
        {
            return;
        }
        perror("Error reading from server FIFO");
        return;
    }
    printf("num of clients: %d\n", globalNumOfClients);

    if (request.requestType == TRY_CONNECT) // try connect: check if there is an empty spot
    {
        if (globalNumOfClients >= globalMaxClients)
        {
            char *clientFIFOWR = malloc(256 * sizeof(char));
            sprintf(clientFIFOWR, "/tmp/clientFIFO%dWR", atoi(request.request));
            printMessage("Server is full. Please try again later.\n", 40);
            Request response;
            response.requestType = ERROR;
            strcpy(response.request, "Server is full. Please try again later.\n");
            int clientFIFOWrite = open(clientFIFOWR, O_WRONLY);
            if (clientFIFOWrite < 0)
            {
                perror("Error opening client FIFO for writing");
                return;
            }
            int written = write(clientFIFOWrite, &response, sizeof(Request));
            if (written < 0)
            {
                perror("Error writing to client FIFO");
                return;
            }
            close(clientFIFOWrite);
            sem_post(serverSemaphore);
            return;
        }
        char *responseString = malloc(256 * sizeof(char));
        int clientPID = atoi(request.request);
        char *clientFIFOWR = malloc(256 * sizeof(char));
        sprintf(clientFIFOWR, "/tmp/clientFIFO%dWR", clientPID);

        printf("Client connected:tryConnect\n");

        sprintf(responseString, "Server is available. Please connect.\n");
        Request response;
        response.requestType = TRY_CONNECT;
        strcpy(response.request, responseString);
        int clientFIFOWrite = open(clientFIFOWR, O_WRONLY);
        if (clientFIFOWrite < 0)
        {
            perror("Error opening client FIFO for writing");
            return;
        }
        int written = write(clientFIFOWrite, &response, sizeof(Request));
        if (written < 0)
        {
            perror("Error writing to client FIFO");
            return;
        }
        close(clientFIFOWrite);
        pid_t parentID = getpid();

        pid_t forkID = fork();
        if (forkID < 0)
        {
            perror("Error forking");
            return;
        }
        else if (forkID == 0)
        {
            char *clientMessage = malloc(256 * sizeof(char));
            sprintf(clientMessage, "Client with PID %s connected using tryConnect\n", request.request);
            writeToLogger(logFile, clientMessage, loggerSemaphore);
            printMessage(clientMessage, strlen(clientMessage));
            handleClient(atoi(request.request), serverFileIOSemaphore, logFile, loggerSemaphore, parentID);
            exit(EXIT_SUCCESS);
        }
        else
        {
            globalNumOfClients++;
            printf("num of clients: %d\n", globalNumOfClients);
        }
    }
    else if (request.requestType == CONNECT) // if connect: add client to list
    {
        if (globalNumOfClients >= globalMaxClients)
        {
            char *clientFIFOWR = malloc(256 * sizeof(char));
            sprintf(clientFIFOWR, "/tmp/clientFIFO%dWR", atoi(request.request));
            printMessage("Server is full. Please wait for an empty spot.\n", 47);
            Request response;
            response.requestType = ERROR;
            strcpy(response.request, "Server is full. Please wait for an empty spot.\n");
            int clientFIFOWrite = open(clientFIFOWR, O_WRONLY);
            if (clientFIFOWrite < 0)
            {
                perror("Error opening client FIFO for writing");
                return;
            }
            int written = write(clientFIFOWrite, &response, sizeof(Request));
            if (written < 0)
            {
                perror("Error writing to client FIFO");
                return;
            }
            close(clientFIFOWrite);
            sem_post(serverSemaphore);
            enqueueClient(atoi(request.request));
            return;
        }
        char *responseString = malloc(256 * sizeof(char));
        int clientPID = atoi(request.request);
        char *clientFIFOWR = malloc(256 * sizeof(char));
        sprintf(clientFIFOWR, "/tmp/clientFIFO%dWR", clientPID);

        printf("Client connected:connect\n");

        sprintf(responseString, "Server is available. Please connect.\n");
        Request response;
        response.requestType = CONNECT;
        strcpy(response.request, responseString);
        int clientFIFOWrite = open(clientFIFOWR, O_WRONLY);
        if (clientFIFOWrite < 0)
        {
            perror("Error opening client FIFO for writing");
            return;
        }

        int written = write(clientFIFOWrite, &response, sizeof(Request));
        if (written < 0)
        {
            perror("Error writing to client FIFO");
            return;
        }
        close(clientFIFOWrite);
        pid_t parentID = getpid();

        pid_t forkID = fork();

        if (forkID < 0)
        {
            perror("Error forking");
            return;
        }
        else if (forkID == 0)
        {
            char *clientMessage = malloc(256 * sizeof(char));
            sprintf(clientMessage, "Client with PID %s connected\n", request.request);
            writeToLogger(logFile, clientMessage, loggerSemaphore);
            printMessage(clientMessage, strlen(clientMessage));
            handleClient(atoi(request.request), serverFileIOSemaphore, logFile, loggerSemaphore, parentID);
            exit(EXIT_SUCCESS);
        }
        else
        {
            globalNumOfClients++;
            printf("num of clients: %d\n", globalNumOfClients);
        }
    }
}

int handleClient(int clientPID, sem_t *serverFileIOSemaphore, int logFile, sem_t *loggerSemaphore, pid_t parentID)
{
    globalClientPID = clientPID;
    char *clientFIFORD = malloc(256 * sizeof(char));
    sprintf(clientFIFORD, "/tmp/clientFIFO%dRD", clientPID);
    char *clientFIFOWR = malloc(256 * sizeof(char));
    sprintf(clientFIFOWR, "/tmp/clientFIFO%dWR", clientPID);
    char *clientMessage = malloc(256 * sizeof(char));
    while (1)
    {
        int clientFIFORead = open(clientFIFORD, O_RDONLY);
        if (clientFIFORead < 0)
        {
            perror("Error opening client FIFO for reading");
            return 0;
        }
        Request request;
        int bytesRead = read(clientFIFORead, &request, sizeof(Request));

        if (bytesRead < 0)
        {
            perror("Error reading from client FIFO");
            return 0;
        }
        if (request.requestType == HELP)
        {
            printMessage("Help\n", 5);
            clientMessage = malloc(256 * sizeof(char));
            sprintf(clientMessage, "Client with PID %d requested help\n", clientPID);
            writeToLogger(logFile, clientMessage, loggerSemaphore);
            printMessage(clientMessage, strlen(clientMessage));
            int res = handleHelp(request, clientFIFORead, clientFIFOWR);
            clientMessage = malloc(256 * sizeof(char));
            if (res == 0)
            {
                sprintf(clientMessage, "Help request failed for client with PID %d\n", clientPID);
            }
            else
            {
                sprintf(clientMessage, "Help request succeeded for client with PID %d\n", clientPID);
            }
            writeToLogger(logFile, clientMessage, loggerSemaphore);
            printMessage(clientMessage, strlen(clientMessage));
        }
        else if (request.requestType == LIST)
        {
            sem_wait(serverFileIOSemaphore);
            clientMessage = malloc(256 * sizeof(char));
            sprintf(clientMessage, "Client with PID %d requested list\n", clientPID);
            printMessage(clientMessage, strlen(clientMessage));
            writeToLogger(logFile, clientMessage, loggerSemaphore);
            int res = handleList(request, clientFIFORead, clientFIFOWR);
            clientMessage = malloc(256 * sizeof(char));
            if (res == 0)
            {
                sprintf(clientMessage, "List request failed for client with PID %d\n", clientPID);
            }
            else
            {
                sprintf(clientMessage, "List request succeeded for client with PID %d\n", clientPID);
            }
            writeToLogger(logFile, clientMessage, loggerSemaphore);
            printMessage(clientMessage, strlen(clientMessage));
            sem_post(serverFileIOSemaphore);
        }
        else if (request.requestType == READFROM)
        {
            sem_wait(serverFileIOSemaphore);
            clientMessage = malloc(256 * sizeof(char));
            sprintf(clientMessage, "Client with PID %d requested read from\n", clientPID);
            writeToLogger(logFile, clientMessage, loggerSemaphore);
            printMessage(clientMessage, strlen(clientMessage));
            int res = handleReadFrom(request, clientFIFORead, clientFIFOWR);
            clientMessage = malloc(256 * sizeof(char));
            if (res == 0)
            {
                sprintf(clientMessage, "Read from request failed for client with PID %d\n", clientPID);
            }
            else
            {
                sprintf(clientMessage, "Read from request succeeded for client with PID %d\n", clientPID);
            }
            writeToLogger(logFile, clientMessage, loggerSemaphore);
            printMessage(clientMessage, strlen(clientMessage));
            sem_post(serverFileIOSemaphore);
        }
        else if (request.requestType == WRITETO)
        {
            sem_wait(serverFileIOSemaphore);
            clientMessage = malloc(256 * sizeof(char));
            sprintf(clientMessage, "Client with PID %d requested write to\n", clientPID);
            writeToLogger(logFile, clientMessage, loggerSemaphore);
            printMessage(clientMessage, strlen(clientMessage));
            int res = handleWriteTo(request, clientFIFORead, clientFIFOWR);
            clientMessage = malloc(256 * sizeof(char));
            if (res == 0)
            {
                sprintf(clientMessage, "Write to request failed for client with PID %d\n", clientPID);
            }
            else
            {
                sprintf(clientMessage, "Write to request succeeded for client with PID %d\n", clientPID);
            }
            writeToLogger(logFile, clientMessage, loggerSemaphore);
            printMessage(clientMessage, strlen(clientMessage));
            sem_post(serverFileIOSemaphore);
        }
        else if (request.requestType == UPLOAD)
        {
            sem_wait(serverFileIOSemaphore);
            clientMessage = malloc(256 * sizeof(char));
            sprintf(clientMessage, "Client with PID %d requested upload\n", clientPID);
            writeToLogger(logFile, clientMessage, loggerSemaphore);
            printMessage(clientMessage, strlen(clientMessage));
            int res = handleUpload(request, clientFIFORead, clientFIFOWR);
            clientMessage = malloc(256 * sizeof(char));
            if (res == 0)
            {
                sprintf(clientMessage, "Upload request failed for client with PID %d\n", clientPID);
            }
            else
            {
                sprintf(clientMessage, "Upload request succeeded for client with PID %d\n", clientPID);
            }
            writeToLogger(logFile, clientMessage, loggerSemaphore);
            printMessage(clientMessage, strlen(clientMessage));
            sem_post(serverFileIOSemaphore);
        }
        else if (request.requestType == DOWNLOAD)
        {
            sem_wait(serverFileIOSemaphore);
            clientMessage = malloc(256 * sizeof(char));
            sprintf(clientMessage, "Client with PID %d requested download\n", clientPID);
            writeToLogger(logFile, clientMessage, loggerSemaphore);
            printMessage(clientMessage, strlen(clientMessage));
            int res = handleDownload(request, clientFIFORead, clientFIFOWR);
            clientMessage = malloc(256 * sizeof(char));
            if (res == 0)
            {
                sprintf(clientMessage, "Download request failed for client with PID %d\n", clientPID);
            }
            else
            {
                sprintf(clientMessage, "Download request succeeded for client with PID %d\n", clientPID);
            }
            writeToLogger(logFile, clientMessage, loggerSemaphore);
            printMessage(clientMessage, strlen(clientMessage));
            sem_post(serverFileIOSemaphore);
        }
        else if (request.requestType == ARCHSERVER)
        {
            sem_wait(serverFileIOSemaphore);
            clientMessage = malloc(256 * sizeof(char));
            sprintf(clientMessage, "Client with PID %d requested archive server\n", clientPID);
            writeToLogger(logFile, clientMessage, loggerSemaphore);
            printMessage(clientMessage, strlen(clientMessage));
            int res = handleArchServer(request, clientFIFORead, clientFIFOWR);
            clientMessage = malloc(256 * sizeof(char));
            if (res == 0)
            {
                sprintf(clientMessage, "Archive server request failed for client with PID %d\n", clientPID);
            }
            else
            {
                sprintf(clientMessage, "Archive server request succeeded for client with PID %d\n", clientPID);
            }
            writeToLogger(logFile, clientMessage, loggerSemaphore);
            printMessage(clientMessage, strlen(clientMessage));
            sem_post(serverFileIOSemaphore);
        }
        else if (request.requestType == KILLSERVER)
        {
            clientMessage = malloc(256 * sizeof(char));
            sprintf(clientMessage, "Client with PID %d requested to kill server\n", clientPID);
            writeToLogger(logFile, clientMessage, loggerSemaphore);
            printMessage(clientMessage, strlen(clientMessage));
            killClient();
            kill(parentID, SIGTERM);
            sem_post(serverFileIOSemaphore);
            sem_post(loggerSemaphore);
            exit(EXIT_SUCCESS);
        }
        else if (request.requestType == QUIT)
        {
            clientMessage = malloc(256 * sizeof(char));
            sprintf(clientMessage, "Client with PID %d disconnected\n", clientPID);
            writeToLogger(logFile, clientMessage, loggerSemaphore);
            printMessage(clientMessage, strlen(clientMessage));
            close(clientFIFORead);
            sem_post(serverFileIOSemaphore);
            sem_post(loggerSemaphore);

            exit(EXIT_SUCCESS);
        }
        else
        {
            printMessage("Invalid request\n", 16);
        }
    }
    return 1;
}

void writeToLogger(int logFile, char *message, sem_t *loggerSemaphore)
{
    pid_t pid = getpid();
    if (pid < 0)
    {
        perror("Error getting PID");
        return;
    }
    else if (pid == 0)
    {
        return;
    }
    sem_wait(loggerSemaphore);
    int written = write(logFile, message, strlen(message));
    if (written < 0)
    {
        perror("Error writing to log file");
        return;
    }
    sem_post(loggerSemaphore);
}

int handleArchServer(Request request, int clientFIFORead, char *clientFIFOWR)
{
    char *fileName = strtok(request.request, " ");
    if (fileName == NULL)
    {
        char *errorMessage = "Filename is missing\n";
        printMessage(errorMessage, strlen(errorMessage));
        Request errorResponse;
        errorResponse.requestType = ERROR;
        strcpy(errorResponse.request, errorMessage);
        int clientFIFOWrite = open(clientFIFOWR, O_WRONLY);
        if (clientFIFOWrite < 0)
        {
            perror("Error opening client FIFO for writing");
            return 0;
        }
        int written = write(clientFIFOWrite, &errorResponse, sizeof(Request));
        if (written < 0)
        {
            perror("Error writing to client FIFO");
            return 0;
        }
        close(clientFIFOWrite);
        close(clientFIFORead);
        return 0;
    }
    pid_t forkID = fork();
    if (forkID < 0)
    {
        perror("Error forking");
        return 0;
    }
    else if (forkID == 0)
    {
        pid_t forkID2 = fork();
        if (forkID2 < 0)
        {
            perror("Error forking");
            return 0;
        }
        else if (forkID2 == 0)
        {
            char *tarCommand = malloc(256 * sizeof(char));
            sprintf(tarCommand, "tar -czf /tmp/%s.tar.gz .", fileName);
            int res = execl("/bin/sh", "sh", "-c", tarCommand, (char *)0);
            if (res < 0)
            {
                perror("Error creating archive");
                return 0;
            }
            free(tarCommand);
            exit(EXIT_SUCCESS);
        }
        else
        {
            int status;
            waitpid(forkID2, &status, 0);
            if (WIFEXITED(status))
            {
                if (WEXITSTATUS(status) == 0)
                {
                    // send the file to the client
                    char filePath[256];
                    sprintf(filePath, "/tmp/%s.tar.gz", fileName);
                    int file = open(filePath, O_RDONLY);
                    if (file < 0)
                    {
                        perror("Error opening archive file");
                        return 0;
                    }
                    struct stat fileStat;
                    if (fstat(file, &fileStat) < 0)
                    {
                        perror("Error getting file stats");
                        return 0;
                    }
                    int fileSize = fileStat.st_size;
                    Request response;
                    response.requestType = ARCHSERVER;
                    sprintf(response.request, "%d", fileSize);
                    int clientFIFOWrite = open(clientFIFOWR, O_WRONLY);
                    if (clientFIFOWrite < 0)
                    {
                        perror("Error opening client FIFO for writing");
                        return 0;
                    }
                    int written = write(clientFIFOWrite, &response, sizeof(Request));
                    if (written < 0)
                    {
                        perror("Error writing to client FIFO");
                        return 0;
                    }
                    int totalBytesRead = 0;
                    while (totalBytesRead < fileSize)
                    {
                        char *packet = malloc(256 * sizeof(char));
                        int bytesRead = read(file, packet, 256);
                        if (bytesRead < 0)
                        {
                            perror("Error reading from file");
                            return 0;
                        }
                        int written = write(clientFIFOWrite, packet, bytesRead);
                        if (written < 0)
                        {
                            perror("Error writing to client FIFO");
                            return 0;
                        }
                        totalBytesRead += bytesRead;
                    }
                    // remove the file from tmp
                    char *rmCommand = malloc(256 * sizeof(char));
                    sprintf(rmCommand, "rm %s", filePath);
                    printf("rmCommand:%s\n", rmCommand);
                    int res = execl("/bin/sh", "sh", "-c", rmCommand, (char *)0);
                    if (res < 0)
                    {
                        perror("Error removing archive file");
                        printf("%s\n", rmCommand);
                        return 0;
                    }

                    free(rmCommand);
                    close(file);
                    close(clientFIFOWrite);
                    close(clientFIFORead);

                    return 1;
                }
                else
                {
                    return 0;
                }
            }
        }
    }
    else
    {
        int status;
        waitpid(forkID, &status, 0);
        if (WIFEXITED(status))
        {
            if (WEXITSTATUS(status) == 0)
            {
                Request response;
                response.requestType = ARCHSERVER;
                strcpy(response.request, "Success\n");
                int clientFIFOWrite = open(clientFIFOWR, O_WRONLY);
                if (clientFIFOWrite < 0)
                {
                    perror("Error opening client FIFO for writing");
                    return 0;
                }
                int written = write(clientFIFOWrite, &response, sizeof(Request));
                if (written < 0)
                {
                    perror("Error writing to client FIFO");
                    return 0;
                }
                close(clientFIFOWrite);
                close(clientFIFORead);
                return 0;
            }
            else
            {
                char *errorMessage = "Error creating archive\n";
                printMessage(errorMessage, strlen(errorMessage));
                Request errorResponse;
                errorResponse.requestType = ERROR;
                strcpy(errorResponse.request, errorMessage);
                int clientFIFOWrite = open(clientFIFOWR, O_WRONLY);
                if (clientFIFOWrite < 0)
                {
                    perror("Error opening client FIFO for writing");
                    return 0;
                }
                int written = write(clientFIFOWrite, &errorResponse, sizeof(Request));
                if (written < 0)
                {
                    perror("Error writing to client FIFO");
                    return 0;
                }
                close(clientFIFOWrite);
                close(clientFIFORead);
                return 0;
            }
        }
    }
    return 1;
}

int handleWriteTo(Request request, int clientFIFORead, char *clientFIFOWR)
{
    char *filename = strtok(request.request, " ");
    char *lineNumber = strtok(NULL, " ");
    char *text = strtok(NULL, " ");
    if (filename == NULL || text == NULL || lineNumber == NULL)
    {
        char *errorMessage = "Filename, line number or text is missing\n";
        printMessage(errorMessage, strlen(errorMessage));
        Request errorResponse;
        errorResponse.requestType = ERROR;
        strcpy(errorResponse.request, errorMessage);
        int clientFIFOWrite = open(clientFIFOWR, O_WRONLY);
        if (clientFIFOWrite < 0)
        {
            perror("Error opening client FIFO for writing");
            return 0;
        }
        int written = write(clientFIFOWrite, &errorResponse, sizeof(Request));
        if (written < 0)
        {
            perror("Error writing to client FIFO");
            return 0;
        }
        close(clientFIFOWrite);
        close(clientFIFORead);
        return 0;
    }
    int lineNumberInt = atoi(lineNumber);
    if (lineNumberInt == -1)
    {
        int file = open(filename, O_WRONLY | O_APPEND | O_CREAT, 0666);
        if (file < 0)
        {
            perror("Error opening file");
            return 0;
        }
        int written = write(file, text, strlen(text));
        if (written < 0)
        {
            perror("Error writing to file");
            return 0;
        }
        close(file);
    }
    else
    {
        int file = open(filename, O_RDWR | O_CREAT, 0666);
        if (file < 0)
        {
            perror("Error opening file");
            return 0;
        }
        int currentLine = 0;
        int currentChar = 0;
        int lineCapacity = 256;
        char *line = malloc(lineCapacity * sizeof(char));
        while (currentLine != lineNumberInt - 1)
        {
            char c;
            int bytesRead = read(file, &c, 1);
            if (bytesRead < 0)
            {
                perror("Error reading from file");
                return 0;
            }
            if (c == '\n')
            {
                currentLine++;
            }
        }
        char c;
        int bytesRead = 0;
        while ((bytesRead = read(file, &c, 1)) > 0)
        {
            if (c == '\n')
            {
                break;
            }
            if (currentChar + bytesRead > lineCapacity)
            {
                lineCapacity *= 2;
                line = realloc(line, lineCapacity * sizeof(char));
            }
            strncat(line, &c, 1);
            currentChar += bytesRead;
        }
        int textLength = strlen(text);
        if (textLength <= currentChar)
        {
            lseek(file, -currentChar, SEEK_CUR);
            int written = write(file, text, textLength);
            if (written < 0)
            {
                perror("Error writing to file");
                return 0;
            }
            if (textLength < currentChar)
            {
                ftruncate(file, lseek(file, 0, SEEK_CUR) - (currentChar - textLength));
            }
        }
        else
        {
            int written = write(file, text, textLength);
            if (written < 0)
            {
                perror("Error writing to file");
                return 0;
            }
        }
        close(file);
    }
    int clientFIFOWrite = open(clientFIFOWR, O_WRONLY);
    if (clientFIFOWrite < 0)
    {
        perror("Error opening client FIFO for writing");
        return 0;
    }
    Request response;
    response.requestType = WRITETO;
    strcpy(response.request, "Success");
    int written = write(clientFIFOWrite, &response, sizeof(Request));
    if (written < 0)
    {
        perror("Error writing to client FIFO");
        return 0;
    }
    close(clientFIFOWrite);
    close(clientFIFORead);
    return 1;
}

int handleDownload(Request request, int clientFIFORead, char *clientFIFOWR)
{
    char *filename = strtok(request.request, " ");
    if (filename == NULL)
    {
        char *errorMessage = "Filename is missing\n";
        printMessage(errorMessage, strlen(errorMessage));
        Request errorResponse;
        errorResponse.requestType = ERROR;
        strcpy(errorResponse.request, errorMessage);
        int clientFIFOWrite = open(clientFIFOWR, O_WRONLY);
        if (clientFIFOWrite < 0)
        {
            perror("Error opening client FIFO for writing");
            return 0;
        }
        int written = write(clientFIFOWrite, &errorResponse, sizeof(Request));
        if (written < 0)
        {
            perror("Error writing to client FIFO");
            return 0;
        }
        close(clientFIFOWrite);
        close(clientFIFORead);
        return 0;
    }
    int file = open(filename, O_RDONLY);
    if (file < 0)
    {
        char *errorMessage = "File does not exist\n";
        printMessage(errorMessage, strlen(errorMessage));
        Request errorResponse;
        errorResponse.requestType = ERROR;
        strcpy(errorResponse.request, errorMessage);
        int clientFIFOWrite = open(clientFIFOWR, O_WRONLY);
        if (clientFIFOWrite < 0)
        {
            perror("Error opening client FIFO for writing");
            return 0;
        }
        int written = write(clientFIFOWrite, &errorResponse, sizeof(Request));
        if (written < 0)
        {
            perror("Error writing to client FIFO");
            return 0;
        }
        close(clientFIFOWrite);
        close(clientFIFORead);
        return 0;
    }
    struct stat fileStat;
    if (fstat(file, &fileStat) < 0)
    {
        perror("Error getting file stats");
        return 0;
    }
    int fileSize = fileStat.st_size;
    Request response;
    response.requestType = DOWNLOAD;
    sprintf(response.request, "%d", fileSize);
    int clientFIFOWrite = open(clientFIFOWR, O_WRONLY);
    if (clientFIFOWrite < 0)
    {
        perror("Error opening client FIFO for writing");
        return 0;
    }
    int written = write(clientFIFOWrite, &response, sizeof(Request));
    if (written < 0)
    {
        perror("Error writing to client FIFO");
        return 0;
    }
    int totalBytesRead = 0;
    while (totalBytesRead < fileSize)
    {
        char *packet = malloc(256 * sizeof(char));
        int bytesRead = read(file, packet, 256);
        if (bytesRead < 0)
        {
            perror("Error reading from file");
            return 0;
        }
        int written = write(clientFIFOWrite, packet, bytesRead);
        if (written < 0)
        {
            perror("Error writing to client FIFO");
            return 0;
        }
        totalBytesRead += bytesRead;
    }

    close(file);
    close(clientFIFOWrite);
    close(clientFIFORead);
    return 1;
}

int handleUpload(Request request, int clientFIFORead, char *clientFIFOWR)
{
    char *filename = strtok(request.request, " ");
    char *fileSize = strtok(NULL, " ");
    if (filename == NULL || fileSize == NULL)
    {
        char *errorMessage = "Filename or file size is missing\n";
        printMessage(errorMessage, strlen(errorMessage));
        Request errorResponse;
        errorResponse.requestType = ERROR;
        strcpy(errorResponse.request, errorMessage);
        int clientFIFOWrite = open(clientFIFOWR, O_WRONLY);
        if (clientFIFOWrite < 0)
        {
            perror("Error opening client FIFO for writing");
            return 0;
        }
        int written = write(clientFIFOWrite, &errorResponse, sizeof(Request));
        if (written < 0)
        {
            perror("Error writing to client FIFO");
            return 0;
        }
        close(clientFIFOWrite);
        close(clientFIFORead);
        return 0;
    }
    int fileSizeInt = atoi(fileSize);
    // check if the file exists
    int file = open(filename, O_RDONLY);
    int sentMessage = 0;
    if (file >= 0)
    {
        char *message = "File already exists. Do you want to overwrite? (y/n)\n";
        printMessage(message, strlen(message));
        Request response;
        response.requestType = UPLOAD;
        strcpy(response.request, "File already exists. Do you want to overwrite? (y/n)");
        int clientFIFOWrite = open(clientFIFOWR, O_WRONLY);
        if (clientFIFOWrite < 0)
        {
            perror("Error opening client FIFO for writing");
            return 0;
        }
        int written = write(clientFIFOWrite, &response, sizeof(Request));
        if (written < 0)
        {
            perror("Error writing to client FIFO");
            return 0;
        }
        Request overwriteRequest;
        int bytesRead = read(clientFIFORead, &overwriteRequest, sizeof(Request));
        if (bytesRead < 0)
        {
            perror("Error reading from client FIFO");
            return 0;
        }
        if (strcmp(overwriteRequest.request, "y") != 0)
        {
            close(clientFIFOWrite);
            close(clientFIFORead);
            return 0;
        }
        sentMessage = 1;
        file = open(filename, O_WRONLY | O_TRUNC, 0666);
        printMessage("Overwriting file\n", 17);
    }
    if (!sentMessage)
    {
        printMessage("Creating file\n", 14);
        Request response;
        response.requestType = UPLOAD;
        strcpy(response.request, "Success\n");
        int clientFIFOWrite = open(clientFIFOWR, O_WRONLY | O_CREAT, 0666);
        if (clientFIFOWrite < 0)
        {
            perror("Error opening client FIFO for writing");
            return 0;
        }
        int written = write(clientFIFOWrite, &response, sizeof(Request));
        if (written < 0)
        {
            perror("Error writing to client FIFO");
            return 0;
        }
        close(clientFIFOWrite);
        file = open(filename, O_WRONLY | O_CREAT, 0666);
        if (file < 0)
        {
            perror("Error opening file");
            return 0;
        }
    }
    int totalBytesRead = 0;
    while (totalBytesRead < fileSizeInt)
    {
        char *packet = malloc(256 * sizeof(char));
        int bytesRead = read(clientFIFORead, packet, 256);
        if (bytesRead < 0)
        {
            perror("Error reading from client FIFO");
            return 0;
        }
        int written = write(file, packet, bytesRead);
        if (written < 0)
        {
            perror("Error writing to file");
            return 0;
        }
        totalBytesRead += bytesRead;
    }

    close(file);
    close(clientFIFORead);
    return 1;
}

int handleReadFrom(Request request, int clientFIFORead, char *clientFIFOWR)
{
    char *filename = strtok(request.request, " ");
    char *lineNumber = strtok(NULL, " ");
    int lineNumberInt = atoi(lineNumber);
    if (filename == NULL)
    {
        printMessage("Filename is missing\n", 20);
        return 0;
    }
    if (lineNumber == NULL)
    {
        perror("Line number is missing");
        Request errorResponse;
        errorResponse.requestType = ERROR;
        strcpy(errorResponse.request, "Line number is missing");
        int clientFIFOWrite = open(clientFIFOWR, O_WRONLY);
        if (clientFIFOWrite < 0)
        {
            perror("Error opening client FIFO for writing");
            return 0;
        }
        int written = write(clientFIFOWrite, &errorResponse, sizeof(Request));
        if (written < 0)
        {
            perror("Error writing to client FIFO");
            return 0;
        }
        close(clientFIFOWrite);
        close(clientFIFORead);
        return 0;
    }
    int file = open(filename, O_RDONLY);
    if (file < 0)
    {
        perror("Error opening file");
        return 0;
    }
    char *line = malloc(256 * sizeof(char));
    int lineCapacity = 256;
    int lineLength = 0;
    if (lineNumberInt == -1)
    {
        char buffer[256];
        int bytesRead = 0;
        while ((bytesRead = read(file, buffer, 256)) > 0)
        {
            if (lineLength + bytesRead > lineCapacity)
            {
                lineCapacity *= 2;
                line = realloc(line, lineCapacity * sizeof(char));
            }
            strncat(line, buffer, bytesRead);
            lineLength += bytesRead;
        }
    }
    else
    {
        int currentLine = 0;
        while (currentLine != lineNumberInt - 1)
        {
            char c;
            int bytesRead = read(file, &c, 1);
            if (bytesRead < 0)
            {
                perror("Error reading from file");
                return 0;
            }
            if (bytesRead == 0)
            {
                break;
            }
            if (c == '\n')
            {
                currentLine++;
            }
        }
        char c;
        int bytesRead = 0;
        if (currentLine != lineNumberInt - 1)
        {
            char *errorMessage = "Line number is out of bounds\n";
            printMessage(errorMessage, strlen(errorMessage));
            Request errorResponse;
            errorResponse.requestType = ERROR;
            strcpy(errorResponse.request, errorMessage);
            int clientFIFOWrite = open(clientFIFOWR, O_WRONLY);
            if (clientFIFOWrite < 0)
            {
                perror("Error opening client FIFO for writing");
                return 0;
            }
            int written = write(clientFIFOWrite, &errorResponse, sizeof(Request));
            if (written < 0)
            {
                perror("Error writing to client FIFO");
                return 0;
            }
            close(clientFIFOWrite);
            close(clientFIFORead);
            return 0;
        }
        while ((bytesRead = read(file, &c, 1)) > 0)
        {
            if (c == '\n')
            {
                break;
            }
            if (lineLength + bytesRead > lineCapacity)
            {
                lineCapacity *= 2;
                line = realloc(line, lineCapacity * sizeof(char));
            }
            strncat(line, &c, 1);
            lineLength += bytesRead;
        }
    }

    int clientFIFOWrite = open(clientFIFOWR, O_WRONLY);
    if (clientFIFOWrite < 0)
    {
        perror("Error opening client FIFO for writing");
        return 0;
    }
    Request response;
    response.requestType = READFROM;
    int packetCount = lineLength / 256 + 1;
    sprintf(response.request, "%d", packetCount);
    int written = write(clientFIFOWrite, &response, sizeof(Request));
    if (written < 0)
    {
        perror("Error writing to client FIFO");
        return 0;
    }
    for (int i = 0; i < packetCount; i++)
    {
        char *packet = malloc(256 * sizeof(char));
        strncpy(packet, line + i * 256, 256);
        strcpy(response.request, packet);
        written = write(clientFIFOWrite, &response, sizeof(Request));
        if (written < 0)
        {
            perror("Error writing to client FIFO");
            close(clientFIFOWrite);
            close(clientFIFORead);
            close(file);
            return 0;
        }
    }
    close(clientFIFOWrite);
    close(clientFIFORead);
    close(file);
    free(line);
    return 1;
}

int handleList(Request request, int clientFIFORead, char *clientFIFOWR)
{
    DIR *dir = opendir(".");
    if (dir == NULL)
    {
        perror("Error opening directory");
        return 0;
    }
    struct dirent *entry;
    int fileNameSize = 256;
    char *fileNames = malloc(fileNameSize * sizeof(char));
    memset(fileNames, 0, fileNameSize);
    int fileNamesLength = 0;
    while ((entry = readdir(dir)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        {
            continue;
        }
        if (fileNamesLength + strlen(entry->d_name) + 1 > fileNameSize)
        {
            fileNameSize *= 2;
            fileNames = realloc(fileNames, fileNameSize * sizeof(char));
        }
        strcat(fileNames, entry->d_name);
        strcat(fileNames, "\n");
        fileNamesLength += strlen(entry->d_name) + 1;
    }
    Request response;
    response.requestType = LIST;
    int packetCount = fileNamesLength / 256 + 1;
    sprintf(response.request, "%d", packetCount);
    int clientFIFOWrite = open(clientFIFOWR, O_WRONLY);
    if (clientFIFOWrite < 0)
    {
        perror("Error opening client FIFO for writing");
        close(clientFIFORead);
        return 0;
    }
    int written = write(clientFIFOWrite, &response, sizeof(Request));
    if (written < 0)
    {
        perror("Error writing to client FIFO");
        close(clientFIFOWrite);
        close(clientFIFORead);
        return 0;
    }
    for (int i = 0; i < packetCount; i++)
    {
        char *packet = malloc(256 * sizeof(char));
        strncpy(packet, fileNames + i * 256, 256);
        strcpy(response.request, packet);
        written = write(clientFIFOWrite, &response, sizeof(Request));
        if (written < 0)
        {
            perror("Error writing to client FIFO");
            close(clientFIFOWrite);
            close(clientFIFORead);
            return 0;
        }
    }
    close(clientFIFOWrite);
    close(clientFIFORead);
    closedir(dir);
    free(fileNames);
    return 1;
}

int handleHelp(Request request, int clientFIFORead, char *clientFIFOWR)
{
    Request response;
    response.requestType = HELP;
    char *helpMessage = malloc(256 * sizeof(char));
    if (strcmp(request.request, "NOARGUMENT") == 0)
    {
        sprintf(helpMessage, "\nAvailable commands are:\nhelp, list, readF, writeT, upload, download, archServer, killServer, quit\n\n");
    }
    else if (strcmp(request.request, "help") == 0)
    {
        sprintf(helpMessage, "help: Displays help message\n");
    }
    else if (strcmp(request.request, "list") == 0)
    {
        sprintf(helpMessage, "list: Lists all files in the server\n");
    }
    else if (strcmp(request.request, "readF") == 0)
    {
        sprintf(helpMessage, "readF <filename> <n>: Reads n th line of the file\n");
    }
    else if (strcmp(request.request, "writeT") == 0)
    {
        sprintf(helpMessage, "writeT <filename> <n> <text>: Writes text to n th line of the file\n");
    }
    else if (strcmp(request.request, "upload") == 0)
    {
        sprintf(helpMessage, "upload <filename>: Uploads file to server\n");
    }
    else if (strcmp(request.request, "download") == 0)
    {
        sprintf(helpMessage, "download <filename>: Downloads file from server\n");
    }
    else if (strcmp(request.request, "archServer") == 0)
    {
        sprintf(helpMessage, "archServer: Archives server directory\n");
    }
    else if (strcmp(request.request, "killServer") == 0)
    {
        sprintf(helpMessage, "killServer: Kills server\n");
    }
    else if (strcmp(request.request, "quit") == 0)
    {
        sprintf(helpMessage, "quit: Disconnects from server\n");
    }
    else
    {
        sprintf(helpMessage, "Invalid command\n");
    }
    strcpy(response.request, helpMessage);
    int clientFIFOWrite = open(clientFIFOWR, O_WRONLY);
    if (clientFIFOWrite < 0)
    {
        perror("Error opening client FIFO for writing");
        return 0;
    }
    int written = write(clientFIFOWrite, &response, sizeof(Request));
    if (written < 0)
    {
        perror("Error writing to client FIFO");
        return 0;
    }
    close(clientFIFOWrite);
    close(clientFIFORead);
    return 1;
}

sem_t *createSemaphore(int serverID, char *semaphoreName)
{
    char *semaphoreNameWithID = malloc(256 * sizeof(char));
    sprintf(semaphoreNameWithID, "/%s%d", semaphoreName, serverID);
    sem_t *semaphore = sem_open(semaphoreNameWithID, O_CREAT, 0666, 1);
    if (semaphore == SEM_FAILED)
    {
        perror("Error creating semaphore");
        return NULL;
    }
    return semaphore;
}

int printMessage(char *message, int length)
{
    int bytesWritten = 0;
    while (bytesWritten < length)
    {
        int bytes = write(STDOUT_FILENO, message + bytesWritten, length - bytesWritten);
        if (bytes < 0)
        {
            return -1;
        }
        bytesWritten += bytes;
    }
    return bytesWritten;
}

void printUsage()
{
    printMessage("Usage: noHosServer <dirName> <max # of Clients>\n", 48);
}

int createLoggerFile(char *dirName)
{
    char *logFileName = malloc(256 * sizeof(char));
    sprintf(logFileName, "%s/log.txt", dirName);
    int logFile = open(logFileName, O_CREAT | O_WRONLY | O_APPEND, 0666);
    if (logFile < 0)
    {
        perror("Error creating log file");
        return -1;
    }
    return logFile;
}

char *createWorkingDirectory(char *dirName)
{
    char *cwd = malloc(256 * sizeof(char));
    getcwd(cwd, 256);
    char *newDir = malloc(256 * sizeof(char));
    sprintf(newDir, "%s/%s", cwd, dirName);
    if (mkdir(newDir, 0777) < 0)
    {
        if (errno != EEXIST)
        {
            perror("Error creating working directory");
            return NULL;
        }
        printMessage("Directory already exists. Continuing...\n", 40);
    }
    return newDir;
}

void unlinkServerFIFO()
{
    char *serverFIFO = malloc(256 * sizeof(char));
    sprintf(serverFIFO, "/tmp/serverFIFO%d", getpid());
    unlink(serverFIFO);
}

void killClient()
{
    if (globalClientPID != -1)
    {
        printf("Killing client %d\n", globalClientPID);
        kill(globalClientPID, SIGTERM);
    }
}

int handleExit()
{
    unlinkServerFIFO();
    char *buffer = malloc(256 * sizeof(char));
    sprintf(buffer, "/tmp/serverFIFO%d", getpid());
    sem_unlink(buffer);
    sprintf(buffer, "/serverSemaphore%d", getpid());
    sem_unlink(buffer);
    sprintf(buffer, "/serverFileIOSemaphore%d", getpid());
    sem_unlink(buffer);
    sprintf(buffer, "/loggerSemaphore%d", getpid());
    sem_unlink(buffer);
    free(buffer);
    return 0;
}

void sigTermHandler(int signo)
{
    if (signo == SIGTERM)
    {
        printf("for pid: %d received SIGTERM\n", getpid());
        if (globalClientPID == -1)
        {
            // Parent process
            printf("Parent received SIGTERM. Killing all children.\n");
            // Kill all children
            kill(0, SIGTERM);
            while (wait(NULL) > 0)
                ;
            handleExit();
            exit(EXIT_SUCCESS);
        }
        else
        {
            // Child process
            killClient();
            printf("Child %d received SIGTERM. Exiting.\n", globalClientPID);
            handleExit();
            exit(EXIT_SUCCESS);
        }
    }
}

void sigIntHandler(int signo)
{
    if (signo == SIGINT)
    {
        printf("for pid: %d received SIGINT\n", getpid());
        if (globalClientPID == -1)
        {
            // Parent process
            printf("Parent received SIGINT. Killing all children.\n");
            // Kill all children
            kill(0, SIGTERM);
            while (wait(NULL) > 0)
                ;
            handleExit();
            exit(EXIT_SUCCESS);
        }
        else
        {
            // Child process
            killClient();
            printf("Child %d received SIGINT. Exiting.\n", globalClientPID);
            handleExit();
            exit(EXIT_SUCCESS);
        }
    }
}

void initClientQueue()
{
    clientQueue = malloc(sizeof(ClientQueue));
    clientQueue->head = NULL;
    clientQueue->tail = NULL;
    clientQueue->size = 0;
}

void initClientQueueElement(ClientQueueElement *element, int clientPID)
{
    element->clientPID = clientPID;
    element->next = NULL;
}

void enqueueClient(int clientPID)
{
    ClientQueueElement *element = malloc(sizeof(ClientQueueElement));
    initClientQueueElement(element, clientPID);
    if (clientQueue->size == 0)
    {
        clientQueue->head = element;
        clientQueue->tail = element;
    }
    else
    {
        clientQueue->tail->next = element;
        clientQueue->tail = element;
    }
    clientQueue->size++;
}

int dequeueClient()
{
    if (clientQueue->size == 0)
    {
        return -1;
    }
    ClientQueueElement *element = clientQueue->head;
    int clientPID = element->clientPID;
    clientQueue->head = element->next;
    free(element);
    clientQueue->size--;
    return clientPID;
}

void printQueue()
{
    ClientQueueElement *current = clientQueue->head;
    while (current != NULL)
    {
        printf("%d\n", current->clientPID);
        current = current->next;
    }
}

void sigChldHandler(int signo)
{
    printf("Decreasing number of clients\n");
    globalNumOfClients--;
    printf("num of clients: %d\n", globalNumOfClients);
    int logFile = open("log.txt", O_RDWR | O_CREAT | O_APPEND, 0666);
    char *buffer = malloc(256 * sizeof(char)); // logger semaphore
    sprintf(buffer, "/loggerSemaphore%d", getpid());
    sem_t *loggerSemaphore = sem_open(buffer, O_CREAT, 0666, 1);

    sprintf(buffer, "/serverFileIOSemaphore%d", getpid());
    sem_t *serverFileIOSemaphore = sem_open(buffer, O_CREAT, 0666, 1);

    if (clientQueue->size > 0)
    {
        printf("Getting client from queue\n");
        printQueue();
        int clientPID = dequeueClient(clientQueue);
        char *clientFIFOWR = malloc(256 * sizeof(char));
        sprintf(clientFIFOWR, "/tmp/clientFIFO%dWR", clientPID);
        Request response;
        response.requestType = CONNECT;
        strcpy(response.request, "Success\n");
        int clientFIFOWrite = open(clientFIFOWR, O_WRONLY);
        if (clientFIFOWrite < 0)
        {
            printf("%s\n", clientFIFOWR);
            perror("Error opening client FIFO for writing");
            return;
        }
        int written = write(clientFIFOWrite, &response, sizeof(Request));
        if (written < 0)
        {
            perror("Error writing to client FIFO");
            return;
        }
        close(clientFIFOWrite);
        pid_t parentID = getpid();
        pid_t forkID = fork();
        if (forkID < 0)
        {
            perror("Error forking");
            return;
        }
        else if (forkID == 0)
        {
            char *clientMessage = malloc(256 * sizeof(char));
            sprintf(clientMessage, "Client with PID %d connected\n", clientPID);
            writeToLogger(logFile, clientMessage, loggerSemaphore);
            printMessage(clientMessage, strlen(clientMessage));
            handleClient(clientPID, serverFileIOSemaphore, logFile, loggerSemaphore, parentID);
            exit(EXIT_SUCCESS);
        }
        else
        {
            printf("Incrementing number of clients\n");
            globalNumOfClients++;
            printf("num of clients: %d\n", globalNumOfClients);
        }
    }
}