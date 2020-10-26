COMMON_OBJECTS=resource_fork.o audio_codecs.o pict.o quickdraw_formats.o mc68k.o mc68k_dasm.o

CXXFLAGS=-I/opt/local/include -g -Wall -std=c++14
LDFLAGS=-L/opt/local/lib -lphosg
EXECUTABLES=render_bits bt_render macski_decomp mohawk_dasm realmz_dasm dc_dasm resource_dasm infotron_render ferazel_render harry_render mshines_render sc2k_render

all: $(EXECUTABLES)


resource_dasm: resource_dasm.o $(COMMON_OBJECTS)
	g++ $(LDFLAGS) -o resource_dasm $^


bt_render: bt_render.o ambrosia_sprites.o $(COMMON_OBJECTS)
	g++ $(LDFLAGS) -o bt_render $^

dc_dasm: dc_dasm.o $(COMMON_OBJECTS)
	g++ $(LDFLAGS) -o dc_dasm $^

ferazel_render: ferazel_render.o $(COMMON_OBJECTS)
	g++ $(LDFLAGS) -o ferazel_render $^

harry_render: harry_render.o ambrosia_sprites.o $(COMMON_OBJECTS)
	g++ $(LDFLAGS) -o harry_render $^

infotron_render: infotron_render.o $(COMMON_OBJECTS)
	g++ $(LDFLAGS) -o infotron_render $^

macski_decomp: macski_decomp.o
	g++ $(LDFLAGS) -o macski_decomp $^

mohawk_dasm: mohawk_dasm.o $(COMMON_OBJECTS)
	g++ $(LDFLAGS) -o mohawk_dasm $^

mshines_render: mshines_render.o $(COMMON_OBJECTS)
	g++ $(LDFLAGS) -o mshines_render $^

realmz_dasm: realmz_dasm.o realmz_lib.o $(COMMON_OBJECTS)
	g++ $(LDFLAGS) -o realmz_dasm $^

render_bits: render_bits.o
	g++ $(LDFLAGS) -o render_bits $^

sc2k_render: sc2k_render.o $(COMMON_OBJECTS)
	g++ $(LDFLAGS) -o sc2k_render $^


clean:
	-rm -f *.o $(EXECUTABLES)

.PHONY: clean
