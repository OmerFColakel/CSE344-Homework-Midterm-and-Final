#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <errno.h>
#include <signal.h>

#define MAX_BUFFER_SIZE 1024 // maximum buffer size
#define PATH_MAX 4096        // maximum path length

typedef struct //  A struct that holds information about files to be copied
{
    int srcFD;               // source file descriptor
    int destFD;              // destination file descriptor
    char srcName[PATH_MAX];  // source file path
    char destName[PATH_MAX]; // destination file path
    int isFifo;              // flag to indicate if the file is a FIFO file
    int isDir;               // flag to indicate if the file is a directory
} filePairStruct;

filePairStruct buffer[MAX_BUFFER_SIZE]; //  An array of filePairStruct structs that holds information about files to be copied
int bufferSize;                      // size of the buffer
int bufferCount = 0;                 // number of items in the buffer
int done = 0;                        // flag to indicate all files are processed
long totalBytes = 0;                 // total bytes transferred
int numRegular = 0;                  // number of regular files transferred
int numFifo = 0;                     // number of FIFO files transferred
int numDir = 0;                      // number of directories transferred

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;        // mutex for buffer
pthread_cond_t bufferNotFull = PTHREAD_COND_INITIALIZER;  // condition variable for buffer not full
pthread_cond_t bufferNotEmpty = PTHREAD_COND_INITIALIZER; // condition variable for buffer not empty

void *manager(void *arg);                                         // manager thread
void *worker(void *arg);                                          // worker thread
void processDirectory(const char *srcDir, const char *destDir); // process directory
void SIGINTHandler(int signo);                                    // signal handler

