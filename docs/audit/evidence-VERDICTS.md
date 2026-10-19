# 复核判定记录（协调者逐条对照源码）

## WS-A（客户端线程/生命周期）
| ID | 判定 | 依据 |
| --- | --- | --- |
| WS-A-01 | **驳回（误报，且建议改法会引入 bug）** | `typings.d.ts:37-41` 与 `PITFALLS §14.3` 定义 isError = 「本次尝试失败（拒绝/握手超时）」。成功认证后会话掉线不是尝试失败，`isError=false` 正确。删 :857 会让上一次失败尝试的 true 残留到成功会话，**正常掉线反而触发自动重连**。:857-858 不是冗余：ArmConnectionWatchdog 在连接被发起时重置，而成功认证时也要把「重新武装」语义带回干净状态。不改。 |
| WS-A-02 | **接受【可闭合】** | HandshakeThreadMain 的 `if (!m_handshakeThreadStop.load()) m_handshakeThreadActive = false;`（:792-793）在 try 块末尾；catch(...)（:796-800）不清标志 → 该线程退出后标志永久 true → HandleUpdate :819 每帧提前 return → Update/看门狗/Send 全停摆。改为 catch 路径也清（或用作用域守卫）。 |
| WS-A-03 | **接受【可闭合】** | BeginConnect（:663-672）是 noexcept，无 try/catch，且 :669 在构造 std::thread 前就把 active 置 true。std::thread 构造抛 system_error → noexcept 里 terminate；即便兜住，活动标志也已置 true 而线程没起来（后果同 A-02）。改为加 try/catch 并在失败时回滚标志。 |
| WS-A-04..09 | 待逐条复核 | 见 .audit/WS-A.md |

## WS-B（引擎交互）
| ID | 判定 | 依据 |
| --- | --- | --- |
| WS-B-02 | **接受【可闭合】** | `Code/client/Games/Skyrim/TESObjectREFR.cpp:1137` `apThis->baseForm->formType` 未判空；仓库自身 `ObjectService.cpp:214/273/407` 均写 `baseForm && ...`，即认定可空。同族 `Actor::RemoveSpell`(:460) 有判空。修法：补 `apThis->baseForm &&`。**注意：报告里路径写成 Forms/TESObjectREFR.cpp，实际无 Forms/ 目录。** |
| WS-B-03 | **接受【可闭合】** | 同族判空缺失，待复核后统一处理 |
| WS-B-01 | **暂缓（依赖运行时，按约定不改）** | 越界前提依赖 HookFormAllocate 是否覆盖全部 actor 分配路径；TiltedCore 为空子模块、无游戏可跑，无法闭合。只报告。 |
| WS-B-04 | **暂缓（依赖运行时）** | VersionDbPtr::Get 的失败出口依赖地址库加载状态，本机无法验证。只报告。 |
| WS-B-06 | **接受【可闭合】** | HookAudit 用单字节 0xE9 判定钩子是否落地，失败会被误报成「与其他 mod 冲突」。属诊断正确性缺陷。 |


## FIX-E（Windows 编译错误）
| 项 | 判定 |
| --- | --- |
| 代码改动 | **通过**。DashboardHost.h 新增 `constexpr int kDashboardWindowUnavailable = 1;`；DashboardHost.cpp:180/188 两处裸 `1` 改为该常量。值恒为 1，运行时行为逐位不变；两平台头在任一 TU 内互斥，无重定义。 |
| 回归 | **通过**。48 个门禁重跑 = 43 过 / 3 失败，与最初基线完全一致，无新增失败。 |
| 注释准确性 | **退回**。新增注释称「caller can fall back to a console run」，但 main.cpp:412-419 只报错并 return，**不回退**；main.cpp:290-292 明写「There is no automatic downgrade any more」。DashboardHost.cpp:186 原有注释同样错误。已退回要求纯注释修正。 |

## FIX-C（编码越界）—— 通过
- Factions.cpp：判据由 `> kMaxFactionCount(0x1FF)` 改为 `> kMaxFactionEntriesToSkip(0x2000)`；<=0x2000 时有界跳过（保留前 kMaxFactionCount 条，其余读进栈上 `Faction dropped` 丢弃）→ 游标与写侧对齐。超 0x2000 保持原语义（清空后 return）。保留了原「保留/丢弃共用同一反序列化点」的正确结构。
- Mods.cpp：13 位计数天然有界，循环跑满 modCount 次、只保留前 kMaxModCount 条 → **完全正确**。
- 线格式、函数签名、noexcept、try/catch 全部未动。g++ 复刻验证：越界计数下修后 drift=0，修前 drift 上万且 trailer 读成垃圾；正常计数下与修前逐字段相等。
- 门禁：wire_symmetry / message_use / message_factories / log_format 全 PASS。
- **遗留**：Code/tests/encoding.cpp:1137-1177 的测试把改前行为写成契约，且敌意包本身不自洽（声称 8191 条却只写 4096 条）→ 已派 FIX-T 同步修正。

## FIX-D（服务端实体泄漏）—— 通过
- CharacterService.h:89 新增 `kMaxCharacterEntities = 64*1024`（对齐 ObjectService.h:37 的 kMaxObjectEntities）。
- CharacterService.cpp:716-726 守卫在 `m_world.create()`(:728) **之前**；只读遍历、不新增成员变量、循环体仅自增、函数仍 const noexcept。
- 核实：全文件唯一一处 `emplace<CharacterComponent>` 在 :756，故 `view<CharacterComponent>` 计数正好覆盖泄漏域；:740 的 destroy 发生在 emplace 之前，不重复计数。
- 门禁全 PASS（log_format 682→683，新增那行占位符与实参数一致）。
- **未解决（已记录）**：本修复只封顶不回收，已泄漏 entity 仍驻留；真正回收需改 GameServer.cpp 清理语义。
- **残余风险（需在最终报告写出）**：封顶后每次被拒请求仍做 O(n) 全表扫描，n 上限 64K；恶意高频请求在满表时仍有 CPU 成本。与本文件既有 O(n) 扫描模式同量级，不阻塞。

