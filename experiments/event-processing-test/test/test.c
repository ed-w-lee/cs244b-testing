#include <sys/auxv.h>
#include <sys/time.h>
#include <stdio.h>
#include <time.h>

int main(int argc, char *argv[]) {
    printf("vdso location: %lx\n", getauxval(AT_SYSINFO_EHDR));
    struct timespec tv;
    clock_gettime(CLOCK_MONOTONIC, &tv);
    printf("{sec: %ld, nsec: %ld}\n", tv.tv_sec, tv.tv_nsec);

    rename("/tmp/test", "/tmp/test2");
}
