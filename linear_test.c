#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"

#define PGSIZE 4096
#define  ARR_SIZE PGSIZE*20


int main(int argc, char *argv[]){
  
  printf(1,"linear test main\n");

  char * arr;
  int i;
  printf(1,"allocation with sbrk\n");
  arr = sbrk(ARR_SIZE); //asking for 20 pages so some will be in the swapfile
  printf(1,"going over the array putting B char in every even place and A in odd place\n");
  for(i=0; i<ARR_SIZE; i++){
    sleep(0);
    if(i%2==0)
    arr[i]='B';
	else
	arr[i]='A';
    if((i % (PGSIZE/2)) == 0){  //print every PGSIZE/2 digits should prints only B
      printf(1,"%c",arr[i]);
    }
  }
  printf(1,"\n\n");
  sbrk(-ARR_SIZE);//deallocating the memory we asked for only program memory should stay
  exit();
}