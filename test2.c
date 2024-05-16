#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"
#include "syscall.h"
#include "traps.h"
#include "memlayout.h"
#include "param.h"

int
main(void)
{
  int initial_freemem, final_freemem;
  int length = 2 * PGSIZE ; // Allocate 2 * PGSIZE
  void *addr;

  // Get initial free memory count
  initial_freemem = freemem();
  printf(1, "Initial free memory count: %d\n", initial_freemem);
  printf(1, "length : %d\n", length);

  // Call mmap to allocate memory
  addr = (void *)mmap(0, length, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
  if (addr == (void *)-1) {
    printf(1, "mmap failed\n");
    exit();
  }

  // Get final free memory count
  final_freemem = freemem();
  printf(1, "Final free memory count: %d\n", final_freemem);

  // Check if the free memory count decreased by 2
  if (initial_freemem - final_freemem == 2) {
    printf(1, "Test passed: freememcount decreased by 2\n");
  } else {
    printf(1, "Test failed: freememcount did not decrease by 2\n");
  }

  // Clean up
  munmap((uint)addr);

  exit();
}

