# WS-D — 专用服务器逻辑审计

范围：`Code/server/**`（PlayerManager、GameServer、Services、Scripting 绑定、admin/`admin_protocol`）。
审计方式：纯静态（本机无 xmake/MSVC，无法构建）；结论逐条以「文件:行号 + 关键代码」为证据。
已排除：`docs/PITFALLS.md` §27/§32/§76/§104/§106/§133 与 `docs/audit/WS1..WS4` 已记录的条目（不重复上报）。

---

## 缺陷总表（19 条）

| ID | 严重度 | 类别 | 一句话 |
| --- | --- | --- | --- |
| D-01 | 中 | 可闭合 | `PlayerLevelRequest` 无范围校验，且每条都 `SendToPlayers`（n 倍放大） |
| D-02 | 中 | 可闭合 + 运行时 | `OwnerComponent` 的 `pOwner`/`InvalidOwners` 在玩家断开后无清理点 |
| D-03 | 中 | 可闭合 | 重复 `AssignCharacterRequest` 反复新建**玩家角色**实体，无上限无回收 |
| D-04 | 中 | 可闭合 | `ModsComponent` 两张表按包内文件名增长，登记在所有拒绝路径之前，无回收 |
| D-05 | 低 | 可闭合 | `PlayerManager::Remove(Player*)` 不判空 |
| D-06 | 低 | 可闭合 | admin 协议服务端无接收路径（整段被注释），`AdminService` 是死代码 |
| D-07 | 中 | 可闭合 | 入站只有「单包 1 MiB」一道门，无任何每连接速率/积压上限 |
| D-08 | 高 | 运行时 | 每条出站消息构造 1 MiB `Buffer`；一次广播 = n 次 |
| D-09 | 中 | 运行时 | 断线重连后旧实体仍带旧 `PlayerId`/`pOwner` |
| D-10 | 高 | 可闭合 | `CharacterService::CreateCharacter` 是唯一没有容量上界的实体创建点 |
| D-11 | 中 | 可闭合 | `m_seed` 32 位→`uint16_t` 截断，65536 个 mod 后 id 撞车 |
| D-12 | 中 | 可闭合 | 服务端两处 `ScratchAllocator` 从未被 `ScopedAllocator` 激活 |
| D-13 | 中 | 运行时 | 断线玩家的 actor 只走所有权转移，无按玩家回收路径 |
| D-14 | 低 | 可闭合 | `OnConsume` 与 handler lambda 之间没有异常边界（当前无触发路径） |
| D-15 | 低 | 运行时 | `OnPartyChangeLeader` 在两次查询之间解引用 `Player*` |
| D-16 | 低 | 可闭合 | `PlayerManager::ForEach` 标 `noexcept` 却调任意 functor（唯一调用者在分配） |
| D-17 | 低 | 可闭合 | `GetByUsername` 两个重载的 const 限定符写反 |
| D-18 | 低 | 可闭合 | `OnCharacterRemoveEvent` 的 view 不含 `CharacterComponent` |
| D-19 | 低 | 运行时 | 修 D-12 时必须同时回答 `Reset()` 与 `Server::Send` 的生命周期关系 |

**最严重三条：D-10、D-08、D-04。** 三者的共同点是「对端能决定服务端分配多少，
而服务端没有任何一条回收路径」，只是入口不同（实体 / 出站缓冲 / 全局 mod 表）。

---

## 关于起点提示「任意玩家一条聊天消息即可打崩专用服务器」是否真修好

**结论：聊天这一条具体路径确已修好；但同一类缺陷（任何已连接玩家 → 解空 optional → 段错误）在范围内仍有 1 处存活。**

已修好的证据（逐条核实，不是读注释）：

| 检查点 | 代码 | 结论 |
| --- | --- | --- |
| 解析不再按「一行聊天」截断 | `Code/encoding/Messages/SendChatMessageRequest.cpp:16` `WireLimits::Clamp(ChatMessage, kMaxChatMessageBytes)`（4096+512） | ✅ 与 `WireLimits.h:47` 一致，信封不会被腰斩 |
| 聊天处理不再解空 optional | `Code/server/Services/OverlayService.cpp:88` `if (const auto character = acMessage.pPlayer->GetCharacter())` | ✅ §32.1 那种「取到即解引用」已消失 |
| 广播路径不依赖 character | `OverlayService.cpp:54-60` 只在 `kLocalChat` 分支内判 `character` | ✅ 无角色时全局/队伍聊天仍可工作，也不崩 |
| 玩家对话同形 | `OverlayService.cpp:103-114` → `sendPlayerMessage(kPlayerDialogue, ...)`，同样走 42 行判空 | ✅ |
| 名字与正文都过 HTML 剥离 | `OverlayService.cpp:39-40` `ChatSanitize::StripHtmlTags` | ✅ `ChatSanitize.h:27-75` 单遍扫描，不抛 |

**但下面 D-01 是同一类缺陷的存活实例（`PlayerService::OnPlayerLevelRequest` 没有可解引用的 optional，但
`PlayerService::OnPlayerRespawnRequest` 的 `*character` 已在 `GameServer.cpp:680-694` 与
`PlayerService.cpp:161-167` 判过；真正的存活实例是 D-01 的实体失效路径）。**

> 说明：本轮**没有**找到第二条「一条聊天包/对话包即可打崩」的路径。曾经最像的那一条
> （`OverlayService.cpp:108` 直接 `sendPlayerMessage`）在 `sendPlayerMessage` 内已判空，安全。

---

## 【可闭合】缺陷

### D-01 · 严重度：中 · `PlayerService::OnPlayerLevelRequest` 对 `NewLevel` 没有任何范围校验，且每条都 `SendToPlayers`（D-07 的最短放大样例）

位置：`Code/server/Services/PlayerService.cpp:217-226`
```cpp
217  void PlayerService::OnPlayerLevelRequest(const PacketEvent<PlayerLevelRequest>& acMessage) const noexcept
218  {
219      acMessage.pPlayer->SetLevel(acMessage.Packet.NewLevel);
220
221      NotifyPlayerLevel notify{};
222      notify.PlayerId = acMessage.pPlayer->GetId();
223      notify.NewLevel = acMessage.Packet.NewLevel;
224
225      GameServer::Get()->SendToPlayers(notify, acMessage.pPlayer);
226  }
```
问题：`NewLevel` 完全由发送方决定（`Code/encoding/Messages/PlayerLevelRequest.cpp:12`
`NewLevel = Serialization::ReadVarInt(aReader) & 0xFFFF;`），而这一段
（a）**没有速率限制**、（b）**没有取值范围**、（c）**每条都向全体玩家群发**。
条件：任何已认证玩家按该 opcode 发一个 3 字节包。
后果：`O(n)` 条 `NotifyPlayerLevel` 每次发送给所有玩家，服务端 CPU 与出口带宽都随发包速率线性增长，
且服务端记录的 `Player::m_level` 可以被任意改成 65535（随后作为 `NotifyPlayerJoined.Level`
发给每个新加入的玩家，见 `GameServer.cpp:1097`）。
证据补充：全仓唯一的速率限制是 `PluginMessagingService.h:46-47` 的令牌桶
（`kBurstCapacity=256`/`kRefillPerSecond=128`），聊天/等级/对话/传送四条路径都不经过它。
最小改法：比照 `WireLimits::ClampExperienceDelta` 的做法，给 `PlayerLevelRequest` 一个
`kMaxPlayerLevel`（如 0x200）并在 `DeserializeRaw` 里钳制；同时把 `SendToPlayers` 换成
「值未变化就不广播」。

---

### D-02 · 严重度：中 · `OwnerComponent` 的 `pOwner` 与 `InvalidOwners` 在玩家断开后**没有任何清理点**

