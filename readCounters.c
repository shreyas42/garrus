#include <linux/perf_event.h>
#include <stdint.h>
#include <stdio.h>
#include <stdint.h>
#include<inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "garrus.h"

/* change this according to your convenience */
/* basically this should read the file which garrus runtime dumps
 * the output to
 */

/* in my particular case, i was using this with memcached */
const char *filename = "";
int 
main (void)
{
    FILE *fp = fopen (filename, "rb+");
    struct rf_event_group rf;
    uint64_t val1, val2, val3;
    while (fread(&rf, sizeof(struct rf_event_group), 1, fp)) {
        val1 = rf.values[0].value;
        val2 = rf.values[1].value;
        val3 = rf.values[2].value;
        printf("%"PRIu64",%"PRIu64",%"PRIu64"\n", val1, val2, val3);
    }
    fclose(fp);
    return 0;
}
