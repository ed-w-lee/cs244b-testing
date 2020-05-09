#include <sys/auxv.h>
#include <sys/time.h>
#include <stdio.h>

int main(int argc, char *argv[]) {
    printf("vdso location: %lx\n", getauxval(AT_SYSINFO_EHDR));
    struct timeval tv;
    gettimeofday(&tv, NULL);
    printf("blah\n");
}
