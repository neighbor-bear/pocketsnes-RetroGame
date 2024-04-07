#
# PocketSNES for the RetroFW
#
# by pingflood; 2019
#

# Define the applications properties here:

TARGET = pocketsnes/pocketsnes.dge

# CHAINPREFIX   := /opt/mipsel-RetroFW-linux-uclibc
# CROSS_COMPILE := $(CHAINPREFIX)/usr/bin/mipsel-linux-

CC  := $(CROSS_COMPILE)gcc
CXX := $(CROSS_COMPILE)g++
STRIP := $(CROSS_COMPILE)strip

SYSROOT := $(shell $(CC) --print-sysroot)
SDL_CFLAGS := $(shell $(SYSROOT)/usr/bin/sdl-config --cflags)
SDL_LIBS := $(shell $(SYSROOT)/usr/bin/sdl-config --libs)

INCLUDE = -I src \
		-I sal/linux/include -I sal/include \
		-I src/include \
		-I menu -I src/linux -I src/snes9x

CFLAGS =  -std=gnu++11 $(INCLUDE) -DRC_OPTIMIZED -DGCW_ZERO -D__LINUX__ -D__DINGUX__ -DFOREVER_16_BIT $(SDL_CFLAGS)
CFLAGS += -O3 -fdata-sections -ffunction-sections -fomit-frame-pointer -fno-builtin -fpermissive
CFLAGS += -mips32 -march=mips32 -mno-mips16 -DMIPS_XBURST 
CFLAGS += -fno-common -Wno-write-strings -Wno-sign-compare -ffast-math -ftree-vectorize
CFLAGS += -funswitch-loops -fno-strict-aliasing
CFLAGS += -DFAST_LSB_WORD_ACCESS
CFLAGS += -flto=4 -fwhole-program -fuse-linker-plugin -fmerge-all-constants
CFLAGS += -fdata-sections -ffunction-sections -fpermissive
# CFLAGS += -fprofile-generate -fprofile-dir=/home/retrofw/profile/pocketsnes
CFLAGS += -fprofile-use -fprofile-dir=./profile

CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti -fno-math-errno -fno-threadsafe-statics

LDFLAGS = $(CXXFLAGS) -lpthread -lz -lpng $(SDL_LIBS) -lSDL_image -Wl,--as-needed -Wl,--gc-sections -s

# Find all source files
SOURCE = src/snes9x menu sal/linux sal
SRC_CPP = $(foreach dir, $(SOURCE), $(wildcard $(dir)/*.cpp))
SRC_C   = $(foreach dir, $(SOURCE), $(wildcard $(dir)/*.c))
OBJ_CPP = $(patsubst %.cpp, %.o, $(SRC_CPP))
OBJ_C   = $(patsubst %.c, %.o, $(SRC_C))
OBJS    = $(OBJ_CPP) $(OBJ_C)

.PHONY : all
all : $(TARGET)

$(TARGET) : $(OBJS)
	$(CXX) $(CXXFLAGS) $^ $(LDFLAGS) -o $@
	$(STRIP) $(TARGET)


%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.cpp
	$(CXX) $(CFLAGS) -c $< -o $@

.PHONY : clean

opk: all
	@mksquashfs \
	pocketsnes/default.retrofw.desktop \
	pocketsnes/snes.retrofw.desktop \
	pocketsnes/pocketsnes.dge \
	pocketsnes/pocketsnes.man.txt \
	pocketsnes/pocketsnes.png \
	pocketsnes/backdrop.png \
	pocketsnes/pocketsnes.opk \
	-all-root -noappend -no-exports -no-xattrs


