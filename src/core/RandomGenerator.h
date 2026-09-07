#pragma once

#include <algorithm>
#include <cstdint>
#include <random>

namespace sanguosha {

class RandomGenerator {
public:
    explicit RandomGenerator(std::uint32_t seed = std::random_device{}());

    template <typename T>
    void shuffle(T &items)
    {
        std::shuffle(items.begin(), items.end(), engine_);
    }

private:
    std::mt19937 engine_;
};

} // namespace sanguosha