位置：`Code/server/Components/OwnerComponent.h:25-27`、`Code/server/GameServer.cpp:689-703`、
`Code/server/Services/CharacterService.cpp:272-357`、`:998-1041`
```cpp
// OwnerComponent.h:25-27
Player* pOwner;
uint32_t OwnershipEpoch;
Vector<const Player*> InvalidOwners{};

// CharacterService.cpp:344-351（唯一写 InvalidOwners 的地方）
344          // A normal release starts a fresh search. A declined grant continues the current
345          // search, retaining failed candidates so unloaded clients cannot bounce ownership.
346          if (message.Reason == OwnershipReleaseReason::Relinquish)
347              ownerComponent.InvalidOwners.clear();
348
349          ownerComponent.InvalidOwners.push_back(acMessage.pPlayer);
350
351          TransferToNextOwner(cEntity, OwnershipTransferReason::Relinquish);
```
**静态事实（已逐条核实）：**

1. `InvalidOwners` 的 `push_back`（`:349`）**只在 `Reason == Relinquish` 时先 clear**。
   `Reason == DeclineGrant`（`:301` 明确允许它通过）直接追加，**没有 `std::find` 去重**。
2. 清理点只有 `TransferOwnership(..., aResetInvalidOwners=true)`（`:975-976`），
   而 `TransferToNextOwner` 调它时**显式传 `false`**（`:1030`）。
   `OnOwnershipTransferEvent`（`:359-367`）会 clear 一次，但那只在「断线」这一条事件上跑。
3. `GameServer::OnDisconnection`（`:677-694`）**只遍历 `view<OwnerComponent>` 并按
   `GetOwner() == pPlayer` 判断**，对 `InvalidOwners` **一个字都没提**，
   随后 `:703` 把 `pPlayer` 删掉。
   ⇒ `InvalidOwners` 里**遗留的 `const Player*` 在玩家断开后成为悬垂指针**。

**可达性分级（这一条我按两半写，因为强度不同）：**

- **【可闭合】`InvalidOwners` 无界增长：** `RequestOwnershipTransfer` 是每包一条的
  （`Message` 无速率门，仅受 `WireLimits::WithinPacketLimit` 约束），
  而 `OwnerComponent` **没有容量上限**。`DeclineGrant` 路径每次追加一个 8 字节指针，
  只要目标 actor 的 ownership 再次回到该玩家（或该玩家一直是 owner 而
  `TransferToNextOwner` 找不到别的候选者），就能持续追加。
  `docs/PITFALLS.md` §76.2 对同族的 `ObjectService` 用的判据是
  「先问这张表什么时候变小」——`InvalidOwners` 的答案是「只在一次成功的
  全量转移时」，而 `TransferToNextOwner` 恰恰把它关掉了。
- **【运行时依赖】`pOwner` 悬垂：** 正常路径下 `:696` 的 `dispatcher.update()`
  会把 `:692` 排队的所有 `OwnershipTransferEvent` **同步**抽干，
  `TransferToNextOwner` 要么把 `pOwner` 改成一个活着的玩家，
  要么在无候选者时 `:1034-1035` `trigger(CharacterRemoveEvent)`（`trigger` 是同步的）
  ⇒ 实体被销毁。所以 **`pOwner` 悬垂只在失败路径上成立**：
  `TransferOwnership` 因 OOM 返回 `false`（`:991-995`）、
  `TransferToNextOwner` 的 `catch`（`:1037-1040`）、
  或 `view.find` 失败（`:1010-1011`）。这三种情况下实体保留
  `pOwner == pPlayer`，随后 `:703` 删掉 `pPlayer`，下一次
  `TransferOwnership` `:960` 的 `pOldOwner->GetId()` 就是 UAF 读。

**证据边界（诚实标注）：** 我最初想把 `pOwner` 悬垂报成主缺陷，逐行推演后**收窄了**：
entt 的 `enqueue`/`update` 组合保证了正常路径的同步性，所以主路径不成立。
保留它的理由是「失败路径的写法和成功路径一样」，而不是「现在就能崩」。

最小改法（一处即可覆盖两半）：
`OnDisconnection` 在 `:703` 之前遍历 `view<OwnerComponent>`，
对每一项执行 `if (oc.GetOwner() == pPlayer) oc.SetOwner(nullptr);` 并从
`InvalidOwners` 里 `erase(remove(...))` 掉 `pPlayer`；
同时给 `InvalidOwners` 在 `:349` 的追加前加一次 `std::find` 去重（与
`TransferToNextOwner` `:1023` 的判据对称）。

---

### D-03 · 严重度：中 · `CharacterService::CreateCharacter` 对「玩家角色」这条唯一路径**没有「已经有过角色」的检查**

位置：`Code/server/Services/CharacterService.cpp:697-803`（关键：`:713`、`:777-787`）
```cpp
713      const auto cEntity = m_world.create();
...
777      if (isPlayer)
778      {
779          const auto pPlayer = acMessage.pPlayer;
780
781          pPlayer->SetCharacter(cEntity);      // 直接覆盖，没有"已有"判断
782          pPlayer->GetQuestLogComponent().QuestContent = message.QuestContent;
783          characterComponent.PlayerId = pPlayer->GetId();
784
785          auto& dispatcher = m_world.GetDispatcher();
786          dispatcher.trigger(PlayerEnterWorldEvent(pPlayer));
787      }
```
问题：`OnAssignCharacterRequest`（`:201-265`）在 `isCustom == true`（玩家角色
`refId == {0, 0x14}` 命中 `:211`，或临时 form 命中 `:212`）时**完全跳过**「找已有实体」
那段（`:215-261` 只在 `!isCustom` 时执行），直接落到 `:264 CreateCharacter(acMessage)`。
`CreateCharacter` 内部对 `isPlayer` 分支也不看 `pPlayer->GetCharacter()` 是否已有值。
条件：已认证玩家重复发送 `AssignCharacterRequest` 且 `ReferenceId = {ModId:0, BaseId:0x14}`。
后果：每包新建一个 `OwnerComponent + CharacterComponent + InventoryComponent +
ActorValuesComponent + MovementComponent + AnimationComponent + CellIdComponent` 的实体
（`:721-774` 共 6~7 次 `emplace`），`Player::m_character` 覆盖成最新那个，
**前一个实体永久留在 registry 里**（唯一的销毁点是 `CharacterRemoveEvent`，
而它只会被 `TransferToNextOwner` 或玩家自己 relinquish 触发）。
`uMaxPlayerCount` 只封顶玩家数（`GameServer.cpp:914`），**不封顶实体数**。
这与 `docs/PITFALLS.md` §76.1 记录的「对象实体表无上界」是**同一类**，但走的是另一条路径
（§76 记的是 `AssignObjectsRequest`），此处是**玩家角色**路径，且连「单包 255 个」这种
天然节流都没有。
最小改法：`CreateCharacter` 在 `isPlayer && acMessage.pPlayer->GetCharacter()` 为真时
直接复用/回绝（`spdlog::warn` + return），或至少在 `OnAssignCharacterRequest` 的
`isPlayer` 分支前加一次「已有角色则走原实体」的短路。

---

### D-04 · 严重度：中 · `ModsComponent` 的两张表按**包内声明的 mod 名**增长，登记发生在所有拒绝路径**之前**，且无任何回收点

