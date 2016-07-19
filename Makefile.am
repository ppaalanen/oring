ACLOCAL_AMFLAGS = -I m4

AM_CFLAGS = $(GCC_CFLAGS)
AM_CPPFLAGS = -I$(builddir)/protocol

bin_PROGRAMS =
BUILT_SOURCES =
CLEANFILES = $(BUILT_SOURCES)

bin_PROGRAMS += oring-cal
oring_cal_SOURCES =							\
	protocol/presentation-time-protocol.c				\
	src/cal.c							\
	src/oring-clock.c						\
	src/oring-clock.h						\
	src/output.c							\
	src/output.h							\
	src/timespec-util.h						\
	src/platform.h							\
	src/zalloc.h							\
	src/xalloc.h							\
	src/xalloc.c							\
	src/helpers.h
nodist_oring_cal_SOURCES =
oring_cal_CFLAGS = $(AM_CFLAGS) $(ORING_CAL_CFLAGS)
oring_cal_LDADD = $(ORING_CAL_LIBS) $(CLOCK_GETTIME_LIBS) -lm

BUILT_SOURCES +=							\
	protocol/presentation-time-protocol.c				\
	protocol/presentation-time-client-protocol.h


.SECONDEXPANSION:

define protostability
$(if $(findstring unstable,$1),unstable,stable)
endef

define protoname
$(shell echo $1 | $(SED) 's/\([a-z\-]\+\)-[a-z]\+-v[0-9]\+/\1/')
endef

protocol/%-protocol.c : $(WAYLAND_PROTOCOLS_DATADIR)/$$(call protostability,$$*)/$$(call protoname,$$*)/$$*.xml
	$(AM_V_GEN)$(MKDIR_P) $(dir $@) && $(wayland_scanner) code < $< > $@

protocol/%-client-protocol.h : $(WAYLAND_PROTOCOLS_DATADIR)/$$(call protostability,$$*)/$$(call protoname,$$*)/$$*.xml
	$(AM_V_GEN)$(MKDIR_P) $(dir $@) && $(wayland_scanner) client-header < $< > $@

protocol/%-protocol.c : $(top_srcdir)/protocol/%.xml
	$(AM_V_GEN)$(MKDIR_P) $(dir $@) && $(wayland_scanner) code < $< > $@

protocol/%-client-protocol.h : $(top_srcdir)/protocol/%.xml
	$(AM_V_GEN)$(MKDIR_P) $(dir $@) && $(wayland_scanner) client-header < $< > $@
