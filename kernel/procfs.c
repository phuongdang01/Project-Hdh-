#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "stat.h"
#include "proc.h"

// Biến ticks và khóa đồng bộ thời gian từ kernel/trap.c
extern uint ticks;
extern struct spinlock tickslock;

// Inode ảo tĩnh đại diện cho tệp /proc/uptime trong RAM
static struct inode proc_uptime_inode;
static struct inode proc_meminfo_inode;
static struct inode proc_processes_inode;
static struct inode proc_status_inode[NPROC];
static int proc_inited = 0;
extern struct proc proc[NPROC];
extern struct spinlock wait_lock;

#define PROC_STATUS_INUM_BASE 10000

// Hàm phụ trợ so sánh 2 chuỗi ký tự an toàn trong kernel
static int
procfs_streq(const char *s1, const char *s2)
{
  while (*s1 && (*s1 == *s2)) {
    s1++;
    s2++;
  }
  return (uchar)*s1 - (uchar)*s2;
}

// Hàm phụ trợ chuyển đổi số nguyên dương sang chuỗi ASCII
static int
procfs_itoa(char *buf, uint n)
{
  char tmp[16];
  int i = 0, j = 0;

  if (n == 0) {
    buf[j++] = '0';
    buf[j] = '\0';
    return j;
  }

  while (n > 0) {
    tmp[i++] = (n % 10) + '0';
    n /= 10;
  }

  while (i > 0)
    buf[j++] = tmp[--i];
  buf[j] = '\0';
  return j;
}

static int
procfs_itoa64(char *buf, uint64 n)
{
  char tmp[24];
  int i = 0, j = 0;

  if (n == 0) {
    buf[j++] = '0';
    buf[j] = '\0';
    return j;
  }

  while (n > 0) {
    tmp[i++] = (n % 10) + '0';
    n /= 10;
  }

  while (i > 0)
    buf[j++] = tmp[--i];
  buf[j] = '\0';
  return j;
}

static char *
procfs_state_name(enum procstate state)
{
  switch (state) {
  case USED: return "USED";
  case SLEEPING: return "SLEEP";
  case RUNNABLE: return "RUNNABLE";
  case RUNNING: return "RUNNING";
  case ZOMBIE: return "ZOMBIE";
  default: return "UNUSED";
  }
}

static void
procfs_append(char *buf, int *len, int max, char *text)
{
  while (*text && *len < max - 1)
    buf[(*len)++] = *text++;
}

static int
procfs_parse_status_path(char *path)
{
  int i = 0;
  uint pid = 0;

  if (path[0] == '/' && path[1] == 'p' && path[2] == 'r' &&
      path[3] == 'o' && path[4] == 'c' && path[5] == '/')
    i = 6;
  else if (path[0] == 'p' && path[1] == 'r' && path[2] == 'o' &&
           path[3] == 'c' && path[4] == '/')
    i = 5;
  else
    return -1;

  if (path[i] < '0' || path[i] > '9')
    return -1;
  while (path[i] >= '0' && path[i] <= '9') {
    pid = pid * 10 + path[i] - '0';
    i++;
  }
  if (path[i] != '/' || procfs_streq(path + i, "/status") != 0)
    return -1;
  return pid > 0 ? pid : -1;
}

int
procfs_is_path(char *path)
{
  return procfs_streq(path, "/proc") == 0 ||
         procfs_streq(path, "proc") == 0 ||
         (procfs_streq(path, "/proc/") == 0) ||
         (path[0] == '/' && path[1] == 'p' && path[2] == 'r' &&
          path[3] == 'o' && path[4] == 'c' && path[5] == '/') ||
         (path[0] == 'p' && path[1] == 'r' && path[2] == 'o' &&
          path[3] == 'c' && path[4] == '/');
}

