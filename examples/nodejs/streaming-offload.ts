
import { MemCloud } from 'memcloud';
import * as fs from 'fs';
import * as path from 'path';
import { Readable } from 'stream';

const cloud = new MemCloud();
// Use a large file size to verify streaming benefits (10MB)
const SIZE_MB = 10;
const FILE_PATH = path.join(process.cwd(), 'temp-stream-test.bin');

async function createLargeFile(filePath: string, sizeMB: number) {
    console.log(`Creating ${sizeMB}MB test file at ${filePath}...`);
    const writeStream = fs.createWriteStream(filePath);
    const chunk = Buffer.alloc(1024 * 1024, 'x'); // 1MB chunks

    for (let i = 0; i < sizeMB; i++) {
        if (!writeStream.write(chunk)) {
            await new Promise<void>(resolve => writeStream.once('drain', () => resolve()));
        }
    }
    writeStream.end();
    await new Promise<void>(resolve => writeStream.on('finish', () => resolve()));
    console.log("File created.");
}

async function main() {
    console.log("🚀 Starting Streaming Offload Demo");

    try {
        await createLargeFile(FILE_PATH, SIZE_MB);

        await cloud.connect();

        // 0. Find a Peer
        console.log("🔍 Looking for peers...");
        let peers = await cloud.peers();
        let targetPeer: string | undefined;

        // Simple wait for peers if none found immediately (mDNS needs a moment)
        if (peers.length === 0) {
            console.log("No peers found immediately, waiting 2s...");
            await new Promise(r => setTimeout(r, 2000));
            peers = await cloud.peers();
        }

        if (peers.length > 0) {
            // peers format: "UUID (name) @ host:port"
            // We just need the UUID or name, usually the first part
            targetPeer = peers[0].split(' ')[0]; // Basic parsing
            console.log(`🤝 Found Peer: ${targetPeer}. Streaming target set.`);
        } else {
            console.log("⚠️ No peers found. Streaming locally.");
        }

        // 1. Create Read Stream
        const readStream = fs.createReadStream(FILE_PATH, { highWaterMark: 1024 * 1024 }); // 1MB chunks

        // 2. Stream to MemCloud (Remote or Local)
        console.log(`📤 Streaming to MemCloud (${targetPeer ? 'Remote' : 'Local'})...`);
        console.time("Stream Time");

        // Pass target if available
        const handle = await cloud.storeStream(readStream, { target: targetPeer });

        console.timeEnd("Stream Time");
        console.log(`✅ Stream Checkpoint: Stored Block ID: ${handle.id}`);

        // 3. Verify
        console.log("📥 Reading back data for verification (simulating load)...");
        // Note: Currently load() fetches the full block. True streaming download is future work.
        const retrievedData = await cloud.load(handle.id);

        console.log("🔍 Verifying data integrity...");
        const originalData = fs.readFileSync(FILE_PATH);

        if (retrievedData.equals(originalData)) {
            console.log("✅ Data verified! Integrity Match.");
        } else {
            console.error("❌ Data verification FAILED! Content mismatch.");
            console.log(`Original Size: ${originalData.length}, Retrieved: ${retrievedData.length}`);

        }

    } catch (e) {
        console.error("❌ Streaming Failed:", e);
    } finally {
        if (fs.existsSync(FILE_PATH)) {
            console.log("Cleaning up temp file...");
            fs.unlinkSync(FILE_PATH);
        }
        cloud.close();
    }
}

main().catch(console.error);
