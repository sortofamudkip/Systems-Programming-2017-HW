#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <stdlib.h>
#include <string.h>
#define READ_SOCKET  0
#define WRITE_SOCKET 1

int player_num[20] = {}; //from 0 to 19
int player_score[20] = {}; //from 0 to 19
int hosts[12] = {}; //from 0 to 11
int all_rounds[4845][4] = {};
int total_rounds = 0;

void reverse(char *s, int n) {
	int left = 0, right = n - 1;
	while (left < right) {
		char c = s[left];
		s[left] = s[right];
		s[right] = c;
		left++, right--;
	}
}

void itoa(int n, char s[]){
	int i, sign;
	if ((sign = n) < 0) n = -n;          
	i = 0;
	do {
		s[i++] = n % 10 + '0';   
	} while ((n /= 10) > 0);
	if (sign < 0) s[i++] = '-';
	s[i] = '\0';
	reverse(s, i);
}

void init() {
	for (int i = 0; i < 20; i++) player_num[i] = i;
	for (int i = 0; i < 12; i++) hosts[i] = i;
}

int pipe_1[12][2] = {}; //child writes into this one, and then parent reads from it
int pipe_2[12][2] = {}; //parent writes into this one, and then child reads from it

void do_host_stuff(int data[]) {
	for (int i = 0; i < 4; ++i) all_rounds[total_rounds][i] = data[i]+1;
	total_rounds++;}

void actually_do_C(int A[], int data[], int start, int end, int index) {
	if (index == 4) {do_host_stuff(data); return;}
	for (int i = start; i <= end && end-i+1 >= 4-index; ++i) {
		data[index] = A[i];
		actually_do_C(A, data, i+1, end, index+1);
	}}

void do_combinations(int A[], int n) {
	int data[4];
	actually_do_C(A, data, 0, n - 1, 0);}

void print_rounds() {
	for (int i = 0; i < total_rounds; ++i) {
		for (int j = 0; j < 4; ++j) {
			printf("%d%c", all_rounds[i][j], " \n"[j == 3]);
		}
	}}

void err_sys(const char *x) {
	perror(x); 
	exit(1);}

typedef struct {
	int fd;
	FILE *r_fp, *w_fp;
} host;

int get_max_host_fd(host host_fd[], int host_num) {
	int max = -1;
	for (int i = 0; i < host_num; ++i) {
		max = (host_fd[i].fd > max) ? host_fd[i].fd : max;
	} return max;
}

void update_stats(int n, int player_id[], int player_rank[]) {
	for (int i = 0; i < n; ++i) {
		player_score[player_id[i]-1] += 4 - player_rank[i];
	}
}

