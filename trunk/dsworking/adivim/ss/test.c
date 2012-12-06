#include<stdio.h>
#ifndef AAA
int foo(int x)
#else
int foo(int x, int y)
#endif
{
#ifdef AAA
	x = x - y;
#endif
	return x;
}

int main()
{
	int i;
	int x = 3;
	int y = 2;
	i = foo(x,y);

	printf("%d\n", i);
	return 0;
}


