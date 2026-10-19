# REGRESSION 复审计报告 —— 游标对齐修复这一轮

审计范围：`git diff` 中的 8 个文件（另有 8 个 plugins 子模块指针变化，与本轮无关）。
方法：逐行读代码 + 手数线上位宽（不采信注释、不采信 check_wire_symmetry.py 结论）+ 复跑 6 个门禁脚本。
**无法构建**（无 xmake/MSVC，TiltedCore 不在树内），因此所有结论均由静态推演得出；
凡是依赖 `TiltedPhoques::Buffer::Reader` 越界行为的结论，本文都给出两种分支，因为该行为
在整个仓库里都是未知的（`docs/PITFALLS.md:330` 明确记录在案）。

---

## 0. 结论

**这一轮修复在「游标对齐」这件事上是有效的，但同时引入了一处新的暴露面，并且只修了 3 个点中的 2.5 个。**

- 三个 Deserialize 的**线格式没有变**（逐位对齐，见 §2.1），**合法流量的行为逐字段一致**（§2.4）。
- 新循环的**越界写、未初始化读都不成立**（§2.3）。
- 但：为了让游标对齐，读侧从「见到超限计数立刻放弃」改成「按声明计数把条目全读掉」，
  于是**一个 2~3 字节的包现在能驱动 8191 / 8192 / 65535 次条目读取**，而这些条目在包里并不存在
  —— D1，新引入的越界读放大。
- `Factions` 的**超硬上限分支仍然「清空 + return 但不消费」**（D2），与本次修复的声明 1 直接矛盾，
  也与 `WireLimits.h:91-93` 自己写下的规则矛盾。
- **同一族缺陷在另外 5 处原样存在**（D3–D7），只是本轮没有碰它们。
- `CharacterService` 的上界守卫位置正确，但引入了 O(n) 扫描/请求的二次方开销，且漏掉了它自己注释里描述的那条泄漏路径（D8）。
- `DashboardHost` 的补常量是**正确的**，值与 Linux 头一致、单个 TU 内不会同时可见（§2.6）。
- 测试的缓冲区推导我按 Serialize 的位宽**重算过，够用**；哨兵也确实能区分「读满」与「读一半」（§2.7）。
  但两个敌意用例被改写成「必须自洽才能测」，于是**真正危险的「声明 N 条却实际没写」这种包彻底失去覆盖**（D9）。

**缺陷统计：13 条** —— P1×3（新引入，同一处代码 3 个实例）、P2×7（含 2 条修复不完整 + 5 条新发现同族）、P3×1、P4×1、P5×1（提示）。

---

## 1. 判定基线（先立规矩，再判代码）

仓库自己在 `Code/encoding/WireLimits.h:91-93` 写下了这一族缺陷的判据：

> `The check runs before the resize and rejects the message instead of clamping the count. A clamp leaves the reader at the end of the entries it kept while the stream still holds the rest, so every field after the list would be read from the wrong offset.`

由此得到两条可机械执行的判据，本报告全程使用：

- **R-1（嵌套性）**：一个 `Deserialize` 体内部若还有字段排在列表之后，则它**必须**把发送方写的每一条都读掉；
  只有「列表是该结构体最后一个字段」或「该结构体自己是顶层消息」时，提前 return 才无害。
- **R-2（上限性）**：读掉的条目数必须被**线上位宽**约束住，不能由对端宣布的数字无限驱动。

本次修复同时满足了 R-1 和 R-2 的「正常段」，但**只满足 R-1 的一半**（D2），且把 R-2 的紧迫性放大了一档（D1）。

---

## 2. 逐项核验（对应任务列出的 7 个重点）

### 2.1 线格式：手数结果 —— 三个都对齐 ✅

不依赖脚本，按 `Serialize` 与 `Deserialize` 的操作序列逐个对齐：

| 类型 | 写侧 | 读侧 | 判定 |
|-|-|-|-|
| Factions | `WriteVarInt(npcCount)` + n×(GameId:varint,varint + Bits8) + `WriteVarInt(extraCount)` + m×(同上) | `ReadVarInt` + n×(GameId.Deserialize + Bits8) + `ReadVarInt` + m×(同上) | ✅ 同序同宽同数 |
| Mods | `WriteBits(count,13)` + n×(Bits16, Bool, String) | `ReadBits(13)` + n×(ReadBits16, ReadBool, ReadString) | ✅ |
| QuestLog | `WriteBits(count,16)` + n×(GameId varint×2, Bits16) | `ReadBits(16)` + n×(GameId.Deserialize, ReadBits16) | ✅ |

