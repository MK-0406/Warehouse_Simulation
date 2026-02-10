#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <time.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>

// Light Pastel Color Codes
#define COLOR_TRAILER  "\x1b[38;5;117m"  // Light blue
#define COLOR_SECURITY "\x1b[38;5;223m"  // Light peach
#define COLOR_FORKLIFT "\x1b[38;5;157m"  // Light green
#define COLOR_JOIN     "\x1b[38;5;219m"  // Light pink
#define COLOR_RESET    "\x1b[0m"

// Constants
#define MIN_TRAILERS 3
#define MAX_TRAILERS 10
#define CONTAINERS_PER_TRAILER 2
#define MIN_ARRIVAL_TIME 3
#define MAX_ARRIVAL_TIME 4
#define SECURITY_CHECK_MIN 2
#define SECURITY_CHECK_MAX 3
#define UNLOAD_TIME_MIN 2
#define UNLOAD_TIME_MAX 4
#define FORKLIFT_MOVE_TIME 3
#define FORKLIFT_IDLE_TIMEOUT 10
#define LOADING_BAYS 2
#define FORKLIFTS 2

// Shared resources
sem_t loading_bays;    
sem_t containers_available; 
sem_t security_request;  
sem_t security_response;

int total_trailers; 
int remaining_containers = 0;
int current_trailer_id = 0; 
bool security_active = true;

pthread_mutex_t containers_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t print_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t trailer_id_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Prints a message with colored formatting */
void print_message(const char* message, const char* color) {
    pthread_mutex_lock(&print_mutex);
    printf("%s%s%s\n", color, message, COLOR_RESET);
    pthread_mutex_unlock(&print_mutex);
}

/* Generates random number between min and max */
int random_range(int min, int max) {
    return min + rand() % (max - min + 1);
}

/* Security officer thread - checks arriving trailers */
void* security_thread(void* arg) {

    do {
        print_message("Security: Standy", COLOR_SECURITY);
        sem_wait(&security_request); // Wait for trailer to request check
        if (!security_active) break;

        pthread_mutex_lock(&trailer_id_mutex);
        int id = current_trailer_id; // Capture the current trailer ID
        pthread_mutex_unlock(&trailer_id_mutex);

        char message[100];
        snprintf(message, sizeof(message), "Security: Checking trailer-%d", id);
        print_message(message, COLOR_SECURITY);

        sleep(random_range(SECURITY_CHECK_MIN, SECURITY_CHECK_MAX)); // Simulate security check

        print_message("Security: Checked & Released", COLOR_SECURITY);
        sem_post (&security_response); // Notify that security check is done
    } while (current_trailer_id != total_trailers);

    print_message("Security: Exit", COLOR_SECURITY);
    print_message("Security joined", COLOR_JOIN);
    return NULL;
}

/* Forklift thread - moves containers to warehouse */
void* forklift_thread(void* arg) {
    int id = (int)(intptr_t)arg;
    char message[100];
    bool first_wait = true;
    struct timespec last_activity;
    clock_gettime(CLOCK_MONOTONIC, &last_activity);

    while (1) {
        // Check idle timeout
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        double elapsed = now.tv_sec - last_activity.tv_sec;
        
        if (elapsed >= FORKLIFT_IDLE_TIMEOUT) {
            snprintf(message, sizeof(message), "Forklift-%d: Time out. Exit", id);
            print_message(message, COLOR_FORKLIFT);
            break;
        }

        // Try to get a container with timeout
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 1; // 1 second timeout

        if (sem_timedwait(&containers_available, &ts) == -1) {
            if (errno == ETIMEDOUT) {
                continue; // No container available, check idle time again
            }
            break; // Other error
        }

        if (first_wait) {
            snprintf(message, sizeof(message), "Forklift-%d: Waiting for containers", id);
            print_message(message, COLOR_FORKLIFT);
            first_wait = false;
        }

        snprintf(message, sizeof(message), "Forklift-%d: Moving a container", id);
        print_message(message, COLOR_FORKLIFT);

        pthread_mutex_lock(&containers_mutex);
        remaining_containers--;
        pthread_mutex_unlock(&containers_mutex);

        snprintf(message, sizeof(message), "Forklift-%d: Remaining = %d", id, remaining_containers);
        print_message(message, COLOR_FORKLIFT);

        sleep(FORKLIFT_MOVE_TIME);
        clock_gettime(CLOCK_MONOTONIC, &last_activity);
    }
    return NULL;
}

