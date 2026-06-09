# DigitalNote v2.0.0.8 — Technical Changelog

Developer-facing changelog covering every meaningful change between
v2.0.0.6 and v2.0.0.8.

This is a **consolidated release** that supersedes the unshipped
v2.0.0.7 work. The active mainnet remains on v2.0.0.6; v2.0.0.7 never
saw a public release, and the work that was scoped to v2.0.0.7 is
folded into v2.0.0.8 along with the masternode voted-consensus feature
and an accumulated body of reliability and correctness fixes.

For the user-facing summary, see `release-notes/v2.0.0.8.md`.

---

# A. MASTERNODE

## A.1 Consensus — voted-payee mechanism

### A.1.1 Headline: Masternode Voted Consensus

v2.0.0.8's central feature is **masternode voted-payee consensus**. In
v2.0.0.6, the masternode paid in each block was selected locally and
could differ between nodes, with `vWinning` (`mnw` P2P messages) as a
loose coordination layer. v2.0.0.8 replaces this with an explicit voting
protocol: masternodes broadcast signed queues of upcoming payees; nodes
tally per-position votes; a supermajority of chain-derived eligible
voters makes a payee canonical for that height;
`GetEnforcedPayee` gates block validation against it.

Activation is height-gated
(`GetEffectiveVotedConsensusActivationHeight()`), with a per-network
floor and an optional `SPORK_15` override that can only *lower* the
activation height, never raise it above the floor. Below the activation
height the legacy `GetBlockPayee` path remains authoritative, so the
change is inert on existing chain history and during the pre-activation
period.

### A.1.2 M1Q queue-based voting (final mechanism)

The single largest change in v2.0.0.8. The voted-consensus mechanism is
built around **per-masternode ordered queues**, not per-height
single-payee votes.

**Mechanism.** Each masternode broadcasts a signed
`CMasternodeVoteQueue`: an ordered list of the next `VOTE_QUEUE_LENGTH`
(= `VOTE_LOOKAHEAD` = 10) payees, computed by deterministic
forward-simulation of the rotation. The simulation replicates the
existing chain-derived ranking exactly (eligibility by collateral
confirmation depth; rank by last-paid-or-confirm height, smallest
first, tie-break smallest vin) and, crucially, **advances each chosen
payee's simulated last-paid height before picking the next position.**
That forward-mutation is what the per-height path could not do — it
removes the lag that produced the §A.1.5 streak. Because every honest
masternode runs the identical deterministic simulation, all queues
agree position-for-position, so per-position consensus is trivially
unanimous and the streak is structurally impossible.

**Consensus read.** `GetEnforcedPayee` →
`GetCanonicalWinnerFromQueues`: gated by a commit point
(`currentTip ≥ targetHeight − VOTE_COMMIT_BUFFER`, buffer = 3), a
voting-eligible floor (`MIN_ENABLED_FOR_CONSENSUS` = 5), and a
per-position supermajority tally over the most recent queues, with
position 0 of the queue broadcast at height *h* supplying the payee for
*h + 1*.

**Wire / propagation.** New `mnvotequeue` / `getmnqueues` P2P messages
and inv type; queues stored as `mapQueues[nQueueHeight][voterVin]`;
pruned behind `VOTE_PAST_HORIZON`; erased on disconnect; equivocation
tracked across the format change. Pre-M1Q peers silently drop the
unknown command. A new `CMasternodeMan::GetQueuePaymentSnapshot()`
takes a single-lock snapshot of all MN payment state for the simulation
(honours the established lock order; does not call `mn.Check()` —
deterministic chain-derived input only).

**Compatibility.** NOT a rolling upgrade once voted consensus is
active: `GetEnforcedPayee` reads ONLY queue consensus once active, so
a mixed M1Q/legacy fleet under enforcement would disagree. All nodes
must upgrade together before activation.

Touched files: `cmasternodevotetracker.{cpp,h}`,
`cmasternodevotequeue.{cpp,h}`, `cmasternodeman.{cpp,h}`,
`cactivemasternode.{cpp,h}`, `cblock.cpp`, `main.cpp`, `net.h`,
`rpcmnengine.cpp`, plus serialization plumbing in `cdatastream.cpp` and
`serialize/{base,read,write}.cpp`.

### A.1.3 Determinism of the canonical winner

Two determinism defects in the original per-height
`GetCanonicalWinner` were fixed during the consensus implementation
(retained in the queue path because the queue producer reuses the same
ranking):

- **Denominator.** The supermajority threshold was computed against a
  live `CountEnabled()` call — a value that varies between nodes and
  over time. Replaced with a deterministic, chain-derived
  eligible-voter count: `CMasternode::IsVotingEligible(N)` plus
  `CMasternodeMan::CountVotingEligible(N)`, both functions purely of
  the voted height `N`.

- **Unique winner.** The original loop returned the first payee in
  `std::map<CScript>` order that cleared the threshold — which (a) is
  not necessarily the most-voted payee, and (b) never checked whether
  *two* payees both cleared. The corrected logic requires a single
  payee with a strict supermajority; if two or more clear, the result
  is AMBIGUOUS and no winner is named.

New constant `VOTER_ELIGIBILITY_DEPTH = 17` (`masternode.h`).

### A.1.4 Vote-payee determinism (PB-INFLIGHT removed)

An earlier patch (codenamed PB-INFLIGHT) had added an "in-flight vote"
fold to `FindOldestNotInVecChainDerived` that bumped an MN's effective
last-paid height based on node-local in-flight vote tally state. This
made the payee a node *votes for* depend on node-local state and
produced a stable divergence on testnet (a persistent 5/2 vote split
along the network boundary, with both clusters internally consistent).
A consensus input must be a pure function of the chain, never of
node-local state. PB-INFLIGHT was removed; `GetConsensusCommittedHeights`
(its only-caller helper) was removed.

With PB-INFLIGHT removed, `FindOldestNotInVecChainDerived` is once
again a pure function of `(masternode list, last-paid cache,
referenceHeight, activation clamp)`. Confirmed on testnet after
removal: `getvoteinfo` from a local node and a remote VPS at the same
height return the identical canonical payee and the identical voter
set.

### A.1.5 Vote-staleness payee streak — fixed structurally by M1Q

The post-activation soak surfaced a recurring short payee streak
(observed as 3-streaks on the 7-MN testnet fleet), the streaking
masternode advancing one rotation position each cycle.

**Mechanism**: the per-height payee selector ranked on
`mapLastPaidHeight`, which is written only when a block connects, while
the vote/selection for a height runs `VOTE_LOOKAHEAD` blocks earlier.
The selector's last-paid view was therefore ~`VOTE_LOOKAHEAD` behind
the height it selected for, so a masternode was re-selected for every
height in that lag window until its own payment connected.

**Severity:** consensus-SAFE — the selector was a pure chain function,
all nodes agreed (7/7 every block through the crossing), no fork. It
was a FAIRNESS defect.

**Resolution**: the M1Q queue redesign (A.1.2) is the structural fix.
The streak's root cause — a per-height vote computed from a last-paid
view that lagged the selected height by `VOTE_LOOKAHEAD` — is
eliminated by broadcasting an ordered queue of upcoming payees computed
by deterministic forward-simulation, in which each chosen payee's
simulated last-paid is advanced before the next position is picked.

### A.1.6 PB-16 clamp tie-collapse — removed

A pre-activation last-paid clamp in `FindOldestNotInVecChainDerived`
was normalising every pre-activation last-paid value to
`activationHeight - 1`. Multiple masternodes thereby collapsed to one
identical rank key, TIED, and the smallest-vin tiebreak froze
selection on one MN for ~`VOTE_LOOKAHEAD` blocks — producing a
period-`VOTE_LOOKAHEAD` streak whenever a PoS stall straddled the
activation height.

The clamp was removed entirely. `mapLastPaidHeight` stores block
HEIGHTS, which do not go stale with wall-clock time — only with block
progression — so last-paid ORDER is correct in every epoch with no
normalisation. The never-paid fallback was changed from `paidHeight 0`
to the masternode's collateral confirmation height, so never-paid MNs
do not tie at 0.

Confirmed at the testnet activation crossing: the clamp-free selector
rotated cleanly across the activation boundary.

### A.1.7 `fMnAdvRelay` enforcement gate — removed

`CheckBlock`'s voted-payee enforcement was gated on **two** conditions:

```
if (nMasterNodeChecksEngageTime != 0 && fMnAdvRelay)
```