细节核对：`Faction::Deserialize`(`Structs/Faction.cpp:19-26`) 与 `Faction::Serialize`(:13-17) 都是
`GameId + 8 bit Rank`；`GameId` 两侧都是「BaseId varint → ModId varint」（`Structs/GameId.cpp:27-37`）。
交叉核对门禁：`python Tools/Scripts/check_wire_symmetry.py` → `OK / 133 pair(s)`（仅作旁证）。
**结论：wire format 未变，本次改动是纯读侧重排。**

### 2.2 游标对齐：正常段成立，硬上限段不成立 ❌

- **Mods**（`Mods.cpp:49-69`）：`modCount` 来自 13 bit（≤0x1FFF），循环是 `i < modCount`，
  循环体内**三条读操作全部在 if 之前**（`ReadBits(16)` / `ReadBool` / `ReadString` + Clamp），
  只有赋值被 `if (i < keptCount)` 包住 → R-1 成立，无中途 return。
- **QuestLog**（`QuestLog.cpp:51-72`）：同上，`entry.Id.Deserialize` 与 `ReadBits(temp,16)` 都在 if 之外 → ✅。
- **Factions 普通段**（`Factions.cpp:66-76, 86-93`）：`entry.Deserialize` 对 kept 与 dropped 是同一处调用 → ✅。
- **Factions 硬上限段**（`Factions.cpp:58-64 与 78-84`）：**仍然 `clear()` 后 `return`，一条都不读** →
  **R-1 不成立**。见 D2。

### 2.3 越界 / 未定义行为：都不成立 ✅（逐条排除）

- **越界写**：`i < keptCount ? Entries[i] : dropped`（`QuestLog.cpp:66`）、`i < npcKept ? NpcFactions[i] : dropped`（`Factions.cpp:74,91`）、
  `if (i < keptCount) { ModList[i] = ... }`（`Mods.cpp:63-68`）——三处都先比较后索引，`i == keptCount` 时走 dropped 分支。
  `resize(keptCount)` 之后合法下标是 `[0, keptCount)`，判断与它一致。**无越界。**
- **clear() 与 resize() 的先后**：`Entries.clear(); Entries.resize(keptCount);`（`QuestLog.cpp:58-59`）
  等价于 `assign(keptCount, {})`，结果就是「恰好 keptCount 条」，复用对象与全新对象结果相同 —— 与原 `push_back` 语义等价。**无问题。**
- **`Entry dropped{}` / `Faction dropped` 是否读到未初始化字段**：
  `QuestLog::Entry` 是 `GameId Id; uint16_t Stage;`，`{}` 零初始化；`Faction::Deserialize` 无条件写 `Id`（两个 varint）与 `Rank`（`Faction.cpp:21-25`），
  `QuestLog` 的 `Id.Deserialize` + `Stage = temp & 0xFFFF` 也无条件覆盖。**两个字段都被完整覆盖，且 dropped 变量此后不再被读取**（不复用回容器）。
  `Faction dropped;` 未写 `{}`，成员确实是 indeterminate —— 但 `Faction::Deserialize` 无条件写满两个成员，**不构成未初始化读**。
  （若将来给 `Faction` 加一个「按 flag 跳过」的字段，这行就要跟着改；建议顺手写成 `Faction dropped{};` 消除隐患，纯风格项。）

### 2.4 行为回退：正常路径逐字段一致 ✅；「钳制 vs 丢弃」的语义**改了，且改对了方向**

- 合法发送方 `size <= kMax` → 写侧写 `min(size,kMax)`，读侧 `kept = min(count, kMax) = count` → 与改前 `resize(count)` 完全等价，**逐字段一致**。
- 改动只影响 `count > kMax` 的畸形包：改前「整列表清空 + 提前 return（后续字段错位）」，改后「保留前 kMax + 读完（后续字段对齐）」。
  这是从「丢弃且错位」变成「钳制且对齐」，方向正确，**没有把钳制改成丢弃、也没有反过来**。

### 2.5 CharacterService 上界：位置对，但有两个实质问题 ⚠️

