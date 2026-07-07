#include "dxcluster.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <strings.h>
#include <sys/select.h>
#include <unistd.h>

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
static int stop_pipe[2] = {-1, -1};

/* ------------------------------------------------ */

/*
 * Close the DXCluster traffic log if it is open.
 *
 * @return Nothing.
 */
static void dxcluster_close_log(void) {
  if (dxcluster_log) {
    fclose(dxcluster_log);
    dxcluster_log = NULL;
  }
}

/*
 * Close both ends of the DXCluster shutdown pipe.
 *
 * @return Nothing.
 */
static void dxcluster_close_stop_pipe(void) {
  if (stop_pipe[0] >= 0) {
    close(stop_pipe[0]);
    stop_pipe[0] = -1;
  }

  if (stop_pipe[1] >= 0) {
    close(stop_pipe[1]);
    stop_pipe[1] = -1;
  }
}

/*
 * Create the non-blocking stop pipe used to wake the worker thread.
 *
 * @return 0 on success, or -1 on failure.
 */
static int dxcluster_create_stop_pipe(void) {
  if (pipe(stop_pipe) < 0)
    return -1;

  for (int i = 0; i < 2; i++) {
    int flags = fcntl(stop_pipe[i], F_GETFL, 0);
    if (flags >= 0)
      fcntl(stop_pipe[i], F_SETFL, flags | O_NONBLOCK);
  }

  return 0;
}

/*
 * Wake the worker thread by writing a byte to the stop pipe.
 *
 * @return Nothing.
 */
static void dxcluster_signal_stop(void) {
  if (stop_pipe[1] < 0)
    return;

  const unsigned char token = 1;
  (void)write(stop_pipe[1], &token, 1);
}

/*
 * Drain any pending bytes from the stop pipe.
 *
 * @return Nothing.
 */
static void dxcluster_drain_stop_pipe(void) {
  if (stop_pipe[0] < 0)
    return;

  char buf[32];
  while (read(stop_pipe[0], buf, sizeof(buf)) > 0)
    ;
}

/*
 * Wait for socket readiness or a stop request.
 *
 * @param sock Socket descriptor to monitor.
 * @param want_write Nonzero to wait for writability, zero to wait for read.
 * @param timeout_sec Timeout in seconds, or negative to wait indefinitely.
 * @return 1 if the socket is ready, 0 on timeout, or -1 on stop/error.
 */
static int dxcluster_wait_for_socket(int sock, int want_write, int timeout_sec) {
  fd_set rfds;
  fd_set wfds;

  FD_ZERO(&rfds);
  FD_ZERO(&wfds);

  FD_SET(stop_pipe[0], &rfds);
  if (want_write)
    FD_SET(sock, &wfds);
  else
    FD_SET(sock, &rfds);

  int maxfd = sock > stop_pipe[0] ? sock : stop_pipe[0];

  struct timeval tv;
  struct timeval *ptv = NULL;
  if (timeout_sec >= 0) {
    tv.tv_sec = timeout_sec;
    tv.tv_usec = 0;
    ptv = &tv;
  }

  int rc = select(maxfd + 1, want_write ? NULL : &rfds,
                  want_write ? &wfds : NULL, NULL, ptv);
  if (rc <= 0)
    return rc;

  if (FD_ISSET(stop_pipe[0], &rfds)) {
    dxcluster_drain_stop_pipe();
    errno = ECANCELED;
    return -1;
  }

  return want_write ? FD_ISSET(sock, &wfds) : FD_ISSET(sock, &rfds);
}

/*
 * Sleep for a period while still responding to stop requests.
 *
 * @param timeout_sec Timeout in seconds.
 * @return 1 if the timeout elapsed, 0 if interrupted, or -1 on stop/error.
 */
static int dxcluster_sleep_or_stop(int timeout_sec) {
  return dxcluster_wait_for_socket(stop_pipe[0], 0, timeout_sec);
}

