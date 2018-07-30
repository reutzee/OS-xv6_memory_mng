#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"

#define PGSIZE 4096
#define ARR_SIZE_FORK PGSIZE*17
#define ARR_SIZE_TEST PGSIZE*17

void forkPageTest(){
  int i;
  char * arr;
  int pid;
  arr = sbrk(ARR_SIZE_FORK); //allocates 20 pages,  so 16 in RAM and 4 in the swapFile
  for(i=0; i<ARR_SIZE_FORK; i++)    {
    arr[i]='M';
}
  printf(1,"\n");
  printf(1,"-----------------------------------\nNow forking - n-----------------------------------\n");
  sleep(20);
  sleep(20);
  printf(1,"FORK\n");
  //Child - change array to C's and print some
  if((pid=fork()) == 0){  
    printf(1,"-----------------------------------\nChild is going to sleep  -\n-----------------------------------\n");
    sleep(300);
    sleep(20);
    sleep(20);
        printf(1,"-----------------------------------\nChild done sleeping -\n-----------------------------------\n");

    int j;
    for(j=0; j<ARR_SIZE_FORK/300; j++){
      arr[j]='C'; //change the whole array for the child   
      if(j==ARR_SIZE_FORK-1)
        arr[j]='x';    
    }
    printf(1,"Child -\n-------\n");
    for(j=0; j<ARR_SIZE_FORK/300; j++){
      if(j % 1000 == 0 || j==ARR_SIZE_FORK-1)
        printf(1,"%c",arr[j]);
    }
    printf(1,"\n\n");
    exit();
  }
  //Parent- change some array letters to 'P's and print some.
  else{
    for(i=0; i<ARR_SIZE_FORK/2; i++){
      arr[i]='P'; //change the some array for the Parent       
      if(i==ARR_SIZE_TEST/2-1)
        arr[i]='X';    
    }
    printf(1,"Parent -\n-------\n");
    for(i=0; i<ARR_SIZE_FORK; i++){
      if(i % 1000 == 0|| i==ARR_SIZE_TEST-1)
        printf(1,"%c",arr[i]);
    }
    printf(1,"\n\n");
    sleep(30);
    wait();
    printf(1,"parent exiting.\n");
    exit();
  }

}