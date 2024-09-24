#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>
#include <sys/wait.h>

#define LOGGER_NAME "log.txt"

typedef struct sortElement
{
    char name[50];
    char grade[50];
} element;

// Write to the file
int writeToFile(int fd, const char *buffer, int size)
{
    int bytesWritten = 0;
    int totalBytesWritten = 0;
    while (totalBytesWritten != size)
    {

        while ((bytesWritten = write(fd, buffer, size)) == -1 && errno == EINTR)
            ;
        if (bytesWritten == -1)
        {
            write(STDERR_FILENO, "Error writing to file", errno);
            fflush(stdout);
            return -1;
        }
        totalBytesWritten += bytesWritten;
    }
    return totalBytesWritten;
}

//  Write to the logger file
void writeToLogger(const char *message, int size)
{
    int bytesWritten = 0;
    int totalBytesWritten = 0;
    int fd = open(LOGGER_NAME, O_WRONLY | O_APPEND | O_CREAT, 0664);
    if (fd == -1)
    {
        writeToFile(STDERR_FILENO, strerror(errno), strlen(strerror(errno)));
        return;
    }
    time_t rawtime;
    struct tm *timeinfo;
    char buffer[80];

    // Get current time
    time(&rawtime);
    timeinfo = localtime(&rawtime);

    // Format time as string
    int bufferSize = strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);

    int newStringSize = size + bufferSize;
    char *newString = (char *)malloc(newStringSize);
    memcpy(newString, buffer, bufferSize);
    memcpy(newString + bufferSize, "-", 1);
    memcpy(newString + bufferSize + 1, message, size);

    while (totalBytesWritten != newStringSize)
    {
        while ((bytesWritten = write(fd, newString, newStringSize)) == -1 && errno == EINTR)
            ;
        if (bytesWritten == -1)
        {
            write(STDERR_FILENO, "Error writing to file: ", errno);
            fflush(stdout);
            return;
        }
        totalBytesWritten += bytesWritten;
    }

    close(fd);
    free(newString);
}

// Add a student grade to the file
void addStudentGrade(const char *filename, const char *name, const char *grade)
{
    int fd = open(filename, O_WRONLY | O_APPEND, 0664);
    if (fd == -1)
    {
        writeToFile(STDOUT_FILENO, strerror(errno), strlen(strerror(errno)));
        writeToLogger("Error while opening the file.(addStudentGrade)\n", 48);
        return;
    }
    int nameSize = strlen(name);
    int gradeSize = strlen(grade);
    int bufferSize = nameSize + gradeSize + 2;
    char buffer[bufferSize];
    memcpy(buffer, name, nameSize);
    memcpy(buffer + nameSize, " ", 1);
    memcpy(buffer + nameSize + 1, grade, gradeSize);
    memcpy(buffer + nameSize + gradeSize + 1, "\n", 1);
    int totalBytesWritten = writeToFile(fd, buffer, bufferSize);
    if (totalBytesWritten != bufferSize)
    {
        writeToFile(STDERR_FILENO, strerror(errno), strlen(strerror(errno)));
        writeToLogger("Error writing to file.(addStudentGrade)\n", 35);
        close(fd);
        return;
    }
    close(fd);
    writeToLogger("Student grade added.\n", 22);
    writeToFile(STDOUT_FILENO, "Student grade added.\n", 22);
}

// Parses given buffer and returns the grade of the student
char *searchStudentHelper(const char *buffer, const char *name, int size)
{
    char *bp = (char *)malloc(size);
    memcpy(bp, buffer, size);
    char *line = strtok(bp, "\n");
    int lineSize = strlen(line);
    while (line != NULL && lineSize > 0)
    {
        for (int i = lineSize - 1; i >= 0; i--)
        {
            if (line[i] == ' ')
            {
                char grade[lineSize - i - 1];
                strcpy(grade, line + i + 1);
                int nameSize = i;
                char newName[nameSize];
                strncpy(newName, line, nameSize);
                if (strcmp(name, newName) == 0)
                {
                    char *returnedString = (char *)malloc(lineSize - i);
                    strcpy(returnedString, grade);
                    return returnedString;
                }
                break;
            }
        }
        bp += lineSize + 1;
        line = strtok(NULL, "\n");
        if (line != NULL)
        {
            lineSize = strlen(line);
        }
        if (lineSize == 0)
        {
            return NULL;
        }
    }
    return NULL;
}

