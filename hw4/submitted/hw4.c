#include "hw4.h"
#include <pthread.h>
#define ENTRY_COUNT 128

volatile int treeCount;
volatile int threadCount;
volatile int jobCount;

Data *lianzi; //testing data array, gonna free this later so we don't MLE
int lianzi_n;
Data *cezi; 
volatile int ceziCount;
Node *Trees[30000];

void init();
void read_train();

//for training
pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t stupid = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t wetmon = PTHREAD_COND_INITIALIZER;
//for testing
pthread_mutex_t fe = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t paciencia = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t carajo = PTHREAD_COND_INITIALIZER;


unsigned int _tid() {return (unsigned int)pthread_self() % 1000;}
//divide using boundaries

Node* create_root(Data *entries[], int left, int right) { 
	// fprintf(stderr, "at %3u\n", _tid());
	// fprintf(stderr, "%3u: left: %d, right: %d\n", _tid(), left, right);
	int zeroes = ones_count(entries, left, right);
	if (zeroes <= 0) { //base case: all 0s or 1s
		// printf("base case at [%d, %d]\n", left, right);
		Node *leaf = create_node(ISLEAF, ISLEAF, -zeroes); //0: all zeros; -1: all ones
		return leaf;
	}
	int ones = (right - left + 1) - zeroes;
	float best_puta = 1000.02; int best_cut = -1, best_dim = -1;
	for (int i = 0; i < 33; ++i) { //for each dim
		quicksork(entries, left, right, i); //sort by that dimension
		float best_local_puta = 532.0; int best_local_cut = -1; //whatever
		for (int j = left; j < right; ++j) { //for each cut position; intentionally excluding rightmost
			float local_puta = getputa(entries, j, left, right);
			// printf("    local cut at %dth pos: puta value %f\n", j, local_puta);
			if (local_puta < best_local_puta) best_local_puta = local_puta, best_local_cut = j;
		}
		if (best_local_puta < best_puta) {
			best_puta = best_local_puta, best_cut = best_local_cut;
			best_dim = i;
		}
		// printf("    best cut pos: at %dth dim, pos = %d, puta value %f\n", i, best_local_cut, best_local_puta);
	}
	// fprintf(stderr, "    best puta: %f, best cut pos: %d, best dim: %d\n", best_puta, best_cut, best_dim);
	assert(best_cut != -1 && best_dim != -1);
	//best dim or best cut is too large
	quicksork(entries, left, right, best_dim);
	float threshold = (entries[best_cut]->features[best_dim] + entries[best_cut+1]->features[best_dim]) / 2.0;
	Node *new = create_node(threshold, best_dim, NOTLEAF);
	new->left = create_root(entries, left, best_cut);
	new->right = create_root(entries, best_cut+1, right);
	return new;
}

volatile int dead_threads = 0;

void* rand_tree_thread(void *arg) {
	while (1) {
		pthread_mutex_lock(&m);
		if (jobCount == treeCount) {
			pthread_mutex_lock(&stupid);
			dead_threads++;
			pthread_cond_signal(&wetmon);
			pthread_mutex_unlock(&m);
			pthread_mutex_unlock(&stupid);
			pthread_exit((void*)0); //if no more jobs, just die
		}
		int pos = jobCount; //save this pos
		// fprintf(stderr, "%3u: at job count %d\n", _tid(), jobCount);
		jobCount++; //we've taken this job
		pthread_mutex_unlock(&m);
		// fprintf(stderr, "%3u: unlocked\n", _tid());
		//otherwise, create a tree
		// for (unsigned long long i = 0; i < 87310304; i++)  {}
	    //fprintf(stderr, "%3u: creating tree at pos %d\n", _tid(), pos);
		Data *entries[ENTRY_COUNT] = {}; 
		for (int i = 0; i < ENTRY_COUNT; i++) entries[i] = &lianzi[rand() % lianzi_n]; //some random pos
		Node *root = create_root(entries, 0, ENTRY_COUNT-1);
		Trees[pos] = root;
		assert(root != NULL);
		//fprintf(stderr, "%3u: done creating tree at pos %d\n", _tid(), pos);
	}
	return (void *)0;
}

void create_random_forest() {
	pthread_t tid[threadCount];
	//create N threads, each thread will create trees
	for (int i = 0; i < threadCount; ++i) {
		pthread_create(&tid[i], NULL, rand_tree_thread, (void *)0);
	}
	pthread_mutex_lock(&stupid);
	while (dead_threads != threadCount) {
		pthread_cond_wait(&wetmon, &stupid);
    }
	pthread_mutex_unlock(&stupid);
	for (int i = 0; i < threadCount; ++i) {
		pthread_join(tid[i], NULL);
	}
//	fprintf(stderr, "all threads joined\n");
}

//this is for the testing step
volatile int q_dead_threads;
volatile int q_jobCount;
int person_prediction[30000];

int evaluate_tree(Node* root, Data* entry) {
	if (root->dimension == ISLEAF) return root->value; //leaf
	if (root->threshold > entry->features[root->dimension]) return evaluate_tree(root->left, entry);
	return evaluate_tree(root->right, entry);
}

