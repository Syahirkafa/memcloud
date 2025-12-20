# MemCloud Interceptor Usage Guide

The MemCloud Interceptor allows you to offload large heap allocations from any C/C++ application to remote MemCloud nodes without modifying the application's source code.

## The `run` Command

The easiest way to use the interceptor is via the `memcli run` command. It automatically handles the necessary environment variables and launches your application with the interceptor enabled.

### Basic Usage

```bash
memcli run --threshold <MB> <executable> [args...]
```

### Options

*   `--threshold` (`-t`): The allocation size threshold in megabytes. Any allocation (`malloc`, `calloc`, `realloc`) equal to or larger than this value will be offloaded to MemCloud. (Default: 8 MB).
*   `--socket` (`-s`): Path to the MemCloud daemon socket (e.g., `/tmp/memcloud.sock`).

### Example

To run a program named `my_app` and offload all allocations 16 MB or larger:

```bash
memcli run --threshold 16 ./my_app --arg1 value1
```

## How it Works

The `memcli run` command sets the following environment variables before executing the target program:

*   `DYLD_INSERT_LIBRARIES` (macOS) / `LD_PRELOAD` (Linux): Points to the `libmemcloud_vm` library.
*   `DYLD_FORCE_FLAT_NAMESPACE=1` (macOS): Required for reliable symbol interception.
*   `MEMCLOUD_MALLOC_THRESHOLD_MB`: Set to the threshold provided.
*   `MEMCLOUD_SOCKET`: Set to the daemon socket path.
*   `DYLD_LIBRARY_PATH` / `LD_LIBRARY_PATH`: Updated to include the path where `libmemsdk` and `libmemcloud_vm` are located.

## Manual Execution

If you prefer to run the interceptor manually, you can set the environment variables yourself:

```bash
# macOS
export DYLD_INSERT_LIBRARIES=/usr/local/lib/libmemcloud_vm.dylib
export DYLD_FORCE_FLAT_NAMESPACE=1
export MEMCLOUD_MALLOC_THRESHOLD_MB=8
./my_app

# Linux
export LD_PRELOAD=/usr/local/lib/libmemcloud_vm.so
export MEMCLOUD_MALLOC_THRESHOLD_MB=8
./my_app
```

## Troubleshooting

*   **Daemon Connection**: Ensure the MemCloud daemon (`memnode`) is running before launching your application.
*   **Symbol Interception**: On macOS, `DYLD_FORCE_FLAT_NAMESPACE=1` is critical. If your application uses its own custom allocator, the interceptor might be bypassed.
*   **Logs**: The interceptor prints status messages to `stderr`, such as `[memcloud-vm] intercepted large allocation`.
