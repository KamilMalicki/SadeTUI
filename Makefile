 # Copyright 2026 KamilMalicki
 #
 # Licensed under the Apache License, Version 2.0 (the "License");
 # you may not use this file except in compliance with the License.
 # You may obtain a copy of the License at
 #
 # http://www.apache.org/licenses/LICENSE-2.0
 #
 # Unless required by applicable law or agreed to in writing, software
 # distributed under the License is distributed on an "AS IS" BASIS,
 # WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 # See the License for the specific language governing permissions and
 # limitations under the License.
# --- Configuration ---
CC       = gcc
CFLAGS   = -O3 -Wall -Wextra
# We use pkg-config to find library flags automatically
PKG_CONFIG = pkg-config
DEPS       = freetype2 vterm
# Check for dependencies
ifeq ($(shell $(PKG_CONFIG) --exists $(DEPS) || echo "missing"), missing)
    $(error One or more dependencies (freetype2, vterm) are missing. Please install them.)
endif

# Get flags from pkg-config
CPPFLAGS = $(shell $(PKG_CONFIG) --cflags $(DEPS))
LDLIBS   = $(shell $(PKG_CONFIG) --libs $(DEPS)) -lutil

# Pretty printing
V = @
Q = $(V:1=)
# Define output colors
GREEN = \033[0;32m
NC    = \033[0m # No Color

# --- Project Setup ---
TARGET = Sade
SRCS   = main.c
OBJS   = $(SRCS:.c=.o)

.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): $(OBJS)
	@printf "  $(GREEN)[ LD ]$(NC)  $@\n"
	$(Q)$(CC) $(OBJS) -o $@ $(LDLIBS)

%.o: %.c
	@printf "  $(GREEN)[ CC ]$(NC)  $<\n"
	$(Q)$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

clean:
	@printf "  [ RM ]  Removing object files and binary\n"
	$(Q)rm -f $(OBJS) $(TARGET)

install: $(TARGET)
	@printf "  [ INSTALL ] Installing to /usr/local/bin\n"
	$(Q)install -d $(DESTDIR)/usr/local/bin
	$(Q)install -m 755 $(TARGET) $(DESTDIR)/usr/local/bin/

uninstall:
	@printf "  [ UNINSTALL ] Removing from /usr/local/bin\n"
	$(Q)rm -f $(DESTDIR)/usr/local/bin/$(TARGET)
