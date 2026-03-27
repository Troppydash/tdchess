set -ex

version="$1"

mkdir -p ./builds/${version}
make config_gen
make pgo_build EXE=./builds/${version}/tdchess_pgo