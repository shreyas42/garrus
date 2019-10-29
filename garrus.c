/* implementation of functions listed in perf_util.h 
   The perf_event_open wrapper was sourced from: 
   http://man7.org/linux/man-pages/man2/perf_event_open.2.html
   The initialize_event_counters was sourced from: 
   https://stackoverflow.com/questions/42088515/perf-event-open-how-to-monitoring-multiple-events
*/

#include <asm/unistd.h>
#include <linux/perf_event.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>

#include "garrus.h"

FILE *writefile;
int nr_threads;
unsigned long *context_map;
pthread_mutex_t context_lock;

struct garrus_meta_t **context_list = NULL;

int context_init(int n_single, int n_group)
{
	int i;
	struct garrus_meta_t *context;
	for (i = 0; i < nr_threads; i++) {
		context = malloc (sizeof(struct garrus_meta_t));
		if (context == NULL) {
			fprintf(stderr, "Out of memory\n");
			return INIT_FAILED;
		}

		context->in_use_single = malloc(n_single * sizeof(int));
		context->in_use_group = malloc(n_group * sizeof(int));
		memset(context->in_use_single, 0, n_single * sizeof(int));
		memset(context->in_use_group, 0, n_group * sizeof(int));

		context->n_single = n_single;
		context->n_group= n_group;
		context->single_init = 0;
		context->ptr_single = 0;
		context->group_init = 0;
		context->ptr_group = 0;
		context_list[i] = context;
	}
	context_map = malloc(nr_threads * sizeof(unsigned long));
	memset(context_map, 0, nr_threads * sizeof(unsigned long));
	pthread_mutex_init(&context_lock, NULL);
	return SUCCESS;
}


int pool_init(struct ev_counter_meta **start, int n, int counter_num_config)
{
	int i;
	struct ev_counter_meta *pool;
	struct ev_counter_meta *this;
	pool = malloc(n * sizeof(struct ev_counter_meta));
	if (pool == NULL) {
		fprintf(stderr, "Out of memory\n");
		return INIT_FAILED;
	}
	memset(pool, 0, n * sizeof(struct ev_counter_meta));
	for (i = 0; i < n ; i++) {
		this = &pool[i];
		if (counter_num_config == COUNTER_NUM_SINGLE) {
			this->size = sizeof(struct rf_event_single);
		} else if (counter_num_config == COUNTER_NUM_GROUP) {
			this->size = sizeof(struct rf_event_group);
		}
		this->num_config =  counter_num_config;
		this->max_n_structs = BUF_SIZE / this->size;
	}
	*start = pool; /* reflecting the pointer in the context struct */
	return SUCCESS;
}

void pool_destroy(struct ev_counter_meta *pool)
{
	if (pool == NULL)
		return ;
	free(pool);
}

void context_destroy()
{
	int i;
	struct garrus_meta_t *context;
	if (context_list == NULL)
		return ; 
	for (i = 0; i < nr_threads; i++) {
		context = context_list[i];
		pool_destroy(context->ev_pool_single);
		pool_destroy(context->ev_pool_group);
		free(context->in_use_single);
		free(context->in_use_group);
		free(context);
	}
	free(context_map);
	pthread_mutex_destroy(&context_lock);
	free(context_list);
}

void close_events(void)
{
	struct garrus_meta_t *context;
	struct ev_counter_meta *pool;
	struct ev_counter_meta *this;
	int i, j, k;
	int n_items;

	/* iterate through all events in each event pool and 
	 * close the file descriptors for each of them
	 */
	for (k = 0; k < nr_threads; k++) {
		context = context_list[k];
		n_items = context->single_init; /* iterate through all initialized events */
		pool = context->ev_pool_single;
		for (i = 0 ; i < n_items ; i++) {
			this = &(pool[i]);
			close(this->leader_fd.fd);
		}
		n_items = context->group_init;
		pool = context->ev_pool_group;
		for (i = 0 ; i < n_items ; i++) {
			this = &(pool[i]);
			close(this->leader_fd.fd);
			for (j = 0 ; j < COUNTER_NUM_GROUP - 1 ; j++) {
				close(this->fd.member_fds[j].fd);
			}
		} 
		context->single_init = 0;
		context->group_init = 0;
		context->ptr_single = 0;
		context->ptr_group = 0;
	}
}

