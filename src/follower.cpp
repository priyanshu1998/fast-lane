#include "fast_lane/shared_memory.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

std::uint64_t parse_u64_arg(char** argv, int index, std::uint64_t fallback)
{
    if (argv[index] == nullptr) {
        return fallback;
    }
    return static_cast<std::uint64_t>(std::stoull(argv[index]));
}

void print_values(const std::vector<std::uint64_t>& values)
{
    const std::size_t limit = std::min<std::size_t>(values.size(), 4);
    std::cout << "[";
    for (std::size_t i = 0; i < limit; ++i) {
        if (i != 0) {
            std::cout << ", ";
        }
        std::cout << values[i];
    }
    if (values.size() > limit) {
        std::cout << ", ...";
    }
    std::cout << "]";
}

} // namespace

int main(int argc, char** argv)
{
    const std::string name = argc > 1 ? argv[1] : "fast_lane_demo";
    const std::uint64_t max_updates = argc > 2 ? parse_u64_arg(argv, 2, 0) : 0;

    try {
        auto channel = fast_lane::SharedMemoryChannel::open_follower(name);
        std::uint64_t last_seen = 0;
        std::uint64_t updates = 0;

        std::cout << "follower: attached to '" << channel.name() << "' with "
                  << channel.capacity() << " bytes of capacity\n";

        while (max_updates == 0 || updates < max_updates) {
            fast_lane::SharedSnapshot snapshot;
            if (!channel.try_read(last_seen, snapshot)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }

            last_seen = snapshot.sequence;
            ++updates;

            fast_lane::CopyOnWriteBuffer local(snapshot.bytes);
            fast_lane::CopyOnWriteBuffer private_branch = local.fork();

            if (!private_branch.empty()) {
                private_branch.set(0, static_cast<std::uint8_t>(private_branch.at(0) ^ 0xffU));
            }

            const auto values = fast_lane::decode_u64_sequence(snapshot.bytes);

            std::cout << "follower: shared sequence " << snapshot.sequence
                      << ", copied " << snapshot.bytes.size() << " bytes, values ";
            print_values(values);

            if (!local.empty()) {
                std::cout << ", local[0]=" << static_cast<int>(local.at(0))
                          << ", private_branch[0]=" << static_cast<int>(private_branch.at(0));
            }
            std::cout << '\n';
        }
    } catch (const std::exception& error) {
        std::cerr << "follower error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
