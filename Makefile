.PHONY: all build run test

all: build

build:
	cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
	+cmake --build build --parallel

run: build
	./build/ToyCars $(ARGS)

test: build
	ctest --test-dir build --output-on-failure
