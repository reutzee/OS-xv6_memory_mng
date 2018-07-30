#include "param.h"
#include "types.h"
#include "defs.h"
#include "x86.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "elf.h"

extern char data[];  // defined by kernel.ld
pde_t *kpgdir;  // for use in scheduler()"not found va trap14\n")


#ifdef NONE
static uint default_age=0;
#else
#ifdef LAPA
static uint default_age=0xffffffff;
#else
static uint default_age=0;
#endif
#endif
char* zero_page[PGSIZE]={0};
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


int findRamIndex(struct proc*p,char* item)
{
    int i;
        for(i=0;i<MAX_PSYC_PAGES;i++)
        {
            if(p->memory_pg_arr[i].va==(char*)-1)
            {
                return i;
            }
        }
        return -1;
}

int readPageFromFile(struct proc* p,int user_address,char* buffer)
{
    int i;
    int read=-1;
    for(i=0;i<MAX_PSYC_PAGES;i++)
    {
        if(p->disk_pg_arr[i].va==(char*)user_address)
        {
            read=readFromSwapFile(p,buffer,i*PGSIZE,PGSIZE);
            p->disk_pg_arr[i].va=(char*)-1;
            p->pages_in_swapfile_counter--;
            if(read==-1)
            {
              panic("failled to read from  swapfile\n");
                return-1;//error
            }
            update_memory(p,(char*)user_address);
            return read;
        }
    }
    return -2;//given user_address wasnt on disk
}

int writePageToFile(struct proc * p, int userPageVAddr, pde_t *pgdir)
{
    int i;
    for(i=0;i<MAX_PSYC_PAGES;i++)
    {
        if(p->disk_pg_arr[i].va==(char*)-1)
        {
            int res=writeToSwapFile(p,(char*)userPageVAddr,PGSIZE*i,PGSIZE);
            if(res==-1)
            {
                return -1;
            }
            p->disk_pg_arr[i].va=(char*)userPageVAddr;
            p->pages_in_swapfile_counter++;
            p->pagedout++;
            return res;
        }   
    }
    return -2;//disk was full
}

static char buff[PGSIZE]; //buffer used to store swapped page in getPageFromFile method



int getPageFromFile(int fault_address)
{
    struct proc* p=myproc();
    if(p==0)
    {return 0;}
    int userPageVAddr = PGROUNDDOWN(fault_address);
    char * newPg = kalloc();
    memset(newPg, 0, PGSIZE);
    lcr3(V2P(myproc()->pgdir)); //refresh CR3 register
    if(p->pages_in_memory_counter>=MAX_PSYC_PAGES)//swap needed
    {
    char* va=0;
    struct free_page* to_remove=select_page_to_remove(p);//select based on defined paging algorithem
    va=to_remove->va;
    to_remove->va=(char*)-1;
    p->pages_in_memory_counter--;
    fixPagedInPTE(userPageVAddr, V2P(newPg), p->pgdir);//update pte of useraddress
    readPageFromFile(p,userPageVAddr, buff);//add to memory datastructre inside the function
    int outPagePAddr = getPagePAddr((int)va,p->pgdir);//get page virtual address wtithout the offset of given vadress
    memmove(newPg, buff, PGSIZE);
    writePageToFile(p, (int)va, p->pgdir);
    fixPagedOutPTE(p->pgdir,(int)va,0);
    char *v = P2V(outPagePAddr);
    kfree(v); //free swapped page
  return 1;
    }
    else//swap isnt needed
    {
    fixPagedInPTE(userPageVAddr, V2P(newPg), p->pgdir);
    readPageFromFile(p,userPageVAddr, (char*)userPageVAddr);
   return 1;
    }
    return 0;
}



// Return the address of the PTE in page table pgdir
// that corresponds to virtual address va.  If alloc!=0,
// create any required page table pages.
static pte_t *
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

int getPagePAddr(int userPageVAddr, pde_t * pgdir){
  pte_t *pte;
  pte = walkpgdir(pgdir, (int*)userPageVAddr, 0);
  if(!pte) //uninitialized page table
    return -1;
  return PTE_ADDR(*pte);
}


void fixPagedInPTE(int userPageVAddr, int pagePAddr, pde_t * pgdir){
  pte_t *pte;
  pte = walkpgdir(pgdir, (int*)userPageVAddr, 0);
  if (!pte)
    panic("PTE of swapped page is missing");
  if (*pte & PTE_P)
  	panic("REMAP!");
  *pte |= PTE_P | PTE_W | PTE_U;      //Turn on needed bits
  *pte &= ~PTE_PG;    //Turn off inFile bit
  *pte |= pagePAddr;  //Map PTE to the new Page
  lcr3(V2P(myproc()->pgdir)); //refresh CR3 register
}



// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned.
static int
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
//                                  rw data +int 
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


void fixPagedOutPTE( pde_t * pgdir,int va,int walkpgdirmode)
{
  pte_t *pte;
  pte = walkpgdir(pgdir,(char*)va, walkpgdirmode);
  if (!pte)
    panic("PTE of swapped page is missing");
  *pte |= PTE_PG;//add pg flag to the page
  *pte &= ~PTE_P;//remove present flag
  *pte &= PTE_FLAGS(*pte); //clear junk physical address
  lcr3(V2P(myproc()->pgdir)); //refresh CR3 register
}



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



int update_memoryscfifo(struct proc* p ,char* va)
{
      if(((p->head==0)&&(p->tail!=0))||((p->head!=0)&&(p->tail==0)))
      {
          panic("structure is wrong tail is  ");//toremove this test
      }
           if(p->head!=0)
      {
          if((p->head->prev)!=0)
          {
            panic("queue head got prev pointer\n");
          }
      }  
  int i;
  for(i=0;i<MAX_PSYC_PAGES;i++)
  {
      if(p->memory_pg_arr[i].va==(char*)-1)
      {
          p->memory_pg_arr[i].va=(char*)va;
          p->memory_pg_arr[i].age=default_age;
          p->pages_in_memory_counter++;
        if((p->head==0)&&(p->tail==0))
        {
          p->memory_pg_arr[i].next=0;
          p->memory_pg_arr[i].prev=0;
          p->head=&p->memory_pg_arr[i];
          p->tail=&p->memory_pg_arr[i];
          return i;
        } 
       if(p->tail!=0)
       {
          if((p->tail->next)!=0)
          {
            panic("queue tail got next pointer\n");//to remove this test
          }//extra check for error handling            
          p->tail->next=&(p->memory_pg_arr[i]); 
          p->tail=&(p->memory_pg_arr[i]);
          p->tail->next=0;
          return i;
        }
      }
  }
  panic("no empty place in memory scfifo\n");
  //update memory datasture
}

int update_memoryaq(struct proc* p,char* va)
{

        if(((p->head==0)&&(p->tail!=0))||((p->head!=0)&&(p->tail==0)))
      {
          panic("structure is wrong tail is  ");//toremove this test
      }
           if(p->head!=0)
      {
          if((p->head->prev)!=0)
          {
            panic("queue head got prev pointer\n");
          }
      }  
  int i;
  for(i=0;i<MAX_PSYC_PAGES;i++)
  {
      if(p->memory_pg_arr[i].va==(char*)-1)
      {
          p->memory_pg_arr[i].va=(char*)va;
          p->memory_pg_arr[i].age=default_age;
          p->pages_in_memory_counter++;
          p->memory_pg_arr[i].next=0;
          p->memory_pg_arr[i].prev=0;

        if((p->head==0)&&(p->tail==0))
        {
          p->head=&p->memory_pg_arr[i];
          p->tail=&p->memory_pg_arr[i];
          return i;
        }
        else
        {
            p->head->prev=&p->memory_pg_arr[i];
            p->head=&p->memory_pg_arr[i];
            return i;     
      }
    }
  }
           
  panic("no empty place in memory aq\n");
  //update memory datasture
}





struct free_page * nfu_alg(struct proc* p)
{
    int i=0;
    int min_index=-1;
    int min_age=-1;
    for(i=0;i<MAX_PSYC_PAGES;i++)
    {
      if(p->memory_pg_arr[i].va!=(char*)-1)
      {
        if(min_index==-1)
        {
            min_index=i;
            min_age=p->memory_pg_arr[i].age;
        }
        else
        {
            if(p->memory_pg_arr[i].age<min_age)
            {
                min_age=p->memory_pg_arr[i].age;
                min_index=i;
            }
        }
      }  
    }
    return &(p->memory_pg_arr[min_index]);
}


int count_ones(uint x)
{
  int res=0;
  int i;
  for(i=0;i<32;i++)
  {
    if(((1<<i)&x)!=0)
    {
      res++;
    }
  }
  return res;
}

