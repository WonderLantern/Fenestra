# Experimental card boot payloads

The optional loader for ZR C1.10 and Z9 C5.32 starts a single worker after the original application-ready handler. Once the card is mounted, the worker opens `A:\FENESTRA\BOOT.BIN` read-only, verifies the transport header and bounds, loads the image into reserved application DDR, and calls its entry. A failed mount, timeout, missing file, malformed header, or failed read or close leaves original dispatch in effect. The original application dispatcher does not wait for card I/O.

The payload owns initialization, additional files, dependencies, runtime hooks, and cleanup. A Fenestra payload can use the reported target to select a model Port. An independent payload can return immediately or start tasks it owns. Replacing a payload on the card works with the same loader, within the ABI and arena below.

## Boot image ABI 1

| File offset | Little-endian field |
| --- | --- |
| 0 | ASCII `FNB1` |
| 4 | Header length, 16 |
| 8 | Flat image length, 4–380,928 bytes |
| 12 | Four-byte-aligned entry offset from the start of the flat image |
| 16 | Flat AArch64 image bytes |

Link the image for `0x401a0000` or make it position-independent. The entry must point to a complete instruction within the image. Include initialized data in the image. The payload sets up any uninitialized storage, relocations, or runtime it needs. The loader strips the header before copying and releases the staging allocation before the call.

The entry uses AAPCS64 and has signature `void entry(const FxCardPayloadBoot *boot)`. See [the C declarations](../include/fenestra_card_payload.h). The runtime argument carries an ABI number and struct size, image and arena sizes, a target name, and the original application SHA-256 as platform metadata. It also exposes read-only card I/O and a synchronous copy service for page-aligned offsets inside the arena. Call the copy service serially on the entry task. The argument, its services and its I/O table are borrowed for the duration of the call. The payload owns any retained copies or task state.

The worker calls one entry per boot and cancels a pending handoff on normal shutdown. Once a payload has started, its shutdown behavior is its responsibility. A payload executes in the loader's native context. ABI checks establish file shape and memory bounds. Use a card containing a payload you prepared or trust.

## Local builds

Build the loader from the target's original application component. The build produces a loader, a one-word branch description, and an identity receipt. Use `--emit-main` to additionally write an application candidate. These are developer artifacts for separate boot qualification. The current release patch workflow remains available for the embedded Fenestra core.

```sh
python fenestra.py card-loader --target z9-c532 --firmware inputs/original-main.bin --output build/z9-card-loader --emit-main
clang --target=aarch64-none-elf -c examples/card_payload/return.S -o build/return.o
llvm-objcopy -O binary -j .text build/return.o build/return.raw
python fenestra.py card-payload --image build/return.raw --output build/BOOT.BIN
```

The example's `ret` instruction is position-independent and exercises the entry/return convention. A C payload or a multi-section assembly payload should link first. For a linked flat image, pass the entry offset to `card-payload` as `--entry-offset`.

The public source tests exercise two unrelated instruction bodies, entry bounds, variable lengths, mount failure and timeout, missing files, and closing errors with synthetic I/O. Physical cold boot, card timing, and native multicore cache behavior require device qualification.

## Fenestra Core and camera Ports

Build the common Core once, then build the Port for each original application. Both card outputs contain the same `BOOT.BIN`.

```sh
python fenestra.py card-core --output build/fenestra-card-core
python fenestra.py card-port --target zr-c110 --firmware inputs/zr-c110-main.bin --core build/fenestra-card-core --output build/zr-card
python fenestra.py card-port --target z9-c532 --firmware inputs/z9-c532-main.bin --core build/fenestra-card-core --output build/z9-card
```

Use the `FENESTRA` directory from either card output. To share a card, keep one `BOOT.BIN` and put both model files under `FENESTRA/PORTS/`. Existing `.wlm` modules keep their own format and use the model's existing SDK bindings. Build the demo separately for each model using the [module tutorial](module-development.md). The ZR Port includes the public capture, sensor trace and event trace adapters. The Z9 Port uses its own native menus and PTP adapters.

The Core reads the Port identified by the loader's target string. It checks the Port's original-application identity, the digest of the running Core, its ABI, callback addresses, and profile boundaries before copying it. The Port verifies displaced instructions, installs single-word RAM branches, and publishes its own state. A missing, unsuitable or uninstalled Port leaves the original dispatch active. With a valid Port, the Fenestra menu retains normal Activate and card startup settings.

Port ABI 1 combines a native callback table with the shared Core's link addresses. A new compatible model supplies verified native bindings and a Port in the existing ABI, and uses the same Core build. A new Core binary needs corresponding Ports linked from that Core's new build receipt. Both updates happen on the card with the existing loader. Device boot, normal shutdown and multicore publication still require qualification.
