# CLAUDE.md — xmrig-proxy

## Session Rules
- Warn at 40% context usage, stop at 50%
- Always ask before committing to git
- Always push after every commit
- Strict git commit policy — never lose work
- Keep this file updated with goals, progress, and next-session tasks
- End each session by updating the IN PROGRESS marker and providing the next session start statement

## Next Session Start Statement
> Session#4 Read CLAUDE.md only. Resume from IN PROGRESS marker. Do NOT run map tool or explore agents until needed — preserve context.

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

## Session #2 — 2026-03-31
**Status: COMPLETE**

### Completed
- [x] Added `EQUIHASH_192_7/200_9/210_9/144_5` to `Algorithm.h` enum (guarded by `XMRIG_ALGO_EQUIHASH`)
- [x] Added `Algorithm::EQUIHASH` family constant
- [x] Added string constants, name map, alias map entries to `Algorithm.cpp`
- [x] Added all four variants to `all()` order vector
- [x] Created `src/base/net/stratum/EquihashStratumClient.h`
- [x] Created `src/base/net/stratum/EquihashStratumClient.cpp`
  - subscribe/authorize/login flow (identical to EthStratumClient pattern)
  - onSubscribeResponse: parses `[subs, nonce1, nonce2_size]`
  - onAuthorizeResponse: sets authorized, fires onLoginSuccess
  - parseNotification: handles `mining.set_difficulty` and `mining.notify` (8-field Zcash)
  - submit: `mining.submit(worker, jobid, ntime, nonce2, solution)`
  - setExtraNonce: copy of EthStratumClient version
- [x] Fixed `JobResult.cpp` to set `m_actualDiff = UINT64_MAX` for Equihash (bypasses 32-byte result check)
- [x] Wired `EquihashStratumClient` into `Pool.cpp::createClient()` for `Algorithm::EQUIHASH` family
- [x] Added `WITH_EQUIHASH` option to `CMakeLists.txt` (default ON)
- [x] Registered `.h/.cpp` in `base.cmake` under `if (WITH_EQUIHASH)` block (also sets `XMRIG_ALGO_EQUIHASH` define)
- [x] **Build: 100% clean, zero errors, zero warnings**

### Key implementation note: Protocol mismatch discovery
The proxy's `Miner.cpp:244` only handles xmrig's proprietary `"submit"` method — not `"mining.submit"`. Miners connecting to the proxy must use xmrig-compatible format. Fields map as:
- `result.nonce` = nonce2 hex (padded to `extraNonce2Size*2` chars)
- `result.result` = Equihash solution as hex string (variable length, 400–1344 bytes)

### Known limitations to test for
- The miner sends jobs to connecting miners in xmrig blob format; miner software must interpret the 140-byte Zcash header blob correctly
- `Job::setBlob()` has a max blob size limit — verify 280 hex chars (140 bytes) is within limits
- The `isValid()` check in JobResult requires `strlen(nonce) == 8` — works for 4-byte nonce2 (8 hex)

## Session #3 — 2026-03-31
**Status: COMPLETE — DISCOVERY SESSION**

### Completed
- [x] Proxy started, connected to ZER pool (192.9.246.79:3092), jobs flowing — pool-facing works
- [x] sa-tromp connected to proxy port 7777 — TCP accepted but stratum handshake fails
- [x] Root cause identified: proxy miner-facing side only accepts xmrig `login` format; sa-tromp (and all standard Equihash miners) use Zcash stratum (`mining.subscribe/authorize`)
- [x] Full plan designed: add Zcash stratum support to the proxy's miner-facing side via auto-detecting new `EquihashMiner` class

### Key decisions made this session
- Proxy should accept **any standard Equihash miner** (sa-tromp, nheqminer, gminer, lolminer) — this is the right architecture
- Auto-detect protocol on the same port: `mining.subscribe` → EquihashMiner path; `login` → existing Miner path
- Proxy handles custom-diff, nonce splitting — miners connect with zero changes
- Implementation plan saved at: `/home/griffithm/.claude/plans/proud-imagining-treasure.md`

<!-- IN PROGRESS -->
## Session #4 — TODO
**Status: READY TO IMPLEMENT**

### Goal
Implement `EquihashMiner` class in xmrig-proxy so standard Zcash stratum miners can connect.

### Files to read at session start (and ONLY these — no broad exploration)
1. `src/proxy/Miner.cpp` — understand parseRequest WaitLoginState block (lines ~199-240) where we add protocol detection
2. `src/proxy/Miner.h` — class structure to mirror for EquihashMiner
3. `src/proxy/events/LoginEvent.h` — to fire login event correctly
4. `src/proxy/events/SubmitEvent.h` — to fire submit event correctly

### Implementation tasks (in order)
1. **Create `src/proxy/EquihashMiner.h`** (~80 lines)
   - State enum: WaitSubscribeState, WaitAuthorizeState, ReadyState, ClosingState
   - Members: m_nonce1 (8 hex), m_fixedByte, m_user, m_pass, m_subscribeId, m_authorizeId, m_lastNtime, m_customDiff, m_state
   - Methods: onData(line), handleSubscribe, handleAuthorize, handleSubmit, setJob, sendNotify, sendSetDifficulty

2. **Create `src/proxy/EquihashMiner.cpp`** (~400 lines)
   - `handleSubscribe()`: reply `{"id":N,"result":[[],"{nonce1_hex}",4],"error":null}` where nonce1 = m_fixedByte as 2 hex chars
   - `handleAuthorize()`: store user/pass, fire `LoginEvent::create(this,...)->start()`
   - `setJob(Job &job)`: decode blob → 8 fields, send `mining.set_difficulty` + `mining.notify`
   - `handleSubmit()`: parse params[1..4] → fire `SubmitEvent::create(...)->start()`
   - Blob decode: bytes[0..3]=version, [4..35]=prevhash, [36..67]=merkle, [68..99]=reserved, [100..103]=ntime, [104..107]=nbits
   - Submit nonce = params[3] (nonce2, 8 hex); result = params[4] (solution hex)

3. **Modify `src/proxy/Miner.cpp` WaitLoginState block** (~5 lines)
   - If first message method == `"mining.subscribe"` → switch this connection to EquihashMiner handler (or delegate)
   - Cleanest: check in parseRequest before the `strcmp(method,"login")` check

4. **Register in `src/base/base.cmake`** (2 lines under `if (WITH_EQUIHASH)`)

5. **Build and test**
   ```bash
   cd /home/griffithm/builds/prox/xmrig-proxy/build && make -j$(nproc)
   ./xmrig-proxy --algo equihash/192,7 -o 192.9.246.79:3092 -u t1e6nAkZLoXUgwRuJ9qj2CF15qkroWVsVVQ.proxw -p x --bind 0.0.0.0:7777
   # In another terminal:
   ./sa-tromp -o stratum+tcp://10.42.0.205:7777 -u t1e6nAkZLoXUgwRuJ9qj2CF15qkroWVsVVQ.wtest -P x
   ```
   Watch for: `miners: 1`, job forwarded, submit received, share accepted at pool.

### Context-saving rules for Session #4
- Read only the 4 files listed above at start — no broad codebase exploration
- The plan file at `/home/griffithm/.claude/plans/proud-imagining-treasure.md` has full protocol details
- sa-tromp source is at `/home/griffithm/sa-tromp/source/silentarmy192_7/stratum.c` (reference only if needed)
- Pool: 192.9.246.79:3092, wallet: t1e6nAkZLoXUgwRuJ9qj2CF15qkroWVsVVQ.proxw
