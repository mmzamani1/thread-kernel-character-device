#define _GNU_SOURCE
#include <stdio.h>
#include <pthread.h>
#include <semaphore.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sched.h>
#include <sys/ioctl.h>
#include <time.h>

#define WRITER_COUNT 1
#define READER_COUNT 4
#define DEVICE_NAME "/dev/myQueue"
#define QUEUE_SIZE 30
#define IOCTL_GET_COUNT _IOR('q', 1, int)

sem_t full, empty, mutex;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t cond_mutex = PTHREAD_MUTEX_INITIALIZER;

char data_to_write;
char data_read;

int writer_done = 0;
int count;

void set_thread_affinity(int core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);

    if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) != 0) {
        perror("pthread_setaffinity_np failed");
    } else {
        printf("Thread %lu bound to core %d\n", (unsigned long)pthread_self(), core_id);
    }
}

void* writer() {
    int fd = open(DEVICE_NAME, O_WRONLY);
    if (fd < 0) {
        perror("Failed to open device");
        exit(EXIT_FAILURE);
    }

    if (ioctl(fd, IOCTL_GET_COUNT, &count) < 0) {
        perror("IOCTL failed");
    }

    srand(time(0));
    data_to_write = 'a' + (rand() % 26);

    while (count < QUEUE_SIZE - 1) {
        sem_wait(&empty);  // Wait for an empty slot in the queue
        sem_wait(&mutex);  // Ensure mutual exclusion while writing

        if (write(fd, &data_to_write, 1) < 0) {
            perror("Failed to write");
            exit(EXIT_FAILURE);
        }
        
        //printf("Writer %lu wrote: %c\n", (unsigned long)pthread_self(), data_to_write);

        sem_post(&mutex);  // Release mutual exclusion
        sem_post(&full);   // Signal that an item is available for reading
    }

    close(fd);

    // Mark writer as done
    writer_done = 1;

    // Signal readers if they're waiting
    pthread_cond_broadcast(&cond);

    return NULL;
}

void* reader() {
    int fd = open(DEVICE_NAME, O_RDONLY);
    if (fd < 0) {
        perror("Failed to open device");
        exit(EXIT_FAILURE);
    }

    while (1) {
        if (ioctl(fd, IOCTL_GET_COUNT, &count) < 0) {
            perror("IOCTL failed");
        }
        //printf("Queue count: %d\n", count);

        // Exit when writer is done and queue is empty
        pthread_mutex_lock(&cond_mutex);
        while (count == 0 && !writer_done) {
            // Wait until there is data or writer is done
            pthread_cond_wait(&cond, &cond_mutex);
        }
        pthread_mutex_unlock(&cond_mutex);

        // Check if we should exit
        if (writer_done && count == 0) {
            break;
        }

        sem_wait(&full);   // Wait for an item to be available for reading
        sem_wait(&mutex);  // Ensure mutual exclusion while reading

        if (read(fd, &data_read, 1) > 0) {
            //printf("Reader %lu read: %c\n", (unsigned long)pthread_self(), data_read);
        } else {
            perror("Failed to read");
        }

        sem_post(&mutex);  // Release mutual exclusion
        sem_post(&empty);  // Signal that space is available for writing
    }

    close(fd);
    return NULL;
}

int main() {
    pthread_t writerTid[WRITER_COUNT];
    pthread_t readerTid[READER_COUNT];
    cpu_set_t cpuset;
    struct timespec start, end;
    float time;

    // ########## Initialize semaphores ##########
    sem_init(&mutex, 0, 1);   // Mutex for mutual exclusion
    sem_init(&empty, 0, QUEUE_SIZE);  // Initially, queue is empty
    sem_init(&full, 0, 0);    // Initially, there are no items to read

    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset);

    clock_gettime(CLOCK_MONOTONIC, &start);

    // Create writer threads
    for (int i = 0; i < WRITER_COUNT; i++) {
        if (pthread_create(&writerTid[i], NULL, writer, NULL) != 0) {
            perror("Failed to create thread");
            return 1;
        }
        pthread_setaffinity_np(writerTid[i], sizeof(cpu_set_t), &cpuset);
    }

    // Create reader threads
    for (int i = 0; i < READER_COUNT; i++) {
        if (pthread_create(&readerTid[i], NULL, reader, NULL) != 0) {
            perror("Failed to create thread");
            return 1;
        }
        pthread_setaffinity_np(readerTid[i], sizeof(cpu_set_t), &cpuset);
    }

    // Join writer threads
    for (int i = 0; i < WRITER_COUNT; i++) {
        if (pthread_join(writerTid[i], NULL) != 0) {
            perror("Failed to join thread");
            return 1;
        }
    }

    // Join reader threads
    for (int i = 0; i < READER_COUNT; i++) {
        if (pthread_join(readerTid[i], NULL) != 0) {
            perror("Failed to join thread");
            return 1;
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    // Clean up semaphores and condition variable
    sem_destroy(&mutex);
    sem_destroy(&empty);
    sem_destroy(&full);
    pthread_mutex_destroy(&cond_mutex);
    pthread_cond_destroy(&cond);

    time = ((end.tv_sec - start.tv_sec) + ((end.tv_nsec - start.tv_nsec)) * 1e-9) * 1e3;
    printf("Single-core scenario: %fms\n", time);
    
    //###################################################################################################
    
    int cores_count = sysconf(_SC_NPROCESSORS_ONLN);

    // ########## Initialize semaphores ##########
    sem_init(&mutex, 0, 1);   // Mutex for mutual exclusion
    sem_init(&empty, 0, QUEUE_SIZE);  // Initially, queue is empty
    sem_init(&full, 0, 0);    // Initially, there are no items to read

    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset);

    clock_gettime(CLOCK_MONOTONIC, &start);

    // Create writer threads
    for (int i = 0; i < WRITER_COUNT; i++) {
        if (pthread_create(&writerTid[i], NULL, writer, NULL) != 0) {
            perror("Failed to create thread");
            return 1;
        }
        CPU_ZERO(&cpuset);
        CPU_SET(i % cores_count, &cpuset);
        pthread_setaffinity_np(writerTid[i], sizeof(cpu_set_t), &cpuset);
    }

    // Create reader threads
    for (int i = 0; i < READER_COUNT; i++) {
        if (pthread_create(&readerTid[i], NULL, reader, NULL) != 0) {
            perror("Failed to create thread");
            return 1;
        }
        CPU_ZERO(&cpuset);
        CPU_SET(i % cores_count, &cpuset);
        pthread_setaffinity_np(readerTid[i], sizeof(cpu_set_t), &cpuset);
    }

    // Join writer threads
    for (int i = 0; i < WRITER_COUNT; i++) {
        if (pthread_join(writerTid[i], NULL) != 0) {
            perror("Failed to join thread");
            return 1;
        }
    }

    // Join reader threads
    for (int i = 0; i < READER_COUNT; i++) {
        if (pthread_join(readerTid[i], NULL) != 0) {
            perror("Failed to join thread");
            return 1;
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    // Clean up semaphores and condition variable
    sem_destroy(&mutex);
    sem_destroy(&empty);
    sem_destroy(&full);
    pthread_mutex_destroy(&cond_mutex);
    pthread_cond_destroy(&cond);

    time = (end.tv_sec - start.tv_sec) + ((end.tv_nsec - start.tv_nsec)) * 1e-9;
    printf("multi-core scenario: %fs\n", time);


    return 0;
}