位置：`Code/server/GameServer.cpp:1021-1033` + `Code/server/Components/ModsComponent.cpp:5-61`
```cpp
// GameServer.cpp:1021-1033（认证成功后，逐个 mod 登记）
1021     for (auto& mod : acRequest->UserMods.ModList)
1022     {
1023         const uint32_t id = mod.IsLite ? modsComponent.AddLite(mod.Filename)
1024                                        : modsComponent.AddStandard(mod.Filename);
...
1032         responseList.ModList.push_back(entry);
1033     }
```
```cpp
// ModsComponent.cpp:20-31
20       const auto id = m_seed++;
22       try { m_standardMods.emplace(acpFilename, Entry{id, 1}); }
26       catch (...) { ... }
31       return id;
```
问题：`m_standardMods` / `m_liteMods`（`ModsComponent.h:36-37`）**没有容量上限、没有
`erase` 调用点、没有按玩家解引用计数**。`Entry::refCount`（`ModsComponent.h:17`）只在
`:10`/`:39` 自增，**全仓零处自减**（已 grep 确认：`refCount` 只有这两个 `++`）。
而写入量的上界由 `Mods::kMaxModCount = 4096`（`Mods.h:57`）× `kMaxFileNameBytes = 260`
（`WireLimits.h:56`）给出，即**单次认证最多 4096 × 260B ≈ 1 MiB 的新字符串**，且名字
每包可以不同（表按 `String` 去重，只有同名才会命中已有行）。
条件：一个**已通过密码校验**（`GameServer.cpp:942`）但**尚未被接纳入服**的连接发 `AuthenticationRequest`，
每次换一批文件名。触发第二次认证的前提是 `PlayerManager::Create` 返回 null
（`:1041-1048`）——**但那一层的 `return` 发生在 `:1021` 的登记循环之后**，
所以「重复认证被拒」并不能阻止表增长；而 mod 策略不匹配（`:1010`）与脚本拒绝加入
（`:1062-1064`）这两条路径**在表已经增长之后**才 return，且 `:1063` 还会把玩家行删掉。
后果：进程生命周期内单调增长的 `Map<String, Entry>`，无上限，无日志。
证据边界：单连接重复认证受 `PlayerManager::Create` 返回 null 限制，但**同一个真实玩家
可以重连**（每次重连是新的 `ConnectionId_t`，玩家行是新的），而调用被删掉的玩家行
（`:1063`）之后连接仍活着 —— 这两条都能让同一个客户端持续写这张表。
**默认配置下这条没有门槛（这是本条最重要的一半）。**
`GameServer.cpp:36` 的密码默认值是空串：
```cpp
36  Console::StringSetting sPassword{"GameServer:sPassword", "Password players ...", ""};
```
而 `:942` 的判定是 `if (acRequest->Token == sPassword.value() || adminPasswordUsed)`
—— 空 == 空 为真。`ModPolicy` 默认也是关的（`:50 bEnableModCheck{..., false}`），
所以 `:953-1013` 那一段 mod 策略检查整个被跳过，但 `:1021-1033` 的登记循环**照常执行**。
⇒ **默认配置的专用服务器上，任何拿到 BUILD_COMMIT 字符串（公开 release 里就有）
的连接，只要连上并重复发 `AuthenticationRequest`，就能无限写入这两张表，
一次都不需要真的进服。** 单次认证最多 4096 条 × 260 字节。
最小改法：给两张表一个 `kMaxModEntries`（如 4096）并在 `AddStandard/AddLite` 超限时
返回一个固定的哨兵 id + `spdlog::warn`；或把登记循环挪到所有拒绝路径之后
（即 `Create` 成功之后再登记）——**后者更彻底**：它同时关掉「未入服也能写状态」这条面。

---

### D-05 · 严重度：低 · `PlayerManager::Remove(Player*)` 完全不判空，且是全仓唯一「传指针进来再取 id」的删除接口

位置：`Code/server/Game/PlayerManager.cpp:66-69`
```cpp
66  void PlayerManager::Remove(Player* apPlayer) noexcept
67  {
68      m_players.erase(apPlayer->GetConnectionId());
69  }
```
问题：`apPlayer` 未判空即解引用。当前两个调用点（`GameServer.cpp:703`、`:1063`）
都在 `pPlayer` 非空的块内，**现状可闭合为「无触发路径」**；但该函数是 `public` 且
被 `PlayerManager` 暴露给 Lua 绑定层（`Scripting/Player_Bindings.cpp:44-57` 把
`PlayerManager` 作为 usertype 暴露，`Remove` 虽未绑定，但同族的
`GetByConnectionId`/`GetById` 已绑定，后续加绑定是常规做法）。
另外它与 `PlayerManager` 其余全部接口的行为不一致：所有 getter 都判空返回 `nullptr`，
只有这里直接解引用；而同文件的 `Create` 已经为「分配失败」写了 `try/catch`
（`:44-63`），说明本文件的约定就是「绝不因外部输入崩」。
后果：一旦出现 `Remove(nullptr)`，属于无 catcher 的段错误（`noexcept` 不挡段错误）。
最小改法：`if (!apPlayer) return;` 一行。

---

### D-06 · 严重度：低 · admin 协议在服务端**没有接收路径**，`AdminService::HandleShutdown` 是一条死代码却仍然挂着 dispatcher sink

位置：`Code/server/GameServer.cpp:595-609`（整段被 `/* */` 注释）、
`Code/server/Services/AdminService.cpp:11-29`、`Code/admin/Transport.cpp:61-69`
```cpp
// GameServer.cpp:595-609
    // TODO: ClientAdminMessageFactory
    /*if (m_adminSessions.contains(aConnectionId)) [[unlikely]]
    {
        const ClientAdminMessageFactory factory;
        ...
        m_adminMessageHandlers[pMessage->GetOpcode()](pMessage, aConnectionId);
    }
    else
    {*/
```
问题：`OnConsume`（`:610-637`）**只走 `ClientMessageFactory`**，`m_adminMessageHandlers`
（`GameServer.h:130`）由 `BindMessageHandlers`（`:290-303`）填满却**零调用点**。
因此 `AdminShutdownRequest` 永远到不了 `AdminService::HandleShutdown`，
`GameServer::Kill()` 的这条「管理员远程关机」路径实际不可达。
与此同时 `AdminApp::OnConnected`（`Transport.cpp:61-69`）往连接上发的是
**`AuthenticationRequest`（游戏协议）**，而服务端 `:906` 的版本检查会先把它踢掉
（`Version` 为空 ≠ `BUILD_COMMIT`）—— 也就是说 admin 客户端**永远连不上**。
后果：不是崩溃，是「功能整体不可用且没有任何日志说明」。更实际的风险是
`AdminService.cpp:31-54` 那个一直挂着的 spdlog sink：注释已写明它不做事
（`(void)msg;`），但它证明这条链路的清理只做了一半。
最小改法：要么删掉 `AdminService` + `m_adminMessageHandlers` + `admin/` 整个 target，
要么把 `:595-609` 恢复并把 admin 客户端的握手换成 `AdminSessionOpen` 语义；
在两者之间，「一半挂着的死代码」是审计负担。

---

### D-07 · 严重度：中 · `GameServer::OnUpdate` 与 `OnConsume`/三个 handler 之间无任何玩家级速率/积压上限；只有「单包大小」一道门

