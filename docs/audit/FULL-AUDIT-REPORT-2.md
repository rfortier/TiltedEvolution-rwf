# 第二轮审计与修复报告

日期：2026-10-19
范围：用户在首轮报告后追加指定的三组缺陷 + 由此连带发现的一批同类缺陷。
方法：静态推理 + 46 个门禁脚本 + `g++` 自包含验证 + `-fsyntax-only` 真编译 + 独立子代理复审计。

---

## 0. 结论摘要

| 项 | 数 |
| --- | --- |
| 用户指定的缺陷组 | 3 组 |
| 实际修复的缺陷组 | **6 组**（含连带发现 3 组） |
| 改动文件 | 19 个（+805/−88） |
| 修复过程中**新引入**并被拦下的问题 | 3 个 |
| 修复过程中**我的错误判断**被证据纠正 | 3 次 |
| 门禁 | 修复前后均 **43 过 / 3 环境性失败**，零新增失败 |

---

## 1. 用户指定的三组

### 1.1 fast_queue 满队列死锁 —— 已修（分两轮才到位）
**根因**：`DediRunner::ReadStdin`（libuv 读回调）在处理一整块粘贴时，对**每个换行**调一次 `HandleConsole` → 每次 `Upload`；而队列只有 **4 槽**（`fast_queue<Item,2>`），唯一消费者 `ConsoleRegistry::Update()` 要等 `uv_run` 返回之后才跑。粘贴 5 行 ⇒ 第 5 次 `Upload` 在 `write_acquire` 无限自旋 ⇒ **服务器永久挂死**。`bConsole` 默认 true。

**修复**：
- `DediRunner.cpp:28` `kMaxCommandsPerTick = 2`，**预算按 tick 而非按回调**（实测按回调仍死锁）
- `ReadStdin` 只把字节追加进 `m_stdinPending`，由 `DrainStdinBuffer` 在预算内切行
- 新增 `PumpPendingCommands`，在 `uv_run` 之后、`m_console.Update()` 之前排空残留行（否则残留行永远等不到新读事件）
- `ConsoleRegistry::Update()` 改为排空循环（独立收益：修掉「单帧 2-4 条时剩余延迟到下一帧」的同类死锁）

**验证**（python 从仓库**原样抽取**函数体编译，对照 `git HEAD`，看门狗把自旋转成 exit 42）：
```
改前 n=5 → DEADLOCK exit=42 ; n=10 → DEADLOCK
改后 n=5 → 5/5 ; n=10 → 10/10 ; n=100 → 100/100   全 PASS
cap 边界：cap=1..4 PASS ; cap=5,8 DEADLOCK   ← 证明驱动的是真实 4 槽队列
```

### 1.2 es_loader 15 条越界缺陷 —— 已加固（路径仍不可达）
**前提核实**：`ESLoader.cpp:54` 直接 `return MakeUnique<RecordCollection>()`，`LoadFiles()` 在 `:56-61` 被注释掉 ⇒ 死路径。**保持不可达**。

**加固**：14 处「校验后中止」，覆盖记录头长度、GRUP size、记录体 size 解析前校验、`ReadZString` 有界扫描、`ReadWString` 长度、VMAD 两级计数、NVNM 有符号计数（负数转 `size_t` 直接 `resize`）、压缩块 `m_dataSize-4` 回绕、chunk 头长度等。

**附带收获**：取回上游 tiltedcore v0.2.9 源码，确定了 `Buffer::Reader` 语义 —— `GetBytePosition()`/`GetBuffer()->GetSize()` 存在（**剩余字节可求**）、`Advance` 不 clamp、`Eof()` 只答「还没到头」、`ReadBytes` 失败不移动游标。**这解除了 `docs/PITFALLS.md:330` 长期记录的「越界行为未知」。**

**边界判据铁律**：一律写减法 `bufferSize - position < n`；写加法会在游标越界后回绕成巨大值而误判「空间充足」。

### 1.3 5 处钳制错位 —— 已修为「超限即丢」，实际覆盖 11 处
用户列出 5 处，全仓 grep 后发现缺陷族共 **11 处**（`Structs/` 5 处 + `Messages/` 6 处），**全部修完**，无修一半留一半。

**判据**（逐处核实，不猜）：写侧是否用 `std::min` 夹取后才写计数。
- 11/11 处的写侧**都夹取** ⇒ 超限必为畸形包 ⇒ **丢包**是正确响应。
- 若某处写侧不夹取，丢包会误杀合法大包，此时应改为「读满丢弃」——本轮无此情况。

