use anyhow::Result;
use mdns_sd::{ServiceDaemon, ServiceInfo, ServiceEvent};
use log::{info, error};
use uuid::Uuid;
use std::sync::Arc;
use crate::peers::PeerManager;
use std::net::SocketAddr;
use std::str::FromStr;

use crate::blocks::InMemoryBlockManager;

pub struct MdnsDiscovery {
    daemon: ServiceDaemon,
    service_type: &'static str,
    node_id: Uuid,
    port: u16,
    peer_manager: Arc<PeerManager>,
    block_manager: Arc<InMemoryBlockManager>,
}

impl MdnsDiscovery {
    pub fn new(node_id: Uuid, port: u16, peer_manager: Arc<PeerManager>, block_manager: Arc<InMemoryBlockManager>) -> Result<Self> {
        let daemon = ServiceDaemon::new()?;
        Ok(Self {
            daemon,
            service_type: "_memcloud._tcp.local.",
            node_id,
            port,
            peer_manager,
            block_manager,
        })
    }

    pub fn start_advertising(&self) -> Result<()> {
        let hostname = format!("memcloud-{}", self.node_id);
        let properties = [("id", self.node_id.to_string())];
        
        let my_service = ServiceInfo::new(
            self.service_type,
            &self.node_id.to_string(), // instance name
            &hostname,
            "", // ip
            self.port,
            match std::collections::HashMap::from_iter(properties.iter().map(|(k, v)| (k.to_string(), v.to_string()))) {
                 props => Some(props)
            },
        )?;

        self.daemon.register(my_service)?;
        info!("Started mDNS advertising for {}", self.node_id);
        Ok(())
    }

    pub fn start_browsing(&self) -> Result<()> {
        let receiver = self.daemon.browse(self.service_type)?;
        let my_id = self.node_id;
        let peer_manager = self.peer_manager.clone();
        let block_manager = self.block_manager.clone();

        tokio::spawn(async move {
            while let Ok(event) = receiver.recv() {
                match event {
                    ServiceEvent::ServiceFound(service_type, fullname) => {
                         info!("mDNS Found Service: {} ({})", fullname, service_type);
                    }
                    ServiceEvent::ServiceResolved(info) => {
                        let fullname = info.get_fullname();
                         // Check if it's us
                        if fullname.contains(&my_id.to_string()) {
                            continue;
                        }

                        // Extract ID and Addr
                        if let Some(id_prop_raw) = info.get_property_val("id") {
                             // Handle the case where the property value might be an Option reference
                             // This handles the specific type mismatch reported by compiler
                             let id_bytes = match id_prop_raw {
                                 Some(b) => b,
                                 None => continue,
                             };
                             
                             if let Ok(id_str) = std::str::from_utf8(id_bytes) {
                                if let Ok(peer_id) = Uuid::from_str(id_str) {
                                    if let Some(addr) = info.get_addresses().iter().next() {
                                        let socket_addr = SocketAddr::new(*addr, info.get_port());
                                        info!("Discovered peer {} at {}", peer_id, socket_addr);
                                        
                                        // Connect
                                        if let Err(e) = peer_manager.add_discovered_peer(peer_id, socket_addr, block_manager.clone(), peer_manager.clone()).await {
                                            error!("Failed to add discovered peer: {}", e);
                                        }
                                    }
                                }
                            }
                        }
                    }
                    _ => {}
                }
            }
        });
        
        info!("Started mDNS browsing");
        Ok(())
    }
}
