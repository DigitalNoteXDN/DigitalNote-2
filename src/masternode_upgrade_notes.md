# DigitalNote v2.0.0.7
## Masternode Payment Enforcement — Network Upgrade Notes
*For future developers — read before removing the fallback in v2.0.0.8+*

---

## Background

During the development of v2.0.0.7, two bugs were found in the masternode payment system that existed in all prior versions:

- `getblocktemplate` always returned the same masternode winner because `GetCurrentMasterNode(1)` used the genesis block hash (height 0) in its score calculation, never varying per block.
- The copy constructor of `CMasternode` overwrote `nLastPaid` with `GetAdjustedTime()` on every copy, making all masternodes appear to have the same last-paid time in the UI.

---

## What Was Fixed in v2.0.0.7

### 1. `src/rpcmining.cpp` — getblocktemplate payee selection

The old code:
```cpp
CMasternode* winningNode = mnodeman.GetCurrentMasterNode(1);
// Always returned same node — used genesis block hash (height=0)
```

Was replaced with:
```cpp
CScript mnPayee;
CTxIn mnVin;
if(masternodePayments.GetBlockPayee(pindexPrev->nHeight + 1, mnPayee, mnVin))
{
    CTxDestination address1;
    ExtractDestination(mnPayee, address1);
    CBitcoinAddress address2(address1);
    result.push_back(json_spirit::Pair("masternode_payee", address2.ToString().c_str()));
}
else
{
    // TRANSITION FALLBACK — see section below
    CMasternode* pmn = mnodeman.FindOldestNotInVec(std::vector<CTxIn>(), 0);
    if(pmn)
    {
        CScript fallbackPayee = GetScriptForDestination(pmn->pubkey.GetID());
        CTxDestination address1;
        ExtractDestination(fallbackPayee, address1);
        CBitcoinAddress address2(address1);
        result.push_back(json_spirit::Pair("masternode_payee", address2.ToString().c_str()));
    }
    else
    {
        result.push_back(json_spirit::Pair("masternode_payee", devpayee2.c_str()));
    }
}
```

### 2. `src/cmasternode.cpp` — Copy constructor nLastPaid bug

The old copy constructor had two consecutive assignments:
```cpp
nLastPaid = other.nLastPaid;   // correct — copies the real value
nLastPaid = GetAdjustedTime(); // BUG — immediately overwrites with current time
```

The second line was removed. The default constructor still correctly sets
`nLastPaid = GetAdjustedTime()` for brand new masternodes only.

---

## The Transition Fallback (v2.0.0.7 Only)

During the transition period when old and new nodes coexist on the network,
a fallback was added to `rpcmining.cpp`. When `vWinning` is empty (no `mnw`
P2P messages received yet), `getblocktemplate` falls back to
`FindOldestNotInVec` to suggest a payee.

> **⚠ IMPORTANT:** This fallback is intentionally in `rpcmining.cpp` ONLY
> (getblocktemplate). It was deliberately **NOT** added to `GetBlockPayee()`
> in `cmasternodepayments.cpp`.
>
> `CheckBlock()` in `cblock.cpp` relies on `GetBlockPayee()` returning
> **false** when `vWinning` is empty — that triggers the lenient validation
> path that accepts any coinbase structure, preventing old-node blocks from
> being rejected and peers from being banned with DoS(100).

Once all nodes on the network have upgraded to v2.0.0.7+, masternodes will
broadcast `mnw` messages and `vWinning` will be populated on all nodes.
At that point the fallback is no longer needed.

---

## How to Remove the Fallback in v2.0.0.8+

When the network is fully upgraded, remove the `else` branch from
`getblocktemplate` in `src/rpcmining.cpp`.

**File:** `src/rpcmining.cpp`  
**Around line 841**

