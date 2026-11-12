# Same flags as we use in the main firmware for compilation
DEFS="-DNMBS_CLIENT_DISABLED -DNMBS_SERVER_READ_COILS_DISABLED -DNMBS_SERVER_READ_DISCRETE_INPUTS_DISABLED -DNMBS_SERVER_WRITE_SINGLE_COIL_DISABLED -DNMBS_SERVER_WRITE_MULTIPLE_COILS_DISABLED -DNMBS_SERVER_READ_FILE_RECORD_DISABLED -DNMBS_SERVER_WRITE_FILE_RECORD_DISABLED -DNMBS_SERVER_READ_WRITE_REGISTERS_DISABLED -DNMBS_SERVER_READ_DEVICE_IDENTIFICATION_DISABLED"
SAN="-g -O1 -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined"

clang   $SAN $DEFS -I.. -c ../nanomodbus.c -o nanomodbus.o
clang++ $SAN $DEFS -I.. fuzz_server.cpp nanomodbus.o -o fuzz_server

mkdir -p corpus

./fuzz_server -max_len=1024 -max_total_time=20 corpus/