位置：`Code/server/GameServer.cpp:558-573`、`:575-639`
```cpp
558  void GameServer::OnUpdate()
559  {
560      const auto cNow = std::chrono::high_resolution_clock::now();
...
569      dispatcher.trigger(UpdateEvent{cDeltaSeconds});
570
571      if (m_requestStop)
572          Close();
573  }
```
问题：`OnConsume` 的唯一门是 `WireLimits::WithinPacketLimit(aSize)`（`:586`，1 MiB）与
「opcode 有 handler」（`:624-635`）。**没有任何每连接速率、每连接积压深度、
每帧最多次数的限制**。全仓唯一的限流是 `PluginMessagingService.h:46-47`。
条件：任何已连接玩家以线速发包。**最省事的一条是聊天**（也正是起点提示里那句话的
残留部分）：
`SendChatMessageRequest`（`MessageType = kGlobalChat = 1`）→
`OverlayService::HandleChatMessage`（`OverlayService.cpp:70-101`）→
`sendPlayerMessage`（`:29-68`）→ `:46 SendToPlayers(notifyMessage)`。
这条链**只做字符串剥离与复制，没有任何计数、冷却或丢弃**，
所以 `docs/PITFALLS.md` §32 修好的「崩」变成了「放大」而不是「消失」。
后果：`SendToPlayers`（`:750-757`）是 `O(n)` 次 `Send`，而 `Send` 每次
**新建 `Buffer buffer(1 << 20)`**（`:713`）—— 1 MiB 堆分配 + `thread_local`
`ScratchAllocator{1 << 18}` 重置（该 arena 从未激活，见 D-12）。
**一条 ≤ 4608 字节的聊天包 ⇒ n 次 1 MiB 分配 + n 份 ≤4608 字节的出站拷贝**，
放大倍数 ≈ n × 1024 / 4.6 ≈ 220n（n = 在线人数）。
纯聊天之外的放大样例见 D-01（每条 3 字节包换 n 条通知）。
证据边界：`Buffer(1 << 20)` 的分配是否真的每次落到 `malloc` 取决于 TiltedCore 的
`Buffer` 实现（`Libraries/TiltedCore` 本机未检出），因此**容量放大倍数**属于
【运行时依赖】；但「每条广播包都构造一个 1 MiB 的 `Buffer` 对象」是静态可证的。
最小改法：把 `Buffer`/writer 提到 `thread_local` 复用（`:711` 那个 `s_allocator`
的意图就是这个，但没有复用到 `buffer` 上），并给 `OnConsume` 加一个每连接
「每秒最多 N 条」的令牌桶（`PluginMessagingService` 的桶可以直接借用）。

---

## 【运行时依赖】缺陷

### D-08 · 严重度：高 · `Buffer buffer(1 << 20)` 在每条出站消息上构造；一次广播 = n 次 1 MiB 分配

位置：`Code/server/GameServer.cpp:709-723`（`Send(ConnectionId, ServerMessage)`）、
`:725-739`（同形的 admin 版）
```cpp
709  void GameServer::Send(const ConnectionId_t aConnectionId, const ServerMessage& acServerMessage) const
710  {
711      static thread_local TiltedPhoques::ScratchAllocator s_allocator{1 << 18};
712
713      Buffer buffer(1 << 20);
714      Buffer::Writer writer(&buffer);
715      writer.WriteBits(0, 8);
716
717      acServerMessage.Serialize(writer);
718
719      TiltedPhoques::PacketView packet(reinterpret_cast<char*>(buffer.GetWriteData()), static_cast<uint32_t>(writer.Size()));
720      Server::Send(aConnectionId, &packet);
721
722      s_allocator.Reset();
723  }
```
问题：`s_allocator`（`1 << 18` = 256 KiB 的 scratch）在本函数里**只被 `Reset()`，
从未被 `ScopedAllocator` 激活**（对照 `Code/admin/Transport.cpp:91`
`TiltedPhoques::ScopedAllocator _\{s_allocator\};` 的用法，那里是对的）。
所以 `Buffer buffer(1 << 20)` 的 1 MiB 走的是**普通堆**。
条件：任何一次广播（`SendToPlayersInRange` 在 `CharacterService::ProcessMovementChanges`
里每 20 ms 一次、每条可见实体一次）。
后果：n 个玩家 × 每条消息 1 MiB 的分配/释放；在 8 人默认配置下，一次
`ProcessMovementChanges` 的广播就是 8 次 1 MiB 分配，而它 50 Hz 跑。
证据边界：`TiltedPhoques::Buffer` 的构造函数是否真的 `malloc(1 << 20)`、
`ScratchAllocator` 未激活时是否退化为普通堆 —— 两者都在 `Libraries/TiltedCore`
（本机未检出）。**这条的成立条件必须实测这一条**：把 `Buffer` 的构造改成计数即可。
最小改法：`TiltedPhoques::ScopedAllocator _\{s_allocator\};` 一行（与 `admin/Transport.cpp:91`
完全同形），并把 `buffer` 提成 `thread_local`。

---

### D-09 · 严重度：中 · 断线重连时 `PlayerManager` 行是新的、但**实体与所有权状态是旧的**

位置：`Code/server/GameServer.cpp:647-707`（`OnDisconnection`）、
`Code/server/Services/CharacterService.cpp:1034-1035`
```cpp
// GameServer.cpp:698-703
698      // Same lifetime as the player row: the messaging service keys its
699      // per-sender rate-limit buckets by PlayerId, ...
701      m_pWorld->GetPluginMessagingService().OnPlayerRemoved(pPlayer->GetId());
702
703      m_pWorld->GetPlayerManager().Remove(pPlayer);
```
问题：`OnDisconnection` **只清理了 `PluginMessagingService` 的桶**（`:701`），
没有清理 `OwnerComponent::InvalidOwners`（见 D-02）、没有清理
`CharacterComponent::PlayerId`（`CharacterService.cpp:783` 写入的旧 `Player::m_id`）、
也没有清理 `PartyService::m_parties` 里可能残留的 `Player*`（`RemovePlayerFromParty`
在 `PlayerLeaveEvent` 里跑，`:666`，先于 `:703` 的 `Remove`，这一点是对的）。
条件：玩家 A 断线，其角色实体在 `TransferToNextOwner` 里被交给 B（或留在原地等 A 回来），
然后 A 用**新的连接**重连并重新 `AssignCharacterRequest`。
后果：旧角色的 `CharacterComponent::PlayerId` 仍是 A 的旧 `m_id`，
而 `Player::GenerateId`（`Code/server/Game/Player.cpp:6-12`）是单调递增的
`atomic<uint32_t>`，**不会复用** ⇒ 旧实体永远指着一个不存在的 player id，
任何按 `PlayerId` 反查的消费者都会拿到 nullptr；
同时旧实体仍带着 `OwnerComponent::pOwner`（见 D-02）指向已释放的 `Player`。
证据边界：是否存在「按 `CharacterComponent::PlayerId` 反查」的消费者 ——
已 grep，目前**没有**（`NotifyPlayerJoined.Level` 走的是 `Player::GetLevel`），
所以这条**当前不产生可见故障**，只在 D-02 修好前后是同一片债。
最小改法：与 D-02 合并修：`OnDisconnection` 里一次性把该玩家在
`OwnerComponent` / `CharacterComponent::PlayerId` 里的痕迹清掉。

---

## 已确认无问题的关键不变量

1. **玩家容器的遍历安全性。**
   `PlayerManager::Iterator`（`PlayerManager.h:9-26`）只做 `++` 与比较，
   所有 29 处 `for (Player* pPlayer : GetPlayerManager())` 都**只在体里 `Send`/读**，
   没有一处调用 `Remove`。全仓 `Remove` 只有 2 个调用点（`GameServer.cpp:703`、`:1063`），
   都在 `OnDisconnection` / 认证失败的非遍历上下文里；`:696` 的 `dispatcher.update()`
   会把排队事件先抽干再 `:703` 删行。**未发现迭代器失效。**

2. **服务端分发表不做空槽调用。**
   `GameServer.cpp:624-635`：先判 `cOpcode >= kClientOpcodeMax`，再判 `!handler`，
   两者都有 `spdlog::error` 并 `return`。`admin/Transport.cpp:44-56` 同形。
   与 `docs/audit/WS3-protocol.md:141-146` 记录的「visitor 53 项 == 枚举 53 项」一致。

3. **按玩家声称的 id 做身份判定已全部移除。**
   `CommandService.cpp:53-54` 与 `:76-79` 用 `acMessage.pPlayer->GetConnectionId()`
   查 `GetAdminSessions()`，不用 `Packet.PlayerId`；`PartyService.cpp:232` 用
   `pParty->LeaderPlayerId == player->GetId()`；`PartyService.cpp:237` 校验踢人目标
   与操作者同队；`PluginMessagingService.cpp:77` 用 `pSender->GetId()` 而非载荷里的 id。
   **未找到「普通玩家可触发管理操作」的路径**（`AdminService` 那条路本身不可达，见 D-06，
   结论方向相反：管理操作整体不工作）。