// Search for a student in the file
char *searchStudent(const char *filename, const char *name)
{
    int fd = open(filename, O_RDONLY); // Open the file with read only mode
    if (fd == -1)                      // Check if the file is opened
    {
        writeToFile(STDERR_FILENO, strerror(errno), strlen(strerror(errno)));
        writeToLogger("Error while opening the file.(searchStudent)\n", 46);
        return NULL;
    }
    char buffer[1024]; // Buffer to read the file
    int bytesRead = 0; // Bytes Read
    char carry[1024];  // Carry Buffer
    int carrySize = 0; // Carry Size
    memset(carry, 0, 1024);
    while (1)
    {
        while ((bytesRead = read(fd, buffer + carrySize, 1024 - carrySize)) == -1 && errno == EINTR) // Reads the file
            ;
        if (bytesRead <= 0) // If the file is empty or the end of the file is reached
        {
            if (bytesRead == -1) // If an error occurs
            {
                writeToFile(STDERR_FILENO, strerror(errno), strlen(strerror(errno)));
                writeToLogger("Error reading file.(searchStudent)\n", 36);
                close(fd);
                return NULL;
            }
            close(fd);
            break;
        }
        for (int i = bytesRead - 1; i >= 0; i--)
        {
            if (buffer[i] == '\n')
            {
                for (int j = 0; j < 1024 - i; ++j)
                {
                    carry[j] = buffer[i + j];
                }
                char *returnedString = searchStudentHelper(buffer, name, i + 1); // Search for the student in the buffer
                if (returnedString != NULL)                                      // If the student is found return the grade
                {
                    writeToLogger("Student found.\n", 16);
                    writeToFile(STDOUT_FILENO, "Student found.\n", 16);
                    return returnedString;
                }
                free(returnedString); // Free the returned string
                memset(buffer, 0, 1024);
                carrySize = 1024 - i;
                for (int j = 0; j < 1024 - i; j++) // Copy the carry buffer to the buffer
                {
                    buffer[j] = carry[j];
                }
                memset(carry, 0, 1024); // Clear the carry buffer
                break;
            }
        }
    }
    close(fd);
    return NULL;
}

// Write how to use the program to the console
void usage()
{
    int returnvals[10];
    returnvals[0] = writeToFile(STDOUT_FILENO, "Note: Please include the character \" \" in the command and arguments\n", 69);
    returnvals[1] = writeToFile(STDOUT_FILENO, "Commands:\n", 11);
    returnvals[2] = writeToFile(STDOUT_FILENO, "addStudentGrade \"filename\" \"name\" \"grade\"\n", 43);
    returnvals[3] = writeToFile(STDOUT_FILENO, "searchStudent \"filename\" \"name\"\n", 34);
    returnvals[4] = writeToFile(STDOUT_FILENO, "showAll \"filename\"\n", 20);
    returnvals[5] = writeToFile(STDOUT_FILENO, "listGrades \"filename\"\n", 23);
    returnvals[6] = writeToFile(STDOUT_FILENO, "listSome \"numofEntries\" \"pageNumber\" \"filename\"\n", 49);
    returnvals[7] = writeToFile(STDOUT_FILENO, "sortAll \"filename\" \"name/grade\" \"asc/dcs\"\n", 43);
    returnvals[8] = writeToFile(STDOUT_FILENO, "gtuStudentGrades \"filename\"\n", 29);
    returnvals[9] = writeToFile(STDOUT_FILENO, "exit\n", 6);
    for (int i = 0; i < 10; i++)
    {
        if (returnvals[i] == -1)
        {
            writeToFile(STDERR_FILENO, strerror(errno), strlen(strerror(errno)));
            writeToLogger("Error writing to stdout.(usage)\n", 33);
            return;
        }
    }
    writeToLogger("Usage printed.\n", 16);
}

