#include "fast_lane/shared_memory.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::string unique_channel_name()
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return "fast_lane_test_" + std::to_string(now);
}

void test_u64_codec()
{
    const std::vector<std::uint64_t> expected{0, 1, 2, 0x1122334455667788ULL};
    const auto bytes = fast_lane::encode_u64_sequence(expected);
    const auto actual = fast_lane::decode_u64_sequence(bytes);

    require(bytes.size() == expected.size() * sizeof(std::uint64_t), "encoded byte count is wrong");
    require(actual == expected, "decoded uint64 sequence differs from input");
}

void test_copy_on_write_buffer()
{
    fast_lane::CopyOnWriteBuffer original({1, 2, 3});
    auto branch = original.fork();

    branch.set(0, 99);
    branch.append(4);

    require(original.size() == 3, "original COW buffer size changed after branch append");
    require(branch.size() == 4, "branch COW buffer size did not change");
    require(original.at(0) == 1, "original COW buffer was modified by branch write");
    require(branch.at(0) == 99, "branch COW buffer write was not applied");
}

void test_shared_memory_round_trip()
{
    const std::string name = unique_channel_name();
    auto leader = fast_lane::SharedMemoryChannel::create_leader(name, 4096);
    auto follower = fast_lane::SharedMemoryChannel::open_follower(name);

    const std::vector<std::uint64_t> first_values{10, 11, 12, 13};
    leader.publish(fast_lane::encode_u64_sequence(first_values));

    fast_lane::SharedSnapshot first_snapshot;
    require(follower.try_read(0, first_snapshot), "follower did not read first published snapshot");
    require(first_snapshot.sequence == 2, "first published sequence should be 2");
    require(fast_lane::decode_u64_sequence(first_snapshot.bytes) == first_values, "first snapshot payload mismatch");

    fast_lane::SharedSnapshot duplicate_snapshot;
    require(!follower.try_read(first_snapshot.sequence, duplicate_snapshot), "try_read reported duplicate sequence as new");

    const std::vector<std::uint64_t> second_values{20, 21, 22, 23, 24};
    leader.publish(fast_lane::encode_u64_sequence(second_values));

    fast_lane::SharedSnapshot second_snapshot;
    require(follower.try_read(first_snapshot.sequence, second_snapshot), "follower did not read second published snapshot");
    require(second_snapshot.sequence == 4, "second published sequence should be 4");
    require(fast_lane::decode_u64_sequence(second_snapshot.bytes) == second_values, "second snapshot payload mismatch");
}

} // namespace

int main()
{
    try {
        test_u64_codec();
        test_copy_on_write_buffer();
        test_shared_memory_round_trip();
    } catch (const std::exception& error) {
        std::cerr << "test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "shared_memory_tests passed\n";
    return EXIT_SUCCESS;
}
