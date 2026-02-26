#include "libtdmm/tdmm.h"

int main(int argc, char *argv[]) {
	t_init(WORST_FIT);
	void * pointer = t_malloc(5*sizeof(int));
	int * intList = (int*)pointer;
	intList[0] = 5;
	intList[1] = 4;
	t_free(pointer);
	return 0;
}