// Write the first 5 entries to the console
void listGrades(const char *fileName)
{
    int fd = open(fileName, O_RDONLY); // Open the file with read only mode
    if (fd == -1)                      // Check if the file is opened
    {
        writeToFile(STDERR_FILENO, strerror(errno), strlen(strerror(errno)));
        writeToLogger("Error while opening the file.(listGrades)\n", 43);
        return;
    }
    char buffer[1024];      // Buffer to read the file
    int bytesRead = 0;      // Bytes Read
    int totalBytesRead = 0; // Total Bytes Read
    int lineCount = 0;      // Line Count
    int bytesWritten = 0;   // Bytes Written
    char *bp;
    while (1)
    {
        while ((bytesRead = read(fd, buffer, 1024)) == -1 && errno == EINTR) // Reads the file until an error occurs. ignores the EINTR error
            ;
        if (bytesRead <= 0) // If the file is empty or the end of the file is reached
        {
            if (bytesRead == -1) // If an error occurs
            {
                writeToFile(STDERR_FILENO, strerror(errno), strlen(strerror(errno)));
                writeToLogger("Error reading file.(listGrades)\n", 33);
                close(fd);
                return;
            }
            break;
        }
        bp = buffer;   // Set the buffer pointer to the start of the buffer
        int index = 0; // Index to keep track of the position of the next newline character
        while (bytesRead > 0)
        {
            while (bp[index] != '\n' && bytesRead > index) // Find the next newline character or the end of the buffer
                index++;

            //  Write the buffer until the newline character to the console and ignore the EINTR error
            while ((bytesWritten = writeToFile(STDOUT_FILENO, bp, index)) == -1 && errno == EINTR)
                ;
            if (bytesWritten < 0) // If an error occurs
            {
                writeToFile(STDERR_FILENO, strerror(errno), strlen(strerror(errno)));
                writeToLogger("Error writing to stdout.(listGrades)\n", 38);
                close(fd);
                return;
            }
            if (bp[index] == '\n') // If the newline character is found increase the line count
            {
                lineCount++;
            }

            totalBytesRead += bytesWritten;
            bytesRead -= bytesWritten;
            bp += index + 1;
            index = 0;
            int temp = 0;
            // Write a newline character to the console and ignore the EINTR error
            while ((temp = writeToFile(STDOUT_FILENO, "\n", 1)) == -1 && errno == EINTR)
                ;
            if (temp < 0)
            {
                writeToFile(STDERR_FILENO, strerror(errno), strlen(strerror(errno)));
                writeToLogger("Error writing to stdout.(listGrades)\n", 38);
                close(fd);
                return;
            }

            if (lineCount == 5) // If the line count is 5 close the file and return
            {
                writeToLogger("First 5 entries listed.\n", 25);
                close(fd);
                return;
            }
        }
    }
    writeToLogger("First 5 entries listed.\n", 25);
    close(fd);
}

