#include "codec.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stddef.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>

static int flag=0;

typedef void (*thread_func_t)(char* data, int key);

typedef struct{
    thread_func_t   func;
    char*    data; 
    int     key;
    struct work_t   *next;
    int     index;
} work_t;

typedef struct{
    work_t    *work_first;
    work_t   *work_last;
    pthread_mutex_t  work_mutex;
    pthread_cond_t   work_cond;
    pthread_cond_t   working_cond;
    pthread_mutex_t  flag_mutex;
    pthread_cond_t   flag_cond;
    size_t           working_cnt;
    size_t           thread_cnt;
    bool             stop;
}tpool_t;

tpool_t *tpool_create(size_t num);
bool tpool_add_work(tpool_t *tm, thread_func_t func, char* data, int key, int index);
static work_t *tpool_work_create(thread_func_t func, char* data, int key, int index);
void tpool_wait(tpool_t *tm);
void tpool_destroy(tpool_t *tm);
static void tpool_work_destroy(work_t *work);
static void *worker(void *arg);
static work_t *tpool_work_get(tpool_t *tm);

tpool_t *tpool_create(size_t num)
{
    tpool_t   *tm;
    pthread_t  thread;

    tm = calloc(1, sizeof(*tm));
    tm->thread_cnt = num;

    pthread_mutex_init(&(tm->work_mutex), NULL);
    pthread_cond_init(&(tm->work_cond), NULL);
    pthread_cond_init(&(tm->working_cond), NULL);

    tm->work_first = NULL;
    tm->work_last  = NULL;

    for (int i=0; i<num; i++) {
        pthread_create(&thread, NULL, worker, tm);
        pthread_detach(thread);
    }

    return tm;
}

bool tpool_add_work(tpool_t *tm, thread_func_t func, char* data, int key, int index) //
{
    work_t *work;
    
    if (tm == NULL)
        return false;
    
    work = tpool_work_create(func, data, key, index);
    if (work == NULL)
        return false;
    
    pthread_mutex_lock(&(tm->work_mutex));
    if (tm->work_first == NULL) {
        tm->work_first = work;
        tm->work_last  = tm->work_first;
    } else {
        tm->work_last->next = work;
        tm->work_last       = work;
    }


    pthread_cond_broadcast(&(tm->work_cond));
    pthread_mutex_unlock(&(tm->work_mutex));

    return true;
}

static work_t *tpool_work_create(thread_func_t func, char* data, int key, int index)  //
{
    work_t *work;

    if (func == NULL)
        return NULL;


    work = malloc(sizeof(*work));
    work->func = func;

    work->data = strdup(data);


    work->key  = key;
    work->index  = index;
    work->next = NULL;
    return work;
}

void tpool_wait(tpool_t *tm)
{
    pid_t tid = gettid();

    if (tm == NULL)
        return;

    pthread_mutex_lock(&(tm->work_mutex));
    while (1) {
        // if ((!tm->stop && tm->working_cnt != 0) || (tm->stop && tm->thread_cnt != 0)) {
        if (tm->work_first != NULL){
            pthread_cond_wait(&(tm->working_cond), &(tm->work_mutex));
        } else {
            break;
        }
    }
    pthread_mutex_unlock(&(tm->work_mutex));
}

void tpool_destroy(tpool_t *tm)
{
    pid_t tid = gettid();

    work_t *work;
    work_t *work2;

    if (tm == NULL)
        return;

    pthread_mutex_lock(&(tm->work_mutex));
    work = tm->work_first;
    while (work != NULL) {
        work2 = work->next;
        tpool_work_destroy(work);
        work = work2;
    }

    tm->stop = true;
    pthread_cond_broadcast(&(tm->work_cond));
    pthread_mutex_unlock(&(tm->work_mutex));
    
    tpool_wait(tm);

    pthread_mutex_destroy(&(tm->work_mutex));
    pthread_cond_destroy(&(tm->work_cond));
    pthread_cond_destroy(&(tm->working_cond));

    free(tm);

}

static void tpool_work_destroy(work_t *work)
{
    if (work == NULL)
        return;
    free(work->data);
    free(work);
}

static work_t *tpool_work_get(tpool_t *tm)
{
    work_t *work;

    if (tm == NULL)
        return NULL;

    work = tm->work_first;
    if (work == NULL)
        return NULL;

    if (work->next == NULL) {
        tm->work_first = NULL;
        tm->work_last  = NULL;
    } else {
        tm->work_first = work->next;
    }

    return work;
}


static void *worker(void *arg)
{
    pid_t tid = gettid();
    tpool_t *tm = arg;
    work_t *work;

    while (1) {
        pthread_mutex_lock(&(tm->work_mutex));

        while (tm->work_first == NULL && !tm->stop)
            pthread_cond_wait(&(tm->work_cond), &(tm->work_mutex));

        if (tm->stop)
            break;
        
        work = tpool_work_get(tm);
        tm->working_cnt++;
        pthread_mutex_unlock(&(tm->work_mutex));

        if (work != NULL) {
            work->func(work->data, work->key);
            
        }

        pthread_mutex_lock(&(tm->flag_mutex));
        while(1){
            if (work->index == flag){
                fprintf(stdout, "%s", work->data);
                // printf("%s", work->data);
                flag++;
                pthread_cond_signal(&(tm->flag_cond));
                tpool_work_destroy(work);
                break;
            }
            else{
                pthread_cond_wait(&(tm->flag_cond), &(tm->flag_mutex));
            }
        }
        pthread_mutex_unlock(&(tm->flag_mutex));

        pthread_mutex_lock(&(tm->work_mutex));
        tm->working_cnt--;
        if (!tm->stop && tm->working_cnt == 0 && tm->work_first == NULL)
            pthread_cond_signal(&(tm->working_cond));
        pthread_mutex_unlock(&(tm->work_mutex));
    }

    tm->thread_cnt--;
    pthread_cond_signal(&(tm->working_cond));
    pthread_mutex_unlock(&(tm->work_mutex));
    return NULL;
}


int main(int argc, char *argv[])
{
    pid_t tid = gettid();
    int key = atoi(argv[1]);
    if (argc < 2)
	{
	    printf("usage: key flag < file \n");
	    printf("!! data more than 1024 char will be ignored !!\n");   	  
        return 0;         
    }   

    thread_func_t func;
    if (strcmp(argv[2], "-e")== 0){
        func = &encrypt;
    }
    else if(strcmp(argv[2], "-d")== 0){
        func = &decrypt;
    }
    else{
        printf("usage: ./coder key flag \n");
	    return 0;
    }

    int numCPU = sysconf(_SC_NPROCESSORS_ONLN);    
	tpool_t *tm;
    tm = tpool_create(numCPU);

	char c;
	int counter = 0;
	int dest_size = 1024;
	char data[dest_size];
    int index = 0; 

	while ((c = getchar()) != EOF)
	{
		data[counter] = c;
		counter++;

		if (counter == 1024){
            tpool_add_work(tm, func, data, key, index);
            index ++;
			counter = 0;
		}
	}
    
	if (counter > 0)
	{
		char lastData[counter];
		lastData[0] = '\0';
		strncat(lastData, data, counter);
        tpool_add_work(tm, func, lastData, key, index);

	}

    tpool_wait(tm);

    tpool_destroy(tm);

	return 0;
}
