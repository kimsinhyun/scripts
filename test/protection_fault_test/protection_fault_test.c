// https://syzkaller.appspot.com/bug?id=82f0e3a17b886989f090ea570e3113a44f56e62e
// autogenerated by syzkaller (https://github.com/google/syzkaller)

#define _GNU_SOURCE

#include <dirent.h>
#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <linux/futex.h>

static void sleep_ms(uint64_t ms)
{
  usleep(ms * 1000);
}

static uint64_t current_time_ms(void)
{
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts))
    exit(1);
  return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static void use_temporary_dir(void)
{
  char tmpdir_template[] = "./syzkaller.XXXXXX";
  char* tmpdir = mkdtemp(tmpdir_template);
  if (!tmpdir)
    exit(1);
  if (chmod(tmpdir, 0777))
    exit(1);
  if (chdir(tmpdir))
    exit(1);
}

static void thread_start(void* (*fn)(void*), void* arg)
{
  pthread_t th;
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 128 << 10);
  int i = 0;
  for (; i < 100; i++) {
    if (pthread_create(&th, &attr, fn, arg) == 0) {
      pthread_attr_destroy(&attr);
      return;
    }
    if (errno == EAGAIN) {
      usleep(50);
      continue;
    }
    break;
  }
  exit(1);
}

#define BITMASK(bf_off, bf_len) (((1ull << (bf_len)) - 1) << (bf_off))
#define STORE_BY_BITMASK(type, htobe, addr, val, bf_off, bf_len)               \
  *(type*)(addr) =                                                             \
      htobe((htobe(*(type*)(addr)) & ~BITMASK((bf_off), (bf_len))) |           \
            (((type)(val) << (bf_off)) & BITMASK((bf_off), (bf_len))))

typedef struct {
  int state;
} event_t;

static void event_init(event_t* ev)
{
  ev->state = 0;
}

static void event_reset(event_t* ev)
{
  ev->state = 0;
}

static void event_set(event_t* ev)
{
  if (ev->state)
    exit(1);
  __atomic_store_n(&ev->state, 1, __ATOMIC_RELEASE);
  syscall(SYS_futex, &ev->state, FUTEX_WAKE | FUTEX_PRIVATE_FLAG, 1000000);
}

static void event_wait(event_t* ev)
{
  while (!__atomic_load_n(&ev->state, __ATOMIC_ACQUIRE))
    syscall(SYS_futex, &ev->state, FUTEX_WAIT | FUTEX_PRIVATE_FLAG, 0, 0);
}

static int event_isset(event_t* ev)
{
  return __atomic_load_n(&ev->state, __ATOMIC_ACQUIRE);
}

static int event_timedwait(event_t* ev, uint64_t timeout)
{
  uint64_t start = current_time_ms();
  uint64_t now = start;
  for (;;) {
    uint64_t remain = timeout - (now - start);
    struct timespec ts;
    ts.tv_sec = remain / 1000;
    ts.tv_nsec = (remain % 1000) * 1000 * 1000;
    syscall(SYS_futex, &ev->state, FUTEX_WAIT | FUTEX_PRIVATE_FLAG, 0, &ts);
    if (__atomic_load_n(&ev->state, __ATOMIC_ACQUIRE))
      return 1;
    now = current_time_ms();
    if (now - start > timeout)
      return 0;
  }
}

static bool write_file(const char* file, const char* what, ...)
{
  char buf[1024];
  va_list args;
  va_start(args, what);
  vsnprintf(buf, sizeof(buf), what, args);
  va_end(args);
  buf[sizeof(buf) - 1] = 0;
  int len = strlen(buf);
  int fd = open(file, O_WRONLY | O_CLOEXEC);
  if (fd == -1)
    return false;
  if (write(fd, buf, len) != len) {
    int err = errno;
    close(fd);
    errno = err;
    return false;
  }
  close(fd);
  return true;
}

