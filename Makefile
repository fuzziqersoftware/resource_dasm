CXX=g++-mp-4.8
COMMON_OBJECTS=resource_fork.o Image.o util.o
REALMZ_DASM_OBJECTS=realmz_dasm.o realmz_lib.o $(COMMON_OBJECTS)
RESOURCE_DUMP_OBJECTS=resource_dump.o $(COMMON_OBJECTS)
CXXFLAGS=-g -Wall -std=c++11
EXECUTABLES=realmz_dasm

all: realmz_dasm resource_dump

realmz_dasm: $(REALMZ_DASM_OBJECTS)
	$(CXX) $(LDFLAGS) -o realmz_dasm $^

resource_dump: $(RESOURCE_DUMP_OBJECTS)
	$(CXX) $(LDFLAGS) -o resource_dump $^

clean:
	-rm -f *.o $(EXECUTABLES)

.PHONY: clean
