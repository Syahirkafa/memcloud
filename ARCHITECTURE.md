# MemCloud Architecture

This document describes the internal architecture of MemCloud, including data flows for core operations.

---

## System Overview

```mermaid
flowchart TD

    subgraph AppLayer[Application Layer]
        CLI["MemCLI"]
        SDK["JS / Python / Rust SDK"]
    end

    subgraph LocalDaemon["MemCloud Daemon (Local)"]
        RPC["Local RPC API<br/>(Unix Socket + JSON TCP)"]
        BlockMgr["Block Manager<br/>(Store/Load/Free)"]
        PeerMgr["Peer Manager<br/>(Connections & Routing)"]
        RAM[("Local RAM Cache")]
        Discovery["mDNS Discovery"]
    end

    subgraph RemoteDevice["Remote Device(s)"]
        RemoteDaemon["Remote MemCloud Daemon"]
        RemoteRAM[("Remote RAM Storage")]
    end

    CLI --> RPC
    SDK --> RPC

    RPC --> BlockMgr
    BlockMgr --> RAM
    BlockMgr --> PeerMgr
    PeerMgr --> Discovery

    Discovery --> RemoteDaemon
    PeerMgr <-->|"TCP / Binary Protocol"| RemoteDaemon

    RemoteDaemon --> RemoteRAM
```

---

## Data Flow: STORE Operation

When a client stores data:

```mermaid
sequenceDiagram
    participant Client as SDK/CLI
    participant RPC as RPC Server
    participant BM as Block Manager
    participant RAM as Local RAM
    participant PM as Peer Manager
    participant Remote as Remote Node

    Client->>RPC: Store { data }
    RPC->>BM: store_block(data)
    
    alt Local Storage
        BM->>RAM: Insert block
        RAM-->>BM: Block ID
    else Remote Storage (--remote flag)
        BM->>PM: route_to_peer(data)
        PM->>Remote: StoreBlock { data }
        Remote-->>PM: Block ID
    end
    
    BM-->>RPC: Stored { id }
    RPC-->>Client: Block ID
```

---

## Data Flow: LOAD Operation

When a client loads data:

```mermaid
sequenceDiagram
    participant Client as SDK/CLI
    participant RPC as RPC Server
    participant BM as Block Manager
    participant RAM as Local RAM
    participant PM as Peer Manager
    participant Remote as Remote Node

    Client->>RPC: Load { id }
    RPC->>BM: get_block(id)
    
    alt Block Found Locally
        BM->>RAM: Lookup block
        RAM-->>BM: Block data
    else Block on Remote Peer
        BM->>PM: fetch_from_peer(id)
        PM->>Remote: RequestBlock { id }
        Remote-->>PM: BlockData
        PM-->>BM: Block data
    end
    
    BM-->>RPC: Loaded { data }
    RPC-->>Client: Data
```

---

## Peer Discovery Flow

MemCloud uses mDNS (Multicast DNS) for automatic peer discovery on the local network:

```mermaid
sequenceDiagram
    participant NodeA as Node A (New)
    participant mDNS as mDNS Multicast
    participant NodeB as Node B (Existing)

    NodeA->>mDNS: Advertise "_memcloud._tcp.local."
    NodeA->>mDNS: Browse for "_memcloud._tcp.local."
    
    NodeB-->>mDNS: Announce presence
    mDNS-->>NodeA: ServiceResolved (NodeB info)
    
    NodeA->>NodeB: TCP Connect (port 8080)
    NodeB-->>NodeA: Connection Accepted
    
    NodeA->>NodeB: Hello { node_id, name }
    NodeB-->>NodeA: Welcome { peer_list }
    
    Note over NodeA,NodeB: Peers are now connected and can exchange blocks
```

---

## Module Structure

```
memcloud/
├── memnode/                 # Core daemon
│   ├── src/
│   │   ├── main.rs          # Entry point, CLI args
│   │   ├── blocks/          # Block storage & management
│   │   ├── discovery/       # mDNS peer discovery
│   │   ├── net/             # TCP transport layer
│   │   ├── peers/           # Peer connection management
│   │   ├── rpc/             # Local RPC server (Unix socket + JSON TCP)
│   │   └── metadata/        # Block metadata & indexing
│   └── Cargo.toml
│
├── memsdk/                  # Rust SDK
│   ├── src/lib.rs           # Client API
│   └── Cargo.toml
│
├── memcli/                  # Command-line interface
│   ├── src/main.rs          # CLI commands (store, load, peers, node)
│   └── Cargo.toml
│
├── js-sdk/                  # TypeScript SDK (npm: memcloud)
│   ├── src/
│   │   ├── api.ts           # MemCloud class
│   │   └── socket.ts        # TCP/Unix socket transport
│   └── package.json
│
└── installers/              # Service files
    ├── macos/               # launchd plist
    └── linux/               # systemd service
```

---

## Wire Protocol

### Local RPC (JSON over TCP/Unix Socket)

All local communication uses length-prefixed JSON:

```
┌────────────────┬────────────────────────────┐
│ Length (4 bytes, big-endian) │ JSON Body  │
└────────────────┴────────────────────────────┘
```

**Example Commands:**
```json
// Store
{ "Store": { "data": [72, 101, 108, 108, 111] } }

// Load
{ "Load": { "id": 12345678 } }

// Set Key-Value
{ "Set": { "key": "my-key", "data": [1, 2, 3] } }

// Get Key-Value
{ "Get": { "key": "my-key" } }

// List Peers
"ListPeers"
```

### Peer-to-Peer Protocol (Binary)

Inter-node communication uses a binary protocol with `bincode` serialization for efficiency:

```rust
enum Message {
    Hello { node_id: Uuid, name: String },
    StoreBlock { id: u64, data: Vec<u8> },
    RequestBlock { id: u64 },
    BlockData { id: u64, data: Vec<u8> },
    SetKey { key: String, data: Vec<u8> },
    GetKey { key: String },
    KeyFound { key: String, data: Option<Vec<u8>> },
    Ping,
    Pong,
}
```

---

## Security Considerations

> ⚠️ **Current Status: Development/LAN Only**

MemCloud is designed for trusted local networks. Current security model:

- **No Authentication**: Any device on the LAN can connect
- **No Encryption**: Data is transmitted in plaintext
- **No Authorization**: All peers have equal access

**Future Roadmap:**
- [ ] TLS encryption for peer connections
- [ ] Pre-shared key authentication
- [ ] Access control lists (ACLs)
