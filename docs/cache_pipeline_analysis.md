# Phase 6: cache pipeline analysis

## Conclusion

Phase 6 succeeds as a bounded proof of concept, with an important limitation.

For AviUtl2 v2.1.4, the observed rendered-video cache is keyed by the public
four-byte integer frame number. The internal double timeline coordinate is not
part of that key. A repeated `func_get_video(80)` therefore returns the first
cached version of frame 80 and can bypass timeline construction, object
evaluation, filter callbacks, and subframe injection.

A debug-only controlled eviction makes `80.0 -> 80.5 -> 80.0` and the reverse
sequence render correctly while every recorded output request remains
`func_get_video(80)`. This proves that the same integer output frame can produce
multiple independent subframe states sequentially without changing project
FPS. It does not prove that two subframe variants can coexist in AviUtl2's
native cache, and the eviction technique is not production-ready.

All internal RVAs below apply only to the tested `aviutl2.exe` v2.1.4 image:

- SHA-256: `ED8AA51A80017839C232F35E7D3F6CB5E56FD09E8E13604726119CFB7C67CE89`
- image base used for analysis: `0x140000000`
- names ending in `Candidate` are neutral labels, not recovered symbols
- no AviUtl2 code bytes were patched

## Observed pipeline

```text
output plugin: func_get_video(integer frame, format)
  -> host bridge RVA 0x22a6c0
  -> RenderVideoWorkerCandidate RVA 0x2206b0
  -> integer-frame cache helper RVA 0x2219d0
       hit -> hit-only path RVA 0x221aba -> cached buffer returned
       miss -> allocation / nested map / render-task scheduling
                -> worker callback RVA 0x269cb0
                -> ordinary wrapper RVA 0x265590
                -> TimelineStateBuildCandidate RVA 0x2662d0
                -> object/filter evaluation and composition
                -> later cache/assembly helper RVA 0x221c00
                -> rendered buffer returned
```

The public boundary is unambiguous: `OUTPUT_INFO::func_get_video` accepts an
integer `frame` and a format in `reference/aviutl2_sdk/output2.h:57`. The only
public buffer-control callback is `func_set_buffer_size` at
`reference/aviutl2_sdk/output2.h:83`; its declaration does not promise cache
invalidation.

The miss-to-worker segment contains indirect queue/virtual dispatch. Its
ordinary render-task destination is supported by the observed worker event and
the static call from RVA `0x269cb0` to `0x265590`, but the complete indirect
edge should not be treated as a recovered named call graph.

## Cache location and key

### First lookup: RVA 0x2219d0

This is the earliest confirmed cache lookup after the public output bridge.

- `0x220740` loads the table/context pointer from worker context offset `+0x88`.
- `0x220753` passes a pointer to a stack dword containing the normalized
  integer frame.
- `0x22077f` calls RVA `0x2219d0`.
- `0x2219d0` hashes exactly four key bytes using FNV-1a 64-bit.
- bucket mask, buckets, and sentinel are read at table offsets `+0x180`,
  `+0x168`, and `+0x158`.
- a node's dword key is compared at node offset `+0x10`.
- miss branches at `0x221a8b` and `0x221ab8` reach the miss path at
  `0x221ae8`.
- a successful key comparison reaches the hit-only block at `0x221aba`, then
  returns the existing result without scheduling the ordinary timeline task.

The experiment placed a hardware execution breakpoint at `0x221aba` after
verifying the expected bytes `48 85 ED 74 09`. On the second and third frame-80
requests it recorded:

```text
key=80 node_key=80 key_ok=1 node_ok=1
table=0x231f3d687c8 node=0x231f49e0d10
timeline candidate_hits=0 modified_hits=0
```

Both hits ran on output thread `32532`. The timeline builder used thread
`38492`; the filter callback reporting `OBJECT_INFO` used thread `24268`.
Consequently, neither an output-thread global nor simple TLS can associate the
three stages safely.

