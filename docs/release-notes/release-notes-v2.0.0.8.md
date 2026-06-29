# DigitalNote XDN v2.0.0.8 Release Notes

DigitalNote v2.0.0.8 is a **major consolidated release** that supersedes
all prior development. The active mainnet is on v2.0.0.6; v2.0.0.7 and
intermediate v2.0.0.8 work never shipped publicly. This release bundles
the entire accumulated body of fixes, features, and improvements built
between v2.0.0.6 and the v2.0.0.8 sealing point.

The headline feature is **masternode voted-payee consensus**, an explicit
signed-voting protocol that replaces the loose locally-computed payment
selection of prior versions. The release also includes substantial work
in the proof-of-stake path, the wallet GUI, balance correctness,
masternode peer handling, the build and release infrastructure, and a
purpose-built v2.0.0.8 testnet established specifically for validating
the new consensus mechanism.

This release is **chain-compatible** with v2.0.0.6 until the new
consensus feature is activated by the network spork key.

---

## 🗳️ Masternode Voted Consensus (headline feature)

v2.0.0.8 introduces **voted-payee consensus** for masternode payments.

In v2.0.0.6, the masternode paid in each block was chosen by each node
locally with only loose coordination, which could lead to disagreement
about who should be paid. v2.0.0.8 replaces this with an explicit,
signature-verified voting system based on **per-masternode ordered
queues** of upcoming payees:

- Each masternode broadcasts a **signed queue** of the next 10 payees,
  computed by the same deterministic forward-simulation on every node.
- Every node tallies the queues it receives by position.
- When a supermajority of eligible masternodes agree on the payee for a
  given position, that payee becomes the **canonical** payee for that
  block height.
- Block validation then enforces the agreed payee.

Because every masternode computes the schedule by the same deterministic
rule (and advances each chosen payee's simulated last-paid height before
picking the next position), the queues agree position-for-position and
payments rotate cleanly and fairly. The result is that all nodes agree
on masternode payments by an auditable, signature-verified vote rather
than by independent local guesswork.

### Activation

Voted consensus does **not take effect** until a set activation height.
Until activation:
- The network behaves exactly as v2.0.0.6 did for payment selection.
- Upgrading to v2.0.0.8 is safe and the new behaviour is inert on
  existing chain history.

After activation:
- Every block at or above the activation height is validated against
  the voted canonical payee.
- If voted consensus has insufficient coverage at activation height
  (e.g. partial network upgrade, transient masternode outage), the
  chain falls back to the legacy payment rule until coverage forms.
  This is a deliberate **soft activation** to avoid chain stalls.

The activation height has a fixed floor (`INT_MAX` in this release,
meaning the feature is off until further notice) and can only be
brought *forward* by the network spork key (`SPORK_15`), never pushed
back beyond the floor. The activation is a one-way upgrade.

This feature has been validated on the v2.0.0.8 testnet through
multi-day soak runs, stress tests covering peer churn and restarts,
SPORK_14/15 rehearsal, and a controlled activation crossing at testnet
height 1837 — block 1837 (PoW) and block 1838 (PoS) both accepted
with 7/7 voter consensus.

---

## ⛏️ Masternode Fixes

### Payment selection and last-paid tracking

- **Fixed: `getblocktemplate` always returned the same masternode
  winner** — the old code used the genesis block hash (height 0) in its
  score calculation, causing the same masternode to win every block
  indefinitely. Now reads from the same authoritative payment record as
  block validation, with a `FindOldestNotInVec` fallback for transition
  periods.
- **Fixed: all masternodes displayed the same "last paid" time** — two
  bugs in the same field: a copy constructor was overwriting the real
  last-paid time with the current time on every copy, and a peer-driven
  constructor was leaving the field as uninitialised stack memory until
  first payment. Both fixed.
- **Fixed: "last paid" display did not reflect voted consensus
  payments** — a second redundant `nLastPaid` writer in `main.cpp`
  `ProcessBlock` was clobbering the correct value set by
  `OnBlockConnected`. The redundant writer is removed; `OnBlockConnected`
  is now the single authority.

### Masternode start and activation

- **Fixed: masternodes stopping when collateral wallet version differs
  from remote daemon** — the version check was too strict; masternodes
  now activate correctly across minor version differences between
  wallet and daemon.
