#ifndef MAIDENHEAD_H
#define MAIDENHEAD_H

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

int locator_to_latlon(
const char *locator,
double *lat,
double *lon);

#endif