4. **字符串与计数的上界在入站路径上齐备且一致。**
   `WireLimits.h` 的 11 个 `Clamp` 调用点逐个核对过：
   `AuthenticationRequest.cpp:37/39/42`、`SendChatMessageRequest.cpp:16`、
   `PlayerDialogueRequest.cpp:14`、`TeleportCommandRequest.cpp:14`、
   `PluginMessagingRequest.cpp:38-39`、`Mods.cpp:60`、`AssignCharacterRequest.cpp:43`。
   `Mods::Deserialize`（`Mods.cpp:47-51`）与 `PluginMessagingRequest.cpp:49-50`
   都是「超限丢整条/整列表」而不是半截读取，不会让 reader 与 writer 失步。

5. **认证失败路径对 `PlayerManager` 无副作用 —— 但对 `ModsComponent` 有。**
   `GameServer.cpp:906-918`（版本）、`:920-937`（SKSE/MO2）、`:998-1012`（mod 策略）、
   `:1042-1048`（重复认证）四条拒绝路径都只 `return`，**不写 `PlayerManager`**；
   `:1059-1065`（脚本拒绝）会 `Kick` + `Remove`，成对。`m_adminSessions` 的
   中间态是已知条目（`docs/PITFALLS.md` §32.2），本轮未新增同类。
   **唯一不满足的是 `ModsComponent` 的两张表** —— 登记循环（`:1021-1033`）
   位于 `:998` 的 mod 策略拒绝与 `:1042` 的重复认证拒绝**之前**，所以
   「被拒绝」不等于「没写状态」，见 **D-04**。

6. **`ScriptService` 的 Lua 边界是完整的。**
   `ScriptService.inl:14-44`/`:52-70` 两个模板都在体内 `try/catch`，
   `ScriptService.cpp:252-263` 的 `ReportFailure` 自己不分配；
   `HandlePlayerJoin`/`HandleChatMessage`/`HandleSetTime` 等 6 个
   `noexcept` 入口**没有一处在 try 块外构造 `String`**（`ScriptService.h:49-55` 的
   注释与代码一致，参数是 `const char*`）。

7. **`admin/Transport.cpp:25-56` 与 `GameServer.cpp:586-635` 是同一个正确形状：**
   先查包大小、再查 opcode 范围、再查空槽 —— 三处接收入口（含客户端）已由
   `docs/audit/WS3-protocol.md` 判过，本轮复核无回归。

---

## 未覆盖区域

### 只做了索引级浏览（读过签名/前 60 行，未逐条推演）
1. `Code/server/Services/CharacterService.cpp` **`:408-430`、`:510-690`**（约 200 行：
   `OnCharacterSpawned`、`OnMountRequest`、`OnNewPackageRequest`、`OnRequestRespawn`、
   `OnSyncExperienceRequest`、`OnDialogueRequest`、`OnSubtitleRequest`）。
   已完整读过 `:80-155`、`:201-357`、`:369-406`、`:430-508`、`:697-803`、
   `:930-1050`、`:1140-1236`。
2. `Code/server/Services/` 其余：`QuestService`（`:20-258` 已读，`:258-406` 的
   `OnQuestSceneChanges` 尾部与 `QuestStageDedupHistory` 未读）、
   `CalendarService`（已全读）、`CombatService`（已全读）、`MagicService`
   （`:24-118` 已读，构造与头文件未读）、`MapService`（已全读）、`WeatherService`
   （已全读）、`ActorValueService`（已全读）、`InventoryService`
   （`:29-160` 已读，`:160-188` 未读）、`ObjectService`（`:20-190` 已读，
   `:190-219` 的 `OnScriptAnimationRequest` 未读）、`PartyService`
   （`:60-140`/`:155-330`/`:400-600` 已读，`:1-60`/`:330-400` 未读）、
   `StringCacheService`（`WS3` 已记无界增长，本轮未重读）、`AdminService`（已全读）、
   `CommandService`（已全读）、`ScriptService`（已全读）。
3. `Code/server/Components/` 全部（只读了 `ModsComponent`、`OwnerComponent`、
   `CharacterComponent` 的字段区）。
4. `Code/server/Scripting/` 全部绑定层（只读了 `Player_Bindings`、`GameServer_Bindings`
   前 40 行、`PartyService_Bindings` 的签名）。
5. `Code/admin/` 的 Magnum/ImGui 交互（`Overlay.cpp` 全读，`AdminApp.cpp` 全读，
   `Main.cpp` 3 行）。**`Overlay.cpp:27/35` 的
   `m_widgets[static_cast<size_t>(m_activeWidgetID)]` 在 `m_activeWidgetID == WidgetID::COUNT`
   时越界，但 `m_toggled` 只由 UI 置位、`SetActiveWidget` 在 `:86` 有守卫
   —— 未能证明可达，故不上报。**
6. `Code/encoding/` 的其余消息体（只按需读了与本范围直接相关的 12 个）。

### 因本机缺依赖而无法核实（不是「没看」，是「看不到」）
1. `Libraries/TiltedCore` **未检出**（`find . -iname "*tiltedcore*"` 零命中；
   `xmake.lua` 里是 `add_requires("tiltedcore 0.2.9")`）。因此以下四条**全部无法核实**，
   D-08/D-12/D-19 的「运行时依赖」定性即来自这里：
   - `TiltedPhoques::Buffer` 的构造函数是否真的 `malloc(1 << 20)`；
   - `ScratchAllocator` 未被 `ScopedAllocator` 激活时是否退化为普通堆；
   - `Server::Send(ConnectionId_t, PacketView*)` 是同步拷贝还是排队（决定 D-19）；
   - `Server::Kick` 是否**同步**触发 `OnDisconnection`（决定 §32.2 那条已知缺陷的窗口，
     也决定 D-15 的窗口宽度）。
2. `entt` 版本：仓内用的是 `entt 3.10`（`docs/PITFALLS.md` §27.3 记录），
   `dispatcher.enqueue` / `update` 的同步语义按该版本的公开行为推演，
   源码不在本机。

### 方法论限制（必须写明）
**本机无法构建**（无 xmake、无 MSVC、`~/.xmake/packages` 为空、`Libraries/` 四个子模块是空目录）。
全部 19 条结论都是**静态读数**，没有一条经过编译或运行验证。
D-01/D-04/D-07/D-11 的「放大倍数」「多少包越线」是算术推演而非实测。
凡我无法把证据链闭合的地方，都写进了该条的「证据边界」段，而不是把结论说得比证据强。

## 补充轮次（第一批之后新增，均为逐行核实）

> 本节每条标题后标 `【可闭合】` / `【运行时依赖】`，与前两节同一分类口径。

### D-10 · 严重度：高 · 【可闭合】 `CharacterService::CreateCharacter` 是**唯一**没有容量上界的实体创建点（`ObjectService` 有 64K 上限，它没有）

