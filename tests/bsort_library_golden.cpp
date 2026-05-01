#include "bsort/bsort.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

[[nodiscard]] std::vector<char> read_file(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("failed to open " + path.string());
  }

  file.seekg(0, std::ios::end);
  const std::streamoff size = file.tellg();
  if (size < 0) {
    throw std::runtime_error("failed to size " + path.string());
  }
  file.seekg(0, std::ios::beg);

  std::vector<char> bytes(static_cast<std::size_t>(size));
  if (!bytes.empty()) {
    file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!file) {
      throw std::runtime_error("failed to read " + path.string());
    }
  }
  return bytes;
}

void expect_equal(const std::vector<char>& actual,
                  const std::vector<char>& expected,
                  const std::string& mode) {
  if (actual == expected) {
    return;
  }

  const auto [actual_it, expected_it] =
      std::mismatch(actual.begin(), actual.end(), expected.begin(), expected.end());
  const std::size_t offset =
      static_cast<std::size_t>(actual_it - actual.begin());
  throw std::runtime_error(mode + " output differs from golden data at byte " +
                           std::to_string(offset));
}

}  // namespace

int main(int argc, char* argv[]) {
  try {
    if (argc != 3) {
      throw std::invalid_argument("usage: bsort_library_golden input output");
    }

    const std::vector<char> input = read_file(argv[1]);
    const std::vector<char> expected = read_file(argv[2]);
    if (input.size() != expected.size()) {
      throw std::runtime_error("golden input and output sizes differ");
    }

    const bsort::SortConfig config {
        .ascii = true,
        .record_size = 100,
        .key_size = 8,
    };
    const bsort::SortConfig no_validation_config {
        .ascii = true,
        .record_size = 100,
        .key_size = 8,
        .validate_keys = false,
    };

    std::vector<char> copied(input.size());
    bsort::sort_records(copied.data(), input.data(), input.size(), config);
    expect_equal(copied, expected, "copy-mode");

    std::vector<char> in_place = input;
    bsort::sort_records(in_place.data(),
                        in_place.data(),
                        in_place.size(),
                        no_validation_config);
    expect_equal(in_place, expected, "in-place");

    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
