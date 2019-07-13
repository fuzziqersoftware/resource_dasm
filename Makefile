COMMON_OBJECTS=resource_fork.o audio_codecs.o mc68k.o util.o
DC_DASM_OBJECTS=dc_dasm.o dc_decode_sprite.o $(COMMON_OBJECTS)
MACSKI_DECOMPRESS_OBJECTS=macski_decompress.o
MOHAWK_DASM_OBJECTS=mohawk_dasm.o $(COMMON_OBJECTS)
REALMZ_DASM_OBJECTS=realmz_dasm.o realmz_lib.o $(COMMON_OBJECTS)
RENDER_BITS_OBJECTS=render_bits.o
RENDER_INFOTRON_LEVELS_OBJECTS=render_infotron_levels.o $(COMMON_OBJECTS)
RESOURCE_DASM_OBJECTS=resource_dasm.o $(COMMON_OBJECTS)
SC2K_DECODE_SPRITE_OBJECTS=sc2k_decode_sprite.o $(COMMON_OBJECTS)

CXXFLAGS=-I/opt/local/include -g -Wall -std=c++14
LDFLAGS=-L/opt/local/lib -lphosg
EXECUTABLES=render_bits macski_decompress mohawk_dasm realmz_dasm dc_dasm resource_dasm render_infotron_levels sc2k_decode_sprite

all: $(EXECUTABLES)

render_bits: $(RENDER_BITS_OBJECTS)
	g++ $(LDFLAGS) -o render_bits $^

realmz_dasm: $(REALMZ_DASM_OBJECTS)
	g++ $(LDFLAGS) -o realmz_dasm $^

mohawk_dasm: $(MOHAWK_DASM_OBJECTS)
	g++ $(LDFLAGS) -o mohawk_dasm $^

dc_dasm: $(DC_DASM_OBJECTS)
	g++ $(LDFLAGS) -o dc_dasm $^

sc2k_decode_sprite: $(SC2K_DECODE_SPRITE_OBJECTS)
	g++ $(LDFLAGS) -o sc2k_decode_sprite $^

macski_decompress: $(MACSKI_DECOMPRESS_OBJECTS)
	g++ $(LDFLAGS) -o macski_decompress $^

resource_dasm: $(RESOURCE_DASM_OBJECTS)
	g++ $(LDFLAGS) -o resource_dasm $^

render_infotron_levels: $(RENDER_INFOTRON_LEVELS_OBJECTS)
	g++ $(LDFLAGS) -o render_infotron_levels $^

clean:
	-rm -f *.o $(EXECUTABLES)

.PHONY: clean
