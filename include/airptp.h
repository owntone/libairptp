#ifndef __AIRPTP_H__
#define __AIRPTP_H__

#include <inttypes.h>

struct airptp_handle;

struct airptp_callbacks
{
  // Optional - set name of thread
  void (*thread_name_set)(const char *name);

  // Debugging
  void (*hexdump)(const char *msg, uint8_t *data, size_t data_len);
  void (*logmsg)(const char *fmt, ...);
};

// Returns a handle if it was possible to bind to port 319 and 320. This
// normally requires root privilies.
struct airptp_handle *
airptp_daemon_bind(void);

// Starts a PTP daemon. Ports must have been bound already. Starting the daemon
// does not require privileges.
int
airptp_daemon_start(struct airptp_handle *hdl, uint64_t clock_id_seed, bool is_shared, struct airptp_callbacks *cb);

// Returns a handle if the host is running a compatible airptp daemon.
struct airptp_handle *
airptp_daemon_find(void);

void
airptp_free(struct airptp_handle *hdl);

// exit_cb?

int
airptp_clock_id_get(uint64_t *clock_id, struct airptp_handle *hdl);

const char *
airptp_last_errmsg(void);

#endif // __AIRPTP_H__
