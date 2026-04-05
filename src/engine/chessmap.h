#pragma once

#include "chess.h"
#include "param.h"
#include "simd.h"
#include <cinttypes>
#include <iostream>
#include <vector>

namespace chessmap
{

constexpr int HL = 128;
constexpr int OUTPUTS = 64;
constexpr int QA = 255;
constexpr int QB = 64;
constexpr int SCALE = 500;

#define INCBIN_SILENCE_BITCODE_WARNING
#include "../hpplib/incbin.h"
INCBIN(Chessmap, "../nets/chessmap/chessmap_20.bin");



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
    bool outputs_cached[OUTPUTS];

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

    void make_move(const chess::Board &board, const chess::Move &move)
    {
        assert(m_head < param::MAX_DEPTH);

        m_head++;
        m_side[m_head].is_clean[0] = m_side[m_head].is_clean[1] = false;
        auto *update = &m_side[m_head].up;

        memset(m_side[m_head].outputs_cached, 0, sizeof(m_side[m_head].outputs_cached));

        // new king pos
        update->king_sq[0] = board.kingSq(chess::Color::WHITE);
        update->king_sq[1] = board.kingSq(chess::Color::BLACK);
        if (board.at(move.from()).type() == chess::PieceType::KING)
        {
            // check castle
            if (move.typeOf() == chess::Move::CASTLING)
            {
                const bool king_side = move.to() > move.from();
                const chess::Square king_to =
                    chess::Square::castling_king_square(king_side, board.sideToMove());
                update->king_sq[board.sideToMove()] = king_to;
            }
            else
            {
                update->king_sq[board.sideToMove()] = move.to();
            }
        }

        chess::Piece from = board.at(move.from());
        chess::Piece to = board.at(move.to());
        bool is_capture = to != chess::Piece::NONE;
        update->type = update::MOVE;
        switch (move.typeOf())
        {
        case chess::Move::NORMAL: {
            update->add1 = {move.to(), from};
            update->sub1 = {move.from(), from};

            if (is_capture)
            {
                update->type = update::CAPTURE;
                update->sub2 = {move.to(), to};
            }
            break;
        }
        case chess::Move::ENPASSANT: {
            update->type = update::CAPTURE;

            update->add1 = {move.to(), from};
            update->sub1 = {move.from(), from};

            update->sub2 = {move.to().ep_square(), board.at(move.to().ep_square())};
            break;
        }
        case chess::Move::PROMOTION: {
            update->add1 = {move.to(), chess::Piece{board.sideToMove(), move.promotionType()}};
            update->sub1 = {move.from(), from};

            if (is_capture)
            {
                update->type = update::CAPTURE;
                update->sub2 = {move.to(), to};
            }
            break;
        }
        case chess::Move::CASTLING: {
            const bool king_side = move.to() > move.from();
            const chess::Square rook_to =
                chess::Square::castling_rook_square(king_side, board.sideToMove());
            const chess::Square king_to =
                chess::Square::castling_king_square(king_side, board.sideToMove());

            update->type = update::CASTLE;
            update->add1 = {king_to, from};
            update->sub1 = {move.from(), from};

            update->add2 = {rook_to, to};
            update->sub2 = {move.to(), to};
            break;
        }
        default:
            assert(false);
        }
    }

    void unmake_move()
    {
        m_head--;
    }

    void catchup(const chess::Board &position)
    {
        for (int side = 0; side <= 1; ++side)
        {
            if (m_side[m_head].is_clean[side])
                continue;

            int base = m_head;
            while (true)
            {
                if (m_side[base].is_clean[side])
                {
                    for (int i = base + 1; i <= m_head; ++i)
                    {
                        update((simd::Vec *)m_side[i - 1].vals[side],
                               (simd::Vec *)m_side[i].vals[side], m_side[i].up, side);
                        m_side[i].is_clean[side] = true;
                    }

                    assert(m_side[m_head].is_clean[side]);
                    break;
                }

                base -= 1;
                assert(base >= 0);
            }
        }

        assert(m_side[m_head].is_clean[0] && m_side[m_head].is_clean[1]);
    }

    void update(simd::Vec *__restrict__ base, simd::Vec *__restrict__ next, const update &update,
                chess::Color side)
    {
        switch (update.type)
        {
        case update::MOVE: {
            fused_add_sub<HL>(
                next, base,
                feature_lookup(update.king_sq[side], side, update.add1.second, update.add1.first),
                feature_lookup(update.king_sq[side], side, update.sub1.second, update.sub1.first));
            break;
        }
        case update::CAPTURE: {
            fused_add_sub_sub<HL>(
                next, base,
                feature_lookup(update.king_sq[side], side, update.add1.second, update.add1.first),
                feature_lookup(update.king_sq[side], side, update.sub1.second, update.sub1.first),
                feature_lookup(update.king_sq[side], side, update.sub2.second, update.sub2.first));
            break;
        }
        case update::CASTLE: {
            fused_add_add_sub_sub<HL>(
                next, base,
                feature_lookup(update.king_sq[side], side, update.add1.second, update.add1.first),
                feature_lookup(update.king_sq[side], side, update.add2.second, update.add2.first),
                feature_lookup(update.king_sq[side], side, update.sub1.second, update.sub1.first),
                feature_lookup(update.king_sq[side], side, update.sub2.second, update.sub2.first));
            break;
        }
        }
    }

