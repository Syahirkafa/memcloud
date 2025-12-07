import * as net from 'net';
import { pack, unpack } from 'msgpackr';

export class MemSocket {
    private client: net.Socket;
    private pathOrPort: string | number;
    private host: string;

    constructor(pathOrPort: string | number = '/tmp/memcloud.sock', host: string = '127.0.0.1') {
        this.pathOrPort = pathOrPort;
        this.host = host;
        this.client = new net.Socket();
    }

    connect(): Promise<void> {
        return new Promise((resolve, reject) => {
            if (typeof this.pathOrPort === 'string') {
                this.client.connect(this.pathOrPort, () => resolve());
            } else {
                this.client.connect(this.pathOrPort, this.host, () => resolve());
            }

            this.client.on('error', (err: any) => {
                // If connecting to generic error, we might want to reject if not connected yet
                // But for established connection, we might want to reconnect or throw
                console.error("MemCloud Socket Error:", err.message);
            });
        });
    }

    async request(command: any): Promise<any> {
        return new Promise((resolve, reject) => {
            // Serialize
            const bodyBuffer = pack(command);
            const lenBuffer = Buffer.alloc(4);
            lenBuffer.writeUInt32BE(bodyBuffer.length, 0);

            // Send
            this.client.write(lenBuffer);
            this.client.write(bodyBuffer);

            // Receive logic (Simple one-off reading)
            // Ideally we need a framer because TCP might split packets/merge packets.
            // For MVP SDK, we implement a simple specialized reader for one response per request expectation?
            // But if we use a shared socket, we need a queue.
            // Simplified: Add a 'once' listener for data? No, TCP streams chunks.

            let accumulated: Buffer = Buffer.alloc(0);
            let expectedLen: number | null = null;

            const onData = (chunk: Buffer) => {
                accumulated = Buffer.concat([accumulated, chunk]);

                if (expectedLen === null) {
                    if (accumulated.length >= 4) {
                        expectedLen = accumulated.readUInt32BE(0);
                        accumulated = accumulated.slice(4);
                    }
                }

                if (expectedLen !== null) {
                    if (accumulated.length >= expectedLen) {
                        const body = accumulated.slice(0, expectedLen);
                        const remainder = accumulated.slice(expectedLen); // Ignoring remainder for now (assuming one resp)

                        // Cleanup
                        this.client.off('data', onData);
                        this.client.off('error', onError);
                        this.client.off('close', onClose);

                        try {
                            const resp = unpack(body);
                            resolve(resp);
                        } catch (e) {
                            reject(e);
                        }
                    }
                }
            };

            const onError = (err: Error) => {
                this.client.off('data', onData);
                this.client.off('close', onClose);
                reject(err);
            };

            const onClose = (hadError: boolean) => {
                this.client.off('data', onData);
                this.client.off('error', onError);
                if (!hadError) {
                    reject(new Error("Socket closed unexpectedly"));
                }
            };

            this.client.on('data', onData);
            this.client.once('error', onError);
            this.client.once('close', onClose);
        });
    }

    end() {
        this.client.end();
    }
}
