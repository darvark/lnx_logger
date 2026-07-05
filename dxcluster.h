#ifndef DXCLUSTER_H
#define DXCLUSTER_H

#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>

#define MAX_SPOTS 500

typedef struct
{
    char time[16];
    char freq[16];
    char call[32];
    char comment[128];

} Spot;

extern Spot spots[MAX_SPOTS];
extern int spot_count;
extern int spot_start;
extern char dxcluster_status[128];
extern pthread_mutex_t dxcluster_mutex;

int dxcluster_start(void);
void dxcluster_stop(void);
void dxcluster_set_status(const char *text);

#endif
