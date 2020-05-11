#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>

void err_sys(const char *x) {
	perror(x); 
	exit(1);
}

char read_FIFO[50] = {};
char write_FIFO[50] = {};

void create_FIFO_names(char player_index, const char* hostname) {
	int host_int = atoi(hostname);
	//for read_FIFO: tack on "host", then tostring(host_id), then _, then the char player_index, then ".FIFO"
	//for write_FIFO: tack on "host", then tostring(host_id), then ".FIFO"
	strcpy(read_FIFO, "host");
	strcpy(write_FIFO, "host");
	strcpy(&read_FIFO[4], hostname);
	strcpy(&write_FIFO[4], hostname);
	char temp[10] = "_X.FIFO";
	temp[1] = player_index;
	if (0 < host_int && host_int <= 9) {
		strcpy(&read_FIFO[5], temp);
		strcpy(&write_FIFO[5], ".FIFO");
	}
	else {
		strcpy(&read_FIFO[6], temp);
		strcpy(&write_FIFO[6], ".FIFO");
		
	}
}

int main(int argc, char const *argv[]) {
	if (argc != 4) err_sys("usage: host_id[] player_index{A,B,C,D} randomkey[0, 65535]");
	char player_index = *argv[2]; //just get the char
	int index = player_index - 'A';
	create_FIFO_names(player_index, argv[1]);
	int random_key = atoi(argv[3]);

	// printf("opened %s amd %s\n", read_FIFO, write_FIFO);

	// printf("random_key = %d\n", random_key);

	// int read_fd = open(read_FIFO, O_RDONLY);
	// int write_fd = open(write_FIFO, O_WRONLY);
	// puts("here");
	FILE* read_FP = fopen(read_FIFO, "r");
	FILE* write_FP = fopen(write_FIFO, "w");
	int A[4];
	for (int i = 0; i < 10; ++i) {
		fscanf(read_FP, "%d%d%d%d", &A[0], &A[1], &A[2], &A[3]);
		int cash = A[index];
		// int to_give = rand() % cash; //THIS IS FOR PLAYER_BONUS
		int to_give = (index == i % 4) ? cash : 0;
		fprintf(write_FP, "%c %d %d\n", player_index, random_key, to_give);
		fflush(write_FP);
	}
	fclose(read_FP); 
	fclose(write_FP);

	exit(0);

	//for 10 times:
	//  you will read 4 ints into an array of A = [Amoney Bmoney Cmoney Dmoney]
	//  so the money you get is cash = A[player_index - 'A']
	//  let C = a value in range [0, cash]
	//  then you will print to write_FP 3 things: (use fprintf)
	//  player_index(char) random_key C


	return 0;
}
