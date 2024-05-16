#pragma once

struct mmap_area{
  struct file * f;
  uint addr;
  int length;
  int offset;
  int prot;
  int flags;
  int fd;
  int fork;
  struct proc *p; 
};