/*
 * Open the DXCluster traffic log if needed.
 *
 * @return Nothing.
 */
static void dxcluster_open_log(void) {
  if (dxcluster_log)
    return;

  dxcluster_log = fopen(DXCLUSTER_TRAFFIC_LOG, "a+");
  if (!dxcluster_log)
    return;

  if (fseek(dxcluster_log, 0, SEEK_END) == 0) {
    long size = ftell(dxcluster_log);
    if (size > DXCLUSTER_TRAFFIC_MAX_SIZE) {
      fclose(dxcluster_log);
      dxcluster_log = fopen(DXCLUSTER_TRAFFIC_LOG, "w");
    }
  }
}

/*
 * Append a raw DXCluster line to the traffic log.
 *
 * @param line Text to log.
 * @return Nothing.
 */
static void dxcluster_log_line(const char *line) {
  if (!line)
    return;

  dxcluster_open_log();
  if (!dxcluster_log)
    return;

  fprintf(dxcluster_log, "%s\n", line);
  fflush(dxcluster_log);

  if (ftell(dxcluster_log) >= DXCLUSTER_TRAFFIC_MAX_SIZE) {
    fclose(dxcluster_log);
    dxcluster_log = fopen(DXCLUSTER_TRAFFIC_LOG, "w");
  }
}

/*
 * Check whether a line looks like a DX de spot.
 *
 * @param line Input text.
 * @return true if the line starts with DX de after whitespace, otherwise false.
 */
static bool dxcluster_line_is_dx_de(const char *line) {
  if (!line)
    return false;

  while (*line && isspace((unsigned char)*line))
    line++;

  return strncasecmp(line, "DX de", 5) == 0;
}

/*
 * Parse and store a spot line in the circular buffer.
 *
 * @param line Raw cluster line.
 * @return Nothing.
 */
static void add_spot(const char *line) {
  if (!line || !line[0])
    return;

  dxcluster_log_line(line);

  char copy[512];
  strncpy(copy, line, sizeof(copy) - 1);
  copy[sizeof(copy) - 1] = 0;

  size_t len = strlen(copy);
  while (len > 0 && (copy[len - 1] == '\r' || copy[len - 1] == '\n' ||
                     copy[len - 1] == ' ' || copy[len - 1] == '\t'))
    copy[--len] = 0;

  pthread_mutex_lock(&dxcluster_mutex);

  int index;
  if (spot_count < MAX_SPOTS)
    index = spot_count++;
  else {
    index = spot_start;
    spot_start = (spot_start + 1) % MAX_SPOTS;
  }

  Spot *s = &spots[index];
  memset(s, 0, sizeof(*s));

  char *save = NULL;
  char *tokens[8] = {0};
  int count = 0;

  for (char *tok = strtok_r(copy, " \t", &save); tok && count < 8;
       tok = strtok_r(NULL, " \t", &save)) {
    tokens[count++] = tok;
  }

  int skip = 0;
  if (count >= 2 && strcmp(tokens[0], "DX") == 0 &&
      strcmp(tokens[1], "de") == 0)
    skip = 2;
  else if (count >= 1 && strcmp(tokens[0], "Spot") == 0)
    skip = 1;

  if (count > skip + 2) {
    strncpy(s->time, tokens[skip], sizeof(s->time) - 1);
    strncpy(s->freq, tokens[skip + 1], sizeof(s->freq) - 1);
    strncpy(s->call, tokens[skip + 2], sizeof(s->call) - 1);
  } else {
    strncpy(s->time, "?", sizeof(s->time) - 1);
    strncpy(s->freq, "?", sizeof(s->freq) - 1);
    strncpy(s->call, line, sizeof(s->call) - 1);
  }

  if (count > skip + 3) {
    size_t offset = 0;
    for (int i = skip + 3; i < count; i++) {
      if (offset < sizeof(s->comment) - 1) {
        offset += snprintf(s->comment + offset, sizeof(s->comment) - offset,
                           "%s%s", i == skip + 3 ? "" : " ", tokens[i]);
      }
    }
  } else {
    strncpy(s->comment, copy, sizeof(s->comment) - 1);
  }

  pthread_mutex_unlock(&dxcluster_mutex);
}