位置：`Code/server/Services/CharacterService.cpp:713` vs `Code/server/Services/ObjectService.cpp:115` / `ObjectService.h:37`
```cpp
// CharacterService.cpp:713（无任何计数/上限）
713      const auto cEntity = m_world.create();
...
721      m_world.emplace<FormIdComponent>(cEntity, gameId.BaseId, gameId.ModId);   // 仅 !isCustom
732      m_world.emplace<OwnerComponent>(cEntity, acMessage.pPlayer);
734      auto& cellIdComponent = m_world.emplace<CellIdComponent>(cEntity, message.CellId);
741      auto& characterComponent = m_world.emplace<CharacterComponent>(cEntity);
760      auto& inventoryComponent = m_world.emplace<InventoryComponent>(cEntity);
763      auto& actorValuesComponent = m_world.emplace<ActorValuesComponent>(cEntity);
768      auto& movementComponent = m_world.emplace<MovementComponent>(cEntity);
774      m_world.emplace<AnimationComponent>(cEntity);
```
```cpp
// ObjectService.cpp:115-122（同族的正确形状）
115      else if (objectCount >= kMaxObjectEntities)
116      {
117          ++refusedCount;
118      }
119      else
120      {
121          const auto cEntity = m_world.create();
122          ++objectCount;
// ObjectService.h:37
37      static constexpr std::uint32_t kMaxObjectEntities = 64 * 1024;
```
问题：`kMaxObjectEntities` 是 `docs/PITFALLS.md` §76.4 为 `ObjectService` 补的上界，
**同一个 commit 没有给 `CharacterService::CreateCharacter` 补**。全仓 `m_world.create()`
只有两个调用点（已 grep 确认：`CharacterService.cpp:713`、`ObjectService.cpp:121`），
**只有后者有上限**。
条件：任何已认证玩家反复发 `AssignCharacterRequest`，每次换一个
`ReferenceId`/`FormId`（这两项完全由客户端填）。`OnAssignCharacterRequest` 在
`!isCustom` 时先按 `FormIdComponent.Id == refId` 线性查找（`:220-227`），
**换 id 就必然落空** ⇒ 走 `:264 CreateCharacter` ⇒ 每次新建 7 个组件。
后果：每个实体约 `OwnerComponent(含 vector) + CharacterComponent(含 SaveBuffer/
FaceTints/FactionsContent) + InventoryComponent(可含 4096×64 effects，见
`Inventory.h:84-85`) + ActorValuesComponent + MovementComponent + AnimationComponent`，
且 `AppearanceBuffer` 上限 32 KiB（`WireLimits.h:64`）。
**单包即可让服务端多分配一份几十 KiB 到数 MiB 的常驻内存**，且没有任何销毁路径
（`CharacterRemoveEvent` 只在 `TransferToNextOwner` 找不到候选者时触发，`:1035`）。
旁证：`docs/PITFALLS.md` §76.2 为同族的对象表写的判据 ——「这张表只有一条回收路径，
而它由『房间里还有人吗』决定」——**逐字适用于这里**，甚至更糟：对象表至少有
`OnPlayerLeaveCellEvent`（`ObjectService.cpp:31-60`）会清空一个 cell，而
`CharacterService` 里没有任何等价的「玩家离开就回收」的清理。
最小改法：把 `kMaxObjectEntities` 抽成共享常量，在 `CreateCharacter` 的
`m_world.create()` 之前加一次 `m_world.view<CharacterComponent>().size() >= kMaxCharacterEntities`
的判断并 `spdlog::warn` + `return`（与 `ObjectService` 的 refusedCount 分支同形）。

---

### D-11 · 严重度：中 · 【可闭合】 `ModsComponent::m_seed` 是 `uint32_t`，而 id 在**三处**被截断成 16 位

位置：`Code/server/Components/ModsComponent.h:34`、`Code/server/GameServer.cpp:1027`、
`Code/encoding/Structs/Mods.h:12-14`
```cpp
// ModsComponent.h:34
34      uint32_t m_seed = 0;

// GameServer.cpp:1025-1028
1025            Mods::Entry entry;
1026            entry.Filename = mod.Filename;
1027            entry.Id = static_cast<uint16_t>(id);      // 32 位 → 16 位，无检查
1028            entry.IsLite = mod.IsLite;
```
问题：`m_seed` 32 位、单调递增、**永不回绕保护**（`ModsComponent.cpp:20`/`:49`
都是裸 `m_seed++`），而发给客户端的 `Mods::Entry::Id` 是 `uint16_t`。
越过 65535 之后，两个不同的 mod 会拿到**相同的 16 位 id**。
条件：服务端在整个进程生命周期里累计登记超过 65536 个**不同的** mod 文件名。
在 D-04（表无上限、无回收）成立的前提下，这不是理论值：一个客户端每次认证换 4096 个
文件名（`Mods.h:57 kMaxModCount`），**17 次认证就能越线**。
后果：客户端 `ModSystem::HandleMods`（`Code/client/Systems/ModSystem.cpp:105-127`）
按 `mod.Id` 填 `m_serverToGame`（`:114`/`:119`，后写覆盖先写）与
`m_standardToServer[pMod->GetId() & 0xFF] = mod.Id`（`:120`）。
id 撞车 ⇒ **两个 mod 的 form id 前缀映射到同一条**，后续每一个由它派生的 form id
都会指向错误的插件。`docs/PITFALLS.md` §104.2 已经为**同类**写过判据：
「两个 mod 共用一个 id，客户端由它派生的每一个 form id 都会撞车，比『这一条没去重』坏得多」
—— 那次防的是「分配失败后返回 0」，这次防的是「分配成功但被截断」。
最小改法：把 `m_seed` 改成 `uint16_t` 并在 `== 0xFFFF` 时停止发新 id（返回
`Mods::kRemoveMarker` 级别的哨兵并 `spdlog::error`），或在 `GameServer.cpp:1027`
先判 `id > 0xFFFF` 再决定是否写入。

---

### D-12 · 严重度：中 · 【可闭合】 `GameServer::Send` 的 `ScratchAllocator` 声明了却**从未激活**，1 MiB 的 `Buffer` 因此每次走普通堆

位置：`Code/server/GameServer.cpp:709-723`、`:725-739`；对照组 `Code/admin/Transport.cpp:80-106`
```cpp
// GameServer.cpp:709-723（server 版：s_allocator 只 Reset，从不 ScopedAllocator）
709  void GameServer::Send(const ConnectionId_t aConnectionId, const ServerMessage& acServerMessage) const
710  {
711      static thread_local TiltedPhoques::ScratchAllocator s_allocator{1 << 18};
712
713      Buffer buffer(1 << 20);
714      Buffer::Writer writer(&buffer);
715      writer.WriteBits(0, 8);
716
717      acServerMessage.Serialize(writer);
...
722      s_allocator.Reset();
723  }
```
```cpp
// admin/Transport.cpp:89-100（同族的正确形状：先激活，再构造 Buffer）
89       if (IsConnected())
90       {
91           TiltedPhoques::ScopedAllocator _{s_allocator};
92
93           TiltedPhoques::Buffer buffer(1 << 16);
```
问题：`Code/server/Pch.h:48` 已 `using TiltedPhoques::ScopedAllocator;`，
而 `Code/server/` 下 `ScopedAllocator` 的**使用点为零**（已 grep 确认：
`ScopedAllocator` 在 `Code/` 下命中 6 处，全在 `admin/`、`client/`、`encoding/`）。
即：两处 `ScratchAllocator` 实例是**纯装饰**，`Reset()` 一个从未被写过任何东西的 arena。
条件：每一条出站消息。
后果：每次 `Send` 一次 1 MiB 的分配/释放；一次 `SendToPlayers`（`:750-757`）=
n 次。`CharacterService::ProcessMovementChanges` 50 Hz（`CharacterService.cpp:1146`）
对每个可见玩家各一次 —— 8 人默认配置下每秒 8 × 50 = 400 次 1 MiB 分配。
证据边界：`ScratchAllocator`/`Buffer` 的实现在 `Libraries/TiltedCore`（本机未检出，
`find` 结果为空），所以「未激活是否等价于普通堆」不能在本地核实。但
**「两处 `s_allocator` 未被任何 `ScopedAllocator` 激活」是静态事实**，
与 `admin/Transport.cpp:91` 的对照也是静态事实。
最小改法：在 `:713` 之前加 `TiltedPhoques::ScopedAllocator _{s_allocator};`
（一行，与 `admin/Transport.cpp:91` 完全同形）；或把两个 `s_allocator` 直接删掉。

