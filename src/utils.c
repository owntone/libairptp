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
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/time.h>
#include <errno.h>
#include <inttypes.h>
#include <fcntl.h>

#include "utils.h"

int
net_bind(const char *node, unsigned short port)
{
  struct addrinfo hints = { 0 };
  struct addrinfo *servinfo;
  struct addrinfo *ptr;
  char strport[8];
  int flags;
  int yes = 1;
  int no = 0;
  int fd = -1;
  int ret;

  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_family = AF_INET6;
  hints.ai_flags = node ? 0 : AI_PASSIVE;

  snprintf(strport, sizeof(strport), "%hu", port);
  ret = getaddrinfo(node, strport, &hints, &servinfo);
  if (ret < 0)
    goto error;

  for (ptr = servinfo; ptr != NULL; ptr = ptr->ai_next)
    {
      if (fd >= 0)
	close(fd);

      fd = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
      if (fd < 0)
	continue;

      flags = fcntl(fd, F_GETFL, 0);
      if (flags < 0)
	continue;
      ret = fcntl(fd, F_SETFL, flags | O_CLOEXEC);
      if (ret < 0)
	continue;

      // TODO libevent sets this, we do the same?
      ret = setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(yes));
      if (ret < 0)
	continue;

      ret = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
      if (ret < 0)
	continue;

      // We want to make sure the socket is dual stack
      ret = (ptr->ai_family == AF_INET6) ? setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &no, sizeof(no)) : 0;
      if (ret < 0)
	continue;

      ret = bind(fd, ptr->ai_addr, ptr->ai_addrlen);
      if (ret < 0)
	continue;

      break;
    }

  freeaddrinfo(servinfo);

  if (!ptr)
    goto error;

  return fd;

 error:
  if (fd >= 0)
    close(fd);
  return -1;
}

void
hexdump(const char *msg, uint8_t *data, size_t data_len)
{
  return; //TODO
}

void
logmsg(const char *fmt, ...)
{
  return; //TODO
}
