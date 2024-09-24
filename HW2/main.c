#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>
#include <stdbool.h>

static sig_atomic_t volatile counter = 0;                   // counter for the number of child processes that have exited



int printMessage(const char* message, int length)           // function to print a message to the console
{
    int bytesWritten = 0;
    while(bytesWritten < length)
    {
        int written = write(STDOUT_FILENO, message + bytesWritten, length - bytesWritten);
        if(written == -1)
        {
            return -1;
        }
        bytesWritten += written;
    }
    return bytesWritten;
}

void sigchld_handler()                                      // signal handler function
{
    int status;                                             // status of the child process
    pid_t pid;                                              // process ID of the child process
    
    while((pid = waitpid(-1, &status, WNOHANG)) > 0)        // loop to wait for all child processes to exit
    {
        if(WIFEXITED(status))                               // check if the child process exited normally
        {
            char* message = (char*)malloc(1024);
            sprintf(message, "Child process exited with status: %d\n", WEXITSTATUS(status));
            printMessage(message, strlen(message));
            free(message);
            message = (char*)malloc(1024);
            sprintf(message, "Exited child process ID: %d\n", pid);
            printMessage(message, strlen(message));
            free(message);
            
        }else
        {
            char* message = (char*)malloc(1024);
            sprintf(message, "Child process terminated by signal: %d\n", WTERMSIG(status));
            printMessage(message, strlen(message));
            free(message);
            message = (char*)malloc(1024);
            sprintf(message, "Terminated child process ID: %d\n", pid);
            printMessage(message, strlen(message));
            free(message);
            
        }
        counter++;
        
    }
    if(pid == -1 && counter != 2)                           // check if waitpid failed
        perror("waitpid");
    if(counter == 2)                                        // check if all child processes have exited
        printMessage("All child processes have exited.\n", 33);
    
}


void sigint_handler()                                       // signal handler function unlinks the fifos and exits the program
{
    unlink("fifo1");
    unlink("fifo2");
    printMessage("Exiting program...\n", 19);
    exit(EXIT_SUCCESS);
}

// function to print the usage of the program
void printUsage()
{
    printMessage("Usage: ./main <size_of_array> <command>\n", 40);
    printMessage("size_of_array: positive integer\n", 32);
    printMessage("command: multiply, sum\n", 23);
} 

// function for the first child process
int child1(char* pipes[2], int arr_len) // gets the pipes and the size of the array
{
    int fd1;
    int* array = (int*)malloc(arr_len * sizeof(int));
    int sum = 0;

    while(((fd1 = open(pipes[0], O_RDONLY)) == -1) && (errno == EINTR));    // open the first fifo
    if(fd1 == -1)
    {
        perror("open");
        return EXIT_FAILURE;
    }

    if(read(fd1, array, arr_len * sizeof(int)) == -1)                       // read the array from the first fifo
    {
        perror("read");
        return EXIT_FAILURE;
    }
    for(int i = 0; i < arr_len; i++)                                        // calculate the sum of the array
    {
        sum += array[i];
    }
    close(fd1);

    int fd2;
    while(((fd2 = open(pipes[1], O_WRONLY)) == -1) && (errno == EINTR));    // open the second fifo
    if(fd2 == -1)
    {
        perror("open");
        return EXIT_FAILURE;
    }
    if(write(fd2, &sum, sizeof(int)) == -1)                                 // write the sum to the second fifo
    {
        perror("write");
        return EXIT_FAILURE;
    }
    close(fd2);

    printMessage("Waiting in child1...\n", 21);                             // wait for 10 seconds
    sleep(10);

    char* str = (char*)malloc(1024);                                        // print the sum
    sprintf(str, "Sum in child1: %d\n", sum);
    printMessage(str, strlen(str));
    return EXIT_SUCCESS;
}

// function for the second child process
int child2(char* pipes[2], int arr_len, int sizeOfCommand) // gets the pipes, the size of the array and the size of the command
{
    int fd2;
    char* command = (char*)malloc(8);
    int* array = (int*)malloc(arr_len * sizeof(int));
    long result = 0, sumFromChild1 = 0;

    while(((fd2 = open(pipes[1], O_RDONLY)) == -1) && (errno == EINTR));        // open the second fifo              
    if(fd2 == -1)                                                               // check if the fifo was opened successfully                           
    {
        perror("open");
        return EXIT_FAILURE;
    }

    if(read(fd2, command, sizeOfCommand) == -1)                                 // read the command and array from the second fifo
    {
        perror("read");
        return EXIT_FAILURE;
    }
    if(read(fd2, array, arr_len * sizeof(int)) == -1)
    {
        perror("read");
        return EXIT_FAILURE;
    }
    if(strcmp(command, "multiply") == 0)                                        // check the command and calculate the result  
    {
        result = 1;
        for(int i = 0; i < arr_len; i++)
        {
            result *= array[i];
        }
    }else if(strcmp(command, "sum") == 0)
    {
        for(int i = 0; i < arr_len; i++)
        {
            result += array[i];
        }
    }

    printMessage("Waiting in child2...\n", 21);                             // wait for 10 seconds
    sleep(10);
    if(read(fd2, &sumFromChild1, sizeof(int)) == -1)                        // read the sum from the first child
    {
        perror("read");
        return EXIT_FAILURE;
    }
    close(fd2);

    char* str = (char*)malloc(1024);                                        // print the result and the final result(sum from child1 + result)                     
    sprintf(str, "Result of child2: %ld\nFinal Result in child2: %ld\n", result, sumFromChild1 + result);
    printMessage(str, strlen(str));
    free(str);

    return EXIT_SUCCESS;
}

