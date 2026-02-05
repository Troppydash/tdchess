#pragma once

#include "param.h"
#include <fstream>
#include <immintrin.h>

#ifdef __AVX2__
#define TDCHESS_NNUE_SIMD
#endif

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
    alignas(64) int16_t output_weights[BUCKET_SIZE][2 * HIDDEN_SIZE];
    // QA * QB quant, 1 -> 1
    int16_t output_bias[BUCKET_SIZE];
};

inline int32_t screlu(int16_t x)
{
    const int32_t val = std::clamp(x, static_cast<int16_t>(0), QA);
    return val * val;
}

class nnue
{
  private:
    network m_network{};
    accumulator m_sides[2][param::MAX_DEPTH]{};
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

    [[nodiscard]] int32_t evaluate(int side2move, int bucket) const
    {
        int32_t output = 0;

#ifdef TDCHESS_NNUE_SIMD
        // SIMD VECTORIZATION
        const __m256i vec_zero = _mm256_setzero_si256();
        const __m256i vec_qa = _mm256_set1_epi16(QA);
        __m256i sum = vec_zero;

        const int16_t *__restrict us_ptr = m_sides[side2move][m_ply].vals;
        const int16_t *__restrict them_ptr = m_sides[side2move ^ 1][m_ply].vals;
        const int16_t *__restrict weight_ptr = m_network.output_weights[bucket];
        const int16_t *__restrict weight_ptr2 = m_network.output_weights[bucket] + HIDDEN_SIZE;

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
        for (size_t i = 0; i < HIDDEN_SIZE; ++i)
        {
            auto &us = m_sides[side2move][m_ply];
            auto &them = m_sides[side2move ^ 1][m_ply];
            output += screlu(us.vals[i]) * m_network.output_weights[bucket][i];
            output += screlu(them.vals[i]) * m_network.output_weights[bucket][HIDDEN_SIZE + i];
        }
#endif

        // output in QA * QB
        output /= static_cast<int32_t>(QA);
        output += static_cast<int32_t>(m_network.output_bias[bucket]);

        // output in [-SCALE, SCALE]
        output *= SCALE;
        output /= static_cast<int32_t>(QA) * static_cast<int32_t>(QB);

        return std::clamp(output, -param::NNUE_MAX, param::NNUE_MAX);
    }

    void make_move(const chess::Board &board, const chess::Move &move)
    {
        clone_ply();

        switch (move.typeOf())
        {
        case chess::Move::NORMAL: {
            const chess::Piece piece = board.at(move.from());
            move_piece(piece, move.from(), move.to());

            // handle capture
            const chess::Piece captured_piece = board.at(move.to());
            if (captured_piece != chess::Piece::NONE)
                remove_piece(captured_piece, move.to());
            break;
        }
        case chess::Move::PROMOTION: {
            const chess::Piece piece = board.at(move.from());
            const chess::Piece promote_piece = chess::Piece{piece.color(), move.promotionType()};
            remove_piece(piece, move.from());
            add_piece(promote_piece, move.to());

            // handle capture
            const chess::Piece captured_piece = board.at(move.to());
            if (captured_piece != chess::Piece::NONE)
                remove_piece(captured_piece, move.to());
            break;
        }
        case chess::Move::ENPASSANT: {
            const chess::Piece piece = board.at(move.from());
            move_piece(piece, move.from(), move.to());

            // captured pawn
            const chess::Piece enp_piece = board.at(move.to().ep_square());
            remove_piece(enp_piece, move.to().ep_square());
            break;
        }
        case chess::Move::CASTLING: {
            const chess::Square king_sq = move.from();
            const chess::Square rook_sq = move.to();

            const bool king_side = move.to() > move.from();
            const chess::Square rook_to =
                chess::Square::castling_rook_square(king_side, board.sideToMove());
            const chess::Square king_to =
                chess::Square::castling_king_square(king_side, board.sideToMove());

            const chess::Piece king_piece = board.at(king_sq);
            const chess::Piece rook_piece = board.at(rook_sq);
            move_piece(king_piece, king_sq, king_to);
            move_piece(rook_piece, rook_sq, rook_to);
            break;
        }
        default: {
            throw std::runtime_error{"invalid move type"};
        }
        }
    }

    void unmake_move()
    {
        m_ply -= 1;
    }

  private:
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
#ifdef TDCHESS_NNUE_SIMD
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
#ifdef TDCHESS_NNUE_SIMD
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
#ifdef TDCHESS_NNUE_SIMD
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

    void clone_ply()
    {
        for (size_t i = 0; i < HIDDEN_SIZE; ++i)
        {
            m_sides[0][m_ply + 1].vals[i] = m_sides[0][m_ply].vals[i];
            m_sides[1][m_ply + 1].vals[i] = m_sides[1][m_ply].vals[i];
        }

        m_ply += 1;
    }
};
