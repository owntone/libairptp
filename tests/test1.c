#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

// TODO fix "include"
#include "include/airptp.h"

int
main(int argc, char * argv[])
{
  struct airptp_handle *hdl;
  uint64_t clock_id;
  int ret;

  hdl = airptp_daemon_find();
  if (!hdl) {
    printf("test1.c no running daemon found, will make one\n");
    hdl = airptp_daemon_bind();
    if (!hdl)
      goto error;

    ret = airptp_daemon_start(hdl, 1, true, NULL);
    if (ret < 0)
      goto error;
  }

  ret = airptp_clock_id_get(&clock_id, hdl);
  if (ret < 0)
    goto error;

  printf("test1.c result clock_id=%" PRIx64 "\n", clock_id);

  sleep(30);

  airptp_free(hdl);

  return 0;

 error:
  printf("test1.c error: %s\n", airptp_last_errmsg());
  return -1;
}
