# 全项目代码审计与修复报告

日期：2026-10-18
范围：`Code/` 全部自研代码（1105 个源文件）+ `Code/skyrim_ui` 前端 TS；8 个插件子模块只查接口边界。
方法：静态推理 + 46 个仓库门禁脚本 + `g++` 编译运行自包含验证程序。

---

## 0. 结论摘要

| 项 | 数 |
| --- | --- |
| 审计覆盖 | 6 个工作流，1105 个源文件 |
| 提交的发现 | 116 条 |
| 复核后**驳回**（误报） | 2 条 |
| 复核后**接受** | 114 条 |
| **已修复** | **4 类缺陷 / 7 个源文件** |
| 只报告未修（依赖运行时或需架构变更） | 110 条 |
| 本轮修复引入、被独立复审计抓出并回滚 | 1 条 |

**门禁基线**：审计前 43 过 / 3 失败；修复后 **43 过 / 3 失败，完全一致，零新增失败**。
三个失败均为环境性：`check_docs.py` 与 `check_workflow_shell.py` 因沙箱禁止子进程管道；`check_package_metadata.py` 因本机看不到本仓库 series tag。

---

## 1. 环境限制（决定了哪些结论能闭合）

- **无法完整构建**：无 xmake、无 MSVC、`~/.xmake/packages` 为空、`Libraries/` 下 TiltedReverse/TiltedUI/TiltedConnect/TiltedHooks 四个子模块是空目录。
- 因此 `TiltedCore/Buffer.hpp`、`glm`、CEF 头文件全部**不在树内**，涉及它们的行为无法静态确定。
- 可用：g++ 16、clang 21、Python 3.12、node 24。
- **所有修复均未经真实编译验证**（除 `DashboardHost.h` 的 g++ `-fsyntax-only`）。这是本报告的已知边界，不是隐瞒。

---

## 2. 已修复（4 类，7 个源文件）

### 2.1 Windows 发布配置编译失败 —— 已修
`Code/server_runner/DashboardHost.{h,cpp}`

`main.cpp:412` 在两个平台分支**汇合之后**引用 `server_runner::kDashboardWindowUnavailable`，而该常量只定义在 `DashboardHostLinux.h:50`；Windows 分支只 `#include "DashboardHost.h"`，后者没有这个常量。**Windows 默认发布配置（`xmake.lua:83`，GUI 面板是 README 主推功能）直接编不过。**

修复：Windows 头补同值常量，并把 `.cpp` 两处裸 `1` 改为该常量以免漂移。

> 验收时发现注释与事实不符（称「会回退到控制台」，而 `main.cpp:412-419` 只报错退出、`main.cpp:290` 明写「不再自动降级」），已退回纯注释修正。

### 2.2 加密层消息错位（部分回滚）—— 见 §4
### 2.3 QuestLog 消息错位 —— 已修
`Code/encoding/Structs/QuestLog.cpp`

改前 `std::min(count, kMaxEntryCount)` 后**只读 count 条**，而两个调用点后面都还有字段：
- `AssignCharacterRequest.cpp:51` → 紧接 FaceTints / IsDragon / IsMount / IsPlayerSummon / CurrentActorData / LeveledNpcPickId
- `CharacterData.cpp:51` → 紧接 FaceTints / AllActorValues / IsDead / IsWeaponDrawn

钳制即错位，且消息**仍被当作合法消息分发**（`DeserializeRaw` 是 `void+noexcept`，无失败通道）。
改为**超限即丢包**（0 次条目解析），并同步修正 `tests/encoding.cpp` 的配套用例。

### 2.4 服务端实体表无上界泄漏 —— 已修
`Code/server/Services/CharacterService.{h,cpp}`

`CreateCharacter` 是同族**唯一没有容量上界的实体创建点**（`ObjectService.cpp:115` 有 `kMaxObjectEntities=64K`）。
断线清理（`GameServer.cpp:683-691`）只销毁 `pPlayer->GetCharacter()` 指向的**那一个** entity，玩家每换一个 `ReferenceId` 就泄漏一个，且无回收路径。

