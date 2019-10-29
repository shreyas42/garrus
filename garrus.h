/* utility functions for accessing perf_events */

#ifndef GARRUS_H_
#define GARRUS_H_

#include <linux/perf_event.h>

#ifndef DEFAULT_PRE
#define DEFAULT_PRE 8
#endif


#ifndef BUF_SIZE
#define BUF_SIZE 4096 /* size of the buffer to read in counter values */
#endif

/*this is for testing purposes */

enum garrus_error_codes {
	SUCCESS = 0,
	INIT_FAILED = -1,
	EV_CREATE_FAILED = -2,
	READ_FAILED = -3
};

enum counter_num_configs {
	COUNTER_NUM_SINGLE = 1,
	COUNTER_NUM_GROUP = 3,
	COUNTER_NUM_MAX, /* support ends here */
};

struct rf_event_single {
    uint64_t value;         /* The value of the event */
    uint64_t id;            /* if PERF_FORMAT_ID */
};

struct rf_event_group {
	uint64_t nr;
	struct {
		uint64_t value;
		uint64_t id;	
	} values[COUNTER_NUM_GROUP];	
};
/*
 * enum to keep track of different event counter
 * configurations
 */
enum counter_struct_configs {
	COUNTER_STRUCT_SINGLE = 1,
	COUNTER_STRUCT_GROUP = 3,
	COUNTER_STRUCT_MAX, 
};

struct fd_to_id_map {
	int fd;
	uint64_t id;		
};

struct ev_counter_meta {
	struct fd_to_id_map leader_fd;
	union fds {
		struct fd_to_id_map member_fd;
		struct fd_to_id_map member_fds[COUNTER_NUM_GROUP - 1];
	}fd;
	enum counter_num_configs num_config;
	char buffer[BUF_SIZE];
	int max_n_structs;
	size_t size;
	int n_structs; /*gives an offset where to write into*/
};

struct garrus_meta_t {
	struct ev_counter_meta *ev_pool_single;
	int *in_use_single;
	int n_single;
	int ptr_single;
	int single_init;
	struct ev_counter_meta * ev_pool_group;
	int *in_use_group;
	int n_group;
	int ptr_group;
	int group_init;
};
extern struct garrus_meta_t **context_list;

int garrus_init(char *outfile, int nr_threads, int n_single, int n_group);
void garrus_cleanup();
void garrus_get_stats();
int garrus_register_thread(unsigned long id);
struct garrus_meta_t *garrus_get_context(unsigned long id);
int initialize_garrus_context(struct garrus_meta_t *context);

/* function to calculate the code for hw_cache_events */
int get_hw_cache_event_code (enum perf_hw_cache_id c_id, 
				    enum perf_hw_cache_op_id o_id, 
		   	            enum perf_hw_cache_op_result_id r_id);


/* wrapper function for the perf_event_open system call */
long perf_event_open (struct perf_event_attr *hw_event, int pid,
		 int cpu, int group_fd, unsigned long flags);


/* general interface to creating an interface group */
int initialize_event_group (struct perf_event_attr *attr, enum perf_type_id ev_type, 
			  int ev_config, int exclude_kernel);

/* interface to initialize a single event to monitor 
   returns a file descriptor from calling the perf_event_open syscall
   caller will have to check the return value, no validation performed here
   internally calls the event_group initializer
*/
int initialize_event_counter (struct perf_event_attr *attr, enum perf_type_id ev_type, 
			  int ev_config, int exclude_kernel);

int add_event_group (struct perf_event_attr *attr, enum perf_type_id ev_type, 
		 int ev_config, int exclude_kernel, int leader_fd); 

int set_identifier (int fd, uint64_t *id);
int start_event_group (int fd);
int start_event_counter (int fd);
int stop_event_group (int fd);
int stop_event_counter (int fd);
struct ev_counter_meta *get_open_event(struct garrus_meta_t *context, int num_config);
void release_open_event(struct garrus_meta_t *context, struct ev_counter_meta **this, int num_config);
void garrus_test(struct garrus_meta_t *context, int num_config);
int read_counters(struct ev_counter_meta *, int num_config);
#endif