struct free_page * lapa_alg(struct proc* p)
{
    int i=0;
    int min_index=-1;
    int min_age=-1;
    int min_ones=0;
    for(i=0;i<MAX_PSYC_PAGES;i++)
    {
     if(p->memory_pg_arr[i].va!=(char*)-1 )
      {
      int tmp_ones=count_ones(p->memory_pg_arr[i].age);
          if(min_index==-1)
        {
            min_index=i;
            min_age=p->memory_pg_arr[i].age;
            min_ones=tmp_ones;
        }
        else//got item already
        {
          if(tmp_ones==min_ones)
            {
              if(p->memory_pg_arr[i].age<min_age)
              {
                  min_age=p->memory_pg_arr[i].age;
                  min_index=i;
                  min_ones=tmp_ones;
              }
            }
            else if(tmp_ones<min_ones)
            {
              min_age=p->memory_pg_arr[i].age;
              min_index=i;
              min_ones=tmp_ones;
            }
        }
      }
    }
    return &(p->memory_pg_arr[min_index]);
}

struct free_page * aq_alg(struct proc* p)
{
  struct free_page* tmp=p->tail;
  struct free_page* oldtail=p->tail;
  if(tmp==0)
  {
    panic("tail empty at aq alg\n");
  }

while(get_user_bit(tmp->va)==0)
{
  p->tail=p->tail->prev;
  p->tail->next=0;
  tmp->prev=0;
  tmp->next=p->head;
  p->head->prev=tmp;
  p->head=tmp;
  tmp=p->tail;
  if(tmp==oldtail)
  {
    panic("all pages in memory are with user bit off\n");
  }
}


        if(tmp->prev!=0)
      {
        tmp->prev->next=tmp->next;
      }
      if(tmp->next!=0)
      {
        tmp->next->prev=tmp->prev;
      }
      if(p->head==tmp)
      {
        p->head=tmp->next;
      }
      if(p->tail==tmp)
      {
        p->tail=tmp->prev;
      }
      return tmp;
  
}

struct free_page * select_page_to_remove(struct proc* p)
{
#ifdef SCFIFO
    return scfifo_alg(p);
#else
#ifdef NFUA
    return nfu_alg(p);
#else
#ifdef LAPA
    return lapa_alg(p);
#else
#ifdef AQ
    return aq_alg(p);
#endif
#endif
#endif
#endif
    panic("unknown paging method2\n");
    return 0;
}


int update_memorynfua(struct proc* p,char* va)
{
  int i;
  for(i=0;i<MAX_PSYC_PAGES;i++)
  {
    if(p->memory_pg_arr[i].va==(char*)-1)
    {
      p->memory_pg_arr[i].va=va;
      p->memory_pg_arr[i].age=default_age;
      p->memory_pg_arr[i].next=0;
      p->memory_pg_arr[i].prev=0;
      p->pages_in_memory_counter++;
      return i;
    }
  }

  panic("no empty place in memory nfua\n");
  return -1;

}
int update_memorylapa(struct proc* p ,char* va)
{
    int i;
  for(i=0;i<MAX_PSYC_PAGES;i++)
  {
    if(p->memory_pg_arr[i].va==(char*)-1)
    {
      p->memory_pg_arr[i].va=va;
      p->memory_pg_arr[i].age=default_age;
      p->memory_pg_arr[i].next=0;
      p->memory_pg_arr[i].prev=0;
      p->pages_in_memory_counter++;
      return i;
    }
  }
  panic("no empty place in memory lapa\n");
  return -1;
}

//update the ram datastucte insert va to it return index of the inserted ram
int update_memory(struct proc* p ,char* va)
{
 #ifdef SCFIFO
 return update_memoryscfifo(p,va);
 #else
 #ifdef NFUA
 return update_memorynfua(p,va);//same as update memorylap
 #else 
 #ifdef LAPA
 return update_memorylapa(p,va);//same as update memory nfua
 #else
 #ifdef AQ
 return update_memoryaq(p,va);
 #endif
 #endif
 #endif
 #endif

panic("unknown paging method\n");
return 0;
}

// Allocate page tables and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
int
oldallocuvm(pde_t *pgdir, uint oldsz, uint newsz)
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


int get_user_bit(char* va)
{
  uint user;
  pte_t *pte = walkpgdir(myproc()->pgdir, (void*)va, 0);
  if (!*pte)
    panic("updateAccessBit: pte is empty");
  user = (*pte) & PTE_U;
  if(user==0)
  {
    return 0;
  }
  return 1;
}
int updateAccessBit(char *va)
{
  uint accessed;
  pte_t *pte = walkpgdir(myproc()->pgdir, (void*)va, 0);
  if (!*pte)
    panic("updateAccessBit: pte is empty");
  accessed = (*pte) & PTE_A;
  (*pte) &= ~PTE_A;
  if(accessed==0)
  {
    return 0;
  }
  else
  {
    return 1;
  }
}

