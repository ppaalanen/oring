ACLOCAL_AMFLAGS = -I m4

bin_PROGRAMS =
noinst_PROGRAMS =
BUILT_SOURCES =

AM_CFLAGS = $(GCC_CFLAGS)
AM_CPPFLAGS = -I$(builddir)/protocol

CLEANFILES = $(BUILT_SOURCES)

bin_PROGRAMS += weston-simple-egl
weston_simple_egl_SOURCES = src/simple-egl.c
nodist_weston_simple_egl_SOURCES =		\
	protocol/xdg-shell-unstable-v5-protocol.c		\
	protocol/xdg-shell-unstable-v5-client-protocol.h
weston_simple_egl_CFLAGS = $(AM_CFLAGS) $(SIMPLE_EGL_CLIENT_CFLAGS)
weston_simple_egl_LDADD = $(SIMPLE_EGL_CLIENT_LIBS) -lm

BUILT_SOURCES +=					\
	protocol/presentation-time-protocol.c				\
	protocol/presentation-time-client-protocol.h			\
	protocol/xdg-shell-unstable-v5-protocol.c			\
	protocol/xdg-shell-unstable-v5-client-protocol.h

# To remove when automake 1.11 support is dropped
export abs_builddir


.SECONDEXPANSION:

define protostability
$(if $(findstring unstable,$1),unstable,stable)
endef

define protoname
$(shell echo $1 | sed 's/\([a-z\-]\+\)-[a-z]\+-v[0-9]\+/\1/')
endef

protocol/%-protocol.c : $(WAYLAND_PROTOCOLS_DATADIR)/$$(call protostability,$$*)/$$(call protoname,$$*)/$$*.xml
	$(AM_V_GEN)$(MKDIR_P) $(dir $@) && $(wayland_scanner) code < $< > $@

protocol/%-client-protocol.h : $(WAYLAND_PROTOCOLS_DATADIR)/$$(call protostability,$$*)/$$(call protoname,$$*)/$$*.xml
	$(AM_V_GEN)$(MKDIR_P) $(dir $@) && $(wayland_scanner) client-header < $< > $@

protocol/%-protocol.c : $(top_srcdir)/protocol/%.xml
	$(AM_V_GEN)$(MKDIR_P) $(dir $@) && $(wayland_scanner) code < $< > $@

protocol/%-client-protocol.h : $(top_srcdir)/protocol/%.xml
	$(AM_V_GEN)$(MKDIR_P) $(dir $@) && $(wayland_scanner) client-header < $< > $@
