set -ex

NEW="1.8.15"
mkdir -p ./logs/${NEW}_sf
./fastchess \
  -engine cmd=../builds/${NEW}/tdchess name=${NEW}  option.DrawContempt=0 option.MoveOverhead=5 option.Hash=32 option.SyzygyPath=/Users/troppydash/Downloads/syzygy \
  -engine cmd=/Users/troppydash/Downloads/stockfish/stockfish-macos-m1-apple-silicon name=SF option.Hash=32  \
  -openings file=UHO_Lichess_4852_v1.epd format=epd order=random \
  -each tc=10+0.2 \
  -resign movecount=3 score=600 -draw movenumber=40 movecount=6 score=20 \
  -sprt elo0=0 elo1=5 alpha=0.15 beta=0.15 \
  -rounds 1000 -concurrency 7 -pgnout notation=san nodes=true file=./logs/${NEW}_sf/games.pgn append=false \
  -show-latency -recover -ratinginterval 1 \
  -log file=./logs/${NEW}_sf/debug.log append=false realtime=true engine=true level=warn