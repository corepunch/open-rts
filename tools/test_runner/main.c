#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    const char *name;
    char *command;
    pid_t pid;
    FILE *output;
    double started;
} test_t;

static double seconds(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec + now.tv_nsec / 1000000000.0;
}

static void report(test_t *test, int status, int timed_out) {
    printf("-- %s (%.1fs): %s --\n", test->name, seconds() - test->started,
           timed_out ? "TIMEOUT" : WIFEXITED(status) && !WEXITSTATUS(status) ? "PASS" : "FAIL");
    rewind(test->output);
    char buffer[4096];
    size_t count;
    while ((count = fread(buffer, 1, sizeof(buffer), test->output))) fwrite(buffer, 1, count, stdout);
    fclose(test->output);
    free(test->command);
    fflush(stdout);
    test->pid = 0;
}

int main(int argc, char **argv) {
    int jobs = 4, budget = 45, first = 1;
    while (first + 1 < argc && argv[first][0] == '-') {
        int *setting = !strcmp(argv[first], "-j") ? &jobs : !strcmp(argv[first], "-t") ? &budget : NULL;
        if (!setting) break;
        *setting = atoi(argv[first + 1]);
        first += 2;
    }
    if (jobs < 1 || jobs > 32 || budget < 1 || first == argc) {
        fprintf(stderr, "usage: %s [-j 1..32] [-t seconds] test...\n", argv[0]);
        return 2;
    }
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    test_t *tests = calloc((size_t)(argc - first), sizeof(*tests));
    if (!tests) return 2;
    int next = first, running = 0, failures = 0;
    while (next < argc || running) {
        while (next < argc && running < jobs) {
            test_t *test = &tests[next - first];
            test->name = argv[next++];
            test->command = strdup(test->name);
            if (!test->command) { free(tests); return 2; }
            test->output = tmpfile();
            if (!test->output) { perror("tmpfile"); free(tests); return 2; }
            fflush(NULL);
            test->started = seconds();
            test->pid = fork();
            if (test->pid == 0) {
                setpgid(0, 0);
                dup2(fileno(test->output), STDOUT_FILENO);
                dup2(fileno(test->output), STDERR_FILENO);
                char *argument = strchr(test->command, '@');
                if (argument) *argument++ = '\0';
                execl(test->command, test->command, argument, (char *)NULL);
                perror(test->command);
                _exit(127);
            }
            if (test->pid < 0) { perror("fork"); fclose(test->output); free(tests); return 2; }
            setpgid(test->pid, test->pid);
            running++;
        }
        for (int i = first; i < next; ++i) {
            test_t *test = &tests[i - first];
            if (!test->pid) continue;
            int status = 0;
            pid_t done = waitpid(test->pid, &status, WNOHANG);
            if (done == 0 && seconds() - test->started >= budget) {
                kill(-test->pid, SIGKILL);
                waitpid(test->pid, &status, 0);
                report(test, status, 1);
                failures++;
                running--;
            } else if (done == test->pid) {
                if (!WIFEXITED(status) || WEXITSTATUS(status)) failures++;
                report(test, status, 0);
                running--;
            } else if (done < 0 && errno != EINTR) {
                perror("waitpid");
                failures++;
                test->pid = 0;
                running--;
            }
        }
        if (running) nanosleep(&(struct timespec){.tv_nsec = 10000000}, NULL);
    }
    printf("%d tests, %d failed\n", argc - first, failures);
    free(tests);
    return failures ? 1 : 0;
}
