set -e

version="$1"
nnue_name="$2"
if [ -z $version ]
then
  echo "no version number as first argument, exiting"
  exit 1
fi

echo "moving version.h"
echo $version > version.txt
xxd -i version.txt > src/version.h

echo "building version $version"
cmake --build ./cmake-build-release --target tdchess_uci -j 10

echo "copying uci and weights to ./builds/${version}"

DIR="./builds/${version}"
if [ -d "$DIR" ]; then
    echo "Directory $DIR exists. Deleting..."
    rm -ri "$DIR"

#    rm -ri "./builds/${version}_linux.zip"
fi

mkdir ./builds/${version}
cp ./cmake-build-release/tdchess_uci ./builds/${version}/tdchess
cp ./nets/${nnue_name}.bin ./builds/${version}/nnue.bin

#echo "zipping into ./builds/${version}_linux.zip"
#zip -r ./builds/${version}_linux.zip ./builds/${version}

echo "done with version $version"