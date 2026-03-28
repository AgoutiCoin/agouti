# TASK: StakePointer PoS System

## Objective

Replace the current coin-age PoS kernel with a masternode-bound StakePointer system.
Only active masternodes (proved by a recent reward payment) may produce PoS blocks.
This eliminates coin-age grinding, long-range attacks, and non-MN staking in one architectural change.

---

## Background

### Current system (coin-age kernel)
Kernel hash: `H(nStakeModifier || nTimeBlockFrom || prevoutIndex || prevoutHash || nTimeTx)`
- Any UTXO of sufficient age and value can stake
- Vulnerable to grinding, coin-age accumulation, timestamp manipulation
- PoS is open to all coin holders

### Target system (StakePointer kernel)
Kernel hash: `H(outpoint.hash || outpoint.n || stakeModifier || nTimeBlockFrom || nTimeStake)`
- Only masternodes may stake; they prove eligibility by pointing to a recent reward payment
- Stake pointer expires after `nStakePointerValidityPeriod` blocks (~3 days)
- Pointers cannot be reused across blocks
- Block is signed by the masternode collateral key

### Key parameters
| Parameter | Value | Rationale |
|---|---|---|
| `nStakePointerValidityPeriod` | 4320 blocks | ~3 days at 60s/block; MN must stay active |
| `nKernelModifierOffset` | 100 blocks | Same as `nMaxReorganizationDepth`; prevents modifier grinding |
| Min pointer age | 100 blocks | Must predate any reorg; equals `nMaxReorganizationDepth` |
| Masternode collateral | 3000 COIN | As per `IsVinAssociatedWithPubkey` |
| New block version | 5 | StakePointer blocks; gates new serialisation and validation |
| Fork height (mainnet) | TBD | Set when ready to activate |

---

## Scope

### Option B — address-match verification
The StakePointer carries `nPos` (the vout index of the MN reward payment in the historical block).
Validation verifies that `pubKeyCollateral` matches the address at `vout[nPos]`.
No change to `masternode-payments.cpp` or payment vout layout is required.

---

## Files

### 1. NEW `src/stakepointer.h`

New class `StakePointer`:

```
Fields:
    uint256  hashBlock              -- hash of the block that paid this MN
    uint256  txid                   -- txid of the payment transaction
    unsigned int nPos               -- vout index of the MN reward output
    CPubKey  pubKeyCollateral       -- MN collateral public key
    CPubKey  pubKeyProofOfStake     -- delegated signing key (may equal pubKeyCollateral)
    vector<unsigned char> vchSigCollateralSignOver
                                    -- signature from collateral key granting rights
                                       to pubKeyProofOfStake (empty if same key)

Methods:
    void SetNull()
    bool IsNull() const
    bool VerifyCollateralSignOver() const
        -- verifies that pubKeyCollateral signed a message granting pubKeyProofOfStake
           permission to produce blocks; called only when the two keys differ
    ADD_SERIALIZE_METHODS (all fields)
```

The sign-over message to verify/create:
`"StakePointer sign-over:" + pubKeyProofOfStake.GetID().ToString()`

This allows a masternode operator to stake with a separate hot key while keeping the
collateral key cold, without moving collateral funds.

---

### 2. `src/primitives/block.h`

Add `stakePointer` member to `CBlock`:

```cpp
StakePointer stakePointer;   // valid only on version-5 PoS blocks
```

Serialisation change — **CONSENSUS RISK**:

```cpp
template <typename Stream, typename Operation>
inline void SerializationOp(...) {
    READWRITE(*(CBlockHeader*)this);
    READWRITE(vtx);
    if (vtx.size() > 1 && vtx[1].IsCoinStake()) {
        READWRITE(vchBlockSig);
        if (nVersion >= 5)
            READWRITE(stakePointer);   // NEW — version-gated
    }
}
```

`SetNull()` must call `stakePointer.SetNull()`.

Note: `nVersion` for new PoS blocks must be set to 5 in the miner.

---

### 3. `src/kernel.h` / `src/kernel.cpp`

Add new function alongside existing code (do not remove old kernel yet):

