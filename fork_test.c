
#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"

int main(int argc, char *argv[])

{  int i;
  char * arr;
  arr = malloc (50000);
  for (i = 0; i < 50; i++) { 
    arr[49100+i] = 'A'; 
    arr[45200+i] = 'B';
  }
  arr[49100+i] = 0; //for null terminating string...
  arr[45200+i] = 0;
  
  if (fork() == 0){ //is son
    for (i = 40; i < 50; i++) { 
	    arr[49100+i] = 'C'; //changes last ten A's to C
	    arr[45200+i] = 'D'; //changes last ten B's to D
  	}
    printf(1, "SON: %s\n",&arr[49100]); // should print AAAAA..CCC...
    printf(1, "SON: %s\n",&arr[45200]); // should print BBBBB..DDD...
  	printf(1,"\n");
    free(arr);
    exit();
  } else { //is parent
    wait();
    printf(1, "PARENT: %s\n",&arr[49100]); // should print AAAAA...
    printf(1, "PARENT: %s\n",&arr[45200]); // should print BBBBB...
    free(arr);
    exit();
  }
}
