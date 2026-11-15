#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(void)
{
  int i;
  volatile uint64 work;

  for (i = 0; i < 8; i++) {
    if (fork() == 0) {
      if (i % 3 == 0) {
        pause(200);
      } else if (i % 3 == 1) {
        for (work = 0; work < 200000000; work++)
          ;
      } else {
        for (work = 0; work < 6; work++) {
          pause(20);
          for (uint64 j = 0; j < 30000000; j++)
            ;
        }
      }
      exit(0);
    }
  }

  pause(100);
  while (wait(0) >= 0)
    ;
  exit(0);
}
