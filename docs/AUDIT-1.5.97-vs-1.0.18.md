# 1.5.97 兼容性审计报告（对照 v1.0.18 定版）

- 审计对象：`main` @ `c0abe640`
- 基线：`v1.0.18`（tag → `5a948fcd`）
- 分叉点：`5a99a0b6`（2026-02-28，上游 TiltedEvolution）
- 审计目的：确认合并上游后，1.5.97（SE 1.5.x legacy）的支持是**真实来源**，而非被静默破坏或虚假保留

## 方法

以 v1.0.18 为唯一真值基线，对 HEAD 做四层对账，由粗到细：

| 层 | 手段 | 能抓到什么 |
|---|---|---|
| ① | 逐字节 git 对象哈希（`git rev-parse <ref>:<path>`） | 整文件被改写、地址库被替换 |
| ② | 提取 `SKYRIM_TARGET_LEGACY` 分支体比对 | legacy 专有代码被上游非 legacy 版覆盖 |
| ③ | 提取全部 `static_assert` 集合比对 | 结构体偏移值被动过（不依赖 `#ifdef` 结构） |
| ④ | 提取显式 `"1.5.97"` 补丁点元组比对 | 1.5.x 专用补丁偏移被改 |

## 结论总览

**1 项真实回归（已修），其余全部来源真实。**

### ① 逐字节哈希 — PASS

31 个地址库/映射表文件全部与 v1.0.18 逐字节相同：

- 10 个 `version-1-5-{3,16,23,39,50,53,62,73,80,97}-0.bin`（pre-AE 地址库）
- 10 个 `versionlib-ae-to-se-1-5-*-0.map`（AE→SE 映射表，含 3701 行的 `1-5-97-0.map`）
- 11 个 AE 侧 `versionlib-1-6-*.bin` / `1-7-*.bin`

同时逐字节相同的代码资产：

| 文件 | 说明 |
|---|---|
| `Code/client/VersionDb.h` / `.cpp` | 地址库加载器整文件（sha `d7aa285e` / `205da2ce`） |
| `Games/Skyrim/BSGraphics/BSGraphicsRenderer.cpp` | 含 `{0x55+2, 0x57, "1.5.97"}` 等补丁点 |
| `Games/Skyrim/Interface/Menus/SkillsMenu.cpp` | 5 个 1.5.97 补丁点 |
| `Games/Skyrim/Interface/UI.cpp` | 2 个 1.5.97 补丁点 |
| `Games/Skyrim/Projectiles/Projectile.cpp` | `kLegacy1597` 行 |
| `Code/skse_bootstrap/main.cpp` | 自部署运行时、`kClientDllLegacyName` |
| `Code/skse_client/main.cpp` | `STClient_Bootstrap` |
| `Games/Skyrim/BSAnimationGraphManager.h` | 相对式 pad，两版共用 |
| `Games/Skyrim/ExtraData/ExtraDataList.h` | 相对式 `#ifndef SKYRIM_TARGET_LEGACY` |

### ② `SKYRIM_TARGET_LEGACY` 分支体 — PASS

9 个含该宏的文件，legacy 分支体逐条核对：

| 文件 | 结论 |
|---|---|
| `Actor.h` | 14 条 legacy 断言与 v1.0.18 **完全一致**（`sizeof(Actor)==0x2B0`、`currentProcess@0xF0` 等） |
| `TESObjectCELL.h` | `cellFlags[5]` → `cellFlags/cellGameFlags/cellState` 为**语义等价重写**：旧 `IsValid()` 读 `cellFlags[4]`＝字节 `0x44`，新 `cellState@0x44` 同字节、同值（`Attached=7`）。`pad45[0x88-0x45]` 演算成立 |
| `TESObjectREFR.h` | 保留 legacy 分支；新增的 `parentCell==0x60` 断言在 `extraData` 之前，两版同值 |
| `PlayerCharacter.h` | **见 ④，唯一回归，已修** |
| `ExtraDataList.h` | `#ifndef` 形式，legacy 侧未定义额外成员，与 v1.0.18 一致 |
| `BSAnimationGraphManager.h` | 相对式 pad（`pad_ptrs2[8]` vs `[9]`），逐字节相同 |
| `ObjectService.cpp` | legacy 分支保留 |
| `Code/client/xmake.lua` / `Code/skse_client/xmake.lua` | 双运行时目标定义完整 |