// Lists All the Students and Their Grades
void showAll(const char *fileName)
{
    int fd = open(fileName, O_RDONLY); // Open the file
    if (fd == -1)                      // Check if the file is opened
    {
        writeToFile(STDERR_FILENO, strerror(errno), strlen(strerror(errno)));
        writeToLogger("Error while opening the file.(showAll)\n", 40);
        return;
    }
    char buffer[1024];      // Buffer to read the file
    int bytesRead = 0;      // Bytes Read
    int totalBytesRead = 0; // Total Bytes Read
    int bytesWritten = 0;   // Bytes Written
    int totalBytesWritten = 0;
    char *bp;
    while (1)
    {
        while ((bytesRead = read(fd, buffer, 1024)) == -1 && errno == EINTR)
            ;
        if (bytesRead <= 0)
        {
            if (bytesRead == -1)
            {
                writeToLogger("Error reading file.(showAll)\n", 30);
                writeToFile(STDERR_FILENO, strerror(errno), strlen(strerror(errno)));
                close(fd);
                return;
            }
            break;
        }
        bp = buffer;
        totalBytesRead += bytesRead;
        while (bytesRead > 0)
        {
            while ((bytesWritten = writeToFile(STDOUT_FILENO, bp, bytesRead)) == -1 && errno == EINTR)
                ;
            if (bytesWritten < 0)
            {
                writeToLogger("Error writing to stdout.(showAll)\n", 35);
                writeToFile(STDERR_FILENO, strerror(errno), strlen(strerror(errno)));
                break;
            }
            totalBytesWritten += bytesWritten;
            bytesRead -= bytesWritten;
            bp += bytesWritten;
        }
    }
    if (totalBytesRead != totalBytesWritten)
    {
        writeToLogger("Error writing to stdout.(showAll)\n", 35);
        writeToFile(STDERR_FILENO, "Error writing to stdout", errno);
        close(fd);
        return;
    }
    writeToFile(STDOUT_FILENO, "All students and their grades listed.\n", 39);
    writeToLogger("All students and their grades listed.\n", 39);
    close(fd);
}

// Lists "numofEntries” “pageNumber” “grades.txt”
void listSome(int numofEntries, int pageNumber, const char *fileName)
{
    // numofEntries is the number of entries to be listed
    // pageNumber is the page number
    // fileName is the file name
    int fd = open(fileName, O_RDONLY); // Open the file
    if (fd == -1)                      // Check if the file is opened
    {
        writeToFile(STDERR_FILENO, "Error: ", errno);
        writeToLogger("Error while opening the file.(listSome)\n", 41);
        return;
    }
    char buffer[1024]; // Buffer to read the file
    int bytesRead = 0; // Bytes Read
    // Buffer might not be enough to read the whole line.
    // searchStudentHelper requires a buffer that is enough to read the whole line
    // So we need to keep the last part of the buffer and append it to the next buffer
    int carryCapacity = 10;                      // Carry Capacity
    char *carry = (char *)malloc(carryCapacity); // Carry Buffer
    int lineCount = 0;                           // Line Count
    int start = (pageNumber - 1) * numofEntries; // Start index
    int end = pageNumber * numofEntries;         // End index
    while (1)
    {
        while ((bytesRead = read(fd, buffer, 1024)) == -1 && errno == EINTR) // Reads the file until an error occurs. ignores the EINTR error
            ;
        if (bytesRead <= 0) // If the file is empty or the end of the file is reached
        {
            if (bytesRead == -1) // If an error occurs
            {
                writeToFile(STDERR_FILENO, "Error reading file", errno);
                writeToLogger("Error reading file.(listSome)\n", 31);
                close(fd);
                return;
            }
            break;
        }
        char *bp = buffer; // Set the buffer pointer to the start of the buffer
        int index = 0;     // Index to keep track of the position of the next newline character
        while (bytesRead > 0)
        {
            while (bp[index] != '\n' && bytesRead > index) // Find the next newline character or the end of the buffer
                index++;
            if (lineCount >= start && lineCount < end) // If the line count is between the start and end index
            {
                int bytesWritten = 0;                                                                  // Bytes Written
                while ((bytesWritten = writeToFile(STDOUT_FILENO, bp, index)) == -1 && errno == EINTR) // Write the buffer until the newline character to the console and ignore the EINTR error
                    ;
                if (bytesWritten < 0) // If an error occurs
                {
                    writeToFile(STDERR_FILENO, "Error writing to stdout", errno);
                    writeToLogger("Error writing to stdout.(listSome)\n", 36);
                    close(fd);
                    return;
                }
                int temp = 0;                                                                // Temp variable to keep track of the bytes written
                while ((temp = writeToFile(STDOUT_FILENO, "\n", 1)) == -1 && errno == EINTR) // Write a newline character to the console and ignore the EINTR error
                    ;
                if (temp < 0) // If an error occurs
                {
                    writeToFile(STDERR_FILENO, "Error writing to stdout", errno);
                    writeToLogger("Error writing to stdout.(listSome)\n", 36);
                    close(fd);
                    return;
                }
            }
            if (lineCount >= end) // If the line count is greater than or equal to the end index close the file and return
            {
                writeToLogger("Listed the entries.\n", 21);
                close(fd);
                return;
            }
            lineCount++; // Increase the line count
            bytesRead -= index + 1;
            bp += index + 1;
            index = 0;
        }
    }
    free(carry); // Free the carry buffer
    writeToLogger("Listed the entries.\n", 21);
    close(fd);
    return;
}