- **守卫在 `m_world.create()` 之前**？是 —— `CharacterService.cpp:716-726` 在 `728` 的 create 之前 ✅。
- **已销毁但视图仍可见的 entity 会不会被计入**？不会。entt 3.10 的 `view<T>` 遍历的是组件池，`registry.destroy()` 会把实体从池中移除；
  本仓库唯一的三处 `destroy()`（`CharacterService.cpp:388,740`、`ObjectService.cpp:59`）都走 entt 正规路径。
  另外用的是 `view<CharacterComponent>()`，**不带 `entt::exclude`/`any` 组合、也没有在遍历中销毁**，不构成 entt 误用 ✅。
- **问题 1（P3）**：计数方式是**每次请求做一次全表线性扫描**（`716-720`），而 `CreateCharacter` 是客户端可反复触发的路径
  （`OnAssignCharacterRequest` → `CharacterService.cpp:264`）。填满 64K 实体的总代价是 `Σi ≈ 2·10⁹` 次迭代，
  且表满后**每次被拒绝的请求仍要扫一遍全表**。这是本次新增的 O(n²) 开销 —— 改前是 O(1)。
  entt 3.10 本来就有 O(1) 的 `m_world.storage<CharacterComponent>().size()`，无需自己数。
- **问题 2（P3）**：上界**漏掉了它自己注释里描述的泄漏路径**。`697-705` 的注释说「实体故意留着，失败时可能已被 SetCharacter 引用」，
  而 `emplace<FormIdComponent>/<OwnerComponent>/<CellIdComponent>`（`736,747,749`）都排在 `emplace<CharacterComponent>`（`756`）**之前**：
  其中任一抛异常，实体就留在了世界里但**没有 CharacterComponent**，于是永远不会被这条上界数到。
  上界数的是与 `ObjectService` 同构的「完整对象数」而不是「实体表大小」，与头文件注释承诺的语义（「the object table 的上界」）不一致。
- 附带（P5）：拒绝时只 `spdlog::error` 不同步任何响应，客户端会重发 —— 与上面 O(n) 扫描叠加。

### 2.6 DashboardHost：补常量是**对的** ✅

- Windows 头 `DashboardHost.h:45` 新增 `constexpr int kDashboardWindowUnavailable = 1;`，Linux 头 `DashboardHostLinux.h:50` 早已有同名同值定义（本次未改）。
- **两个头在任一 TU 内是否同时可见**：不是。全仓库只有三处 include —— `main.cpp:36/38` 由 `#if defined(_WIN32)` 二选一，
  `DashboardHost.cpp:1` 只含自己的头，`DashboardHostLinux.cpp:1` 同理。所以不存在重定义。
  （注：`constexpr` 在命名空间作用域是内部链接，**但同名重定义仍是硬错误** —— 若将来有一个 TU 同时 include 两者会直接编不过，这是可接受的脆弱性。）
- **值与 .cpp 返回值一致**：`DashboardHost.cpp:180,189` 都 `return kDashboardWindowUnavailable`（=1），Windows 行为与 Linux（`DashboardHostLinux.cpp:94,109,130,145`）完全一致 ✅。
- 修复的**必要性**也成立：`main.cpp:412` 的 `server_runner::kDashboardWindowUnavailable` 在该文件里来自 Windows 头，
  改前 Windows 头没有这个符号 → Windows 分支编不过。补在 Windows 头是正确落点（不是把常量搬到公共头）。
- 另外确认引用点被 `#if defined(SERVERUI_DASHBOARD_IMGUI)`（`main.cpp:317` … `:421 #elif`）包住，与 include 的守卫一致 ✅。

### 2.7 测试：缓冲区够，哨兵有效，但契约被改成了「只测自洽包」 ⚠️

**缓冲区大小自己重算（不采信注释）：**

- Mods 每个条目：`WriteBits(Id,16)` = 16 bit，`WriteBool` = 1 bit（即便按最保守的 8 bit 算），
  `WriteString("mod.esp")` = 长度 varint 1 字节(=8 bit) + 7 字节 = 56 bit ⇒ **81 bit ≈ 11 字节**。
  取 32 字节/条 ⇒ 8191 条需要 ≈ 90 KB，实际给了 `8191*32 = 262112` 字节。**够，且余量 2.9 倍** ✅
- QuestLog 每个条目：`GameId(1, index)` = varint(BaseId≤65534, ≤3 字节 = 24 bit) + varint(ModId=1, 1 字节 = 8 bit) + `Bits16` = 48 bit = **6 字节**。
  取 16 字节/条 ⇒ 65535 条实际 ≈ 393 KB，给了 `65535*16+8 = 1048568` 字节。**够，余量 2.6 倍**；
  且 1048568 ≤ `WireLimits::kMaxPacketBytes`(1048576)，注释里「这是真实对端可能交过来的包」成立 ✅
