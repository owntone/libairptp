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
#include "ptp_definitions.h"
#include "daemon.h"


/* -------------------------------- Globals --------------------------------- */

// Shared
const char *airptp_errmsg;


/* -------------------------------- Helpers --------------------------------- */

static void dummy_thread_name_set(const char *name) { return; }
static void dummy_hexdump(const char *msg, uint8_t *data, size_t data_len) { return; }
static void dummy_logmsg(const char *fmt, ...) { return; }

static struct airptp_callbacks dummy_cb = {
  .thread_name_set = dummy_thread_name_set,
  .hexdump = dummy_hexdump,
  .logmsg = dummy_logmsg,
};

/* ----------------------------------- API ---------------------------------- */

struct airptp_handle *
airptp_daemon_bind(void)
{
  struct airptp_handle *hdl = NULL;
  int fd_event = -1;
  int fd_general = -1;

  fd_event = net_bind(NULL, PTP_EVENT_PORT);
  if (fd_event < 0)
    goto error;

  fd_general = net_bind(NULL, PTP_GENERAL_PORT);
  if (fd_general < 0)
    goto error;

  hdl = calloc(1, sizeof(struct airptp_handle));
  if (!hdl)
    goto error;

  hdl->daemon.event_svc.port = PTP_EVENT_PORT;
  hdl->daemon.event_svc.fd = fd_event;

  hdl->daemon.general_svc.port = PTP_GENERAL_PORT;
  hdl->daemon.general_svc.fd = fd_general;

  hdl->state = AIRPTP_STATE_PORTS_BOUND;
  hdl->is_daemon = true;

  return hdl;

 error:
  free(hdl);
  if (fd_event >= 0)
    close(fd_event);
  if (fd_general >= 0)
    close(fd_general);
  return NULL;
}

// Starts a PTP daemon. Ports must have been bound already. Starting the daemon
// does not require privileges.
int
airptp_daemon_start(struct airptp_handle *hdl, uint64_t clock_id_seed, bool is_shared, struct airptp_callbacks *cb)
{
  int ret;

  if (!hdl->is_daemon || hdl->state != AIRPTP_STATE_PORTS_BOUND)
    goto error;

  if (!cb)
    cb = &dummy_cb;

  // From IEEE EUI-64 clockIdentity values: "The most significant 3 octets of
  // the clockIdentity shall be an OUI. The least significant two bits of the
  // most significant octet of the OUI shall both be 0. The least significant
  // bit of the most significant octet of the OUI is used to distinguish
  // clockIdentity values specified by this subclause from those specified in
  // 7.5.2.2.3 [Non-IEEE EUI-64 clockIdentity values]".
  // If we had the MAC address at this point we, could make a valid EUI-48 based
  // clocked from mac[0..2] + 0xFFFE + mac[3..5]. However, since we don't, we
  // create a non-EUI-64 clock ID from 0xFFFF + 6 byte seed, ref 7.5.2.2.3.
  hdl->clock_id = clock_id_seed | 0xFFFF000000000000;

  ret = daemon_start(&hdl->daemon, is_shared, hdl->clock_id, cb);
  if (ret < 0)
    goto error;

  hdl->state = AIRPTP_STATE_RUNNING;

  return 0;

 error:
  return -1;
}

struct airptp_handle *
airptp_daemon_find(void)
{
  struct airptp_handle *hdl = NULL;
  struct airptp_shm_struct *daemon_info = MAP_FAILED;
  time_t now;
  int fd = -1;

  fd = shm_open(AIRPTP_SHM_NAME, O_RDONLY, 0);
  if (fd < 0)
    goto out;

  daemon_info = mmap(NULL, sizeof(struct airptp_shm_struct), PROT_READ, MAP_SHARED, fd, 0);
  if (daemon_info == MAP_FAILED)
    goto out;

  if (daemon_info->version_major != AIRPTP_SHM_STRUCTS_VERSION_MAJOR)
    goto out;

  now = time(NULL);
  if (daemon_info->ts + AIRPTP_SHM_STALE_SECS < now)
    goto out;

  hdl = calloc(1, sizeof(struct airptp_handle));
  if (!hdl)
    goto out;

  hdl->clock_id = daemon_info->clock_id;
  hdl->state = AIRPTP_STATE_RUNNING;
  hdl->is_daemon = false;

  munmap(daemon_info, sizeof(struct airptp_shm_struct));
  close(fd);

  return hdl;

 out:
  free(hdl);
  if (daemon_info != MAP_FAILED)
    munmap(daemon_info, sizeof(struct airptp_shm_struct));
  if (fd >= 0)
    close(fd);
  return NULL;
}

void
airptp_free(struct airptp_handle *hdl)
{
  if (!hdl)
    return;

  if (hdl->is_daemon)
    daemon_stop(&hdl->daemon);

  free(hdl);
}

int
airptp_clock_id_get(uint64_t *clock_id, struct airptp_handle *hdl)
{
  if (hdl->state != AIRPTP_STATE_RUNNING)
    return -1;

  *clock_id = hdl->clock_id;
  return 0;
}

const char *
airptp_last_errmsg(void)
{
  return "NOT IMPL";
//  return airptp_errmsg;
}
