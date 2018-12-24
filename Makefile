COMMON_OBJECTS=resource_fork.o audio_codecs.o mc68k.o util.o
REALMZ_DASM_OBJECTS=realmz_dasm.o realmz_lib.o $(COMMON_OBJECTS)
DC_DASM_OBJECTS=dc_dasm.o dc_decode_sprite.o $(COMMON_OBJECTS)
RESOURCE_DUMP_OBJECTS=resource_dump.o $(COMMON_OBJECTS)
RENDER_INFOTRON_LEVELS_OBJECTS=render_infotron_levels.o $(COMMON_OBJECTS)
CXXFLAGS=-I/opt/local/include -g -Wall -std=c++14
LDFLAGS=-L/opt/local/lib -lphosg
EXECUTABLES=realmz_dasm resource_dump render_infotron_levels

all: realmz_dasm dc_dasm resource_dump render_infotron_levels

realmz_dasm: $(REALMZ_DASM_OBJECTS)
	g++ $(LDFLAGS) -o realmz_dasm $^

dc_dasm: $(DC_DASM_OBJECTS)
	g++ $(LDFLAGS) -o dc_dasm $^

resource_dump: $(RESOURCE_DUMP_OBJECTS)
	g++ $(LDFLAGS) -o resource_dump $^

render_infotron_levels: $(RENDER_INFOTRON_LEVELS_OBJECTS)
	g++ $(LDFLAGS) -o render_infotron_levels $^

clean:
	-rm -f *.o $(EXECUTABLES)

.PHONY: clean
