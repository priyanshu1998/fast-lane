#include "fast_lane/shared_memory.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

std::size_t parse_size_arg(char** argv, int index, std::size_t fallback)
{
    if (argv[index] == nullptr) {
        return fallback;
    }
    return static_cast<std::size_t>(std::stoull(argv[index]));
}

std::vector<std::uint64_t> make_sequence(std::uint64_t tick)
{
    std::vector<std::uint64_t> values;
    values.reserve(16);
    for (std::uint64_t offset = 0; offset < 16; ++offset) {
        values.push_back((tick << 32U) | offset);
    }
    return values;
}

} // namespace

int main(int argc, char** argv)
{
    const std::string name = argc > 1 ? argv[1] : "fast_lane_demo";
    const std::size_t capacity = argc > 2 ? parse_size_arg(argv, 2, 64 * 1024) : 64 * 1024;
    const std::size_t interval_ms = argc > 3 ? parse_size_arg(argv, 3, 500) : 500;

    try {
        auto channel = fast_lane::SharedMemoryChannel::create_leader(name, capacity);

        std::cout << "leader: publishing to '" << channel.name() << "' with "
                  << channel.capacity() << " bytes of capacity\n";

        for (std::uint64_t tick = 1;; ++tick) {
            const auto values = make_sequence(tick);
            const auto bytes = fast_lane::encode_u64_sequence(values);
            channel.publish(bytes);

            std::cout << "leader: sequence " << tick
                      << ", wrote " << values.size() << " uint64 values ("
                      << bytes.size() << " bytes)\n";

            std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
        }
    } catch (const std::exception& error) {
        std::cerr << "leader error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
