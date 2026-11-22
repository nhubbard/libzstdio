#define _POSIX_C_SOURCE 200809L

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum { WORKER_COUNT = 8 };

struct worker {
  pthread_barrier_t *barrier;
  int pipefd[2];
  unsigned char expected;
  int failed;
};

static void check_pthread(int error, const char *operation) {
  if (error != 0) {
    fprintf(stderr, "%s: %s\n", operation, strerror(error));
    exit(EXIT_FAILURE);
  }
}

static void wait_for_workers(pthread_barrier_t *barrier) {
  const int result = pthread_barrier_wait(barrier);
  if (result != PTHREAD_BARRIER_SERIAL_THREAD) {
    check_pthread(result, "pthread_barrier_wait");
  }
}

static void *exercise_descriptor_io(void *argument) {
  struct worker *worker = argument;
  unsigned char actual = 0;

  wait_for_workers(worker->barrier);
  if (write(worker->pipefd[1], &worker->expected, 1) != 1) {
    perror("write");
    worker->failed = 1;
  }

  wait_for_workers(worker->barrier);
  // A failed write leaves this pipe empty. Keep reaching every barrier.
  if (!worker->failed && (read(worker->pipefd[0], &actual, 1) != 1 ||
                          actual != worker->expected)) {
    fputs("read: descriptor round trip failed\n", stderr);
    worker->failed = 1;
  }

  wait_for_workers(worker->barrier);
  if (close(worker->pipefd[0]) != 0) {
    perror("close read end");
    worker->failed = 1;
  }
  if (close(worker->pipefd[1]) != 0) {
    perror("close write end");
    worker->failed = 1;
  }

  return NULL;
}

int main(void) {
  pthread_barrier_t barrier;
  pthread_t threads[WORKER_COUNT];
  struct worker workers[WORKER_COUNT] = {0};

  check_pthread(pthread_barrier_init(&barrier, NULL, WORKER_COUNT),
                "pthread_barrier_init");

  for (size_t i = 0; i < WORKER_COUNT; ++i) {
    workers[i].barrier = &barrier;
    workers[i].expected = (unsigned char)('A' + i);
    if (pipe(workers[i].pipefd) != 0) {
      perror("pipe");
      return EXIT_FAILURE;
    }
  }

  for (size_t i = 0; i < WORKER_COUNT; ++i) {
    check_pthread(
        pthread_create(&threads[i], NULL, exercise_descriptor_io, &workers[i]),
        "pthread_create");
  }

  int status = EXIT_SUCCESS;
  for (size_t i = 0; i < WORKER_COUNT; ++i) {
    check_pthread(pthread_join(threads[i], NULL), "pthread_join");
    if (workers[i].failed) {
      status = EXIT_FAILURE;
    }
  }

  check_pthread(pthread_barrier_destroy(&barrier), "pthread_barrier_destroy");
  return status;
}