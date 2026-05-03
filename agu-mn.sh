#!/bin/bash
clear

# ---- sudo handling ----
SUDO=""
if [[ "${EUID}" -ne 0 ]]; then
  SUDO="sudo"
fi

$SUDO apt-get update
$SUDO apt-get -y upgrade
$SUDO apt-get install -y nano htop curl wget unzip ufw

# Swap
if ! swapon --show | grep -q /var/swap.img; then
  $SUDO touch /var/swap.img
  $SUDO chmod 600 /var/swap.img
  $SUDO dd if=/dev/zero of=/var/swap.img bs=1024k count=2000
  $SUDO mkswap /var/swap.img
  $SUDO swapon /var/swap.img
  echo "/var/swap.img none swap sw 0 0" | $SUDO tee -a /etc/fstab
fi

CONF_DIR=~/.agouti
if [[ -d "$CONF_DIR" ]]; then
  echo "Existing .agouti found, removing blockchain data..."
  rm -rf "$CONF_DIR/database" "$CONF_DIR/sporks" "$CONF_DIR/chainstate" "$CONF_DIR/blocks"
fi

# Download binaries from latest release
LATEST=$(curl -s https://api.github.com/repos/AgoutiCoin/agouti/releases/latest | grep '"tag_name"' | head -1 | cut -d'"' -f4)
echo "Downloading agouti binaries $LATEST..."
agouti-cli stop > /dev/null 2>&1
sleep 2

$SUDO wget -q "https://github.com/AgoutiCoin/agouti/releases/download/$LATEST/agoutid" -O /usr/local/bin/agoutid
$SUDO wget -q "https://github.com/AgoutiCoin/agouti/releases/download/$LATEST/agouti-cli" -O /usr/local/bin/agouti-cli
$SUDO chmod +x /usr/local/bin/agoutid /usr/local/bin/agouti-cli

# Detect public IP
echo "Detecting IP..."
IP=$(curl -s4 --connect-timeout 5 icanhazip.com | tr -d '[:space:]')

if [[ -z "$IP" ]]; then
  echo "Could not detect IP automatically. Enter VPS public IP:"
  read -e IP
fi
echo "Using IP: $IP"

# Masternode private key
EXISTING_KEY=$(grep -s "^masternodeprivkey=" "$CONF_DIR/$CONF_FILE" | cut -d'=' -f2)
if [[ -n "$EXISTING_KEY" ]]; then
  echo ""
  echo "Existing masternode private key found. Press Enter to keep it, or paste a new key:"
  read PRIVKEY
  [[ -z "$PRIVKEY" ]] && PRIVKEY="$EXISTING_KEY"
else
  echo ""
  echo "Enter masternode private key (from wallet: Tools > Debug Console > masternode genkey):"
  read PRIVKEY
fi

# Write config
CONF_FILE=agouti.conf
PORT=5151

mkdir -p "$CONF_DIR"
cat > "$CONF_DIR/$CONF_FILE" <<EOF
rpcuser=user$(shuf -i 100000-10000000 -n 1)
rpcpassword=pass$(shuf -i 100000-10000000 -n 1)
rpcallowip=127.0.0.1
rpcport=6161
listen=1
server=1
daemon=1
logtimestamps=1
masternode=1
port=$PORT
masternodeaddr=$IP:$PORT
masternodeprivkey=$PRIVKEY
EOF

# Download and apply blockchain snapshot
SNAPSHOT=/tmp/agoutisnapshot.zip
if [[ -f "$SNAPSHOT" ]]; then
  SIZE=$(stat -c%s "$SNAPSHOT")
  if [[ "$SIZE" -lt $((1900 * 1024 * 1024)) ]]; then
    echo "Incomplete snapshot found ($((SIZE / 1024 / 1024))MB), removing..."
    rm "$SNAPSHOT"
  else
    echo "Valid snapshot found ($((SIZE / 1024 / 1024))MB), skipping download."
  fi
fi

if [[ ! -f "$SNAPSHOT" ]]; then
  echo "Downloading blockchain snapshot..."
  wget "https://agouti.io/agoutisnapshot15032026.zip" -O "$SNAPSHOT"
fi
echo "Extracting snapshot to $CONF_DIR..."
unzip -o "$SNAPSHOT" -d "$CONF_DIR"
rm "$SNAPSHOT"

# UFW
echo "Configuring firewall..."
$SUDO ufw allow ssh
$SUDO ufw allow $PORT/tcp
$SUDO ufw allow 6161/tcp
$SUDO ufw --force enable

# Start daemon
echo "Starting agoutid..."
agoutid -daemon
echo "Done. Wait ~30s then run: agouti-cli getinfo"
