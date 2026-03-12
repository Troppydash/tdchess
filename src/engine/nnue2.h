#pragma once

#include "../hpplib/chess.h"
#include "param.h"
#include <cinttypes>

#include <arm_neon.h>

namespace simd
{

using Vec = int16x8_t;
constexpr size_t WIDTH = 8;
constexpr size_t ALIGN = 64;

constexpr Vec add16(Vec a, Vec b)
{
    return vaddq_s16(a, b);
}

constexpr Vec sub16(Vec a, Vec b)
{
    return vsubq_s16(a, b);
}

} // namespace simd

namespace nnue2
{

constexpr int HL = 1536;
constexpr int KINGS = 8;
constexpr int OUTPUTS = 1;
constexpr int QA = 403;
constexpr int QB = 81;
constexpr int SCALE = 400;

// clang-format off
constexpr int KING_BUCKET[] = {
      0, 1, 2, 3, 11, 10, 9, 8,
      4, 4, 5, 5, 13, 13, 12, 12,
      6, 6, 6, 6, 14, 14, 14, 14,
      6, 6, 6, 6, 14, 14, 14, 14,
      7, 7, 7, 7, 15, 15, 15, 15,
      7, 7, 7, 7, 15, 15, 15, 15,
      7, 7, 7, 7, 15, 15, 15, 15,
      7, 7, 7, 7, 15, 15, 15, 15,
};
// clang-format on

constexpr int GET_KING_BUCKET(int sq)
{
    return KING_BUCKET[sq] % KINGS;
}

#include "../hpplib/incbin.h"
INCBIN(Embed2, "../nets/motor.bin");

// horizontally mirrored, king input buckets, output buckets, single layer nnue
struct network
{
    alignas(simd::ALIGN) int16_t feature_weights[KINGS][768][HL];
    alignas(simd::ALIGN) int16_t feature_bias[HL];

    alignas(simd::ALIGN) int16_t output_weights[OUTPUTS * 2 * HL];
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
    alignas(simd::ALIGN) int16_t vals[2][HL];
    bool is_clean[2];
    update up;
};

constexpr int FINNY_TABLE_ENTRIES = 2;
struct finny_table
{
    struct entry
    {
        accumulator acc;
        // [side][color]
        chess::Bitboard bycolor[2][2];
        // [side][piece_type]
        chess::Bitboard bypiece[2][6];
    };

    // [is_mirrored][king_bucket]
    entry ent[2][KINGS][FINNY_TABLE_ENTRIES];

    // full clear
    void clear()
    {
        memset(ent, 0, sizeof(ent));
    }
};

struct net
{
    network m_network{};
    accumulator m_side[param::MAX_DEPTH]{};
    int m_head{0};

    finny_table m_table{};

    net()
    {
        clear();
    };

    void incbin_load() const
    {
        const unsigned char *data = gEmbed2Data;
        if (gEmbed2Size != sizeof(network))
        {
            std::cout << gEmbed2Size << ", " << sizeof(network) << std::endl;
            std::cout << "failed to load network\n";
            exit(0);
        }

        std::memcpy((void *)&m_network, data, gEmbed2Size);
    }

    bool load_network(const std::string &path)
    {
        std::ifstream file(path, std::ios::binary | std::ios::ate);

        if (!file.is_open())
        {
            std::cerr << "info failed to open " << path << std::endl;
            return false;
        }

        // Sanity check: Does the file size match our struct size?
        std::streamsize size = file.tellg();
        if (size != sizeof(network))
        {
            std::cerr << "info size mismatch! File: " << size
                      << " bytes, Struct: " << sizeof(network) << " bytes." << std::endl;
            return false;
        }

        // Go back to the start and read the whole thing
        file.seekg(0, std::ios::beg);
        if (file.read(reinterpret_cast<char *>(&m_network), sizeof(network)))
        {
            return true;
        }

        return false;
    }

    void make_move(const chess::Board &board, const chess::Move &move)
    {
        assert(m_head < param::MAX_DEPTH);

        m_head++;
        m_side[m_head].is_clean[0] = m_side[m_head].is_clean[1] = false;
        auto *update = &m_side[m_head].up;

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
        }
    }

    void unmake_move()
    {
        m_head--;
    }

