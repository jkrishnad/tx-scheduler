#pragma once
#include <cstddef>

class SharedRegion {
  public:
    explicit SharedRegion(int fd);
    // destructor.
    ~SharedRegion();
    // Copy constructor
    SharedRegion(const SharedRegion &) = delete;
    // Copy assignment
    SharedRegion &operator=(const SharedRegion &) = delete;
    // move constructor
    SharedRegion(SharedRegion &&other) noexcept;

    std::byte *data() const { return data_; };
    std::size_t size() const { return size_; };

  private:
    int fd_{-1};

    std::byte *data_{nullptr};

    std::size_t size_{0};
};