int pool_preallocate(struct garrus_meta_t *context, int counter_num_config)
{
	int i;
	int dtlb_miss_code;
	int dtlb_hit_code;
	struct ev_counter_meta *pool;
	struct ev_counter_meta *this;
	int n_items;
	struct perf_event_attr attr;

	dtlb_miss_code = get_hw_cache_event_code(PERF_COUNT_HW_CACHE_DTLB, 
			PERF_COUNT_HW_CACHE_OP_READ,
			PERF_COUNT_HW_CACHE_RESULT_MISS);
	
	dtlb_hit_code = get_hw_cache_event_code(PERF_COUNT_HW_CACHE_DTLB, 
			PERF_COUNT_HW_CACHE_OP_READ,
			PERF_COUNT_HW_CACHE_RESULT_ACCESS);
	if (counter_num_config == COUNTER_NUM_SINGLE) {
		pool = context->ev_pool_single;
		n_items = context->n_single;
		for (i = 0; i < n_items; i++) {
			this = &pool[i];
			this->leader_fd.fd = initialize_event_counter(&attr, PERF_TYPE_HW_CACHE, dtlb_miss_code, 1);
			if (this->leader_fd.fd == -1 ) {
				fprintf(stderr, "Failed to open leader %llx, failed to open dtlb miss event\n", attr.config);
                 		return EV_CREATE_FAILED;
			}
			set_identifier(this->leader_fd.fd, &this->leader_fd.id);
			context->single_init += 1;
		}	
	} else if (counter_num_config == COUNTER_NUM_GROUP) {
		pool = context->ev_pool_group;
		n_items = context->n_group;
		for (i = 0; i < n_items; i++) {
			this = &pool[i];
			this->leader_fd.fd = initialize_event_group(&attr, PERF_TYPE_SOFTWARE, PERF_COUNT_SW_PAGE_FAULTS, 1);
			if (this->leader_fd.fd == -1 ) {
				fprintf(stderr, "Failed to open leader %llx, failed to open page fault event\n", attr.config);
                 		return EV_CREATE_FAILED;
			}
			set_identifier(this->leader_fd.fd, &this->leader_fd.id);
			this->fd.member_fds[0].fd = add_event_group(&attr, PERF_TYPE_HW_CACHE, dtlb_miss_code, 1, this->leader_fd.fd);
			if (this->fd.member_fds[0].fd == -1 ) {
				fprintf(stderr, "Failed to open leader %llx, failed to open dtlb miss event\n", attr.config);
                 		return EV_CREATE_FAILED;
			}
			set_identifier(this->fd.member_fds[0].fd, &this->fd.member_fds[0].id);
			this->fd.member_fds[1].fd = add_event_group(&attr, PERF_TYPE_HW_CACHE, dtlb_hit_code, 1, this->leader_fd.fd);
			if (this->fd.member_fds[1].fd == -1 ) {
				fprintf(stderr, "Failed to open leader %llx, failed to open dtlb hit event\n", attr.config);
                 		return EV_CREATE_FAILED;
			}
			set_identifier(this->fd.member_fds[1].fd, &this->fd.member_fds[1].id);
			context->group_init += 1;
		}	
	}
	return SUCCESS;
}

int garrus_init(char *outfile, int nr_ts, int n_single, int n_group)
{
	int ret, i;
	struct garrus_meta_t *context;
	if(!n_single)
		n_single = DEFAULT_PRE;
	if(!n_group)
		n_group = DEFAULT_PRE;
	if (outfile == NULL) {
		fprintf(stderr, "File name unspecified\n");
		return INIT_FAILED;
	}
	
	writefile = fopen(outfile, "wb+");
	if (writefile == NULL) {
		fprintf(stderr, "Failed to open file\n");
		return INIT_FAILED;
	}
	nr_threads = nr_ts;
	context_list = malloc(nr_threads * sizeof(struct garrus_meta_t *)); //allocating the array of contexts per thread
	if (context_list == NULL) {
		fprintf(stderr, "Out of memory\n");
		return INIT_FAILED;
	}
	context_init(n_single, n_group);
	for (i = 0; i < nr_threads; i++) {
		context = context_list[i];
		ret = pool_init(&context->ev_pool_single, n_single, COUNTER_NUM_SINGLE);
		if (ret == INIT_FAILED)
			return ret;
		ret = pool_init(&context->ev_pool_group, n_group, COUNTER_NUM_GROUP);
		if (ret == INIT_FAILED)
			return ret;
	}
	return SUCCESS;
}

