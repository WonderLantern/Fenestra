"""Execute original SoundService instructions; requires a user-supplied image.

No firmware bytes are embedded or exported. Codec/DMA operations are mocked
at the documented AudioIF boundary; this is not an audible hardware test.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from platform_profiles import AUDIO_PROFILES, BASE, ROOT, load

LAYOUTS = {
    "zr-c110": dict(factory=0x40f36690, pool_allocate=0x41382674,
        allocate=0x40f41084, release=0x40f410c4, copy=0x41485d80,
        enqueue=0x40f40da4, state_address=0xa03403890,
        lower_config=0x40f35f10, lower_start=0x40f35ffc, lower_pcm=0x40f36144,
        lower_close=0x40f3641c, audio_submit=0x40e837f8, raw_pcm=0x40f377d4,
        native_stop_result=4, test_config=0x442e2880),
    "z9-c532": dict(factory=0x40d262cc, pool_allocate=0x4118b5cc,
        allocate=0x40d30458, release=0x40d30498, copy=0x4127f880,
        enqueue=0x40d30178, state_address=0xa032195f0,
        lower_config=0x40d25ba0, lower_start=0x40d25c88, lower_pcm=0x40d25dd0,
        lower_close=0x40d26068, audio_submit=0x40c81618, raw_pcm=0x40d27420,
        native_stop_result=0, test_config=0x44bbdd10),
}


def verify(target, firmware):
    from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE, UC_HOOK_MEM_UNMAPPED
    from unicorn.arm64_const import UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2
    from unicorn.arm64_const import UC_ARM64_REG_LR, UC_ARM64_REG_PC, UC_ARM64_REG_SP, UC_ARM64_REG_CPACR_EL1
    profile, image = load(target, firmware)
    a, layout = AUDIO_PROFILES[target], LAYOUTS[target]
    uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    uc.mem_map(BASE, (len(image) + 4095) & ~4095)
    uc.mem_write(BASE, image)
    trap, ack, pcm_callback = 0x70000000, 0x70000100, 0x70000200
    uc.mem_map(trap, 4096)
    uc.mem_map(0x70100000, 0x100000)
    heap, heap_end = 0x71000000, 0x72000000
    uc.mem_map(heap, heap_end - heap)
    uc.reg_write(UC_ARM64_REG_CPACR_EL1, 3 << 20)
    pending, audio_pending, replies, periods, live, freed, checks = [], [], [], [], set(), [], []
    lower_calls = []

    def read(address, count):
        return bytes(uc.mem_read(address, count))

    def unpack(fmt, address):
        return struct.unpack(fmt, read(address, struct.calcsize(fmt)))

    def store(fmt, address, *values):
        uc.mem_write(address, struct.pack(fmt, *values))

    def missing(_uc, _access, address, size, _value, _context):
        if not 0xa00000000 <= address < 0xa06000000:
            raise AssertionError(f"unexpected original memory access {address:#x}")
        first, last = address & ~4095, (address + size + 4095) & ~4095
        uc.mem_map(first, last - first)
        return True

    def malloc(size):
        nonlocal heap
        result = heap
        heap += (size + 63) & ~63
        assert 0 < size and heap <= heap_end
        live.add(result)
        return result

    def ret(value=0):
        uc.reg_write(UC_ARM64_REG_X0, value)
        uc.reg_write(UC_ARM64_REG_PC, uc.reg_read(UC_ARM64_REG_LR))

    def hook(_uc, address, _size, _context):
        x0, x1, x2 = (uc.reg_read(r) for r in (UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2))
        if address in (layout["allocate"], layout["pool_allocate"]):
            ret(malloc(x0))
        elif address == layout["release"]:
            assert x0 in live, f"original freed an unowned/still-referenced request {x0:#x}"
            live.remove(x0)
            freed.append(x0)
            ret()
        elif address == layout["copy"]:
            uc.mem_write(x0, read(x1, x2))
            ret(x0)
        elif address == layout["enqueue"]:
            assert unpack("<Q", x1 + 16)[0] == x1
            pending.append(x1)
            ret(1)
        elif address == layout["audio_submit"]:
            audio_pending.append(x0)
            ret(1)  # Only asynchronous native STOP reaches this mocked boundary.
        elif address in (layout["lower_config"], layout["lower_start"], layout["lower_pcm"], layout["lower_close"]):
            lower_calls.append(address)
            ret(1)
        elif address == ack:
            replies.append((x0, x1))
            ret()
        elif address == pcm_callback:
            periods.append((x0, read(x1, 24)))  # Copy original stack payload now.
            ret()

    uc.hook_add(UC_HOOK_MEM_UNMAPPED, missing)
    uc.hook_add(UC_HOOK_CODE, hook)

    def run(address, *args):
        uc.reg_write(UC_ARM64_REG_SP, 0x701ff000)
        uc.reg_write(UC_ARM64_REG_LR, trap)
        for index, value in enumerate(args):
            uc.reg_write(UC_ARM64_REG_X0 + index, value)
        uc.emu_start(address, trap, count=100000)
        assert uc.reg_read(UC_ARM64_REG_PC) == trap, f"original call did not return: {address:#x}"
        return uc.reg_read(UC_ARM64_REG_X0)

    def native_state():
        return run(a["state"])

    def drain():
        while pending:
            run(a["dispatcher"], pending.pop(0))

    def request(offset, parameters, result_offset, expected=1):
        data = malloc(32)
        uc.mem_write(data, bytes(parameters).ljust(32, b"\0"))
        message = malloc(56)
        opaque = malloc(8)
        store("<QQQQIIQQ", message, 0, 0, 0, ack, a["command_base"] + offset, 0, data, opaque)
        before = len(replies)
        run(a["submit"], message)
        assert pending == [message]
        drain()
        if expected is not None:
            assert replies[before:] == [(a["command_base"] + offset, opaque)]
            assert read(data + result_offset, 1) == bytes([expected])
        else:
            assert len(replies) == before, "STOP acknowledged before AudioIF completed"
        assert message in live and data in live and opaque in live
        return message, data, opaque

    # The factory and message handlers execute original instructions, not a
    # reconstructed state machine. Empty BSS models an idle initialized service.
    run(layout["factory"])
    assert native_state() == 0
    config = struct.pack("<6I", 1, 5, 0, 0, 9600, a["init_extra"])
    stock = image[layout["test_config"] - BASE:layout["test_config"] - BASE + 24]
    assert stock[:16] == config[:16]
    if target == "zr-c110":
        assert stock == config
    request(0, config, a["init_result"])
    assert native_state() == 1
    checks.append("original_init_and_target_result_byte")
    _, buffers, _ = request(5, bytes(16), 12)
    pool, size = unpack("<QI", buffers)
    assert pool in live and size >= 19200
    checks.append("original_factory_and_buffer_capacity")
    request(6, struct.pack("<QIIIB3x", pool, 9600, 2, 0, 1), 21)
    request(1, struct.pack("<I4xQ8x", 1, pcm_callback), a["start_result"])
    assert native_state() == 2 and unpack("<Q", a["consumer"])[0] == pcm_callback
    checks.append("original_pcm_and_start_callback_layout")
    before = len(lower_calls)
    request(0, config, a["init_result"], expected=0)
    assert native_state() == 2 and len(lower_calls) == before
    checks.append("original_busy_init_refuses_without_device_call")
    for bank in (pool, pool + 9600):
        before = len(freed)
        run(layout["raw_pcm"], bank, 9600, 2, 0, 0)
        drain()
        event, payload = periods[-1]
        assert event == a["command_base"] + 7 and struct.unpack_from("<Q", payload)[0] == bank
        assert len(freed) == before + 2
    checks.append("original_dma_callback_queued_copied_and_freed")
    message, data, opaque = request(2, bytes(8), 0, expected=None)
    assert native_state() == 3 and len(audio_pending) == 1
    native = audio_pending.pop()
    callback, command, parameters, context = (unpack("<Q", native + 24)[0], unpack("<I", native + 32)[0],
        unpack("<Q", native + 40)[0], unpack("<Q", native + 48)[0])
    assert command == 0x42020107
    store("<B", parameters + layout["native_stop_result"], 1)
    run(callback, command, context)
    before = len(freed)
    drain()
    assert native_state() == 0 and replies[-1] == (a["command_base"] + 2, opaque) and read(data, 1) == b"\x01"
    assert len(freed) == before + 4 and message in live and data in live
    checks.append("original_async_stop_waits_for_device_completion_and_retains_caller")
    request(9, bytes(8), 0)
    assert native_state() == 0
    checks.append("original_close_only_after_confirmed_stop")
    return dict(schema="fenestra.native-audio-verification/v1", target=target,
        original_sha256=hashlib.sha256(image).hexdigest(), checks=checks, passed=True,
        original_dispatch_executed=True, physical_output_verified=False,
        boundaries=["native allocator/copy/queue primitives", "synchronous AudioIF configuration/start/PCM/standby",
                    "asynchronous AudioIF STOP acceptance and device completion"],
        idle_bss_assumed=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", choices=LAYOUTS, required=True)
    parser.add_argument("--firmware", required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.output and not args.output.resolve().is_relative_to(ROOT / "build"):
        parser.error("output must be within the owned ignored build directory")
    if args.output and (args.output.resolve() == Path(args.firmware).resolve() or
            (args.output.exists() and args.output.samefile(args.firmware))):
        parser.error("original input must remain read-only")
    result = verify(args.target, args.firmware)
    rendered = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered, encoding="utf-8")
    print(rendered, end="")


if __name__ == "__main__":
    main()
