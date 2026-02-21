#pragma once

#include "param.h"
#include <fstream>

#ifdef __AVX2__
#define TDCHESS_NNUE_SIMD
#include <immintrin.h>
#endif

#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

#define TDCHESS_NNUE_INLINE

constexpr size_t HIDDEN_SIZE = 1568;
constexpr int16_t QA = 255;
constexpr int16_t QB = 64;
constexpr int32_t SCALE = 400;
constexpr size_t BUCKET_SIZE = 8;

struct alignas(64) accumulator
{
    int16_t vals[HIDDEN_SIZE];
};

struct network
{
    // QA quant, 64*3 -> hidden_size
    accumulator feature_weights[768];
    // QA quant, hidden_size -> hidden_size
    accumulator feature_bias;

    // QB quant, 2*hidden_size -> 1
    alignas(64) int16_t output_weights[BUCKET_SIZE * 2 * HIDDEN_SIZE];
    // QA * QB quant, 1 -> 1
    int16_t output_bias[BUCKET_SIZE];
};

constexpr int32_t screlu(int16_t x)
{
    const int32_t val = std::clamp(x, static_cast<int16_t>(0), QA);
    return val * val;
}

struct dirty_entry
{
    // uint64_t hash = 0;
    chess::Move move = chess::Move::NO_MOVE;
    chess::Piece piece = chess::Piece::NONE;
    chess::Piece captured = chess::Piece::NONE;
    chess::Color stm = chess::Color::NONE;
    bool is_clean = false;
};

class nnue
{
  private:
    network m_network{};
    accumulator m_sides[2][param::MAX_DEPTH]{};

    std::array<dirty_entry, param::MAX_DEPTH + 10> m_entries{};
    int m_ply{0};

  public:
    explicit nnue() = default;

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

    static int translate(const chess::Color &perspective, const chess::Piece &piece,
                         const chess::Square &square)
    {
        return ((piece.color() == perspective ? 0 : 6) + piece.type()) * 64 +
               square.relative_square(perspective).index();
    }

    void initialize(const chess::Board &position)
    {
        m_ply = 0;

        // for (size_t i = 0; i < m_entries.size(); ++i)
        //     m_entries[i].is_clean = false;

        m_entries[0].is_clean = true;

        // clear all entries
        for (size_t i = 0; i < HIDDEN_SIZE; ++i)
            m_sides[0][0].vals[i] = m_sides[1][0].vals[i] = m_network.feature_bias.vals[i];

        chess::Bitboard occ = position.occ();
        while (!occ.empty())
        {
            const int i = occ.pop();
            chess::Square square{i};
            chess::Piece piece = position.at(square);
            add_piece(piece, square);
        }
    }

