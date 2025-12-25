#!/bin/sh
set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
MAGENTA='\033[0;35m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Banner
echo "${MAGENTA}"
echo "╔══════════════════════════════════════════════════════════════╗"
echo "║                                                              ║"
echo "║               ☁  M E M C L O U D   I N S T A L L E R  ⚡      ║"
echo "║                                                              ║"
echo "║     'Turning nearby devices into your personal RAM farm.'    ║"
echo "║                                                              ║"
echo "║                    Created by Vibhanshu Garg                 ║"
echo "║                                                              ║"
echo "╚══════════════════════════════════════════════════════════════╝"
echo "${NC}"

# Logging helpers
log_info() {
    echo "${BLUE}➤${NC} $1"
}

log_success() {
    echo "${GREEN}✔${NC} $1"
}

log_warn() {
    echo "${YELLOW}⚠${NC} $1"
}

log_error() {
    echo "${RED}✖${NC} $1"
}

echo ""
log_info "Initializing MemCloud deployment sequence..."

# Detect OS + Architecture
log_info "Scanning system parameters..."
OS="$(uname -s)"
ARCH="$(uname -m)"
sleep 0.3

# Fetch latest version from GitHub
log_info "Fetching latest MemCloud release metadata..."
VERSION=$(curl -s https://api.github.com/repos/vibhanshu2001/memcloud/releases/latest | grep '"tag_name":' | sed -E 's/.*"([^"]+)".*/\1/')

if [ -z "$VERSION" ]; then
    log_warn "Could not fetch latest version — falling back to v0.1.0"
    VERSION="v0.1.0"
else
    log_info "Latest version detected: ${CYAN}${VERSION}${NC}"
fi

# Determine asset URL
case "$OS" in
  Linux)
    ASSET_URL="https://github.com/vibhanshu2001/memcloud/releases/download/${VERSION}/memcloud-x86_64-unknown-linux-gnu.tar.gz"
    ;;
  Darwin)
    if [ "$ARCH" = "x86_64" ]; then
        ASSET_URL="https://github.com/vibhanshu2001/memcloud/releases/download/${VERSION}/memcloud-x86_64-apple-darwin.tar.gz"
    elif [ "$ARCH" = "arm64" ]; then
        ASSET_URL="https://github.com/vibhanshu2001/memcloud/releases/download/${VERSION}/memcloud-aarch64-apple-darwin.tar.gz"
    else
        log_error "Unsupported architecture: $ARCH"
        exit 1
    fi
    ;;
  *)
    log_error "Unsupported OS: $OS"
    exit 1
    ;;
esac

echo ""
log_info "Preparing download for ${CYAN}${OS} (${ARCH})${NC}"
log_info "Source: ${ASSET_URL}"

echo ""
log_info "Downloading MemCloud ${VERSION} package..."
curl -L -o memcloud.tar.gz "$ASSET_URL"

echo ""
log_info "Extracting payload..."
mkdir -p /tmp/memcloud_install
tar -xzf memcloud.tar.gz -C /tmp/memcloud_install

# Install binaries
echo ""
log_info "Deploying binaries to /usr/local/bin (root privileges required)..."
if sudo mv /tmp/memcloud_install/memnode /usr/local/bin/ && sudo mv /tmp/memcloud_install/memcli /usr/local/bin/; then

    # Install VM interceptor and SDK libraries
    log_info "Installing VM interceptor and SDK libraries..."
    case "$OS" in
      Linux)
        sudo mv /tmp/memcloud_install/libmemcloud_vm.so /usr/local/lib/ 2>/dev/null || true
        sudo mv /tmp/memcloud_install/libmemsdk.so /usr/local/lib/ 2>/dev/null || true
        ;;
      Darwin)
        sudo mv /tmp/memcloud_install/libmemcloud_vm.dylib /usr/local/lib/ 2>/dev/null || true
        sudo mv /tmp/memcloud_install/libmemsdk.dylib /usr/local/lib/ 2>/dev/null || true
        ;;
    esac
    
    # Install C header
    if [ -f /tmp/memcloud_install/include/memcloud.h ]; then
        sudo mkdir -p /usr/local/include
        sudo mv /tmp/memcloud_install/include/memcloud.h /usr/local/include/
    fi
    
    # Update library cache on Linux
    if [ "$OS" = "Linux" ]; then
        sudo ldconfig 2>/dev/null || true
    fi

    rm memcloud.tar.gz
    rm -rf /tmp/memcloud_install

    echo ""
    log_success "MemCloud successfully installed! 🚀"
    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo ""
    echo "  ${GREEN}✨ You're Ready to Begin:${NC}"
    echo "    Start daemon:   ${CYAN}memcli node start --name \"MyDevice\" --total-memory 4gb${NC}"
    echo "    Connect Peer:   ${CYAN}memcli connect <IP> --offer-storage 1gb${NC}
    Check status:   ${CYAN}memcli node status${NC}"
    echo "    Stop daemon:    ${CYAN}memcli node stop${NC}"
    echo ""
    echo "  ${GREEN}🔮 VM-Backed Allocation (Remote RAM Pooling):${NC}"
    if [ "$OS" = "Linux" ]; then
        echo "    Run any app with remote RAM (Recommended):"
        echo "    ${CYAN}memcli run --threshold 8 <your-command>${NC}"
        echo ""
        echo "    Manual method:"
        echo "    ${CYAN}LD_PRELOAD=/usr/local/lib/libmemcloud_vm.so <your-command>${NC}"
    else
        echo "    Run any app with remote RAM (Recommended):"
        echo "    ${CYAN}memcli run --threshold 8 <your-command>${NC}"
        echo ""
        echo "    Manual method:"
        echo "    ${CYAN}DYLD_INSERT_LIBRARIES=/usr/local/lib/libmemcloud_vm.dylib <your-command>${NC}"
    fi
    echo "    Tune threshold: ${CYAN}export MEMCLOUD_MALLOC_THRESHOLD_MB=100${NC}"
    echo ""
    echo "  ${GREEN}📦 Using the JS/TypeScript SDK:${NC}"
    echo "    ${CYAN}npm install memcloud${NC}"
    echo ""
    echo "  ${GREEN}📚 Helpful Links:${NC}"
    echo "    📖 Docs:         https://github.com/vibhanshu2001/memcloud#readme"
    echo "    🐞 Issues:       https://github.com/vibhanshu2001/memcloud/issues"
    echo "    📦 NPM:          https://www.npmjs.com/package/memcloud"
    echo ""
    echo "  ${GREEN}🧹 Uninstall:${NC}"
    echo "    ${CYAN}curl -fsSL https://raw.githubusercontent.com/vibhanshu2001/memcloud/main/uninstall.sh | sh${NC}"
    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo ""
    echo "${MAGENTA}Welcome to the Distributed Memory Future. ✨${NC}"
    echo "${CYAN}Created by Vibhanshu Garg${NC}"
else
    log_error "Binary installation failed — insufficient permissions?"
    exit 1
fi
