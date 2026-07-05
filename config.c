#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

Config config;

static void trim(char *s)
{
    if (!s)
        return;

    char *p = s;

    while (isspace((unsigned char)*p))
        p++;

    if (p != s)
        memmove(
            s,
            p,
            strlen(p) + 1);

    size_t len = strlen(s);

    while (len &&
           isspace(
               (unsigned char)s[len - 1]))
    {
        s[--len] = 0;
    }
}

static void set_defaults(void)
{
    config.lat = 0.0;
    config.lon = 0.0;

    strcpy(
        config.locator,
        "");

    strcpy(
        config.dxc_host,
        "telnet.reversebeacon.net");

    config.dxc_port = 7000;

    strcpy(
        config.dxc_call,
        "N0CALL");
}

int config_load(
    const char *filename)
{
    set_defaults();

    FILE *f =
        fopen(filename, "r");

    if (!f)
        return -1;

    char line[256];

    while (
        fgets(
            line,
            sizeof(line),
            f))
    {
        trim(line);

        if (line[0] == 0)
            continue;

        if (line[0] == '#')
            continue;

        char *eq =
            strchr(line, '=');

        if (!eq)
            continue;

        *eq = 0;

        char *key = line;
        char *value = eq + 1;

        trim(key);
        trim(value);

        if (strcmp(
                key,
                "LAT") == 0)
        {
            config.lat =
                atof(value);
        }
        else if (strcmp(
                     key,
                     "LON") == 0)
        {
            config.lon =
                atof(value);
        }
        else if (strcmp(
                     key,
                     "LOCATOR") == 0)
        {
            strncpy(
                config.locator,
                value,
                sizeof(config.locator));

            config.locator[sizeof(config.locator) - 1] = 0;
        }
        else if (strcmp(
                     key,
                     "DXC_HOST") == 0)
        {
            strncpy(
                config.dxc_host,
                value,
                sizeof(config.dxc_host));

            config.dxc_host[sizeof(config.dxc_host) - 1] = 0;
        }
        else if (strcmp(
                     key,
                     "DXC_PORT") == 0)
        {
            config.dxc_port =
                atoi(value);
        }
        else if (strcmp(
                     key,
                     "DXC_CALL") == 0)
        {
            strncpy(
                config.dxc_call,
                value,
                sizeof(config.dxc_call));

            config.dxc_call[sizeof(config.dxc_call) - 1] = 0;
        }
    }

    fclose(f);

    return 0;
}
