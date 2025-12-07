# MemCloud JavaScript SDK

A TypeScript SDK for connecting to the MemCloud distributed in-memory data store.

## Installation

```bash
npm install memcloud
```

## Requirements

- Node.js >= 14
- MemCloud daemon running locally (`memcli node start`)

## Quick Start

```typescript
import { MemCloud } from 'memcloud';

async function main() {
    const cloud = new MemCloud();
    await cloud.connect();

    // Store data
    const handle = await cloud.store("Hello, MemCloud!");
    console.log("Stored with ID:", handle.id);

    // Retrieve data
    const data = await cloud.load(handle.id);
    console.log("Retrieved:", data.toString());

    // Key-Value Store
    await cloud.set("user:123", JSON.stringify({ name: "Alice", role: "admin" }));
    const user = await cloud.get("user:123");
    console.log("User:", JSON.parse(user.toString()));

    // List peers
    const peers = await cloud.peers();
    console.log("Connected peers:", peers);

    cloud.close();
}

main();
```

## API

### `new MemCloud(options?)`
Creates a new MemCloud client instance.

Options:
- `socketPath` (optional): Path to the Unix socket (default: `/tmp/memcloud.sock`)
- `tcpHost` (optional): TCP host for JSON RPC (default: `127.0.0.1`)
- `tcpPort` (optional): TCP port for JSON RPC (default: `7070`)

### `connect(): Promise<void>`
Connects to the local MemCloud daemon.

### `store(data: string | Buffer): Promise<Handle>`
Stores data in the local node. Returns a handle with the block ID.

### `load(id: number): Promise<Buffer>`
Loads data by block ID.

### `set(key: string, value: string | Buffer): Promise<number>`
Sets a key-value pair. Returns the block ID.

### `get(key: string): Promise<Buffer>`
Gets a value by key.

### `peers(): Promise<string[]>`
Lists connected peers.

### `close(): void`
Closes the connection.

## License

MIT
