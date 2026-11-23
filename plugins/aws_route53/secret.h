//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
// Key material: a value that cannot be printed or turned into a string by
// accident, held in a page of its own that is locked out of swap, left out of
// core dumps, and cleansed before it is returned. reveal() is the one way to
// the bytes, for the code that signs with them or sends them.
#pragma once
#include <cstddef>
#include <iosfwd>
#include <string>
#include <string_view>

namespace manannan::aws {

class Secret {
 public:
  Secret() = default;
  // Copies the value into locked memory and cleanses the string it came from.
  explicit Secret(std::string&& value);
  Secret(const Secret& other);
  Secret& operator=(const Secret& other);
  Secret(Secret&& other) noexcept;
  Secret& operator=(Secret&& other) noexcept;
  ~Secret();

  bool empty() const { return _size == 0; }
  std::string_view reveal() const { return {_data, _size}; }

  friend std::ostream& operator<<(std::ostream&, const Secret&) = delete;

 private:
  void assign(std::string_view value);
  void release() noexcept;

  char* _data{};
  std::size_t _size{};
  std::size_t _mapped{};
};

}  // namespace manannan::aws