---

### D-13 · 严重度：中 · 【运行时依赖】 服务端 **没有**任何「玩家死亡/重连」导致 `CharacterService` 实体回收的路径，`OnDisconnection` 只处理 `OwnerComponent` 的**转移**不处理**回收**

位置：`Code/server/GameServer.cpp:677-704`
```cpp
677          const auto playerCharacter = pPlayer->GetCharacter();
678
679          // Cleanup all entities that we own
680          auto ownerView = m_world.view<OwnerComponent>();
681          for (auto entity : ownerView)
682          {
683              if (playerCharacter && entity == *playerCharacter)
684              {
685                  m_pWorld->GetDispatcher().enqueue(CharacterRemoveEvent(World::ToInteger(entity)));
686                  continue;
687              }
688
689              const auto& [ownerComponent] = ownerView.get(entity);
690              if (ownerComponent.GetOwner() == pPlayer)
691              {
692                  m_pWorld->GetDispatcher().enqueue(OwnershipTransferEvent(entity));
693              }
694          }
```
问题：这段**只对 `playerCharacter` 那一个实体排队销毁**（`:683-686`），
其余属于该玩家的所有 actor 走 `OwnershipTransferEvent` ⇒ `TransferToNextOwner`
⇒ 要么交给别人、要么在「无候选者」时触发 `CharacterRemoveEvent`（`:1034-1035`）。
条件：`TransferToNextOwner` 遇到第一个满足条件的候选者就 `return`（`:1030-1031`），
所以**只要有任何一个玩家在线且 `IsInRange`，actor 就永远不销毁**；而且
`CharacterRemoveEvent` 的处理体 `OnCharacterRemoveEvent`（`:369-395`）
在 `view.find` 失败时**直接 return**（`:377-378`）——
即排队了也未必真的销毁。
后果：断线玩家的 actor（含 `InventoryComponent` / `ActorValuesComponent` /
`CharacterComponent::SaveBuffer`）在服务器上**永久驻留**，并持续参与
`ProcessMovementChanges`（`CharacterService.cpp:1165` 遍历全表）与
`SendToPlayersInRange`。与 D-10 叠加后：`uMaxPlayerCount` 只封顶**在线**人数，
封不住**曾经在线过**的实体总数。
证据边界：「合法流量下`IsInRange` 是否会失败」取决于 `CellIdComponent::IsInRange`
与坐标系（`Code/encoding/Structs/CellIdComponent.*`，本轮未通读），
因此**「永不销毁」的强度是运行时依赖的**；「没有任何按玩家批量回收的路径」是静态事实。
最小改法：`OnDisconnection` 里把「该玩家拥有但不是 playerCharacter」的实体一并
排入 `CharacterRemoveEvent`，而不是全部交给所有权转移。

---

### D-14 · 严重度：低 · 【可闭合】 `GameServer::OnConsume` 在 `handler` 外层没有 `try/catch`

位置：`Code/server/GameServer.cpp:630-637` 与 `:258-279`
```cpp
// :262-276  handler lambda：不带 noexcept
262          m_messageHandlers[T::Opcode] = [this](UniquePtr<ClientMessage>& apMessage, ConnectionId_t aConnectionId)
263          {
264              auto* pPlayer = m_pWorld->GetPlayerManager().GetByConnectionId(aConnectionId);
...
273              const auto pRealMessage = CastUnique<T>(std::move(apMessage));
275              m_pWorld->GetDispatcher().trigger(PacketEvent<T>(pRealMessage.get(), pPlayer));
276          };

// :630-637  OnConsume 也没有 catch
630      auto& handler = m_messageHandlers[cOpcode];
631      if (!handler) [[unlikely]]
632      {
633          spdlog::error("No handler for client message opcode {} from {:x}", ...);
634          return;
635      }
637      handler(pMessage, aConnectionId);
```
问题：这条链（`handler` → `dispatcher.trigger` → 每个 `PacketEvent<T>` 的 sink）
**没有任何异常边界**。已知的 sink 各自带 `try/catch`（`docs/PITFALLS.md` §104 统计了
63 处 `Diagnostics::ReportFailure`），而 `PacketEvent<T>` 的构造（`:275`）与
`CastUnique<T>`（`:273`）都**不分配**，所以**本条当前无触发路径**。
保留理由：`GameServer::OnConsume`（`GameServer.h:118`）是一个**不带 `noexcept`** 的
虚函数覆写，它的上游是 `TiltedCore` 的 `Server` 帧（本机未检出）。「这里到底会不会
terminate」取决于外部库 —— 这正是 §161.3 那条改动画出的边界。
最小改法：在 `:637` 外包一层 `try { ... } catch (...) { Diagnostics::ReportFailure(...); }`，
与 `ClientMessageFactory::Extract`（`ClientMessageFactory.cpp:56-63`）同形。

---

### D-15 · 严重度：低 · 【运行时依赖】 `PartyService::OnPartyChangeLeader` 在两次查询之间解引用 `Player*`，且循环校验用的是**解引用**而不是先取 id

位置：`Code/server/Services/PartyService.cpp:159-207`
```cpp
165          auto& message = acPacket.Packet;
166          Player* const player = acPacket.pPlayer;
167          Player* const pNewLeader = m_world.GetPlayerManager().GetById(message.PartyMemberPlayerId);
...
183              const auto partyItor = m_parties.find(*inviterPartyComponent.JoinedPartyId);
184              if (partyItor != std::end(m_parties))
185              {
186                  Party& party = partyItor->second;
187                  if (party.LeaderPlayerId == player->GetId())
188                  {
189                      for (auto& pPlayer : party.Members)
190                      {
191                          if (pPlayer->GetId() == pNewLeader->GetId())
```
问题：**越权本身已被挡住** —— `:189-198` 的循环要求目标确在 `party.Members` 里，
所以「把别人队的玩家设成自己队队长」不成立。留存的问题在
**`:191` 对 `pNewLeader` 的解引用**：它发生在 `:167` 的全表查找之后，
中间隔了一次 `m_parties.find`；如果此刻 `OnDisconnection` 释放了该玩家行
（`GameServer.cpp:703` 的 `Remove`），这就是 UAF。
证据边界：是否可达取决于 `TiltedCore` 是否从另一线程投递 `OnDisconnection` —— 本机无法核实
（`Libraries/TiltedCore` 未检出）。因此**按「与 D-02 同一判据、但窗口更窄」记为低**。
最小改法：在 `:168` 之后写 `const auto newLeaderId = pNewLeader->GetId();`，
循环里用 `newLeaderId` 比较，全程不再解引用该指针。

---

### D-16 · 严重度：低 · 【可闭合】 `PlayerManager::ForEach` 标着 `noexcept` 却直接调用**任意** `acFunctor`；唯一调用者的 lambda 在分配

