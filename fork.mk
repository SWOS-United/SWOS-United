# fork.mk -- SWOS United's own build additions. OpenSWOS's Makefile -include's this file
# (docs/FORKING.md), so SWOS United never edits the Makefile itself.
#
# mbedTLS (third_party/mbedtls, 3.6.2 -- Apache-2.0 OR GPL-2.0-or-later): HTTPS to
# sensiblesoccer.de -- the account log-in, the rankings, the leagues and the match results
# (src/https_client.c). Native netplay builds only; a browser has HTTPS of its own.
ifeq ($(NETPLAY),1)
ifndef WASM
MBEDTLS_DIR  := third_party/mbedtls
MBEDTLS_SRCS := $(wildcard $(MBEDTLS_DIR)/library/*.c)
MBEDTLS_OBJS := $(patsubst $(MBEDTLS_DIR)/library/%.c,$(OBJDIR)/mbedtls/%.o,$(MBEDTLS_SRCS))
OBJECTS      += $(MBEDTLS_OBJS)
DEPS         += $(MBEDTLS_OBJS:.o=.d)
CFLAGS       += -I$(MBEDTLS_DIR)/include -DSWOS_HTTPS=1
# -w: third-party code, compiled as upstream ships it.
FORK_CFLAGS  := $(ARCHFLAGS) -O2 -g -MMD -MP -w -I$(MBEDTLS_DIR)/include -I$(MBEDTLS_DIR)/library

# ($(OBJDIR)/.build-flags: the Makefile's BUILD_FLAGS_STAMP, not defined yet where this is read)
$(OBJDIR)/mbedtls/%.o: $(MBEDTLS_DIR)/library/%.c $(OBJDIR)/.build-flags
	@mkdir -p $(dir $@)
	$(CC) $(FORK_CFLAGS) -c $< -o $@
endif
endif
