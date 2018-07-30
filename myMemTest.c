#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"

#define PGSIZE 4096
#define  ARR_SIZE PGSIZE*15

#define test_len 40

static unsigned int 
x=123456789,y=362436069,z=521288629,w=88675123,v=886756453; 
      /* replace defaults with five random seed values in calling program */ 
unsigned int xorshift(void) // taken from https://mathoverflow.net/questions/29494/pseudo-random-number-generation-algorithms for get random code
{unsigned int t; 
 t=(x^(x>>7)); x=y; y=z; z=w; w=v; 
 v=(v^(v<<6))^(t^(t<<13)); return (y+y+1)*v;}




int main(int argc, char *argv[])
{
	


  printf(1,"random test main\n");
  char * arr;
  int i;
  int tmp=0;
  printf(1,"allocation with malloc\n");
  arr = sbrk(ARR_SIZE); //asking for 20 pages so some will be in the swapfile
  printf(1,"going over the array putting B char in random placesand D in half pagesize interval around the random place\n");
  for(i=0; i<test_len; i++){
    printf(1,"i=%d\n",i);
    tmp=xorshift()%ARR_SIZE;
    arr[tmp]='A';
    int j=PGSIZE/4;
    while(j>0)
    {
      arr[(tmp+j)%ARR_SIZE]='B';
      j--;
    }
    j=-1*PGSIZE/4;
    while(j<0)
    {
      int k=tmp+j;
      if(k<0)
      {
        k=0;
      }
    arr[(k)%ARR_SIZE]='D';
    j++;
    }
    sleep(0);
}
  int j=PGSIZE/4;
  printf(1,"testing last assigment should get  B A\n");
  printf(1,"%c \n",arr[(tmp+j)%ARR_SIZE]);
  printf(1,"%c\n",arr[tmp]);
  printf(1,"\n\n");
  sbrk(-ARR_SIZE);//deallocating the memory we asked for only program memory should stay
  printf(1,"random TEST DONE\n");


char * argv2[]={"fork_test",0};
char * argv3[]={"linear_test",0};
char * argv4[]={"fork_test2",0};

 int pid=fork();
if(pid==0)

{
	exec("fork_test",argv2);
	exit();
}
 sleep(199);
 wait();//wait
 printf(1,"forktest1 done\n");

pid=fork();

if(pid==0)//3rd test
{
//	sleep(199);
	exec("linear_test",argv3);
	exit();
}

sleep(199);
wait();
printf(1,"linear_test done\n");
pid=fork();
if(pid==0)//last test
{
	exec("fork_test2",argv4);
}
sleep(600);
wait();
exit();
}

