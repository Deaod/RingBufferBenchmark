# Ring Buffer Benchmark

A set of benchmarks for a few different ring buffer implementations. 

## Set-Up

This project uses C++17 features, which is only supported with CMake 3.8 or later.

### Visual Studio 2017

Execute the following inside the cloned repository:

```
cd benchmark
git submodule init
git submodule update
cd ..
mkdir build
cd build
cmake -G "Visual Studio 15 2017 Win64" ..
RingBufferBenchmark.sln
cd ..
```

### Others

TBD