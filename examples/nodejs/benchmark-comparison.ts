
import { MemCloud } from 'memcloud';
import { createClient } from 'redis';
import { Client as MemjsClient } from 'memjs';
import Table from 'cli-table3';

const ITEMS = 10_000;
const DATA_SIZE = 1024; // 1KB
const DATA = Buffer.alloc(DATA_SIZE, 'x');

async function benchmarkMemCloud() {
    const cloud = new MemCloud();
    await cloud.connect();

    // SET
    const startSet = process.hrtime.bigint();
    const ids: string[] = [];
    for (let i = 0; i < ITEMS; i++) {
        const h = await cloud.store(DATA);
        ids.push(h.id);
    }
    const endSet = process.hrtime.bigint();

    // GET
    const startGet = process.hrtime.bigint();
    for (let i = 0; i < ITEMS; i++) {
        await cloud.load(ids[i]);
    }
    const endGet = process.hrtime.bigint();

    cloud.close();

    return {
        set: Number(endSet - startSet) / 1e9,
        get: Number(endGet - startGet) / 1e9
    };
}

async function benchmarkRedis() {
    const client = createClient();
    await client.connect();

    // SET
    const startSet = process.hrtime.bigint();
    for (let i = 0; i < ITEMS; i++) {
        await client.set(`key:${i}`, DATA);
    }
    const endSet = process.hrtime.bigint();

    // GET
    const startGet = process.hrtime.bigint();
    for (let i = 0; i < ITEMS; i++) {
        await client.get(`key:${i}`);
    }
    const endGet = process.hrtime.bigint();

    await client.disconnect();

    return {
        set: Number(endSet - startSet) / 1e9,
        get: Number(endGet - startGet) / 1e9
    };
}

async function benchmarkMemcached() {
    const client = MemjsClient.create();

    // SET
    const startSet = process.hrtime.bigint();
    const promisesSet = [];
    for (let i = 0; i < ITEMS; i++) {
        // memjs uses callbacks/promises, let's await to be fair sequence
        promisesSet.push(client.set(`key:${i}`, DATA, {}));
    }
    await Promise.all(promisesSet);
    const endSet = process.hrtime.bigint();

    // GET
    const startGet = process.hrtime.bigint();
    const promisesGet = [];
    for (let i = 0; i < ITEMS; i++) {
        promisesGet.push(client.get(`key:${i}`));
    }
    await Promise.all(promisesGet);
    const endGet = process.hrtime.bigint();

    client.close();

    return {
        set: Number(endSet - startSet) / 1e9,
        get: Number(endGet - startGet) / 1e9
    };
}

async function main() {
    console.log(`🚀 Starting Benchmark (${ITEMS.toLocaleString()} ops, 1KB payloads)`);
    const table = new Table({ head: ['System', 'SET (ops/sec)', 'GET (ops/sec)', 'Status'] });

    // MemCloud
    try {
        process.stdout.write("Measuring MemCloud... ");
        const m = await benchmarkMemCloud();
        const setOps = Math.round(ITEMS / m.set);
        const getOps = Math.round(ITEMS / m.get);
        table.push(['MemCloud', setOps.toLocaleString(), getOps.toLocaleString(), '✅']);
        console.log("Done");
    } catch (e) {
        console.log("Failed");
        table.push(['MemCloud', '-', '-', '❌ Error']);
    }

    // Redis
    // try {
    //     process.stdout.write("Measuring Redis... ");
    //     const r = await benchmarkRedis();
    //     const setOps = Math.round(ITEMS / r.set);
    //     const getOps = Math.round(ITEMS / r.get);
    //     table.push(['Redis', setOps.toLocaleString(), getOps.toLocaleString(), '✅']);
    //     console.log("Done");
    // } catch (e) {
    //     console.log("Skipped (Not Running)");
    //     table.push(['Redis', '-', '-', '⚠️  Unavailable']);
    // }

    // Memcached
    // try {
    //     process.stdout.write("Measuring Memcached... ");
    //     const mc = await benchmarkMemcached();
    //     const setOps = Math.round(ITEMS / mc.set);
    //     const getOps = Math.round(ITEMS / mc.get);
    //     table.push(['Memcached', setOps.toLocaleString(), getOps.toLocaleString(), '✅']);
    //     console.log("Done");
    // } catch (e) {
    //     console.log("Skipped");
    //     table.push(['Memcached', '-', '-', '⚠️  Unavailable']);
    // }

    console.log(table.toString());
}

main();
