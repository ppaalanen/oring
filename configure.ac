m4_define([oring_major_version], [0])
m4_define([oring_minor_version], [0])
m4_define([oring_micro_version], [1])
m4_define([oring_version],
          [oring_major_version.oring_minor_version.oring_micro_version])

AC_PREREQ([2.64])
AC_INIT([oring],
        [oring_version],
        [],
        [oring],
        [])

AC_SUBST([ORING_VERSION_MAJOR], [oring_major_version])
AC_SUBST([ORING_VERSION_MINOR], [oring_minor_version])
AC_SUBST([ORING_VERSION_MICRO], [oring_micro_version])
AC_SUBST([ORING_VERSION], [oring_version])

AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

AC_USE_SYSTEM_EXTENSIONS
AC_SYS_LARGEFILE

AM_INIT_AUTOMAKE([1.11 parallel-tests foreign no-dist-gzip dist-xz color-tests subdir-objects])

AM_SILENT_RULES([yes])

# Check for programs
AC_PROG_CC

PKG_PROG_PKG_CONFIG()

AC_CHECK_DECL(CLOCK_MONOTONIC,[],
	      [AC_MSG_ERROR("CLOCK_MONOTONIC is needed")],
	      [[#include <time.h>]])

PKG_CHECK_MODULES(WAYLAND_PROTOCOLS, [wayland-protocols >= 1.2],
		  [ac_wayland_protocols_pkgdatadir=`$PKG_CONFIG --variable=pkgdatadir wayland-protocols`])
AC_SUBST(WAYLAND_PROTOCOLS_DATADIR, $ac_wayland_protocols_pkgdatadir)


PKG_CHECK_MODULES(SIMPLE_EGL_CLIENT,
                  [egl glesv2 wayland-client wayland-egl wayland-cursor])

if test "x$GCC" = "xyes"; then
	GCC_CFLAGS="-Wall -Wextra -Wno-unused-parameter \
		-Wno-missing-field-initializers -g -fvisibility=hidden \
		-Wstrict-prototypes -Wmissing-prototypes -Wsign-compare"
fi
AC_SUBST(GCC_CFLAGS)

AC_PATH_PROG([wayland_scanner], [wayland-scanner])
if test x$wayland_scanner = x; then
	PKG_CHECK_MODULES(WAYLAND_SCANNER, [wayland-scanner])
	wayland_scanner=`$PKG_CONFIG --variable=wayland_scanner wayland-scanner`
fi

AC_CONFIG_FILES([Makefile])

AC_OUTPUT
