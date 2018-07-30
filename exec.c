#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "defs.h"
#include "x86.h"
#include "elf.h"


#ifdef NONE
#else
#ifdef LAPA
static uint default_age=0xffffffff;
#else
static uint default_age=0;
#endif
#endif




int
exec(char *path, char **argv)
{
  char *s, *last;
  int i, off;
  uint argc, sz, sp, ustack[3+MAXARG+1];
  struct elfhdr elf;
  struct inode *ip;
  struct proghdr ph;
  pde_t *pgdir, *oldpgdir;
  struct proc *curproc = myproc();


#ifndef NONE
  int backup_pages_in_memory=curproc->pages_in_memory_counter;
  int backup_pages_in_swapfile=curproc->pages_in_swapfile_counter;
  uint backup_pagedout=curproc->pagedout;
  struct free_page backup_free_page [MAX_PSYC_PAGES];
  struct disk_info backup_disk_info [MAX_PSYC_PAGES];
  struct free_page* backup_tail;
  struct free_page* backup_head;
  int backup_fault=curproc->fault_counter;

  int index;
  for(index=0;index<MAX_PSYC_PAGES;index++)
  {
    backup_free_page[index].va=curproc->memory_pg_arr[index].va;
    backup_free_page[index].next=curproc->memory_pg_arr[index].next;
    backup_free_page[index].prev=curproc->memory_pg_arr[index].prev;
    backup_free_page[index].age=curproc->memory_pg_arr[index].age;
    backup_disk_info[index].location=curproc->disk_pg_arr[index].location;
    backup_disk_info[index].va=curproc->disk_pg_arr[index].va;
  }
  backup_head=curproc->head;
  backup_tail=curproc->tail;
#endif


  begin_op();

  if((ip = namei(path)) == 0){
    end_op();
    cprintf("exec: fail\n");
    return -1;
  }
  ilock(ip);
  pgdir = 0;

  // Check ELF header
  if(readi(ip, (char*)&elf, 0, sizeof(elf)) != sizeof(elf))
    goto bad;
  if(elf.magic != ELF_MAGIC)
    goto bad;

  if((pgdir = setupkvm()) == 0)
    goto bad;


  #ifndef NONE

  curproc->pages_in_memory_counter=0;
  curproc->pages_in_swapfile_counter=0;
  curproc->pagedout=0;
  curproc->fault_counter=0;
  for(index=0;index<MAX_PSYC_PAGES;index++)
  {
    curproc->memory_pg_arr[index].va=(char*)-1;
    curproc->memory_pg_arr[index].next=0;
    curproc->memory_pg_arr[index].prev=0;
    curproc->memory_pg_arr[index].age=default_age;
    curproc->disk_pg_arr[index].va=(char*)-1;
    curproc->disk_pg_arr[index].location=-1;
  }
  curproc->head=0;
  curproc->tail=0;


  #endif

  // Load program into memory.
  sz = 0;
  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
    if(readi(ip, (char*)&ph, off, sizeof(ph)) != sizeof(ph))
      goto bad;
    if(ph.type != ELF_PROG_LOAD)
      continue;
    if(ph.memsz < ph.filesz)
      goto bad;
    if(ph.vaddr + ph.memsz < ph.vaddr)
      goto bad;
    if((sz = allocuvm(pgdir, sz, ph.vaddr + ph.memsz)) == 0)
      goto bad;
    if(ph.vaddr % PGSIZE != 0)
      goto bad;
    if(loaduvm(pgdir, (char*)ph.vaddr, ip, ph.off, ph.filesz) < 0)
      goto bad;
  }
  iunlockput(ip);
  end_op();
  ip = 0;

  // Allocate two pages at the next page boundary.
  // Make the first inaccessible.  Use the second as the user stack.
  sz = PGROUNDUP(sz);
  if((sz = allocuvm(pgdir, sz, sz + 2*PGSIZE)) == 0)
    goto bad;
  clearpteu(pgdir, (char*)(sz - 2*PGSIZE));
  sp = sz;

  // Push argument strings, prepare rest of stack in ustack.
  for(argc = 0; argv[argc]; argc++) {
    if(argc >= MAXARG)
      goto bad;
    sp = (sp - (strlen(argv[argc]) + 1)) & ~3;
    if(copyout(pgdir, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
      goto bad;
    ustack[3+argc] = sp;
  }
  ustack[3+argc] = 0;

  ustack[0] = 0xffffffff;  // fake return PC
  ustack[1] = argc;
  ustack[2] = sp - (argc+1)*4;  // argv pointer

  sp -= (3+argc+1) * 4;
  if(copyout(pgdir, sp, ustack, (3+argc+1)*4) < 0)
    goto bad;

  // Save program name for debugging.
  for(last=s=path; *s; s++)
    if(*s == '/')
      last = s+1;
  safestrcpy(curproc->name, last, sizeof(curproc->name));
  // Commit to the user image.

  #ifndef NONE//where we reset the swapfile
  if(!(curproc->pid==1||(curproc->parent->pid==1)))
  {

  removeSwapFile(curproc);
  //remove old swapfile and data
  createSwapFile(curproc);
}
//create new swapfile  and data

  #endif
  oldpgdir = curproc->pgdir;
  curproc->pgdir = pgdir;
  curproc->sz = sz;
  curproc->tf->eip = elf.entry;  // main
  curproc->tf->esp = sp;



  
  switchuvm(curproc);
  freevm(oldpgdir);
  return 0;

 bad:
  if(pgdir)
    freevm(pgdir);
  if(ip){
    iunlockput(ip);
    end_op();
  }
  #ifndef NONE
  curproc->pages_in_swapfile_counter=backup_pages_in_swapfile;
  curproc->pages_in_memory_counter=backup_pages_in_memory;
  curproc->pagedout=backup_pagedout;
  curproc->fault_counter=backup_fault;
  for(i=0;i<MAX_PSYC_PAGES;i++)
  {
    curproc->memory_pg_arr[i].va=backup_free_page[i].va;
    curproc->memory_pg_arr[i].next=backup_free_page[i].next;
    curproc->memory_pg_arr[i].prev=backup_free_page[i].prev;
    curproc->memory_pg_arr[i].age=backup_free_page[i].age;
    curproc->disk_pg_arr[i].location=backup_disk_info[i].location;
    curproc->disk_pg_arr[i].va=backup_disk_info[i].va;
  }
  curproc->head=backup_head;
  curproc->tail=backup_tail;
  #endif
  return -1;
}