/* Trailer thread - arrives, gets checked, unloads */
void* trailer_thread(void* arg) {
    int id = (int)(intptr_t)arg;
    char message[100];

    snprintf(message, sizeof(message), "Trailer-%d: Arrived.", id);
    print_message(message, COLOR_TRAILER);

    snprintf(message, sizeof(message), "Trailer-%d: Under checking...", id);
    print_message(message, COLOR_TRAILER);

    // Request security check
    pthread_mutex_lock(&trailer_id_mutex);
    current_trailer_id = id;
    pthread_mutex_unlock(&trailer_id_mutex);

    sem_post(&security_request);  // Notify security
    sem_wait(&security_response); // Wait for security check to complete

    snprintf(message, sizeof(message), "Trailer-%d: Waiting for loading bay...", id);
    print_message(message, COLOR_TRAILER);

    // Make containers available for forklifts
    pthread_mutex_lock(&containers_mutex);
    remaining_containers += CONTAINERS_PER_TRAILER;
    pthread_mutex_unlock(&containers_mutex);

    sem_wait(&loading_bays); // Wait for available bay

    snprintf(message, sizeof(message), "Trailer-%d: Total containers = %d", id, remaining_containers);
    print_message(message, COLOR_TRAILER);

    sleep(random_range(UNLOAD_TIME_MIN, UNLOAD_TIME_MAX)); // Simulate unloading

    for (int i = 0; i < CONTAINERS_PER_TRAILER; i++) {
        sem_post(&containers_available);
    }
    snprintf(message, sizeof(message), "Trailer-%d: Unloaded. Leaving...", id);
    print_message(message, COLOR_TRAILER);

    sem_post(&loading_bays); // Release loading bay
    return NULL;
}

int main(int argc, char* argv[]) {
    // Validate command line arguments
    if (argc != 2) {
        printf("Usage: %s <number_of_trailers (%d-%d)>\n", argv[0], MIN_TRAILERS, MAX_TRAILERS);
        return 1;
    }

    total_trailers = atoi(argv[1]);
    if (total_trailers < MIN_TRAILERS || total_trailers > MAX_TRAILERS) {
        printf("Number of trailers must be between %d and %d\n", MIN_TRAILERS, MAX_TRAILERS);
        return 1;
    }

    srand(time(NULL)); // Initialize random seed
    printf("Total number of trailers: %d\n", total_trailers);

    // Initialize semaphores
    if (sem_init(&loading_bays, 0, LOADING_BAYS) != 0 ||
        sem_init(&containers_available, 0, 0) != 0 ||
        sem_init(&security_request, 0, 0) != 0 ||
        sem_init(&security_response, 0, 0) != 0) {
        perror("Semaphore initialization failed");
        return 1;
    }

    // Initialize mutexes
    if (pthread_mutex_init(&containers_mutex, NULL) != 0) {
        perror("Mutex initialization failed");
        return 1;
    }
    if (pthread_mutex_init(&print_mutex, NULL) != 0) {
        perror("Mutex initialization failed");
        return 1;
    }
    if (pthread_mutex_init(&trailer_id_mutex, NULL) != 0) {
        perror("Mutex initialization failed");
        return 1;
    }

    // Create security thread
    pthread_t security;
    if (pthread_create(&security, NULL, security_thread, NULL) != 0) {
        perror("Failed to create security thread");
        return 1;
    }

    // Create forklift threads
    pthread_t forklifts[FORKLIFTS];
    for (int i = 0; i < FORKLIFTS; i++) {
        if (pthread_create(&forklifts[i], NULL, forklift_thread, (void*)(intptr_t)(i + 1)) != 0) {
            perror("Failed to create forklift thread");
            return 1;
        }
    }

    // Create trailer threads with random arrival times between 3-4 seconds
    pthread_t trailers[MAX_TRAILERS];
    for (int i = 0; i < total_trailers; i++) {
        if (pthread_create(&trailers[i], NULL, trailer_thread, (void*)(intptr_t)(i + 1)) != 0) {
            perror("Failed to create trailer thread");
            return 1;
        }
        sleep(random_range(MIN_ARRIVAL_TIME, MAX_ARRIVAL_TIME));
    }

    // Wait for all trailers to complete
    for (int i = 0; i < total_trailers; i++) {
        pthread_join(trailers[i], NULL);
    }
    print_message("All trailers joined", COLOR_JOIN);

    // Shutdown security
    security_active = false; // Signal security thread to stop
    sem_post(&security_request); // Wake up security to exit
    pthread_join(security, NULL); // Wait for security thread to terminate

    //for (int i = 0; i < FORKLIFTS; i++) {
    //    sem_post(&containers_available); // Wake up forklifts if waiting
    //}
    for (int i = 0; i < FORKLIFTS; i++) {
        pthread_join(forklifts[i], NULL); // Wait for forklift threads to finish
    }
    print_message("All forklifts joined", COLOR_JOIN);

    // Clean up semaphores
    sem_destroy(&loading_bays);
    sem_destroy(&containers_available);
    sem_destroy(&security_request);
    sem_destroy(&security_response);

    // Destroy mutex locks
    pthread_mutex_destroy(&containers_mutex);
    pthread_mutex_destroy(&print_mutex);
    pthread_mutex_destroy(&trailer_id_mutex);

    print_message("Program terminates.", COLOR_JOIN);
    return 0;
}