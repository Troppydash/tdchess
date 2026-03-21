# EXE=name

build:
	mkdir -p cmake-build-release
	cmake -DCMAKE_BUILD_TYPE=Release -G Ninja -S . -B ./cmake-build-release -DCARE_PGO=on -DPGOGEN=on
	cmake --build ./cmake-build-release --target tdchess_uci
	rm -f ./cmake-build-release/*.profraw
	rm -f ./cmake-build-release/*.profdata

	# TODO: try to make this work
	#./pgo/fastchess \
#		-engine cmd=./cmake-build-release/tdchess_uci name=A option.MoveOverhead=5 option.Hash=16 option.SyzygyPath=/Users/troppydash/Downloads/syzygy \
#		-engine cmd=./builds/1.7.14/tdchess name=B option.MoveOverhead=5 option.Hash=16 option.SyzygyPath=/Users/troppydash/Downloads/syzygy \
#		-openings file=./pgo/UHO_Lichess_4852_v1.epd format=epd order=random \
#        -each tc=2+0.1 \
#        -resign movecount=3 score=600 -draw movenumber=40 movecount=6 score=20 \
#        -rounds 1
	./cmake-build-release/tdchess_uci pgo

	mv *.profraw ./cmake-build-release
	xcrun llvm-profdata merge -output=./cmake-build-release/tdchess_uci.profdata ./cmake-build-release/*.profraw

	cmake -DCMAKE_BUILD_TYPE=Release -G Ninja -S . -B ./cmake-build-release -DCARE_PGO=on -DPGOGEN=off
	cmake --build ./cmake-build-release --target tdchess_uci

	# copy file to exe
	cp ./cmake-build-release/tdchess_uci $(EXE)

readpgo:
	xcrun llvm-profdata show --counts --all-functions ./cmake-build-release/*.profraw