```cpp
// New StakePointer kernel — no coin-age, no tx.nTime
bool CheckStakePointerKernelHash(
    unsigned int nBits,
    const COutPoint& outpoint,       // the stake pointer's collateral outpoint
    const CBlockIndex* pindexFrom,   // block index of the pointer's hashBlock
    const CBlockIndex* pindexPrev,   // previous block (nHeight - 1)
    uint32_t nTimeStake,             // block.nTime of the new block
    uint256& hashProofOfStake);
```

Kernel hash construction:
```
stakeModifier = pindexPrev->GetAncestor(pindexPrev->nHeight - nKernelModifierOffset)->GetBlockHash()
ss << outpoint.hash << outpoint.n << stakeModifier << pindexFrom->GetBlockTime() << nTimeStake
hashProofOfStake = Hash(ss.begin(), ss.end())
```

Target check (no coin-age multiplier):
```
bnTarget.SetCompact(nBits)
hashProofOfStake < nMasternodeCollateral * bnTarget
```

where `nMasternodeCollateral = 3000 * COIN`.

The existing `CheckStakeKernelHash` is retained unchanged for blocks below the fork height.

---

### 4. `src/main.cpp`

#### 4a. New global

```cpp
// Maps pointer hash -> blockhash that consumed it; prevents pointer reuse
std::map<uint256, uint256> mapUsedStakePointers;
```

Must be populated during `ConnectBlock` and pruned during `DisconnectBlock`.

#### 4b. New helper

```cpp
bool IsStakePointerUsed(const CBlockIndex* pindex, const COutPoint& outpointPointer);
```

Computes pointer hash from `outpointPointer`, checks `mapUsedStakePointers`.
Returns false (not used) if not found. Follows Crown's logic.

#### 4c. New function `CheckBlockProofPointer`

```cpp
bool CheckBlockProofPointer(
    const CBlockIndex* pindex,
    const CBlock& block,
    CPubKey& pubkeyOut,
    COutPoint& outpointStakePointerOut);
```

Validation sequence:
1. `stakePointer.hashBlock` must be in `mapBlockIndex` and on `chainActive`
2. `pindexFrom->nHeight >= pindex->nHeight - nStakePointerValidityPeriod`
   (pointer not too old)
3. `pindexFrom->nHeight <= pindex->nHeight - nMaxReorganizationDepth`
   (pointer not too recent — outside reorg window)
4. Pointer must not be from a budget payment block
5. `IsStakePointerUsed` must return false
6. Read `blockFrom` from disk; locate transaction by `stakePointer.txid`
7. Check `vout[stakePointer.nPos]` exists
8. Extract destination; derive `CBitcoinAddress` from `stakePointer.pubKeyCollateral`
9. Address match: collateral address == reward output address
   (Option B verification — no fixed slot dependency)
10. If `pubKeyProofOfStake != pubKeyCollateral`:
    call `stakePointer.VerifyCollateralSignOver()` — must pass
11. Set `pubkeyOut` and `outpointStakePointerOut`; return true

#### 4d. New function `CheckStake`

```cpp
bool CheckStake(const CBlockIndex* pindex, const CBlock& block, uint256& hashProofOfStake);
```

Sequence:
1. `block.vtx[0].vout[0].nValue == 0` (coinbase must be empty for PoS)
2. Call `CheckBlockProofPointer` → get `pubkeyMasternode`, `outpointStakePointer`
3. Verify the payment the pointer references is a legitimate masternode payment
   (output value == 3000 COIN OR use `IsMasternodePayment` if available)
4. `CheckBlockSignature(block, pubkeyMasternode)` — block signature must be valid
5. `CheckStakePointerKernelHash(...)` — kernel hash must meet target

#### 4e. Integration into `CheckBlock` / `AcceptBlock`

In `CheckBlock`:
- For `nVersion >= 5` PoS blocks (above fork height): call `CheckStake`
- For PoS blocks below fork height: use existing `CheckProofOfStake` path

In `ConnectBlock`:
- After successful connect: record `mapUsedStakePointers[pointerHash] = block.GetHash()`