## FIX-T / FIX-Q（测试契约 + QuestLog）—— 通过
- FIX-T：只改 `[encoding.mods_count]`；敌意包从「声称 8191 只写 4096」改为真正写满 8191；断言改 `size()==kMaxModCount` + 尾部哨兵。反向对照证明哨兵能区分「读满」与「读一半」。
- FIX-Q：QuestLog.cpp 由「钳制计数只读 count 条」改为「读满声明计数、只保留前 kMaxEntryCount」，与 Factions/Mods 同策略；`push_back` 改 `resize(keptCount)` + 栈上 `Entry dropped{}`。单测同步修正。
- 门禁：wire_symmetry / message_use / message_factories / log_format / wire_field_reads / noexcept_allocations 全 PASS。

## 修复遗留边界（**只报告不改**，须写入最终报告）
- `Factions.cpp` 的第三个 `return`（`extraCount > kMaxFactionEntriesToSkip`，约 :78-83）发生在**已读完 `npcCount` 段之后**。此时游标已消费第一段，再 return 会让外层 `LatestAction`/`QuestContent` 从错误偏移继续读。
  - 这不是本次修复引入的退化（改前更早就会错位），但修复**没有闭合这个分支**。
  - 不修的理由：要真正闭合必须让 `Deserialize` 能向调用方报告失败（改签名/加通道，波及上百条消息），回归风险远大于收益；且该分支要求单条消息声明 > 8192 个 faction，属极端敌意输入。
  - 结论：**Bounded-skip 覆盖了 <= 0x2000 的全部现实攻击面；超出部分仍会错位，已如实记录。**

## 独立回归复审计结论（.audit/REGRESSION.md）—— 采纳并已分派回滚
### D1【P1，本轮修复引入的真实回归】—— 接受，已派 FIX-R 回滚
- 我的「有界跳过」把旧的「超限即 clear+return（0 次解析）」放大器闸门拆掉了，条目数仍由对端 varint 决定：Factions 3B→8192 读、Mods 3B→8191 读、QuestLog 2B→65535 读（改前分别 0 / 0 / 4096）。
- 决定性事实：三个 Serialize 都用 std::min 夹取后才写计数 ⇒ **合法发送方永不可能超限** ⇒ 超限必然是畸形包 ⇒ 丢包是正确响应，不存在需要「对齐」的正常情形。
- 且 Reader 越界行为未知（PITFALLS:330），全仓无 Remaining()/BytesLeft/Tell。
- **判断：错位本身就是丢包；我拿「受控丢弃」换成了「不受控读」，严格更糟。回滚。**
- 这是本轮审计最重要的自我纠错：修复不能在消除一个问题的同时放大另一个更危险的攻击面。
### D2【P2】Factions 硬上限分支仍不消费 —— 随 FIX-R 一并消除（回滚后所有超限分支统一为 clear+return）
### D3–D7【P2，新发现 5 处】同一模板：计数被 min 钳制但没「读完丢弃」
- 位置：ActorValues.cpp:65-91、Inventory.cpp:140-149、Inventory.cpp:72-92、ReferenceUpdate.cpp:47-54、AnimationVariables.cpp:134-158。
- **不修**，理由：与 D1 同一个两难——「读完」会放大，「钳制」会错位；真正解法需要 Reader 的剩余字节接口（不在树内）。
- **但注意**：这 5 处与 Factions/Mods **不是**同一情形 —— 它们没有「超限即丢弃」的闸门，钳制后继续读，错位是既有缺陷（非本轮引入）。已记录待 vendor tiltedcore 后统一处理。
### D8【P3】CharacterService 上界实现 O(n) —— 部分接受
- (a) O(n) 全表扫描：`CreateCharacter` 客户端可反复触发，满表后每次被拒仍扫全表。**接受，但不本轮修**：entt 3.10 的 `storage<C>().size()` 是 O(1)，但本机无法编译验证该 API 在此版本可用性，改动风险高于收益。**已记入最终报告作为已知改进项。**
- (b) 计数漏掉 :716 之前 emplace 失败留下的裸实体：**不成立** —— 我核实过 `create()` 之后唯一一处 `emplace<CharacterComponent>` 在 :756，且 :740 的 destroy 发生在其之前；但若 `OwnerComponent`(:748)/`CellIdComponent`(:750) 的 emplace 抛异常，确实会留下**无 CharacterComponent 的裸 entity**，不被计数覆盖。这是真实但极窄的 OOM 路径，**记录不修**。
### D9【P4】测试被改成「必须自洽」而失去对 D1 触发条件的覆盖 —— 接受，FIX-R 一并恢复
### D10【P5】提示项
- `main.cpp:414-416` "The server was NOT started." 与事实不符（`core.Start()` 在 :311 已成功）：**接受，随 FIX-E 的后续一并修正**。
- 两个 Dashboard 头各自定义同名 constexpr：**不成立** —— main.cpp 由 `_WIN32` 二选一，各 .cpp 只含自己的头，无 TU 同时可见（复审计自己也确认了这一点）。