static long syz_open_dev(volatile long a0, volatile long a1, volatile long a2)
{
  if (a0 == 0xc || a0 == 0xb) {
    char buf[128];
    sprintf(buf, "/dev/%s/%d:%d", a0 == 0xc ? "char" : "block", (uint8_t)a1,
            (uint8_t)a2);
    return open(buf, O_RDWR, 0);
  } else {
    char buf[1024];
    char* hash;
    strncpy(buf, (char*)a0, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    while ((hash = strchr(buf, '#'))) {
      *hash = '0' + (char)(a1 % 10);
      a1 /= 10;
    }
    return open(buf, a2, 0);
  }
}

#define FS_IOC_SETFLAGS _IOW('f', 2, long)
static void remove_dir(const char* dir)
{
  int iter = 0;
  DIR* dp = 0;
retry:
  while (umount2(dir, MNT_DETACH) == 0) {
  }
  dp = opendir(dir);
  if (dp == NULL) {
    if (errno == EMFILE) {
      exit(1);
    }
    exit(1);
  }
  struct dirent* ep = 0;
  while ((ep = readdir(dp))) {
    if (strcmp(ep->d_name, ".") == 0 || strcmp(ep->d_name, "..") == 0)
      continue;
    char filename[FILENAME_MAX];
    snprintf(filename, sizeof(filename), "%s/%s", dir, ep->d_name);
    while (umount2(filename, MNT_DETACH) == 0) {
    }
    struct stat st;
    if (lstat(filename, &st))
      exit(1);
    if (S_ISDIR(st.st_mode)) {
      remove_dir(filename);
      continue;
    }
    int i;
    for (i = 0;; i++) {
      if (unlink(filename) == 0)
        break;
      if (errno == EPERM) {
        int fd = open(filename, O_RDONLY);
        if (fd != -1) {
          long flags = 0;
          if (ioctl(fd, FS_IOC_SETFLAGS, &flags) == 0) {
          }
          close(fd);
          continue;
        }
      }
      if (errno == EROFS) {
        break;
      }
      if (errno != EBUSY || i > 100)
        exit(1);
      if (umount2(filename, MNT_DETACH))
        exit(1);
    }
  }
  closedir(dp);
  for (int i = 0;; i++) {
    if (rmdir(dir) == 0)
      break;
    if (i < 100) {
      if (errno == EPERM) {
        int fd = open(dir, O_RDONLY);
        if (fd != -1) {
          long flags = 0;
          if (ioctl(fd, FS_IOC_SETFLAGS, &flags) == 0) {
          }
          close(fd);
          continue;
        }
      }
      if (errno == EROFS) {
        break;
      }
      if (errno == EBUSY) {
        if (umount2(dir, MNT_DETACH))
          exit(1);
        continue;
      }
      if (errno == ENOTEMPTY) {
        if (iter < 100) {
          iter++;
          goto retry;
        }
      }
    }
    exit(1);
  }
}

static void kill_and_wait(int pid, int* status)
{
  kill(-pid, SIGKILL);
  kill(pid, SIGKILL);
  for (int i = 0; i < 100; i++) {
    if (waitpid(-1, status, WNOHANG | __WALL) == pid)
      return;
    usleep(1000);
  }
  DIR* dir = opendir("/sys/fs/fuse/connections");
  if (dir) {
    for (;;) {
      struct dirent* ent = readdir(dir);
      if (!ent)
        break;
      if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
        continue;
      char abort[300];
      snprintf(abort, sizeof(abort), "/sys/fs/fuse/connections/%s/abort",
               ent->d_name);
      int fd = open(abort, O_WRONLY);
      if (fd == -1) {
        continue;
      }
      if (write(fd, abort, 1) < 0) {
      }
      close(fd);
    }
    closedir(dir);
  } else {
  }
  while (waitpid(-1, status, __WALL) != pid) {
  }
}

static void setup_test()
{
  prctl(PR_SET_PDEATHSIG, SIGKILL, 0, 0, 0);
  setpgrp();
  write_file("/proc/self/oom_score_adj", "1000");
}

static void setup_sysctl()
{
  char mypid[32];
  snprintf(mypid, sizeof(mypid), "%d", getpid());
  struct {
    const char* name;
    const char* data;
  } files[] = {
      {"/sys/kernel/debug/x86/nmi_longest_ns", "10000000000"},
      {"/proc/sys/kernel/hung_task_check_interval_secs", "20"},
      {"/proc/sys/net/core/bpf_jit_kallsyms", "1"},
      {"/proc/sys/net/core/bpf_jit_harden", "0"},
      {"/proc/sys/kernel/kptr_restrict", "0"},
      {"/proc/sys/kernel/softlockup_all_cpu_backtrace", "1"},
      {"/proc/sys/fs/mount-max", "100"},
      {"/proc/sys/vm/oom_dump_tasks", "0"},
      {"/proc/sys/debug/exception-trace", "0"},
      {"/proc/sys/kernel/printk", "7 4 1 3"},
      {"/proc/sys/net/ipv4/ping_group_range", "0 65535"},
      {"/proc/sys/kernel/keys/gc_delay", "1"},
      {"/proc/sys/vm/oom_kill_allocating_task", "1"},
      {"/proc/sys/kernel/ctrl-alt-del", "0"},
      {"/proc/sys/kernel/cad_pid", mypid},
  };
  for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
    if (!write_file(files[i].name, files[i].data))
      printf("write to %s failed: %s\n", files[i].name, strerror(errno));
  }
}

