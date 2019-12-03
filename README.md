# Ring Buffer Benchmark

A set of benchmarks for a few different ring buffer implementations.

## Set-Up

This project uses C++17 features, which is only supported with CMake 3.8 or later.
It also uses the following dependencies:
 * [folly](https://github.com/facebook/folly/)
 * [boost-lockfree](https://github.com/boostorg/lockfree)

Both can be grabbed using [vcpkg](https://github.com/microsoft/vcpkg).

### Visual Studio 2019

Execute the following inside the cloned repository:

```
cd benchmark
git submodule init
git submodule update
cd ..
mkdir build
cd build
cmake -G "Visual Studio 16" -A "x64" -DCMAKE_TOOLCHAIN_FILE=[vcpkg root]\scripts\buildsystems\vcpkg.cmake ..
RingBufferBenchmark.sln
cd ..
```

### Linux with Clang

```
git clone https://github.com/Deaod/RingBufferBenchmark
cd RingBufferBenchmark
git submodule update --init
mkdir build
cd build

# Build
cmake -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)

# Run
./RingBufferBenchmark
```

Note: Clang will output about 600 warnings that some values are uninitialized

### Linux with GCC

The codebase currently does not compile with GCC 8 and GCC 9
