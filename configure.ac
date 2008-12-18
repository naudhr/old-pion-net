# ------------------------------------------------
# Pion Network Library autoconf configuration file
# ------------------------------------------------

# Set Pion version information
AC_INIT([pion-net], [1.0.8], [support@atomiclabs.com])

# Initialize some other things
AC_PREREQ([2.59])
AC_CONFIG_AUX_DIR(net/build)
AC_CONFIG_SRCDIR(net/src/TCPServer.cpp)

# package and version args are now taken from AC_INIT()
AM_INIT_AUTOMAKE

# Setup libtool
AC_LIBTOOL_WIN32_DLL
AC_LIBTOOL_DLOPEN
AC_PROG_LIBTOOL

# Setup doxygen support
DX_DOXYGEN_FEATURE(ON)
DX_HTML_FEATURE(ON)
DX_INIT_DOXYGEN(pion-net, net/doc/Doxyfile, net/doc)

# Define CXXFLAGS before AC_PROG_CXX to suppress the default autoconf
# compiler options
CFLAGS="-DPION_NETWORK_LIBRARY"
CXXFLAGS="-DPION_NETWORK_LIBRARY"

# Currently only the embedded common library can be used
PION_COMMON_HOME=`pwd`/common
PION_COMMON_LIB="$PION_COMMON_HOME/src/libpion-common.la"
PION_COMMON_MAKEDIRS=common
AC_SUBST(PION_COMMON_HOME)
AC_SUBST(PION_COMMON_LIB)
AC_SUBST(PION_COMMON_MAKEDIRS)
AC_MSG_NOTICE([Using the embedded pion-common library])

# Include pion-config.inc
m4_include([common/build/pion-setup.inc])
m4_include([common/build/pion-boost.inc])
m4_include([common/build/pion-config.inc])

# Output Makefiles
AC_OUTPUT(pion-net.pc Makefile net/Makefile
	net/include/Makefile net/include/pion/Makefile net/include/pion/net/Makefile
	common/Makefile common/include/Makefile common/include/pion/Makefile
	common/src/Makefile common/tests/Makefile common/tests/PluginsUsedByUnitTests/Makefile
	net/src/Makefile net/services/Makefile net/utils/Makefile net/tests/Makefile)
