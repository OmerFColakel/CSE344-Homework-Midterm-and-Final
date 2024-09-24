#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#include <semaphore.h>
#include <signal.h>

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

int checkArguments(int argc, char *argv[]);                                                                           // check arguments
sem_t *initializeSemaphore(int serverID);                                                                             // initialize semaphore
int printMessage(char *message, int length);                                                                          // print message
int createClientFIFOs();                                                                                              // create client FIFOs
int makeConnectionRequest(char *command, int serverID, sem_t *sem);                                                   // make connection request
void cleanFIFOs();                                                                                                    // clean FIFOs
char **tokenizeBuffer(char *buffer);                                                                                  // tokenize buffer
void handleHelp(char *clientFIFOReadName, char *clientFIFOWriteName, char *command);                                  // handle help command
void handleList(char *clientFIFOReadName, char *clientFIFOWriteName);                                                 // handle list command
void handleReadF(char *clientFIFOReadName, char *clientFIFOWriteName, char *filename, char *lineNumber);              // handle readF command
void handleWriteT(char *clientFIFOReadName, char *clientFIFOWriteName, char *filename, char *text, char *lineNumber); // handle writeT command
void handleKillServer(char *clientFIFOReadName, char *clientFIFOWriteName);                                           // handle killServer command
void killSignalHandler(int signal);                                                                                   // kill signal handler
void handleUpload(char *clientFIFOReadName, char *clientFIFOWriteName, char *filename);                               // handle upload command
void handleDownload(char *clientFIFOReadName, char *clientFIFOWriteName, char *filename);                             // handle download command
void handleArchServer(char *clientFIFOReadName, char *clientFIFOWriteName, char *filename);                           // handle archServer command
void sigTermHandler(int signo);                                                                                       // sigterm signal handler
void sigIntHandler(int signo);                                                                                        // sigint signal handler

int main(int argc, char *argv[])
{
    if (checkArguments(argc, argv) == -1)
    {
        return -1;
    }

    char *command = argv[1];

    int serverID = atoi(argv[2]);
    sem_t *sem = initializeSemaphore(serverID);
    if (sem == NULL)
    {
        printMessage("Failed to initialize semaphore\n", 30);
        return -1;
    }

    struct sigaction act;
    // with ctrl+c signal, cleanFIFOs function will be called
    act.sa_handler = cleanFIFOs;
    sigaction(SIGINT, &act, NULL);

    struct sigaction killAct;
    killAct.sa_handler = killSignalHandler;
    sigaction(SIGKILL, &killAct, NULL);

    struct sigaction sigTermAct;
    sigTermAct.sa_handler = sigTermHandler;
    sigaction(SIGTERM, &sigTermAct, NULL);

    struct sigaction sigIntAct;
    sigIntAct.sa_handler = sigIntHandler;
    sigaction(SIGINT, &sigIntAct, NULL);

    if (createClientFIFOs() == -1)
    {
        printMessage("Failed to create client FIFOs\n", 30);
        return -1;
    }
    if (makeConnectionRequest(command, serverID, sem) == -1)
    {
        printMessage("Connection request failed\n", 24);
        exit(EXIT_FAILURE);
    }

    pid_t clientPID = getpid();
    char clientFIFOReadName[256];
    char clientFIFOWriteName[256];
    sprintf(clientFIFOReadName, "/tmp/clientFIFO%dRD", clientPID);  // server will read from this FIFO
    sprintf(clientFIFOWriteName, "/tmp/clientFIFO%dWR", clientPID); // server will write to this FIFO
    while (1)
    {
        printMessage("Enter command: ", 15);
        char *buffer = (char *)malloc(256);
        int bytesRead = read(STDIN_FILENO, buffer, 256);
        if (bytesRead == -1)
        {
            perror("read");
            return -1;
        }
        buffer[bytesRead - 1] = '\0';
        char **args = tokenizeBuffer(buffer);
        if (args == NULL)
        {
            printMessage("Failed to tokenize buffer\n", 26);
            continue;
        }

        if (strcmp(args[0], "help") == 0)
        {
            handleHelp(clientFIFOReadName, clientFIFOWriteName, args[1]);
        }
        else if (strcmp(args[0], "list") == 0)
        {
            handleList(clientFIFOReadName, clientFIFOWriteName);
        }
        else if (strcmp(args[0], "readF") == 0)
        {
            printf("readF\n");
            handleReadF(clientFIFOReadName, clientFIFOWriteName, args[1], args[2]);
        }
        else if (strcmp(args[0], "writeT") == 0)
        {
            handleWriteT(clientFIFOReadName, clientFIFOWriteName, args[1], args[2], args[3]);
        }
        else if (strcmp(args[0], "upload") == 0)
        {
            handleUpload(clientFIFOReadName, clientFIFOWriteName, args[1]);
        }
        else if (strcmp(args[0], "download") == 0)
        {
            handleDownload(clientFIFOReadName, clientFIFOWriteName, args[1]);
        }
        else if (strcmp(args[0], "archServer") == 0)
        {
            handleArchServer(clientFIFOReadName, clientFIFOWriteName, args[1]);
        }
        else if (strcmp(args[0], "killServer") == 0)
        {
            handleKillServer(clientFIFOReadName, clientFIFOWriteName);
        }
        else if (strcmp(args[0], "quit") == 0)
        {
            Request request;
            request.requestType = QUIT;
            int clientFIFORead = open(clientFIFOReadName, O_WRONLY);
            if (clientFIFORead == -1)
            {
                perror("open");
                return -1;
            }
            int written = write(clientFIFORead, &request, sizeof(Request));
            if (written == -1)
            {
                perror("write");
                return -1;
            }
            close(clientFIFORead);
            cleanFIFOs();
            exit(EXIT_SUCCESS);
        }
        else
        {
            printMessage("Invalid command\n", 16);
        }
    }
    cleanFIFOs();

    return 0;
}

