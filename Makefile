COMMON_OBJECTS=resource_fork.o audio_codecs.o pict.o quickdraw_formats.o mc68k.o mc68k_dasm.o

ifeq ($(shell uname -s),Darwin)
	PHOSG_LIB_DIR=/opt/local
else
	PHOSG_LIB_DIR=/usr/local
endif

CXXFLAGS=-I$(PHOSG_LIB_DIR)/include -g -Wall -std=c++14
LDFLAGS=-L$(PHOSG_LIB_DIR)/lib
LDLIBS=-lphosg -lpthread
EXECUTABLES=render_bits bt_render macski_decomp mohawk_dasm realmz_dasm dc_dasm resource_dasm infotron_render ferazel_render harry_render mshines_render sc2k_render

all: $(EXECUTABLES)


resource_dasm: resource_dasm.o $(COMMON_OBJECTS)
	g++ $(LDFLAGS) -o resource_dasm $^ $(LDLIBS)


bt_render: bt_render.o ambrosia_sprites.o $(COMMON_OBJECTS)
	g++ $(LDFLAGS) -o bt_render $^ $(LDLIBS)

dc_dasm: dc_dasm.o $(COMMON_OBJECTS)
	g++ $(LDFLAGS) -o dc_dasm $^ $(LDLIBS)

ferazel_render: ferazel_render.o $(COMMON_OBJECTS)
	g++ $(LDFLAGS) -o ferazel_render $^ $(LDLIBS)

harry_render: harry_render.o ambrosia_sprites.o $(COMMON_OBJECTS)
	g++ $(LDFLAGS) -o harry_render $^ $(LDLIBS)

infotron_render: infotron_render.o $(COMMON_OBJECTS)
	g++ $(LDFLAGS) -o infotron_render $^ $(LDLIBS)

macski_decomp: macski_decomp.o
	g++ $(LDFLAGS) -o macski_decomp $^ $(LDLIBS)

mohawk_dasm: mohawk_dasm.o $(COMMON_OBJECTS)
	g++ $(LDFLAGS) -o mohawk_dasm $^ $(LDLIBS)

mshines_render: mshines_render.o $(COMMON_OBJECTS)
	g++ $(LDFLAGS) -o mshines_render $^ $(LDLIBS)

realmz_dasm: realmz_dasm.o realmz_lib.o $(COMMON_OBJECTS)
	g++ $(LDFLAGS) -o realmz_dasm $^ $(LDLIBS)

render_bits: render_bits.o
	g++ $(LDFLAGS) -o render_bits $^ $(LDLIBS)

sc2k_render: sc2k_render.o $(COMMON_OBJECTS)
	g++ $(LDFLAGS) -o sc2k_render $^ $(LDLIBS)


clean:
	-rm -f *.o $(EXECUTABLES)

.PHONY: clean