// 1. Hàm tra cứu và cấp phát Inode ảo cho ProcFS
struct inode *
procfs_lookup(char *path)
{
  // Khởi tạo Inode ảo một lần duy nhất
  if (proc_inited == 0) {
    initsleeplock(&proc_uptime_inode.lock, "proc_uptime");
    proc_uptime_inode.dev = 0;
    proc_uptime_inode.inum = 9999; // Định danh inum ảo đặc biệt
    proc_uptime_inode.ref = 0;
    proc_uptime_inode.type = T_PROC;
    proc_uptime_inode.valid = 1; // valid = 1 để ilock() không đọc block từ đĩa
    proc_uptime_inode.nlink = 1;
    initsleeplock(&proc_meminfo_inode.lock, "proc_meminfo");
    proc_meminfo_inode.dev = 0;
    proc_meminfo_inode.inum = 9998;
    proc_meminfo_inode.ref = 0;
    proc_meminfo_inode.type = T_PROC;
    proc_meminfo_inode.valid = 1;
    proc_meminfo_inode.nlink = 1;
    initsleeplock(&proc_processes_inode.lock, "proc_processes");
    proc_processes_inode.dev = 0;
    proc_processes_inode.inum = 9997;
    proc_processes_inode.ref = 0;
    proc_processes_inode.type = T_PROC;
    proc_processes_inode.valid = 1;
    proc_processes_inode.nlink = 1;
    for (int i = 0; i < NPROC; i++) {
      initsleeplock(&proc_status_inode[i].lock, "proc_status");
      proc_status_inode[i].dev = 0;
      proc_status_inode[i].inum = PROC_STATUS_INUM_BASE + i;
      proc_status_inode[i].ref = 0;
      proc_status_inode[i].type = T_PROC;
      proc_status_inode[i].valid = 1;
      proc_status_inode[i].nlink = 1;
    }
    proc_inited = 1;
  }

  // Khớp đường dẫn /proc/uptime hoặc proc/uptime
  if (procfs_streq(path, "/proc/uptime") == 0 ||
      procfs_streq(path, "proc/uptime") == 0) {
    proc_uptime_inode.ref++;
    return &proc_uptime_inode;
  }
  if (procfs_streq(path, "/proc/meminfo") == 0 ||
      procfs_streq(path, "proc/meminfo") == 0) {
    proc_meminfo_inode.ref++;
    return &proc_meminfo_inode;
  }
  if (procfs_streq(path, "/proc/processes") == 0 ||
      procfs_streq(path, "proc/processes") == 0) {
    proc_processes_inode.ref++;
    return &proc_processes_inode;
  }
  int pid = procfs_parse_status_path(path);
  if (pid > 0) {
    struct proc *p;
    for (p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      int match = p->pid == pid && p->state != UNUSED;
      release(&p->lock);
      if (match) {
        int slot = p - proc;
        proc_status_inode[slot].ref++;
        proc_status_inode[slot].inum = PROC_STATUS_INUM_BASE + pid;
        return &proc_status_inode[slot];
      }
    }
  }
  return 0; // Trả về null nếu không tìm thấy tệp
}

// 2. Logic sinh dữ liệu on-demand cho /proc/uptime
int
procfs_read_uptime(int user_dst, uint64 dst, uint off, uint n)
{
  char buf[32];

  // Chiếm khóa tickslock an toàn trước khi đọc ticks
  acquire(&tickslock);
  uint cur_ticks = ticks;
  release(&tickslock);

  // Định dạng chuỗi: "<ticks>\n"
  int len = procfs_itoa(buf, cur_ticks);
  buf[len++] = '\n';
  buf[len] = '\0';

  // Nếu vị trí đọc (offset) vượt quá độ dài dữ liệu hiện có -> trả về 0 (EOF)
  if (off >= len)
    return 0;

  // Tính số lượng byte thực tế cần chép
  uint m = len - off;
  if (m > n)
    m = n;

  // Sao chép an toàn từ buffer kernel sang buffer của user process
  if (either_copyout(user_dst, dst, buf + off, m) == -1)
    return -1;

  return m;
}

int
procfs_read_meminfo(int user_dst, uint64 dst, uint off, uint n)
{
  char buf[64];
  uint64 free_b = freemem();
  int len = 0;

  char *prefix = "MemFree: ";
  while (*prefix) {
    buf[len++] = *prefix++;
  }

  len += procfs_itoa(buf + len, free_b);
  buf[len++] = '\n';
  buf[len] = '\0';

  if (off >= len)
    return 0;

  uint m = len - off;
  if (m > n)
    m = n;

  if (either_copyout(user_dst, dst, buf + off, m) == -1)
    return -1;

  return m;
}