void dump_to_file(void)
{
	int i, j;
	int n_items;
	struct garrus_meta_t *context;
	struct ev_counter_meta *pool;
	struct ev_counter_meta *this;
	void *rf;
	for (i = 0; i < nr_threads; i++) {
		context = context_list[i];
		pool = context->ev_pool_single;
		n_items = context->single_init;
		for (j = 0; j < n_items; j++) {
			this = &pool[j];
			rf = (struct rf_event_single *)this->buffer; 
			if (this->n_structs > 0) {
				flockfile(writefile);
				fwrite(rf,this->size, this->n_structs, writefile);
				funlockfile(writefile);
			}
		}
		pool = context->ev_pool_group;
		n_items = context->group_init;
		for (j = 0; j < n_items; j++) {
			this = &pool[j];
			rf = (struct rf_event_group *)this->buffer;
			if (this->n_structs > 0) {
				flockfile(writefile);
				fwrite(rf, this->size, this->n_structs, writefile);
				funlockfile(writefile);
			}
		}
	}
}

void garrus_cleanup(void)
{
	/*
	 * basically cleanup elements in reverse 
	 * order of initialization
	 * typically called during normal termination
	 * OR during signal handling
	 */	
	/* this may be redundantm but im closing all open fds anyways */
	/*have to dump contents to file */
	if (context_list == NULL)
		return ;
	dump_to_file();
	close_events();
	/* free the context and pools */
	context_destroy();

	/* close the writefile */
	fclose(writefile);
}

void garrus_get_stats(void)
{
	struct garrus_meta_t *context;
	int i, j, k;
	struct ev_counter_meta *pool;
	struct ev_counter_meta *this;
	
	if (context_list == NULL) {
		fprintf(stderr, "Garrus not initialized\n");
		return ;
	}

	for (k = 0; k < nr_threads; k++) {
		context = context_list[k];
		if (context->ev_pool_single == NULL || context->ev_pool_group == NULL) {
			fprintf(stderr, "Pools not initialized\n");
			return ;
		}
		printf("Printing stats for pools of allocated event counters for context %d\n", k);
		printf("Size of single event pool: %d\n", context->n_single);
		printf("Size of group event pool: %d\n", context->n_group);
		printf("Number of pre-initialized events in single pool : %d\n", context->single_init);
		printf("Number of pre-initialized events in group pool : %d\n", context->group_init);

		pool = context->ev_pool_single;
		printf("Printing intialized event counter information for single event pool for context %d\n=======================================\n", k);
		for (i = 0 ; i < context->single_init ; i++) {
			this = &pool[i];
			printf("Leader fd : %d\tLeader id :%"PRIu64"\n", this->leader_fd.fd, this->leader_fd.id);
			printf("Conter num config : %d\n", this->num_config);
			printf("Max structs: %d\n", this->max_n_structs);
			printf("Size of read_format struct : %lu\n", this->size);
			printf("Current write offset: %d\n", this->n_structs);
			printf("$=================$\n");
		}	
		pool = context->ev_pool_group;
		printf("Printing initialized event counter information for group event pool for context %d\n=======================================\n", k);
		for (i = 0 ; i < context->group_init ; i++) {
			this = &pool[i];
			printf("Leader fd : %d\tLeader id :%"PRIu64"\n", this->leader_fd.fd, this->leader_fd.id);
			for (j = 0 ; j < COUNTER_NUM_GROUP - 1 ; j++) {
				printf("Group event fd : %d\t Group event id : %"PRIu64"\n", this->fd.member_fds[j].fd, this->fd.member_fds[j].id);
			}
			printf("Conter num config : %d\n", this->num_config);
			printf("Max structs: %d\n", this->max_n_structs);
			printf("Size of read_format struct : %lu\n", this->size);
			printf("Current write offset: %d\n", this->n_structs);
			printf("$=================$\n");
		}	
	}
}

int garrus_register_thread (unsigned long id) 
{
	int ptr;
	int code;
	code = INIT_FAILED;
	pthread_mutex_lock(&context_lock);
	ptr = 0;
	while (ptr < nr_threads) {
		if (context_map[ptr] == 0) {
			context_map[ptr] = id;
			code = SUCCESS;
			break;
		} else if (context_map[ptr] == id) {
			code = INIT_FAILED;
			break;
		}
		ptr++;
	}
	pthread_mutex_unlock(&context_lock);
	return code;

}

struct garrus_meta_t *garrus_get_context (unsigned long id)
{
	int i;
	for (i = 0; i < nr_threads; i++) {
		if (context_map[i] == id) {
			return context_list[i];
		}
	}
	return NULL;	
}

int initialize_garrus_context(struct garrus_meta_t *context)
{
	int ret1, ret2;
	ret1 = pool_preallocate(context, COUNTER_NUM_SINGLE);
	ret2 = pool_preallocate(context, COUNTER_NUM_GROUP);
	if (ret1 == EV_CREATE_FAILED || ret2 == EV_CREATE_FAILED)
		return INIT_FAILED;
	return SUCCESS;
}

