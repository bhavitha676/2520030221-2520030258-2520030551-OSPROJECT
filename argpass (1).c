#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_LINE 2048
#define MAX_ARGS 128

static void show_proc_file(const char *path, int nul_mode) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "Cannot open %s: %s\n", path, strerror(errno));
        return;
    }
    char buf[512];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < n; ++i) {
            if (buf[i] == '\0') { if (nul_mode == 1) putchar(' '); else if (nul_mode == 2) putchar('\n'); }
            else putchar(buf[i]);
        }
    }
    close(fd);
    putchar('\n');
}

static int child_report(int argc, char **argv, char **envp) {
    (void)envp;
    puts("\n--- Executed child program ---");
    printf("PID: %ld\nargc: %d\n", (long)getpid(), argc);
    for (int i = 0; i < argc; ++i)
        printf("argv[%d] = [%s]\n", i, argv[i]);
    const char *v = getenv("DEMO_VAR");
    printf("DEMO_VAR = [%s]\n", v ? v : "<not set>");
    puts("/proc/self/cmdline (NULs shown as spaces):");
    show_proc_file("/proc/self/cmdline", 1);
    puts("/proc/self/environ (NULs shown as newlines):");
    show_proc_file("/proc/self/environ", 2);
    puts("--- Child finished ---\n");
    return 0;
}

static int run_demo(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s demo <arg1> [arg2 ...]\n", argv[0]);
        return 1;
    }
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return 1; }
    if (pid == 0) {
        char *child_argv[MAX_ARGS];
        int count = 0;
        child_argv[count++] = (char *)"argpass-child";
        child_argv[count++] = (char *)"--child";
        for (int i = 2; i < argc && count < MAX_ARGS - 1; ++i)
            child_argv[count++] = argv[i];
        child_argv[count] = NULL;
        char *child_env[] = { (char *)"PATH=/usr/bin:/bin", (char *)"DEMO_VAR=passed_from_parent", (char *)"LANG=C", NULL };
        execve("/proc/self/exe", child_argv, child_env);
        perror("execve");
        _exit(127);
    }
    int status;
    if (waitpid(pid, &status, 0) < 0) { perror("waitpid"); return 1; }
    printf("Parent: child %ld ", (long)pid);
    if (WIFEXITED(status)) printf("exited with status %d\n", WEXITSTATUS(status));
    else if (WIFSIGNALED(status)) printf("was terminated by signal %d\n", WTERMSIG(status));
    return 0;
}

static int pipe_demo(void) {
    int fds[2];
    if (pipe(fds) < 0) { perror("pipe"); return 1; }
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); close(fds[0]); close(fds[1]); return 1; }
    if (pid == 0) {
        close(fds[1]);
        char msg[256];
        ssize_t n = read(fds[0], msg, sizeof(msg) - 1);
        if (n < 0) { perror("read"); close(fds[0]); _exit(1); }
        msg[n] = '\0';
        dprintf(STDOUT_FILENO, "Child received through anonymous pipe: %s\n", msg);
        close(fds[0]);
        _exit(0);
    }
    close(fds[0]);
    const char *message = "Arguments and environment travel through exec; this message travels through a pipe.";
    if (write(fds[1], message, strlen(message)) < 0) perror("write");
    close(fds[1]);
    int status;
    if (waitpid(pid, &status, 0) < 0) perror("waitpid");
    return 0;
}

static int options_demo(int argc, char **argv) {
    int opt, number = 1, verbose = 0;
    optind = (!strcmp(argv[0], "options")) ? 1 : 2; /* shell argv starts with command; program argv starts with executable */
    while ((opt = getopt(argc, argv, "n:v")) != -1) {
        switch (opt) {
            case 'n':
                number = atoi(optarg);
                if (number < 1 || number > 20) {
                    fprintf(stderr, "-n must be from 1 to 20\n");
                    return 1;
                }
                break;
            case 'v': verbose = 1; break;
            default:
                fprintf(stderr, "Usage: %s options [-v] [-n count]\n", argv[0]);
                return 1;
        }
    }
    for (int i = 1; i <= number; ++i) {
        printf("Option demonstration: iteration %d of %d\n", i, number);
        if (verbose) puts("  -v enabled: extra detail displayed");
    }
    return 0;
}

