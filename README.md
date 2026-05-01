# bsort

`bsort` is a C++23 implementation of the original fixed-width-record radix
sorter. It builds both:

- a reusable C++ library exported as `bsort::bsort`
- a command-line executable named `bsort`

The historical README for the original automake/C implementation is preserved
in [LEGACY_README.md](LEGACY_README.md).

## Requirements

- CMake 3.22 or newer
- A C++23 compiler
- POSIX-like system APIs for the executable's file mapping path

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Useful CMake options:

```sh
-DBSORT_BUILD_EXECUTABLE=OFF   # build only the library
-DBUILD_SHARED_LIBS=ON         # build libbsort as a shared library
-DBSORT_NATIVE=ON              # add -march=native on GCC/Clang-like compilers
-DBSORT_LTO=ON                 # enable CMake IPO/LTO when supported
-DBSORT_PGO=generate|use       # GCC profile generation/profile use builds
```

## Install

```sh
cmake --install build --prefix /opt/bsort
```

The install tree contains:

```text
bin/bsort
include/bsort/bsort.hpp
lib/libbsort.a
lib/cmake/bsort/bsortConfig.cmake
```

If `BUILD_SHARED_LIBS=ON`, the library artifact is installed as the platform
shared-library equivalent instead.

## Command-Line Use

Sort a file in place:

```sh
bsort -r 100 -k 8 records.bin
```

Read one file and write sorted output to another:

```sh
bsort -r 100 -k 8 -i input.bin -o output.bin
```

For printable ASCII keys, use `-a`:

```sh
bsort -a -i input.bin -o output.bin
```

Important options:

```text
-r N       record size in bytes; default 100
-k N       key size in bytes; default 8
-a         keys are printable ASCII bytes 32..126 inclusive
-s N       push-ahead stack size; default 16, or 8 with -a
-c N       cleanup-sort cutoff; default 500
-t N       std::sort cleanup threshold; default 96, or 256 with -a
-A MODE    output allocation: truncate or fallocate
-I ADVICE  input madvise: normal, random, sequential, willneed
-O ADVICE  output madvise: normal, random, sequential, willneed
```

## Library Use

From CMake:

```cmake
find_package(bsort CONFIG REQUIRED)

add_executable(my_sorter main.cpp)
target_link_libraries(my_sorter PRIVATE bsort::bsort)
```

Example:

```cpp
#include <bsort/bsort.hpp>

#include <cstddef>
#include <vector>

int main() {
  std::vector<std::byte> input = load_records();
  std::vector<std::byte> output(input.size());

  bsort::SortConfig config {
      .ascii = true,
      .record_size = 100,
      .key_size = 8,
  };

  bsort::sort_records(output.data(), input.data(), input.size(), config);
}
```

The public API is:

```cpp
namespace bsort {

struct SortConfig {
  bool ascii = false;
  std::size_t record_size = 100;
  std::size_t key_size = 8;
  std::size_t stack_size = 0;
  std::size_t cut_off = 0;
  std::optional<std::size_t> std_sort_cleanup_min;
};

void sort_records(void* output,
                  const void* input,
                  std::size_t byte_size,
                  SortConfig config = {});

void sort_records(char* output,
                  const char* input,
                  std::size_t byte_size,
                  SortConfig config = {});

}  // namespace bsort
```

`byte_size` must be a multiple of `record_size`, and `key_size` must not exceed
`record_size`.

If `output == input`, bsort sorts in place and still performs the initial
histogram/validation pass. If `output` and `input` are distinct overlapping
memory ranges, the library rejects the call with `std::invalid_argument`.

Leaving `stack_size` or `cut_off` as zero uses the library defaults. Leaving
`std_sort_cleanup_min` unset uses the default threshold.
