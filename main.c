#include "libtdmm/tdmm.h"
#include <stdio.h>

int main(int argc, char *argv[]) {
	t_init(WORST_FIT);
	void * pointer = t_malloc(5*sizeof(int));
	void * pointer2 = t_malloc(5*sizeof(int));
	void * pointer3 = t_malloc(5*sizeof(int));
	int * intList = (int*)pointer;
	intList[0] = 5;
	intList[1] = 4;
	t_free(pointer);
	t_free(pointer2);
	return 0;
}