    [[nodiscard]] int32_t evaluate(int side2move, int bucket)
    {
        catchup();

        int32_t output = 0;

#ifdef TDCHESS_NNUE_SIMD
        // SIMD VECTORIZATION
        const __m256i vec_zero = _mm256_setzero_si256();
        const __m256i vec_qa = _mm256_set1_epi16(QA);
        __m256i sum = vec_zero;

        const int16_t *__restrict us_ptr = m_sides[side2move][m_ply].vals;
        const int16_t *__restrict them_ptr = m_sides[side2move ^ 1][m_ply].vals;
        const int16_t *__restrict weight_ptr = m_network.output_weights + bucket * 2 * HIDDEN_SIZE;
        const int16_t *__restrict weight_ptr2 =
            m_network.output_weights + bucket * 2 * HIDDEN_SIZE + HIDDEN_SIZE;

        for (size_t i = 0; i < HIDDEN_SIZE; i += 16)
        {
            const __m256i us_ = _mm256_load_si256((__m256i *)(us_ptr + i));
            const __m256i them_ = _mm256_load_si256((__m256i *)(them_ptr + i));
            const __m256i us_weights = _mm256_load_si256((__m256i *)(weight_ptr + i));
            const __m256i them_weights = _mm256_load_si256((__m256i *)(weight_ptr2 + i));

            const __m256i us_clamped = _mm256_min_epi16(_mm256_max_epi16(us_, vec_zero), vec_qa);
            const __m256i them_clamped =
                _mm256_min_epi16(_mm256_max_epi16(them_, vec_zero), vec_qa);

            // do (clamp*weight)*clamp
            const __m256i us_results =
                _mm256_madd_epi16(_mm256_mullo_epi16(us_weights, us_clamped), us_clamped);
            const __m256i them_results =
                _mm256_madd_epi16(_mm256_mullo_epi16(them_weights, them_clamped), them_clamped);

            sum = _mm256_add_epi32(sum, us_results);
            sum = _mm256_add_epi32(sum, them_results);
        }

        __m128i x128 = _mm_add_epi32(_mm256_extracti128_si256(sum, 1), _mm256_castsi256_si128(sum));
        __m128i x64 = _mm_add_epi32(x128, _mm_shuffle_epi32(x128, _MM_SHUFFLE(1, 0, 3, 2)));
        __m128i x32 = _mm_add_epi32(x64, _mm_shuffle_epi32(x64, _MM_SHUFFLE(1, 1, 1, 1)));
        output += _mm_cvtsi128_si32(x32);
#elifdef __ARM_NEON
        // Thanks gemini
        const int16_t *__restrict us = m_sides[side2move][m_ply].vals;
        const int16_t *__restrict them = m_sides[side2move ^ 1][m_ply].vals;

        const int16_t *__restrict weights1 = m_network.output_weights + bucket * 2 * HIDDEN_SIZE;
        const int16_t *__restrict weights2 =
            m_network.output_weights + bucket * 2 * HIDDEN_SIZE + HIDDEN_SIZE;

        const int16x8_t v_zero = vdupq_n_s16(0);
        const int16x8_t v_qa = vdupq_n_s16(QA);

        // 8 accumulators to fully saturate the 4 SIMD pipelines
        int32x4_t acc0 = vdupq_n_s32(0);
        int32x4_t acc1 = vdupq_n_s32(0);
        int32x4_t acc2 = vdupq_n_s32(0);
        int32x4_t acc3 = vdupq_n_s32(0);
        int32x4_t acc4 = vdupq_n_s32(0);
        int32x4_t acc5 = vdupq_n_s32(0);
        int32x4_t acc6 = vdupq_n_s32(0);
        int32x4_t acc7 = vdupq_n_s32(0);

        for (size_t i = 0; i < HIDDEN_SIZE; i += 32)
        {
            // --- LOAD & CLAMP US ---
            int16x8_t u0 = vld1q_s16(us + i);
            int16x8_t u1 = vld1q_s16(us + i + 8);
            int16x8_t u2 = vld1q_s16(us + i + 16);
            int16x8_t u3 = vld1q_s16(us + i + 24);

            int16x8_t cu0 = vminq_s16(vmaxq_s16(u0, v_zero), v_qa);
            int16x8_t cu1 = vminq_s16(vmaxq_s16(u1, v_zero), v_qa);
            int16x8_t cu2 = vminq_s16(vmaxq_s16(u2, v_zero), v_qa);
            int16x8_t cu3 = vminq_s16(vmaxq_s16(u3, v_zero), v_qa);

            // --- LOAD & CLAMP THEM ---
            int16x8_t t0 = vld1q_s16(them + i);
            int16x8_t t1 = vld1q_s16(them + i + 8);
            int16x8_t t2 = vld1q_s16(them + i + 16);
            int16x8_t t3 = vld1q_s16(them + i + 24);

            int16x8_t ct0 = vminq_s16(vmaxq_s16(t0, v_zero), v_qa);
            int16x8_t ct1 = vminq_s16(vmaxq_s16(t1, v_zero), v_qa);
            int16x8_t ct2 = vminq_s16(vmaxq_s16(t2, v_zero), v_qa);
            int16x8_t ct3 = vminq_s16(vmaxq_s16(t3, v_zero), v_qa);

            // --- WEIGHTS & MULTIPLY ---
            // Interleaving US and THEM here to help the scheduler
            int16x8_t mu0 = vmulq_s16(vld1q_s16(weights1 + i), cu0);
            int16x8_t mt0 = vmulq_s16(vld1q_s16(weights2 + i), ct0);
            int16x8_t mu1 = vmulq_s16(vld1q_s16(weights1 + i + 8), cu1);
            int16x8_t mt1 = vmulq_s16(vld1q_s16(weights2 + i + 8), ct1);
            int16x8_t mu2 = vmulq_s16(vld1q_s16(weights1 + i + 16), cu2);
            int16x8_t mt2 = vmulq_s16(vld1q_s16(weights2 + i + 16), ct2);
            int16x8_t mu3 = vmulq_s16(vld1q_s16(weights1 + i + 24), cu3);
            int16x8_t mt3 = vmulq_s16(vld1q_s16(weights2 + i + 24), ct3);

            // --- ACCUMULATE ---
            acc0 = vmlal_s16(acc0, vget_low_s16(mu0), vget_low_s16(cu0));
            acc1 = vmlal_s16(acc1, vget_high_s16(mu0), vget_high_s16(cu0));
            acc2 = vmlal_s16(acc2, vget_low_s16(mt0), vget_low_s16(ct0));
            acc3 = vmlal_s16(acc3, vget_high_s16(mt0), vget_high_s16(ct0));

            acc4 = vmlal_s16(acc4, vget_low_s16(mu1), vget_low_s16(cu1));
            acc5 = vmlal_s16(acc5, vget_high_s16(mu1), vget_high_s16(cu1));
            acc6 = vmlal_s16(acc6, vget_low_s16(mt1), vget_low_s16(ct1));
            acc7 = vmlal_s16(acc7, vget_high_s16(mt1), vget_high_s16(ct1));

            // Second half of 32
            acc0 = vmlal_s16(acc0, vget_low_s16(mu2), vget_low_s16(cu2));
            acc1 = vmlal_s16(acc1, vget_high_s16(mu2), vget_high_s16(cu2));
            acc2 = vmlal_s16(acc2, vget_low_s16(mt2), vget_low_s16(ct2));
            acc3 = vmlal_s16(acc3, vget_high_s16(mt2), vget_high_s16(ct2));

            acc4 = vmlal_s16(acc4, vget_low_s16(mu3), vget_low_s16(cu3));
            acc5 = vmlal_s16(acc5, vget_high_s16(mu3), vget_high_s16(cu3));
            acc6 = vmlal_s16(acc6, vget_low_s16(mt3), vget_low_s16(ct3));
            acc7 = vmlal_s16(acc7, vget_high_s16(mt3), vget_high_s16(ct3));
        }

        // Final sum reduction
        int32x4_t sum_l = vaddq_s32(vaddq_s32(acc0, acc1), vaddq_s32(acc2, acc3));
        int32x4_t sum_h = vaddq_s32(vaddq_s32(acc4, acc5), vaddq_s32(acc6, acc7));
        output += vaddvq_s32(vaddq_s32(sum_l, sum_h));
#else
        const int16_t *__restrict us = m_sides[side2move][m_ply].vals;
        const int16_t *__restrict them = m_sides[side2move ^ 1][m_ply].vals;

        const int16_t *__restrict weights1 = m_network.output_weights + bucket * 2 * HIDDEN_SIZE;
        const int16_t *__restrict weights2 =
            m_network.output_weights + bucket * 2 * HIDDEN_SIZE + HIDDEN_SIZE;

        for (size_t i = 0; i < HIDDEN_SIZE; ++i)
        {
            output += screlu(us[i]) * weights1[i] + screlu(them[i]) * weights2[i];
        }
#endif

        // output in QA * QB
        output /= static_cast<int32_t>(QA);
        output += static_cast<int32_t>(m_network.output_bias[bucket]);

        // output in [-SCALE, SCALE]
        output *= SCALE;
        output /= static_cast<int32_t>(QA) * static_cast<int32_t>(QB);

        if (output <= -param::NNUE_MAX)
            return -param::NNUE_MAX;

        if (output >= param::NNUE_MAX)
            return param::NNUE_MAX;

        return output;
    }

