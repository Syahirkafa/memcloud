use std::sync::Arc;
use dashmap::DashMap;
use crate::metadata::BlockId;

pub struct VmRegion {
    pub id: u64,
    pub size: u64,
    pub pages: DashMap<u64, BlockId>,
}

pub struct VmRegionManager {
    regions: DashMap<u64, Arc<VmRegion>>,
}

impl VmRegionManager {
    pub fn new() -> Self {
        Self {
            regions: DashMap::new(),
        }
    }

    pub fn create_region(&self, size: u64) -> u64 {
        let id = rand::random::<u64>();
        let region = VmRegion {
            id,
            size,
            pages: DashMap::new(),
        };
        self.regions.insert(id, Arc::new(region));
        id
    }

    pub fn get_region(&self, id: u64) -> Option<Arc<VmRegion>> {
        self.regions.get(&id).map(|r| r.clone())
    }

    pub fn get_stats(&self) -> (usize, usize) {
        let regions = self.regions.len();
        let mut pages = 0;
        for r in self.regions.iter() {
            pages += r.value().pages.len();
        }
        (regions, pages)
    }

    pub fn remove_region(&self, id: u64) -> Option<Arc<VmRegion>> {
        self.regions.remove(&id).map(|(_, r)| r)
    }
}