### Later maps

Static inspection found two more maps on this path:

| Location | Observed key behavior | Role confidence |
|---|---|---|
| RVA `0x0f4490` | FNV-1a over four bytes, dword node key at `+0x10` | nested/shared integer-key map; exact semantic owner unknown |
| RVA `0x221c00` | initial and nested integer-key maps; hit begins near `0x221cfb` | later cache/assembly helper |

No inspected map hashes or compares the private double passed to
`TimelineStateBuildCandidate`. This establishes absence from these maps, not
from every cache in AviUtl2.

### Effective key scope

The directly observed lookup identity is:

```text
(table/context identity, int32 frame)
```

The dword hash itself contains only the integer frame. Table identity may
partition caches by render context or format, but ownership and lifetime of
that table are not yet proven. Format and object state were not observed in the
four-byte key. The table uses a guard/lifetime object at `+0xb0`, so clearing or
editing its buckets in place would risk list, reference-count, and concurrent
state corruption.

## Controlled collision tests

The test project has `rate=30`, `scale=1`, and 161 frames. Before the target
sequence, the probe requests 60 unique frames to make the sequence repeatable.
Every recorded target calls public `func_get_video(80)`.

Two stable image states were observed:

- coordinate `80.0`: `OBJECT_INFO.time = 2.6666666666666665`, RGB24 FNV-1a64
  `875555f88e3def2b`
- coordinate `80.5`: `OBJECT_INFO.time = 2.6833333333333331`, RGB24 FNV-1a64
  `e8cc2a2b6ddd098f`

### Baseline, no cache action

| Sequence | Requested coordinates | Timeline builder hits | Returned hashes | Result |
|---|---|---|---|---|
| A | `80.0, 80.5, 80.0` | `1, 0, 0` | `H0, H0, H0` | 80.5 is stale |
| B | `80.5, 80.0, 80.5` | `1, 0, 0` | `H05, H05, H05` | 80.0 is stale |

`H0` is `875555f88e3def2b`; `H05` is `e8cc2a2b6ddd098f`.
Order therefore changes the output: whichever subframe is rendered first owns
the native integer-frame cache entry.

Sequence A additionally captured the exact cache-hit block. Requests 61 and
62 both hit the same table/node with integer key 80, and neither entered the
timeline builder. This closes the earlier ambiguity between the first lookup
and downstream cache candidates for the observed repeated-frame bypass.

### Public buffer-size change

The probe called `func_set_buffer_size(1)`, then `(2)`, then `(1)` around
sequence A. All hashes remained `H0`. On the 80.5 request, the only observed
timeline construction was for integer frame 81, consistent with prefetch;
frame 80 remained cached. Public buffer resizing is therefore not a reliable
cache-clear operation.

### Controlled one-entry eviction

The debug test selected public video buffer size one and inserted an unrecorded
request for integer frame 79 between target samples:

```text
get_video(80) -> get_video(79) -> get_video(80) -> get_video(79) -> get_video(80)
```

| Sequence | Target coordinates | Timeline builder hits | OBJECT_INFO.time | Target hashes |
|---|---|---|---|---|
| A | `80.0, 80.5, 80.0` | `1, 1, 1` | `2.6666667, 2.6833333, 2.6666667` | `H0, H05, H0` |
| B | `80.5, 80.0, 80.5` | `1, 1, 1` | `2.6833333, 2.6666667, 2.6833333` | `H05, H0, H05` |

The spacer frame 79 consistently hashed to `0f343fd7231687b8` and reported
`OBJECT_INFO.time = 2.6333333333333333`. The spacer is not counted as an output
sample.

This proves that cached frame 80 can be evicted and rebuilt at another double
coordinate without stale output. It does not establish that AviUtl2 stores
`80.0` and `80.5` simultaneously; the evidence instead shows sequential
replacement of an integer-keyed entry.

