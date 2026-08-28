/*
 * runner.c - a tiny stopwatch for benchmarks/run.sh.
 *
 * `cc` is the one tool we can rely on being present; `date +%N`, python3,
 * and hyperfine are not. This runs a command K times after one discarded
 * warmup and reports min/median/mean wall time via CLOCK_MONOTONIC.
 *
 *   runner <runs> <label> -- <command> [args...]
 *   -> RESULT <label> min=<ms> median=<ms> mean=<ms> runs=<K>
 *
 * Child stdout/stderr go to /dev/null. A non-zero child exit aborts.
 */
#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1.0e6;
}

static int cmp_double(const void *a, const void *b) {
    double d = *(const double *)a - *(const double *)b;
    return (d > 0) - (d < 0);
}

static int run_once(char **cmd) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(1);
    }
    if (pid == 0) {
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
        }
        execvp(cmd[0], cmd);
        perror("execvp");
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        perror("waitpid");
        exit(1);
    }
    return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 0 : -1;
}

int main(int argc, char **argv) {
    if (argc < 5 || strcmp(argv[3], "--") != 0) {
        fprintf(stderr, "usage: %s <runs> <label> -- <command> [args...]\n", argv[0]);
        return 2;
    }

    int runs = atoi(argv[1]);
    const char *label = argv[2];
    char **cmd = &argv[4];
    if (runs < 1) {
        runs = 1;
    }

    if (run_once(cmd) != 0) {
        fprintf(stderr, "RESULT %s error=warmup-failed\n", label);
        return 1;
    }

    double *samples = calloc((size_t)runs, sizeof(double));
    if (!samples) {
        perror("calloc");
        return 1;
    }

    for (int i = 0; i < runs; i++) {
        double t0 = now_ms();
        if (run_once(cmd) != 0) {
            fprintf(stderr, "RESULT %s error=run-failed\n", label);
            free(samples);
            return 1;
        }
        samples[i] = now_ms() - t0;
    }

    qsort(samples, (size_t)runs, sizeof(double), cmp_double);
    double min = samples[0];
    double median = (runs % 2)
        ? samples[runs / 2]
        : (samples[runs / 2 - 1] + samples[runs / 2]) / 2.0;
    double sum = 0.0;
    for (int i = 0; i < runs; i++) {
        sum += samples[i];
    }
    double mean = sum / runs;
    free(samples);

    printf("RESULT %s min=%.3f median=%.3f mean=%.3f runs=%d\n",
           label, min, median, mean, runs);
    return 0;
}
