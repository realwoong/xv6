#include "param.h"
#include "types.h"
#include "defs.h"
#include "x86.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "elf.h"

//pa3
#include "vm.h"
#include "file.h"
#include "spinlock.h"
#include <stdbool.h>
#include <stddef.h>

int freememcount = 0;
extern char data[];  // defined by kernel.ld
pde_t *kpgdir;  // for use in scheduler()

// Set up CPU's kernel segment descriptors.
// Run once on entry on each CPU.
void
seginit(void)
{
  struct cpu *c;

  // Map "logical" addresses to virtual addresses using identity map.
  // Cannot share a CODE descriptor for both kernel and user
  // because it would have to have DPL_USR, but the CPU forbids
  // an interrupt from CPL=0 to DPL=3.
  c = &cpus[cpuid()];
  c->gdt[SEG_KCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, 0);
  c->gdt[SEG_KDATA] = SEG(STA_W, 0, 0xffffffff, 0);
  c->gdt[SEG_UCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, DPL_USER);
  c->gdt[SEG_UDATA] = SEG(STA_W, 0, 0xffffffff, DPL_USER);
  lgdt(c->gdt, sizeof(c->gdt));
}

// Return the address of the PTE in page table pgdir
// that corresponds to virtual address va.  If alloc!=0,
// create any required page table pages.
pte_t *
walkpgdir(pde_t *pgdir, const void *va, int alloc)
{
  pde_t *pde;
  pte_t *pgtab;

  pde = &pgdir[PDX(va)];
  if(*pde & PTE_P){
    pgtab = (pte_t*)P2V(PTE_ADDR(*pde));
  } else {
    if(!alloc || (pgtab = (pte_t*)kalloc()) == 0)
      return 0;
    // Make sure all those PTE_P bits are zero.
    memset(pgtab, 0, PGSIZE);
    // The permissions here are overly generous, but they can
    // be further restricted by the permissions in the page table
    // entries, if necessary.
    *pde = V2P(pgtab) | PTE_P | PTE_W | PTE_U;
  }
  return &pgtab[PTX(va)];
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned.
int
mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm)
{
  char *a, *last;
  pte_t *pte;

  a = (char*)PGROUNDDOWN((uint)va);
  last = (char*)PGROUNDDOWN(((uint)va) + size - 1);
  for(;;){
    if((pte = walkpgdir(pgdir, a, 1)) == 0)
      return -1;
    if(*pte & PTE_P)
      panic("remap");
    *pte = pa | perm | PTE_P;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// There is one page table per process, plus one that's used when
// a CPU is not running any process (kpgdir). The kernel uses the
// current process's page table during system calls and interrupts;
// page protection bits prevent user code from using the kernel's
// mappings.
//
// setupkvm() and exec() set up every page table like this:
//
//   0..KERNBASE: user memory (text+data+stack+heap), mapped to
//                phys memory allocated by the kernel
//   KERNBASE..KERNBASE+EXTMEM: mapped to 0..EXTMEM (for I/O space)
//   KERNBASE+EXTMEM..data: mapped to EXTMEM..V2P(data)
//                for the kernel's instructions and r/o data
//   data..KERNBASE+PHYSTOP: mapped to V2P(data)..PHYSTOP,
//                                  rw data + free physical memory
//   0xfe000000..0: mapped direct (devices such as ioapic)
//
// The kernel allocates physical memory for its heap and for user memory
// between V2P(end) and the end of physical memory (PHYSTOP)
// (directly addressable from end..P2V(PHYSTOP)).

// This table defines the kernel's mappings, which are present in
// every process's page table.
static struct kmap {
  void *virt;
  uint phys_start;
  uint phys_end;
  int perm;
} kmap[] = {
 { (void*)KERNBASE, 0,             EXTMEM,    PTE_W}, // I/O space
 { (void*)KERNLINK, V2P(KERNLINK), V2P(data), 0},     // kern text+rodata
 { (void*)data,     V2P(data),     PHYSTOP,   PTE_W}, // kern data+memory
 { (void*)DEVSPACE, DEVSPACE,      0,         PTE_W}, // more devices
};

// Set up kernel part of a page table.
pde_t*
setupkvm(void)
{
  pde_t *pgdir;
  struct kmap *k;

  if((pgdir = (pde_t*)kalloc()) == 0)
    return 0;
  memset(pgdir, 0, PGSIZE);
  if (P2V(PHYSTOP) > (void*)DEVSPACE)
    panic("PHYSTOP too high");
  for(k = kmap; k < &kmap[NELEM(kmap)]; k++)
    if(mappages(pgdir, k->virt, k->phys_end - k->phys_start,
                (uint)k->phys_start, k->perm) < 0) {
      freevm(pgdir);
      return 0;
    }
  return pgdir;
}

// Allocate one page table for the machine for the kernel address
// space for scheduler processes.
void
kvmalloc(void)
{
  kpgdir = setupkvm();
  switchkvm();
}

// Switch h/w page table register to the kernel-only page table,
// for when no process is running.
void
switchkvm(void)
{
  lcr3(V2P(kpgdir));   // switch to the kernel page table
}

// Switch TSS and h/w page table to correspond to process p.
void
switchuvm(struct proc *p)
{
  if(p == 0)
    panic("switchuvm: no process");
  if(p->kstack == 0)
    panic("switchuvm: no kstack");
  if(p->pgdir == 0)
    panic("switchuvm: no pgdir");

  pushcli();
  mycpu()->gdt[SEG_TSS] = SEG16(STS_T32A, &mycpu()->ts,
                                sizeof(mycpu()->ts)-1, 0);
  mycpu()->gdt[SEG_TSS].s = 0;
  mycpu()->ts.ss0 = SEG_KDATA << 3;
  mycpu()->ts.esp0 = (uint)p->kstack + KSTACKSIZE;
  // setting IOPL=0 in eflags *and* iomb beyond the tss segment limit
  // forbids I/O instructions (e.g., inb and outb) from user space
  mycpu()->ts.iomb = (ushort) 0xFFFF;
  ltr(SEG_TSS << 3);
  lcr3(V2P(p->pgdir));  // switch to process's address space
  popcli();
}

// Load the initcode into address 0 of pgdir.
// sz must be less than a page.
void
inituvm(pde_t *pgdir, char *init, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pgdir, 0, PGSIZE, V2P(mem), PTE_W|PTE_U);
  memmove(mem, init, sz);
}

// Load a program segment into pgdir.  addr must be page-aligned
// and the pages from addr to addr+sz must already be mapped.
int
loaduvm(pde_t *pgdir, char *addr, struct inode *ip, uint offset, uint sz)
{
  uint i, pa, n;
  pte_t *pte;

  if((uint) addr % PGSIZE != 0)
    panic("loaduvm: addr must be page aligned");
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, addr+i, 0)) == 0)
      panic("loaduvm: address should exist");
    pa = PTE_ADDR(*pte);
    if(sz - i < PGSIZE)
      n = sz - i;
    else
      n = PGSIZE;
    if(readi(ip, P2V(pa), offset+i, n) != n)
      return -1;
  }
  return 0;
}

