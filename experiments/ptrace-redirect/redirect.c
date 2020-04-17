#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include <sys/ptrace.h>
#include <sys/reg.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/user.h>
#include <fcntl.h>
#include <linux/limits.h>

static void process_signals(pid_t child);
static int wait_for_open(pid_t child);
static void read_file(pid_t child, char *file);
static void redirect_file(pid_t child, const char *file);

int main(int argc, char **argv)
{
    pid_t pid;
    int status;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <prog> <arg1> ... <argN>\n", argv[0]);
        return 1;
    }

    if ((pid = fork()) == 0) {
        ptrace(PTRACE_TRACEME, 0, 0, 0);
        kill(getpid(), SIGSTOP);
        return execvp(argv[1], argv + 1);
    } else {
        waitpid(pid, &status, 0);
        ptrace(PTRACE_SETOPTIONS, pid, 0, PTRACE_O_TRACESYSGOOD);
        process_signals(pid);
        return 0;
    }
}

static void process_signals(pid_t child)
{
    const char *suffix = ".__bk";
    const char *prefix = "/tmp/our_cs244b_test_13245646/";

    while(1) {
        char orig_file[PATH_MAX];
        char new_file[PATH_MAX];

        /* Wait for open syscall start */
        if (wait_for_open(child) != 0) break;

        /* Find out file and re-direct if it is the target */

        read_file(child, orig_file);
        printf("old: %s\n", orig_file);

        if (strncmp(prefix, orig_file, strlen(prefix)) == 0) {
            if (strlen(orig_file) + strlen(suffix) + 10 < PATH_MAX) {
                strcpy(new_file, orig_file);
                strcpy(new_file + strlen(orig_file), suffix);
                new_file[strlen(orig_file) + strlen(suffix) + 1] = 0;
            } else {
                fprintf(stderr, "too big of an argument rip\n");
                exit(1);
            }
            printf("to write: %s\n", new_file);

            redirect_file(child, new_file);
            read_file(child, new_file);
            printf("new: %s\n", new_file);
        }


        /* Wait for open syscall exit */
        if (wait_for_open(child) != 0) break;
    }
}

static int wait_for_open(pid_t child)
{
    int status;

    while (1) {
        ptrace(PTRACE_SYSCALL, child, 0, 0);
        waitpid(child, &status, 0);
        /* Is it the openat syscall (sycall number 257 in x86_64)? */
        if (WIFSTOPPED(status) && WSTOPSIG(status) & 0x80 &&
            ptrace(PTRACE_PEEKUSER, child, sizeof(long)*ORIG_RAX, 0) == 257)
            return 0;
        if (WIFEXITED(status))
            return 1;
    }
}

static void read_file(pid_t child, char *file)
{
    char *child_addr;
    int i;

    child_addr = (char *) ptrace(PTRACE_PEEKUSER, child, sizeof(long)*RSI, 0);

    do {
        long val;
        char *p;

        val = ptrace(PTRACE_PEEKTEXT, child, child_addr, NULL);
        if (val == -1) {
            fprintf(stderr, "PTRACE_PEEKTEXT error: %s", strerror(errno));
            exit(1);
        }
        child_addr += sizeof (long);

        p = (char *) &val;
        for (i = 0; i < sizeof (long); ++i, ++file) {
            *file = *p++;
            if (*file == '\0') break;
        }
    } while (i == sizeof (long));
}

static void redirect_file(pid_t child, const char *file)
{
    char *stack_addr, *file_addr;

    stack_addr = (char *) ptrace(PTRACE_PEEKUSER, child, sizeof(long)*RSP, 0);
    /* Move further of red zone and make sure we have space for the file name */
    stack_addr -= 128 + PATH_MAX;
    file_addr = stack_addr;

    /* Write new file in lower part of the stack */
    do {
        int i;
        char val[sizeof (long)];

        for (i = 0; i < sizeof (long); ++i, ++file) {
            val[i] = *file;
            if (*file == '\0') break;
        }

        ptrace(PTRACE_POKETEXT, child, stack_addr, *(long *) val);
        stack_addr += sizeof (long);
    } while (*file);

    /* Change argument to open */
    ptrace(PTRACE_POKEUSER, child, sizeof(long)*RSI, file_addr);
}