/* returns the code for a hardware cache event */
int get_hw_cache_event_code (enum perf_hw_cache_id c_id, enum perf_hw_cache_op_id o_id,
                   enum perf_hw_cache_op_result_id r_id)
{
	return (c_id) | (o_id << 8) | (r_id << 16);
}

/* wrapper function for perf_event_open syscall */
long perf_event_open (struct perf_event_attr *hw_event, int pid,
		 int cpu, int group_fd, unsigned long flags)
{
	int ret;
	ret = syscall (__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
	return ret;	
}

/* initialize the event attribute structure and calls the perf_event_open
   syscall, returns a file descriptor for the opened event, which can be
   used in ioctl or mmaped, no validation performed, user has to take care
*/

int __initialize_event (struct perf_event_attr *attr, enum perf_type_id ev_type, 
			  int ev_config, int exclude_kernel, int is_group, int group_leader)
{	
	int fd;
	memset (attr, 0, sizeof (struct perf_event_attr));
	attr->type = ev_type;
	attr->size = sizeof (struct perf_event_attr);
	attr->config = ev_config;
	attr->disabled = 1;
	attr->exclude_kernel = exclude_kernel; // don't count kernel events
	attr->exclude_hv = 1;
	if (is_group)
	{
		attr->read_format = PERF_FORMAT_GROUP | PERF_FORMAT_ID;
	} else {
		attr->read_format = PERF_FORMAT_ID;
	}

	fd = perf_event_open(attr, 0, -1, group_leader, 0);
	return fd;
}

int initialize_event_group (struct perf_event_attr *attr, enum perf_type_id ev_type, 
			int ev_config, int exclude_kernel)
{
	int fd;
	fd = __initialize_event (attr, ev_type, ev_config, exclude_kernel,
				     1, -1);
	return fd;
	
}

/* call to the general interface event_group initializer,
   again its up to the user to validate the fd returned is valid
*/
int initialize_event_counter (struct perf_event_attr *attr, enum perf_type_id ev_type, 
			  int ev_config, int exclude_kernel)
{	int fd;
	fd = __initialize_event (attr, ev_type, ev_config, exclude_kernel,
				     0, -1);
	return fd;
}

int add_event_group (struct perf_event_attr *attr, enum perf_type_id ev_type, 
		 int ev_config, int exclude_kernel, int leader_fd)
{
	int fd;
	fd = __initialize_event (attr, ev_type, ev_config, exclude_kernel,
				 1, leader_fd);
	return fd;
}

int set_identifier (int fd, uint64_t *id)
{
	return ioctl (fd, PERF_EVENT_IOC_ID, id);
}

int __start_event (int fd, int arg)
{
	ioctl (fd, PERF_EVENT_IOC_RESET, arg);
	return ioctl (fd, PERF_EVENT_IOC_ENABLE, arg);
}

int start_event_group (int fd)
{
	return __start_event (fd, PERF_IOC_FLAG_GROUP);
} 

int start_event_counter (int fd)
{
	return __start_event (fd, 0);
}

int __stop_event (int fd, int arg)
{
	return ioctl (fd, PERF_EVENT_IOC_DISABLE, arg);
}

int stop_event_group (int fd)
{
	return __stop_event (fd, PERF_IOC_FLAG_GROUP);
} 

int stop_event_counter (int fd)
{
	return __stop_event (fd, 0);
}

/* function to search through initialized events to get an unused event 
 * this will have to be inside a critical section to make it thread safe
 */ 
int search_free_event(struct garrus_meta_t *context, int num_config)
{
	int start, end;
	int *freelist;
	if(num_config == COUNTER_NUM_SINGLE) {
		freelist = context->in_use_single;
		end = context->single_init;
	} else if (num_config == COUNTER_NUM_GROUP) {
		freelist = context->in_use_group;
		end = context->group_init;
	}
	for (start = 0; start < end; start++) {
		if (!freelist[start]) {
			freelist[start] = 1;
			return start;
		}
	}
	return -1;
}
/*
 * this function doesnt have to be inside a critical section 
 */
int get_event_index(struct garrus_meta_t *context, struct ev_counter_meta *this, int num_config) 
{
	struct ev_counter_meta *pool;
	if (num_config == COUNTER_NUM_SINGLE) {
		pool = context->ev_pool_single;
	} else if (num_config == COUNTER_NUM_GROUP) {
		pool = context->ev_pool_group;
	}
	int index = this - pool;
	return index;
}

struct ev_counter_meta *get_open_event(struct garrus_meta_t *context, int num_config)
{
	struct ev_counter_meta *this;
	int index;
	if (num_config == COUNTER_NUM_SINGLE) {
		if (context->single_init > 0 && context->ptr_single < context->single_init) {
			index = search_free_event(context, COUNTER_NUM_SINGLE);
			if (index == -1){
				this = NULL;
				context->ptr_single = 0;
			}
			else{
				this = &context->ev_pool_single[index];
				context->ptr_single = (context->ptr_single + 1) % context->single_init;
			}
		}
	} else if (num_config == COUNTER_NUM_GROUP) {
		if (context->group_init > 0 && context->ptr_group < context->group_init) {
			index = search_free_event(context, COUNTER_NUM_GROUP);
			if (index == -1){
				this = NULL;
				context->ptr_group = 0;
			}
			else{
				this = &context->ev_pool_group[index];
				context->ptr_group = (context->ptr_group + 1) % context->group_init;
			}
		}
	}
	return this;	
}

void release_open_event(struct garrus_meta_t *context, struct ev_counter_meta **this, int num_config)
{
	int index = get_event_index(context, *this, num_config);
	if (num_config == COUNTER_NUM_SINGLE) {
		context->in_use_single[index] = 0;
	} else if (num_config == COUNTER_NUM_GROUP) {
		context->in_use_group[index] = 0;
	}
	*this = NULL;
}

void garrus_test(struct garrus_meta_t *context, int num_config)
{
	struct ev_counter_meta *event;
	if (num_config == COUNTER_NUM_SINGLE) {
		event = get_open_event(context, num_config);
		start_event_counter(event->leader_fd.fd);
		char* ptr;
        	int i;
         	ptr = malloc(128 * 128 * 128);
         	for (i = 0; i < 128 * 128 * 128; i++) {
             		ptr[i] = (char) (i & 0xff); // pagefault
         	}
         	free(ptr);
		stop_event_counter(event->leader_fd.fd);
		char buffer[BUF_SIZE];
		struct rf_event_single *rf = (struct rf_event_single *)buffer;
		read(event->leader_fd.fd, buffer, sizeof(struct rf_event_single));
		printf ("id : %"PRIu64"   value : %"PRIu64"\n", rf->id, rf->value);
		if (event != NULL)
			release_open_event(context, &event, COUNTER_NUM_SINGLE);
	} else if (num_config == COUNTER_NUM_GROUP) {
		event = get_open_event(context, COUNTER_NUM_GROUP);
		start_event_group(event->leader_fd.fd);
		char* ptr;
        	int i;
         	ptr = malloc(128 * 128 * 128);
         	for (i = 0; i < 128 * 128 * 128; i++) {
             		ptr[i] = (char) (i & 0xff); // pagefault
         	}
         	free(ptr);
		stop_event_group(event->leader_fd.fd);
		char buffer[4096];
		struct rf_event_group *rf = (struct rf_event_group *)buffer;
		read(event->leader_fd.fd, buffer, sizeof(struct rf_event_group));
		printf("Number of events: %"PRIu64"\n",rf->nr);
		for(int i = 0; i < rf->nr;i ++) {
			printf("The id is : %"PRIu64"    and the value is %"PRIu64"\n", rf->values[i].id, rf->values[i].value);
		}
		if (event != NULL)
			release_open_event(context, &event, COUNTER_NUM_GROUP);
	}
}

int read_counters(struct ev_counter_meta *event, int num_config)
{
	if (event == NULL) {
		fprintf(stderr, "Failed to read counters\n");
		return READ_FAILED;
	}
	int max_events, n_events;
	int curr_offset;
	int size;
	void *rf;
	if (num_config == COUNTER_NUM_SINGLE) {
		rf = (struct rf_event_single *)event->buffer;
	} else if (num_config == COUNTER_NUM_GROUP) {
		rf = (struct rf_event_group *)event->buffer;
	}

	max_events = event->max_n_structs;
	n_events = event->n_structs;
	size = event->size;
	if (n_events < max_events) {
		curr_offset = n_events * size;
		lseek(event->leader_fd.fd, 0 ,SEEK_SET);
		read(event->leader_fd.fd, event->buffer + curr_offset, size);
		n_events++;
	} else {
		flockfile(writefile);
		fwrite(rf, size, max_events, writefile);
		funlockfile(writefile);
		n_events = 0;
		curr_offset = n_events * size;
		lseek(event->leader_fd.fd, 0 ,SEEK_SET);
		read(event->leader_fd.fd, event->buffer + curr_offset, size);
		n_events++;
	}
	event->n_structs = n_events;
	return SUCCESS;
}