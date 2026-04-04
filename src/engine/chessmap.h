#pragma once

#include "chess.h"
#include "param.h"
#include "simd.h"
#include <cinttypes>
#include <iostream>
#include <vector>

namespace chessmap
{

constexpr int HL = 256;
constexpr int OUTPUTS = 64 + 64;
constexpr int QA = 255;
constexpr int QB = 64;
constexpr int SCALE = 100;

#define INCBIN_SILENCE_BITCODE_WARNING
#include "../hpplib/incbin.h"
INCBIN(Chessmap, "../nets/chessmap/chessmap.bin");

struct network
{
    alignas(64) int16_t feature_weights[768][HL];
    alignas(64) int16_t feature_bias[HL];

    alignas(64) int16_t output_weights[OUTPUTS][2 * HL];
    int16_t output_bias[OUTPUTS];
};

struct update
{
    chess::Square king_sq[2];

    std::pair<chess::Square, chess::Piece> add1;
    std::pair<chess::Square, chess::Piece> add2;
    std::pair<chess::Square, chess::Piece> sub1;
    std::pair<chess::Square, chess::Piece> sub2;

    enum
    {
        MOVE,
        CAPTURE,
        CASTLE
    } type;
};

struct accumulator
{
    alignas(64) int16_t vals[2][HL];
    int16_t outputs[OUTPUTS];
    bool is_clean[2];
    update up;
};

class net
{
    network m_network{};
    accumulator m_side[param::MAX_DEPTH]{};
    int m_head{0};

  public:
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

    int16_t *evaluate(const chess::Board &ref)
    {
        // catchup(ref);
        assert(m_side[m_head].is_clean[0]);

        const int16x8_t *__restrict__ us = (int16x8_t *)(m_side[m_head].vals[ref.sideToMove()]);
        const int16x8_t *__restrict__ them =
            (int16x8_t *)(m_side[m_head].vals[ref.sideToMove() ^ 1]);

        for (int bucket = 0; bucket < OUTPUTS; ++bucket)
        {
            const int16x8_t *__restrict__ us_weights =
                (int16x8_t *)(m_network.output_weights[bucket]);
            const int16x8_t *__restrict__ them_weights =
                (int16x8_t *)(m_network.output_weights[bucket] + HL);

            int32_t output = flatten(us, us_weights) + flatten(them, them_weights);

            output /= QA;
            output += m_network.output_bias[bucket];

            output *= SCALE;
            output /= QA * QB;

            m_side[m_head].outputs[bucket] = output;
        }

        return m_side[m_head].outputs;
    }

    int32_t flatten(const int16x8_t *__restrict__ acc, const int16x8_t *__restrict__ weight)
    {
        const int16x8_t v_zero = vdupq_n_s16(0);
        const int16x8_t v_qa = vdupq_n_s16(QA);

        int32x4_t out0 = vdupq_n_s32(0);
        int32x4_t out1 = vdupq_n_s32(0);
        int32x4_t out2 = vdupq_n_s32(0);
        int32x4_t out3 = vdupq_n_s32(0);

        for (int i = 0; i < HL / 8; i += 2)
        {
            __builtin_prefetch(&acc[i + 2]);
            __builtin_prefetch(&weight[i + 2]);

            int16x8_t c0 = vminq_s16(vmaxq_s16(acc[i + 0], v_zero), v_qa);
            int16x8_t c1 = vminq_s16(vmaxq_s16(acc[i + 1], v_zero), v_qa);

            int16x8_t pm0 = vmulq_s16(c0, weight[i + 0]);
            int16x8_t pm1 = vmulq_s16(c1, weight[i + 1]);

            out0 = vmlal_s16(out0, vget_low_s16(pm0), vget_low_s16(c0));
            out1 = vmlal_s16(out1, vget_low_s16(pm1), vget_low_s16(c1));
            out2 = vmlal_high_s16(out2, pm0, c0);
            out3 = vmlal_high_s16(out3, pm1, c1);
        }

        return vaddvq_s32(vaddq_s32(vaddq_s32(out0, out1), vaddq_s32(out2, out3)));
    }

