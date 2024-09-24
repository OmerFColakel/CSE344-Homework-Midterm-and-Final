#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>

#define MAX_PICKUP 4 // Maximum number of pickup cars that can be parked in temporary lot
#define MAX_AUTO 8   // Maximum number of auto cars that can be parked in temporary lot

typedef struct //  Structure to represent a car
{
    int id;   // Unique identifier for the car
    int type; // 'P' for pickup, 'A' for auto
} Car;

pthread_mutex_t newPickup;      //  Mutex to handle new pickup cars
pthread_mutex_t newAuto;        //  Mutex to handle new auto cars
pthread_mutex_t inChargePickup; //  Mutex to handle parking of pickup cars
pthread_mutex_t inChargeAuto;   //  Mutex to handle parking of auto cars

int carID = 0;
int tempPickupCount = 0;
int tempAutoCount = 0;

void *carOwner(void *arg);     //  Function to handle car owners
void *carAttendant(void *arg); //  Function to handle parking of cars
void sigINTHandler(int sig);   //  Signal handler for SIGINT
void handleExit();             //  Clean up mutexes
void showParkingStatus();      //  Display current parking status

int main()
{

    //  Register signal handler for SIGINT (Ctrl+C)
    //  The only way to exit the program is by sending SIGINT
    struct sigaction sa;
    sa.sa_handler = sigINTHandler;
    sigaction(SIGINT, &sa, NULL);

    //  Create a thread for the car attendant and keep creating threads for car owners
    pthread_t attendantThread;
    pthread_t ownerThread;

    //  Initialize mutexes for handling new cars and parking of cars
    pthread_mutex_init(&newPickup, NULL);
    pthread_mutex_init(&newAuto, NULL);
    pthread_mutex_init(&inChargePickup, NULL);
    pthread_mutex_init(&inChargeAuto, NULL);

    //  Create thread for car attendant
    int pickup = 0, autoCar = 1;
    pthread_create(&attendantThread, NULL, carAttendant, &pickup);
    pthread_create(&attendantThread, NULL, carAttendant, &autoCar);

    //  Keep creating threads for car owners
    while (1)
    {
        int randTime = rand() % 3;                          //  Random time between 0 and 2 seconds
        pthread_create(&ownerThread, NULL, carOwner, NULL); //  Create thread for car owner
        sleep(randTime);                                    //  Sleep random time between 0 and 2 seconds
    }

    //  Wait for the car attendant thread to finish
    pthread_join(attendantThread, NULL);

    //  Clean up mutexes
    handleExit();
    return 0;
}

void *carOwner(void *arg)
{
    Car car;

    car.type = (rand() % 2); //  Randomly assign type of car

    if (car.type == 0) //  Handle new pickup car
    {
        pthread_mutex_lock(&newPickup);   //  Lock mutex for new pickup cars
        car.id = carID++;                 //  Assign unique ID to the car
        if (tempPickupCount < MAX_PICKUP) //  Check if there is space for new pickup car
        {
            tempPickupCount++;                                //  Increment count of pickup cars
            printf("Car Owner: New Pickup Car %d\n", car.id); //  Display message for new pickup car
            showParkingStatus();                              //  Display current parking status
            pthread_mutex_unlock(&newPickup);                 //  Unlock mutex for new pickup cars
        }
        else
        {
            pthread_mutex_unlock(&newPickup);                          //  Unlock mutex for new pickup cars
            printf("Car Owner: No space for Pickup Car %d\n", car.id); //  Display message if no space for new pickup car
        }
    }
    else
    {
        pthread_mutex_lock(&newAuto); //  Lock mutex for new auto cars
        car.id = carID++;             //  Assign unique ID to the car
        if (tempAutoCount < MAX_AUTO) //  Check if there is space for new auto car
        {
            tempAutoCount++; //  Increment count of auto cars
            printf("Car Owner: New Auto Car %d\n", car.id);
            showParkingStatus();
            pthread_mutex_unlock(&newAuto); //  Unlock mutex for new auto cars
        }
        else
        {
            pthread_mutex_unlock(&newAuto);                          //  Unlock mutex for new auto cars
            printf("Car Owner: No space for Auto Car %d\n", car.id); //  Display message if no space for new auto car
        }
    }

    return NULL;
}

void *carAttendant(void *arg)
{
    //  Take the type from argument
    int typeOfAttendant = *((int *)arg);
    while (1)
    {

        if (typeOfAttendant == 0) //  Handle pickup cars
        {
            pthread_mutex_lock(&inChargePickup); //  Lock mutex for parking of pickup cars
            if (tempPickupCount > 0)             //  Check if there are pickup cars to park
            {
                tempPickupCount--; //  Decrement count of pickup cars
                printf("Car Attendant: Pickup Car parked\n");
                showParkingStatus();
            }
            pthread_mutex_unlock(&inChargePickup); //  Unlock mutex for parking of pickup cars
        }
        else //  Handle auto cars
        {
            pthread_mutex_lock(&inChargeAuto); //  Lock mutex for parking of auto cars
            if (tempAutoCount > 0)             //  Check if there are auto cars to park
            {
                tempAutoCount--; //  Decrement count of auto cars
                printf("Car Attendant: Auto Car parked\n");
                showParkingStatus();
            }
            pthread_mutex_unlock(&inChargeAuto); //  Unlock mutex for parking of auto cars
        }
        sleep(3); // Simulate additional time taken for other duties
    }
    return NULL;
}

void sigINTHandler(int sig) //  Signal handler for SIGINT
{
    printf("\nInterrupt signal received\n");

    handleExit();
    exit(0); //  Exit the program
}

void handleExit() //  Clean up mutexes
{
    printf("Destroying mutexes\n");
    pthread_mutex_destroy(&newPickup);
    pthread_mutex_destroy(&newAuto);
    pthread_mutex_destroy(&inChargePickup);
    pthread_mutex_destroy(&inChargeAuto);
}

void showParkingStatus() //  Display current parking status
{
    printf("Temp Pickup Count: %d/%d, Temp Auto Count: %d/%d\n", tempPickupCount, MAX_PICKUP, tempAutoCount, MAX_AUTO);
}