struct thread_t {
  int created, call;
  event_t ready, done;
};

static struct thread_t threads[16];
static void execute_call(int call);
static int running;

static void* thr(void* arg)
{
  struct thread_t* th = (struct thread_t*)arg;
  for (;;) {
    event_wait(&th->ready);
    event_reset(&th->ready);
    execute_call(th->call);
    __atomic_fetch_sub(&running, 1, __ATOMIC_RELAXED);
    event_set(&th->done);
  }
  return 0;
}

static void execute_one(void)
{
  int i, call, thread;
  for (call = 0; call < 14; call++) {
    for (thread = 0; thread < (int)(sizeof(threads) / sizeof(threads[0]));
         thread++) {
      struct thread_t* th = &threads[thread];
      if (!th->created) {
        th->created = 1;
        event_init(&th->ready);
        event_init(&th->done);
        event_set(&th->done);
        thread_start(thr, th);
      }
      if (!event_isset(&th->done))
        continue;
      event_reset(&th->done);
      th->call = call;
      __atomic_fetch_add(&running, 1, __ATOMIC_RELAXED);
      event_set(&th->ready);
      event_timedwait(&th->done, 50);
      break;
    }
  }
  for (i = 0; i < 100 && __atomic_load_n(&running, __ATOMIC_RELAXED); i++)
    sleep_ms(1);
}

static void execute_one(void);

#define WAIT_FLAGS __WALL

static void loop(void)
{
  int iter = 0;
  for (;; iter++) {
    char cwdbuf[32];
    sprintf(cwdbuf, "./%d", iter);
    if (mkdir(cwdbuf, 0777))
      exit(1);
    int pid = fork();
    if (pid < 0)
      exit(1);
    if (pid == 0) {
      if (chdir(cwdbuf))
        exit(1);
      setup_test();
      execute_one();
      exit(0);
    }
    int status = 0;
    uint64_t start = current_time_ms();
    for (;;) {
      if (waitpid(-1, &status, WNOHANG | WAIT_FLAGS) == pid)
        break;
      sleep_ms(1);
      if (current_time_ms() - start < 5000)
        continue;
      kill_and_wait(pid, &status);
      break;
    }
    remove_dir(cwdbuf);
  }
}

#ifndef __NR_memfd_create
#define __NR_memfd_create 319
#endif