/* Splits a command into tokens in place; supports single/double quotes and backslash escapes. */
static int tokenize(char *line, char **args) {
    int argc = 0;
    char *src = line, *dst = line;
    while (*src) {
        while (isspace((unsigned char)*src)) ++src;
        if (!*src) break;
        if (argc >= MAX_ARGS - 1) { fprintf(stderr, "Too many arguments (limit %d).\n", MAX_ARGS - 1); return -1; }
        args[argc++] = dst;
        char quote = 0;
        while (*src) {
            if (!quote && isspace((unsigned char)*src)) break;
            if ((*src == '\'' || *src == '"') && (!quote || quote == *src)) {
                if (quote) quote = 0; else quote = *src;
                ++src;
                continue;
            }
            if (*src == '\\' && src[1]) { ++src; *dst++ = *src++; continue; }
            *dst++ = *src++;
        }
        if (quote) { fprintf(stderr, "Unmatched quote in command.\n"); return -1; }
        if (*src) ++src; /* move past delimiter before writing NUL in-place */
        *dst++ = '\0';
        while (isspace((unsigned char)*src)) ++src;
    }
    args[argc] = NULL;
    return argc;
}

static void print_help(void) {
    puts("Commands:\n"
         "  demo <args...>       fork + execve with custom argv/envp; inspect /proc\n"
         "  pipe                 send a message parent-to-child through an anonymous pipe\n"
         "  options [-v] [-n N]  demonstrate getopt option parsing (N: 1..20)\n"
         "  help                 show this help\n"
         "  exit                 quit the mini-shell\n"
         "  Any other command    execute a Linux program using fork + execvp\n"
         "Quote text with spaces, e.g. echo \"hello Linux world\"");
}

static int mini_shell(void) {
    char line[MAX_LINE];
    char *args[MAX_ARGS];
    puts("Argument Passing Mini-Shell (C / Linux). Type 'help' for commands.");
    for (;;) {
        printf("argpass$ "); fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) { putchar('\n'); break; }
        line[strcspn(line, "\n")] = '\0';
        int argc = tokenize(line, args);
        if (argc <= 0) continue;
        if (!strcmp(args[0], "exit") || !strcmp(args[0], "quit")) break;
        if (!strcmp(args[0], "help")) { print_help(); continue; }
        if (!strcmp(args[0], "demo")) { run_demo(argc, args); continue; }
        if (!strcmp(args[0], "pipe")) { pipe_demo(); continue; }
        if (!strcmp(args[0], "options")) { options_demo(argc, args); continue; }

        pid_t pid = fork();
        if (pid < 0) { perror("fork"); continue; }
        if (pid == 0) {
            execvp(args[0], args);
            fprintf(stderr, "%s: %s\n", args[0], strerror(errno));
            _exit(errno == ENOENT ? 127 : 126);
        }
        int status;
        if (waitpid(pid, &status, 0) < 0) perror("waitpid");
        else if (WIFEXITED(status)) printf("[child exit status: %d]\n", WEXITSTATUS(status));
        else if (WIFSIGNALED(status)) printf("[child terminated by signal: %d]\n", WTERMSIG(status));
    }
    puts("Mini-shell closed.");
    return 0;
}

int main(int argc, char **argv, char **envp) {
    if (argc >= 2 && !strcmp(argv[1], "--child")) return child_report(argc, argv, envp);
    if (argc >= 2 && !strcmp(argv[1], "demo")) return run_demo(argc, argv);
    if (argc >= 2 && !strcmp(argv[1], "pipe")) return pipe_demo();
    if (argc >= 2 && !strcmp(argv[1], "options")) return options_demo(argc, argv);
    if (argc >= 2 && !strcmp(argv[1], "shell")) return mini_shell();
    if (argc >= 2 && !strcmp(argv[1], "help")) { print_help(); return 0; }
    if (argc == 1) return mini_shell();
    fprintf(stderr, "Usage: %s [shell|demo <args...>|pipe|options [-v] [-n N]|help]\n", argv[0]);
    return 1;
}