In `DisconnectBlock`:
- Remove the entry from `mapUsedStakePointers`

---

### 5. `src/chainparams.h` / `src/chainparams.cpp`

Add to `CChainParams`:

```cpp
int nKernelModifierOffset;          // blocks before pointer used as modifier source
int nStakePointerValidityPeriod;    // max age of a valid stake pointer in blocks
int nStakePointerForkHeight;        // activation height for StakePointer consensus
```

Add accessor methods:
```cpp
int KernelModifierOffset() const { return nKernelModifierOffset; }
int ValidStakePointerDuration() const { return nStakePointerValidityPeriod; }
int StakePointerForkHeight() const { return nStakePointerForkHeight; }
```

Mainnet values:
```cpp
nKernelModifierOffset       = 100;
nStakePointerValidityPeriod = 4320;   // ~3 days
nStakePointerForkHeight     = TBD;    // set before activation
```

Testnet values:
```cpp
nKernelModifierOffset       = 10;
nStakePointerValidityPeriod = 200;    // shorter for testing
nStakePointerForkHeight     = TBD;
```

---

### 6. `src/wallet.cpp`

The staker must find a valid stake pointer from its own recent masternode reward payments.

New function `GetStakePointer`:

```cpp
bool CWallet::GetStakePointer(StakePointer& stakePointerOut) const;
```

Logic:
1. Iterate wallet transactions; find outputs paid to `pubKeyCollateralAddress`
   with value == 3000 COIN within the last `nStakePointerValidityPeriod` blocks
2. Skip if from a budget block; skip if already used (`IsStakePointerUsed`)
3. Skip if too recent (within `nMaxReorganizationDepth` of tip)
4. Build `StakePointer`:
   - `hashBlock` from `wtx.hashBlock`
   - `txid` from `wtx.GetHash()`
   - `nPos` from the matching vout index
   - `pubKeyCollateral` from the wallet key for that output
   - `pubKeyProofOfStake` = `pubKeyCollateral` (hot key delegation is optional; implement later)
   - `vchSigCollateralSignOver` = empty (same key)
5. Return first valid pointer found

This function is called from the stake miner before block assembly.

---

### 7. `src/miner.cpp`

In `CreateNewBlock` / stake miner loop:

1. Call `wallet.GetStakePointer(stakePointer)` — if none found, skip (this node is not
   an eligible MN staker at this time)
2. Set `pblock->nVersion = 5`
3. Assign `pblock->stakePointer = stakePointer`
4. Use `pubKeyProofOfStake` (from the pointer) to sign the block via `SignBlock`
5. Pass `outpointStakePointer` to `CheckStakePointerKernelHash` to compute proof

---

## Consensus risks

| Change | Risk level | Notes |
|---|---|---|
| `stakePointer` field in `CBlock` serialisation | **HIGH** | Version-gated on `nVersion >= 5`; old nodes reject new PoS blocks — intended hard fork |
| Kernel hash function change | **HIGH** | Entirely different inputs; all nodes must upgrade before fork height |
| `mapUsedStakePointers` state | **MEDIUM** | Must be correctly populated/pruned across reorgs |
| Block version bump to 5 | **LOW** | Existing version check logic gates on `> 3` for accumulator; must not conflict |

---

## Implementation order

1. `src/stakepointer.h` — data structure and serialisation
2. `src/chainparams.h/.cpp` — new parameters (no consensus change)
3. `src/primitives/block.h` — add field and version-gate serialisation
4. `src/kernel.h/.cpp` — new kernel function
5. `src/main.cpp` — `IsStakePointerUsed`, `CheckBlockProofPointer`, `CheckStake`, integration
6. `src/wallet.cpp` — `GetStakePointer`
7. `src/miner.cpp` — integrate into block assembly
8. Set `nStakePointerForkHeight` and test on testnet before mainnet activation

---

## Out of scope

- ProofTracker / BlockWitness witness signatures (stake repackaging defence — separate task)
- Key delegation (sign-over to a hot key) — data structure supports it; wallet logic deferred
- Systemnode / second node tier
- Any changes to `masternode-payments.cpp` or payment vout ordering
