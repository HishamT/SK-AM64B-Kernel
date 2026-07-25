#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#define HELLO_IOCTL_CLEAR _IO('h', 1)

int main(void)
{
  int fd = open("/dev/hello", O_RDWR);

  if (fd < 0) {
    perror("open");
    return 1;
  }

  if (ioctl(fd, HELLO_IOCTL_CLEAR) < 0) {
    perror("ioctl");
    close(fd);
    return 1;
  }

  close(fd);
  return 0;
}