#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace fast_lane {

struct SharedSnapshot {
    std::uint64_t sequence = 0;
    std::vector<std::uint8_t> bytes;
};

class SharedMemoryError : public std::runtime_error {
public:
    explicit SharedMemoryError(const std::string& message);
};

class SharedMemoryChannel {
public:
    static SharedMemoryChannel create_leader(const std::string& name, std::size_t capacity);
    static SharedMemoryChannel open_follower(const std::string& name);

    SharedMemoryChannel(SharedMemoryChannel&& other) noexcept;
    SharedMemoryChannel& operator=(SharedMemoryChannel&& other) noexcept;

    SharedMemoryChannel(const SharedMemoryChannel&) = delete;
    SharedMemoryChannel& operator=(const SharedMemoryChannel&) = delete;

    ~SharedMemoryChannel();

    std::size_t capacity() const;
    std::string name() const;

    void publish(const std::vector<std::uint8_t>& bytes);
    bool try_read(std::uint64_t last_seen_sequence, SharedSnapshot& out) const;

private:
    struct Impl;

    explicit SharedMemoryChannel(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

class CopyOnWriteBuffer {
public:
    CopyOnWriteBuffer();
    explicit CopyOnWriteBuffer(std::vector<std::uint8_t> bytes);

    CopyOnWriteBuffer fork() const;

    bool empty() const;
    std::size_t size() const;
    const std::uint8_t* data() const;

    std::uint8_t at(std::size_t offset) const;
    void set(std::size_t offset, std::uint8_t value);
    void append(std::uint8_t value);

private:
    void detach();

    std::shared_ptr<std::vector<std::uint8_t>> bytes_;
};

std::vector<std::uint8_t> encode_u64_sequence(const std::vector<std::uint64_t>& values);
std::vector<std::uint64_t> decode_u64_sequence(const std::vector<std::uint8_t>& bytes);

} // namespace fast_lane
