//
// Manannan - plugin-based dynamic DNS updater
//
// Copyright (C) 2026 Tom Cully <mail@tomcully.com>
// Licensed under the GNU GPLv3 - see <https://www.gnu.org/licenses/gpl-3.0.html>
//
#include "secret.h"

#include <openssl/crypto.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <system_error>
#include <utility>

namespace manannan::aws {

Secret::Secret(std::string&& value) {
  assign(value);
  OPENSSL_cleanse(value.data(), value.size());
  value.clear();
}
Secret::Secret(const Secret& other) { assign(other.reveal()); }
Secret& Secret::operator=(const Secret& other) {
  if (this != &other) {
    release();
    assign(other.reveal());
  }
  return *this;
}
Secret::Secret(Secret&& other) noexcept
    : _data(std::exchange(other._data, nullptr)), _size(std::exchange(other._size, 0)), _mapped(std::exchange(other._mapped, 0)) {}
Secret& Secret::operator=(Secret&& other) noexcept {
  if (this != &other) {
    release();
    _data = std::exchange(other._data, nullptr);
    _size = std::exchange(other._size, 0);
    _mapped = std::exchange(other._mapped, 0);
  }
  return *this;
}
Secret::~Secret() { release(); }

void Secret::assign(std::string_view value) {
  if (value.empty()) return;
  const auto page = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
  const auto mapped = (value.size() + page - 1) / page * page;
  void* memory = mmap(nullptr, mapped, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (memory == MAP_FAILED) throw std::system_error(errno, std::generic_category(), "cannot map memory for a secret");  // LCOV_EXCL_LINE
  // Locking fails only over RLIMIT_MEMLOCK, which the tests, running as root
  // in CI and on the build machines, cannot reach.
  if (mlock(memory, mapped) != 0) {
    const auto error = errno;                                                                    // LCOV_EXCL_LINE
    munmap(memory, mapped);                                                                      // LCOV_EXCL_LINE
    throw std::system_error(error, std::generic_category(), "cannot lock memory for a secret");  // LCOV_EXCL_LINE
  }
#ifdef MADV_DONTDUMP
  madvise(memory, mapped, MADV_DONTDUMP);
#endif
  std::memcpy(memory, value.data(), value.size());
  _data = static_cast<char*>(memory);
  _size = value.size();
  _mapped = mapped;
}

void Secret::release() noexcept {
  if (!_data) return;
  OPENSSL_cleanse(_data, _mapped);
  munlock(_data, _mapped);
  munmap(_data, _mapped);
  _data = nullptr;
  _size = 0;
  _mapped = 0;
}

}  // namespace manannan::aws