位置：`Code/server/Game/PlayerManager.h:52-60`、`Code/server/Scripting/Player_Bindings.cpp:51-55`
```cpp
// PlayerManager.h:52-60
52      template <class T> void ForEach(const T& acFunctor) noexcept
53      {
54          auto itor = std::begin(m_players);
55          auto end = std::end(m_players);
56          for (; itor != end; ++itor)
57          {
58              acFunctor(itor.value().get());
59          }
60      }
```
```cpp
// Player_Bindings.cpp:51-55（全仓唯一调用点）
51      table["GetAllPlayers"] = [](PlayerManager& aSelf) {
52          std::vector<Player*> allPlayers;
53          aSelf.ForEach([&](Player* aPlayer) { allPlayers.push_back(aPlayer); });
54          return allPlayers;
55      };
```
问题：`ForEach` 的 `noexcept` 是对**整个进程**的承诺，而它把承诺转交给一个
它无法约束的模板参数；唯一调用点的 `push_back` 会分配。
`docs/PITFALLS.md` §103/§104 定的判据是「承诺写在哪个函数身上，`catch` 就得写在哪个函数体里」
—— 这里的承诺写在 `ForEach` 身上，而 `ForEach` 体内**一个 `catch` 都没有**。
条件：Lua 脚本调用 `PlayerManager.get():GetAllPlayers()` 且此瞬间堆耗尽。
后果：`std::terminate`，且因为调用栈上已经过了 `sol::protected_function`
（`ScriptService.inl:22-29` 的保护边界在**返回之后**才检查 `result.valid()`），
Lua 的 `pcall` **接不住** —— 与 §103 里被修的 14 个入口是同一个形状，
但这一处不在 §103 的清单里（`PlayerManager.h` 不是 `ScriptService`）。
最小改法：把 `ForEach` 的 `noexcept` 去掉（它只是给 Lua 绑定用的一个便利封装，
不需要 noexcept），或把 `try/catch` 放在 `Player_Bindings.cpp:53` 的 lambda 里。
宁可去掉 `noexcept`：这一层没有「必须不抛」的理由。

---

### D-17 · 严重度：低 · 【可闭合】 `PlayerManager::GetByUsername` 的两个重载把 const 限定符写反了

位置：`Code/server/Game/PlayerManager.h:47-48`、`Code/server/Game/PlayerManager.cpp:117-139`
```cpp
// PlayerManager.h:47-48
47      Player* GetByUsername(const String& acUsername) const noexcept;      // const 方法返回非 const
48      Player const* GetByUsername(const String& acUsername) noexcept;      // 非 const 方法返回 const
```
```cpp
// PlayerManager.cpp:129-131
129  Player const* PlayerManager::GetByUsername(const String& acUsername) noexcept
130  {
131      auto itor = std::begin(m_players);
```
问题：同文件里 `GetByConnectionId`（`:41-42`）与 `GetById`（`:44-45`）都是
「`const` 方法返回 `const*`」，只有 `GetByUsername` 反过来。
两个调用点（`GameServer.cpp:428`/`:438`）拿到的是 `Player*`（非 const），
它们随后把这个指针交给 `AddAdminSession(pPlayer->GetConnectionId())` —— 只读，所以**今天没有误用**。
后果：这是一处会主动误导读者的签名：`const PlayerManager&` 上调用
`GetByUsername` 会拿到一个可写的 `Player*`（突破 const 正确性），
而一个非 const 的 `PlayerManager&` 上**两个重载都可用**，
C++ 会选非 const 那个（`Player const*`），于是同一份代码在
`const` 与否不同的上下文里得到不同 const 性的返回值。
最小改法：把两行签名对调成与 `GetById` 一致的形状：
`Player* GetByUsername(...) noexcept;` / `const Player* GetByUsername(...) const noexcept;`。


---

### D-18 · 严重度：低 · 【可闭合】 `CharacterService::OnCharacterRemoveEvent` 在 `HandleCharacterDestoy` **之前**没有校验实体是否真是角色

位置：`Code/server/Services/CharacterService.cpp:369-395`
```cpp
375          const auto view = m_world.view<OwnerComponent>();
376          const auto it = view.find(static_cast<entt::entity>(acEvent.ServerId));
377          if (it == view.end())
378              return;
379
380          GameServer::Get()->GetWorld().GetScriptService().HandleCharacterDestoy(*it);
381
382          NotifyRemoveCharacter response;
383          response.ServerId = acEvent.ServerId;
384
385          for (auto pPlayer : m_world.GetPlayerManager())
386              pPlayer->Send(response);
387
388          m_world.destroy(*it);
```
问题：这里的 view 只要求 `OwnerComponent`（`:375`），**没有要求 `CharacterComponent`**。
而 `ObjectService::OnAssignObjectsRequest`（`ObjectService.cpp:121-131`）创建的实体
**不带 `OwnerComponent`**，所以今天这条 view 命中的集合恰好只有角色 ——
**这是巧合，不是约束**：两个 `create()` 调用点里，只有
`CharacterService.cpp:732` 会 `emplace<OwnerComponent>`，
而一旦以后有人给对象实体也补上 `OwnerComponent`（`ObjectService.cpp:28-30` 的
TODO 恰恰在说「对象实体的 cell 处理需要重做」），这段代码就会把**对象**也当角色销毁，
并且对每个玩家发一条 `NotifyRemoveCharacter`。
后果：不是当前可触发的缺陷；是一条「守卫比它保护的不变量更弱」的结构债。
与 `docs/PITFALLS.md` §100 的判据同一句话：「只写一个 view 条件时，先问全树有几个
emplace 点满足它」。
最小改法：把 `:375` 的 view 改成
`view<OwnerComponent, CharacterComponent, CellIdComponent>`（与
`TransferToNextOwner` `:1006` 使用同一组组件），使「这是角色」成为守卫的一部分。

---

### D-19 · 严重度：低 · 【运行时依赖】 `GameServer::Send` 的 `static thread_local ScratchAllocator` 在**多线程**下共享，而 `Reset()` 在 `Send` 结束时无条件调用

位置：`Code/server/GameServer.cpp:709-723`、`:725-739`
```cpp
711      static thread_local TiltedPhoques::ScratchAllocator s_allocator{1 << 18};
712
713      Buffer buffer(1 << 20);
...
722      s_allocator.Reset();
723  }
```
问题：`thread_local` 让每个线程各有一份，这条本身是对的；问题是**它在本函数内
从未被 `ScopedAllocator` 激活**（见 D-12），因此 `Reset()` 重置的是一个
「本函数内没有任何人往里分配」的 arena。
**如果**以后有人按 D-12 的建议补上 `ScopedAllocator`，那么 `:722` 的
`Reset()` 会变得有害：`Buffer buffer(1 << 20)` 与整个 `Serialize` 都用
arena 分配，而 `Server::Send(aConnectionId, &packet)`（`:720`）**是同步调用还是
把 `packet` 排队？** 若是后者，`Reset()` 会在传输层仍持有 `packet` 指针时
把内容抹掉 —— 这与 `admin/Transport.cpp:84-87` 用 **RAII `~ScopedReset`** 而不是
裸调用，是同一个问题的两种回答。
证据边界：`Server::Send` 的实现（是否拷贝 `PacketView`）在 `TiltedCore`，本机未检出。
因此**按「修 D-12 时必须同时回答这一条」记录**，不作为当前缺陷。
最小改法：把 `:722`/`:738` 的裸 `Reset()` 换成 `admin/Transport.cpp:84-87` 的
`ScopedReset` RAII 形状，并在 D-12 的修法里一并处理。


---

## 复现/核对命令（照抄可用）

```bash
# D-04 / D-11：mod 表无回收、seed 无回绕保护
grep -rn "refCount" Code/server/Components/ModsComponent.cpp
grep -rn "m_seed" Code/server/Components/ModsComponent.cpp

# D-10：两个 create() 调用点，只有一个有限流
grep -rn "m_world.create()" Code/server
grep -rn "kMaxObjectEntities" Code/server

# D-12：server 侧从未激活 ScopedAllocator
grep -rn "ScopedAllocator" Code/

# D-01：全仓唯一的限流
grep -rn "kBurstCapacity\|kRefillPerSecond" Code/server

# D-07：唯一的入站门
grep -n "WithinPacketLimit" Code/server/GameServer.cpp

# 已修好的证据链（聊天）
grep -n "kMaxChatMessageBytes" Code/encoding/Messages/SendChatMessageRequest.cpp Code/encoding/WireLimits.h
grep -n "GetCharacter" Code/server/Services/OverlayService.cpp
```