int pageIsInFile(int userPageVAddr, pde_t * pgdir) {
  pte_t *pte;
  pte = walkpgdir(pgdir, (char *)userPageVAddr, 0);
  if((*pte&PTE_PG)&& (!(*pte&PTE_P)))
  return 1; //PAGE IS IN FILE
  else 
    return 0;
}


struct free_page * scfifo_alg(struct proc* p)
{
  //cprintf("at scfifo alg\n");
  struct free_page* oldhead=p->head;
  struct free_page* tmp=p->head;

  int accessbit_head=-1;
  //cprintf("pages_in_memory_counter is %d\n",p->pages_in_memory_counter);
  do
  {
    accessbit_head=updateAccessBit(p->head->va);
    if(accessbit_head==-1)
      {
        panic("access bit is not 1 or zero in scfifo swap pages\n");
      }
   //   cprintf("access bit is %d \n",accessbit_head);
      if(accessbit_head==1)
      {
        p->head=p->head->next;
        p->head->prev=0;
        tmp->next=0;
        tmp->prev=p->tail;
        p->tail->next=tmp;
        p->tail=tmp;
        tmp=p->head;
      }
      else//found who to switch
      {
        tmp=p->head;
        if(p->head==p->tail)//if only 1 page in memory
        {
          p->tail->next=0;
          p->head->prev=0;
        }
//        p->head=p->head->next;
        //cprintf("no%d",t);
      }
    }while(p->head!=oldhead&&accessbit_head==1);
    //cprintf("found who to switch scfifo \n");
    //cprintf("out of loop\n");
      if(tmp->prev!=0)
      {
        tmp->prev->next=tmp->next;
      }
      if(tmp->next!=0)
      {
        tmp->next->prev=tmp->prev;
      }
      if(p->head==tmp)
      {
        p->head=tmp->next;
      }
      if(p->tail==tmp)
      {
        p->tail=tmp->prev;
      }
      return tmp;
}

void insert_to_disk(struct proc* p,int i,struct free_page* tmp)
{
  p->disk_pg_arr[i].va=tmp->va;  
  p->pages_in_swapfile_counter++;  
  p->pagedout++;
  if(writeToSwapFile(p,(char*)(PTE_ADDR(tmp->va)),i*PGSIZE,PGSIZE)<=0)
  {
    panic("failled to write to swapfile\n");
  }

  pte_t *pte1=walkpgdir(p->pgdir,(void*)tmp->va,0);
  if(!*pte1)
  {
    panic("pte1 is empty\n");
  }
 // pte_t *pte2=walkpgdir(p->pgdir,(void*)p->disk_pg_arr[i].va,1);//map
  tmp->va=(char*)-1;
  p->pages_in_memory_counter--;
// cprintf("kfree insert to disk\n");
  kfree((char*)PTE_ADDR(P2V_WO(*pte1)));
//  cprintf("kfree insert to disk done\n");
  *pte1=PTE_W|PTE_U|PTE_PG;
//  cprintf("DID THIS\n");
  lcr3(V2P(p->pgdir));
  //got slot in ram now for new page
}

void moveram_to_disk(struct proc* p)
{
  int i;
  for(i=0;i<MAX_PSYC_PAGES;i++)
  {
    if(p->disk_pg_arr[i].va==(char*)-1)
    {
      break;
   }
  }

  if(i>=MAX_PSYC_PAGES)
  panic("disk full\n");
#ifdef SCFIFO

if(p->head==0||p->tail==0)
{
  panic("head or tail null\n");
}
#endif

//cprintf("moveram_to_disk before scfifo\n");
struct free_page * tmp=select_page_to_remove(p);
//cprintf("moveram_to_disk after scfifo\n");
insert_to_disk(p,i,tmp);
//cprintf("after inserting page to swapfile \n");
}