- 新增的哨兵与计数一起参与位流，未额外插入 padding，因此「写端偏移 == 读端应有的偏移」这一前提成立 ✅

**哨兵是否真能区分「读满」与「读一半」：**

- Mods：停在 `keptCount=4096` 时游标落在第 4097 条（id = 4096 = 0x1000）的 16 bit id 字段开头，
  取 13 bit 得 0x200（MSB 优先）或 0x1000（LSB 优先），**两者都 ≠ 0x5A5**。哨兵有效 ✅
- QuestLog：停在 4096 时游标落在第 4097 条的 **GameId varint 开头**（不是注释里说的 stage）；
  那里取 16 bit 落在 varint 里，取到 0x5A5 的概率可以忽略，且注释里"stage 4096"的说法**与真实位置不符**（D9 附注）。
  断言本身仍然成立，只是注释的推理链错了。

**问题（D9）**：两个敌意用例被改写成「包必须自洽才能断言」，注释里明说
「A packet that names a count it never fills is not something this case can assert against」。
但这恰恰是对手真正会发的包，而且正是 D1 那条新放大面的触发条件。**测试被改成贴合实现，而不是约束实现。**

### 2.8 其它旁证门禁（全绿，仅作参考，不能替代上面的手数）

`check_wire_symmetry` OK / `check_wire_field_reads` OK / `check_allocation_guards` OK /
`check_message_factories` OK / `check_gate_integrity` OK / `check_log_format_args` OK / `check_map_coverage` OK。
注意：**这些门禁没有一条能发现 D1/D2/D3–D7**，因为它们只比对同文件内的读写序列、
或只看「resize 前有没有比较」——而这几处的缺陷是「比较了、也 resize 了，但循环次数少于对端声明的次数」。

---

## 3. 缺陷清单

> 类型：**新引入** = 本轮改动带来的；**修复不完整** = 本轮本该修但没修干净的；
> **新发现** = 本轮没碰、但同族的既有缺陷；**提示** = 风格/可维护性。

| ID | 严重度 | 类型 | 位置 | 缺陷 |
|-|-|-|-|-|
| **D1a** | P1 | 新引入 | `Structs/Factions.cpp:72-76, 89-93` | 按声明计数全读，但条目数由对端的 varint 决定；包内没有这些条目时读越界 |
| **D1b** | P1 | 新引入 | `Structs/Mods.cpp:53-69` | 同上，13 bit 计数 ⇒ 最多 8191 条（改前超限即放弃，0 次读） |
| **D1c** | P1 | 新引入 | `Structs/QuestLog.cpp:64-72` | 同上，16 bit 计数 ⇒ 最多 65535 条（改前上限 4096 条，放大 16 倍） |
| **D2a** | P2 | 修复不完整 | `Structs/Factions.cpp:58-64` | `npcCount > 0x2000` 时 `clear()+return`，一条都不读 ⇒ 调用方的 LatestAction/QuestContent/FaceTints 从错位偏移解析 |
| **D2b** | P2 | 修复不完整 | `Structs/Factions.cpp:78-84` | `extraCount > 0x2000` 时同样提前 return，此时游标停在 extraCount varint 之后 |
| **D3** | P2 | 新发现 | `Structs/ActorValues.cpp:65-91` | 两个计数 `min(count,164)` 后只读 164 条；`ActorData` / `CharacterData` / `CharacterSpawnRequest` 在它后面还要读字段 ⇒ 同样的错位 |
| **D4** | P2 | 新发现 | `Structs/Inventory.cpp:140-149` | 条目计数 `min(count,0x1000)` 后只读 4096 条，紧接着读 `CurrentMagicEquipment` ⇒ 错位 |
| **D5** | P2 | 新发现 | `Structs/Inventory.cpp:72-92` | 附魔效果计数 `min(count,0x40)`，之后还有 ExtraHealth/ExtraPoisonId/... 5 个字段 ⇒ 错位（且外层是 Inventory 的条目循环，会级联） |
| **D6** | P2 | 新发现 | `Structs/ReferenceUpdate.cpp:47-54` | ActionEvents 计数 `min(count,0x400)`；外层 `Client/ServerReferencesMoveRequest` 的 map 循环紧接着读下一个 key ⇒ 级联错位 |
| **D7** | P2 | 新发现 | `Structs/AnimationVariables.cpp:134-158` | 三个计数都 `min(...,0x1000)`，随后按钳制后的位数读 Integers/Floats ⇒ 与发送方写下的变长尾巴错位（经 ActionEvent → Movement 进多个消息） |
| **D8** | P3 | 新引入 | `CharacterService.cpp:716-726`、`.h:89` | 上界用 O(n) 全表扫描实现 ⇒ 每次 CreateCharacter O(n)、填表总代价 O(n²)；且数的是「有 CharacterComponent 的实体」，**漏掉 716 行之前 emplace 失败留下的裸实体**，正是注释描述的泄漏路径 |
| **D9** | P4 | 测试 | `tests/encoding.cpp:1160-1217, 1880-1942` | 敌意用例被改成「必须自洽」；真正的攻击包（声明 N 条但没写）失去覆盖。另：QuestLog 哨兵注释把游标位置说成「stage 4096」，实际落在第 4097 条的 varint 开头 |
| **D10** | P5 | 提示 | `main.cpp:414-416`；两个 Dashboard 头 | (a) "The server was NOT started." 与事实不符（`core.Start()` 在 `:311` 已成功，随后 `RequestStop()`）；(b) 两个头各自定义同名 `kDashboardWindowUnavailable`，任一 TU 同时 include 即硬错误 |

