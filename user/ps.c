#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"

int
main(void)
{
  int fd, n;
  char buf[1024];

  fd = open("/proc/processes", O_RDONLY);
  if (fd < 0) {
    printf("ps: cannot open /proc/processes\n");
    exit(1);
  }
  while ((n = read(fd, buf, sizeof(buf) - 1)) > 0) {
    buf[n] = '\0';
    printf("%s", buf);
  }
  close(fd);
  exit(0);
}
