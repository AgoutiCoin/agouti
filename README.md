# Agouti Core

Agouti (AGU) is a Proof-of-Stake cryptocurrency with masternodes, on-chain governance,
and the StakePointer protocol — a masternode-bound staking mechanism that replaces
coin-age grinding.

Agouti is a fork of [PIVX](https://github.com/PIVX-Project/PIVX) that forked
[Dash](https://github.com/dashpay/dash) that forked
[Bitcoin](https://github.com/bitcoin/bitcoin).

---

## Table of Contents

1. [Blockchain Specifications](#1-blockchain-specifications)
2. [Block Reward Schedule](#2-block-reward-schedule)
3. [Fees and Collaterals](#3-fees-and-collaterals)
4. [Building from Source](#4-building-from-source)
5. [Running a Full Node](#5-running-a-full-node)
6. [Masternode Setup Guide](#6-masternode-setup-guide)
7. [Governance and Budget System](#7-governance-and-budget-system)
8. [StakePointer Guide](#8-stakepointer-guide)
9. [Wallet Synchronisation Notes](#9-wallet-synchronisation-notes)
10. [Network Parameters Reference](#10-network-parameters-reference)

---

## 1. Blockchain Specifications

| Parameter                  | Mainnet               | Testnet     | Regtest |
|----------------------------|-----------------------|-------------|---------|
| Ticker                     | AGU                   | AGU         | AGU     |
| Block target time          | 60 seconds            | 60 seconds  | 60 s    |
| Base unit (1 AGU)          | 100,000,000 satoshis  | —           | —       |
| Maximum supply             | 3,000,000 AGU         | 43,199,500  | —       |
| PoW period                 | Blocks 1–500          | 1–200       | 1–200   |
| PoS period                 | Block 501+            | 201+        | 201+    |
| Coin maturity              | 15 confirmations      | 15          | 15      |
| Stake minimum age          | 1 hour                | 1 hour      | —       |
| Max block size             | 2 MB                  | 2 MB        | 2 MB    |
| Genesis time               | 2018-09-13 (Unix 1536892874) | —  | —       |
| P2P port                   | 5151                  | 7777        | 51436   |
| RPC port                   | 6161                  | 38843       | —       |

**Address prefixes (mainnet)**

| Type              | Base58 prefix | Leading character |
|-------------------|---------------|-------------------|
| Public key address| 83            | A                 |
| Script address    | 97            | G                 |
| WIF private key   | 130           | U                 |

**Network magic bytes (mainnet):** `09 39 09 18`

**Genesis message:**
> "You can be free. You can live and work anywhere in the world. You can be independent
> from routine and not answer to anybody."

---

## 2. Block Reward Schedule

### Subsidy per block

| Height range           | Subsidy per block |
|------------------------|-------------------|
| 0                      | 1,809,759 AGU (premine) |
| 1 – 44,640             | 1.5 AGU           |
| 44,641 – 570,240       | 1.0 AGU           |
| 570,241 – 1,095,839    | 0.5 AGU           |
| 1,095,840 – 1,621,439  | 0.25 AGU          |
| 1,621,440+             | 0.125 AGU         |

Emission is capped at 3,000,000 AGU total supply. Rewards are set to zero once the cap
is reached.

### Reward distribution

| Height range             | Masternode share | Staker share |
|--------------------------|------------------|--------------|
| 101 – 2,564,999          | 90 %             | 10 %         |
| 2,565,000+               | 50 %             | 50 %         |

At the current subsidy of 0.125 AGU (height > 1,621,440) and after block 2,565,000:
- Masternode payment ≈ 0.0625 AGU per block
- Staker reward ≈ 0.0625 AGU per block

---

## 3. Fees and Collaterals

### Transaction fees

| Fee type                   | Amount                                      |
|----------------------------|---------------------------------------------|
| Minimum relay fee          | Dynamic — set by `-minrelaytxfee` (default: per-byte rate) |
| Default wallet send fee    | 0 AGU (mempool minimum applied automatically) |
| InstantSend minimum fee    | 0.1 AGU (CENT = 10,000,000 satoshis)        |
| High-fee warning threshold | 0.1 AGU                                     |
| Insane fee rejection limit | 10,000 × minimum relay fee                  |

### Masternode collateral

| Item                        | Amount      |
|-----------------------------|-------------|
| Masternode collateral       | 3,000 AGU   |
| Collateral confirmations    | 15 (coin maturity) |

The collateral UTXO must remain unspent for the masternode to remain active. Spending
it removes the node from the network immediately.

### Governance / budget fees

| Item                          | Amount   | Notes                                   |
|-------------------------------|----------|-----------------------------------------|
| Proposal submission fee       | 50 AGU   | Burnt; paid to a provably unspendable address |
| Finalisation fee              | 50 AGU   | Burnt; paid when submitting finalised budget |
| Collateral confirmations      | 6        | Required before proposal is considered valid |
| Budget cycle length           | 43,200 blocks (~30 days, mainnet) | 720 blocks on testnet |
| Monthly budget pool           | 2% of block subsidy × 43,200 blocks |                    |

At 0.125 AGU/block the monthly budget is approximately:
  `0.125 × 0.02 × 43,200 = 108 AGU/month`

### Obfuscation (mixing) collateral

| Item                     | Amount           |
|--------------------------|------------------|
| Session collateral       | 10 AGU           |
| Maximum pool size        | 99,999.99 AGU    |

---

## 4. Building from Source

### Dependencies

```bash
sudo apt-get install build-essential libtool autotools-dev automake pkg-config \
  libssl-dev libevent-dev bsdmainutils libboost-all-dev
```

### Build with depends (recommended)

```bash
cd depends
make -j$(nproc) HOST=x86_64-pc-linux-gnu
cd ..
./autogen.sh
./configure --prefix=$(pwd)/depends/x86_64-pc-linux-gnu
make -j$(nproc)
```

### Install

```bash
sudo make install
```

Binaries: `agoutid`, `agouti-cli`, `agouti-tx`

---

## 5. Running a Full Node

### Basic configuration (`~/.agouti/agouti.conf`)

```ini
rpcuser=agoutiuser
rpcpassword=changethispassword
rpcallowip=127.0.0.1
listen=1
server=1
daemon=1
```

### Start and stop

```bash
agoutid -daemon
agouti-cli stop
```

### Useful RPC commands

```bash
agouti-cli getinfo
agouti-cli getblockcount
agouti-cli getpeerinfo
agouti-cli listunspent
agouti-cli sendtoaddress <address> <amount>
```

---

## 6. Masternode Setup Guide

A masternode requires:
- A server with a static IP address running `agoutid`
- A controller wallet holding the 3,000 AGU collateral
- The collateral UTXO must remain unspent

### Step 1 — Prepare the collateral

On the controller wallet, send exactly **3,000 AGU** to a new address you control.
Wait for **15 confirmations**.

```bash
agouti-cli getnewaddress
agouti-cli sendtoaddress <new-address> 3000
```

### Step 2 — Generate a masternode key

```bash
agouti-cli masternode genkey
```

Save the output — this is the `masternodeprivkey`.

### Step 3 — Locate the collateral output

```bash
agouti-cli masternode outputs
```

Note the transaction ID and output index (usually 0 or 1).

### Step 4 — Configure the hot server

On the remote server, add to `~/.agouti/agouti.conf`:

```ini
masternode=1
masternodeprivkey=<key from step 2>
masternodeaddr=<server-IP>:5151
rpcuser=agoutiuser
rpcpassword=changethispassword
daemon=1
listen=1
server=1
```

Restart `agoutid` on the server and wait for it to sync.

### Step 5 — Configure the controller wallet

Add to `~/.agouti/masternode.conf` (one line per masternode):

```
<alias> <server-IP>:5151 <masternodeprivkey> <collateral-txid> <output-index>
```

Example:

```
mn1 203.0.113.10:5151 92Jhdk...privkey... a3f8c1...txid... 0
```

### Step 6 — Start the masternode

From the controller wallet:

```bash
agouti-cli masternode start-alias mn1
```

### Step 7 — Verify

On the server:

```bash
agouti-cli masternode status
```

Expected output includes `"status": "Masternode successfully started"`.

On the network:

```bash
agouti-cli masternodelist | grep <server-IP>
```

### Masternode rewards

The masternode receives its share of the block reward approximately every
`(total_masternodes × 60 seconds)` on average. Payments are deterministic and rotate
through the masternode list.

---

## 7. Governance and Budget System

Agouti's on-chain governance allows masternode operators to vote on budget proposals.
Approved proposals are paid from the monthly budget pool at the superblock.

### Budget cycle

- **Cycle length:** 43,200 blocks (~30 days) on mainnet
- **Superblock:** the last block of each cycle; approved proposals are paid here
- **Budget pool:** 2% of each block's subsidy accumulates over the cycle

### Submitting a proposal

#### Step 1 — Draft the proposal

```bash
agouti-cli mnbudget prepare \
  "<proposal-name>" \
  "<url>" \
  <payment-count> \
  <start-block> \
  "<payment-address>" \
  <monthly-payment-AGU>
```

This returns a `preparationTxHash`. The command creates a collateral transaction
burning **50 AGU**. Wait for **6 confirmations** before proceeding.

#### Step 2 — Submit the proposal

```bash
agouti-cli mnbudget submit \
  "<proposal-name>" \
  "<url>" \
  <payment-count> \
  <start-block> \
  "<payment-address>" \
  <monthly-payment-AGU> \
  "<preparationTxHash>"
```

This returns a `proposalHash`.

#### Step 3 — Finalise

Once sufficient votes are gathered, finalise the budget:

```bash
agouti-cli mnfinalbudget suggest
```

A **50 AGU finalisation fee** is paid at this step. Wait for 6 confirmations.

### Voting on proposals

```bash
agouti-cli mnbudget vote <proposalHash> yes|no|abstain
```

To vote with a specific masternode alias:

```bash
agouti-cli mnbudget vote-alias <proposalHash> yes <alias>
```

A proposal passes when `yes_votes - no_votes > total_masternodes / 10` (10% net
majority of the eligible masternode count).

### Checking proposal status

```bash
agouti-cli mnbudget show
agouti-cli mnbudget getvotes <proposalHash>
agouti-cli mnfinalbudget show
```

### Budget payment

At the superblock the daemon automatically includes budget payments in the coinbase
transaction. No manual action is required.

---

## 8. StakePointer Guide

The StakePointer protocol activates at block **2,690,000** on mainnet. After this
height, each Proof-of-Stake block must be signed by a masternode operator rather than
by an ordinary staking UTXO. Coin-age grinding is eliminated.

### How it works

1. The block must include a `StakePointer` structure identifying:
   - A recent masternode reward output (`txid`, `nPos`) from the block at `hashBlock`,
     within the **validity window** (blocks `[tip − 4320, tip − 15]`)
   - The masternode's collateral public key (`pubKeyCollateral`)
   - An optional signing delegation key (`pubKeyProofOfStake`)

2. The kernel hash is computed over:
   `H(outpoint.hash || outpoint.n || stakeModifier || blockTime || stakeTime)`
   weighted against the fixed **3,000 AGU collateral**, not coin age.

3. The block is signed with `pubKeyProofOfStake` (or `pubKeyCollateral` directly
   when no sign-over delegation is used).

4. Each stake pointer may only be used **once per session**. It is recorded in
   `mapUsedStakePointers` and rejected if resubmitted.

### Requirements

- The node must be a **registered, active masternode**.
- The masternode must have received at least one block reward within the last
  4,320 blocks (~3 days).
- The controlling wallet must hold the collateral key (or a delegated signing key).

### Configuration

Add to `agouti.conf` on the staking/masternode server:

```ini
staking=1
reservebalance=0
```

No separate StakePointer configuration is required once the node is a running
masternode. The wallet selects a valid pointer automatically.

### Staking workflow (post-fork)

The miner loop:

1. Calls `GetStakePointer()` to locate a valid masternode payment output in the
   wallet within the validity window.
2. Constructs a version-5 block with the `stakePointer` field populated.
3. Verifies the kernel hash meets target difficulty.
4. Signs the block with the proof-of-stake key.
5. Broadcasts the block.

### If you missed the validity window

If the node was offline and the most recent masternode payment fell outside the
4,320-block window, wait for the next payment cycle. To rescan for payments after
returning online:

```bash
agouti-cli stop
agoutid -rescan
```

### Block version

Post-fork PoS blocks use `nVersion = 5`. Pre-fork blocks remain `nVersion ≤ 4`.
The `stakePointer` field is only serialised when `nVersion >= 5`.

### Upgrade path for existing masternodes

1. Update to this release.
2. Ensure `staking=1` is in `agouti.conf`.
3. At block 2,690,000 the node begins StakePointer staking automatically.
4. No collateral transaction or restart is required.

---

## 9. Wallet Synchronisation Notes

### Pre-fork blocks (nVersion ≤ 4)

The `stakePointer` field is absent from serialisation. All existing sync paths are
unaffected. Upgraded nodes re-serialise pre-fork blocks identically.

### Post-fork blocks (nVersion 5)

The `stakePointer` field is appended after `vchBlockSig` in the serialisation stream,
gated on `nVersion >= 5`. Nodes running the old software will reject version-5 blocks.
Upgrading before block 2,690,000 is required.

### mapUsedStakePointers (reorg safety)

The in-memory map preventing stake pointer reuse is rebuilt from `ConnectBlock` during
initial sync and on restart. It is not persisted to disk. After a clean restart the
guard reconstructs itself as blocks are connected. For a heavily reorged chain, the map
may temporarily allow a theoretically duplicate pointer until the corresponding block
is reconnected — this is acceptable because the kernel hash still provides economic
security.

### Rescan after upgrade

If the wallet was not running during a masternode payment period near the fork, run:

```bash
agouti-cli stop
agoutid -rescan
```

This ensures `GetStakePointer()` can locate eligible payment outputs.

---

## 10. Network Parameters Reference

### Mainnet

| Parameter                     | Value          |
|-------------------------------|----------------|
| Chain ID                      | main           |
| Network magic                 | 09 39 09 18    |
| P2P port                      | 5151           |
| RPC port                      | 6161           |
| Last PoW block                | 500            |
| Masternode collateral         | 3,000 AGU      |
| Stake minimum age             | 1 hour         |
| Coin maturity                 | 15 blocks      |
| Budget cycle                  | 43,200 blocks  |
| Budget proposal fee           | 50 AGU         |
| Budget finalisation fee       | 50 AGU         |
| MN/staker split (pre-2565000) | 90 / 10        |
| MN/staker split (post-2565000)| 50 / 50        |
| StakePointer fork height      | 2,690,000      |
| StakePointer validity window  | 4,320 blocks   |
| Kernel modifier lookback      | 100 blocks     |

### Testnet

| Parameter                     | Value         |
|-------------------------------|---------------|
| P2P port                      | 7777          |
| RPC port                      | 38843         |
| Last PoW block                | 200           |
| Budget cycle                  | 720 blocks    |
| Budget proposal fee           | 50 AGU        |
| StakePointer fork height      | 300           |
| StakePointer validity window  | 200 blocks    |
| Kernel modifier lookback      | 10 blocks     |

### Regtest

| Parameter                     | Value    |
|-------------------------------|----------|
| P2P port                      | 51436    |
| Last PoW block                | 200      |
| StakePointer fork height      | 201      |
| StakePointer validity window  | 50 blocks|
| Kernel modifier lookback      | 5 blocks |

---

## Licence

Distributed under the MIT software licence. See `COPYING` for details.
