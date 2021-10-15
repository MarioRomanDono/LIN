#include <sys/syscall.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>

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
    
	if (strlen(argv[1]) != 3 || argv[1][0] != '0' || argv[1][1] != 'x' || argv[1][2] < '0' || argv[1][2] > '7' ) {
		fprintf(stderr, "Ledmask must be an hex number between 0x0 and 0x7\n");
		return -1;
	}


	mask = strtoul(argv[1], NULL, 16);

	res = ledctl(mask);

	if (res != 0) {
		perror("Error during ledctl execution");
		return -1;
	}

	return 0;
}
