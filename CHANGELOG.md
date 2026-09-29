# 更新日志

本项目的更新记录以 **GitHub Releases** 为准:

> https://github.com/komAAmok/SkyrimTogetherNext/releases

每个 tag(形如 `v1.0.20`)对应一个 Release,附上版本号相同的两个包——
客户端 mod 与专用服务器。逐条提交的历史见 `git log` 与各次 PR。

## v1.1.9(2026-09-29)

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