修复：加 `kMaxCharacterEntities = 64*1024` 守卫，位置在 `m_world.create()` **之前**；只读遍历、不新增成员变量、保持 `const noexcept`。

> **已知局限（不修）**：只封顶不回收，已泄漏 entity 仍驻留；且用 O(n) 全表扫描实现，满表后每次被拒仍扫全表。entt 3.10 的 `storage<C>().size()` 可 O(1) 但本机无法编译验证。

---

## 3. 复核中驳回的误报（2 条）

### 3.1 WS-A-01 建议删除 `TransportService.cpp:857` —— **驳回，且该改法会引入 bug**
子代理认为成功认证时清 `m_attemptErrorReported` 会丢失失败原因。但：
- `typings.d.ts:37-41` 与 `PITFALLS §14.3` 定义 `isError` = 「**本次尝试**失败（服务器拒绝或握手超时）」；
- 一次成功认证后会话掉线，**本次尝试并没有失败**，`false` 是正确行为；
- 删掉这行会让上一次失败尝试的 `true` 残留到成功会话，**正常掉线反而触发自动重连**。

### 3.2 D10 两个 Dashboard 头同名常量 ODR 冲突 —— **驳回**
`main.cpp:36/38` 由 `_WIN32` 二选一，各 `.cpp` 只含自己的头，无 TU 同时可见。

---

## 4. 本轮修复引入、经复审计抓出并已回滚的回归（重要）

**D1：读侧放大。** 我最初批准的「有界跳过」修法（把 Factions/Mods 改为「按对端声明计数读完以对齐游标」）**拆掉了一道安全闸门**。

决定性事实链：
1. 三个 `Serialize` 都用 `std::min(size, kMax)` **夹取之后才写计数** ⇒ 合法发送方**永远不可能**写出超限计数；
2. ⇒ 「计数 > 上限」**必然**是畸形/恶意包，丢包本就是正确响应；
3. 改后条目数仍由对端 varint 决定：Factions 3 字节包 → 8192 次读、Mods 3 字节 → 8191 次、QuestLog 2 字节 → 65535 次（改前分别 0 / 0 / 4096）；
4. 而 `Buffer::Reader` **越界行为未知**（`docs/PITFALLS.md:330` 明确记录；全仓 `Remaining()/BytesLeft/Tell` 零命中）。

**判断**：旧代码的「错位」本身就伴随丢包（消息作废）；我用「受控丢弃」换成了「不受控读」，严格更糟。**已回滚**：`Factions.cpp` 与 `Mods.cpp` 现与 HEAD 代码逐行等价，`kMaxFactionEntriesToSkip` 已删除；`QuestLog.cpp` 保留「超限即丢」这一处真实改进。

**教训（已写入代码注释）**：修复不能在消除一个问题的同时放大另一个更危险的攻击面。当 `Reader` 的越界语义未知时，任何「循环次数由对端决定」的写法都不可接受。

---

## 5. 只报告未修的重要发现（按优先级，共 110 条，摘录）

### 运行时可闭合但本机无法验证（按约定不动代码）
- **WS-B-01【高】** `Actor::GetExtension()`（`Games/Skyrim/Actor.cpp:308-337`）用 `static_cast<ExActor*>(this)`，而扩容只按两个精确 `sizeof` 分支做；19 处消费点含写操作 ⇒ **越界写**。是否成立取决于 `HookFormAllocate` 是否覆盖全部 actor 分配路径。
- **WS-B-04【高】** `VersionDbPtr::Get()`（`VersionDb.h:556-598`）有两条失败出口返回裸 `nullptr`，13 个 `POINTER_SKYRIMSE` 调用点直接当函数指针调用。
- **WS-D-08【高】** `GameServer::Send`（`GameServer.cpp:709-723`）每条出站消息构造 `Buffer buffer(1 << 20)`；一次广播 = n 次 1 MiB 分配，`ProcessMovementChanges` 50 Hz。
- **WS-D-04【中】** `ModsComponent` 两张表无上限、`refCount` 全仓零自减；登记循环在 `GameServer.cpp:1021` 位于所有拒绝路径**之前**，默认配置（空密码、ModPolicy 关）下任何拿到 BUILD_COMMIT 的连接都能无限写入。