### ③ 断言集合 — PASS（9 个差异文件，8 个为等价重命名）

| 文件 | 判定 | 依据 |
|---|---|---|
| `TESActorBaseData.h` | **等价重命名** | `flags`→`actorBaseFlags` 等；重算布局：`deathItem`/`unk1C` 均 @0x20、`baseTemplateForm`/`owner` 均 @0x30、`factions` @0x40、`sizeof==0x58` |
| `ExtraLeveledCreature.h` | **等价重命名** | `npc1`→`originalBase` @0x10、新增 `templateBase` @0x18 |
| `TESNPC.h` | **等价重命名** | `npcTemplate`→`faceNPC`（新增断言 @0x1F0）、`outfits[2]`→`defaultOutfit`/`sleepOutfit` |
| `TESActorBase.h` | 新增断言 | `sizeof(TESActorBase)==0x150`，无字段变动 |
| `TESObjectCELL.h` | 等价重构 | 见 ② |
| `MenuTopicManager.h` | 新增断言 | v1.0.18 无断言，HEAD 补 3 条，指向既有字段 |
| `GameVM.h` | **版本无关的上游修正** | `virtualMachine` `0x200`→`0x210`，无 legacy 分支（两版共用）；见下方专项 |
| `TESObjectREFR.h` | 新增断言 | 见 ② |
| `PlayerCharacter.h` | **真实回归** | 见 ④ |

### ④ 显式 `"1.5.97"` 补丁点 — PASS，唯一定位到 PlayerCharacter

9 个 `{legacy, modern, "1.5.97"}` 元组**全部完全一致，无增减**。

## 唯一真实回归：`PlayerCharacter.h` legacy 锚点

### 现象

1.5.97 下点击「新游戏」进入游戏，加载完成 2ms 后立即 CTD：

```
[21:00:36.786] Finished loading, triggering visit cell
[21:00:36.788] [critical] VectoredExceptionHandler: crash occurred!
exception code is c0000005, at address 0x7ffb1207591a, flags 0
faulting instruction is in SkyrimTogetherRuntime_1_5.dll+0xb591a
access type read (code 0), target address 0xff7fffff (unmapped)
```

崩溃指令（离线反汇编 hexdump 得出，函数入口 `0xb58d0`）：

```
1fb: test r14, r14            ; if (pLocation)
1fe: je   0x240
200: mov  rdx, [r14]          ; <-- 崩在这：读 vtable
203: mov  rbx, [rdx+0x170]    ; GetName()
```

`r14 = 0xff7fffff = -FLT_MAX`，来自 `mov r14, [rax+0xAD0]`，即 `pPlayer->locationForm`。

### 根因

`PlayerCharacter` 是**绝对锚点 pad** 布局：`pad1` 的锚点单独决定整条尾巴，
后续 `pad588[0x9B0-0x598]`、`pad9B8[0xAC8-0x9B8]` 是固定增量的**纯胶水**。

```
v1.0.18   (5a948fcd)          : pad1[0x580 - sizeof(Actor)]   ✓ 真值
d374a1a5  「Merge upstream」  : pad1[0x590 - sizeof(Actor)]   ✗ 回归
c0abe640  （本次修复）         : pad1[0x580 - sizeof(Actor)]   ✓
```

合并提交 `d374a1a5` 的自述解决规则是：

> **adopt upstream value, then apply the 1.5.x delta on top**

对 `pad1` 这条规则**不成立**——上游把 AE 锚点 `0x588`→`0x590`，合并把 `0x590`
直接写进了 legacy 分支，**legacy 增量实际为 0**，整条尾巴相对真值漂 `+0x10`。

于是读 `[player+0xAD0]` 拿到的是真 `locationForm` **之后的**内容，恰好是
`0xFF7FFFFF`；随后 `GetName()` 虚调用读 vtable → AV read。

| 字段 | 真·SE 1.5.97 | 回归值 | AE 1.7.x |
|---|---|---|---|
| 锚点 `pad1` | 0x580 | 0x588 | 0x590 |
| `objectives` | 0x580 | 0x588 | 0x590 |
| `pSkills` | 0x9B0 | 0x9B8 | 0x9C0 |
| **`locationForm`** | **0xAC8** | **0xAD0** | 0xAD8 |
| `baseTints` | 0xB10 | 0xB18 | 0xB20 |
| `overlayTints` | 0xB28 | 0xB30 | 0xB38 |
| `sizeof` | 0xBE0 | 0xBE8 | 0xBF0 |