void fork_hosts(int host_num, int player_count) {
	host host_fd[20];
	for (int i = 0; i < host_num; ++i) {
		if (pipe(pipe_1[i]) < 0 || pipe(pipe_2[i]) < 0) err_sys("pipe failed\n");
		if ((host_fd[i].fd = fork()) < 0) err_sys("fork failed\n");
		//the entire program is copied to child here. The child should close two sockets, and the parent should also close two.
		if (host_fd[i].fd == 0) { //we are a child
			//close pipes that we don't need
			close(pipe_1[i][READ_SOCKET]);
			close(pipe_2[i][WRITE_SOCKET]);
			//redirection
			//stdin is now the read socket
			close(STDIN_FILENO);
			dup2(pipe_2[i][READ_SOCKET], STDIN_FILENO);
			//stdout is now the write socket
			close(STDOUT_FILENO);
			dup2(pipe_1[i][WRITE_SOCKET], STDOUT_FILENO);

			char host_name[5] = {};
			itoa(i+1, host_name);
			//then exec
			execl("./host", "./host", host_name, (char *)0); //go run host.c instead
			exit(0);
		}
		else { //we are the parent
			close(pipe_1[i][WRITE_SOCKET]);
			close(pipe_2[i][READ_SOCKET]);
		}
	}
	//get the FILE versions of our child fds
	for (int i = 0; i < host_num; i++) {
		host_fd[i].r_fp = fdopen(pipe_1[i][READ_SOCKET], "r");
		if (!host_fd[i].r_fp) err_sys("cannot open read FP");
		host_fd[i].w_fp = fdopen(pipe_2[i][WRITE_SOCKET], "w");
		if (!host_fd[i].r_fp) err_sys("cannot open write FP");
	}

	//do multiplexing
	int bid_count = 0;
	fd_set master_set; FD_ZERO(&master_set);
	
	int max_fd = -1;
	for (int i = 0; i < host_num; ++i) {
		if (max_fd < pipe_1[i][READ_SOCKET]) max_fd = pipe_1[i][READ_SOCKET];
	}
	for (int i = 0; i < host_num; i++) {
		FD_SET(pipe_1[i][READ_SOCKET], &master_set);
	}

    struct timeval timeout;
    timeout.tv_sec = 0; 
    timeout.tv_usec = 10;

    //give everyone something to do 
    for (int i = 0; i < host_num && i < player_count && i < total_rounds; ++i) {
		fprintf(host_fd[i].w_fp, "%d %d %d %d\n", 
			all_rounds[bid_count][0], all_rounds[bid_count][1], 
			all_rounds[bid_count][2], all_rounds[bid_count][3]);
		// printf("wrote %d %d %d %d to host\n", all_rounds[bid_count][0], all_rounds[bid_count][1], all_rounds[bid_count][2], all_rounds[bid_count][3]);
		fflush(host_fd[i].w_fp);
    	bid_count++;
    }
    int write_count = bid_count;
    int read_count = 0;
	while (read_count < total_rounds) {
		fd_set read_set = master_set;
		int fd_ready = select(max_fd+1, &read_set, NULL, NULL, &timeout);
		for (int i = 0; i < host_num && fd_ready > 0 && read_count < total_rounds; ++i) {
			int current_read_fd = pipe_1[i][READ_SOCKET];
			FILE* current_host_read_fp  = host_fd[i].r_fp;
			FILE* current_host_write_fp = host_fd[i].w_fp;
			if (FD_ISSET(current_read_fd, &read_set)) {
				fd_ready--;
				int player_id[20] = {}, player_rank[20] = {};
				for (int i = 0; i < 4; ++i) { //****NOT ****** PLAYER COUNT
					int a = fscanf(current_host_read_fp, "%d%d", &player_id[i], &player_rank[i]);
					// fprintf(stderr, "player: %d rank: %d\n", player_id[i], player_rank[i]);
					if (!(1 <= player_id[i] && player_id[i] <= player_count)) {
						fprintf(stderr, "bad id: %d\n", player_id[i]);
						fprintf(stderr, "ERROR at read count = %d, write count = %d", read_count, write_count);
						exit(1);
					}
					if (!(1 <= player_rank[i] && player_rank[i] <= 4)) {
						fprintf(stderr, "bad rank: %d\n", player_rank[i]);
						exit(1);
					}
					if (a != 2) err_sys("could not get correct result");
				}
				read_count++;
				update_stats(player_count, player_id, player_rank);
				if (write_count < total_rounds) { //if there are still more rounds
					fprintf(current_host_write_fp, "%d %d %d %d\n", 
						all_rounds[write_count][0], all_rounds[write_count][1], 
						all_rounds[write_count][2], all_rounds[write_count][3]);
					fflush(current_host_write_fp);	
					// printf("wrote %d %d %d %d to host\n", all_rounds[bid_count][0], all_rounds[bid_count][1], all_rounds[bid_count][2], all_rounds[bid_count][3]);
					write_count++;
				}
			}
		}
	}
	for (int i = 0; i < host_num; ++i) {
		fprintf(host_fd[i].w_fp, "-1 -1 -1 -1\n");
		fflush(host_fd[i].w_fp);
		waitpid(host_fd[i].fd, NULL, 0);
	}
}

void insertion_sort(int n, int key[], int satellite[], int is_ascending);

int main(int argc, char const *argv[]) {
	init();
	if (argc != 3) err_sys("usage: a.out host_num[1,12] player_num[4,20]");
	int host_num   = atoi(argv[1]);
	int player_count = atoi(argv[2]);
	do_combinations(player_num, player_count);
	fork_hosts(host_num, player_count);

	int rank[20] = {1};

	insertion_sort(player_count, player_score, player_num, 1);

	for (int i = 1, total_rank = 1; i < player_count; ++i, total_rank++) {
		if (player_score[i] != player_score[i-1]) rank[i] = total_rank+1;
		else rank[i] = rank[i-1];
	}

	insertion_sort(player_count, player_num, rank, 0);

	for (int i = 0; i < player_count; ++i) {
		printf("%d %d\n", player_num[i]+1, rank[i]);
	}
	return 0;
}

void insertion_sort(int n, int keys[], int satellite[], int is_ascending) {
	for (int i = 1; i < n; ++i) {
		int j = i-1, key = keys[i], meow = satellite[i];
		if (is_ascending) {
			while (j >= 0 && keys[j] < key) {
				keys[j+1] = keys[j];
				satellite[j+1] = satellite[j];
				j--;
			}			
		}
		else {
			while (j >= 0 && keys[j] > key) {
				keys[j+1] = keys[j];
				satellite[j+1] = satellite[j];
				j--;
			}
		}
		keys[j+1] = key;
		satellite[j+1] = meow;
	}	
}