    int32_t get_output(const chess::Board &ref, chess::Move move) const
    {
        int32_t result;
        if (ref.sideToMove() == chess::Color::BLACK)
        {
            result = (int)m_side[m_head].outputs[move.from().index() ^ 56];
        }
        else
        {
            result = (int)m_side[m_head].outputs[move.from().index()];
        }

        return std::clamp(result, 0, 2000);
    }

    int32_t evaluate_cached(const chess::Board &ref, chess::Move move)
    {
        assert(m_side[m_head].is_clean[0]);

        const int16x8_t *__restrict__ us = (int16x8_t *)(m_side[m_head].vals[ref.sideToMove()]);
        const int16x8_t *__restrict__ them =
            (int16x8_t *)(m_side[m_head].vals[ref.sideToMove() ^ 1]);

        int bucket_from = move.from().index();
        int bucket_to = move.to().index();

        if (ref.sideToMove() == chess::Color::BLACK)
        {
            bucket_from ^= 56;
            bucket_to ^= 56;
        }

        if (!m_side[m_head].outputs_cached[bucket_from])
        {
            m_side[m_head].outputs_cached[bucket_from] = true;

            const int16x8_t *__restrict__ us_weights =
                (int16x8_t *)(m_network.output_weights[bucket_from]);
            const int16x8_t *__restrict__ them_weights =
                (int16x8_t *)(m_network.output_weights[bucket_from] + HL);

            int32_t output = flatten(us, us_weights) + flatten(them, them_weights);

            output /= QA;
            output += m_network.output_bias[bucket_from];

            output *= SCALE;
            output /= QA * QB;

            m_side[m_head].outputs[bucket_from] = output;
        }

        // if (!m_side[m_head].outputs_cached[64 + bucket_to])
        // {
        //     m_side[m_head].outputs_cached[64 + bucket_to] = true;

        //     const int16x8_t *__restrict__ us_weights =
        //         (int16x8_t *)(m_network.output_weights[64 + bucket_to]);
        //     const int16x8_t *__restrict__ them_weights =
        //         (int16x8_t *)(m_network.output_weights[64 + bucket_to] + HL);

        //     int32_t output = flatten(us, us_weights) + flatten(them, them_weights);

        //     output /= QA;
        //     output += m_network.output_bias[64 + bucket_to];

        //     output *= SCALE;
        //     output /= QA * QB;

        //     m_side[m_head].outputs[64 + bucket_to] = output;
        // }

        return get_output(ref, move);
    }

    // deprecated
    int16_t *evaluate(const chess::Board &ref, chess::Bitboard mask)
    {
        catchup(ref);
        assert(m_side[m_head].is_clean[0]);

        const int16x8_t *__restrict__ us = (int16x8_t *)(m_side[m_head].vals[ref.sideToMove()]);
        const int16x8_t *__restrict__ them =
            (int16x8_t *)(m_side[m_head].vals[ref.sideToMove() ^ 1]);

        while (mask)
        {
            int bucket = mask.pop();

            // if it is black moving, the squares are flipped
            int bucket_index = ref.sideToMove() == chess::Color::WHITE ? bucket : bucket ^ 56;

            const int16x8_t *__restrict__ us_weights =
                (int16x8_t *)(m_network.output_weights[bucket_index]);
            const int16x8_t *__restrict__ them_weights =
                (int16x8_t *)(m_network.output_weights[bucket_index] + HL);

            int32_t output = flatten(us, us_weights) + flatten(them, them_weights);

            output /= QA;
            output += m_network.output_bias[bucket_index];

            output *= SCALE;
            output /= QA * QB;

            // we always store using the abs bucket index
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
            if (position.occ() & chess::Bitboard::fromSquare(i ^ 56))
                printf("%4hd ", result[i ^ 56]);
            else
                printf("     ");

            if (i % 8 == 7)
                std::cout << "\n";
        }

        // std::cout << "End Square\n";
        // for (int i = 0; i < 64; ++i)
        // {
        //     printf("%4hd ", result[64 + (i ^ 56)]);
        //     if (i % 8 == 7)
        //         std::cout << "\n";
        // }
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
