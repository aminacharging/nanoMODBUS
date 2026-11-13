set -e

# Same flags as we use in the main firmware for compilation
DEFS="-DNMBS_CLIENT_DISABLED -DNMBS_SERVER_READ_COILS_DISABLED -DNMBS_SERVER_READ_DISCRETE_INPUTS_DISABLED -DNMBS_SERVER_WRITE_SINGLE_COIL_DISABLED -DNMBS_SERVER_WRITE_MULTIPLE_COILS_DISABLED -DNMBS_SERVER_READ_FILE_RECORD_DISABLED -DNMBS_SERVER_WRITE_FILE_RECORD_DISABLED -DNMBS_SERVER_READ_WRITE_REGISTERS_DISABLED -DNMBS_SERVER_READ_DEVICE_IDENTIFICATION_DISABLED"
SAN="-g -O1 -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined"

clang   $SAN $DEFS -I.. -c ../nanomodbus.c -o nanomodbus.o
clang++ $SAN $DEFS -I.. fuzz_server.cpp nanomodbus.o -o fuzz_server

mkdir -p corpus

./fuzz_server -max_len=1024 -max_total_time=30 corpus/

if [ "$1" = "coverage" ]; then
    COV="-g -O0 -fsanitize=fuzzer -fprofile-instr-generate -fcoverage-mapping"

    clang   $COV $DEFS -I.. -c ../nanomodbus.c -o nanomodbus_cov.o
    clang++ $COV $DEFS -I.. fuzz_server.cpp nanomodbus_cov.o -o fuzz_server_cov

    LLVM_PROFILE_FILE=cov.profraw ./fuzz_server_cov -runs=0 corpus/
    llvm-profdata-18 merge -sparse cov.profraw -o cov.profdata

    llvm-cov-18 report ./fuzz_server_cov -instr-profile=cov.profdata -show-functions ../nanomodbus.c
    llvm-cov-18 show ./fuzz_server_cov -instr-profile=cov.profdata -format=html -output-dir=cov_html ../nanomodbus.c

    # `bash run.sh coverage <function>`: line hit counts of one function, e.g. recv_read_registers_res
    if [ -n "$2" ]; then
        llvm-cov-18 show ./fuzz_server_cov -instr-profile=cov.profdata -name-regex="(^|:)$2\$" ../nanomodbus.c
    fi
fi