    void make_move(const chess::Board &board, const chess::Move &move)
    {
        m_ply += 1;

        // only mark the entry dirty if incorrect position
        // if (board.hash() != m_entries[m_ply].hash || move != m_entries[m_ply].move)
        // {
        // m_entries[m_ply].hash = board.hash();
        m_entries[m_ply].move = move;
        m_entries[m_ply].piece = board.at(move.from());
        m_entries[m_ply].captured = move.typeOf() == chess::Move::ENPASSANT
                                        ? board.at(move.to().ep_square())
                                        : board.at(move.to());
        m_entries[m_ply].stm = board.sideToMove();
        m_entries[m_ply].is_clean = false;
        // }
    }

    void unmake_move()
    {
        m_ply -= 1;
    }

  private:
    void catchup()
    {
        // scan back til clean entry
        int clean_ply = m_ply;
        while (!m_entries[clean_ply].is_clean)
        {
            clean_ply -= 1;
        }
        clean_ply += 1;

        // apply update
        int current_ply = m_ply;
        for (; clean_ply <= current_ply; ++clean_ply)
        {
            clone_ply(clean_ply - 1, clean_ply);
            m_ply = clean_ply;
            apply_move(m_entries[clean_ply]);
            m_entries[clean_ply].is_clean = true;
        }

        assert(current_ply == m_ply);
    }

