# Cross-compile the Windows PE bridge for use under DW-Proton / Wine.
MINGW_CXX ?= x86_64-w64-mingw32-g++
CXXFLAGS ?= -O2 -std=c++17 -municode -static -static-libgcc -static-libstdc++ -s
LIBS = -lshell32 -luser32 -lkernel32

.PHONY: all clean

all: endfield_bridge.exe

endfield_bridge.exe: endfield_bridge.cpp
	$(MINGW_CXX) $(CXXFLAGS) -o $@ $< $(LIBS)

clean:
	rm -f endfield_bridge.exe
