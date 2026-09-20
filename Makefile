CXX ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra

SRC := nav/geo.cpp nav/deadreckon.cpp nav/mission.cpp nav/avoid.cpp \
       sim/fake_sensors.cpp sim/main_native.cpp

all: build/sim build/node

build/sim: $(SRC)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(SRC) -o build/sim

run: build/sim
	./build/sim

clean:
	rm -rf build

.PHONY: all run clean

NODE_SRC := node/classify.cpp node/frame.cpp sim/node_native.cpp

build/node: $(NODE_SRC)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(NODE_SRC) -o build/node

node: build/node

.PHONY: node

GAP_SRC := nav/gap.cpp sim/gap_test.cpp

build/gaptest: $(GAP_SRC)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(GAP_SRC) -o build/gaptest

gaptest: build/gaptest
	./build/gaptest

.PHONY: gaptest