### 重点展开

**D1（P1，本轮唯一「新引入」的实质问题）**

改前的三条早退路径合起来等于一层**放大器闸门**：见到超限计数立刻放弃，一次条目都不读。改后为了对齐游标，
这个闸门被拆掉了，于是同一个 2~3 字节的包现在要求读侧走完：

| 目标 | 读侧接受的声明上限 | 一条自洽条目最少占 | 全读要求的包长 | 改前读次数 |
|-|-|-|-|-|
| Factions | 0x2000（硬上限分支见 D2） | GameId 两 varint(1+1 B) + 8 bit = 24 bit | 8192 × 3 B = 24 KB | **0**（>0x1FF 即 return） |
| Mods | 0x1FFF（13 bit 全宽） | Bits16 + 1 bit bool + 空串 1 B 长度 = 25 bit | 8191 × 25 bit ≈ 25 KB | **0**（>4096 即 return） |
| QuestLog | 0xFFFF（16 bit 全宽） | 两 varint(1+1 B) + Bits16 = 32 bit | 65535 × 4 B ≈ 256 KB | 4096（改前就是钳制读） |

对照关系一句话：改前是「包不够大 ⇒ 一次都不读」，改后是「不管包多大 ⇒ 按对端说的数字读到底」。

也就是说：**一个 3 字节的包现在能让读取端做最多 8192 / 8191 / 65535 次「本该不存在」的条目解析**。
后果分两种，而仓库里无法判定是哪一种（`docs/PITFALLS.md:330`：`Buffer::Reader` 越界后的行为未知，源码随 xmake 拉取的 tiltedcore，不在树内）：

- 若 Reader **抛异常**：被各自的 `catch(...)` 吃掉，结果与改前相同（列表清空），只是白烧 CPU。可接受。
- 若 Reader **不检查边界**（memcpy / reinterpret 越界）：这就是一条**认证后即可触发、可从 0 次放大到 6.5 万次的越界读**，
  而且 `Serialization::ReadString` 的路径还会先用越界读来的长度去分配字符串（`Mods.cpp:58`）。

无论哪一种，「读多少次」的决定权从**包的实际长度**转移到了**对端写的一个整数**，这违反了 R-2 的精神。
改前的早退不是好方案（它导致对齐 bug），但改后的全读也不是安全的替代品——**正确的第三种做法是：先看 reader 还剩多少字节，用剩余长度反推能读多少条**，
剩余不足时按 R-1 放弃整包（因为此时发送方自己就没写全，不存在「对齐」可言）。

**D2（P2，与声明 1 正面矛盾）**

声明 1 说：「改为按声明计数把条目全部读完以保持游标对齐」。但 `Factions.cpp:59` 和 `:79` 仍然保留了
`clear(); return;` 的早退，只是阈值从 `kMaxFactionCount`(0x1FF) 抬到了 `kMaxFactionEntriesToSkip`(0x2000)。
后果是：`npcCount == 0x2001` 的攻击包，读侧一条都不读就返回，`AssignCharacterRequest` 紧接着从
「npcCount varint 之后」的位置去解 `LatestAction` / `QuestContent` / `FaceTints` / `CurrentActorData`
（`Messages/AssignCharacterRequest.cpp:48-59`）——**这正是本次修复要消灭的那种错位，原样保留了下来**。
而且它违反了仓库自己在 `WireLimits.h:91-93` 写下的规则。同一函数里两段代码遵守两套规则，是最容易被下一轮改坏的地方。