    void apply_move(const dirty_entry &entry)
    {
        switch (entry.move.typeOf())
        {
        case chess::Move::NORMAL: {
            const chess::Piece piece = entry.piece;

            // handle capture
            const chess::Piece captured_piece = entry.captured;
            if (captured_piece != chess::Piece::NONE)
                move_piece_remove_piece(piece, entry.move.from(), entry.move.to(), captured_piece,
                                        entry.move.to());
            else
                move_piece(piece, entry.move.from(), entry.move.to());

            break;
        }
        case chess::Move::PROMOTION: {
            const chess::Piece piece = entry.piece;
            const chess::Piece promote_piece =
                chess::Piece{piece.color(), entry.move.promotionType()};
            remove_add_piece(piece, entry.move.from(), promote_piece, entry.move.to());

            // handle capture
            const chess::Piece captured_piece = entry.captured;
            if (captured_piece != chess::Piece::NONE)
                remove_piece(captured_piece, entry.move.to());
            break;
        }
        case chess::Move::ENPASSANT: {
            const chess::Piece piece = entry.piece;
            // captured pawn
            const chess::Piece enp_piece = entry.captured;
            move_piece_remove_piece(piece, entry.move.from(), entry.move.to(), enp_piece,
                                    entry.move.to().ep_square());
            break;
        }
        case chess::Move::CASTLING: {
            const chess::Square king_sq = entry.move.from();
            const chess::Square rook_sq = entry.move.to();

            const bool king_side = entry.move.to() > entry.move.from();
            const chess::Square rook_to = chess::Square::castling_rook_square(king_side, entry.stm);
            const chess::Square king_to = chess::Square::castling_king_square(king_side, entry.stm);

            const chess::Piece king_piece = entry.piece;
            const chess::Piece rook_piece = entry.captured;
            move_piece(king_piece, king_sq, king_to);
            move_piece(rook_piece, rook_sq, rook_to);
            break;
        }
        default: {
            throw std::runtime_error{"invalid move type"};
        }
        }
    }

    void add_feature(int side, int feature_idx)
    {
        const int16_t *__restrict weight = m_network.feature_weights[feature_idx].vals;
        for (size_t i = 0; i < HIDDEN_SIZE; ++i)
        {
            m_sides[side][m_ply].vals[i] += weight[i];
        }
    }

    void remove_feature(int side, int feature_idx)
    {
        const int16_t *__restrict weight = m_network.feature_weights[feature_idx].vals;
        for (size_t i = 0; i < HIDDEN_SIZE; ++i)
        {
            m_sides[side][m_ply].vals[i] -= weight[i];
        }
    }

    void add_piece(const chess::Piece &piece, const chess::Square &square)
    {
#ifdef TDCHESS_NNUE_INLINE
        // hand roll simd
        int white_feature_idx = translate(chess::Color::WHITE, piece, square);
        int black_feature_idx = translate(chess::Color::BLACK, piece, square);

        const int16_t *__restrict white_weights = m_network.feature_weights[white_feature_idx].vals;
        const int16_t *__restrict black_weights = m_network.feature_weights[black_feature_idx].vals;
        int16_t *__restrict white_values = m_sides[0][m_ply].vals;
        int16_t *__restrict black_values = m_sides[1][m_ply].vals;
        for (size_t i = 0; i < HIDDEN_SIZE; ++i)
        {
            white_values[i] += white_weights[i];
            black_values[i] += black_weights[i];
        }
#else
        int white_feature_idx = translate(chess::Color::WHITE, piece, square);
        int black_feature_idx = translate(chess::Color::BLACK, piece, square);
        add_feature(static_cast<int>(chess::Color::WHITE), white_feature_idx);
        add_feature(static_cast<int>(chess::Color::BLACK), black_feature_idx);
#endif
    }

