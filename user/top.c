#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"

static void
snapshot(int round)
{
  int fd, n;
  char buf[1024];

  printf("\n=== top snapshot %d (uptime %d ticks) ===\n", round, uptime());
  fd = open("/proc/processes", O_RDONLY);
  if (fd < 0) {
    printf("top: cannot open /proc/processes\n");
    return;
  }
  while ((n = read(fd, buf, sizeof(buf) - 1)) > 0) {
    buf[n] = '\0';
    printf("%s", buf);
  }
  close(fd);
}

int
main(void)
{
  int i;

  for (i = 1; i <= 5; i++) {
    snapshot(i);
    if (i < 5)
      pause(20);
  }
  exit(0);
}
