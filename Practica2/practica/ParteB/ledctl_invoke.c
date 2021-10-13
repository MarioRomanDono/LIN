#include <sys/syscall.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <ctype.h>

#define __NR_LEDCTL 442

long ledctl(unsigned int mask) {
	return (long) syscall(__NR_LEDCTL, mask);
}

int main(int argc, char* argv[]) {
	unsigned int mask;
	int res;

	if (argc != 2) {
		fprintf(stderr, "Usage: ./ledctl_invoke <ledmask>\n");
		return -1;
	}
       
	mask = strtoul(argv[1], NULL, 16);

	if (errno != 0) {
		fprintf(stderr, "Ledmask must be a valid hex number\n");
		return -1;
	}
       
	res = ledctl(mask);

	if (res != 0) {
		perror("Error during ledctl execution");
		return -1;
	}

	return 0;
}
