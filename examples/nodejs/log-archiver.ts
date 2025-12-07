
import { MemCloud } from 'memcloud';
import { Readable } from 'stream';
import * as zlib from 'zlib';

const cloud = new MemCloud();
const LOG_COUNT = 1_000_000; // 1 Million Logs
// Roughly 1 million logs * ~150 bytes = ~150MB Raw -> Compressed to ~20-30MB
// Let's make it bigger to show the point? 2 Million?
// 1M is fast enough to demo.

interface LogEntry {
    timestamp: string;
    level: string;
    method: string;
    url: string;
    status: number;
    latency_ms: number;
    user_agent: string;
    request_id: string;
}

// Generates a stream of fake access logs
class LogSource extends Readable {
    private count: number;
    private sentCount: number;
    private lastLog: number;

    constructor(count: number) {
        super();
        this.count = count;
        this.sentCount = 0;
        this.lastLog = 0;
    }

    _read(size: number) {
        let pushMore = true;

        while (pushMore && this.sentCount < this.count) {
            const log: LogEntry = {
                timestamp: new Date().toISOString(),
                level: 'INFO',
                method: ['GET', 'POST', 'PUT', 'DELETE'][Math.floor(Math.random() * 4)],
                url: `/api/resource/${Math.floor(Math.random() * 1000)}`,
                status: [200, 201, 400, 404, 500][Math.floor(Math.random() * 5)],
                latency_ms: Math.floor(Math.random() * 200),
                user_agent: 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7)...',
                request_id: Math.random().toString(36).substring(7)
            };

            const json = JSON.stringify(log) + '\n';
            this.sentCount++;

            // Push to stream
            pushMore = this.push(json);

            // Log progress every 100k
            if (this.sentCount - this.lastLog >= 100_000) {
                const rss = Math.round(process.memoryUsage().rss / 1024 / 1024);
                const p = Math.round((this.sentCount / this.count) * 100);
                console.log(`📝 Logged ${this.sentCount.toLocaleString()} / ${this.count.toLocaleString()} (${p}%) | 🧠 RSS: ${rss}MB`);
                this.lastLog = this.sentCount;
            }
        }

        if (this.sentCount >= this.count) {
            this.push(null);
        }
    }
}

async function main() {
    console.log(`🚀 Starting Real-Time Log Archiver`);
    console.log(`   Task: Generate ${LOG_COUNT.toLocaleString()} logs -> Gzip -> Stream to Peer`);

    try {
        await cloud.connect();

        // 1. Find Peer
        console.log("🔍 Looking for offload targets...");
        let peers = await cloud.peers();
        if (peers.length === 0) {
            console.log("   No peers immediately found. Waiting 2s...");
            await new Promise(r => setTimeout(r, 2000));
            peers = await cloud.peers();
        }

        const targetPeer = peers.length > 0 ? peers[0].split(' ')[0] : undefined;

        if (targetPeer) {
            console.log(`🤝 Archiving to Peer: ${targetPeer}`);
        } else {
            console.log(`⚠️ No peers found! Archiving locally.`);
        }

        // 2. Build Pipeline
        const logSource = new LogSource(LOG_COUNT);
        const gzip = zlib.createGzip();

        // Pipe logs into gzip
        logSource.pipe(gzip);

        // 3. Stream Compressed Output to MemCloud
        console.log("📦 Pipeline Started: [Generator] -> [Gzip] -> [MemCloud]");
        console.time("Archival Time");

        // We pass the gzip stream to MemCloud
        const handle = await cloud.storeStream(gzip, { target: targetPeer });

        console.timeEnd("Archival Time");

        console.log("✅ Archival Complete!");
        console.log(`   🗄️  Archive ID: ${handle.id}`);

        const finalRss = Math.round(process.memoryUsage().rss / 1024 / 1024);
        console.log(`   🏁 Final Local RAM: ${finalRss}MB`);

    } catch (e) {
        console.error("❌ Archival Failed:", e);
    } finally {
        cloud.close();
    }
}

main();
