// #pragma once
//
// namespace Cuckoo
// {
//
// inline int h1(uint64_t key)
// {
//     return key & 0x1fff;
// }
//
// inline int h2(uint64_t key)
// {
//     return (key >> 16) & 0x1fff;
// }
//
// uint64_t keys[8192];
// chess::Move moves[8192];
//
// inline void init() {
//
//     memset(keys, 0, sizeof(keys));
//     memset(moves, 0, sizeof(moves));
//
//     // Only used to check integrity of cuckoo tables
//     int count = 0;
//
//     for (chess::PieceType pt : {chess::PieceType::KNIGHT, chess::PieceType::BISHOP, chess::PieceType::ROOK, chess::PieceType::QUEEN, chess::PieceType::KING}) {
//         for (chess::Color color : {chess::Color::WHITE, chess::Color::BLACK}) {
//
//             chess::Piece piece{color, pt};
//
//             for (chess::Square s1 = chess::Square::SQ_A1; s1 < 64; ++s1) {
//                 for (chess::Square s2 = s1+1; s2 < 64; ++s2) {
//                     if (getPieceAttacks(pt, s1, 0) & s2) {
//
//                         Move move = createMove(s1, s2, MT_NORMAL);
//
//                         Key key = ZOBRIST_PSQ[piece][s1] ^ ZOBRIST_PSQ[piece][s2] ^ ZOBRIST_TEMPO;
//
//                         int slot = h1(key);
//
//                         while (true) {
//                             std::swap(keys[slot], key);
//                             std::swap(moves[slot], move);
//
//                             if (!move)
//                                 break;
//
//                             // Use the other slot
//                             slot = (slot == h1(key)) ? h2(key) : h1(key);
//                         }
//
//                         count++;
//                     }
//                 }
//             }
//         }
//     }
//
//     if (count != 3668) {
//         std::cout << "oops! cuckoo table is broken." << std::endl;
//         exit(-1);
//     }
// }
// } // namespace Cuckoo