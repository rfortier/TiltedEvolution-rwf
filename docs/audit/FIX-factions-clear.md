# FIX-Q8 — `NotifyFactionsChanges` did not clear `Changes` before parsing

Status: **fixed and verified**. One file changed:
`Code/encoding/Messages/NotifyFactionsChanges.cpp`.

## Defect

`DeserializeRaw` read the entry count and then only ever *inserted* into
`Changes`; nothing removed an entry that the previous message had put there.

```cpp
uint64_t count = Serialization::ReadVarInt(aReader);

// Early abort as we don't want to allocate a ton of memory.
if (count > kMaxChangeCount)
    return;                       // (1) Changes keeps every entry of the last message

for (auto i = 0u; i < count; ++i)
{
    auto& change = Changes[Serialization::ReadVarInt(aReader) & 0xFFFFFFFF];  // (2)
    change.Deserialize(aReader);
}
```

1. **Over-limit return leaves the old entries in place.** The early abort is
   meant to answer "this packet carries nothing usable", but the object it
   returns in still holds the previous packet's map. A reused object therefore
   reports a faction change that was never sent again.
2. **The normal path does not clear either.** `operator[]` on a key that is
   already present keeps the existing value and hands back a reference to it,
   so the entry is *overwritten*, never removed. A key that the previous packet
   named and the current packet does not is left behind with its old value.

Message objects are reused in this codebase (the client factory recycles them,
and the sibling `NotifyActorValueChanges` / `NotifyActorMaxValueChanges` /
`RequestActorValueChanges` / `RequestActorMaxValueChanges` handlers each carry
a comment saying so), which is what turns both points from latent into live.

## Fix

The same one-liner the four sibling handlers already have, in the same place —
after the count is read, before the over-limit check, so it covers both paths:

```cpp
uint64_t count = Serialization::ReadVarInt(aReader);

// A message object can be reused: without this the entries of the previous
// message would linger and be applied a second time.
Changes.clear();
```

`SerializeRaw`, `kMaxChangeCount`, the signature, the header and the
`noexcept` contract are untouched. No `try/catch` was added: allocation
failure inside a `noexcept` frame is a separate concern and out of scope here.
```diff
@@ -26,6 +26,10 @@ void NotifyFactionsChanges::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReade

     uint64_t count = Serialization::ReadVarInt(aReader);

+    // A message object can be reused: without this the entries of the previous
+    // message would linger and be applied a second time.
+    Changes.clear();
+
     // Early abort as we don't want to allocate a ton of memory.
     //
     // Strictly greater: the writer sends min(Changes.size(), kMaxChangeCount), so a
```

The file is 46 lines after the edit; `read` reported
`lines.length == totalLines == 46`, so the write-back is complete and was not
truncated.

## Evidence

### 1. Gates — all four PASS, before and after

Run from the repository root with `python Tools/Scripts/<name>.py`:

| gate | before | after |
| --- | --- | --- |
| `check_wire_symmetry` | rc=0, 133 pairs | rc=0, 133 pairs |
| `check_message_use` | rc=0, 109 messages | rc=0, 109 messages |
| `check_message_factories` | rc=0, 112 opcodes | rc=0, 112 opcodes |
| `check_log_format_args` | rc=0, 684 log calls | rc=0, 684 log calls |

No gate moved from PASS to FAIL.

### 2. Wire-format reproduction (`g++`, `/tmp/q8`)

A self-contained driver builds the real wire format through the real
`NotifyFactionsChanges::SerializeRaw` / `DeserializeRaw`, the real
`Factions` / `Faction` / `GameId` structs and the real
`Message.cpp` base, once against `git show HEAD:…NotifyFactionsChanges.cpp`
(`app_old`) and once against the edited file (`app_new`):

```
g++ -std=c++20 -O0 -g -Wall -Wextra -DQ8_EXPECT_FIXED \
    -I .audit/q7stub -I Code -I Code/encoding -I /tmp/q8/new \
    -include Code/encoding/EncodingPch.h \
    /tmp/q8/main.cpp /tmp/q8/new/Messages/NotifyFactionsChanges.cpp \
    Code/encoding/Messages/Message.cpp \
    Code/encoding/Structs/Factions.cpp Code/encoding/Structs/Faction.cpp \
    Code/encoding/Structs/GameId.cpp -o /tmp/q8/app_new
```

`clear()` appears 0 times in the `HEAD` copy and 1 time in the edited copy, so
the two binaries really differ by this change.

**Before the fix (`git show HEAD`):**