    void remove_add_piece(const chess::Piece &removed_piece, const chess::Square &removed_square,
                          const chess::Piece &added_piece, const chess::Square &added_square)
    {
#ifdef __ARM_NEON
        int white_removed_feature_idx =
            translate(chess::Color::WHITE, removed_piece, removed_square);
        int black_removed_feature_idx =
            translate(chess::Color::BLACK, removed_piece, removed_square);
        const int16_t *__restrict white_removed_weights =
            m_network.feature_weights[white_removed_feature_idx].vals;
        const int16_t *__restrict black_removed_weights =
            m_network.feature_weights[black_removed_feature_idx].vals;

        int white_feature_idx = translate(chess::Color::WHITE, added_piece, added_square);
        int black_feature_idx = translate(chess::Color::BLACK, added_piece, added_square);

        const int16_t *__restrict white_weights = m_network.feature_weights[white_feature_idx].vals;
        const int16_t *__restrict black_weights = m_network.feature_weights[black_feature_idx].vals;

        int16_t *__restrict white_values = m_sides[0][m_ply].vals;
        int16_t *__restrict black_values = m_sides[1][m_ply].vals;

        for (size_t i = 0; i < HIDDEN_SIZE; i += 32)
        {
            // Prefetching weights only (since they are the most likely to be outside L1)
            __builtin_prefetch(white_weights + i + 64, 0, 3);
            __builtin_prefetch(white_removed_weights + i + 64, 0, 3);
            __builtin_prefetch(black_weights + i + 64, 0, 3);
            __builtin_prefetch(black_removed_weights + i + 64, 0, 3);

            // --- PHASE 1: Structured Loads ---
            // Loads 32 elements (64 bytes) per side in one instruction
            int16x8x4_t wv = vld1q_s16_x4(white_values + i);
            int16x8x4_t wa = vld1q_s16_x4(white_weights + i);
            int16x8x4_t wr = vld1q_s16_x4(white_removed_weights + i);

            int16x8x4_t bv = vld1q_s16_x4(black_values + i);
            int16x8x4_t ba = vld1q_s16_x4(black_weights + i);
            int16x8x4_t br = vld1q_s16_x4(black_removed_weights + i);

            // --- PHASE 2: Math and Interleaved Stores ---
            // We compute the delta (wa - wr) first to hide wv load latency
            int16x8x4_t w_res, b_res;

#pragma unroll
            for (int j = 0; j < 4; ++j) {
                w_res.val[j] = vaddq_s16(wv.val[j], vsubq_s16(wa.val[j], wr.val[j]));
                b_res.val[j] = vaddq_s16(bv.val[j], vsubq_s16(ba.val[j], br.val[j]));
            }

            // --- PHASE 3: Structured Stores ---
            vst1q_s16_x4(white_values + i, w_res);
            vst1q_s16_x4(black_values + i, b_res);
        }
#else
        int white_removed_feature_idx =
            translate(chess::Color::WHITE, removed_piece, removed_square);
        int black_removed_feature_idx =
            translate(chess::Color::BLACK, removed_piece, removed_square);
        const int16_t *__restrict white_removed_weights =
            m_network.feature_weights[white_removed_feature_idx].vals;
        const int16_t *__restrict black_removed_weights =
            m_network.feature_weights[black_removed_feature_idx].vals;

        int white_feature_idx = translate(chess::Color::WHITE, added_piece, added_square);
        int black_feature_idx = translate(chess::Color::BLACK, added_piece, added_square);

        const int16_t *__restrict white_weights = m_network.feature_weights[white_feature_idx].vals;
        const int16_t *__restrict black_weights = m_network.feature_weights[black_feature_idx].vals;

        int16_t *__restrict white_values = m_sides[0][m_ply].vals;
        int16_t *__restrict black_values = m_sides[1][m_ply].vals;
        for (size_t i = 0; i < HIDDEN_SIZE; ++i)
        {
            white_values[i] += white_weights[i] - white_removed_weights[i];
            black_values[i] += black_weights[i] - black_removed_weights[i];
        }
#endif
    }

    void remove_piece(const chess::Piece &piece, const chess::Square &square)
    {
#ifdef TDCHESS_NNUE_INLINE
        // hand roll simd
        int white_feature_idx = translate(chess::Color::WHITE, piece, square);
        int black_feature_idx = translate(chess::Color::BLACK, piece, square);

        const int16_t *__restrict white_weights = m_network.feature_weights[white_feature_idx].vals;
        const int16_t *__restrict black_weights = m_network.feature_weights[black_feature_idx].vals;
        int16_t *__restrict white_values = m_sides[0][m_ply].vals;
        int16_t *__restrict black_values = m_sides[1][m_ply].vals;
        for (size_t i = 0; i < HIDDEN_SIZE; ++i)
        {
            white_values[i] -= white_weights[i];
            black_values[i] -= black_weights[i];
        }
#else
        int white_feature_idx = translate(chess::Color::WHITE, piece, square);
        int black_feature_idx = translate(chess::Color::BLACK, piece, square);
        remove_feature(static_cast<int>(chess::Color::WHITE), white_feature_idx);
        remove_feature(static_cast<int>(chess::Color::BLACK), black_feature_idx);
#endif
    }

