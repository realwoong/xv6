#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"
#include "syscall.h"
#include "traps.h"
#include "memlayout.h"
#include "param.h"

#define PGSIZE 4096

void test_mmap_basic() {
    int fd = open("testfile", O_CREATE | O_RDWR);
    if (fd < 0) {
        printf(1, "@@Failed to open file\n");
        return;
    }
    write(fd, "hello, world\n", 14);
    close(fd);

    fd = open("testfile", O_RDWR);
    if (fd < 0) {
        printf(1, "@@Failed to open file\n");
        return;
    }

    printf(1, "@@freemem count : %d\n", freemem() );
    uint addr = (uint)mmap(0, 3*PGSIZE, PROT_READ, 0, fd, 0);
    uint addr2 = (uint)mmap(0, 3*PGSIZE, PROT_READ, 0, fd, 0);
    if (addr == 0) {
        printf(1, "@@mmap Failed\n");
    } else {
        printf(1, "@@mmap succeeded: %s\n", (char *)addr2);
        munmap(addr);
    }
    if (addr2 == 0) {
        printf(1, "@@mmap Failed\n");
    } else {
        printf(1, "@@mmap succeeded: %s\n", (char *)addr2);
        munmap(addr2);
    }
    printf(1, "@@freemem count : %d\n", freemem() );
    close(fd);
}

void test_mmap_flags() {
    int fd = open("testfile", O_CREATE | O_RDWR);
    if (fd < 0) {
        printf(1, "@@Failed to open file\n");
        return;
    }
    write(fd, "hello, world\n", 14);
    close(fd);

    fd = open("testfile", O_RDWR);
    if (fd < 0) {
        printf(1, "@@@Failed to open file\n");
        return;
    }

    int flags[] = {0, MAP_ANONYMOUS, MAP_POPULATE, MAP_ANONYMOUS | MAP_POPULATE};
    int prot[] = {PROT_READ, PROT_READ | PROT_WRITE};

    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 4; j++) {
            uint addr = (uint)mmap(0, PGSIZE, prot[i], flags[j], fd, 0);
            if (addr == 0) {
                printf(1, "@@mmap Failed for prot=%d, flags=%d\n", prot[i], flags[j]);
            } else {
                printf(1, "@@mmap succeeded for prot=%d, flags=%d\n", prot[i], flags[j]);
                if (!(flags[j] & MAP_ANONYMOUS)) {
                    printf(1, "@@mmap contents: %s\n", (char *)addr);
                }
                munmap(addr);
            }
        }
    }
    close(fd);
}

void test_mmap_offset() {
    int fd = open("testfile", O_CREATE | O_RDWR);
    if (fd < 0) {
        printf(1, "@@Failed to open file\n");
        return;
    }
    if (write(fd, "hello, world\n", 14) != 14) {
        printf(1, "@@Failed to write initial data\n");
        close(fd);
        return;
    }
    // 파일에 페이지 크기만큼 더 많은 데이터를 추가
    for (int i = 0; i < PGSIZE; i++) {
        if (write(fd, "a", 1) != 1) {
            printf(1, "@@Failed to write additional data\n");
            close(fd);
            return;
        }
    }
    if (write(fd, "additional data\n", 16) != 16) {
        printf(1, "@@Failed to write additional data\n");
        close(fd);
        return;
    }
    close(fd);

    fd = open("testfile", O_RDWR);
    if (fd < 0) {
        printf(1, "@@Failed to open file\n");
        return;
    }

    uint addr = (uint)mmap(0, PGSIZE, PROT_READ, 0, fd, PGSIZE);

    if (addr == 0) {
        printf(1, "@@mmap with offset Failed\n");
    } else {
        printf(1, "@@mmap with offset succeeded: %s\n", (char *)addr);
        munmap(addr);
    }

    close(fd);
}
void test_mmap_anonymous() {
    uint addr = (uint)mmap(0, PGSIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS, -1, 0);
    if (addr == 0) {
        printf(1, "@@anonymous mmap Failed\n");
    } else {
        ((char *)addr)[0] = 'A';  // 쓰기 테스트
        if (((char *)addr)[0] == 'A') {
            printf(1, "@@anonymous mmap succeeded\n");
        } else {
            printf(1, "@@anonymous mmap Failed to write\n");
        }
        munmap(addr);
    }
}