int main(int argc, char *argv[])
{
    struct sigaction act;
    act.sa_handler = SIGINTHandler;
    act.sa_flags = 0;
    if ((sigemptyset(&act.sa_mask) == -1) || (sigaction(SIGINT, &act, NULL) == -1))
    {
        perror("Failed to set SIGINT handler");
        return 1;
    }

    if (argc != 5) // Check if the number of arguments is correct
    {
        fprintf(stderr, "Usage: %s <buffer_size> <num_workers> <srcDir> <destDir>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    bufferSize = atoi(argv[1]);
    int numberOfWorkers = atoi(argv[2]);
    char *srcDir = argv[3];
    char *destDir = argv[4];

    if (bufferSize <= 0 || numberOfWorkers <= 0) // Check if the buffer size and number of workers are valid
    {
        fprintf(stderr, "Invalid buffer size or number of workers\n");
        exit(EXIT_FAILURE);
    }

    struct stat statbuf;
    if (stat(destDir, &statbuf) != 0)
    {
        if (errno == ENOENT)
        {
            // Directory does not exist, create it
            if (mkdir(destDir, 0755) != 0)
            {
                perror("mkdir destDir");
                exit(EXIT_FAILURE);
            }
        }
        else
        {
            perror("stat destDir");
            exit(EXIT_FAILURE);
        }
    }
    else if (!S_ISDIR(statbuf.st_mode))
    {
        fprintf(stderr, "Destination exists but is not a directory\n");
        exit(EXIT_FAILURE);
    }

    pthread_t managerThread;                  // manager thread
    pthread_t workerThreads[numberOfWorkers]; // worker threads

    struct timeval start, end;                                   // start and end time
    gettimeofday(&start, NULL);                                  // get the start time
    pthread_create(&managerThread, NULL, manager, (void *)argv); // create the manager thread

    for (int i = 0; i < numberOfWorkers; i++) // create the worker threads
    {
        pthread_create(&workerThreads[i], NULL, worker, NULL);
    }

    pthread_join(managerThread, NULL); // wait for the manager thread to finish

    for (int i = 0; i < numberOfWorkers; i++) // wait for the worker threads to finish
    {
        pthread_join(workerThreads[i], NULL);
    }

    gettimeofday(&end, NULL);                 // get the end time
    long seconds = end.tv_sec - start.tv_sec; // calculate the total time
    long miliseconds = (end.tv_usec - start.tv_usec) / 1000;
    long minutes = seconds / 60;
    seconds = seconds % 60;

    //  Print the statistics
    char *stdoutBuffer = (char *)malloc(4096);
    sprintf(stdoutBuffer, "All files copied successfully.\n\n---------------STATISTICS--------------------\nConsumers: %d - Buffer Size: %d\nNumber of Regular File: %d\nNumber of FIFO File: %d\nNumber of Directory: %d\nTOTAL BYTES COPIED: %ld\nTOTAL TIME: %02ld:%02ld.%03ld (min:sec.mili)\n", numberOfWorkers, bufferSize, numRegular, numFifo, numDir, totalBytes, minutes, seconds, miliseconds);
    write(STDOUT_FILENO, stdoutBuffer, strlen(stdoutBuffer));
    free(stdoutBuffer);
    return 0;
}

//  Process the directory
void processDirectory(const char *srcDir, const char *destDir)
{
    DIR *src_dp = opendir(srcDir); // open the source directory
    if (src_dp == NULL)             // check if the directory is opened successfully
    {
        perror("opendir");
        return;
    }

    struct dirent *entry;                     // directory entry
    while ((entry = readdir(src_dp)) != NULL) // read the directory
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) // skip the current and parent directory
        {
            continue;
        }

        char src_path[PATH_MAX], dest_path[PATH_MAX];                    // source and destination path
        snprintf(src_path, PATH_MAX, "%s/%s", srcDir, entry->d_name);   // create the source path
        snprintf(dest_path, PATH_MAX, "%s/%s", destDir, entry->d_name); // create the destination path

        struct stat statbuf;              // file status
        if (stat(src_path, &statbuf) < 0) // get the file status
        {
            perror("stat"); // check if the file status is retrieved successfully
            continue;       // continue to the next file
        }

        if (S_ISREG(statbuf.st_mode)) // check if the file is a regular file
        {
            int src_fd = open(src_path, O_RDONLY); // open the source file
            if (src_fd < 0)                        // check if the file is opened successfully
            {
                perror("open src"); // print the error message
                continue;           // continue to the next file
            }

            int dest_fd = open(dest_path, O_WRONLY | O_CREAT | O_TRUNC, 0644); // open the destination file to write the content of the source file to it
            if (dest_fd < 0)                                                   // check if the file is opened successfully
            {
                close(src_fd);
                perror("open dest");
                continue;
            }

            pthread_mutex_lock(&mutex);       // lock the mutex
            while (bufferCount == bufferSize) // check if the buffer is full
            {
                pthread_cond_wait(&bufferNotFull, &mutex); // wait for the buffer to be not full
            }

            buffer[bufferCount].srcFD = src_fd;                         // set the source file descriptor
            buffer[bufferCount].destFD = dest_fd;                       // set the destination file descriptor
            strncpy(buffer[bufferCount].srcName, src_path, PATH_MAX);   // set the source path
            strncpy(buffer[bufferCount].destName, dest_path, PATH_MAX); // set the destination path
            buffer[bufferCount].isFifo = 0;                             // set the flag to indicate that the file is not a FIFO file
            buffer[bufferCount].isDir = 0;                              // set the flag to indicate that the file is not a directory
            bufferCount++;                                              // increment the number of items in the buffer

            pthread_cond_signal(&bufferNotEmpty); // signal that the buffer is not empty
            pthread_mutex_unlock(&mutex);         // unlock the mutex
        }
        else if (S_ISFIFO(statbuf.st_mode)) // check if the file is a FIFO file
        {
            if (mkfifo(dest_path, 0644) < 0)
            {
                perror("mkfifo");
                continue;
            }

            pthread_mutex_lock(&mutex);
            while (bufferCount == bufferSize)
            {
                pthread_cond_wait(&bufferNotFull, &mutex);
            }

            strncpy(buffer[bufferCount].srcName, src_path, PATH_MAX);
            strncpy(buffer[bufferCount].destName, dest_path, PATH_MAX);
            buffer[bufferCount].isFifo = 1;
            buffer[bufferCount].isDir = 0;
            bufferCount++;

            pthread_cond_signal(&bufferNotEmpty);
            pthread_mutex_unlock(&mutex);
        }
        else if (S_ISDIR(statbuf.st_mode)) // check if the file is a directory
        {
            if (mkdir(dest_path, 0755) < 0)
            {
                perror("mkdir");
                continue;
            }

            pthread_mutex_lock(&mutex);
            numDir++;
            pthread_mutex_unlock(&mutex);

            processDirectory(src_path, dest_path); // process the directory recursively
        }
    }

    closedir(src_dp); // close the source directory
}