// Test the fork function
void forkTester()
{
    while (1)
    {
    }
}

int compareAsc(char *a, char *b)
{
    return strcmp(a, b);
}

int compareDsc(char *a, char *b)
{
    return strcmp(b, a);
}

void bubbleSort(element *elements, int elementsSize, char *sortBy, char *order)
{
    int (*compare)(char *, char *);

    if (strcmp(order, "asc") == 0)
    {
        compare = compareAsc;
    }
    else
    {
        compare = compareDsc;
    }
    int isSortbyName = strcmp(sortBy, "name") == 0;
    for (int i = 0; i < elementsSize; i++)
    {
        for (int j = 0; j < elementsSize - i - 1; j++)
        {
            if (isSortbyName)
            {
                if (compare(elements[j].name, elements[j + 1].name) > 0)
                {
                    element temp = elements[j];
                    elements[j] = elements[j + 1];
                    elements[j + 1] = temp;
                }
            }
            else
            {
                if (compare(elements[j].grade, elements[j + 1].grade) > 0)
                {
                    element temp = elements[j];
                    elements[j] = elements[j + 1];
                    elements[j + 1] = temp;
                }
            }
        }
    }
}

void sortAll(const char *filename, char *sortBy, char *order)
{
    int fd = open(filename, O_RDONLY);
    if (fd == -1)
    {
        writeToFile(STDERR_FILENO, "Error opening file", errno);
        writeToLogger("Error opening file.\n", 20);
        return;
    }
    if (strcmp(sortBy, "name") != 0 && strcmp(sortBy, "grade") != 0)
    {
        writeToFile(STDERR_FILENO, "Invalid argument for sorting", errno);
        writeToLogger("Invalid argument for sorting.\n", 31);
        close(fd);
        return;
    }
    if (strcmp(order, "asc") != 0 && strcmp(order, "dsc") != 0)
    {
        writeToFile(STDERR_FILENO, "Invalid argument for sorting", errno);
        writeToLogger("Invalid argument for sorting.\n", 31);
        close(fd);
        return;
    }
    element elements[2048];
    char buffer[1024];
    int bytesRead = 0;
    int totalBytesRead = 0;
    int lineCount = 0;
    int carry = 0;
    while (1)
    {
        while ((bytesRead = read(fd, buffer + carry, 1024 - carry)) == -1 && errno == EINTR)
            ;
        if (bytesRead <= 0)
        {
            if (bytesRead == -1)
            {
                writeToFile(STDERR_FILENO, "Error reading file", errno);
                writeToLogger("Error reading file.\n", 20);
                close(fd);
                break;
            }
            break;
        }
        char *bp = buffer;
        totalBytesRead += bytesRead;
        while (bytesRead > 0)
        {
            char *line = strtok(bp, "\n");
            int lineSize = strlen(line);
            if (line != NULL && lineSize < bytesRead + 1)
            {
                for (int i = sizeof(line) - 1; i >= 0; --i)
                {
                    if (line[i] == ' ')
                    {
                        char grade[sizeof(line) - i - 1];
                        strcpy(grade, line + i + 1);
                        char name[i];
                        strncpy(name, line, i);
                        strcpy(elements[lineCount].name, name);
                        strcpy(elements[lineCount].grade, grade);
                        lineCount++;
                        if (lineCount == 2048)
                        {

                            break;
                        }
                        break;
                    }
                }

                bytesRead -= strlen(line) + 1;
                bp += strlen(line) + 1;
            }
            else
            {
                carry = bytesRead;
                bytesRead = 0;

                for (int i = 0; i < carry; i++)
                {
                    buffer[i] = buffer[1024 - carry + i];
                }
                memset(buffer + carry, 0, 1024 - carry);
            }
            if (lineCount == 2048)
            {
                break;
            }
        }
        if (lineCount == 2048)
        {
            break;
        }
    }

    writeToFile(STDOUT_FILENO, "Sorting...\n", 12);
    bubbleSort(elements, lineCount, sortBy, order);
    writeToFile(STDOUT_FILENO, "Sorted.\n", 9);
    for (int i = 0; i < lineCount; i++)
    {
        writeToFile(STDOUT_FILENO, elements[i].name, strlen(elements[i].name));
        writeToFile(STDOUT_FILENO, " ", 1);
        writeToFile(STDOUT_FILENO, elements[i].grade, strlen(elements[i].grade));
        writeToFile(STDOUT_FILENO, "\n", 1);
    }
}

