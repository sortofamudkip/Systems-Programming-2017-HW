#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/select.h>
#include <time.h>
#include <signal.h>

#define ISLEAF -2
#define NOTLEAF -1
#define ISZERO 0
#define ISONE 1

typedef struct data {
	int id;
	float features[33];
	int label;
} Data;

typedef struct node {
	float threshold;
	int dimension;
	short value; //0 or 1
	struct node *left, *right;
} Node;

Node* create_node(float threshold, int dimension, short value) {
	Node *n = (Node *)malloc(sizeof(Node));
	n->left = n->right = NULL;
	n->threshold = threshold;
	n->dimension = dimension;
	n->value = value;
	return n;
}

void print_trees();

int ones_count(Data *entries[], int left, int right) {
	int one_count = 0;
	for (int i = left; i <= right; ++i) {
		if (entries[i]->label == 1) one_count++;
	}
	if (one_count == (right-left+1)) return -1;
	return one_count;
}

void err_sys(char *s) {perror(s); exit(1);}

float getputa(Data *entries[], int index, int start, int end){
	float left_1 = 0, left_0 = 0, right_1 = 0, right_0 = 0;
	int i;
	for (i = start; i <= index; ++i) {
		if(entries[i]->label == 1) left_1++;
		if(entries[i]->label == 0) left_0++;
	}
	left_1 /= (index - start + 1); left_0 /= (index - start + 1);
	for (i = index+1; i <= end; ++i) {
		if(entries[i]->label == 1) right_1++;
		if(entries[i]->label == 0) right_0++;
	}
	right_1 /= (end - index); right_0 /= (end - index);

	float GINI = left_1 * (1-left_1) + left_0 * (1-left_0) + right_1 * (1-right_1) + right_0 * (1-right_0);
	return GINI;
}

///sorting by dimensions

void swap_entries(Data **a, Data **b) {
	Data *t = *a; *a = *b; *b = t;
}

int partition(Data *entries[], int left, int right, int dimension) {
	int pivot = right, divider = left;
	for (int i = left; i <= right; i++) {
		if (entries[i]->features[dimension] < entries[pivot]->features[dimension]) {
			swap_entries(&entries[i], &entries[divider++]);
		}
	}
	swap_entries(&entries[pivot], &entries[divider]);
	return divider;
}

void quicksork(Data *entries[], int left, int right, int dimension) {
	if (left < right) {
		int p = partition(entries, left, right, dimension);
		quicksork(entries, left, p-1, dimension);
		quicksork(entries, p+1, right, dimension);
	}
}

void traverse(Node *root) {
	if (root) {
		traverse(root->left);
		if (root->dimension == ISLEAF) printf("leaf: value = %d\n", root->value);
		else printf("node: dimension = %d, threshold = %f\n", root->dimension, root->threshold);
		traverse(root->right);

	}
}




