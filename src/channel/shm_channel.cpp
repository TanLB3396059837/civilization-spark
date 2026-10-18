#include "civ/channel/shm_channel.hpp"
#include <stdexcept>
#include <cstring>
#include <thread>
#include <new>
#include <limits>

#ifdef _WIN32

namespace civ {

ShmChannel::ShmChannel(const std::string& name, bool create, std::size_t size)
    : name_(name), size_(size), creator_(create) {
    if (name_.empty())
        throw std::invalid_argument("shm name must not be empty");
    if (size_ <= sizeof(Header))
        throw std::invalid_argument("shm size must exceed header size");
    if (size_ > std::numeric_limits<DWORD>::max())
        throw std::invalid_argument("shm size exceeds Windows mapping limit");
    if (create) {
        hMap_ = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr,
                                   PAGE_READWRITE, 0,
                                   static_cast<DWORD>(size_), name_.c_str());
        if (!hMap_) throw std::runtime_error("CreateFileMapping failed");
        ptr_ = MapViewOfFile(hMap_, FILE_MAP_ALL_ACCESS, 0, 0, size_);
        if (!ptr_) throw std::runtime_error("MapViewOfFile failed");
        hdr_ = new (ptr_) Header();
    } else {
        hMap_ = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name_.c_str());
        if (!hMap_) throw std::runtime_error("OpenFileMapping failed");
        ptr_ = MapViewOfFile(hMap_, FILE_MAP_ALL_ACCESS, 0, 0, size_);
        if (!ptr_) throw std::runtime_error("MapViewOfFile failed");
        hdr_ = static_cast<Header*>(ptr_);
    }
    data_ = static_cast<Byte*>(ptr_) + sizeof(Header);
}

ShmChannel::~ShmChannel() { close(); }

void ShmChannel::send(ByteView d) {
    if (d.size() > size_ || d.size() > size_ - sizeof(Header) ||
        d.size() > std::numeric_limits<std::uint32_t>::max())
        throw std::runtime_error("frame too large");
    while (hdr_->write_seq.load(std::memory_order_acquire) -
           hdr_->read_seq.load(std::memory_order_acquire) >= 1)
        std::this_thread::yield();
    std::memcpy(data_, d.data(), d.size());
    hdr_->frame_len.store(static_cast<std::uint32_t>(d.size()),
                          std::memory_order_relaxed);
    hdr_->write_seq.fetch_add(1, std::memory_order_release);
}

Bytes ShmChannel::recv() {
    while (hdr_->write_seq.load(std::memory_order_acquire) ==
           hdr_->read_seq.load(std::memory_order_acquire))
        std::this_thread::yield();
    std::uint32_t len = hdr_->frame_len.load(std::memory_order_relaxed);
    if (len > size_ - sizeof(Header))
        throw std::runtime_error("corrupt shared-memory frame length");
    Bytes out(len);
    std::memcpy(out.data(), data_, len);
    hdr_->read_seq.fetch_add(1, std::memory_order_release);
    return out;
}

void ShmChannel::close() noexcept {
    if (ptr_) { UnmapViewOfFile(ptr_); ptr_ = nullptr; }
    if (hMap_) { CloseHandle(hMap_); hMap_ = nullptr; }
}

} // namespace civ

#else

#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

namespace civ {

ShmChannel::ShmChannel(const std::string& name, bool create, std::size_t size)
    : name_(name), size_(size), creator_(create) {
    if (name_.empty())
        throw std::invalid_argument("shm name must not be empty");
    if (size_ <= sizeof(Header))
        throw std::invalid_argument("shm size must exceed header size");
    if (create) {
        fd_ = shm_open(name_.c_str(), O_CREAT | O_RDWR, 0666);
        if (fd_ < 0) throw std::runtime_error("shm_open failed");
        if (ftruncate(fd_, size_) < 0) throw std::runtime_error("ftruncate failed");
    } else {
        fd_ = shm_open(name_.c_str(), O_RDWR, 0666);
        if (fd_ < 0) throw std::runtime_error("shm_open failed");
    }
    ptr_ = mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
    if (ptr_ == MAP_FAILED) throw std::runtime_error("mmap failed");
    hdr_ = static_cast<Header*>(ptr_);
    data_ = static_cast<Byte*>(ptr_) + sizeof(Header);
    if (create) hdr_ = new (ptr_) Header();
}

ShmChannel::~ShmChannel() { close(); }

void ShmChannel::send(ByteView d) {
    if (d.size() > size_ || d.size() > size_ - sizeof(Header) ||
        d.size() > std::numeric_limits<std::uint32_t>::max())
        throw std::runtime_error("frame too large");
    while (hdr_->write_seq.load(std::memory_order_acquire) -
           hdr_->read_seq.load(std::memory_order_acquire) >= 1)
        std::this_thread::yield();
    std::memcpy(data_, d.data(), d.size());
    hdr_->frame_len.store(static_cast<std::uint32_t>(d.size()),
                          std::memory_order_relaxed);
    hdr_->write_seq.fetch_add(1, std::memory_order_release);
}

Bytes ShmChannel::recv() {
    while (hdr_->write_seq.load(std::memory_order_acquire) ==
           hdr_->read_seq.load(std::memory_order_acquire))
        std::this_thread::yield();
    std::uint32_t len = hdr_->frame_len.load(std::memory_order_relaxed);
    if (len > size_ - sizeof(Header))
        throw std::runtime_error("corrupt shared-memory frame length");
    Bytes out(len);
    std::memcpy(out.data(), data_, len);
    hdr_->read_seq.fetch_add(1, std::memory_order_release);
    return out;
}

void ShmChannel::close() noexcept {
    if (ptr_) { munmap(ptr_, size_); ptr_ = nullptr; }
    if (fd_ >= 0) { close(fd_); fd_ = -1; }
    // 只有创建者负责删除共享内存对象；客户端 unlink 会让服务端后续重连失败。
    if (creator_) shm_unlink(name_.c_str());
}

} // namespace civ
#endif
