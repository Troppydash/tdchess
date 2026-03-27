# TDChess

## Dependencies
Build libraries
- Fathom for endgame tables
- chess-library for chess board

Data sources
- Syzygy database: http://tablebase.sesse.net/syzygy/3-4-5/

## How to build

CMake config
```bash
make config_gen
```

Test build and run on a list of sample positions.
```bash
make test
```

UCI sprt build. This generates `./builds/<ver>/tdchess` as an uci executable.
```bash
./build.sh <ver>
```

UCI pgo build (Also need to uncomment the CAREPGO lines in `CMakeLists.txt` and re-config-gen). This generates `./pgo/<exe>` as an pgo optimized uci executable.
```bash
make EXE=./pgo/<exe>
```

## License
This project is licensed under the GNU General Public License v3.0 - see the [COPYING](COPYING) file for details