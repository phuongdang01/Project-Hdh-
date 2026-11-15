#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

void
assert_test(int condition, char *test_name)
{
  if (condition) {
    printf("[PASS] %s\n", test_name);
  } else {
    printf("[FAIL] %s\n", test_name);
  }
}

// Kiểm thử đọc nội dung file bất kỳ trong /proc
void
test_read_file(char *path, char *test_name)
{
  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    assert_test(0, test_name);
    return;
  }

  char buf[256];
  int n = read(fd, buf, sizeof(buf) - 1);
  close(fd);

  if (n > 0) {
    buf[n] = '\0';
    assert_test(1, test_name);
  } else {
    assert_test(0, test_name);
  }
}

// Kiểm thử trường hợp đọc với buffer dung lượng nhỏ (Chunked read)
void
test_small_buffer(void)
{
  int fd = open("/proc/uptime", O_RDONLY);
  if (fd < 0) {
    assert_test(0, "Doc voi buffer nho (Mơ file /proc/uptime)");
    return;
  }

  char buf[4];
  int total_read = 0;
  int n;

  while ((n = read(fd, buf, 3)) > 0) {
    total_read += n;
  }
  close(fd);

  assert_test(total_read > 0, "Doc voi buffer nho (Read chunking)");
}

void
test_processes(void)
{
  int fd = open("/proc/processes", O_RDONLY);
  char buf[512];
  int n;

  if (fd < 0) {
    assert_test(0, "Doc danh sach tien trinh tu struct proc");
    return;
  }
  n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if (n > 0) {
    buf[n] = '\0';
    printf("%s", buf);
    assert_test(1, "Doc danh sach tien trinh tu struct proc");
  } else {
    assert_test(0, "Doc danh sach tien trinh tu struct proc");
  }
}

void
append_number(char *buf, int *len, int value)
{
  char digits[16];
  int n = 0;

  if (value == 0) {
    buf[(*len)++] = '0';
    return;
  }
  while (value > 0) {
    digits[n++] = '0' + value % 10;
    value /= 10;
  }
  while (n > 0)
    buf[(*len)++] = digits[--n];
}

void
test_status(void)
{
  char path[32];
  char buf[256];
  int fd, n, len = 6;

  strcpy(path, "/proc/");
  append_number(path, &len, getpid());
  strcpy(path + len, "/status");
  fd = open(path, O_RDONLY);
  if (fd < 0) {
    assert_test(0, "Doc /proc/<pid>/status");
    return;
  }
  n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if (n > 0) {
    buf[n] = '\0';
    printf("%s", buf);
    assert_test(1, "Doc /proc/<pid>/status");
  } else {
    assert_test(0, "Doc /proc/<pid>/status");
  }
}

void
test_open_modes(void)
{
  int fd;

  fd = open("/proc/processes", O_WRONLY);
  assert_test(fd < 0, "Tu choi mo ProcFS de ghi");
  if (fd >= 0)
    close(fd);

  fd = open("/proc/processes", O_RDWR);
  assert_test(fd < 0, "Tu choi mo ProcFS hai chieu");
  if (fd >= 0)
    close(fd);

  fd = open("/proc/new", O_CREATE);
  assert_test(fd < 0, "Tu choi tao file trong ProcFS");
  if (fd >= 0)
    close(fd);
}

int
main(int argc, char *argv[])
{
  printf("=======================================\n");
  printf("     BO KIEM THU PROCFS (XV6)          \n");
  printf("=======================================\n");

  test_read_file("/proc/uptime", "Doc file /proc/uptime");
  test_read_file("/proc/meminfo", "Doc file /proc/meminfo");
  test_small_buffer();
  test_processes();
  test_status();
  test_open_modes();

  printf("=======================================\n");
  exit(0);
}