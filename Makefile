#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://illumos.org.
#
# Copyright 2026 token.paul
#

CC      = gcc
CFLAGS  = -O2 -Wall -Wextra
LDLIBS  = -ldns_sd -lsocket -lnsl

TARGET  = mdns-adv
SRC     = mdns-adv.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) $(LDLIBS) -o $(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all clean

