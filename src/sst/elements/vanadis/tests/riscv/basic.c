#include <stdio.h>

void myFunc(int *valueIn) {
   int a = 42;

   (*valueIn) = (*valueIn) + a;
}

int main( int argc, char* argv[] ) {
   int n = 0x9090;

   myFunc(&n);

	return 0;
}
