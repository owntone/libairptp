#ifndef __AIRPTP_DAEMON_H__
#define __AIRPTP_DAEMON_H__

int
daemon_start(struct airptp_daemon *daemon, bool is_shared, uint64_t clock_id, struct airptp_callbacks *cb);

void
daemon_stop(struct airptp_daemon *daemon);

#endif // __AIRPTP_DAEMON_H__
