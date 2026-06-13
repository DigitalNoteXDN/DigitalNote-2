mkdir build
cd build

echo "--- Compile ---"

cmake ..

make
#make VERBOSE=1

cmake --install . --prefix "../../../libs/mnemonic"

echo "--- RUN ---"

cd ../../../libs/mnemonic/bin
./Mnemonic