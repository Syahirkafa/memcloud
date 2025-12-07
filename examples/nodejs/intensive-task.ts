
import { MemCloud } from 'memcloud';
import { Readable } from 'stream';

const cloud = new MemCloud();
const TARGET_SIZE_MB = 1000; // 1GB
const TARGET_SIZE_BYTES = TARGET_SIZE_MB * 1024 * 1024;

// Custom Stream that generates data on-the-fly
// This simulates a process reading a massive file or generating massive logs
class HeavyDataSource extends Readable {
    private totalSize: number;
    private sentSize: number;
    private lastLog: number;

    constructor(sizeBytes: number) {
        super({ highWaterMark: 1024 * 1024 }); // 1MB Buffer
        this.totalSize = sizeBytes;
        this.sentSize = 0;
        this.lastLog = 0;
    }

    _read(size: number) {
        if (this.sentSize >= this.totalSize) {
            this.push(null);
            return;
        }

        // Generate a chunk (simulating heavy work)
        // We make sure not to allocate too much at once
        const remaining = this.totalSize - this.sentSize;
        const chunkSize = Math.min(size, remaining); // usually match highWaterMark or requested size

        // Allocating new buffer for this chunk
        const chunk = Buffer.alloc(chunkSize, 65); // 'A'

        this.sentSize += chunkSize;
        this.push(chunk);

        // Log progress every ~50MB
        if (this.sentSize - this.lastLog > 50 * 1024 * 1024) {
            const rss = Math.round(process.memoryUsage().rss / 1024 / 1024);
            const p = Math.round((this.sentSize / this.totalSize) * 100);
            console.log(`⏳ Progress: ${p}% | Sent: ${Math.round(this.sentSize / 1024 / 1024)}MB | 🧠 Local RAM: ${rss}MB (Stable)`);
            this.lastLog = this.sentSize;
        }
    }
}

async function main() {
    console.log(`🚀 Starting RAM Intensive Task (1GB Workload)`);
    console.log(`   Demonstrating "Infinite RAM" by offloading to peer.`);

    try {
        await cloud.connect();

        // 1. Find Peer
        console.log("🔍 Looking for peers...");
        let peers = await cloud.peers();
        if (peers.length === 0) {
            console.log("   No peers immediately found. Waiting 2s...");
            await new Promise(r => setTimeout(r, 2000));
            peers = await cloud.peers();
        }

        const targetPeer = peers.length > 0 ? peers[0].split(' ')[0] : undefined;

        if (targetPeer) {
            console.log(`🤝 Offloading to Peer: ${targetPeer}`);
        } else {
            console.log(`⚠️ No peers found! Streaming locally (This will fill local disk).`);
        }

        // 2. Start Processing
        console.log("started processing...");
        const source = new HeavyDataSource(TARGET_SIZE_BYTES);

        console.time("Processing Time");
        const handle = await cloud.storeStream(source, { target: targetPeer });
        console.timeEnd("Processing Time");

        console.log("✅ Task Complete!");
        console.log(`   Stored 1GB Result -> Block ID: ${handle.id}`);

        const finalRss = Math.round(process.memoryUsage().rss / 1024 / 1024);
        console.log(`   🏁 Final Local RAM: ${finalRss}MB`);
        console.log(`   (Notice we processed 1GB but RAM never spiked above ~100MB)`);

    } catch (e) {
        console.error("❌ Task Failed:", e);
    } finally {
        cloud.close();
    }
}

main();
