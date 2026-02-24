TEST_CFLAGS = $(CFLAGS) $(LIBEVENT_CFLAGS)
TEST_LIBS = $(LIBS) $(LIBEVENT_LIBS)

AM_CPPFLAGS = -I$(top_srcdir)

test1_SOURCES = test1.c
test1_LDADD = $(top_builddir)/libairptp.a -lpthread $(TEST_LIBS)
test1_CFLAGS = $(TEST_CFLAGS)

check_PROGRAMS = test1