**为什么不选「读完丢弃」**（这是首轮我曾批准、后被回滚的方案）：
条目宽度由内容决定，读循环次数由**对端的计数**决定 ⇒ 2-3 字节的包能驱动上万次解析。`docs/PITFALLS.md:311-312` 早已记录同一结论：「**选 reject 而非 clamp**：clamp 会让 reader 停在保留项末尾而流里仍有剩余，后续字段全部错位」。**我最初批准「读完丢弃」时漏读了这条历史决策。**

---

## 2. 连带发现并修复的三组

### 2.1 PluginMessaging 越界读/静默填零（WS5-6）—— 已修
两处（`PluginMessagingRequest` / `NotifyPluginMessaging`）原本：夹取到 24 KiB 后**按 24 KiB 循环读**，`byte` 每轮重新置 0 且 `ReadBits` 返回值被丢弃 ⇒「读越界」与「读到 0」不可区分。

修复后三层判据：64 位比较超限 → 减法校验剩余字节 → 逐字节检查 `ReadBits` 返回值。

**验证中发现两个独立缺陷**：
- 旧代码先 `static_cast<uint32>` 再 `min`，`2^32+10` 会窄化成 `10` 被当合法小 payload 接受
- 逐字节 `ReadBits` 检查**不是冗余**：payload 起点常非字节对齐（`SenderIsHost` 只占 1 位），实测 `remaining=1` 时字节对齐读成功、位偏移 1 读失败

### 2.2 `m_stdinPending` 无界增长 —— 已修
1.1 的修复引入：每 tick 只消费 2 条，而 `m_stdinPending` 是只增不设上限的 `String` ⇒ 超大粘贴 OOM。
修复：`kMaxPendingStdinBytes = 64 KiB`，超限则**报错 + 清空**（不静默丢弃）。

**关键细节**：我要求「超限就 `return`」，但那会**跳过 libuv 缓冲区释放**（`DediRunner.cpp:180-181`）⇒ 每次被拒读泄漏一个 buffer，把「有界缓冲」换成「无界泄漏」，正是本修复要防的场景。子代理改为 `else` 并用阴性对照证明（早退版泄漏 59 个缓冲区，交付版 0 个）。

### 2.3 13 处过时注释 —— 已更新
本轮修复在多处注释里引用「`Buffer::Reader` 越界行为未知」，而该前提已被 1.2 解开。批量更新为「条目宽度由内容决定，剩余字节只能弱约束」，结论（reject 优于 consume）不变、依据更新。

---

## 3. 修复过程中新引入、并被拦下的问题（3 个）

| # | 问题 | 发现方式 |
| --- | --- | --- |
| 1 | 首轮「有界跳过」拆掉防放大闸门：3 字节包可驱动 8192 次读 | 独立复审计 → 已回滚 |
| 2 | `m_stdinPending` 无界增长 | 修复者自报 → 已修（2.2） |
| 3 | 我建议的 `return` 会泄漏 libuv 缓冲区 | 子代理用阴性对照拦下 |

---

## 4. 我的判断错误被证据纠正（3 次）

| # | 我的错误 | 实际 |
| --- | --- | --- |
| 1 | 死锁在 `ConsoleRegistry::Update()` 侧 | 在 `Upload()` 内，`Update()` 根本没被进入 |
| 2 | 建议复用 `m_tty.data` | `Context` 是 `StartTerminalIO()` 的局部 static，dashboard 路径不进入该函数 |
| 3 | 超限应 `return` | `return` 会跳过 libuv 缓冲区释放 |

**教训**：定位与修法必须由证据确定，不能由协调者推断确定。

---

## 5. 已知残留（诚实披露）

1. **「超限即丢」不消除错位，只消除伪造大数量攻击**。`Deserialize` 是 `void+noexcept`，没有失败通道；丢包对它是局部的，**调用方的后续字段仍从拒绝点继续读**。这与 `QuestLog` 处境相同。要彻底解决需给整个编码层加失败通道（波及上百条消息，回归风险远大于收益）。
2. **es_loader 仍是死路径**，本轮的加固只在重新启用时才有意义。
3. **所有修复未经真实编译/运行**（除 `-fsyntax-only` 与自包含复刻）。
4. `ParseValue` 数组分支的**递归深度无上界**未解决（需改签名，按约束停下报告）。
5. **ServerGui.cpp（无 imgui 时的 Win32 面板）未审计**。
6. `Code/encoding` 写侧 `WriteBits` 返回值未检查（`Resize` 失败会静默缺字节），记录未修。
