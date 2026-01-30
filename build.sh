
version="$1"
if [ -z $version ]
then
  echo "no version number as first argument, exiting"
  exit 1
fi

echo "building version $version"
cmake --build ./cmake-build-release --target tdchess_uci -j 10

echo "copying uci and weights to ./builds/${version}"
mkdir ./builds/${version}
cp ./cmake-build-release/tdchess_uci ./builds/${version}/tdchess
cp ./nets/${version}.bin ./builds/${version}/nnue.bin

echo "done with version $version"