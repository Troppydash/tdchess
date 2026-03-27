# EXE=name

test:
	cmake -DCMAKE_BUILD_TYPE=Debug -G Ninja -S . -B ./cmake-build-release
	cmake --build ./cmake-build-release --target tdchess_test
	./cmake-build-release/tdchess_test
	
uci_build:
	cmake -DCMAKE_BUILD_TYPE=Release -G Ninja -S . -B ./cmake-build-release
	cmake --build ./cmake-build-release --target tdchess_uci
	
config_gen:
	cmake -DCMAKE_BUILD_TYPE=Release -G Ninja -S . -B ./cmake-build-release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
	ln -s cmake-build-release/compile_commands.json .

pgo_build:
	mkdir -p cmake-build-release
	cmake -DCMAKE_BUILD_TYPE=Release -G Ninja -S . -B ./cmake-build-release -DCARE_PGO=on -DPGOGEN=on
	cmake --build ./cmake-build-release --target tdchess_uci
	rm -f ./cmake-build-release/*.profraw
	rm -f ./cmake-build-release/*.profdata

	./cmake-build-release/tdchess_uci pgo

	mv *.profraw ./cmake-build-release
	xcrun llvm-profdata merge -output=./cmake-build-release/tdchess_uci.profdata ./cmake-build-release/*.profraw

	cmake -DCMAKE_BUILD_TYPE=Release -G Ninja -S . -B ./cmake-build-release -DCARE_PGO=on -DPGOGEN=off
	cmake --build ./cmake-build-release --target tdchess_uci

	# copy file to exe
	cp ./cmake-build-release/tdchess_uci $(EXE)

log_pgo:
	xcrun llvm-profdata show --counts --all-functions ./cmake-build-release/*.profraw