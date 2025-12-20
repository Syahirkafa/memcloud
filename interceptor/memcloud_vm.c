#define _GNU_SOURCE
#define _XOPEN_SOURCE 700
#include "../include/memcloud.h"
#include <dlfcn.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#ifndef __APPLE__
#include <malloc.h>
#endif

#ifdef __APPLE__
#include <sys/types.h>
struct _malloc_zone_t;
typedef struct _malloc_zone_t malloc_zone_t;
extern malloc_zone_t *malloc_default_zone(void);
extern void *malloc_zone_malloc(malloc_zone_t *zone, size_t size);
extern void *malloc_zone_calloc(malloc_zone_t *zone, size_t num_items,
                                size_t size);
extern void *malloc_zone_realloc(malloc_zone_t *zone, void *ptr, size_t size);
extern void malloc_zone_free(malloc_zone_t *zone, void *ptr);
extern size_t malloc_size(const void *ptr);
#endif

#ifndef RTLD_NEXT
#define RTLD_NEXT ((void *)-1l)
#endif

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS 0x20
#endif

#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif

#define MAX_REGIONS 1024

static size_t vm_threshold = (8 * 1024 * 1024); // 8MB default

static void *(*real_mmap)(void *, size_t, int, int, int, off_t) = NULL;

typedef struct VmRegion {
  void *addr;
  size_t size;
  uint64_t region_id;
  uint8_t *dirty_bits; // 1 byte per page for simplicity
  int active;
} VmRegion;

static VmRegion *regions = NULL;
static pthread_mutex_t region_mutex = PTHREAD_MUTEX_INITIALIZER;
static __thread int in_hook = 0;
static int initialized = 0;
static int initializing = 0;
static int symbols_initialized = 0;
static int sdk_initialized = 0;

static void log_msg(const char *msg) {
  if (msg)
    write(2, msg, strlen(msg));
}

static void log_fmt(const char *fmt, ...) {
  char buf[512];
  va_list args;
  va_start(args, fmt);
  int n = vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  if (n > 0)
    write(2, buf, n);
}

#ifdef __APPLE__
struct interpose_s {
  void *new_func;
  void *orig_func;
};
#define DYLD_INTERPOSE(_replacement, _replacee)                                \
  __attribute__((                                                              \
      used)) static const struct interpose_s interpose_##_replacement          \
      __attribute__((section("__DATA,__interpose"))) = {                       \
          (void *)(unsigned long)&_replacement,                                \
          (void *)(unsigned long)&_replacee};

void *my_malloc(size_t size);
void *my_calloc(size_t nmemb, size_t size);
void *my_realloc(void *ptr, size_t size);
void my_free(void *ptr);

DYLD_INTERPOSE(my_malloc, malloc)
DYLD_INTERPOSE(my_calloc, calloc)
DYLD_INTERPOSE(my_realloc, realloc)
DYLD_INTERPOSE(my_free, free)

#define HOOK(name) my_##name
static void *internal_malloc(size_t s) {
  return malloc_zone_malloc(malloc_default_zone(), s);
}
static void *internal_calloc(size_t n, size_t s) {
  return malloc_zone_calloc(malloc_default_zone(), n, s);
}
static void *internal_realloc(void *p, size_t s) {
  return malloc_zone_realloc(malloc_default_zone(), p, s);
}
static void internal_free(void *p) {
  malloc_zone_free(malloc_default_zone(), p);
}
#else
static void *(*real_malloc)(size_t) = NULL;
static void *(*real_calloc)(size_t, size_t) = NULL;
static void *(*real_realloc)(void *, size_t) = NULL;
static void (*real_free)(void *) = NULL;
#define internal_malloc real_malloc
#define internal_calloc real_calloc
#define internal_realloc real_realloc
#define internal_free real_free
#define HOOK(name) name
#endif

static void page_fault_handler(int sig, siginfo_t *si, void *ctx_ptr);
static void *sync_thread(void *arg);