/*
 * Update the shared DXCluster status string.
 *
 * @param text Status message to store.
 * @return Nothing.
 */
void dxcluster_set_status(const char *text) {
  if (!text)
    return;

  char tmp[128];
  snprintf(tmp, sizeof(tmp), "%s (%s:%d)", text, config.dxc_host,
           config.dxc_port);

  pthread_mutex_lock(&dxcluster_mutex);
  snprintf(dxcluster_status, sizeof(dxcluster_status), "%s", tmp);
  pthread_mutex_unlock(&dxcluster_mutex);
}

/*
 * Send the initial DXCluster command sequence after login.
 *
 * @param sock Connected socket descriptor.
 * @return 0 on success, or -1 on failure.
 */
static int send_cluster_commands(int sock) {
  const char *cmds[] = {"set prompt off\r\n", "set/terse\r\n", "dx\r\n"};

  for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
    if (send(sock, cmds[i], strlen(cmds[i]), 0) < 0)
      return -1;

    usleep(200000);
  }

  return 0;
}

/*
 * Send the configured login callsign to the DXCluster server.
 *
 * @param sock Connected socket descriptor.
 * @return 0 on success, or -1 on failure.
 */
static int send_cluster_login_call(int sock) {
  if (!config.dxc_call[0])
    return 0;

  char login_cmd[64];
  snprintf(login_cmd, sizeof(login_cmd), "%s\r\n", config.dxc_call);

  if (send(sock, login_cmd, strlen(login_cmd), 0) < 0)
    return -1;

  usleep(200000);
  return 0;
}

/*
 * Detect common login prompts in incoming cluster text.
 *
 * @param text Input buffer to inspect.
 * @return true if the text resembles a login prompt, otherwise false.
 */