int main()
{
    char buffer[1024];
    ssize_t bytesRead;
    writeToFile(STDOUT_FILENO, "Enter a command: ", 17);
    fflush(stdout);
    while (1)
    {

        // Read input from stdin
        bytesRead = read(STDIN_FILENO, buffer, sizeof(buffer));
        if (bytesRead == -1)
        {
            writeToFile(STDERR_FILENO, "Error reading from stdin: ", errno);
            writeToLogger("Error reading from stdin.\n", 27);
            return 1;
        }
        if (bytesRead == 0)
        {
            writeToFile(STDERR_FILENO, "EOF: ", errno);
            writeToLogger("EOF.\n", 6);
            break;
        }

        // Null terminate the string
        buffer[bytesRead - 1] = '\0';

        // Parse the input command and arguments
        char *token = strtok(buffer, " \"");
        if (token == NULL)
        {
            writeToLogger("Error: Invalid command.\n", 25);

            usage();
            continue;
        }
        else if (strcmp(token, "exit") == 0 || strcmp(token, "exit\n") == 0)
        {
            writeToLogger("Exiting.\n", 10);
            break;
        }

        // Extract the command and arguments
        char *command = token;
        printf("Command: %s\n", command);
        char *args[3];
        for (int i = 0; i < 3; i++)
        {
            args[i] = NULL;
        }
        for (int i = 0; i < 3; i++)
        {
            token = strtok(NULL, "\"");
            if (token == NULL)
            {
                args[i] = NULL;
                break;
            }
            args[i] = token;
            strtok(NULL, "\"");
        }

        // Execute the appropriate function based on the command
        if (strcmp(command, "addStudentGrade") == 0 && args[0] != NULL && args[1] != NULL && args[2] != NULL)
        {
            pid_t pid = fork();
            if (pid == 0)
            {
                writeToLogger("Adding student grade.\n", 23);
                addStudentGrade(args[0], args[1], args[2]);
            }
            else if (pid == -1)
            {
                writeToFile(STDERR_FILENO, "Error forking", errno);
                writeToLogger("Error forking.\n", 15);
            }
            else
            {
                int status;
                waitpid(pid, &status, 0);
            }
        }
        else if (strcmp(command, "searchStudent") == 0 && args[0] != NULL && args[1] != NULL)
        {
            pid_t pid = fork();
            if (pid == 0)
            {
                writeToLogger("Searching for student.\n", 24);
                char *grade = searchStudent(args[0], args[1]);
                if (grade != NULL)
                {
                    writeToFile(STDOUT_FILENO, "Grade: ", 8);
                    writeToFile(STDOUT_FILENO, grade, strlen(grade));
                    writeToFile(STDOUT_FILENO, "\n", 2);
                }
                else
                {
                    writeToFile(STDOUT_FILENO, "Student not found\n", 18);
                }
            }
            else if (pid == -1)
            {
                writeToFile(STDERR_FILENO, "Error forking", errno);
                writeToLogger("Error forking.\n", 15);
            }
        }
        else if (strcmp(command, "showAll") == 0 && args[0] != NULL)
        {
            pid_t pid = fork();
            if (pid == 0)
            {
                writeToLogger("Showing all students.\n", 23);
                showAll(args[0]);
            }
            else if (pid == -1)
            {
                writeToFile(STDERR_FILENO, "Error forking", errno);
                writeToLogger("Error forking.\n", 15);
            }
        }
        else if (strcmp(command, "listGrades") == 0 && args[0] != NULL)
        {

            pid_t pid = fork();
            if (pid == 0)
            {
                writeToLogger("Listing grades.\n", 17);
                listGrades(args[0]);
            }
            else if (pid == -1)
            {
                writeToFile(STDERR_FILENO, "Error forking", errno);
                writeToLogger("Error forking.\n", 15);
            }
        }
        else if (strcmp(command, "listSome") == 0 && args[0] != NULL && args[1] != NULL && args[2] != NULL)
        {
            pid_t pid = fork();
            if (pid == 0)
            {
                writeToLogger("Listing some grades.\n", 22);
                listSome(atoi(args[0]), atoi(args[1]), args[2]);
            }
            else if (pid == -1)
            {
                writeToFile(STDERR_FILENO, "Error forking", errno);
                writeToLogger("Error forking.\n", 15);
            }
        }
        else if (strcmp(command, "sortAll") == 0 && args[0] != NULL && args[1] != NULL && args[2] != NULL)
        {

            pid_t pid = fork();
            if (pid == 0)
            {
                writeToLogger("Sorting all grades.\n", 21);
                sortAll(args[0], args[1], args[2]);
            }
            else if (pid == -1)
            {
                writeToFile(STDERR_FILENO, "Error forking", errno);
                writeToLogger("Error forking.\n", 15);
            }
        }
        else if (strcmp(command, "gtuStudentGrades") == 0 && args[0] != NULL)
        {
            pid_t pid = fork();
            if (pid == 0)
            {
                writeToLogger("Creating file.\n", 16);
                int fd = open(args[0], O_WRONLY | O_CREAT | O_EXCL, 0664);
                if (fd == -1)
                {
                    if (errno == EEXIST)
                    {
                        writeToFile(STDOUT_FILENO, "File already exists\n", 21);
                        writeToLogger("File already exists.\n", 21);
                    }
                    else
                    {
                        writeToFile(STDERR_FILENO, "Error creating file", errno);
                        writeToLogger("Error creating file.\n", 22);
                    }
                }
                else
                {
                    writeToFile(STDOUT_FILENO, "File created successfully\n", 27);
                    writeToLogger("File created successfully.\n", 28);
                }
                close(fd);
            }
            else if (pid == -1)
            {
                writeToFile(STDERR_FILENO, "Error forking", errno);
                writeToLogger("Error forking.\n", 15);
            }
        }
        else if (strcmp(command, "gtuStudentGrades") == 0 || strcmp(command, "gtuStudentGrades\n") == 0)
        {
            pid_t pid = fork();
            if (pid == 0)
            {
                writeToLogger("Usage printed.\n", 16);
                usage();
            }
            else if (pid == -1)
            {
                writeToFile(STDERR_FILENO, "Error forking", errno);
                writeToLogger("Error forking.\n", 15);
            }
        }
        else if (strcmp(command, "forkTester") == 0)
        {
            pid_t pid = fork();
            if (pid == 0)
            {
                forkTester();
            }
            else if (pid == -1)
            {
                writeToFile(STDERR_FILENO, "Error forking", errno);
                writeToLogger("Error forking.\n", 15);
            }
            else
            {
                writeToFile(STDOUT_FILENO, "Forked successfully\n", 20);
                writeToLogger("Forked successfully.\n", 21);
            }
        }
        else
        {

            pid_t pid = fork();
            if (pid == 0)
            {
                writeToFile(STDERR_FILENO, "Error: Invalid command\n", 23);
                writeToLogger("Usage printed.\n", 16);
                usage();
            }
            else if (pid == -1)
            {
                writeToFile(STDERR_FILENO, "Error forking", errno);
                writeToLogger("Error forking.\n", 15);
            }
        }
    }

    return 0;
}
