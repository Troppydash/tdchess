# TDChess


## Dependencies
External libraries
- Fathom for endgame tables
- chess-library for chess board
- boost for elo tests
- chess-polyglot-reader

Data sources
- Opening book: https://komodochess.com/pub/Komodo3-book.zip
- Syzygy database: http://tablebase.sesse.net/syzygy/3-4-5/
- NNUE training: https://huggingface.co/datasets/linrock/test80-2024/blob/main/test80-2024-06-jun-2tb7p.min-v2.v6.binpack.zst

Opening books
- For running: https://sites.google.com/site/computerschess/perfect-2023-books
- For testing: https://www.chessprogramming.net/new-version-of-the-baron-v3-43-plus-the-barons-polyglot-opening-book/

Code references
- Fishtest GSPRT: https://github.com/official-stockfish/fishtest/blob/master/server/fishtest/stats/sprt.py

## Elo test

Notations
- LOS shows probability of improvement.
- Expected elo is the expected logistic elo gain
- Default settings are: elo0 = 0, elo1 = 5, alpha = 5%, beta = 5%
- Uses the SPRT test, code copied from stockfish fishtest

Process
1. Run elo using short time control
2. Aim for around 100 games
3. Accept if LLR > b, Reject if LLR < a

## License
This project is licensed under the GNU General Public License v3.0 - see the [COPYING](COPYING) file for details