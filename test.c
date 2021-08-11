#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

int main() {
    #if 0
    int fd = open("remote_file.ibd", O_CREAT | O_RDWR, 0644);
    perror("");
    printf("got fd %d\n", fd);
    printf("Wrote %zd\n", pwrite(fd, "abc", 3, 0));
    perror("");
    char buf[4] = {};
    ssize_t numread = pread(fd, buf, 3, 0);
    printf("Read %zd bytes\n", numread);
    printf("Buf is %s\n", buf);
    perror("");
    #endif
    printf("%d %d %d", F_GETPATH, F_GETFL, F_SETFL);
    // TEST WHAT SYSCALLS fopen, fgets does
}
