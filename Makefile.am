# ------------------------------------
# pion-net automake configuration file 
# ------------------------------------

ACLOCAL_AMFLAGS = -I common/build

SUBDIRS = @PION_COMMON_MAKEDIRS@ net

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = pion-net.pc

EXTRA_DIST = autogen.sh *.sln *.xcodeproj

include common/build/doxygen.inc
include net/build/net.inc

all-local: docs

clean-local: clean-docs
