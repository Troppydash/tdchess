#pragma once

#include <cinttypes>
#include <iostream>
#include <vector>
#include "chess.h"

namespace chessmap
{

constexpr int HL = 256;
constexpr int OUTPUTS = 64 + 64;
constexpr int QA = 255;
constexpr int QB = 64;

#define INCBIN_SILENCE_BITCODE_WARNING
#include "../hpplib/incbin.h"
INCBIN(Chessmap, "../nets/chessmap.bin");

struct network
{
    alignas(64) int16_t feature_weights[768][HL];
    alignas(64) int16_t feature_bias[HL];

    alignas(64) int16_t output_weights[OUTPUTS][2 * HL];
    int16_t output_bias[OUTPUTS];
};

struct net
{
    network m_network{};

    net()
    {
        load_network();
    }

    void load_network()
    {
        const unsigned char *data = gChessmapData;
        if (gChessmapSize != sizeof(network))
        {
            std::cout << gChessmapSize << ", " << sizeof(network) << std::endl;
            std::cout << "failed to load network\n";
            exit(0);
        }

        std::memcpy((void *)&m_network, data, gChessmapSize);
    }

    // TODO: evaluate
    // painful

    std::vector<int> evaluate(const chess::Board &position)
    {
        
        
        return {};
    }
};

} // namespace chessmap