    void initialize(const chess::Board &position)
    {
        m_head = 0;
        m_side[0].up.king_sq[0] = position.kingSq(chess::Color::WHITE);
        m_side[0].up.king_sq[1] = position.kingSq(chess::Color::BLACK);

        // refresh head
        fused_copy<HL>((simd::Vec *)m_side[m_head].vals[0], (simd::Vec *)m_network.feature_bias);
        fused_copy<HL>((simd::Vec *)m_side[m_head].vals[1], (simd::Vec *)m_network.feature_bias);
        
        auto occ = position.occ();
        while (occ)
        {
            auto sq = occ.pop();
            acc_add_piece(m_side[m_head], 0, position.kingSq(0), position.at(sq), sq);
            acc_add_piece(m_side[m_head], 1, position.kingSq(1), position.at(sq), sq);
        }
        
        m_side[m_head].is_clean[0] = m_side[m_head].is_clean[1] = true;
    }

    void show_evaluation(const chess::Board &position) const
    {
        auto *result = m_side[m_head].outputs;
        std::cout << "Start Square\n";
        for (int i = 0; i < 64; ++i)
        {
            if (position.occ() & chess::Bitboard::fromSquare(i^56))
                printf("%4hd ", result[i ^ 56]);
            else
                printf("     ");
            
            if (i % 8 == 7)
                std::cout << "\n";
        }

        std::cout << "End Square\n";
        for (int i = 0; i < 64; ++i)
        {
            printf("%4hd ", result[64 + (i ^ 56)]);
            if (i % 8 == 7)
                std::cout << "\n";
        }
    }

  private:
    simd::Vec *feature_lookup(chess::Square king_sq, chess::Color side, chess::Piece piece,
                              chess::Square square)
    {
        return reinterpret_cast<simd::Vec *>(
            m_network.feature_weights[((piece.color() == side ? 0 : 6) + piece.type()) * 64 +
                                      square.relative_square(side).index()]);
    }

    void acc_add_piece(accumulator &acc, chess::Color side, chess::Square king_sq,
                       chess::Piece piece, chess::Square square)
    {
        fused_add<HL>((simd::Vec *)acc.vals[side], (simd::Vec *)acc.vals[side],
                      feature_lookup(king_sq, side, piece, square));
    }

    /// fused updates ///

    template <int Size>
    static void fused_copy(simd::Vec *__restrict__ out, const simd::Vec *__restrict__ in)
    {
        for (int i = 0; i < Size / simd::WIDTH; ++i)
            out[i] = in[i];
    }

    template <int Size>
    static void fused_add(simd::Vec *out, const simd::Vec *in, const simd::Vec *add)
    {
        for (int i = 0; i < Size / simd::WIDTH; ++i)
            out[i] = simd::add16(in[i], add[i]);
    }

    template <int Size>
    static void fused_sub(simd::Vec *out, const simd::Vec *in, const simd::Vec *sub)
    {
        for (int i = 0; i < Size / simd::WIDTH; ++i)
            out[i] = simd::sub16(in[i], sub[i]);
    }

    template <int Size>
    static void fused_add_sub(simd::Vec *out, const simd::Vec *in, const simd::Vec *add,
                              const simd::Vec *sub)
    {
        for (int i = 0; i < Size / simd::WIDTH; ++i)
            out[i] = simd::sub16(simd::add16(in[i], add[i]), sub[i]);
    }

    template <int Size>
    static void fused_add_sub_sub(simd::Vec *out, const simd::Vec *in, const simd::Vec *add,
                                  const simd::Vec *sub1, const simd::Vec *sub2)
    {
        for (int i = 0; i < Size / simd::WIDTH; ++i)
            out[i] = simd::sub16(simd::sub16(simd::add16(in[i], add[i]), sub1[i]), sub2[i]);
    }

    template <int Size>
    static void fused_add_add_sub_sub(simd::Vec *out, const simd::Vec *in, const simd::Vec *add1,
                                      const simd::Vec *add2, const simd::Vec *sub1,
                                      const simd::Vec *sub2)
    {
        for (int i = 0; i < Size / simd::WIDTH; ++i)
            out[i] = simd::sub16(
                simd::add16(simd::sub16(simd::add16(in[i], add1[i]), sub1[i]), add2[i]), sub2[i]);
    }
};

} // namespace chessmap
