#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.61])
AC_INIT([DVBZAP], [0.1], [l2mrroberto@gmail.com])
AC_CONFIG_SRCDIR([src/dvbzap.c])
AM_INIT_AUTOMAKE
AC_CONFIG_HEADERS([src/config.h])

dnl
dnl Check for linux dvb api headers
dnl
AC_CHECK_HEADER([linux/dvb/dmx.h])
if test "$ac_cv_header_linux_dvb_dmx_h" = no; then
        AC_MSG_ERROR([linux dvb api headers are required to build])
fi

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_MAKE_SET
AC_PROG_CC_C99


AC_CHECK_LIB([dvbapi], [dvbdemux_set_section_filter])
AC_CHECK_LIB([ucsi], [atsc_text_segment_decode],[atsc_long_names="yes"], [atsc_long_names="no"])
AC_CHECK_LIB([pthread], [pthread_create])
AC_CHECK_LIB([rt], [clock_gettime])
AC_CHECK_LIB([m], [log2l])


dnl
dnl  Test coverage
dnl
AC_ARG_ENABLE(coverage,
	[  --enable-coverage       build for test coverage (default disabled)],,
	[enable_coverage="no"])
AS_IF([test "${enable_coverage}" != "no"], [
	CFLAGS="-fprofile-arcs -ftest-coverage ${CFLAGS}"
	CXXFLAGS="-fprofile-arcs -ftest-coverage ${CXXFLAGS}"
	LDFLAGS="-lgcov ${LDFLAGS}"
])

dnl
dnl duma support
dnl
AC_ARG_ENABLE(duma,
  [  --enable-duma          Debbuging DUMA library (default disabled)],,
  [enable_duma="no"])

if test "${enable_duma}" = "yes"
then
  AC_CHECK_LIB([duma], [_duma_malloc],[], [enable_duma="no"])
  if test "${enable_duma}" != "no"
  then
    LDFLAGS="-lduma ${LDFLAGS}"
  else
    AC_MSG_WARN([The DUMA library needs to be installed before])
  fi
fi

AC_ARG_ENABLE(tune_old,
  [  --enable-tune_old          Use old code for tuning (default disabled)],,[enable_tune_old="no"])

if test "${enable_tune_old}" = "yes"
then
  AC_DEFINE(TUNE_OLD, 1, Define if you want the old code for tuning)
fi

# Checks for header files.
AC_HEADER_RESOLV
AC_CHECK_HEADERS([arpa/inet.h fcntl.h netdb.h netinet/in.h stdint.h stdlib.h string.h sys/ioctl.h sys/socket.h sys/time.h syslog.h unistd.h values.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_INT32_T
AC_TYPE_SIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_CHECK_FUNCS([alarm gettimeofday inet_ntoa memset socket strerror strstr])

AC_CONFIG_FILES([Makefile
                 src/Makefile])
AC_OUTPUT

#put resume at the end
echo ""
echo "-------------------------------------------------------"
echo "DVBZAP configure results:"
echo ""
echo "Features"
echo ""

if test "${enable_tune_old}" = "yes" ; then
        echo "Build with old tuning code:                         yes"
fi

echo ""
echo "Debugging"
echo ""

if test "${enable_coverage}" = "yes" ; then
        echo "Build with code coverage debug symbols              yes"
else
        echo "Build with code coverage debug symbols               no"
fi

if test "${enable_duma}" = "yes" ; then
        echo "Build with debugging DUMA library                    yes"
else
        echo "Build with debugging DUMA library                    no"
fi
