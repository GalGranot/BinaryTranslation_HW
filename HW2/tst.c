#include <stdio.h>

#define MAX_CALLS 1000

void foo()
{


}


void bar()
{
  foo();

}


void gal()
{

 bar();

}

void main()
{

 int i,j;

 for (j=0; j < 4; j++)
   for (i=0; i < MAX_CALLS; i++)
     gal();

}
