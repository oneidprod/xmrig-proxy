# CLAUDE.md — xmrig-proxy

## Session Rules
- Warn at 40% context usage, stop at 50%
- Always ask before committing to git
- Always push after every commit
- Strict git commit policy — never lose work
- Keep this file updated with goals, progress, and next-session tasks
- End each session by updating the IN PROGRESS marker and providing the next session start statement

## Next Session Start Statement
> Session#2 Run your map tool, read CLAUDE.md. Resume from IN PROGRESS marker.

---

## Project Goal
Add Equihash stratum protocol support to xmrig-proxy so it can proxy miners for Equihash-based coins.

### Equihash Variants to Support
| Variant | Coins | Solution Size |
|---------|-------|---------------|
| 192,7 | ZER (Zero), ZEL (Flux legacy) | 400 bytes |
| 200,9 | ZEC (Zcash), ZEN (Horizen) | 1344 bytes |
| 210,9 | ZEC variant | 1344 bytes |
| 144,5 | BEAM, BTG (Bitcoin Gold) | 100 bytes |

**Test target for session #2:** Equihash 192,7 against local SNOMP ZER pool.

### Future Work (not in scope yet)
- VerusHash (VRSC) — same wire format as Equihash, different PoW hash
- GhostRider (RTM) — see detailed notes below; our Equihash work will directly enable it

---

## Architecture Plan

### New Class: EquihashStratumClient
- Inherits from `Client` (not EthStratumClient directly, but modeled after it)
- Single class parameterized by `Algorithm` — handles all variants
- Lives at: `src/base/net/stratum/EquihashStratumClient.h/cpp`

### Key pattern reference: `EthStratumClient.cpp`
- 2-phase login (subscribe + authorize) ← Zcash uses same pattern
- `mining.set_difficulty` parsing ← reuse
- Extra nonce handling (`m_extraNonce` pair) ← reuse
- Block header reconstruction from `mining.notify` ← adapt for Zcash 8-field format

### Algorithm Enum Additions (`src/base/crypto/Algorithm.h/cpp`)
```cpp
EQUIHASH_192_7  = 0x65010000,  // "equihash/192,7"
EQUIHASH_200_9  = 0x65020000,  // "equihash/200,9"
EQUIHASH_210_9  = 0x65030000,  // "equihash/210,9"
EQUIHASH_144_5  = 0x65040000,  // "equihash/144,5"
EQUIHASH        = 0x65000000,  // family
```

### Blake2b dependency
- Check `openssl version` — if ≥ 1.1.1, use `EVP_blake2b512()` (zero extra code)
- If not: embed `BLAKE2/ref/blake2b-ref.c` in `src/3rdparty/blake2/` (~500 LOC, public domain)
- Only needed for difficulty validation; proxy forwards solution as-is to pool

### Protocol (Zcash stratum — same for all Equihash variants)
```
LOGIN:
  mining.subscribe(user_agent, placeholder) → [[subs], nonce1_hex, nonce2_size]
  mining.authorize(worker, password) → true/false

NOTIFY (mining.notify):
  [jobid, version, prevhash, merkle_root, reserved, time, bits, clean_jobs]
  (8 fields — all variants identical)

SUBMIT:
  mining.submit(worker, jobid, time, nonce2, equihash_solution)
```

---

## Files to Change

| File | Change |
|------|--------|
| `src/base/net/stratum/EquihashStratumClient.h` | **CREATE** ~100 lines |
| `src/base/net/stratum/EquihashStratumClient.cpp` | **CREATE** ~800 lines |
| `src/base/crypto/Algorithm.h` | Add 4 enum entries + family |
| `src/base/crypto/Algorithm.cpp` | Add string mappings |
| `CMakeLists.txt` or `src/base/base.cmake` | Register new source files (2 lines) |
| `src/base/net/stratum/AutoClient.cpp/h` | Add EQUIHASH_MODE detection |

---

---

## GhostRider (RTM) — Status & Notes
**Partially implemented, NOT working against standard pools.**

### What exists in the code
- `EthStratumClient.cpp` has GhostRider job parsing (`mining.notify` 9-field Bitcoin format, lines 260-355)
- `EthStratumClient.cpp` has GhostRider submit format (lines 79-87)
- `AutoClient::parseLogin()` can detect GhostRider via xmrig-style login response