    void move_piece(const chess::Piece &piece, const chess::Square &start, const chess::Square &end)
    {
#ifdef TDCHESS_NNUE_INLINE
#ifdef __ARM_NEON
        int white_remove_feature_idx = translate(chess::Color::WHITE, piece, start);
        int black_remove_feature_idx = translate(chess::Color::BLACK, piece, start);
        int white_add_feature_idx = translate(chess::Color::WHITE, piece, end);
        int black_add_feature_idx = translate(chess::Color::BLACK, piece, end);

        const int16_t *__restrict white_remove_weights =
            m_network.feature_weights[white_remove_feature_idx].vals;
        const int16_t *__restrict white_add_weights =
            m_network.feature_weights[white_add_feature_idx].vals;
        const int16_t *__restrict black_remove_weights =
            m_network.feature_weights[black_remove_feature_idx].vals;
        const int16_t *__restrict black_add_weights =
            m_network.feature_weights[black_add_feature_idx].vals;
        int16_t *__restrict white_values = m_sides[0][m_ply].vals;
        int16_t *__restrict black_values = m_sides[1][m_ply].vals;

        for (size_t i = 0; i < HIDDEN_SIZE; i += 32)
        {
            // Prefetching the weight blocks (usually the bottleneck)
            __builtin_prefetch(white_add_weights + i + 64, 0, 3);
            __builtin_prefetch(white_remove_weights + i + 64, 0, 3);
            __builtin_prefetch(black_add_weights + i + 64, 0, 3);
            __builtin_prefetch(black_remove_weights + i + 64, 0, 3);

            // --- LOAD PHASE (Each call loads 32 elements/64 bytes) ---
            // White loads
            int16x8x4_t wv = vld1q_s16_x4(white_values + i);
            int16x8x4_t wa = vld1q_s16_x4(white_add_weights + i);
            int16x8x4_t wr = vld1q_s16_x4(white_remove_weights + i);

            // Black loads
            int16x8x4_t bv = vld1q_s16_x4(black_values + i);
            int16x8x4_t ba = vld1q_s16_x4(black_add_weights + i);
            int16x8x4_t br = vld1q_s16_x4(black_remove_weights + i);

            // --- MATH & STORE PHASE ---
            // We process the 4 sub-registers (val.val[0...3])
            // Strategy: value + (add - remove)

            // Process White
            int16x8x4_t w_res;
            w_res.val[0] = vaddq_s16(wv.val[0], vsubq_s16(wa.val[0], wr.val[0]));
            w_res.val[1] = vaddq_s16(wv.val[1], vsubq_s16(wa.val[1], wr.val[1]));
            w_res.val[2] = vaddq_s16(wv.val[2], vsubq_s16(wa.val[2], wr.val[2]));
            w_res.val[3] = vaddq_s16(wv.val[3], vsubq_s16(wa.val[3], wr.val[3]));
            vst1q_s16_x4(white_values + i, w_res);

            // Process Black
            int16x8x4_t b_res;
            b_res.val[0] = vaddq_s16(bv.val[0], vsubq_s16(ba.val[0], br.val[0]));
            b_res.val[1] = vaddq_s16(bv.val[1], vsubq_s16(ba.val[1], br.val[1]));
            b_res.val[2] = vaddq_s16(bv.val[2], vsubq_s16(ba.val[2], br.val[2]));
            b_res.val[3] = vaddq_s16(bv.val[3], vsubq_s16(ba.val[3], br.val[3]));
            vst1q_s16_x4(black_values + i, b_res);
        }
#else
        // hand roll simd
        int white_remove_feature_idx = translate(chess::Color::WHITE, piece, start);
        int black_remove_feature_idx = translate(chess::Color::BLACK, piece, start);
        int white_add_feature_idx = translate(chess::Color::WHITE, piece, end);
        int black_add_feature_idx = translate(chess::Color::BLACK, piece, end);

        const int16_t *__restrict white_remove_weights =
            m_network.feature_weights[white_remove_feature_idx].vals;
        const int16_t *__restrict white_add_weights =
            m_network.feature_weights[white_add_feature_idx].vals;
        const int16_t *__restrict black_remove_weights =
            m_network.feature_weights[black_remove_feature_idx].vals;
        const int16_t *__restrict black_add_weights =
            m_network.feature_weights[black_add_feature_idx].vals;
        int16_t *__restrict white_values = m_sides[0][m_ply].vals;
        int16_t *__restrict black_values = m_sides[1][m_ply].vals;
        for (size_t i = 0; i < HIDDEN_SIZE; ++i)
        {
            white_values[i] += white_add_weights[i] - white_remove_weights[i];
            black_values[i] += black_add_weights[i] - black_remove_weights[i];
        }
#endif
#else
        remove_piece(piece, start);
        add_piece(piece, end);
#endif
    }