`fMnAdvRelay` is set from a config option `-mnadvrelay` defaulting to
**false**; no node sets it. The flag originated as the toggle for an
abandoned "masternode advanced relay system" (`version.h`: *"Unfinished,
not used"*) and was repurposed as the enforcement gate without being
renamed or documented. The consequence: voted-consensus enforcement —
the headline feature — could never engage, on any node, regardless of
activation height. It was a permanent dry-run.

The four `cblock.cpp` enforcement-gate conditions drop
`&& fMnAdvRelay`, becoming `if (nMasterNodeChecksEngageTime != 0)`. The
strict checks additionally gained a devops-fallback allowance and a
loud unconditional NOTICE when a devops-fallback payee is seen
at/after activation. The `fMnAdvRelay` extern, definition,
`-mnadvrelay` config read, help text, and the unused
`MIN_MASTERNODE_ADV_RELAY` sentinel constant were all removed.

**Release lesson recorded:** consensus enforcement must never ship
gated behind an undocumented flag, still less one named for an
unrelated abandoned feature.

Touched files: `cblock.cpp`, `util.h`, `util.cpp`, `init.cpp`,
`version.h`.

### A.1.8 Engagement and vote-payee agreement

Four fixes that take the consensus machinery from "present but inert"
to "engaging correctly":

- **Deterministic vote payee.** `BroadcastVote`/queue producer derives
  the payee from a `referenceHeight` that is a pure function of the
  voted height (`forHeight - VOTE_LOOKAHEAD - REORG_DEPTH_BUFFER`), so
  every masternode votes for the same payee for a given height —
  regardless of where each individual node's chain tip happens to sit.

- **Chain-derived candidate pool.** `FindOldestNotInVecChainDerived`
  gained a `bool fChainDerivedEligibility` parameter (default false).
  The vote path passes `true`, selecting candidates by
  `IsVotingEligible` rather than the live `IsEnabled`, consistent with
  A.1.3's denominator.

- **Same-IP connectivity.** `OpenNetworkConnection` rejected a second
  connection to an IP already connected, via an IP-only
  `FindNode((CNetAddr)addrConnect)` term. On a testnet (and any setup)
  with multiple masternodes behind one IP on distinct ports, this
  prevented all but one from connecting. The IP-only term was removed;
  port-distinct peers on a shared IP now connect.

- **Staker readiness gate.** `ThreadStakeMiner` now checks, before
  `CreateNewBlock`, whether voted consensus is active for the next
  height and — if so — whether this node can actually produce the
  voted payee. If it cannot, the staker DEFERS (logs and sleeps)
  rather than mint a block on the legacy payee that the vote-aware
  fleet would reject. After the M1Q transition the gate was retargeted
  at `GetCanonicalWinnerFromQueues(tip+1)` — the same source the
  validator uses — fixing an indefinite defer that had stopped the
  PoS staker from resuming after the M1Q switch-over.

### A.1.9 Vote propagation — relay-before-validate (with security split)

`ProcessMessageMasternodeVote` (the `mnvote` handler) originally
dropped a vote *and failed to relay it* whenever the receiving node
did not yet have the voting masternode in its `mnodeman` list. A node
with an incomplete masternode list — most importantly a non-masternode
node such as the PoS staker — was therefore a vote *black hole*: it
neither recorded nor forwarded votes.

Fixed by **relay-before-validate**: the handler is reordered so the
cheap checks (`AlreadyHaveVote`, vote-height window) and the `inv`
relay happen *before* the masternode-list-dependent voter lookup and
signature check. The signature is still fully verified before the
vote is *recorded*; relaying an unverified `inv` only costs peers a
`getdata` round-trip, and every node validates independently before
tallying.

A subsequent audit found that the bare relay-before-validate would
have opened a **vote-flood amplification DoS** for known-voter junk:
a junk-signature vote with a fresh hash and a real voter vin would
have propagated network-wide. The fix is a **relay split**:
relay-before-validate is kept *only* for the genuinely-uncheckable
`voter == NULL` case (preserving the black-hole fix); when the voter
*is* known, relay happens *after* `CheckSignature` passes. With
amplification handled, the bad-signature score is lowered `100 → 5`:
still rate-limiting against a genuine bad-signature spammer, but no
single stale-key or config-fault event bans an honest peer.

This handling is preserved through the M1Q queue path (which also
applies the relay split principle to queue broadcasts).

### A.1.10 Validation hook — `GetEnforcedPayee`

`GetEnforcedPayee(nBlockHeight, payeeOut, vinOut)` (`cblock.cpp`) is
the validation hook. Three regimes:

1. Before the activation height — defer to legacy
   `masternodePayments.GetBlockPayee` (the v2.0.0.6 `vWinning` map).
2. At/after activation, consensus formed — return the canonical voted
   payee from the vote tracker.
3. At/after activation, no consensus yet — **permissive fallback**:
   behave as pre-activation. This is a deliberate soft-fork choice so
   a consensus gap does not stall the chain.

On the voted path the tracker keys payees by `scriptPubKey`, not by
vin, so `vinOut` is explicitly cleared (`CTxIn()`) — a downstream
`mnodeman.Find(vinOut)` then returns NULL deterministically rather
than matching a stale leftover vin.

### A.1.11 `CheckBlock` masternode payee verification — rework

`CheckBlock` contained **two** masternode/devops payment-verification
blocks. Comparison against the v2.0.0.6 source confirmed both are
ancestral upstream code, byte-for-byte unchanged across v2.0.0.6/7/8 —
nothing here is a recent regression.

**Block A** (the `foundPayee` / `foundPaymentAndPayee` block) was dead
code. Its core test required a single output to carry *both* the
masternode payee script *and* the masternode payment amount — but it
seeded that amount from the last coinstake vout, which is the *devops*
output. Masternode pays 150 XDN, devops 50 XDN, so the test is
unsatisfiable whenever a real enforced payee exists. Across three
releases on mainnet it has never enforced anything.

**Block B** (the `nProofOfIndexMasternode` / `fBlockHasPayments`
block) is the real verification — type-aware (PoW and PoS),
structure-checked, amounts checked at known indices, with the
masternode-checks-delay grace logic.

The rework, three parts:

- **Block A removed.** ~185 lines deleted, replaced by a marker
  comment. One cross-dependency fixed: Block B referenced
  `fIsInitialDownload`, declared only inside Block A; the
  declaration is re-added to Block B's scope.

- **PoS voted-payee enforcement added.** The PoW path already had
  voted-consensus enforcement; the PoS path had no equivalent. PoS
  now mirrors PoW exactly: same gate, same `GetEnforcedPayee` call,
  same fall-through to the pre-existing weak check when no
  enforceable voted payee exists.

- **`DoS(10) → DoS(100)`.** Block B's final `!fBlockHasPayments`
  rejection is raised consistent with every other hard `CheckBlock`
  failure. The startup checks-delay grace window is unaffected.

Touched file: `cblock.cpp`.

### A.1.12 Equivocation handling

Two fixes folded into the v2.0.0.8 release after late-cycle observation
on testnet.

**OnFreshDsee wiring (Issue 1).** The documented Path A auto-clear
mechanism for the equivocator blacklist — "cleared by OnFreshDsee on
the next legitimate dsee or dseep" — was wired only into the
new-MN-add codepath at `cmasternodeman.cpp:1036`. The dsee known-MN
update path and the dseep heartbeat handler never invoked it. Result:
Path A was dead code in steady-state operation. Once a voter was
marked equivocator, the documented automatic recovery never fired.

Two call sites added: `cmasternodeman.cpp:880` (dsee known-MN path,
after `pmn->UpdateLastSeen()` inside the `acceptable` branch) and
`cmasternodeman.cpp:1130` (dseep handler, after `pmn->UpdateLastSeen()`
in the heartbeat path). Both insert
`voteTracker.OnFreshDsee(vin.prevout);`.

**Equivocation false-positive on legitimate re-broadcast (Issue 3).**
`CMasternodeVoteTracker::ProcessQueue` was flagging any second queue
from the same `(voter, nQueueHeight)` with a different hash as
equivocation. But M1Q spec S10.1 explicitly permits legitimate
re-broadcast when a block-connect changes the chain ancestry feeding
into the deterministic queue computation — the re-signed queue has a
later `nTimeSigned` and produces a different hash by definition.

Observed on testnet 2026-06-02 06:07:48: a 4-second 4305→4306 advance
caused all 7 MNs to broadcast spec-S10.1-compliant queue v2 within the
same second, and an observer node that didn't see the same MN-local
disconnect marked the entire fleet as malicious equivocators. Consensus
formation stalled for 125+ blocks until the operator ran
`clearequivocator` x7.

The hash-discrimination branch at `cmasternodevotetracker.cpp:684` was
replaced with **newer-wins replacement** based on `nTimeSigned`:
a later legitimate broadcast replaces an earlier one rather than
marking the broadcaster.

(A third related concern — that equivocation marking is per-node-local,
so two observers in the same fleet land in different equivocator sets
depending on gossip arrival order — is **scope only** for this
release; a chain-wide enforcement design would have substantial
adverse-effect risks if shipped without extensive analysis. Deferred
to v2.0.0.9.)

Touched files: `cmasternodeman.cpp`, `cmasternodevotetracker.cpp`.

### A.1.13 Removal of the dormant per-height vote path

With M1Q (queue-based) voting as the sole consensus mechanism, the
legacy per-height `mnvote` / `getmnvotes` path was dormant: still
present in the codebase but unused. Mainnet on v2.0.0.6 has no M1Q
wire awareness, so there was no defensive-deserialise window to
protect. Removed in full.

The removal was executed in 11 sub-steps. Notable elements:

- `mnvote` and `getmnvotes` P2P message handlers removed (~212 lines)
- `ProcessVote` method + declaration removed
- `BroadcastVote` method + declaration removed from `cactivemasternode`
- Legacy `GetCanonicalWinner` (per-height variant) removed
- `GetVoteInfo` method + `VoteInfo` wrapper struct removed; retained
  `VoteInfoEntry` (still used by `GetQueueInfo`)
- All vote-only tracker state removed: `mapVotes`, `mapVotesByHash`,
  `setSeenVoter`, `VoteRecord`, `MakeVoterKey`, vote-path
  `OnBlockConnected/Disconnected`, `AlreadyHaveVote`, `GetVoteByHash`,
  `Sync` vote-branch, `RemoveVoterVote`; plus the corresponding
  `main.cpp` dispatch sites
- `CMasternodeVote` wire class deleted entirely: `cmasternodevote.{cpp,h}`
  deleted, 4 `.pri` file entries removed, 6 serialization-instantiation
  sites removed (`cdatastream.cpp`, `serialize/base.cpp`,
  `serialize/read.cpp`, `serialize/write.cpp`), 7 includes removed
- `MSG_MASTERNODE_VOTE` in `net.h`'s inv-type enum replaced with
  `MSG_MASTERNODE_VOTE_RESERVED` to preserve the numeric value of
  `MSG_MASTERNODE_VOTE_QUEUE` (auto-numbered enums shift if removed
  outright)

Approximate scale: ~500 lines removed, ~40 lines added, 2 source files
deleted, 4 build-system files updated.

Brace-balance preserved on every touched file. Verification sweep
confirms zero remaining bare `mnvote` / `getmnvotes` references
outside of historical comments documenting the removal.

## A.2 Masternode reliability and lifecycle

### A.2.1 `nLastPaid` correctness (multiple fixes)

**Inherited from v2.0.0.7 work, plus a v2.0.0.8 cleanup of the redundant
writer.**

Three bugs in the same field, all fixed:

1. **Copy constructor** had two consecutive assignments: `nLastPaid =
   other.nLastPaid` followed by `nLastPaid = GetAdjustedTime()`. The
   second line overwrote the real last-paid time with the current time
   on every copy. Second line removed.

2. **The `newAddr`/`newVin`/`newPubkey` constructor** (used when a
   peer's `dsee` arrives and a fresh `CMasternode` is constructed)
   never initialised `nLastPaid` at all. The field held uninitialised
   stack memory — typically a small repeating value like `17` — until
   the MN was actually paid. Added `nLastPaid = GetAdjustedTime()` so
   newly-Registered MNs show "now" until first payment, matching the
   default constructor's semantics.

3. **Redundant `main.cpp` `ProcessBlock` writer removed.** A second
   `nLastPaid` writer at `main.cpp` `ProcessBlock` (via
   `GetEnforcedPayee → Find(vin) → nLastPaid = GetAdjustedTime()`)
   wrote wall-clock "now" (not the paying block's time), used an unset
   `vin` on the voted path (so it matched the wrong masternode or
   none), and ran *after* `CMasternodeMan::OnBlockConnected` had
   already set the field correctly — clobbering the right value with
   a wrong one every block. `OnBlockConnected` already identifies the
   masternode the block *actually* paid (`FindByPayeeAddress` on the
   block's outputs) and sets that MN's `nLastPaid` to the connecting
   block's `GetBlockTime()`. The redundant writer is removed from both
   the LiteMode and non-LiteMode branches; `OnBlockConnected` is now
   the single authority.

### A.2.2 `getblocktemplate` masternode-winner fix

`rpcmining.cpp:841-857`. The old code called `GetCurrentMasterNode(1)`
which passed no block height, defaulting to height 0 (genesis block
hash) inside `CalculateScore()`. The same masternode won every block
indefinitely. Fix: replaced with `masternodePayments.GetBlockPayee(
pindexPrev->nHeight + 1, mnPayee, mnVin)` — same data source as block
validation. Added `FindOldestNotInVec` fallback for when `vWinning` is
empty (transition period while network upgrades).

### A.2.3 Cross-version masternode activation

`cmasternodeman.cpp:901`. `EnableHotColdMasterNode` check was
`protocolVersion == PROTOCOL_VERSION` — a strict equality that broke
as soon as wallet and daemon were on different builds. Changed to
`protocolVersion >= MIN_PEER_PROTO_VERSION`. Any acceptable version
now activates the masternode regardless of minor version differences.

### A.2.4 `AvailableCoinsMN` — locked outputs included on MN-start path

`cwallet.{h,cpp}`, `cactivemasternode.cpp:618`. New parameter
`bool fIncludeLockedMN = false` on `AvailableCoinsMN`; the
`IsLockedCoin` filter in the per-output predicate becomes
`(fIncludeLockedMN || !IsLockedCoin(...))`. `SelectCoinsMasternode`
passes `true`.

**Production bug**: from the introduction of persistent locks (BDB
record type A4 `lockedoutputs`), the `AvailableCoinsMN` lock-filter
incorrectly treated user-locked collaterals (and the wallet's own
auto-locked collaterals from `LockCoin`) as unavailable. Symptom:
re-Register after a restart failed with `"could not allocate vin"`
whenever the collateral had been locked. Fix architecturally aligns
with invariant 1: locks are user data, not MN state.

### A.2.5 Remote masternode start now auto-locks the collateral

`cactivemasternode.cpp` inner `Register(CTxIn, CService, ...)` at the
success path before `return true`. The local-MN path (`ManageStatus()`)
has always called `pwalletMain->LockCoin(vin.prevout)` after a
successful Register; the remote-MN outer `Register(...)` did not.
Both paths now protect collateral identically. Required
`#include "thread.h"` for the `LOCK` macro.

### A.2.6 `StopMasterNode` — collateral lock no longer auto-released

`cactivemasternode.cpp:237-246`. Previously called
`pwalletMain->UnlockCoin(vin.prevout)`. Removed.

Architectural invariant: locks are user data, not masternode lifecycle
state. Auto-unlocking on stop silently undid user-set locks (including
persistent locks set via the `lockunspent` RPC or via the new B2 lock
controls described in C.3). The lock on masternode start (in
`ManageStatus`) is retained — that protects the collateral while the
masternode is running, but is no longer rolled back automatically on
stop.

### A.2.7 Local masternode start — `masternode.conf`-aware collateral selection

A locally-started masternode (`CActiveMasternode::ManageStatus`)
selected its collateral by calling the no-txhash `GetMasterNodeVin`,
which scans the wallet for every 2,000,000-XDN UTXO and takes the
first one. The remote-start path passes the collateral txid/index from
`masternode.conf`; the local path consulted `masternode.conf` not at
all.

On a host whose wallet holds the collateral for several masternodes —
the normal cold/collateral wallet that funds all of an operator's
remotes — a local masternode bound to an *arbitrary* collateral,
frequently one belonging to a declared remote masternode. Two daemons
then ran on one collateral identity, each signing votes with its own
key; the network ban-stormed on the resulting `CheckSignature`
failures.

`masternode.conf` is now used as a **subtraction filter** by a new
method `GetLocalMasternodeVin`. The wallet scan is kept — it gives
autostart and resilience — and is only disambiguated:

- **Case A** — a `masternode.conf` entry whose private key matches
  this daemon's `masternodeprivkey` is this node's own declaration;
  its collateral is used exactly. Deterministic.
- **Case B** — no matching entry: every collateral declared in
  `masternode.conf` (those belong to remote masternodes) is subtracted
  from the scan, and selection runs on the remainder.

The local masternode start refuses (with a specific, logged reason
surfaced in `masternode status`) when: the subtraction leaves no
candidate (every collateral in the wallet is a declared remote); or
`masternode.conf` exists but will not parse. `masternode.conf` is
re-validated at masternode-start time rather than relying on the
init-time read.

A single-masternode operator with no `masternode.conf` sees no
behaviour change.

Touched files: `cactivemasternode.cpp`, `cactivemasternode.h`.

## A.3 Peer-reliability and ban-storm prevention (Round 4-A)

A set of fixes to peer-facing masternode message handling, prompted by
a testnet investigation in which nodes were banning each other's whole
subnets and dropping connections.

### A.3.1 NAT-hairpin address hygiene

The testnet runs with `upnp=0` / `discover=0` and manual router
port-forwarding. An inbound port-forwarded connection arrives
NAT-hairpinned — the accepted socket's source address is rewritten to
the LAN gateway (e.g. `192.168.1.1`). The version handler admitted a
peer's reported `addrFrom` into `addrman` without a routability check,
so a non-routable, LAN-relative address could enter `addrman` and be
gossiped network-wide. The `addrman.Add` is now gated on
`addrFrom.IsRoutable()` — the codebase's own predicate. The connection
itself is left intact (messages still flow, a local mesh keeps
working); only the address is prevented from propagating as a network
identity. `main.cpp`.

### A.3.2 `State()` NULL guard

`ProcessMessage` dereferenced `State(pfrom->GetId())->nLastBlockProcess`
unconditionally; `State()` returns NULL when the node id is not in
`mapNodeState`. The result is now null-checked. `main.cpp`.

### A.3.3 `dseg` re-ask penalty

A peer re-requesting the masternode list inside the rate-limit window
was scored `Misbehaving(34)` — a third of a ban — for what is normal
behaviour after a restart or dropped connection. Three such re-asks
reached the ban threshold. The duplicate request is now rate-limited
and ignored with no misbehaviour score. `cmasternodeman.cpp`.

### A.3.4 `dseg` requester-side retry

`DsegUpdate` sent one `dseg` and recorded a 3-hour cool-off
unconditionally — so a lost request, or a peer that dropped before
replying, left the node with no masternode list for three hours.
`DsegUpdate` now picks the retry interval by list state: a short
`MASTERNODES_DSEG_RETRY_SECONDS` (3 min) while the list is empty, the
full 3 hours once it is populated. Self-correcting.
`cmasternodeman.cpp`, `masternodeman.h`.

### A.3.5 `Misbehaving`-surface audit

Eight `Misbehaving` calls in the masternode handlers were reviewed
against one test: can the *receiver's* own incomplete or stale state —
not the sender's behaviour — trigger it on an honest peer? Two were
found to be receiver-state-relative and fixed:

- `dsee` `GetInputAge < MASTERNODE_MIN_CONFIRMATIONS` — `GetInputAge`
  is relative to the receiving node's chain height, so a node still
  syncing false-positives on a valid `dsee`. The `Misbehaving(20)` is
  now gated on `!IsInitialBlockDownload()`. `cmasternodeman.cpp`.
- `mnvote` `CheckSignature` failure — see A.1.9.

## A.4 GUI updates relating to masternodes

(Detailed in C.3 Wallet GUI; cross-referenced here for completeness.)

- B1: 2,000,000 XDN incoming-collateral popup (`bitcoingui.cpp:1015-1079`)
- B2: Lock/Unlock context menu on the user's own masternodes table
  (`masternodemanager.{h,cpp,ui}`)
- B3: Masternode list selection mode (ExtendedSelection)
- B4: Lock column in My Master Nodes table
- B5: Lock column spacing
- B6: Locked Outputs dialog (Tools menu)
- Masternodes page default tab corrected (`masternodemanager.ui`)
- Masternode "last paid" column reads from authoritative voted-payment
  record

---

# B. PROOF-OF-STAKE / STAKING

## B.1 Lock-order deadlocks — ABBA chain and resolution

### B.1.1 First ABBA — original §23 wedge

The Windows proof-of-stake staking node became unresponsive several
times during the soak — process alive (GUI/RPC responsive), block
processing inert. The initial unsymboled dumps showed no thread on a
critical section, leading to a withdrawn hypothesis of lock-order
deadlock.

A symboled debug build resolved it: the cause IS an ABBA lock-order
deadlock — between `cs_main` and the vote-tracker lock `cs` —
gdb-confirmed on the symboled binary. The unsymboled dumps simply
could not show it.

**Resolved.** `GetCanonicalWinner` and `GetVoteInfo` had their lock
scope narrowed so the global acquisition order is one-directional
(`cs_main` → `voteTracker.cs`). Confirmed by a multi-hour armed soak
with no recurrence, and re-confirmed live: the PoS block at testnet
height 1838 minted cleanly through the activation crossing.

### B.1.2 Second ABBA — readiness-probe site (CW0, scope-corrected)

The PoS staker on Windows wedged after ~18 hours of soak (2026-05-28).
GUI/RPC remained responsive; block production stopped; peer count
decayed. Symptoms matched the original B.1.1 pattern, but with the
M1Q queue-based path now in place the lock-order had shifted.

A symboled debug build with gdb captured the deadlock:

- Thread 10 (msghand): in `ProcessGetData` calling `GetQueueByHash` —
  waiting on `voteTracker.cs`, currently holding `cs_main`.
- Thread 18 (`ThreadStakeMiner`): in the §29 readiness-gate probe
  holding `voteTracker.cs` and waiting on `cs_main`.

Classic ABBA across (`cs_main`, `voteTracker.cs`).

The initial fix (called CW0) added an outer `LOCK(cs_main)` around the
readiness probe BEFORE the inner `voteTracker.cs` — establishing the
canonical order. **First attempt soaked clean for 24 hours and then
wedged again at the 18-hour mark on 2026-05-30**, with a different
gdb signature: cs_main held by the staker thread itself, no contention
with msghand.

Root cause of the recurrence: the LOCK had been applied at correct
lock order but with too-wide scope — covering a `MilliSleep(5000)`
defer in the readiness-probe path. Under sustained defer (queue
tracker not yet ready), cs_main was held for 5 seconds per loop
iteration, starving cs_main consumers.

**Resolved.** Scope the LOCK strictly around the
`GetCanonicalWinnerFromQueues` call site; release the lock BEFORE the
`MilliSleep`. +21 lines of code (mostly explanatory comment), one
additional brace pair, zero semantic change beyond scope. Audit
confirmed no other site in the codebase holds cs_main across a sleep
— the readiness-probe site was unique.

Touched file: `miner.cpp`.

### B.1.3 Third ABBA — SignBlock chain (CW5)

The CW0-fixed binary wedged again on 2026-05-31 at 18:04 — a third
datapoint on the ~18-hour pattern across 2026-05-28, 2026-05-30,
2026-05-31.

gdb capture proved a DIFFERENT ABBA at a DIFFERENT site:

- Thread 10 (msghand): held `cs_main`, waiting on `voteTracker.cs`
  (via `ProcessGetData → GetQueueByHash` at
  `cmasternodevotetracker.cpp:542`).
- Thread 18 (`ThreadStakeMiner`): held `voteTracker.cs`, waiting on
  `cs_main` (via `SignBlock → CreateCoinStake → GetEnforcedPayee →
  GetCanonicalWinnerFromQueues → CountVotingEligible → ... →
  GetTransaction`).

This is the SignBlock chain, which predates the CW0-site readiness
probe and is now identified as the **original cause** of the
2026-05-28 wedge. The 18-hour-mark pattern across all three wedges
resolves to the mean-time-to-collision for this ABBA on this fleet
topology.

A brace-aware audit of every site that takes `voteTracker.cs`
identified **6 Class B** (canonical-order-violating) call sites, all
reaching the inner lock through one of two functions:
`GetCanonicalWinnerFromQueues` or `GetQueueInfo`.

**Resolved.** Two `LOCK(cs)` → `LOCK2(cs_main, cs)` upgrades:

- `cmasternodevotetracker.cpp:725`
  (`GetCanonicalWinnerFromQueues`): covers `CreateCoinStake` (PROVEN
  bug), `getblocktemplate`, masternode-winners RPC, and `CheckBlock`
  x2 via submitblock RPC.
- `cmasternodevotetracker.cpp:951` (`GetQueueInfo`): covers
  `getvoteinfo` RPC.

Functions are now self-protecting against any caller that doesn't
already hold cs_main. `boost::recursive_mutex` makes the redundant
re-acquire from already-correct callers (CW0, ProcessMessages,
CreateNewBlock) a no-op.

**Drift restorations.** Two silent source-tree drifts surfaced during
the CW5 rebuild:

- `cmasternodevotetracker.h` was missing the `QueueInfo` struct
  declaration (21 lines deleted with no commit trail). Restored.
- `cmasternodevotetracker.cpp`'s `GetVoteInfo` definition was missing
  the `CMasternodeVoteTracker::VoteInfo` return-type prefix.
  Restored at CW5 time; subsequently removed entirely by the per-height
  vote-path removal (A.1.13).

Touched files: `cmasternodevotetracker.cpp`, `cmasternodevotetracker.h`.

## B.2 PoS coinstake spent-tracking (CW4 Fix B)

`miner.cpp:880-891`. The kernel input consumed by a successful PoS
coinstake was previously marked spent only via the catch-all per-block
`FixSpentCoins` reconciliation that ran on a subsequent block
connection. Between coinstake creation and that reconciliation, the
wallet's per-tx `vfSpent` array still showed the kernel input as
unspent. Race scenarios where the staker thread re-examined available
coins in that window could attempt to re-stake the just-consumed UTXO.

The fix is **a targeted `MarkSpent` call directly in the coinstake
construction path**, immediately after the kernel is successfully
signed. This eliminates the dependency on `FixSpentCoins` running
later — `vfSpent` is in sync with reality from the moment the
coinstake is built.

Validated on testnet: 16+ stakes, `repairwallet` returns 0 mismatches
after the run (versus the previous behaviour where mismatches
accumulated until reconciliation ran). 30h+ soak clean. No interaction
with watch-only logic (watch-only addresses cannot stake — no private
key — so the targeted MarkSpent operates exclusively on spendable
UTXOs).

Touched file: `miner.cpp`.

## B.3 Staker mint-retry storm (Velocity back-off)

`Velocity()` (`velocity.cpp`) enforces a minimum block spacing
(`BLOCK_SPACING_MIN = 45s`); a too-early PoS block is rejected. But
`ThreadStakeMiner` discarded `CheckStake`'s return value and slept a
flat 500 ms before retrying — so a staker that found a valid kernel
while the chain tip was younger than 45 s rebuilt the same too-early
block, was rejected again, and spun: a tight CPU/log storm until
wall-clock crossed the threshold.

Fixed in two parts:

- **Spacing back-off gate.** Before `CreateNewBlock`, the staker
  computes `nEarliestValid = tipTime + BLOCK_SPACING_MIN`; if that is
  in the future, it sleeps until then (capped per-iteration,
  rate-limited log) instead of spinning. A too-early block is never
  attempted.

- **`CheckStake` result honoured.** The loop now captures the bool;
  a rejected block backs off the normal miner interval rather than
  retrying in 500 ms.

Touched file: `miner.cpp` (and a `mining.h` include for
`BLOCK_SPACING_MIN`).

## B.4 Staking icon — state-machine rewrite (CW2 / CW6)

The interim staking-icon work in the §31 polish pass added a "warming
up" clock for the post-restart window. Subsequent observation showed
the fix was incomplete:

- It relied on `nLastCoinStakeSearchInterval`, a counter that
  `SignBlock` updates only on EXIT-WITHOUT-FINDING-A-BLOCK paths.
  On a wallet that finds blocks successfully, the counter stays at
  zero — and the icon stayed on "warming up" indefinitely even
  while blocks were being produced.
- The tooltip's expected-time-between-blocks calculation used a
  difficulty × 2³² formula that conflated network stake weight with
  PoW difficulty; produced nonsense values (observed: "1763 days
  13 hours" on a healthy testnet staker).

### B.4.1 Rewrite (CW2 — supersedes the interim fix)

A proper state machine replaces the counter-based logic.

**Miner side** (`miner.cpp`): two `std::atomic` variables published by
`ThreadStakeMiner`:

- `nLastStakeLoopTime` — `GetTime()` snapshot updated at the top of
  every outer-loop iteration. Acts as a heartbeat.
- `fLastStakeLoopProductive` — set to `true` immediately before the
  `SignBlock` attempt; set to `false` at every short-circuit branch
  (wallet locked, vNodes-empty/IBD, fTryToSync early-out,
  voted-consensus defer, velocity-spacing back-off).

Both atomics are read without lock by the GUI thread.

**GUI side** (`qt/bitcoingui.{cpp,h}`): a three-state machine
(`Hammer` / `Clock` / `None`) computed by a Phase-A walk (full
prerequisite check) on initial entry; once Hammer is resolved, a
hammer-latch flag transitions to Phase-B walks (invalidating-events
only) plus a 5-minute staleness floor. This eliminates icon flutter on
transient defers — once the staker is running, the hammer holds steady
until something genuinely changes (wallet locks, peers drop, IBD
re-enters, or the staking thread stops responding).

**Tooltip math** — corrected to the formula the codebase already
exposes via `getstakinginfo`:

```
expected_time = GetTargetSpacing × networkWeight / walletWeight
              = 120 × networkWeight / walletWeight   (seconds)
```

Tooltip formats compact: "1d 2h", "3h 4m", "5m 30s", "45s".

### B.4.2 Maturity-window state (CW6, v1.1 with load-crash fix)

The CW2 state machine still incorrectly reported `None` /
"not staking" whenever `nWeight == 0`, even on a wallet that was
actively staking — because a frequent-staking wallet with one large
UTXO that recycles rapidly will have all stakeable balance permanently
inside the 25-block maturity window, leaving `GetStakeWeight()`
permanently at 0.

CW6 adds a fourth path to the icon logic by also checking `GetStake()`
(the immature-coinstake total shown as "Stake" in the Balances panel):

| Condition | Icon | Tooltip |
|---|---|---|
| `nWeight == 0` AND `nStake > 0` | **Clock** | "Recently staked, coins maturing" |
| `nWeight == 0` AND `nStake == 0` | None | "No mature coins" *(unchanged)* |
| `nWeight > 0` | (existing logic) | (existing) |

The clock state correctly conveys the in-progress state where coins
are advancing through maturity.

**v1.1 load-crash correction.** The first version of CW6 caused a
startup crash because it called `GetStake()` before `pwalletMain` was
fully loaded. v1.1 added a `fWalletLoadComplete` guard that defers the
check until the wallet is ready.

Touched files: `qt/bitcoingui.{cpp,h}`.

---

# C. WALLET

## C.1 Balance computation and spent-tracking

### C.1.1 Cold-start balance underreport (v2.0.0.7 work)

A latent bug present since at least v2.0.0.6 began surfacing in the
v2.0.0.7 work. On large wallets (hundreds of thousands of
transactions, multi-second load), the staking-icon GUI poll could fire
before the keystore was fully populated. `IsMine()` returned
`ISMINE_NO` for outputs whose key hadn't yet been loaded, the
per-transaction balance/credit caches were populated with zeros, and
those zeros persisted for the rest of the session. Symptom:
`getbalance` returned a value substantially below the true balance
after launch, with the magnitude of the underbalance varying across
cold-start runs.

Two-part fix:

- **Init gate.** `fWalletLoadComplete` flag in `init.cpp:82`, set true
  at `init.cpp:1643` after wallet load and `ReacceptWalletTransactions`
  complete. Two GUI poll callbacks gate on this flag and early-return:
  `DigitalNoteGUI::updateWeight` (`bitcoingui.cpp:1600`) and
  `WalletModel::pollBalanceChanged` (`walletmodel.cpp:278`). Both are
  externed via `init.h:21`.

- **Cache invalidation on keystore change.** `MarkAllTxCachesDirty`
  (`cwallet.cpp:2582`) walks `mapWallet` and dirties every per-tx
  balance/credit cache. Called from nine keystore-mutation sites in
  `cwallet.cpp`: `AddKeyPubKey`, `AddCryptedKey`, `AddCScript`,
  `AddWatchOnly`, encrypted-wallet `Unlock`, and several others.
  Internally gated by `fWalletLoadComplete` so that the inevitable
  batch of mutations during wallet load does not produce
  N×|mapWallet| dirties.

Touched files: `src/init.{h,cpp}`, `src/cwallet.{h,cpp}`,
`src/qt/bitcoingui.cpp`, `src/qt/walletmodel.cpp`.

### C.1.2 `vfSpent` → `mmTxSpends` reader migration (CW4 Fix C, new in v2.0.0.8)

The wallet historically maintained two parallel spent-tracking systems:

| System | Type | Populated by | Read by |
|---|---|---|---|
| `vfSpent` | per-tx `std::vector<char>` on CWalletTx | `MarkSpent` / `MarkUnspent` calls from CommitTransaction, ReacceptWalletTransactions, FixSpentCoins, and (per B.2) the targeted PoS-coinstake loop | `CWalletTx::IsSpent(n)` |
| `mmTxSpends` | global `std::multimap<COutPoint, uint256>` on CWallet | `AddToSpends`, automatic from every `AddToWallet` | `CWallet::IsSpent(hash, n)`, with `GetDepthInMainChain() >= 0` filter |

`vfSpent` requires explicit MarkSpent/MarkUnspent calls on the correct
wtx at the correct lifecycle moment — miss the call, the flag is
stale. `mmTxSpends` requires only that the consuming wtx is in
mapWallet (which is automatic for every wtx involving the wallet) and
includes a depth check that filters out orphans automatically.

The codebase had been **partially migrated**: watch-only readers
(`CWalletTx::GetAvailableWatchOnlyCredit` at `cwallettx.cpp:522`,
`GetWatchOnlyBalance`, `GetWatchOnlyStake`) already used the
mmTxSpends path. Spendable readers still used vfSpent.

**Observable symptom.** A wallet that observed a transaction
involving its own keys via P2P gossip (rather than initiating it
locally) would leave the consumed inputs flagged as spendable in
vfSpent. The reported balance over-stated the true available balance
by the value of the consumed inputs. Users could observe this after
running `repairwallet`, which would "mysteriously" reduce their
reported balance.

CW4 Fix C migrates the 8 remaining spendable-side reader sites:

| File:Line | Function | Migration |
|---|---|---|
| cwallet.cpp:291 | CountInputsWithAmount | `pcoin->IsSpent(i)` → `this->IsSpent(pcoin->GetHash(), i)` |
| cwallet.cpp:462 | AvailableCoinsForStaking | same pattern |
| cwallet.cpp:552 | AvailableCoins | same pattern |
| cwallet.cpp:647 | AvailableCoinsMN | same pattern |
| cwallet.cpp:3027 | Reaccept conflict-detection (outer) | `wtxHash` local extracted; `wtx.IsSpent(0/1)` → `this->IsSpent(wtxHash, 0/1)` |
| cwallet.cpp:3050 | Reaccept conflict-detection (inner) | `wtx.IsSpent(i)` → `this->IsSpent(wtxHash, i)` (reuses outer `wtxHash`) |
| cwallet.cpp:5766 | GetAddressBalances | same pattern |
| cwallettx.cpp:445 | CWalletTx::GetAvailableCredit | `IsSpent(i)` → `pwallet->IsSpent(GetHash(), i)` (delegates to parent wallet) |

KEPT unchanged:
- `cwallet.cpp:6247-6266` — `FixSpentCoins` itself. This IS the
  reconciliation function; by design it compares vfSpent against
  chain truth.
- `cwallettx.cpp:308-321` — `CWalletTx::IsSpent(n)` definition. Stays
  callable for `FixSpentCoins` and disk serialization.
- `cwallettx.cpp:64, 115, 179, 184, 212, 241, 246-248, 283-289,
  299-303` — serialization helpers and writers. Disk format
  compatibility — `vfSpent` is still populated correctly for
  back-compat and for `FixSpentCoins` to reconcile.

Validated on parallel-testnet differential test (clone-datadir
methodology): a wallet without Fix C (reference) and a wallet with
Fix C, both running on the same testnet. Three independent
demonstrations of the bug pattern:
- Single-input send: reference wallet drifts up by input value;
  Fix-C wallet tracks correctly.
- Multi-input send (2000 XDN, 2 inputs aggregated): reference drifts
  by change-UTXO value (337.4997); Fix-C tracks correctly;
  `repairwallet` on reference reports exact mismatches.
- Staking (7 stakes accumulated): reference drifts by sum of consumed
  inputs (249,847.26); Fix-C tracks correctly; `repairwallet` on
  reference reports 7 mismatches summing exactly to the drift.

After Fix C, the only remaining reader of `vfSpent` is `FixSpentCoins`
itself.

### C.1.3 Watch-only credit — AddToSpends on live-add (v2.0.0.7 work)

`cwallet.cpp:2625`. `AddToSpends(hash)` was only invoked from the
`fFromLoadWallet=true` branch in `AddToWallet`. Without this in the
live-add branch, `mmTxSpends` was empty for any tx added during a
rescan or live operation. Symptom: watch-only credit on a freshly
imported active address summed to roughly the total ever received
rather than the current unspent balance. Self-healed on restart
because wallet load re-populates `mmTxSpends` from scratch. Note: this
affected watch-only credit only — `GetBalance` did not consult
`mmTxSpends` until CW4 Fix C (C.1.2 above).

### C.1.4 `nTimeSmart` for rescan-discovered txes

`cwallet.cpp:2709-2724`. Previously `wtx.nTimeSmart` was clamped UP to
`latestEntry` (the most recent existing tx's time) when a rescan
discovered an older tx. Result: all rescan-discovered txes ended up
timestamped at the most recent existing tx. Now: if
`blocktime < latestEntry`, set `nTimeSmart = blocktime` directly;
otherwise apply the original clamp.

## C.2 Coin selection

### C.2.1 `AvailableCoinsForStaking` — per-outpoint locks

Changed from per-transaction collateral-amount filtering to
per-outpoint locking via `setLockedCoins` (`cwallet.cpp:406-456`).
Previously, an entire transaction was excluded from staking if any
output equalled the masternode collateral amount (2,000,000 XDN) or
any output passed `IsCollateralAmount()` (multiples of 1 XDN between
1 and 5 XDN). This punished:

- Innocent recipients of 2M XDN payments (entire tx excluded,
  including unrelated change outputs)
- Transactions whose change happened to land at a "collateral amount"
- Users who genuinely received 2M but didn't intend to use it as
  masternode collateral

Now: only excludes outputs explicitly locked by the user via Coin
Control, `lockunspent` RPC, or the masternode UI (which writes through
to `setLockedCoins`).

## C.3 Watch-only

### C.3.1 P2PK `IsMine` fallback

`script.cpp:3470-3494`. When a P2PK output's pubkey-derived keyID
isn't in the keystore, construct the P2PKH-equivalent script and
check `setWatchOnly`. If matched, return `ISMINE_WATCH_ONLY`.
Reasoning: `importaddress` stores the P2PKH form of an address in
`setWatchOnly`, but coinstakes and some receives use the P2PK form for
the same logical address. Without this check, those P2PK outputs would
be invisible to watch-only tracking — stake rewards (which are
coinstakes paying back via P2PK) wouldn't be tracked at all.

Verified dead code for non-watch-only wallets — `keystore.HaveKey(keyID)`
returns true first, returning `ISMINE_SPENDABLE` before ever reaching
this fallback.

### C.3.2 Spendable stake separation

`cwallet.cpp:3220` and `cwallet.cpp:3242`. `ISMINE_ALL` →
`ISMINE_SPENDABLE` in the call to `GetCredit` for `GetStake` and
`GetNewMint`. Was counting watch-only stake/mining-reward into the
wallet's own (spendable) stake/newmint columns. Watch-only stake is
now reported separately by `GetWatchOnlyStake()`.

Verified neutral on non-watch-only wallets — the two filters evaluate
identically when no watch-only addresses are present.

### C.3.3 `RemoveWatchOnly` — three-phase prune

Substantially expanded from a one-line wrapper (`cwallet.cpp:1121+`).
Now performs three phases:

- Phase A (0-60% progress): walk `mapWallet`, identify orphan
  transactions whose only relevance to the wallet was via the script
  being removed. Outputs of these txes will return `ISMINE_NO` from
  `IsMine` after the script is removed, so they would appear as
  "(n/a)" ghost rows in the GUI if not pruned.
- Phase B (60-90%): erase orphans from `mapWallet` and from disk via
  `EraseTx`. Notify GUI per-erased-tx via
  `NotifyTransactionChanged(CT_DELETED)`.
- Phase C (90-100%): mark all remaining transactions dirty so cached
  watch-only credit recomputes on next access.

Now accepts a `RemoveProgressFn` callback for GUI progress reporting
(`cwallet.h:226-227`). Heavy operation on wallets with many
watch-only-related historical txes (the per-tx `IsMine()` evaluation
dominates wall-clock time).

### C.3.4 Compatibility check against CW4 Fix B + Fix C (new in v2.0.0.8)

The CW4 Fix B (B.2) writer-side fix and Fix C (C.1.2) reader migration
were audited against the v2.0.0.7 watch-only work and confirmed
non-regressing:

- **Fix B** operates on the coinstake input within
  `CreateCoinStake`/`CreateNewBlock`. The kernel input must be
  spendable (no private key, no kernel signature), so Fix B's
  targeted MarkSpent operates exclusively on spendable UTXOs. Zero
  interaction with watch-only logic.
- **Fix C** migrates spendable readers to `mmTxSpends`. The watch-only
  readers (`GetAvailableWatchOnlyCredit`, `GetWatchOnlyBalance`,
  `GetWatchOnlyStake`) were already on `mmTxSpends`. Fix C brings
  spendable into alignment; both spendable and watch-only readers now
  share the same correct spent-tracking semantics.
- All v2.0.0.7 watch-only fixes (P2PK IsMine fallback, RemoveWatchOnly
  three-phase prune, GetStake/GetNewMint ISMINE_SPENDABLE filter,
  AvailableCoinsForStaking setLockedCoins approach) are preserved
  byte-for-byte; none of them are in the Fix B/Fix C edit lists.

## C.4 Wallet rebuild — `-rebuildwallet` and `Tools → Compact Wallet`

A complete BDB-cursor-level dump-and-restore mechanism that replaces
`-salvagewallet` for routine wallet maintenance. Reclaims free pages,
rebuilds the B-tree, and produces a smaller wallet file. Preserves
every BDB record type — watch-only addresses, A4 coin locks, stealth
addresses, multisig redeem scripts, the BIP39 mnemonic master key,
address book entries, transaction history, locked outputs,
recovery-phrase flags. Encrypted wallets stay encrypted (the mkey
records are dumped as-is and restored verbatim; no password prompt
during rebuild).

### C.4.1 Pipeline (`walletrebuild.cpp::RebuildWallet`)

1. **Pre-flight checks.** Refuse if `wallet.dat.bak`,
   `wallet.dat.new`, or `wallet.dat.dump` already exist. Refuse if
   free disk space is less than 2× the source wallet size. Refuse if
   `wallet.dat` itself is missing.
2. **Dump.** `DumpAllRecords` cursor-walks the source wallet and
   writes every record to `wallet.dat.dump` in the v1 dump format.
   Read-only on the source. Permissions tightened to 0600 on POSIX.
3. **Close source.** `dbenv.Flush(false)` checkpoints the BDB log and
   releases all open handles to the source wallet.
4. **Create from dump.** `CreateFromDump` reads the dumpfile,
   validates the double-SHA256 checksum and record count *before*
   creating any destination state, then opens a fresh BDB at
   `wallet.dat.new` and writes every record. Records are committed
   in batches of 10,000 to bound BDB's dirty-page cache. A single
   transaction wrapping all records works on small wallets but fails
   with `ENOMEM` (BDB ret=12) on large ones — discovered when the
   800k-record dev wallet hit the cache wall at record 172,073.
   Periodic commit keeps cache pressure bounded with negligible
   commit overhead.
5. **Verify.** `VerifyNewWallet` cursor-walks the freshly written
   `wallet.dat.new` and counts records, comparing to the expected
   count from the dump footer. Any mismatch aborts the rebuild
   without swapping.
6. **Swap.** Two BDB-level renames in order:
   `wallet.dat → wallet.dat.bak`, then `wallet.dat.new → wallet.dat`.
   Uses `dbenv.dbenv.dbrename()` rather than filesystem `rename()` so
   the env's internal log stays consistent with what's on disk.
7. **Cleanup.** Delete `wallet.dat.dump` on both success and failure
   — privacy wins over forensic recovery, and the `.bak` is the
   rollback path.
8. **Outcome marker.** Write `.rebuildwallet-result` for the GUI to
   surface to the user on next paint.

### C.4.2 Crash recovery

The handler at the top of `RebuildWallet` detects a state where
`wallet.dat` is missing but both `wallet.dat.bak` and `wallet.dat.new`
are present — the unambiguous signature of a crash between the two
renames in step 6. The recovery is mechanical: complete the second
rename (`wallet.dat.new → wallet.dat`) and write a
`recovered_from_crash` outcome marker.

### C.4.3 Dump file format v1

```
# DigitalNote wallet rebuild dump created by DigitalNote 2.0.0.8 (<date>)
# * Created on <ISO-8601 UTC>
# * Source wallet: <filename>
# * Best block at time of dump was <height> (<hash>),
#   mined on <ISO-8601 UTC>
# * Format: bdb-raw-v1

<lowercase_hex_key> <lowercase_hex_value>
... (one record per line, BDB cursor order)

# checksum dsha256=<hex> records=<N>
# End of dump
```

### C.4.4 Hidden RPCs

- `dumprawwallet <filename>` — writes a v1 dumpfile from the live
  wallet via cursor walk. Read-only.
- `createfromdumpfile <dumpfile> <new-wallet-filename>` — reads a v1
  dumpfile, validates checksum and count, writes a fresh BDB.

### C.4.5 Marker-file protocol

- `.rebuildwallet-pending` — empty file. Presence signals "perform
  a rebuild on next AppInit2 before LoadWallet". Written by GUI;
  consumed by handler.
- `.rebuildwallet-result` — single text line + optional reason line.
  Written by handler at end of every rebuild attempt. Token values:
  `success`, `recovered_from_crash`, `failed_preswap`,
  `failed_filesystem`.

## C.5 `-salvagewallet` deprecation

The decision to deprecate `-salvagewallet` was made after research into
Bitcoin Core, PIVX, Dash, and Bitcoin ABC. Core removed `-salvagewallet`
in 0.21.0 (PR #17219) for three reasons: default key not preserved,
wallet version not preserved, and keys silently skipped. The DigitalNote
codebase was hitting the third — `CWalletDB::Recover`'s inner loop uses
`DB_NOOVERWRITE`, which silently drops collisions.

`-salvagewallet` now refuses to run unless `-iknowsalvagewalletisdangerous`
is also passed (`init.cpp:553-589`). The help text points users to
`-rebuildwallet`. The escape hatch is preserved for support cases
where rebuildwallet itself fails on a wallet too corrupt for cursor
iteration.

## C.6 BIP39 mnemonic recovery (D2 design)

~675 lines of new functionality in `cwallet.cpp` plus the entire
`src/bip39/` subdirectory. New functions: `AddMnemonicMasterKey`,
`RemoveMnemonicMasterKey`, `HasMnemonicMasterKey`, plus the
modification to `Unlock` that tries all master keys in `mapMasterKeys`
rather than returning false on first decrypt failure.

The wallet stores two `CMasterKey` envelopes encrypted under different
keys. `CMasterKey[1]` decrypts under the password-derived hex key;
`CMasterKey[2]` decrypts under the recovery-phrase-derived hex key.
Both unlock the same `vMasterKey`. `Unlock` and
`ChangeWalletPassphrase` were both modified to iterate all master keys.

**Design D2 — recovery phrase derives from `vMasterKey`, not from the
password.** `AddMnemonicMasterKey` takes no arguments; it derives the
mnemonic from the wallet's `vMasterKey` directly. This means the
24-word recovery phrase is **stable across password changes** — once
a wallet has been BIP39-upgraded, the recovery phrase the user wrote
down stays valid even after a `walletpassphrasechange`.
`CMasterKey[2]` is rotated with the new password but the underlying
`vMasterKey` (and therefore the mnemonic) doesn't change.

GUI: new dialogs `recoveryphraseupgradedialog`, `rotatephrasedialog`,
`seedphrasedialog`. Centralised GUI state management via the new
`guistate` namespace.

New RPC: `getrecoveryphrase` (in `rpcbip39.cpp`).

## C.7 Wallet decryption code (NOT CALLED, retained)

`DecryptWallet` (~200 lines, `cwallet.cpp:1644-1791`). Two-phase
commit: writes all plain keys with overwrite first, then erases
encrypted records, so a mid-operation crash leaves wallet.dat in a
recoverable state. Also writes a safety backup file
(`decrypt_wallet_backup.txt`) before any modification, deleted on
success.

Marked "NOT CALLED — retained for future use" in source. The Settings
menu shows a "Decrypt Wallet..." label when the wallet is locked, but
the action is permanently disabled (`bitcoingui.cpp:1378-1380`); there
is no live path to invoke `DecryptWallet`. The function is kept in the
tree for future development but is not exposed.

## C.8 New BDB record types

- `lockedoutput` — persistent UTXO lock (per-outpoint, replaces
  transaction-level filtering)
- `recoveryphraseflag` — marker that the wallet has been
  BIP39-upgraded
- `EraseLockedOutput`, `EraseRecoveryPhraseFlag` — corresponding
  erase helpers
- `EraseCryptedKey`, `EraseMasterKey`, `WriteKeyOverwrite`,
  `EraseTx` — primitives used by `RemoveWatchOnly` and the (NOT
  CALLED) `DecryptWallet`

All registered in `walletdb.h:50-82`.

---

# D. NETWORK

## D.1 VRX difficulty-recovery curve — fix and determinisation

A correctness-and-determinism story across the v2.0.0.6 → v2.0.0.8
arc. Three distinct items, all in `VRX_ThreadCurve` (`blockparams.cpp`).

### D.1.1 v2.0.0.6 baseline — the recovery curve never engaged

In v2.0.0.6, `VRX_ThreadCurve` defined the stall-time input as:

```cpp
blkTime = pindexLast->GetBlockTime();
cntTime = BlockVelocityType->GetBlockTime();
difTime = blkTime - cntTime;
```

`BlockVelocityType` comes from
`GetLastBlockIndex(pindexLast, fProofOfStake)` — which walks back to
the most recent block of the *requested* type. On a PoW retarget,
`fProofOfStake=false`, and the walk terminates at `pindexLast` itself
(if `pindexLast` is PoW) or at the most recent PoW ancestor.

Either way, on the PoW retarget code path, `blkTime == cntTime` and
`difTime == 0`. The recovery loop:

```cpp
while(difTime > (hourRounds * 60 * 60)) { ... }
```

was therefore **dead code in practice** — the predicate was never
true and the loop body never executed. Difficulty did not drop
progressively during stalls; the chain had no automatic recovery
mechanism. Long pauses on mainnet (the "block not found for an hour"
events) had to recover via other means (manual mining catch-up,
network self-correction over many subsequent blocks). This was the
underlying cause of mainnet's painful chain-stall behaviour.

The bug existed in v2.0.0.6 throughout its lifetime. It is corrected
in v2.0.0.8 by introducing a real `difTime` value (see D.1.2 below)
and then making the calculation deterministic (D.1.3).

### D.1.2 Curve-engagement fix introduced post-v2.0.0.6 (wall-clock based)

Between v2.0.0.6 and the v2.0.0.7/v2.0.0.8 development cycle, a fix
was added to make the recovery curve actually engage during stalls:

```cpp
wallClockDelta = GetAdjustedTime() - blkTime;
difTime = std::max(blkTime - cntTime, wallClockDelta);
```

`GetAdjustedTime()` is the node's network-adjusted wall clock. When
the chain stalls, the wall clock advances past the last block time,
`wallClockDelta` grows, the recovery loop engages, and difficulty
drops progressively. This worked correctly at mint time: the miner's
wall clock at block-construction time is approximately the block's
own timestamp, so the computed `nBits` is approximately what the
block ends up sealed with.

But it broke from-genesis resync. When a node re-validates a
historical block, the validator's `GetAdjustedTime()` is the current
wall clock (potentially years after the block was mined), `blkTime`
is the historical block's predecessor time, `wallClockDelta` is
*enormous*, the recovery loop engages hard, and the validator
computes a completely different `nBits` than the block carried.
Result: `AcceptBlock` rejects the block on the `nBits` mismatch and
the chain cannot resync past the first stall-recovery block.

This was the manifested symptom that originally motivated D.1.3 —
chains failing to resync past a specific block in the historical
record.

### D.1.3 Determinism via committed block time

D.1.3 preserves the engagement behaviour of D.1.2 (the curve does
fire during real stalls) while making the validator's calculation
deterministic.

Threaded an optional `int64_t nNewBlockTime` through
`GetNextTargetRequired → VRX_Retarget → VRX_ThreadCurve`. Final form:

```cpp
int64_t blockDelta = blkTime - cntTime;
int64_t nEffectiveNewTime = (nNewBlockTime > 0) ? nNewBlockTime
                                                : GetAdjustedTime();
int64_t wallClockDelta = nEffectiveNewTime - blkTime;
difTime = std::max(blockDelta, wallClockDelta);
```

Caller responsibilities:
- **Validation** (`AcceptBlock` in `cblock.cpp`) passes the candidate
  block's committed `GetBlockTime()`. The retarget for a given
  historical block is now a pure function of committed chain data
  and reproduces identically on every node, forever.
- **Mining** (`miner.cpp:251`, in `CreateNewBlock`) passes
  `GetAdjustedTime()` — preserves D.1.2's curve-engagement
  behaviour during live block production.
- `nNewBlockTime == 0` is the defensive "not supplied" fallback to
  `GetAdjustedTime()` rather than computing a nonsense negative
  delta.

A fresh testnet genesis was rebuilt on a binary containing this fix;
that chain resyncs from genesis with no `nBits` mismatch.

Touched files: `blockparams.{h,cpp}`, `cblock.cpp`, `miner.cpp`,
`rpcmining.cpp`.

### D.1.4 Historical mainnet `nBits` exception list — extended

The mainnet historical `nBits` exception list in `AcceptBlock` falls
into two categorically distinct classes. Both are canonical chain
history; both must be honoured by any conforming validator; but they
have unrelated origins and are documented separately so the
provenance of each entry is clear.

**Class A — controlled fork operations (4 entries, pre-existing).**
Blocks produced under deliberately-set minimum difficulty
(`1f00ffff`) as part of controlled chain operations:

- Heights **46921, 46923, 46924** — the v1.0.1.5 mandatory-update
  activation cluster, May 2019. Three blocks within ~3 minutes, all
  at floor difficulty, all carrying the activation transition for
  the mandatory upgrade gated by
  `VERION_1_0_1_5_MANDATORY_UPDATE_START`.
- Height **403116** — the predecessor block to the v1.0.4.2
  hardfork at height 403117. Floor difficulty was set to provide a
  deterministic, instantly-mineable anchor block immediately
  preceding the chain correction. Block 403117 itself carries the
  one-shot treasury operation (1,000,000,000 XDN injection via
  `GetDevOpsPayment`'s `nHeight == VERION_1_0_4_2_MANDATORY_UPDATE_BLOCK`
  branch) and is *not* in the exception list — its `nBits` is
  consensus-derivable.

These four entries have always been in the exception list (since
v2.0.0.6 or earlier). They are not "stall-recovery archaeology" and
v2.0.0.8 does not change their status. Listed here for completeness
of the exception-list provenance record.

**Class B — stall-recovery archaeology (~30 new entries).**
Blocks where v2.0.0.6's broken VRX recovery curve (D.1.1) produced
different `nBits` than v2.0.0.8's working curve (D.1.2 / D.1.3)
computes. v2.0.0.6's recovery loop never engaged at all (`difTime`
was always zero on PoW retarget), so during long stalls the miner
computed difficulty from the standard NORMAL retarget path while
v2.0.0.8's working curve correctly drops difficulty toward the
floor. Each Class B block is a stall-recovery event somewhere in
mainnet history, identifiable by:

- A long block-spacing gap from the preceding block (typically
  exceeding the recovery curve's hourly boundaries at 3600 / 7200 /
  10800 / 14400 / 18000 seconds)
- A `nBits` mismatch where v2.0.0.8's computed value is *easier*
  than what the block carries (mantissa larger, or exponent higher)
- Distribution roughly 75% PoS / 25% PoW, spread across mainnet
  history from May 2019 through 2024 with no era concentration
- Two extreme cases (heights 394624 and 423410) where v2.0.0.8's
  curve fully reached the `1f00ffff` floor

These blocks were consensus-valid under v2.0.0.6 rules at mint
time. v2.0.0.8 grandfathers them via height exception so the
working-curve correction does not invalidate existing chain
history.

A representative illustrative case is **height 138092**, a
December 2019 PoS block sealed after an 82-minute stall. Three
distinct historical mechanisms visible in the same block:

1. **Pre-swap reward layout**: at this height the staker reward was
   150 XDN and the masternode reward was 100 XDN (see H.6 for the
   reward swap at height 403117). The staker received their 150;
   the masternode would have received 100.
2. **Devops fallback fired**: the 82-minute stall left the producer
   with a stale local masternode list, so `GetCurrentMasterNode()`
   returned no payee, and the masternode share was paid to the
   devops address per the consensus fallback rule. Devops received
   100 (the unclaimed MN share) plus 50 (the normal devops payment),
   summing to 150 XDN to devops in that block. Total subsidy
   remained `nBlockStandardReward` (300 XDN) — staker 150 + devops
   150.
3. **Broken VRX curve**: block carries `1c096cbb` (v2.0.0.6 NORMAL
   retarget value), v2.0.0.8 computes `1c12d977` (easier — the
   working curve dropped difficulty for the long stall).

Each of these is canonical and intentional under the rules at the
time. v2.0.0.8 does not change any of them; the exception list
makes block 138092 acceptable to a re-syncing v2.0.0.8 validator
without disturbing its historical content.

**Architectural property preserved.** The strict `nBits` check
remains fully active. There is no tolerance band, no leniency, no
relaxed comparison. The exception list is a height-keyed allow-list
that the validator consults before applying the strict check —
forgery at any non-exception height fails immediately, and forgery
at an exception height would require winning the chain-work race
for that historical block (computationally infeasible).

**Exception list closure**. Every block mined under v2.0.0.8
produces `nBits` from the deterministic working curve, so miner and
validator necessarily agree (see D.1.5 for the residual edge case
and its CW7 fix that eliminates it entirely). The exception list
**never grows after v2.0.0.8 tag** — Class A is by definition
historical, and Class B is closed by D.1.3+D.1.5 working together
for all post-tag blocks.

Touched file: `cblock.cpp` (exception list constants and the height
check at the `nBits != nBitsRequired` site).

### D.1.5 Mining-side same-timestamp guarantee (CW7)

The post-D.1.3 mining code at `miner.cpp:251` calls
`GetNextTargetRequired(..., GetAdjustedTime())` early in
`CreateNewBlock`, before `pblock->nTime` is finalized (at
`miner.cpp:701`, with `UpdateTime` applied for PoW at line 705). The
two wall-clock reads are typically within milliseconds-to-seconds of
each other and round to the same answer through the recovery loop's
hourly boundaries.

But for a stall block whose wall-clock delta from `blkTime` is
straddling one of the recovery-loop boundaries (3600, 7200, 10800,
14400, 18000 seconds), the few-second gap between the two reads can
push the validator-recomputed delta across the boundary, producing a
different `nBits`. The block then fails its own producer's
self-validation in `AcceptBlock` and is silently dropped; the miner
restarts `CreateNewBlock` with a fresh timestamp and usually
succeeds on retry, but in rare cases the block proceeds and becomes
a new historical mismatch requiring an exception-list entry.

Probability per stall: approximately `(typical T_seal - T_nbits
gap) / (boundary spacing) × (boundaries reached)` ≈ `5s / 3600s × 5`
≈ 0.7%.

**Fix**: compute `pblock->nBits` from the *same* timestamp that will
be sealed into `pblock->nTime`. `pblock->nBits` is set at line 251
but never read between lines 251 and 701, so the assignment can be
moved to after `pblock->nTime` is set:

```cpp
// In CreateNewBlock, after pblock->nTime is finalized (around line 705
// for PoW, line 701 for PoS):
pblock->nBits = GetNextTargetRequired(pindexPrev, fProofOfStake, pblock->nTime);
```

After CW7, miner and validator compute `nBits` from the byte-identical
`pblock->nTime` value. There is no possible boundary-crossing
divergence; no new historical exception entries can arise from
post-tag blocks; the D.1.4 exception list is permanently closed at
its v2.0.0.8 size.

Touched file: `miner.cpp` — one line moved, one line deleted.

### D.1.6 Diagnostic logging

`VRX_Retarget DIAG` lines — per-block retarget path
(`DRYRUN` / `CRVRESET` / `NORMAL`) with `difTime`, `hourRounds`, and
the computed `nBits`. `AcceptBlock nBits MISMATCH` line — prints, on
any difficulty mismatch, the `nBits` the block carries versus the
`nBits` the node computed. Both are gated behind `-debug=retarget`
in normal operation; the `MISMATCH` line is left ungated as a
reject-path diagnostic that is operationally valuable.

Post-CW8 the `MISMATCH` line additionally carries a `[PoS]` / `[PoW]`
tag so future archaeology doesn't need a separate enrichment pass.

## D.1bis Devops address rotation, strict-check re-enablement, and producer/validator off-by-one fix (CW9)

A coordinated three-part change shipped under the umbrella label
"CW9" and gated by the consensus constant
`VERION_2_0_1_0_MANDATORY_UPDATE_BLOCK = 1400000` on mainnet and
`VERION_2_0_1_0_TESTNET_UPDATE_BLOCK = 100` on testnet. Together
these resolve the longstanding deferral of "Dev address rotation"
recorded in earlier changelogs as H.8, fix a producer/validator
off-by-one in the ladder lookup that has been latent since v1.0.1.5,
and restore the strict devops-address check that was commented out
sometime in the v2.0.0.6-or-earlier era.

### D.1bis.1 The deferred problem

The v1.0.4.2 devops address `dafC1LknpDu7eALTf5DPcnPq2dwq7f9YPE` has
been the active devops recipient since July 2021. Over the
intervening four-plus years its wallet has accumulated hundreds of
thousands of incoming transactions (one per block, plus reward
splits, plus user donations, plus internal operational
transactions). Repeated Compact Wallet runs reduce the live record
set but cannot reduce the BDB file below approximately 720MB —
appended-only growth and free-list fragmentation keep the on-disk
footprint large even after compaction.

A wallet that large is operationally painful (slow rescans, long
backup times, slow `repairwallet`, slow `dumpwallet`) and creates a
single point of failure: the entire treasury's spending capability
depends on one wallet file that has been continuously operating
across multiple Linux distributions, multiple Bitcoin-Qt forks, and
multiple binary upgrades. The rotation moves the active receiving
address to a fresh wallet with no transaction history, immediately
restoring fast operations.

### D.1bis.2 Rotation activation parameters

| Network | New devops address | Activation block | Activation date (estimated) |
|---|---|---|---|
| Mainnet | `dGoFPie9QZmQ1Ty1beqSHytxNruehpGtGa` | 1,400,000 | ~early September 2027 (at current ~164,981 blocks/year, ~15 months from June 2026 tip 1,180,777) |
| Testnet | `tSRDftd9ghEZq3pbwRmwp2FT7VuLcvmtnX` | 100 | Within first few hours of testnet genesis-restart |

Both addresses are fresh wallets generated specifically for v2.0.1.0.
No prior transaction history; not derivable from any pre-rotation
wallet. The testnet activation height of 100 is intentionally early
so that the rotation mechanism is exercised live within the first
hour of testnet operation, providing immediate empirical
confirmation that producer and validator agree on the rotation
boundary before the same code reaches mainnet.

### D.1bis.3 The off-by-one — diagnosis

`getDevelopersAdress(const CBlockIndex*)` returned the devops address
expected for the block referenced by its `pindex` argument. Code
called this with two distinct meanings:

**Validator** (`cblock.cpp:1322`): passed `pindex` — the block being
validated. Correct: asks "what address should THIS block pay?"

**Producer** (`miner.cpp:531`, `miner.cpp:617`, `cwallet.cpp:4046`,
`cwallet.cpp:4126`, `rpcmining.cpp:830`): passed `pindexBest` or
`pindexPrev` — the chain tip, NOT the block being constructed.
Incorrect: asks "what address did the PREVIOUS block pay?" when the
intent was "what address should the block I'm about to mine pay?"

At any rotation boundary, ladder(N-1) ≠ ladder(N), so the producer
constructed a block paying the OLD address while the validator
expected the NEW. The defect was latent across the entire chain
history because:

- v1.0.0.0 → v1.0.1.5 transition (May 2019): occurred during a
  controlled hardfork with time-based gates and a transition window
  where validator was lax
- v1.0.1.5 → v1.0.4.2 transition (July 2021): occurred during a
  controlled chain-correction rollback (block 403116 → 403117) where
  the validator was forced lax by checkpoint
- The strict address check had been commented out since pre-v2.0.0.6,
  so post-rotation chain mismatches logged but did not reject

Without the strict check, the only observable consequence was that
producers paid the OLD devops address for one block at each rotation
boundary. Audit of mainnet chain history confirms: block 65020
(v1.0.1.5 rotation) and block 403117 (v1.0.4.2 rotation) both have
producer-paid recipients that disagree with the ladder's expected
output. Both blocks remain canonical mainnet history.

### D.1bis.4 The off-by-one — fix

The ladder function is refactored to take height directly. The
existing `CBlockIndex*`-based signature becomes a thin wrapper.

```cpp
// New primary function.  Caller passes the height of the block
// whose devops payee is being determined.
std::string getDevelopersAdressForHeight(int nHeight, int64_t nBlockTime);

// Backward-compatible wrapper, used by validator code.
std::string getDevelopersAdress(const CBlockIndex* pindex);
```

The wrapper delegates: `return getDevelopersAdressForHeight(pindex->nHeight, pindex->GetBlockTime());`

All producer callers migrate to the height-based form, passing
`pindexBest->nHeight + 1` or `pindexPrev->nHeight + 1` (the height
of the block being constructed):

```cpp
// Before (off-by-one):
getDevelopersAdress(pindexBest);

// After (correct):
getDevelopersAdressForHeight(pindexBest->nHeight + 1, GetAdjustedTime());
```

Validator callers continue using the wrapper — their `pindex`
argument was already the block being validated, so no off-by-one
exists on that side. Wrapper preserves source-level compatibility
without changing semantics.

The `nBlockTime` parameter on the new function is used only by the
legacy pre-v1.0.1.5 time-based boundary check (`nBlockTime <
VERION_1_0_1_5_MANDATORY_UPDATE_START`). Any timestamp from a block
mined post-May-2019 produces the same result through that branch,
so producers calling the new function from the present forward may
pass `GetAdjustedTime()` interchangeably with any committed block
time.

### D.1bis.5 Strict check re-enablement — height-gated

The commented-out blocks at `cblock.cpp:1530-1536` (PoS path) and
`cblock.cpp:1715-1721` (PoW path) are restored, with a height gate
ensuring the strict comparison fires only from the rotation
activation block onwards:

```cpp
// New (post-CW9):
LogPrintf("CheckBlock() : PoS Recipient devops address validity "
          "could not be verified -- expected %s, got %s\n",
    strVfyDevopsAddress.c_str(),
    addressOut.ToString().c_str());

const int nStrictHeight = TestNet()
    ? VERION_2_0_1_0_TESTNET_UPDATE_BLOCK
    : VERION_2_0_1_0_MANDATORY_UPDATE_BLOCK;

if (pindex->nHeight >= nStrictHeight)
{
    fBlockHasPayments = false;   // strict rejection post-rotation
}
// pre-rotation: log only, accept
```

Resulting behaviour matrix:

| Network | Heights | Validator behaviour |
|---|---|---|
| Mainnet | 1 → 1,399,999 | Lax (log mismatch, accept block) — preserves all canonical chain history, including the v1.0.1.5 transition irregularities and the v1.0.4.2 chain-correction block 403117 mismatch |
| Mainnet | 1,400,000 → ∞ | Strict — `fBlockHasPayments = false` on mismatch |
| Testnet | 1 → 99 | Lax |
| Testnet | 100 → ∞ | Strict |

The log line is also improved to print expected-vs-actual instead of
just "could not be verified" — operationally useful for any
mismatch report.

### D.1bis.6 Boundary semantics

Two boundary semantics are kept distinct intentionally:

- **Pre-existing v1.0.4.2 boundary uses `<=`** (preserved verbatim
  from pre-CW9 code). Block at exactly height 403117 returns the
  v1.0.1.5 address according to the ladder, even though chain history
  shows that block actually paid the v1.0.4.2 address. The lax
  pre-rotation validator absorbs this; do not change `<=` to `<`
  because chain history depends on the existing comparison semantics
  at the validator side (the strict check having been commented out
  is exactly what allowed this discrepancy to persist).

- **New v2.0.1.0 boundary uses `<` (strictly less than)**. Block at
  exactly `VERION_2_0_1_0_MANDATORY_UPDATE_BLOCK` returns the NEW
  v2.0.1.0 address — i.e. the rotation activation block itself is
  the FIRST block under the new regime. Producer and validator
  agree because both ask the ladder about the same height after the
  off-by-one fix.

### D.1bis.7 Why no transition window

The v1.0.1.5 rotation included a transition window: a 52-hour period
(`VERION_1_0_1_5_MANDATORY_UPDATE_START` to
`VERION_1_0_1_5_MANDATORY_UPDATE_END`) during which the validator
accepted either the old or new address. The intent was operator
forgiveness during the upgrade window.

Empirically that transition window caused more problems than it
solved. The v1.0.1.7 historical commit
(`911ec6fbaa72839b4309e35e5e45caa8d855df04`) reveals that the
transition-window code had an off-by-one: it used
`pindexBest->GetBlockTime()` (the chain tip's timestamp) instead of
`pindex->GetBlockTime()` (the block being validated's timestamp).
After the tip's timestamp passed `_END`, strict mode kicked in
retroactively for ALL blocks during resync — including those within
the transition window — breaking from-genesis sync. The fix in the
v2.0.0.x era was simply to comment out the strict check entirely.

CW9 ships without a transition window. The producer/validator
off-by-one fix removes the original reason transition windows were
needed (producer and validator now agree on every block including
the boundary). Removing the window also removes the entire class of
off-by-one bugs that haunted the v1.0.1.5 transition.

### D.1bis.8 Pre-rotation chain history is canonical and unchanged

CW9 changes nothing about the validation of any block at height <
`VERION_2_0_1_0_MANDATORY_UPDATE_BLOCK`. The lax pre-rotation
validator continues to accept whatever the chain history shows.
This is the correct design choice: any attempt to retroactively
re-validate the chain under stricter rules would reject blocks
that have been canonical for years, breaking resync for every
operator at every level (full nodes, exchanges, pool operators,
explorer maintainers).

The full pre-rotation chronology of devops payments, established
through full-chain archaeology against v2.0.0.6 source and confirmed
empirically via RPC probe (June 2026):

| Phase | Heights | Time | Devops behaviour |
|---|---|---|---|
| Reserve emission | 1 → ~16 | Jan 27 2019+ | Bulk 80M XDN supply; no devops vouts |
| Standard rewards, no devops | ~16 → 28265 | Jan-Apr 2019 | Pre-`START_DEVOPS_PAYMENTS` |
| **Devops + MN activation** | 28266 | **Apr 5 2019 20:00 UTC** | `START_DEVOPS_PAYMENTS = 1554494400` gate fires |
| v1.0.0.0 era | 28266 → 65019 | Apr-Jul 2019 | `dSCXLHTZ...` is devops recipient (sparse early — masternodes paid the share on some blocks; consistent later) |
| **v1.0.1.5 rotation** | 65020 | **Jul 2 2019 21:05 UTC** | Single-block rotation to `dHy3LZv...` (same UTC second as 65019 — single producer's binary swap-over) |
| v1.0.1.5 era | 65020 → 403116 | Jul 2019 → Jul 2021 | `dHy3LZv...` is devops recipient |
| **v1.0.4.2 rotation** | 403116 → 403117 | **Jul 2021** | Chain-correction hardfork; 1B XDN treasury injection at 403117; rotation to `dafC1Lkn...` |
| v1.0.4.2 era | 403117 → 1,399,999 | Jul 2021 → ~Sep 2027 | `dafC1Lkn...` is devops recipient |
| **v2.0.1.0 rotation** | 1,400,000 | **~Sep 2027** (estimated) | Rotation to `dGoFPie9...`, strict check re-engages |
| v2.0.1.0 era | 1,400,000 → ∞ | ~Sep 2027+ | `dGoFPie9...` is devops recipient |

One peculiarity of the v1.0.0.0 era is worth recording: the address
`dSCXLHTZJJqTej8ZRszZxbLrS6dDGVJhw7` was an active PoS staking
wallet (84 staking-reward blocks observed between heights 2300 and
18400) before it was anointed as the v1.0.0.0 devops recipient at
block 28266. Whoever was running that staker also became the
devops operator. The two roles continued in parallel — same wallet,
collecting both staking rewards as a normal PoS participant AND
50-XDN devops payments as the consensus-defined recipient — until
the v1.0.1.5 rotation moved the devops role to `dHy3LZv...`.

Blocks 28331 and 28332 have anomalous coinstake-position amounts
(50.50 and 50.0002 instead of exactly 50). These are early coinstake
construction bugs in the v1.0.0.0 binary that included fee components
in the devops vout amount. Fixed in some subsequent point release;
the affected blocks are canonical history and validate lax under
CW9.

### D.1bis.9 Producer/validator agreement under the new design

After CW9, the canonical producer-validator agreement at the
rotation boundary is:

```
Block 1,399,999 (mainnet, the LAST block of the v1.0.4.2 era):
  Producer asks getDevelopersAdressForHeight(1399999, t)
    -> nHeight < VERION_2_0_1_0_MANDATORY_UPDATE_BLOCK (1400000)
    -> returns VERION_1_0_4_2_DEVELOPER_ADDRESS = "dafC1Lkn..."
  Validator asks getDevelopersAdressForHeight(1399999, t)  (via wrapper)
    -> same result, "dafC1Lkn..."
  Strict check: nHeight (1399999) >= nStrictHeight (1400000) is false
    -> lax check, accept regardless

Block 1,400,000 (mainnet, the FIRST block of the v2.0.1.0 era):
  Producer asks getDevelopersAdressForHeight(1400000, t)
    -> nHeight < VERION_2_0_1_0_MANDATORY_UPDATE_BLOCK is FALSE
    -> returns VERION_2_0_1_0_DEVELOPER_ADDRESS = "dGoFPie9..."
  Validator asks getDevelopersAdressForHeight(1400000, t)
    -> same result, "dGoFPie9..."
  Strict check: nHeight (1400000) >= nStrictHeight (1400000) is true
    -> STRICT enforcement engaged
  Producer paid "dGoFPie9..." (correct per ladder)
    -> matches, fBlockHasPayments stays true, block accepts
```

Identical analysis holds for testnet with `VERION_2_0_1_0_TESTNET_UPDATE_BLOCK = 100`.

### D.1bis.10 Deferred items

A checkpoint at rotation_height + 2000 (mainnet: block 1,402,000;
testnet: block 2,100) was originally planned to ship in v2.0.0.8.
This is **deferred to a future point release** (v2.0.0.8.1 or
v2.0.0.9) because the block in question does not yet exist — there
is no block hash to anchor the checkpoint to.

Once block 1,402,000 has been mined post-rotation on mainnet, the
next point release should add:

```cpp
(1402000, uint256("0x<hash of block 1402000>"))
```

to `checkpoints.cpp`. This anchors the chain past the rotation
window, preventing deep reorgs from reaching back into the lax
pre-rotation validation region.

Until that checkpoint ships, deep-reorg protection in the rotation
window relies on accumulated chain work alone. Adequate at
>2000 confirmations under normal operation, but explicit
checkpointing is the canonical mechanism and should be added when
the block exists.

## D.1ter GUI transaction-list one-block lag for locally-mined PoW coinbases (CW10)

### D.1ter.1 The symptom

Solo PoW miners on v2.0.0.8 reported that newly-mined coinbase
rewards did not appear in the wallet GUI's *Recent Transactions* or
*Transactions* tab when the block was sealed. The reward appeared
only when the *next* block arrived — i.e. with one block of lag.

Confirmed via:
- `listtransactions "*" 20` shows the immature 150 XDN entry
  immediately after the block lands
- The GUI shows nothing until a subsequent block is added
- When the subsequent block lands, the previous one's coinbase
  appears with the correct (10-minute-old) timestamp

The wallet was recording the transaction; only the GUI display
lagged.

### D.1ter.2 Mechanism — a textbook race

The notification path for newly-added wallet transactions:

```
ConnectBlock(block N)
  → SyncTransaction(coinbase)
    → AddToWalletIfInvolvingMe
      → AddToWallet
        → NotifyTransactionChanged(hash, CT_NEW)  [cwallet.cpp:2778]
```

The GUI's static handler in `transactiontablemodel.cpp` runs
synchronously on the core thread the moment that signal fires:

```cpp
mapWallet_t::iterator mi = wallet->mapWallet.find(hash);
bool inWallet     = mi != wallet->mapWallet.end();
bool showTransaction = (inWallet
                         && TransactionRecord::showTransaction(mi->second));
```

`TransactionRecord::showTransaction()` for a coinbase tx requires
`IsInMainChain()` to be true:

```cpp
bool TransactionRecord::showTransaction(const CWalletTx &wtx)
{
    if (wtx.IsCoinBase())
    {
        if (!wtx.IsInMainChain())
            return false;
    }
    return true;
}
```

`CBlockIndex::IsInMainChain()`:

```cpp
bool CBlockIndex::IsInMainChain() const
{
    return (pnext || this == pindexBest);
}
```

At the moment `NotifyTransactionChanged` fires from `AddToWallet`,
the chain index for block N has not yet been linked: `pnext` is
null and `pindexBest` is still block N-1. **Both clauses of
`IsInMainChain()` evaluate to false.** Therefore `showTransaction`
is `false`.

The static handler queues `updateTransaction(hash, CT_NEW, false)`
via `Qt::QueuedConnection`. When the GUI thread processes it,
`priv->updateWallet` examines the captured `showTransaction=false`
for `CT_NEW` and silently drops the row.

The chain link is then completed at `cblock.cpp:2283`:

```cpp
pindexNew->pprev->pnext = pindexNew;
```

But the GUI's queued-event was already dispatched with the
pre-link `showTransaction=false` value.

### D.1ter.3 The historical "previous-block" hack

The original Bitcoin-Qt code base addressed this with a deferred
notify in `cblock.cpp:1103-1110`:

```cpp
if (pindexNew == pindexBest)
{
    // Notify UI to display prev block's coinbase if it was ours
    static uint256 hashPrevBestCoinBase;

    g_signals.UpdatedTransaction(hashPrevBestCoinBase);
    hashPrevBestCoinBase = vtx[0].GetHash();
}
```

The intent: store the current block's coinbase hash in a static
variable, fire the notify only on the *next* successful chain
extension. By that point the previous block's `IsInMainChain()` is
definitely true, the static handler computes `showTransaction=true`,
and the GUI inserts the row.

This works — but it bakes in a one-block UI lag for every locally
mined PoW coinbase, forever. PoS coinstakes are not affected
because `vtx[1]` (coinstake) is not subject to the `IsCoinBase()`
filter in `showTransaction()`. Mainnet stakers see their rewards
instantly. Solo PoW miners see them one block late.

The lag has been present in the code base since the original
Bitcoin Core heritage and survives in many forks. The reason it
came to attention during v2.0.0.8 verification was the genesis-
restart testnet with a single PoW miner — the lag was the only
update path actually exercised under controlled conditions.

### D.1ter.4 The fix

`cblock.cpp:1108`. Add one signal emission at the existing
`hashPrevBestCoinBase` site:

```cpp
if (pindexNew == pindexBest)
{
    static uint256 hashPrevBestCoinBase;

    g_signals.UpdatedTransaction(hashPrevBestCoinBase);

    // v2.0.0.8 CW10: also notify UI of the CURRENT block's coinbase.
    // By the time execution reaches here, pnext and pindexBest are
    // set correctly, so the re-notify reads IsInMainChain()=true,
    // computes showTransaction=true, and the GUI inserts the row.
    g_signals.UpdatedTransaction(vtx[0].GetHash());

    hashPrevBestCoinBase = vtx[0].GetHash();
}
```

`CWallet::UpdatedTransaction()` (cwallet.cpp:6065-6082) guards by
`mapWallet.find(hashTx)` — so the call is a no-op on any node that
didn't mine the block. Nodes that did mine see the GUI update
immediately.

The pre-existing `UpdatedTransaction(hashPrevBestCoinBase)` line is
preserved deliberately. Should anything elsewhere in the code base
depend on the "previous block coinbase notification" side effect,
that path is unchanged. The new line is purely additive.

### D.1ter.5 Scope and risk

| Item | Detail |
|---|---|
| Files touched | 1 (`cblock.cpp`) |
| Net line change | +1 line of code, +30 lines of comment explaining why |
| Header changes | None |
| Consensus impact | None — `g_signals.UpdatedTransaction` is a UI signal only |
| P2P impact | None |
| Wallet.dat impact | None |
| Affects | Solo PoW miners running the GUI |
| Does NOT affect | PoS stakers, pool miners (external), daemon-only nodes, relay nodes |
| Risk | Very low — additive UI signal, existing notify preserved |

### D.1ter.6 What this validates

The fix being trivially small (1 effective line) underscores that
this was a pre-existing latent display issue, not a regression
caused by any v2.0.0.7 or v2.0.0.8 work. Verified against the
v2.0.0.6 reference `walletmodel.cpp` from the historical archive
— the polling code and the static `hashPrevBestCoinBase` mechanism
are byte-identical to the current source. The bug shipped with
DigitalNote from its earliest releases and has been masked by the
fact that the affected user population (solo PoW miners watching
the GUI) is small.

## D.1quater Masternode payee validation — activation gating (CW12)

**Shipping in v2.0.0.8:** CW12 (activation gate) only.

**Deferred to post-soak / v2.0.0.9 if needed:** CW11 (tiered DoS) + PB-MN-FETCH Lite (active broadcast fetch). See `DEFERRED-CW11-PBMNFETCH-notes.md` for full description and packaged-but-not-shipping bundle. The deferred fixes address a hypothetical post-activation propagation race that has not been empirically observed in any test run; they remain ready to ship as a hotfix if the race manifests at activation.

### D.1quater.1 The symptom

During v2.0.0.8 testnet validation, a new staker was brought online.
Within minutes a single block (height 206) paying the new staker's
masternode was rejected by **5 of 8** testnet nodes with:

```
CheckBlock() : PoW Recipient masternode address validity could not be verified -- rejecting
CheckBlock() : PoW/PoS non-miner reward payments could not be verified
ERROR: CheckBlock() : PoW/PoS invalid payments in current block
Misbehaving: 192.168.1.1:61374 (0 -> 100) BAN THRESHOLD EXCEEDED
```

The rejecting nodes had not yet received the new masternode's
`dseep` registration broadcast.  The other 3 nodes had received it.
A peer relaying the block was instant-banned (DoS 100, the v2.0.0.8
penalty for this check, raised from DoS 10 earlier in v2.0.0.8).
Because all peers in the LAN were connecting via NAT hairpin, the
peer-IP `192.168.1.1` represented the gateway interface for every
LAN peer -- effectively partitioning the LAN.  Each affected node
sat stalled for the duration of the 24h default ban or until a
non-banned peer happened to deliver the missing broadcast.

### D.1quater.2 Root-cause structural analysis

Two layers compound here, both worth unpacking.

**Layer 1 — Regression: a never-active legacy gate was removed without
understanding why it existed.**

In v2.0.0.6, the strict "is the payee a registered masternode" check
in `CheckBlock` was wrapped in a flag called `fMnAdvRelay`:

```cpp
// v2.0.0.6: init.cpp
fMnAdvRelay = GetBoolArg("-mnadvrelay", false);

// v2.0.0.6: cblock.cpp PoS unknown-payee site
if (nMasterNodeChecksEngageTime != 0)
{
    if (fMnAdvRelay)
    {
        // reject
        fBlockHasPayments = false;
    }
    else
    {
        LogPrintf("CheckBlock() : PoS Recipient masternode address
                  validity skipping, Checks delay still active!\n");
        // do NOT reject -- log only
    }
}
```

The `fMnAdvRelay` flag defaulted to false and was set only by the
`-mnadvrelay` command-line / config switch, which **was never used in
production** -- no docs, no operator guidance, no examples in the
field point to a node having ever set this true on mainnet.  The
effective v2.0.0.6 mainnet behaviour was: this strict check never
fired.  Whoever wrote v2.0.0.6 left it disabled-by-default,
presumably aware that it depends on volatile gossip state and
shouldn't be enforced unconditionally.

In v2.0.0.8, under "Spec C D2: fMnAdvRelay gate removed," we
unconditionally enabled the strict check on the assumption that
"a payee that is neither a registered masternode nor the devops
fallback address is invalid -- reject unconditionally once the
checks-delay warmup has elapsed."  That assertion is wrong --
"invalid" depends on this node's view of the mn list, not on chain
consensus.  We removed a protection without understanding what it
was protecting against.

**Layer 2 — Propagation race that the original gate was protecting
against.**

Block-propagation outpaces broadcast-propagation.  Two independent
gossip pipelines exist for masternode-related data:

1. **Block relay** -- inv-driven, high priority, deduplicated.  A new
   block reaches the network in seconds.
2. **Masternode registration relay** -- `dseep`/`dsee`/`mnb`-driven,
   lower priority, gossipped to a random subset of connected peers
   per relay hop.  A new masternode's registration reaches the
   network in waves, typically tens of seconds to minutes.

When a new mn is registered and immediately wins its first payment
(quite common because consensus-selection draws from the eligible
set on every block), the block paying it can systematically arrive
at distant peers before the registration that would validate it.

This is the same propagation-race that motivated the original
INV/getheaders/getblocks staggering in Bitcoin.  v2.0.0.8 did not
inherit a corresponding stagger for the masternode gossip, and the
strict payee check raised the consequence from "drop the block" to
"ban the peer."

**Combined effect.**  Without the v2.0.0.6 fMnAdvRelay gate AND
without any propagation-race mitigation, the strict check fires at
every honest peer that's one gossip hop behind on mn-list state.
On a small testnet cluster with NAT hairpinning, this amplified
into a network-partition-class outage (5 of 8 nodes mutually-banned
the LAN gateway IP for ~1 hour).

### D.1quater.3 The fix shipping in v2.0.0.8 — CW12 activation gate

A single targeted change: the strict weak mn-list check in
`CheckBlock` is gated on the voted-consensus activation height.

At both weak-check sites (PoS ~line 1448 and PoW ~line 1657 in the
v2.0.0.8 tree), the existing `if (nMasterNodeChecksEngageTime != 0)`
guard is widened to include the activation-height check:

```cpp
const int nWeakCheckActivationHeight =
    GetEffectiveVotedConsensusActivationHeight();

if (nMasterNodeChecksEngageTime != 0 &&
    pindex->nHeight >= nWeakCheckActivationHeight)
{
    LogPrintf("CheckBlock() : PoS Recipient masternode address "
              "validity could not be verified -- rejecting\n");
    fBlockHasPayments = false;
}
```

`GetEffectiveVotedConsensusActivationHeight()` is the canonical
"are we post-activation?" check used elsewhere in v2.0.0.8 by
`GetEnforcedPayee` (the strong voted-consensus check, which is
correctly self-gated already).  Returns:

- `INT_MAX` on mainnet by default (no SPORK_15 override) → weak
  check never fires → matches v2.0.0.6 effective mainnet behaviour
  byte-for-byte
- `2000` on testnet by default → weak check fires from height 2000
  onwards, the same height voted-consensus activates
- A spork-set value if `SPORK_15_VOTED_CONSENSUS_ACTIVATION` is
  non-zero and below floor → weak and voted-consensus checks
  activate together

### D.1quater.4 Why this height (not VERION_2_0_1_0)

v2.0.0.8 has three different "activation heights" for different
features:

| Constant | Mainnet | Testnet | Gates what |
|---|---|---|---|
| `VERION_2_0_1_0_*_UPDATE_BLOCK` | 1,400,000 | 100 | Devops address rotation (CW9) |
| `VOTED_CONSENSUS_ACTIVATION_FLOOR_*_VAL` | INT_MAX | 2000 | Voted-consensus enforcement |
| `SPORK_15_VOTED_CONSENSUS_ACTIVATION` | 0 (no override) | 0 (no override) | Spork override that LOWERS the floor |

The devops rotation (CW9) and voted-consensus enforcement are two
separate features.  The strict mn-list weak check is logically part
of voted-consensus enforcement -- the question it asks ("is this
payee a registered masternode?") is a strict subset of the
voted-consensus check ("is this payee the SPECIFIC voted-consensus
payee?").  Post-voted-consensus-activation, the weak check is
essentially a redundant fast-path; pre-activation, it asks a
question whose answer doesn't constrain consensus.

Gating CW12 on `GetEffectiveVotedConsensusActivationHeight()`
therefore:

- Aligns the weak check's activation with the feature it logically
  belongs to (voted-consensus enforcement)
- Respects the SPORK_15 spork override (same trigger for both gates)
- Restores v2.0.0.6 effective mainnet behaviour pre-spork (since
  the mainnet floor defaults to INT_MAX, the check never fires
  without an operator-set spork)
- Engages strict enforcement at exactly the height voted-consensus
  becomes the canonical mn-payment selector

### D.1quater.5 Scope and risk

| Item | Detail |
|---|---|
| Files touched | 1 (`cblock.cpp`) |
| Net code change | ~12 lines effective (2 sites × `const int` decl + widened `if` predicate), plus ~50 lines of inline comments explaining the history and rationale |
| New variable | 1 local `const int nWeakCheckActivationHeight` per site |
| New function | None |
| New include | None |
| Header changes | None |
| Consensus impact | **None for the SAME network state.**  CW12 restores v2.0.0.6-effective behaviour pre-spork; post-spork it engages the strict check at the same height voted-consensus enforcement engages anyway (so the union of validation rules is unchanged).  A node with CW12 and a node without CW12 will agree on every block at every height in either regime. |
| P2P impact | None |
| Wallet.dat impact | None |
| Activation height | Mainnet floor = INT_MAX (effectively disabled until sporked).  Testnet floor = 2000. |
| Risk | Very low -- behavioural restoration to v2.0.0.6-effective pre-spork; the strict-check engagement is moved from "always after warmup" to "after warmup AND at/after activation" |

### D.1quater.6 What CW12 does NOT include

Two additional mitigations were designed and implemented during
the CW12 cycle but are **NOT shipping** in v2.0.0.8:

1. **CW11 -- tiered DoS scoring.** Would lower the DoS score
   from 100 to 10 for soft-failure rejections (mn-list miss,
   voted-consensus mismatch) while keeping DoS(100) for hard
   failures (wrong amount, wrong devops).
2. **PB-MN-FETCH Lite -- active broadcast fetch.** Would send a
   `dseg` request to the relaying peer on unknown-payee
   rejection, populating our mn list within one network round-
   trip so subsequent blocks paying the same mn validate cleanly.

These address a hypothetical post-activation propagation race
(vote ledger or mn-list state divergence between honest peers).
The race has not been empirically observed in any test run:

- The previous SPORK_15 rehearsal was masked entirely by
  `fMnAdvRelay=false` on the fleet (enforcement never fired)
- The current testnet is pre-activation (strict checks dormant)
- Mainnet history has never had strict enforcement engaged
  (`fMnAdvRelay` was false network-wide for years)

CW12 alone fully addresses the observed bug (pre-activation
firing).  CW11 + PB-MN-FETCH Lite remain coded, verified,
packaged in `v208-CW11-PBMNFETCH-DEFERRED-bundle.zip`, and ready
to ship as a hotfix if the race manifests at mainnet activation
or in extended post-activation testnet soak.  Full description in
`DEFERRED-CW11-PBMNFETCH-notes.md`.

### D.1quater.7 What this validates

CW12 closes the observed bug on the testnet (block 206 ban
storm) by restoring v2.0.0.6 effective behaviour pre-spork.  It
also makes explicit what Spec C D2 already intended ("strict
check engages under identical conditions" to voted-consensus
enforcement) -- the implementation missed the explicit
activation-height check at the weak site, assuming the upstream
warmup gate was sufficient.  CW12 corrects that oversight.

For mainnet, this means:

- v2.0.0.8 nodes running with default config (no
  `SPORK_15_VOTED_CONSENSUS_ACTIVATION` override) behave
  identically to v2.0.0.6 mainnet for masternode-payee
  enforcement.  No new rejections, no new bans.
- A future mainnet `SPORK_15` to enable voted-consensus
  enforcement activates BOTH the weak and strong checks at the
  same height, in lockstep.  This is the design intent.

For testnet, this means:

- The current testnet (~height 214, floor 2000) will not fire
  the weak check until height 2000.  The block 206 scenario will
  not recur at current heights.
- When the testnet reaches 2000 (or a future SPORK_15 lowers
  the floor), strict enforcement activates cleanly.  At that
  point, observe whether the deferred propagation-race scenario
  manifests -- if it does, deploy `v208-CW11-PBMNFETCH-DEFERRED-bundle.zip`.

## D.1quinquies Debug logging operator-experience fix (CW14)

### D.1quinquies.1 The trap

The `-debug=<category>` flag in v2.0.0.6 / pre-CW14 v2.0.0.8
supported 21 categories (listed in `--help` output). The wildcard
form was the bare `-debug` (no value), which puts an empty string
"" into the categories set; `LogAcceptCategory` treated "" as the
"log everything" marker.

Two intuitive forms operators reasonably tried but which silently
did nothing:

- `-debug=1` — looks like "enable debug level 1" but the codebase
  treated `1` as a category name. No `LogPrint("1", ...)` calls
  exist, so the daemon ran with `fDebug=true` but no extra log
  output appeared.
- `-debug=all` — same trap, more egregious because the operator
  explicitly asked for "all categories."

This was discovered during 2026-06-08 stress-test prep when an
operator using `-debug=1` couldn't see expected `checkblock`
category output, then asked why.  Source inspection confirmed the
trap.

### D.1quinquies.2 The fix

Single change in `LogAcceptCategory` (`util.cpp:397-401`):

```cpp
// before
if (setCategories.count(std::string("")) == 0 &&
    setCategories.count(std::string(category)) == 0)
{
    return false;
}

// after (CW14)
if (setCategories.count(std::string("")) == 0 &&
    setCategories.count(std::string("all")) == 0 &&
    setCategories.count(std::string("1")) == 0 &&
    setCategories.count(std::string(category)) == 0)
{
    return false;
}
```

Plus a help-text update in `init.cpp:307-312` documenting the
alias forms and the disable forms (`-debug=0`, `-nodebug`).

### D.1quinquies.3 Effect

| Invocation | Pre-CW14 | Post-CW14 |
|---|---|---|
| `-debug` | Wildcard (all categories) | Wildcard (unchanged) |
| `-debug=all` | Nothing extra logged (trap) | **Wildcard** |
| `-debug=1` | Nothing extra logged (trap) | **Wildcard** |
| `-debug=masternode` | Masternode category only | Masternode category only (unchanged) |
| `-debug=0` | Off | Off (unchanged) |
| `-nodebug` | Off | Off (unchanged) |

No consensus impact. Pure logging-layer behaviour change.

### D.1quinquies.4 Scope and risk

| Item | Detail |
|---|---|
| Files touched | 2 (`util.cpp`, `init.cpp`) |
| Net code change | 2 lines in util.cpp (added two `count` checks), 2 lines in init.cpp (added two help-text strings) |
| New variables / functions / parameters | None |
| Header changes | None |
| Consensus impact | **None.** Logging layer only. |
| P2P impact | None |
| Wallet.dat impact | None |
| Activation height | None — engages on upgrade |
| Risk | Zero — pure additive |

### D.1quinquies.5 Companion documentation

Created `debug-logging-guide.md` for the operator wiki: full
category reference with descriptions of what each category logs
and which file it lives in, common diagnostic-scenario recipes
("why isn't my staker working?" etc.), log file size estimates per
category, log rotation guidance for Linux and Windows.

This addresses the broader "operators don't know what's possible"
gap that the CW14 alias trap exposed.

## D.2 Startup block-verification rollback

After the enforcement-gate removal (A.1.7) made the weak
masternode-payee check enforce, a node restarting could roll its
chain back hundreds of blocks because the verify pass re-ran the
check against an empty runtime masternode list.

Two-iteration fix history is informative:

**First attempt (pre-activation):** gated on
`pindex->pnext == NULL` as a "this is the tip" proxy. This worked
pre-activation but was wrong: `pindex->pnext == NULL` is true for
BOTH a live new tip (must run the payee check) AND the stored tip
during the verify pass (must skip) — one bit cannot separate them, so
the tip's check ran against an empty masternode list and rejected.
Discovered post-activation on two nodes.

**Resolved fix:** restored the genuine 2.0.0.6 / 2.0.0.7 guard
`hashPrevBlock == hashBestChain` ("this block extends the current
tip"), which DOES separate the two states (live new block extends the
still-current tip → runs; no stored block, not even the tip, extends
it during verify → skips). Two expression-only edits (PoS + PoW); the
enforcement block is nested inside the same guard so both the weak
check and the voted-consensus enforcement get the corrected behaviour.

Reorg connects also skip (fall through to legacy — safe; those blocks
were strict-checked on first receipt).

Accepted, documented behaviour: enforcement has a defined blind window
during initial block download and post-stall recovery (tip more than
8h old). This is correct soft-fork behaviour — enforcement is a
current-tip activity.

## D.3 Velocity miner timing (already covered in B.3)

Cross-reference: B.3 covers the staker mint-retry storm caused by
`Velocity()`'s 45-second minimum block spacing being applied without
back-off. Touches the staker side of the network protocol.

---

# E. TESTNET

## E.1 Testnet — established and operational

**New in v2.0.0.8.** A working testnet is a deliverable of this cycle
in its own right. The release's central feature (voted consensus,
A.1) genuinely cannot be validated without a live multi-node network,
and bringing that network up was substantial work — not a footnote to
the code changes.

### E.1.1 Why a clean rebuild was necessary

An initial testnet attempt became unrecoverable during development.
It had been run across several iterations of buggy pre-fix binaries
and accumulated: a forked PoS staker, mutually-banned nodes, and —
critically — chain history that could not be resynced past a stalled
block because of the VRX non-determinism bug (D.1.3). It was no longer a
sound substrate for testing consensus.

Rather than nurse a corrupted chain, the testnet was rebuilt from
genesis on a binary containing the D.1.3 VRX fix plus the
voted-consensus work. This is a *genesis-clean restart*, not a new
genesis: the testnet genesis block is hardcoded in
`ctestnetparams.cpp` and unchanged (`nTime` 1547848830, `nNonce`
16793, hash
`0x000510a669c8d36db04317fa98f7bf183d18c96cef5a4a94a6784a2c47f92e6c`,
asserted at startup). Every node's chain data was wiped; the daemon
re-creates block 0 from the hardcoded definition and the chain is
mined fresh from block 1 by the fixed binary.

### E.1.2 Testnet parameters

From `ctestnetparams.cpp` and related headers:

- P2P port `28092`, RPC port `28094`.
- Network selected with the `-testnet` switch only.
- Genesis: 18 Jan 2019 (`timeTestNetGenesis`), coinbase output is
  empty (`SetEmpty()`) — there is **no premine baked into the
  testnet genesis**; a fresh testnet is funded entirely by mining
  from block 1.
- Masternode payments and devops payments are active from block 1
  (`START_MASTERNODE_PAYMENTS_TESTNET` /
  `START_DEVOPS_PAYMENTS_TESTNET` = 1546300800).
- **Voted-consensus activation floor: 2000** (`cblock.cpp`
  `VOTED_CONSENSUS_ACTIVATION_FLOOR_TESTNET_VAL`). Voted consensus
  is dormant (legacy `GetBlockPayee` authoritative) for blocks
  1–1999, then `GetEnforcedPayee` begins consulting
  `GetCanonicalWinnerFromQueues` at height 2000.

### E.1.3 Topology

6 masternodes + 1 PoS staker. The masternode collateral, keys, and
`masternode.conf` entries are specific to the rebuilt chain. The
spork-key and devops-address wallets are preserved across any rebuild
— those keypairs are pinned to values hardcoded in the source.

### E.1.4 Public testnet seed

The domain `testnet.xdn-explorer.com` is added to the testnet
`vSeeds` and to the generated default testnet conf (`addnode`), with
IPv4 and IPv6 fallbacks.

## E.2 SPORK_14 / SPORK_15 activation rehearsal and crossing

The voted-consensus activation height is controlled by
`GetEffectiveVotedConsensusActivationHeight()`: a per-network floor
(testnet 2000) with an optional `SPORK_15` override that can only
*lower* the activation height, never raise it above the floor, and
cannot un-activate a height already passed. `SPORK_15` carries a
block HEIGHT as its value, not a timestamp.

Staged rehearsal and then a full controlled crossing were run on the
testnet:

- `SPORK_14` broadcast and propagated fleet-wide; signature
  verification and propagation confirmed on every node including
  the remote VPS daemons.
- `SPORK_15` was used to bring activation forward from the 2000
  floor for a controlled crossing.
- The crossing was executed at height **1837** with the whole fleet
  on a uniform binary.

**Result — the crossing SUCCEEDED.** Block 1837 (PoW) and block 1838
(PoS) both logged "masternode payee matches voted consensus", both
with 7/7 voter consensus, both accepted. The PoS and PoW
strict-enforcement sites are separate code paths; both confirmed live.
Devops payout was correct on both block types. No rejections, no
devops-fallback NOTICE, no stall. The clamp-free payee selector
rotated across the activation boundary without producing a
catastrophic streak.

---

# F. GUI

## F.1 Menu organisation and theme

- **New Tools menu** — maintenance and system actions consolidated:
  Show Backups; Check Wallet, Repair Wallet, Compact Wallet; Locked
  Outputs; Debug Window, Open Data Directory, Edit Config File, Edit
  Config Ext File. The last four moved from Help (which now holds
  only About / About Qt). Settings holds security state and
  preferences only.
- **Dark theme** — Qt stylesheet applied to all top-level widgets at
  startup via `bitcoin.cpp`. Toggle persists across sessions via the
  existing options-model preference.
- **In-app DigitalNote.conf editor** — new menu entry; safe writes,
  warns about pending restart requirements.

## F.2 Splash and startup

- **Splash** is `480×462px` with `Qt::WA_TranslucentBackground`
  (light theme) or opaque (maintenance mode).
- **Maintenance Mode splash** triggers automatically on
  `-rebuildwallet`, `-rescan`, `-reindex`,
  `-iknowsalvagewalletisdangerous`, or `-maintenancemode`. Uses a
  separate baked-in PNG (`splash_maintenance.png`). Provides a
  chromed window with title bar, minimise button, close button, and
  taskbar entry. Initial implementation used runtime QPainter to
  draw the maintenance text on top of the standard splash but was
  abandoned after the painted text exhibited progressive bolding
  caused by Qt repaint behaviour; baked-PNG replacement eliminated
  the issue.
- **`splashref`** is now nulled before `splash.finish()` to handle
  late `InitMessage` calls that would otherwise paint onto a splash
  about to close.
- **Rescan progress** — splash now shows `Rescanning... block N / M`
  updating every 5000 blocks, including the post-rebuild rescan.
- **New 16/32/48/64/128/256 PNG icons** and new multi-resolution
  `DigitalNote.ico` (with 24×24 entry for Windows 11 tray).

## F.3 Masternode collateral UX

Six changes that together turn the 2M XDN collateral UTXO into a
first-class concept the user can manage from the GUI:

- **B1: 2M XDN incoming-collateral popup**
  (`bitcoingui.cpp:1015-1079`).
- **B2: Lock/Unlock context menu** on the user's own masternodes
  table (`masternodemanager.{h,cpp,ui}`).
- **B3: Masternode list selection mode** (ExtendedSelection).
- **B4: Lock column in My Master Nodes table** —
  closed-padlock icon and "Locked"/"Unlocked" text per row.
- **B5: Lock column spacing.**
- **B6: Locked Outputs dialog** — modal dialog opened from
  `Tools → Locked Outputs...`. Six columns: Lock indicator /
  Address / Label / Amount / Type / TXID:vout. Three-tier
  classification with per-tier confirmation messages on unlock.

## F.4 Worker thread infrastructure

Worker-thread classes added under `src/qt/` to keep the GUI
responsive during slow wallet operations. Three are actively
wired and in use; two are present in the source tree but not yet
wired to GUI consumers (see H.7).

**Wired and in use:**

- `watchonlyworker` — drives `RemoveWatchOnly` with progress
  callback. Consumed by `removewatchonlydialog.cpp`. Required
  because the three-phase prune (C.3.3) can take many seconds on
  wallets with many watch-only-related historical transactions.
- `masternodeworker` — drives masternode start / stop / status
  operations. Consumed by `masternodemanager.cpp`. Required
  because masternode RPC paths can block on network / chain state.
- `decryptworker` — drives the recovery-phrase upgrade flow
  (re-encryption of an existing wallet under the hex-derived key
  so the BIP39 mnemonic unlocks it). Consumed by
  `seedphrasedialog.cpp` at lines 339-353. Distinct from the
  `CWallet::DecryptWallet` method (C.7), which is not called.

**Present in source but not yet wired:**

- `coincontrolworker` — intended to background Coin Control's
  long-running select-coins operations. No GUI consumer
  instantiates it; Coin Control operations currently run on the
  GUI thread (which is acceptable in practice — the operations
  are fast enough on typical wallets that the responsiveness gap
  is small).
- `sendcoinsworker` — intended to background the `sendcoins`
  validation / signing path. No GUI consumer instantiates it;
  `sendcoinsdialog.cpp` performs the work on the GUI thread.

Both unwired classes compile cleanly and their header / source
files are in the build manifest, but they are effectively
dormant code in v2.0.0.8. Listed in H.7 as a cleanup item — the
correct disposition (wire them, or remove them) is deferred.

## F.5 Recovery phrase — old wallet upgrade flow

For wallets encrypted with v2.0.0.6 or earlier, the recovery-phrase
upgrade is a one-time process invoked via `Settings → Recovery Phrase`
(or automatically on first unlock when the upgrade dialog determines
it's eligible).

- `recoveryphraseupgradedialog.{cpp,h}` — explanatory dialog with
  three options: proceed, decline (with confirmation), or postpone.
- `decryptworker.{cpp,h}` — background-thread re-encryption: tries
  hex-derived key first (new wallets), falls back to raw password
  (old wallets), then re-encrypts with hex-derived key so the
  recovery phrase works going forward.
- The wallet stays encrypted throughout the process; there is no
  plaintext window.

## F.6 `guistate` namespace — per-wallet GUI preferences

New `src/qt/guistate.{cpp,h}` centralises QSettings-backed per-wallet
flags. Two flags currently:

- `isRecoveryPhraseUpgradeDeclined` / `setRecoveryPhraseUpgradeDeclined`
- `is2MCollateralPromptSuppressed` / `set2MCollateralPromptSuppressed`

Keys are derived from the absolute wallet path (hashed) so multiple
wallets in different datadirs don't share state.

## F.7 Transaction display

- **`transactiondesc.cpp`** — Transaction-detail "Transaction ID"
  suffix removed (was always `-000` because derived from a
  display-row sort ordinal, not a real index). The detail line now
  shows the clean transaction hash.
- **Double-click in transactions tab** now opens the detail dialog.
  Previously broken via slot/signal mismatch
  (`transactionview.cpp:195`); fixed by changing `SLOT` to `SIGNAL`
  for `doubleClicked(QModelIndex)`.

## F.8 Misc visual polish

- **Balance label minimum widths** — Available, Stake, Pending,
  Total, watch-only column all now have minimum widths so they don't
  clip the trailing "N" in "XDN".
- **Copyright year in About dialog** — updated to 2018-2026.
- **Coin Control lock icon** — was white-on-transparent (designed
  for the dark-coloured status bar); now black-on-transparent for
  the default theme.
- **MAINNET indicator** in the status bar with tooltip.
- **Password generator** — 20-character cryptographically random in
  `askpassphrasedialog.cpp`.
- **"Forgot password?"** link wired to the recovery-phrase unlock
  flow.
- **"For staking only" checkbox** hidden by default; only shown when
  a staking unlock is the explicit context.

## F.9 Debug console parser fix

`parseCommandLine` (`src/qt/rpcconsole.cpp`). Backslashes typed
outside any quotes were being silently consumed (treated as
bash-style escape characters with no preserved character), so a
Windows path typed without quotes like `c:\temp\test.dmp` was being
passed to the RPC as `c:tempest.dmp`.

Symptom on `dumpwallet`: dump file silently created at the wrong
path. The user thought the file went to `c:\temp\test.dmp` but it
actually went to `<exe>/temptest.dmp` containing private keys, with
no error or warning surfaced.

Fix: `STATE_ESCAPE_OUTER` now mirrors `STATE_ESCAPE_DOUBLEQUOTED`'s
smarter logic. Backslash is consumed only when followed by another
backslash or whitespace; every other `\X` sequence preserves the
backslash literally, so Windows paths typed without quotes now work
as users expect.

## F.10 Qt signal/slot warnings cleared

Five distinct slot/signal mismatches that produced runtime warnings
at startup (all gone after the fix pass):

- `SendCoinsEntry::payAmountChanged` slot/signal mismatch
- `OptionsModel::transactionFeeChanged(CAmount)` signature mismatch
- `TransactionView::doubleClicked` slot/signal mismatch
- `DigitalNoteAmountField::textChanged` slot/signal mismatch
- Splash `showProgress` connect failure (was connecting `walletModel`
  inside `setClientModel` where `walletModel` was null)

---

# G. BUILD & RELEASE

## G.1 Version stamping

The reported version string was `v1.0.3.5-488-g<hash>-dirty` — an old
git tag, not the codebase version. `version.cpp` included a `build.h`
(generated by `share/genbuild.{bat,sh}`) which defined `BUILD_DESC`
from `git describe`. `git describe` resolves to the most recent
*reachable* tag, and this repository's most recent tag is an ancient
`v1.0.3.5` — so a v2.0.0.x codebase reported itself as v1.0.3.5. The
version string depended on git tag state, which is only verifiable
*after* a build.

The version is now controlled entirely by source files. The
`HAVE_BUILD_INFO` / `#include "../build/build.h"` block in
`version.cpp` is commented out (not deleted — a note explains how to
reinstate git-derived build info later if wanted). With `BUILD_DESC`
no longer supplied by `build.h`, `version.cpp`'s existing
`#ifndef BUILD_DESC` fallback builds the string from
`clientversion.h`'s `CLIENT_VERSION_*` defines:

```
v2.0.0.8-XDN-DigitalNote-Core
```

`FormatFullVersion()` returns this single string, so `getinfo`'s
`version`, the daemon `-version` banner, the RPC User-Agent, the peer
`version` message, and the Qt About box all report it consistently.

**Build date.** With `build.h` disabled, `BUILD_DATE` falls back to
the compiler's `__DATE__` / `__TIME__` — the actual build timestamp.
This is fresh as long as `version.cpp` is recompiled; proper per-build
version bumping in `clientversion.h` forces that recompilation, and
the build script also `touch`es `version.cpp` every build as
belt-and-braces.

The `genbuild.{bat,sh}` scripts and the `USE_BUILD_INFO=1` build flag
are no longer used.

Touched files: `version.cpp` (source), `DigitalNote_config.pri`
(build-config).

## G.2 Version numbers

- `clientversion.h`: `CLIENT_VERSION_BUILD` 6 → 8 (skipping 7 since
  v2.0.0.7 never shipped).
- `DigitalNote_config.pri`: `DIGITALNOTE_VERSION_BUILD` aligned with
  `clientversion.h`.
- `CLIENT_VERSION_IS_RELEASE` flipped to `true` at the tagged-build
  release-checklist step.
- `PROTOCOL_VERSION` bumped; voted-consensus `mnvotequeue` /
  `getmnqueues` messages are new but are silently ignored by
  v2.0.0.6 peers.

## G.3 Build infrastructure

- BIP39 library merged inline (`src/bip39/`); previously a git
  submodule. CI workflows updated to `submodules: false`.
- **`USE_BIP39` build flag retired.** The opt-out path was already
  non-functional in the source tree because BIP39 calls were woven
  into `cwallet.cpp`, `qt/askpassphrasedialog.cpp`,
  `qt/walletmodel.{cpp,h}`, and `qt/seedphrasedialog.{cpp,h}`
  without `#ifdef` guards. BIP39 is now load-bearing for the wallet
  UX and not optional.
- New build infrastructure: `include/libs/bip39.pri`,
  `include/libs/gmp.pri` (GMP added for BIP39 arithmetic).
- `DigitalNote_config.pri` Linux block uncommented and activated
  (was entirely commented out in v2.0.0.6, causing all header
  lookups to fail on Linux).
- Linux `MINIUPNPC_API_VERSION=18` defined explicitly via
  `DEFINES +=` in `DigitalNote_config.pri`. The Makefile.mingw used
  to compile miniupnpc 2.2.8 doesn't define the API version macro
  correctly in installed headers on Linux.
- `net.cpp` — explicit `#if MINIUPNPC_API_VERSION >= 18` branching
  for the `UPNP_GetValidIGD` signature change in API v18.
- BIP39 `#include` paths changed from `"bip39/xxx.h"` (quoted) to
  `<bip39/xxx.h>` (angle brackets) to use the `INCLUDEPATH` from
  `bip39.pri` rather than the source-relative search.
- `cdb.cpp` — explicit template instantiations for
  `CDB::Erase<std::string>` and
  `CDB::Erase<std::pair<std::string,uint>>`. Without these the
  linker can't resolve `EraseRecoveryPhraseFlag` and
  `EraseMasterKey`.

## G.4 CI / GitHub Actions

GitHub Actions workflows: Linux x64, Linux aarch64, macOS Intel +
Apple Silicon, Windows MSYS2. Tag-driven release workflow auto-builds
all platforms and publishes a GitHub Release with binaries and
SHA256SUMS.

**Release artifact naming convention changed.** Previous convention:
`DigitalNote-qt.<os>.<arch>` and `DigitalNoted.<os>.<arch>` (with no
version, no separator consistency). New convention:
`<name>-<version>-<os>-<arch>.<ext>`. Examples:
`DigitalNote-qt-2.0.0.8-win-x64.exe`,
`DigitalNote-qt-2.0.0.8-linux-arm64`,
`DigitalNoted-2.0.0.8-macos-x64.dmg`. OS values: `win`, `linux`,
`macos`. Arch values: `x64`, `arm64`.

- **32-bit builds dropped** (previously `linux.i386` and `win32`).
- **macOS Apple Silicon native build added** (previously Intel-only).

## G.5 `linux-x64-compat` variant

For older Linux distributions. Standard `linux-x64` builds against
Ubuntu 24.04+ (glibc 2.39+, libstdc++ 14+) and fails on systems with
older C runtimes with `GLIBC_2.x not found` or
`GLIBCXX_3.4.x not found` errors. The compat variant builds against
an older glibc baseline (2.31+, covering Ubuntu 20.04, Debian 11,
RHEL 9) and statically links libstdc++ so the binary doesn't depend
on the host system's libstdc++ at all. Same source tree, same `.pro`
files; the difference is purely in the CI runner image and the LIBS
line including `-static-libstdc++ -static-libgcc`. Both
`DigitalNote-qt` and `DigitalNoted` ship in the compat flavour.

---

# H. APPENDICES

## H.1 Tests added

`src/test/`: amount, hash, script, spork, transaction, util, version
(7 new files).
`src/qt/test/`: walletmodel.
`test/bip39/`: test_bip39_wallet (top-level, BIP39 vector tests).
`test/integration/`: test_entropy_boundaries, test_mnemonic_roundtrip,
test_seed_vectors.
`test/qt/`: test_seedphrasedialog.

## H.2 New constants

- `VOTER_ELIGIBILITY_DEPTH = 17` (masternode.h)
- `VOTE_LOOKAHEAD = 10` (masternode.h)
- `VOTE_PAST_HORIZON` (masternode.h)
- `REORG_DEPTH_BUFFER` (masternode.h)
- `VOTE_COMMIT_BUFFER = 3` (masternode.h)
- `MIN_ENABLED_FOR_CONSENSUS = 5` (masternode.h)
- `VOTE_QUEUE_LENGTH = 10` (masternode.h, equals VOTE_LOOKAHEAD)
- `MASTERNODES_DSEG_RETRY_SECONDS` (masternodeman.h)
- `BLOCK_SPACING_MIN = 45` (mining.h)

## H.3 File inventory — substantially modified

`cwallet.{cpp,h}`, `cwallettx.{cpp,h}`, `cmasternodevotetracker.{cpp,h}`,
`cmasternodevotequeue.{cpp,h}`, `cmasternode.{cpp,h}`,
`cmasternodeman.{cpp,h}`, `cactivemasternode.{cpp,h}`, `cblock.cpp`,
`main.cpp`, `miner.cpp`, `blockparams.{cpp,h}`, `script.cpp`,
`rpcmining.cpp`, `rpcdump.cpp`, `crpctable.cpp`, `cdb.cpp`,
`cbasickeystore.{cpp,h}`, `ccryptokeystore.{cpp,h}`,
`cdatastream.cpp`, `serialize/{base,read,write}.cpp`,
`net.{cpp,h}`, `version.{cpp,h}`, `init.{cpp,h}`,
`walletdb.{cpp,h}`, `walletrebuild.{cpp,h}`, `bitcoin.cpp`,
`bitcoingui.{cpp,h}`, `walletmodel.{cpp,h}`, `optionsmodel.{cpp,h}`,
`sendcoinsdialog.cpp`, `sendcoinsentry.{cpp,h}`, `rpcconsole.cpp`,
`coincontroldialog.cpp`, `transactiontablemodel.cpp`,
`transactiondesc.cpp`, `askpassphrasedialog.cpp`,
`masternodemanager.{cpp,h,ui}`, `bitcoin.qrc`,
`fork.{cpp,h}` (rewritten for CW9 height-based ladder),
`include/app/sources.pri`, `include/app/headers.pri`,
`include/app/forums.pri`, `include/libs/bip39.pri`,
`include/libs/gmp.pri`, `include/daemon/{sources,headers}.pri`.

## H.4 Files deleted

- `cmasternodevote.{cpp,h}` (legacy per-height vote class, removed
  by A.1.13)

## H.5 Files added

```
src/bip39/                                              (entire subdirectory inlined from former submodule)
src/rpcbip39.cpp                                        (BIP39 RPCs)
src/walletrebuild.{h,cpp}                               (dump primitive)
src/cmasternodevotequeue.{cpp,h}                        (M1Q queue class)
src/cmnqueuesnapshot.h                                  (M1Q payment snapshot)
src/qt/coincontrolworker.{h,cpp}
src/qt/decryptworker.{h,cpp}
src/qt/guistate.{h,cpp}
src/qt/lockedoutputsdialog.{h,cpp}
src/qt/forms/lockedoutputsdialog.ui
src/qt/masternodeworker.{h,cpp}
src/qt/recoveryphraseupgradedialog.{h,cpp}
src/qt/removewatchonlydialog.{h,cpp}
src/qt/rotatephrasedialog.{h,cpp}
src/qt/seedphrasedialog.{h,cpp}
src/qt/sendcoinsworker.{h,cpp}
src/qt/watchonlyworker.{h,cpp}
src/qt/res/icons/lock_closed_solid.png
src/qt/res/icons/lock_open_solid.png
src/qt/res/images/splash_maintenance.png
```

## H.6 Verified — not bugs

Items investigated during the cycle and confirmed *not* to be defects;
recorded so they are not re-investigated:

- **`GetEnforcedPayee` permissive `payee == CScript()` pass**
  (`cblock.cpp` CheckBlock). When `GetEnforcedPayee` returns false or
  an empty payee, `CheckBlock` accepts the block's payee as-is. This
  is the intended permissive soft-fork fallback — it prevents a
  consensus gap from stalling the chain.

- **Masternode / staker reward swap at height
  `VERION_1_0_4_2_MANDATORY_UPDATE_BLOCK` (403117).** The block
  subsidy split changed at this height to increase the masternode
  share relative to the staker share. Pre-swap layout (`nHeight <
  403117`): staker 150, masternode 100, devops 50. Post-swap layout
  (`nHeight >= 403117`): staker 100, masternode 150, devops 50.
  Total block subsidy unchanged at `nBlockStandardReward` (300 XDN).
  Devops payment is consistently 50 XDN throughout chain history
  with one exception: the fork block itself (403117) carries a
  one-shot 1,000,000,000 XDN payment via the `nHeight ==
  VERION_1_0_4_2_MANDATORY_UPDATE_BLOCK` branch in
  `GetDevOpsPayment` — a chain correction event from the v1.0.4.2
  hardfork. All height-gated cleanly in `GetMasternodePayment` and
  `GetDevOpsPayment` in `blockparams.cpp`. Testnet operates under
  pre-swap rules (post-genesis-restart, height <<< 403117) until
  testnet reaches the same height years from now.

- **Devops-receives-on-no-masternode-found is the consensus
  fallback.** When the masternode payment selector returns no
  payee (typically because the producing node's local masternode
  list is stale or empty — e.g. after a long stall, after restart,
  during transient peer churn), the masternode share is paid to the
  devops address in addition to the standard devops payment. This
  is required for block validity to be independent of the producing
  node's local masternode list state, and is a long-standing
  consensus rule preserved unchanged in v2.0.0.8. The voted-payee
  consensus (A.1), once activated, prevents the trigger condition
  (stale local list) from determining the payee at all — the
  canonical payee comes from queue tally, not from each node's
  local lookup — but the no-payee-found fallback remains as the
  ultimate safety net.

- **Block 138092 unusual payout layout.** Confirmed consensus-valid.
  See D.1.4 narrative for the full breakdown. Three legitimate
  mechanisms (pre-swap reward layout, devops fallback firing during
  82-minute stall, broken v2.0.0.6 VRX curve) intersect in one
  block; nothing about the block needs correcting.

## H.7 Cleanup items — deliberately deferred

- **`CountEnabled` mutates while counting.** `CMasternodeMan::CountEnabled`
  calls `mn.Check()` on each masternode while iterating. The
  `Check()` side effect is relied on (consciously or not) by several
  live callers; splitting count from refresh is a multi-subsystem
  behavioural change with no observed symptom. Left as-is. Note: the
  function voted consensus actually uses for its denominator
  (`CountVotingEligible`) is already clean and does not call
  `Check()`.

- **VRX module-global thread-safety.** `blockparams.cpp` uses
  module-level globals (`fDryRun`, `bnOld`, `bnNew`, etc.) across
  the retarget call chain. This is a code-hygiene concern, *not* the
  D.1.3 resync determinism bug (which was purely the
  `GetAdjustedTime()` read).

- **`pindexBest` vs `pindexLast` retarget hygiene (PB-FORK).** A
  retarget-hygiene cleanup; not a consensus-behaviour change.

- **Unwired GUI worker classes.** `src/qt/coincontrolworker.{cpp,h}`
  and `src/qt/sendcoinsworker.{cpp,h}` are present in the build but
  have no GUI consumers in v2.0.0.8 (see F.4). The header / source
  pairs compile cleanly and add a small amount of dead code to the
  binary. Two valid dispositions for a future release: (a) wire
  them to their intended sites in `coincontroldialog.cpp` and
  `sendcoinsdialog.cpp` respectively, gaining background-thread
  responsiveness for those operations; or (b) remove them from the
  source tree and the build manifest. Left in place for v2.0.0.8
  because removing source files mid-release is more disruptive than
  the cost of carrying dormant code; the right disposition is a
  design choice for v2.0.0.9 or later.

## H.8 Deferred to a future version

- **Checkpoint at v2.0.1.0 rotation + 2000 blocks.** Originally
  planned for v2.0.0.8; deferred because the rotation activation
  block (mainnet 1,400,000 / testnet 100) does not exist yet at
  v2.0.0.8 tag time. Add to `checkpoints.cpp` in v2.0.0.8.1 or
  v2.0.0.9 once block 1,402,000 has been mined post-rotation on
  mainnet (testnet: block 2,100). The entry takes the form
  `(1402000, uint256("0x<hash>"))`. Until that checkpoint ships,
  deep-reorg protection in the rotation window relies on accumulated
  chain work alone.

- **Issue 2 — chain-wide equivocation enforcement.** The
  equivocation marking is per-node-local (see A.1.12). A chain-wide
  enforcement design would have substantial adverse-effect risks
  (fleet-wide false-positive cascade, liveness failure below
  threshold, adversarial trigger amplification, asymmetric slashing,
  recovery friction, operator burden, sybil amplification) if
  shipped without extensive analysis. Scoped only; deferred to
  v2.1+.

- **CW3 — S2 conflicting-dsee diagnostic.** Deferred to v2.0.0.9.

## H.9 Architectural invariants for future Rust port

These are properties of the v2.0.0.8 design that should survive a
future re-implementation. Recorded here so a port doesn't accidentally
regress them.

1. **Locks are user data, not masternode state.** Auto-unlocking on
   stop is wrong (A.2.6).
2. **Per-output, not per-transaction.** Coin selection and lock
   semantics work at the outpoint level; transaction-level filtering
   produces user-visible bugs (C.2.1).
3. **Forward-compatible storage.** New BDB record types added in
   v2.0.0.7/v2.0.0.8 use distinct keys and don't require migration
   of existing records.
4. **Collateral by user choice.** The wallet does not auto-classify
   outputs as "masternode collateral"; the user explicitly opts in
   via the B1 popup, the masternode UI, or `lockunspent`.
5. **No strict version equality on protocolVersion.** Use
   `>= MIN_PEER_PROTO_VERSION` everywhere, not `== PROTOCOL_VERSION`.
6. **Multi-source lock state with union semantics.**
   `setLockedCoins` (in-memory) and `lockedoutput` (BDB) are merged
   at load; locks set by Coin Control, `lockunspent`, and the GUI
   all flow through the same mechanism.
7. **Consensus inputs are pure functions of chain.** Never of
   node-local state. PB-INFLIGHT (A.1.4) is the canonical violation
   to avoid.
8. **Spent-tracking via mmTxSpends, not vfSpent.** The reader-side
   path that auto-handles reorgs is the correct one. vfSpent
   reconciliation via FixSpentCoins is a band-aid for code that
   hasn't been migrated to mmTxSpends yet (CW4 Fix C completes that
   migration).
9. **Writer/reader pairing.** Whenever a code path mutates wallet
   spent-state, the writer must update both vfSpent (for disk
   format) and mmTxSpends (via AddToSpends → AddToWallet). The reader
   should consult only mmTxSpends. CW4 Fix B + Fix C together
   establish this pairing for both send and stake paths.
10. **Enforcement gates must be explicit.** A.1.7 (`fMnAdvRelay`) is
    the canonical violation: consensus enforcement gated behind an
    undocumented config flag named for an unrelated abandoned
    feature. Never again.
11. **Chain-correction operations are canonical history.** Block
    heights involved in controlled fork operations (mandatory-update
    activation clusters, chain-correction rollback anchors, treasury
    operations) are accepted by the validator via height-keyed
    mechanisms — the `nBits` exception list for difficulty-floor
    anchors, exact-height equality checks for treasury injections.
    These are not bugs to be optimised away; any conforming
    implementation must reproduce them exactly to validate canonical
    chain history.
12. **One-shot consensus operations are pinned by exact-height
    equality, never spork-gated.** The treasury injection at
    `VERION_1_0_4_2_MANDATORY_UPDATE_BLOCK` is enforced by
    `GetDevOpsPayment` checking `nHeight == THE_BLOCK` — a single
    block, exactly. This pattern prevents replay (attacker would
    have to win the chain-work race for the historical block,
    computationally infeasible) and prevents accidental retroactive
    application at a different height. Spork-gated activations
    apply to ranges; exact-height equality applies to events.
13. **Devops as no-payee fallback is a consensus invariant.**
    Block validity must not depend on the producing node's local
    masternode list state. When the masternode payment selector
    returns no payee, the masternode share is paid to the devops
    address. This rule has held across both pre- and post-swap
    reward eras, and through every other release. Voted-payee
    consensus (A.1) reduces how often the fallback fires by
    removing dependence on local lookups, but does not remove the
    fallback itself — it remains the ultimate safety net for any
    case the voted path also can't resolve.

---

## Development status summary (v2.0.0.8 sealed)

| Area | Status |
|---|---|
| Voted consensus core (A.1.1–A.1.5) | Implemented, deployed, crossing-confirmed |
| `fMnAdvRelay` gate removal (A.1.7) | Applied; enforcement confirmed at crossing |
| Engagement + vote-payee agreement (A.1.8) | Implemented, deployed |
| Vote propagation relay split (A.1.9) | Implemented, deployed |
| Validation hook (A.1.10) + CheckBlock rework (A.1.11) | Applied |
| Equivocation Issue 1 + Issue 3 (A.1.12) | Applied — 50h soak clean |
| Per-height vote path removal (A.1.13) | Applied; ~500 lines removed, 2 files deleted |
| nLastPaid correctness (A.2.1) | Resolved (all 3 sub-bugs) |
| Local MN collateral selection (A.2.7) | Resolved |
| Peer reliability fixes (A.3) | Applied |
| PoS lock-order ABBA chain (B.1) | Resolved through three iterations |
| CW4 Fix B PoS spent-tracking (B.2) | Applied; 48h parallel-wallet soak validated |
| Velocity back-off (B.3) | Resolved |
| Staking icon CW2/CW6 (B.4) | Applied; CW6 v1.1 with load-crash fix |
| CW4 Fix C reader migration (C.1.2) | Applied; smoke-test passed with 3 independent demonstrations; 48h parallel-wallet soak validated (510 mismatches / 2,978,104.26090000 XDN drift on reference wallet reconciled to zero on Fix-C wallet) |
| Balance correctness, watch-only (C.1, C.3) | Resolved |
| Wallet rebuild (C.4) | Implemented and tested |
| BIP39 (C.6) | Implemented |
| VRX recovery curve broken in v2.0.0.6 (D.1.1) | Diagnosed and superseded |
| VRX wall-clock-based curve engagement (D.1.2) | Applied (post-v2.0.0.6 work) |
| VRX retarget determinism via committed time (D.1.3) | Resolved |
| Historical mainnet `nBits` exception list extension CW8 (D.1.4) | Applied; 30 heights from full-chain scan, sorted-array + `std::binary_search` helper, log line enriched with `[PoS]`/`[PoW]` tag |
| Mining-side same-timestamp guarantee CW7 (D.1.5) | Applied — late `nBits` assignment using committed `pblock->nTime` closes the 0.7%-per-stall residual mismatch risk |
| Devops rotation + strict-check + off-by-one fix CW9 (D.1bis) | Applied — `getDevelopersAdressForHeight()` refactor, all 5 producer callers migrated, strict check re-enabled with height gate; testnet rotation block 100, mainnet rotation block 1,400,000 |
| Startup verify rollback (D.2) | Resolved with correct guard |
| Testnet construction (E.1) | Operational; soak ongoing |
| SPORK_14/15 activation crossing (E.2) | Completed — SUCCEEDED at testnet height 1837 |
| Version stamping (G.1) | Implemented |
| CI / artifact naming (G.4) | Implemented |

---

*v2.0.0.8 sealed. Document committed at the same commit as the source.*