// Allocate page tables and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
int
allocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{

  #ifdef NONE
  return oldallocuvm(pgdir,oldsz,newsz);
  #endif

if((myproc()!=0)&&((myproc()->pid==1)||(myproc()->parent->pid==1)))
  return oldallocuvm(pgdir,oldsz,newsz); //sh and init dont not do any paging work like NONE only sh is child of init with pid=1
  char *mem;
  uint a;
  struct proc* p =myproc();
  if(newsz >= KERNBASE)
    return 0;
  if(newsz < oldsz)
    return oldsz;
     if (PGROUNDUP(newsz)/PGSIZE > MAX_TOTAL_PAGES) {
        cprintf("proc is too big\n", PGROUNDUP(newsz)/PGSIZE);
        return 0;
      }
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
  //  cprintf("kfree mem allocuvm\n");
    kfree(mem);
  //  cprintf("kfree mem allocuvm done\n");
    return 0;}

    if(p->pages_in_memory_counter>=MAX_PSYC_PAGES)//need to swap
    {
      if(p->pages_in_swapfile_counter>=MAX_PSYC_PAGES)//cant swap
      {
        cprintf("pages in swap file counter is %d  pages in memory counter is %d \n", p->pages_in_swapfile_counter, p->pages_in_memory_counter);
        panic("swap file and ram full should have detected before starting to allocate memmory\n");
      }

      moveram_to_disk(p);
      update_memory(p,(char*)a);
      //insert_disk(p,(char*)a);
    }
    else//there is place in ram just insert
    {
      //cprintf("counter of pages in memory is %d\n",p->pages_in_memory_counter);
      update_memory(p,(char*)a);
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
//  cprintf("deallocuvm\n");
  //cprintf("deallocuvm\n");
  pte_t *pte;
  uint a, pa;
  #ifndef NONE
  struct proc* p =myproc();
  #endif
  if(newsz >= oldsz)
    return oldsz;
  a = PGROUNDUP(newsz);
  for(; a  < oldsz; a += PGSIZE){
    #ifndef NONE
    int found=0;
    #endif
    pte = walkpgdir(pgdir, (char*)a, 0);
    if(!pte)
      a = PGADDR(PDX(a) + 1, 0, 0) - PGSIZE;
    else if((*pte & PTE_P) != 0){
      pa = PTE_ADDR(*pte);
      if(pa == 0)
      {
        panic("kfree_deallocuvm");
      }
      char *v = P2V(pa);
      #ifndef NONE
      
      int i=0;

      for(i=0;i<MAX_PSYC_PAGES;i++)
      {
        if(p->memory_pg_arr[i].va==(char*)a)
        {
          if(p->memory_pg_arr[i].next!=0)
          {
            p->memory_pg_arr[i].next->prev=p->memory_pg_arr[i].prev;//next->prev=prev
          }
          if(p->memory_pg_arr[i].prev!=0)
          {
            p->memory_pg_arr[i].prev->next=p->memory_pg_arr[i].next;//prev->next=next
          }
          if(&p->memory_pg_arr[i]==p->head)
          {
            p->head=p->head->next;
          }
          if(&p->memory_pg_arr[i]==p->tail)
          {
            p->tail=p->tail->prev;
          }
          p->memory_pg_arr[i].next=0;
          p->memory_pg_arr[i].prev=0;
          p->memory_pg_arr[i].age=default_age;
          p->memory_pg_arr[i].va=(char*)-1;
          found++;
          if(p->pgdir==pgdir)
        p->pages_in_memory_counter--;
            
        }
      }

      #endif
     // cprintf("kfree v deallocuvm\n");
      kfree(v);
    //  cprintf("kfree v dealocuvm done\n");
      *pte = 0;
    }
    #ifndef NONE
    else if((*pte & PTE_PG) != 0)
    {
      *pte=0;
      int i;
      for(i=0;i<MAX_PSYC_PAGES;i++)
      {
        if(p->disk_pg_arr[i].va==(char*)a)
        {
          p->disk_pg_arr[i].va=(char*)-1;
          p->disk_pg_arr[i].location=-1;
          found++;
          if(p->pgdir==pgdir)
         p->pages_in_swapfile_counter--;
        }
      }
    }
        if(found>1)
      {
        cprintf("found= %d \n",found);
        panic("found same virtual address more then once in the data strucure\n");
      }
   #endif
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
    if(!(*pte & PTE_P)&&(!(*pte & PTE_PG)))
      panic("copyuvm: page not present");
    {
      if(*pte& PTE_PG)
      {
      fixPagedOutPTE(d,i,0);
      continue;
      }
    }
    pa = PTE_ADDR(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      goto bad;
    memmove(mem, (char*)P2V(pa), PGSIZE);
    if(mappages(d, (void*)i, PGSIZE, V2P(mem), flags) < 0)
      goto bad;
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

//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.

