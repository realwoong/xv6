#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"
#include "syscall.h"
#include "traps.h"
#include "memlayout.h"
#include "param.h"

#define PGSIZE 4096

void test_mmap() {
       printf(1,"mem : %d\n", freemem());
    int fd = open("testfile", O_CREATE | O_RDWR);
    write(fd, "hello, world\n", 14);
    close(fd);

    fd = open("testfile", O_RDWR);

    // 각 플래그 조합 테스트
    int flags[] = {0, MAP_ANONYMOUS, MAP_POPULATE, MAP_ANONYMOUS | MAP_POPULATE};
    int prot[] = {PROT_READ, PROT_READ | PROT_WRITE};

    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 4; j++) {
            char *addr = (char*)mmap(0, PGSIZE, prot[i], flags[j], fd, 0);
            if (addr == (char*)-1) {
                printf(1, "mmap failed for prot=%d, flags=%d\n", prot[i], flags[j]);
            } else {
                printf(1, "mmap succeeded for prot=%d, flags=%d\n", prot[i], flags[j]);
                if (!(flags[j] & MAP_ANONYMOUS)) {
                    printf(1, "mmap contents: %s\n", addr);
                }
                munmap((uint)addr);
            }
        }
    }
    close(fd);
       printf(1,"mem : %d\n", freemem());
}

void test_page_fault() {
       printf(1,"mem : %d\n", freemem());
    char *addr = (char*)mmap(0, PGSIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS, -1, 0);
    if (addr == (char*)-1) {
        printf(1, "mmap failed\n");
        return;
    }
    addr[0] = 'A';  // Trigger a page fault by writing to the allocated memory
    if (addr[0] == 'A') {
        printf(1, "page fault handled successfully\n");
    } else {
        printf(1, "page fault handling failed\n");
    }
    munmap((uint)addr);
       printf(1,"mem : %d\n", freemem());
}

void test_munmap() {
       printf(1,"mem : %d\n", freemem());
    char *addr = (char*)mmap(0, PGSIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS, -1, 0);
    if (addr == (char*)-1) {
        printf(1, "mmap failed\n");
        return;
    }
    if (munmap((uint)addr) == 1) {
        printf(1, "munmap succeeded\n");
    } else {
        printf(1, "munmap failed\n");
    }
    // Try accessing the unmapped memory
    if (addr[0] == 0) {
        printf(1, "access to unmapped memory failed\n");
    } else {
        printf(1, "access to unmapped memory succeeded (unexpected)\n");
    }
       printf(1,"mem : %d\n", freemem());
}

void test_freemem() {
    int free_before = freemem();
    char *addr = (char*)mmap(0, PGSIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS, -1, 0);
    if (addr == (char*)-1) {
        printf(1, "mmap failed\n");
        return;
    }
    int free_after = freemem();
    if (free_before > free_after) {
        printf(1, "freemem test succeeded\n");
    } else {
        printf(1, "freemem test failed\n");
    }
    munmap((uint)addr);
    int free_final = freemem();
    if (free_after < free_final) {
        printf(1, "freemem test cleanup succeeded\n");
    } else {
        printf(1, "freemem test cleanup failed\n");
    }
}

int main(int argc, char *argv[]) {
    printf(1, "Running mmap test\n");
    test_mmap();

    printf(1, "Running page fault test\n");
    test_page_fault();

    printf(1, "Running munmap test\n");
    test_munmap();

    printf(1, "Running freemem test\n");
    test_freemem();
    exit();
}