佐证：v1.0.18 同文件内注释原文已写明正确规则——

> the Actor base is 8 bytes smaller … so objectives sits at 0x580 instead of 0x588;
> **all following absolute-anchor pads land 8 bytes earlier automatically**

即**只改锚点，后面自动跟随**。合并把这条注释连同正确值一并丢掉。

### 修复（`c0abe640`）

1. `PlayerCharacter.h`：legacy 锚点恢复 `0x580`，legacy 断言全部恢复真值。
2. `TESForm.h`：新增 `IsPlausiblyValidForm()` + 自由函数 `IsPlausibleFormPointer()`
   （**先筛指针值再解引用**，避免守卫自身触发同样的崩溃）。
3. `DiscordService.cpp`：`OnLocationChangeEvent` 对 `pLocation` 加守卫。
4. `DiscoveryService.cpp`：`VisitCell` 不再对无效指针触发 `LocationChangeEvent`。

## 专项：`SkyrimVM` 偏移改动是安全的

`GameVM.h` 的 `virtualMachine` 由 `0x200`→`0x210`（上游修正），并新增
`pad218` / `inactive`。判断依据：

- **无 `SKYRIM_TARGET_LEGACY` 分支** → 两版本共用同一值，`0x210` 是引擎真实值。
- 1.5.97 上 VM 的**主获取路径**仍是 papyrus 钩子写入的 `SetVirtualMachine()`
  （`s_pVirtualMachine`），结构体偏移只是 **fallback**，
  且被 `LooksLikeVirtualMachine()`（要求 `MEM_PRIVATE` + vtable 落在 `MEM_IMAGE`）拦截。
- v1.0.18 的注释指出 1.5.97 上按结构体偏移读会拿到非 VM 的非空指针——该防护仍在。

结论：此次改动**不降低** 1.5.97 安全性，反而消除了 `0x200`/`0x210` 的静默分叉
（HEAD 已改为 `offsetof(SkyrimVM, virtualMachine)`，两处不会再漂）。

## 专项：构建/打包链路完整

| 环节 | 位置 | 状态 |
|---|---|---|
| legacy 静态库 | `Code/client/xmake.lua:90` `build_client("SkyrimTogetherClientLegacy", {"SKYRIM_TARGET_LEGACY=1"})` | ✓ |
| legacy 运行时 DLL | `Code/skse_client/xmake.lua:72` `build_runtime_dll("SkyrimTogetherClientDllLegacy", "SkyrimTogetherRuntime_1_5", ...)` | ✓ |
| CI 显式构建 | `.github/workflows/windows.yml:129` `xmake build SkyrimTogetherClientDllLegacy` + 产物存在性检查 | ✓ |
| 运行时选择 | `Code/skse_bootstrap/main.cpp:41-42,349` 按 game version 选 `_1_5` | ✓ |
| 打包 | `.github/workflows/release.yml:81` 校验两个 DLL 都存在 | ✓ |

> 注：`SkyrimTogetherClientDllLegacy` 在 `Code/client/xmake.lua` 中不存在，
> 它定义在 `Code/skse_client/xmake.lua`（静态库 → 共享库两级），**不是缺失目标**。

## 遗留

- CI 结果未在本机确认（`gh` 未安装、GitHub API 未认证限流）。
  需在 Actions 页面确认 `c0abe640` 的 legacy 与 AE 两个目标均通过，
  重点看新增的 legacy 断言（`locationForm == 0xAC8`、`sizeof == 0xBE0`）。

## 后续同步上游时的硬性动作

1. **先逐字节哈希对账**（最快，几秒清掉大部分文件），再对差异文件做分支体/断言/补丁点比对。
2. **凡 `#ifdef SKYRIM_TARGET_LEGACY` 的偏移，禁止用「AE 值 − 8」推导**。
   必须回 v1.0.18 或 `git log -S"<pad 字面量>"` 找到当初定值的提交，**照抄断言表，不要重算**。
3. **合并解决规则要检查适用范围**：「上游值 + legacy delta」只对**相对式 pad** 成立；
   **锚点式 pad 必须各自独立定值**。
4. 提取 legacy 分支体时注意：遇 `#else` 需**清空已收集内容**，否则会误取非 legacy 侧。
5. 同步后跑一遍断言集合对账，新增/改动的断言逐个判断是「等价重命名」还是「真回归」。
