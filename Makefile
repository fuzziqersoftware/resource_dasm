COMMON_OBJECTS=ResourceFile.o AudioCodecs.o PICTRenderer.o QuickDrawFormats.o MemoryContext.o M68KEmulator.o PEFFFile.o PPC32Emulator.o TrapInfo.o

ifeq ($(shell uname -s),Darwin)
	INSTALL_DIR=/opt/local
	CXXFLAGS +=  -DMACOSX -mmacosx-version-min=10.11
	LDFLAGS +=  -mmacosx-version-min=10.11
else
	INSTALL_DIR=/usr/local
	CXXFLAGS +=  -DLINUX
	LDFLAGS +=  -pthread
endif

CXXFLAGS=-I$(INSTALL_DIR)/include -g -Wall -std=c++14
LDFLAGS=-L$(INSTALL_DIR)/lib
LDLIBS=-lphosg -lpthread
EXECUTABLES=render_bits bt_render macski_decomp mohawk_dasm realmz_dasm dc_dasm resource_dasm infotron_render ferazel_render harry_render mshines_render sc2k_render

all: $(EXECUTABLES) libresource_dasm.a

install-lib: libresource_dasm.a
	mkdir -p $(INSTALL_DIR)/include/resource_dasm
	cp libresource_dasm.a $(INSTALL_DIR)/lib/
	cp -r *.hh $(INSTALL_DIR)/include/resource_dasm/


resource_dasm: resource_dasm.o $(COMMON_OBJECTS)
	g++ $(LDFLAGS) -o resource_dasm $^ $(LDLIBS)

libresource_dasm.a: $(COMMON_OBJECTS)
	rm -f libresource_dasm.a
	ar rcs libresource_dasm.a $(COMMON_OBJECTS)


bt_render: bt_render.o AmbrosiaSprites.o $(COMMON_OBJECTS)
	g++ $(LDFLAGS) -o bt_render $^ $(LDLIBS)

dc_dasm: dc_dasm.o $(COMMON_OBJECTS)
	g++ $(LDFLAGS) -o dc_dasm $^ $(LDLIBS)

ferazel_render: ferazel_render.o $(COMMON_OBJECTS)
	g++ $(LDFLAGS) -o ferazel_render $^ $(LDLIBS)

harry_render: harry_render.o AmbrosiaSprites.o $(COMMON_OBJECTS)
	g++ $(LDFLAGS) -o harry_render $^ $(LDLIBS)

infotron_render: infotron_render.o $(COMMON_OBJECTS)
	g++ $(LDFLAGS) -o infotron_render $^ $(LDLIBS)

macski_decomp: macski_decomp.o
	g++ $(LDFLAGS) -o macski_decomp $^ $(LDLIBS)

mohawk_dasm: mohawk_dasm.o $(COMMON_OBJECTS)
	g++ $(LDFLAGS) -o mohawk_dasm $^ $(LDLIBS)

mshines_render: mshines_render.o $(COMMON_OBJECTS)
	g++ $(LDFLAGS) -o mshines_render $^ $(LDLIBS)

realmz_dasm: realmz_dasm.o RealmzLib.o $(COMMON_OBJECTS)
	g++ $(LDFLAGS) -o realmz_dasm $^ $(LDLIBS)

render_bits: render_bits.o
	g++ $(LDFLAGS) -o render_bits $^ $(LDLIBS)

sc2k_render: sc2k_render.o $(COMMON_OBJECTS)
	g++ $(LDFLAGS) -o sc2k_render $^ $(LDLIBS)


clean:
	-rm -f *.o $(EXECUTABLES) libresource_dasm.a

.PHONY: clean