//  Purpose of the manager thread is to process the source directory
void *manager(void *arg)
{
    char **argv = (char **)arg;
    char *srcDir = argv[3];  //  Source directory
    char *destDir = argv[4]; // Destination directory

    processDirectory(srcDir, destDir); // process the source directory

    pthread_mutex_lock(&mutex);
    done = 1;
    pthread_cond_broadcast(&bufferNotEmpty);
    pthread_mutex_unlock(&mutex);

    return NULL;
}

//  Purpose of the worker thread is to copy the files from the buffer
void *worker(void *arg)
{
    (void)arg;
    while (1)
    {
        pthread_mutex_lock(&mutex);       // lock the mutex
        while (bufferCount == 0 && !done) // check if the buffer is empty
        {
            pthread_cond_wait(&bufferNotEmpty, &mutex); // wait for the buffer to be not empty
        }

        if (bufferCount == 0 && done) // check if the buffer is empty and all files are processed
        {
            pthread_mutex_unlock(&mutex); // unlock the mutex
            break;
        }

        filePairStruct filePair = buffer[--bufferCount]; // get the file pair from the buffer
        pthread_cond_signal(&bufferNotFull);           // signal that the buffer is not full
        pthread_mutex_unlock(&mutex);                  // unlock the mutex

        if (!filePair.isFifo && !filePair.isDir) // check if the file is a regular file
        {
            char buffer[4096];
            ssize_t bytes;
            while ((bytes = read(filePair.srcFD, buffer, sizeof(buffer))) > 0) // read the content of the source file
            {
                if (write(filePair.destFD, buffer, bytes) != bytes) // write the content of the source file to the destination file
                {
                    perror("write");
                    break;
                }
                pthread_mutex_lock(&mutex);
                totalBytes += bytes;
                pthread_mutex_unlock(&mutex);
            }

            pthread_mutex_lock(&mutex); // lock the mutex
            numRegular++;
            pthread_mutex_unlock(&mutex); // unlock the mutex

            if (close(filePair.srcFD) < 0) // close the source file
            {
                perror("close src");
            }

            if (close(filePair.destFD) < 0) // close the destination file
            {
                perror("close dest");
            }
        }
        else if (filePair.isFifo) // check if the file is a FIFO file
        {
            pthread_mutex_lock(&mutex);
            numFifo++;
            pthread_mutex_unlock(&mutex);
        }

        char *stdoutBuffer = (char *)malloc(4096);
        sprintf(stdoutBuffer, "Copied: %s -> %s\n", filePair.srcName, filePair.destName);
        write(STDOUT_FILENO, stdoutBuffer, strlen(stdoutBuffer));
        free(stdoutBuffer);
    }

    return NULL;
}

//  Signal handler for SIGINT
void SIGINTHandler(int signo)
{
    if (signo == SIGINT)
    {

        char *stdoutBuffer = (char *)malloc(4096);
        sprintf(stdoutBuffer, "SIGINT received. Exiting...\n");
        write(STDOUT_FILENO, stdoutBuffer, strlen(stdoutBuffer));
        free(stdoutBuffer);

        pthread_mutex_lock(&mutex);
        done = 1;
        pthread_cond_broadcast(&bufferNotEmpty);
        pthread_mutex_unlock(&mutex);

        pthread_mutex_lock(&mutex);
        for (int i = 0; i < bufferCount; i++)
        {
            if (!buffer[i].isFifo && !buffer[i].isDir)
            {
                close(buffer[i].srcFD);
                close(buffer[i].destFD);
            }
        }
        pthread_mutex_unlock(&mutex);

        exit(EXIT_FAILURE);
    }
}
