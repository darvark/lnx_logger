#include "dxcluster.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/select.h>
#include <stdlib.h>
#include <ctype.h>

#include "config.h"

#define DXCLUSTER_TRAFFIC_LOG "dxcluster_traffic.txt"
#define DXCLUSTER_TRAFFIC_MAX_SIZE (1024 * 1024)

Spot spots[MAX_SPOTS];
int spot_count = 0;
int spot_start = 0;
char dxcluster_status[128] = "DXCluster idle";
pthread_mutex_t dxcluster_mutex = PTHREAD_MUTEX_INITIALIZER;
static FILE *dxcluster_log = NULL;

static int running = 0;
static pthread_t thread_id;
struct sockaddr_in addr;

/* ------------------------------------------------ */

static void dxcluster_close_log(void)
{
    if (dxcluster_log)
    {
        fclose(dxcluster_log);
        dxcluster_log = NULL;
    }
}

static void dxcluster_open_log(void)
{
    if (dxcluster_log)
        return;

    dxcluster_log = fopen(DXCLUSTER_TRAFFIC_LOG, "a+");
    if (!dxcluster_log)
        return;

    if (fseek(dxcluster_log, 0, SEEK_END) == 0)
    {
        long size = ftell(dxcluster_log);
        if (size > DXCLUSTER_TRAFFIC_MAX_SIZE)
        {
            fclose(dxcluster_log);
            dxcluster_log = fopen(DXCLUSTER_TRAFFIC_LOG, "w");
        }
    }
}

static void dxcluster_log_line(const char *line)
{
    if (!line)
        return;

    dxcluster_open_log();
    if (!dxcluster_log)
        return;

    fprintf(dxcluster_log, "%s\n", line);
    fflush(dxcluster_log);

    if (ftell(dxcluster_log) >= DXCLUSTER_TRAFFIC_MAX_SIZE)
    {
        fclose(dxcluster_log);
        dxcluster_log = fopen(DXCLUSTER_TRAFFIC_LOG, "w");
    }
}

static bool dxcluster_line_is_dx_de(const char *line)
{
    if (!line)
        return false;

    while (*line && isspace((unsigned char)*line))
        line++;

    return strncasecmp(line, "DX de", 5) == 0;
}

static void add_spot(const char *line)
{
    if (!line || !line[0])
        return;

    dxcluster_log_line(line);

    char copy[512];
    strncpy(copy, line, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = 0;

    size_t len = strlen(copy);
    while (len > 0 && (copy[len - 1] == '\r' || copy[len - 1] == '\n' || copy[len - 1] == ' ' || copy[len - 1] == '\t'))
        copy[--len] = 0;

    pthread_mutex_lock(&dxcluster_mutex);

    int index;
    if (spot_count < MAX_SPOTS)
        index = spot_count++;
    else
    {
        index = spot_start;
        spot_start = (spot_start + 1) % MAX_SPOTS;
    }

    Spot *s = &spots[index];
    memset(s, 0, sizeof(*s));

    char *save = NULL;
    char *tokens[8] = {0};
    int count = 0;

    for (char *tok = strtok_r(copy, " \t", &save);
         tok && count < 8;
         tok = strtok_r(NULL, " \t", &save))
    {
        tokens[count++] = tok;
    }

    int skip = 0;
    if (count >= 2 && strcmp(tokens[0], "DX") == 0 && strcmp(tokens[1], "de") == 0)
        skip = 2;
    else if (count >= 1 && strcmp(tokens[0], "Spot") == 0)
        skip = 1;

    if (count > skip + 2)
    {
        strncpy(s->time, tokens[skip], sizeof(s->time) - 1);
        strncpy(s->freq, tokens[skip + 1], sizeof(s->freq) - 1);
        strncpy(s->call, tokens[skip + 2], sizeof(s->call) - 1);
    }
    else
    {
        strncpy(s->time, "?", sizeof(s->time) - 1);
        strncpy(s->freq, "?", sizeof(s->freq) - 1);
        strncpy(s->call, line, sizeof(s->call) - 1);
    }

    if (count > skip + 3)
    {
        size_t offset = 0;
        for (int i = skip + 3; i < count; i++)
        {
            if (offset < sizeof(s->comment) - 1)
            {
                offset += snprintf(s->comment + offset,
                                   sizeof(s->comment) - offset,
                                   "%s%s",
                                   i == skip + 3 ? "" : " ",
                                   tokens[i]);
            }
        }
    }
    else
    {
        strncpy(s->comment, copy, sizeof(s->comment) - 1);
    }

    pthread_mutex_unlock(&dxcluster_mutex);
}

void dxcluster_set_status(const char *text)
{
    if (!text)
        return;

    char tmp[128];
    snprintf(tmp,
             sizeof(tmp),
             "%s (%s:%d)",
             text,
             config.dxc_host,
             config.dxc_port);

    pthread_mutex_lock(&dxcluster_mutex);
    snprintf(dxcluster_status,
             sizeof(dxcluster_status),
             "%s",
             tmp);
    pthread_mutex_unlock(&dxcluster_mutex);
}

static int send_cluster_commands(int sock)
{
    const char *cmds[] = {
        "set prompt off\r\n",
        "set/terse\r\n",
        "dx\r\n"
    };

    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++)
    {
        if (send(sock, cmds[i], strlen(cmds[i]), 0) < 0)
            return -1;

        usleep(200000);
    }

    return 0;
}

