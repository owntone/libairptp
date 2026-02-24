/*
MIT License

Copyright (c) 2026 OwnTone

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

// For shm_open
#include <sys/mman.h>
#include <sys/stat.h>        /* For mode constants */
#include <fcntl.h>

#include "airptp_internal.h"
#include "msg_handle.h"

#define DAEMON_INTERVAL_SECS_SHM_UPDATE 5

static struct timeval daemon_send_announce_tv =
{
  .tv_sec = AIRPTP_INTERVAL_MS_ANNOUNCE / 1000,
  .tv_usec = (AIRPTP_INTERVAL_MS_ANNOUNCE % 1000) * 1000
};
static struct timeval daemon_send_signaling_tv =
{
  .tv_sec = AIRPTP_INTERVAL_MS_SIGNALING / 1000,
  .tv_usec = (AIRPTP_INTERVAL_MS_SIGNALING % 1000) * 1000
};
static struct timeval daemon_send_sync_tv =
{
  .tv_sec = AIRPTP_INTERVAL_MS_SYNC / 1000,
  .tv_usec = (AIRPTP_INTERVAL_MS_SYNC % 1000) * 1000
};
static struct timeval daemon_shm_update_tv =
{
  .tv_sec = DAEMON_INTERVAL_SECS_SHM_UPDATE,
  .tv_usec = 0
};

static void
daemon_shm_destroy(struct airptp_shm_struct *shm, int fd)
{
  if (shm != MAP_FAILED)
    munmap(shm, sizeof(struct airptp_shm_struct));
  if (fd >= 0)
    close(fd);
  shm_unlink(AIRPTP_SHM_NAME);
}

