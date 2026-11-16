// A minimal stand-in for /usr/bin/time: runs a command and reports what the
// kernel measured for it.
//
// usage: measure <command> [args...]      (stdin/stdout are passed through)
// prints to stderr: "<wall seconds> <cpu seconds> <peak resident set, KiB>"
//
// This process stays tiny on purpose: on Linux a child's peak-RSS counter
// starts from its parent's, so a large launcher would inflate the reading.
#include <chrono>
#include <cstdio>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: measure <command> [args...]\n");
        return 2;
    }
    const auto start = std::chrono::steady_clock::now();
    const pid_t child = fork();
    if (child < 0) {
        std::perror("fork");
        return 2;
    }
    if (child == 0) {
        execvp(argv[1], argv + 1);
        std::perror("exec");
        _exit(127);
    }
    int status = 0;
    rusage usage{};
    if (wait4(child, &status, 0, &usage) < 0) {
        std::perror("wait4");
        return 2;
    }
    const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const auto seconds = [](const timeval& t) { return static_cast<double>(t.tv_sec) + static_cast<double>(t.tv_usec) / 1e6; };
    std::fprintf(stderr, "%.3f %.3f %ld\n", wall, seconds(usage.ru_utime) + seconds(usage.ru_stime), usage.ru_maxrss);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}