static int send_cluster_login_call(int sock)
{
    if (!config.dxc_call[0])
        return 0;

    char login_cmd[64];
    snprintf(login_cmd, sizeof(login_cmd), "%s\r\n", config.dxc_call);

    if (send(sock, login_cmd, strlen(login_cmd), 0) < 0)
        return -1;

    usleep(200000);
    return 0;
}

static bool looks_like_login_prompt(const char *text)
{
    if (!text || !text[0])
        return false;

    if (strcasestr(text, "login"))
        return true;

    if (strcasestr(text, "callsign"))
        return true;

    if (strcasestr(text, "call:"))
        return true;

    return false;
}

/* ------------------------------------------------ */

static void *cluster_thread(void *arg)
{
    (void)arg;

    while (running)
    {
        dxcluster_set_status("Connecting...");

        int sock = socket(AF_INET, SOCK_STREAM, 0);

        if (sock < 0)
        {
            dxcluster_set_status("Socket error");
            break;
        }

        int flags = fcntl(sock, F_GETFL, 0);
        if (flags >= 0)
            fcntl(sock, F_SETFL, flags | O_NONBLOCK);

        dxcluster_set_status("Resolving host...");

        struct hostent *host = gethostbyname(config.dxc_host);
        if (!host)
        {
            dxcluster_set_status("DNS failed");
            close(sock);
            sleep(5);
            continue;
        }

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));

        addr.sin_family = AF_INET;
        addr.sin_port = htons(config.dxc_port);
        memcpy(&addr.sin_addr, host->h_addr_list[0], host->h_length);

        dxcluster_set_status("Connecting to host...");

        if (connect(sock,
                    (struct sockaddr *)&addr,
                    sizeof(addr)) < 0)
        {
            if (errno != EINPROGRESS && errno != EWOULDBLOCK)
            {
                dxcluster_set_status("Connect failed");
                close(sock);
                sleep(1);
                continue;
            }
        }

        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(sock, &wfds);

        struct timeval tv;
        tv.tv_sec = 30;
        tv.tv_usec = 0;

        int sel = select(sock + 1, NULL, &wfds, NULL, &tv);
        if (sel <= 0)
        {
            dxcluster_set_status("Connect timeout");
            close(sock);
            sleep(1);
            continue;
        }

        int so_error = 0;
        socklen_t len = sizeof(so_error);
        getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &len);
        if (so_error != 0)
        {
            dxcluster_set_status("Connect failed");
            close(sock);
            sleep(1);
            continue;
        }

        dxcluster_set_status("Connected");

        bool login_sent = config.dxc_call[0] ? false : true;
        if (login_sent)
        {
            if (send_cluster_commands(sock) < 0)
            {
                dxcluster_set_status("Init command send failed");
                close(sock);
                sleep(1);
                continue;
            }

            dxcluster_set_status("Connected");
        }
        else
        {
            dxcluster_set_status("Waiting for login prompt...");
        }

        char buf[512];
        char pending[1024] = {0};
        size_t pending_len = 0;
        int login_wait_loops = 0;

        int n = 0;

        while (running)
        {
            n = read(sock, buf, sizeof(buf));

            if (n <= 0)
            {
                if (errno != EAGAIN && errno != EWOULDBLOCK)
                    break;
                usleep(100000);
                continue;
            }

            if ((size_t)n + pending_len >= sizeof(pending))
            {
                pending_len = 0;
            }

            memcpy(pending + pending_len, buf, (size_t)n);
            pending_len += (size_t)n;
            pending[pending_len] = 0;

            if (!login_sent && looks_like_login_prompt(pending))
            {
                if (send_cluster_login_call(sock) < 0)
                {
                    dxcluster_set_status("Login send failed");
                    break;
                }

                if (send_cluster_commands(sock) < 0)
                {
                    dxcluster_set_status("Command send failed");
                    break;
                }

                login_sent = true;
                dxcluster_set_status("Logged in");
            }

            char *line_start = pending;
            char *newline = NULL;

            while ((newline = strchr(line_start, '\n')) != NULL)
            {
                *newline = 0;
                char *line = line_start;
                if (*line == '\r')
                    line++;

                while (*line && isspace((unsigned char)*line))
                    line++;

                if (!login_sent && looks_like_login_prompt(line))
                {
                    if (send_cluster_login_call(sock) < 0)
                    {
                        dxcluster_set_status("Login send failed");
                        break;
                    }

                    if (send_cluster_commands(sock) < 0)
                    {
                        dxcluster_set_status("Command send failed");
                        break;
                    }

                    login_sent = true;
                    dxcluster_set_status("Logged in");
                    line_start = newline + 1;
                    continue;
                }

                if (!login_sent)
                {
                    line_start = newline + 1;
                    continue;
                }

                if (line[0] != '\0' && dxcluster_line_is_dx_de(line))
                {
                    add_spot(line);
                    dxcluster_set_status("Spot received");
                }

                line_start = newline + 1;
            }

            if (line_start != pending)
            {
                pending_len = strlen(line_start);
                memmove(pending, line_start, pending_len + 1);
            }

            if (!login_sent)
            {
                login_wait_loops++;
                if (login_wait_loops >= 50)
                {
                    if (send_cluster_login_call(sock) < 0)
                    {
                        dxcluster_set_status("Login fallback failed");
                        break;
                    }

                    if (send_cluster_commands(sock) < 0)
                    {
                        dxcluster_set_status("Command send failed");
                        break;
                    }

                    login_sent = true;
                    dxcluster_set_status("Login fallback sent");
                }
            }
        }

        close(sock);

        if (!running)
            break;

        dxcluster_set_status("Reconnecting...");
        usleep(1000000);
    }

    if (!running)
        dxcluster_set_status("Disconnected");

    return NULL;
}

/* ------------------------------------------------ */

int dxcluster_start(void)
{
    running = 1;

    if (pthread_create(&thread_id, NULL,
                       cluster_thread, NULL) != 0)
    {
        running = 0;
        return -1;
    }

    return 0;
}

/* ------------------------------------------------ */

void dxcluster_stop(void)
{
    running = 0;
    pthread_join(thread_id, NULL);
    dxcluster_close_log();
}
