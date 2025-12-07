use serde::{Serialize, Deserialize};
use tokio::net::{TcpListener, TcpStream};
use tokio::io::{AsyncReadExt, AsyncWriteExt, BufWriter};
use anyhow::Result;
use log::{info, error};
use std::net::SocketAddr;
use crate::metadata::{BlockId, NodeId};
use tokio::sync::Mutex;

#[derive(Serialize, Deserialize, Debug)]
pub enum Message {
        version: u16,
        node_id: NodeId,
        name: String,
        total_memory: u64,
        used_memory: u64,
    },
    PutBlock {
        id: BlockId,
        data: Vec<u8>,
    },
    GetBlock {
        id: BlockId,
    },
    BlockData {
        id: BlockId,
        data: Option<Vec<u8>>,
    },
    // Distributed KV
    GetKey {
        key: String,
    },
    KeyFound {
        key: String,
        data: Option<Vec<u8>>,
    },
    Ack,
    Flush,
}

use std::sync::Arc;
use crate::peers::PeerManager;
use crate::blocks::{InMemoryBlockManager, BlockManager}; // Added BlockManager trait

pub struct TransportServer {
    listener: TcpListener,
    block_manager: Arc<InMemoryBlockManager>,
    peer_manager: Arc<PeerManager>,
}

impl TransportServer {
    pub async fn bind(start_port: u16, block_manager: Arc<InMemoryBlockManager>, peer_manager: Arc<PeerManager>) -> Result<(Self, u16)> {
        let mut port = start_port;
        // Try up to 10 ports
        for _ in 0..10 {
            let addr = format!("0.0.0.0:{}", port);
            match TcpListener::bind(&addr).await {
                Ok(listener) => {
                    info!("Transport listening on {}", addr);
                    return Ok((Self { listener, block_manager, peer_manager }, port));
                }
                Err(e) if e.kind() == std::io::ErrorKind::AddrInUse => {
                    info!("Port {} in use, trying next available port...", port);
                    port += 1;
                }
                Err(e) => return Err(anyhow::Error::new(e)),
            }
        }
        anyhow::bail!("Could not bind to any port starting from {} (tried 10 ports)", start_port);
    }

    pub async fn run(&self) {
        loop {
            match self.listener.accept().await {
                Ok((stream, addr)) => {
                    info!("Incoming connection from {}", addr);
                    let bm = self.block_manager.clone();
                    let pm = self.peer_manager.clone();
                    
                    // Split stream
                    let (reader, writer) = stream.into_split();
                    
                    // Send Hello (Handshake Reply)
                    // We need self ID and Name. PeerManager has it but it's private.
                    // We should probably expose it or pass it.
                    // For now, let's just use "Server" or maybe PeerManager has a getter?
                    // Ideally we pass `my_id, my_name` to TransportServer struct too?
                    // Or PeerManager provides a method to "send hello using my identity".
                    // But we have a split writer here.
                    // Let's assume PeerManager exposes getters for self_id/name.
                    
                    let my_id = pm.get_self_id();
                    let my_name = pm.get_self_name();
                    
                    let hello = Message::Hello {
                        version: 1,
                        node_id: my_id,
                        name: my_name,
                        total_memory: 8 * 1024 * 1024 * 1024, // 8GB Mock
                        used_memory: bm.used_space(),
                    };
                    
                    // Sending Hello needs to happen before we pass writer to handler, OR we clone/lock it?
                    // We can just use the writer here directly before wrapping in mutex?
                    // But `send_message` takes TcpStream. `send_message_locked` takes MutexGuard.
                    // Let's implement `send_message_write_half`?
                    // Or just use block:
                    // Wrap in BufWriter
                    let mut writer = BufWriter::new(writer);

                    {
                        // quick send
                        let bytes = bincode::serialize(&hello).unwrap_or_default();
                        let len = bytes.len() as u32;
                        use tokio::io::AsyncWriteExt;
                        if let Err(e) = writer.write_all(&len.to_be_bytes()).await {
                            error!("Failed to send Hello: {}", e);
                            continue;
                        };
                         if let Err(e) = writer.write_all(&bytes).await {
                            error!("Failed to send Hello: {}", e);
                            continue;
                        };
                         if let Err(e) = writer.flush().await {
                            error!("Failed to flush Hello: {}", e);
                            continue;
                        };
                    }
                    
                    let writer_arc = Arc::new(tokio::sync::Mutex::new(writer));

                    tokio::spawn(async move {
                        if let Err(e) = handle_connection_split(reader, writer_arc, addr, bm, pm).await {
                            error!("Connection error from {}: {}", addr, e);
                        }
                    });
                }
                Err(e) => {
                    error!("Accept error: {}", e);
                }
            }
        }
    }
}

