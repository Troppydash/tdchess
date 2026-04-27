set -ex

NEW="1.9.12"
OLD="1.9.11"
mkdir -p ./logs/${NEW}_${OLD}
./fastchess \
  -engine cmd=../builds/${NEW}/tdchess name=${NEW} option.DrawContempt=0 option.MoveOverhead=5 option.Hash=32 option.SyzygyPath=/Users/troppydash/Downloads/syzygy \
  -engine cmd=../builds/${OLD}/tdchess name=${OLD} option.DrawContempt=0 option.MoveOverhead=5 option.Hash=32 option.SyzygyPath=/Users/troppydash/Downloads/syzygy \
  -openings file=UHO_Lichess_4852_v1.epd format=epd order=random \
  -each tc=15+0.2 \
  -resign movecount=3 score=600 -draw movenumber=40 movecount=6 score=20 \
  -sprt elo0=0 elo1=5 alpha=0.15 beta=0.15 \
  -rounds 100000 -concurrency 8 -pgnout notation=san nodes=true file=./logs/${NEW}_${OLD}/games.pgn append=false \
  -show-latency  -recover -ratinginterval 1 \
  -log file=./logs/${NEW}_${OLD}/debug.log append=false realtime=true engine=true level=warn
