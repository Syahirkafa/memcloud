use serde::{Serialize, Deserialize};
use std::path::PathBuf;
use std::sync::{Arc, RwLock};
use std::fs;
use anyhow::Result;
use log::{info, error};

#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct TrustedDevice {
    pub public_key: String,
    pub name: String,
    pub first_seen: u64,
    pub last_approved: u64,
}

#[derive(Serialize, Deserialize, Debug, Default)]
struct TrustedStoreData {
    trusted: Vec<TrustedDevice>,
}

pub struct TrustedStore {
    file_path: PathBuf,
    data: Arc<RwLock<TrustedStoreData>>,
}

impl TrustedStore {
    pub fn new() -> Self {
        let home = dirs::home_dir().expect("Could not find home directory");
        let path = home.join(".memcloud").join("trusted_devices.json");
        
        let store = Self {
            file_path: path.clone(),
            data: Arc::new(RwLock::new(TrustedStoreData::default())),
        };
        
        if let Err(e) = store.load() {
            if path.exists() {
               error!("Failed to load trusted devices: {}", e);
            }
        }
        
        store
    }

    fn load(&self) -> Result<()> {
        if !self.file_path.exists() {
            return Ok(());
        }
        let content = fs::read_to_string(&self.file_path)?;
        let data: TrustedStoreData = serde_json::from_str(&content)?;
        let mut lock = self.data.write().unwrap();
        *lock = data;
        Ok(())
    }

    fn save(&self) -> Result<()> {
        let lock = self.data.read().unwrap();
        let content = serde_json::to_string_pretty(&*lock)?;
        
        if let Some(parent) = self.file_path.parent() {
            fs::create_dir_all(parent)?;
        }
        fs::write(&self.file_path, content)?;
        Ok(())
    }

    pub fn is_trusted(&self, public_key: &str) -> bool {
        let lock = self.data.read().unwrap();
        lock.trusted.iter().any(|d| d.public_key == public_key)
    }

    pub fn add_trusted(&self, public_key: String, name: String) -> Result<()> {
        {
            let mut lock = self.data.write().unwrap();
            lock.trusted.retain(|d| d.public_key != public_key);
            
            let now = std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_secs();

            lock.trusted.push(TrustedDevice {
                public_key,
                name,
                first_seen: now,
                last_approved: now,
            });
        }
        self.save()
    }

    pub fn remove_trusted(&self, public_key_or_name: &str) -> Result<bool> {
        let mut removed = false;
        {
            let mut lock = self.data.write().unwrap();
            let initial_len = lock.trusted.len();
            lock.trusted.retain(|d| {
                let match_key = d.public_key == public_key_or_name;
                let match_name = d.name == public_key_or_name;
                !match_key && !match_name
            });
            removed = lock.trusted.len() != initial_len;
        }
        if removed {
            self.save()?;
        }
        Ok(removed)
    }

    pub fn list_trusted(&self) -> Vec<TrustedDevice> {
        let lock = self.data.read().unwrap();
        lock.trusted.clone()
    }
}
