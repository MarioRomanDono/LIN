#include <sys/syscall.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#define __NR_LEDCTL 442

long lin_ledctl(unsigned int mask) {
	return (long) syscall(__NR_LEDCTL, mask);
}

int main(int argc, char* argv[]) {
	unsigned int mask;
	int res;

	if (argc != 2) {
		fprintf(stderr, "Usage: ./ledctl_invoke <ledmask>");
		return -1;
	}

	mask = strtoul(argv[1], NULL, 16);

	if (mask < 0x0) {
		fprintf(stderr, "Ledmask cannot be negative");
		return -1;
	}

	res = lin_ledctl(mask);

	if (res != 0) {
		perror("Error during ledctl execution: ");
		return -1;
	}

	return 0;
}