int
procfs_read_processes(int user_dst, uint64 dst, uint off, uint n)
{
  struct proc *p;
  char line[96];
  char number[24];
  int len, pos = 0, copied = 0;
  uint start, count;

  procfs_append(line, &pos, sizeof(line), "PID STATE SIZE NAME\n");
  len = pos;
  pos = 0;
  if (off < len && copied < n) {
    start = off;
    count = len - start;
    if (count > n - copied)
      count = n - copied;
    if (either_copyout(user_dst, dst + copied, line + start, count) == -1)
      return -1;
    copied += count;
  }
  pos = len;

  for (p = proc; p < &proc[NPROC] && copied < n; p++) {
    acquire(&p->lock);
    if (p->state != UNUSED) {
      len = 0;
      procfs_itoa(number, p->pid);
      procfs_append(line, &len, sizeof(line), number);
      procfs_append(line, &len, sizeof(line), " ");
      procfs_append(line, &len, sizeof(line), procfs_state_name(p->state));
      procfs_append(line, &len, sizeof(line), " ");
      procfs_itoa64(number, p->sz);
      procfs_append(line, &len, sizeof(line), number);
      procfs_append(line, &len, sizeof(line), " ");
      procfs_append(line, &len, sizeof(line), p->name);
      procfs_append(line, &len, sizeof(line), "\n");
      if (off < pos + len) {
        start = off > pos ? off - pos : 0;
        count = len - start;
        if (count > n - copied)
          count = n - copied;
        if (either_copyout(user_dst, dst + copied, line + start, count) == -1) {
          release(&p->lock);
          return -1;
        }
        copied += count;
      }
      pos += len;
    }
    release(&p->lock);
  }
  return copied;
}

int
procfs_read_status(int user_dst, uint64 dst, uint off, uint n, int pid)
{
  struct proc *p;
  char buf[192];
  char number[24];
  char name[16];
  int len = 0;
  int parent_pid = 0;
  enum procstate state;
  uint64 size;

  for (p = proc; p < &proc[NPROC]; p++) {
    acquire(&wait_lock);
    acquire(&p->lock);
    if (p->pid == pid && p->state != UNUSED) {
      state = p->state;
      size = p->sz;
      memmove(name, p->name, sizeof(name));
      name[sizeof(name) - 1] = '\0';
      if (p->parent != 0)
        parent_pid = p->parent->pid;
      release(&p->lock);
      release(&wait_lock);

      procfs_append(buf, &len, sizeof(buf), "Name: ");
      procfs_append(buf, &len, sizeof(buf), name);
      procfs_append(buf, &len, sizeof(buf), "\nPid: ");
      procfs_itoa(number, pid);
      procfs_append(buf, &len, sizeof(buf), number);
      procfs_append(buf, &len, sizeof(buf), "\nPPid: ");
      procfs_itoa(number, parent_pid);
      procfs_append(buf, &len, sizeof(buf), number);
      procfs_append(buf, &len, sizeof(buf), "\nState: ");
      procfs_append(buf, &len, sizeof(buf), procfs_state_name(state));
      procfs_append(buf, &len, sizeof(buf), "\nSize: ");
      procfs_itoa64(number, size);
      procfs_append(buf, &len, sizeof(buf), number);
      procfs_append(buf, &len, sizeof(buf), "\n");
      goto copy_status;
    }
    release(&p->lock);
    release(&wait_lock);
  }
  return -1;

copy_status:
  if (off >= len)
    return 0;
  uint m = len - off;
  if (m > n)
    m = n;
  if (either_copyout(user_dst, dst, buf + off, m) == -1)
    return -1;
  return m;
}

// 3. Hàm điều phối đọc chính của hệ thống tệp ảo ProcFS
int
procfs_read(struct inode *ip, int user_dst, uint64 dst, uint off, uint n)
{
  if (ip == &proc_uptime_inode || ip->inum == 9999) {
    return procfs_read_uptime(user_dst, dst, off, n);
  }
  if (ip == &proc_meminfo_inode || ip->inum == 9998) {
    return procfs_read_meminfo(user_dst, dst, off, n);
  }
  if (ip == &proc_processes_inode || ip->inum == 9997) {
    return procfs_read_processes(user_dst, dst, off, n);
  }
  if (ip->inum >= PROC_STATUS_INUM_BASE) {
    return procfs_read_status(user_dst, dst, off, n,
                              ip->inum - PROC_STATUS_INUM_BASE);
  }
  return -1;
}