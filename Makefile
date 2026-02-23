# EXE=name

build:
	mkdir -p cmake-build-release
	cmake -DCMAKE_BUILD_TYPE=Release -G Ninja -S . -B ./cmake-build-release
	cmake --build ./cmake-build-release --target tdchess_uci
	# copy file to exe
	cp ./cmake-build-release/tdchess_uci $(EXE)
