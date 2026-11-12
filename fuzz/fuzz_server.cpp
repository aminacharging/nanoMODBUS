#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdio.h>

#include "nanomodbus.h"

namespace {

constexpr uint8_t RTU_ADDRESS = 1;
constexpr uint16_t REGISTER_COUNT = 0x200;

struct fuzz_state_t {
    nmbs_t* nmbs;
    const uint8_t* data;
    size_t size;
    size_t pos;
    uint16_t registers[REGISTER_COUNT];
};

// ASan can't see writes past msg.buf that land in msg.buf_idx (same object), so check every read target here
void check_inside_msg_buf(const fuzz_state_t& state, const uint8_t* buf, uint16_t count) {
    const uint8_t* const begin = state.nmbs->msg.buf;
    const uint8_t* const end = begin + sizeof(state.nmbs->msg.buf);
    if (buf >= begin && buf <= end)
        assert(buf + count <= end && "read past nmbs_t::msg.buf");
}

int32_t fuzz_read(uint8_t* buf, uint16_t count, int32_t /* byte_timeout_ms */, void* arg) {
    fuzz_state_t& state = *static_cast<fuzz_state_t*>(arg);
    check_inside_msg_buf(state, buf, count);

    const size_t left = state.size - state.pos;
    const size_t n = count < left ? count : left;
    memcpy(buf, state.data + state.pos, n);
    state.pos += n;
    return (int32_t) n;
}

int32_t fuzz_write(const uint8_t* buf, uint16_t count, int32_t /* byte_timeout_ms */, void* /* arg */) {
    // Touch every byte so ASan flags responses built from out-of-bounds memory
    volatile uint8_t sink = 0;
    for (uint16_t i = 0; i < count; i++)
        sink ^= buf[i];
    (void) sink;
    return count;
}

// Most of the messages would be totally random and CRC would not work for them, but it would block the whole flow, that's why return 0.
uint16_t fuzz_crc_calc(const uint8_t* /* data */, uint32_t /* length */, void* /* arg */) {
    return 0;
}

bool registers_in_range(uint16_t address, uint16_t quantity) {
    return (uint32_t) address + quantity <= REGISTER_COUNT;
}

nmbs_error read_registers(uint16_t address, uint16_t quantity, uint16_t* registers_out, uint8_t /* unit_id */,
                          void* arg) {
    // The library must already have rejected quantities the spec doesn't allow
    assert(quantity >= 1 && quantity <= 125);
    fuzz_state_t& state = *static_cast<fuzz_state_t*>(arg);
    if (!registers_in_range(address, quantity))
        return NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
    memcpy(registers_out, &state.registers[address], quantity * sizeof(uint16_t));
    return NMBS_ERROR_NONE;
}

nmbs_error write_single_register(uint16_t address, uint16_t value, uint8_t /* unit_id */, void* arg) {
    fuzz_state_t& state = *static_cast<fuzz_state_t*>(arg);
    if (address >= REGISTER_COUNT)
        return NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
    state.registers[address] = value;
    return NMBS_ERROR_NONE;
}

nmbs_error write_multiple_registers(uint16_t address, uint16_t quantity, const uint16_t* registers,
                                    uint8_t /* unit_id */, void* arg) {
    assert(quantity >= 1 && quantity <= 123);
    fuzz_state_t& state = *static_cast<fuzz_state_t*>(arg);
    if (!registers_in_range(address, quantity))
        return NMBS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
    memcpy(&state.registers[address], registers, quantity * sizeof(uint16_t));
    return NMBS_ERROR_NONE;
}

}    // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 1)
        return 0;

    nmbs_t nmbs;
    fuzz_state_t state = {};
    state.nmbs = &nmbs;
    state.data = data + 1;
    state.size = size - 1;

    nmbs_platform_conf platform_conf;
    nmbs_platform_conf_create(&platform_conf);
    platform_conf.transport = (data[0] & 1) ? NMBS_TRANSPORT_TCP : NMBS_TRANSPORT_RTU;
    platform_conf.read = fuzz_read;
    platform_conf.write = fuzz_write;
    if (platform_conf.transport == NMBS_TRANSPORT_RTU)
        platform_conf.crc_calc = fuzz_crc_calc;
    platform_conf.arg = &state;

    nmbs_callbacks callbacks;
    nmbs_callbacks_create(&callbacks);
    callbacks.read_holding_registers = read_registers;
    callbacks.read_input_registers = read_registers;
    callbacks.write_single_register = write_single_register;
    callbacks.write_multiple_registers = write_multiple_registers;
    callbacks.arg = &state;

    if (nmbs_server_create(&nmbs, RTU_ADDRESS, &platform_conf, &callbacks) != NMBS_ERROR_NONE)
        return 0;
    nmbs_set_read_timeout(&nmbs, 0);
    nmbs_set_byte_timeout(&nmbs, 0);

    // Several frames per input, so state left over from a previous (bad) frame is exercised too.
    // Stop once a poll consumes nothing, otherwise an exhausted input would spin forever
    for (int i = 0; i < 64 && state.pos < state.size; i++) {
        const size_t pos_before = state.pos;
        nmbs_server_poll(&nmbs);
        assert(nmbs.msg.buf_idx <= sizeof(nmbs.msg.buf));
        if (state.pos == pos_before)
            break;
    }

    return 0;
}