static bool looks_like_login_prompt(const char *text) {
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

/*
 * Worker thread that manages the DXCluster connection and spot feed.
 *
 * @param arg Unused thread argument.
 * @return NULL when the thread exits.
 */
static void *cluster_thread(void *arg) {
  (void)arg;

  pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
  pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

  while (running) {
    int sock = -1;

    dxcluster_set_status("Connecting...");
    pthread_testcancel();

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
      dxcluster_set_status("Socket error");
      break;
    }

    int flags = fcntl(sock, F_GETFL, 0);
    if (flags >= 0)
      fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    dxcluster_set_status("Resolving host...");
    pthread_testcancel();

    struct hostent *host = gethostbyname(config.dxc_host);
    if (!host) {
      dxcluster_set_status("DNS failed");
      close(sock);
      if (dxcluster_sleep_or_stop(5) < 0)
        break;
      continue;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config.dxc_port);
    memcpy(&addr.sin_addr, host->h_addr_list[0], host->h_length);

    dxcluster_set_status("Connecting to host...");
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
      if (errno != EINPROGRESS && errno != EWOULDBLOCK) {
        dxcluster_set_status("Connect failed");
        close(sock);
        if (dxcluster_sleep_or_stop(1) < 0)
          break;
        continue;
      }
    }

    int sel = dxcluster_wait_for_socket(sock, 1, 30);
    if (sel < 0) {
      close(sock);
      break;
    }

    if (sel == 0) {
      dxcluster_set_status("Connect timeout");
      close(sock);
      if (dxcluster_sleep_or_stop(1) < 0)
        break;
      continue;
    }

    int so_error = 0;
    socklen_t len = sizeof(so_error);
    getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &len);
    if (so_error != 0) {
      dxcluster_set_status("Connect failed");
      close(sock);
      if (dxcluster_sleep_or_stop(1) < 0)
        break;
      continue;
    }

    dxcluster_set_status("Connected");

    bool login_sent = config.dxc_call[0] ? false : true;
    char buf[512];
    char pending[1024] = {0};
    size_t pending_len = 0;
    int login_wait_loops = 0;

    if (login_sent) {
      if (send_cluster_commands(sock) < 0) {
        dxcluster_set_status("Init command send failed");
        close(sock);
        if (dxcluster_sleep_or_stop(1) < 0)
          break;
        continue;
      }

      dxcluster_set_status("Connected");
    } else {
      dxcluster_set_status("Waiting for login prompt...");
    }

    while (running) {
      int n = 0;
      int ready = dxcluster_wait_for_socket(sock, 0, -1);
      if (ready < 0)
        break;

      n = read(sock, buf, sizeof(buf));
      if (n <= 0)
        break;

      if ((size_t)n + pending_len >= sizeof(pending))
        pending_len = 0;

      memcpy(pending + pending_len, buf, (size_t)n);
      pending_len += (size_t)n;
      pending[pending_len] = 0;

      if (!login_sent && looks_like_login_prompt(pending)) {
        if (send_cluster_login_call(sock) < 0) {
          dxcluster_set_status("Login send failed");
          break;
        }

        if (send_cluster_commands(sock) < 0) {
          dxcluster_set_status("Command send failed");
          break;
        }

        login_sent = true;
        dxcluster_set_status("Logged in");
      }

      char *line_start = pending;
      char *newline = NULL;

      while ((newline = strchr(line_start, '\n')) != NULL) {
        *newline = 0;
        char *line = line_start;
        if (*line == '\r')
          line++;

        while (*line && isspace((unsigned char)*line))
          line++;

        if (!login_sent && looks_like_login_prompt(line)) {
          if (send_cluster_login_call(sock) < 0) {
            dxcluster_set_status("Login send failed");
            break;
          }

          if (send_cluster_commands(sock) < 0) {
            dxcluster_set_status("Command send failed");
            break;
          }

          login_sent = true;
          dxcluster_set_status("Logged in");
          line_start = newline + 1;
          continue;
        }

        if (line[0] != '\0' && dxcluster_line_is_dx_de(line)) {
          add_spot(line);
          dxcluster_set_status("Spot received");
        }

        line_start = newline + 1;
      }

      if (line_start != pending) {
        pending_len = strlen(line_start);
        memmove(pending, line_start, pending_len + 1);
      }

      if (!login_sent) {
        login_wait_loops++;
        if (login_wait_loops >= 50) {
          if (send_cluster_login_call(sock) < 0) {
            dxcluster_set_status("Login fallback failed");
            break;
          }

          if (send_cluster_commands(sock) < 0) {
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
    if (dxcluster_sleep_or_stop(1) < 0)
      break;
  }

  if (!running)
    dxcluster_set_status("Disconnected");

  return NULL;
}

/* ------------------------------------------------ */

/*
 * Start the background DXCluster worker thread.
 *
 * @return 0 on success, or -1 on failure.
 */
int dxcluster_start(void) {
  running = 1;

  if (stop_pipe[0] < 0 && dxcluster_create_stop_pipe() != 0) {
    running = 0;
    return -1;
  }

  if (pthread_create(&thread_id, NULL, cluster_thread, NULL) != 0) {
    running = 0;
    dxcluster_close_stop_pipe();
    return -1;
  }

  return 0;
}

/* ------------------------------------------------ */

/*
 * Stop the background DXCluster worker thread and release its resources.
 *
 * @return Nothing.
 */
void dxcluster_stop(void) {
  running = 0;
  dxcluster_signal_stop();
  pthread_cancel(thread_id);
  pthread_join(thread_id, NULL);
  dxcluster_close_log();
  dxcluster_close_stop_pipe();
}