int evaluate_all_trees(Data *entry) { //we can parellelize this too
	int score[2] = {};
	for (int i = 0; i < treeCount; ++i) { //for eaach tree
		score[evaluate_tree(Trees[i], entry)]++;
	}
	return score[0] > score[1] ? 0 : 1;
}

void* run_trees(void *arg) {
	while (1) {
		pthread_mutex_lock(&fe);
		if (q_jobCount == ceziCount) {
			pthread_mutex_lock(&paciencia);
			q_dead_threads++;
			pthread_cond_signal(&carajo);
			pthread_mutex_unlock(&fe);
			pthread_mutex_unlock(&paciencia);
			pthread_exit((void*)0); //if no more jobs, just die
		}
		int pos = q_jobCount; //save this pos
		q_jobCount++; //we've taken this job
		pthread_mutex_unlock(&fe);
		//do the job

		Data *query = &cezi[pos];
		//fprintf(stderr, "%3u: evaluating job %d\n", _tid(), pos);
		person_prediction[query->id] = evaluate_all_trees(query);

		//fprintf(stderr, "%3u: done job %d\n", _tid(), pos);
	}
	return (void *)0;

}


void parallelize_query() {
	pthread_t tid2[threadCount];
	for (int i = 0; i < threadCount; ++i) {
		pthread_create(&tid2[i], NULL, run_trees, (void *)0);
	}
	pthread_mutex_lock(&paciencia);
	while (q_dead_threads != threadCount) {
		pthread_cond_wait(&carajo, &paciencia);
	}
	pthread_mutex_unlock(&paciencia);
	for (int i = 0; i < threadCount; ++i) {
		pthread_join(tid2[i], NULL);		
	}
   // fprintf(stderr, "all parallelize threads joined\n");
}

int predict_people(FILE *test_fp) {
	assert(test_fp); int max = -1;
	//read in test data, and then save result
	cezi = (Data *)malloc(30000 * sizeof(Data));
	while (~fscanf(test_fp, "%d", &cezi[ceziCount].id)) {
		for (int i = 0; i < 33; i++) fscanf(test_fp, "%f", &cezi[ceziCount].features[i]);
		max = cezi[ceziCount].id > max ? cezi[ceziCount].id : max;
		ceziCount++;
	}
	// for (int i = 0; i < ceziCount; ++i) {
	// 	printf("person %d:\n", cezi[i].id);
	// 	for (int j = 0; j < 33; j++) printf("%f ", cezi[i].features[j]); puts("");
	// } puts("");
	// fprintf(stderr, "read success, max = %d\n", max);
	// assert(max != -1);
	for (int i = 0; i < max; i++) person_prediction[i] = -1;
	parallelize_query();
	return max;
}

void print_results();

int main(int argc, char *argv[]) {
	FILE *train_fp, *test_fp;
	init(argc, argv, &train_fp, &test_fp);
	read_train(train_fp);
	create_random_forest();
	free(lianzi); fclose(train_fp);
	// print_trees();
	int max_id = predict_people(test_fp);
	print_results(argv[4], max_id);
	return 0;
}

void init(int argc, char *argv[], FILE **train_fp, FILE **test_fp) {
	if (argc != 9) {
		puts("usage: ./hw4 -data [data_dir] -output submission.csv -tree [tree_number] -thread [thread_number]");
		exit(0);
	}
	srand(time(NULL));

	lianzi = (Data *)malloc(30000*sizeof(Data));
	if (!lianzi) err_sys("not enough memory for testing_data");
	treeCount = atoi(argv[6]);
	threadCount = atoi(argv[8]);
	char train_name[50], test_name[50]; 
	sprintf(train_name, "%s/training_data", argv[2]);
	sprintf(test_name, "%s/testing_data", argv[2]);
	FILE *train = fopen(train_name, "r");
	if (!train) err_sys("no such training data file");
	*train_fp = train;
	FILE *test = fopen(test_name, "r");
	if (!test) err_sys("no such testing data file");
	*test_fp = test;
}

void read_train(FILE *train_fp) {
	while (~fscanf(train_fp, "%d", &lianzi[lianzi_n].id)) { //id
		//labels
		for (int i = 0; i < 33; i++) fscanf(train_fp, "%f", &lianzi[lianzi_n].features[i]);
		fscanf(train_fp, "%d", &lianzi[lianzi_n++].label);
	}
}


void print_trees() {
	for (int i = 0; i < treeCount; ++i) {
		puts("---tree start ---");
		traverse(Trees[i]);
		puts("---tree end   ---");
	}
}

void print_results(char ofile[], int max_id) {
	FILE *output = fopen(ofile, "w");
	assert(output != NULL);
	fprintf(output, "id,label\n");
	for (int i = 0; i < max_id; ++i) {
		if (person_prediction[i] != -1) fprintf(output, "%d,%d\n", i, person_prediction[i]);
	}
}