- **Fixed: masternode start failing with "could not allocate vin" when
  collateral is locked** — the candidate-selection path no longer
  filters out locked outputs. Locked collaterals are normal (they're
  the wallet's own protective signal that an MN is in active use), and
  the start path now correctly recognises them as valid.
- **Fixed: local masternode start ignored masternode.conf** — when
  running a hot wallet with `-masternode=1` on a host whose wallet held
  multiple collaterals, the local masternode could bind to an arbitrary
  collateral, frequently one belonging to a declared remote masternode.
  Two daemons then signed votes with one collateral identity, producing
  ban storms across the fleet. The collateral selection now correctly
  honours `masternode.conf`.

### Collateral lock semantics

- **Remote masternode start now auto-locks the collateral** — local
  masternodes always did this, but the remote-Register code path
  skipped the lock. Now both paths protect the collateral identically.
  Existing remote-MN setups will pick up the lock the next time the
  controller wallet ReRegisters.
- **Stopping a masternode no longer auto-unlocks the collateral** —
  locks set on a masternode's collateral UTXO are user data and now
  persist across stop/start. To spend a previously-locked collateral,
  use the new Locked Outputs dialog (Tools → Locked Outputs...), or the
  Lock/Unlock context menu in the Masternodes tab.

### Peer reliability and ban-storm prevention

- **Fixed: `dseg` re-ask penalty** — a peer re-requesting the masternode
  list inside the rate-limit window was scored `Misbehaving(34)` — a
  third of a ban — for what is normal behaviour after a restart or
  dropped connection. Three such re-asks reached the ban threshold.
  Duplicate requests are now rate-limited and ignored without penalty.
- **Fixed: `dseg` requester-side retry** — `DsegUpdate` used a 3-hour
  cool-off unconditionally, so a lost request left the node with no
  masternode list for three hours. The retry interval now adapts to
  list state: 3-minute retries while empty, full 3 hours once populated.
  This is the most directly visible fix for a real chain phenomenon:
  during long chain stalls on prior versions, the stale masternode
  list could mean no masternode could be located at payout time, and
  the payment fell through to the devops address via the
  consensus-defined fallback. The adaptive retry restores the
  masternode list within minutes after a stall, restoring normal
  payouts immediately.
- **Fixed: invalid-signature `mnvote` ban-storm** — an `mnvote` with
  a `CheckSignature` failure scored `Misbehaving(100)` (instant ban).
  Observed on testnet as ban storms where honest relayers were being
  banned for forwarding votes they could not have known were
  unverifiable. The relay path now blocks junk-signature votes from
  propagating; the residual misbehaviour score is lowered to a
  rate-limiting `5`.
- **Fixed: masternode-payee strict-check firing pre-activation
  (CW12)** — v2.0.0.8's earlier "Spec C D2" change unconditionally
  enabled a strict masternode-payee validation check that
  v2.0.0.6 had effectively never run (the v2.0.0.6 gate flag
  `fMnAdvRelay` defaulted to false and was never set true in
  production). Spec C D2 was correct in principle ("consensus
  enforcement should not ship behind an undocumented flag") but
  the implementation missed adding an explicit activation-height
  gate, leaving the strict check firing in the warmup-elapsed
  window regardless of whether voted-consensus enforcement was
  active. Combined with normal masternode-broadcast propagation
  lag, this produced cascading peer bans: observed on testnet
  block 206 where **5 of 8 nodes** banned the LAN gateway IP
  (via NAT hairpin) and stalled for hours. The fix gates the
  strict weak check on `GetEffectiveVotedConsensusActivationHeight()`
  — the same height at which voted-consensus enforcement
  activates. Pre-spork on mainnet (default floor `INT_MAX`),
  the check never fires — matching v2.0.0.6 effective mainnet
  behaviour precisely. Post-spork the check engages in
  lockstep with voted-consensus enforcement.
- **Fixed: NAT-hairpin address pollution** — inbound port-forwarded
  connections arrive with their source address rewritten to the LAN
  gateway, and that address was being entered into `addrman` and
  gossiped network-wide. The address admission is now gated on
  `addrFrom.IsRoutable()`; the connection itself still flows, only the
  bogus address is prevented from propagating.
- **Fixed: dsee processing during initial block download** — a node
  still syncing was scoring `Misbehaving(20)` on valid dsee messages
  whose collateral confirmation depth hadn't yet been reached locally.
  Now gated on `!IsInitialBlockDownload()`.
- **Fixed: peer handshake gap on queue catch-up** — a v2.0.0.8 node
  that connected to peers received only future broadcasts, not the
  peer's in-flight queue inventory. A freshly-restarted or
  newly-connected node would therefore start with an empty queue map
  for already-in-flight heights and defer block production until new
  schedules arrived. The handshake now includes a `getmnqueues` request
  so peers respond with their current queue inventory.

### Equivocation handling

- **Fixed: equivocation auto-clear was dead code in steady state** —
  the documented Path A recovery (cleared by `OnFreshDsee` on a
  legitimate later broadcast) was wired only into the new-MN
  registration path. Routine dsee re-broadcast and dseep heartbeats
  never invoked it. The Path A wiring is now correctly invoked from
  all three call sites: new-MN add, dsee known-MN update, and dseep
  heartbeat.
- **Fixed: equivocation false-positive on legitimate re-broadcast** —
  the equivocation detection branch was treating any second queue at
  the same height with a different hash as malicious, even though
  M1Q spec S10.1 explicitly permits legitimate re-broadcast after a
  brief chain reorg with a refreshed timestamp or schedule. Observed
  on testnet 2026-06-02 as all 7 MNs being simultaneously flagged
  during a normal 4-second 4305→4306 advance with no local disconnect.
  Detection is replaced with **newer-wins replacement** based on
  `nTimeSigned`: a later legitimate broadcast replaces an earlier one
  rather than marking the broadcaster as a malicious equivocator.

---

## 🪙 Masternode Collateral Tools

- **2,000,000 XDN incoming-collateral popup** — when an incoming
  transaction lands a UTXO of exactly the masternode collateral amount,
  you'll be prompted whether to lock it. Three choices: lock now, ask
  again next time, or never ask for this wallet.
- **Lock column in My Master Nodes table** — at-a-glance visibility of
  every configured masternode's collateral lock state, with
  closed-padlock icon and "Locked"/"Unlocked" text.
- **Lock / Unlock context menu** — right-click any row in the My Master
  Nodes table to lock or unlock that masternode's collateral.
- **NEW: Locked Outputs dialog** — `Tools → Locked Outputs...` opens a
  comprehensive view of every locked output in the wallet. Each row
  shows: lock status, address, label, amount, type classification
  (Masternode: `<alias>` / 2M XDN not in masternode.conf / Other locked
  output), and full TXID:vout. Click the lock cell or double-click any
  cell to unlock — with a tier-appropriate confirmation dialog.
  Right-click for Copy txid / Copy address / Copy amount. Watch-only
  outputs are filtered out.
- **Standard selection model in My Master Nodes table** — click selects
  (replacing previous selection), Ctrl-click adds, Shift-click
  range-selects. Replaces the older "every click toggles"
  multi-selection that required explicit unselect.

---

## ⛏️ Proof-of-Stake Reliability

- **Fixed: rare staker stalls under load (Class B lock-order deadlock)**
  — under specific multi-thread timing, the staking thread could
  deadlock with peer message processing across `cs_main` and
  `voteTracker.cs`. Diagnosed via gdb on a symboled debug build after
  three observed wedges at ~18-hour intervals. The vote-tracker's
  consensus-read functions now self-protect with `LOCK2(cs_main, cs)`
  so the canonical lock-acquisition order (`cs_main` before
  `voteTracker.cs`) holds in every code path. Confirmed clean through
  multi-day soak after the fix.
- **Fixed: PoS coinstake input stale spent-flag** — the consumed kernel
  input was being marked spent only via the catch-all per-block
  `FixSpentCoins` reconciliation, which left a window where the wallet
  could attempt to re-stake the same UTXO. The mark is now targeted
  directly at the kernel input in the coinstake construction path,
  eliminating the reliance on band-aid reconciliation.
- **Fixed: payee streak / chain stall at activation crossing** — a
  pre-activation last-paid clamp was collapsing multiple masternodes to
  the same rank key, causing the smallest-vin tiebreak to freeze
  selection on one MN for the entire vote-lookahead window. The clamp
  is removed; the chain-derived ranking is correct in every epoch
  without normalisation. Confirmed at the testnet activation crossing
  with a clean rotation.
- **Fixed: post-activation payee streak artifact (M1Q queue redesign)**
  — the per-height vote path ranked on `mapLastPaidHeight`, which is
  written only when a block connects, while the vote/selection for a
  height runs lookahead blocks earlier. The selector's last-paid view
  was therefore lookahead-blocks behind the height it selected for, so
  a masternode could be re-selected for every height in that lag window
  until its own payment connected. Resolved by the **M1Q queue redesign**
  (forward-simulated ordered queues) which removes the lag
  structurally.
- **Fixed: staker mint-retry storm on too-early blocks** —
  `Velocity()` enforces a minimum block spacing of 45 seconds; a
  too-early PoS block is rejected. The staker thread was discarding
  the rejection result and retrying every 500 ms, producing a CPU/log
  storm until wall-clock crossed the threshold. The staker now backs
  off until the spacing is satisfied.
- **Fixed: post-restart staking visibility** — the staking icon
  previously showed "not staking" until the first successful coinstake
  search interval elapsed, often appearing inactive for several
  minutes after a healthy startup. The icon now reflects the actual
  thread state: a clock during warm-up, the hammer once actively
  searching, the inactive icon when staking is genuinely off.
- **Fixed: staking icon stuck on "warming up" for healthy stakers** —
  a counter the icon previously read was only updated when the kernel
  search exited *without* finding a block, so on a wallet that found
  blocks successfully, the counter stayed at zero and the icon
  displayed the "warming up" clock indefinitely even while blocks were
  being produced. The icon now reads directly from the staking
  thread's heartbeat via a proper state machine.
- **Fixed: staking icon showed "no mature coins" while actively
  staking** — on a frequent-staking wallet where all stakeable balance
  was permanently inside the maturity window, `nWeight` reported 0 and
  the icon showed the "no mature coins" state despite the wallet
  successfully staking every few minutes. The icon now distinguishes
  "actively staking, coins maturing" (clock) from "no stakeable
  balance" (none).
- **Improved: expected-time-between-blocks tooltip** — replaces the
  previous nonsense values (one observation: "1763 days 13 hours" on
  a healthy testnet staker) with a correct formula based on your
  wallet weight and the network's total stake weight.
- **Fixed: PoS staker did not resume after M1Q switch-over** — after
  the queue-based consensus replaced the per-height vote path, the
  staker's readiness gate still probed the old (now-unpopulated)
  per-height vote map and logged "deferring … vote tracker not ready"
  indefinitely. The gate now probes the queue path the validator
  actually consults.

---

## 🌐 Testnet — Established and Operational

**New in v2.0.0.8.** Prior versions of DigitalNote did not have a
functional testnet; the v2.0.0.8 voted-consensus feature genuinely
could not be validated without one, and bringing the testnet up was
substantial work in its own right.

- **Testnet rebuilt from genesis** — an earlier testnet attempt had
  accumulated forked PoS state, mutually-banned nodes, and chain
  history that could not be resynced past a stalled block (caused by
  the now-fixed VRX difficulty bug, see Network section). The fleet
  was wiped to genesis on a binary containing the determinism fixes
  and the voted-consensus work; a genesis-clean chain on the fixed
  binary is correct by construction.
- **Testnet parameters** — P2P port 28092, RPC port 28094, selected
  via the `-testnet` switch only. Genesis is 18 Jan 2019 with no
  premine (`SetEmpty()` coinbase). Masternode and devops payments
  are active from block 1. Voted-consensus activation floor is
  height 2000 (testnet only); a long pre-activation window allows
  vote propagation testing with zero fork risk.
- **Topology** — 6 masternodes + 1 PoS staker, with a permanent
  public explorer + seed node at `testnet.xdn-explorer.com` (added
  to the testnet `vSeeds` and the generated default testnet conf).
- **Bug fixes uncovered during testnet construction** — bringing up
  a real multi-node network surfaced an entire class of
  configuration-driven bugs that single-node testing could not have
  exposed: same-IP multi-MN connection rejection (`OpenNetworkConnection`
  was using an IP-only `FindNode` check that prevented all but one
  MN on a shared IP from connecting); NAT-hairpin address pollution;
  duplicate-masternode-identity faults from collateral selection
  ignoring `masternode.conf`; ban storms from over-aggressive scoring
  on routine peer behaviour; consensus enforcement gated behind a
  long-abandoned config flag (`-mnadvrelay`, defaulting to false, so
  enforcement could never engage). All such bugs are fixed in this
  release.
- **SPORK_14/15 activation rehearsal** — staged broadcast and
  propagation of SPORK_14 across the entire fleet, followed by a
  full controlled activation crossing at height 1837 using
  `SPORK_15` to bring the activation height forward from the 2000
  floor. The crossing succeeded: block 1837 (PoW) and block 1838
  (PoS) both accepted with 7/7 voter consensus, with the
  clamp-free payee selector rotating cleanly across the boundary.

---

## 🔐 BIP39 Recovery Phrase

- **24-word recovery phrase** generated automatically when encrypting
  a new wallet — shown once immediately after encryption, never shown
  again without password verification.
- **Recovery phrase unlocks your wallet** — both your password AND
  your 24-word recovery phrase can be used to unlock the wallet
  (wallet.dat must be present).
- **Existing wallet upgrade** — users with wallets encrypted in
  v2.0.0.6 or earlier can upgrade via `Settings → Recovery Phrase`.
  A one-time process that keeps your wallet encrypted throughout. If
  you decline, the prompt won't return for that wallet — you can
  still upgrade manually any time.
- **Password required to reveal phrase** — `Settings → Recovery
  Phrase` requires your password before displaying the 24 words.
- **Recovery phrase stable across password changes** — once your
  wallet has been BIP39-upgraded, the recovery phrase you wrote down
  stays valid even after you change your password.
- **`getrecoveryphrase` RPC command** — wallet must be unlocked
  before calling; returns your 24-word recovery phrase.

---

## 💰 Balance and Wallet Correctness

### Balance display

- **Fixed: balance could appear lower than the true balance after
  launching the wallet** — on wallets with a large number of
  transactions, the balance shown immediately after startup could be
  less than the real spendable balance. The discrepancy persisted for
  the rest of the session until the wallet was restarted with
  `-staking=0` or unlocked. Your coins were never at risk; the bug
  was in the displayed total only.
- **Fixed: imported watch-only addresses showed inflated credit until
  restart** — live-added transactions during a rescan now correctly
  populate the spend tracking.
- **Fixed: watch-only stake leaked into Spendable Stake column** —
  wallets with watch-only addresses no longer have their watch-only
  stake totals counted as spendable.
- **Fixed: P2PK outputs invisible to watch-only tracking** — stake
  rewards paid back via P2PK to imported watch-only addresses are now
  correctly tracked.
- **Fixed: rescan-discovered historical transactions timestamped at
  the wrong time** — old transactions discovered by a rescan are now
  stamped with their actual block time rather than being clamped to
  the most recent existing transaction's time.

### Spent-tracking correctness (new in v2.0.0.8)

- **Fixed: wallet balance over-reported after observing sends or
  stakes via gossip** — when a wallet observed a transaction
  involving its own keys via P2P gossip (rather than initiating it
  locally), the consumed inputs remained flagged as spendable in the
  wallet's per-transaction `vfSpent` tracking. The reported balance
  would over-state the true available balance by the value of the
  consumed inputs. Users could observe this after running
  `repairwallet`, which would "mysteriously" reduce their reported
  balance. The wallet's balance and coin-selection readers now
  consult the automatic global spend-tracking map (`mmTxSpends`),
  which is populated as transactions are added to the wallet
  regardless of source. The displayed balance now reflects true
  on-chain state automatically, without periodic `repairwallet`
  invocations.
- **Fixed: PoS coinstake input stale spent-flag** — see Proof-of-Stake
  Reliability above. The writer-side companion to the reader fix.
- **Improved reorg robustness** — the new spent-tracking reader is
  reorg-safe: outputs whose consuming transaction was orphaned during
  a reorg correctly drop back to unspent without manual repair.
- **Spendable and watch-only balance now share the same spent-tracking
  semantics** — the watch-only readers had already migrated to the
  `mmTxSpends`-based reader in earlier work; the spendable readers
  catch up in this release. Both balance lines now derive from the
  same automatic, reorg-safe source of truth, eliminating a
  long-standing asymmetry where watch-only balance was correct while
  spendable balance could drift on gossip-observed activity. This was
  audited against the v2.0.0.7 watch-only fixes (P2PK IsMine fallback,
  spendable/watch-only stake separation, RemoveWatchOnly three-phase
  prune, AvailableCoinsForStaking lock-coin semantics) and confirmed
  non-regressing.

### Coin selection

- **Fixed: 2M payments excluded the entire transaction from staking**
  — only the specific output is excluded now (and only if the user
  has explicitly locked it), not all the change outputs alongside it.

### Unlock flow

- **Fixed: `CWallet::Unlock()` now tries all master keys** — changed
  from failing on first key mismatch to iterating all keys, enabling
  both password and recovery phrase to unlock the wallet.

---

## 🛠️ Rebuild Wallet (Maintenance)

- **New: `Tools → Compact Wallet`** — rebuilds your wallet.dat file
  to reclaim free pages and rebuild the internal structure. The
  result is usually a smaller and faster wallet, especially on
  long-running wallets that have accumulated many transactions.
- **What it does:** dumps every record in your wallet, validates the
  dump with a checksum, builds a fresh wallet, swaps it in, and
  rescans.
- **What it preserves:** all of it — private keys, addresses,
  balances, transaction history, watch-only addresses, locked
  outputs, the BIP39 mnemonic, address book entries. Encrypted
  wallets stay encrypted; you don't need to enter your password.
- **Safety:** before the swap, your original wallet is renamed to
  `wallet.dat.bak` in your data directory. If anything goes wrong,
  your old wallet is right there to fall back to. We recommend taking
  an independent backup as well before you start, just in case.
- **What to expect:** the wallet shuts down and restarts
  automatically. The rebuild itself can take minutes to hours
  depending on wallet size; the wallet is unusable during this time
  and shows a maintenance-mode splash screen. After the rebuild, a
  rescan runs to refresh the transaction cache. When the wallet
  finally opens normally, a one-shot dialog tells you the outcome.
- **For advanced users:** the same operation is available from the
  command line via `-rebuildwallet`, and the underlying dump and
  create primitives are exposed as hidden RPCs (`dumprawwallet`,
  `createfromdumpfile`) for testing and manual recovery workflows.

---

## 🌐 Network and Synchronisation

- **Fixed: difficulty-recovery curve never engaged in v2.0.0.6** —
  v2.0.0.6's `VRX_ThreadCurve` computed `difTime = blkTime - cntTime`
  where both inputs resolved to the same block on PoW retarget, so
  `difTime` was always zero and the stall-recovery loop never fired.
  This was the cause of long chain pauses on mainnet where mining
  difficulty stayed high through stalls instead of dropping
  progressively. v2.0.0.8 fixes the recovery curve so it engages
  correctly during real stalls, allowing the chain to auto-recover
  from periods where mining capacity couldn't keep up.
- **Fixed: VRX retarget became non-deterministic during resync** —
  the curve-engagement fix between v2.0.0.6 and v2.0.0.8 used the
  node's wall clock (`GetAdjustedTime()`) as the curve's stall-time
  input. This worked at mining time (miner's wall clock ≈ block
  timestamp) but during a from-genesis resync, the validator's wall
  clock is years ahead of the block being validated, the recovery
  loop engaged hard, and the validator computed a completely
  different `nBits` than the block carried. v2.0.0.8 makes the
  validator's retarget a deterministic function of committed block
  time, so re-validation reproduces the original computation
  exactly.
- **Extended historical `nBits` exception list** — because v2.0.0.6's
  broken curve and v2.0.0.8's working curve produce different `nBits`
  values for blocks that followed long stalls, mainnet history
  contains around 30 blocks whose committed `nBits` v2.0.0.8's
  correct computation disagrees with. These blocks were valid under
  v2.0.0.6's rules at the time they were mined; they are accepted by
  v2.0.0.8 via an extended height-exception list in `AcceptBlock`.
  The strict `nBits` check remains fully active for every other
  block, including all blocks mined post-v2.0.0.8 (which produce
  determinism-by-construction nBits values that always pass). The
  exception list is one-time archaeology, never grows after release.
- **Mining-side same-timestamp guarantee** — v2.0.0.8 finalises the
  block's `nTime` first, then computes `nBits` using that committed
  timestamp. Pre-fix, the miner computed `nBits` from
  `GetAdjustedTime()` early in `CreateNewBlock` while the validator
  used `pblock->nTime`; a ~3,600-second hourly-boundary window
  existed where the two could fall on opposite sides of a recovery
  step and produce different difficulty values, causing self-rejection
  on submission. The fix eliminates that residual risk by using the
  block's own committed time on both sides. After v2.0.0.8, miner
  and validator agree on `nBits` by construction.
- **New: devops address rotation — activates at block 1,400,000** —
  the consensus-defined devops payment recipient rotates to a fresh
  wallet at mainnet block 1,400,000 (estimated ~early September 2027
  at current block rate). The previous address has accumulated four
  years of transaction history and the wallet file has grown to
  approximately 720MB on disk even after repeated Compact Wallet
  runs. The new address (`dGoFPie9QZmQ1Ty1beqSHytxNruehpGtGa`) is a
  fresh wallet with no transaction history, restoring fast wallet
  operations. The rotation is automatic at the activation block — no
  operator action required. Pool operators and exchanges who pay or
  receive directly from the devops address should update their
  references; everyone else's blocks include the correct address
  automatically via the consensus rule. On testnet the same rotation
  fires at block 100 to allow live validation of the mechanism within
  the first hour of testnet operation.
- **New: strict devops-address check re-enabled, height-gated** —
  the validator's strict comparison of the block's devops payment
  recipient against the canonical expected address (commented out
  since pre-v2.0.0.6 to avoid breaking resync past historical
  transition irregularities) is restored in v2.0.0.8 and gated to
  activate at the rotation block. Pre-rotation chain history
  (everything before block 1,400,000 on mainnet) continues to
  validate leniently — log-only on any mismatch — preserving full
  from-genesis resync compatibility including the v1.0.1.5
  transition irregularities (mid-2019) and the v1.0.4.2
  chain-correction (block 403117, mid-2021). Post-rotation
  (1,400,000 and beyond on mainnet, 100 and beyond on testnet) the
  strict check rejects any block where the devops recipient
  disagrees with the consensus-defined expected address. This
  closes the misdirected-payment vulnerability that would otherwise
  allow a producer to redirect devops funds without consequence.
- **New: producer/validator off-by-one in the devops ladder, fixed** —
  the devops-recipient lookup function is refactored to a pure
  height-based form (`getDevelopersAdressForHeight(int, int64_t)`)
  with the existing `CBlockIndex*`-based form as a backward-compatible
  wrapper. All producer-side callers (in `miner.cpp`, `cwallet.cpp`,
  and `rpcmining.cpp`) migrate to ask the ladder about the height of
  the block being mined, not the chain tip. Pre-fix, the producer
  asked about `pindexBest` (= N-1) when constructing block N, while
  the validator asked about the block at N — so at rotation
  boundaries they disagreed by one block. Latent in mainnet history
  for v1.0.1.5 (block 65020) and v1.0.4.2 (block 403117) because
  the strict check was commented out; after CW9, both sides ask
  about the same block by construction.
- **Fixed: wallet banning peers on startup** — block validation now
  correctly handles mixed-version networks during the transition
  period; peers running older versions are no longer incorrectly
  banned.
- **Fixed: startup verification rolling back the chain** — after the
  enforcement-gate removal made the masternode-payee check enforce,
  a node restarting could roll its chain back hundreds of blocks
  because the verify pass re-ran the check against an empty
  runtime masternode list. The original v2.0.0.6 guard
  (`hashPrevBlock == hashBestChain`, meaning "this block extends
  the current tip") is restored, so the check runs only for genuinely
  live tip blocks, not historical blocks being re-verified at startup.

---

## 🖥️ Wallet GUI

### Menu organisation and theme

- **New Tools menu** — maintenance and system actions consolidated
  under a dedicated Tools menu: Show Backups; Check Wallet, Repair
  Wallet, Compact Wallet; Locked Outputs; Debug Window, Open Data
  Directory, Edit Config File, Edit Config Ext File. The last four
  moved from Help (which now holds only About / About Qt — clean
  reference-only). Settings holds security state and preferences only
  (Encrypt Wallet, Change Passphrase, Unlock variants, Lock Wallet,
  Recovery Phrase, Options).
- **Dark theme** — toggle via `Settings → Dark Theme`; applies to all
  windows and dialogs.
- **In-app DigitalNote.conf editor** — a new menu entry allows editing
  the wallet's configuration file without leaving the application.
  Changes are written safely and the wallet warns about pending
  restart requirements.

### Splash and startup

- **New splash screen** — circle logo with transparent background and
  centred loading text.
- **Maintenance Mode splash** — distinct chromed splash for `-rescan`,
  `-reindex`, `-rebuildwallet`, and other long-running operations, so
  it's clear the wallet is doing maintenance rather than starting
  normally. Stays on top so it isn't buried under other windows
  during long runs; minimise to tray and restore via the tray icon
  both work.
- **Fixed: rescan splash appeared frozen** — the splash now shows
  progress as `Rescanning... block N / M` updating every 5000 blocks,
  including the post-rebuild rescan triggered by Compact Wallet.
- **New app icons** — updated circle logo icons across all sizes and
  the Windows `.ico` file. Includes a 24×24 entry to satisfy Windows
  11's tray expectation (was previously a 32×32-only logged warning).

### Password and unlock

- **Password generator** — `⚙ Generate strong password` button in the
  encrypt wallet dialog creates a random 20-character password with
  one-click copy.
- **"Forgot password?" link** — the unlock dialog now has a link to
  unlock via recovery phrase.
- **Staking checkbox hidden** — the "for staking only" checkbox is
  hidden by default in standard unlock mode.

### Status bar and tooltips

- **MAINNET indicator** — network type shown in the status bar with
  tooltip.
- **Build date and version in About box** — the build script now
  embeds the build date into the version string, so `getinfo`, the
  GUI title bar, and the About box accurately identify which build
  is running. Replaces the long-broken behaviour of self-reporting
  as `v1.0.3.5` (an ancient git tag) for the entire v2.0.0.x line.

### Transaction display

- **Fixed: locally-mined PoW coinbases now appear in the transaction
  list immediately** — previously, a freshly-mined PoW block's
  coinbase would only appear in the GUI's Recent Transactions and
  Transactions tab when the *next* block arrived (a one-block UI
  lag). The cause was a static handler that intentionally deferred
  the notification by one block via `hashPrevBestCoinBase`. The
  block's `IsInMainChain()` check at AddToWallet-time returned false
  (chain links were not yet finalised), so the GUI filtered the row
  out. Solo PoW miners now see their wins instantly. PoS stakers
  were never affected (coinstake is not subject to the same filter).
- **Fixed: double-clicking a transaction in the Transactions tab now
  opens the transaction details dialog** — previously a no-op;
  right-click → "Show transaction details" was the only working
  path.
- **Fixed: transaction-detail "Transaction ID" suffix was always
  `-000`** — the suffix was being derived from a display-row sort
  ordinal rather than an input or output index. The detail line now
  shows the clean transaction hash.
- **Bounded notification batching during import** — during initial
  block download and rescan operations, individual transaction
  notifications could pile up. They are now batched into a single
  summary notification per batch (showing the count and kind).

### Masternode page

- **Masternodes page default tab corrected** — the page now opens on
  the "DigitalNote Network" tab by default (was opening on "My
  Masternodes").

### Debug console

- **Debug console input field is disabled with an elapsed-time
  indicator while a command is in flight**, so you can tell when the
  wallet is busy versus stuck.
- **Windows paths typed without quotes** (e.g. `dumpwallet
  C:\temp\test.dmp`) now work correctly; previously the parser
  silently mangled backslashes.
- **Fixed: `-debug=all` and `-debug=1` silently did nothing (CW14).**
  Both forms looked like intuitive ways to enable wildcard logging
  but were treated as unknown category names (`fDebug` was set true
  but no `LogPrint("category", ...)` calls matched). The bare
  `-debug` form was the only wildcard. Now all three (`-debug`,
  `-debug=all`, `-debug=1`) enable wildcard logging identically.
  Help text also updated to document the disable forms (`-debug=0`,
  `-nodebug`). Comprehensive operator reference in
  `debug-logging-guide.md` covers all 21 categories with
  diagnostic-scenario recipes and log-volume guidance.

### Removing watch-only addresses

- **Fixed: removing a watch-only address left "(n/a)" ghost rows** —
  orphaned transactions are now pruned, with a progress bar for the
  (potentially slow) operation.

### Misc visual polish

- **Fixed: balance labels could clip the trailing "N" in "XDN"** —
  minimum width set on all balance labels (Available, Stake, Pending,
  Total, plus watch-only column).
- **Fixed: copyright year in About dialog** — updated to 2018-2026.
- **Fixed: Coin Control lock icon nearly invisible against light
  theme** — was white-on-transparent (designed for the dark-coloured
  status bar); now black-on-transparent for the default theme. Status
  bar lock icon unchanged.
- **Various Qt signal/slot warnings cleared** from the debug log on
  startup.

---

## 🔧 Build & Release

- **GitHub Actions CI/CD** — automated builds for Windows x64, Linux
  x64, Linux ARM64, macOS Intel, and macOS Apple Silicon.
- **Automated releases** — pushing a version tag (`v2.0.0.8`) builds
  all platforms and publishes a GitHub Release with binaries and
  SHA256 checksums automatically.
- **Build version stamping** — version string is now controlled
  entirely by source files (`clientversion.h`), not derived from
  potentially-stale git tags. Build date falls back to the compiler's
  `__DATE__` / `__TIME__` so the date is always accurate to the build.
- **BIP39 library merged inline** — no longer an external submodule;
  compiled directly into the wallet binary. The optional `USE_BIP39=0`
  build flag has been retired since BIP39 is now load-bearing for
  the wallet UX (recovery phrase, "forgot password" recovery).
- **New asset naming convention** for release downloads:
  `<name>-<version>-<os>-<arch>.<ext>`. For v2.0.0.8 the assets are:
  - `DigitalNote-qt-2.0.0.8-win-x64.exe`
  - `DigitalNote-qt-2.0.0.8-linux-x64`
  - `DigitalNote-qt-2.0.0.8-linux-arm64`
  - `DigitalNote-qt-2.0.0.8-macos-x64.dmg` (Intel)
  - `DigitalNote-qt-2.0.0.8-macos-arm64.dmg` (Apple Silicon)
  - `DigitalNoted-2.0.0.8-win-x64.exe`
  - `DigitalNoted-2.0.0.8-linux-x64`
  - `DigitalNoted-2.0.0.8-linux-arm64`
- **`linux-x64-compat` build variant** — for older Linux
  distributions. Standard `linux-x64` builds against Ubuntu 24.04+
  (glibc 2.39+, libstdc++ 14+); the compat variant builds against an
  older glibc baseline (2.31+, covering Ubuntu 20.04, Debian 11,
  RHEL 9) and statically links libstdc++.
- **32-bit builds dropped** — previous releases shipped `linux.i386`
  and `win32` variants; these are no longer produced. If you require
  a 32-bit build, you'll need to build from source.
- **Apple Silicon native build added** — previous macOS builds were
  Intel-only; v2.0.0.8 ships a native arm64 dmg alongside the Intel
  one.

---

## ⚠️ Deprecations & Known Issues

### Deprecated

- **`-salvagewallet` is deprecated.** It silently drops records on
  collision and has been removed from Bitcoin Core, Bitcoin ABC, and
  Dash for the same reason. The flag now refuses to run unless
  `-iknowsalvagewalletisdangerous` is also passed. For routine
  maintenance (compacting a bloated wallet, recovering from minor BDB
  issues), use **Compact Wallet** in the Tools menu — it is safe,
  preserves all wallet data, and is the recommended path going
  forward. For genuine corruption recovery, restore from your
  wallet.dat backup; the salvagewallet escape hatch is preserved
  only for the rare case where Compact Wallet itself fails on a
  wallet too damaged for cursor iteration to traverse.

### Known minor display behaviour

- **The masternode list "active" column refreshes on the standard
  GUI timer cadence** (~5 seconds). Status changes from incoming
  dsee messages may take up to one timer interval to appear. No
  functional impact.
- **Coinstake transaction rows in the transaction list are displayed
  as a single line** showing the net reward, rather than separate
  rows for the staked input and the new output. This is by design
  (matches existing PoS wallet conventions); a "show inputs" option
  may be added in a future release if users request it.
- **Wallets with very many transactions (tens of thousands or more)
  take time to display the transaction list after the splash closes.**
  The wallet is functional during this period — RPC calls work,
  staking continues — but the GUI window may appear delayed. Compact
  Wallet (which dramatically reduces wallet file size on bloated
  wallets) typically improves this. A proper fix (incremental load)
  is planned for the next release.
- **Toast notification spam during `importwallet`** on the GUI — the
  existing batching mechanism doesn't fully suppress notifications
  during the import path. Workaround: use Compact Wallet for routine
  maintenance instead of `dumpwallet` + `importwallet`. To be
  addressed in the next release.

---

## ⚠️ Upgrade Notes

### Backward compatibility

- **Chain-compatible** with v2.0.0.6 until the voted consensus
  activation height is reached. You can run mixed-version networks
  safely up to that point.
- **Network protocol** is bumped. v2.0.0.8 nodes accept connections
  from v2.0.0.6 clients and function normally. After voted-consensus
  activation, only v2.0.0.8 (or later) masternodes contribute to the
  consensus vote — older clients can still connect and sync but
  their votes are not counted.
- **No mandatory upgrade** is forced by this release. The voted
  consensus activation is a separate spork-controlled event.
- **Recovery phrase** is only available for wallets encrypted with
  v2.0.0.8 or later. Users with older wallets should go to
  `Settings → Recovery Phrase` immediately after upgrading to
  complete the one-time upgrade process.

### Recommended upgrade order

For masternode operators with multiple roles in the network:

1. **Masternodes first** — they generate the queue broadcasts that
   the network will rely on post-activation.
2. **Mining pools second**.
3. **Stakers third**.
4. **Full nodes last**.

### For regular wallet users

Update at your convenience. There's no impact on send/receive,
balance display, or wallet file compatibility. The voted consensus
feature affects only masternode payment selection, not user
transactions.

### For pool / exchange operators

Update at your convenience. Block validation and reorg handling
follow the soft-activation pattern described above — there's no
risk of a sudden chain split at activation, even if some pool nodes
upgrade later than others. The recommended approach is the same as
for any other upgrade: test in your staging environment first, then
roll out to production at a quiet window.

---

## 🔍 MISC

Smaller items, polish work, and items worth noting that don't fit
the categories above.

### Polish and operational logging

- **Per-block log noise reduced** — steady-state masternode/devops
  payee verification chatter, "matches voted consensus" lines, and
  the init-progress spray during chain load are now gated behind
  debug categories. A normal (debug-off) node now logs around two
  lines per block plus loud errors; `-debug=1` restores everything
  for soak nodes that need full visibility. `--help` documents the
  available `-debug` categories: `masternode`, `mnengine`,
  `instantx`, `smsg`, `webwallet`, `retarget`, `init`.
- **Fixed: ~1378 "coinstake as individual tx" errors per launch**
  spamming the debug log — wallet startup now correctly excludes
  coinstakes from mempool reaccept (these errors had no functional
  impact but made the log very noisy).

### Internal modernisation

- **Legacy per-height vote message path removed.** The
  `mnvote`/`getmnvotes` P2P messages that preceded the M1Q
  queue-based path were dormant in the source tree but unused.
  Around 500 lines of dead code and two source files were removed,
  reducing binary size and eliminating a class of potential
  confusion for future contributors.
- **Lock-ordering throughout the masternode-payment validation
  path** has been audited and standardised so the same
  lock-acquisition pattern (`cs_main` before `voteTracker.cs`)
  holds in every code path that uses both locks, both directly and
  via callees.

### Verified — not bugs

- **Permissive `payee == empty` pass in `CheckBlock`** — when the
  voted-payee getter returns false or an empty payee (e.g. during
  a pre-activation window or post-activation consensus gap),
  `CheckBlock` accepts the block's payee as-is. This is the intended
  permissive soft-fork fallback — it prevents a consensus gap from
  stalling the chain.

### Acknowledgements

Voted consensus design and validation: thanks to all testnet
masternode operators who ran extended soak sessions and reported
edge cases throughout the v2.0.0.8 cycle. The multi-day
parallel-staker observation runs were essential for catching the
late-cycle lock-order issues, the equivocation edge cases, and the
balance-tracking interaction that this release closes out.

---

## 🔗 Resources

- **Website:** https://digitalnote.org/
- **Repository:** https://github.com/DigitalNoteXDN/DigitalNote-2
- **Block explorer:** https://xdn-explorer.com/
- **Testnet explorer:** https://testnet.xdn-explorer.com

