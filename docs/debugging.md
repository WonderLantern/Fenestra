# USB/PTP and native debugging

## Connection and identity

Start the camera normally, activate Fenestra as needed, and enable USB Debug. Host tools use native PTP vendor operations. No external root-shell utility or replacement Windows camera driver is required. Importing a Python module does not open a device; an explicit CLI command establishes the connection.

Windows uses WPD; Linux uses PyUSB/libusb. Install `requirements-host.txt`. On Windows, COM type libraries are loaded from the system directory and generated caches go under the source tree's `build/` directory. Linux requires device access permissions. The tools do not automatically detach kernel drivers or close unrelated PTP sessions.

Set `FENESTRA_PTP_PROFILE` to `zr-c110`, `z9-c532` or `auto`. The client checks the connected camera's model and firmware before establishing an FXP2 session.

```sh
python host/ptp_client.py --help
python host/ptp_rtos.py --help
python host/ptp_linux.py --help
```

## FXP2 requests

HELLO, SUBMIT, FETCH and CONTROL use operations `0x95e0` through `0x95e3`, respectively. Requests carry session, sequence, operation, length and CRC fields. A client has one request in flight. If submission succeeds but acknowledgement is lost, retain its identity: retrying a non-idempotent native operation may execute it twice.

A disconnect or mismatched reply can leave execution status unknown. Query retained status or follow the applicable recovery procedure. Cancellation does not undo effects already performed. Confirm worker completion before reusing its resources.

## RTOS

`host/ptp_rtos.py` supports registers, address translation, memory reads and writes, dumps, native execution, shell/call operations and job management. Consult `--help` for arguments. Addresses accept hexadecimal notation.

```sh
python host/ptp_rtos.py registers
python host/ptp_rtos.py status
python host/ptp_rtos.py execute --line "echo Fenestra"
```

MMIO reads can have device side effects. Native calls require the correct ABI, execution context, locks and object lifetime. Debug privileges do not establish a boundary that prevents Flash writes.

## Linux agent

The agent source is `linux/fenestra_agent.c`; the source archive does not contain a compiled agent. Build it with an AArch64 Linux toolchain capable of static linking:

```sh
python host/build_linux_agent.py --cc aarch64-linux-gnu-gcc --output-dir build/linux-agent
python host/ptp_linux.py --agent build/linux-agent/fenestra-agent info
python host/ptp_linux.py --agent build/linux-agent/fenestra-agent exec -- /bin/uname -a
```

Run the builder in Linux or supply an available cross-compiler. It does not select a WSL distribution, container or remote host. Bootstrap checks that `/tmp` is tmpfs before creating a directory there. Access is limited by the native process's UID/GID. The agent provides shell, exec, file and process-memory access under those permissions.

## Observation and pixel data

`ptp_observe.py`, `ptp_sensor_trace.py`, `ptp_event_trace.py` and `ptp_frame_sample.py` implement their respective host schemas. Query firmware capabilities and schema versions first. ZR observation templates are not Z9 capture implementations.

Interpret pixel data together with dimensions, stride, packing, source address space, lease lifetime and first-fault information. A recorded format or buffer address alone does not establish sensor ADC depth or complete-frame capture.

## Remote PTP over SSH

Install matching host files and dependencies on the computer connected to the camera, then verify local PTP access. Configure SSH authentication separately. The remote endpoint is:

```sh
python -u host/ptp_remote.py --stdio --profile z9-c532
```

The stdio channel carries binary data. Do not allocate a terminal or write login messages to stdout. On the client, provide an argument-vector configuration such as:

```json
{
  "argv": ["ssh", "-T", "camera-host", "python3", "-u", "fenestra/host/ptp_remote.py", "--stdio", "--profile", "z9-c532"],
  "timeout_seconds": 30
}
```

`camera-host` is an example SSH alias and `fenestra/` is an example deployment directory. Set `FENESTRA_PTP_PROCESS_CONFIG` to the configuration file to route ordinary PTP commands through the subprocess transport. The tools do not listen on a public port, create SSH credentials, reconnect automatically or replay requests with uncertain outcomes. Network and camera-job timeouts are separate settings.

## Reporting problems

Include the source version, firmware SHA-256, profile, command, acknowledgement status and failure stage. Use schema descriptions, error codes and synthetic reproductions. Exclude raw memory, card contents, credentials, device serial numbers and personal host paths.