### Why it doesn't work with real RTM pools
`AutoClient::parseLogin()` (AutoClient.cpp line 53) only routes to ETH_MODE if the pool sends an xmrig-style single JSON-RPC `login` response containing `"algo"` and `"extra_nonce"` fields. Real Raptoreum stratum pools use **Bitcoin-style stratum**: `mining.subscribe` → `mining.authorize` → `mining.notify`. There is no `login` method — so `AutoClient` never switches to ETH_MODE and the GhostRider job parsing in EthStratumClient is never reached.

### How our Equihash work enables GhostRider completion
`EquihashStratumClient` will implement the proper Bitcoin-style subscribe+authorize flow. GhostRider uses the **same flow** — the only differences are:
1. Job format: GhostRider uses 9-field notify with coinbase+merkle-branches (code already in EthStratumClient lines 260-355)
2. Submit format: already in EthStratumClient lines 79-87
3. Difficulty: uses `ceil(diff * 65536.0)` (already in EthStratumClient lines 221-223)

After Equihash is done, GhostRider support = copy EquihashStratumClient, swap in the existing GhostRider job/submit/difficulty logic. Estimated ~2-4 hours of work.

---

<!-- IN PROGRESS -->
## Session #1 — 2026-03-31
**Status: COMPLETE**

### Completed
- [x] Explored both proxies (xmrig-proxy C++ and stratum-proxy Java)
- [x] Decided on xmrig-proxy as primary implementation target
- [x] Identified EthStratumClient as the template to follow
- [x] Defined all Equihash variants: 192,7; 200,9; 210,9; 144,5
- [x] Documented Java proxy findings in `stratum-proxy/stratum-proxy-notes.md`
- [x] Verified OpenSSL 3.0.2 — Blake2b via EVP available, no extra deps needed
- [x] Verified baseline build compiles clean (cmake + make 100%)
- [x] Confirmed donate level already 0 in src/donate.h
- [x] Deeply read EthStratumClient.cpp and AutoClient.cpp — full understanding of patterns
- [x] Discovered GhostRider is NOT working against real RTM pools — documented above

## Session #2 — TODO
**Status: READY TO CODE**

### Starting Tasks (in order)
1. Add `EQUIHASH_*` entries to `Algorithm.h` enum + `Algorithm::Family`
2. Add string constants + name/alias map entries to `Algorithm.cpp`
3. Add to `all()` order vector in `Algorithm.cpp`
4. Create `src/base/net/stratum/EquihashStratumClient.h`
5. Create `src/base/net/stratum/EquihashStratumClient.cpp`
   - `subscribe()` / `authorize()` / `login()` — from EthStratumClient pattern
   - `onSubscribeResponse()` — parse `[subs, nonce1, nonce2_size]`
   - `onAuthorizeResponse()` — set authorized, fire login success
   - `parseNotification()` — handle `mining.set_difficulty` and `mining.notify` (8-field Zcash)
   - `submit()` — `mining.submit(worker, jobid, time, nonce2, solution)`
   - `setExtraNonce()` — from EthStratumClient, works as-is
6. Register in `CMakeLists.txt`
7. Wire into pool client selection (config `--algo equihash/192,7` or URL scheme)
8. Build and test against SNOMP ZER pool

### Key Implementation Notes for Session #2
- Zcash `mining.notify` is 8 fields: `[jobid, version, prevhash, merkle_root, reserved, time, bits, clean]`
  - GhostRider uses 9 fields with coinbase1/coinbase2/merkle-branches — different
  - No merkle tree calculation needed for Zcash (merkle_root is given directly)
- Nonce is 32 bytes total: `nonce1(pool) + nonce2(worker)`, zero-padded
- Solution forwarded as-is — proxy does NOT validate the Equihash proof
- Difficulty check: SHA256d of 140-byte header compared against target
- `m_extraNonce2Size` from subscribe response = nonce2 byte length (32 - nonce1_bytes)
- Blake2b: `EVP_DigestInit_ex(ctx, EVP_blake2b512(), NULL)` — OpenSSL 3.0.2 confirmed available
