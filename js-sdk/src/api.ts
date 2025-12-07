import { MemSocket } from './socket';

export interface Handle {
    id: number; // BigInt serialized as number in JSON? Rust u64 fits in JS storage?
    // JSON default behavior for u64 might be number if it fits double, or generic. 
    // Rust serde_json serializes u64 as Number usually. JS Number is safe up to 2^53.
    // u64 goes up to 2^64. We might lose precision!
    // FIX: Rust serde_json can handle strings for large numbers if configured, but default is Number.
    // For this demo, random u64 might exceed safe integer.
    // We should treat ID as *any* or string if possible.
    // Ideally update Rust to serialize ID as String?
    // Let's treat it as any for now.
}

export class MemCloud {
    private socket: MemSocket;

    constructor(pathOrPort: string | number = '/tmp/memcloud.sock') {
        this.socket = new MemSocket(pathOrPort);
    }

    async connect() {
        await this.socket.connect();
        console.log("Connected to MemCloud Daemon");
    }

    async store(data: string | Buffer): Promise<Handle> {
        // Convert data to number array for Rust Vec<u8> via serde_json? 
        // Rust serde_json `Vec<u8>` expects an Array of integers `[1, 2, ...]` usually.
        // It does NOT support raw byte string by default unless using `serde_bytes`.
        // We defined SdkCommand { data: Vec<u8> }.
        // JSON: `{ "Store": { "data": [ ... ] } }`.

        const payload = Buffer.isBuffer(data) ? data : Buffer.from(data);
        const dataArray = Array.from(payload);

        console.log(`Storing ${payload.length} bytes...`);
        const resp = await this.socket.request({ Store: { data: dataArray } });

        if (resp.Stored) {
            console.log(`Stored Block ID: ${resp.Stored.id}`);
            return { id: resp.Stored.id };
        } else if (resp.Error) {
            throw new Error(resp.Error.msg);
        }
        throw new Error("Unknown response");
    }

    async load(idOrHandle: number | Handle): Promise<Buffer> {
        const id = typeof idOrHandle === 'number' ? idOrHandle : idOrHandle.id;
        console.log(`Loading Block ID: ${id}...`);
        const resp = await this.socket.request({ Load: { id } });

        if (resp.Loaded) {
            // data is array of numbers
            const buf = Buffer.from(resp.Loaded.data);
            console.log(`Loaded ${buf.length} bytes.`);
            return buf;
        } else if (resp.Error) {
            throw new Error(resp.Error.msg);
        }
        throw new Error("Unknown response");
    }

    async set(key: string, data: string | Buffer): Promise<Handle> {
        const payload = Buffer.isBuffer(data) ? data : Buffer.from(data);
        const dataArray = Array.from(payload);

        console.log(`Setting '${key}'...`);
        const resp = await this.socket.request({ Set: { key, data: dataArray } });

        if (resp.Stored) {
            console.log(`Set '${key}' -> ID: ${resp.Stored.id}`);
            return { id: resp.Stored.id };
        } else if (resp.Error) {
            throw new Error(resp.Error.msg);
        }
        throw new Error("Unknown response");
    }

    async get(key: string): Promise<Buffer> {
        console.log(`Getting '${key}'...`);
        const resp = await this.socket.request({ Get: { key } });

        if (resp.Loaded) {
            const buf = Buffer.from(resp.Loaded.data);
            console.log(`Got '${key}': ${buf.length} bytes.`);
            return buf;
        } else if (resp.Error) {
            throw new Error(resp.Error.msg);
        }
        throw new Error("Unknown response");
    }

    async peers(): Promise<string[]> {
        console.log("Listing peers...");
        const resp = await this.socket.request("ListPeers"); // Enum unit variant might be string "ListPeers" in JSON? 
        // Rust serde_json enum formatting: 
        // Unit variant: "ListPeers"
        // Struct variant: { "Store": ... }

        if (resp.List) {
            return resp.List.items;
        } else if (resp.Error) {
            throw new Error(resp.Error.msg);
        }
        throw new Error("Unknown response");
    }

    disconnect() {
        this.socket.end();
    }

    /** Alias for disconnect() */
    close() {
        this.disconnect();
    }
}