## Cache-layer implications

| Cache area requested by Phase 6 | Evidence | Subframe conclusion |
|---|---|---|
| composed/rendered video cache | dynamically confirmed at first hit path `0x221aba` | collides on integer frame 80 |
| object/effect evaluation caches | both 80.0 and 80.5 produce correct distinct states after top-level eviction | no stale result observed in this fixture, but key layout not recovered |
| media/source cache | not isolated; source media is not the changing signal in the fixture | compatibility remains unknown |
| preview cache | output test did not exercise preview | out of scope for this result |

The successful eviction sequence is positive end-to-end evidence that no
downstream cache in this project fixture immediately collapses 80.0 and 80.5.
It is not enough to declare every object, effect, media, or temporal filter
cache subframe-safe. Stateful effects, motion blur, scripts, feedback, and
third-party filters require a broader cache matrix in a later phase.

## Solution options

| Option | Performance | Stability / correctness | Plugin compatibility | Maintenance | Phase 6 assessment |
|---|---|---|---|---|---|
| A. Disable relevant cache | worst; every sample fully renders | reasonable only if scoped to one output task; no public disable switch was found | may preserve filter semantics, but shared preview/output state is risky | private flag/path must be rediscovered per build | plausible, not yet safely located |
| B. Extend key with double coordinate | best potential reuse and true coexistence | best semantics if all dependent maps use the same normalized coordinate | potentially best for temporal filters | highest reverse-engineering burden; fixed four-byte maps cannot simply be widened | preferred long-term idea, not implemented |
| C. Force invalidation before each sample | extra render/lookup work; neighbor method is roughly two requests per sample | Phase 6 proves sequential correctness only; neighbor evaluation can affect stateful filters and fails at bounds | may trigger unexpected callbacks/side effects | simple probe, poor production robustness | debug proof succeeded; reject as production design |
| D. Separate render context | memory-heavy; may sacrifice cache sharing | strongest isolation if construction, ownership, and teardown are correct | could avoid altering shared plugin state | context creation/lifetime/API remains unresolved | architecturally promising, no safe prototype yet |

Editing the map key in place or clearing buckets directly is excluded: the maps
have sentinels, chains, guards, and node lifetimes. Changing the frame dword to
force a miss is also invalid because that same value propagates as the integer
timeline frame.

## Success boundary and next step

Phase 6 meets the requested proof condition in this precise sense:

1. identical public request index 80 produced independently evaluated 80.0 and
   80.5 states;
2. both request orders were deterministic;
3. project FPS stayed 30 and encoder metadata was not involved;
4. the stale-frame cause is now tied to a dynamically observed integer cache
   hit rather than inferred only from missing builder calls.

Phase 6 does not deliver a production cache architecture. The next phase
should propagate a render-task identity or normalized double coordinate to a
shadow cache/key, or prove creation of an isolated render context. It should
not use neighbor-frame eviction except as a regression oracle.

## Evidence files

- `experiments/cache_bypass_test/cache_hit_A.log`
- `experiments/cache_bypass_test/cache_hit_A_watch.log`
- `experiments/cache_bypass_test/baseline_B.log`
- `experiments/cache_bypass_test/baseline_B_watch.log`
- `experiments/cache_bypass_test/resize_A.log`
- `experiments/cache_bypass_test/resize_A_watch.log`
- `experiments/cache_bypass_test/evict_A.log`
- `experiments/cache_bypass_test/evict_A_watch.log`
- `experiments/cache_bypass_test/evict_B.log`
- `experiments/cache_bypass_test/evict_B_watch.log`
- `experiments/cache_bypass_test/phase6_results.tsv`
- `experiments/cache_bypass_test/phase6_object_info_excerpt.log`

The probe and watcher source are
`experiments/cache_bypass_test/cache_bypass_probe.cpp` and
`experiments/subframe_scheduler_test/subframe_scheduler_watch.cpp`.
