#define _GNU_SOURCE
#define _XOPEN_SOURCE 700
#include "../include/memcloud.h"
#include <dlfcn.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>

#ifndef RTLD_NEXT
#define RTLD_NEXT ((void *)-1l)
#endif

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS 0x20
#endif

#define PAGE_SIZE 4096
static size_t mmap_threshold = (256 * 1024 * 1024); // 256MB default

static void *(*real_mmap)(void *, size_t, int, int, int, off_t) = NULL;

typedef struct VmRegion {
  void *addr;
  size_t size;
  uint64_t region_id;
  uint8_t *dirty_bits; // 1 bit per page
  struct VmRegion *next;
} VmRegion;

static VmRegion *head_region = NULL;
static pthread_mutex_t region_mutex = PTHREAD_MUTEX_INITIALIZER;

static VmRegion *find_region(void *addr) {
  VmRegion *curr = head_region;
  while (curr) {
    if (addr >= curr->addr &&
        addr < (void *)((uint8_t *)curr->addr + curr->size)) {
      return curr;
    }
    curr = curr->next;
  }
  return NULL;
}

static void *sync_thread(void *arg) {
  while (1) {
    usleep(100000); // 100ms
    pthread_mutex_lock(&region_mutex);
    VmRegion *curr = head_region;
    while (curr) {
      size_t num_pages = curr->size / PAGE_SIZE;
      for (size_t i = 0; i < num_pages; i++) {
        if (curr->dirty_bits[i]) {
          void *page_addr = (void *)((uintptr_t)curr->addr + (i * PAGE_SIZE));
          if (memcloud_vm_store(curr->region_id, i, page_addr, PAGE_SIZE) ==
              0) {
            curr->dirty_bits[i] = 0;
          }
        }
      }
      curr = curr->next;
    }
    pthread_mutex_unlock(&region_mutex);
  }
  return NULL;
}

static void sigsegv_handler(int sig, siginfo_t *si, void *ctx_ptr) {
  void *fault_addr = si->si_addr;
  ucontext_t *ctx = (ucontext_t *)ctx_ptr;

  pthread_mutex_lock(&region_mutex);
  VmRegion *region = find_region(fault_addr);

  if (region) {
    uint64_t page_offset = (uintptr_t)fault_addr - (uintptr_t)region->addr;
    uint64_t page_index = page_offset / PAGE_SIZE;
    void *page_start =
        (void *)((uintptr_t)region->addr + (page_index * PAGE_SIZE));

    if (si->si_code == SEGV_MAPERR) {
      uint8_t page_buf[PAGE_SIZE];
      if (memcloud_vm_fetch(region->region_id, page_index, page_buf,
                            PAGE_SIZE) < 0) {
        fprintf(stderr,
                "[MemCloud] Critical: Failed to fetch page %" PRIu64 "\n",
                page_index);
        pthread_mutex_unlock(&region_mutex);
        exit(1);
      }

      // Map fixed
      if (mmap(page_start, PAGE_SIZE, PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == MAP_FAILED) {
        perror("mmap fixed failed");
        pthread_mutex_unlock(&region_mutex);
        exit(1);
      }
      memcpy(page_start, page_buf, PAGE_SIZE);
      mprotect(page_start, PAGE_SIZE, PROT_READ);

    } else if (si->si_code == SEGV_ACCERR) {
      region->dirty_bits[page_index] = 1;
      mprotect(page_start, PAGE_SIZE, PROT_READ | PROT_WRITE);
    }

    pthread_mutex_unlock(&region_mutex);
    return;
  }

  pthread_mutex_unlock(&region_mutex);
  fprintf(stderr,
          "[MemCloud] Segmentation fault at %p (not a MemCloud region)\n",
          fault_addr);
  exit(1);
}

void __attribute__((constructor)) init_interceptor() {
  real_mmap = dlsym(RTLD_NEXT, "mmap");
  if (memcloud_init() != 0) {
    fprintf(stderr, "[MemCloud] Warning: Could not connect to daemon.\n");
  }

  char *threshold_env = getenv("MEMCLOUD_VM_THRESHOLD_MB");
  if (threshold_env) {
    size_t mb = atoi(threshold_env);
    if (mb > 0) {
      mmap_threshold = mb * 1024 * 1024;
      fprintf(stderr,
              "[MemCloud] MMAP_THRESHOLD set to %zu MB from environment "
              "variable.\n",
              mb);
    } else {
      fprintf(stderr,
              "[MemCloud] Warning: Invalid MEMCLOUD_VM_THRESHOLD_MB value "
              "'%s'. Using default %zu MB.\n",
              threshold_env, mmap_threshold / (1024 * 1024));
    }
  } else {
    fprintf(stderr, "[MemCloud] Using default MMAP_THRESHOLD: %zu MB.\n",
            mmap_threshold / (1024 * 1024));
  }

  struct sigaction sa;
  sa.sa_flags = SA_SIGINFO;
  sigemptyset(&sa.sa_mask);
  sa.sa_sigaction = sigsegv_handler;
  if (sigaction(SIGSEGV, &sa, NULL) == -1) {
    perror("sigaction");
  }

  pthread_t thread;
  pthread_create(&thread, NULL, sync_thread, NULL);
  pthread_detach(thread);
}

void *mmap(void *addr, size_t length, int prot, int flags, int fd,
           off_t offset) {
  if (!real_mmap)
    real_mmap = dlsym(RTLD_NEXT, "mmap");
  if (prot == (PROT_READ | PROT_WRITE) && (flags & MAP_ANONYMOUS) &&
      length >= mmap_threshold) {
    uint64_t region_id;
    if (memcloud_vm_alloc(length, &region_id) == 0) {
      void *reserved_addr =
          real_mmap(addr, length, PROT_NONE, flags, fd, offset);
      if (reserved_addr != MAP_FAILED) {
        VmRegion *new_reg = malloc(sizeof(VmRegion));
        new_reg->addr = reserved_addr;
        new_reg->size = length;
        new_reg->region_id = region_id;
        new_reg->dirty_bits = calloc(length / PAGE_SIZE, 1);

        pthread_mutex_lock(&region_mutex);
        new_reg->next = head_region;
        head_region = new_reg;
        pthread_mutex_unlock(&region_mutex);

        fprintf(stderr, "[memcloud-vm] mapped %zu MB remote VM region\n",
                length / (1024 * 1024));
        return reserved_addr;
      }
    }
  }

  return real_mmap(addr, length, prot, flags, fd, offset);
}