    int32_t evaluate(const chess::Board &ref)
    {
        catchup(ref);
        assert(m_side[m_head].is_clean[0]);

        // compute output_bucket
        int bucket = 0;

        const int16x8_t *__restrict us = (int16x8_t *)m_side[m_head].vals[ref.sideToMove()];
        const int16x8_t *__restrict them = (int16x8_t *)m_side[m_head].vals[ref.sideToMove() ^ 1];

        const int16x8_t *__restrict us_weights =
            (int16x8_t *)(m_network.output_weights + bucket * 2 * HL);
        const int16x8_t *__restrict them_weights =
            (int16x8_t *)(m_network.output_weights + bucket * 2 * HL + HL);

        const int16x8_t v_zero = vdupq_n_s16(0);
        const int16x8_t v_qa = vdupq_n_s16(QA);

        int32x4_t us_output1 = vdupq_n_s32(0);
        int32x4_t us_output2 = vdupq_n_s32(0);
        int32x4_t them_output1 = vdupq_n_s32(0);
        int32x4_t them_output2 = vdupq_n_s32(0);

        for (int i = 0; i < HL / 8; i += 1)
        {
            int16x8_t us_clamped = vminq_s16(vmaxq_s16(us[i], v_zero), v_qa);
            int16x8_t them_clamped = vminq_s16(vmaxq_s16(them[i], v_zero), v_qa);

            int16x8_t us_premult = vmulq_s16(us_clamped, us_weights[i]);
            int16x8_t them_premult = vmulq_s16(them_clamped, them_weights[i]);

            us_output1 = vmlal_s16(us_output1, vget_low_s16(us_premult), vget_low_s16(us_clamped));
            us_output2 =
                vmlal_s16(us_output2, vget_high_s16(us_premult), vget_high_s16(us_clamped));

            them_output1 =
                vmlal_s16(them_output1, vget_low_s16(them_premult), vget_low_s16(them_clamped));
            them_output2 =
                vmlal_s16(them_output2, vget_high_s16(them_premult), vget_high_s16(them_clamped));
        }

        int32_t output = vaddvq_s32(
            vaddq_s32(vaddq_s32(us_output1, us_output2), vaddq_s32(them_output1, them_output2)));
        output /= QA;
        output += m_network.output_bias[bucket];

        output *= SCALE;
        output /= QA * QB;

        return std::clamp((int)output, -param::NNUE_MAX, (int)param::NNUE_MAX);
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
                if (need_refresh(side, m_side[base].up.king_sq[side],
                                 m_side[m_head].up.king_sq[side]))
                {
                    // full refresh head
                    refresh(position, side, m_head);
                    break;
                }

                if (m_side[base].is_clean[side])
                {
                    for (int i = base + 1; i <= m_head; ++i)
                    {
                        update((simd::Vec *)m_side[i - 1].vals[side],
                               (simd::Vec *)m_side[i].vals[side], m_side[i].up, side);
                        m_side[i].is_clean[side] = true;
                    }

                    break;
                }

                base -= 1;
                assert(base >= 0);
            }
        }

        assert(m_side[m_head].is_clean[0] && m_side[m_head].is_clean[1]);
    }

    void update(simd::Vec *base, simd::Vec *next, const update &update, chess::Color side)
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

    void refresh(const chess::Board &board, chess::Color side, int index)
    {
        // finny table refresh
        chess::Square king_sq = board.kingSq(side);
        int bucket = GET_KING_BUCKET(king_sq.relative_square(side).index());
        assert(king_sq.file() == king_sq.relative_square(side).file());
        int is_mirrored = king_sq.file() >= chess::File::FILE_E;

        auto *ref = &m_table.ent[is_mirrored][bucket][0];

        // select better table entry randomly
        if (board.hash() % 2)
        {
            auto *ref_alt = &m_table.ent[is_mirrored][bucket][1];

            int count[2] = {0, 0};
            for (int color = 0; color <= 1; ++color)
            {
                for (int piece = 0; piece < 6; ++piece)
                {
                    chess::PieceType piece_type{(chess::PieceType::underlying)piece};
                    auto new_bb = board.pieces(piece_type, color);

                    {
                        auto old_bb = ref->bycolor[side][color] & ref->bypiece[side][piece];
                        auto added = new_bb & ~old_bb;
                        auto removed = old_bb & ~new_bb;
                        count[0] += std::max(added.count(), removed.count());
                    }

                    {
                        auto old_bb = ref_alt->bycolor[side][color] & ref_alt->bypiece[side][piece];
                        auto added = new_bb & ~old_bb;
                        auto removed = old_bb & ~new_bb;
                        count[1] += std::max(added.count(), removed.count());
                    }
                }
            }

            // // if simple refresh is faster, do it
            // // +1 due to the initial fused_copy for biases
            // if (std::min(count[0], count[1]) > board.occ().count() + 1) [[unlikely]]
            // {
            //     // update the worst one
            //     if (count[1] > count[0])
            //         ref = ref_alt;
            //
            //     fused_copy<HL>((simd::Vec *)m_side[index].vals[side],
            //                    (simd::Vec *)m_network.feature_bias);
            //
            //     chess::Bitboard occ = board.occ();
            //     while (occ)
            //     {
            //         chess::Square sq = occ.pop();
            //         acc_add_piece(m_side[index], side, king_sq, board.at(sq), sq);
            //     }
            //
            //     fused_copy<HL>((simd::Vec *)ref->acc.vals[side],
            //                    (simd::Vec *)m_side[index].vals[side]);
            //     ref->acc.is_clean[side] = true;
            //     memcpy(&ref->bycolor[side], &board.occ_bb_, sizeof(ref->bycolor[0]));
            //     memcpy(&ref->bypiece[side], &board.pieces_bb_, sizeof(ref->bypiece[0]));
            //     m_side[index].is_clean[side] = true;
            //     return;
            // }

            if (count[1] < count[0])
                ref = ref_alt;
        }

        // initial bias
        if (!ref->acc.is_clean[side])
        {
            fused_copy<HL>((simd::Vec *)ref->acc.vals[side], (simd::Vec *)m_network.feature_bias);
            ref->acc.is_clean[side] = true;
        }

        for (int color = 0; color <= 1; ++color)
        {
            for (int piece = 0; piece < 6; ++piece)
            {
                chess::PieceType piece_type{(chess::PieceType::underlying)piece};

                auto old_bb = ref->bycolor[side][color] & ref->bypiece[side][piece];
                auto new_bb = board.pieces(piece_type, color);

                auto added = new_bb & ~old_bb;
                auto removed = old_bb & ~new_bb;

                while (added && removed)
                {
                    chess::Square sq_to = added.pop();
                    chess::Square sq_from = removed.pop();
                    acc_move_piece(ref->acc, side, king_sq, {piece_type, color}, sq_from, sq_to);
                }

                while (added)
                {
                    chess::Square sq = added.pop();
                    acc_add_piece(ref->acc, side, king_sq, {piece_type, color}, sq);
                }

                while (removed)
                {
                    chess::Square sq = removed.pop();
                    acc_remove_piece(ref->acc, side, king_sq, {piece_type, color}, sq);
                }
            }
        }

        fused_copy<HL>((simd::Vec *)m_side[index].vals[side], (simd::Vec *)ref->acc.vals[side]);
        memcpy(&ref->bycolor[side], &board.occ_bb_, sizeof(ref->bycolor[0]));
        memcpy(&ref->bypiece[side], &board.pieces_bb_, sizeof(ref->bypiece[0]));
        m_side[index].is_clean[side] = true;
    }

    void initialize(const chess::Board &position)
    {
        // empty, since likely to be not close
        m_table.clear();

        m_head = 0;
        m_side[0].up.king_sq[0] = position.kingSq(chess::Color::WHITE);
        m_side[0].up.king_sq[1] = position.kingSq(chess::Color::BLACK);
        refresh(position, chess::Color::WHITE, 0);
        refresh(position, chess::Color::BLACK, 0);
    }

    void clear()
    {
        // nothing
    }

  private:
    static bool need_refresh(chess::Color side, chess::Square old_king, chess::Square new_king)
    {
        if (old_king == new_king)
            return false;

        if ((old_king.index() & 0b100) != (new_king.index() & 0b100))
            return true;

        return GET_KING_BUCKET(old_king.relative_square(side).index()) !=
               GET_KING_BUCKET(new_king.relative_square(side).index());
    }

    simd::Vec *feature_lookup(chess::Square king_sq, chess::Color side, chess::Piece piece,
                              chess::Square square)
    {
        // mirror if king on right
        if (king_sq.index() & 0b100)
            square = chess::Square{square.index() ^ 7};

        return reinterpret_cast<simd::Vec *>(
            m_network.feature_weights[GET_KING_BUCKET(king_sq.relative_square(side).index())]
                                     [((piece.color() == side ? 0 : 6) + piece.type()) * 64 +
                                      square.relative_square(side).index()]);
    }

  private:
    /// accumulator functions ///

    void acc_add_piece(accumulator &acc, chess::Color side, chess::Square king_sq,
                       chess::Piece piece, chess::Square square)
    {
        fused_add<HL>((simd::Vec *)acc.vals[side], (simd::Vec *)acc.vals[side],
                      feature_lookup(king_sq, side, piece, square));
    }

    void acc_remove_piece(accumulator &acc, chess::Color side, chess::Square king_sq,
                          chess::Piece piece, chess::Square square)
    {
        fused_sub<HL>((simd::Vec *)(acc.vals[side]), (simd::Vec *)(acc.vals[side]),
                      feature_lookup(king_sq, side, piece, square));
    }

    void acc_move_piece(accumulator &acc, chess::Color side, chess::Square king_sq,
                        chess::Piece piece, chess::Square square_from, chess::Square square_to)
    {
        fused_add_sub<HL>((simd::Vec *)acc.vals[side], (simd::Vec *)acc.vals[side],
                          feature_lookup(king_sq, side, piece, square_to),
                          feature_lookup(king_sq, side, piece, square_from));
    }

    /// fused updates ///

    template <int Size> static void fused_copy(simd::Vec *out, const simd::Vec *in)
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

} // namespace nnue2
