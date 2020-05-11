#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#define EXIT_ERROR 255

int main(int argc, char const *argv[]) {
	char filename[1001]; 
	sleep(10);
	int len = read(0, filename, 1000);
	filename[len] = '\0';
	int f = open(filename, O_RDONLY);
	if (f == -1) {
		write(STDERR_FILENO, "from file reader: bad file name", strlen("from file reader: bad file name"));
		write(STDOUT_FILENO, "404 Not Found", strlen("404 Not Found"));
		fsync(STDOUT_FILENO);
		return EXIT_ERROR;
	}
	char buffer[1025]; int n;
	while((n = read(f, buffer, 1024)) != 0) {
		buffer[n] = '\0';
		write(STDOUT_FILENO, buffer, n);
		fsync(STDOUT_FILENO);
	}
	return 0;
}