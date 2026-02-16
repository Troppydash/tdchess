#pragma once

#include "param.h"
#include <fstream>

#ifdef __AVX2__
#define TDCHESS_NNUE_SIMD
#include <immintrin.h>
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

struct alignas(64) network
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

    [[nodiscard]] int16_t evaluate(int side2move, int bucket)
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
#else
        const int16_t *__restrict us = m_sides[side2move][m_ply].vals;
        const int16_t *__restrict them = m_sides[side2move ^ 1][m_ply].vals;

        const int16_t *__restrict weights1 = m_network.output_weights + bucket * 2 * HIDDEN_SIZE;
        const int16_t *__restrict weights2 = m_network.output_weights + bucket * 2 * HIDDEN_SIZE + HIDDEN_SIZE;

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
            remove_piece(piece, entry.move.from());
            add_piece(promote_piece, entry.move.to());

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
#else
        move_piece(moved_piece, start, end);
        remove_piece(removed_piece, removed);
#endif
    }

    void clone_ply(int a, int b)
    {
        for (size_t i = 0; i < HIDDEN_SIZE; ++i)
        {
            m_sides[0][b].vals[i] = m_sides[0][a].vals[i];
            m_sides[1][b].vals[i] = m_sides[1][a].vals[i];
        }
    }
};
