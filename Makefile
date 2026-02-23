# EXE=name

build:
	mkdir -p cmake-build-release
	cmake -DCMAKE_BUILD_TYPE=Release -G Ninja -S . -B ./cmake-build-release -DPGOGEN=on
	cmake --build ./cmake-build-release --target tdchess_uci
	rm -f ./cmake-build-release/*.profraw
	rm -f ./cmake-build-release/*.profdata
	./cmake-build-release/tdchess_uci pgo
	mv *.profraw ./cmake-build-release
	xcrun llvm-profdata merge -output=./cmake-build-release/tdchess_uci.profdata ./cmake-build-release/*.profraw

	cmake -DCMAKE_BUILD_TYPE=Release -G Ninja -S . -B ./cmake-build-release -DPGOGEN=off
	cmake --build ./cmake-build-release --target tdchess_uci

	# copy file to exe
	cp ./cmake-build-release/tdchess_uci $(EXE)
