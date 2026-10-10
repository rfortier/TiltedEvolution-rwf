# 更新日志

本项目的更新记录以 **GitHub Releases** 为准:

> https://github.com/komAAmok/SkyrimTogetherNext/releases

每个 tag(形如 `v1.0.20`)对应一个 Release,附上版本号相同的两个包——
客户端 mod 与专用服务器。逐条提交的历史见 `git log` 与各次 PR。

## v1.1.9(2026-10-01)

### 修复:客户端 `Code/client` 里 698 个 `noexcept` 只有 9 个写了 `catch`,142 个在自己分配或格式化日志 —— 判据是「承诺写在哪个函数身上,`catch` 就得写在哪个函数体里」

**现象**:没有任何现象。`Code/client/**` 共 446 个 `.cpp/.h/.inl`,里面 **698 个 `noexcept` 函数定义**,体内含 `catch` 的只有 **9 个**;同一个脚本数出 **142 个**定义在自己分配内存或调用 `spdlog`/`fmt::format`。`Code/client` 全文的 `try`/`catch` 计数是 **18 / 14**,`Code/encoding` 与 `Code/admin` 是 **0 / 0**。

这与刚提交的服务端那一批(`941bd606`,`Code/server` 211 → 212 个 `noexcept`、6 → 70 个 `catch`)是同一族缺陷的另一半:那一批只覆盖了 `Code/server`,客户端从来没有过边界。

**根因**:`noexcept` 写在函数身上,分配发生在函数体里,而 catch 一个都没有 —— `std::bad_alloc` 从体内穿出去就是 `std::terminate`。客户端比服务端更糟的一点是:**`Code/client` 里没有任何 `set_terminate`、`std::terminate`、`AddVectoredExceptionHandler` 或 `SetUnhandledExceptionFilter`**(`git grep -rn -E "set_terminate|std::terminate|SetUnhandledExceptionFilter|AddVectoredExceptionHandler" -- Code/client` 零命中)。`CrashHandler` 装的是 `LPTOP_LEVEL_EXCEPTION_FILTER`,而 terminate 是 C++ 运行时的 `abort()`,不经过它 —— 所以这一族在客户端连 dump 都没有。

**判据(同族对照,载重)**:

| 对照 | 事实 | 结论 |
| --- | --- | --- |
| 本仓已有的正确形状 | `Code/server/Diagnostics.h`(commit `941bd606` 新增,27 行)与更早的 `ScriptService::ReportFailure`(`e7253f3b`) | 修法不是新发明,是让客户端用同一个 helper |
| 「同族其它实现长什么样」 | 客户端已有的 9 个 catch 里,8 个是 `PluginMessagingService.cpp` 自己的 `Log()`(`cd74a67e`) | 全客户端只有那一处边界 |
| 日志本身会分配 | `Code/client/xmake.lua` 没有定义 `SPDLOG_NO_EXCEPTIONS`;`TiltedOnlineApp.cpp:75 spdlog::flush_every(std::chrono::seconds(1))`、`:80 logger->flush_on(spdlog::level::warn)` | 与 `docs/PITFALLS.md` §103 第三条教训同一格:失败的报告本身也会失败 |
| 「下游有守卫」不等于「值被挡住了」 | `Code/client/Services/TransportService.h` 的 `OnConsume`/`OnConnected`/`OnDisconnected`/`OnUpdate` 四个覆写**都不带 `noexcept`** | 但 handler 自己是 ⇒ 抛出仍在 handler 里 terminate,上游非 `noexcept` 救不了它 |
| `#if 0` 里的分配不算 | 3 个站点(`DebugService::OnUpdate`、`CombatService::OnHitEvent`、`CombatService::RunTargetUpdates`)的全部命中都在 `#if 0` 块内 | 逐行核对后不改:`OnUpdate` 的 10 行活代码不分配,另外两个函数体整个是 `#if 0` |

**可达性**:142 个站点落在四条路径上,没有一条需要管理员:

- **收包**:`Code/client/Services/TransportService.cpp:181-220 OnConsume`(服务端可发的每一种包)→ `:222 DispatchMessage` → 每个服务的 `OnNotify*`。这条路径上还有 `CharacterService.cpp` 的 30 个定义,是客户端最密的一处。
- **tick**:`TransportService::HandleUpdate` → 各服务的 `Run*`(`RunRemoteUpdates`、`RunSmallHealthUpdates`、`RunNakedNPCBugChecks`、`ApplyQueuedEffects`、`RunExperienceUpdates`、`ApplyCachedWeaponDraws`、`RunWeatherUpdates`、`ProcessLeveledConforms`)。
- **握手泵**:`TransportService::HandshakeThreadMain` / `PumpConnectionLocked` / `PumpHeartbeat` —— 按 `TransportService.h` 的注释,这条路径上的线程**不是游戏线程**。
- **hook 回调**:`HookAudit::Record/Report/Verify`、`GamePatch.h` 的 6 个字节补丁 helper、`Actor`/`TESObjectREFR`/`PlayerCharacter` 等引擎侧 helper。

**修法**:新增 `Code/client/Diagnostics.h`(27 行),与 `Code/server/Diagnostics.h` 逐字同形,经 `Code/client/TiltedOnlinePCH.h:50-52` 进入每个编译单元。选 PCH 而不是逐文件 include:`Code/client/xmake.lua:17 set_pcxxheader("TiltedOnlinePCH.h")` 已把它设成强制包含,而 `TiltedOnlinePCH.cpp` 只有一行 `#include <TiltedOnlinePCH.h>` —— 不这么写就会有一个不包含它的 TU。

42 个文件、142 个函数体加了 `try { ... } catch (...) { Diagnostics::ReportFailure(__FUNCTION__, "..."); }`,**一个 `noexcept` 都没有删**(698 → 699,新增的是 `Diagnostics::ReportFailure` 自己)。非 void 的补了显式回退:

| 函数 | catch 里的回退 | 语义 |
| --- | --- | --- |
| `GamePatch.h` 的 `Anchor`/`At`/`WriteBytes`/`Nop`/`BranchTarget`/`SwapCall` | `nullptr` / `false` | 「补丁没打上」是这些函数本来就会给出的答案 |
| `VersionDbPtr::GetPtr` | `return nullptr;` | 与「这个 offset 没有指针」同一个答案,`operator T*` 与 `operator->` 都直接转它 |
| `Actor::RemoveSpell`、`EffectItem::IsNightVisionEffect`、`LeveledNpcSystem::ApplyPick`、`CellRespawnOverrides.cpp:115` | `false` | 「没生效」,调用方本来就处理 |
| `EnchantmentItem::Create`、`Projectile::Launch`、`TESObjectREFR::GetExtraDataFromItem` | `nullptr` / `apResult` | 与引擎拒绝同一个答案 |
| `TESObjectCELL::GetRefsByFormTypes`、`TESObjectREFR::RemoveNonQuestItems`、`Actor::GetFactions`、`TESObjectREFR::GetInventory` | `{}` | 空表是安全答案:调用方遍历后什么都不做 |
| `Utils::GetServerId` | `std::nullopt` | 与「查不到」同一个答案 |
| `Utils::GetByServerId<T>` | `nullptr` | 每个调用点本来就检查空指针 |
| `PapyrusService::Get`、`ObjectService::CreateObjectEntity`、`CharacterService::CreateCharacterForEntity` | `nullptr` / `entt::null` | 与「没有这个函数 / 没有这个实体」同一个答案 |

**验证**:`git diff --numstat -- Code/client` = **42 files changed, 5561 insertions(+), 4126 deletions(-)**,1318 个 hunk;`Code/client/Diagnostics.h` 是唯一的新文件。同一个普查脚本在 `HEAD` 与工作树上各跑一次:

| 读数 | `HEAD` | 现在 |
| --- | --- | --- |
| `noexcept` 定义数 | 698 | 699 |
| 体内含 `catch` | 9 | 149 |
| 体内有分配/日志标记且无 `catch` | 142 | **3** |
| 其中命中全在 `#if 0` 内 | 0 | 3(逐行核对) |
| `Code/client` 全文 `try`/`catch` | 18 / 14 | 158 / 164 |

`Diagnostics::ReportFailure` 调用点 **139 个,分布在 41 个文件**:`CharacterService.cpp` 30、`Actor.cpp` 9、`MagicService.cpp` 9、`ObjectService.cpp` 8、`TransportService.cpp` 8、`TESObjectREFR.cpp` 7、`OverlayService.cpp` 7、`GamePatch.h` 6、`ActorValueService.cpp` 5、`InventoryService.cpp` 5、`PartyService.cpp` 4、`WeatherService.cpp` 4,其余各 1–3。

留在原样的三处不是遗漏,是**逐行核对过命中全在 `#if 0` 里**:`DebugService::OnUpdate` 的活代码只有窗口查找与一次挂起的 `MoveTo`,不分配;`CombatService::OnHitEvent` 与 `RunTargetUpdates` 的函数体整个是 `#if 0`。

`Code/client/xmake.lua:13-14 add_headerfiles("**.h")` / `add_files("**.cpp")` 覆盖新头与全部 42 个文件;`:17 set_pcxxheader("TiltedOnlinePCH.h")` 保证 `Diagnostics.h` 进入每个 TU。每个改过的文件都做过花括号/圆括号配平、`try`/`catch` 缩进配对与行尾检查(工作树里 6 个文件本来就是 LF,与 `HEAD` 一致,未动;`Code/client/Components/LocalAnimationComponent.h` 是其中之一,现在仍是 LF)。
**本机不编译**(`docs/PITFALLS.md` §0/§1),所以判据是静态的:分配点是逐字读出来的,`noexcept`、`try`/`catch` 与 `#if 0` 的判定都是脚本数出来的。

### 修复:服务端 `Code/server` 里 211 个 `noexcept` 只有 6 个写了 `catch`,而其中 27 个在自己分配、44 个在格式化日志 —— 判据是「`noexcept` 是一条承诺,失败的报告本身也会失败」

**现象**:没有任何现象。`Code/server/**` 共 100 个 `.cpp/.h/.inl`,里面 **211 个 `noexcept` 函数定义**,体内含 `catch` 的只有 **6 个**。用等长清空注释与字面量、再按花括号配平取函数体的脚本逐个数,得到两族分配点,它们有重叠:

```
家族一:体内自己分配内存(27 个定义)
  Code/server/Components/ModsComponent.cpp:5  AddStandard          m_standardMods.emplace(...)
  Code/server/Components/ModsComponent.cpp:20 AddLite              m_liteMods.emplace(...)
  Code/server/Game/Animation/ActionReplayCache.cpp:5  FormRefinedReplayChain  Vector<ActionEvent>{begin+1, end}
  Code/server/Game/Animation/ActionReplayCache.cpp:18 AppendAll            m_actions.push_back(action)
  Code/server/Game/Animation/ActionReplayCache.cpp:31 RefineReplayCache    action.EventName = String{*instantAnimName}
  Code/server/Game/Animation/ActionReplayCache.cpp:64 IsExitAction         String lowercaseName{acAction.EventName}
  Code/server/Game/PlayerManager.cpp:27       Create               m_players.emplace(id, MakeUnique<Player>(...))
  Code/server/GameServer.cpp:1149             SanitizeUsername     String username = acUsername;
  Code/server/GameServer.h:92                 AddAdminSession      m_adminSessions.insert(acSession)
  Code/server/Services/ActorValueService.cpp:23/:50/:77   ...ActorValuesList[id] = value / notify.Values = message.Values
  Code/server/Services/CharacterService.cpp:251/:374/:587/:904/:960      push_back / 8 次 emplace / Map<...>
  Code/server/Services/ObjectService.cpp:31/:60           Vector<entt::entity> toDestroy / response.Objects.push_back
  Code/server/Services/PartyService.cpp:113/:226/:273/:320/:439/:463/:487  m_parties[partyId] / push_back / Players[id] = ...
  Code/server/Services/QuestService.cpp:27   OnQuestChanges       dedupHistory.emplace_back(...)

家族二:体内只有日志格式化,自己不含分配标记(44 个定义,共 106 条 spdlog 调用)
  Code/server/Game/PlayerManager.cpp:16                    PlayerManager::Get
  Code/server/Services/CharacterService.cpp  x11           :190 :332 :362 :445 :495 :508 :560 :573 :681 :745 :794
  Code/server/Services/InventoryService.cpp  x3            :28 :99 :148
  Code/server/Services/MagicService.cpp      x4            :24 :40 :53 :72
  Code/server/Services/ActorValueService.cpp x1            :102
  Code/server/Services/OverlayService.cpp    x1            :29  sendPlayerMessage
  Code/server/Services/PartyService.cpp      x1            :190 OnPartyKick
  Code/server/Services/PlayerService.cpp     x1            :153 OnPlayerRespawnRequest
  Code/server/Services/PluginMessagingService.cpp x1        :54  OnPluginMessage
  Code/server/Services/QuestService.cpp      x1            :309 QuestStageDedupHistory::Expire
  Code/server/Services/CombatService.cpp     x1            :15
  Code/server/Services/AdminService.cpp      x1            :14  HandleShutdown
```

**根因**:`noexcept` 写在函数身上,分配发生在函数体里,而 catch 一个都没有 —— 一个 `std::bad_alloc` 从体内穿出去就是 `std::terminate`,没有 dump,也没有日志。

家族二更隐蔽:**日志本身会分配**。`Code/server/xmake.lua:47` 固定 `spdlog`(v1.13.0),全仓没有任何地方定义 `SPDLOG_NO_EXCEPTIONS`(`git grep -rn -E "SPDLOG_ACTIVE_LEVEL|SPDLOG_LEVEL|SPDLOG_NO_EXCEPTIONS" -- Code xmake.lua` 零命中),所以 `spdlog::error`/`warn` 会把参数格式化进一个新建的 buffer —— 在一个 `noexcept` 的 catch 里直接调它,等于把 terminate 从原来的位置挪到处理它的位置上。这正是 `docs/PITFALLS.md:14097-14098`(§103 第三条教训)已经写下的那句「失败的报告本身也会失败」,只是当时只落在了 `ScriptService` 一个文件里。

**判据(同族对照,载重)**:

| 对照 | 事实 | 结论 |
| --- | --- | --- |
| 本仓已有的正确形状 | `Code/server/Services/ScriptService.cpp:252-263`(commit `e7253f3b`)的 `ReportFailure` 是 `noexcept` + 内层 `try/catch(...)` | 修法不是新发明,是把这个形状搬给别的服务用 |
| 同族的下一次提交 | 同一批的 51 个站点里,`ScriptService.*` 已经修完(14 个 `noexcept` 里 13 个在分配) | 剩下的 211 个定义是同一族,只是没轮到 |
| 「同族其它实现长什么样」 | `git grep -n -E "\bcatch \(" -- Code/server` 在本次修改前是 **14 处**,其中 2 处在 `ScriptService` | 全仓的服务层没有第二处边界 |
| 日志级别不是编译期属性 | `Code/server_runner/main.cpp:43 Console::StringSetting sLogLevel{"sLogLevel", "Log level to print", "info"}`、`:74 globalOut->set_level(level::from_str(sLogLevel.value()))` | 「`debug` 默认不打」是运行期设置,不是代码性质;44 个定义里的 `debug` 行同样要守 |
| 「下游有守卫」不等于「值被挡住了」 | `GameServer.h:103/104/106` 的 `OnUpdate`/`OnConsume`/`OnDisconnection` 三个覆写**都不带 `noexcept`** | 但 handler 自己是 `noexcept` ⇒ 抛出仍然在 handler 里 terminate,上游非 `noexcept` 救不了它 |

**可达性**:这 71 个站点全部落在三条路径上,没有一条需要管理员:

- **tick 线程**:`GameServer.cpp:557-571 OnUpdate` → `:567 dispatcher.trigger(UpdateEvent{cDeltaSeconds})`,五个 sink(`CalendarService.cpp:18`、`CharacterService.cpp:53`、`PartyService.cpp:24`、`ScriptService.cpp:57`、`StringCacheService.cpp:10`)。`CharacterService::OnUpdate` 又转调 `ProcessFactionsChanges`(2 秒一次)与 `ProcessMovementChanges`(20 ms 一次)—— 后者每个 tick 为每个在线玩家建一张 `Map<uint32_t, ServerReferencesMoveRequest>`。
- **收包**:`GameServer.cpp:573-618 OnConsume` → `:616 m_messageHandlers[pMessage->GetOpcode()](pMessage, aConnectionId)`。任何一个已连接玩家都能发:聊天、任务更新、任务场景、队伍创建/邀请/接受/踢人/换队长、背包/装备、移动、法术/投射物、物件分配。
- **断连**:`GameServer.cpp:626-686 OnDisconnection` → `:645 trigger(PlayerLeaveEvent)` → `PartyService::OnPlayerLeave` → `RemovePlayerFromParty` + `BroadcastPlayerList`,后者是 O(n²) 条消息、每条一个 `Map<uint32_t, String>`。

触发条件本身是堆耗尽。本机不编译(`docs/PITFALLS.md` §0/§1),所以每一条都是静态读数,不是复现。

**修法**:新增 `Code/server/Diagnostics.h`(27 行),把 `ScriptService` 那个私有静态提升成一个每个服务都能用的自由函数:

```cpp
// Code/server/Diagnostics.h
namespace Diagnostics
{
    inline void ReportFailure(const char* acpContext, const char* acpDetail) noexcept
    {
        try
        {
            spdlog::error("{}: {}", acpContext, acpDetail);
        }
        catch (...)
        {
        }
    }
}
```

两个形参都是 `const char*`,调用点一律传字面量或 `__FUNCTION__` —— 两者都不由这次调用构造,这正是 `docs/PITFALLS.md:14095-14096`(§103 第二条教训)说的那件事。

它经 `Code/server/Pch.h:29 #include <Diagnostics.h>` 进入所有编译单元。选 `Pch.h` 而不是逐文件 include,是因为 `Code/server/xmake.lua:9 set_pcxxheader("Pch.h")` 已经把它设成强制包含:`Code/server/Pch.cpp` 是**空文件**,所以不这么写就会有一个不包含它的 TU。

18 个文件、71 个函数体加了 `try { ... } catch (...) { Diagnostics::ReportFailure(__FUNCTION__, "..."); }`,**一个 `noexcept` 都没有删**(211 → 212,新增的那一个是 `Diagnostics::ReportFailure` 自己)。三个非 void 的函数补了显式回退值:

| 函数 | catch 里的回退 | 语义 |
| --- | --- | --- |
| `PlayerManager::Get` | `return nullptr;` | 与「没有实例」同一个答案,调用方本来就处理 |
| `PlayerManager::Create` | `return nullptr;` | `GameServer.cpp:1006-1013` 已经把这个答案当成「该连接已有玩家」 |
| `ModsComponent::AddStandard`/`AddLite` | 无(先取 `const auto id = m_seed++;` 再 `try`) | id 必须唯一:`Code/client/Systems/ModSystem.cpp:88-101` 用它建 `m_serverToGame`/`m_standardToServer` 反查表,两个 mod 共用一个 id 会让客户端派生的每个 form id 撞车,比「这一条没去重」更坏。`emplace` 失败只丢去重行 —— 服务端除这个 `find` 之外没有任何读者 |
| `GameServer::SanitizeUsername` | `return String{};` | 退化成空名,查找失败并打印,唯一调用点是 `GameServer.cpp:436` 的控制台命令 |
| `ActionReplayCache::RefineReplayCache`/`IsExitAction` | `return false;` | 「不是退出动作」是安全答案:动作留在缓存里,客户端不重置动画图 |
| `CharacterService::CanClaimOwnership`/`TransferOwnership` | `return false;` | 「不能认领」/「没转移成功」,调用方不会广播一次只做了一半的交接 |

`ScriptService` 那份私有静态**保持不动**。它与新的自由函数行为完全一致,把 8 个调用点改成 `Diagnostics::` 是纯改名、零行为差,只会让这次 diff 变大;两边并存不产生第二种语义。

**验证**:`git diff --numstat -- Code/server` = **18 files changed, 2196 insertions(+), 1564 deletions(-)**;`git status` 里 `Code/server/Diagnostics.h` 是唯一的新文件。

同一个普查脚本在 `HEAD` 与工作树上各跑一次:

| 读数 | `HEAD` | 现在 |
| --- | --- | --- |
| `noexcept` 定义数 | 211 | 212 |
| 体内含 `catch` | 6 | 70 |
| 体内有分配标记且无 `catch` | 27 | **0** |
| 体内有 `spdlog::` 且无 `catch` | 44(106 条日志) | **0**(0 条) |
| `Code/server` 全文 `try`/`catch` 计数 | 11 / 14 | 75 / 80 |

`Diagnostics::ReportFailure` 调用点 **63 个,分布在 17 个文件**:`CharacterService.cpp` 18、`PartyService.cpp` 10、`OverlayService.cpp` 5、`ActorValueService.cpp` 4、`ActionReplayCache.cpp` 4、`MagicService.cpp` 4、`InventoryService.cpp` 3、`QuestService.cpp` 3、`ModsComponent.cpp` 2、`PlayerManager.cpp` 2、`ObjectService.cpp` 2、`GameServer.cpp`/`GameServer.h`/`AdminService.cpp`/`CombatService.cpp`/`PlayerService.cpp`/`PluginMessagingService.cpp` 各 1。

`Code/server/xmake.lua:12-13 add_headerfiles("**.h")` / `add_files("**.cpp")` 覆盖新头与全部 18 个文件;`:9 set_pcxxheader("Pch.h")` 保证 `Diagnostics.h` 进入每个 TU。每个改过的文件都做过花括号/圆括号/方括号配平与行尾检查(`Code/server/Services/PluginMessagingService.{cpp,h}` 本来就没有文件尾换行,与 `HEAD` 一致,未动)。
**本机不编译**(`docs/PITFALLS.md` §0/§1),所以判据是静态的:分配点是逐字读出来的,`noexcept` 数量是脚本数出来的。

### 修复:服务端脚本层的 14 个 noexcept 里 13 个在分配,而全仓唯一有 catch 的文件把边界放在了调用者身上 —— 判据是「noexcept 的检查要顺着调用链走」

**现象**:没有任何现象。`Code/server/Services/ScriptService.h`(pin 76 行)、`ScriptService.cpp`(pin 235 行)、`ScriptService.inl`(pin 42 行)三个文件里 **14 个函数声明**都写着 `noexcept`,而其中 **13 个的函数体在分配**:

```cpp
// Code/server/Services/ScriptService.inl:3-8(pin,修复前)
template <typename... Args>
std::tuple<bool, String> ScriptService::CallCancelableEvent(const String& acName, Args&&... args) noexcept
{
    m_eventCanceled = false;

    auto& callbacks = m_callbacks[acName];   // :8 缺失即插入 ⇒ 分配
```

唯一不分配的那一个是 `:42 void OnPlayerEnterWorld(const PlayerEnterWorldEvent& acEvent) noexcept` —— 它的整个函数体是注释掉的。

**根因**:这 14 个 `noexcept` 覆盖了从脚本加载到每一帧 tick 的全部路径,而 `try`/`catch` 在 `.inl` 里是 **0/0**,在 `.cpp` 里只有 **2 处**:

| 位置(pin) | 分配点 | 谁调它 |
| --- | --- | --- |
| `ScriptService.inl:3-26 CallCancelableEvent` | `:8 m_callbacks[acName]`、`:12 sol::protected_function pf{callback}`、`:17 sol::error err = result;`、`:22`/`:25 std::make_tuple` | 六个入口 |
| `ScriptService.inl:28-42 CallEvent` | `:30 m_callbacks[acName]`、`:34 sol::protected_function pf{callback}`、`:38 sol::error err = result;` | `OnUpdate`、`HandlePlayerQuit` |
| `ScriptService.cpp:63-72 ~ScriptService`(隐式 `noexcept`) | `:68 m.clear()`、`:71 collect_garbage()` | `World` 析构 |
| `ScriptService.cpp:74-102 Initialize` | `:80 m_globals = luaVm.globals()`、`:82-83 open_libraries`、`:87 luaVm["package"]["path"] = "./?.lua"`、`:91-101` 每个 manifest 建一条 path | `Code/server/GameServer.cpp:227` |
| `ScriptService.cpp:169-197 HandlePlayerQuit` | `:171 std::string reason;` + 6 次赋值 | `Code/server/GameServer.cpp:634` |
| `ScriptService.cpp:204-217 OnUpdate` | `:211 CallEvent("onUpdate", acEvent.Delta)` | `UpdateEvent` sink |
| `ScriptService.cpp:226-229 AddEventHandler` | `:228 m_callbacks[String(acName)].push_back(acFunction);` | Lua 的 `addEventHandler` |
| `ScriptService.cpp:231-235 CancelEvent` | `:234 m_cancelReason = aReason;` | Lua 的 `cancelEvent` |
| 六个入口 `:144`/`:149`/`:154`/`:159`/`:164`/`:199` | 每个都在 `noexcept` 函数体里把字面量建成临时 `String` | 各自的 handler |

`:121 catch (std::exception& exception)` 在 `LoadScript` 里(那个函数**不** `noexcept`,而且它的 catch 是对的);`:213 catch (sol::error& exception)` 在 `OnUpdate` 里,只接 `sol::error`。

**判据(同族对照,载重)**:判据不是「这里会不会耗尽内存」,而是「**全仓唯一有 `try` 的那个文件,它的边界只覆盖了两条路径**」。

`git grep --cached -n -E "\bcatch \(" -- Code/server` 在整个 `Code/server` 只命中 **2 处**,两处都在本文件(`:121`、`:213`),其余 **0 处**。`docs/PITFALLS.md:6179-6180`(§55)记的是同一件事:「全仓 `catch` 只有 8 处」,其中就有这两处。

同族的先例是同一栋楼里刚修完的两处:

| 场景 | `noexcept` 边界在哪 | catch 写在哪 |
| --- | --- | --- |
| §101 客户端 STRPM 桥 | `extern "C"` 导出,外面没有调用者 | 函数体里 |
| §92/§93 插件补丁 | ABI 虚函数,消费方持逐字副本 | 函数体里 |
| 本节 服务端 `ScriptService` | `Code/server/GameServer.h:103 void OnUpdate() override;`、`:104 OnConsume(...)`、`:106 OnDisconnection(...)` 三者**都不带 `noexcept`** | **仍然写在函数体里** |

第三格是本节最需要解释的一格:它明明可以靠「删掉 `noexcept`」把责任推给上游,但没有。理由是承诺写在本文件身上,而调用链上确实有一个只接 `sol::error` 的 catch —— 那正是「边界放错了位置」的证据:`OnUpdate` 的 `:209-216` catch 在**调用者**身上,而分配在被调者体内(`docs/PITFALLS.md:12389-12394` §92.2 的判据逐字:「这条缺陷的判据不是「`Worker` 有没有分配」,而是**「`Worker` 的调用链上有没有人分配」**」)。

**可达性**:七个入口各有至少一条不需要管理员的路径:

| 入口(pin) | 触发 | 调用点 |
| --- | --- | --- |
| `HandlePlayerJoin` | 每个客户端认证后一次 | `Code/server/GameServer.cpp:1023` |
| `HandlePlayerQuit` | 每次断连一次 | `Code/server/GameServer.cpp:634`(在 `:628 m_adminSessions.erase` 之后、`:645 trigger(PlayerLeaveEvent)` **之前**) |
| `HandleChatMessage` | 每条聊天包一次 | `Code/server/Services/OverlayService.cpp:75` |
| `HandleCharacterMove` | **每个 action event 一次** | `Code/server/Services/CharacterService.cpp:411` |
| `HandleCharacterSpawn` / `HandleCharacterDestoy` | 每个角色出现/消失一次 | `Code/server/Services/CharacterService.cpp:371` / `:339` |
| `HandleSetTime` | 每次时间同步一次 | `Code/server/Services/CalendarService.cpp:92` |

`Initialize` 是最贵的一条:`Code/server/GameServer.cpp:227 m_pWorld->GetScriptService().Initialize(*m_pResources);` 在 `:218 void GameServer::Initialize()` 内,**监听之前**;它唯一的间接调用者是 `Code/server_runner/DediRunner.cpp:37 m_pServerInstance->Initialize();`(`DediRunner` 的构造函数里),而 `DediRunner` 由 `Code/server_runner/main.cpp:233 const auto cpRunner{std::make_unique<DediRunner>(argc, argv)};` 直接构造 ⇒ 抛出即**进程终止,没有监听、没有日志文件、没有 dump**。

Lua 侧只有一处消费者:`plugins/STRPluginMessagingAPI/extras/str-server-resources/strpm-chat-relay/main.lua:158 addEventHandler("onChatMessage", function(entityId, message)` + `:163 cancelEvent("STRPM transport envelope")`。`addEventHandler` 在脚本加载期被调(即 `Initialize` 的 `ForEachManifest` 循环里),`cancelEvent` 在**每条聊天包**上被调。

**修法**:三个文件,13 个函数体。`noexcept` **一处不删**。

第一步不是加 `try`,是**改签名**:`CallCancelableEvent`/`CallEvent` 的第一个参数从 `const String& acName` 改成 `const char* acName`。

```cpp
// Code/server/Services/ScriptService.h:49-57(修复后)
    // The name is a literal at every call site below, and it used to be a
    // `const String&`: that made each of the six noexcept entry points build a
    // temporary String *outside* the callee's try block, on the way in, where
    // nothing can catch it. Taking the pointer keeps the only allocation inside
    // the function that now handles it.
    template <typename... Args>
    std::tuple<bool, String> CallCancelableEvent(const char* acName, Args&&... args) noexcept;

    template <typename... Args> void CallEvent(const char* acName, Args&&... args) noexcept;
```

六个入口全都传字面量,而 `const char*` → `String` 的临时对象是在**调用者的 `noexcept` 函数体里、被调者 `try` 之前**构造的 —— 在被调者体内新加的 catch 结构性地够不着它。改成指针后,唯一的分配(`m_callbacks[acName]` 那一次隐式转换)回到 `try` 里面。`HandlePlayerQuit` 的 `std::string reason` 同理:

```cpp
// Code/server/Services/ScriptService.cpp:193-199(修复后)
void ScriptService::HandlePlayerQuit(ConnectionId_t aConnectionId, Server::EDisconnectReason aReason) noexcept
{
    // A pointer, not a std::string: the reason is one of six literals, and building
    // the string was the last allocation left on the way into CallEvent - on the
    // outside of its try block, in a noexcept function, on the disconnect path
    // where the server is already shedding state.
    const char* reason = "Unknown";
```

第二步是把 catch 放到**分配所在的那一层**:

```cpp
// Code/server/Services/ScriptService.inl:14-43(修复后)
    try
    {
        m_eventCanceled = false;

        auto& callbacks = m_callbacks[acName];

        for (auto& callback : callbacks)
        {
            sol::protected_function pf{callback};
            auto result = pf(std::forward<Args>(args)...);

            if (!result.valid())
            {
                sol::error err = result;
                spdlog::error(err.what());
            }

            if (m_eventCanceled == true)
                return std::make_tuple(true, m_cancelReason);
        }
    }
    catch (...)
    {
        ReportFailure(__FUNCTION__, "ran out of memory while calling the event handlers");
    }

    return {};
```

失败值取「**未取消**」(`return {};`),不是 `std::make_tuple(true, m_cancelReason)`:`true` 那一支要构造的正是刚刚构造失败的 `String`,而且「未取消」让 `HandlePlayerJoin` 不踢人、`HandleChatMessage` 照常广播 —— 把脚本层的失败升级成玩法层的失败(踢人/丢聊天)是更坏的结果。原来的 `return std::make_tuple(false, "");` 也改成值初始化,因为 catch 路径上不能有第二次分配。

第三步是**报告本身不能再失败**。`xmake.lua:47` 钉的是 `"spdlog v1.13.0"`,全仓没有定义 `SPDLOG_NO_EXCEPTIONS`,所以 `spdlog::error` 的格式化会分配:

```cpp
// Code/server/Services/ScriptService.cpp:252-263(修复后)
void ScriptService::ReportFailure(const char* acpContext, const char* acpDetail) noexcept
{
    try
    {
        spdlog::error("{}: {}", acpContext, acpDetail);
    }
    catch (...)
    {
        // Nothing left to say it with. The alternative - letting the report of a
        // failure become the failure - is strictly worse.
    }
}
```

所有 catch 只调它,两个参数都是字面量或 `__FUNCTION__`(静态存储期),所以调用本身不构造任何对象。

`OnUpdate` 里那个只接 `sol::error` 的 catch 被**删掉**而不是保留:

```cpp
// Code/server/Services/ScriptService.cpp:231-243(修复后)
void ScriptService::OnUpdate(const UpdateEvent& acEvent) noexcept
{
    if (m_sandboxes.size() == 0)
        return;

    // This used to be wrapped in a try/catch for sol::error, which was the only
    // thing that could come out of the call at the time. The handler now lives in
    // CallEvent itself - a Lua callback is not the only way that call can fail, so
    // the boundary had to move down to where the allocations are. Keeping a second
    // catch here would also mean keeping an allocating log call inside a noexcept
    // tick handler, which is the very thing being fixed.
    CallEvent("onUpdate", acEvent.Delta);
}
```

`sol::error` 现在由 `.inl` 里的 `if (!result.valid())` 分支处理,所以这一层 catch 剩下的唯一作用是在一个 `noexcept` 的 tick handler 里保留一次会分配的日志调用。

`CancelEvent` 的顺序是刻意的:

```cpp
// Code/server/Services/ScriptService.cpp:282-297(修复后)
    // The flag is set before the reason is copied, so a failure here still cancels
    // the event and only loses its text. That order is deliberate: cancellation is
    // what the caller asked for, the reason is only how it is reported.
    m_eventCanceled = true;

    try
    {
        m_cancelReason = aReason;
    }
    catch (...)
    {
        ReportFailure(__FUNCTION__, "ran out of memory while storing the cancellation reason");
    }
```

`m_eventCanceled = true` 不分配,所以它留在 `try` 外面:取消是调用者要的,理由是报告用的。

**验证**:`git diff --stat` = `Code/server/Services/ScriptService.cpp | 138 ++++++++++++++++++++++++---------` / `Code/server/Services/ScriptService.h | 17 +++-` / `Code/server/Services/ScriptService.inl | 77 +++++++++++++------` / **3 files changed, 168 insertions(+), 64 deletions(-)**,7 个 hunk(`.inl` 1 个 `@@ -1,42 +1,71 @@`、`.h` 1 个 `@@ -46,14 +46,27 @@ struct ScriptService`、`.cpp` 5 个);`ScriptService.inl` 42 行 → 71 行,`ScriptService.h` 76 行 → 89 行,`ScriptService.cpp` 235 行 → 297 行。
`noexcept` 的**声明一处未动**(14 → 14,新增的第 15 个是 `ReportFailure`);`catch` 从 **2 处 → 8 处**,`try` 从 **2 处 → 8 处**,新增的 6 对全在分配点所在的那一层。
`Code/server/xmake.lua:12 add_headerfiles("**.h")`、`:13 add_files("**.cpp")` 覆盖三个文件;`.inl` 经 `Code/server/Services/ScriptService.h:89 #include "ScriptService.inl"` 进入同一个编译单元。
`Code/tests/xmake.lua` 的 target 是 `TPTests`、依赖 `SkyrimEncoding`,**不含 `SkyrimTogetherServer`**,所以服务端的改动在本仓测试里覆盖不到。
**本机不编译**(`docs/PITFALLS.md` §0/§1),所以判据是静态的:13 个函数体的分配点是逐字读出来的,`Code/server/GameServer.h:103/104/106` 三个覆写都不带 `noexcept` 也是逐字读出来的。
### 修复:服务端的任务去重历史只有时间上界、没有条数上界,而键由客户端任选 —— 判据是「同一层楼的任务日志已经写了 4096,而它们是同一类表」

**现象**:没有任何现象。`Code/server/Services/PartyService.h`(pin 141 行)里的 `struct QuestStageDedupHistory` 只有一条退役路径:

```cpp
// Code/server/Services/PartyService.h:79-80(pin)
void inline Expire() noexcept;
Container m_cache{};       // Short, time-ordered, duplicates valid (do they ever happen?)
```

**根因**:`Expire()` 只看时钟。`Code/server/Services/QuestService.cpp:309-321`(pin):

```cpp
const auto expiration = std::chrono::steady_clock::now() - uQuestHistoryExpiration.value_as<std::chrono::milliseconds>();

while (!m_cache.empty() && m_cache.front().timestamp < expiration)
{
    auto& it = m_cache.front();
    spdlog::debug(
        "{}: expiring dedup history entry quest/scene: {:X}, stage: {}, status: {}, by player {:X}", __FUNCTION__,
        it.questOrSceneId.LogFormat(), it.questStage, it.questStatus, it.playerId);
    m_cache.pop_front();
}
```

窗口是 `Code/server/Services/QuestService.cpp:17` 的 `Console::Setting uQuestHistoryExpiration("GameServer:uQuestHistoryExpiration", "Time in milliseconds to retain quest progression changes for deduplication", 30000U);` —— 30 秒。而 `Add` 在三条路径上都是无条件调用的(`:170` leader、`:203` member、`:258` scene begin),键由发送方逐包任选:`Code/encoding/Messages/RequestQuestUpdate.h` 的 `GameId Id; uint16_t Stage; uint8_t Status; uint8_t ClientQuestType;`,而 `GameId` 是 `{uint32_t BaseId; uint32_t ModId;}` 的 128 位值。⇒ 一个不断编新 id 的客户端,在这 30 秒窗口里能塞进多少行只取决于它的发送速率。

**判据(同族对照,载重)**:同一层楼的任务日志已经为这件事写过答案 —— `Code/server/Services/QuestService.h:27-32`:

| 事实 | 实测 |
| --- | --- |
| 同族上限 | `Code/server/Services/QuestService.h:32 static constexpr std::uint32_t kMaxQuestLogEntries = 4096;` |
| 它的注释 | 「A quest log row is keyed by a GameId the client picks, and the only thing that ever retires one is a Stopped update for that same id: a client that keeps inventing ids grows this table for as long as it stays connected.」 |
| 它的满表行为 | `Code/server/Services/QuestService.cpp:71 else if (entries.size() >= kMaxQuestLogEntries)` → `:78 spdlog::warn(...)` → **仍然转发**,只拒绝新键 |
| 全树速率限制 | `git grep -n -iE "ratelimit\|rate_limit\|tokenbucket\|kBurst" -- Code/server` 只命中 `Code/server/Services/PluginMessagingService.{h,cpp}` 的令牌桶 ⇒ 任务包没有任何速率限制 |
| 全树 `kMax*` | `git grep -n -E "kMax[A-Za-z]+\s*=" -- Code/server` 共 7 条:6 条既有 + 本条新增的 1 条 |

两张表的键都是对端任选的 `GameId`,退役路径都只有一条。所以这里取同一个数:**4096**。

**可达性**:包从 `Code/server/GameServer.cpp:261-275` 的 handler lambda(不带 `noexcept`)进,`:274 m_pWorld->GetDispatcher().trigger(PacketEvent<T>(pRealMessage.get(), pPlayer));`,由 `Code/server/Services/QuestService.cpp:23` 的 sink 接到 `:27 void QuestService::OnQuestChanges(const PacketEvent<RequestQuestUpdate>& acMessage) noexcept`。服务端 handler 链上没有任何一层限流,唯一的门是 `Code/server/GameServer.cpp:584` 的单包大小检查。

**修法**:`Code/server/Services/PartyService.h:58` 加 `static constexpr uint32_t kMaxEntries = 4096;`(与隔壁同名同值),`Add` 先查重、满表拒新:

```cpp
// Code/server/Services/QuestService.cpp:348-377(修复后)
    Expire();

    // Every row is keyed by what the sender picked and Expire() looks at the clock alone, so a
    // client that keeps inventing ids grows this deque for the whole expiration window. A row that
    // is already here is the ordinary case - it is what a deduplication history is for - and it is
    // moved to the back instead of being recorded twice: the window is meant to run from the most
    // recent occurrence - the scene master check below asks for "a recent scene change", and a
    // player who re-enters the same scene is exactly the case it is there for - and the deque then
    // only grows from ids nobody has sent before. Past the cap a new id is refused the way a full
    // quest log refuses one; the rows already here keep deduplicating.
    const auto existing = std::find_if(m_cache.begin(), m_cache.end(), [&](const Entry& e) {
        return e.questOrSceneId == acQuestOrSceneId && e.questStage == acQuestStage && e.questStatus == acQuestStatus && e.playerId == acPlayerId;
    });

    if (existing != m_cache.end())
        m_cache.erase(existing);

    else if (m_cache.size() >= kMaxEntries)
    {
        spdlog::warn("{}: dedup history is full ({} entries), not recording quest/scene {:X}, stage: {}, status: {}, by player {:X}", __FUNCTION__, m_cache.size(),
                     acQuestOrSceneId.LogFormat(), acQuestStage, acQuestStatus, acPlayerId);
        return;
    }

    m_cache.emplace_back(acQuestOrSceneId, acQuestStage, acQuestStatus, acPlayerId, acTimeStamp);
```

查重那一半不是为了省内存,是为了**不改变语义**:`Found`/`FoundWPlayerId` 的判定全部发生在 `Add` 之前(`:160`、`:178`、`:202`、`:256`),它们只问「有没有这一行」,第二行对它们没有任何区别。第一版写的是 `return;`(重复即丢弃),被 `:256` 否掉了 —— 那里的注释逐字「Checking for a recent scene change in history keeps us unstuck in case scene end event is missed」,直接丢弃会让「玩家重进同一场景」这一正是它要覆盖的用例看到一条陈旧行;`erase` 加落到末尾的 `emplace_back` 是**刷新那一行的窗口**,不是丢掉它。

**验证**:`git diff --stat` = `Code/server/Services/PartyService.h | 7 +++++++` / `Code/server/Services/QuestService.cpp | 23 +++++++++++++++++++++++` / **2 files changed, 30 insertions(+)**,两个 hunk(`@@ -50,6 +50,13 @@ struct QuestStageDedupHistory`、`@@ -350,5 +350,28 @@ void QuestStageDedupHistory::Add(const QuestOrSceneId& acQuestOrSceneId, const Q`),`PartyService.h` 141 行 → 148 行,`QuestService.cpp` 354 行 → 377 行。
`Code/tests/xmake.lua` 的 target 是 `TPTests`、依赖 `SkyrimEncoding`,**不含 `SkyrimTogetherServer`**,所以服务端的改动在本仓测试里覆盖不到。
**本机不编译**(`docs/PITFALLS.md` §0/§1),所以判据是静态的:`Add` 的三个调用点、`Found`/`FoundWPlayerId` 的四个判定点、以及全树唯一的速率限制都是逐条读出来的。
### 修复:客户端 STRPM 桥的 19 个入口全写着 noexcept,其中 8 个函数体在分配,而它们唯一的出口是同一个 DLL 里的 C 导出 —— 判据是「导出外面没有能接住异常的调用者,所以接住要写在函数体里」

**现象**:没有任何现象。`Code/client/Services/PluginMessagingService.cpp`(pin 405 行)里 **19 个函数签名**都带 `noexcept`,其中 **8 个函数体在分配**:

```cpp
// Code/client/Services/PluginMessagingService.cpp:123(pin,修复前)
STRPM::Result PluginMessagingService::RegisterChannel(const char* acpChannel, STRPM::ReceiveCallback aCallback, void* apUserData, STRPM::ListenerHandle* apOutHandle) noexcept
```

`:141` 逐字 `m_listeners.push_back(Listener{ acpChannel, aCallback, apUserData });`,`:142` 逐字 `m_channels[handle] = acpChannel;`。

**根因**:这 19 个入口没有一个是内部调用 —— 它们全部被 `Code/client/Services/PluginMessagingExport.cpp` 里的函数局部 static 结构体取地址后交给插件:

| 位置 | 写法 |
| --- | --- |
| `Code/client/Services/PluginMessagingExport.cpp:75` | `STRPM_EXPORT STRPM::Result STRPM_CALL STR_QueryPluginMessagingInterface(...) noexcept` |
| `:80` | `STR_QueryPluginMessagingProxyResolver(...) noexcept` |
| `:146` | `STR_QueryPluginMessagingDiagnostics(...) noexcept` |
| `:151` | `STRPM_QueryTransportInterface(...) noexcept` |
| `:25-37` / `:54-65` / `:125-140` | 三个 `s_interface` 的 14 个 lambda,**逐个都写 `noexcept`** |

导出是 C ABI,插件通过 `GetProcAddress` 直接调 —— **外面没有能接住异常的调用者**,而抛出点(本文件)与 `noexcept` 边界(同 DLL 的导出)之间没有任何一层写 `try`。这与 §99 的跨 DLL 情形不同:那里是两个模块各写各的 `noexcept`,这里是同一个模块里从函数体一路裸奔到 `extern "C"`。

**判据(同族对照,载重)**:判据不是「这里会不会耗尽内存」,而是「**这 8 个帧里哪一个是最后一道 `noexcept`**」。逐个查调用链:

| 函数 | pin 行 | 体内分配点 | 它被谁调 |
| --- | --- | --- | --- |
| `FireProxyMapping` | `:63-84` | `:74 Vector<MappingListener> listeners;` + `:79 push_back` | `OnPlayerComponentAdded`/`OnPlayerComponentRemoved`(entt observer) |
| `RegisterChannel` | `:123-148` | `:141 push_back` + `:142 m_channels[handle] =` | `PluginMessagingExport.cpp:30` |
| `UnregisterChannel` | `:150-170` | `:158 const auto channel = it->second;` | `PluginMessagingExport.cpp:31` |
| `Send` | `:172-226` | `:192 request.Channel =` + `:217 PluginData.assign` | `PluginMessagingExport.cpp:32` |
| `SetLocalDisplayName` | `:247-260` | `:258 m_localDisplayName =` | `PluginMessagingExport.cpp:36` |
| `ResolveProxy` | `:290-324` | `:306 m_pWorld->view<FormIdComponent, PlayerComponent>()` | `PluginMessagingExport.cpp:56` |
| `RegisterProxyMappingListener` | `:326-341` | `:339 m_mappingListeners.push_back` | `PluginMessagingExport.cpp:57` |
| `OnPluginMessage` | `:359-405` | `:386 Vector<Delivery> deliveries;` + `:396`/`:400 push_back` | `Code/client/World.cpp:65` 的 dispatcher sink |

同族先例(载重):插件侧那 7 个子模块**没有任何一处 `try`/`catch`**(逐个文件实测 `try=0 catch=0`),而它们全部经这四个导出进来 —— 也就是说这 8 个帧是这条链上唯一能放 `catch` 的地方。

**可达性**:触发频率由调用方决定,不由这条路径决定:
- `FireProxyMapping` 每次玩家实体出现/消失各一次(`Code/client/Services/Generic/CharacterService.cpp:358 OnAssignCharacter`、`:481 OnCharacterSpawn`,`Code/client/Services/Generic/OverlayService.cpp:434 OnPlayerComponentRemoved`,`Code/client/Services/Generic/WeatherService.cpp:110 OnPlayerComponentRemoved` —— **这四个调用帧自己也都写着 `noexcept`**);
- `ResolveProxy` 每个插件每收到一个载荷调一次(七个插件的接收路径全部经它);
- `OnPluginMessage` 每个入站载荷一次。

**修法**:给这 8 个函数体各包一层 `try { … } catch (...) { Log("…"); return <既有失败值>; }`,catch 里只记一行、**不重试**。`noexcept` 一处未改 —— 删掉它只会把终止点挪到导出那一帧。

```cpp
// Code/client/Services/PluginMessagingService.cpp:142-174(修复后)
    try
    {
        std::scoped_lock lock(g_pluginMessagingMutex);

        for (const auto& listener : m_listeners)
        {
            if (listener.Channel == acpChannel)
                return STRPM::Result::kChannelAlreadyRegistered;
        }

        handle = m_nextHandle++;

        // Both rows allocate: the listener owns a copy of the channel name and
        // the handle table owns another.
        m_channels[handle] = acpChannel;
        m_listeners.push_back(Listener{ acpChannel, aCallback, apUserData });

        if (apOutHandle)
            apOutHandle->value = handle;
    }
    catch (...)
    {
        // The registration never completed, so nothing may be left of it. A row
        // without a listener would be invisible - nothing enumerates the handle
        // table - and it would hold a channel name forever.
        {
            std::scoped_lock lock(g_pluginMessagingMutex);
            m_channels.erase(handle);
        }

        Log("a plugin channel registration ran out of memory");
        return STRPM::Result::kNotAvailable;
    }
```

`RegisterChannel` 是唯一需要回滚的一处:两条表都分配,而 `m_channels` **没有任何枚举者**,半写下的行会永远持有一个 channel 名。`handle` 提到 `try` 外初始化,catch 里重新取锁擦掉那一行(`scoped_lock` 已析构,不重入)。

失败值一律沿用契约里**既有**的码(`kNotAvailable`/`kTransportError`/`kTargetNotFound`):七个插件各持一份 `ResultToString`,并且没有一处区分「没找到」与「分配失败」(`plugins/DAVSyncTogether/src/DAVNetworkService.cpp:181`、`plugins/TradeTogether/src/StrMessagingTransport.cpp:440`、`plugins/AnimSyncTogether/src/STRPMClient.cpp:375` 都只看 `kOk`/`kTargetNotFound`/`kNotAvailable`)⇒ 新增一个码的收益为零。

**验证**:`git diff --numstat` = `161	67	Code/client/Services/PluginMessagingService.cpp` / **30 个 hunk** / 405 行 → **499 行**;`noexcept` 出现次数 **19 → 19**(签名一处未动),`try`/`catch` **0/0 → 9/9**。
`Code/client/xmake.lua:21` 的 `add_files("**.cpp|Games/Skyrim/**")` 覆盖本文件与 `PluginMessagingExport.cpp`,两者都在 `SkyrimTogetherClient` 与 `SkyrimTogetherClientLegacy` 的编译闭包内。
`Code/tests/xmake.lua` 的 target 是 `TPTests`、依赖 `SkyrimEncoding`,**不含 `SkyrimTogetherClient`**,所以客户端的改动在本仓测试里覆盖不到。
**本机不编译**(§0/§1),所以判据是静态的:8 个函数体各自的分配点是逐字读出来的,而它们的调用者全是 `noexcept` 或 C 导出。

### 修复:服务端的邀请过期清理遍历的是一个没有任何实体携带的组件,所以一次都没有执行过 —— 判据是「全树只有这一个 view 指向没有挂载点的组件」

**现象**:没有任何现象。`Code/server/Services/PartyService.cpp:88`(修复前)逐字

```cpp
    auto view = m_world.view<PartyComponent>();
    for (auto entity : view)
    {
        auto& partyComponent = view.get<PartyComponent>(entity);
```

这段代码的唯一作用是每 10 秒(`:86 m_nextInvitationExpire = cCurrentTick + 10000;`)把 `PartyComponent::Invitations` 里过期的邀请擦掉。

**根因**:`PartyComponent` 在这棵树里**不是实体组件,而是 `Player` 的一个成员**。`Code/server/Game/Player.h:54` 逐字 `PartyComponent m_party;`。
全树唯一曾经的挂载点是 `OnPlayerJoin` 里的 `m_world.emplace<PartyComponent>(acEvent.Entity);`,它在 `663a187e Refactor player` 里被删除 —— 同一个提交把 `PartyService.h` 的 `Vector<entt::entity> Members;` 改成 `Vector<Player*> Members;`、把 `BroadcastPlayerList(std::optional<entt::entity>)` 改成 `BroadcastPlayerList(Player*)`,把 `emplace<PartyComponent>` 换成 `Player` 行上的 `m_party` 成员,而 `:88` 这个 view 被漏掉了。
`git log --oneline -S "view<PartyComponent>"` 只给出 `9e0c078f`,说明它从被写下的那天起就没改过。

一个 view 遍历的是「携带该组件的实体」;没有实体携带它,循环体一次都不执行 ⇒ **邀请从来没有被擦除过一次**。

**判据(同族对照,载重)**:判据不是「这段循环写错了」,而是「**全树只有这一个 view 指向没有挂载点的组件**」—— 对着 `Code/` 全树取差集,结果只有一条:

| 事实 | 实测 |
| --- | --- |
| 全树 `emplace/emplace_or_replace/try_emplace/replace/insert_or_assign/emplace_back<PartyComponent>` | **0 处** |
| 全树 `view<PartyComponent>` | **1 处**(`Code/server/Services/PartyService.cpp:88`) |
| 被 view 的组件总数 | 22 个,只有 `PartyComponent` 落在差集里 |
| 唯一读者 | 就是这个 view(`Invitations` 在客户端 `Code/client/Services/PartyService.h:30` 有一对同名的,是另一个成员) |
| 同族正确写法 | `GetParty()` 在 `Code/server` 有 **29 处**读者:`GameServer.cpp:788/810/845`、`PartyService.cpp:47/52/64/116/138/163/204/212/249/260/284/285/332/333/391`、`QuestService.cpp:39/175/215/231`、`MapService.cpp:26/37`、`OverlayService.cpp:45/47/118`、`CharacterService.cpp:556`、`WeatherService.cpp:34` —— **全部走 `Player*`** |

**可达性**:写入点是 `Code/server/Services/PartyService.cpp:303`(`OnPartyInvite` 里的 `inviteePartyComponent.Invitations[pInviter] = cExpiryTick;`),读取点是 `:330`(`OnPartyAcceptInvite` 里的 `if (selfPartyComponent.Invitations.count(pInviter) == 0) return;`)。
上界由写者决定,不由传输决定:键是 `Player*`,一个队伍的玩家数 ≤ `uMaxPlayerCount`(默认 8),玩家断线时整个 `Player`(含 `m_party`)随 `PlayerManager::Remove` 销毁 ⇒ **这不是无界增长,是过期语义失效** —— 邀请在 `cExpiryTick = GetTick() + 60000` 之后仍然被服务端接受。
客户端 `Code/client/Services/Generic/PartyService.cpp:66-74` 早因本地 `m_invitations` 过期而拒发,但服务端没有对应门禁。

**修法**:改成遍历玩家管理器,与其余 29 处读者一致 —— `Code/server/Services/PartyService.cpp:88-110`(修复后)逐字

```cpp
    // The invitations are a member of Player, not a component of an entity. PartyComponent used
    // to be attached with emplace<PartyComponent> in OnPlayerJoin; that call went away when the
    // party moved onto the player row (Code/server/Game/Player.h:54) and the view was left
    // behind. A view over a component no entity carries iterates nothing, so no invitation was
    // ever expired: the entry a leader wrote in OnPartyInvite stayed for as long as the invitee
    // stayed connected, and OnPartyAcceptInvite kept accepting an invitation the client had
    // already dropped. Walking the player manager is what the other 29 readers of GetParty() do.
    for (Player* pPlayer : m_world.GetPlayerManager())
    {
        auto& partyComponent = pPlayer->GetParty();
        auto itor = std::begin(partyComponent.Invitations);
        while (itor != std::end(partyComponent.Invitations))
        {
            if (itor->second < cCurrentTick)
            {
                itor = partyComponent.Invitations.erase(itor);
            }
            else
            {
                ++itor;
            }
        }
    }
```

擦除循环体逐字照抄 pin,只把取容器的两行换掉。`GetPlayerManager()` 的 range-for 在本仓另有 **25 处**先例(`Code/server/GameServer.cpp:328/722/731/762/805/837/1049` 等),`PlayerManager::Iterator`(`Code/server/Game/PlayerManager.h:9-26`)**只前进不删除**,所以遍历中改 `Player` 的成员是安全的。

**验证**:`git diff --stat` = `Code/server/Services/PartyService.cpp | 12 +++++++++---` / `1 file changed, 9 insertions(+), 3 deletions(-)`,唯一 hunk `@@ -85,10 +85,16 @@ void PartyService::OnUpdate(const UpdateEvent& acEvent) noexcept`。
`Code/tests/xmake.lua` 的 target 是 `TPTests`、依赖 `SkyrimEncoding`,**不含 `SkyrimTogetherServer`**,所以服务端的改动在本仓测试里覆盖不到。
**本机不编译**(`docs/PITFALLS.md` §0/§1),所以判据是静态的:全树差集只剩这一条,而 `view` 的语义就是「携带该组件的实体集合」。

### 修复:STRPM 桥的映射函数写着 noexcept,而其中三个跨 DLL 调用会分配的导出、五个自己建容器,每个都只被 VEH 与工作线程调用 —— 判据是「noexcept 的边界画在哪一层,就要把 try 写在哪一层」

**现象**:没有任何现象。`plugins/STRPluginMessagingAPI/src/STRPMProxyResolverBridge.h`(pin 1165 行)里八个函数连着写 `noexcept`,其中五个体内会分配:

```cpp
        void ObserveSender(
            STRPM::ConnectionID connectionID,
            std::uint32_t playerId) noexcept
```

`:773` 逐字 `g_connectionToPlayer[connectionID] = playerId;`;`:792` 逐字 `std::vector<STRPM::ConnectionID> connections;`(`ObservePlayerProxy`,另有 `:795` 的 `g_playerToForm[playerId] = formID;` 与 `:799` 的 `connections.push_back(connectionID);`);`:815`/`:827` 同形(`RemovePlayerProxy`);`:850` 逐字 `std::vector<std::pair<STRPM::ConnectionID, STRPM::ProxyFormID>> mappings;` 加 `:857` 的 `mappings.emplace_back(connectionID, form->second);`(`FlushMappings`)。

**根因**:这三层各自都能抛,而 `noexcept` 写在最里面那一层:

1. **跨 DLL 的一层**。`:732` 的 `ReportResolvedMapping`、`:748` 的 `RemoveResolvedMapping`、`:754` 的 `ClearRuntimeMappings` 自己不做容器,但它们用 `:711-730` 的三个取址函数(`GetModuleHandleW(L"STRPluginMessagingAPI.dll")` + `GetProcAddress`)取到 `src/STRPMProxyResolverRuntime.h` 的三个导出 —— `:254 STRPM_ReportProxyMapping`、`:261 STRPM_RemoveProxyMapping`、`:267 STRPM_ClearProxyMappings`。
   那三个导出**都不写 `noexcept`**,体内却各有一份 `std::vector<Listener> listeners;`(`:107`/`:153`/`:177`)与一次 `listeners = _listeners;`(`:136`/`:167`/`:184`)。
   ⇒ **抛出点在 API DLL,而 `noexcept` 边界在桥 DLL**;异常穿过 `extern "C"` 边界之后落进的第一个 `noexcept` 帧,就是这三个薄壳。
2. **容器的一层**。`ObserveSender`/`ObservePlayerProxy`/`RemovePlayerProxy`/`FlushMappings` 体内各建一个容器(上引四行)。
3. **调用者的一层**。这八个函数的调用者里既有 `noexcept` 也有非 `noexcept`:
   `src/STRPMProxyResolverTrace.h:185`/`:194` 调 `ObservePlayerProxy`/`RemovePlayerProxy`,而它们在 `:158 bool CaptureLifecycleCall(CONTEXT* context) noexcept` 体内,唯一调用者是 `:265`(在非 `noexcept` 的 `ExceptionHandler` 里)⇒ 终止落在 `CaptureLifecycleCall`;
   `src/STRPMProxyResolverBridge.h:886` 调 `ObserveSender`,它在 `:864 void ObserveIncomingEnvelope(CONTEXT* context) noexcept` 体内,唯一调用者是 `:949`(桥自己的 `ExceptionHandler` 的 `EXCEPTION_BREAKPOINT` 分支);
   `:990`/`:996` 调 `ObservePlayerProxy`/`RemovePlayerProxy`、`:1029` 调 `ClearMappings`(同一个 `ExceptionHandler` 的 `kDisconnected` 分支)、`:1132` 调 `FlushMappings`(在 `:1088 void Worker(std::stop_token token)` 的每秒循环里)、`src/STRPMProxyResolverBootstrapV2.h:312` 同形 —— **两个 Worker 都不是 `noexcept`**。

**判据(同族对照,载重)**:判据不是「这些函数会不会分配」,而是「**`noexcept` 画在哪一层,异常就会在哪一层的帧里变成 terminate**」。
桥 DLL 这一侧把 `noexcept` 写在了最内层五个函数上,而最外层能接住异常的两个 VEH 处理器与两个 Worker 都没写;API DLL 那一侧的三个导出干脆没写。
所以「删掉桥侧的 `noexcept`」不是修法:它只是把终止点从被调者挪到 `CaptureLifecycleCall`(那条链上的上一层恰好写了 `noexcept`),而另外三条链上的终止点则外移 —— **两种结果都不是「有人处理了」**。
同族先例是同一个文件里的四处(`:203 SnapshotProcessMemory`、`:627 ReadString`、`:652 PeekRawEnvelope`、`:1139 Start()`),它们的形状统一是「体内 `try` + `catch (...)` + 记一行日志 + 返回失败值」,不重试。`0004`/`0005`/`0006` 三个既有补丁给的都是同一个形状。

**修法**:在 `:760` 前插 8 行理由注释,然后**给这八个函数各包一层 `try { … } catch (...) { Log(…); }`** —— 注意是八个而不是五个:
三个薄壳(`ReportResolvedMapping`/`RemoveResolvedMapping`/`ClearRuntimeMappings`)虽然自己不建容器,但它们才是跨 DLL 那一跳的执行者,漏掉它们,异常就会在它们自己的帧里终止。
函数体其余部分逐字照抄 pin,只整体缩进 4 格;失败时只记一行日志,不重试 —— 丢一条映射退化成一次漏映射,而这个解析器本来就容忍漏映射。

**验证**:`0007` 的 `git apply --check --cached` 退 **0**、前向 `git apply --check` 退 **0**;
应用后 `git apply --check --reverse` 退 **0**,再次前向 `--check` 退 **1** 并报
`error: patch failed: src/STRPMProxyResolverBridge.h:729` 与 `error: src/STRPMProxyResolverBridge.h: patch does not apply`;
补丁里的 pin blob 哈希与 `git rev-parse :src/STRPMProxyResolverBridge.h` 相等(`55eabd3`);
累积校验 `0001`…`0007` 依次退 **0**,`git diff --stat` 空、`git diff --cached --stat` = **13 files changed, 526 insertions(+), 213 deletions(-)**。
`python Code\plugins\tools\plugin_patches.py verify` → `PATCH VERIFY OK` / **37** patch(es);`… check` → `PATCH CHECK OK`。
**本机不编译**(`docs/PITFALLS.md` §0/§1),所以「这条路径到底会不会耗尽内存」不可判定;
判据不是「会不会发生」,而是「发生之后这八个帧里哪一个是最后一道 `noexcept`」。

**交付**:`Code/plugins/patches/STRPluginMessagingAPI/0007-keep-an-allocation-failure-out-of-the-proxy-resolver-mapping-table.patch`
(12270 B / 282 行,单文件单 hunk `@@ -729,136 +729,204 @@`,`src/STRPMProxyResolverBridge.h` `55eabd3`→`34dcb17`)。
The plugin is a submodule this project does not own and cannot push, and CI runs
`git submodule update --init --force --recursive`, which discards a working-tree only change.
It is therefore shipped as the patch above, which `plugin_patches.py` applies before configuring and building.

### 修复:STRPM 的两份日志重定向冒充 `fopen_s` 却继续写着 noexcept,而它们改用的 std::filesystem 每写一行日志都要分配 —— 判据是「替换一个 C 函数时,noexcept 要按替换后的实现重判,不能按被替换的签名继承」

**现象**:没有任何现象。`plugins/STRPluginMessagingAPI/src/STRPMLogRedirect.h:19-22` 逐字

```cpp
inline errno_t STRPMRedirectFopen(
    FILE** file,
    const char* filename,
    const char* mode) noexcept
```

该文件在 `:76` 用 `#define fopen_s STRPMRedirectFopen` 把自己顶替掉 `fopen_s`,而
`plugins/STRPluginMessagingAPI/CMakeLists.txt:423` 逐字
`/FI${CMAKE_CURRENT_SOURCE_DIR}/src/STRPMLogRedirect.h` 把它强制包含进 `STRPluginMessagingBridge` 的每一个 TU;
`:415` 用同样的 `/FI` 把 `src/STRPMDiagnosticLogRedirect.h` 注入 `STRPluginMessagingDiagnostic`。
两份头文件没有任何源文件 `#include`(`git grep -n "LogRedirect" -- *.cpp *.h CMakeLists.txt` 只命中 `CMakeLists.txt` 的那三行),
也就是说 **改这两份头文件就等于改两个目标的全部日志出口**,而它们此前从未被任何补丁触及。

**根因**:`src/STRPMLogRedirect.h:39` 逐字 `std::filesystem::path logDirectory(documents);`,
后面是 `:42-44` 的三次 `logDirectory /= …`、`:47` `std::filesystem::create_directories(logDirectory, error);`、
`:50` `const auto logPath = logDirectory / L"STRPluginMessagingBridge.log";` —— **四次分配**,全部发生在 `fopen_s` 被调用的那一刻。
被替换的 `fopen_s` 是 C 运行时函数,它不抛;替换它的这个函数会抛,却把原签名的 `noexcept` 一起继承了下来。

**判据(同族对照,载重)**:不是「要不要加 try/catch」,而是「**谁在 `noexcept` 后面调用它**」。
经这一个宏走到那四次分配的调用者有九个,九个全是 `noexcept`:
`src/STRPluginMessagingBridgeReceive.cpp:124` 的 `void Log(const char* text) noexcept`(`:129` 就是那次 `fopen_s`)与
`:134` 的 `void LogAddress(const char* label, std::uintptr_t value) noexcept`;
`src/STRPluginMessagingBridgeSKSE.cpp:43` 的 `void LogIdentity(const char* format, ...) noexcept`;
`src/STRPMProxyResolverBridge.h:126` 的 `void Log(const char* format, ...) noexcept`;
`src/STRPMChatUiSuppress.h:73` 的 `inline void Log(const char* text) noexcept`、`:83` 的 `inline void LogAddress(…) noexcept`、
`:508` 的 `inline DWORD WINAPI Worker(void*) noexcept`;
`src/STRPMChatUiSuppressV3.h:282` 的 `inline void MarkPendingEnvelope() noexcept` 与 `:361` 的 `inline void LogExecuteAsyncResolution(…) noexcept`。
所以「把 `noexcept` 删掉」不是修法:异常会立刻撞进上一层 `noexcept` 的边界,终止只是从被调者挪到调用者,
`std::terminate` 与「没有崩溃转储」两件事一件不少。同族先例就在隔壁 —— `src/STRPMRuntimeLoadRedirect.h` 用同样的宏替换,
既有补丁 `0005` 给它的选择也是 `try { … } catch (...) { }` 加落到本来就有的兜底,而不是删 `noexcept`。

**修法**:两份头文件各把 `SHGetKnownFolderPath` 守卫块里的那一段包进 `try { … } catch (...) { }`,
失败时落到函数本来就有的 `:71 return ::fopen_s(file, filename, mode);` —— **不改去向,只让失败不再终止**。两处细节:

1. `CoTaskMemFree(documents)` 原来紧跟在 `std::filesystem::path logDirectory(documents);` 之后;那行一旦抛出,`documents` 就泄漏。
   所以释放被移进 `try` 并紧跟一句 `documents = nullptr;`,`try` 之后再补 `if (documents != nullptr) CoTaskMemFree(documents);` —— 既不泄漏也不双重释放。
2. 两份文件的缩进风格不同(`src/STRPMLogRedirect.h:53`/`:59` 是同行花括号,`src/STRPMDiagnosticLogRedirect.h:53-54`/`:60-61` 是另起一行),
   补丁按原文各自保留,没有顺手统一。

诊断侧更窄:它的唯一 TU `src/STRPluginMessagingDiagnosticSKSE.cpp` 全文件 24 个函数**一个 `noexcept` 都没有**,
所以那里的危险只来自 `STRPMDiagnosticRedirectFopen` 自己;但这一份同样必须包,否则终止点就落在函数体内。

**验证**:`0006` 的 `git apply --check --cached` 退 **0**、前向 `git apply --check` 退 **0**;
应用后 `git apply --check --reverse` 退 **0**,再次前向 `--check` 退 **1** 并报
`error: patch failed: src/STRPMLogRedirect.h:36` 与 `error: patch failed: src/STRPMDiagnosticLogRedirect.h:36`;
补丁里两个 pin blob 哈希与 `git rev-parse :<path>` 相等(`src/STRPMLogRedirect.h` `3e63576`、`src/STRPMDiagnosticLogRedirect.h` `48d002c`);
累积校验 `0001`…`0006` 依次退 **0**,`git diff --stat` 空、`git diff --cached --stat` = **13 files changed, 386 insertions(+), 141 deletions(-)**。
`python Code\plugins\tools\plugin_patches.py verify` → `PATCH VERIFY OK` / **36** patch(es);`… check` → `PATCH CHECK OK`。
**本机不编译**(`docs/PITFALLS.md` §0/§1),所以「这条路径到底会不会耗尽内存」不可判定;
判据不是「会不会发生」,而是「发生之后这行日志还剩不剩得下一条记录」。

**交付**:`Code/plugins/patches/STRPluginMessagingAPI/0006-keep-an-allocation-failure-out-of-the-log-redirect.patch`
(7300 B / 169 行,两个文件:`src/STRPMLogRedirect.h` 的 `@@ -36,35 +36,54 @@`、`src/STRPMDiagnosticLogRedirect.h` 的 `@@ -36,37 +36,54 @@`)。
The plugin is a submodule this project does not own and cannot push, and CI runs
`git submodule update --init --force --recursive`, which discards a working-tree only change.
It is therefore shipped as the patch above, which `plugin_patches.py` applies before configuring and building.
### 修复:DAV 解码一件盔甲的激活 addon 列表时长度由对端决定,而每个元素都要过一次引擎查找 —— 判据是「上限要绑到自己的发送侧能产出什么,而不是绑到传输层允许什么」

**现象**:`plugins/DAVSyncTogether/src/NetworkProtocol.cpp:142-186 DecodeArmorState` 解 `DAVSTATE|armo=…|state=…|variant=…|active=…` 时,`:168-183` 按逗号逐个切分 `active=` 字段并把每个 token 追加进结果:

```cpp
        std::string_view active = *activeToken;
        while (!active.empty()) {
            const auto comma = active.find(',');
            const auto token = active.substr(0, comma);
            if (!token.empty()) {
                const auto identity = DecodeIdentity(token);
                if (!identity) {
                    return std::nullopt;
                }
                result.activeArmorAddons.push_back(*identity);
            }
            if (comma == std::string_view::npos) {
                break;
            }
            active.remove_prefix(comma + 1);
        }
```

循环次数只由对端给的 `active=` 有多长决定,**没有任何计数上限**。而每个 token 都要走一次 `DecodeIdentity`(`:84-104`),那里 `:100-102` 逐字
`if (auto* resolved = identity.Resolve()) { identity.runtimeFormID = resolved->GetFormID(); }` ——
`FormIdentity::Resolve()`(`plugins/DAVSyncTogether/src/FormIdentity.cpp:18-29`)在 `plugin` 非空时走
`RE::TESDataHandler::GetSingleton()->LookupForm(localFormID, plugin)` ⇒ **一个 token 一次按「插件名 + 局部 ID」的引擎查找**,而插件名也是对端给的。

**根因**:这个字段的长度是对端填的,而解码器把它当成真的。全 DAV 只有两个 `kMax*` 常量,都来自 STRPM 头
(`plugins/DAVSyncTogether/include/STRPluginMessagingAPI/STRPluginMessagingAPI.h:21 kMaxPayloadBytes = 24 * 1024`),**插件自己一处上限都没有**。

**判据(同族对照,载重)**:这不是「要不要防」的问题,是「防到哪个数」的问题 —— 上限必须绑到**我们自己的发送侧能产出什么**。
发送侧就在同一个文件:`EncodeArmorState`(`NetworkProtocol.cpp:116-140`)`:126-131` 逐字
`for (std::size_t i = 0; i < armor.activeArmorAddons.size(); ++i) {`,而这个 vector 由
`plugins/DAVSyncTogether/src/DAVProbe.cpp:175-182` 填 —— 从 `VisitArmorNodes`(`:391-412`,递归走 3D 节点树、按节点名里的 `(ARMA)/(ARMO)` 归到该件盔甲名下)取那一份,
`:182 SortAndUniqueIdentities(state.activeArmorAddons);` 去重。
⇒ 一件盔甲的 addon 数 = 这件盔甲当前挂了多少个 ARMA 节点,原版与常见 mod 都在个位到十几个的数量级;**64 已是它的数倍**。
传输层那个 24 KiB 换算下来约一万两千个 token,只能用来判「包大不大」,不能用来判「一件盔甲该有几个 addon」。
同族的既定修法是 `Code/plugins/patches/IEDSyncTogether/0002-bound-the-live-peer-table-and-the-decoded-object-list.patch` 的 `kMaxRemoteStates = 64` / `kMaxObjects = 512`、
`Code/plugins/patches/MorphSyncTogether/0006-bound-a-single-morph-chunk-and-the-values-a-snapshot-may-carry.patch` 的 `kMaxMorphValues`、
`Code/plugins/patches/TradeTogether/0002-bound-the-peer-name-and-request-id-tables.patch` —— 四处都是同一句话:**列表长度由对端给,解码器必须自己封顶**。

**修法**:整包拒收而不是截断。`plugins/DAVSyncTogether/src/NetworkProtocol.h:15` 的 `RemoteArmorState` 里加

```cpp
        static constexpr std::size_t kMaxActiveArmorAddons = 64;
```

`src/NetworkProtocol.cpp:172` 的 `if (!token.empty()) {` 之后加

```cpp
                if (result.activeArmorAddons.size() >= RemoteArmorState::kMaxActiveArmorAddons) {
                    return std::nullopt;
                }
```

**为什么是拒收而不是截断**:截断后的列表会被下游当成一份完整状态。`plugins/DAVSyncTogether/src/DAVNetworkService.cpp:164-174` 逐字
`bool activeValid = true; for (auto& identity : message->activeArmorAddons) { … if (!addon) { activeValid = false; … } }` ——
`activeValid` 只问「列表里每个元素解没解开」,**不问「列表全不全」**;列表短一截照样 `:185 const bool valid = resolvedArmor != nullptr && activeValid;` 为真。
而拒收路径本来就有:`DAVNetworkService.cpp:154-158` 逐字
`auto message = DecodeArmorState(received.payload); if (!message) { SKSE::log::warn("DAVST STRPM RX malformed payload connection={} sequence={}", received.connectionID, received.sequence); return; }`
⇒ 返回 `std::nullopt` 不需要新增任何分支、日志或调用点改动。

**交付**:`Code/plugins/patches/DAVSyncTogether/0003-bound-the-active-armor-addon-list-a-single-payload-may-carry.patch`
(1405 B / 33 行,2 个文件各 1 个 hunk;`src/NetworkProtocol.cpp` `6d0b2bf`→`a9bc4fe`、`src/NetworkProtocol.h` `ff1e445`→`c6ce500`)。
The plugin is a submodule this project does not own and cannot push, and CI runs
`git submodule update --init --force --recursive`, which discards a working-tree only change.
It is therefore shipped as the patch above, which `plugin_patches.py` applies before configuring and building.


### 修复:OStimNG 的输入回调先查一次「玩家线程在跑」、再去取指针,两次查询之间线程会被删掉 —— 判据是「判空和用指针之间不能再隔一次查询」

**现象**:按场景快捷键时崩溃,前提是那一帧恰好有线程被停。`plugins/OStimNG/skse/src/Events/EventListener.cpp:81` 逐字
`if (Threading::ThreadManager::GetSingleton()->playerThreadRunning()) {`,`:83` 再逐字 `Threading::Thread* playerThread = Threading::ThreadManager::GetSingleton()->getPlayerThread();` ——
这是对同一把 `m_threadMapMtx` 的**两次独立查询**,`playerThreadRunning()` 返回 `true` 不保证紧接着的 `getPlayerThread()` 还拿到同一个指针。

**根因**:两次查询之间线程可以被删掉。`ThreadManager::stopThreadNoLock`(`plugins/OStimNG/skse/src/Core/ThreadManager.cpp:204-219`)在后台线程里执行
`:209 UI::UIState::GetSingleton()->HandleThreadRemoved(thread);` → `:210 m_threadMap.erase(threadID);` → `:211 thread->close();` → `:212 delete thread;`,
而输入回调在游戏线程(`plugins/OStimNG/skse/src/Main.cpp:66 RE::BSInputDeviceManager::GetSingleton()->AddEventSink(Events::EventListener::GetSingleton());`)⇒ **写指针的线程不是读指针的线程**。
`:83` 拿到的指针在 `:84` 立刻被 `playerThread->getActorCount()`/`getCenter()` 解引用;而 `ThreadManager::getPlayerThread()` 本身**从不判空**(它在 `m_threadMap` 里找不到 `playerThread` 时返回 `nullptr`)。
同族里另外四处(`:149`/`:151`/`:155`/`:160`)连 `playerThreadRunning()` 这道门都没有,直接 `getPlayerThread()->…`。
其中 `:84` 那条更重:`Furniture::selectFurniture(..., true, [playerThread](GameAPI::GameObject furniture) { playerThread->changeFurniture(furniture, nullptr); })` ——
`plugins/OStimNG/skse/src/Furniture/Furniture.cpp:146 if (alwaysAsk || MCM::MCMTable::selectFurniture())` 恒真,而 `:155` 走 `GameAPI::Game::showMessageBox(..., callback)`;
`plugins/OStimNG/skse/src/GameAPI/Game.cpp:31-58` 的两条出口(UIExtensions 的 `DispatchStaticCall` 与 `MessageBoxMenu::QueueMessage`)**都是异步** ⇒
按值捕获的 `playerThread` 在**后续帧**才被解引用,那时 `delete thread` 早已执行。

**判据(同族对照,载重)**:只隔 16 行就是正确写法 ——
`EventListener.cpp:165` 逐字 `Threading::Thread* thread = Threading::ThreadManager::GetSingleton()->getPlayerThread();`、`:166 if (thread) {`;
`plugins/OStimNG/skse/src/Core/Thread.cpp:624-640 Thread::stopFaded()`(行号是 **pin 的**;工作区里该函数在 `:687-703`,因为 0001–0004 先给这个文件加了 63 行)里那条 detached 线程逐字 `Thread* thread = ThreadManager::GetSingleton()->getPlayerThread(); if (thread) { thread->stop(); }`;
`plugins/OStimNG/skse/src/Core/Thread/ThreadFurniture.cpp:15-26` 同形。⇒「取一次局部、就地判空」是这个文件与这个族的既定写法,`:83`/`:149`/`:151`/`:155`/`:160` 是 5 处例外。

**修法**:不改任何公开签名,五处一律「取局部 → 判空 → 用局部」;`:84` 那条额外**去掉按值捕获** ——
lambda 改为在触发时重新 `getPlayerThread()` 并判空,而不是把一个可能已经析构的指针搬进后续帧。

**交付**:`Code/plugins/patches/OStimNG/0005-keep-a-deleted-player-thread-out-of-the-input-handler.patch`
(3465 B / 54 行,单文件 2 个 hunk;`skse/src/Events/EventListener.cpp` `796ffd3`→`1952eec`,212 行)。
The plugin is a submodule this project does not own and cannot push, and CI runs
`git submodule update --init --force --recursive`, which discards a working-tree only change.
It is therefore shipped as the patch above, which `plugin_patches.py` applies before configuring and building.

### 修复:OStimNG 的家具搜索把选项上限算出来却不用,把半径内的家具全部塞进消息框 —— 判据是「算出来的上限必须出现在唯一的消费者身上」

**现象**:家具搜索半径内超过 8 件家具时,消息框收到的按钮数超过游戏上限。`plugins/OStimNG/skse/src/GameAPI/Game.h:17` 逐字 `inline static int getMessageBoxOptionLimit() { return 9; }`(含 `$ostim_message_none` 那一项)。
上限是**游戏本体**的:`plugins/OStimNG/skse/src/GameAPI/Game.cpp:56 messagebox->buttonText.push_back(text.c_str());` 往 `RE::MessageBoxData` 里逐条塞按钮,而这里传进去的 `options` 长度就是按钮数。

**根因**:`plugins/OStimNG/skse/src/Furniture/Furniture.cpp:149` 逐字
`int max = std::min(static_cast<int>(furniture.size()), GameAPI::Game::getMessageBoxOptionLimit() - 1);`,
而 `:150` 逐字 `for (int i = 0; i < furniture.size(); i++) {` —— `max` 算出来之后**一次都没被读过**;`:151`/`:152` 把 `furniture[i].first->name` 与 `furniture[i].second` 成对塞进 `options`/`objects`,于是按钮数与 `objects` 长度都等于 `furniture.size()`。
全树 `std::min(static_cast<int>` 只有这一处命中 ⇒ 不是「抄错了参数顺序」,是「压根没用」。
两个兄弟用了:`Core/ThreadStarter/DebugNPCThreadStarter.cpp:33` 与 `Core/ThreadStarter/PlayerThreadStarter.cpp:108` 都逐字
`int max = std::min<int>(GameAPI::Game::getMessageBoxOptionLimit() - 1, actors.size());`,并在 `while (i < max)` 里读它。

**修法**:`:150` 改为 `for (int i = 0; i < max; i++) {` —— `options` 与 `objects` 同步截断,`:156 if (result == 0 || result > objects.size())` 的边界判断因此保持成立(两者长度始终一致)。

**交付**:`Code/plugins/patches/OStimNG/0006-honour-the-message-box-option-limit-when-listing-furniture.patch`
(712 B / 14 行,单文件 1 个 hunk;`skse/src/Furniture/Furniture.cpp` `223b6bf`→`ba08674`,198 行)。
The plugin is a submodule this project does not own and cannot push, and CI runs
`git submodule update --init --force --recursive`, which discards a working-tree only change.
It is therefore shipped as the patch above, which `plugin_patches.py` applies before configuring and building.

- 三重校验实跑(cwd = `plugins/OStimNG`):两个补丁 `git apply --check --cached` 各退 **0**(能打到 pin)、前向 `git apply --check` 各退 **0**(未应用时干净);
  合并应用后 `git apply --check --reverse` 退 **0**(与工作区一致);再次前向 `--check` 退 **1** 并报 `error: patch failed: skse/src/Events/EventListener.cpp:81` 与 `error: patch failed: skse/src/Furniture/Furniture.cpp:147`(已应用,符合预期)。
- 累积校验:`0001`…`0006` 依次 `git apply --cached` 全部退 **0**,`git diff --cached --stat` = **18 files changed, 408 insertions(+), 151 deletions(-)**,
  `git diff --stat` 在补丁全部应用到索引后无输出,随后 `git reset -q` 复原。
- `python Code\plugins\tools\plugin_patches.py verify` → `PATCH VERIFY OK` / **34** patch(es);`… check` → `PATCH CHECK OK`。
- **本机不编译**(`docs/PITFALLS.md` §0/§1,`plugins/OStimNG/skse/AGENTS.md` 逐字「do not attempt to build c++ or cmake」),所以「两次查询之间线程会不会真的被删掉」不可判定;
  判据不是「会不会发生」,而是「发生之后 `:84` 读的是空指针还是已析构对象」—— 修复前两条都有。

### 修复:OStimNG 的菜单在后台线程删掉线程对象之后仍然解引用它 —— 判据是「判空要判在解引用那一刻,不是判在函数入口」

**现象**:没有任何现象,直到停线程与菜单读取撞在同一帧。UI 层持有两个 `public` 裸指针 ——
`plugins/OStimNG/skse/src/UI/UIState.h:49-50` 逐字 `Threading::Thread* currentThread;` 与 `Graph::Node* currentNode;`,两个都没有初始化器;
`UIState::HandleThreadRemoved`(`plugins/OStimNG/skse/src/UI/UIState.cpp:106-110`)只清 `currentThread`,**`currentNode` 一次都不清**。
清空写在 `ThreadManager::stopThreadNoLock`(`plugins/OStimNG/skse/src/Core/ThreadManager.cpp:204-219`)里,顺序逐字是
`:209 UI::UIState::GetSingleton()->HandleThreadRemoved(thread);` → `:210 m_threadMap.erase(threadID);` → `:211 thread->close();` → `:212 delete thread;`,
而它唯一的调用点 `:51` 在 `:38` 起的后台线程 `m_excitementThread` 里(`:44 if (!GameAPI::Game::isPaused())` 之后)—— **写指针的线程不是读指针的线程**。

**根因**:全树 15 处解引用 `currentThread`/`currentNode` 时不判空,其中 5 处落在延迟执行的 lambda 里 ——
`UI/OStimMenu.h:13-19` 的 `Update()` 逐字 `while (!_taskQueue.empty()) { auto& task = _taskQueue.front(); task(); _taskQueue.pop(); }`,
`:63-67 QueueUITask` 只入队,`UI/OStimMenu.cpp:69-74 AdvanceMovie` 才调 `Update()` ⇒ **队列里的 lambda 在下一帧执行,与入队时刻不同步**;`SKSE::GetTaskInterface()->AddTask(...)` 同理。
于是「帧 N 入队、后台线程在 N 与 N+1 之间停线程、帧 N+1 执行」这三步就构成一次空指针解引用:
`UI/Scene/SceneMenu.cpp:193 ChangeAnimation`、`:206`/`:209 ChangeSpeed`、`UI/Search/SearchMenu.cpp:165 SelectOption`,以及经 `UI/Scene/SceneMenu.cpp:106-124 UpdateMenuData` 的 `QueueUITask` 落到 `:130`/`:131`/`:134`/`:138 BuildMenuData`。
同步路径 10 处:`UI/Align/AlignMenu.cpp:66 LoadCurrentAlignment`、`:210 ToggleActor`、`:252 Increment`、`UI/Scene/SceneOptions.cpp:78 BuildSubPages`、`:105 BuildOptions`、
`UI/Search/SearchMenu.cpp:153`/`:154 Search`。
第 15 处 `ModAPI/OstimNG-API-Thread.cpp:439 SearchScenes` **没有修**:该文件的 pin blob 是 CRLF(26928 B / CR=685),
而 `.gitattributes:38` 对 `Code/plugins/patches/**/*.patch` 强制 `text eol=lf` —— 实测按同样手法生成的补丁对索引 `git apply --check --cached` 退 **1**
(`error: patch failed: skse/src/ModAPI/OstimNG-API-Thread.cpp:437` / `error: skse/src/ModAPI/OstimNG-API-Thread.cpp: patch does not apply`),
只有对工作树的前向 `--check` 才退 0 ⇒ 这个文件**不可打补丁**;而且 `SearchScenes` 在本仓**零调用点**(7 处命中里 5 处是声明/快照,另 2 处是同一份 `OStimAPI/ModThreadControl.h` 的逐字副本),按既有判据(候选 3 不可达未修)记为**不修**。
`Core/ThreadManager.cpp:144-146 AnySceneRunning()` 是 `m_threadMap.size() > 0`,**不是**「`currentThread` 非空」,所以 `UIState::HandleControl` 首行那道门拦不住这一族。
同一条族里还有一条比 nullptr 更糟的路径:`ThreadManager::UntrackAllThreads`(`plugins/OStimNG/skse/src/Core/ThreadManager.cpp:133-142`)逐字 `for (auto& entry : m_threadMap) { delete entry.second; } m_threadMap.clear();`,
**完全不通知 UI**;它的唯一调用者 `plugins/OStimNG/skse/src/Serial/Manager.cpp:290-299 Revert(...)` 是读档/回滚 —— 那条路径留下的是非空悬垂指针。

**判据(同族对照,载重)**:同一个 `plugins/OStimNG/skse/src/ModAPI/OstimNG-API-Thread.cpp` 里 24 处 `GetThread(threadID)` **每一处都紧跟判空**,
`UI/Scene/SceneMenu.cpp:152-159 UpdateSpeed` 逐字先 `auto thread = UI::UIState::GetSingleton()->currentThread; if (!thread) { return; }` 再取 `currentNode` 并同样判空,
`UI/Align/AlignMenu.cpp:83-85`、`UI/UIState.cpp:15-18`、`UI/UIState.cpp:85-97 NodeChanged` 也都是先判后用 ⇒ UI 层这 15 处是 24:1 的惯例差,不是风格问题。
`getFurnitureTypeInternal()` 也在这批里:`plugins/OStimNG/skse/src/Core/Thread.h:226` 逐字 `Furniture::FurnitureType* furnitureType = nullptr;`,
同族 `Core/ThreadActor.cpp:538` 已按「可能为空」写(`if (Furniture::FurnitureType* furnitureType = thread->getFurnitureTypeInternal()) {`),而 `SceneMenu.cpp:138`/`SearchMenu.cpp:154` 直接 `->isChildOf(...)`。

**修法**:不改任何公开签名,只把「用之前先取局部、取到之后再判空」补上 ——
(1) `UIState.h:49-50` 两个成员就地补 `= nullptr`;(2) `UIState.cpp:106-110` 在清 `currentThread` 的同一个分支里补 `currentNode = nullptr;`;
(3) `ThreadManager.cpp:137-139` 让 `UntrackAllThreads` 在 `delete` 之前逐个调 `UI::UIState::GetSingleton()->HandleThreadRemoved(entry.second);`,与 `stopThreadNoLock` 同一形状;
(4) `AlignMenu.cpp` 的 `LoadCurrentAlignment`/`ToggleActor`/`Increment` 各取一次局部指针再判空(`ToggleActor` 另加 `currentNode->actors.empty()` —— 原式 `selectedSlot > currentNode->actors.size() - 1` 在空容器上是无符号回绕);
(5) `SceneMenu.cpp` 的 `BuildMenuData` 把 `currentNode`/`currentThread`/`getFurnitureTypeInternal()` 各取一次局部并判空,`ChangeAnimation`/`ChangeSpeed` 的 lambda 内先取局部再判空;
(6) `SceneOptions.cpp` 的 `auto& thread = UI::UIState::GetSingleton()->currentThread;` 改成 `auto thread = …` 并紧跟 `if (thread == nullptr) continue;`(引用绑定的空指针不会因为判空而变得安全);
(7) `SearchMenu.cpp` 的 `Search` 在 `thread == nullptr` 时 `AssignData(data); return;`(保留「无结果」的既有语义,而不是让菜单停在上一次的结果上)。

**交付**:`Code/plugins/patches/OStimNG/0004-keep-a-dead-thread-out-of-the-menus-that-dereference-it.patch`
(9466 B / 210 行,7 个文件 14 个 hunk;`UIState.h` `0d0a5d5`→`806c2e5`、`UIState.cpp` `5149498`→`ac68b9c`、`ThreadManager.cpp` `2be63d4`→`64af7ba`、
`AlignMenu.cpp` `64a3686`→`3d84077`、`SceneMenu.cpp` `68d56ba`→`fd02463`、`SceneOptions.cpp` `67ad326`→`1e7c074`、`SearchMenu.cpp` `6efe1b5`→`35b6df4`)。
The plugin is a submodule this project does not own and cannot push, and CI runs
`git submodule update --init --force --recursive`, which discards a working-tree only change.
It is therefore shipped as the patch above, which `plugin_patches.py` applies before configuring and building.

- 三重校验实跑(cwd = `plugins/OStimNG`):`git apply --check --cached` 退 **0**(能打到 pin)、前向 `git apply --check` 退 **0**(未应用时干净)、
  应用后 `git apply --check --reverse` 退 **0**(与工作区一致);再次前向 `--check` 退 **1** 并报 `error: patch failed: skse/src/Core/ThreadManager.cpp:135`(已应用,符合预期)。
- 累积校验:`0001`/`0002`/`0003`/`0004` 依次 `git apply --cached` 全部退 **0**,`git diff --stat`(工作区 vs 累积索引)**无输出**,
  `git diff --cached --stat` = 16 files changed, 387 insertions(+), 143 deletions(-),随后 `git reset -q` 复原。
- `python Code\plugins\tools\plugin_patches.py verify` → `PATCH VERIFY OK` / **32** patch(es);`… check` → `PATCH CHECK OK`。
- `git apply` 会打两行 `warning: 2 lines add whitespace errors.`(补丁第 165/178 行):那两行是 pin 里本来就带尾空白的行(`if(results[i]->actors.size() == thread->getActorCount() ` 行尾 1 空格、`});    ` 行尾 4 空格),
  只被局部改写,**不是新引入的空白** —— 这类告警要回到 pin 里核对,不能就地删掉尾空白(会多出一个不属于本补丁的改动)。
- **本机不编译**(`docs/PITFALLS.md` §0/§1,`plugins/OStimNG/skse/AGENTS.md` 逐字「do not attempt to build c++ or cmake」),所以「后台线程与菜单读取会不会真的撞上」不可判定;
  判据不是「会不会发生」,而是「发生之后读的是空指针还是已析构对象」—— 修复前两条都有,修复后 `HandleThreadRemoved` 覆盖了全部三条销毁路径(`stopThreadNoLock`/`UntrackAllThreads`/`migrateThread`)。

### 修复:OStimNG 的 Scene API 七个入口都写着 noexcept,而每个函数体都在分配、指针参数又从不判空 —— 判据是「声明在别人手里的 noexcept 撤不掉,只能自己接住」

**现象**:没有任何现象。`plugins/OStimNG/skse/src/ModAPI/OstimNG-API-Main.cpp`(pin 132 行)里七个入口逐个写着 `noexcept`,
而这七个 `noexcept` 是写在 `plugins/OStimNG/skse/src/ModAPI/OstimNG-API-Scene.h:30-46` 的**虚函数声明**上的 ——
消费方 `plugins/OStimTogether/src/OStimAPI/ModSceneControl.h:19` 持有一份逐字副本(`:22` 注释逐字「Exact OStim Scene ModAPI V1 vtable prefix through StopScene.
No virtual destructor.」,`:24-29`/`:31`/`:39`/`:48`/`:58-60` 是同一批 `noexcept = 0;`),所以这一侧的 `noexcept` **撤不掉**:
单边去掉会让定义与声明的异常规格不一致,而保留声明、任由异常逃出去,terminate 就落在调用方 `plugins/OStimTogether/src/CoopSessionManager.cpp:655` 那一帧上。

**根因**:七个入口里六个的整个函数体都在分配,而它们调用的每一层都不写 `noexcept`:
`GameAPI::GameActor::convertVector()`(`plugins/OStimNG/skse/src/GameAPI/GameActor.cpp:10`,形参**按值** `std::vector<RE::Actor*>`,`:11 std::vector<GameActor> ret` + `:13 ret.push_back`)、
`Threading::startThread()`(`plugins/OStimNG/skse/src/Core/ThreadStarter/ThreadStarter.cpp:13`,非 noexcept,`:20 std::vector<GameAPI::GameActor> actorSet` + `:24`/`:29`/`:34` 三处 `"actor " + actor.getName() + …` 字符串拼接 + `:37 push_back` + `:42 Trait::ActorCondition::create`)、
`Thread::stopFaded()`(`plugins/OStimNG/skse/src/Core/Thread.cpp:687`,非 noexcept,`:694 std::thread fadeThread = std::thread([&] { … });` —— `std::thread` 构造失败会抛 `std::system_error`)、
`Thread::startAutoMode()`(`plugins/OStimNG/skse/src/Core/Thread/ThreadAutoControl.cpp:119` → `progressAutoMode()` `:162 std::string action = … ? "malemasturbation" : "femalemasturbation";`、`:163 getRandomNode(...)` 按值吃 `std::vector<Trait::ActorCondition>` + `std::function`)。
`ThreadStartParams` 在 `plugins/OStimNG/skse/src/Core/ThreadManager.h:17`/`:50` 都是**按值**传递,所以一次 `startThread` 还要多拷一整包。
分配失败逃出这些 `noexcept` 边界就是 `std::terminate`,**没有崩溃转储**(`docs/PITFALLS.md` §45);调用链上没有任何 catch ——
`CoopSessionManager.cpp`(1275 行)的四处 try/catch 在 `:85-87`/`:99-101`/`:877-919`/`:990-1009`,与 `:607`/`:693` 两个入口**都不相干**。

**修法**:不动 `noexcept`(声明不在本仓,`noexcept` 也不进 vtable 布局,单边撤销只换来一个「声明与定义不一致」),
给六个会分配的函数体各加一层 `try { … } catch (...) { SKSE::log::error("Scene API: <名字> failed with an exception"); return Result::Failed; }` ——
**不重试**:调用方 `CoopSessionManager.cpp:687-697` 本来就有 `RetryStartApprovedOwnerSession`,这里再退避只是把同一段分配走两遍。
第七个 `TryGetAutoMode` **不加 try**:`ThreadManager::GetThread()`(`plugins/OStimNG/skse/src/Core/ThreadManager.cpp:108`)只取一次 `std::shared_lock`,
`Thread::isInAutoMode()`(`plugins/OStimNG/skse/src/Core/Thread.h:163`)是 inline 返回一个 `bool`,体内零分配。
同一补丁还补上这批入口一直缺的判空:`StartScene` 加 `!pluginName || !actors || !threadID`(原代码 `:15` 直接读 `actors[i]`),
三个 `StartXScene` 各加 `!pluginName || !threadID`(actor 指针原本就判了),`TryGetAutoMode` 的 `if (!thread)` 扩成 `if (!thread || !autoMode)`(原代码 `:110` 直接写 `*autoMode`)。
`startingAnimation` 的修法不是改 `getNodeById` 的签名,而是在四个调用点写成 `startingAnimation ? Graph::GraphTable::getNodeById(startingAnimation) : nullptr;` ——
`getNodeById`(`plugins/OStimNG/skse/src/Graph/GraphTable/GraphTableNodes.cpp:144`)按值吃 `std::string`,传 null 就是从 null 构造 `std::string`(UB),三目之后天然落进本来就有的 `if (node)` 分支。

**同族先例(判据载重)**:同一批 API 的兄弟 `plugins/OStimNG/skse/src/ModAPI/OstimNG-API-Thread.cpp`(685 行,39 个 `noexcept` 函数)**每一个指针参数都先判** ——
`:297 if (!outData) return;`、`:374 if (!outData) return false;`、`:394 if (!data) return APIResult::Invalid;`、`:426 if (!query || !buffer || bufferSize == 0) return 0;`、`:567 if (!actorFormIDs || actorCount == 0) return false;`;
Scene 侧是这唯一一处两者都缺的。异常接住的形状在本仓已出现多次:`Util/VectorUtil.cpp:11`/`:22`(OStimNG 自己的 `try { … } catch (const std::exception& ex) {}`)、
`STRPluginMessagingBridgeReceive.cpp:174-182`、`STRPMChatUiSuppress.h:113-143`。判据不是「要不要用异常」,而是「**不让异常穿过一个 `noexcept` 边界**」。

**交付**:`Code/plugins/patches/OStimNG/0003-keep-an-allocation-failure-out-of-the-scene-api-entries.patch`
(11507 B / 225 行,一个文件 `skse/src/ModAPI/OstimNG-API-Main.cpp`、一个 hunk `@@ -6,106 +6,149 @@`;pin blob `75bc52c` → 新 blob `f7f4067`,132 行 → 175 行)。
The plugin is a submodule this project does not own and cannot push, and CI runs
`git submodule update --init --force --recursive`, which discards a working-tree only change.
It is therefore shipped as the patch above, which `plugin_patches.py` applies before configuring and building.

- 三重校验实跑(cwd = `plugins/OStimNG`):`git apply --check --cached` 退 **0**(能打到 pin)、前向 `git apply --check` 退 **0**(未应用时干净)、
  应用后 `git apply --check --reverse` 退 **0**(与工作区一致);再次前向 `--check` 退 **1** 并报 `error: patch failed: skse/src/ModAPI/OstimNG-API-Main.cpp:6`(已应用,符合预期)。
- 打到 pristine 索引后 `git show :skse/src/ModAPI/OstimNG-API-Main.cpp` 与手写的新文本**逐字节相等**,随后 `git reset -q` 复原。
- 累积校验:`0001`/`0002`/`0003` 依次 `git apply --cached` 全部退 **0**,`git diff --stat`(工作区 vs 累积索引)**无输出**,`git diff --cached --stat` = 10 files changed, 339 insertions(+), 121 deletions(-)。
- `python Code\plugins\tools\plugin_patches.py verify` → `PATCH VERIFY OK` / **31** patch(es) apply to their pinned commits;`… check` → `PATCH CHECK OK`。
- **本机不编译**(`docs/PITFALLS.md` §0/§1,`plugins/OStimNG/skse/AGENTS.md` 亦逐字写着「do not attempt to build c++ or cmake」),所以「这条链会不会真的耗尽内存」不可判定;
  判据不是「会不会发生」,而是「发生之后进程还剩不剩得下一条记录」—— 修复前是 `std::terminate` 且无转储,修复后是一行日志加一个 `Result::Failed`。

### 修复:STRPM 桥的接收解析器写着 noexcept,而它调用的四个查找函数全都在建 vector —— 判据是「noexcept 的检查要顺着调用链走」

**现象**:没有任何现象。`plugins/STRPluginMessagingAPI/src/STRPluginMessagingBridgeReceive.cpp:431`(pin 1221 行)的
`bool ResolveOnConsumeAddress() noexcept` 由 `:1143 bool Start(STRPM::ReceiveCallback, void*) noexcept`(pin;工作树 `:1181`)调用,
而 `Start` 由 `plugins/STRPluginMessagingAPI/src/STRPluginMessagingBridge.cpp:991`(在 `:946 BootstrapLoop` 内)调用 ——
`BootstrapLoop` 每 750 ms 重试一次(`:36 kBootstrapRetryDelay`),**没有上限也没有退避**。

**根因**:`ResolveOnConsumeAddress` 体内一行分配都没有,但它的调用链上每一步都在建容器:
`EnumerateRuntimeMemory()`(`:240`,非 noexcept,`:242 std::vector<MemorySpan> result` + `:264 result.push_back`)、
`FindTransportTypeDescriptor()`(`:311`,非 noexcept,转调 `FindBytes` + `:318 std::sort` + `:319 erase(unique)`)、
`FindCompleteObjectLocator()`(`:325`,非 noexcept,`:331 std::vector<std::uintptr_t> matches` + `:362 push_back`)、
`FindTransportVftable()`(`:374`,非 noexcept,同形);而 `FindBytes()`(`:274`)每扫 1 MiB 还额外建一个
`std::vector<std::uint8_t> snapshot`(`:295`)。五个被调者**没有一个**写着 `noexcept`,也没有一个有 try/catch。
分配失败逃出 `ResolveOnConsumeAddress` 的 `noexcept` 边界就是 `std::terminate`,**没有崩溃转储**(`docs/PITFALLS.md` §45)。

**修法**:不动 `noexcept` 本身(`Start` 的返回值就是「解析成功了没有」,失败是它的正常语义),
把 `ResolveOnConsumeAddress` 的整个函数体包进 `try { … } catch (...) { Log("receive resolver: ran out of memory while resolving TransportService::OnConsume"); return false; }`。
日志走已有的 `Log(...)`(`:126`,只 `fopen_s`/`fprintf`/`fclose`,不分配);**不重试** ——
`BootstrapLoop` 本来就会在 750 ms 后重来一次,这里再退避只是把同一段分配走两遍。
同一补丁的第二处:`plugins/STRPluginMessagingAPI/src/STRPMRuntimeLoadRedirect.h:15` 的
`inline HMODULE LoadLibraryAdjacentToBroker(LPCWSTR) noexcept` 在 `:34`/`:37`/`:38` 建 `std::wstring` 并 `resize`/`+=`;
它经该文件末尾的 `:55 #define LoadLibraryW STRPMRuntimeLoad::LoadLibraryAdjacentToBroker` 冒充 Win32 加载器
(唯一调用方 `LoadStrBridgeModule` 在 `plugins/STRPluginMessagingAPI/src/STRPluginMessagingAPIRuntime.cpp:854`,而该文件**全文件零 noexcept、零 try/catch**),
所以把这三步分配包进 `try { … } catch (...) {}`,失败时落到函数本来就有的兜底 `return ::LoadLibraryW(modulePath);`(`:46`)。

**同族先例(判据载重)**:`SnapshotProcessMemory()`(`:165`)在同一份文件里早就写着这个形状 ——
`:174-182 try { snapshot.resize(size); } catch (...) { snapshot.clear(); return false; }`;
`STRPMChatUiSuppress.h:113-143`、`STRPMChatUiSuppressV3.h:221-228`/`:254-262` 是另外三处。
判据不是「要不要用异常」,而是「**不让异常穿过一个 `noexcept` 边界**」(`CODE_GUIDELINES.md:47` 的「Don't use exceptions」约束的是控制流,不是这个)。

**交付**:`Code/plugins/patches/STRPluginMessagingAPI/0005-keep-an-allocation-failure-out-of-the-receive-resolver-and-the-load-path.patch`
(8360 B / 174 行,两个文件:`src/STRPluginMessagingBridgeReceive.cpp` 的 `@@ -428,69 +428,84 @@`、`src/STRPMRuntimeLoadRedirect.h` 的 `@@ -31,14 +31,23 @@`)。
The plugin is a submodule this project does not own and cannot push, and CI runs
`git submodule update --init --force --recursive`, which discards a working-tree only change.
It is therefore shipped as the patch above, which `plugin_patches.py` applies before configuring and building.

- 三重校验实跑(cwd = `plugins/STRPluginMessagingAPI`):`git apply --check --cached` 退 **0**(能打到 pin)、
  前向 `git apply --check` 退 **0**(未应用时干净)、应用后 `git apply --check --reverse` 退 **0**(与工作区一致);
  再次前向 `--check` 退 **1** 并报 `error: patch failed: src/STRPluginMessagingBridgeReceive.cpp:428`(已应用,符合预期)。
- 打到 pristine 索引后 `git show :<path>` 的结果与手写的新文本**逐字节相等**(两个文件都比对过),随后 `git reset -q` 复原。
- 累积校验:`0001`…`0005` 依次 `git apply --cached` 全部退 **0**,`git diff --stat`(工作区 vs 累积索引)**无输出**,
  `git diff --cached --stat` = 11 files changed, 300 insertions(+), 91 deletions(-)。
- `python Code\plugins\tools\plugin_patches.py verify` → `PATCH VERIFY OK` / **30** patch(es) apply to their pinned commits;
  `… check` → `PATCH CHECK OK`。
- **本机不编译**(`docs/PITFALLS.md` §0/§1),所以「这条链会不会真的耗尽内存」不可判定;判据不是「会不会发生」,
  而是「发生之后进程还剩不剩得下一条记录」—— 修复前是 `std::terminate` 且无转储,修复后是一行日志加一次「尚未解析」的重试。

### 修复:插件层有一个线程入口声明了 noexcept,而它调用的每一步都在分配 —— 判据是「noexcept 是一条承诺,不是装饰」

**现象**:没有任何现象。`plugins/STRPluginMessagingAPI/src/STRPMChatUiSuppressBootstrap.h:55`(pin 120 行)的
`inline DWORD WINAPI Worker(void*) noexcept` 由 `:95 Start()` 经 `CreateThread` 起,而 `Start()` 由
`plugins/STRPluginMessagingAPI/src/STRPluginMessagingBridgeSKSE.cpp:299`(在 `:289 SKSEPlugin_Load` 内)调用 ——
每次加载这个 SKSE 插件都会起这条线程,没有配置能关掉它。

**根因**:`Worker` 的等待循环里每一步都在分配 —— `ResolveExternalStrAllocationBase()`(`:9`,体内 `EnumerateMemory()`
与 `FindBytes()` 各自增长一个 `std::vector`,`std::sort`/`erase(unique)` 之外还有一个 `std::vector<void*>`)、
`STRPMChatUiSuppressV3::ResolveExecuteAsync()`(`plugins/STRPluginMessagingAPI/src/STRPMChatUiSuppressV3.h:461`,
非 noexcept:两处 `EnumerateMemory`/`FindBytes`、`std::sort`、`std::unordered_map<std::uintptr_t, std::size_t> votes`)、
`STRPMChatUiSuppressV3::Arm()`(`:514`,非 noexcept)。分配失败逃出一个 `noexcept` 入口就是 `std::terminate`,
**没有崩溃转储**(`docs/PITFALLS.md` §45)。更隐蔽的是 `ResolveExternalStrAllocationBase()` 自己也写着 `noexcept`,
却去调两个非 noexcept 且都会 `push_back` 的函数 —— terminate 会发生在**被调者内部**,外层即使有 handler 也够不着。

**修法**:两处,都不动 `noexcept` 本身(这个入口是 `CreateThread` 的回调,`noexcept` 是这里唯一诚实的声明)。
① `ResolveExternalStrAllocationBase()` 去掉 `noexcept` —— 它本来就是会分配的那一层;
② `Worker` 的循环体包一层 `try { … } catch (...) { Log("… ran out of memory"); return 0; }`,
日志走已有的 `STRPMChatUiSuppress::detail::Log`(`:75`,只 `fopen_s`/`fprintf`/`fclose`,不分配)。
失败**不重试**:这个循环等的是运行时解包(最多 120 次 × 1 秒),内存耗尽时重试只是再走一遍同样的分配。

**同族先例(判据载重)**:`plugins/STRPluginMessagingAPI/src/STRPMProxyResolverBootstrapV2.h:248` 与
`plugins/STRPluginMessagingAPI/src/STRPMProxyResolverTrace.h:301` 的 `Worker` 都**没有** `noexcept`,且各自的 `Start()`
都用 try/catch 兜住线程创建(`:325-334` / `:329`)。`STRPMChatUiSuppressBootstrap` 是全项目唯一偏离这个形状的线程入口。

**不可达的同形副本不修**:`plugins/STRPluginMessagingAPI/src/STRPMChatUiSuppress.h:508` 的 `Worker` 有同一个形状
(`:516` 的 `ResolveStrAllocationBase()` 同样写着 `noexcept` 却调非 noexcept 的 `EnumerateMemory`/`FindBytes`),
但 `STRPMChatUiSuppress::Start()`(`:542`)**没有任何调用点** —— 全仓 1758 个源文件扫描,`&detail::Worker` 只出现在
`:550`,即 `Start()` 自身内;`STRPMChatUiSuppressV2.h` 也无人 include。与 Morph 那份「同一段源码的两个副本」不同,
这里是两份独立实现,其中一份死 ⇒ 只修活的那一份。

**交付**:`Code/plugins/patches/STRPluginMessagingAPI/0004-keep-an-allocation-failure-out-of-the-chat-ui-observers-thread-entry.patch`
(3999 B / 81 行,改一个文件:`src/STRPMChatUiSuppressBootstrap.h` 的 `@@ -6,7 +6,7 @@`、`@@ -52,6 +52,14 @@`、`@@ -60,28 +68,37 @@`)。
The plugin is a submodule this project does not own and cannot push, and CI runs
`git submodule update --init --force --recursive`, which discards a working-tree only change.
It is therefore shipped as the patch above, which `plugin_patches.py` applies before configuring and building.

- 三重校验实跑(cwd = `plugins/STRPluginMessagingAPI`):`git apply --check --cached` 退 **0**(能打到 pin)、
  前向 `git apply --check` 退 **0**(未应用时干净)、应用后 `git apply --check --reverse` 退 **0**(与工作区一致)。
- `python Code\plugins\tools\plugin_patches.py verify` → `PATCH VERIFY OK` / **29** patch(es) apply to their pinned commits;
  `… check` → `PATCH CHECK OK`。
- **本机不编译**(`docs/PITFALLS.md` §0/§1),所以「这条线程会不会真的耗尽内存」不可判定;判据不是「会不会发生」,
  而是「发生之后进程还剩不剩得下一条记录」—— 修复前是 `std::terminate` 且无转储,修复后是一行日志加一个未武装的观察者。
### 修复:Morph 的快照只夹了块数,没夹单块大小也没夹总条数 —— 判据是「上界要绑到自己的发送侧能产出什么,而不是绑到传输层允许什么」

**现象**:没有任何现象。这是 §90 那条判据(「拒绝一个状态」vs「夹住一个字段」)在同一个插件里的第二处落地,
也是同族里第一处**上界本身选错了参照物**的:`plugins/MorphSyncTogether/src/MorphSyncServiceBodyHair.cpp:60`
(与它那份被 `#include` 进来的副本 `plugins/MorphSyncTogether/src/MorphSyncService.cpp:585`)只写了 `*count > 128`,
夹的是**块数**;而一块能有多大、整份快照能有多少条 morph,一个门也没有。

**根因**:发送侧(`plugins/MorphSyncTogether/src/MorphSyncService.cpp:441` 的 `constexpr std::size_t kChunkLimit = 2200;`)
把一次快照切成 2200 字符的块,所以**合法**的 `props` 永远不超过 2200 字符、一份快照的值也就那么多;
而接收侧的门只数了块数。一个忽略分块器的对端可以把 128 块每块都塞到传输上限(`STRPM::kMaxPayloadBytes` = 24 KiB),
于是 `values` 可达数十万条,每条都要 `Split` + `HexDecode` + `std::stof`(每个被接受的 value 两次 `std::string` 分配),
全部跑在游戏线程的 `AddTask` 任务里,随后还要逐条 `_bodyMorph->SetMorph` —— 这正是需求 (4)「不得拖慢交互」的那一类。

**修法**:两个上界,都绑到本插件自己的发送侧能产出什么,而不是绑到传输层允许什么 ——
`static constexpr std::size_t kMaxMorphChunkBytes = 4096;`(发送侧切块常量的两倍)与
`static constexpr std::size_t kMaxMorphValues = 4096;`(一份快照就是一个 actor 自己的 morph 列表)。
单块超限走**已有的**入口拒绝分支(与 `*count > 128` 同形,`MST MORPH RX invalid packet`);
总条数超限在解析**之前**用一个预扫描判定(`std::count(chunk.begin(), chunk.end(), ';') + 1` 累加),
这样「拒绝」不需要先付出解析代价,并把这一份 assembly 从 `_assemblies` 里删掉。

**双份实现**:`MorphSyncServiceBodyHair.cpp` 与 `MorphSyncService.cpp` 是同一段源码的两个副本
(`MorphSyncServiceBodyHair.cpp:11` 用 `#define` 改名后 `#include "MorphSyncService.cpp"`,把那份的 `HandleMorphPacket`
变成无调用点的 `HandleMorphPacketLegacy`),两处必须同形改,否则补丁只覆盖一半。

**交付**:`Code/plugins/patches/MorphSyncTogether/0006-bound-a-single-morph-chunk-and-the-values-a-snapshot-may-carry.patch`
(4486 B / 98 行,改三个文件:`src/MorphSyncService.h` 的 `@@ -110,6 +110,18 @@`、`src/MorphSyncServiceBodyHair.cpp` 的
`@@ -57,7 +57,8 @@` 与 `@@ -106,6 +107,22 @@`、`src/MorphSyncService.cpp` 的 `@@ -572,7 +572,8 @@` 与 `@@ -620,6 +621,22 @@`)。
The plugin is a submodule this project does not own and cannot push, and CI runs
`git submodule update --init --force --recursive`, which discards a working-tree only change.
It is therefore shipped as the patch above, which `plugin_patches.py` applies before configuring and building.
pre-image 一律是 **pin 内容**(`plugin_patches.py:439-442`:每个补丁都要能独立打到 pristine 索引上),
所以 `MorphSyncService.h` 的常量块刻意放在 `ReadField` 声明之后(`:110`),与 0004 加表上界的那处(`:126`)分开 —— 两个 hunk 不重叠。

- 三重校验实跑:`git apply --check --cached` 退 **0**(能打到 pin)、`git apply --check --reverse` 退 **0**(与工作区一致)、
  前向 `git apply --check` 退 **1** 报 `error: patch failed: src/MorphSyncService.h:110`(已应用时的预期)。
- **累积验证**:`0001`…`0006` 依次 `git apply --cached` 打到 pristine 索引,六个都退 0;随后
  `git diff --stat`(工作树 vs 累积索引)输出**空** ⇒「pin + 六个补丁」逐字节等于工作树;
  `git diff --cached --stat`(累积索引 vs pin)= 13 files changed, 242 insertions(+), 12 deletions(-)。验证完 `git reset -q`。
- `python Code\plugins\tools\plugin_patches.py verify` → `PATCH VERIFY OK` / **28** patch(es) apply to their pinned commits;
  `… check` → `PATCH CHECK OK`;`python Code\plugins\tools\plugin_snapshot.py verify` → `SNAPSHOT OK`;
  `python Tools\Scripts\check_docs.py` → `DOC CHECK OK`;`python Code\plugins\tools\check_transport_compat.py` → `TRANSPORT COMPATIBILITY OK`;
  `python Code\plugins\tools\strpm_contract.py check` → `CONTRACT OK`。
- **本机不编译**(`docs/PITFALLS.md` §0/§1),所以「一个对端到底会不会这么发」不可判定;判据不是「有没有人这么发」,
  而是「发了以后解析代价由谁决定」—— 修复前由对端决定,修复后由本插件自己的发送侧常量决定。
### 修复:IED 的远端变换把对端的 `nan` 原样写进引擎节点,而它唯一的调用点恰恰在 NaN 时不会被调用 —— 判据是「拒绝一个状态」和「夹住一个字段」在这里不是同一个修复

**现象**:没有任何现象。这是 §89 那条判据(「这个字段的下游有没有第二道门」)在插件层的第一处落地,
也是**同一族里唯一一条「下游的守卫反而让缺陷更难触发」的**:`plugins/IEDSyncTogether/src/RemoteIEDRenderer.cpp:392`
的 `WatchdogTick()` 用 `needsCorrection = rotationDelta > kRotationEpsilon || positionDelta > kPositionEpsilon || scaleDelta > kScaleEpsilon`
决定要不要调 `:444 ApplyRawTransform(remoteObject, tracked.object);`,而三个 delta 由 `:207-219 MaxMatrixDelta` /
`:221-230 MaxPositionDelta` / `:417 std::abs(...)` 算出 —— 全是 `std::max`/`std::abs` 链。**NaN 参与比较恒为假,
所以整条 delta 链在 NaN 上得 0,`needsCorrection` 得 `false`,修正根本不会发生。** 这是降低可达性,不是消除:
同一个对象里只要**还有一项是有限的超阈值值**,`ApplyRawTransform` 就会被调用,于是 NaN 那一项跟着一起写进节点。

**根因**:`nan` 与 `inf` 在这条链上是**合法输入**,而且**合法发送侧自己就会产生它们**。

| 环节 | 位置 | 逐字 |
| --- | --- | --- |
| 捕获 | `plugins/IEDSyncTogether/src/LocalCaptureProbe.cpp:15-18` | `float Q(float value) { return std::round(value * 1000.0f) / 1000.0f; }` —— 量化不剥非有限值(`round(NaN)` 仍是 `NaN`) |
| 编码 | `plugins/IEDSyncTogether/src/LocalIEDState.cpp:56-67` | `fmt::format("…\|{:.6f}\|{:.6f}\|{:.6f}\|{:.6f}", …, object.position[0], object.position[1], object.position[2], object.scale)` —— **fmt 对 NaN 输出文本 `nan`、对 inf 输出 `inf`** |
| 解码 | `plugins/IEDSyncTogether/src/LocalIEDState.cpp:33`(修复前) | `out = static_cast<T>(std::stof(std::string(text)));` —— `std::stof` 把 `"nan"`/`"inf"`/`"infinity"` 当**合法输入接受,不抛** |
| 落点 | `plugins/IEDSyncTogether/src/RemoteIEDRenderer.cpp:238-246` | `object->local.translate.x = captured.position[0];` … `object->local.rotate.entry[row][col] = captured.rotationMatrix[index++];` —— 无守卫;紧邻的 `:241 object->local.scale = std::clamp(captured.scale, 0.01f, 100.0f);` 是**唯一**有守卫的一行 |

`:32-37` 那个 `try { … } catch (...) { return false; }` 只拦 `std::invalid_argument`/`std::out_of_range`,
对非有限值一次也不拦。而 `std::clamp` 同样挡不住 NaN —— NaN 与任何值比较都为假,`clamp` 原样返回。

**判据**:这里**不是**「夹住一个字段」的场合,是「拒绝一个状态」的场合,三条理由:

1. **没有替代值。** 位置、九个旋转项、缩放各是一个独立语义的浮点;`0.f` 对一个位置来说是「原点」、
   对一个缩放来说是「零尺寸」,都不是「这个字段没有值」。§70/§89 用 `0.f` 替代是因为那些字段的下游
   拿到 `0.f` 只是「不生效」;这里写进的是节点的局部变换,`0.f` 是一个**合法但错误**的变换。
2. **失败形态已经存在,而且已经打过日志。** `plugins/IEDSyncTogether/src/STRPMAdapter.cpp:527-535` 本来就有
   `auto state = DecodeLocalIEDState(payload); if (!state) { SKSE::log::warn("STRPM IED state RX decode failed: sender={} name=\"{}\" bytes={}", …); return; }`
   —— 在解码层拒掉,上层走的就是这条既有路径:整份 state 作废、一条 warn、不新增日志点、不改函数签名、不改线格式。
3. **一个 state 是一份快照,不是一串独立字段。** 21 个字段描述同一个对象集合;其中一项是 `nan` 时,
   「其余项照写」并不比「整份丢掉」更接近对端真实的外观 —— 发送侧的 NaN 来自引擎自己给出的坏变换,
   这种情况下对端看到的本来就不是一个可用的姿态。

**修法**:只改 `plugins/IEDSyncTogether/src/LocalIEDState.cpp` 一个文件、两个 hunk:

```cpp
// Code/plugins/patches/IEDSyncTogether/0003-reject-a-transform-value-the-sender-would-never-produce.patch
 // src/LocalIEDState.cpp(修复后 :4-5)
 #include <charconv>
+#include <cmath>

 // src/LocalIEDState.cpp(修复后 :33-39)
                 try {
                     out = static_cast<T>(std::stof(std::string(text)));
+                    // std::stof accepts "nan" and "inf", and the sender's own
+                    // encoding writes them for a transform the engine produced.
+                    // There is no finite value that stands in for either one, so
+                    // the object is rejected rather than clamped.
+                    return std::isfinite(out);
                 } catch (...) {
                     return false;
                 }
```

把 `return true;` 换成 `return std::isfinite(out);` 之后,`ParseNumber` 的**整型分支一字未动**
(`std::from_chars` + `result.ptr == last` 本来就拒掉一切非数字),浮点分支对非有限值返回 `false`,
于是 `:134-144` 的 `if (!ParseNumber(fields[8], object.position[0]) || …) { return std::nullopt; }`
与九个旋转项那一段照原样生效 —— **不需要新增任何一个判断点**。

**交付**:`Code/plugins/patches/IEDSyncTogether/0003-reject-a-transform-value-the-sender-would-never-produce.patch`
(943 B / 25 行 / `index 26d1461..0e0879d`)。子模块不归本项目所有,而 CI 的
`git submodule update --init --force --recursive --depth=1` 会丢弃只改工作树的改动,所以以补丁交付。
补丁的两个 hunk 都在 0002 的 hunk(`@@ -93,6 +93,9 @@`)**之前**,所以按 `patch_files()` 的排序累积应用时行号不漂移。

- 补丁的 pre-image 是 **pin 内容**(`git rev-parse :src/LocalIEDState.cpp` = `26d146145ce40693fa2634e605008b385a3ff384`,
  与 0002 补丁头的 `index 26d1461..` 一致),因为 `plugin_patches.py:439-442` 的 `verify` 是**对索引**跑的:
  「The index holds the pinned content even when the patch is already applied to the working tree, so "already applied"
  cannot hide a patch that has stopped matching the pin.」
- 三重校验实跑:`git apply --check --cached` 退 **0**(能打到 pin)、`git apply --check --reverse` 退 **0**(与工作区一致)、
  前向 `git apply --check` 退 **1** 报 `error: patch failed: src/LocalIEDState.cpp:2`(已应用时的预期)。
- **累积验证(这条是本轮新增的复核手法)**:把 `0001`+`0002`+`0003` 依次 `git apply --cached` 打到 pristine 索引上,
  三个都退 0;随后 `git diff --stat`(**工作树 vs 累积索引**)输出**空** —— 即「pin + 三个补丁」逐字节等于工作树,
  补丁集没有漏改也没有多改;`git diff --cached --stat`(**累积索引 vs pin**)= 5 files changed, 31 insertions(+), 2 deletions(-)。
  验证完 `git reset -q` 还原索引(否则 `verify` 会因 `--cached` 读到已打补丁的索引而报 4 条
  `does not apply to the pinned commit - the submodule was re-pinned without rewriting the patch`)。
- `python Code\plugins\tools\plugin_patches.py verify` → `PATCH VERIFY OK` / **27** patch(es) apply to their pinned commits;
  `… check` → `PATCH CHECK OK`;`python Code\plugins\tools\plugin_snapshot.py verify` → `SNAPSHOT OK`。
- **本机不编译**(`docs/PITFALLS.md` §0/§1),所以「谁在真正发 NaN」不可判定;判据不是「有没有人这么发」,
  而是「发了以后有没有人挡」—— 修复前一个都没有,修复后解码层整份拒绝。
### 修复:线上浮点还剩下两处裸读,一处直接写进引擎的转向、一处进 TintMask 的 alpha —— 判据是「这个字段的下游有没有第二道门」

**现象**:没有任何现象。这是 §70 那条判据(「引擎 setter 之前是最后一道门」)的收尾:全仓
`reinterpret_cast<float*>` 只剩 6 处,其中 4 处读的是引擎自己的内存,剩下两处读的是线上字节 ——
`Code/encoding/Structs/Movement.cpp:38` 的 `Direction` 与 `Code/encoding/Structs/Tints.cpp:67` 的 `Alpha`。

**根因**:两个字段都用 `aReader.ReadBits(tmp, 32)` 取位再 `reinterpret_cast<float*>` 重解释,
这条路绕开了 `Serialization::ReadFloat` —— 于是 §70 给 `Code/encoding` 里二十多处读点补的
`WireLimits::ClampFinite` 一处也没覆盖到它们。两个字段的下游都没有第二道门:

| 字段 | 读点(修复前) | 下游 |
| --- | --- | --- |
| `Movement::Direction` | `Code/encoding/Structs/Movement.cpp:38` | `Code/client/Services/Generic/CharacterService.cpp:683 point.Direction = movement.Direction;` → `Code/client/Systems/InterpolationSystem.cpp:112 apActor->currentProcess->middleProcess->direction = second.Direction;`(**直写引擎 middle process**,`:110` 只判了空) |
| `Tints::Entry::Alpha` | `Code/encoding/Structs/Tints.cpp:67` | `Code/client/Systems/FaceGenSystem.cpp:119 tints[i]->alpha = tintsEntries[i].Alpha;`(**写进引擎 `TintMask`**,该函数唯一的门是 `:153 if (acTints.Entries.empty()) return;`) |

两个字段都属于「`+inf` 过得去」那一类:`InterpolationSystem` 拿 `Direction` 算的是朝向,`FaceGenSystem` 拿
`Alpha` 交给引擎混合;`NaN` 参与算术后还是 `NaN`,而这两个下游都不做比较,所以没有一道门能替它挡住。

**修法**:两处统一走既有的 `WireLimits::ClampFinite`(`Code/encoding/WireLimits.h:99`),两个文件各补
`#include <WireLimits.h>`(写在第 2 行,与 `Code/encoding/Messages/AddTargetRequest.cpp:1-2` 同形):

```cpp
// Code/encoding/Structs/Movement.cpp(修复后 :36-43)
    uint64_t tmp = 0;
    aReader.ReadBits(tmp, 32);
    uint32_t tmp32 = tmp & 0xFFFFFFFF;

    // A NaN or an infinity survives every arithmetic operation it takes part in, and this
    // value is assigned to the middle process of an actor on the receiving client without
    // being read again. The sender cannot express either one.
    Direction = WireLimits::ClampFinite(*reinterpret_cast<float*>(&tmp32));
```

```cpp
// Code/encoding/Structs/Tints.cpp(修复后 :66-72)
        aReader.ReadBits(buffer, 32);
        tmp = buffer & 0xFFFFFFFF;

        // The alpha reaches TintMask::alpha on the receiving client, which the engine then
        // blends with. A NaN or an infinity is not a large number but the absence of one,
        // and no legitimate sender produces either.
        entry.Alpha = WireLimits::ClampFinite(*reinterpret_cast<float*>(&tmp));
```

**为什么是「替换为 0」而不是「夹进一个区间」**:与 §70 同一条 —— 没有任何区间能让 `NaN` 变得有意义,
而这两个字段都不带符号相关的判断(不像血量增量那样需要一个下界),所以不需要给它们单独定范围。
**为什么不动那 4 处引擎内存的 `reinterpret_cast<float*>`**:它们读的不是线上字节
(`Code/client/Games/Skyrim/TESObjectREFR.cpp:243`/`:314` 是引擎的图变量表、
`Code/client/Services/Debug/Views/AnimDebugView.cpp:164` 是调试视图、
`Code/client/Games/Skyrim/Interface/Menus/HUDMenuUtils.cpp:12` 是引擎调用参数)。

**验证**:本机不编译(`docs/PITFALLS.md` §0/§1),证据是逐字命中 + 门禁 + 往返测试。

`Code/tests/encoding.cpp` 的 `[encoding.finite_floats]` 新增四个块(`:1431-1503`),新增两条
`#include`(`:47 <Structs/Movement.h>`、`:49 <Structs/Tints.h>` —— 这两个结构体此前**没有任何往返测试**):

- `Movement` 的 `Direction = NaN` 读回 `0.f`;`Direction = -2.75f` 读回 `-2.75f` 且 `received == sent`
  —— 后一条证明**有限的合法值原样通过**,也就是「读的时候替换了」是前一条断言成立的唯一原因。
- `Tints` 的 `Alpha = +inf` 读回 `0.f`;`Alpha = 0.375f` / `Color = 0x11223344` / `Type = 7` 整包 `==`
  —— 后一条同时钉住字段顺序没被改动。
- 全仓 `git grep -rn "ClampFinite" -- Code/encoding` 现在 **30 处**命中(含定义 `WireLimits.h:99`)。
- `python Tools\Scripts\check_docs.py` → `DOC CHECK OK`。
### 修复:服务端的日志 sink 把 admin 协议的帧发给游戏连接 —— 判据是「收件人集合里的成员,是不是这个协议的端点」

**现象**:没有任何现象,而且是**纵深修复** —— 这一条和上一条同源,今天连触发条件都凑不齐:
管理端根本连不上服务端。`Code/admin/Transport.cpp:43-51` 的 `AdminApp::OnConnected` 建的
`AuthenticationRequest` **只填了 `Token`**,`Version` 留空;而 `Code/server/GameServer.cpp:885`
的版本判断发生在 `:918` 看令牌**之前**,于是管理端永远拿不到 `m_adminSessions` 的席位。

**根因**:`Code/server/Services/AdminService.cpp:22-31`(修复前)把**每一条服务端日志**
格式化成一个 admin 协议的 `ServerLogs`,交给 `Code/server/GameServer.h:73-78` 的 `ForEachAdmin`
广播出去 —— 而那个集合的语义是「**管理员凭据**」,不是「管理端连接」:

```cpp
// Code/server/Services/AdminService.cpp(修复前 :22-31)
void AdminService::sink_it_(const spdlog::details::log_msg& msg)
{
    spdlog::memory_buf_t formatted;
    formatter_->format(msg, formatted);

    ServerLogs logs;
    logs.Logs = fmt::to_string(formatted);

    GameServer::Get()->ForEachAdmin([&logs](ConnectionId_t aId) { GameServer::Get()->Send(aId, logs); });
}
```

`m_adminSessions` 的**两个**写入点都在游戏握手路径上,成员必然是普通游戏连接:

| 写入点 | 语义 |
| --- | --- |
| `Code/server/GameServer.cpp:925` | 游戏客户端用管理员密码登录(`:918` 的 `adminPasswordUsed`),在 `HandleAuthenticationRequest` 里插入 |
| `Code/server/GameServer.cpp:430`/`:441` | `addadmin` 控制台命令按用户名解析出 `Player*` 再取其 connection id |

而两套协议的**编号是重叠的**:`Code/admin_protocol/Opcodes.h:13` 的 `kServerLogs = 1`,
`Code/encoding/Opcodes.h:11` 的 `kAssignCharacterResponse = 1`。于是这一帧落在游戏客户端上时,
`Code/encoding/Messages/ServerMessageFactory.cpp:32-42` 会**合法地**解出 `AssignCharacterResponse`,
接着 `Code/client/Services/Generic/CharacterService.cpp:358-430` 拿这串字节去当
`Cookie`、`AllActorValues`、`CurrentInventory`、动作列表——即「一条日志行被当成一次角色指派」。

**修法**:这一帧**没有收件人**,所以不构造它。`sink_it_` 只留一行 `(void)msg;`,sink 本身
继续挂在服务端 logger 上(`Code/server/World.cpp:24-25`),把这条流留给下一个读者。

- **为什么不是「删掉这个 sink」**:它被 `World` 持有(`Code/server/World.h:55`),删掉要动
  `World.cpp:24-25` 与头文件;`docs/PITFALLS.md:873-874` 已经就同一处下过结论:
  「删掉会改日志行为。保留。」
- **为什么不是「给 `ForEachAdmin` 加过滤」**:过滤器要回答的是「这个连接是不是管理端」,
  而**这个问题今天没有答案** —— 服务端根本没有「管理端连接」这个概念,只有「知道管理员密码的
  游戏连接」。等 `Code/server/GameServer.cpp:593-607` 的 admin 分派从注释里出来,
  判据才有对象。
- **顺带**:`sink_it_` 是**每一条 spdlog 调用**都会走的路径,原实现在这里把同一条日志
  **第二次格式化**(`formatter_->format`)并做一次 `fmt::to_string`。日志量越大,这笔
  开销越稳定地落在服务端线程上(满足「不拖慢」那条要求)。

**验证**:本机不编译(`docs/PITFALLS.md` §0/§1),证据是逐字命中 + 门禁 + 双向反向验证。

- `python Code\plugins\tools\check_transport_compat.py` → `TRANSPORT COMPATIBILITY OK`。
- 门禁新增两半(`Code/plugins/tools/check_transport_compat.py:249-315`):
  **①** 解析 `ServerAdminOpcode.kServerLogs` 与 `ServerOpcode.kAssignCharacterResponse` 的**数值**,
  两者必须仍然相等(注释里的那句话必须成立);
  **②** 读 `Code/server/Services/AdminService.cpp` 的**代码行**(先剥掉 `//` 之后的内容),
  不允许出现 `ServerLogs` 或 `ForEachAdmin`。
- **反向验证过两次**:把 `ServerLogs logs;` 放回 `sink_it_` 的函数体 → 门禁
  `TRANSPORT COMPATIBILITY FAILED (1)` + 上面那条失败说明,exit 1;把
  `kServerLogs` 改成 `= 9` → 同样 `FAILED (1)`,exit 1。两次还原后都回到 `OK`。
  这条证据同时说明第二半查的是**定义**而不是**提及** —— 新写的注释里就有 `ServerLogs` 与
  `ForEachAdmin` 两个词,注释被剥掉后门禁不误报。

### 修复:整包尺寸这条上限有两个接收入口在用、第三个没用,而注释里写的就是「只有两个」 —— 判据是「这条上限的入口数,和入口的总数对得上吗」

**现象**:没有任何现象,而且这一条比 §84 更远:管理端(`TiltedAdmin`)当前**根本收不到**服务端的消息 ——
`Code/server/GameServer.cpp:593-607` 的 admin 分派整段被注释掉(`// TODO: ClientAdminMessageFactory`)。
所以这不是活缺陷,是**纵深修复**:§84 把判据限定在「游戏本体/插件层」,明确写下了「不改管理端」;
这一条推翻的正是那个限定 —— 不是因为管理端现在可打,而是因为**同一条上限的作用域应该等于接收入口的个数**。

**根因**:`Code/encoding/WireLimits.h:23-27` 的注释自己给出了那个错误的范围:

```cpp
// Code/encoding/WireLimits.h(修复前 :23-27)
// Note the limit of the fix: the clamp runs after the string has been read, so it bounds
// what the value is used for, not the transient allocation inside the reader. Bounding
// the packet itself is what kMaxPacketBytes below does, and the only place for it is the
// receive entry point (GameServer::OnConsume and Client::OnConsume), which see the
// packet size.
```

全仓的接收入口是**三个**,不是两个:

| 入口 | §84 之前 | §84 | 本条之后 |
| --- | --- | --- | --- |
| `Code/server/GameServer.cpp:573 GameServer::OnConsume` | 无判断 | `WireLimits::WithinPacketLimit` | 不变 |
| `Code/client/Services/Generic/TransportService.cpp:181 TransportService::OnConsume` | 无判断 | `WireLimits::WithinPacketLimit` | 不变 |
| `Code/admin/Transport.cpp:16 AdminApp::OnConsume` | 无判断 | **漏了** | `WireLimits::WithinPacketLimit` |

漏掉的那一处把 `aSize` 直接交给 `ViewBuffer`,再交给 `ServerAdminMessageFactory::Extract`:

```cpp
// Code/admin/Transport.cpp(修复前 :12-25)
void AdminApp::OnConsume(const void* apData, uint32_t aSize)
{
    ServerAdminMessageFactory factory;
    TiltedPhoques::ViewBuffer buf((uint8_t*)apData, aSize);   // 对端说的尺寸,没有任何上界
    TiltedPhoques::Buffer::Reader reader(&buf);

    auto pMessage = factory.Extract(reader);
    if (!pMessage)
    {
        return;
    }

    m_messageHandlers[pMessage->GetOpcode()](pMessage);
}
```

- **为什么 `kMaxPacketBytes` 对管理端也是对的**:管理端自己的出包上界是 `1 << 16`(`Code/admin/Transport.cpp:75`、`:103`
  两个 `Send` 重载都建 `TiltedPhoques::Buffer buffer(1 << 16)`),服务端给它发消息走的是 `Code/server/GameServer.cpp:708`
  那个 `1 << 20` 的缓冲。取大者 ⇒ `1 << 20` 对两端都成立,不需要为管理端单独定义一个常量。
  这也是 §84 已经立过的判据:「这个值必须**大于等于**发送侧能产出的一切」。
- **为什么不按「反正现在接不到消息」放过**:门禁查的是**入口数**,不是「今天有没有流量」。
  管理端的分派被注释掉是一行 `TODO`,而不是一个架构约束;消费端
  `Code/admin/Transport.cpp:128-131` 的 `HandleMessage(const ServerLogs&)` 也还在。把 `//` 去掉的那一刻,
  这个入口就是活的,而那时没人会想起来补一个尺寸判断。
  (生产侧那半 —— `AdminService::sink_it_` 把每条服务端日志打包成 `ServerLogs` 广播出去 ——
  已在**上一条**里关掉:它的收件人集合是游戏连接,而 `kServerLogs` 与 `kAssignCharacterResponse`
  同为 `1`。这里只留接收入口这一半。)
- **为什么顺带给管理端加 `#include <spdlog/spdlog.h>`**:管理端此前没有任何日志设施 ——
  `Code/admin` 与 `Code/admin_protocol` 两个目录里 `spdlog` 出现 0 次(唯一命中是 `Code/admin/xmake.lua:26` 的包名)。
  §84 的「不改管理端」正是拿这一点当理由之一(「没有日志设施、失败分支是裸 `return;`」)。
  这次反过来:一个丢弃分支如果什么都不说,门禁就成了唯一的证据,而门禁只证明调用存在、不证明它被走到。
  `Code/admin/xmake.lua:24-31` 的 `add_packages(... "spdlog" ...)` 已经在依赖里,构建脚本不需要改。

**验证**:本机不编译(`docs/PITFALLS.md` §0/§1),证据是逐字命中 + 门禁 + 反向验证。

- `python Code\plugins\tools\check_transport_compat.py` → `TRANSPORT COMPATIBILITY OK`。
- **反向验证过**:把 `AdminApp::OnConsume` 里的 `if (!WireLimits::WithinPacketLimit(aSize))` 临时换成
  `if (aSize > (1u << 20))`,门禁输出
  `TRANSPORT COMPATIBILITY FAILED (1) - Code/admin/Transport.cpp AdminApp::OnConsume does not call WireLimits::WithinPacketLimit;
  the size of the packet is known only there, so without it the bound does nothing`,exit 1;还原后重新 `OK`。
- 门禁的 `PACKET_GATE_SITES` 从 2 项加到 3 项(`Code/plugins/tools/check_transport_compat.py:211-215`),
  那条 rationale 注释里原本写的「the receive entry points」也改成了「三个」。

> **教训**:第一条:**「只有两个入口」这句话本身没有被门禁检查过。**
> §84 的注释、门禁的 `PACKET_GATE_SITES`、和真实入口数是三份互相独立的记录,当时只有前两份互相对齐。
> 判据是「这条上限的入口数,和入口的总数对得上吗」—— 一份手工维护的名单,它的价值等于它被核对过的次数。
> 第二条:**「现在不可达」和「不该有这道门」是两回事。**
> 这一处离活缺陷的距离是一行 `//`,而它旁边就写着 `// TODO`。
> 把范围排除写进文档(§84 的「不改管理端」)比写进代码更危险:文档不会被编译器或门禁提醒。
> 第三条:**同一条上限在两个进程里的正确值可能相同,但理由不同。**
> 这里 `1 << 20` 同时覆盖管理端的 `1 << 16` 出包与服务端的 `1 << 20` 出包 ——
> 是「取大者」这条既有判据让它一次成立,而不是因为管理端「碰巧」和客户端一样大。
### 修复:插件层的浮点图变量只有发送侧判了有限,接收侧把线上的数直接喂给动画图 —— 判据是「这个值两端都判过吗」

**现象**:没有任何现象。这是 §70 那二十处「对端说了算的浮点」在插件层的同一条:正常流量下永远不会分岔,
只有在对手存在时才成立,而对手存在时它是**未认证的对端**就能触发的那一类。

**根因**:同一条通道的两端,一侧判、一侧不判。发送侧 (`plugins/AnimSyncTogether/src/GraphVariableSync.cpp:68`) 是:

```cpp
// plugins/AnimSyncTogether/src/GraphVariableSync.cpp:68
if (!holder->GetGraphVariableFloat(variableName, value) || !std::isfinite(value) || !HasChanged(rule, value)) {
    break;
}
```

接收侧 (`plugins/AnimSyncTogether/src/STRPMClient.cpp:459-546`) 从头到尾没有任何有限性判断:

```cpp
// plugins/AnimSyncTogether/src/STRPMClient.cpp(修复前 :459-546)
void STRPMClient::ApplyGraphVariableMessage(..., float floatValue)   // :459 线上来的 float
{
    const auto type = static_cast<GraphVariableType>(variableType);  // :471
    if (!SyncRules::GetSingleton()->ShouldSyncGraphVariable(name, type)) { ... }

    switch (type) {
    case GraphVariableType::kFloat:
        applied = actor->SetGraphVariableFloat(variableName, floatValue);   // :531 直接进引擎
```

`floatValue` 来自 `packet->graphVariableFloat`(`:291`),而那个结构体是**逐字节从 STRPM 载荷强转**出来的
(`:260` 只判了 `message->size == sizeof(SyncPacket)`,即长度,不判内容)⇒ `NaN` / `±inf` 一路畅通。

- **为什么不是「反正动画图会夹住」**:动画图不会。这个变量喂的是 `SetGraphVariableFloat`,它写进行为图之后被
  选剪辑的逻辑与 OAR 的替换条件读走(`:543-544` 之后紧跟 `OARClient::ClearConditionStateData`);
  同族已经修过 6 处(`WireLimits::ClampFinite`,见 §70),判据是同一条:引擎 setter 之前是最后一道门。
- **为什么判在 `ShouldSyncGraphVariable` 之后而不是之前**:`type` 只有在规则里存在时才可信 ——
  规则表要求 `it->second == type`(`plugins/AnimSyncTogether/src/SyncRules.cpp:126-132`),
  所以 `type == GraphVariableType::kFloat` 成立时,这条规则真的是 `float`,而不是对端自称的。
  把判断放在前面就等于相信对端填的 `variableType`。
- **为什么整包丢弃而不是夹成 0**:一个不是有限数的图变量没有「接近的值」,它是发送侧自己都会拒绝发送的东西
  (`GraphVariableSync.cpp:68`)⇒ 收下它就等于承认这一包是数据。与 §70 里那些有量纲的量不同,
  这里没有可以夹取的目标值。
- **交付形式是补丁**:`plugins/AnimSyncTogether` 是本仓不拥有、也不能 push 的子模块,
  而 CI 会 `git submodule update --init --force --recursive` 丢掉工作树里的改动。

**验证**:本机不编译(`docs/PITFALLS.md` §0/§1),证据是逐字命中 + 补丁可打在 pin 上 + 门禁全绿。

- `python Code\plugins\tools\plugin_patches.py verify` → `PATCH VERIFY OK` / `26 patch(es) apply to their pinned commits`。
- `git apply --check --cached` 对该补丁 exit 0(打在 pin `a432ec34` 上),`--reverse` 对当前工作树 exit 0。
- 同族对照:整个插件层的 `SetGraphVariableFloat` 只有 4 处,另外 3 处(`plugins/OStimNG/skse/src/GameAPI/GameActor.cpp:74`、
  `:102`、`plugins/OStimTogether/src/FreeScenePhaseSync.cpp:953`)写的是本地算出来的常量或本地状态,不是线上的字节。
- 插件层 `std::isfinite` 的既有用法共 26 处(MorphSyncTogether / OStimTogether 为主),本条补上的是唯一漏掉的一处接收侧。

> **教训**:第一条:**「发送侧判过」不是「这个值安全」。**
> 发送侧的判断保护的是发送侧自己;一个值从线上回来时,它只经过了长度检查。
> 判据是「这个值两端都判过吗」,而不是「有没有人判过」。
> 第二条:**判定的位置比判定本身更重要。**
> 同一个 `isfinite`,放在 `ShouldSyncGraphVariable` 之前是错的 —— 那时 `type` 还是对端自称的;
> 放在之后,`type` 才由本地规则表担保。**先证明「这个字段是什么」,再判「这个值合不合法」。**
> 第三条:**「引擎会兜住」在这个仓库里已经被推翻过很多次,这次也一样。**
> `SetGraphVariableFloat` 之后没有第二道门,它写进去的东西会被选剪辑与 OAR 条件读走 ——
> 而这一处比 §70 那些更难发现,因为它长在一个**只由规则文件开关**的路径上:
> 默认的 `HelmetToggle2.rules` 只声明了两个 `int`,所以即使实机跑起来也看不到它。

### 修复:任务日志的条数上限写侧在用、读侧被忽略,而读侧拿它 resize —— 判据是「同一份常量有没有被两边都读到」

**现象**:没有任何现象。这一条同样属于「修掉它不会让任何一局游戏变好」的那一类:它只在对手存在时才有意义。

**根因**:`QuestLog` 的条数常量一直在,而且一直在写侧被使用,读侧却绕过了它:

```cpp
// Code/encoding/Structs/QuestLog.cpp(修复前 :13 / :25-28 / :39-54)
static constexpr size_t kMaxEntryCount = 0xFFFF;   // 文件作用域,只有写侧读它

void QuestLog::Serialize(...) const noexcept
{
    const auto count = std::min<size_t>(Entries.size(), kMaxEntryCount);   // :27 写侧夹了
    aWriter.WriteBits(count, 16);

void QuestLog::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    aReader.ReadBits(temp, 16);
    const size_t count = temp & 0xFFFF;
    Entries.resize(count);                          // :45 读侧照抄线上的数
```

`count` 最大 65535,一条 `Entry` 是一个 `GameId` 加一个 `uint16_t`,而 `GameId` 的两个字段各自是一个
VarInt —— 也就是**只要两个字节**就能让接收侧按 65535 条去 `resize`,在任何一条条目字节被读到之前。
`WireLimits::kMaxPacketBytes`(1 MiB)挡不住这一步:它在 `GameServer::OnConsume` 里判,而这里要分配的是
「条数 × 单条大小」,与包有多大无关;`WireLimits` 里那些逐字段的上限也全都跑在读取之后。

- **为什么上界取 `0x1000` 而不是继续用 `0xFFFF`**:`0xFFFF` 是线上计数的位宽,不是数据的量级。
  任务日志的行数由客户端 `PlayerCharacter::objectives` 决定(`Code/client/Services/Generic/CharacterService.cpp:1439`
  遍历的就是那个数组),真实存档在几百以内;`Inventory` 的同类上限(也是 VarInt 计数)取的就是 `0x1000`。
  写侧仍然按位宽夹到 `0xFFFF`,因为那是「线上能不能表示」的问题,不是「该不该分配」的问题。
- **为什么读侧是 `clear()` + 逐条 `push_back`,而不是「夹取后 `resize`」**:夹取只解决了这一包要分配多少,
  没解决「包短于声明时那些没被读到的条目」—— `resize` 出来的条目是默认值,会被当成真的日志内容。
  逐条 `push_back` 让分配跟着真正读到的条目走。这也是 `Inventory`(`Code/encoding/Structs/Inventory.cpp:124-136`)
  与 `Tints`(`Code/encoding/Structs/Tints.cpp:52-70`)已经用的形状。
- **`clear()` 是必须的,不是顺手加的**:`Deserialize` 不保证被调进一个全新的对象,
  追加语义会让复用的对象每收一包就长一截 —— `Tints.cpp:50-51` 上已经写过同一条注释。
- **为什么常量从文件作用域挪进结构体**:门禁要能在 `Code/tests` 里断言它,而测试只能看到公开成员。
  服务端同类上限的既有位置是类内 `static constexpr`(`Code/server/Services/ObjectService.h:31-37`)。
- **为什么同时给服务端的任务日志表加上限**:`Code/server/Services/QuestService.cpp:73` 的 `entries.emplace_back`
  是这张表唯一的增长点,而唯一的删除点是 `:91-101` 那条 `Stopped` 分支 —— 键是客户端挑的 `GameId`,
  客户端一直造新 id 就一直涨,和 `ObjectService` 那张表是同一类。上限取 4096,已存在的 id 照常更新,
  满了只拒绝新增的那一条(`ObjectService` 的四条规则逐条照搬)。

**验证**:本机不编译(`docs/PITFALLS.md` §0/§1),证据是逐字命中 + 新用例。
`Code/tests/encoding.cpp:1642-1704` 新增
`TEST_CASE("A quest log cannot make the reader allocate what it never sent", "[encoding.quest_log]")`:`

- 写侧喂 `kMaxEntryCount + 7` 条,读回来正好是 `kMaxEntryCount` 条,且首尾两条的内容对得上。
- 手写一个声明 `0xFFFF` 条、实际也只写了 `kMaxEntryCount` 条的包,读侧停在 `kMaxEntryCount` —— 夹取生效且流被消费干净。
- 一个已经装了两条旧条目的对象收到只声明一条的包之后只剩一条 ⇒ 替换语义,不是追加。

> **教训**:第一条:**「常量存在」不等于「常量在生效」。**
> 这一处比 §84 那处更隐蔽 —— §84 是上限根本不存在,这里是上限存在、名字对、值对、注释也对,
> 只是**读侧那一行没引用它**。检查的办法不是「有没有这个常量」,而是「用到它的地方是不是两边都有」。
> 第二条:**`resize(线上的数)` 和 `push_back(读到的条目)` 在正常流量下完全一样。**
> 两者只在包短于声明时分岔,而那正是对手能造出来的唯一形状 —— 用正常流量做回归测试永远测不出区别。
> 第三条:**上限的「值」和上限的「位置」是两个决定。**
> 写侧夹的是「线上能不能表示」,所以用位宽;读侧夹的是「该不该按这个数分配」,所以用量级。
> 两者可以不同,但必须是**两个都写出来**的决定,不能靠一个常量同时承担。

### 修复:整包尺寸没有任何上限,而包内每一个长度字段都被当成真的 —— 判据是「谁在分配之前知道这个包有多大」

**现象**:没有任何现象。这是本仓库里唯一一类「修掉它不会让任何一局游戏变好」的缺陷 —— 它只在对手存在时才有意义,
而对手存在的假设下,它是**未认证连接**就能触发的那一类。

**根因**:两个接收入口都先把字节包成 `ViewBuffer`、再交给工厂解析,**中间没有任何尺寸判断**:

```cpp
// Code/server/GameServer.cpp(修复前 :572-602)
void GameServer::OnConsume(const void* apData, const uint32_t aSize, const ConnectionId_t aConnectionId)
{
    ViewBuffer buf((uint8_t*)apData, aSize);          // :574 aSize 唯一一次被使用
    Buffer::Reader reader(&buf);

    const ClientMessageFactory factory;
    auto pMessage = factory.Extract(reader);          // :593 从这里开始相信包内的一切
}
```

客户端 `Code/client/Services/Generic/TransportService.cpp:180-206` 是同一个形状。

包内每一个长度字段都是「这个包里有这么多字节」的**声明**,而解析器按声明分配(`Mods::Deserialize` 的 8191 计数、
`Inventory` 的 4096 条、每条字符串的 `ReadString`)。`Code/encoding/WireLimits.h` 里的那些字符串上限全都
**在读完之后**才夹取 —— 它们约束的是「值怎么用」,不是 reader 内部的瞬时分配。这个缺口在 `WireLimits.h` 的
头注释 `:23-26` 里被明确写下来了,写的是「Bounding the packet itself is a separate change, and the only place for it
is the receive entry point」—— 文档把判据写死了,实现一直不存在。

**为什么没有任何东西报错**:因为报错的前提是有人发大包,而合法客户端永远发不出大包 —— 两个发送侧都在固定缓冲区里
序列化(服务端 `Code/server/GameServer.cpp:676` 的 `Buffer buffer(1 << 20)`,客户端
`Code/client/Services/Generic/TransportService.cpp:165` 的 `Buffer buffer(1 << 16)`)。于是这条路径在正常对局里
**不可达**,在 CI 里也不可达,只有在对手存在的假设下才可达 —— 而那时它是未认证连接可达。

**为什么 CI 挡不住、本地也看不见**:没有任何测试构造过「一个比发送侧缓冲区更大的包」,因为没有哪一处代码在问这个问题。
`Code/**` 全仓搜 `aSize >` / `packetSize` / `kMaxPacket` **零命中** —— 这不是「门禁太松」,是「门禁不存在」。

**修法**:上限放进 `Code/encoding/WireLimits.h`,与其它上限放在一起;判断在两个接收入口的最前面:

```cpp
// Code/encoding/WireLimits.h
inline constexpr uint32_t kMaxPacketBytes = 1 << 20;

[[nodiscard]] inline bool WithinPacketLimit(const uint64_t aSize) noexcept
{
    return aSize <= kMaxPacketBytes;
}
```

```cpp
// Code/server/GameServer.cpp:584-588(客户端同形,TransportService.cpp:189-193)
    if (!WireLimits::WithinPacketLimit(aSize))
    {
        spdlog::warn("Dropping a {} byte packet from {:x}: larger than any packet this framework can produce", aSize, aConnectionId);
        return;
    }
```

- **为什么上限是 `1 << 20` 而不是更小**:这个值必须**大于等于**发送侧能产出的一切,否则它会开始丢弃合法流量,
  而丢包的后果看起来像失同步、不像丢包。服务端序列化进 `1 << 20`,客户端进 `1 << 16`,取两者的大者 —— 这就是
  「我们自己的对端能发出的最大包」这个量的定义。写得再小需要先证明「没有任何一条合法报文能超过 X」,
  而 `AssignObjectsRequest` 一条就能带 255 个 `ObjectData`、每个含一个完整 `Inventory`(4096 条 × 每条 ≤64 个
  `EffectItem`,每项含最长 260 字节的附魔串),这个证明本地做不出来。
- **为什么放在 `Code/encoding` 而不是放在两个调用者旁边**:`Code/tests/xmake.lua` 只 `add_deps("SkyrimEncoding")`,
  那是测试目标唯一能看到的库 —— 门禁放服务端就永远没有用例(`docs/PITFALLS.md:7446-7465` 的 §62.4 已立过同一条判据)。
- **为什么拆出一个 `WithinPacketLimit` 而不是直接写 `aSize > kMaxPacketBytes`**:边界本身要可测。
  把判断留成常量比较,测试只能测常量;留成函数,测试能测「等于上限通过、上限加一不通过」。
- **为什么是「丢弃 + 一行 warn」而不是「夹取」**:夹取在这里没有意义 —— 包被截断之后剩下的字节不再是一个包。
  这一条与字符串那些上限的取舍**相反**,理由是两者处理的对象不同:字符串截短之后仍然是那个字符串的近似,
  包截短之后不是。
- **不改管理端**(`Code/admin/Transport.cpp:12-25`):它是独立桌面二进制、没有日志设施、失败分支是裸 `return;`,
  不在「游戏本体/插件层」的判据范围内。
- **同时加一条门禁**:`Code/plugins/tools/check_transport_compat.py` 现在会读 `WireLimits.h` 里的 `kMaxPacketBytes`,
  断言它不低于服务端的 `1 << 20`,并断言两个接收入口真的调用了 `WithinPacketLimit`。
  理由是这条上限有一个**静默失败方向**:把值改小不会让任何测试变红,只会让大包在对手不存在的对局里消失。

**验证**:本机不编译(`docs/PITFALLS.md` §0/§1),证据是逐字命中 + 新用例 + 门禁。
`Code/tests/encoding.cpp:1612-1638` 新增
`TEST_CASE("A packet larger than any sender can produce is rejected before it is read", "[encoding.packet_limit]")`:

- `WithinPacketLimit(0)` / `(1)` / `(kMaxPacketBytes)` 通过,`(kMaxPacketBytes + 1)` 不通过 —— 边界钉在用例里。
- `WithinPacketLimit(std::numeric_limits<uint64_t>::max())` 不通过 ⇒ 64 位尺寸不会回绕进接受区间。
- `kMaxPacketBytes >= (1u << 20)` ⇒ 这条上限永远不会低于服务端自己的出包能力。
- `kMaxPacketBytes` 大于三个字符串上限 ⇒ 两层门禁独立,改一层不会把另一层关掉。

`python Code\plugins\tools\check_transport_compat.py` 通过,且**反向验证过**:把客户端那处调用临时换成
`aSize > (1u << 20)` 之后,门禁输出
`TRANSPORT COMPATIBILITY FAILED (1) - Code/client/Services/Generic/TransportService.cpp TransportService::OnConsume does not call WireLimits::WithinPacketLimit; the size of the packet is known only there, so without it the bound does nothing`。

> **教训**:第一条:**「上限写在文档里」和「上限存在」是两件事。**
> `WireLimits.h` 的头注释逐字写了「整包上限只能在接收入口加」,写了很久,而两个入口一次都没判过尺寸 ——
> 一段把缺口描述得非常准确的注释,读起来和一段修好的代码一样让人放心。
> 第二条:**安全上限的失败方向是「静默丢包」,不是「崩溃」。**
> 把 `1 << 20` 改成 `1 << 10` 不会让任何测试变红,只会让某些报文在正常对局里凭空消失,
> 而消失的后果看起来像失同步 —— 所以这条常量需要一个门禁去盯它的**下界**,光有用例盯上界不够。
> 第三条:**同一个「太大」,在字符串层和包层的正确取舍是相反的。**
> 字符串截短之后仍然是那个字符串的近似(所以夹取);包截短之后不是包(所以丢弃)。
> 判据不是「夹取更温和」,而是「截断之后剩下的东西还有没有意义」。

### 修复:字符串缓存的起点是对端填的字段,而它被拿去喂 assert —— 判据是「这个断言的实参是谁选的」

**现象**:客户端在游戏线程上收到一条 `StringCacheUpdate` 之后进程直接退出,没有崩溃栈、没有异常、
没有日志 —— 因为杀掉它的不是崩溃,是 `assert`。触发条件不是「对端发坏包」,而是「对端的字符串缓存
与本地不同步」,后者在版本混连、存档重载、脚本改过游标之后都可能自然发生。

**根因**:`Code/encoding/StringCache.cpp` 的 `Deserialize` 用两条断言校验连续性,而两条的实参**都不是本地算的**:

```cpp
// Code/encoding/StringCache.cpp(修复前 :75-87)
    // We should only receive contiguous updates
    assert(aMessage.StartId == m_idToString.size());          // :78 实参来自线上

    for (auto& value : aMessage.Values)
    {
        const auto expectedId = m_idToString.size();
        auto id = Add(value);

        assert(id == expectedId);                             // :85 实参受对端内容影响
    }
```

`StartId` 是包里的字段(`Code/encoding/Messages/StringCacheUpdate.cpp:23` 的
`StartId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;`,没有任何本地校验);
而 `Add` 在值**已经在缓存里**时早退返回旧 id(`Code/encoding/StringCache.cpp:26-36`),
所以 `:85` 那条在「同一条消息里出现重复值」时也会失败 —— 而重复值是一条完全合法的消息。

**为什么没有任何东西报错**:`assert` 失败不产生日志,它调用 `abort()`。
更糟的是它**看起来像文档**:`Code/encoding/StringCache.cpp` 写侧的注释当时写着
「the receiver asserts that the ids are contiguous」,`docs/PITFALLS.md` 的 §49.2 判据表
与 §73 的排除语也都把它当成「正确的不变量断言」引用过。三处引用一处代码,没有一处问过
「这个实参是谁选的」。

**为什么 CI 挡不住、本地也看不见**:全仓 `NDEBUG` **零命中**,而 CI 的默认 `build-mode` 是 `releasedbg`
(`.github/workflows/windows.yml:11-14`)—— 也就是说断言在**发布构建里也是活的**,这条路径不需要 debug 构建。
本地同样看不见:`StartId` 不匹配要求两端缓存长度不同,而正常流程下服务端从 0 开始推、客户端从 0 开始收,
只有失步(混版本、重载存档、脚本改游标)才会碰到。

**修法**:两条断言换成「不连续就丢弃这条消息,缓存原样保留」(`Code/encoding/StringCache.cpp:76-104`):

```cpp
    if (aMessage.StartId != m_idToString.size())
        return;

    for (auto& value : aMessage.Values)
    {
        const auto expectedId = m_idToString.size();
        const auto id = Add(value);

        if (id != expectedId)
            return;
    }
```

- **为什么是「丢弃」而不是「清空缓存」**:第一版写的是 `Clear()`,测试也按那个方向写好了。改判的理由是代价 ——
  服务端每 2000 ms 才推一次增量(`Code/server/Services/StringCacheService.cpp:17`),
  清空会把一次失步放大成两秒的同步空洞,而丢弃只损失这一条。
- **为什么丢弃是安全的**:`m_idToString` 在任何 `return` 点上都仍是发送侧缓存的**连续前缀**。
  依据只有两条 —— `StartId` 的判断在任何变更之前;`Add` 在值已存在时早退且不触碰任何容器。
  所以「下一次发送的切片仍然对得上」是可推导的,不是希望。
- **重复值那条分支不是回滚**:`{"gamma","gamma"}` 的第一条 `Add` 成功之后才轮到第二条返回旧 id,
  丢弃之后缓存里有 `gamma` —— 这一点写进了测试的注释。
- **写侧注释同步改掉**(`Code/encoding/StringCache.cpp:55-59`):那句「the receiver asserts that the ids are contiguous」
  描述的是一个会把进程杀掉的契约,改实现不改它,下一轮审计还会把它当先例引用。
- **不打日志**:`Code/encoding` 层没有任何日志设施(全目录 3 个 `#include <iostream>` 一处使用都没有,也没有 spdlog),
  而唯一的调用点 `Code/client/Services/Generic/StringCacheService.cpp:28-31` 是 `noexcept` 且没有返回值,
  拿不到失败信息 ⇒ 这次是静默自愈。

**验证**:本机不编译(`docs/PITFALLS.md` §0/§1),证据是逐字命中 + 新用例。
`Code/tests/encoding.cpp:575-647` 新增
`TEST_CASE("A string cache update that does not line up is dropped", "[encoding.string_cache_selfheal]")`:

- `StartId = 5` 而本地长度是 2 ⇒ 断言 `Size() == 2`、`alpha`/`beta` 都还在、`gamma` 没进去
  (把「丢弃而不是清空」钉在用例里:改成 `Clear()` 这条断言就红)。
- 紧接着造一个「id 9 不在缓存里」的 `CachedString` 包,断言名字读成空串 —— 「丢弃之后消费者仍然安全」的证据。
- 重复值 `{"gamma","gamma"}` ⇒ 断言 `Size() == 3` 且 `gamma` 在,即第一条落了地。
- 对齐的消息(`StartId = 1`,`{"beta","gamma"}`)照旧生效 ⇒ 断言三条全在,证明正常路径没有被一起关掉。

> **教训**:第一条:**`assert` 不是「文档化的前置条件」,是「谁都能触发的进程退出」。**
> 判断一条断言该不该留,只问一句:它的实参是**谁**选的?本地算的,留;对端填的,换掉。
> 第二条:**注释会替错误作证。** 写侧那句「the receiver asserts that the ids are contiguous」
> 把「对端一跳就终止进程」描述成了一条设计约定,而它读起来完全像文档 ——
> 改掉实现时必须回头把引用它的注释、判据表、代码块引文一起改掉。
> 第三条:**「丢弃」和「清空」都叫「失败处理」,但代价差两个数量级。**
> 失败处理的粒度要跟「重来一次的代价」对齐:这里重来要等 2000 ms,所以粒度是这一条消息,不是整个缓存。

### 修复:一个 cell 里第 256 个之后的容器与门永远不同步 —— 判据是「写侧夹住了计数,那被夹掉的那些谁负责」

**现象**:一个放着 255 个以上容器与门的 cell,进去之后只有前 255 个会同步。第 256 个开始,门锁不上、
容器里的东西两边不一样,而且**每一次**重进这个 cell 都是同样一批被丢掉 —— 不是偶发,不是顺序问题。

**根因**:`AssignObjectsRequest` 的计数只有 8 位,写侧的处理是自洽的:

```cpp
// Code/encoding/Messages/AssignObjectsRequest.cpp(修复前 :11-14)
const auto count = std::min<size_t>(Objects.size(), kMaxObjectCount);   // :11 夹住
aWriter.WriteBits(count, 8);

for (size_t i = 0; i < count; ++i)                                      // :14 只写 count 条
    Objects[i].Serialize(aWriter);
```

包因此永远是合法的:计数与实际写出的条目数一致,读侧消费 `count` 条之后游标正好落在包尾。
但发送侧并不知道有这道夹取 —— `Code/client/Services/Generic/ObjectService.cpp` 把
`pCell->GetRefsByFormTypes({FormType::Container, FormType::Door})` 的**全部**结果推进 `request.Objects`,
然后 `:197 m_transport.Send(request);` **只发一次、不问多少条**。超过 255 条的那些既没进这个包,
也没有第二个包。

**为什么没有任何东西报错**:这是这一族里最容易被放过的一种形状。§49 修的是**写侧与读侧不一致**
(写侧只写计数低位、循环却写全部条目)⇒ 读侧少消费、剩余字节被当下一条消息解析 ⇒ **整条流失步**,
那种缺陷会以「连不上」「同步乱掉」的形式炸出来。这里写侧与读侧完全一致,流也没有失步:
丢的是**服务端从没听说过的那几个对象**,而客户端对「回包里没有这一项」的处理本来就是「不建本地映射」
(`Code/client/Services/Generic/ObjectService.cpp:200-243` 逐条 `GetGameId` + `Cast<TESObjectREFR>`,
命中才 `CreateObjectEntity`)—— 于是「服务端拒绝了它」与「这个对象根本不存在」在客户端看来是同一件事。
两边都没有一行日志。

**为什么 CI 挡不住、本地也看不见**:门禁能查的是「解析器信不信对端给的数」,而这里的数字是**写侧自己算的**,
永远自洽 —— 没有任何断言能从一条合法包上看出一部分内容根本没进包。本地也难碰上:
`docs/PITFALLS.md:9823` 自己写着任何真实 cell 的容器与门都在**几十到几千**量级,
所以 255 这个数看着绰绰有余,而「几千」那一半正是这个上限够不着的地方。

**修法**:
- 常量上移到两个消息头(`Code/encoding/Messages/AssignObjectsRequest.h:24-27`、
  `AssignObjectsResponse.h:23-26`,先例是 `Code/encoding/Structs/Tints.h:30-33`)。
  它原来是两个 `.cpp` 里的 file-scope `static`,而**发送侧现在也要按同一个数切片** ——
  一个约定只能有一个出处。
- `Code/client/Services/Generic/ObjectService.cpp:197-201`:在 `push_back` **之前**判满,
  满则先 `Send` 再 `clear`;收尾 `:206-207` 从无条件 `Send` 改成 `if (!request.Objects.empty())`,
  否则空 cell(或全被 `ShouldSyncObject` 过滤掉的 cell)会多发一个空包。
- **服务端不需要跟着改**:`response.Objects` 只在该 handler 的两处 push(命中 `Code/server/Services/ObjectService.cpp:98`、
  新建 `:123`),拒绝分支 `:100-103` 不 push ⇒ `response.Objects.size() <= acMessage.Packet.Objects.size()`。
  请求侧切片之后,响应侧永远到不了 255,`AssignObjectsResponse` 的夹取再也触发不到 ——
  一次改动同时修好两个方向,但**只改生产者的那一侧**。

**验证**:本机不编译,证据是逐字命中 + 新用例。`Code/tests/encoding.cpp:1462-1537` 新增
`TEST_CASE("An object list longer than the 8 bit count is sent in slices", "[encoding.assign_objects]")`:
先断言 `AssignObjectsRequest::kMaxObjectCount == 0xFF`,再造 258 条 `ObjectData`,
第一片 255 条往返后 `Objects.size() == kMaxObjectCount` 且 `back().Id` 正是第 255 条,
第二片 3 条往返后 `back().Id` 正是最后一条 —— 也就是「原来会被丢掉的那三条现在真的到了」。
最后把 258 条整批交给一个请求,断言写侧仍然只发 255 条且尾条是第 255 条,
把「发送侧必须切片」这个前提钉在用例里(否则以后有人删掉切片,这条断言会红)。

> **教训**:第一条:**「写侧夹住了计数」不等于「被夹掉的东西有人管」。**
> 夹取让包合法,而**合法正是它不报错的原因** —— 判据要问的是「这个上限是谁定的,超出它的那部分由谁负责」,
> 而这里定上限的是编码层、超出的部分是发送方,两边互不知情。
> 第二条:**同一个约定出现在两个文件里时,它迟早会变成两个各自为政的字面量。**
> 常量留在 `.cpp` 的 file-scope `static` 里,是因为当时只有写侧一个读者;发送侧要切片时它就必须上移到消息头。
> 第三条:**修一侧就够的时候,把「为什么另一侧不用改」的关系式写出来。**
> `response.size() <= request.size()` 这句话就是「不用对称地改两遍」的全部理由;
> 写不出这句话,就该两边都改。

### 修复:Morph 的三张表,键由对端选、键长也由对端选,而唯一的清理点是读档 —— 判据是「这个键是谁选的,而它什么时候被删掉」

**现象**:同一个对端每包换一个显示名,`plugins/MorphSyncTogether` 里三张表就每包多一行,
再也没有一行被删掉。三张表分别是 `UdpTransport::_senderConnections`(显示名 → 连接号)、
`AppearanceProbe::_snapshots`(显示名 + proxy FormID → 采样)、
`AppearanceProbe::_appearanceCaches`(显示名 → 面部材质基线)。

**根因**:三张表的键里都有对端自报的 `displayName`,而三处都没做长度判定、也都没做行数判定。
`UdpTransport.cpp` 的 `SanitizeField` 只把 `|`/CR/LF 换成 `_`、空则填 `"Player"` ——
它看起来像一道净化,净化的却是**分隔符**,不是长度。写入点则是每次收包无条件
`_senderConnections[sender] = message.sender.connectionID;`,连 `contains` 判定都没有。
`AppearanceProbe` 侧更隐蔽:两张表全文件只有两个 `operator[]` 插入点
(`AppearanceProbe.cpp` 的 `_snapshots[key]` 与 `_appearanceCaches[key]`),
其余 19 个读者全是 `find`,从不插入 —— 所以只要拦住这两个 `operator[]`,表就不会被撑大。

**为什么没有任何东西报错**:三张表都是「上一包是什么」的缓存,键配不上任何东西时本来也只返回
`nullptr`。表涨大不会让任何一次查询失败,只会让 `MorphSyncService.cpp` 每 tick 的那轮
`senderSet` 收集与 `ResolveRemoteProxyByName` 扫描变长 —— 那不是「内存慢慢涨」,
是每秒一次的 O(表大小 × 演员数)扫描,而它表现为卡顿,不表现为报错。

**为什么 CI 挡不住、本地也看不见**:清理代码是**存在**的 —— `UdpTransport::Stop()` 里有
`_senderConnections.clear()`,`AppearanceProbe::Reset()` 里有两个 `clear()`。
但 `Stop()` 的唯一调用者是 `~UdpTransport()`,`src/main.cpp` 只调 `Start()`;
`Reset()` 的唯一调用者是 `kPreLoadGame` 事件。也就是说:**进程起来之后,第一张表永远不清;
另外两张表只在读档时清。** 判据不是「有没有清理代码」,是「这行清理跑得到吗」——
而这一点,只有把调用者一路查到底才看得见。

**修法**:
- `plugins/MorphSyncTogether/src/UdpTransport.cpp`:加 `kMaxSenderNameBytes = 256` 与
  `kMaxSenderConnections = 64`,写入前先判键长,再在锁内判 `contains` / 行数;满了丢这一包并 `warn`。
- `plugins/MorphSyncTogether/src/AppearanceProbe.h`:加 `kMaxProbeKeyBytes = 256`、
  `kMaxProbeSnapshots = 256`、`kMaxAppearanceCaches = 128`。
- `plugins/MorphSyncTogether/src/AppearanceProbe.cpp`:键长门禁放在 `Probe()` 最前,
  两张表的行数上限各自贴在自己的 `operator[]` 前面。三处都是「满了丢这一次采样」,不是「清表」。
- 交付为 `Code/plugins/patches/MorphSyncTogether/0005-bound-the-peer-supplied-probe-keys-and-the-sender-name-table.patch`
  (插件是子模块,本仓不拥有也不能 push,CI 的 `git submodule update --init --force --recursive` 会丢弃只改工作树的改动)。

**验证**:本机不编译,证据是逐字命中 + 门禁 + 顺序应用。
`python Code\plugins\tools\plugin_patches.py verify` 从 24 个补丁变成 25 个并全绿;
`plugin_patches.py check` 报 `PATCH CHECK OK`;
`python Code\plugins\tools\check_transport_compat.py` 报 `TRANSPORT COMPATIBILITY OK`;
`python Tools\Scripts\check_docs.py` 报 `DOC CHECK OK`。
补丁的三个目标文件都能从 `src/AppearanceProbeBodyHair.cpp:9 #include "AppearanceProbe.cpp"`
这条链到达(`AppearanceProbe.cpp` 与 `MorphSyncService.cpp` 都不在 `CMakeLists.txt` 的 `SOURCES` 里,
它们是被 `#include` 编进去的),所以 §78 那条编译闭包门禁没有报「文件不在构建闭包里」。
按 0001 → 0002 → 0003 → 0004 → 0005 的顺序对 pin `017c7b0c` 全部应用成功。

> **教训**:第一条:**「键由对端选」要连着问「键有多长」,因为这两件事通常由同一段代码负责 —— 而它只做了其中一件。**
> 第二条:**一个只增不减的表,要看它唯一的清理点是谁在什么时候调。**
> 「有 `clear()`」不等于「`clear()` 会跑」;两张表的答案是「读档」,第三张表的答案是「析构」,而那个析构函数在进程起来之后再也不会被调用。
> 第三条:**节流不等于上限。** `Probe()` 的节流门禁查的是「这个键在不在表里」,
> 所以它对**新键**完全无效 —— 越是攻击者想要的路径,越是节流管不着的那条。

### 修复:中继的四个身份字段可以被对端抢注 —— 判据是「同一个名字,中继写在尾、对端写在头,而读取器取的是第一个」

**现象**:插件层的传输通道上,中继脚本每转发一封信封,就把服务端认证过的身份追加在**尾部**:

```lua
-- plugins/STRPluginMessagingAPI/extras/str-server-resources/strpm-chat-relay/main.lua(修复前 :180-185)
local relayEnvelope =
    message ..
    "|sender=" .. tostring(senderConnectionId) ..
    "|senderPlayerId=" .. tostring(senderPlayerId) ..
    "|senderName=" .. safeField(player:GetUsername()) ..
    "|serverTick=" .. tostring(gameServer:GetTick())
```

而 `message` 整段是**发送方自己拼的**。信封的头由对端写、尾由中继写,
同一个名字因此可以在一封信封里出现两次 —— 谁赢,取决于读的人取的是哪一个。

**根因**:两侧取的是相反的。中继脚本的字段表是**后者胜**:

```lua
-- main.lua(修复前 :30-41)
local function splitFields(envelope)
  local fields = {}
  ...
      fields[key] = value                                   -- :37,后出现的覆盖先出现的
```

而接收侧桥的读取器是**前者胜**:

```cpp
// plugins/STRPluginMessagingAPI/src/STRPluginMessagingBridgeReceive.cpp(修复前 :758-774)
std::optional<std::string_view> ReadField(std::string_view packet, std::string_view key)
{
    ...
        if (token.size() > key.size() && token.starts_with(key) && token[key.size()] == '=')
            return token.substr(key.size() + 1);              // :769,第一个匹配就返回
```

中继自己不会被骗 —— 它不读 `fields["sender"]`,它用的是 `player:GetConnectionId()` 与
`player:GetId()`(修复前 `:168-169`),取的是会话真值。被欺骗的是**接收方客户端**:
`AddFragment`(修复前 `:825-924`)的九次 `ReadField` 里有两次读的是保留字段,
`:869 pending.senderName = senderName ? std::string(*senderName) : std::string(fallbackPlayerName);`
之后一路写到

```cpp
// plugins/STRPluginMessagingAPI/src/STRPluginMessagingBridgeReceive.cpp(修复前 :936-937)
message.sender.connectionID = completed->senderId;
message.sender.displayName = completed->senderName.c_str();
```

第二处在 `src/STRPMProxyResolverBridge.h`:`ObserveIncomingEnvelope` 读 `sender`/`senderPlayerId`
后直接 `ObserveSender(*connectionID, *playerId)`(修复前 `:886`),写进 `g_connectionToPlayer`。

**为什么没有任何东西报错**:抢注出来的是**合法值**。`senderName` 是一个合法字符串,
`sender` 是一个合法连接号,信封本身仍然过 `validateEnvelope`(修复前 `:82-111`)——
它校验 `msg`/`seq`/`channel`/`payload`/`flags`/`part`/`parts`/`target`,
**从不过问字段集合**,所以多一个 `|sender=` 不是畸形。受害面是所有以 displayName 为键的表:
`plugins/MorphSyncTogether/src/UdpTransport.h:48`、`src/TngSync.h:58`、
`MorphSyncService.h:130-134`、`plugins/IEDSyncTogether/src/STRPMAdapter.h:63`、
`plugins/TradeTogether/src/StrMessagingTransport.cpp:517`;
而 `plugins/MorphSyncTogether/src/MorphSyncService.cpp:1012` 的
`if (!rawName || !EqualsInsensitive(rawName, name)) return;` 会把伪造的名字解析成
**一个真实存在的代理角色** —— 于是外观 / TNG / 交易的身份可以按名字冒充。

**为什么 CI 挡不住、本地也看不见**:`check_transport_compat.py` 当时只把中继的
`MAX_ENVELOPE_LENGTH` 与 `WireLimits.h` 的 `kMaxChatMessageBytes` 放在一起看
(上一节刚加的那条),以及两个 opcode 下标。**没有任何一条检查把中继追加的字段名
与读取它们的字段名放在一起看** —— 而这正是缺陷所在:名字是同一个,位置是两头。
每一层单独读都是对的:中继确实追加了真值,桥确实读了一个 `senderName`。

**修法**:保留字段单独按「尾部」读,其余字段的读法一个都不动。

- `src/STRPluginMessagingBridgeReceive.cpp:776-797` 与 `src/STRPMProxyResolverBridge.h:681-702`:
  新增 `ReadRelayField`,与前者的唯一区别是**记下最后一个匹配再返回**(`found = token.substr(...)`,
  循环结束才 `return found;`)。桥里只有 `sender`/`senderName` 两处换成它,另外七个字段
  (`msg`/`channel`/`seq`/`flags`/`part`/`parts`/`payload`)都是客户端写、中继不改的,仍走 `ReadField`。
- `src/STRPMProxyResolverBridge.h`:该文件的两个读取点**都**是保留字段,没有客户端字段的读者,
  所以原来的 `ReadField` 整体被 `ReadRelayField` 取代。
- `extras/str-server-resources/strpm-chat-relay/main.lua:13-17`、`:88-95`、`:183-187`:
  中继自己判一次 —— `RESERVED_FIELDS = { "sender", "senderPlayerId", "senderName", "serverTick" }`,
  信封里已经带了其中任何一个就丢弃并打印
  `[STRPM] Dropped envelope carrying reserved field <name>`。这一条挡的是**转发**,
  上面两条挡的是**读取**,两层各自独立成立。
- `docs/OFFICIAL_STR_CHAT_TUNNEL.md:86-95`(子模块内):追加字段那行补上漏写的
  `senderPlayerId`,并写明这四个名字是保留名、中继会拒收已带保留名的信封、桥从尾部读。

**没有把 `ReadField` 整体翻成后者胜**,这是这次修复里最容易做错的一步:`payload` 必须保持**前者胜**。
identity 通道(`main.lua:176-178`,修复前)用 `replacePayload(message, hexEncodeAscii(tostring(senderPlayerId)))`
覆写的是**第一个** `|payload=`(修复前 `:70-80` 的 `string.gsub(..., 1)`),中继写进去的 PlayerId
就落在第一个 `payload=` 上。整体翻转的话,客户端在尾部补一个 `|payload=` 就能顶掉它 ——
「认证过的身份」会从另一个口子漏掉。

**验证**:本机不编译(`docs/PITFALLS.md` §0/§1),所以证据是逐字命中 + 门禁 + 反向验证,不是编译产物。
`Code/plugins/tools/check_transport_compat.py` 新增一段(三部分:三个文件必须都在、
`main.lua` 必须声明 `RESERVED_FIELDS` 且**真的调用** `reservedFieldPresent(fields)` 且在追加字段之前、
两个 C++ 文件各自的 `ReadRelayField` 函数体必须含 `found = token.substr` 且不得含 `return token.substr`),
并把 `ReadRelayField(envelope, "sender")`/`"senderName"`/`"senderPlayerId"` 三个调用点写死。
反向验证过两轮:把桥的 `found = token.substr(...)` 改回 `return token.substr(...)`,
门禁立刻 exit 1 并给出「ReadRelayField returns the first match again」;
把 `local reserved = reservedFieldPresent(fields)` 改成 `local reserved = nil`,
门禁立刻 exit 1 并给出「declares RESERVED_FIELDS but never calls reservedFieldPresent」;
两次都恢复后回到 `TRANSPORT COMPATIBILITY OK`。
交付是 `Code/plugins/patches/STRPluginMessagingAPI/0003-read-the-relays-own-sender-fields-from-the-tail.patch`
(7076 字节 / 156 行 LF / 9 个 hunk / 4 个文件),按 0001 → 0002 → 0003 的顺序对 pin `62143fd` 全部应用成功。

> **教训**:第一条:**同一个名字可以在一份报文里出现两次,而「谁赢」是两个相反的实现细节。**
> 中继的 Lua 表是后者胜,接收侧桥的读取器是前者胜 —— 两个都对,合起来就是可抢注。
> 定这类字段时先问「这个字段是**谁写的**,写在哪一段」,再问「读的人取第一个还是最后一个」。
> 第二条:**「服务端认证过了」不等于「客户端读到的就是认证值」。**
> 中继确实把真值追加进去了,认证没有失效;失效的是**读取**,而读取发生在别人机器上。
> 第三条:**修一个「取第一个」的读取器时,要看同一个函数里有没有别的字段依赖「取第一个」。**
> 这里 `payload` 就依赖 —— 翻整体的方向会把 identity 通道的认证从另一个口子放走。
### 修复:一个按「一行聊天」定的上限,把插件层的传输通道静默截断了 —— 截断后的 hex 仍然是合法 hex

**现象**:`ChatMessage` 这一个线上字段同时承担两件事。对玩家它是聊天行;
对 7 个伴生插件它是**唯一的传输通道** —— 桥把 `STRPM|v2|...` 信封塞进聊天行,
服务端的中继脚本把信封读出来再转发给目标玩家。两条契约的尺寸相差 8 倍:

```lua
-- plugins/STRPluginMessagingAPI/extras/str-server-resources/strpm-chat-relay/main.lua
local MAX_ENVELOPE_LENGTH = 4096                                        -- :8
...
if string.len(message) > MAX_ENVELOPE_LENGTH then
  print("[STRPM] Dropped oversized envelope")                           -- :150-153
  return
end
```

而上一轮给这个字段加的上限是按聊天行定的:

```cpp
// Code/encoding/Messages/SendChatMessageRequest.cpp(修复前)
ChatMessage = Serialization::ReadString(aReader);
WireLimits::Clamp(ChatMessage, WireLimits::kMaxChatMessageBytes);   // :16,当时是 512
```

**根因**:夹取发生在**中继脚本之前**。服务端收包链是
`Code/server/GameServer.cpp:572-602 OnConsume` → `ClientMessageFactory::Extract`
(`Code/encoding/Messages/ClientMessageFactory.cpp:17-20` 里 `ptr->DeserializeRaw(aReader)`)→
`Code/server/Services/OverlayService.cpp:60-81 HandleChatMessage` →
`Code/server/Services/ScriptService.cpp:164-167` 的 `CallCancelableEvent("onChatMessage", ...)`,
而中继脚本正是挂在 `onChatMessage` 上的。>512 字符的信封在脚本看到它之前就已经短了一截,
`plugins/STRPluginMessagingAPI/src/STRPluginMessagingBridge.cpp:31` 的
`constexpr std::size_t kMaxEnvelopeChars = 3500;` 与这个 `512` 同时存在于仓库里,却没有任何一处对比过。

**为什么没有任何东西报错**:被切掉的是 `payload=<hex>` 的**尾巴**。信封字段顺序是
`msg|seq|channel|target|flags|part|parts|payload`,中继追加的 `sender`/`senderPlayerId`/
`senderName`/`serverTick` 在最后 ⇒ 截断后 `part`/`parts` 仍在,中继脚本
`plugins/STRPluginMessagingAPI/extras/str-server-resources/strpm-chat-relay/main.lua:89` 的
`string.match(fields["payload"], "^[0-9A-Fa-f]*$")` 对任何 hex 前缀都通过;
接收侧 `plugins/STRPluginMessagingAPI/src/STRPluginMessagingBridgeReceive.cpp:798` 的
`if ((text.size() & 1u) != 0) return std::nullopt;` 只多判一条奇偶 ⇒
偶数长度的截断 hex 被**照常解码**,消费方拿到一个更短、内容错误的载荷,而不是「这条消息被丢了」。

**为什么 CI 挡不住、本地也看不见**:三个数值住在三个互不引用的文件里(C++ 头、Lua 资源、C++ 桥),
没有任何一条检查把它们放在一起看。这不是编译能发现的问题 —— 每一层单独看都是对的。

**修法**:
- `Code/encoding/WireLimits.h:33-44`:`kMaxChatMessageBytes` 从 `512` 改成 `4096 + 512`,
  并把「这个字段同时是插件层传输通道」与「截断 hex 仍是合法 hex」写进注释。
- `Code/plugins/tools/check_transport_compat.py:153-198`:新增一段,读 `main.lua` 的
  `MAX_ENVELOPE_LENGTH` 与 `WireLimits.h` 的 `kMaxChatMessageBytes`,要求后者**严格大于**前者
  —— 中继会在信封后面追加 `|sender=|senderPlayerId=|senderName=|serverTick=`。
- `Code/tests/encoding.cpp:838-886`:新增断言,要求一个「中继上限 + 元数据」大小的信封在
  `SendChatMessageRequest` 与 `NotifyChatMessageBroadcast` 两个方向都原样往返,并显式断言它 `> 512`。
- `docs/PITFALLS.md`:§62.4 里那句 `kMaxChatMessageBytes = 512` 已按同一提交更正,新增 §79。

**验证**:本机不编译(`docs/PITFALLS.md` §0/§1),所以证据是逐字命中 + 门禁 + 反向验证,不是编译产物。
`python Code/plugins/tools/check_transport_compat.py` 全绿
(`client opcodes: 53 in use, plugin request at index 52` / `server opcodes: 56 in use, plugin notify at index 55` /
`chat opcodes  : send 38, broadcast 36`);把 `WireLimits.h` 改回 `512` 后门禁立刻 exit 1 并给出上面那段文案,
改回 `4096 + 512` 后恢复 exit 0。`python Tools/Scripts/check_docs.py` 全绿。

> **教训**:第一条:**给字段定上限时,要问的不是「这个值多大」,而是「这个字段被当成什么在用」。**
> `ChatMessage` 的名字只说了聊天,而同一个仓库里 `kMaxEnvelopeChars = 3500` 已经写着它还是传输层。
> 第二条:**截断一个 hex 字符串不会让它变成非法 —— 它会变成一个更短的合法值。**
> 「下游会校验」在这里是空的:校验器能看出的只有奇偶,看不出内容少了一半。
> 第三条:**跨文件的数值契约要进门禁,不能靠注释。**
> 注释只能提醒读到这里的人;门禁能在任何人改其中一个时立刻说话 —— 而它这次报的是本仓自己上一轮引入的回归。
### 修复:IEDSyncTogether 的一张上限加在一个从来没被编译的文件上 —— 三条门禁全绿,而那个文件不在任何二进制里

**现象**:`plugins/IEDSyncTogether` 在 pin `ad4710a6` 上的 `src/` 有 25 个 `.cpp/.h`,
而 `CMakeLists.txt:15-32` 的 `SOURCES` 只列 16 项。`src/SyncService.{h,cpp}`、`src/ProxyResolver.{h,cpp}`、
`src/Config.{h,cpp}`、`src/StrTransport.{h,cpp}`、`src/UdpTransport.{h,cpp}`、`src/StrServerDiscovery.{h,cpp}`
这 12 个文件既不在 `SOURCES` 里,也没有任何 `#include` 链从 `SOURCES` 到达 —— 它们**不在任何二进制里**。

**根因**:上一轮给这张「键由对端填」的表加上限时,补丁打的是 `src/SyncService.h` 与 `src/SyncService.cpp`。
那是 STRPM-only 运行时之前的旧架构,`ace777c16633ad4d368c5e9b5b58e7b83104dd7a`(2026-08-19,
`Build v0.6.0 STRPM-only runtime`)把它从 `SOURCES` 里删掉了 15 行,文件留在树里没删。
于是那条上限**从未进过任何二进制**,而 `git apply` 只看文本能不能对上,一路绿灯。

真正在编译的是 `src/STRPMAdapter.{h,cpp}` 与 `src/LocalIEDState.{h,cpp}`,两张表的实际位置是:

```cpp
// plugins/IEDSyncTogether/src/STRPMAdapter.h(修复前)
std::unordered_map<STRPM::ConnectionID, RemoteSnapshot> _remoteStates;   // :63,键是对端填的

// plugins/IEDSyncTogether/src/LocalIEDState.cpp(修复前)
for (const auto line : Split(payload, '\n'))                            // :80,行数由对端决定
    state.objects.emplace_back(std::move(object));                       // :140,每行追加一个
```

**为什么 CI 挡不住、本地也看不见**:`plugin_patches.py verify` 当时只判三件事 ——
补丁能不能打上、`+` 行的 fmt 占位符有没有写错、补丁是不是已经应用过。
这三件事**都只关心补丁的文本**,没有一件问过「被改的文件编不编」;
而文档里点名的补丁路径、`// plugins/…(修复后)` 标记也没有任何东西核对过,
所以旧补丁名可以删掉、标记可以继续指着一个没人再改的文件。

**修法**:补丁换靶。删掉 `0002-bound-the-remote-snapshot-table.patch`,
新增 `Code/plugins/patches/IEDSyncTogether/0002-bound-the-live-peer-table-and-the-decoded-object-list.patch`
(4 个 hunk,2735 字节),打真正被编译的四个文件:

- `src/STRPMAdapter.h:61` 加 `static constexpr std::size_t kMaxRemoteStates = 64;`,
  `src/STRPMAdapter.cpp:555` 在 `_remoteStates[senderID] = RemoteSnapshot{…}` 之前守卫,
  满表丢弃这个包并 `SKSE::log::warn("STRPM IED state RX dropped: peer table full size={} sender={}", …)`;
- `src/LocalIEDState.h:36` 加 `static constexpr std::size_t kMaxObjects = 512;`,
  `src/LocalIEDState.cpp:96` 在解 `O|` 行时判上限 —— **整包拒收而不是截断**,
  因为少几个对象的快照会被下游当成完整快照渲染。

**顺带加了两条门禁**(`Code/plugins/tools/plugin_patches.py`),都挂在 CI 已经在跑的那道 `verify` 上:

1. **构建闭包**:从该插件每份 `CMakeLists.txt` 的源 token 出发做 `#include "…"` 闭包,
   补丁改的每个源文件都必须落在这个闭包里,否则报错并直接说明「这个文件永远不会进二进制」。
   含 `GLOB` 的插件(OStimNG)整插件跳过;MorphSyncTogether 那种靠 `#include "X.cpp"` 编译的文件靠闭包到达。
2. **文档与补丁目录一致**:文档(`README.md`、`README_EN.md`、`CHANGELOG.md`、`docs/PITFALLS.md`)里
   点名的 `Code/plugins/patches/<id>/<file>.patch` 必须真实存在,
   点名的 `// plugins/<id>/<path>(修复后)` 标记必须真的是该插件某个补丁的 `--- a/` 目标。
   含 `...` 的刻意的省略写法跳过。

反向验证过:门禁**第一次实跑就报出 7 条、零误报** —— 4 条是文档点名的补丁路径已经不存在
(`README.md`、`README_EN.md`、`CHANGELOG.md`、`docs/PITFALLS.md` 各 1),
3 条是 `plugins/IEDSyncTogether/src/SyncService.{h,cpp}(修复后)` 这种标记还在、
而那个文件已经没有任何补丁会改它。这 7 处引用全部改正,
并且 §78 自己引用旧补丁名/旧标记的那三行也被自己的门禁抓出来、改成不带路径前缀的写法。

**验证**:`python Code\plugins\tools\plugin_patches.py verify` → `PATCH VERIFY OK` / `23 patch(es)`;
补丁总数仍是 23(重写不是新增)。

> **教训**:补丁能应用 ≠ 补丁编得过,也 ≠ **补丁改的文件编得过**。
> `git apply` 的绿灯只证明文本能对上;一个文件从 `SOURCES` 里被删掉之后,
> 树里那一份还在、`grep` 还找得到、补丁还能打上,唯一不再发生的事就是**它被编译**。
>
> **教训**:审计一个「热路径」之前,先问这个文件有没有被编译。
> 这一轮最初怀疑的是 `src/SyncService.cpp:302-379` 的 `RefreshProxyMitigation()`
> 每 750 ms 对每张快照做一次 ProcessLists 全表扫描 —— 结论是这个函数不在任何发布产物里,
> 该候选因此结案,不是「不值得修」,而是**修了也没有任何效果**。
>
> **教训**:一条判据只有在**它能自己跑**的时候才算判据。
> 「这个补丁改的文件在不在构建闭包里」是可以用 CMakeLists 的源列表 + `#include` 闭包机械判定的,
> 所以它进了门禁;而「这条上限到底有没有生效」只能靠编译器,本地没有工具链时,
> 唯一能守住的底线就是**别让补丁打到编译不到的地方**。
### 修复:两张键由对端选、长度也由对端选的表 —— TradeTogether 的对端名字表与请求 id 历史表

**现象**:TradeTogether 的 `StrMessagingTransport` 把「对端名字 → 连接号」写进
`_namedConnections`,而那个名字是**包内 `from` 字段** hex 解码后的字符串:
`Protocol::HexDecode`(`plugins/TradeTogether/src/Protocol.cpp:37-54`)只判长度是偶数、每个字符是十六进制,
**没有任何长度上限**,它拿到的字段又来自上限为 `24 * 1024` 的载荷 ⇒ 单个键最长约 12 KiB。
同一插件里 `TradeState::recentIncoming` 的键是包内 `request` 字段,而**同一次解析里 `from`/`to` 是有 256 上限的**
(`Trade.cpp:1061`),`requestID` 却只判了「非空」。

**根因**:两张表的键都由对端选,而**没有任何一处问过键有多长、表有多少行**:

```cpp
// plugins/TradeTogether/src/StrMessagingTransport.cpp(修复前)
std::optional<std::string> peerName;
if (const auto encodedFrom = Protocol::ReadField(packet, "from"))
{
    peerName = Protocol::HexDecode(*encodedFrom);       // :514-515,长度只受 24 KiB 载荷约束
}
...
if (peerName && !peerName->empty() && !Protocol::EqualsInsensitive(*peerName, GetLocalPlayerName()))
{
    ...
    if (a_message->sender.connectionID != 0)
    {
        std::scoped_lock lock(_proxyMutex);
        _namedConnections[*peerName] = a_message->sender.connectionID;   // :517
    }
}
```

```cpp
// plugins/TradeTogether/src/Trade.cpp(修复前)
if (state.recentIncoming.contains(a_requestID))
{
    return;
}
state.recentIncoming.emplace(a_requestID, std::chrono::steady_clock::now());   // :749-751
```

判据是**回收路径**:`_namedConnections` 只有三处清空(`kCleared` 分支 `:184`、`Start()` `:317`、
`Stop()` `:402`),其余只有一处 `std::erase_if(… e.second == a_event->connectionID)`(`:194`)——
**按连接号删,不按名字删**,所以换一个 `from` 就是一个永远删不掉的键;`recentIncoming` 只有
`Update()` 里的按龄清扫(`:976-983`,`historyLifetime = std::max(requestTimeout * 2, 60000ms)`),
而 `HandleIncomingRequest` 的插入发生在**「本机是否正忙」这道判断之前**(`:754-766` 先插入、`:768` 才判忙)
⇒ 被自动拒绝的请求同样占一行,60 秒窗口内的行数完全由对端决定。

这张表的代价落在**游戏线程**:`ResolveConnectionIDForPlayerName`(`:233`)先持 `_proxyMutex`
线性扫 `_namedConnections` 做大小写不敏感比较(`:241`),而它的唯一调用点是 `SendTo`(`:420`);
`Update()` 的按龄清扫则挂在每帧输入事件(`Input.cpp:52`)与每个网络包(`Trade.cpp:976`/`:998`/`:1047`)上。

**判据可机械执行**:同族的修法已经在仓里 —— `IEDSyncTogether/0002` 的 `kMaxRemoteStates = 64`
与 `kMaxObjects = 512`、
`MorphSyncTogether/0004` 的三张 64、`OStimTogether/0007` 的 `kMaxResolvedEntries`/`kMaxRemoteNames = 512`、
`0008` 的 `kMaxRemotePreps = 32`/`kMaxReadyTokens = 256`,全部是「先判 `contains` 再判上限,
已有的键永远仍可更新,被拒的只是新键」。这一批缺的只有这两张。

**修法**:

- `plugins/TradeTogether/src/StrMessagingTransport.cpp:14-20` 加两个文件内常量
  `constexpr std::size_t kMaxPeerNameBytes = 256;` 与 `constexpr std::size_t kMaxNamedConnections = 64;`。
  256 与 Trade 层自己的 `Trade.cpp:1061` 上限**取同一个值**:更长的名字在那边必然被丢掉,
  在这边也永远匹配不上任何一次 `ResolveConnectionIDForPlayerName`,所以拒绝它不丢信息。
- `:520-538` 收包守卫加上 `peerName->size() <= kMaxPeerNameBytes`,写入改成
  `if (_namedConnections.contains(*peerName) || _namedConnections.size() < kMaxNamedConnections)`
  才赋值,否则打一条 `spdlog::warn("TradeTogether STRPM peer name table is full: size={} nameBytes={}", …)`。
- `:261-265` 代理解析那条写入路径同样夹住(`ResolveConnectionIDForPlayerName` 一次会写两个键),
  否则「表满」只能靠收包路径挡住一半。
- `plugins/TradeTogether/src/Trade.cpp:21-27` 加 `constexpr std::size_t kMaxRequestIDBytes = 64;`
  与 `constexpr std::size_t kMaxRecentIncoming = 256;`。64 是本地 `MakeRequestID()` 产出的 32 字符
  (`:100-108`,`fmt::format("{:08X}{:016X}{:08X}", …)`)的两倍。
- `:757-763` 在 `contains` 之后、插入之前加满表早退(满表丢弃并打一条 warn);
  `:1068-1070` 的校验加上 `requestID->size() > kMaxRequestIDBytes`。
- 交付:`plugins/TradeTogether` 是子模块,而 CI 的
  `git submodule update --init --force --recursive --depth=1` 会丢弃只改工作树的改动,
  所以修复以 `Code/plugins/patches/TradeTogether/0002-bound-the-peer-name-and-request-id-tables.patch`
  交付(`0001` 只改 `src/main.cpp`,所以这一份的基线就是「`0001` 已应用」)。

**验证**:`python Code\plugins\tools\plugin_patches.py verify` → `PATCH VERIFY OK` / `23 patch(es)`;
`check` → `PATCH CHECK OK`;`plugin_snapshot.py verify` → `SNAPSHOT OK` / `8 plugin(s) have a durable
copy matching the pinned commit`;`Tools\Scripts\check_docs.py` → `DOC CHECK OK`;
`check_transport_compat.py` → `TRANSPORT COMPATIBILITY OK`(**这一批没有动线格式**)。
补丁的两个方向都实测过:`git apply --check --cached`(对 index = pin)exit 0,
`git apply --check --reverse`(对工作树)exit 0,而不带这两个开关的 `git apply --check` 对工作树
必然失败(`error: patch failed: src/StrMessagingTransport.cpp:11`)—— 因为改动已经在里面。
两处修改都不新增 include(`plugins/TradeTogether/src/PCH.h:16-34` 已有
`<chrono> <cstdint> <string> <unordered_map>`)。本机不编译(仓库约束),所以证据是逐字命中 + 门禁,
不是编译产物。

**教训**:同一次解析里两个字段一个有上界一个没有,这就是判据 —— `from`/`to` 早就有 256 上限,
而同一个包里的 `request` 只有「非空」。

**教训**:上限要加在**写入它的那一层**,不是消费它的那一层 —— Trade 层的 256 校验发生在
transport 已经把键写进 `_namedConnections` 之后,「下游会挡住」挡住的只是下游的使用,不是上游的增长。

**教训**:「有回收路径」不等于「有上界」—— `recentIncoming` 有按龄清扫,看起来是自愈的,
但 60 秒窗口里的行数完全由对端决定,而清扫本身每帧都要遍历整张表;
回收路径的**周期**决定了窗口内能堆多高。
### 修复:对象实体表只增不减 —— 一张行数由客户端决定、却只由「房间空掉」回收的表

**现象**:服务端 `ObjectService` 的实体数由客户端发来的 `AssignObjectsRequest` 决定:
每个包里最多 255 个对象,而**包没有速率限制**、实体总数**没有上限**。每个新建实体都带一份
对端给的 `Inventory`(≤ 4096 条 × 每条 ≤ 64 个附魔效果),而全仓唯一销毁它的地方是
`OnPlayerLeaveCellEvent` —— 也就是说**只要有一个玩家待在这个 cell 里,它里面的对象就永远活着**。
一个客户端反复发同一个包(哪怕每次只改一个 `Id`)就能让实体表一直涨下去。

**根因**:这条路径上没有任何一处问过「现在有多少」:

```cpp
// Code/server/Services/ObjectService.cpp(修复前)
else
{
    const auto cEntity = m_world.create();          // :96,没有计数、没有上限
    m_world.emplace<FormIdComponent>(cEntity, object.Id);
    auto& objectComponent = m_world.emplace<ObjectComponent>(cEntity, acMessage.pPlayer);
    ...
}
```

判据是**同一个系统里的另一条容量路径早就有上限**:`GameServer.cpp:877` 在
`GetPlayerManager().Count() >= uMaxPlayerCount.value_as<uint32_t>()` 时直接拒绝入服
(默认 8,`GameServer.cpp:29`),而同族的每一张对端可控表(插件层的 `kMaxRemoteSnapshots`、
`kMaxRemotePreps`、`kMaxReadyTokens`)都已经有上限 —— 缺席的只有这张实体表。

**为什么不在服务端校验 `object.CellId`**:看起来「对端声称的对象所在 cell」应该等于发送者
当前的 cell,但**合法流量下这两个值也会不一致**:`Code/client/Services/Generic/DiscoveryService.cpp`
只有 `gameCenterGrid` 变化才发 `ShiftGridCellRequest`,而 `gameCurrentGrid` 单独变化时只发
`CellChangeEvent`;并且 `Code/client/World.cpp` 里 `ObjectService`(`:47`)的 `CellChangeEvent`
sink 注册早于 `PlayerService`(`:55`)⇒ 客户端**总是先发** `AssignObjectsRequest`、后更新自己的
`CellIdComponent`。严格相等会打断正常同步,所以这条不做。

**同一文件里的第二个缺陷**是只写不读的字段:

```cpp
// Code/server/Components/ObjectComponent.h(修复前)
struct ObjectComponent
{
    ObjectComponent(Player* apLastSender) : pLastSender(apLastSender) {}
    Player* pLastSender;        // 全仓只有这三行:构造参数、初始化、字段本身
    LockData CurrentLockData{};
};
```

`git grep -rn pLastSender -- Code` 只有 3 处命中、**没有任何读者**;而 `Player` 由
`GameServer.cpp:610-670 OnDisconnection` 里的 `m_pWorld->GetPlayerManager().Remove(pPlayer)`
释放,对象实体却要活到 cell 空掉为止 —— 于是这个指针在玩家掉线后必然悬垂。判据是
**「没有人读的字段」比「加一处清理」更安全的修法是删掉它**,不是给它补一个失效通知。

**修法**:

- `Code/server/Services/ObjectService.h:37` 加类内常量(与 `PluginMessagingService.h:44-47`
  的 `static constexpr std::uint32_t kMax…` 同形),`:29-34` 写清它为什么存在:
  `static constexpr std::uint32_t kMaxObjectEntities = 64 * 1024;`
  (4 字节/实体 ⇒ 约 256 KiB,远大于任何真实 cell 的容器与门;`Pch.h` 无 `<algorithm>`,
  所以这里补了 `#include <cstdint>`。)
- `Code/server/Services/ObjectService.cpp:64-68` 在 `view` 建立之后数一遍现有实体
  (`uint32_t objectCount` / `for (auto it = std::begin(view); …) ++objectCount;`),
  `:100-107` 未命中分支改成 `else if (objectCount >= kMaxObjectEntities) { ++refusedCount; }`,
  只有能建的那一支才 `++objectCount;` —— **已有的对象永远仍能命中查询分支**,
  被拒的只是新实体。
- `:127-129` 饱和时打一条 `spdlog::warn("Object table is full ({} objects), {} of {} requested
  objects from player {} were refused", …)`。被拒的实体不进 `response`,客户端那一侧
  (`Code/client/Services/Generic/ObjectService.cpp:200-243`)对「回包里没有这一项」的处理
  本来就是「不建本地映射」,所以是**少同步一个对象,而不是记错一个对象**。
- `Code/server/Components/ObjectComponent.h` 删掉带参构造与 `pLastSender`(保留
  `#include <Game/Player.h>`:它是组件头里唯一 include 它的地方,别的 TU 靠它传递拿完整 `Player`);
  `Code/server/Services/ObjectService.cpp:111` 随之改成 `m_world.emplace<ObjectComponent>(cEntity);`。

**顺带补掉同一批浮点读取里最后 6 处**:`Code/encoding/Messages/RequestPlayerHealthUpdate.cpp:14`、
`NotifyPlayerHealthUpdate.cpp:16`、`Code/encoding/Structs/Inventory.cpp:19`(效果强度 `Magnitude`)、
`:22`(`RawCost`)、`:63`(`ExtraCharge`)、`:76`(`ExtraHealth`),以及
`Code/encoding/Structs/AnimationVariables.cpp:149`(动画变量浮点)。判据是**这些值接下来决定了什么**:
`ExtraCharge` 经 `Code/client/Games/Skyrim/TESObjectREFR.cpp:611` 的 `if (arEntry.ExtraCharge > 0.f)`
进 `SetChargeData`、`ExtraHealth` 经 `:657` 同形的门进 `SetHealth` —— **`+inf` 比 `0.f` 大,所以它过得去**;
`Magnitude`/`RawCost` 经 `Code/client/Games/Skyrim/Forms/EnchantmentItem.cpp:24-27` 直接写进引擎的
`EffectItem`;血量百分比在 `Code/client/Services/Generic/OverlayService.cpp:543` 只做
`>= 0.f ? … : 0.f` 的三元,**同样是 `+inf` 过得去**;而 `AnimationVariables` 的浮点由对端填的
位图决定读不读(`:143-149`)。六处统一改成 `WireLimits::ClampFinite(Serialization::ReadFloat(aReader))`
(`Code/encoding/WireLimits.h:74` 已有),四个文件各补 `#include <WireLimits.h>`(写在第 2 行,
与 `Code/encoding/Messages/AddTargetRequest.cpp:1-2` 同形)。

**验证**:`Code/tests/encoding.cpp` 的 `[encoding.finite_floats]` 新增五个块,接收对象一律先写有限哨兵
`1.f`,让「读的时候替换了」成为断言成立的唯一原因:`Inventory::Entry` 的 `ExtraCharge = +inf` /
`ExtraHealth = NaN` 读回 `0.f`(`:1222-1223`)、`Inventory::EffectItem` 的 `Magnitude = +inf` /
`RawCost = -inf` 读回 `0.f`(`:1244-1245`)、`RequestPlayerHealthUpdate` 与 `NotifyPlayerHealthUpdate`
各做一次整包往返(`:1258`/`:1271`,后者同时断言 `PlayerId == 21` 证明字段顺序没被改动)、
`AnimationVariables` 用 `GenerateDiff`/`ApplyDiff` 走位图路径断言 `{0, 0, 0, 2.5}`(`:1295-1300`,
2.5 那一条证明**有限的合法值原样通过**)。`git grep -rn "ClampFinite" -- Code/encoding` 现在 28 处命中、
`ClampFinite` 定义本身在 `WireLimits.h:74`;`git grep -rn "pLastSender" -- Code` **exit 1 / 零命中**;
`python Tools\Scripts\check_docs.py` → `DOC CHECK OK`;
`python Code\plugins\tools\plugin_patches.py verify` → `PATCH VERIFY OK` / `22 patch(es)`;
`check` → `PATCH CHECK OK`;`plugin_snapshot.py verify` → `SNAPSHOT OK`;
`check_transport_compat.py` → `TRANSPORT COMPATIBILITY OK`(没有新 opcode,`Code/encoding` 的线格式不变)。
本机不编译(仓库约束),所以证据是逐字命中与门禁,不是编译产物。

**教训**:「这个字段合法吗」和「这个字段能有多少个」是两个问题 —— `object.Id` 的取值空间是 2^64、
`AssignObjectsRequest` 也确实是合法包,而表的**行数**没人管。

**教训**:回收路径决定了上限的必要性 —— 只在「房间空了」时回收,就等于「只要有人待着就一直涨」;
先问「这张表什么时候变小」,再问「它能变多大」。

**教训**:只写不读的字段是负债而不是接口 —— 它不会因为「反正没人用」而安全,只会等到有人用它时
才发现它已经悬垂了;删掉它比给它补清理便宜。
### 修复:任意客户端一个畸形包即可打崩专用服务器,并修掉一整类「线上计数与实体数不一致」

**现象**:专用服务器上,一个**还没通过认证**的连接发一个畸形的
`AssignCharacterRequest`,服务端进程直接终止,没有崩溃转储、没有日志。

**根因**:`Factions::Deserialize`(`Code/encoding/Structs/Factions.cpp`)在阵营
数量超过 511 时**抛异常**:

```cpp
// Code/encoding/Structs/Factions.cpp(修复前)
auto npcCount = Serialization::ReadVarInt(aReader);
if (npcCount > 0x1FF)
    throw std::runtime_error("Too many NPC factions received !");
```

而这条调用链**整条都是 `noexcept`**:`AssignCharacterRequest::DeserializeRaw`
(`Code/encoding/Messages/Message.h:24`)是 `noexcept` 虚函数,
`CharacterData::Deserialize`、`CharacterSpawnRequest::DeserializeRaw` 同样如此;
`CODE_GUIDELINES.md:47` 也明文写着 "Don't use exceptions"。于是异常从 `noexcept`
帧里逃出 ⇒ `std::terminate` ⇒ **进程直接死**,连崩溃处理都没机会跑。
判据是**解析发生在认证之前**:`GameServer::OnConsume`(`Code/server/GameServer.cpp:592`)
先 `factory.Extract(reader)` 再 `:600 m_messageHandlers[...]`,而
`GetByConnectionId` 与 `Kick` 都在 handler 内部 —— 所以**未认证的连接就能触发**。

同一族还有第二副面孔,而且**合法流量**就能触发:计数写在线上,但写侧只写了
计数的低位、循环却写了全部条目,读侧于是只消费 `count` 条,剩下的字节被当成
下一条消息解析 —— **整条流从此失步**。最典型的是 `Mods`:

```cpp
// Code/encoding/Structs/Mods.cpp(修复前)
const uint16_t modCount = std::min(ModList.size(), size_t(4096)) & 0xFFFF;
aWriter.WriteBits(modCount, 13);
for (auto& entry : ModList)   // ← 写的是全部条目,不是 modCount 条
```

`Tints` 更直接:8 位计数**没有 clamp**,超过 255 个条目即静默截断失步。
第三副面孔是**复用对象上的累积**:`insert()` / `push_back()` 都不清空,
消息对象复用一次就把上一包的内容再应用一遍。

**判据是仓库自己的既有约定**——同族缺陷里早就有两处正确写法:

| 位置 | 写法 | 上界来自 |
| --- | --- | --- |
| `NotifyFactionsChanges.cpp:27-28` | `if (count >= kMaxChangeCount) return;` | 早退,不抛异常 |
| `PluginMessagingRequest.cpp:40-42` | `std::min<uint32_t>(size, kMaxPayloadBytes)` | 注释:"Never trust the wire length: a peer can claim more bytes than it sent." |
| `Factions` / `Mods` / `Tints` / … | 无上界,或计数与循环不一致 | ❌ **本次缺陷** |

判据可机械执行:**「`WriteBits(x.size() & MASK, N)` 后面跟 `for (auto& e : x)`」
= 计数与实体数必然不一致**;以及**「`ReadVarInt` 出来的计数直接进
`resize`/`assign`/循环」= 无上界**。

**修复**:38 个 `Code/encoding/**` 文件,统一成两句话 ——
**读侧读完计数立刻 `std::min` 夹紧(或早退),绝不抛异常;写侧写几条就只循环几条。**

```cpp
// Code/encoding/Structs/Factions.cpp(修复后)
const auto npcCount = std::min<uint64_t>(Serialization::ReadVarInt(aReader), kMaxFactionCount);
NpcFactions.resize(npcCount);
```

```cpp
// Code/encoding/Structs/Mods.cpp(修复后)
for (size_t i = 0; i < modCount; ++i)   // ← 与 13 位计数一致
```

新增的上界常量全部有出处,不是随手取的数:`kMaxActorValueCount = 164` 就是游戏的
`kActorValueCount`;`kMaxFactionCount = 0x1FF`、`kMaxObjectCount = 0xFF`、
`kMaxTintCount = 0xFF`、`kMaxEntryCount = 0xFFFF` 都是**线上计数本身的位宽**;
`kMaxEntryCount = 0x1000` / `kMaxEffectCount = 0x40` / `kMaxVariableCount = 0x1000`
是 VarInt 计数的宽上界,给真实数据留足余量(玩家背包、附魔效果、行为图变量
都在几百以内)。

**同时修掉三处同族缺陷**:`AnimationVariables::String_to_VectorBool`
(`Code/encoding/Structs/AnimationVariables.cpp:33-42`)的位表外层循环**不检查
`chars` 边界**,字符串偏短就**读越界**;`Inventory::Entry::Deserialize` 与
`Inventory::Deserialize` 的 `push_back` 不清空;`MagicEquipment::Deserialize`
在三个「这个手有没有法术」的 flag 全为假时**保留上一包的法术**,等于把已经
收起来的法术又装回去。

**验证**:全 `Code/encoding` 再查一遍 `WriteBits(...size()...)` 与
`WriteVarInt(aWriter, ...size())`,**零命中**;34 个 `ReadVarInt` 读点逐个核对,
计数语义的全部有上界,非计数的(Id / Tick / DiscordId / MessageType)本就不需要;
`git diff --shortstat` = `39 files changed, 396 insertions(+), 120 deletions(-)`;
`python Tools/Scripts/check_docs.py` 全绿。

### 修复:断开连接时,「还没有角色的玩家」会销毁别人的角色

**现象**:玩家在**还没建好角色**的时候断开连接(连上后被踢、角色创建失败、或角色已被
删除后掉线),服务端会把**另一个玩家**的角色实体销毁掉 —— 那个玩家会看到自己的角色
凭空消失,或客户端开始报找不到实体。

**根因**:`GameServer::OnDisconnection` 用 `0` 冒充「没有角色」:

```cpp
// Code/server/GameServer.cpp(修复前)
entt::entity playerCharacter = pPlayer->GetCharacter().value_or(static_cast<entt::entity>(0));
...
if (entity == playerCharacter)      // ← 没有角色时,这里等于 entity == 0
```

`entt::entity` 的 `0` **不是空值**。EnTT 的空值是「entity 位全 1」
(`skypjack/entt v3.10.0` 的 `src/entt/entity/entity.hpp`:`entity_mask = 0xFFFFF`、
`reserved = entity_mask | (version_mask << entity_shift)`),而 `0` 恰恰是
`registry::create()` 交出的**第一个实体** —— 正常情况下就是第一个玩家的角色。
于是「没有角色」的玩家断开时 `entity == 0` 命中该实体,走的是
`CharacterRemoveEvent` 而不是 `OwnershipTransferEvent`,**别人的角色被直接销毁**。

**判据是仓库自己的既有约定**:同一个函数里「有角色 / 没角色」分得很清楚,
只有这一处把 `optional` 拍平成了整数:

| 位置 | 写法 | 判定 |
| --- | --- | --- |
| `Code/server/Services/OverlayService.cpp:75` | `if (const auto character = acMessage.pPlayer->GetCharacter())` | ✅ |
| `Code/server/Services/PlayerService.cpp:157` | `if (!pPlayer->GetCharacter()) return;` | ✅ |
| `Code/server/Services/CommandService.cpp:100` | `auto character = pTargetPlayer->GetCharacter(); if (character)` | ✅ |
| `Code/server/GameServer.cpp:636` | `GetCharacter().value_or(static_cast<entt::entity>(0))` | ❌ 本次缺陷 |

可机械执行的判据:**`optional<entt::entity>` 绝不能 `value_or(0)`** ——
EnTT 里没有「0 号空实体」这个概念。全仓 `value_or(` 只有这一处。

**修复**:保留 `optional`,在比较处判空:

```cpp
// Code/server/GameServer.cpp(修复后)
const auto playerCharacter = pPlayer->GetCharacter();
...
if (playerCharacter && entity == *playerCharacter)
```

**修崩溃,不改行为**:有角色的玩家断开时走的路径一条没变。

**验证**:`Select-String 'value_or\('` 全仓**零命中**;
`static_cast<entt::entity>(0)` / `entt::entity{0}` / `entt::null` 同样**零命中**;
`git diff --shortstat -- Code` = `2 files changed, 6 insertions(+), 4 deletions(-)`;
`python Tools/Scripts/check_docs.py` 全绿。

### 修复:游戏读盘时解引用空 cell —— 一整个「`GetParentCellEx()` 允许返回空」的家族

**现象**:玩家在读盘(或换 cell)的窗口里点「连接」,握手正好在这个窗口完成,
客户端进程直接终止。同一个窗口里只要有 `MoveTo` 走到,也一样崩。

**根因**:`TESObjectREFR::MoveTo`(`Code/client/Games/Skyrim/TESObjectREFR.cpp:385-400`)
把 cell 直接读穿:

```cpp
// Code/client/Games/Skyrim/TESObjectREFR.cpp(修复前)
void TESObjectREFR::MoveTo(TESObjectCELL* apCell, const NiPoint3& acPosition) const noexcept
{
    ...
    TiltedPhoques::ThisCall(s_internalMoveTo, this, GetNullHandle(), apCell, apCell->worldspace, ...);
}
```

`apCell->worldspace` 没有判空,所以传进来的 cell 为空就是空指针解引用。
这是**单点缺陷、九个调用点共享** —— 修 `MoveTo` 一处,九个调用点全部受益。

同一族还有四处直接写 `parentCell->formID`,而 `parentCell` 在同一个窗口里同样是空的:
`TransportService.cpp:307`(`OnConnected`,玩家点连接时)、`ObjectService.cpp:127`(`OnCellChange`)、
`CharacterService.cpp:1359`(`RequestServerAssignment`,同一表达式解引用两次)、
以及两个调试视图的「Teleport to me」按钮。

**判据是仓库自己的既有约定**:`TESObjectREFR.h:160-162` 的注释就写着
"The actual parent cell is the field below at offset 0x60; use `GetParentCellEx()` when the
effective cell is needed.",而 `GetParentCellEx()`(`TESObjectREFR.cpp:500-503`)是

```cpp
return parentCell ? parentCell : GetSaveParentCell();
```

—— **回退的虚函数本身可以返回空**。`PlayerCharacter.cpp:111` 用它之后仍在 `:117` 判空,
`docs/PITFALLS.md` 的 §22(`:1559-1591`)也已经写下这条法条:
"`PlayerCharacter::Get() != nullptr` **不蕴含** `GetParentCellEx() != nullptr`"。

| 位置 | 写法 | 判定 |
| --- | --- | --- |
| `CharacterService.cpp:1285-1291` | 取 cell → 判空 → 打日志 `return` | ✅ |
| `CommandService.cpp:58-77` / `OverlayService.cpp:520-539` | 取 cell → 判空 → 提示 `return` | ✅ |
| `CharacterService.cpp:1739` | `if (!pCell \|\| !pCell->IsAttached())` | ✅ |
| `AnimationSystem.cpp:118` | `if (const auto pCell = pActor->parentCell)` | ✅ |
| `TESObjectREFR::MoveTo` | `apCell->worldspace`,无判空 | ❌ **本次缺陷** |
| `TransportService` / `ObjectService` / `CharacterService` | `parentCell->formID`,无判空 | ❌ 同族 |

可机械执行的判据:**`parentCell->` 直接解引用 = 缺陷**;取 cell 必须走
`GetParentCellEx()` 且结果判空。

**修复**:6 个文件。`MoveTo` 开头加单点守卫:

```cpp
// Code/client/Games/Skyrim/TESObjectREFR.cpp(修复后)
    // The cell is read through below, and GetParentCellEx() is allowed to come
    // back empty while the game is between cells.
    if (!apCell)
        return;
```

四处 `parentCell->formID` 改走 `GetParentCellEx()` 并判空(空时保持默认 `CellId`,
服务端把它当普通 cell 值,由首次 cell-change 自愈);三个 `MoveTo` 调用点改传
`GetParentCellEx()`;两个调试视图用 `if (pCell)` 而不是提前 `return` ——
后者会把当前帧剩下的界面元素整段跳过。

**修崩溃,不改行为**:`parentCell` 非空时 `GetParentCellEx()` 就是 `parentCell` 本身,
正常路径一个字节都没变;为空时原本是崩溃。

**验证**:`Get-ChildItem -Path Code\client -Recurse -Include *.cpp,*.h | Select-String 'parentCell->|parentCell\.'`
剩余 11 处**全部已有守卫**;`git diff --stat -- Code` = `6 files changed, 31 insertions(+), 9 deletions(-)`;
`python Tools/Scripts/check_docs.py` 全绿。

### 修复:主菜单与读盘期间解引用「还没有玩家」 —— 一整个 `PlayerCharacter::Get()` 允许返回空的家族

**现象**:游戏刚启动停在主菜单,或者正在读一个存档,此时点「连接」;又或者只是等一个
断线包、一条经验同步包、一次传送响应到达 —— 客户端进程直接终止。

**根因**:`PlayerCharacter::Get()` 在读盘与主菜单期间返回空,而 18 处调用点直接写
`PlayerCharacter::Get()->...`。判据不是本次新立的,是仓库自己早就写在两处的:

```cpp
// Code/client/Services/Generic/OverlayService.cpp:143-149
    // Its per-frame caller passes PlayerCharacter::Get(), which is null while a
    // save is loading and on the main menu. The two other callers look the actor
    // up from the world and already know it is there.
    if (!apActor)
        return 0.f;
```

```cpp
// Code/client/Services/Generic/PlayerService.cpp:215-220
    // Runs every frame from OnUpdate, including while a save is loading and on
    // the main menu, where there is no player yet. This file already checks the
    // same pointer on another path; these two did not.
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return;
```

`Code/client/Games/Skyrim/PlayerCharacter.cpp:44-49` 的实现同样说明它可空:

```cpp
PlayerCharacter* PlayerCharacter::Get() noexcept
{
    POINTER_SKYRIMSE(PlayerCharacter*, s_character, 401069);
    return *s_character.Get();
}
```

| 位置 | 写法 | 判空 |
| --- | --- | --- |
| `OverlayService.cpp:615` | `CalculateHealthPercentage(PlayerCharacter::Get())` | ✅(函数内判) |
| `PlayerService.cpp:218` | `PlayerCharacter* pPlayer = PlayerCharacter::Get(); if (!pPlayer) return;` | ✅ |
| `PlayerService.cpp:309`/`:326`/`:359` | `if (auto* pPlayer = PlayerCharacter::Get())` | ✅ |
| `PlayerService.cpp:86` | `PlayerCharacter::Get()->SetDifficulty(m_previousDifficulty);` | 否(本次缺陷) |
| `CharacterService.cpp:602` | `pActor->MoveTo(PlayerCharacter::Get()->GetParentCellEx(), ...);` | 否(本次缺陷) |
| `TransportService.cpp:275` | `PlayerCharacter* pPlayer = PlayerCharacter::Get();` 后 `:280` 就解引用 | 否(本次缺陷) |
| `Forms.cpp:55` | `Cast<TESNPC>(PlayerCharacter::Get()->baseForm)` | 否(本次缺陷) |
| `References.cpp:60` | `s_getDifficultyMultiplier(PlayerCharacter::Get()->difficulty, ...)` | 否(本次缺陷) |
| `TESObjectREFR.cpp:919` | `PlayerCharacter* pPlayer = PlayerCharacter::Get();` 后 `pPlayer->IsItemInInventory(...)` | 否(本次缺陷) |

**可机械执行的判据**:`PlayerCharacter::Get()->` 出现一次,就是一处缺陷 ——
表达式里根本没有放判空的位置。判空必须落在一个局部量上,写成
`if (PlayerCharacter* pPlayer = PlayerCharacter::Get())`。

**修复**:9 个文件、18 处,一律「取一次到局部量 + 判空」,不改调用顺序、不改原有语义:

```cpp
// Code/client/Games/References.cpp
    PlayerCharacter* pPlayer = PlayerCharacter::Get();

    bool isPlayer = apHittee == pPlayer;

    // The player is null on the main menu and while a save is loading, where no
    // damage is real; the read below would fault on it.
    if (!pPlayer)
        return aDamage;

    float multiplier = s_getDifficultyMultiplier(pPlayer->difficulty, ActorValueInfo::kHealth, isPlayer);
```

```cpp
// Code/client/Services/Generic/TransportService.cpp(OnConnected)
    // The player is null on the main menu and while a save is loading, where the
    // handshake can still complete; everything below degrades to a default.
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    ...
    auto* pNpc = pPlayer ? Cast<TESNPC>(pPlayer->baseForm) : nullptr;
    ...
    if (pPlayer)
    {
        if (const auto* pWorldSpace = pPlayer->GetWorldSpace())
            modSystem.GetServerModId(pWorldSpace->formID, request.WorldSpaceId);
        if (const auto* pParentCell = pPlayer->GetParentCellEx())
            modSystem.GetServerModId(pParentCell->formID, request.CellId);
        request.Level = pPlayer->GetLevel();
    }
```

其余七处同构:`Forms.cpp`(`TESNPC::Initialize` 的六个默认值改到 `if (pPlayerBaseForm)` 内)、
`TESObjectREFR.cpp`(`pPlayer && !pPlayer->IsItemInInventory(...)`)、
`PlayerService.cpp`(五处)、`CharacterService.cpp`(八处)、`MapService.cpp`(两处)、
`CommandService.cpp`/`OverlayService.cpp`(传送各一处)。

**修崩溃,不改行为**:指针非空时走的还是原来那条路;为空时原本是崩溃。传送的两处
仍然先取 cell 并保留原来的失败提示(`"Teleporting to player failed."`),只是不再对着
空玩家解引用。

**验证**:`Get-ChildItem -Path Code\client -Recurse -Include *.cpp,*.h | Select-String 'PlayerCharacter::Get\(\)->'`
→ **零命中**(修前 18 处);`git diff --stat -- Code` = `9 files changed, 98 insertions(+), 34 deletions(-)`;
`python Tools/Scripts/check_docs.py` 全绿。

### 修复:字符串缓存超过 65535 条时,后加入的玩家会永久卡在「收到一半的缓存」

**现象**:服务器开得久了(或装了大量会往字符串缓存里塞名字的 mod),新加入的玩家
**部分文字显示不出来** —— 字幕、对话、动画事件名是空的,而老玩家一切正常。重连没用,
换一台机器也没用;只有服务器重启才会好一阵子。

**根因**:字符串缓存是**增量同步**的,每个玩家记一个「我已经收到第几条」的游标
(`Player::GetStringCacheId()`)。发送侧按游标把剩下的条目切成一条消息发出去。
一个 `StringCacheUpdate` 消息的计数是 **16 位**(`Code/encoding/Messages/StringCacheUpdate.cpp:26`
读回来是 `& 0xFFFF`),所以**一条消息最多带 65535 条**。而发送侧原本是:

```cpp
// Code/encoding/StringCache.cpp(修复前)
    update.Values.insert(std::begin(update.Values), itor, std::end(m_idToString));

    aStartId = m_idToString.size();   // ← 不管写出去几条,游标都推到末尾
```

写出去的条目被 16 位计数截断,而**游标仍然推到了缓存末尾**。接收侧只收到前 65535 条,
它的本地缓存长度就停在那里;下一次增量同步时,服务端按**已经推到末尾的游标**认为
「这个人已经收全了」,于是**一条都不再发** —— 两边的 id 从此永久错位,而且**再也不会自愈**。

**判据**:仓库自己早就写下了「计数写在线上、写几条就只循环几条」这条规则
(v1.1.9 前面几节刚统一过一整族),`StringCacheUpdate.cpp` 的 16 位掩码正是这个家族
的另一副面孔 —— **只是这次错的是「游标」而不是「循环」**:写出去的条数与游标推进的
条数必须相等,否则接收侧永远追不上。

**修复**:发送侧按 16 位上界切一片出去,并且**只把游标推进真正写出去的那几条**;
剩下的留给下一次增量同步。接收侧的 `assert(aMessage.StartId == m_idToString.size())`
也因此重新成立。

```cpp
// Code/encoding/StringCache.cpp(修复后)
        const auto count = std::min<size_t>(m_idToString.size() - aStartId, StringCacheUpdate::kMaxValueCount);

        auto itor = std::begin(m_idToString);
        std::advance(itor, aStartId);

        auto end = itor;
        std::advance(end, count);

        update.Values.insert(std::begin(update.Values), itor, end);

        aStartId += static_cast<uint32_t>(count);
```

上界常量 `StringCacheUpdate::kMaxValueCount = 0xFFFF` 放在消息头里
(`Code/encoding/Messages/StringCacheUpdate.h`),因为**它本来就是线上计数的位宽**,
不是随手取的数。

**同一处还有第二层截断,修完游标才会露出来**:`StringCacheUpdate::StartId` 字段本身声明成
`uint16_t`,而游标是 `uint32_t`(`StringCache::Serialize(uint32_t& aStartId)`)——
`update.StartId = aStartId;` 是一次**隐式窄化**。缓存超过 65535 条之后,发出去的 `StartId`
已经不是真实游标了,接收侧那条连续性判据**永远不可能成立**:debug 构建下每收一条就炸,
release 构建下断言被编译掉,表现为 id 静默错位。只修游标不修字段,等于把「永久卡住」
换成「每一条都错位」。现把字段放宽成 `uint32_t`,读侧掩码同步改成 `& 0xFFFFFFFF`:

```cpp
// Code/encoding/Messages/StringCacheUpdate.h
-    uint16_t StartId{0};
+    uint32_t StartId{0};

// Code/encoding/Messages/StringCacheUpdate.cpp
-    StartId = Serialization::ReadVarInt(aReader) & 0xFFFF;
+    StartId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
```

VarInt 对小于 65536 的值**编码字节完全相同**,所以线上格式向后兼容:新旧版本混连时,
老版本发来的 `StartId` 语义不变。

**验证**:`Code/encoding/StringCache.cpp` 里不再有 `aStartId = m_idToString.size();`
这种「无条件推到末尾」的写法;`python Tools/Scripts/check_docs.py` 全绿。

### 修复:远端角色的动画回放队列没有上界,动画图没就绪时会一直涨

`RemoteAnimationComponent::TimePoints` 是远端角色的**动作回放队列**:服务端以 50 Hz 把范围内
每个非 owner 角色的动作整批发下来,客户端逐条塞进这个队列,再由 `AnimationSystem::Update`
每帧弹出**到期的那一条**去播放。入口有两处,都不看队列有多长:

```cpp
// Code/client/Services/Generic/CharacterService.cpp:689
animationComponent.TimePoints.push_back(action);
// Code/client/Systems/AnimationSystem.cpp:104
aAnimationComponent.TimePoints.insert(aAnimationComponent.TimePoints.end(), acReplay.Actions.begin(), acReplay.Actions.end());
```

出口却只有「队首到期就 pop 一条」这一条,而且被两道门挡着:actor 解析不出来时调用点
直接跳过(`if (!pActor) continue;`),`Actor` 有了但 `animationGraphHolder.IsReady()` 还是假时
函数提前返回(注释原文「Animation graph not ready, keep the action in queue and try again later」)。
两道门都是**正常状态** —— 远端角色刚 spawn、模型还没加载完,或者玩家刚切完 cell ——
但队列在这期间只进不出:动作越积越多,而**没有任何一处会裁剪它**。

判据在**同族的另一个队列**上:`Code/client/Systems/InterpolationSystem.cpp:19` 的插值窗口有
`constexpr size_t kMaxSamples = 8;`,注释写得很清楚 ——「Keep the window bounded. Newest samples
win; dropping from the front is what makes this safe against a play head that has stopped
advancing.」动画回放队列面对的是同一个问题(play head 停了),却没有这个上界。

修法是给队列加硬上界,并把裁剪放在**所有提前返回之前**:

```cpp
// Code/client/Systems/AnimationSystem.cpp
constexpr size_t kMaxQueuedActions = 256;

    auto& actions = aAnimationComponent.TimePoints;

    while (actions.size() > kMaxQueuedActions)
        actions.pop_front();

    // An unresolvable actor has nothing to replay, but its queue still has to be
    // trimmed above, or it grows with every snapshot that mentions it.
    if (!apActor)
        return;
```

调用点一并改掉:以前 `if (!pActor) continue;` 会让「actor 解析不出来」这种情况连裁剪的机会
都没有,现在无论 actor 能不能解析都调用 `AnimationSystem::Update`,把裁剪做掉。

**验证**:`Code/client/Services/Generic/CharacterService.cpp` 里 `AnimationSystem::Update` 的调用
不再被 `if (!pActor)` 跳过;`kMaxQueuedActions` 只在 `AnimationSystem.cpp` 定义一处;
`python Tools/Scripts/check_docs.py` 全绿。

### 修复:队伍里的三处空指针 —— 「查询队伍的接口自己会造出一个空队伍」

服务端有三个和「玩家队伍」有关的解引用,根因都是**把「组件里存着一个 id」当成「队伍一定存在」**。

**第一处**:`PartyService::GetPlayerParty` 是所有人查队伍的唯一入口,它用的是 `operator[]`:

```cpp
// Code/server/Services/PartyService.cpp(修复前)
    if (inviterPartyComponent.JoinedPartyId)
        return &m_parties[*inviterPartyComponent.JoinedPartyId];
```

`m_parties` 是 `TiltedPhoques::Map<uint32_t, Party>`,`operator[]` 在 key 不存在时会**默认构造一个
`Party` 并插进去**。于是一个「id 已经失效」的玩家调用这个函数,不会拿到 `nullptr`,而是拿到一个
**成员为空、`LeaderPlayerId` 为 0 的假队伍**,同时往 `m_parties` 里塞了一条永远清不掉的垃圾。
同文件的 `GetById`(`:36-43`)**用的是 `find` 并返回 `nullptr`**,调用方 `WeatherService.cpp:23`/`:39`
和 `CharacterService.cpp:782` 也都写了 `if (!pParty) return;` —— 只有这一处不同,判据就是同文件内的
两种写法不一致。

**第二处**:`QuestService::OnQuestChanges` 拿到返回值后**直接解引用,全程没有判空**:

```cpp
// Code/server/Services/QuestService.cpp(修复前,:40-42)
    PartyService::Party* pParty = partyService.GetPlayerParty(pPlayer);
    Player* pLeader = isLeader ? pPlayer : m_world.GetPlayerManager().GetById(pParty->LeaderPlayerId);
    auto& dedupHistory = pParty->GetQuestStageDedupHistory();
```

客户端只在**进了队伍**时才发这个包(`Code/client/Services/Generic/QuestService.cpp` 的三处发送点
`:72`/`:108`/`:158` 都带 `!m_world.Get().GetPartyService().IsInParty()` 门禁),但**已经在途的请求**
遇上玩家退队/被踢就会以「没有队伍」到达,而服务端对此不做校验;`GameServer::Kick` 只是标记断开,
真正的 `OnDisconnection` 要等下一轮事件循环,这个窗口是真实存在的。判据同样在**同一个文件里**:
`OnQuestSceneChanges`(`:210-211`)写了早退保护 `if (!partyComponent.JoinedPartyId.has_value()) return;`。

修法是只把**依赖队伍的那部分**跳过去,`pLeader`/`SceneMaster` 改用三目表达式,`dedupHistory` 的引用
从函数头部**下移到 `if` 块内**:

```cpp
    PartyService::Party* pParty = partyService.GetPlayerParty(pPlayer);
    Player* pLeader = pParty == nullptr ? nullptr : (isLeader ? pPlayer : m_world.GetPlayerManager().GetById(pParty->LeaderPlayerId));
    ...
    notify.SceneMaster = pParty == nullptr ? 0 : pParty->GetQuestStageDedupHistory().GetSceneMaster();
    ...
    if (inParty && pParty != nullptr)
    {
        auto& dedupHistory = pParty->GetQuestStageDedupHistory();
```

**任务日志的记账(quest log)留在 `if` 之外**:`Player::GetQuestLogComponent()` 全仓只有两个消费者
(`CharacterService.cpp:658` 的赋值和这里),单人/未组队玩家的事件同样要被记录,不能因为「不在队伍」
整体跳过。`SceneMaster` 写 0 而不是跳过,也与 `QuestStageDedupHistory::m_sceneMaster{0}` 的既有注释
「PlayerIDs start at 1, so zero is "not set."」一致。

**第三处**:`GameServer::SendToLeader`(`:755`)从来没判过 `apLeader`:

```cpp
    if (const_cast<Player*>(apLeader)->GetParty().JoinedPartyId != acPartyComponent.JoinedPartyId)
```

它全仓**只有一个调用点** —— 就是上面 `QuestService.cpp:194` 传进来的 `pLeader`,而那个值来自
`GetPlayerManager().GetById(pParty->LeaderPlayerId)`,`GetById` 查不到就返回 `nullptr`(队伍会先于玩家行
存活到 `PlayerLeaveEvent` 跑完)。现按同函数的既有风格加一条早退:

```cpp
    if (!apLeader)
    {
        spdlog::warn("Party leader no longer exists, canceling broadcast.");
        return;
    }
```

**验证**:`Code/server/Services/PartyService.cpp` 的 `GetPlayerParty` 里不再出现 `m_parties[`;
`QuestService.cpp` 的 `dedupHistory` 引用只在 `if (inParty && pParty != nullptr)` 块内使用;
`GameServer.cpp` 的 `SendToLeader` 在使用 `apLeader` 之前先判空;`python Tools/Scripts/check_docs.py` 全绿。

### 修复:客户端给远程角色生成脸部时,「没有材质的蒙版着色器」与「纹理工厂返回空」都会被直接写穿

**现象**:部分角色(尤其是外观从网络同步过来的角色)进场时客户端崩溃,崩溃点落在
`skee64.dll` 之外的**我们自己的运行时**里,读 `BSMaskedShaderMaterial` 时 fault。

**根因**:`FaceGenSystem::Update`(`Code/client/Systems/FaceGenSystem.cpp`)在蒙版着色器分支里
连取两个**引擎给的指针**却都不判空:

```cpp
// Code/client/Systems/FaceGenSystem.cpp(修复前,:86-90)
BSMaskedShaderMaterial* pMaterial = static_cast<BSMaskedShaderMaterial*>(pLightingShader->material);

BSFixedString name("");
auto pTexture = CreateTexture(name);
pTexture->buffer = CreateResourceView(s_textureHolder.Get(), 512, 512);
```

`BSLightingShaderProperty::material` 是裸 `void*`(`Code/client/Games/Skyrim/NetImmerse/BSShaderProperty.h:11`),
蒙版着色器**可以没有材质**;`CreateTexture` 的签名是
`using TCreateTexture = NiRenderedTexture*(__fastcall)(BSFixedString& aName);`(`:56`),
**以返回空表示失败**。两者都在几行之后被写穿 —— `:126` 写 `pMaterial->renderedTexture`、
`:90` 写 `pTexture->buffer` —— 而本函数在 `noexcept` 帧里,空指针就是直接 fault。

**判据在同一个函数里**:`GetHeadTriBasedGeom`(`:22-51`)把这条链上的**每一个**引擎返回值都判了空
(`pFaceNode && pActorBase`、`pFacePart`、`pHeadNode`、`pGeometry`),只有进入蒙版分支之后这两处没有。

**修法**:两处各自判空并**先 `pShaderProperty->DecRef()` 再 return**(着色器在上面刚 `IncRef` 过),
把这一帧跳过去,下一帧自然重试:

```cpp
        if (!pMaterial)
        {
            pShaderProperty->DecRef();
            return;
        }
        ...
        if (!pTexture)
        {
            pShaderProperty->DecRef();
            return;
        }
```

### 修复:服务端把「还没送出去的动作队列」当成已发送,直接清空

**现象**:某个玩家不在场时打出的动作(挥砍、施法),等他走进范围后**永远看不到** —— 不是延迟,是彻底没有。

**根因**:`Code/server/Services/CharacterService.cpp` 的 `ProcessMovementChanges` 在实体循环**结束之后**
对**全部**实体无条件清空动作队列:

```cpp
// Code/server/Services/CharacterService.cpp(修复前,:1013-1017)
    m_world.view<AnimationComponent>().each([](AnimationComponent& animationComponent)
    {
        // Remove actions we've sent
        animationComponent.Actions.clear();
    });
```

而「真的送出去了」与「走过这个循环」是两件事,两条路径都只走循环不发送:

- 实体因为 `if (movementComponent.Sent == true) continue;`(`:990`)提前跳过 —— **没有新移动、但队列里有新动作**;
- 有移动,但内层玩家循环里**没有任何玩家在范围内**(`:1000` 全部 `continue`)。

`animationComponent.Actions` 全仓只有两个读者(这里的读出与清空),写入只有 `:413` 一处,所以清空即**永久丢失**。

**判据在同一个文件里**:同族的 factions 早就改成了「送出才清」——
`bool queued = false;` → 真正塞进消息时 `queued = true;` → `if (queued) characterComponent.SetDirtyFactions(false);`
(`:924-945`,注释原文 "Only clear the flag once the change actually went out. An actor that nobody is near
right now stays dirty, so the next player to come into range still gets it instead of keeping whatever
factions it spawned with."),只有动作队列这一处还停在旧写法。

**修法**:同一个 `queued` 模式 + 与回放缓存同宽的上界(`kReplayCacheMaxSize = 32`):

```cpp
        bool actionsQueued = false;
        ...
            update.ActionEvents = animationComponent.Actions;
            actionsQueued = true;
        ...
        if (actionsQueued)
        {
            animationComponent.Actions.clear();
        }
        else if (animationComponent.Actions.size() > kMaxPendingActionEvents)
        {
            animationComponent.Actions.erase(animationComponent.Actions.begin(), animationComponent.Actions.end() - kMaxPendingActionEvents);
        }
```

新增 `constexpr uint32_t kMaxPendingActionEvents = 32;`(`:48`)。
**为什么还要加上界**:队列现在**允许跨 tick 保留**,没人靠近的实体就会一直攒;上界与
`Code/server/Game/Animation/ActionReplayCache.h:19` 的回放缓存一致(32),超出丢最旧的。

**验证**:`Code/client/Systems/FaceGenSystem.cpp` 的蒙版分支里 `pMaterial` 与 `pTexture` 都在使用前判空
且两条早退都先 `DecRef`;`Code/server/Services/CharacterService.cpp` 里 `Actions.clear()` 只出现在
`if (actionsQueued)` 块内、`kMaxPendingActionEvents` 在越界时生效;`python Tools/Scripts/check_docs.py` 全绿。

### 修复:OStim 的远程 addon 缓存从不清理,读档后会把上一个存档的状态套到同 id 的角色上

**现象**:读档(或开新档)之后进 OStim 场景时,**远程**角色的 OCum 覆盖贴图与 OStim 物体状态
会短暂按**上一个存档**的样子渲染 —— 不是不同步,是本地缓存里还留着旧存档的条目。

**根因**:`plugins/OStimTogether/src/AddonBridge.cpp` 的两个缓存 `_remoteOverlays` / `_remoteObjects`
以 `RE::FormID` 为一级键,而 FormID 在新存档里会被**重新分配**。这两个容器全仓只有写入
(`:531`、`:617`)与读取(`:686-691`),**没有任何 `erase`/`clear` 路径**。

**判据在同一个文件外**:`plugins/OStimTogether/src/main.cpp:212-223` 的
`case SKSE::MessagingInterface::kPreLoadGame:` 逐条清理了 9 个单例
(`EquipmentLock::ClearAllOStimTargets()`、`ClearManual()`、`DefaultOutfitGuard::RestoreAll()`、
`CoopSessionManager::Reset()`、`OStimBridge::ResetRemoteState()`、`FreeScenePhaseSync::Reset()`、
`ParticipantAlignmentSync::Reset()`、`FreeSceneSelfOriginLock::Reset()`、`OCumStateSync::Reset()`、
`PPAIntegration::Reset()`),**唯独 AddonBridge 缺席**。同构先例:`OStimBridge::ResetRemoteState()`
(`:1623-1649`)一次清掉 15 个成员 map,`MorphSyncService::Reset()` 同样清三个 map。

**修法**:给 `AddonBridge` 补一个 `ClearRemoteState()`(持 `_stateMutex` 清两个 map),
并挂到 `kPreLoadGame` 那一串清理里:

```cpp
// plugins/OStimTogether/src/AddonBridge.cpp
void AddonBridge::ClearRemoteState()
{
    std::scoped_lock lock(_stateMutex);
    _remoteOverlays.clear();
    _remoteObjects.clear();
}
```

**交付方式**:该修复落在**本项目不拥有、也无法推送**的子模块里,所以按既有做法做成
`Code/plugins/patches/OStimTogether/0003-clear-the-remote-addon-cache-on-load.patch`,
由 `plugin_patches.py apply` 在配置与构建前打到子模块工作树上 —— CI 的
`git submodule update --init --force --recursive --depth=1` 会丢弃任何只存在于工作树的改动。

**验证**:`python Code/plugins/tools/plugin_patches.py verify` → `PATCH VERIFY OK`(9 个补丁全部
能打到各自的 pin 上);`check` → `PATCH CHECK OK`;`python Tools/Scripts/check_docs.py` 全绿。

### 修复:上一轮空指针清扫的判据漏掉了「先取值、后解引用」这一整种形状

**现象**:在主菜单、或正在读档时,客户端收到一条远程**慢速法术**(Slow Time 系)的
`NotifyAddTarget`,进程可能直接崩;另一处是玩家死后回到主菜单/读档,1.5 秒后那次「补倒地」
会解引用一个已经不存在的玩家。

**根因**:两处都是**先把 `PlayerCharacter::Get()` 存进局部变量,之后才解引用**,
而它主菜单与读档期间为空:

```cpp
// Code/client/Services/Generic/MagicService.cpp(修前)
if (pEffect->IsSlowEffect())
    pActor = PlayerCharacter::Get();          // :456 覆盖掉上面刚判过空的 pActor

pActor->magicTarget.AddTarget(data, ...);     // :458 直接解引用
```

同一个函数 `:433` 的 `acMessage.CasterId && (pCaster = PlayerCharacter::Get());` 还会把
`pCaster` 写成空,而这个值经 `:440 data.pCaster = pCaster;` 交给引擎
(`MagicTarget::AddTarget` → `RealAddTarget`)。`PlayerService.cpp` 那处同理:

```cpp
// Code/client/Services/Generic/PlayerService.cpp(修前)
PlayerCharacter* pPlayer = PlayerCharacter::Get();          // :295
PlayerCharacter::SetGodMode(true);
m_godmodeStart = true;
pPlayer->currentProcess->KnockExplosion(pPlayer, &pPlayer->position, 0.f);   // :300
```

**判据**:`PlayerCharacter::Get()` 是 `POINTER_SKYRIMSE(PlayerCharacter*, s_character, 401069)`,
主菜单与读档期间为空;而两条路径**都不需要玩家存在就能走到** —— `OnNotifyAddTarget` 由收包触发
(`m_notifyAddTargetConnection`),`RunPostDeathUpdates` 每帧从 `OnUpdate` 调用,而
`m_knockdownStart` 是**1.5 秒前**记下的,这段时间里存档可能已经被离开。

**为什么上一轮没发现**:上一次清扫的验收条件是
「`PlayerCharacter::Get()->` across Code/client: no matches, was 18」—— 那是一条**形状**判据
(在同一个表达式里解引用)。本轮把同一个问题重问成「**取值**之后才解引用的站点」,
`grep '= PlayerCharacter::Get();'` 在 `Code/**/*.cpp` 有 **31 处**,全在这条判据之外。

**修法**:两处都改成「拿到非空才覆盖」,原值保留:

```cpp
if (acMessage.CasterId)
{
    if (auto* pPlayer = PlayerCharacter::Get())
        pCaster = pPlayer;
}
```

```cpp
if (pEffect->IsSlowEffect())
{
    if (auto* pPlayer = PlayerCharacter::Get())
        pActor = pPlayer;
}
```

```cpp
PlayerCharacter* pPlayer = PlayerCharacter::Get();
if (pPlayer)
{
    PlayerCharacter::SetGodMode(true);
    m_godmodeStart = true;
    pPlayer->currentProcess->KnockExplosion(pPlayer, &pPlayer->position, 0.f);
    FadeOutGame(false, true, 0.5f, true, 2.f);
    m_godmodeDeadline = std::chrono::steady_clock::now() + 10s;
}
m_knockdownStart = false;
```

**为什么不直接 `return`**:慢速法术那个 hack 只是「把效果挪到玩家身上」,玩家不在时留在已经解析
并判过空的目标身上即可,没必要丢掉整条远程法术;`m_knockdownStart = false;` 特意留在 `if` 之外,
是为了让「玩家不在」的这一次也能把倒地状态收尾,不至于每帧重试。

**验证**:`grep 'PlayerCharacter::Get()->' Code` 仍为 0 命中;`Code/client/Services/Generic/MagicService.cpp`
`:437`/`:466` 两处都是 `if (auto* pPlayer = PlayerCharacter::Get())` 守卫、`:470` 的
`pActor->magicTarget.AddTarget(...)` 之前 `pActor` 已被 `:413-419` 判过空;
`Code/client/Services/Generic/PlayerService.cpp:300` 是 `if (pPlayer)` 守卫;
`python Tools/Scripts/check_docs.py` 全绿。

### 修复:专用服务器一个手滑的命令行参数就能让它连端口都没监听就退出

**现象**:给专用服务器带上 `--uPort=abc`(或任何写错的数值参数),进程**在打印任何一行日志之前就退出**,
不监听端口、不写崩溃转储;把参数去掉一切正常。

**根因**:启动期设置解析有**两处**会抛异常,而整条路径上没有一处 `catch`:

```cpp
// Code/components/console/CommandSettingsProvider.cpp(修复前)
std::string value = argv[i];
value = value.substr(value.find_first_not_of('-'));   // 空串或全由 '-' 组成时 find_first_not_of 返回 npos
...
case SettingBase::Type::kInt: setting->data.as_int32 = std::stol(args[pName]); break;
case SettingBase::Type::kUInt: setting->data.as_uint32 = std::stoul(args[pName]); break;
case SettingBase::Type::kInt64: setting->data.as_int64 = std::stoll(args[pName]); break;
case SettingBase::Type::kUInt64: setting->data.as_uint64 = std::stoull(args[pName]); break;
case SettingBase::Type::kFloat: setting->data.as_float = std::stof(args[pName]); break;
```

`std::string::substr(npos)` 抛 `std::out_of_range`;`std::sto*` 对解析不了的输入抛
`std::invalid_argument`(`abc`、空串)或 `std::out_of_range`(超出该类型范围)。

**判据 1 —— 这条路径上没有第二个机会**:调用链是
`Code/server_runner/main.cpp:233` → `DediRunner::DediRunner`(`Code/server_runner/DediRunner.cpp:34`,
`CreateGameServer` 的回调)→ `CreateGameServer`(`Code/server/main.cpp:100 aCallback();`)
→ `DediRunner::LoadSettings`(`:53 LoadSettingsFromCommand`)。异常要逃出的是 **`main()` 的调用栈**,
而 `grep "catch" Code` 全仓只有 **8 处**,没有一处在这条链上(`Code/server_runner/**` 与
`Code/server/main.cpp` 各 0 处)。这不是「异常会被上层处理」,是「异常直接结束进程」。

**判据 2 —— 写同样这五个字段的另外两条路径早就不抛**:`set` 控制台命令
(`Code/components/console/ConsoleRegistry.cpp:141-145`)用的是
`pSetting->data.as_int32 = ConvertStringValue(value.c_str(), pSetting->data.as_int32);`;
`.ini` 载入(`Code/components/console/IniSettingsProvider.cpp:122-126`)五个数值类型全走
`GetIniValue` → `ConvertStringValue<TVal>(szValue, a_nDefault)`。
`Code/components/console/ConsoleUtils.h:20-26` 的注释原文就是 "on error, you get the default"。
⇒ 三条路径里,**只有命令行这一条会抛**,而它也是唯一一条**出错就没有第二次机会**的
(另外两条出错只是该值保持默认,服务器照常起来)。

**修法**:跳过没有非横线字符的参数,五个数值改走 `ConvertStringValue`。

```cpp
// Code/components/console/CommandSettingsProvider.cpp(修复后,:20-27)
// substr(npos) throws, and an argument made of nothing but dashes (or an empty one)
// leaves find_first_not_of at npos. Nothing above this call catches, so the whole
// argument is skipped instead of taking the server down over it.
const auto cFirstFlagChar = value.find_first_not_of('-');
if (cFirstFlagChar == std::string::npos)
    continue;

value = value.substr(cFirstFlagChar);
```

```cpp
// Code/components/console/CommandSettingsProvider.cpp(修复后,:55-59)
case SettingBase::Type::kInt: setting->data.as_int32 = ConvertStringValue(args[pName].c_str(), setting->data.as_int32); break;
case SettingBase::Type::kUInt: setting->data.as_uint32 = ConvertStringValue(args[pName].c_str(), setting->data.as_uint32); break;
case SettingBase::Type::kInt64: setting->data.as_int64 = ConvertStringValue(args[pName].c_str(), setting->data.as_int64); break;
case SettingBase::Type::kUInt64: setting->data.as_uint64 = ConvertStringValue(args[pName].c_str(), setting->data.as_uint64); break;
case SettingBase::Type::kFloat: setting->data.as_float = ConvertStringValue(args[pName].c_str(), setting->data.as_float); break;
```

**为什么不加 `try`/`catch`**:`CODE_GUIDELINES.md:47` 明文 "Don't use exceptions, don't use STL code
that can throw, use the nothrow version if available or the unsafe version.";而且这里要的语义是
「这个参数不认识,就跳过它」,不是「这个参数不认识,就整体失败」—— 一条写错的参数不该让同一行里
`--uMaxPlayerCount=32` 这种**正确的参数**也一起失效。`ConvertStringValue` 失败时保留
`setting->data` 里的原值(即该设置项的默认值),与 `.ini` 载入、`set` 命令的行为完全一致。

**验证**:`Select-String -Path Code\components\console\CommandSettingsProvider.cpp -Pattern 'std::sto'`
→ **0 命中**;`python Tools/Scripts/check_docs.py` 全绿。

### 修复:查询队伍的接口自己会造出一个空队伍,而踢人接口不看对方在不在自己队里

**现象**:服务端的队伍系统有**两个**独立的故障,都由同一批客户端可控的 id 触发:

1. **跨队踢人**:任何一个队伍的队长,可以用 `PartyKickRequest` 把**别的队伍**里的玩家踢出去(只要
   猜得到对方的 `PlayerId`)。被踢的玩家莫名其妙地离开队伍。
2. **队伍凭空出现**:`GetPlayerParty()` 这类「查询」接口用的是 `m_parties[id]`。`JoinedPartyId`
   还在、但队伍已经被销毁时,这个下标会**静默插入**一个全新的空队伍 —— 于是每个调用者都会把
   这个空队伍当成真队伍继续用,而它的 `LeaderPlayerId` 是**未初始化的**。

**根因**:`Code/server/Services/PartyService.cpp` 里同时存在两种写法 —— `GetById()`(`:36-43`)
一直用 `find` 并判 `end`,而 `GetPlayerParty()`、`OnPartyChangeLeader`、`OnPartyKick`、
`OnPlayerJoin`、`OnPartyInvite`、`OnPartyAcceptInvite`、`RemovePlayerFromParty` 七处用的是
`m_parties[...]`:

```cpp
// Code/server/Services/PartyService.cpp(修复前)
Party& party = m_parties[id];                  // 队伍不存在时会插入一个空队伍
auto& members = party.Members;
members.erase(std::find(std::begin(members), std::end(members), apPlayer));  // 找不到时 erase(end())
```

```cpp
// Code/server/Services/PartyService.h:89-96(修复前)
struct Party
{
    uint32_t LeaderPlayerId;   // 没有默认初始化
    ...
};
```

两条判据:

- `std::find` 找不到时返回 `end()`,而 `vector::erase(end())` 是**未定义行为**(空 vector 上会把
  size 减成 `SIZE_MAX`)。跨队踢人时 `apPlayer` 本来就不在 `members` 里,所以这条路径**必然**走到
  `erase(end())` —— 而它恰好是 `if (members.empty())` 成立的那条分支。
- `operator[]` 插入的新 `Party` 里 `LeaderPlayerId` 是未定义值,紧接着的
  `if (party.LeaderPlayerId == player->GetId())` 就是**读未初始化内存**。

`OnPartyKick` 原本只有两道门:`if (!pKick)`(目标存在)和 `if (party.LeaderPlayerId ==
player->GetId())`(请求者是队长)。**没有一道门问过「pKick 在不在请求者的队伍里」**,而
`PartyMemberPlayerId` 是从线里直接读出来的:

```cpp
// Code/encoding/Messages/PartyKickRequest.cpp:11
PartyMemberPlayerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
```

**修法**:七处 `m_parties[...]` 全部改成 `find`/`GetById` + 判空(只有 `OnPartyCreate` 里 `:117`
那一处保留下标 —— 它的 key 刚由 `m_nextId++` 生成,必然不存在);`RemovePlayerFromParty` 在 `erase`
前先判 `std::find` 的结果;`OnPartyKick` 增加成员归属校验:

```cpp
// Code/server/Services/PartyService.cpp(修复后,OnPartyKick)
const auto* pParty = GetById(*inviterPartyComponent.JoinedPartyId);
if (pParty != nullptr && pParty->LeaderPlayerId == player->GetId())
{
    const auto& kickPartyComponent = pKick->GetParty();
    if (!kickPartyComponent.JoinedPartyId || *kickPartyComponent.JoinedPartyId != *inviterPartyComponent.JoinedPartyId)
    {
        spdlog::error("[PartyService]: Player {} is not in the party of {}", message.PartyMemberPlayerId, player->GetId());
        return;
    }
    ...
}
```

```cpp
// Code/server/Services/PartyService.cpp(修复后,RemovePlayerFromParty)
const auto partyItor = m_parties.find(id);
if (partyItor != std::end(m_parties))
{
    Party& party = partyItor->second;
    auto& members = party.Members;
    const auto memberItor = std::find(std::begin(members), std::end(members), apPlayer);
    if (memberItor != std::end(members))
    {
        members.erase(memberItor);
        ...
```

顺带修掉 `Code/server/Services/QuestService.cpp:223` 的 `pParty` 未判空即解引用 —— 那是
`GetPlayerParty()` 改成返回 `nullptr` 之后**唯一**没跟上的调用点(同文件 `:46-47` 早就用了
`pParty == nullptr ? ... : ...` 的三目写法,`Code/server/Services/WeatherService.cpp:23`/`:39`
两处也都判了空)。

**验证**:`Select-String -Path Code -Include *.cpp,*.h,*.hpp,*.inl -Pattern 'erase\(std::find'`
→ **0 命中**;`Select-String -Path Code\server\Services\PartyService.cpp -Pattern 'm_parties\['`
→ 只剩 `OnPartyCreate` 的 1 处;`python Tools/Scripts/check_docs.py` 全绿。

### 修复:OStimNG 的动画事件注解只判了「2 个字段」,其余全部落进越界分支

**现象**:OStimNG 场景里,一条字段数不是 2 的 `OStimEvent` 注解会让插件读越界;
任何一段数字写错的注解会让 `std::stoi` 抛异常,而异常从 `RE::BSTEventSink`
的事件槽逃出去 ⇒ `std::terminate`。

**根因**:`plugins/OStimNG/skse/src/Core/Thread.cpp:849-856`(修复前)只判了
`payloadVec.size() == 2`,**其余全部落进 `else`**:

```cpp
// plugins/OStimNG/skse/src/Core/Thread.cpp(修复前)
std::vector<std::string> payloadVec = stl::string_split(a_event->payload.c_str(), ',');
if (payloadVec.size() == 2) {
    int actorIndex = GetActor(actor)->index;
    callEvent(payloadVec[0], actorIndex, std::stoi(payloadVec[1]), actorIndex);
} else {
    callEvent(payloadVec[0], std::stoi(payloadVec[1]), std::stoi(payloadVec[2]), std::stoi(payloadVec[3]));
}
```

注解语法有两个合法形状,写在 `data/SKSE/Plugins/OStim/list of annotations.txt:14-19`:
`OStimEvent.[event],[target]` 与 `OStimEvent.[event],[actor],[target],[performer]`。
代码把第一个判成正例,**把「不是它」当成了「就是第二个」**。而
`stl::string_split`(`skse/src/Util.cpp:47-62`)会丢掉尾部的空 token,于是
`""` → size 0、`"a,"` → size 1、`"a,b,c"` → size 3 —— 三种都不是 2,三种都落进
`else`,前两种读 `payloadVec[1..3]` 越界、第三种读 `[3]` 越界。

**payload 由谁决定**:注解文本写在动画图的 behaviour 数据里
(`data/Nemesis_Engine/mod/ostim/0_master/#0106.txt:1222`),但同一条 tag 也能
**从网络重放** —— `plugins/AnimSyncTogether/src/STRPMClient.cpp:436-437` 把收到的
字符串直接 `actor->NotifyAnimationGraph(inputEvent)`,唯一过滤是
`:394` 的 `SyncRules::...ShouldSyncEvent(tag)`。所以**远端客户端能决定这段 payload**。

**判据在同一个文件里**:`skse/src/Papyrus/PapyrusScenes.h:18-28` 的 `GetActor`
先判 `thread` 再判 `actor`,而紧邻的 `:10` / `:15` 两个入口**一个都不判**;
`skse/src/Papyrus/PapyrusThread.h:279` 对 `actor`/`target`/`performer` 三个下标
做了范围校验,`ProcessEvent` 这条**同构的入口**不做。⇒ 判据是
**「同一个文件里已经有判空的正确写法」**,不是「看起来像不像会空」。

同一族在这次一并修掉的还有:`GetActor(actor)->` 在 `ProcessEvent` 里 **10 处**直接
解引用(而 `skse/src/Core/Thread.cpp:458-463` 明确 `return nullptr;`);
`getCurrentNodeInternal()`(`skse/src/Core/Thread.h:125 Graph::Node* m_currentNode = nullptr;`)
在 `NodeHandler::handlePeakAnnotation` 与 `nodeChanged` 里两处直接解引用;
`NodeHandler::threadEnd()` 的 `delete this` 之后 `Thread::nodeHandler` 仍是**悬垂指针**
(而 `getNodeHandler()` 是 public、`nodeHandler` 是 private,外部无法置空);
`LoopPeakHandler::speedChanged()` 三重解引用加一个不判范围的下标;
`PeakSoundPlayer` 构造函数三层解引用;`RemoveActor()` 在空 `m_actors` 上算
`size() - 1`。

**修法**:新增一个不抛异常的注解数字解析器,并把每个入口改成先判空再取值:

```cpp
// plugins/OStimNG/skse/src/Core/Thread.cpp(修复后)
namespace {
    bool parseAnnotationInt(const std::string& a_text, int& a_out, int a_base = 10) {
        const char* begin = a_text.data();
        const char* end = begin + a_text.size();
        ...
        const auto result = std::from_chars(begin, end, value, a_base);
        if (result.ec != std::errc{}) {
            return false;
        }
        ...
    }
}
```

**交付方式**:该修复落在**本项目不拥有、也无法推送**的子模块里,所以按既有做法做成
`Code/plugins/patches/OStimNG/0002-guard-the-animation-event-path.patch`,
由 `plugin_patches.py apply` 在配置与构建前打到子模块工作树上 —— CI 的
`git submodule update --init --force --recursive --depth=1` 会丢弃任何只存在于工作树的改动。

**验证**:`python Code/plugins/tools/plugin_patches.py verify` → `PATCH VERIFY OK`(10 个补丁
全部能打到各自的 pin 上);`check` → `PATCH CHECK OK`;
`python Code/plugins/tools/plugin_snapshot.py verify` → `SNAPSHOT OK`;
`python Tools/Scripts/check_docs.py` 全绿。

### 修复:任意客户端一个畸形认证包即可让服务端空转数十亿次,并顺手改写全服日历

**现象**:专用服务器上,一个客户端只要在认证包里把日历字段写成
`Month = 0xFFFFFFFF`,服务端在**接受他进服的那一瞬间**就会卡在
`GetTimeInDays()` 的月份循环里空转约 42.9 亿次;写成
`Day = 0xFFFFFFFF` 则会卡在 `Update()` 的进位 `while` 里约 1.4 亿次。
两种都会让整个 tick 线程停住,所有玩家一起掉线。

**根因**:这五个字段是从线上原样读进来的,读侧一个范围校验都没有。

```cpp
// Code/encoding/Structs/TimeModel.cpp(修复前)
Day = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
Month = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
Year = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
```

`& 0xFFFFFFFF` 对 `uint32_t` 是恒等操作,等于什么都没做。而 `Month` 直接
成了 `Code/common/DateTime.cpp:38-46` 的循环上界:

```cpp
// Code/common/DateTime.cpp
float DateTime::GetTimeInDays() const noexcept
{
    float totalDays = static_cast<float>(m_timeModel.Year) * 365.f;
    for (uint32_t i = 0u; i < m_timeModel.Month; i++)
        totalDays += GetNumberOfDaysByMonthIndex(i);
    ...
}
```

`Day` 则成了 `DateTime::Update` 里 `while (m_timeModel.Day > GetNumberOfDaysByMonthIndex(m_timeModel.Month))`
的减法次数。同一族的还有 `Time` 与 `TimeScale`:两者都没有 NaN/Inf 检查,
却被 `m_timeModel.Day += static_cast<uint32_t>(m_timeModel.Time / 24.f);` 拿去
做「浮点转无符号整数」——NaN 与负值在这里都是未定义行为。

**可达性**:认证包在认证**之前**就被解析(`Code/server/GameServer.cpp:572 OnConsume`
→ `:592 factory.Extract(reader)`),`BindMessageHandlers` 里那道「拿不到 player 就踢」
的判空(`:262`)被 `AuthenticationRequest` 的专属分支(`:281-286`)绕开了。
服务端没设密码时(`:33-34` 默认空串、`:183 m_isPasswordProtected`),
`:905` 的 `"" == ""` 判定为真,所以任何一个 `Version` 对得上、
服务端没满、SKSE/MO2 标志合格的客户端都能走到 `:1053` 的
`PlayerJoinEvent(pPlayer, ..., acRequest->PlayerTime)`。

**顺带的数据污染**:`Code/server/Services/CalendarService.cpp:40` 拿客户端数据
算 `playerHasFurthestTime`,一旦为真就把客户端的 `Day/Month/Year` **无条件**
抄进服务端的 `m_dateTime`(`:50-52`),再广播给所有人(`:54-58`)⇒ 一个客户端
可以单方面改写全服日历。夹住范围同时也把这条路上的越界值挡掉了。

**修法**:按 `Code/encoding/Structs/Factions.h:18-20` 的既有做法,
在结构体上声明边界、在读侧夹住,而不是抛异常 —— `Deserialize` 处在 `noexcept` 链上:

```cpp
// Code/encoding/Structs/TimeModel.h(修复后)
static constexpr float kMaxTimeScale = 1000.f;
static constexpr float kMaxTime = 24.f;
static constexpr uint32_t kMaxDay = 31u;
static constexpr uint32_t kMaxMonth = 11u;
static constexpr uint32_t kMaxYear = 999u;
```

边界取自服务端**自己**已有的判断:`CalendarService::SetTimeScale` 的
`aScale <= 1000.f`、`SetDate` 的 `aMonth < 12` 与 `aYear <= 999`、
`std::fmod(Time, 24.f)`。非有限值一律归零,因为 NaN/Inf 夹不进任何区间:

```cpp
// Code/encoding/Structs/TimeModel.cpp(修复后)
float ClampFloat(float aValue, float aMax) noexcept
{
    if (!std::isfinite(aValue))
        return 0.f;

    return std::min(std::max(0.f, aValue), aMax);
}
```

`Serialize` 未改:夹在**读**侧,所以客户端发出去的是原值,而
`ServerTimeSettings` 的广播在两端也各过一次 `Deserialize`,同样被夹住。

**验证**:`Code/tests/encoding.cpp:549 TEST_CASE("TimeModel wire bounds", "[encoding.time_model]")`
用 `Serialization::WriteFloat/WriteVarInt` 手工造畸形包,覆盖 NaN、`1.0e30f`、
三个 `0xFFFFFFFF` 与三个负值,并回环一次正常值;
CI 的 `.github/workflows/windows.yml:594-638` 会跑 `TPTests.exe` 并要求
`All tests passed`。`python Tools/Scripts/check_docs.py` 全绿。

### 修复:血量增量既没有范围校验、服务端账本的符号还和客户端相反

**现象**:一个客户端发一条 `RequestHealthChangeBroadcast`,把 `DeltaHealth` 写成
`-1e38` 或 `-inf`,范围内**所有**客户端都会把那个 actor 的血量算成非正数,
然后 `pActor->Kill()` —— 非玩家 actor 一律被远程处决;写成 `NaN` 则让
`ForceActorValue` 收到 NaN。

**根因**:这个字段从线上原样读进来,读侧没有任何值域检查,而它在接收端是
「加到血量上的增量」,加完还要判死。

```cpp
// Code/encoding/Messages/RequestHealthChangeBroadcast.cpp(修复前)
Id = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
DeltaHealth = Serialization::ReadFloat(aReader);
```

接收端 `Code/client/Services/Generic/ActorValueService.cpp:295-305`:

```cpp
const float newHealth = pActor->GetActorValue(ActorValueInfo::kHealth) + acMessage.DeltaHealth;
pActor->ForceActorValue(ActorValueOwner::ForceMode::DAMAGE, ActorValueInfo::kHealth, newHealth);
const float health = pActor->GetActorValue(ActorValueInfo::kHealth);
if (!pActor->IsDead() && health <= 0.f)
{
    ActorExtension* pExtension = pActor->GetExtension();
    // Players should never be killed
    if (!pExtension->IsPlayer())
        pActor->Kill();
}
```

同一处还有第二个问题:**服务端账本的符号是反的**。服务端在
`Code/server/Services/ActorValueService.cpp:90` 用的是
`ActorValuesList[24] = currentHealth - message.DeltaHealth`,而客户端用的是 `+`,
两端的约定本来就该一致 —— 客户端的发送侧
(`Code/client/Games/Skyrim/Actor.cpp:1093`/`:1142`/`:1169`)发的是「受伤为负、
治疗与回血为正」,所以服务端的减号会把每一次受伤都记成加血。这份账本不是死数据:
它会经 `InitialActorValues`/`AllActorValues` 分发给新加入的玩家
(`Code/server/Services/CharacterService.cpp:109`/`:688`/`:883`),再由客户端
`Actor::SetActorValues` 逐键 `ForceActorValue` 写回引擎。

**可达性**:`RequestHealthChangeBroadcast` 在服务端 handler 表里
(`Code/server/Services/ActorValueService.cpp:19`),不需要任何所有权即可触发 ——
同文件另三个 handler(`:31`/`:58`/`:110`)都写了 `IsCurrentOwner`,只有这一个没有。

**修法**:按 `TimeModel` 的同一范式,在**结构体上**声明上界、在**读侧**夹住,
并且两个方向都夹(客户端发来的 `RequestHealthChangeBroadcast` 与服务端广播的
`NotifyHealthChangeBroadcast` 各自过一次 `Deserialize`):

```cpp
// Code/encoding/Messages/NotifyHealthChangeBroadcast.cpp
if (!std::isfinite(DeltaHealth))
    DeltaHealth = 0.f;
else
    DeltaHealth = std::min(std::max(DeltaHealth, -kMaxHealthDelta), kMaxHealthDelta);
```

`kMaxHealthDelta = 100000.f` 声明在两个结构体里(`RequestHealthChangeBroadcast.h`
与 `NotifyHealthChangeBroadcast.h`),注释写明它为什么存在。**不加所有权门**:
客户端对「打远程玩家」本来就不发广播
(`Code/client/Games/Skyrim/Actor.cpp:1096-1099` 直接 `return wouldKill;`),而 NPC 的
所有权归 spawn 方,贸然加门会挡掉合法的 NPC 受击广播 —— 那属于玩法决策,
不在「稳定性/兼容/同步」的审计范围内。服务端账本的减号同时改成加号,与客户端一致。

**验证**:`Code/tests/encoding.cpp:609 TEST_CASE("Health change broadcast wire bounds", "[encoding.health_change]")`
手工造包覆盖 NaN、`-1.0e30f`、`1.0e30f`,并回环一次 `-12.5f`;
CI 的 `.github/workflows/windows.yml:594-638` 会跑 `TPTests.exe` 并要求
### 修复:actor value 的**键**从线上原样读入,而它是引擎表的下标

**现象**:一个客户端发一条 `RequestActorValueChanges`,把 `Values` 的键写成
`0xFFFFFFFF`,范围内的客户端会拿这个键去 `SetActorValue`/`ForceActorValue`,
也就是直接写进引擎的 actor value 表下标;值是 `NaN`/`inf` 时,污染的是那个
actor value 之后**每一次**读取(血量、速度、抗性都由同一张表供给)。

**根因**:这条消息只夹了**条目数**,没夹**键的取值范围**,也没夹值的有限性。
结构体与四个消息的读侧都是同一形状:

```cpp
// Code/encoding/Structs/ActorValues.cpp(修复前)
const uint32_t key = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
auto value = Serialization::ReadFloat(aReader);
ActorValuesList.insert({key, value});
```

接收端 `Code/client/Services/Generic/ActorValueService.cpp:346` 与 `:376` 只按
键**挑分支**,没有任何范围校验:

```cpp
// Code/client/Services/Generic/ActorValueService.cpp:341-346
if (key == ActorValueInfo::kStamina || key == ActorValueInfo::kMagicka || key == ActorValueInfo::kHealth)
{
    pActor->ForceActorValue(ActorValueOwner::ForceMode::DAMAGE, key, value);
    continue;
}
pActor->SetActorValue(key, value);
```

`Actor::SetActorValue`(`Code/client/Games/Skyrim/Actor.cpp:732-735`)把它转发给
`actorValueOwner.SetValue(aId, aValue)`,而那张表的合法下标只有
`Code/client/Games/Skyrim/Forms/ActorValueInfo.h:174` 的 `kActorValueCount = 164`
个。同一个 `ActorValues` 结构体还随角色数据一起走
(`AssignCharacterResponse.AllActorValues`、`CharacterSpawnRequest.InitialActorValues`),
经服务端账本(`Code/server/Services/CharacterService.cpp:109`/`:688`/`:883`)
分发给新加入的玩家,再由 `Code/client/Games/Skyrim/Actor.cpp:836-843`
逐键写回引擎 —— 一份被污染的账本会把污染复制给每一个后进服的玩家。

**可达性**:`RequestActorValueChanges`/`RequestActorMaxValueChanges` 在服务端
handler 表里(`Code/server/Services/ActorValueService.cpp:17-18`),两者都有
`IsCurrentOwner` 门(`:31`/`:58`) —— 也就是说,能触发它的是**那个 actor 的
当前所有者**,不是任意客户端。但「所有者」这个身份本身是可以在游戏里转移的
(读档、换角色、`OwnershipEpoch` 递增),而客户端自己的发送侧
(`Code/client/Services/Generic/ActorValueService.cpp:131`)只发 `0..163` 的键,
所以越界键**只可能来自被改过的客户端或半途失步的流**。

**修法**:与 `TimeModel`/血量增量同一范式 —— 上界声明在结构体上、夹在**读**侧、
四个消息与嵌套结构体共用同一个判据:

```cpp
// Code/encoding/Structs/ActorValues.cpp
bool ActorValues::IsValidActorValue(uint32_t aId, float aValue) noexcept
{
    return aId <= kMaxActorValueId && std::isfinite(aValue);
}
```

```cpp
// Code/encoding/Messages/RequestActorValueChanges.cpp:39-47(修复后)
const uint32_t key = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
const auto value = Serialization::ReadFloat(aReader);

// Both fields are read above so that an entry which is dropped here still
// consumes its bytes and the rest of the stream stays aligned.
if (!ActorValues::IsValidActorValue(key, value))
    continue;

Values.insert({key, value});
```

`Code/encoding/Structs/ActorValues.h:20` 新增
`static constexpr uint32_t kMaxActorValueId = static_cast<uint32_t>(kMaxActorValueCount - 1);`,
与既有的条目数上限 `kMaxActorValueCount` 并列 —— 前者管「能收几条」,后者管
「下标能到哪」,两者此前被当成了同一个东西。**一处刻意的取舍**:被丢弃的条目
仍然要**消费掉自己的字节**(键与值都先读出来再判),`continue` 而不是 `break`,
否则它后面所有字段都会从错误的偏移读起 —— 那是把「一个坏键」换成「整包错位」。
`Serialize` 侧未改,与 `TimeModel` 的修复同一策略:夹在读侧。

**验证**:`Code/tests/encoding.cpp:670 TEST_CASE("Actor value wire bounds", "[encoding.actor_values]")`
手工造包覆盖越界键 `0xFFFFFFFF`、`0x12345678`、`NaN` 值,并在包尾放一个
`0xDEADBEEF` 哨兵 —— 读完之后它必须原样读得到,以此证明被丢弃的条目确实
消费了自己的字节;另有一段回环一次正常的两键集合。
CI 的 `.github/workflows/windows.yml:594-638` 会跑 `TPTests.exe` 并要求
`All tests passed`。`python Tools/Scripts/check_docs.py` 全绿。

### 修复:四处的「判空」其实是日志,宏从不中止 —— release 里它们就是空指针解引用

**现象**:客户端有三条路径在引擎可能给出空指针的地方继续解引用:抛射物发射后取回
`Projectile*`(`Code/client/Games/Skyrim/Projectiles/Projectile.cpp`)、从附魔额外数据读
`EnchantmentItem*` 及其 `EffectItem` 的 `pEffectSetting`(`Code/client/Games/Skyrim/TESObjectREFR.cpp`
的 `GetItemFromExtraData`)、把附魔写回额外数据列表(`GetExtraDataFromItem` 的 `SetEnchantmentData`)。
四处都写了 `TP_ASSERT`,看上去像判过了。

**根因**(修复前):
```cpp
// Code/client/Utils.h:11-15
#define TP_ASSERT(Expr, Msg, ...)                                    \
    if (!(Expr))                                                     \
    {                                                                \
        Utils::Assert(#Expr, fmt::format(Msg, __VA_ARGS__).c_str()); \
    }

// Code/client/Utils.h:28-34
static void Assert(const char* apExpression, const char* apMessage)
{
    spdlog::critical("Assertion failed ({}): {}", apExpression, apMessage);
    if (IsDebuggerPresent())
        __debugbreak();
}
```
`TP_ASSERT` 既不 `abort` 也不返回失败,它只写一行日志;`__debugbreak()` 也只在挂了调试器时
执行 —— 文件上方作者自己写着 `// TODO: should this be debug only? I removed the check since
debug is broken, can only use releasedbg`。所以 `TP_ASSERT(pProjectile, ...)` 之后的
`pProjectile->fPower` 没有任何保护。

**可达性**:四条指针来源都允许为空 —— `Code/client/Games/Skyrim/Projectiles/Projectile.cpp:99`
的 `ThisCall(RealLaunch, ...)` 把结果写进出参;`Code/client/Games/Skyrim/TESObjectREFR.cpp:126-140`
的 `TESObjectREFR::GetByHandle` 显式允许 `pResult` 保持 `nullptr`;`Code/client/Games/Skyrim/RTTI.h:17-27`
的 `Cast<>` 在 RTTI 未映射的旧版游戏上**恒**返回 `nullptr`(注释逐字:
`// guard against unmapped rtti ids (legacy game versions): the game's dynamic cast dereferences`);
`Code/client/Games/Skyrim/Forms/EnchantmentItem.cpp:39` 的 `EnchantmentItem* pItem = nullptr;` 在创建被
引擎拒绝时原样返回。附魔那两条路径**线上可达**:`Inventory::Entry.ExtraEnchantId` 随
`NotifyInventoryChanges` / `NotifyObjectInventoryChanges` 过线(`Code/encoding/Structs/Inventory.cpp:64`),
`GetExtraDataFromItem` 里 `arEntry.ExtraEnchantId.ModId == 0xFFFFFFFF` 走 `EnchantmentItem::Create`,
否则走 `Cast<EnchantmentItem>(TESForm::GetById(...))` —— 两条分支都能给出空指针,随后直接进引擎的
`Code/client/Games/Skyrim/ExtraData/ExtraDataList.cpp:130-135 SetEnchantmentData`。

**修法**:把三处「日志」换成真分支。`Projectile.cpp` 在拿到空的 `result` 时 `return nullptr`;
拿到空的 `pProjectile` 时保留事件本身、只让 `Event.Power` 留在 `arData.fPower` 的初值上 ——
**少发一个抛射物比漏发一个更糟**,所以 `World::Get().GetRunner().Trigger(Event)` 没有被跳过。
`GetItemFromExtraData` 把 `pEnchantment` 的整段(`GetServerModId`、`formID & 0xFF000000`、
`listOfEffects` 遍历)移进 `if (pEnchantment)`,并把 `if (!pEffectItem) continue;` 收紧成
`if (!pEffectItem || !pEffectItem->pEffectSetting) continue;`(`pEffectSetting` 在
`EnchantmentItem::Create` 里同样只被日志、不被跳过);`GetExtraDataFromItem` 把
`SetEnchantmentData` 包进 `if (pEnchantment)`。`arEntry.ExtraEnchantCharge` /
`ExtraEnchantRemoveUnequip` 仍在 `if` 之外赋值 —— 它们来自 `pExtraEnchantment` 本身,
与 `pEnchantment` 是否为空无关。

**验证**:这四处是引擎指针判空,`Code/tests` 只覆盖 `Code/encoding`,且 `Projectile::Launch` /
`GetItemFromExtraData` / `GetExtraDataFromItem` 都依赖引擎与 `World::Get()`,
**没有可写的单元测试** —— 以静态复核为准:重扫 `Code/client` 全部 9 个 `TP_ASSERT` 站点,
每一个后面现在都紧跟真 `if`(`Projectile.cpp:20`/`:30`/`:106`/`:116`、
`TESObjectREFR.cpp:536`/`:571`,以及 `:521`/`:629` 的注释与其后的 `:528 if (pEnchantment)` /
`:634 if (pEnchantment)`)。`python Tools/Scripts/check_docs.py` 全绿。

### 修复:线上字符串完全没有长度上限 —— 一个字段就能让接收方分配任意大的内存

**现象**:线上的字符串是「长度 + 这么多字节」,长度由发送方随便写,接收方读完也不知道整包
到底多大。于是 `Username`、`ChatMessage`、`Text`、`SoundFilename`、`AppearanceBuffer`、
`ModList[i].Filename`、缓存字符串的长度全部由对端决定,而这些值还会继续往下走:进 CEF 渲染、
进 `TESNPC::Deserialize`、进 `SubtitleManager::ShowSubtitle`、进服务端整个会话都持有的
per-player map。`Code/encoding` 里 30 处 `Serialization::ReadString` 只有 4 处带上限
(全部是 `NotifyPluginMessaging` / `PluginMessagingRequest` 的插件消息通道)。

**根因**(修复前):
```cpp
// Code/encoding/Messages/AuthenticationRequest.cpp:28(修复前)
Username = Serialization::ReadString(aReader);

// Code/encoding/Messages/NotifyChatMessageBroadcast.cpp:15-16(修复前)
PlayerName = Serialization::ReadString(aReader);
ChatMessage = Serialization::ReadString(aReader);

// Code/encoding/Structs/Mods.cpp:32-46(修复前)
const size_t modCount = data & 0xFFFF;
ModList.resize(modCount);
for (size_t i = 0; i < modCount; ++i)
{
    aReader.ReadBits(data, 16);
    ModList[i].Id = data & 0xFFFF;
    ModList[i].IsLite = Serialization::ReadBool(aReader);
    ModList[i].Filename = Serialization::ReadString(aReader);
}
```
一次 `Mods::Deserialize` 就能产出 65535 个任意长的文件名。

**可达性**:这些全是普通客户端消息。`AuthenticationRequest` 本身**不需要先过任何门**——
它就是那道门,而 `Username` 读进来的那一刻玩家对象还没被创建;`SendChatMessageRequest` /
`PlayerDialogueRequest` / `SubtitleRequest` 只要连上就能发;`Mods` 随 `AuthenticationRequest`
一起过线。客户端侧同样没有上限:用户名取自游戏内 NPC 的全名
(`Code/client/Services/Generic/TransportService.cpp:290` 的 `request.Username = pNpc->fullName.value.AsAscii();`),
聊天内容直接来自 UI 输入框,而输入框**没有 `maxlength`**。

**修法**:新建 `Code/encoding/WireLimits.h`,把每个字段的上限写成具名常量,并提供
`inline void Clamp(TiltedPhoques::String& aValue, const uint32_t aMaxBytes) noexcept`;
22 个文件在读完字符串之后调用它。上限取的是**宽松到没有任何合法发送方会碰到**的值
(`kMaxDisplayNameBytes = 64`、`kMaxChatMessageBytes = 4096 + 512`(2026-10-10 更正:该字段同时是插件层的传输通道,见本节之后的新条目)、`kMaxDialogueTextBytes = 1024`、
`kMaxSoundPathBytes = 260`、`kMaxFileNameBytes = 260`、`kMaxVersionBytes` / `kMaxTokenBytes = 64`、
`kMaxAppearanceBytes = 1 << 15`、`kMaxCachedStringBytes = 512`)。
`kMaxAppearanceBytes` 不是拍出来的:`Code/client/Games/Forms.cpp:20-36` 的 `TESNPC::Serialize`
写进一个 `char buffer[1 << 15]` 的栈缓冲,本来就产不出更长的东西。

**为什么夹取而不是丢包**:为一个字段丢掉整包,会把「显示得不对」升级成「掉线」,而
`String::resize` 向下不会抛。这也决定了这个修法的**边界**:夹取发生在字符串读完之后,它约束的是
「值怎么用」,不是 reader 内部的瞬时分配 —— 真正的整包上限只能加在接收入口
(`GameServer::OnConsume` / `Client::OnConsume`),只有它们知道包的真实大小。这条边界写在
`Code/encoding/WireLimits.h` 的头注释里。

**验证**:`Code/tests/encoding.cpp:761 TEST_CASE("Wire string limits", "[encoding.wire_limits]")`
覆盖四段:`Clamp` 的单元行为(短串不动、恰好等于上限不动、超长被截);`AuthenticationRequest`
的 `Username` 超长时读回恰好 `kMaxDisplayNameBytes`;`SendChatMessageRequest` 的 `ChatMessage`
超长时被截而 `MessageType` 保持;`NotifyChatMessageBroadcast` 的 `PlayerName` 与 `ChatMessage`
各自被截。另有一段正常长度的往返 `REQUIRE(sentRound == receivedRound)`,证明夹取没有改动合法包。
CI 的 `.github/workflows/windows.yml:594-638` 会跑 `TPTests.exe` 并要求 `All tests passed`。
`python Tools/Scripts/check_docs.py` 全绿。

### 修复:服务端在 `noexcept` 的聊天转发里每包重编译一次 `std::regex`

**现象**:`OverlayService::sendPlayerMessage` 和两条脚本绑定各自构造一个
`std::regex escapeHtml{"<[^>]+>\\s+(?=<)|<[^>]+>"}` 来剥掉聊天文本里的 HTML 标签。
构造发生在 `noexcept` 的函数里,而且每个聊天包、每次脚本调用都要重编译一次 —— 位置就在
把消息转发给所有人的那条路上。

**根因**(修复前):
```cpp
// Code/server/Services/OverlayService.cpp:29-37(修复前)
void sendPlayerMessage(const ChatMessageType acType, const String acContent, Player* aSendingPlayer) noexcept
{
    NotifyChatMessageBroadcast notifyMessage{};

    std::regex escapeHtml{"<[^>]+>\\s+(?=<)|<[^>]+>"};
    notifyMessage.MessageType = acType;
    notifyMessage.PlayerName = std::regex_replace(aSendingPlayer->GetUsername(), escapeHtml, "");
    notifyMessage.ChatMessage = std::regex_replace(acContent, escapeHtml, "");
```
`CODE_GUIDELINES.md:47` 写的是「Don't use exceptions, don't use STL code that can throw」,而
`std::regex` 正是一个会抛的 STL 类型:`std::regex` 的构造与 `std::regex_replace` 都要分配,
在 `noexcept` 的函数里分配失败就是 `std::terminate` —— 一条聊天消息换来整个进程结束。
同样的构造还出现在 `Code/server/Scripting/GameServer_Bindings.cpp:20` 与 `:29`。

**可达性**:`Code/server/Services/OverlayService.cpp:80`(`HandleChatMessage` 的末尾)与 `:85`
(`OnPlayerDialogue`)都直接调 `sendPlayerMessage`,两个都是**任何已连接玩家能发的包**;
两条脚本绑定则是服务器自己的 Lua 脚本调用。全仓 `std::regex` 的构造点只有这 3 处。

**修法**:新建 `Code/encoding/ChatSanitize.h`,`StripHtmlTags` 单遍扫描,语义与原正则逐字对齐
(`[^>]+` 要求括号间至少一个字符,所以 `"<>"` 与未闭合的 `<` 都算文本;`<[^>]+>\s+(?=<)` 这一支
会连标签之间的空白一起吃掉)。`\s` 的集合按 ECMAScript 默认文法写死,不走 locale 相关的
`std::isspace`。它放在 `Code/encoding` 而不是两个服务端调用者旁边,是因为那是测试目标链接的
静态库:`Code/tests/xmake.lua` 只 `add_deps("SkyrimEncoding")`。

**验证**:把 `StripHtmlTags` 逐字移植成 JS,与原正则对拍;字母表 `['<','>','a',' ','\t','\n']`,穷举
长度 0–6 的**全部 55998 个字符串**外加 17 个手写探针 ⇒ **0 处不一致**
(`"<b>hello</b>"→"hello"`、`"<b> <i>x</i>"→"x"`、`"<b> keep <i>x</i>"→" keep x"`、
`"a <> b"→"a <> b"`、`"<>"→"<>"`、`"x <b> y"→"x  y"`、`"<b> "→" "` 等全部逐字相同)。
这 11 例连同幂等性一起写进 `Code/tests/encoding.cpp:850 TEST_CASE("Chat sanitize", "[encoding.chat_sanitize]")`。
`git grep -n "regex" -- Code/server` 现在 0 命中。
`python Tools/Scripts/check_docs.py` 全绿。

### 修复:两个面板在构造函数里 `JSON.parse` 一个可能不是 JSON 的 localStorage 值

**现象**:连服务器密码面板与服务器列表面板偶尔整块空白 —— 列表不渲染、收藏不显示,
控制台里是一条 `Unexpected token` / `Unexpected non-whitespace character after JSON`,
而且**清掉 localStorage 就恢复正常**。因为读的是同一个键,两个面板会一起坏。

**根因**(修复前):
```ts
// Code/skyrim_ui/src/app/components/connect-password/connect-password.component.ts:142(修复前)
let savedServerList = JSON.parse(
  this.storeService.get('savedServerList', '[]'),
);

// Code/skyrim_ui/src/app/components/server-list/server-list.component.ts:189(修复前)
const favoriteServerList = JSON.parse(
  this.storeService.get('favoriteServerList', '[]'),
);
```
`localStorage` 的寿命比写它的那个构建长。`savedServerList` 与 `favoriteServerList` 是
**追加式**写进去的(`savedServerList` 在每次连接成功时被重写),所以「两个数组被拼在
一起」或者「写到一半被中断」都会留下一个仍然存在、但已经不是合法 JSON 的值。
而这两处解析发生在**组件构造期**:`JSON.parse` 抛出后构造函数没有完成,面板不是显示
空列表,而是根本不渲染。

**可达性**:两个键都由 `Code/skyrim_ui/src/app/services/store.service.ts` 的 `set` 写入,
读取点只有这两个组件(`connect-password.component.ts:149`/`:170`、
`server-list.component.ts:192`)。触发条件不需要用户做任何异常操作:换一个构建、
或在写入过程中关掉游戏就够。

**修法**:`store.service.ts:34-48` 新增
`public getJsonArray<T>(key: string, valueIfNull: T[]): T[]` —— 读出来先判 `null`,再
`try { JSON.parse } catch { return valueIfNull; }`,并且用 `Array.isArray(parsed)` 拒绝
「能解析但不是数组」的值(对象、字符串、数字同样会让 `.find`/`for...of` 出问题)。
两个组件改为调用它,`SavedServer` 接口就近声明在 `connect-password.component.ts:20-24`。
**为什么不就地包一层 `try/catch`**:两处解析的意图完全相同(「这个键要么是一个数组,
要么当它不存在」),把它写进 `StoreService` 才只有一处需要维护;而且下一个读数组的人
不必再想一遍这件事。

**验证**:两处调用点已改为 `getJsonArray<SavedServer>('savedServerList', [])` 与
`getJsonArray<Server>('favoriteServerList', [])`(`Server` 来自
`Code/skyrim_ui/src/app/models/server.ts`);`git grep -n "JSON.parse"` 现在只剩
`client.service.ts:579` 与 `store.service.ts:42` 两处,且都在 `try/catch` 内。
`python Tools/Scripts/check_docs.py` 全绿。

### 修复:认证失败的 JSON 载荷是手拼的,服务端给的一个引号就让整条错误原因消失

**现象**:认证被拒时玩家看到的不是原因,而是「服务器没有理由地拒绝了连接」。服务器其实**把原因送过来了**
(`wrong_version` / `mods_mismatch` / `client_mods_disallowed` / `wrong_password` / `server_full`),
它写在 `ConnectionErrorEvent::ErrorDetail` 里,由 `OverlayService::OnConnectionError` 交给 CEF,
UI 用 `JSON.parse` 读出来。这条载荷是**手工拼接**的字符串:值直接写在引号之间,一个 `"` 就提前结束字符串,
整个文档从此不再是 JSON,`client.service.ts` 只能回落到 `no_reason`。

**根因**(修复前):
```cpp
// Code/client/Services/Generic/TransportService.cpp:675(修复前)
ErrorInfo += fmt::format("\"expectedVersion\": \"{}\", \"version\": \"{}\"", acMessage.Version, BUILD_COMMIT);

// Code/client/Services/Generic/TransportService.cpp:686(修复前)
ErrorInfo += fmt::format("[\"{}\",\"{}\"]", m.Filename.c_str(), m.Id);
```
`acMessage.Version` 是**服务端**的版本字符串,`m.Filename` 是**服务端**回传的 mod 文件名 ——
两个都是本侧无法控制的文本,而且都是刚过线的东西。同一段还有第二处:
`case AR::kClientModsDisallowed:` 里 `if (acMessage.MO2Active)` 只包住了逗号那一行,
`ErrorInfo += "\"MO2\"";` 在 `if` 之外,**无条件**执行。于是只装了 SKSE 的玩家被告知
「请移除 MO2」—— 一个他根本没装的 mod 管理器。服务端早就把两个真实 flag 回传了
(`Code/server/GameServer.cpp:896-897 serverResponse.SKSEActive = acRequest->SKSEActive;` /
`serverResponse.MO2Active = acRequest->MO2Active;`),拼数组时用的却是错的。

**可达性**:这条路径不需要玩家做任何异常操作,也不需要恶意客户端 —— 服务端与客户端的版本号
本来就不同(`BUILD_COMMIT` 取自 `Code/common/BuildInfo.h.in:4`),而版本不一致正是 `wrong_version`
这条分支存在的理由;mod 文件名由对端在 `AuthenticationRequest` 里上报,允许包含文件系统允许的任何字符。
**两端都在本地不可控**:`acMessage.Version` 已被 `Code/encoding/Messages/AuthenticationResponse.cpp:19`
夹到 64 字节,`m.Filename` 已被 `Code/encoding/Structs/Mods.cpp:46` 夹到 260 字节 —— 长度夹住了,
但**内容**没有。

**修法**:新建 `Code/encoding/JsonEscape.h`(`Escape(const TiltedPhoques::String&)`,按 RFC 8259 section 7
转义引号、反斜杠与全部 < 0x20 的字节,其余原样保留),`TransportService.cpp` 的三处拼接改走它。
**为什么是转义而不是拒包**:这些字符串已经过了 `WireLimits::Clamp` 的长度上限,文件名允许包含文件系统
允许的任何字符,而一个需要转义的名字仍然是**必须展示给玩家**的名字 —— 为它丢掉整个包,会把
「显示得不对」升级成「掉线」。

**验证**:`Code/tests/encoding.cpp:882 TEST_CASE("JSON escaping of the authentication error payload", "[encoding.json_escape]")`
覆盖转义表(空串 / 普通文本 / `"` / `\\` / `\b\f\n\r\t` / 其余控制字符走 `\u00xx` / 非 ASCII 原样)、
一个试图提前闭合文档的版本字符串,以及**全部 256 个字节**逐个过一遍:结果里每个 `"` 前面必须有奇数个反斜杠,
且不能残留任何 < 0x20 的字节。`python Tools/Scripts/check_docs.py` 全绿。
### 修复:三种「对端说了算」的读取各自越过了自己的边界

**现象**:三处独立缺陷,共同点都是**读侧按对端给的数走,而写侧早就知道真正的上界**:

1. 一个手搓的认证包把 mod 计数写成 13 位能表达的最大值 `8191`,服务器/客户端就会为 8191 个
   `Mods::Entry`(每项含一个最长 260 字节的文件名)分配内存,然后在包已耗尽的情况下继续读。
2. 队伍里某个玩家说的台词(字幕)到了对端**只剩文本,丢了话题**:客户端把 `TopicFormId` 发出去,
   服务器转发的 `NotifySubtitle` 里这一项恒为 0。
3. 一个 `Status` 不在枚举内的 `RequestQuestUpdate`(线上是裸的 8 位字段,没有范围校验)会让服务器
   把一个**未初始化的 `notify.Status`** 读进日志、再原样转发给全队。

**根因**(修复前):

```cpp
// Code/encoding/Structs/Mods.cpp:38-39(修复前)
const size_t modCount = data & 0xFFFF;
ModList.resize(modCount);   // 13 位读允许 8191,而写侧 :22 封顶 4096

// Code/server/Services/CharacterService.cpp:577-579(修复前)
NotifySubtitle notify{};
notify.ServerId = message.ServerId;
notify.Text = message.Text;      // TopicFormId 从未被写,线上恒为 0

// Code/server/Services/QuestService.cpp:109-113(修复前)
default:
    spdlog::error("... unknown quest status {} ...", __FUNCTION__, notify.Status, ...);
    break;   // 不 return;notify.Status 在这条路径上从未被赋值
```

第三条之所以是不确定值而不是 0:`NotifyQuestUpdate` 有**用户提供的**默认构造函数
(`NotifyQuestUpdate() : ServerMessage(Opcode) {}`),按 C++ 的规则它不算聚合,`NotifyQuestUpdate notify{}`
**不会零初始化** `Stage`/`Status`/`ClientQuestType` 这三个没有默认成员初始化器的字段。

**可达性**:第一条在认证包解析路径上,即**未认证连接可达**(`GameServer::OnConsume` 先解析后认证);
第二条是每个玩家说台词时的正常路径,只是信息被丢掉,不崩;第三条需要一个伪造包 —— 客户端自己的
三个发送点只会写 0..3(其中 `Code/client/Services/Generic/QuestService.cpp:188` 写的是布尔值,
只能得到 0 或 1),所以它只对「对端不守规矩」成立,但那正是这条链要防的。

**修法**:`Mods` 新增 `static constexpr uint64_t kMaxModCount = 4096`(`Code/encoding/Structs/Mods.h`,
放在成员之后,与 `Factions.h`/`ActorValues.h` 同一风格),写侧改用它,读侧 `std::min<uint64_t>(data & 0xFFFF, kMaxModCount)`
—— 两边同宽,读者与写者对流的解释重新一致;`OnSubtitleRequest` 补上 `notify.TopicFormId = message.TopicFormId;`;
`QuestService` 的 `default:` 改为打印**请求里的** `message.Status` 并 `return`,不再把无法映射的状态转发出去。
另外把客户端 `OnQuestUpdate` 的 `default:` 里那句永远不可达的 `spdlog::error`(它在 `break;` 之后)提到 `break;` 之前,
未知状态现在会真的留下一条日志。

**验证**:`Code/tests/encoding.cpp:937 TEST_CASE("Mods count is bounded by the writer bound", "[encoding.mods_count]")`
(4096+7 条 mod 往返后只剩 4096 条;手工写入计数 8191 的包被夹到 4096 且末尾条目的 id 正确)与
`Code/tests/encoding.cpp:980 TEST_CASE("Subtitle topic travels in both directions", "[encoding.subtitle]")`
(两端各自的 `TopicFormId` 往返后保持)。`python Tools/Scripts/check_docs.py` 全绿。
### 修复:插件的会话表只增不减,而它正好压在每次按键的路径上

**现象**:OStim Together 的联机场景开得越多,「场景开始键」越钝 —— 同一次按键要做两遍
`_ownerSessions` 全表遍历;并且每个「加演员」菜单、每个直连会话、每次预检捕获都会往表里
加一条,而整张表只在读档时清空一次。

**根因**(修复前):
```cpp
// plugins/OStimTogether/src/CoopSessionManager.cpp(修复前)
// 三处写入,零处删除
:311        _ownerSessions[sessionID] = std::move(session);   // 直连会话
:423        _ownerSessions[sessionID] = std::move(session);   // 预检捕获
// plugins/OStimTogether/src/AddActorConsentGate.cpp:80
            _ownerSessions[gateID] = std::move(gate);          // 加演员闸门

// 唯一的清空点,只在读档时跑一次
// plugins/OStimTogether/src/main.cpp:216(kPreLoadGame)
            OStimTogether::CoopSessionManager::GetSingleton().Reset();
```
失败路径只翻标志位、不删条目:`CoopSessionManager.cpp:707`(`CancelOwnerSession`)、
`:679`(`StartApprovedOwnerSession` 启动失败)、`AddActorConsentGate.cpp:151`/`:181`
(`PollAddActorConsent` 的两条终态分支)。而
`plugins/OStimTogether/compat/OStimUIConsent/Data/Scripts/Source/OSKSE.psc:87-108` 会一直
`PollAddActorConsent` 到它返回非 0 —— 也就是说**每打开一次加演员菜单就必然命中一条终态
分支**,每条都留下一份永不回收的状态。

**可达性**:两次全表遍历都在按键路径上 —— `plugins/OStimTogether/src/Input.cpp:163` 先调
`CleanupCompletedDirectSessions()`(函数体整个内联在 `CoopSessionManager.h:41-57`),`:165`
再调 `TryGateDirectSceneStart()`,后者在 `CoopSessionManager.cpp:249` 调
`HasPendingDirectIntent()`(`std::any_of` 扫同一张表)。按键路径本来就是每个按键都要过的,
而表长只增不减。

**修法**:把「标 `canceled`」换成「真删」,并让清理循环接受所有已结束的会话 ——
`CleanupCompletedDirectSessions` 改为迭代器循环,条件从
`session.directStartIntent && !active && !canceled && activeThreadID >= 0` 放宽为
`!active && !restarting && !canceled && activeThreadID >= 0`(原来只有直连会话会被收,
预检捕获的那条永远留着),命中即 `_activeOwnerByThread.erase(...)` +
`_ownerSessions.erase(it)`;`CancelOwnerSession` 在锁内先取 `snapshot`、再写
`snapshot.canceled = true`、然后 `_ownerSessions.erase(it)`(所有调用方与所有路由查询对
「找不到」与「已取消」的处理完全一致,所以留着一个 `canceled` 条目只是把它多留一整个存档);
`StartApprovedOwnerSession` 的启动失败分支同样改删;`PollAddActorConsent` 的三条终态分支
各 `_ownerSessions.erase(gateIt)`。另外 `RouteOwnerPayload` 在「线程映射还在、会话已经不在
了」时从 `return false` 改为 `return pending`:线程确实是本侧捕获过的,它的原始流量必须继续
被吞掉,不能因为条目被删就转发给所有 peer。

**验证**:`python Code\plugins\tools\plugin_patches.py verify` → `PATCH VERIFY OK` /
`11 patch(es) apply to their pinned commits`;`check` → `PATCH CHECK OK`;`apply` 幂等
(`patches: 0 applied, 11 already present`)。三个文件的 `{`/`}` 平衡都是 0
(`CoopSessionManager.cpp` 1274 行 323/323、`CoopSessionManager.h` 231 行 37/37、
`AddActorConsentGate.cpp` 200 行 42/42)。`python Tools/Scripts/check_docs.py` 全绿。
### 修复:插件的最高频日志点开着同步刷盘,而它就在主线程的动画路径上
**现象**:`AnimSyncTogether` 在 10 分钟的会话里写了 **43271 行日志(7.3 MB)**,其中 39834 行(92.1%)来自
`src/AnimationInputProbe.cpp:76`,3407 行(7.9%)来自 `src/AnimationProbe.cpp:228`;最忙的一秒写了 290 行。
这两条都是 `SKSE::log::info`,无条件触发,而且**每一条都要同步刷盘**。
**根因**(修复前):
```cpp
// plugins/AnimSyncTogether/src/main.cpp:25-26(修复前)
log->set_level(spdlog::level::trace);
log->flush_on(spdlog::level::trace);   // trace 是最低等级 ⇒ 每条消息都立刻落盘
```
`flush_on(N)` 的参数是**刷盘起点**而不是日志等级,所以 `flush_on(trace)` 等于 `flush_on(info)` 等于
「每条都刷」:同一份日志里零条 trace,43271 行全是 `[info]`,也就是 600 秒里 43271 次阻塞刷盘(约 72 次/秒)。
```cpp
// plugins/AnimSyncTogether/src/AnimationInputProbe.cpp:76-80(修复前)
SKSE::log::info("AnimInput actor=00000014 localPlayer=true event='{}' result={} tracked={}", name, result, tracked);
```
**可达性**:两条日志分别挂在本地玩家的动画图输入 vfunc(`RE::VTABLE_PlayerCharacter[3]`,
`src/AnimationInputProbe.cpp:27` 的 `write_vfunc(0x1, PlayerNotifyAnimationGraph)`)和动画图事件 sink 上,
都在**主线程**同步执行,触发点是每次移动、攻击、受击、换装备。`basic_file_sink_mt` 每次刷盘都要抢锁并进内核,
直接落在用户要求的「不得让加载、过场动画、与 NPC/场景交互变慢」上。本仓主程序早已对同一件事定过调
(`Code/client/TiltedOnlineApp.cpp:75-80`:`flush_every(1s)` + `flush_on(warn)`,注释写明「per-frame logging
不得变成 per-frame file I/O」),插件走的却是相反配置。两条日志的内容本身也不值得留在默认等级:
`tracked=` 的 39834 行**全部是 `false`**,`localPlayer=` 的 3407 行**全部是 `true`**、`remoteConnection=` 全为 `0`
—— 它们从未描述过一个真正被同步的事件。
**修法**:`src/main.cpp:28-33` 改成主程序同款策略(`set_level(info)` + `spdlog::flush_every(std::chrono::seconds(1))`
+ `flush_on(spdlog::level::warn)`),info 行最多晚一秒落盘、warn 及以上仍立刻落盘;`AnimationInputProbe.cpp:76`
与 `AnimationProbe.cpp:228` 降为 `SKSE::log::debug`;同步改 `README.md` 的 `## Logging` 节,把 `AnimInput`/`AnimEvent`
标为 debug 并说明 Helmet Toggle 那几条低频诊断仍在默认等级。
**验证**:交付形式是 `Code/plugins/patches/AnimSyncTogether/0001-stop-flushing-every-log-line-and-demote-the-two-per-event-markers.patch`
(该插件是子模块,CI 的 `git submodule update --init --force --recursive` 会丢弃工作树改动)。`python Code\plugins\tools\plugin_patches.py verify`
→ `PATCH VERIFY OK` / `12 patch(es) apply to their pinned commits`;同目录下 `check` → `PATCH CHECK OK`。
`git -C plugins/AnimSyncTogether grep -n "flush_on\|flush_every\|set_level"` 命中 `src/main.cpp:28`/`:32`/`:33`,
`flush_on(spdlog::level::trace)` 已无命中。`python Tools/Scripts/check_docs.py` 全绿。
### 修复:二十处「对端说了算」的浮点直接进了引擎,而 NaN 之后每一次比较都不再成立

**现象**:一个法术效果强度、一次经验同步、一发弹道的落点与朝向,发送方都可以写成 `NaN` 或 `±Inf`。
这二十个字段读完就交给引擎,中间没有任何一处检查 —— 而 `NaN` 不是「很大的数」,它是「没有数」:
它参与算术之后仍然是 `NaN`,参与比较永远为假,所以它落到哪个引擎字段上,哪个字段就再也不可比较、
不可夹紧、不可存储。`Inf` 是同一件事的另一半。

**根因**(修复前):

```cpp
// Code/encoding/Messages/AddTargetRequest.cpp:23(修复前,NotifyAddTarget.cpp:23 同)
Magnitude = Serialization::ReadFloat(aReader);

// Code/encoding/Messages/SyncExperienceRequest.cpp:12(修复前,NotifySyncExperience.cpp:12 同)
Experience = Serialization::ReadFloat(aReader);

// Code/encoding/Messages/ProjectileLaunchRequest.cpp:44-46 / :52-54 / :62-63(修复前,NotifyProjectileLaunch.cpp 同)
OriginX = Serialization::ReadFloat(aReader);
OriginY = Serialization::ReadFloat(aReader);
OriginZ = Serialization::ReadFloat(aReader);
ZAngle = Serialization::ReadFloat(aReader);
XAngle = Serialization::ReadFloat(aReader);
YAngle = Serialization::ReadFloat(aReader);
Power = Serialization::ReadFloat(aReader);
Scale = Serialization::ReadFloat(aReader);
```

这三个家族各有各的落点,但都落在引擎里:

1. `Magnitude` 经 `Code/client/Services/Generic/MagicService.cpp:450 data.fMagnitude = acMessage.Magnitude;`
   进 `MagicTarget::AddTargetData`,再交给 `pActor->magicTarget.AddTarget(data, ...)`。
2. `Experience` 经 `Code/client/Services/Generic/CharacterService.cpp:1094 OnNotifySyncExperience` 直接进
   `PlayerCharacter::AddSkillExperience`(`Code/client/Games/Skyrim/PlayerCharacter.cpp:74-87`),
   那个函数把值原样传给 `RealAddSkillExperience`,再回读 `newExperience - oldExperience` —— 两个 `NaN` 相减还是 `NaN`。
3. 那八个浮点填的是 `Projectile::LaunchData`(`Code/client/Games/Skyrim/Projectiles/Projectile.h:18-50`,
   `static_assert(sizeof(LaunchData) == 0xA8)`),`Projectile::Launch` 把 `fPower` 写进弹丸实体,
   其余进 `pProjectile` 的弹道解算。

同族的既有先例只有两处:`Code/encoding/Messages/RequestHealthChangeBroadcast.cpp:20` 的 `std::isfinite` 夹紧
(`kMaxHealthDelta = 100000.f`)与 `Code/encoding/Structs/TimeModel.cpp:11-15` 的 `ClampFloat`。
两者都在 `Code/encoding/` 里,`WireLimits.h` 的头部注释本来就写着这些读侧钳制该怎么放。

**修法**:`Code/encoding/WireLimits.h` 新增 `inline float ClampFinite(const float aValue) noexcept`,
非有限值**替换**为 `0.f` 而不是夹进某个区间 —— 没有任何区间能让 `NaN` 变得有意义,而这批字段里也没有哪一个
像血量增量那样带「正负即判决」的语义。六个 `.cpp` 的第 2 行加 `#include <WireLimits.h>`,二十处读取改成
`X = WireLimits::ClampFinite(Serialization::ReadFloat(aReader));`。
**有限的合法值必须原样通过**:发送方真的能产生 `99999.f` 的落点与 `-1234.5f` 的角度,
把它们夹进某个范围就等于每一发弹道都被悄悄挪位。

**验证**:`Code/tests/encoding.cpp` 新增 `TEST_CASE("A non finite wire float is replaced where it is read", "[encoding.finite_floats]")`
—— 先直接断言 `ClampFinite` 对 `1.5f`/`-1.5f`/`0.f` 恒等、对 `NaN`/`±Inf` 给 `0.f`,
再对六个消息各做一次「整包往返 + 每个接收对象先写哨兵 `1.f`」,最后用一条 `REQUIRE(received == sent)`
证明合法弹道原样通过。`python Tools/Scripts/check_docs.py` 全绿。
### 修复:血量广播的读取被钳制块挤掉了,于是这个包一直在送未初始化的血量增量
**现象**:一个 `RequestHealthChangeBroadcast` / `NotifyHealthChangeBroadcast` 到达时,包里 `DeltaHealth`
的那四个字节**从不被消费**;紧接着的 `if (!std::isfinite(DeltaHealth))` 检查的是**未初始化的成员** ——
这两个结构体的 `float DeltaHealth;` 没有默认成员初始化器(`RequestHealthChangeBroadcast.h:25`、
`NotifyHealthChangeBroadcast.h:25`),基类 `ClientMessage`/`ServerMessage` 只初始化 `m_opcode`,而工厂是
`TiltedPhoques::MakeUnique<T>()` + `DeserializeRaw`。
**根因**(修复前):
```cpp
// Code/encoding/Messages/RequestHealthChangeBroadcast.cpp:11-24(修复前)
Id = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;

// Never trust the wire: a NaN or an infinity would follow this value into every
// arithmetic operation the receiver performs on it.
if (!std::isfinite(DeltaHealth))
    DeltaHealth = 0.f;
else
    DeltaHealth = std::min(std::max(DeltaHealth, -kMaxHealthDelta), kMaxHealthDelta);
```
写侧一直是 `Serialization::WriteFloat(aWriter, DeltaHealth);`(`:8`),而 `git grep -n ReadFloat` 在这两个
文件上**零命中**:读取那一行是在加入钳制的那次提交里被替换掉的,钳制因此作用于一个与线上无关的值。
**为什么测试没拦住**:`Code/tests/encoding.cpp:620 TEST_CASE("Health change broadcast wire bounds", "[encoding.health_change]")`
只断言钳制的结果(`NaN → 0`、`-1e30 → -kMaxHealthDelta`、`1e30 → kMaxHealthDelta`)。钳制对一个恰好为 0 的
未初始化内存是恒等,所以那三条断言在缺陷上照样通过;`windows.yml:618` 的门禁只检查退出码与 `All tests passed`,
也区分不出「读进来了」和「碰巧是对的」。
**可达性**:服务端 `Code/server/Services/ActorValueService.cpp:77-100 OnHealthChangeBroadcast` 把这个值原样
写进 `ActorValuesList[24]` 并放进转发包;客户端 `Code/client/Services/Generic/ActorValueService.cpp:286-312`
把它加进目标血量,结果不再为正时对非玩家 actor 调 `pActor->Kill()`。发送侧
`Code/client/Services/Generic/ActorValueService.cpp:189-202` 一直在送真实的伤害/治疗(小伤害攒进
`m_smallHealthChanges`、`RunSmallHealthUpdates` 每 250 ms 批量发),所以这条路径在联机里是热的。
**修法**:两个文件的 `DeserializeRaw` 各补回一行 `DeltaHealth = Serialization::ReadFloat(aReader);`
(`Code/encoding/Messages/RequestHealthChangeBroadcast.cpp:16`、`Code/encoding/Messages/NotifyHealthChangeBroadcast.cpp:16`),
位置在 `Id` 之后、钳制之前 —— 钳制逻辑与既有断言都不用改。
用例同时给四个接收对象先写一个**有限哨兵**(`nan.DeltaHealth = 1.f;`、`clamped.DeltaHealth = 1.f;`、
`notification.DeltaHealth = 1.f;`、`received.DeltaHealth = 1.f;`):钳制对 1.f 是恒等,于是断言成立的**唯一**
原因就是那一行读取存在 —— 删掉它,`REQUIRE(nan.DeltaHealth == 0.f)` 立刻变成 `1.f == 0.f`。
**验证**:`git grep -n ReadFloat Code/encoding/Messages/RequestHealthChangeBroadcast.cpp Code/encoding/Messages/NotifyHealthChangeBroadcast.cpp`
现在各命中 `:16`;`git grep -c "Seeded with a finite sentinel" Code/tests/encoding.cpp` 为 4。
另外对 `Code/encoding/Messages/**` 与 `Code/encoding/Structs/**` 的全部 `.cpp` 做了「写侧标识符 − 读侧标识符」
差集扫描,过滤掉临时变量赋值(`uint64_t tmp; aReader.ReadBits(tmp, N); Field = tmp & MASK;`)造成的误报后,
**只剩这两个文件**;反向(读了但从不写)为 0 命中。`python Tools/Scripts/check_docs.py` 全绿。
### 修复:一个插件的最高频日志在默认配置下同步刷盘,而它一次采样最多打 45 条
**现象**:`MorphSyncTogether` 在 10 分钟的会话里写了 **1864 行日志(348 KB)**,其中 1433 行(76.9%)来自
`src/AppearanceProbe.cpp:814`、187 行(10.0%)来自 `src/UdpTransport.cpp:195`、94 行来自
`src/MorphSyncService.cpp:469`、93 行来自 `src/TngSync.cpp:177`;这些行**每一条都要同步刷盘**。
**根因**(修复前):
```cpp
// plugins/MorphSyncTogether/src/main.cpp:21-22(修复前)
spdlog::set_level(spdlog::level::trace);
spdlog::flush_on(spdlog::level::trace);   // trace 是最低等级 ⇒ 每条消息都立刻落盘
```
`flush_on(N)` 的参数是**刷盘起点**而不是日志等级,所以 `flush_on(trace)` 等于 `flush_on(info)` 等于
「每条都刷」:同一份日志里零条 trace、零条 debug,1864 行 = `info` 1675 + `warning` 187 + `error` 2,
也就是 586 秒里 1864 次阻塞刷盘。那 1433 行更不是「一秒一条」,而是「一次采样一簇」——
`_verbose` 闸门的三处默认值全是开的,而一次探测的明细行数实测 min 1 / avg 24.71 / **max 45**:
```cpp
// plugins/MorphSyncTogether/src/AppearanceProbe.cpp:812-819(闸门默认值改了,循环本身没动)
if (_verbose) {
    for (const auto& detail : data.details) {
        SKSE::log::info("MST APPEARANCE DETAIL scope={} label=\"{}\" actor={:08X} {}", scope, label, actor->GetFormID(), detail);
    }
}
```
同一函数在它之前 18 行已经打过一条 `MST APPEARANCE CHANGE`(`src/AppearanceProbe.cpp:771-787`,
一行 14 个字段,含 `details=` 的**个数**),只在哈希变化时触发,实测 36 行。也就是说同一份状态被
描述了两遍:一遍是总结,一遍是逐元素展开,而后者 1433 行里 `scope=` 与 `actor=` 各只有一个取值
(全是 `LOCAL` / `00000014`)、distinct payload 只有 131 个。
**可达性**:`MorphSyncService::SyncLoop` 按 `SyncIntervalMs`(默认 1000 ms)切片轮询,每轮经 `QueueTick()`
把 `TickOnGameThread()` 投到**游戏主线程**,而它第一件事之一就是
`src/MorphSyncService.cpp:290 AppearanceProbe::GetSingleton().ProbeLocalPlayer(player);`。
所以那次「一次 45 行」的循环连同 45 次 `basic_file_sink_mt` 阻塞刷盘全在主线程的一个 tick 里;
同一 tick 内的两条广播日志与那 187 条 `result=not-connected` 的 warn 也各自同步落盘。
这直接落在用户要求的「不得让加载、过场动画、与 NPC/场景交互变慢」上。本仓主程序早已对同一件事定过调
(`Code/client/TiltedOnlineApp.cpp:75-80`:`flush_every(1s)` + `flush_on(warn)`,注释写明「per-frame logging
不得变成 per-frame file I/O」),插件走的却是相反配置。
**修法**:`src/main.cpp:22` 的 `flush_on(spdlog::level::trace)` 改为主程序同款策略
(`spdlog::flush_every(std::chrono::seconds(1))` + `flush_on(spdlog::level::warn)`),info 行最多晚一秒落盘、
warn 及以上仍立刻落盘;`set_level(trace)` **保留不动**(该插件全部 3 个 trace 站点都在「已经决定不发」的
分支上,抬等级是行为变化)。`_verbose` 的三处默认值一起翻成 false —— `src/AppearanceProbe.h:164`、
`src/Config.h:39`、`package/Data/SKSE/Plugins/MorphSyncTogether.ini` 的 `Verbose=0`(ini 覆盖前两者,
必须同改),并在 `README.md` 的 `## Expected log` 节说明排查时把它设回 1。
**验证**:交付形式是 `Code/plugins/patches/MorphSyncTogether/0003-stop-flushing-every-log-line-and-disable-the-appearance-detail-log.patch`
(该插件是子模块,CI 的 `git submodule update --init --force --recursive` 会丢弃工作树改动)。`python Code\plugins\tools\plugin_patches.py verify`
→ `PATCH VERIFY OK` / `13 patch(es) apply to their pinned commits`;同目录下 `check` → `PATCH CHECK OK`。
`git -C plugins/MorphSyncTogether grep -n "flush_on\|flush_every\|set_level"` 命中 `src/main.cpp:25`/`:26`,
`flush_on(spdlog::level::trace)` 已无命中。`python Tools/Scripts/check_docs.py` 全绿。
### 修复:客户端可控的 id 与索引在五个使用点上都没被判定过「它真的有效」
**现象**:一次远程施法、一次远程打断施法、一发远程弹道到达时,发送方在包里选中的是哪个 actor、
用哪只手施法、弹丸基类存不存在,接收方都不判定就往下用;覆层的「传送到队员」按钮则会在
第 65536 个玩家之后把人送到**另一个人**身边。这五处都不是崩溃,而是「对端说什么就是什么」:
`Cast<Actor>` 在运行版本的 RTTI 没有映射时**返回空指针**(`Code/client/Games/Skyrim/RTTI.h:17-27`),
`GenerateMagicCasters` 只在引擎肯给的时候才填槽(`Code/client/Games/Skyrim/Actor.cpp:915-924`),
`CastingSource` 是 `int32_t`(`Code/encoding/Messages/NotifySpellCast.h:22`、`NotifyInterruptCast.h:20`、
`NotifyProjectileLaunch.h:42`、`ProjectileLaunchRequest.h:45`),而读侧只判了上界、没判下界。

**根因**(修复前):

```cpp
// Code/client/Services/Generic/MagicService.cpp:149-156(修复前,OnNotifySpellCast)
Actor* pActor = Cast<Actor>(pForm);
pActor->GenerateMagicCasters();                       // Cast 可以是空的

// Only left hand casters need dual casting (?)
pActor->casters[CS::LEFT_HAND]->SetDualCasting(acMessage.IsDualCasting);   // 槽可以是空的

if (acMessage.CastingSource >= 4)                     // int32_t:-1 进不来
    return;

pSpell = pActor->magicItems[acMessage.CastingSource]; // 于是负数索引到了 magicItems[4]
```

```cpp
// Code/client/Services/Generic/MagicService.cpp:284-286(修复前,OnNotifyInterruptCast)
Actor* pActor = Cast<Actor>(pForm);
pActor->GenerateMagicCasters();                       // 同上一处,没有判空
```

```cpp
// Code/client/Services/Generic/CombatService.cpp:125 / :140(修复前,OnNotifyProjectileLaunch)
launchData.pProjectileBase = TESForm::GetById(cProjectileBaseId);   // 可以是空的

launchData.eCastingSource = (MagicSystem::CastingSource)acMessage.CastingSource;   // 没有范围判定
```

```cpp
// Code/encoding/Messages/TeleportRequest.h:20 / TeleportRequest.cpp:12(修复前)
uint16_t PlayerId{};                                        // 字段 16 位
PlayerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF; // 读侧按 32 位掩码
```

前四处各自都能落到一次无效解引用或一次越界索引上,而它们要的只是「这个值有效吗」这一句判断;
第五处的字段宽度则与它要装的东西不是一回事:玩家 id 由 `Code/server/Game/Player.cpp:1-30` 的
`static constinit std::atomic<uint32_t> s_counter{1};` 单调发号、**从 1 开始、不回收、不回绕**,所以
第 65536 个连接是一个普通连接;而 `Code/encoding/Messages/NotifyPartyInfo.h:25 Vector<uint32_t> PlayerIds{};`
送到覆层的 id 本来就是 32 位(`Code/client/Services/Generic/PartyService.cpp:126`/`:168` 取,`:137-141` 转发给 CEF),
`Code/client/Services/Generic/OverlayClient.cpp:170 request.PlayerId = aEventArgs->GetInt(0);` 又是一次 `int → uint16_t`,
两次窄化之后服务端 `Code/server/Services/OverlayService.cpp:90` 用它 `GetById` —— 指向的是**另一个**玩家。

**判据**:`Cast<T>` 会返回空、`GenerateMagicCasters` 会留下空槽、`CastingSource` 是带符号的、
玩家 id 是 32 位且不回收 —— 这四条都是**被调用方写明的**事实,而五个使用点都假设了反面。
同族的对照组就在同一个文件里:`Code/client/Games/Skyrim/Projectiles/Projectile.cpp:56-68` 对
`Cast<Actor>(arData.pShooter)` 判了空,`Code/client/Services/Generic/CombatService.cpp:112-116` 对
`Cast<TESObjectCELL>` 也判了空 —— 也就是说这一族在本仓是**多数守住了、少数没守住**,而不是没有范式。

**修法**:

1. `Code/client/Services/Generic/MagicService.cpp:151-157`(`OnNotifySpellCast`)与 `:302-306`(`OnNotifyInterruptCast`)
   在 `Cast<Actor>` 之后加 `if (!pActor) { spdlog::warn(...); return; }`。
2. 同文件 `:161-167` 再加一道 `if (!pActor->casters[CS::LEFT_HAND])` —— `GenerateMagicCasters` 的循环体是
   `if (casters[i] == nullptr) casters[i] = Cast<ActorMagicCaster>(GetMagicCaster(static_cast<CS>(i)));`,
   引擎不给就仍然是空。
3. `:172` 与 `:274` 的范围判定补上下界:`if (acMessage.CastingSource < 0 || acMessage.CastingSource >= 4)`;
   `Code/client/Services/Generic/CombatService.cpp:146-152` 把弹道路径上那句 C 风格强转换成同一段判定 + `static_cast`。
4. `Code/client/Services/Generic/CombatService.cpp:127-131` 在 `pProjectileBase` 赋值之后判空(弹丸基类不存在时
   `Projectile::Launch` 拿不到任何东西可发)。
5. `Code/encoding/Messages/TeleportRequest.h:23` 把 `uint16_t PlayerId{};` 加宽成 `uint32_t PlayerId{};`,
   `Code/client/Services/Generic/OverlayClient.cpp:170` 改成 `static_cast<uint32_t>(aEventArgs->GetInt(0))`;
   读侧 `& 0xFFFFFFFF` 与写侧 `WriteVarInt` 都不用动,线格式对已存在的小 id 完全不变。

**验证**:`Code/tests/encoding.cpp` 新增 `TEST_CASE("A teleport request carries the whole player id", "[encoding.teleport_request]")`,
对 `0`/`1`/`0xFFFF`/`0x10000`/`0x10001`/`0xFFFFFFFF` 各做一次整包往返并断言 `received.PlayerId == aPlayerId` ——
`0x10000` 与 `0x10001` 正是 16 位字段会塌成同一个值的两个 id。其余四处在 `Code/client/Services/Generic/` 里,
C++ 编译由 CI 执行(`windows.yml`)。`python Tools/Scripts/check_docs.py` 全绿。
### 修复:另外四个插件的最高频日志点也都在同步刷盘
**现象**:`DAVSyncTogether`、`IEDSyncTogether`、`TradeTogether`、`OStimTogether` 四个插件里,每一条日志都会在
产生它的那个线程上**同步写进内核**,而它们的等级设置默认开着 `trace` —— 也就是说这两层里所有 trace/debug 调用点
都是活的,并且每一条都要等磁盘。这与 §67/§68 已经修好的 `AnimSyncTogether`、`MorphSyncTogether` 是同一行配置。

**根因**:`spdlog::flush_on(N)` 的参数是**刷盘起点**而不是日志等级 —— 它表示「level >= N 的消息打完立刻 flush」,
所以 `flush_on(trace)` 覆盖全部消息。四个插件的初始化里各写着这一行,而写法只有一处不同:
`plugins/DAVSyncTogether/src/main.cpp:28`、`plugins/IEDSyncTogether/src/main.cpp:23`、
`plugins/OStimTogether/src/main.cpp:51` 是 `spdlog::flush_on(spdlog::level::trace);`,
`plugins/TradeTogether/src/main.cpp:20` 是 `logger->flush_on(spdlog::level::info);` ——
`info` 恰好是这四个插件里唯一一直有输出的等级,所以它比另外三处更重。
四段初始化里都没有 `spdlog::flush_every(...)`,而本仓既有的策略(§67 起)是每秒 flush 一次、warn 及以上立即落盘。

**判据**:刷盘起点是一个数,而等级是另一个数,两个都要读;而 `flush_every` 是**默认 logger** 的全局设置,
`flush_on` 是**某一个 logger** 的设置,所以 `TradeTogether` 那一份不能照抄另外三份 ——
它的 `flush_on` 必须留在 `logger->` 上,`flush_every` 必须写在 `spdlog::set_default_logger(...)` **之后**。

**修法**:四个补丁各只改一段初始化,`set_level(trace)` 全部保留(抬等级是行为变化,与本次缺陷无关):

```cpp
// plugins/DAVSyncTogether/src/main.cpp:26-32(修复后,IEDSyncTogether 与 OStimTogether 逐字同款)
spdlog::set_default_logger(std::move(log));
spdlog::set_level(spdlog::level::trace);
// flush_on(trace) wrote every line to disk before the emitting thread
// could continue, and the level above enables trace by default.
// Warnings stay immediate; the rest is flushed once a second.
spdlog::flush_every(std::chrono::seconds(1));
spdlog::flush_on(spdlog::level::warn);
```

```cpp
// plugins/TradeTogether/src/main.cpp:19-26(修复后,唯一需要区别对待的一份)
logger->set_level(spdlog::level::trace);
logger->flush_on(spdlog::level::warn);
logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [TradeTogether] [%l] [%s:%#] %v");
spdlog::set_default_logger(std::move(logger));
spdlog::flush_every(std::chrono::seconds(1));
```

没有降任何一条日志的等级:这四个插件里「每次收发都会打」的 warn(`IEDSyncTogether/src/STRPMAdapter.cpp:224`/`:514`/`:539`、
`OStimTogether/src/AddonBridge.cpp:268`/`:277`/`:283`/`:484`/`:494`/`:510`/`:587`/`:598`、
`TradeTogether/src/Trade.cpp:1055`/`:1063`/`:1080`/`:1110`/`:1165`、`DAVSyncTogether/src/DAVNetworkService.cpp:144`/`:156`)
都是「对端可控输入触发」的路径,降级只会让现场剩下一片沉默,而新策略下它们本来就是立即落盘的。

**验证**:交付形式是四个补丁(`Code/plugins/patches/DAVSyncTogether/0002-stop-flushing-every-log-line-to-disk.patch`、
`Code/plugins/patches/IEDSyncTogether/0001-stop-flushing-every-log-line-to-disk.patch`、
`Code/plugins/patches/TradeTogether/0001-stop-flushing-every-log-line-to-disk.patch`、
`Code/plugins/patches/OStimTogether/0005-stop-flushing-every-log-line-to-disk.patch`)—— 这四个插件都是子模块,
CI 的 `git submodule update --init --force --recursive` 会丢弃工作树改动。
`python Code\plugins\tools\plugin_patches.py verify` → `PATCH VERIFY OK` / `17 patch(es) apply to their pinned commits`(13 → 17);
同目录下 `check` → `PATCH CHECK OK`;`plugin_snapshot.py verify` → `SNAPSHOT OK`;
四个 `src/main.cpp` 上 `flush_on(spdlog::level::trace)` 已无命中,`flush_every(std::chrono::seconds(1))` 各命中一处
(`DAVSyncTogether:31`、`IEDSyncTogether:26`、`OStimTogether:54`、`TradeTogether:26`)。`check_docs.py` 全绿。
### 修复:三个插件的对端可控缓存只增不减,以及一个「门禁全绿却装不上」的补丁

**现象**:两件事。其一,`IEDSyncTogether`、`MorphSyncTogether`(含 `TngSync`)、`OStimTogether` 的四张远端缓存表全部以「对端在载荷里自选的字符串」为键,唯一清空点是读档,会话内没有上限 —— 而它们每 750ms/1s 会被遍历一次做演员名字扫描,所以表越大,游戏线程上的那次扫描越贵;其二,上一版提交的 `OStimTogether/0006` 补丁从 pin 出发装不上,`plugin_patches.py apply` 直接中止,而 `verify` 与 `check` 两条门禁当时都是绿的。

**根因**:

一、四张表的键由对端写,而全仓没有 `erase`:

```cpp
// plugins/IEDSyncTogether/src/SyncService.cpp(修复前)
const auto instanceID = ReadField(packet, "id");   // :232,长度零检查
auto& snapshot = _remoteSnapshots[*instanceID];    // :262,insert-or-default
// plugins/MorphSyncTogether/src/MorphSyncService.cpp(修复前)
auto& snapshot = _remoteSnapshots[sender];         // :667
auto& snapshot = _remotePubicSnapshots[sender];    // :742
// plugins/MorphSyncTogether/src/TngSync.cpp(修复前)
auto& state = _remoteStates[sender];               // :221
```

IED 的 `id=` 是它自己的包装字段,由对端整包替换(`plugins/IEDSyncTogether/src/StrTransport.cpp:302-309` 拼的是 `IEDST|v1|id={}|from={}|relay=0|{}`,而 `UdpTransport.cpp:680` 只查前缀);Morph/TNG 的 `from=` 来自 `plugins/MorphSyncTogether/src/UdpTransport.cpp:219-222` 的对端显示名,过一道 `SanitizeField`(`:26-37`,只换 `|`/CR/LF、空则 `"Player"`,**没有长度上限**)。同族的 `_assemblies` 是唯一有 prune 的表(`MorphSyncService.cpp:356-366`,15 秒过期),旁边两张快照表没有。

二、补丁的基线选错了:

```powershell
git apply --check --cached  # verify:对 index,即 pin
git apply --check --reverse # check:对工作树,只问「是否已应用」
git apply                   # apply:CI 的那条路,从 pin 出发按文件名顺序落
```

`0006` 是拿 pin 当基线生成的,于是把 `0003` 的 `AddonBridge.cpp`/`AddonBridge.h` 两段 hunk 又抄了一遍;对 pin 自洽、对「`0003` 已应用」的工作树则找不到锚点。

**判据可机械执行**:「这个键是谁选的,而它什么时候被删掉」—— 键由对端选、又没有任何删除路径 = 缺陷;补丁门禁则要问「这条门禁跑在哪种工作树上」—— 只有从 pin 出发的 `apply` 能发现基线选错。

**修法**:

一、五张表各自设上限,满了丢包而不是丢表:

```cpp
// plugins/IEDSyncTogether/src/STRPMAdapter.h(修复后)
static constexpr std::size_t kMaxRemoteStates = 64;
// plugins/MorphSyncTogether/src/MorphSyncService.h(修复后)
static constexpr std::size_t kMaxRemoteSnapshots = 64;
static constexpr std::size_t kMaxRemotePubicSnapshots = 64;
// plugins/MorphSyncTogether/src/TngSync.h(修复后)
static constexpr std::size_t kMaxRemoteStates = 64;
```

先判 `contains` 再判上限:已有的键永远还能更新,被拒的只是**新**键。

二、`FreeScenePhaseSync` 的两张表按各自语义设界:

```cpp
// plugins/OStimTogether/src/FreeScenePhaseSync.h(修复后)
static constexpr std::size_t kMaxRemotePreps = 32;
static constexpr std::size_t kMaxReadyTokens = 256;
```

`_remotePreps` 是有序队列,满了 `erase(_remotePreps.begin())` 丢掉的正是最不可能还在飞的那条;`_lastReadyToken` 没有顺序语义,满了整体清空只值一包。

三、重写 `0006`,把基线从 pin 改成「`0003` 已应用」:先把已应用的工作树 `git add -A` 进 index,再 `git diff`,落盘后 `git reset` 把 index 还原成 pin,让 `verify` 仍然有意义。补丁从 6567 B 变成 5780 B,少掉的正是 `0003` 的 `ClearRemoteState` 两段 hunk。

**验证**:

四处修改都在插件层,交付方式是补丁(插件是子模块,本仓不拥有、也不能 push):

- `Code/plugins/patches/IEDSyncTogether/0002-bound-the-live-peer-table-and-the-decoded-object-list.patch`(2026-10-10 重写:原版打在 `src/SyncService.{h,cpp}` 上,而这两个文件在本插件的 `CMakeLists.txt` 里从来没进过 `SOURCES`,补丁打上去等于没打;现在改打真正被编译的 `src/STRPMAdapter.{h,cpp}` 与 `src/LocalIEDState.{h,cpp}`)
- `Code/plugins/patches/MorphSyncTogether/0004-bound-the-remote-snapshot-tables.patch`
- `Code/plugins/patches/OStimTogether/0008-bound-the-phase-sync-tables.patch`
- `Code/plugins/patches/OStimTogether/0006-coalesce-the-addon-state-reapply-and-bound-the-remote-caches.patch`(重写)

`python Code\plugins\tools\plugin_patches.py verify` → `PATCH VERIFY OK` / `22 patch(es) apply to their pinned commits`(19 → 22);`apply` → **`patches: 22 applied, 0 already present`**(从干净工作树,这是 CI 的那条路径);`check` → `PATCH CHECK OK`;`plugin_snapshot.py verify` → `SNAPSHOT OK`;`check_transport_compat.py` → `TRANSPORT COMPATIBILITY OK`;`check_docs.py` 全绿。另有一项更强的复核:22 个补丁从 pin 依次应用后,九个被改文件与手工编辑的版本**逐字节相同**。

**教训**:「这个键合法吗」和「这个键能有几个」是两个问题 —— `SanitizeField`/`IsSafeAddonToken` 把形状管得很好,条数没人管。

**教训**:同族的表要用同一把尺子量 —— `_assemblies` 有 15 秒 prune 而旁边两张快照表没有,缺陷的形状是「同一处清理里,谁缺席」。

**教训**:门禁要跑在它实际会跑的那种工作树上 —— `verify` 对 index、`check` 对工作树,两者都看不出「补丁的基线选错了」。
### 修复:插件层一次节点变化就起两个线程,以及三张只增不减的对端可控缓存

**现象**:两件事,都发生在 OStim 多人场景里。其一,场景每换一个动画节点,`OStimTogether` 就为每个非玩家 actor 起 2 个 detached 线程(250ms 与 1100ms 各一个),节点切得越密线程起得越多;其二,插件的四张缓存键全部由对端在载荷里自选,而它们只在读档时清空一次,会话内没有上限。

**根因**:

一、重放调度没有合并,而两个调用点都在游戏线程上:

```cpp
// plugins/OStimTogether/src/AddonBridge.cpp(修复前)
schedule(std::chrono::milliseconds(250), "T250");
schedule(std::chrono::milliseconds(1100), "T1100");
```

调用点一是 `plugins/OStimTogether/src/AddonStateRepair.cpp:86`(在 `OStim::ThreadInterface::registerNodeChangedListener` 的回调里对每个非玩家 actor 调一次),节点变化来自 `plugins/OStimNG/skse/src/Core/Thread.cpp:337` 的 `EventUtil::invokeListeners(nodeChangedListeners);`;调用点二是 `plugins/OStimTogether/src/OStimBridge.cpp:3941`(停止场景时对同一批 actor 再调一轮)。

二、缓存键由对端选,而没有任何删除路径:

```cpp
// plugins/OStimTogether/src/AddonBridge.cpp(修复前)
auto& cached = _remoteOverlays[actor->GetFormID()][cacheKey];      // :531
_remoteObjects[actor->GetFormID()][key] = CachedObjectState{...};  // :617
// plugins/OStimTogether/src/ActorResolverSTRPM.cpp(修复前)
_strpmRemoteByName[name] = formID;   // :662
_resolved[key] = resolved.chosen;    // :928
```

`IsSafeAddonToken` 只约束每个键的长度与字符集,不约束**数量**;唯一的清理点是 `ClearRemoteState()`,而它只在 `plugins/OStimTogether/src/main.cpp:227` 的 `kPreLoadGame` 被调一次。

**判据可机械执行**:「这个键是谁选的,而它什么时候被删掉」—— 键由对端选、又没有任何删除路径 = 缺陷。

**修法**:

一、重放合并成一个可自愈的守卫:

```cpp
// plugins/OStimTogether/src/AddonBridge.cpp(修复后)
constexpr auto kReapplyGuardWindow = std::chrono::seconds(5);
...
const auto now = std::chrono::steady_clock::now();
std::scoped_lock lock(_stateMutex);
const auto it = _reapplyPending.find(actorFormID);
if (it != _reapplyPending.end() && it->second > now)
{
    return;
}
_reapplyPending[actorFormID] = now + kReapplyGuardWindow;
```

末阶段(`"T1100"`)在 `sleep_for` 之后 `_reapplyPending.erase(actorFormID)`。用过期时间而不是只用一个 set:如果这中间游戏读了档,`kPreLoadGame` 会重置十个子系统,那个线程可能再也跑不到 erase,守卫会把 actor 永久锁死。

二、两张远程缓存按 actor 设上限:

```cpp
// plugins/OStimTogether/src/AddonBridge.cpp(修复后)
constexpr std::size_t kMaxRemoteChannelsPerActor = 16;
constexpr std::size_t kMaxRemoteObjectsPerActor = 16;
```

先判 `contains` 再判上限:已有的 channel 永远还能更新,被拒的只是**新**键。

三、两个解析缓存设界(`ActorResolver.h` 的 `kMaxResolvedEntries = 512` / `kMaxRemoteNames = 512`):名字缓存满了跳过这一个名字(它是「省一次扫描」的缓存,满了只是多扫一次),解析缓存满了直接清空(它没有任何读者)。

**验证**:

两处修改都在插件层,交付方式是补丁(插件是子模块,本仓不拥有、也不能 push):

- `Code/plugins/patches/OStimTogether/0006-coalesce-the-addon-state-reapply-and-bound-the-remote-caches.patch`
- `Code/plugins/patches/OStimTogether/0007-bound-the-resolver-caches.patch`

`python Code\plugins\tools\plugin_patches.py verify` → `PATCH VERIFY OK` / `19 patch(es) apply to their pinned commits`(17 → 19);同目录下 `check` → `PATCH CHECK OK`;`apply` → `patches: 0 applied, 19 already present`;`python Tools/Scripts/check_docs.py` 全绿。插件是 CMake 工程、本机不编译(`plugins/OStimNG/skse/AGENTS.md` 明写不要构建 C++/CMake),所以这一批的证据是补丁门禁 + 逐字命中。

**教训**:后台线程的数量不能由对端或事件频率决定。

**教训**:「有校验」不等于「有上限」—— 判据要问「这个键是谁选的,而它什么时候被删掉」,不是「这个键合法吗」。

**教训**:合并用的守卫必须能自己过期,否则一次中途读档就能把它变成死锁。
### 修复:管理员判定由对端自称的 id 决定,拒绝的调用仍然改状态,以及一个 `assert` 就长在收包路径上

**现象**:三件事,都可以由一个普通玩家触发。其一,任何玩家在聊天框打 `/settime 12 00` 都能改全服时间,
不需要是管理员;其二,管理员打 `/settime 99 99` 会看到「成功」,而服务器的时间倍率被这一条非法参数**改掉**了;
其三,服务端在 `releasedbg` 构建下收到一个「指向本地缓存里没有的字符串 id」的包会直接 `abort`。

**根因**:

一、`CommandService::OnSetTimeCommand` 的管理员分支拿 `acMessage.Packet.PlayerId` 去比对管理员列表:

```cpp
// Code/server/Services/CommandService.cpp(修复前)
const auto cPlayerId = static_cast<uint32_t>(acMessage.Packet.PlayerId);
for (const auto session : GameServer::Get()->GetAdminSessions())
{
    const auto* pAdmin = PlayerManager::Get()->GetByConnectionId(session);
    if (!pAdmin)
        continue;
    if (pAdmin->GetId() == cPlayerId)   // ← 「某个管理员的 id 等于我自称的 id」
```

`Packet.PlayerId` 是**发送者自己填的**:客户端在 `Code/client/Services/Generic/OverlayClient.cpp:163`
把 `m_transport.GetLocalPlayerId()` 写进 `SetTimeCommandEvent`,再由
`Code/client/Services/Generic/CommandService.cpp:35 request.PlayerId = acEvent.PlayerId;` 原样上线。
而每个玩家的 id 都是公开的 —— `NotifyPlayerJoined` 会把它广播给所有人。于是判定退化成「我知道某个管理员的
id,我就能当管理员」。对照同函数下面的队长分支:`pPartyService->IsPlayerLeader(acMessage.pPlayer)` 用的是
**真指针**,`PartyService.cpp:50` 从队伍表里读 `LeaderPlayerId` —— 同一个 handler 里两种判定,一种用真身份、
一种用对端字段,这个不一致本身就是判据。

同一段循环还有第二个缺陷:`GetByConnectionId` 对「还没有 player row 的会话」返回 `nullptr`,而管理员的
connection id 在 `GameServer.cpp:909`(`m_adminSessions.insert(aConnectionId)`)就进了集合,player row 要到
`:990`(`GetPlayerManager().Create`)才建,中间有三条提前返回(模组策略不符 `:960`、重复认证 `:996`、脚本拒绝
入服 `:1013`)都不会回头清理这个集合 —— 集合只在断线时 `:612 m_adminSessions.erase(aConnectionId);`。
`pAdmin` 判空写成了 `continue`,所以这一段不会崩;真正的问题是**判空之后仍然拿 `GetId()` 去比**,而
「某个管理员的 id 等于我自称的 id」这个条件与「我是那个管理员」无关。

二、`CalendarService::SetTime` 把赋值放在了校验之前:

```cpp
// Code/server/Services/CalendarService.cpp(修复前)
m_dateTime.m_timeModel.TimeScale = aScale;          // ← 先写
if (aHours >= 0 && aHours <= 23 && aMinutes >= 0 && aMinutes <= 59)
{
    ...
    return true;
}
return false;                                        // ← 拒绝了,但上面已经写了
```

`TimeScale` 是**整个世界**的时间倍率(`DateTime::GetDeltaTime` 拿它乘每帧的毫秒数,`Code/common/DateTime.cpp:35`)。
于是「时/分不合法」这条拒绝路径反而把倍率改了,而且**绕过了这个字段在别处的两道界**:
`SetTimeScale`(`Code/server/Services/CalendarService.cpp:148`)要求 `0..1000`,线上读取
(`Code/encoding/Structs/TimeModel.cpp:47 ClampFloat(TimeScale, kMaxTimeScale)`)也要求 `0..1000`,
只有这一处裸写。更糟的是 `aScale` 可以是非有限值:`NaN` 会顺着 `GetDeltaTime` 进 `DateTime::Update`,
最后落在 `static_cast<uint32_t>(m_timeModel.Time / 24.f)`(`Code/common/DateTime.cpp`)—— 把 `NaN` 转成无符号整数
是未定义行为。两个调用点(`Code/server/Services/CommandService.cpp:58`、`:75`)**都没有看返回值**,
一律回 `kSuccess`,于是玩家看到「成功」而什么都没发生。

判据是同族的三处写法:`SetDate`(`:96-113`)先判后写、`SetTimeScale`(`:148`)先判后写、
`GameServer_Bindings.cpp:35-37` 的 Lua `SetTime` 绑定**先夹再调**
(`aHours = std::max(0, std::min(aHours, 23));` … `aScale = std::max(1.0f, std::min(aScale, 100.0f));`)。
同一个函数在脚本绑定里先夹、在聊天命令里不夹 —— 「多数地方守住了」不是「这一族守住了」。

三、`CachedString::Deserialize` 对「收到一个本地缓存里没有的 id」直接 `assert(false)`:

```cpp
// Code/encoding/Structs/CachedString.cpp(修复前)
const auto cValue = StringCache::Get()[cId];
if (cValue)
    *this = *cValue;
else
    assert(false);   // ← 对端可控的输入,断言 = 进程终止
```

`CachedString` 出现在 `ActionEvent::EventName` / `TargetEventName`,而 `ActionEvent` 经
`ReferenceUpdate.cpp:43 Actions[i].ApplyDifferential(aReader);` 从 `ClientReferencesMoveRequest` 的
`Map<uint32_t, ReferenceUpdate> Updates` 读进来 —— 这是**每帧都在跑的活流量**,不是死代码。
`assert` 的活性:`xmake.lua:27` 只有 `mode.debug`/`mode.releasedbg`/`mode.release`,全仓没有 `NDEBUG`,
而 `.github/workflows/windows.yml:11-14` 的 `build-mode` 默认值是 `releasedbg` —— **发布构建里这个断言是活的**。
这条路径整条是 `noexcept`(`DeserializeRaw` 是 `noexcept` 虚函数),所以没有「抛出去让人接」这个选项。

**判据可机械执行**:「授权判定读的是对端给的字段,而不是这次连接本身的身份」= 缺陷;
「函数先写成员再校验参数,失败分支不撤销」= 缺陷;「`assert` 的输入来自线上」= 缺陷。

**修法**:

1. 管理员判定改成问这次连接本身:`if (GameServer::Get()->GetAdminSessions().contains(acMessage.pPlayer->GetConnectionId()))`。
   `m_adminSessions` 存的就是 connection id(`GameServer.h:124`),而 `acMessage.pPlayer` 由
   `GameServer.cpp:254-279` 的 handler 生成器保证非空(它先 `GetByConnectionId` 再 `trigger`)。
   这一改同时消掉了那个循环 —— 不再需要「遍历 + 判空 + continue」。`Packet.PlayerId` 仍是线上字段,
   但服务端不再读它。
2. `CalendarService::SetTime` 改成先判后写,并把倍率的界写全:

```cpp
// Code/server/Services/CalendarService.cpp(修复后)
if (!std::isfinite(aScale) || aScale < 0.f || aScale > TimeModel::kMaxTimeScale)
    return false;

if (aHours < 0 || aHours > 23 || aMinutes < 0 || aMinutes > 59)
    return false;

m_dateTime.m_timeModel.TimeScale = aScale;
```

   界用的是 `TimeModel::kMaxTimeScale` 而不是再写一个字面量 `1000.f` —— 这个常量本来就是「服务端手工设置倍率时
   拒绝的上界」(注释原文)。`std::isfinite` 判的是「这是不是一个数」:`NaN` 夹进任何区间都还是 `NaN`,所以是非有限
   值一律拒绝,与 `WireLimits::ClampFinite` 同一策略。顺带把该文件里裸写的 `floor(` / `ceil(` 补上 `std::`
   并显式 `#include <cmath>` —— 这个头此前是靠传递包含进来的。
3. 两个调用点尊重返回值,并把「参数不合法」与「没有权限」分成两个结果:

```cpp
// Code/encoding/Messages/NotifySetTimeResult.h(修复后)
enum class SetTimeResult : uint8_t
{
    kSuccess,
    kNoPermission,
    // The request was understood and authorized, but the hour or the minute in it
    // was not a time. Appended, so the two values above keep their bytes.
    kInvalidTime
};
```

   追加在末尾而不是插在中间:枚举按字节上线(`NotifySetTimeResult.cpp:5-6` 写 8 位、`:14` 读 8 位),
   而 `Code/encoding/Opcodes.h:57-59` 的注释要求「只许追加」;`Code/plugins/tools/check_transport_compat.py`
   也只约束 opcode 的相对顺序,不看枚举值。客户端 `OnSetTimeResult` 据此分两条提示:
   `kNoPermission` → 「只有管理员/私服队长能改时间」,其余 → 「那不是时间,用 /settime HH MM,小时 0-23、分钟 0-59」。
4. `assert(false)` 换成把名字留空:

```cpp
// Code/encoding/Structs/CachedString.cpp(修复后)
*this = TiltedPhoques::String();
```

   空名字不是「静默吞掉」:消费这个字段的 `Code/server/Game/Animation/ActionReplayCache.cpp:66` 与 `:81`
   本来就是拿名字去查固定表(`AnimationEventLists::kExitSpecial.contains(...)` / `kIgnore.contains(...)`),
   查不到即返回 `false`,空串走的是同一条路。修掉的是**进程终止**,不是这个字段的语义。

**验证**:

`Code/tests/encoding.cpp` 新增两个用例:

- `TEST_CASE("A refused set time names which refusal it was", "[encoding.set_time_result]")`(`:1232-1262`)
  先断言三个枚举值的字节依次是 `0`/`1`/`2`(证明 `kInvalidTime` 是追加的),再对三者各做一次整包往返并
  `REQUIRE(received == sent)`。
- `TEST_CASE("A cached string with an unknown id reads as empty", "[encoding.cached_string]")`(`:1264-1304`)
  先造一个「有 id、但 id 不在缓存里」的包,断言名字变成空串而不是进程死;再往缓存里放一条真实字符串,
  断言同一个字段仍然解析得出内容 —— 证明兜底没有把命中路径一起吃掉。

`Code/server/Services/CommandService.cpp` 与 `Code/server/Services/CalendarService.cpp` 是服务端 C++,
本机不编译(`docs/PITFALLS.md` §0/§1:Windows/MSVC 只由 GitHub CI 验证);
`python Tools/Scripts/check_docs.py` 全绿。

**教训**:一个字段只要发送者能自己填,它就不能用来回答「你是谁」。

**教训**:「拒绝」必须发生在任何写入之前 —— 否则「失败」这个词只是返回值的样子。

**教训**:`assert` 是给「代码自己错了」用的;对端可控的输入落在它上面,就是把进程的存活交给对端。

## v1.1.8(2026-09-28)

### 修复:任意玩家一条聊天消息即可打崩专用服务器

**现象**:专用服务器上,一个**还没建好角色**的玩家(刚连上、角色创建失败,或
角色被删除后重连)发一条聊天消息,服务端进程直接终止。

**根因**:`Player::GetCharacter()` 返回的是
`std::optional<entt::entity>`,它在 `CharacterService` 创建角色**之前一直是空**。
`OverlayService::HandleChatMessage`(`Code/server/Services/OverlayService.cpp`)
把它**直接解引用**后交给脚本回调:

```cpp
auto [canceled, reason] = m_world.GetScriptService().HandleChatMessage(
    *acMessage.pPlayer->GetCharacter(), acMessage.Packet.ChatMessage);
```

解引用空的 `optional` 是未定义行为:MSVC release 下取到的是一个**构造出来的
垃圾实体**,脚本层再拿它去 `ctx().at<T>()` 就崩。这不是"玩家操作不当":
`SendChatMessageRequest` 任何已连接客户端都能发,与是否建好角色无关。

**判据是仓库自己的既有约定**——同一个调用在三处都有判空,只有这一处漏了:

| 位置 | 是否判空 |
| --- | --- |
| `WeatherService::OnWeatherChange` | ✅ `if (!...GetCharacter()) return;` |
| `PlayerService::OnPlayerRespawnRequest` | ✅ `if (!character) return;` |
| `OverlayService::HandleChatMessage` | ❌ **本次缺陷** |

这与 v1.1.4 修掉的 `CommandService::OnSetTimeCommand` 是**同一类**:
一条远程报文触发服务端空指针,判据也是"旁边三处都判了、这一处没判"。

**修复**:把脚本回调放进判空分支。**只跳过脚本回调,不吞掉消息** ——
`onChatMessage` 拿到的是实体,传一个默认构造的实体等于把垃圾指针交给脚本;
而下面的广播(`sendPlayerMessage`)自己已经判空,所以该玩家的全局/队伍聊天
照常送达。**修崩溃,不改行为。**

**验证**:改后全仓库再查一遍 `*...GetCharacter()` 的解引用点,只剩
`WeatherService` 与 `PlayerService` 的两处,且三处**都在自己的判空分支里**;
客户端侧同口径(`.value()` 与 `*optional`)逐个核对,全部有前置 `has_value()`
/判空。

### 修复:1.6.x 上 esp 与主菜单 logo 都在,但 F2 菜单打不开

**现象**:游戏 1.6.1170 + SKSE 2.2.6 下,SkyrimTogetherNext 看起来装好了
(Creations 菜单里能看到 esp,主菜单有 logo),**但游戏内按 F2 没有任何反应**。

**根因**:`SkyrimTogetherSKSE.dll` 其实**一次都没被 SKSE 加载过**。
`skse64.log` 里那一行就是判据:

```text
plugin SkyrimTogetherSKSE.dll (00000000  00000000) no version data 0 (handle 0)
```

`handle 0` 表示 SKSE 根本没把它加进加载列表。SKSE 2.2.x 的
`PluginManager::ScanPlugins` 按名字查找 **`SKSEPlugin_Version`** 这个导出;
**找不到就直接记 `no version data` 并跳过,没有回退到旧的 `SKSEPlugin_Query`**。
而本 DLL 只导出了 `Query`/`Load`。

后果是链条式的:`SKSEPlugin_Load` 从未执行 → 运行时
(`SkyrimTogetherRuntime.dll`)**从未被部署** → F2 菜单永不出现。
esp 与 logo 是普通 mod 文件,不受影响,所以"看起来装好了"。

**为什么本地测不出来**:SKSE 2.0.x(游戏 1.5.97)**从不读这个导出**,仍然走
`Query`,所以同一个 DLL 在 1.5.97 上一直是好的。

**修复**:`Code/skse_bootstrap/main.cpp` 增加 `SKSEPlugin_Version` 导出,
声明**版本无关**(`kVersionIndependent_Signatures` +
`kVersionIndependentEx_NoStructUse`)。这两个标志缺一不可:少了后者,
SKSE 1.6.629+ 会答 `only compatible with versions earlier than 1.6.629`;
少了前者,1.7.99+ 会要求 V5 地址库。本 DLL 不解析任何引擎地址,所以版本无关
是如实描述,也避免了每出一版游戏就要改列表、重编译。

新增门禁 `Code/plugins/tools/check_skse_bootstrap.py`,**8 种失败模式各自反向验证**
(含本次真实缺陷、导出被命名空间修饰、结构体字段换序、两个标志缺失、
把标志只写在注释里)。完整取证见 `docs/PITFALLS.md` §42。

### 修复:引导页里 OStim Together 是灰的、根本点不动(装好前置也没用)

**现象**:MO2 引导页里 `OStim Together` 灰掉、无法勾选,而且**先把 OStim
Standalone 装好也照样是灰的**。

**根因不是"前置没装",而是向导里一条永远不可能成立的判断。** MO2 只把
FOMOD 的 `fileDependency` 拿去比对**插件列表**(esp/esm/esl),指向别的东西时
它一律判为缺失,并在日志里明说:

```text
A dependency on non esp/esm/esl SKSE/Plugins/OStim.dll will always find it as missing.
```

上一版把 `detectFile` 写成了 `SKSE/Plugins/OStim.dll`,于是 `Active` 与
`Inactive` 两个分支都不可能命中,`NotUsable` 永远生效——**这条判断跟装没装无关**。

**修复**:

1. `detectFile` 改指前置真实携带的插件文件 `OStim.esp`;
2. `merge_fomod.py` 增加门禁:拒绝非 esp/esm/esl 的 `detectFile`
   (反向验证:改回 `OStim.dll` → exit 1 并指名 `plugins.json`);
3. 顺带修掉拒绝信息比代码旧一个版本的问题:`SelectGraphLayout` 已接受
   OStimNG `7.5.1.x`,而拒绝文案仍称最新只到 `7.5.0.2`——真被拒绝的玩家
   会被告知去装一个比他手上更旧的版本。文案现改为
   `7.4.0.3 (7.4c), 7.5.0.2 (7.5b) and 7.5.1.x`,并新增
   `check_ostim_support_message.py` 把**文案与选择器双向绑死**
   (四种漂移各自反向验证 → 均 exit 1)。

完整取证与命令见 `docs/PITFALLS.md` §41。

### 修复:消息门面只在 1.6.1170 上加载,其余版本静默失效

**现象**:上一条(bootstrap 缺 `SKSEPlugin_Version`)修好之后,1.6.1170 上
`SkyrimTogetherSKSE.dll` 能加载了,但**插件层的同步依然是死的**——桥接日志里
反复刷同一行,永不成功:

```text
send resolver pass failed in 4453 ms; waiting for mapped STR 1.8.0 runtime
ProxyResolver waiting: spans=20 transport=no loaded=no unloaded=no
```

**根因是同一类缺陷的另两副面孔**,而且**都在开发机(1.5.97)上看不见**:

1. **`STRPluginMessagingBridge.dll` 没有导出 `SKSEPlugin_Query`**。
   SKSE 2.0.x(1.5.97)要求 `Query` 与 `Load` **同时存在**,少一个就判
   `does not appear to be an SKSE plugin`——本仓库 1.5.97 的 `skse64.log`
   里就有这一行:

   ```text
   plugin D:\game\SkyrimSE\Data\SKSE\Plugins\\STRPluginMessagingBridge.dll does not appear to be an SKSE plugin
   ```

2. **三个门面 DLL 的 `compatibleVersions` 只写了 `1.6.1170`**,而
   `versionIndependence` 为 `0`。SKSE 2.2.x 对非版本无关的插件,会把这个列表
   拿去和**精确的运行版本**比对,于是 `1.6.317/640/1130/1179` 与
   `1.7.99/104`——**每一个本包都提供了地址库的版本**——全部被判
   `disabled, incompatible with current version of the game`。

两条都在同一处放大:**门面是所有伴侣插件取传输层的入口**(伴侣插件用
`LoadLibraryW` 按名加载它)。门面在某个版本上被 SKSE 跳过,那一条游戏线上
所有插件的同步一起失效。

**为什么 1.6.1170 上是好的**:列表恰好命中,所以这个缺陷只有换一个游戏版本
才暴露——这是本次会话第三次撞上"本地测不出"。

**修复**(`Code/plugins/patches/STRPluginMessagingAPI/0002-*.patch`,子模块只走补丁):

1. 三个 DLL 改为声明**版本无关**(`kVersionIndependent_Signatures` +
   `kVersionIndependentEx_NoStructUse`)。门面**只做签名/锚点扫描**:不含任何
   游戏头文件、不出现任何 `RE::` 类型、没有硬编码地址,所以版本无关是如实描述,
   而不是为了让门禁通过;
2. `STRPluginMessagingBridge.dll` 与 `STRPluginMessagingDiagnostic.dll` 补上
   `SKSEPlugin_Query`(返回 `true`,与 API 门面一致);
3. 三处 `kRuntime_1_6_1170` 列表与不再需要的前置声明一并清掉。

**新增门禁** `Code/plugins/tools/check_skse_entry_points.py`:把 SKSE 两族的
准入规则**回放**在每个手写插件 × **每一个随包发布的运行时**上。
插件按"SKSE 看到的名字"分组而不是按文件,因为 API 门面的版本数据与
`Query`/`Load` 分处两个文件。**反向验证**:还原补丁 → 5 条失败(桥接缺 Query、
三个列表不覆盖);把 bootstrap 的导出改名 → 6 条失败(缺 `Version`)。

完整取证见 `docs/PITFALLS.md` §43。

## v1.1.7(2026-09-27)

### 修复:v1.1.6 的远程仓库编译全红 —— 三处「只有 CI 才会说的话」

**现象**:`v1.1.6` 打 tag 后,四条流水线全挂 —— `Build windows`、`Release build`、
`Playable Skyrim Together Build` 三个都死在同一步 `Build the OStimNG prerequisite`,
`Plugin gates` 死在 `Verify the plugin source snapshots`。而**本地 13 个门禁全绿**,
因为这两处失败都发生在本地看不⻅的地方。

#### A. 那一步的 PowerShell 根本没被解析

```text
Write-Host "COMMONLIB_SSE_FOLDER=$env:COMMONLIB_SSE_FOLDER      ← 少一个收尾引号
Write-Host "COMMONLIB_SSE_FOLDER=$env:COMMONLIB_SSE_FOLDER"     ← 下一行是完整的重复
```

PowerShell 对**整段 `run:` 块**先解析再执行,一个未闭合的字符串会让整块**语法失败**,
于是这一步在任何命令执行前就退出 1,日志里连一行 CMake 输出都没有。三条流水线
共用同一段 `run:`(windows.yml 是被 `release.yml` 与 `windows-playable-build.yml`
复用的 reusable workflow),所以**一处笔误红三条**。

#### B. 那一步依赖的路径,在钉住的那个提交里已经被删掉了

```text
$commonlib = 'plugins/OStimNG/skse/extern/CommonLibSSE-NG'
if (-not (Test-Path (Join-Path $commonlib 'CMakeLists.txt'))) { throw ... }
```

OStimNG 的 `skse/CMakeLists.txt` 会断言 `COMMONLIB_SSE_FOLDER` 并 `add_subdirectory()`
它,所以构建 OStimNG **需要 CommonLibSSE-NG 的源码检出**,而不是插件链的那份 vcpkg 包。
这条路径过去由 OStimNG 自己的 `skse/extern/CommonLibSSE-NG` gitlink 提供 —— 而
`f20db82`("updated compiler",OStimNG 转向 vcpkg 的那一次)**删掉了这个 gitlink**。
我们钉的 `3954683b` 在它之后,于是路径在钉住的树里**不存在**,断言必然抛。

**修法**:把修订钉在 `Code/plugins/plugins.json` 的 `commonLibSseNg` 里,紧挨着它必须匹配的
OStimNG pin,由 CI 读取并 `fetch`。选 `v8.1.0`(`3c0f5a87`)不是随手挑的:
OStimNG 自己的 `126fc52` 就是"fixed build configs, skseapi definition and trampoline for
**commonlib 8.1.0**",而钉住的源码用到 `SKSE_EXPORT`、`GetTrampoline`、
`SkyrimVM::GetVMRuntimeData()`、`MessageBoxMenu::QueueMessage` —— 逐个在 `v8.1.0` 里核对过。

同一处还修掉三个**本来也会让这一步失败**的配置错误:

| 原来 | 问题 | 现在 |
| --- | --- | --- |
| `cmake -S ... -B plugins/OStimNG/build -A x64` | 手写的 `-A` 覆盖不了预设:OStimNG 用 **Ninja** 生成器 | `cmake -S plugins/OStimNG/skse --preset release` |
| 同上 | 没有 `VCPKG_OVERLAY_PORTS`,而 OStimNG 的 `clib-util` **不在上游 vcpkg**(baseline 与 master 都 404),manifest 解析不了 | 预设自带 `${sourceDir}/cmake/ports/` |
| 构建 `plugins/OStimNG/build` | 预设的 `binaryDir` 是 `${sourceDir}/build/${presetName}`,实际产物在 `skse/build/release` | 构建前断言 `CMakeCache.txt` 在正确的树里 |
| `Get-ChildItem 'plugins/*/vcpkg.json'` | **只找一层**,而 OStimNG 的 manifest 在 `plugins/OStimNG/skse/`,于是它 pin 的 `e3ed4186` 从不被 fetch,而 vcpkg 的 `builtin-baseline` 只跑 `git show`、没有回退 ⇒ 在 `--depth 1` 的 vcpkg 里必然 `failed to \`git show\` versions/baseline.json` | 改为 `-Recurse -Filter 'vcpkg.json'`(overlay ports 没有 `builtin-baseline`,会被守卫跳过) |

另外补上 `extern/openvr`:CommonLibSSE-NG 在 `ENABLE_SKYRIM_VR` 打开时会把
`extern/openvr/headers` 加进包含路径并链接 `lib/win64/openvr_api.lib`,而 OStimNG
强制打开三个运行时。它是 CommonLibSSE-NG 自己的子模块,用**它钉住的 gitlink sha** 直接
`fetch`(`submodule update` 在这个 `git init` + 单提交 `fetch` 出来的检出里解析不出 URL,
会报 `no url found for submodule path`)。

#### C. 备份里少了一个文件,因为插件自己的 .gitignore 说了算

```text
snapshots/plugins/OStimNG/.gitignore:553:gfxfontlib.swf
```

快照**原样保留插件的 `.gitignore`**(它本身是被备份的源码,且门禁要求它与钉住提交逐字节一致),
而嵌套的 `.gitignore` **优先于**根目录 `.gitignore` 里的 `!/snapshots/**` 否定规则。
于是 `gfxfontlib.swf`(111 KB,在 `flash/` 下)**在本地存在、却从未进过 git**。
本地的 `verify` 看的是工作区,于是绿;CI 是干净检出,文件不在,于是红 ——
**同一个门禁在两台机器上量的是两件事**。

**修法**:`snapshot` 用 `git add --force` 暂存(这是唯一能无视 ignore 规则的机制),
并把被覆盖的规则打进 `UPSTREAM.json` 的 `ignoredRulesOverridden`;`verify` 改问
**"git 是否记录了这个文件"**而不是"磁盘上有没有"。

### 新增两个门禁(各反向验证过)

| 门禁 | 拦什么 | 反向验证 |
| --- | --- | --- |
| `check_workflow_shell.py` | `.github/workflows/` 里任何 pwsh 块**语法不通过** | 把那个收尾引号删掉 → 精确指出文件、步骤、行号,exit 1 |
| `check_ostimng_build.py` | 清单没钉 CommonLibSSE-NG,或 CI 步骤不再读它 | 删 pin / 换成 tag / 改掉字段名 → 三种都 exit 1 |

`check_workflow_shell.py` 用 PowerShell 自己的解析器,覆盖全部 35 个块;
`${{ ... }}` 先替换成字面量再解析,否则 GitHub 表达式会被当成语法错误误报。

### 顺带修掉三个同类问题

- `plugin_snapshot.py update` 仍在用"根目录有没有 `CMakeLists.txt`"判断子模块是否检出,
  于是 OStimNG 每次都被静默跳过(`not checked out, skipped`)—— 而这正是重钉时必须覆盖的
  那一个前置。§39.5 说"统一为 `is_checked_out()`",但当时只改了 `snapshot` 与 `verify`。
- `UPSTREAM.json` 的 `upstream` 读的是 `.git/config`(只在子模块初始化后才有条目),
  所以 OStimNG 那份记成了**空字符串**。改读 `.gitmodules`(受版本控制的普通内容)。
- `snapshot` 每次都会重写 8 份 `UPSTREAM.json`,把 `ignoredRulesOverridden` 写成空数组,
  产生无意义的 diff。现在只在非空时写入。

### 门禁全绿(13 项)

`strpm_contract`、`plugin_snapshot verify`、`plugin_patches verify`、`papyrus_archive`、
`check_exports`、`check_transport_compat`、`check_skee_abi`、`check_ostimng_versions`、
`check_ostimng_build`、`check_workflow_shell`、`check_facegen_scope`、`check_docs`、`merge_fomod check`。
## v1.1.6(2026-09-27)

### 修复:两处「刚进游戏就闪退」—— 一处在我们的运行时,一处在 RaceMenu 接口版本

**现象**:MO2 + SKSE 打开游戏「一小会儿就直接闪退」。日志里三种崩溃,其中两种反复出现,
都是 `EXCEPTION_ACCESS_VIOLATION`,而且两次读到的都是 `0x14`
(即 `TESForm::formID` 的偏移)附近那个**不可能是地址**的数:

```
# A（三次）skee64.dll+0x81be8                  mov eax, [r14+0x14]   target 0x15
# B（一次）  SkyrimTogetherRuntime_1_5.dll+0x16CFA8  mov eax, [r15+0x14]   target 0x14
```

**A:RaceMenu 的接口按名字取,布局没人核对。** 插件发 `kMessageExchangeInterface` 拿到
`IInterfaceMap`,再 `QueryInterface("Overlay")` —— 这一步**只按名字查表,不校验布局**。
实测本机的 `skee64.dll` 里 `.?AVOverlayInterface@@` 的 vtable 是**版本 1**(23 槽),
而两个插件声明的是**版本 2**:插件以为槽位 `[11]` 是 `GetOverlayCount`,
DLL 那里其实是 `RevertHeadOverlays`。于是 `GetOverlayCount(Spell, ...)` 把
`rdx = 1`(枚举值 `OverlayType::Spell`)当成 `TESObjectREFR*` 传进去,
读 `[1+0x14]` = `0x15` → 闪退。

这也解释了**为什么能进游戏**:四次 `Normal` 调用的 `rdx = 0`,
而 v1 的 `RevertHeadOverlays` 开头恰好有 `testq %rdx,%rdx; je`,
所以它们**全部安全返回**;只有 `Spell` 才踩中 —— 正是「玩一小会儿才崩」。

**修法**:两个插件(补丁形式,见 `Code/plugins/patches/`)在采用接口前**先查 `GetVersion()`**,
版本不足就拒绝该接口并打印明确错误,而不是继续调用。`GetVersion()` 在每种布局里都是槽位 `[1]`,
是唯一能在调用其它方法之前安全调用的方法。`IBodyMorphInterface` 只追加,所以版本 4 的声明对版本 5 仍有效,
门禁写成 `>= 4` 而非 `== 5`。

**B:引擎用空指针调用装备钩子来清空槽位。** `Code/client/Games/Skyrim/EquipManager.cpp` 的六个钩子里,
只有 `apActor` 判了空。法术/龙吼四个直接读 `apSpell->formID` / `apShout->formID`,
而**引擎清空一只手时就是用空形式调用它们**的 → 读 `[0+0x14]` → 闪退。
(物品那两个钩子的 `pSlot` 早就写了 `?: 0` 三元写法,说明这个坑当时知道,只是没推广过去。)

**修法**:六个钩子统一判空,且为空时**原样转发给原函数**而不是返回 `nullptr`
(返回 `nullptr` 会让引擎以为「这次装备被取消」,与清空槽位的行为不符);
`pEquipSlot` 同样补上 `?: 0`。

### 新增门禁:SKEE 接口声明的顺序与版本

新增 `Code/plugins/tools/check_skee_abi.py`(已接入 `plugins.yml`):
把两个插件声明的 SKEE 方法**顺序**逐槽与 RaceMenu 的真实顺序比对,并要求每份声明都带
版本门禁。**反向验证过三种改坏方式**,每一种都会被它抓到:删掉 `RevertOverlay`、
对调 `GetOverlayCount`/`GetOverlayFormat`、移除版本门禁。

**顺带修掉门禁自己开的一个洞**:门禁**拒绝**接口时只是把指针置空,并**没有**让所有人停止使用它。
`MorphSyncService::Start()` 在 BodyMorph 接口不可用时确实直接返回,所以同步线程不会启动 ——
但 `main.cpp` 里**传输层是独立启动的**,`Start()` 失败不会回滚它,于是远端包照收,
`HandleMorphPacket()` → `TryApplyRemote()` 的漂移分支仍然会执行 `_bodyMorph->ClearMorphs()`
等四个调用,**全是没判空的解引用**。这条链上 `CaptureMorphs()` 和 `TickOnGameThread()` 都判了空,
只有「网络上来的那一条」没判 —— 于是它看起来像已经修好了。
MorphSyncTogether 侧补上这一处判空;
**OStimTogether 侧查过了,不需要改** —— `RaceMenuOverlayBridge` 里每个取用缓存接口指针的函数
(如 `ApplyRemoteOverlayChunk` / `RefreshLocalOverlayGeometry`)开头都有
`if (!overlay || !overrides) { ... return; }`,被拒绝的接口根本走不到调用点。
判空按「四个调用是一笔事务」处理:进门就返回,
而不是只做其中一部分把代理留在改了一半的形态上。

**被拒绝之后会掉什么功能** —— 实测本机那份 `skee64.dll`(FileVersion 4.0.0.0,2020-10-04)的
`GetVersion()`:`BodyMorph` 报 **4**(门禁要求 `>= 4`,**通过**),`Overlay`/`Override` 报 **1**(要求 `>= 2`,拒绝)。
所以**身体/面部 morph 同步照常工作**,只是覆盖层那一半(阴毛/体毛/面部覆盖层)不再同步;
两个被拒绝的接口各打一条 `error` 日志说明原因。要不要写 v1 兼容层、以及为什么不该写,见 `docs/PITFALLS.md` §37.6。

> 详细取证(反汇编、`target address` 的读法、两个布局的逐槽对照、拒绝接口之后的残留解引用)见 `docs/PITFALLS.md` §37。

### 修复:自定义种族的头不显示(任何「头不是本机生成的」角色)

**现象**:用**自带头部 `.nif`/`.tri` 的自定义种族**时,头是**隐形**的 ——
没有用 RaceMenu 雕刻、没有套预设、没有动任何滑块,只是把种族自带的头部网格直接用上。
报告者补了一条关键线索:**同一套自定义头部网格配原版种族是正常显示的**
(Nolvus v6 给原版种族换的就是 HPH High Poly Head + Expressive Facegen),
而这一切在 **STR 1.7.1.42 上都是好的**。所以既不是 `.nif`/`.tri` 本身的问题,也不只是「自定义种族」四个字,
而是**「这张脸不是在本机生成的」**。

**根因:上游 #764 删掉了「强制运行时生成脸」那一行。**

引擎用 `bUseFaceGenPreprocessedHeads`(默认 **1**)决定角色的头从哪来:
从磁盘上预先烘焙好的 `Data\meshes\actors\character\FaceGenData\FaceGeom\<插件>\<formid>.nif` 加载,
还是**当场生成**。远端玩家的外观是**序列化后传过来、在本机重建**的,
它描述的那张脸**从没在本机生成过**,所以磁盘上**不存在**匹配的烘焙网格 ——
引擎照默认路径去找、找不到,头就是空的。

自定义种族踩得最狠,因为**它的头只以种族自带的 `.nif`/`.tri` 形式存在,从来没有 FaceGenData**;
而原版种族即使换了自定义头部网格,那些烘焙资源仍然对得上,所以「原版种族 + 自定义头」正常 ——
这正是报告里「vanilla races with custom head meshes work fine」的由来。

1.7.1 之所以是好的,是因为它**每帧**都在执行:

```cpp
// Every frame make sure we won't use preprocessed facegen
POINTER_SKYRIMSE(uint32_t, bUseFaceGenPreprocessedHeads, 378620);
*bUseFaceGenPreprocessedHeads = 0;
```

上游 PR #764(`7e09f8b4`,`Now respects bUseFaceGenPreprocessedHeads flag.`)把这行删了,
为的是修**原版角色**的脖子接缝、黑脸与头部错配(v1.8.0 发布说明:
`Re-enabled bUseFaceGenPreprocessedHeads; this fixes occasional mismatched heads,
face discoloration and facegen mods`)。
**本仓库每个 1.1.x 都含这个删除,而报告者说好的 1.7.1 不含** —— 版本边界与报告完全吻合
(`git merge-base --is-ancestor 7e09f8b4` 对 v1.1.0/1.1.1/1.1.5/HEAD 为真,对 v1.7.1 为假)。

仓库里其实**一直躺着一个专为此写的函数**:`Code/client/Games/Skyrim/Actor.cpp` 的
`Actor::QueueUpdate()` —— 它把该 INI 置 0、调用引擎的 Actor 更新、再把值放回去,
正是「按需强制一次运行时生成」。但它**从未被任何调用点引用**
(1.0.x 到 1.8.x、以及上游 master/dev 全部 ref 都 grep 过,只有它自己的定义),
因为当年走的是「每帧全局清零」那条路。

**修法:把「清零」收窄到真正需要它的角色上。**

`CharacterService` 新增一个**窗口**:

- `SetRemoteFaceGenForce(true)` 在**两个**「外观来自网络」的生成路径上、
  **在把 actor 交给引擎之前**调用(引擎要到**下一帧**才建身体,晚一步就晚了);
- 它先把**当前值存下来**,关窗时**存回原值**,不写死成 1 ——
  这个设置不是我们拥有的,玩家自己关过就得保持关着;
- `UpdateRemoteFaceGenWindow()` **每帧**从 `WaitingFor3D` 推导窗口该不该开:
  只看 `FormId == GameId{}`(即 base 是**在本机从缓冲区重建**的)那些角色。
  用「推导」而不是「配对开关」是故意的 —— 断线、生成失败、角色被删都会自动掉出这个视图,
  不需要谁记得去关,与它下面那个早就在等同一个组件的 `RunSpawnUpdates` 一致。

于是**两边都满足**:本机生成的角色(玩家的,以及所有磁盘上就有烘焙头的 NPC)
继续走 #764 想要的预处理路径,脖子接缝那个修复不被回退;
只有**头从网络来**的角色改用运行时生成 —— 也就是那些隐形的头。

### 新增门禁:预处理头部开关的作用域

新增 `Tools/Scripts/check_facegen_scope.py`(读源码前**先剥注释**),查五件事:

1. **全局清零不准回来** —— 它正是 #764 回退掉的东西,
   在 `TiltedOnlineApp.cpp` 里重新加回会被抓(那里长的注释里提到该设置不算);
2. **每个写这个开关的地方都必须「先存后还原」** —— 包括那个没被调用的 `Actor::QueueUpdate()`,
   它是个正确的存/还原,门禁要求它保持正确;
3. **窗口必须按「来自网络」定界**(等 3D + 空的 FormId),否则不是恒开就是恒关;
4. **窗口必须在 `Actor::Create` 之前打开** —— 身体是后面那一帧才建的;
5. **每帧必须关窗**,以及**取 INI 前必须判空**。

**反向验证过十种改坏方式**,每一种都被抓到:重新加回全局清零;
两处生成路径各自把开窗挪到 `Actor::Create` 之后;去掉每帧关窗;
把窗口放大成「所有等待中的角色」;把 `FormId` 比较反过来;
从视图里去掉 `RemoteComponent`;去掉 INI 判空;两个「先存后还原」各丢掉一次还原。

> 其中「把 `FormId` 比较反过来」**最初逃过了第一版门禁** —— 那一版只查标记是否存在、
> 不查它是不是那个比较,已改成用正则校验比较本身。改坏方式能被自己写的门禁漏掉,
> 只有真去改一遍才会发现。

> 详细取证(上游 #764 的完整正文、`merge-base --is-ancestor` 逐个 tag 的版本边界、
> 以及为什么 tint 纹理那条路不是原因)见 `docs/PITFALLS.md` §38。
### 新增:OStimNG 作为 OStim Together 的前置 mod 纳入本仓库结构

OStim Standalone 的源码以 `plugins/OStimNG`(子模块,`3954683`)进入本仓库,并在
`Code/plugins/plugins.json` 的 `prerequisites` 中登记——它不是随包插件,而是
OStim Together 依赖的运行时。

**为什么是"前置"而不是"打包"**:OStimNG 自己的 `data/` 是 256 MB 的动画、贴图与音效
(974 个 `.hkx` 153 MB、338 个 `.dds` 71 MB),本仓库既不构建、也不打补丁、更不出货。
把源码纳入结构是为了**钉住版本、可打补丁、可校验**,不是为了替玩家分发资源。

**快照只备份代码**:`snapshotScope` 在清单里显式声明,只备份 `skse/`、`flash/` 等
700 个文件 2.4 MB。是否备份由"本项目是否消费它"决定,而不是由一个体积阈值决定。

### 新增门禁:`check_ostimng_versions.py`——"1.5.x 到 1.7.x"从声称变成校验

OStimNG 通过 SKSE 地址库解析引擎地址,而 `REL::ID()` 在 id 缺失时**不是软失败,是
加载即中止**(CommonLibSSE-NG `REL/IDDB.cpp` 的 `report_id_lookup_failure`)。
所以"支持 1.5.x 到 1.7.x"是关于**随包地址库里 id 覆盖率**的断言,本门禁逐条核对:

- `RELOCATION_ID(se, ae)` 的 SE id 必须在**每一个** pre-AE 地址库(10 个,含 1.5.97)里;
- 其 AE id 必须在**每一个** AE 地址库(14 个,含 1.6.1170 / 1.7.104)里。

当前 33 个 `RELOCATION_ID` 站点全部通过。**反向验证过**:把任一 id 改成不存在的值,
门禁 exit 1 并指名文件与行号。

### 修复:OStimNG 在 1.5.80/1.5.97 上会改坏一条 6 字节指令

`GameHooks.h` 的三处 hook 用 `write_call<5>` / `write_branch<5>`,它们**只覆盖 5 字节**,
因此只有当站点本身是 5 字节 rel32 分支时才安全。实测 1.5.97 的 `SkyrimSE.exe`:

| 站点 | SE 偏移 | 首字节 | 真实指令 | 结果 |
| --- | --- | --- | --- | --- |
| IsThirdPerson | `+0x132` | `E8` | `call rel32`(5B) | 正常安装 |
| **GetHeading** | `+0x152` | `FF` | **`call [rax+0x520]`(6B)** | **改坏** |
| PackageStart | `+0x47` | `E9` | `jmp rel32`(5B) | 正常安装 |

`write_call<5>` 在 GetHeading 处会把 6 字节指令劈开:末尾 1 字节 `00` 变成孤立尾巴,
而且它用 `disp32` 中间四个字节反推"原函数地址",得到 `0x63a8e7`——一个纯属巧合落在
`.text` 里的地址。

**修法**:打补丁后每处先核对首字节是否等于期望的 opcode,不符则**跳过该 hook 并 warn**,
而不是写进去。补丁落在 `Code/plugins/patches/OStimNG/`(子模块不可推送,CI 又用
`--force` 检出,工作区改动会被丢弃)。已用真实 1.5.97 二进制验证:GetHeading 跳过,
另两处照常安装。

### 修复:OStim Together「没激活」——日志里现在会指名缺 OStim

**根因不是配置,是前置根本没装**。实测该安装的 `skse64.log` 里**只有
`OStimTogether.dll`,没有 `OStim.dll`**;MO2 的 21 个 mod 里没有任何 OStim;
`OStimTogether.log` 逐条记录 `OStim.dll module not found` 与十余条
`unavailable`/`missing`,而插件本身照常 READY。

也就是说:"没激活"的观感,来自**插件加载成功、但每个子系统都自行关闭**,且没有任何
面向玩家的说明。三处修复:

1. 缺 OStim 时改为 `critical`,并写明要装 OStim Standalone、要确认 `OStim.dll` 与
   `OStim.esp` 已启用;
2. FOMOD 向导里 OStim Together 的类型改为**依赖型**:检测 `SKSE/Plugins/OStim.dll`
   ——`Active` → Recommended,`Inactive` → CouldBeUsable(装了没勾),缺失 → NotUsable;
3. 描述文字在两种语言下都点名"需要先安装 OStim Standalone"。

### 修复:七个插件的联机同步"装上就是死的"——门面不再只看 ini

出货 ini 把 `STRBridgeModule` 指向桥接,而桥接在本框架内**结构性地收不到任何包**:
它的接收端在 `SkyrimSE.exe` 自己的分配区里找 `TransportService` 的 RTTI,而本框架的
`TransportService` 住在独立模块里,于是解析器永不就绪、每次 `send()` 返回
`kNotConnected`。本次实测再次确认:该安装的 `STRPluginMessagingBridge.log` 反复打印
`receive resolver: TransportService RTTI type descriptor not uniquely resolved`,
而 `OStimTogether.log` 记录 `backend=str-bridge knownPeers=0`。

**修法**:给门面打补丁——在读取 ini 指定模块**之前**,先探测**已加载**的框架运行时
(两个版本名都试),探测到就用它。关键约束:**只用 `GetModuleHandleW`,绝不用
`LoadLibraryW`**。两个运行时是不同 ABI,且任何安装后两者都躺在游戏根目录,盲目
`LoadLibrary` 会把跨 ABI 映像拉进进程(这正是 §32.6 记录过的风险)。探测不到时行为
与原先完全一致,独立桥接场景不受影响。

该行为已由 `check_transport_compat.py` 的新断言绑定:文档必须描述"已加载运行时探测",
补丁必须同时提到两个运行时名,且**新增代码里不得出现 `LoadLibraryW`**。三种回归都
反向验证过。

## v1.1.5(2026-09-27)

### 文档约定:已修的缺陷不再写进 README

两个 README 里那节「曾经误报为冲突、现已兼容:KiLoader / KreatE」**已删除**,
同时把这条约定写进 `CODE_GUIDELINES.md`(新增「Where a fix is explained, and
where it is not」一节),以后照此办理。

**为什么删**:README 回答的是「装哪个版本、什么能用、出问题怎么办」。
一个**已经修好**的缺陷不属于这三者 —— 修复出货之后读者没有任何可做的动作,
于是那一段只会被读一次,然后永久占着位置,把玩家真正要看的内容越推越远;
更糟的是它会**过时成一段关于没人再跑的构建的描述**。

**以后放哪里**:

| 内容 | 去处 |
| --- | --- |
| 缺陷已修(包括被广泛误报的、用户点名问过的) | `CHANGELOG.md`;细节值得留就进 `docs/PITFALLS.md` |
| 玩家必须**动手**的变化 | README:要装的版本、支持的游戏版本、**仍然存在**的冲突、安装步骤、该看的日志 |
| 已修缺陷在 README 的痕迹 | 至多版本表里一行 |
| 「不要启用这个 Mod」清单 | 只列**仍然冲突**的;修好就删掉,不留作历史 |

**判据**(写之前先问):*这个修复出货之后,读者会因此做什么不一样的事?*
答案是「没有」,就写进 CHANGELOG,不写进 README。

> 顺带把 1.1.5 那一行从「修复叙事」压回一行摘要:版本表是索引,不是事故报告。
### 修复:插件补丁里的 fmt 占位符写错,导致 CI 编译失败

**现象**:`Build windows` 与 `Playable Skyrim Together Build` 两个工作流都红,
标注为 `companion plugin build failed: DAVSyncTogether (build)`,编译错误是
`DAVConfigIndex.cpp(109,17): error C7595: 'fmt::v12::fstring<...>::fstring': call to
immediate function is not a constant expression`。

**根因**:错在**本仓库自己的补丁文件**,不在插件源码里。
`Code/plugins/patches/DAVSyncTogether/0001-config-index-enumeration.patch` 里那两条
日志的占位符写成了 `path=\"{\"} \" error=\"{\"}` —— **转义引号写进了花括号里面**。
C++ 编译器看到的是 `path="{"} " error="{"}`;`fmt` 在**编译期**解析格式串时,
把花括号里的 `"` 当成**格式说明符**,而 `std::string` 没有对应它的 formatter,
于是直接编译失败。写成 `path=\"{}\" error=\"{}\"` 即两个正常占位符,问题消失。

**为什么 CI 挡不住、本地也看不见**:

- `git apply` 只看补丁**能不能打上**。这条补丁语法完全正确、上下文完全匹配,
  `PATCH VERIFY OK` 一路绿灯 —— 它根本不知道打进去的 C++ 编不过;
- 本地没有 Windows SDK 与 vcpkg,`fmt` 的编译期检查跑不起来,
  所以这个错误**只在 Windows 构建里、十几分钟之后**才现身。

**修法**:补丁里两行占位符改回 `\"{}\"`。**补丁语义一字未动**,
只是把转义引号移到花括号外面;`git diff` 恰好 2 行。

**顺带加了一条门禁**(`Code/plugins/tools/plugin_patches.py`):`verify` 与 `check`
现在会**逐行扫补丁的新增行**,凡是花括号里出现引号的占位符一律报错,并直接指出
正确写法。规则刻意收得很窄 —— 真正的格式说明符是宽度/精度/类型字母,
引号出现在里面**必然是转义写错了**,所以不会误伤合法写法。反向验证过:
把补丁改回原来的写法,门禁立刻报出两处(第 25、34 行)。

> **教训**:补丁能应用 ≠ 补丁编得过。`git apply` 的绿灯只证明文本能对上,
而**唯一的真值来源是编译器**。这类「本地绿、远端红」的坑,
要么在本地补齐工具链,要么把判据写成门禁 —— 这次选了后者,
因为它是**可判定**的:花括号里不该有引号。

### 修复:KiLoader / KreatE 的「不兼容」弹窗(它们本来并不冲突)

**现象**:装了 KiLoader(以及依赖它的 KreatE / AELAS)之后,启动游戏弹
`KiLoader initialization failed`,细节是两条 `Couldn't open logging file`,
第二条 `std::system_error: The process cannot access the file because it is being
used by another process`。看起来像本框架与 KiLoader 不兼容,上游也有同样的
报告([TiltedEvolution#766](https://github.com/tiltedphoques/TiltedEvolution/issues/766),
至今 open,回复是「无法承诺支持 KiLoader」)。

**根因**(证据来自报错文本本身,不是推测):

1. 报错里的进程名是 **`KiLoaderTPProcess`** —— 那是**本框架的 CEF 辅助进程**
   `TPProcess.exe` 的名字,不是游戏进程。也就是说 KiLoader 是在**我们的辅助进程里**
   被拉起来的;
2. 为什么会被拉起来:`TPProcess.exe` 位于**游戏根目录**,Windows 解析它的导入表时
   **先搜 exe 所在目录**,再搜 `System32`;而 `libcef.dll` 按名字延迟加载
   `dxgi.dll`、`d3d11.dll`、`d3d12.dll`、`dcomp.dll`(自写 PE 解析读延迟导入表确认)。
   这四个正是 ENB、ReShade、SpecialK、Community Shaders 用来挂代理的名字,于是
   辅助进程加载的是**游戏根目录的 ENB 代理**,ENB 再带起 `enbseries\` 下的卫星 DLL,
   `KiLoaderSatelliteENB` 随之拉起一份 KiLoader;
3. 为什么那一份必死:KiLoader 的日志路径按**当前进程名**推导,于是它去开
   `Data\KiLoader\KiLoader.log` —— 而游戏进程**整场**都占着这个文件
   (报错第一行的 `AppData\Local\KiLoaderTPProcess\Logs\KiLoader.log` 同样来自
   进程名)。同一个框架的两份实例无法共用该文件,第二份在启动阶段即失败。

所以这**不是**功能冲突,而是辅助进程误加载了不属于它的 DLL,再由 KiLoader 如实报出
了这个碰撞。

**修法**:`Code/tp_process/main.cpp` 在 `CefExecuteProcess` 之前,按**完整路径**加载
`System32` 的 `dxgi.dll`、`d3d11.dll`、`d3d12.dll`、`dcomp.dll` —— 这四个既是
`libcef.dll` **延迟导入表**里的名字(直接读 PE 得到,其余延迟导入都是 USER32/SHELL32
这类不可能被 mod 占用的系统名),也正是图形框架会占用的代理名。加载器在搜索任何目录
之前会先按**基名**匹配已加载模块,因此随后的按名加载(延迟导入正是按名)直接命中它们,
不再落到游戏根目录。

两个细节是**实测**出来的,不是照抄:

- **顺序必须 `dxgi` 在前**:`System32\d3d11.dll` 静态导入 `dxgi.dll`,先加载 `d3d11`
  会让它自己的依赖解析到游戏根目录的代理,加载失败并报 `ERROR_BAD_EXE_FORMAT (193)`。
  本机用一份同名的假代理复现:先 `d3d11` → 193;先 `dxgi` → 两者都正确落到 `System32`。
  (`d3d12.dll` / `dcomp.dll` **不**导入 `dxgi`,这点也实测过,所以只有前两个的相对顺序
  是硬要求);
- **不能改用 `SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32)`**:本框架自己的
  overlay 运行时(`libEGL.dll`、`libGLESv2.dll`、`vk_swiftshader.dll`、
  `d3dcompiler_47.dll`)同样部署在游戏根目录并**按名**加载,收紧搜索路径会把
  「辅助进程误加载 ENB」换成「overlay 起不来」。只钉这四个名字即可,
  因为 `libcef.dll` 的延迟导入表里,只有这四个是第三方会占用名字的图形 DLL。

**未走的路(以及为什么)**:把 `d3d11.dll` / `dxgi.dll` 从游戏根目录删掉、或让 ENB
改用别的代理名,都是在要求玩家改自己的 mod 列表;给辅助进程换一个不在游戏根目录的
路径,则要动 CEF 的 `browser_subprocess_path` 与打包布局,收益不抵风险。

> **教训**:「两个 mod 不兼容」这个结论,在**报错里的进程名不是游戏进程**时就要先怀疑。
> 本例里 `KiLoaderTPProcess` 这一处字样已经指出肇事者是我们自己的辅助进程;
> 顺着「谁在这个进程里、它为什么在这里」查,比顺着「KiLoader 和谁冲突」查快得多。

### 版本策略:把 **1.1.1** 指定为稳定版(推荐版本回到 1.1.1)

两个 README 的横幅推荐版本改为 **1.1.1**,并在版本表里用
`**1.1.1** ⭐ **稳定版**` 单独标出;其余版本行**取消加粗**,保持普通样式,
让「哪一个是稳定版」一眼可辨。中英文两版同步。

> **这一条与 §35 的记录并不矛盾,是同一个判据的两种用法。** §35 里门禁抓到的
> 「推荐版本落后两版」,指的是**无人维护造成的漂移**:横幅停在 `1.1.1`、
> 而最新 tag 已经到 `1.1.4`,谁也不知道该装哪个。现在横幅回到 `1.1.1`,
> 是**有理由的选择**,并且这个理由写在横幅里、由门禁绑住(见下):
> `1.1.2 ~ 1.1.4` 照常出货、照常可联机,只是**未被指定为稳定版**。
> 两者的区别不在数字,在于**说不说得清为什么**。

门禁相应扩了 `recommended-version` 这条(没有新开一条检查):横幅指定的推荐版本,
必须**正是**版本表里标了 ⭐ 稳定版的那一行。这样「横幅说 1.1.1」与「表格标 1.1.1」
不会各自漂移,也顺带保证表里**恰好只有一行**是稳定版 —— 两行都标、或一行都没标,
都会红。四条分支(横幅与标记不一致 / 两行都标 / 一行都没标 / 中英各自不一致)
都做了反向验证。

同时把版本表行的识别放宽到允许数字后面跟稳定版标记(`**1.1.1** ⭐ **稳定版**`),
但**只允许这一个标记**:第一版放宽成「数字后面跟任意内容」时,
`| 1.5.3 ~ 1.5.97(老 SE) |` 这类**游戏版本表**的行也会被当成版本表行,
于是缺行可以用无关的行蒙混过去 —— 正是这条检查存在的意义被它自己绕开了。
已收紧为只匹配 `⭐ **稳定版**` / `⭐ **stable**`,并做了反向验证。

### 文档与约束:让"文档更新"成为可检查的规则

`CODE_GUIDELINES.md` 新增一节 **Documentation is part of the change**:
**每次版本修改必须在同一个提交里更新文档**,并且发 tag 之前必须先过一遍
`Tools/Scripts/check_docs.py`。光写规则没用,所以规则配了一条门禁
(已接入 `plugins.yml`,并在 `docs/RELEASE-AND-MO2.md` 的发版步骤里前置)。

新增 `Tools/Scripts/check_docs.py`,四项检查:

| 检查 | 断言 |
| --- | --- |
| `coverage` | 所有引用 1.5.x 覆盖率的文档都必须引用**当前**数字;历史口径必须显式标注"已被取代/当时" |
| `version-tables` | 两个 README 的版本表必须覆盖 CHANGELOG 的每一个版本,且两者**互不矛盾** |
| `tags-have-sections` | 每个已打的 tag 都必须在 CHANGELOG 里有对应小节 |
| `recommended-version` | 推荐版本必须真实存在(有 CHANGELOG 小节),中英一致 |

**它立刻抓到了四处真实问题**,都已修:

- **`v1.1.3` 打了 tag 却没有 CHANGELOG 小节** —— 该版本的内容只存在于两个
  README 的版本表里,CHANGELOG 里既没有 `## v1.1.3` 也没有任何小节。
  已补写完整小节;
- **`v1.1.4` 在两个 README 的版本表里都没有行** —— 版本表停在 1.1.3;
- **覆盖率数字是旧口径**:`README.md`、`README_EN.md`、`Tools/ida/README.md`、
  `docs/LAN-RADMIN-GUIDE.md` 都还写着 3080/3069/11 个,而生成器给的是
  **3082/3069/13 个**;
- **推荐版本落后两个版本**:横幅写着 `1.1.1`,而最新 tag 是 `1.1.4`。

另外把"未映射 id 是**函数**还是**数据**"这条区分写进了两个 README 与
`docs/LAN-RADMIN-GUIDE.md`:函数 id 的桩"调用即返回 0"可以安全调用,
数据 id 不能走同一条路(桩是可执行内存,当数据读会读到机器码)。
这正是 §34 那处修复背后的判据,值得让读者也看得到。

门禁本身写错过两次,**两次都是自测发现的**,都已修正:

- 第一版只检查"文档里出现过正确数字",于是**同一文档里新旧两个值并存时它会通过**
  ——正是它要防的漂移形态。改为枚举文档中**所有**覆盖率形状的数字逐个比对;
- 第二版又太紧:把 `Tools/ida/README.md` 里**有意保留的历史口径**
  (已标注 `Superseded`)误报,还把 `3698/3698 offsets validated` 这个
  **另一个指标**误判成 id 覆盖率。改为"历史口径必须显式标注为历史",
  并排除两边相等的 pair。

还有一处**通过得太容易**的坑:该检查依赖 `git tag`,而 `actions/checkout@v4`
**默认不取 tag**,那样它会跑零次并报告成功。已在 CI 侧显式加
`fetch-depth: 0` 与 `fetch-tags: true`,并在门禁侧加了"一个本仓库的 tag
都看不见就直接报错"的守卫。

顺带记下一个此前没人写下来的事实:**tag 命名空间是共享的**。上游与本仓库
都用 `v1.1.x`,且指向不同提交(本仓库 `v1.1.0` = `03cf2cbb`,
上游 = `fd8748f0`),所以"这个 tag 是不是我们的"必须显式列出系列
(`OWN_TAG_SERIES`)而不是靠名字判断。

### 同步上游 #901:Havok 控制器时间步长(未发布,来自上游 dev)

上游 `dev` 上有 2 个 v1.8.2 之后的提交,其中 `fbf72883`(#901)修的是
**远端 actor 的 Havok 控制器时间步长为 0** 导致的物理污染:

远端 actor 跳过原生移动处理,新建的 controller 因此停在 `deltaTime = 0`。
插值位置更新仍会重置速度,**除以 0 产生非有限加速度**,再传播进接触质量修正 ——
表现就是"物体和 NPC 碰撞后、踩到散落物品后**异常滑行**"。修法是两条:
在远端移动处理里刷新 controller 时序,以及在强制位置更新前刷新、拿不到有效步长时
推迟 controller 定位。涉及 7 个文件(3 个新增)。

**1.5.x 适配(两处,都不是猜的)**:

- 新增的 3 个文件里,`hkStepInfo` 是**纯数据布局**,与版本无关,原样照搬;
  `bhkCharacterController::stepInfo` 的 **0x80 偏移是实测出来的**,不是照抄上游:
  本机有一份真实的 1.5.97 `SkyrimSE.exe`,在其 `.text` 里搜"加载物理步长全局 +
  求倒数 + 通过同一寄存器写 +0x88 与 +0x8C"这一模式,**全镜像只有一处**
  (RVA `0xDBFDD4`,所属函数 `0xDBF630`)。该 RVA 在
  `version-1-5-97-0.bin` 里是真实符号(id `76436`),它读的全局
  (`0x1E083A8`)也是(id `512261`)—— 两头都有名字,不是插值猜出来的。
  0x88/0x8C 即 `deltaTime`/`invDeltaTime`,所以 `stepInfo` 就在 0x80,
  与上游对 1.6.x/1.7.x 的断言一致,**该成员在两代之间没有移动,故不像 `Actor`
  那样需要版本分支**;
- 两个新 id 在 **1.5.97 上都没有映射**(已核对:`39856`、`389089`;
  同一对 id 在 1.6.317/640/1170/1179 与 1.7.99/104 的库里**全部有映射**,
  所以 1.6.x/1.7.x 走的就是上游原路径)。两者性质不同,处理也不同:
  - `39856`(`AIProcess::GetCharController`,`AIProcess.cpp:14`)是**函数 id**。
    `VersionDb.h:60-62` 明文写着桩的契约是"调用它返回 0 而不是崩",
    所以按本仓库既有写法用 `POINTER_SKYRIMSE` + `ThisCall` 是**正确且一致**的,
    未映射时自然得到 `nullptr`,两个调用点都判空跳过刷新;
  - `389089`(物理步长那个 **float 全局**,id 是**数据 id**)**不能**这样写:
    未映射时桩是一段**可执行内存**(`48 31 C0 0F 57 C0 C3`),`*stub()` 是
    **把它自己的机器码当 float 读**。实测那几个字节解码为 `1.895e-29`,
    恰好被 `UpdateDeltaTime` 的 `<= 0.0001` 规则挡掉 —— 但那是**桩的编码
    碰巧**,不是契约:换一段解码成大有限值的桩,就会被当成合法步长直接写进
    controller。故改用 `FindAddressById` 显式解析、失败返回 0,与
    `GarbageCollector::Get()`、`LeveledNpcSystem::CanRecordPick()` 处理各自
    未映射数据 id 的做法一致;

**顺带修掉一处我自己引入的空指针面**:上游的 `ForcePosition` 没有解引用
`GetExtension()`,而本仓库的 `Actor::GetExtension()` (`Actor.cpp:285-298`)
在既非 `ExActor` 也非 `ExPlayerCharacter` 时**返回 nullptr**。直接照搬会在
"本来就是要让它更安全的这条路径"上新增一个解引用。已按同文件
`HookSetPosition`(`:201`)的写法判空。

验证:`hkStepInfo` 的退化阶梯用 g++ 编成可执行并**分进程**跑过两种映射状态
(静态缓存"只问一次",同一进程内换映射不会重算,这点也是实测发现的):
1.6.x/1.7.x 有映射 → `deltaTime=0.0166667 / inv=60`;1.5.x 无映射 →
退回移动步长 `0.02 / inv=50`,连移动步长也没有时保留 controller 里已有的值。
另用 g++ 把改过的逻辑连同一份最小 shim 编译运行过,确认签名与调用约定无误。

## v1.1.4(2026-09-26)

同步上游 **v1.8.2**(3 个提交,分叉点 `e0002073`),并按本仓库的多版本逻辑改写了两处
会在 1.5.x 上失效的写法。

### 上游同步

- **#899 按"原始 NPC + 房主选中的模板"重建等级化 NPC**:此前远端角色的 base 被**直接**
  换成房主选中的模板,丢掉了只存在于原始 NPC 上的数据(守卫的名字、默认装备等)。
  现在走引擎自己的 `CreateTemplateActorBase(原始, 模板)`,与单机解析结果一致;
  旧的临时 base 交给引擎的 `GarbageCollector` 回收(与 `RecalcLeveledActor` 同一套策略,
  但**不动**旧实现留下的静态 pick);
- **#893 排除各派系的监狱储物容器**:入狱时没收的赃物/随身物品容器按派系各存一份,
  同步它们等于让一个玩家的物品进另一个玩家的储物箱。现在遍历 `ModManager::factions`
  收集这些容器指针并从对象同步里排除(只比较指针,不读它们的内容);

### 1.5.x 兼容(本次同步的主要风险)

- **`GarbageCollector::Get()` 不再解引用零返回桩**:单例 id `400329` 在 1.5.97
  映射表里**不存在**,而 `POINTER_SKYRIMSE` 对未映射 id 返回的是"零返回桩"——
  照抄上游会变成**解引用地址 0**。已改为直查地址库,未映射返回 `nullptr`,
  调用点判空后保留旧 base(泄漏一个临时 form,不崩);
- **修掉一个上游引入的每帧 Disable/Enable 死循环**:上游用 `GetLeveledPick() == pPick`
  判断"重建完成",而这个值读的是 `SetLeveledCreature`(id `20231`,1.5.97 **未映射**)
  写进去的 extra data ⇒ 比较**永远为假**,`WaitingFor3D` 会每帧重新禁用再启用该角色。
  新增 `LeveledNpcSystem::CanRecordPick()`(只查一次并缓存),不可写时退回本仓库原有路径
  并把完成判定切回 `baseForm == pPick`;
- `TESObjectREFR::SetBaseForm` 更名为上游的 `SetObjectReference`(仅改名,布局不变);
- `TESFaction` 补 `CrimeData` / `ModManager::factions`:偏移不是猜的——
  form 数组自 `0x10` 起、每项 `0x18`、按 FormType 索引,Faction(11)→`0x118`
  与既有 Quest(77)→`0x748` 互相印证,三个 `static_assert` 同时钉住;

### 插件层专项审计

- **服务端可被任意玩家一条聊天命令打崩**:`CommandService::OnSetTimeCommand` 对
  `m_adminSessions` 里每个连接调 `PlayerManager::GetByConnectionId(session)->GetId()`,
  **不判空**。认证流程里连接 id 在 `GameServer.cpp:896` 先进集合,玩家行到 `:977`
  才建立,而 `:947`(mod 策略不符)、`:983`(重复认证)都会提前返回且**不回头清理集合**
  ——集合只在断线时(`:612`)擦除。该集合的其余所有读者都判了空,其中
  `GameServer.cpp:496-502` 还专门为此打印 `"Admin session not found"`。已改为取到即判、
  失败即 `continue`;
- **修正一处会误导部署的文档**:`docs/COMPANION-PLUGINS.md` 声称"出货的 ini 指向框架运行时,
  所以按名字加载门面的插件仍然走原生传输"——与 `976f9702` 之后的实际出货值**完全相反**
  (ini 里是 `STRBridgeModule=STRPluginMessagingBridge.dll`)。已改写为事实,并写明
  七个随包插件**无一例外**都按名字找门面 DLL、因而**都落在聊天隧道桥接上**;
- `check_transport_compat.py` 增加两条断言,把该文档与**实际出货的 ini 取值**绑死
  (文档必须出现 ini 里的真实值;不得再声称指向框架运行时)。已做反向验证:注入这两种
  回归后门禁均 `exit 1` 并指名道姓;
- **已查明但未改动**(插件仓库是钉死的子模块,本仓库不推送,改了进不了 CI 与发布):
  `STRBridgeModule` 只有**一个**键,而运行时按游戏版本有两个名字
  (`SkyrimTogetherRuntime.dll` / `SkyrimTogetherRuntime_1_5.dll`),门面的加载器又是
  精确模块名匹配 —— 单份 ini 表达不了两者。两条候选修法都**需要实机验证**,
  本轮不翻转行为,已连同证据与边界写入 `docs/PITFALLS.md` §32;
- **新增本仓库自持的插件补丁机制**(见下),并用它修掉了
  `DAVSyncTogether` 的一处崩溃:`DAVConfigIndex::Load()` 里
  `recursive_directory_iterator` 的构造落在循环内 `try` **之外**,而 `Load()` 由
  `OnSKSEMessage(kDataLoaded)` 直接调用、`main.cpp` 又没有 handler ⇒
  `filesystem_error` 逃出 SKSE 消息回调即 `std::terminate`。触发条件很平常:
  `Data/SKSE/Plugins/DynamicArmorVariants` **存在但不是目录**(残留的同名文件)。
  已改用 `error_code` 重载,枚举失败只记一行日志;
- **删掉 `IEDSyncTogether` 的 `RelayHost` 子选项**:它唯一的作用是打开旧 UDP 传输,
  而该传输**根本没被编译**(CMake SOURCES 里没有 `UdpTransport.cpp`/`StrTransport.cpp`/
  `SyncService.cpp`/`Config.cpp`),勾了也不会有任何效果——与其留一个装样子,
  不如删掉。同时把"随包的 `IEDSyncTogether.ini` 目前是惰性的"写进清单
  `payloadNote`,免得有人报"我的配置被忽略"。

### 新增:本仓库自持的插件补丁机制

七个插件是**钉死的子模块**,指向本仓库**没有权限推送**的仓库(已实测:
`git push --dry-run` 对 `Caelvanost/DAVSyncTogether` 返回 `403`;两个上游的 HEAD
又恰好等于当前 pin,所以也没有"等上游修"这条路)。而 CI 的
`git submodule update --init --force --recursive --depth=1` 会**丢弃工作树里的一切改动**
—— 于是"在子模块里改一行"这种修法**永远进不了包**。

新增 `Code/plugins/patches/<plugin>/*.patch` + `Code/plugins/tools/plugin_patches.py`
把这条件拆掉:补丁是本仓库的**普通内容**,照常 review、照常随包发布,在插件被
configure/build **之前**应用到子模块工作树上。

| 命令 | 用途 | 在哪跑 |
| --- | --- | --- |
| `apply` | 应用全部补丁(**幂等**:已应用则跳过) | `windows.yml`,紧跟 checkout、在 build 之前 |
| `verify` | 每个补丁都必须仍能打到 pin 上 | `plugins.yml`,与其它门禁并列 |
| `check` | 当前工作树里补丁是否已应用 | 本地 |
| `revert` | 反向应用,回到 pin | 本地 |

`verify` 用 `git apply --check --cached`(对**索引**而不是工作树),所以"已经应用了"
**掩盖不了**"这个补丁已经配不上新的 pin"——这正是重新 pin 时最需要拦住的失误。

同时改了 `plugin_snapshot.py`:快照门禁原来比对**工作树**,补丁一应用它就误报
"snapshot differs"。现在它通过 `git show <pin>:<path>` 读**pin 的那个提交**的内容,
量的仍然是"备份是否还等于被构建的那个提交",而不是"工作树是否干净"。
两种状态(已打/未打)都实测通过,篡改快照仍然会被抓住。

### 清单

- `Tools/missing_1_5_97_ids.txt`:新增三个未映射 id(`20231`、`36460`、单例 `400329`),
  覆盖率 **3069/3080(99.6%)**。顺带修掉生成器 `collect_codebase_ids()` 的两个口径错误
  (`*.[ch]pp` 通配漏掉全部 301 个 `.h`;注释掉的调用被当成活的),旧口径 3075 因此
  既漏 8 个又虚增 3 个;
- `docs/PITFALLS.md` §3.1:记录本次同步的两处判断与查证手段。

## v1.1.3(2026-09-26)

**插件生态落地**:把剩余三个插件按同一配方并入,可选插件由 3 个增至 6 个。

> 本节是**补写**的。`v1.1.3` 当时打了 tag,却没有在 CHANGELOG 里留下任何小节——
> 该版本的内容只存在于两个 README 的版本表里。`Tools/Scripts/check_docs.py`
> 的 `tags-have-sections` 检查把这类"打了 tag 却无人可读"的发布挡住了,
> 本节即由该检查发现后补齐。

- 并入 `AnimSyncTogether`(动画图谱变量/事件同步)、`DAVSyncTogether`
  (Dynamic Armor Variants 外观同步)、`TradeTogether`(玩家间物品与金币交易),
  子模块、契约校验、打包清单、CI 构建、耐久快照、安装向导六处同步更新;
- 删除 FOMOD 里那段**两条指引都是死路**的 OStim 提示(上游既无 release 也无 tag,
  该脚本又从不进包),中英文改为只描述功能;
- 修复**自己引入的发布阻断**:两个插件 pin 的 vcpkg baseline 在 CI 的
  `--depth 1` 克隆里取不到,而 vcpkg 的 builtin registry 路径**没有 fetch 回退**,
  已改为按 manifest 反推 baseline 再逐个 fetch;
- **OStim 的两个 Papyrus 脚本终于能编了**:改用开源编译器
  `russo-2025/papyrus-compiler`(pin tag + SHA-256),补 12 个基础类型 stub
  (`plugins/` 是 gitlink,只能放本仓库),`OSKSE.pex` / `OStimTogetherNative.pex`
  随包出货,**Add Actor 同意门控真正可用**;
- 顺带修掉 `OStimTogether_OCum.esp` **出货却不带脚本**的缺口(补 4 个 `Form` +
  1 个 `Game` stub,`OStimTogetherOCum.pex` 接入同一编译通道),并让打包脚本支持
  **子选项的 artifacts**(原先静默忽略);
- `GameFiles/Skyrim/scripts/source/` 的 10 个出货脚本补上可复现的编译入口,
  其中 `SkyrimTogetherVerifyLaunchScript.psc` 修掉编译器不支持的 `\n` 转义
  (它会**静默产出字面反斜杠**);归档 papyrus-compiler 源码到 `snapshots/tools/` 并加门禁。

## v1.1.2(2026-09-26)

第四轮审计:全仓复查 + 插件层专项,以及**一次真实缺陷的回滚**。

### 稳定性修复(全部为可复现的空指针)

- **服务端可被远程打崩**:同一连接上发第二次 `AuthenticationRequest` 时
  `PlayerManager::Create` 返回 `nullptr`,而调用点直接解引用。已改为拒绝重复认证;
- **`PlayerService` 三个每帧路径**在载入/主菜单(此时没有玩家)解引用空玩家指针:
  `RunRespawnUpdates`、`RunBeastFormDetection`、`RunDifficultyUpdates`;同文件另两处
  本来就判空,这三处漏了;
- **`DiscoveryService::DetectGridCellChange`**:`GetParentCellEx()` 与坐标回退
  **都**可能为空,而下一行直接读 `pCell->formID`。同文件另外两处对同一个调用都判了空;
- **`CalculateHealthPercentage`**:每帧传入 `PlayerCharacter::Get()`,函数内部不判空;
- **`WeatherService`** 三处 `Sky::Get()->` 未判空(其中一处由服务端消息驱动),
  同文件另外两处判了空;
- **`OverlayService`/`PartyService`/`InputService` 五处 `GetOverlayApp()->`**:
  该指针在 `Create()` 之前为空,而组队事件正是连接后立刻会到的;
- **`baseForm` 空指针**:仓库自己的调试视图里就写着 `if (!pRefr->baseForm)`,
  但 12 处日志/分支直接解引用它(含每帧的裸体检查与网络驱动的物体/魔法/角色路径);
- **`EntitiesView` 一个死守卫**:先写 `"UNNAMED"` 又在下一行无条件 `sprintf_s` 覆盖它,
  等于没判。

### 插件层(专项)

- **ProxyResolver 的映射监听器从来没被调用过**:`OStimTogether` 与 `IEDSyncTogether`
  都注册了 `registerListener`,而框架只把回调存起来、从不触发;OStim 的反向映射
  (`_connectionByProxy`)因此永远是空的,而它的注释写着这条映射是 "Required"。
  现在由 `on_construct/on_destroy<PlayerComponent>` 触发 `kAdded`/`kRemoved`
  (`FormIdComponent` 每实体只写一次、拆除时移除,所以这两类事件就是全部迁移);
- **`setLogCallback` 存了不用**:插件装上的回调永远不会被调用。现在框架自己的
  插件层诊断(丢弃超长 channel / 超限载荷)会通过它发出,且**在锁外**调用;
- **`setLocalDisplayName` 存了不用**:如实注明这是**有意不上线**的——
  发送者显示名只能来自服务端认证过的登录名,否则任何插件都能冒充别人;
- **服务端限流桶泄漏**:`m_buckets` 以单调递增、永不复用的 `PlayerId` 为键且从不清理,
  长开的服务器会一直涨。现在随玩家行一起移除。

### 快捷键:只保留 F2

按需求把**除 F2 以外的所有游戏内快捷键**注释或禁用:

| 键 | 原用途 | 处理 |
|---|---|---|
| **F2** | 联机菜单 | **保留**(唯一) |
| 右 Ctrl | F2 的别名 | **移除**(第二个绑定=第二个要维护的东西) |
| F3 | 调试菜单栏 | `#if 0`;`toggleDebugUI` 绑定仍在,变成纯 opt-in |
| F4 | 揭示其他玩家 | 移除按键;`reveal players` 按钮照常工作 |
| F6 | Discord 覆盖层解锁 | 置于 `if (false && …)`,并在注释里写明重新启用覆盖层时不得复活 |
| F7/F8 | 开发快捷方式 | 保持 `#if 0` |

### 版本支持(1.5.x / 1.6.x / 1.7.x)

- 复核 `VersionDb` 三条加载路径:format 1(1.5.x,SE id 经 ae-to-se 映射表翻译)、
  format 2(1.6.x AE)、format 5(1.7.99+ 密集偏移数组);
- **34 个地址库文件还原**(见上),1.5.x 的 10 个 `.bin` + 10 张映射表、
  1.6.x/1.7.x 的 13 个 `versionlib` 全部在位;
- `GamePatch::At` 的按版本偏移与 `legacyMeasuredOn` 前缀校验保持原样(1.5.97 之外的
  1.5.x 不会被套用 1.5.97 的偏移)。

### 上一轮遗留

- **修复地址库文件误删**:上一个提交在搬移 `STRPluginMessagingAPI.ini` 时,
  连带删掉了 `GameFiles/Skyrim/SKSE/Plugins/` 下**全部 34 个**地址库文件
  (`version-*.bin` 10 个、`versionlib-*.bin` 13 个、`versionlib-ae-to-se-*.map` 11 个)。
  打包路径直接吃 `GameFiles/Skyrim/`,少了它们玩家一启动就会"地址库失败"并退出。
  **34 个文件已按删除前的 blob 哈希逐一还原**;
- **`#if 0` 逐块判读**:22 处 → **删 15 留 7**。留下的 7 处都是"改 0 为 1"型开关
  (F6/F7/F8、旧战斗瞄准 ×3、地图菜单、imgui 上游 ×2),判据是**块外有没有为它留位置**;
- **debug 天气开关从未生效**:`Sky` 三个 hook 里的 `s_shouldUpdateWeather` 判据
  全在 `#if 0` 里,开关写了个没人读的变量。判据已启用(该变量只由 debug 窗口写,
  而该窗口在 release 版被裁掉,所以对发布版无影响);
- **EF 哨兵结论写进代码**:查清 SKSE 插件路径**本来就不需要** `_initterm_e` 哨兵
  (SKSE 已先加载 EF,我们的 hook 装在其 thunk 之上),只补注释、不改逻辑;
- **发布冻结解除**:`RELEASE-FREEZE.md` 删除,README 中英横幅同步移除;
  `release.yml` 的 guard 保留(冻结文件仍是唯一开关)。

- **修复地址库文件误删**:上一个提交在搬移 `STRPluginMessagingAPI.ini` 时,
  连带删掉了 `GameFiles/Skyrim/SKSE/Plugins/` 下**全部 34 个**地址库文件
  (`version-*.bin` 10 个、`versionlib-*.bin` 13 个、`versionlib-ae-to-se-*.map` 11 个)。
  打包路径直接吃 `GameFiles/Skyrim/`,少了它们玩家一启动就会"地址库失败"并退出。
  **34 个文件已按删除前的 blob 哈希逐一还原**;
- **`#if 0` 逐块判读**:22 处 → **删 15 留 7**。留下的 7 处都是"改 0 为 1"型开关
  (F6/F7/F8、旧战斗瞄准 ×3、地图菜单、imgui 上游 ×2),判据是**块外有没有为它留位置**;
- **debug 天气开关从未生效**:`Sky` 三个 hook 里的 `s_shouldUpdateWeather` 判据
  全在 `#if 0` 里,开关写了个没人读的变量。判据已启用(该变量只由 debug 窗口写,
  而该窗口在 release 版被裁掉,所以对发布版无影响);
- **EF 哨兵结论写进代码**:查清 SKSE 插件路径**本来就不需要** `_initterm_e` 哨兵
  (SKSE 已先加载 EF,我们的 hook 装在其 thunk 之上),只补注释、不改逻辑;
- **发布冻结解除**:`RELEASE-FREEZE.md` 删除,README 中英横幅同步移除;
  `release.yml` 的 guard 保留(冻结文件仍是唯一开关)。**本次不打 tag。**

## v1.1.1(2026-09-25)

第三轮审计。本轮换了两条新判据(容器迭代中改动、链式解引用),并复核了
前两轮修改本身。

- **链式解引用**:`VisitInteriorCell` 里的
  `PlayerCharacter::Get()->GetParentCellEx()->formID` 在 **load 全程**会取到空
  cell(`VisitCell()` 只在入口判过玩家存在,进内层就不再判);`VisitExteriorCell`
  的坐标回退也可能为空。两处都已补；
- **`Actor::Create`** 取玩家后**连续解引用三次**,已改为取到即判、失败即返回；
- **`DebugService`** 对 `Actor::Create` 的返回值从未判空就连续解引用,
  且 `PlayerCharacter::Get()` 与 `baseForm` 同样未判——一并补上；
- **修正上一轮自己引入的缺陷**:`Actor::Create` 的玩家判空曾被插在
  `New()` **之后**,提前返回会泄漏刚分配的 actor。已改为**先判空、再分配**；
- 容器迭代类判据(10 处)逐个核对后确认**全部安全**:代码库统一采用
  "先收集、后改动"或"循环外 clear"。

## v1.1.0(2026-09-25)

性能与同步的算法级改进,以及两轮全仓库审计。

- **插值改 Catmull-Rom**:远端玩家移动由线性改为三次曲线,线段交界处方向连续;
  丢包时不再"冻结—跳变",改为沿最后速度做**有界外推**(上限 1.25),并对
  播放头落后窗口的情形补了下限 0,避免反向外推;
- **帧循环 16 ms → 8 ms**:`SetTimer` 按 15.625 ms 系统 tick 向上取整,
  16 ms 实为 31.25 ms(约 32 次/秒),8 ms 落在单个 tick 内(约 64 次/秒),
  远端玩家采样率翻倍;
- **移动更新复杂度**:`OnReferencesMoveRequest` 由 O(更新数 × 实体数)
  降为 O(实体数 + 更新数)(先建 serverId 索引再查表);
- **修复重复生成同一远程玩家**导致引擎在 `SkyrimSE.exe+0x23d000` 解引用空指针
  的崩溃;重复守卫补 `LocalComponent` 检查;
- **全仓库 `GetById` 解引用审计**(两轮,判据不同):修复 15 处无守卫解引用,
  含 `Actor::Create` 对玩家的连续三次解引用、`PlayerService`/`PartyService`
  对硬编码全局的写入(断线路径也写,退出时崩)、`VisitInteriorCell`
  在 loading 期间对空 cell 的链式解引用等;
- 热路径日志降级:启动首秒曾写 220 行,现降至 debug。

## 历史沿革

本项目 fork 自 [TiltedEvolution](https://github.com/tiltedphoques/TiltedEvolution),
其 GitHub fork 链为 `tiltedphoques/TiltedEvolution` → `rfortier/TiltedEvolution-rwf`
→ 本仓库。

本文件此前保存的是**上游 2022 年 7 月之前**由工具自动生成的 changelog
(最后一条为 v1.38.3,2022-07-01),与本仓库使用的版本号体系(`1.0.x`)无关,
留在根目录容易让打包/报错的人对不上号,因此移除;需要那段历史可查上游仓库,
或本仓库的 git 历史(该文件在移除此内容前已被完整提交过)。
