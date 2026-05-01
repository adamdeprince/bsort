#include "bsort/bsort.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>

namespace {

constexpr bsort::SortConfig test_config {
    .ascii = false,
    .record_size = 4,
    .key_size = 2,
};

void expect_equal(const std::array<char, 16>& actual,
                  const std::array<char, 16>& expected) {
  if (!std::equal(actual.begin(), actual.end(), expected.begin())) {
    throw std::runtime_error("unexpected library sort output");
  }
}

}  // namespace

int main() {
  constexpr std::array<char, 16> unsorted {
      'b', '2', 'A', 'A',
      'a', '2', 'B', 'B',
      'a', '1', 'C', 'C',
      'b', '1', 'D', 'D',
  };
  constexpr std::array<char, 16> sorted {
      'a', '1', 'C', 'C',
      'a', '2', 'B', 'B',
      'b', '1', 'D', 'D',
      'b', '2', 'A', 'A',
  };

  std::array<char, 16> copied {};
  bsort::sort_records(copied.data(),
                      unsorted.data(),
                      copied.size(),
                      test_config);
  expect_equal(copied, sorted);

  std::array<char, 16> in_place = unsorted;
  bsort::sort_records(in_place.data(),
                      in_place.data(),
                      in_place.size(),
                      test_config);
  expect_equal(in_place, sorted);

  std::array<char, 24> overlapping {};
  std::memcpy(overlapping.data(), unsorted.data(), unsorted.size());
  bool rejected_overlap = false;
  try {
    bsort::sort_records(overlapping.data() + 4,
                        overlapping.data(),
                        unsorted.size(),
                        test_config);
  } catch (const std::invalid_argument&) {
    rejected_overlap = true;
  }
  if (!rejected_overlap) {
    throw std::runtime_error("overlapping ranges were not rejected");
  }

  bool rejected_bad_ascii = false;
  std::array<char, 4> bad_ascii {'A', '\x7f', 'Z', 'Z'};
  try {
    bsort::sort_records(bad_ascii.data(),
                        bad_ascii.data(),
                        bad_ascii.size(),
                        bsort::SortConfig {
                            .ascii = true,
                            .record_size = 4,
                            .key_size = 2,
                        });
  } catch (const std::runtime_error&) {
    rejected_bad_ascii = true;
  }
  if (!rejected_bad_ascii) {
    throw std::runtime_error("bad ASCII key byte was not rejected");
  }

  return 0;
}