    void move_piece_remove_piece(const chess::Piece &moved_piece, const chess::Square &start,
                                 const chess::Square &end, const chess::Piece &removed_piece,
                                 const chess::Square removed)
    {
#ifdef TDCHESS_NNUE_INLINE

#ifdef __ARM_NEON
        // MOVE PIECE
        int white_remove_feature_idx = translate(chess::Color::WHITE, moved_piece, start);
        int black_remove_feature_idx = translate(chess::Color::BLACK, moved_piece, start);
        int white_add_feature_idx = translate(chess::Color::WHITE, moved_piece, end);
        int black_add_feature_idx = translate(chess::Color::BLACK, moved_piece, end);
        const int16_t *__restrict white_remove_weights =
            m_network.feature_weights[white_remove_feature_idx].vals;
        const int16_t *__restrict white_add_weights =
            m_network.feature_weights[white_add_feature_idx].vals;
        const int16_t *__restrict black_remove_weights =
            m_network.feature_weights[black_remove_feature_idx].vals;
        const int16_t *__restrict black_add_weights =
            m_network.feature_weights[black_add_feature_idx].vals;

        // REMOVE PIECE
        int white_feature_idx = translate(chess::Color::WHITE, removed_piece, removed);
        int black_feature_idx = translate(chess::Color::BLACK, removed_piece, removed);
        const int16_t *__restrict white_weights = m_network.feature_weights[white_feature_idx].vals;
        const int16_t *__restrict black_weights = m_network.feature_weights[black_feature_idx].vals;

        int16_t *__restrict white_values = m_sides[0][m_ply].vals;
        int16_t *__restrict black_values = m_sides[1][m_ply].vals;

        for (size_t i = 0; i < HIDDEN_SIZE; i += 32)
        {
            // Prefetching the four weight arrays (likely the bottleneck)
            __builtin_prefetch(white_add_weights + i + 64, 0, 3);
            __builtin_prefetch(white_remove_weights + i + 64, 0, 3);
            __builtin_prefetch(black_add_weights + i + 64, 0, 3);
            __builtin_prefetch(black_remove_weights + i + 64, 0, 3);

            // --- WHITE SIDE ---
            // Load current values and the 3 weight sources
            int16x8x4_t wv = vld1q_s16_x4(white_values + i);
            int16x8x4_t wa = vld1q_s16_x4(white_add_weights + i);
            int16x8x4_t wr = vld1q_s16_x4(white_remove_weights + i);
            int16x8x4_t wc = vld1q_s16_x4(white_weights + i);

            int16x8x4_t w_res;
            // (Val + Add) - (Remove_Moved + Remove_Captured)
            w_res.val[0] = vsubq_s16(vaddq_s16(wv.val[0], wa.val[0]), vaddq_s16(wr.val[0], wc.val[0]));
            w_res.val[1] = vsubq_s16(vaddq_s16(wv.val[1], wa.val[1]), vaddq_s16(wr.val[1], wc.val[1]));
            w_res.val[2] = vsubq_s16(vaddq_s16(wv.val[2], wa.val[2]), vaddq_s16(wr.val[2], wc.val[2]));
            w_res.val[3] = vsubq_s16(vaddq_s16(wv.val[3], wa.val[3]), vaddq_s16(wr.val[3], wc.val[3]));

            // Store White immediately to free registers for Black
            vst1q_s16_x4(white_values + i, w_res);

            // --- BLACK SIDE ---
            int16x8x4_t bv = vld1q_s16_x4(black_values + i);
            int16x8x4_t ba = vld1q_s16_x4(black_add_weights + i);
            int16x8x4_t br = vld1q_s16_x4(black_remove_weights + i);
            int16x8x4_t bc = vld1q_s16_x4(black_weights + i);

            int16x8x4_t b_res;
            b_res.val[0] = vsubq_s16(vaddq_s16(bv.val[0], ba.val[0]), vaddq_s16(br.val[0], bc.val[0]));
            b_res.val[1] = vsubq_s16(vaddq_s16(bv.val[1], ba.val[1]), vaddq_s16(br.val[1], bc.val[1]));
            b_res.val[2] = vsubq_s16(vaddq_s16(bv.val[2], ba.val[2]), vaddq_s16(br.val[2], bc.val[2]));
            b_res.val[3] = vsubq_s16(vaddq_s16(bv.val[3], ba.val[3]), vaddq_s16(br.val[3], bc.val[3]));

            vst1q_s16_x4(black_values + i, b_res);
        }
#else
        // MOVE PIECE
        int white_remove_feature_idx = translate(chess::Color::WHITE, moved_piece, start);
        int black_remove_feature_idx = translate(chess::Color::BLACK, moved_piece, start);
        int white_add_feature_idx = translate(chess::Color::WHITE, moved_piece, end);
        int black_add_feature_idx = translate(chess::Color::BLACK, moved_piece, end);
        const int16_t *__restrict white_remove_weights =
            m_network.feature_weights[white_remove_feature_idx].vals;
        const int16_t *__restrict white_add_weights =
            m_network.feature_weights[white_add_feature_idx].vals;
        const int16_t *__restrict black_remove_weights =
            m_network.feature_weights[black_remove_feature_idx].vals;
        const int16_t *__restrict black_add_weights =
            m_network.feature_weights[black_add_feature_idx].vals;

        // REMOVE PIECE
        int white_feature_idx = translate(chess::Color::WHITE, removed_piece, removed);
        int black_feature_idx = translate(chess::Color::BLACK, removed_piece, removed);
        const int16_t *__restrict white_weights = m_network.feature_weights[white_feature_idx].vals;
        const int16_t *__restrict black_weights = m_network.feature_weights[black_feature_idx].vals;

        int16_t *__restrict white_values = m_sides[0][m_ply].vals;
        int16_t *__restrict black_values = m_sides[1][m_ply].vals;
        for (size_t i = 0; i < HIDDEN_SIZE; ++i)
        {
            white_values[i] += white_add_weights[i] - white_remove_weights[i] - white_weights[i];
            black_values[i] += black_add_weights[i] - black_remove_weights[i] - black_weights[i];
        }
#endif
#else
        move_piece(moved_piece, start, end);
        remove_piece(removed_piece, removed);
#endif
    }