uint64_t r[3] = {0xffffffffffffffff, 0xffffffffffffffff, 0xffffffffffffffff};

void execute_call(int call)
{
  intptr_t res = 0;
  switch (call) {
  case 0:
    *(uint32_t*)0x20000700 = 1;
    *(uint32_t*)0x20000704 = 0x80;
    *(uint8_t*)0x20000708 = 0;
    *(uint8_t*)0x20000709 = 0;
    *(uint8_t*)0x2000070a = 0;
    *(uint8_t*)0x2000070b = 0;
    *(uint32_t*)0x2000070c = 0;
    *(uint64_t*)0x20000710 = 0x50f;
    *(uint64_t*)0x20000718 = 0;
    *(uint64_t*)0x20000720 = 5;
    STORE_BY_BITMASK(uint64_t, , 0x20000728, 0, 0, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000728, 0, 1, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000728, 0, 2, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000728, 0, 3, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000728, 0, 4, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000728, 1, 5, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000728, 0, 6, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000728, 0, 7, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000728, 0, 8, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000728, 0, 9, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000728, 0, 10, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000728, 0, 11, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000728, 0, 12, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000728, 0, 13, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000728, 0, 14, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000728, 0, 15, 2);
    STORE_BY_BITMASK(uint64_t, , 0x20000728, 0, 17, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000728, 0, 18, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000728, 0, 19, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000728, 0, 20, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000728, 0, 21, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000728, 0, 22, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000728, 0, 23, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000728, 0, 24, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000728, 0, 25, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000728, 0, 26, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000728, 0, 27, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000728, 0, 28, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000728, 0, 29, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000728, 0, 30, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000728, 0, 31, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000728, 0, 32, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000728, 0, 33, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000728, 0, 34, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000728, 0, 35, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000728, 0, 36, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000728, 0, 37, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000728, 0, 38, 26);
    *(uint32_t*)0x20000730 = 0;
    *(uint32_t*)0x20000734 = 0;
    *(uint64_t*)0x20000738 = 0;
    *(uint64_t*)0x20000740 = 0;
    *(uint64_t*)0x20000748 = 0x2a040;
    *(uint64_t*)0x20000750 = 0;
    *(uint32_t*)0x20000758 = 0;
    *(uint32_t*)0x2000075c = 0;
    *(uint64_t*)0x20000760 = 0;
    *(uint32_t*)0x20000768 = 0;
    *(uint16_t*)0x2000076c = 0;
    *(uint16_t*)0x2000076e = 0;
    *(uint32_t*)0x20000770 = 0;
    *(uint32_t*)0x20000774 = 0;
    *(uint64_t*)0x20000778 = 0;
    syscall(__NR_perf_event_open, 0x20000700ul, 0, 0xff7ffffffffffffful, -1,
            0ul);
    break;
  case 1:
    syscall(__NR_mkdirat, 0xffffffffffffff9cul, 0ul, 0x1fful);
    break;
  case 2:
    memcpy((void*)0x200001c0, "/dev/loop#\000", 11);
    res = -1;
    res = syz_open_dev(0x200001c0, 0x75f, 0x103382);
    if (res != -1)
      r[0] = res;
    break;
  case 3:
    memcpy((void*)0x20000240, ".^\305", 3);
    res = syscall(__NR_memfd_create, 0x20000240ul, 0ul);
    if (res != -1)
      r[1] = res;
    break;
  case 4:
    syscall(__NR_fsetxattr, -1, 0ul, 0ul, 0ul, 0ul);
    break;
  case 5:
    *(uint64_t*)0x20000540 = 0x20000580;
    memcpy((void*)0x20000580, "\x3f\xa0\x95", 3);
    *(uint64_t*)0x20000548 = 3;
    syscall(__NR_pwritev, r[1], 0x20000540ul, 1ul, 0x81806, 0);
    break;
  case 6:
    syscall(__NR_ioctl, r[0], 0x4c00, r[1]);
    break;
  case 7:
    res = syscall(__NR_socket, 0x11ul, 3ul, 0x300);
    if (res != -1)
      r[2] = res;
    break;
  case 8:
    syscall(__NR_setsockopt, r[2], 0x107, 1, 0ul, 0ul);
    break;
  case 9:
    syscall(__NR_ioctl, -1, 0x540e, 0ul);
    break;
  case 10:
    syscall(__NR_ioctl, r[0], 0x4c06, -1);
    break;
  case 11:
    syscall(__NR_sendfile, r[0], r[0], 0ul, 0x24002da8ul);
    break;
  case 12:
    *(uint32_t*)0x20000100 = 4;
    *(uint32_t*)0x20000104 = 0x80;
    *(uint8_t*)0x20000108 = 0;
    *(uint8_t*)0x20000109 = -1;
    *(uint8_t*)0x2000010a = 0;
    *(uint8_t*)0x2000010b = 6;
    *(uint32_t*)0x2000010c = 0;
    *(uint64_t*)0x20000110 = 0;
    *(uint64_t*)0x20000118 = 0xc0030;
    *(uint64_t*)0x20000120 = 0;
    STORE_BY_BITMASK(uint64_t, , 0x20000128, 0, 0, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000128, 1, 1, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000128, 1, 2, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000128, 0, 3, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000128, 0, 4, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000128, 0, 5, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000128, 1, 6, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000128, 0, 7, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000128, 0, 8, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000128, 0, 9, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000128, 1, 10, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000128, 0, 11, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000128, 1, 12, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000128, 0, 13, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000128, 1, 14, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000128, 0, 15, 2);
    STORE_BY_BITMASK(uint64_t, , 0x20000128, 0, 17, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000128, 0, 18, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000128, 1, 19, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000128, 0, 20, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000128, 0, 21, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000128, 0, 22, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000128, 1, 23, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000128, 0, 24, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000128, 1, 25, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000128, 0, 26, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000128, 1, 27, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000128, 1, 28, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000128, 0, 29, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000128, 0, 30, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000128, 0, 31, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000128, 0, 32, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000128, 1, 33, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000128, 0, 34, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000128, 1, 35, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000128, 0, 36, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000128, 1, 37, 1);
    STORE_BY_BITMASK(uint64_t, , 0x20000128, 0, 38, 26);
    *(uint32_t*)0x20000130 = 0;
    *(uint32_t*)0x20000134 = 0;
    *(uint64_t*)0x20000138 = 0x20000040;
    *(uint64_t*)0x20000140 = 0;
    *(uint64_t*)0x20000148 = 0;
    *(uint64_t*)0x20000150 = 0x401;
    *(uint32_t*)0x20000158 = 9;
    *(uint32_t*)0x2000015c = 7;
    *(uint64_t*)0x20000160 = 3;
    *(uint32_t*)0x20000168 = 0x41;
    *(uint16_t*)0x2000016c = 5;
    *(uint16_t*)0x2000016e = 0;
    *(uint32_t*)0x20000170 = 7;
    *(uint32_t*)0x20000174 = 0;
    *(uint64_t*)0x20000178 = 6;
    syscall(__NR_perf_event_open, 0x20000100ul, 0, 5ul, -1, 0xbul);
    break;
  case 13:
    syscall(__NR_ioctl, r[0], 0x4c01, 0);
    break;
  }
}
int main(void)
{
  syscall(__NR_mmap, 0x1ffff000ul, 0x1000ul, 0ul, 0x32ul, -1, 0ul);
  syscall(__NR_mmap, 0x20000000ul, 0x1000000ul, 7ul, 0x32ul, -1, 0ul);
  syscall(__NR_mmap, 0x21000000ul, 0x1000ul, 0ul, 0x32ul, -1, 0ul);
  setup_sysctl();
  use_temporary_dir();
  loop();
  return 0;
}