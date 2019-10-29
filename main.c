/* sample program to test out instrumenting source code using perf_event_open */

#include <asm/unistd.h>
#include <assert.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/perf_event.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>

#include "garrus.h"

/* defining the number of threads we want to use to run the do_something function 
 #define NR_THREADS 1
* deprecated, using command line args instead
*/

/* macro which sets the size of the read buffer for the event counters */
#define BUF_SIZE 4096 

/* macro for setting the size of the array */
#define N_ARR 128

void *
do_something (void *arg)
{
	int j;
    int loops = *(int *)arg;
	while (j < loops)
	{
		struct perf_event_attr attr;
		uint64_t id1, id2, id3;
		uint64_t val1, val2, val3;
		int fd1, fd2, fd3;
		char buf[BUF_SIZE];

		struct rf_event_group *rf = (struct rf_event_group *)buf;
		int i;
		int cache_code;
		pthread_t id = pthread_self ();
		
		fd1 = initialize_event_group (&attr, PERF_TYPE_SOFTWARE, PERF_COUNT_SW_PAGE_FAULTS, 1);
		if (fd1 == -1)
		{
		      fprintf(stderr, "Error opening leader %llx ; Caused by thread: %lu opening event : page fault\n", attr.config, id);
		      exit(EXIT_FAILURE);
		}
		set_identifier (fd1, &id1);

        cache_code = get_hw_cache_event_code (PERF_COUNT_HW_CACHE_DTLB, 
							  PERF_COUNT_HW_CACHE_OP_READ, 
							  PERF_COUNT_HW_CACHE_RESULT_MISS);
		
		fd2 = add_event_group (&attr, PERF_TYPE_HW_CACHE, cache_code,
						1,fd1);
		if (fd2 == -1)
		{
		      fprintf(stderr, "Error opening leader %llx ; Caused by thread: %lu opening event : dTLB read miss\n", attr.config, id);
		      exit(EXIT_FAILURE);
		}
		set_identifier (fd2, &id2);

		cache_code = get_hw_cache_event_code (PERF_COUNT_HW_CACHE_DTLB, 
							  PERF_COUNT_HW_CACHE_OP_READ, 
							  PERF_COUNT_HW_CACHE_RESULT_ACCESS);
		
		fd3 = add_event_group (&attr, PERF_TYPE_HW_CACHE, cache_code,
						1, fd1);
		if (fd3 == -1)
		{
		      fprintf(stderr, "Error opening leader %llx ; Caused by thread: %lu opening event dTLB read hit\n", attr.config, id);
		      exit(EXIT_FAILURE);
		}
		set_identifier (fd3, &id3);	
        
		start_event_group (fd1);	
		
        /*start of section we want to measure */
		char* ptr;

		ptr = malloc(N_ARR * N_ARR * N_ARR);
		for (i = 0; i < N_ARR * N_ARR * N_ARR; i++) {
			ptr[i] = (char) (i & 0xff); // pagefault
		}
		free(ptr);
		/* end of section we want to measure */
		
		stop_event_group (fd1);	
		read (fd1, buf, sizeof(struct rf_event_group));
		
        for (i = 0; i < rf->nr; i++) {
			if (rf->values[i].id == id1) {
				printf ("id1 found at index: %d\n",i);
				val1 = rf->values[i].value;
			} 
			else if (rf->values[i].id == id2) {
				printf ("id2 found at index: %d\n",i);
				val2 = rf->values[i].value;
			}
			else if (rf->values[i].id == id3) {
				printf ("id3 found at index: %d\n",i);
				val3 = rf->values[i].value;
			}
		}

		printf("%"PRIu64",%"PRIu64",%"PRIu64"\n", val1, val2, val3);
        close (fd1);
        close (fd2);
        close (fd3);
		j++;
	}
}

int
main (int argc, char **argv)
{
	if (argc != 4)
	{
		fprintf(stderr, "Correct usage: %s <NR_THREADS> <iterations> <loops>\n", argv[0]);
		exit(EXIT_FAILURE);		
	}

    /* parsing command line args */
	printf ("%lu\n", sizeof (struct rf_event_group));
	const int NR_THREADS = atoi (argv[1]);
    const int iterations = atoi (argv[2]);
    const int loops = atoi (argv[3]);
	pthread_t threadIds[NR_THREADS];
    const unsigned long arr_size = N_ARR * N_ARR * N_ARR;
	int errorCode; /* return value of pthread_create */
	int out_fd, err_fd;

    /* obtaining the timestamp for appending to the filename */
    unsigned long timestamp = (unsigned long) time (NULL);
    
    chdir ("/home/shreyas/sample_programs/perf_events/results");

    /*opening the file for writing */
    char file_path[100];
    sprintf (file_path, "results_%d_%d_%d_%d_%lu.csv", NR_THREADS, iterations, loops, N_ARR, timestamp);
    
    /* opening a file for dumping errors */
    char err_path[100];
    sprintf (err_path, "errors_%d_%d_%d_%d_%lu.txt", NR_THREADS, iterations, loops, N_ARR, timestamp);
    
    /* opening and creating files */
    out_fd = open (file_path, O_CREAT | O_RDWR, 0644 );
    err_fd = open (err_path, O_CREAT | O_RDWR, 0644 );
    
    /* duplicating file descriptors */
    dup2 (out_fd, STDOUT_FILENO);
    close (out_fd);
    dup2 (err_fd, STDERR_FILENO);
    close (err_fd);
    
    printf ("#Test for NR_THREADS= %d ; Iterations= %d ; Loops= %d, ARR_SIZE=%lu\n", NR_THREADS, iterations, loops, arr_size);
    printf ("#Test 3: Using a single printf to dump output\n");
    printf ("Page Faults, dTLB read miss, dTLB read hit\n");
    
    /* spawning the threads */
	for (int iters = 0 ; iters < iterations ; iters++)
	{
		for(int i = 0; i < NR_THREADS ; i++)
		{
		    errorCode = pthread_create (&(threadIds[i]), NULL, &do_something, (void *)&loops);
		    if (errorCode != 0)
		    {
			printf ("Failed to initialize thread %d...\n",i);
		    }	
		}

		/* waiting on threads to complete */
		for (int i = 0 ; i < NR_THREADS ; i++)
		{
		    pthread_join (threadIds[i], NULL);
		}
	}	
	return 0;
}