// Allocate page tables and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
int
allocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  char *mem;
  uint a;

  if(newsz >= KERNBASE)
    return 0;
  if(newsz < oldsz)
    return oldsz;

  a = PGROUNDUP(oldsz);
  for(; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){
      cprintf("allocuvm out of memory\n");
      deallocuvm(pgdir, newsz, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pgdir, (char*)a, PGSIZE, V2P(mem), PTE_W|PTE_U) < 0){
      cprintf("allocuvm out of memory (2)\n");
      deallocuvm(pgdir, newsz, oldsz);
      kfree(mem);
      return 0;
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
int
deallocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  pte_t *pte;
  uint a, pa;

  if(newsz >= oldsz)
    return oldsz;

  a = PGROUNDUP(newsz);
  for(; a  < oldsz; a += PGSIZE){
    pte = walkpgdir(pgdir, (char*)a, 0);
    if(!pte)
      a = PGADDR(PDX(a) + 1, 0, 0) - PGSIZE;
    else if((*pte & PTE_P) != 0){
      pa = PTE_ADDR(*pte);
      if(pa == 0)
        panic("kfree");
      char *v = P2V(pa);
      kfree(v);
      *pte = 0;
    }
  }
  return newsz;
}

// Free a page table and all the physical memory pages
// in the user part.
void
freevm(pde_t *pgdir)
{
  uint i;

  if(pgdir == 0)
    panic("freevm: no pgdir");
  deallocuvm(pgdir, KERNBASE, 0);
  for(i = 0; i < NPDENTRIES; i++){
    if(pgdir[i] & PTE_P){
      char * v = P2V(PTE_ADDR(pgdir[i]));
      kfree(v);
    }
  }
  kfree((char*)pgdir);
}