    void clone_ply(int a, int b)
    {
#ifdef __ARM_NEON
        const int16_t* __restrict sw = m_sides[0][a].vals;
        const int16_t* __restrict sb = m_sides[1][a].vals;
        int16_t* __restrict dw = m_sides[0][b].vals;
        int16_t* __restrict db = m_sides[1][b].vals;

        for (size_t i = 0; i < HIDDEN_SIZE; i += 64)
        {
            // Prefetching even further ahead (256 bytes) because we are moving so much data
            __builtin_prefetch(sw + i + 128, 0, 3);
            __builtin_prefetch(sb + i + 128, 0, 3);

            // --- WHITE SIDE (64 bytes / 32 elements) ---
            int16x8x4_t w_block1 = vld1q_s16_x4(sw + i);      // Loads 64 bytes into 4 registers
            int16x8x4_t w_block2 = vld1q_s16_x4(sw + i + 32); // Loads next 64 bytes

            // --- BLACK SIDE (64 bytes / 32 elements) ---
            int16x8x4_t b_block1 = vld1q_s16_x4(sb + i);
            int16x8x4_t b_block2 = vld1q_s16_x4(sb + i + 32);

            // --- STORES ---
            vst1q_s16_x4(dw + i, w_block1);
            vst1q_s16_x4(dw + i + 32, w_block2);

            vst1q_s16_x4(db + i, b_block1);
            vst1q_s16_x4(db + i + 32, b_block2);
        }
#else
        for (size_t i = 0; i < HIDDEN_SIZE; ++i)
        {
            m_sides[0][b].vals[i] = m_sides[0][a].vals[i];
            m_sides[1][b].vals[i] = m_sides[1][a].vals[i];
        }
#endif
    }
};
