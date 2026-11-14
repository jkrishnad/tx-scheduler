#include "shm.hpp"
#include <cerrno>
#include <cstddef>
#include <sys/mman.h>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>

SharedRegion::SharedRegion(int fd) : fd_(fd) {
    // Ask the kernel how big the block is.
    struct stat st{};
    int fs = fstat(fd, &st);

    if (fs < 0) {
        int err = errno;
        close(fd);
        throw std::system_error(err, std::generic_category(), "fstat error");
    }
    // Map the whole block, read-write, shared with the other process.
    void *p =
        mmap(nullptr, st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    // The destructor won't run if we throw, so close the fd here.
    if (p == MAP_FAILED) {
        int err = errno;
        close(fd);
        throw std::system_error(err, std::generic_category(),
                                "mmap pointer error");
    }
    // Keep the pointer and size: data()/size() hand them out, the destructor
    // unmaps them.
    data_ = static_cast<std::byte *>(p);
    size_ = st.st_size;
}

SharedRegion::~SharedRegion() {
    if (data_) {
        munmap(data_, size_);
    }
    if (fd_ >= 0) {
        close(fd_);
    }
}

SharedRegion::SharedRegion(SharedRegion &&other) noexcept
    : fd_(other.fd_), data_(other.data_), size_(other.size_) {
    other.fd_ = -1;
    other.data_ = nullptr;
    other.size_ = 0;
}
