# Makefile for apt-transport-sftp

all: sftp

CXXFLAGS = -g -O2 -Wall
LDLIBS   = -lapt-pkg
LDLIBS  += $(shell pkg-config --libs libssh2)

sftp_SRCS  = sftp.cc connect.cc rfc2553emu.cc

sftp_OBJS  = $(patsubst %.cc,%.o,$(sftp_SRCS))
sftp_DEPS  = $(patsubst %.cc,.%.d,$(sftp_SRCS))

$(sftp_OBJS) : %.o : %.cc
	$(CXX) -c $(CXXFLAGS) $< -o $@

$(sftp_DEPS) : .%.d : %.cc
	$(CXX) -MM $(CXXFLAGS) $<  > $@

include $(sftp_DEPS)

sftp : $(sftp_OBJS)
	$(CXX) -o $@ $(sftp_OBJS) $(LDLIBS)

install: sftp
	install -d $(DESTDIR)/usr/lib/apt/methods/
	install sftp $(DESTDIR)/usr/lib/apt/methods/

clean:
	@rm -f .*.d *.o *~ core sftp *.dump

.PHONY: all clean test install
