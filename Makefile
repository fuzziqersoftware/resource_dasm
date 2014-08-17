CXX=g++-mp-4.8
OBJECTS=realmz_dasm.o realmz_lib.o Image.o
CXXFLAGS=-g -Wall -std=c++11
EXECUTABLES=realmz_dasm

all: realmz_dasm

realmz_dasm: $(OBJECTS)
	$(CXX) $(LDFLAGS) -o realmz_dasm $^

clean:
	-rm -f *.o $(EXECUTABLES)

.PHONY: clean