D3–D7 是同一族的新发现，判据完全相同（见 §1 的 R-1）：它们的计数都被 `std::min` 钳制，
而钳制**没有**被「读完丢弃」补上，于是结构体内后续字段（或外层循环的下一条）从错位偏移解析。
这 5 处与本轮修的 3 处是**同一个缺陷模板**，只是本轮只修了被点名的 3 个文件。

---

## 4. 建议修法（不实施，供决策）

1. **D1**：引入「剩余字节」判据而不是「声明计数」判据。例如在 `Buffer::Reader` 可用时取已读/总长，
   用 `min(declared, remainingBytes / minEntryBytes)` 决定能读多少；剩余不足时按 R-1 整包放弃（此时对齐无意义）。
   若无法拿到 remaining，则保留一个**硬上限 + 校验**（例如要求 `count * minEntryBytes <= 包长`）。
2. **D2**：把两处早退改成「读到 `kMaxFactionEntriesToSkip` 条就停，但**不 return**」——即在超硬上限时
   仍然把 `Faction::Deserialize` 走完、之后继续读 `extraCount`，只是两者都不保留。这样 R-1 在同一函数里统一。
3. **D3–D7**：套用本轮已经验证过的模板（`kept/dropped` 双目标），或给这 5 处补上「读到声明条数、只保留前 k 条」。
4. **D8**：改用 `m_world.storage<CharacterComponent>().size()`（O(1)）；若要真正兜住 716 行之前的失败路径，
   上界应改成对 `m_world.storage<OwnerComponent>().size()` 或 `m_world.view<OwnerComponent>().size_hint()` 判定，那是所有分支都会先 emplace 的组件。
5. **D9**：补一个「声明 0xFFFF 条但只写 3 条」的用例，断言 `Deserialize` 不崩、列表为空或受限即可——它才是 D1 的回归护栏。

---

## 5. 复核命令（照抄）

```bash
cd /d/sktest/SkyrimTogetherNext

# 1. 线格式（旁证，非结论）
python Tools/Scripts/check_wire_symmetry.py

# 2. 本轮改动面
git diff -- Code/encoding/Structs/Factions.cpp Code/encoding/Structs/Mods.cpp Code/encoding/Structs/QuestLog.cpp \
             Code/server/Services/CharacterService.cpp Code/server/Services/CharacterService.h \
             Code/server_runner/DashboardHost.cpp Code/server_runner/DashboardHost.h Code/tests/encoding.cpp

# 3. D2：Factions 里仍然「早退不消费」的两处
grep -n 'clear();' -A2 Code/encoding/Structs/Factions.cpp

# 4. D3-D7：所有「钳制计数后继续读」的嵌套结构体（把 min 后面的循环数与对端声明数对比）
grep -rn 'std::min<.*>(Serialization::ReadVarInt' Code/encoding/Structs/

# 5. D8：守卫在 create 之前（应看到 716-726 早于 728）
sed -n '710,732p' Code/server/Services/CharacterService.cpp

# 6. D10：两个头的同名常量与唯一 include 点
grep -rn 'kDashboardWindowUnavailable' Code/ | grep -v tests
grep -rn 'DashboardHost.*\.h"' Code/ --include=*.cpp
```

**复核结果**：#1 OK（133 对）；#3 命中 2 处（D2a/D2b）；#4 命中 `ActorValues.cpp:65,79`、
`Inventory.cpp:72,140`、`ReferenceUpdate.cpp:47`、`AnimationVariables.cpp:134-136`（D3–D7）；
#5 守卫位于 716-726、create 位于 728 ✅；#6 只有 `main.cpp:36/38`（`_WIN32` 二选一）、
`DashboardHost.cpp:1`、`DashboardHostLinux.cpp:1` 三个 include 点，两者不会同时可见 ✅。

---

## 6. 一句话交付

**该轮的 3 个反序列化器把「对齐」做对了、线格式没动、正常路径零行为变化；但代价是把「读多少条」的决定权交给了对端宣布的数字（D1，P1），Factions 的硬上限分支还把要修的那个 bug 原样留着（D2），另有 5 处同族缺陷未动（D3–D7），CharacterService 的上界位置正确但实现代价与覆盖面都有问题（D8）。DashboardHost 与测试的大小推导经复核是正确的。**