```
packet[A,B] = 55 bytes, packet[A] = 28 bytes, oversized header = 2 bytes

scenario 1: reuse one message object, parse [A,B] then [A]
  after [A,B]: size=2 [key=43690 npc(id=572653571/286326787 rank=3 ) extra(id=1145307139/858980355 rank=4 ), key=48059 npc(id=572653575/286326791 rank=7 ) extra(id=1145307143/858980359 rank=8 )]
  after [A]  : size=2 [key=43690 npc(id=572653579/286326795 rank=11 ) extra(id=1145307147/858980363 rank=12 ), key=48059 npc(id=572653575/286326791 rank=7 ) extra(id=1145307143/858980359 rank=8 )]
  stale key B still present: YES, rank of A: 11

scenario 2: reuse that object again, parse an oversized header
  after oversized: size=2 [key=43690 npc(id=572653579/286326795 rank=11 ) extra(id=1145307147/858980363 rank=12 ), key=48059 npc(id=572653575/286326791 rank=7 ) extra(id=1145307143/858980359 rank=8 )]

scenario 3: single clean parse of [A,B]
  fresh parse: size=2 [key=43690 npc(id=572653571/286326787 rank=3 ) extra(id=1145307139/858980355 rank=4 ), key=48059 npc(id=572653575/286326791 rank=7 ) extra(id=1145307143/858980359 rank=8 )]
  matches the object that was serialized: YES
```

**After the fix (working tree):**

```
packet[A,B] = 55 bytes, packet[A] = 28 bytes, oversized header = 2 bytes

scenario 1: reuse one message object, parse [A,B] then [A]
  after [A,B]: size=2 [key=43690 npc(id=572653571/286326787 rank=3 ) extra(id=1145307139/858980355 rank=4 ), key=48059 npc(id=572653575/286326791 rank=7 ) extra(id=1145307143/858980359 rank=8 )]
  after [A]  : size=1 [key=43690 npc(id=572653579/286326795 rank=11 ) extra(id=1145307147/858980363 rank=12 )]
  stale key B still present: NO, rank of A: 11

scenario 2: reuse that object again, parse an oversized header
  after oversized: size=0 []

scenario 3: single clean parse of [A,B]
  fresh parse: size=2 [key=43690 npc(id=572653571/286326787 rank=3 ) extra(id=1145307139/858980355 rank=4 ), key=48059 npc(id=572653575/286326791 rank=7 ) extra(id=1145307143/858980359 rank=8 )]
  matches the object that was serialized: YES
```

Reading the two runs side by side:

- **Reuse, `[A,B]` then `[A]`** — before: `size=2`, key 48059 (B) still there
  with rank 7 and its old `GameId`s. After: `size=1`, only key 43690 (A), with
  the rank 11 that the second packet carried. The stale entry is gone.
- **Oversized count** (`0x401 = kMaxChangeCount + 1`) — before: the map still
  holds the two entries from the previous packet. After: `size=0`.
- **Clean single parse** — the `HEAD` and fixed builds produce byte-identical
  output for scenario 3 (`diff` of the two extracts exits 0), and the parsed
  map compares equal to the object that was serialized. The normal path is
  unchanged.

Each binary also self-checks: `app_old` asserts the defect is present
(4/4 PASS, process rc=0) and `app_new` asserts the fixed behaviour
(5/5 PASS, process rc=0), so a binary that silently did nothing could not have
reported success.

### 3. Compile check (`g++ 16.1.0`, stub `TiltedCore`)

The repository cannot be built in full, so the edited translation unit was
compiled on its own against the stub headers under `.audit/q7stub` with the
project precompiled header force-included — the same include set the previous
work on this family used:

```
$ g++ -std=c++20 -fsyntax-only -I .audit/q7stub -I Code -I Code/encoding \
      -include Code/encoding/EncodingPch.h \
      Code/encoding/Messages/NotifyFactionsChanges.cpp
rc=0
```

## Scope

`git diff --stat` for the file: `1 file changed, 4 insertions(+)`. Nothing else
in `Code/encoding/Messages/NotifyFactionsChanges.*` was touched, and no
spdlog dependency was introduced.

## Residual, not addressed here

`DeserializeRaw` is `noexcept` and still has no `try/catch`, while both
`Changes[key]` and `Factions::Deserialize` can allocate. That is a separate
defect with its own blast radius (`std::terminate`, not stale entries) and was
deliberately left alone; `Factions::Deserialize` already clears its vectors on
its own error paths, so the two interact but do not overlap.
