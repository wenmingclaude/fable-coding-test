// measure -- run a command and report its wall time, CPU time and peak
// memory, as seen by the operating system (wait4), not by the program.
//
//   measure LABEL COMMAND [ARGS...]      (stdin/stdout are passed through)
//
// Output, on stderr:   LABEL  wall 0.412 s  cpu 0.398 s  peak 181.3 MiB
#include <chrono>
#include <cstdio>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s LABEL COMMAND [ARGS...]\n", argv[0]);
        return 2;
    }
    const auto start = std::chrono::steady_clock::now();
    const pid_t child = fork();
    if (child < 0) return 2;
    if (child == 0) {
        execvp(argv[2], argv + 2);
        std::perror(argv[2]);
        _exit(127);
    }
    int status = 0;
    rusage usage{};
    if (wait4(child, &status, 0, &usage) < 0) return 2;
    const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const double cpu = static_cast<double>(usage.ru_utime.tv_sec + usage.ru_stime.tv_sec) +
                       static_cast<double>(usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) / 1e6;
    std::fprintf(stderr, "%-34s wall %6.3f s  cpu %6.3f s  peak %7.1f MiB\n", argv[1], wall, cpu,
                 static_cast<double>(usage.ru_maxrss) / 1024.0);  // ru_maxrss is in KiB on Linux
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}