use tokio::net::tcp::{OwnedReadHalf, OwnedWriteHalf};

// Helper for sending messages with a mutex-guarded write half
pub async fn send_message_locked(writer: &mut tokio::sync::MutexGuard<'_, BufWriter<OwnedWriteHalf>>, msg: &Message) -> Result<()> {
    let bytes = bincode::serialize(msg)?;
    let len = bytes.len() as u32;
    writer.write_all(&len.to_be_bytes()).await?;
    writer.write_all(&bytes).await?;
    writer.flush().await?; // Ensure packet is sent
    Ok(())
}

pub async fn handle_connection_split(
    mut reader: OwnedReadHalf, 
    writer: Arc<Mutex<BufWriter<OwnedWriteHalf>>>, 
    addr: SocketAddr, 
    block_manager: Arc<InMemoryBlockManager>, 
    peer_manager: Arc<PeerManager>
) -> Result<()> {
    // 1. Read length prefix (u32)
    let mut len_buf = [0u8; 4];
    
    // Simple loop to read messages (assuming persistent connection)
    loop {
        // Read length
        if let Err(_) = reader.read_exact(&mut len_buf).await {
            // connection closed or error
            break;
        }
        let len = u32::from_be_bytes(len_buf) as usize;
        
        // Read body
        let mut buf = vec![0u8; len];
        reader.read_exact(&mut buf).await?;

        // Deserialize
        let msg: Message = bincode::deserialize(&buf)?;
        info!("Received message from {}: {:?}", addr, msg);

        match msg {
            Message::Hello { version: _, node_id, name, total_memory, used_memory } => {
                // Handle Handshake
                peer_manager.handle_hello(node_id, name, addr, writer.clone(), total_memory, used_memory);
            }
            Message::GetBlock { id } => {
                use crate::blocks::BlockManager;
                match block_manager.get_block(id) {
                    Ok(Some(block)) => {
                        let resp = Message::BlockData { id, data: Some(block.data) };
                        let mut w = writer.lock().await;
                        send_message_locked(&mut w, &resp).await?;
                    }
                    Ok(None) => {
                        let resp = Message::BlockData { id, data: None };
                        let mut w = writer.lock().await;
                        send_message_locked(&mut w, &resp).await?;
                    }
                    Err(e) => {
                         error!("Error retrieving block {}: {}", id, e);
                         let resp = Message::BlockData { id, data: None };
                         let mut w = writer.lock().await;
                         send_message_locked(&mut w, &resp).await?;
                    }
                }
            }
            Message::BlockData { id, data } => {
                if let Some(d) = data {
                    peer_manager.satisfy_request(id, d);
                } else {
                     info!("Received generic BlockData empty for {}", id);
                }
            }
            Message::PutBlock { id, data } => {
                 use crate::blocks::{BlockManager, Block};
                 info!("Storing remote block from peer");
                 let block = Block { id, data };
                 if let Err(e) = block_manager.put_block(block) {
                     error!("Failed to store remote block: {}", e);
                 }
            }
            Message::GetKey { key } => {
                // Check local index
                let id_opt = block_manager.get_named_block_id(&key);
                let mut data_opt = None;
                
                if let Some(id) = id_opt {
                    // Try to get data locally (sync or async? we are in async)
                    if let Ok(Some(block)) = block_manager.get_block(id) {
                         data_opt = Some(block.data);
                    }
                }
                
                let resp = Message::KeyFound { key, data: data_opt };
                let mut w = writer.lock().await;
                send_message_locked(&mut w, &resp).await?;
            }
            Message::KeyFound { key, data } => {
                if let Some(d) = data {
                    peer_manager.satisfy_key_request(&key, d);
                }
            }
            Message::Flush => {
                info!("Received Flush command from peer. Clearing local memory.");
                block_manager.flush();
            }
            _ => {}
        }
    }
    Ok(())
}

#[allow(dead_code)]
pub async fn send_message(stream: &mut TcpStream, msg: &Message) -> Result<()> {
    let bytes = bincode::serialize(msg)?;
    let len = bytes.len() as u32;
    stream.write_all(&len.to_be_bytes()).await?;
    stream.write_all(&bytes).await?;
    Ok(())
}