int main(int argc, char *argv[]) {
    if(argc != 3)                                           // check if the number of arguments is correct
    {                       
        printUsage();
        return EXIT_FAILURE;
    }
    int arr_len = 0;
    if((arr_len = atoi(argv[1])) <= 0)                      // check if the size of the array is a positive integer
    {
        printUsage();
        return EXIT_FAILURE;
    }
    char* command = argv[2];
    // check if the command is valid
    if(command == NULL || (strcmp(command, "multiply") != 0 && strcmp(command, "sum") != 0))
    {
        printUsage();
        return 1;
    }
    srand(time(NULL));                                      // seed for random number generation
    struct sigaction sa;                                    // signal handler
    sa.sa_handler = sigchld_handler;                        // signal handler function
    sigemptyset(&sa.sa_mask);                               // initialize signal set
    sa.sa_flags = SA_RESTART;                               // restart interrupted system calls
    if(sigaction(SIGCHLD, &sa, NULL) == -1)                 // set signal handler for SIGCHLD
    {
        perror("sigaction");                                // print error message if sigaction fails
        return EXIT_FAILURE;
    }
    struct sigaction sa2;                                   // signal handler
    sa2.sa_handler = sigint_handler;                        // signal handler function
    sigemptyset(&sa2.sa_mask);                              // initialize signal set
    sa2.sa_flags = SA_RESTART;                              // restart interrupted system calls
    if(sigaction(SIGINT, &sa2, NULL) == -1)                 // set signal handler for SIGINT
    {
        perror("sigaction");                                // print error message if sigaction fails
        return EXIT_FAILURE;
    }


    
    int sizeOfCommand = strlen(command);                    // size of the command
    char* pipes[2] = {"fifo1", "fifo2"};                    // names of the pipes
    if(mkfifo(pipes[0], 0666) == -1)                        // try to create the first fifo
    {
        if(errno != EEXIST)                                 // if the error is not that the fifo already exists
        {
            perror("mkfifo");
            return EXIT_FAILURE;
        }
        // if the fifo already exists, print a message and continue
        printMessage("pipe1 already exists.\nContinuing...\n", 36);
    }
    if(mkfifo(pipes[1], 0666) == -1)                        // try to create the second fifo
    {
        if(errno != EEXIST)                                 // if the error is not that the fifo already exists
        {
            perror("mkfifo");
            return EXIT_FAILURE;
        }
        // if the fifo already exists, print a message and continue
        printMessage("pipe2 already exists.\nContinuing...\n", 36);
    }

    pid_t pid1 = -2;
    pid_t pid2 = fork();                                    // fork in the parent process to create the second child
    if(pid2 == -1)                                          // check if the fork was successful
    {
        perror("Error while forking for child2");
        return EXIT_FAILURE;
    }else if(pid2 == 0)                                     
    {
        return child2(pipes, arr_len, sizeOfCommand);       // call the child2 function
    }else
    {
        int* array = (int*)malloc(arr_len * sizeof(int));   // allocate memory for the array
        int sum = 0;                                        // initialize the sum
        for(int i = 0; i < arr_len; i++)                    // fill the array with random numbers and calculate the sum
        {
            array[i] = rand() % 100;
            sum += array[i];
        }
       
        char* str = (char*)malloc(1024);                    // create a string to print the initial sum and array
        sprintf(str, "Initial sum: %d\nInitial array: ", sum);
        for(int i = 0; i < arr_len; i++)
        {
            char* str2 = (char*)malloc(4);
            sprintf(str2, "%d ", array[i]);
            strcat(str, str2);
        }
        strcat(str, "\n");
        printMessage(str, strlen(str));
        free(str);

        int fd2;                                            // file descriptor for the second fifo
        while(((fd2 = open(pipes[1], O_WRONLY)) == -1) && (errno == EINTR)); // open the second fifo
        if(fd2 == -1)
        {
            perror("open");
            return EXIT_FAILURE;
        }
        // write the command and array to second fifo
        if(write(fd2, command, strlen(command)) == -1)
        {
            perror("write");
            return EXIT_FAILURE;
        }
        if(write(fd2, array, arr_len * sizeof(int)) == -1)
        {
            perror("write");
            return EXIT_FAILURE;
        }
        close(fd2);                                         // close the second fifo

        pid1 = fork();                                      // fork in the parent process to create the first child
        if(pid1 == -1)
        {
            perror("Error while forking for child1");
            return EXIT_FAILURE;
        }else if(pid1 == 0)
        {
            return child1(pipes, arr_len);                  // call the child1 function
        }else
        {
            int fd1;                                        // file descriptor for the first fifo
            while(((fd1 = open(pipes[0], O_WRONLY)) == -1) && (errno == EINTR)); // open the first fifo
            if(fd1 == -1)
            {
                perror("open");
                return EXIT_FAILURE;
            }
            // write the array to the first fifo
            if(write(fd1, array, arr_len * sizeof(int)) == -1)
            {
                perror("write");
                return EXIT_FAILURE;
            }
            close(fd1);
            

            while(counter != 2)                                        // infinite loop to print a message every 2 seconds
            {
                printMessage("proceeding...\n", 14);
                sleep(2);
            }
            free(array);                                                // free the memory allocated for the array

            
        }
    }
    unlink("fifo1");                                        // remove the first fifo
    unlink("fifo2");                                        // remove the second fifo
    return EXIT_SUCCESS;
}