void test_mmap_populate() {
    int fd = open("testfile", O_CREATE | O_RDWR);
    if (fd < 0) {
        printf(1, "@@Failed to open file\n");
        return;
    }
    write(fd, "hello, world\n", 14);
    close(fd);

    fd = open("testfile", O_RDWR);
    if (fd < 0) {
        printf(1, "@@Failed to open file\n");
        return;
    }

    uint addr = (uint)mmap(0, PGSIZE, PROT_READ, MAP_POPULATE, fd, 0);
    if (addr == 0) {
        printf(1, "@@mmap with MAP_POPULATE Failed\n");
    } else {
        printf(1, "@@mmap with MAP_POPULATE succeeded: %s\n", (char *)addr);
        munmap(addr);
    }
    close(fd);
}

void test_mmap_errors() {
    // 잘못된 파일 디스크립터
    uint addr = (uint)mmap(0, PGSIZE, PROT_READ, 0, -1, 0);
    if (addr == 0) {
        printf(1, "@@mmap Failed as expected with invalid fd\n");
    } else {
        printf(1, "@@mmap succeeded unexpectedly with invalid fd\n");
        munmap(addr);
    }

    // 페이지 정렬되지 않은 주소
    
    addr = (uint)mmap(12345, PGSIZE, PROT_READ, 0, 0, 0);
    if (addr == 0) {
        printf(1, "@@mmap Failed as expected with unaligned address\n");
    } else {
        printf(1, "@@mmap succeeded unexpectedly with unaligned address\n");
        munmap(addr);
    }
}

void test_mmap_protection() {
    int fd = open("testfile", O_CREATE | O_RDWR);
    if (fd < 0) {
        printf(1, "@@Failed to open file\n");
        return;
    }
    write(fd, "hello, world\n", 14);
    close(fd);

    fd = open("testfile", O_RDWR);
    if (fd < 0) {
        printf(1, "@@Failed to open file\n");
        return;
    }

    uint addr = (uint)mmap(0, PGSIZE, PROT_WRITE, 0, fd, 0);
    if (addr == 0) {
        printf(1, "@@mmap Failed\n");
    } else {
        printf(1, "@@mmap with PROT_WRITE succeeded\n");
        ((char *)addr)[0] = 'A'; // 쓰기 테스트
        if (((char *)addr)[0] == 'A') {
            printf(1, "@@PROT_WRITE test succeeded\n");
        } else {
            printf(1, "@@PROT_WRITE test failed\n");
        }
        munmap(addr);
    }
    close(fd);
}

void test_mmap_size() {
    int fd = open("testfile", O_CREATE | O_RDWR);
    if (fd < 0) {
        printf(1, "@@Failed to open file\n");
        return;
    }
    write(fd, "hello, world\n", 14);
    close(fd);

    fd = open("testfile", O_RDWR);
    if (fd < 0) {
        printf(1, "@@Failed to open file\n");
        return;
    }

    uint addr = (uint)mmap(0, PGSIZE * 2, PROT_READ, 0, fd, 0);
    if (addr == 0) {
        printf(1, "@@mmap Failed\n");
    } else {
        printf(1, "@@mmap with size 2*PGSIZE succeeded\n");
        printf(1, "@@mmap contents: %s\n", (char *)addr);
        munmap(addr);
    }
    close(fd);
}

int main(int argc, char *argv[]) {

    
    printf(1, "@@Running mmap basic test\n");
    test_mmap_basic();

    printf(1, "@@Running mmap prot and flags test\n");
    test_mmap_flags();



    printf(1, "@@Running mmap offset test\n");
    test_mmap_offset();

    printf(1, "@@Running mmap anonymous test\n");
    test_mmap_anonymous();

    printf(1, "@@Running mmap populate test\n");
    test_mmap_populate();


    printf(1, "@@Running mmap error cases test\n");
    test_mmap_errors();

    printf(1, "@@Running mmap protection test\n");
    test_mmap_protection();

    printf(1, "@@Running mmap size test\n");
    test_mmap_size();
    
    printf(1, "@@freemem : %d\n ", freemem());
    
    printf(1, "@@________test end_________\n");
    
    test_mmap_basic();
    exit();
    
}
