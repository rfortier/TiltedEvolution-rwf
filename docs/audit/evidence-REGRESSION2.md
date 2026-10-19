# 第二轮独立回归核验（协调者自做，替代被中断的复审计）

**说明**：我派出的最终独立复审计子代理运行超时且未产出，已中断。以下是我自己完成的针对性核验，
不是子代理报告。时间：2026-10-19。

---

## 1. 线格式对称性（逐位手工核对）

### PluginMessagingRequest.cpp
| 方向 | 操作序列 |
| --- | --- |
| SerializeRaw | WriteString, WriteBits(8), WriteVarInt, WriteVarInt, WriteBits(8)×N, WriteVarInt, WriteVarInt |
| DeserializeRaw | ReadString, ReadBits(8), ReadVarInt, ReadVarInt, ReadBits(8)×N, ReadVarInt, ReadVarInt |

**对称 ✅**。（初次自动提取时报告"DES 多一个 ReadBits"，经查是注释散文里出现的
单词 `ReadBits` 被正则误捕，非真实操作。人工逐行确认 7 对 7 完全对应。）

### 关键安全属性（我逐个确认）
- `claimedBytes` 以 **64 位**读出后再比较 `kMaxPayloadBytes`，然后才收窄 → 阻断 `2^32+10` 窄化绕过
- 剩余字节判据是**减法** `size > bufferSize - position`，且先判 `position > bufferSize` → 游标越界后不会误判
- `ReadBits` 返回值被检查 → 「读越界」与「读到 0」可区分（旧代码做不到）
- 三个失败路径都 `PluginData.clear()` 后 `return`，无半应用状态

## 2. 「丢包」站点容器清空（全量 12 处）

逐处核对「超限分支是否在 return 前清空容器」：

| 文件 | 结果 |
| --- | --- |
| Structs/ActorValues.cpp | 清两个 map ✅ |
| Structs/AnimationVariables.cpp | 清三个容器 ✅ |
| Structs/Inventory.cpp（条目 + 效果） | 各清各自 ✅ |
| Structs/Factions.cpp | 清两个 vector ✅ |
| Structs/Mods.cpp | 清 ModList ✅ |
| Structs/QuestLog.cpp | 清 Entries ✅ |
| Structs/ReferenceUpdate.cpp | 清 ActionEvents ✅ |
| Messages/{Notify,Request}{Actor,Max}ValueChanges (4) | 清 Values ✅ |
| Messages/{Client,Server}ReferencesMoveRequest | 清 Updates ✅ |
| Messages/{PluginMessagingRequest,NotifyPluginMessaging} | 清 PluginData ✅ |
| Messages/NotifyFactionsChanges | 清 Changes ✅（本轮补上） |

**12/12 全部有清空，无遗漏。**

## 3. DediRunner 缓冲区/预算正确性

- `m_stdinBudget` 只在 `while (m_stdinBudget != 0)` **内部**递减 → **不可能下溢**
- 每 tick 在 `TickOnce` 重置为 `kMaxCommandsPerTick(2)` → 单 tick 上传量 ≤ 2 < 4 槽 → 不会自旋
- `DrainStdinBuffer` 的 `consumed = newline + 1` 每轮严格前进 → `find` 必然推进 → **循环终止**
- `ReadStdin` 的**所有路径**都到达 `:180-181` 的 libuv 缓冲区释放（超限分支用 `else` 而非 `return`，
  这正是 FIX-Q6 用阴性对照拦下我错误指令的地方）
- 残留不丢：超出预算的行留在 `m_stdinPending`，由 `PumpPendingCommands` 在下一 tick 处理

## 4. es_loader 边界判据

- 全部写成减法 `bufferSize - position < n`，无一处加法（加法会在游标越界后回绕）
- `LoadFiles()` 仍不可达（`ESLoader.cpp:89` MakeUnique、`:92` 注释块未动）→ 未把不可达 bug 变成可达 bug

## 5. 门禁

修复前后均 **43 过 / 3 环境性失败**，失败项与原因完全一致（沙箱禁子进程管道 ×2、本机看不到 tag ×1）。
**零新增失败。**

## 6. 未覆盖（诚实披露）

- 无真实编译、无真实运行（除 `-fsyntax-only` 与自包含复刻程序）
- 无游戏/无服务器实机验证
- 被中断的复审计原本还要检查的项：`Inventory` 嵌套返回对外层循环的影响（FIX-Q2 已给出边界说明：
  `Entry::Deserialize` 是 `void+noexcept`，无法通知外层停止，但已把「376 次越界读 + 64 个伪造效果」
  降到「0 + 0」），我未再独立复算这一项。