static void symbols_init() {
  if (symbols_initialized)
    return;

#ifndef __APPLE__
  real_malloc = dlsym(RTLD_NEXT, "malloc");
  real_calloc = dlsym(RTLD_NEXT, "calloc");
  real_realloc = dlsym(RTLD_NEXT, "realloc");
  real_free = dlsym(RTLD_NEXT, "free");
#endif
  real_mmap = dlsym(RTLD_NEXT, "mmap");
  if (!real_mmap)
    real_mmap = dlsym((void *)0, "mmap");

  if (real_mmap && !regions) {
    regions =
        real_mmap(NULL, sizeof(VmRegion) * MAX_REGIONS, PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    uint8_t *bits_pool =
        real_mmap(NULL, MAX_REGIONS * (1024 * 1024), PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (regions != MAP_FAILED && bits_pool != MAP_FAILED) {
      for (int i = 0; i < MAX_REGIONS; i++) {
        regions[i].dirty_bits = bits_pool + (i * 1024 * 1024);
        regions[i].active = 0;
      }
    }
  }
  symbols_initialized = 1;
}

static void lazy_init() {
  symbols_init();
  if (sdk_initialized || initializing)
    return;
  initializing = 1;

  pthread_t th;
  pthread_create(&th, NULL, sync_thread, NULL);
  pthread_detach(th);

  const char *env = getenv("MEMCLOUD_MALLOC_THRESHOLD_MB");
  if (env)
    vm_threshold = (size_t)atoll(env) * 1024 * 1024;

  const char *sock = getenv("MEMCLOUD_SOCKET");
  log_msg("[memcloud-vm] lazy_init: calling memcloud_init\n");
  memcloud_init_with_path(sock ? sock : "/tmp/memcloud.sock");

  sdk_initialized = 1;
  initializing = 0;
  initialized = 1;
  log_msg("[memcloud-vm] lazy init complete\n");
}

static VmRegion *find_region_exact(void *addr) {
  if (!regions)
    return NULL;
  for (int i = 0; i < MAX_REGIONS; i++) {
    if (regions[i].active && regions[i].addr == addr)
      return &regions[i];
  }
  return NULL;
}

static VmRegion *find_region(void *addr) {
  if (!regions)
    return NULL;
  for (int i = 0; i < MAX_REGIONS; i++) {
    if (regions[i].active && addr >= regions[i].addr &&
        addr < (void *)((uint8_t *)regions[i].addr + regions[i].size)) {
      return &regions[i];
    }
  }
  return NULL;
}

static void *allocate_remote_region(size_t size) {
  uint64_t region_id;
  if (memcloud_vm_alloc(size, &region_id) != 0)
    return NULL;

  void *addr =
      real_mmap(NULL, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (addr == MAP_FAILED)
    return NULL;

  if (mprotect(addr, PAGE_SIZE, PROT_NONE) != 0) {
    log_fmt("[memcloud-vm] FATAL: mprotect(PROT_NONE) failed: %s\n",
            strerror(errno));
    munmap(addr, size);
    return NULL;
  }

  log_fmt("[memcloud-vm] DEBUG: allocated PROT_NONE region at %p (size=%zu)\n",
          addr, size);

  pthread_mutex_lock(&region_mutex);
  for (int i = 0; i < MAX_REGIONS; i++) {
    if (!regions[i].active) {
      regions[i].addr = addr;
      regions[i].size = size;
      regions[i].region_id = region_id;
      regions[i].active = 1;
      memset(regions[i].dirty_bits, 0, (size + PAGE_SIZE - 1) / PAGE_SIZE);
      pthread_mutex_unlock(&region_mutex);
      log_msg("[memcloud-vm] intercepted large allocation\n");
      return addr;
    }
  }
  pthread_mutex_unlock(&region_mutex);
  munmap(addr, size);
  return NULL;
}

static int free_remote_region(void *ptr) {
  pthread_mutex_lock(&region_mutex);
  VmRegion *reg = find_region_exact(ptr);
  if (reg) {
    uint64_t rid = reg->region_id;
    munmap(reg->addr, reg->size);
    reg->active = 0;
    pthread_mutex_unlock(&region_mutex);
    memcloud_free(rid);
    return 1;
  }
  pthread_mutex_unlock(&region_mutex);
  return 0;
}

void *HOOK(malloc)(size_t size) {
  if (in_hook)
    return internal_malloc(size);
  in_hook = 1;
  lazy_init();
  void *res = NULL;
  if (size >= vm_threshold && sdk_initialized) {
    res = allocate_remote_region(size);
    if (!res) {
      log_fmt("[memcloud-vm] FATAL: VM allocation failed for %zu bytes. "
              "Aborting.\n",
              size);
      abort();
    }
    in_hook = 0;
    return res;
  }
  res = internal_malloc(size);
  in_hook = 0;
  return res;
}

void *HOOK(calloc)(size_t nmemb, size_t size) {
  if (in_hook)
    return internal_calloc(nmemb, size);
  in_hook = 1;
  lazy_init();
  size_t total = nmemb * size;
  void *res = NULL;
  if (total >= vm_threshold && sdk_initialized) {
    res = allocate_remote_region(total);
    if (!res) {
      log_fmt("[memcloud-vm] FATAL: VM allocation failed for %zu bytes "
              "(calloc). Aborting.\n",
              total);
      abort();
    }
    in_hook = 0;
    return res;
  }
  res = internal_calloc(nmemb, size);
  in_hook = 0;
  return res;
}

void *HOOK(realloc)(void *ptr, size_t size) {
  if (in_hook)
    return internal_realloc(ptr, size);
  in_hook = 1;
  lazy_init();
  if (!ptr) {
    void *r = HOOK(malloc)(size);
    in_hook = 0;
    return r;
  }
  pthread_mutex_lock(&region_mutex);
  VmRegion *reg = find_region_exact(ptr);
  if (reg) {
    pthread_mutex_unlock(&region_mutex);
    void *new_p = NULL;
    if (size >= vm_threshold && sdk_initialized) {
      new_p = allocate_remote_region(size);
      if (!new_p) {
        log_fmt(
            "[memcloud-vm] FATAL: VM realloc failed for %zu bytes. Aborting.\n",
            size);
        abort();
      }
      size_t c = (size < reg->size) ? size : reg->size;
      memcpy(new_p, ptr, c);
      free_remote_region(ptr);
    } else {
      new_p = internal_malloc(size);
      if (new_p) {
        size_t c = (size < reg->size) ? size : reg->size;
        memcpy(new_p, ptr, c);
        free_remote_region(ptr);
      }
    }
    in_hook = 0;
    return new_p;
  }
  pthread_mutex_unlock(&region_mutex);
  void *res = NULL;
  if (size >= vm_threshold && sdk_initialized) {
    res = allocate_remote_region(size);
    if (!res) {
      log_fmt(
          "[memcloud-vm] FATAL: VM realloc failed for %zu bytes. Aborting.\n",
          size);
      abort();
    }
    size_t old_s;
#ifdef __APPLE__
    old_s = malloc_size(ptr);
#else
    old_s = malloc_usable_size(ptr);
#endif
    size_t c = (size < old_s) ? size : old_s;
    memcpy(res, ptr, c);
    internal_free(ptr);
    in_hook = 0;
    return res;
  }
  res = internal_realloc(ptr, size);
  in_hook = 0;
  return res;
}

void HOOK(free)(void *ptr) {
  if (in_hook || !ptr) {
    if (ptr)
      internal_free(ptr);
    return;
  }
  in_hook = 1;
  lazy_init();
  if (!free_remote_region(ptr))
    internal_free(ptr);
  in_hook = 0;
}

static void page_fault_handler(int sig, siginfo_t *si, void *ctx_ptr) {
  void *fault_addr = si->si_addr;
  void *page_start = (void *)((uintptr_t)fault_addr & ~(PAGE_SIZE - 1));

  pthread_mutex_lock(&region_mutex);
  VmRegion *region = find_region(page_start);
  if (region) {
    log_fmt("[memcloud-vm] mapping page at %p\n", page_start);
    uintptr_t page_index =
        ((uintptr_t)page_start - (uintptr_t)region->addr) / PAGE_SIZE;

    if (real_mmap) {
      if (real_mmap(page_start, PAGE_SIZE, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1,
                    0) == MAP_FAILED) {
        log_fmt("[memcloud-vm] FATAL: mmap(MAP_FIXED) failed at %p: %s\n",
                page_start, strerror(errno));
        abort();
      }
      memcloud_vm_fetch(region->region_id, page_index, page_start, PAGE_SIZE);
      region->dirty_bits[page_index] = 1;
      log_fmt("[memcloud-vm] successfully mapped page at %p (region=%llu)\n",
              page_start, (unsigned long long)region->region_id);
    } else {
      log_msg("[memcloud-vm] FATAL: real_mmap is NULL in handler\n");
      abort();
    }
  } else {
    pthread_mutex_unlock(&region_mutex);
    signal(sig, SIG_DFL);
    raise(sig);
    return;
  }
  pthread_mutex_unlock(&region_mutex);
}

static void *sync_thread(void *arg) {
  while (1) {
    usleep(100000);
    pthread_mutex_lock(&region_mutex);
    if (regions) {
      for (int i = 0; i < MAX_REGIONS; i++) {
        if (regions[i].active) {
          size_t num_pages = regions[i].size / PAGE_SIZE;
          for (size_t j = 0; j < num_pages; j++) {
            if (regions[i].dirty_bits[j]) {
              void *p = (void *)((uintptr_t)regions[i].addr + (j * PAGE_SIZE));
              if (memcloud_vm_store(regions[i].region_id, j, p, PAGE_SIZE) ==
                  0) {
                regions[i].dirty_bits[j] = 0;
              }
            }
          }
        }
      }
    }
    pthread_mutex_unlock(&region_mutex);
  }
  return NULL;
}

static void install_sigsegv_handler() {
  struct sigaction sa;
  sa.sa_flags = SA_SIGINFO | SA_NODEFER;
  sigemptyset(&sa.sa_mask);
  sa.sa_sigaction = page_fault_handler;
  sigaction(SIGSEGV, &sa, NULL);
#ifdef __APPLE__
  sigaction(SIGBUS, &sa, NULL);
#endif
  log_fmt("[memcloud-vm] SIGSEGV handler installed (pid=%d)\n", getpid());
}

__attribute__((constructor)) void init_interceptor() {
  log_msg("[memcloud-vm] constructor start\n");
  symbols_init();
  install_sigsegv_handler();
  log_msg("[memcloud-vm] constructor end\n");
}
