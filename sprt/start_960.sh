set -ex

NEW="1.8.0"
OLD="1.8.0-alt"
mkdir -p ./logs/${NEW}_${OLD}
./fastchess \
  -engine cmd=../builds/${NEW}/tdchess name=${NEW} option.MoveOverhead=5 option.Hash=16 option.SyzygyPath=/Users/troppydash/Downloads/syzygy \
  -engine cmd=../builds/${OLD}/tdchess name=${OLD} option.MoveOverhead=5 option.Hash=16 option.SyzygyPath=/Users/troppydash/Downloads/syzygy \
  -openings file=chess960_book_3moves.pgn format=pgn order=random \
  -each tc=10+0.2 \
  -variant fischerandom \
  -resign movecount=3 score=600 -draw movenumber=40 movecount=6 score=20 \
  -sprt elo0=0 elo1=5 alpha=0.15 beta=0.15 \
  -rounds 1000 -concurrency 1 -pgnout notation=san nodes=true file=./logs/${NEW}_${OLD}/games.pgn append=false \
  -show-latency  -recover -ratinginterval 1 \
  -log file=./logs/${NEW}_${OLD}/debug.log append=false realtime=true engine=true level=warn