void handleArchServer(char *clientFIFOReadName, char *clientFIFOWriteName, char *filename)
{
    if (filename == NULL)
    {
        printMessage("Filename is missing\n", 20);
        return;
    }
    Request request;
    request.requestType = ARCHSERVER;
    sprintf(request.request, "%s", filename);
    int clientFIFORead = open(clientFIFOReadName, O_WRONLY);
    if (clientFIFORead == -1)
    {
        perror("open");
        return;
    }
    int written = write(clientFIFORead, &request, sizeof(Request));
    if (written == -1)
    {
        perror("write");
        return;
    }
    int clientFIFOWrite = open(clientFIFOWriteName, O_RDONLY);
    if (clientFIFOWrite == -1)
    {
        perror("open");
        return;
    }
    Request response;
    int bytesRead = read(clientFIFOWrite, &response, sizeof(Request));
    if (bytesRead == -1)
    {
        perror("read");
        return;
    }
    if (response.requestType == ERROR)
    {
        printMessage(response.request, strlen(response.request));
        return;
    }
    int fileSize = atoi(response.request);
    char *fileBuffer = (char *)malloc(fileSize);
    int totalReadBytes = 0;
    while (totalReadBytes < fileSize)
    {
        bytesRead = read(clientFIFOWrite, fileBuffer + totalReadBytes, fileSize - totalReadBytes);
        if (bytesRead == -1)
        {
            perror("read");
            return;
        }
        totalReadBytes += bytesRead;
    }
    char *archivedFilename = (char *)malloc(256);
    sprintf(archivedFilename, "%s.tar.gz", filename);
    int file = open(archivedFilename, O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (file == -1)
    {
        if (errno == EEXIST)
        {
            printMessage("File already exists. Do you want to overwrite? (y/n)\n", 53);
            char *buffer = (char *)malloc(256);
            int bytesRead = read(STDIN_FILENO, buffer, 256);
            if (bytesRead == -1)
            {
                perror("read");
                return;
            }
            if (strcmp(buffer, "n\n") == 0)
            {
                printMessage("File will not be overwritten\n", 29);
                return;
            }
        }
        else
        {
            perror("open");
            return;
        }
        file = open(archivedFilename, O_WRONLY | O_TRUNC);
    }
    int totalWrittenBytes = 0;
    while (totalWrittenBytes < fileSize)
    {
        written = write(file, fileBuffer + totalWrittenBytes, fileSize - totalWrittenBytes);
        if (written == -1)
        {
            perror("write");
            return;
        }
        totalWrittenBytes += written;
    }
    printMessage("File archived successfully\n", 30);
    close(clientFIFOWrite);
    close(clientFIFORead);
    return;
}

void handleDownload(char *clientFIFOReadName, char *clientFIFOWriteName, char *filename)
{
    if (filename == NULL)
    {
        printMessage("Filename is missing\n", 20);
        return;
    }
    int file = open(filename, O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (file == -1)
    {
        if (errno == EEXIST)
        {
            printMessage("File already exists. Do you want to overwrite? (y/n)\n", 53);
            char *buffer = (char *)malloc(256);
            int bytesRead = read(STDIN_FILENO, buffer, 256);
            if (bytesRead == -1)
            {
                perror("read");
                return;
            }
            if (strcmp(buffer, "n\n") == 0)
            {
                printMessage("File will not be overwritten\n", 29);
                return;
            }
        }
        else
        {
            perror("open");
            return;
        }
        file = open(filename, O_WRONLY | O_TRUNC);
    }
    Request request;
    request.requestType = DOWNLOAD;
    sprintf(request.request, "%s", filename);
    int clientFIFORead = open(clientFIFOReadName, O_WRONLY);
    if (clientFIFORead == -1)
    {
        perror("open");
        return;
    }
    int written = write(clientFIFORead, &request, sizeof(Request));
    if (written == -1)
    {
        perror("write");
        return;
    }
    int clientFIFOWrite = open(clientFIFOWriteName, O_RDONLY);
    if (clientFIFOWrite == -1)
    {
        perror("open");
        return;
    }
    Request response;
    int bytesRead = read(clientFIFOWrite, &response, sizeof(Request));
    if (bytesRead == -1)
    {
        perror("read");
        return;
    }
    if (response.requestType == ERROR)
    {
        printMessage(response.request, strlen(response.request));
        return;
    }
    int fileSize = atoi(response.request);
    char *fileBuffer = (char *)malloc(fileSize);
    int totalReadBytes = 0;
    while (totalReadBytes < fileSize)
    {
        bytesRead = read(clientFIFOWrite, fileBuffer + totalReadBytes, fileSize - totalReadBytes);
        if (bytesRead == -1)
        {
            perror("read");
            return;
        }
        totalReadBytes += bytesRead;
    }
    int totalWrittenBytes = 0;
    while (totalWrittenBytes < fileSize)
    {
        written = write(file, fileBuffer + totalWrittenBytes, fileSize - totalWrittenBytes);
        if (written == -1)
        {
            perror("write");
            return;
        }
        totalWrittenBytes += written;
    }
    printMessage("File downloaded successfully\n", 30);
    close(clientFIFOWrite);
    close(clientFIFORead);
    return;
}

void handleUpload(char *clientFIFOReadName, char *clientFIFOWriteName, char *filename)
{

    if (filename == NULL)
    {
        printMessage("Filename is missing\n", 20);
        return;
    }
    int file = open(filename, O_RDONLY);
    if (file == -1)
    {
        perror("open");
        return;
    }
    struct stat fileStat;
    if (fstat(file, &fileStat) == -1)
    {
        perror("fstat");
        return;
    }
    int fileSize = fileStat.st_size;
    Request request;
    request.requestType = UPLOAD;
    sprintf(request.request, "%s %d", filename, fileSize);
    int clientFIFORead = open(clientFIFOReadName, O_WRONLY);
    if (clientFIFORead == -1)
    {
        perror("open");
        return;
    }
    int written = write(clientFIFORead, &request, sizeof(Request));
    if (written == -1)
    {
        perror("write");
        return;
    }
    int clientFIFOWrite = open(clientFIFOWriteName, O_RDONLY);
    if (clientFIFOWrite == -1)
    {
        perror("open");
        return;
    }
    Request response;
    int bytesRead = read(clientFIFOWrite, &response, sizeof(Request));
    if (bytesRead == -1)
    {
        perror("read");
        return;
    }
    if (response.requestType == ERROR)
    {
        printMessage(response.request, strlen(response.request));
        return;
    }
    else if (response.requestType == UPLOAD)
    {
        char *message = "File already exists. Do you want to overwrite? (y/n)";
        if (strcmp(response.request, message) == 0)
        {
            printMessage(message, strlen(message));
            char *buffer = (char *)malloc(256);
            int bytesRead = read(STDIN_FILENO, buffer, 256);
            if (bytesRead == -1)
            {
                perror("read");
                return;
            }
            Request overwriteRequest;
            overwriteRequest.requestType = UPLOAD;
            if (strcmp(buffer, "y\n") == 0)
            {
                sprintf(overwriteRequest.request, "%s", "y");
            }
            else
            {
                sprintf(overwriteRequest.request, "%s", "n");
            }
            written = write(clientFIFORead, &overwriteRequest, sizeof(Request));
            if (written == -1)
            {
                perror("write");
                return;
            }
            if (strcmp(buffer, "y\n") != 0)
            {
                printMessage("File will not be overwritten\n", 30);
                return;
            }
        }
        else
            printMessage(response.request, strlen(response.request));
    }
    char *fileBuffer = (char *)malloc(fileSize);
    bytesRead = read(file, fileBuffer, fileSize);
    if (bytesRead == -1)
    {
        perror("read");
        return;
    }
    int writtenBytes = 0;
    while (writtenBytes < fileSize)
    {
        written = write(clientFIFORead, fileBuffer + writtenBytes, fileSize - writtenBytes);
        if (written == -1)
        {
            perror("write");
            return;
        }
        writtenBytes += written;
    }

    printMessage("File uploaded successfully\n", 30);
    close(clientFIFOWrite);
    close(clientFIFORead);
    return;
}

void killSignalHandler(int signal)
{
    printf("Received kill signal\n");
    char *clientFIFOReadName = malloc(256);
    pid_t clientPID = getpid();
    sprintf(clientFIFOReadName, "/tmp/clientFIFO%dRD", clientPID);
    Request request;
    request.requestType = QUIT;
    int clientFIFORead = open(clientFIFOReadName, O_WRONLY);
    if (clientFIFORead == -1)
    {
        perror("open");
        return;
    }
    int written = write(clientFIFORead, &request, sizeof(Request));
    if (written == -1)
    {
        perror("write");
        return;
    }
    close(clientFIFORead);
    cleanFIFOs();
    exit(0);
}

void handleKillServer(char *clientFIFOReadName, char *clientFIFOWriteName)
{
    Request request;
    request.requestType = KILLSERVER;
    int clientFIFORead = open(clientFIFOReadName, O_WRONLY);
    if (clientFIFORead == -1)
    {
        perror("open");
        return;
    }
    int written = write(clientFIFORead, &request, sizeof(Request));
    if (written == -1)
    {
        perror("write");
        return;
    }
    int clientFIFOWrite = open(clientFIFOWriteName, O_RDONLY);
    if (clientFIFOWrite == -1)
    {
        perror("open");
        return;
    }
    Request response;
    int bytesRead = read(clientFIFOWrite, &response, sizeof(Request));
    if (bytesRead == -1)
    {
        perror("read");
        return;
    }
    printMessage(response.request, strlen(response.request));
    close(clientFIFOWrite);
    return;
}

void handleWriteT(char *clientFIFOReadName, char *clientFIFOWriteName, char *filename, char *lineNumber, char *text)
{
    if (filename == NULL)
    {
        printMessage("Filename or text is missing\n", 30);
        return;
    }
    Request request;
    request.requestType = WRITETO;
    if (text == NULL)
    {
        sprintf(request.request, "%s %s %s", filename, "-1", lineNumber);
    }
    else
    {
        sprintf(request.request, "%s %s %s", filename, lineNumber, text);
    }
    printf("Request: %s\n", request.request);
    int clientFIFORead = open(clientFIFOReadName, O_WRONLY);
    if (clientFIFORead == -1)
    {
        perror("open");
        return;
    }
    printf("Opened clientFIFORead\n");
    int written = write(clientFIFORead, &request, sizeof(Request));
    if (written == -1)
    {
        perror("write");
        return;
    }
    printf("Written: %d\n", written);
    int clientFIFOWrite = open(clientFIFOWriteName, O_RDONLY);
    if (clientFIFOWrite == -1)
    {
        perror("open");
        return;
    }
    printf("Opened clientFIFOWrite\n");
    Request response;
    int bytesRead = read(clientFIFOWrite, &response, sizeof(Request));
    if (bytesRead == -1)
    {
        perror("read");
        return;
    }
    printf("Response: %s\n", response.request);
    printMessage(response.request, strlen(response.request));
    close(clientFIFOWrite);
    return;
}

void handleReadF(char *clientFIFOReadName, char *clientFIFOWriteName, char *filename, char *lineNumber)
{
    Request request;
    request.requestType = READFROM;
    if (filename == NULL)
    {
        printMessage("Filename is missing\n", 20);
        return;
    }
    if (lineNumber == NULL)
    {
        sprintf(request.request, "%s %s", filename, "-1");
    }
    else
    {
        sprintf(request.request, "%s %s", filename, lineNumber);
    }
    int clientFIFORead = open(clientFIFOReadName, O_WRONLY);
    if (clientFIFORead == -1)
    {
        perror("open");
        return;
    }
    int written = write(clientFIFORead, &request, sizeof(Request));
    if (written == -1)
    {
        perror("write");
        return;
    }
    int clientFIFOWrite = open(clientFIFOWriteName, O_RDONLY);
    if (clientFIFOWrite == -1)
    {
        perror("open");
        return;
    }
    Request response;
    int bytesRead = read(clientFIFOWrite, &response, sizeof(Request));
    if (bytesRead == -1)
    {
        perror("read");
        return;
    }
    if (response.requestType == ERROR)
    {
        printMessage(response.request, strlen(response.request));
        return;
    }
    int packetCount = atoi(response.request);
    for (int i = 0; i < packetCount; i++)
    {
        bytesRead = read(clientFIFOWrite, &response, sizeof(Request));
        if (bytesRead == -1)
        {
            perror("read");
            return;
        }
        printMessage(response.request, strlen(response.request));
    }
    printMessage("\n", 1);
    close(clientFIFOWrite);
    return;
}

void handleList(char *clientFIFOReadName, char *clientFIFOWriteName)
{
    Request request;
    request.requestType = LIST;
    int clientFIFORead = open(clientFIFOReadName, O_WRONLY);
    if (clientFIFORead == -1)
    {
        perror("open");
        return;
    }
    int written = write(clientFIFORead, &request, sizeof(Request));
    if (written == -1)
    {
        perror("write");
        return;
    }
    int clientFIFOWrite = open(clientFIFOWriteName, O_RDONLY);
    if (clientFIFOWrite == -1)
    {
        perror("open");
        return;
    }
    Request incomingRequest;
    int bytesRead = read(clientFIFOWrite, &incomingRequest, sizeof(Request));
    if (bytesRead == -1)
    {
        perror("read");
        return;
    }
    int packetCount = atoi(incomingRequest.request);
    for (int i = 0; i < packetCount; i++)
    {
        bytesRead = read(clientFIFOWrite, &incomingRequest, sizeof(Request));
        if (bytesRead == -1)
        {
            perror("read");
            return;
        }
        printMessage(incomingRequest.request, strlen(incomingRequest.request));
    }
    close(clientFIFOWrite);
    close(clientFIFORead);
    return;
}

void handleHelp(char *clientFIFOReadName, char *clientFIFOWriteName, char *command)
{
    Request request;
    request.requestType = HELP;
    if (command != NULL)
        sprintf(request.request, "%s", command);
    else
        sprintf(request.request, "%s", "NOARGUMENT");
    int clientFIFORead = open(clientFIFOReadName, O_WRONLY);
    if (clientFIFORead == -1)
    {
        perror("open");
        return;
    }
    int written = write(clientFIFORead, &request, sizeof(Request));
    if (written == -1)
    {
        perror("write");
        return;
    }
    int clientFIFOWrite = open(clientFIFOWriteName, O_RDONLY);
    if (clientFIFOWrite == -1)
    {
        perror("open");
        return;
    }
    Request incomingRequest;
    int bytesRead = read(clientFIFOWrite, &incomingRequest, sizeof(Request));
    if (bytesRead == -1)
    {
        perror("read");
        return;
    }
    printMessage(incomingRequest.request, strlen(incomingRequest.request));
    close(clientFIFOWrite);
    return;
}

char **tokenizeBuffer(char *buffer)
{
    char **tokens = (char **)malloc(256 * sizeof(char *));
    char *token = strtok(buffer, " ");
    int i = 0;
    if (token == NULL)
    {
        return NULL;
    }
    while (token != NULL)
    {
        tokens[i] = token;
        i++;
        token = strtok(NULL, " ");
    }
    tokens[i] = NULL;
    return tokens;
}

void cleanFIFOs()
{
    pid_t clientPID = getpid();
    char clientFIFOReadName[256];
    char clientFIFOWriteName[256];
    sprintf(clientFIFOReadName, "/tmp/clientFIFO%dRD", clientPID);
    sprintf(clientFIFOWriteName, "/tmp/clientFIFO%dWR", clientPID);
    if (unlink(clientFIFOReadName) == -1)
    {
        perror("unlink");
    }
    if (unlink(clientFIFOWriteName) == -1)
    {
        perror("unlink");
    }
    exit(EXIT_FAILURE);
}

int makeConnectionRequest(char *command, int serverID, sem_t *sem)
{
    char serverFIFOName[256];
    sprintf(serverFIFOName, "/tmp/serverFIFO%d", serverID);
    int serverFIFO = open(serverFIFOName, O_WRONLY);
    if (serverFIFO == -1)
    {
        perror("open");
        cleanFIFOs();
        return -1;
    }
    sem_wait(sem);
    Request request;
    if (strcmp(command, "connect") == 0)
    {
        request.requestType = CONNECT;
    }
    else if (strcmp(command, "tryConnect") == 0)
    {
        request.requestType = TRY_CONNECT;
    }
    else
    {
        printMessage("Invalid command\n", 16);
        cleanFIFOs();
        return -1;
    }
    sprintf(request.request, "%d", getpid());
    int written = write(serverFIFO, &request, sizeof(Request));
    if (written == -1)
    {
        perror("write");
        cleanFIFOs();
        return -1;
    }
    close(serverFIFO);
    char clientFIFOWriteName[256];
    sprintf(clientFIFOWriteName, "/tmp/clientFIFO%dWR", getpid());
    Request response;
    int clientFIFOWrite = open(clientFIFOWriteName, O_RDONLY);
    if (clientFIFOWrite == -1)
    {
        perror("open");
        cleanFIFOs();
        return -1;
    }
    int bytesRead = read(clientFIFOWrite, &response, sizeof(Request));
    if (bytesRead == -1)
    {
        perror("read");
        cleanFIFOs();
        return -1;
    }
    printMessage(response.request, strlen(response.request));
    close(clientFIFOWrite);
    if (response.requestType == ERROR && strcmp(response.request, "Server is full. Please try again later.\n") == 0)
    {
        cleanFIFOs();
        sem_post(sem);
        return -1;
    }
    sem_post(sem);

    if (response.requestType == ERROR && strcmp(response.request, "Server is full. Please wait for an empty spot.\n") == 0)
    {
        int clientFIFOWrite = open(clientFIFOWriteName, O_RDONLY);
        if (clientFIFOWrite == -1)
        {
            perror("open");
            cleanFIFOs();
            return -1;
        }
        Request response;
        int bytesRead = read(clientFIFOWrite, &response, sizeof(Request));
        if (bytesRead == -1)
        {
            perror("read");
            cleanFIFOs();
            return -1;
        }
        printMessage(response.request, strlen(response.request));
        return 0;
    }

    return 0;
}

int createClientFIFOs()
{
    pid_t clientPID = getpid();
    char clientFIFOReadName[256];
    char clientFIFOWriteName[256];
    sprintf(clientFIFOReadName, "/tmp/clientFIFO%dRD", clientPID);
    sprintf(clientFIFOWriteName, "/tmp/clientFIFO%dWR", clientPID);
    if (mkfifo(clientFIFOReadName, 0666) == -1)
    {
        perror("mkfifo");
        return -1;
    }
    if (mkfifo(clientFIFOWriteName, 0666) == -1)
    {
        perror("mkfifo");
        return -1;
    }
    printMessage(clientFIFOReadName, strlen(clientFIFOReadName));
    printMessage("\n", 1);
    printMessage(clientFIFOWriteName, strlen(clientFIFOWriteName));
    printMessage("\n", 1);

    return 0;
}

int printMessage(char *message, int length)
{
    int bytesWritten = 0;
    while (bytesWritten < length)
    {
        int written = write(STDOUT_FILENO, message + bytesWritten, length - bytesWritten);
        if (written == -1)
        {
            perror("write");
            return -1;
        }
        bytesWritten += written;
    }
    return bytesWritten;
}

int checkArguments(int argc, char *argv[])
{
    if (argc != 3)
    {
        printMessage("Invalid number of arguments. Usage: ./NehosClient <command> <serverID>\n", 71);
        return -1;
    }
    char *command = argv[1];
    if (strcmp(command, "connect") != 0 && strcmp(command, "tryConnect") != 0)
    {
        printMessage("Invalid command. Must be either 'connect' or 'tryConnect'\n", 58);
        return -1;
    }
    int serverID = atoi(argv[2]);
    if (serverID < 1)
    {
        printMessage("Invalid server ID. Must be a positive integer\n", 48);
        return -1;
    }

    return 0;
}

sem_t *initializeSemaphore(int serverID)
{
    char semaphoreName[256];
    sprintf(semaphoreName, "/serverSemaphore%d", serverID);
    sem_t *sem = sem_open(semaphoreName, O_CREAT, 0666, 1);
    if (sem == SEM_FAILED)
    {
        perror("sem_open");
        return NULL;
    }
    return sem;
}

void sigTermHandler(int signo)
{
    printf("Received SIGTERM signal\n");
    cleanFIFOs();
    exit(0);
}

void sigIntHandler(int signo)
{
    printf("Received SIGINT signal\n");
    Request request;
    request.requestType = QUIT;
    char *clientFIFOReadName = malloc(256);
    pid_t clientPID = getpid();
    sprintf(clientFIFOReadName, "/tmp/clientFIFO%dRD", clientPID);
    int clientFIFORead = open(clientFIFOReadName, O_WRONLY);
    if (clientFIFORead == -1)
    {
        perror("open");
        cleanFIFOs();
        exit(1);
    }
    int written = write(clientFIFORead, &request, sizeof(Request));
    if (written == -1)
    {
        perror("write");
        cleanFIFOs();
        exit(1);
    }
    close(clientFIFORead);
    cleanFIFOs();
    exit(0);
}