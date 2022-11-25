PKGS=gl sdl2
ENABLE=-DBUILD_LINUX -DGFX_GL2
PLATFORM_CFLAGS=$(shell pkg-config --cflags $(PKGS)) $(ENABLE)
PLATFORM_LINK=$(shell pkg-config --libs $(PKGS)) -pthread
include Makefile.common