Find this entire block:
```cpp
CScript mnPayee;
CTxIn mnVin;
if(masternodePayments.GetBlockPayee(pindexPrev->nHeight + 1, mnPayee, mnVin))
{
    CTxDestination address1;
    ExtractDestination(mnPayee, address1);
    CBitcoinAddress address2(address1);
    result.push_back(json_spirit::Pair("masternode_payee", address2.ToString().c_str()));
}
else
{
    // vWinning has no entry - fall back to FindOldestNotInVec (same as ProcessBlock)
    CMasternode* pmn = mnodeman.FindOldestNotInVec(std::vector<CTxIn>(), 0);
    if(pmn)
    {
        CScript fallbackPayee = GetScriptForDestination(pmn->pubkey.GetID());
        CTxDestination address1;
        ExtractDestination(fallbackPayee, address1);
        CBitcoinAddress address2(address1);
        result.push_back(json_spirit::Pair("masternode_payee", address2.ToString().c_str()));
    }
    else
    {
        result.push_back(json_spirit::Pair("masternode_payee", devpayee2.c_str()));
    }
}
```

Replace with:
```cpp
CScript mnPayee;
CTxIn mnVin;
if(masternodePayments.GetBlockPayee(pindexPrev->nHeight + 1, mnPayee, mnVin))
{
    CTxDestination address1;
    ExtractDestination(mnPayee, address1);
    CBitcoinAddress address2(address1);
    result.push_back(json_spirit::Pair("masternode_payee", address2.ToString().c_str()));
}
else
{
    result.push_back(json_spirit::Pair("masternode_payee", devpayee2.c_str()));
}
```

That is the only change required for the fallback removal.

---

## Future: Enabling Strict Enforcement (v2.0.0.8+)

Once the fallback is removed, you should also enable strict masternode payment
enforcement by adding a local fallback inside `GetBlockPayee()`. This is
Fix 4 from the original analysis — it makes enforcement deterministic on
non-MN nodes that never receive `mnw` messages.

> **⚠ ONLY do this AFTER the entire network has upgraded.** Adding it while
> old nodes exist will cause them to be banned (DoS 100) when their blocks
> fail the payee validation check in `CheckBlock()`.

**File:** `src/cmasternodepayments.cpp`

Find:
```cpp
bool CMasternodePayments::GetBlockPayee(int nBlockHeight, CScript& payee, CTxIn& vin)
{
    for(CMasternodePaymentWinner& winner : vWinning)
    {
        if(winner.nBlockHeight == nBlockHeight)
        {
            payee = winner.payee;
            vin = winner.vin;

            return true;
        }
    }

    return false;
}
```

Replace with:
```cpp
bool CMasternodePayments::GetBlockPayee(int nBlockHeight, CScript& payee, CTxIn& vin)
{
    // First try vWinning (populated by mnw P2P messages from active masternodes)
    for(CMasternodePaymentWinner& winner : vWinning)
    {
        if(winner.nBlockHeight == nBlockHeight)
        {
            payee = winner.payee;
            vin = winner.vin;

            return true;
        }
    }

    // Fallback: compute winner locally using FindOldestNotInVec.
    // Same algorithm as ProcessBlock.
    // SAFE only when ALL nodes on the network are v2.0.0.8+.
    CMasternode* pmn = mnodeman.FindOldestNotInVec(std::vector<CTxIn>(), 0);
    if(pmn)
    {
        payee = GetScriptForDestination(pmn->pubkey.GetID());
        vin = pmn->vin;
        return true;
    }

    return false;
}
```

---

## Summary Table

| File | Change | Version | Future Action |
|------|--------|---------|---------------|
| `src/rpcmining.cpp` | Replace `GetCurrentMasterNode` with `GetBlockPayee` + `FindOldestNotInVec` fallback | v2.0.0.7 | Remove fallback `else` branch in v2.0.0.8 |
| `src/cmasternode.cpp` | Remove spurious `nLastPaid = GetAdjustedTime()` in copy constructor | v2.0.0.7 | None — permanent fix |
| `src/cmasternodepayments.cpp` | `GetBlockPayee` unchanged (fallback NOT added) | v2.0.0.7 | Add `FindOldestNotInVec` fallback in v2.0.0.8 after full network upgrade |
