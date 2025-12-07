import { MemCloud } from 'memcloud';

const cloud = new MemCloud();

async function main() {
    await cloud.connect();

    const handle = await cloud.store("My Data");
    console.log("Stored ID:", handle.id);

    const data = await cloud.load(handle.id);
    console.log("Data:", data.toString());

    await cloud.set("app-config", JSON.stringify({ theme: "dark" }));
    const config = await cloud.get("app-config");
    console.log("Config:", JSON.parse(config.toString()));
    const peers = await cloud.peers();
    console.log("Peers:", peers);

    if (peers.length > 0) {
        const targetPeer = peers[0]; // Pick first peer
        console.log(`Attempting to store data on peer: ${targetPeer}`);
        try {
            const remoteHandle = await cloud.store("Data for neighbor", targetPeer);
            console.log("Remote Stored ID:", remoteHandle.id);
        } catch (e) {
            console.error("Remote store failed:", e);
        }
    } else {
        console.log("No peers connected to test remote storage.");
    }

    cloud.close();
}

main();