static int
daemon_shm_create(struct airptp_shm_struct **shm, uint64_t clock_id)
{
  struct airptp_shm_struct *info = MAP_FAILED;
  int fd;
  int ret;

  fd = shm_open(AIRPTP_SHM_NAME, O_CREAT | O_EXCL | O_RDWR, 0644);
  if (fd < 0)
    goto error;

  ret = ftruncate(fd, sizeof(struct airptp_shm_struct));
  if (ret < 0)
    goto error;

  info = mmap(NULL, sizeof(struct airptp_shm_struct), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (info == MAP_FAILED)
    goto error;

  info->version_major = AIRPTP_SHM_STRUCTS_VERSION_MAJOR;
  info->version_minor = AIRPTP_SHM_STRUCTS_VERSION_MINOR;
  info->clock_id = clock_id;
  info->ts = time(NULL);

  *shm = info;

  return fd;

 error:
  daemon_shm_destroy(info, fd);
  return -1;   
}

static void
service_stop(struct airptp_service *svc)
{
  if (svc->ev)
    event_free(svc->ev);

  svc->ev = NULL;
}

static int
service_start(struct airptp_service *svc, event_callback_fn cb, struct airptp_daemon *daemon)
{
  svc->ev = event_new(daemon->evbase, svc->fd, EV_READ | EV_PERSIST, cb, daemon);
  if (!svc->ev)
    goto error;

  event_add(svc->ev, NULL);
  return 0;

 error:
  service_stop(svc);
  return -1;
}


/* ============================== Event handling ============================ */

static void
send_announce_cb(int fd, short what, void *arg)
{
  struct airptp_daemon *daemon = arg;

  if (daemon->num_slaves == 0)
    return; // Don't reschedule

  msg_announce_send(daemon);

  event_add(daemon->send_announce_timer, &daemon_send_announce_tv);
}

static void
send_signaling_cb(int fd, short what, void *arg)
{
  struct airptp_daemon *daemon = arg;

  if (daemon->num_slaves == 0)
    return; // Don't reschedule

  msg_signaling_send(daemon);

  event_add(daemon->send_signaling_timer, &daemon_send_signaling_tv);
}

static void
send_sync_cb(int fd, short what, void *arg)
{
  struct airptp_daemon *daemon = arg;

  if (daemon->num_slaves == 0)
    return; // Don't reschedule

  msg_sync_send(daemon);

  event_add(daemon->send_sync_timer, &daemon_send_sync_tv);
}

static void
incoming_cb(int fd, short what, void *arg)
{
  struct airptp_daemon *daemon = arg;
  const char *svc_name = (fd == daemon->event_svc.fd) ? "PTP EVENT" : "PTP GENERAL";
  union net_sockaddr peer_addr;
  socklen_t peer_addrlen = sizeof(peer_addr);
  uint8_t req[1024];
  ssize_t len;

  // Shouldn't be necessary, but silences scan-build complaint about sa_family
  // possibly being garbage after recvfrom()
  peer_addr.sa.sa_family = AF_UNSPEC;

  len = recvfrom(fd, req, sizeof(req), 0, &peer_addr.sa, &peer_addrlen);
  if (len <= 0 || peer_addr.sa.sa_family == AF_UNSPEC)
    {
      if (len < 0)
	logmsg("Service %s read error: %s\n", svc_name, strerror(errno));
      return;
    }

  msg_handle(daemon, req, len, &peer_addr, peer_addrlen);
}

static void
shm_update_cb(int fd, short what, void *arg)
{
  struct airptp_daemon *daemon = arg;

  daemon->info->ts = time(NULL);

  event_add(daemon->shm_update_timer, &daemon_shm_update_tv);
}

static void
exit_cb(int fd, short what, void *arg)
{
  struct airptp_daemon *daemon = arg;
  char buf[1];
  int ret;

  ret = read(fd, buf, 1);
  if (ret < 0)
    logmsg("Unexpected error from exit_cb read");

  event_base_loopbreak(daemon->evbase);
  daemon->is_running = false;
}


/* ------------------------------- Main loop -------------------------------- */

// Runs a PTP clock daemon either shared (with a shared mem interface) or
// private
static void *
run(void *arg)
{
  struct airptp_daemon *daemon = arg;
  int shm_fd = -1;
  int ret;

  if (daemon->cb && daemon->cb->thread_name_set)
    daemon->cb->thread_name_set("libairptp");

  ret = service_start(&daemon->event_svc, incoming_cb, daemon);
  if (ret < 0)
    goto stop;

  ret = service_start(&daemon->general_svc, incoming_cb, daemon);
  if (ret < 0)
    goto stop;

  daemon->exit_ev = event_new(daemon->evbase, daemon->exit_pipe[0], EV_READ | EV_PERSIST, exit_cb, daemon);
  if (!daemon->exit_ev)
    goto stop;
  event_add(daemon->exit_ev, NULL);

  daemon->shm_update_timer = evtimer_new(daemon->evbase, shm_update_cb, daemon);
  if (!daemon->shm_update_timer)
    goto stop;
  event_add(daemon->shm_update_timer, &daemon_shm_update_tv);

  daemon->send_announce_timer = evtimer_new(daemon->evbase, send_announce_cb, daemon);
  daemon->send_signaling_timer = evtimer_new(daemon->evbase, send_signaling_cb, daemon);
  daemon->send_sync_timer = evtimer_new(daemon->evbase, send_sync_cb, daemon);
  if (!daemon->send_announce_timer || !daemon->send_signaling_timer || !daemon->send_sync_timer)
    goto stop;

  if (daemon->is_shared) {
    shm_fd = daemon_shm_create(&daemon->info, daemon->clock_id);
    if (shm_fd < 0)
      goto stop;
  }

  event_base_dispatch(daemon->evbase);

  if (daemon->is_running)
    logmsg("Event loop terminated ahead of time");

 stop:
  daemon_shm_destroy(daemon->info, shm_fd);
  if (daemon->send_announce_timer)
    event_free(daemon->send_announce_timer);
  if (daemon->send_signaling_timer)
    event_free(daemon->send_signaling_timer);
  if (daemon->send_sync_timer)
    event_free(daemon->send_sync_timer);
  if (daemon->exit_ev)
    event_free(daemon->exit_ev);
  service_stop(&daemon->general_svc);
  service_stop(&daemon->event_svc);
  pthread_exit(NULL);
}

int
daemon_start(struct airptp_daemon *daemon, bool is_shared, uint64_t clock_id, struct airptp_callbacks *cb)
{
  int ret;

  ret = msg_handle_init();
  if (ret < 0)
    goto error;

  ret = pipe(daemon->exit_pipe);
  if (ret < 0)
    goto error;

  evutil_make_socket_nonblocking(daemon->exit_pipe[0]);
  evutil_make_socket_nonblocking(daemon->exit_pipe[1]);

  daemon->info = MAP_FAILED;
  daemon->is_shared = is_shared;
  daemon->clock_id = clock_id;
  daemon->cb = cb;

  daemon->evbase = event_base_new();
  if (!daemon->evbase)
    goto error;

  ret = pthread_create(&daemon->tid, NULL, run, daemon);
  if (ret < 0)
    goto error;

  daemon->is_running = true;

  return 0;

 error:
  if (daemon->evbase)
    event_base_free(daemon->evbase);
  return -1;
}

void
daemon_stop(struct airptp_daemon *daemon)
{
  char byte = 1;
  int ret;

  if (!daemon->is_running)
    goto cleanup;

  ret = write(daemon->exit_pipe[1], &byte, 1);
  if (ret < 0)
    logmsg("Unexpected return value from daemon_stop() write");

  ret = pthread_join(daemon->tid, NULL);
  if (ret != 0)
    logmsg("Could not join ptpd thread: %s\n", strerror(errno));

 cleanup:
  event_base_free(daemon->evbase);

  // These were opened by airptp_daemon_bind()
  if (daemon->event_svc.fd >= 0)
    close(daemon->event_svc.fd);
  if (daemon->general_svc.fd >= 0)
    close(daemon->general_svc.fd);
}
