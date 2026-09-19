CXX ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra

SRC := nav/geo.cpp nav/deadreckon.cpp nav/mission.cpp nav/avoid.cpp \
       sim/fake_sensors.cpp sim/main_native.cpp

all: build/sim

build/sim: $(SRC)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(SRC) -o build/sim

run: build/sim
	./build/sim

clean:
	rm -rf build

.PHONY: all run clean
