#pragma once

#include <cstddef>
#include <optional>

namespace bsort {

struct SortConfig {
  bool ascii = false;
  std::size_t record_size = 100;
  std::size_t key_size = 8;
  std::size_t stack_size = 0;
  std::size_t cut_off = 0;
  std::optional<std::size_t> std_sort_cleanup_min;
  bool validate_keys = true;
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
