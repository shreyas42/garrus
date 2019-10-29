#include <asm/unistd.h>
#include <assert.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/perf_event.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>

#include "garrus.h"
#define N_ARR 128

void memory_test(void)
{
        char *ptr;
        int i;
        ptr = malloc(N_ARR * N_ARR * N_ARR);
        for (i = 0; i < N_ARR * N_ARR * N_ARR; i++) {
                ptr[i] = (char) (i & 0xff); //pagefault
        }
        free(ptr);
}

void *do_something(void *arg) 
{
        pthread_t id;
        struct garrus_meta_t *context;
        struct ev_counter_meta *event;
        struct rf_event_group *rf;
        id = pthread_self();
        garrus_register_thread(id);
        context = garrus_get_context(id);
        initialize_garrus_context(context);
        for (int i = 0; i < 100; i++) {
                event = get_open_event(context, COUNTER_NUM_GROUP);
                start_event_group(event->leader_fd.fd);
                memory_test();
                stop_event_group(event->leader_fd.fd);
                // read_counters(event, COUNTER_NUM_GROUP);
                rf = (struct rf_event_group *)event->buffer;
                read(event->leader_fd.fd, event->buffer, sizeof(struct rf_event_group));
                printf("Number of events: %"PRIu64"\n",rf->nr);
                for(int i = 0; i < rf->nr;i ++) {
                        printf("The id is : %"PRIu64"    and the value is %"PRIu64"\n", rf->values[i].id, rf->values[i].value);
                }
                release_open_event(context, &event, COUNTER_NUM_GROUP);
        }
        return (void *)0;
}

int main(void)
{
        const int NR_THREADS = 4;
        const int NR_SINGLE = 4;
        const int NR_GROUP = 4;
        pthread_t threadIds[NR_THREADS];
        garrus_init("testwrite.txt", NR_THREADS, NR_SINGLE, NR_GROUP);
        // get_stats();
        for (int i = 0; i < NR_THREADS; i++) {
                pthread_create(&threadIds[i], NULL, do_something, NULL);
        }
        for (int i = 0; i < NR_THREADS; i++) {
                pthread_join(threadIds[i], NULL);
        }
        garrus_cleanup();
        return 0;
}