// Clear PTE_U on a page. Used to create an inaccessible
// page beneath the user stack.
void
clearpteu(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if(pte == 0)
    panic("clearpteu");
  *pte &= ~PTE_U;
}

// Given a parent process's page table, create a copy
// of it for a child.
pde_t*
copyuvm(pde_t *pgdir, uint sz)
{
  pde_t *d;
  pte_t *pte;
  uint pa, i, flags;
  char *mem;

  if((d = setupkvm()) == 0)
    return 0;
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, (void *) i, 0)) == 0)
      panic("copyuvm: pte should exist");
    if(!(*pte & PTE_P))
      panic("copyuvm: page not present");
    pa = PTE_ADDR(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      goto bad;
    memmove(mem, (char*)P2V(pa), PGSIZE);
    if(mappages(d, (void*)i, PGSIZE, V2P(mem), flags) < 0) {
      kfree(mem);
      goto bad;
    }
  }
  return d;

bad:
  freevm(d);
  return 0;
}

//PAGEBREAK!
// Map user virtual address to kernel address.
char*
uva2ka(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if((*pte & PTE_P) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  return (char*)P2V(PTE_ADDR(*pte));
}

// Copy len bytes from p to user address va in page table pgdir.
// Most useful when pgdir is not the current page table.
// uva2ka ensures this only works for PTE_U pages.
int
copyout(pde_t *pgdir, uint va, void *p, uint len)
{
  char *buf, *pa0;
  uint n, va0;

  buf = (char*)p;
  while(len > 0){
    va0 = (uint)PGROUNDDOWN(va);
    pa0 = uva2ka(pgdir, (char*)va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (va - va0);
    if(n > len)
      n = len;
    memmove(pa0 + (va - va0), buf, n);
    len -= n;
    buf += n;
    va = va0 + PGSIZE;
  }
  return 0;
}

//pa3
struct {
  struct spinlock lock;
  struct mmap_area mmap_area[64];
} mtable;
void init_mtable(void) {
  initlock(&mtable.lock, "mtable");
  acquire(&mtable.lock);
  for (int i = 0; i < 64; i++) {
    mtable.mmap_area[i].p = 0;
  }
  release(&mtable.lock);
}



static struct mmap_area *get_mtable_entry(void) {
  struct mmap_area *ret = NULL;
  acquire(&mtable.lock);
  
  for (ret = mtable.mmap_area; ret < &mtable.mmap_area[64]; ret++) {
    if (ret->p == NULL) {
      release(&mtable.lock);
      ret->fork = 0;
      return ret;
    }
  }

  release(&mtable.lock);
  return NULL;
}

uint 
mmap(uint addr, int length, int prot, int flags, int fd, int offset)
{
  struct proc *p = myproc();
  struct mmap_area *m;
  char *alloc;

  if (addr % PGSIZE != 0) {
        return 0; 
    }

    if (length % PGSIZE != 0) {
        return 0; 
    }

    if (prot != PROT_READ && prot != (PROT_READ | PROT_WRITE)) {
        return 0; 
    }


  if ((flags & MAP_ANONYMOUS) && fd != -1) {
    return 0; // Error: Invalid combination of MAP_ANONYMOUS and fd
  }

  // 에러통과
  m = get_mtable_entry();
  if (m == 0) {
    return 0; // Error: Not enough mtable entry
  }

  m->fd = fd;
  m->p = p;
  m->addr = addr;
  m->length = length;
  m->prot = prot;
  m->flags = flags;
  m->f = 0;

  // Handle MAP_ANONYMOUS
  if (flags & MAP_ANONYMOUS) {
    if (flags & MAP_POPULATE) {
      for (uint i = (addr + 0x40000000); i < length + (addr + 0x40000000); i += PGSIZE) {
        alloc = kalloc();
        if (alloc == 0) {
          return 0; // Error: Memory allocation failed
        }
        memset(alloc, 0, PGSIZE);
        if (mappages(p->pgdir, (void*)i, PGSIZE, V2P(alloc), prot | PTE_U) < 0) {
          kfree(alloc);
          return 0; // Error: Mapping pages failed
        }
      }
    }
    return (addr + 0x40000000);
  }

  // Handle file mapping
  struct file *f = p->ofile[fd];
  if (f == 0 || ((prot & PROT_READ) && !f->readable) || ((prot & PROT_WRITE) && !f->writable)) {
    return 0; // Error: Invalid file or access permissions
  }
  m->f = f;
  m->offset = offset;

  if (flags & MAP_POPULATE) {
    f->off = offset;
    for (uint i = (addr + 0x40000000); i < length + (addr + 0x40000000); i += PGSIZE) {
      alloc = kalloc();
      if (alloc == 0) {
        return 0; // Error: Memory allocation failed
      }
      memset(alloc, 0, PGSIZE);
      if (fileread(f, alloc, PGSIZE) < 0) {
        kfree(alloc);
        return 0; // Error: File read failed
      }
      if (mappages(p->pgdir, (void*)i, PGSIZE, V2P(alloc), prot | PTE_U) < 0) {
        kfree(alloc);
        return 0; // Error: Mapping pages failed
      }
    }
    f->off = offset;
  }

  return (addr + 0x40000000);
}


int
munmap(uint addr)
{
  struct mmap_area *m = 0;
  struct proc *p = myproc();
  uint vaddr = addr - 0x40000000;
  pte_t *t;

  if (addr % PGSIZE != 0) {
    return -1; // Error: addr is not page-aligned
  }


  acquire(&mtable.lock);

  for (m = mtable.mmap_area; m < &mtable.mmap_area[64]; m++) {
    if (m->addr == vaddr && m->p == p) {
      break;
    }
  }

  release(&mtable.lock);

  if (m == 0 || m >= &mtable.mmap_area[64]) {
    return -1; // Error: mmap_area not found
  }

  for (int i = 0; i < m->length; i += PGSIZE) {
    t = walkpgdir(p->pgdir, (const void *)(addr + i), 0);
    if (t && (*t & PTE_P)) {
      kfree(P2V(PTE_ADDR(*t))); // Use PTE_ADDR to get the page address
      *t = 0;
    }
  }

  m->p = 0;
  m->fd = 0;
  m->f = 0;

  return 1;
}



int pfh(uint err)
{
    struct proc *p;
    struct mmap_area *m = 0;
    uint addr = PGROUNDDOWN(rcr2()); // 페이지 폴트가 발생한 주소를 가져와 페이지 크기로 내림
    addr = addr - 0x40000000; // 해당 주소를 매핑된 주소로 변환
    p = myproc(); // 현재 프로세스를 가져옴

    acquire(&mtable.lock);
    // mmap_area에서 해당 주소가 포함된 영역을 찾음
    for (m = mtable.mmap_area; m < &mtable.mmap_area[64]; m++) {
        if (m->p == p && m->addr <= addr && addr < (m->addr + m->length)) {
            break;
        }
    }
    release(&mtable.lock);


    // 매핑된 영역을 찾지 못한 경우
    if (m == &mtable.mmap_area[64] || m->p != p) {
        //cprintf("____pa3 : fail : 01\n");
        return -1; // 프로세스 종료
    }

    // 쓰기 보호를 확인
    if (!(m->prot & PROT_WRITE) && (err & 0x2)) {
        //cprintf("____pa3 : fail : 02\n");
        return -1; // 프로세스 종료
    }

    // 페이지를 할당하고 초기화
    char *alloc = kalloc(); // 새로운 페이지 할당
    if (alloc == 0) {
        //cprintf("____pa3 : fail : 03\n");
        return -1; // 프로세스 종료
    }
    memset(alloc, 0, PGSIZE); // 페이지 초기화

    if (m->f) { // 파일 매핑인 경우
        uint off = m->f->off;
        uint new_off = m->offset + ((addr - m->addr <= 0) ? 0 : PGROUNDDOWN(addr - m->addr));
        m->f->off = new_off;
        
        //cprintf("____pa3 : fileread offset: %d\n", new_off); // 디버그 로그 추가
        int read_res = fileread(m->f, alloc, PGSIZE);
        //cprintf("____pa3 : fileread result: %d\n", read_res); // 디버그 로그 추가
        
        if (read_res < 0) {
            kfree(alloc);
            m->f->off = off; // 원래 파일 오프셋 복원
            //cprintf("____pa3 : fail : 04\n");
            return -1; // 프로세스 종료
        }
        m->f->off = off; // 원래 파일 오프셋 복원
    }

    // 페이지 테이블에 매핑
    if (mappages(p->pgdir, (void*)(addr + 0x40000000), PGSIZE, V2P(alloc), m->prot | PTE_U) < 0) {
        kfree(alloc);
        //cprintf("____pa3 : fail : 05\n");
        return -1; // 프로세스 종료
    }

    //cprintf("____pa3 : success : 01\n");
    return 0; // 페이지 폴트 처리 성공
}


int freemem(void)
{
        return freememcount;
}