### 静态可证但需架构变更
- **D3–D7**：`ActorValues.cpp:65-91`、`Inventory.cpp:140-149`、`Inventory.cpp:72-92`、`ReferenceUpdate.cpp:47-54`、`AnimationVariables.cpp:134-158` —— 计数被 `std::min` 钳制但未「读完丢弃」，结构体后续字段从错位偏移解析。**与本轮回滚同源**：真正解法需要 `Reader` 的剩余字节接口（不在树内）。
- **WS5-3【高】** `GridCellCoords::IsCellInGridCell`（`:58-65`）`abs(INT32_MIN)` 为 UB，伪造坐标可绕过范围判定实现全图可见。已用 g++ 复现。
- **WS5-6【中】** `PluginMessagingRequest.cpp:47-56`/`NotifyPluginMessaging.cpp:44-52`：`ReadBits` 返回值被丢弃，「读越界」与「读到 0」不可区分。
- **WS-F CTN-01【高，已实测】** `fast_queue.hpp:98-109` 满队列无限自旋，容量 4 槽，读写同线程 ⇒ 粘贴 5 行控制台命令即可让服务器**永久挂死**。`bConsole` 默认 true。
- **WS-F UI-01【高】** `OverlayClient.cpp:37-39` 在派发前无条件读 `eventArgs` 的 `[0][1][2]`，16 个桥接函数中 8 个零参数 ⇒ 越界读。
- **WS-F ESL-01【致命但死路径】** es_loader 15 条越界/未校验长度缺陷，全部落在 `ESLoader.cpp:54` 提前 return 的死路径上（`LoadFiles()` 被注释掉）。**重新启用前必须先修。**

---

## 6. 验证证据

| 项 | 证据 |
| --- | --- |
| 门禁 | 审计前后均为 **43 过 / 3 失败**，失败项与原因完全一致 |
| Factions/Mods 回滚 | 剥离注释后与 `HEAD` 逐行等价；`kMaxFactionEntriesToSkip` 全仓零命中 |
| QuestLog | 代码层仅一处改动：钳制 → 超限即丢 |
| encoding.cpp | 仅 3 个 hunk，全部落在两个指定 TEST_CASE 内；+26/−11 |
| DashboardHost.h | g++ `-fsyntax-only` 通过；常量值在 Windows 头、Linux 头、`.cpp` 返回值三处一致 |
| CharacterService | 守卫行号 < `create()` 行号；全文件唯一 `emplace<CharacterComponent>` 在守卫之后 |

---

## 7. 未覆盖区域（诚实披露）

1. **所有修复未经真实编译**。仅 `DashboardHost.h` 做过 `-fsyntax-only`。
2. `TiltedCore`（`Buffer::Reader` 越界语义、`TaskQueue` 容量、`Client::Close` 回调时机）不在树内 —— 这是最大缺口，多条结论悬在它上面。
3. `Code/client/Games/` 下 300+ 文件的钩子体未逐行审（WS-B 只覆盖了指定文件）。
4. `DashboardHostLinux.cpp` 全文未读（本机 Windows 不编译）。
5. 前端 81 个 TS 只审了 9 个。
6. WS-D 的 `CharacterService.cpp:510-690`、`PartyService`、`InventoryService`、`QuestService` 部分段落未审。
7. 无游戏可运行 —— 所有「运行时依赖」结论均未在实机验证。

---

## 8. 过程中的两个工程事故（已修复，记录备查）

1. **子代理批量失败**：第一波 5 个审计子代理全部中途失败且无收尾消息（范围过大）。改为「明确文件清单 + 每条发现立即落盘到 `.audit/`」，第二波全部完成。
2. **`tools.read` 静默截断**：一个修复子代理写入时被 `tools.read` 返回的不完整内容截断了 `encoding.cpp`（1933 → 1502 行，无截断信号），它自行用 `git show HEAD` 逐字节恢复并主动上报。**教训：大文件回写前必须核对 `lines.length` vs `totalLines`。**
