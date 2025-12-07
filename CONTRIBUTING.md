# Contributing to MemCloud

Thank you for your interest in contributing to MemCloud! 🎉

## Getting Started

### Prerequisites

- **Rust** (1.70+): Install via [rustup](https://rustup.rs/)
- **Node.js** (14+): For the JS SDK
- **macOS or Linux**: Windows support is not yet available

### Building from Source

```bash
# Clone the repository
git clone https://github.com/vibhanshu2001/memcloud.git
cd memcloud

# Build all workspace crates
cargo build --release --workspace

# Binaries are in target/release/
ls target/release/memnode target/release/memcli
```

### Running the Daemon

```bash
# Run memnode with debug logging
RUST_LOG=info ./target/release/memnode --name "DevNode" --port 8080

# Or use the CLI to start in background
./target/release/memcli node start --name "DevNode"
```

### Running the CLI

```bash
# Check node status
./target/release/memcli node status

# Store some data
./target/release/memcli store "Hello, MemCloud!"

# List peers
./target/release/memcli peers
```

---

## Project Structure

```
memcloud/
├── memnode/     # Core daemon (Rust)
├── memsdk/      # Rust SDK library
├── memcli/      # Command-line interface
├── js-sdk/      # TypeScript SDK (npm package)
└── installers/  # systemd/launchd service files
```

---

## Development Workflow

### 1. Pick an Issue

Check the [Issues](https://github.com/vibhanshu2001/memcloud/issues) page for:
- `good first issue` - Great for newcomers
- `help wanted` - We'd love your input
- `enhancement` - Feature requests

### 2. Create a Branch

```bash
git checkout -b feature/your-feature-name
```

### 3. Make Your Changes

- Follow Rust best practices and idioms
- Add tests for new functionality
- Update documentation as needed

### 4. Test Your Changes

```bash
# Run all tests
cargo test --workspace

# Check formatting
cargo fmt --check

# Run clippy lints
cargo clippy --workspace
```

### 5. Submit a Pull Request

- Write a clear PR description
- Reference any related issues
- Be responsive to code review feedback

---

## Code Style

- **Rust**: Follow the standard Rust style guide (`cargo fmt`)
- **TypeScript**: Use Prettier with default settings
- **Commits**: Use conventional commits when possible (`feat:`, `fix:`, `docs:`)

---

## Testing

### Running Tests

```bash
# All workspace tests
cargo test --workspace

# Specific crate
cargo test -p memnode

# With logging output
RUST_LOG=debug cargo test -- --nocapture
```

### Integration Testing

1. Start two nodes on different ports:
   ```bash
   RUST_LOG=info ./target/release/memnode --name "NodeA" --port 8080 &
   RUST_LOG=info ./target/release/memnode --name "NodeB" --port 8081 &
   ```

2. They should discover each other via mDNS. Check with:
   ```bash
   ./target/release/memcli peers
   ```

3. Store data on one, retrieve from another:
   ```bash
   ./target/release/memcli store "test data" --remote
   ```

---

## Where to Start

New to the codebase? Here are some good entry points:

| Area | File | Description |
|------|------|-------------|
| CLI commands | `memcli/src/main.rs` | Add new CLI subcommands |
| RPC handlers | `memnode/src/rpc/mod.rs` | Handle new RPC operations |
| Block storage | `memnode/src/blocks/mod.rs` | Block management logic |
| Peer discovery | `memnode/src/discovery/mod.rs` | mDNS service |
| JS SDK | `js-sdk/src/api.ts` | TypeScript client API |

---

## Getting Help

- **GitHub Issues**: Bug reports and feature requests
- **Discussions**: General questions and ideas

---

## License

By contributing, you agree that your contributions will be licensed under the MIT License.
