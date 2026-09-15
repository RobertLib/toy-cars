.PHONY: all build run run-mobile test

all: build

build:
	cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
	+cmake --build build --parallel

run: build
	./build/ToyCars $(ARGS)

run-mobile: build
	./build/ToyCars --touch --size 874x402 $(ARGS)

test: build
	ctest --test-dir build --output-on-failure
