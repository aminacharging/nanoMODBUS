# nanoMODBUS server fuzzing

libFuzzer harness for the nanoMODBUS server, built with the same defines as the firmware
(`app/cmake/nanomodbus.cmake`): server only, FC 3, 4, 6 and 16.

Requires clang (`sudo apt install clang`). Run all commands from this directory.

## Input format

- Byte 0: bit 0 selects the transport, `0` = RTU, `1` = TCP.
- The rest is the byte stream on the wire. One input can contain several frames.
- RTU: the CRC check is replaced by a fake one; frames ending in `00 00` pass.

## Build

```bash
DEFS="-DNMBS_CLIENT_DISABLED -DNMBS_SERVER_READ_COILS_DISABLED -DNMBS_SERVER_READ_DISCRETE_INPUTS_DISABLED -DNMBS_SERVER_WRITE_SINGLE_COIL_DISABLED -DNMBS_SERVER_WRITE_MULTIPLE_COILS_DISABLED -DNMBS_SERVER_READ_FILE_RECORD_DISABLED -DNMBS_SERVER_WRITE_FILE_RECORD_DISABLED -DNMBS_SERVER_READ_WRITE_REGISTERS_DISABLED -DNMBS_SERVER_READ_DEVICE_IDENTIFICATION_DISABLED"
SAN="-g -O1 -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined"

clang   $SAN $DEFS -I.. -c ../nanomodbus.c -o nanomodbus.o
clang++ $SAN $DEFS -I.. fuzz_server.cpp nanomodbus.o -o fuzz_server
```

`run.sh` does the build and a 20 second run.

## Run

```bash
mkdir -p corpus
./fuzz_server -max_len=1024 corpus/                    # until Ctrl+C
./fuzz_server -max_len=1024 -max_total_time=600 corpus/ # 10 minutes
./fuzz_server -max_len=1024 -fork=4 corpus/            # 4 processes in parallel
```

New interesting inputs are saved to `corpus/`. Coverage stops growing quickly (`cov:` in the
output), so 30–60 minutes with `-fork` after a library change is enough.

## Crashes

A crash is saved as `crash-<hash>`. To reproduce it:

```bash
./fuzz_server crash-<hash>
xxd crash-<hash>      # show the input bytes
```

Keep crash inputs after the fix and re-run them after library updates.

## Coverage report

```bash
COV="-g -O0 -fsanitize=fuzzer -fprofile-instr-generate -fcoverage-mapping"
clang   $COV $DEFS -I.. -c ../nanomodbus.c -o nanomodbus_cov.o
clang++ $COV $DEFS -I.. fuzz_server.cpp nanomodbus_cov.o -o fuzz_server_cov

LLVM_PROFILE_FILE=cov.profraw ./fuzz_server_cov -runs=0 corpus/
llvm-profdata-18 merge -sparse cov.profraw -o cov.profdata

llvm-cov-18 report ./fuzz_server_cov -instr-profile=cov.profdata ../nanomodbus.c
llvm-cov-18 show ./fuzz_server_cov -instr-profile=cov.profdata -format=html -output-dir=cov_html ../nanomodbus.c
```

Open `cov_html/index.html`; red lines were never reached.
