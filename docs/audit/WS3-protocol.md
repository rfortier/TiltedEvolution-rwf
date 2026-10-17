# WS3 · 网络协议审计（范围收窄）

范围：`Code/encoding/WireLimits.h`(211行)、`Code/encoding/Opcodes.h`(123行)、`Code/encoding/Differential.h`(44行)、
`Code/encoding/StringCache.{h,cpp}`(36/174行)、`Code/encoding/Messages/StringCacheUpdate.{h,cpp}`(25/45行)。
代表性消息三条：**带字符串** `NotifyChatMessageBroadcast`、**带数组** `NotifyPlayerList`、**带浮点** `NotifyProjectileLaunch`。
为回答 Q1/Q2/Q3 必须确认接收入口与工厂，额外只读了 `GameServer.cpp:574-619,689-719`、
`TransportService.cpp:169-279,813-903`、`admin/Transport.cpp:16-41`、
`ClientMessageFactory.cpp`/`ServerMessageFactory.cpp` 全文、`Message.{h,cpp}` 全文、`AuthenticationRequest/Response.cpp`、
`AssignCharacterRequest.cpp`、`PluginMessagingRequest.cpp`、`NotifyPluginMessaging.cpp`、`Mods.cpp`、`Factions.cpp`、
`AnimationVariables.cpp`、`ReferenceUpdate.cpp`、`CachedString.cpp`、`Tints.cpp`，以及两侧的
`StringCacheService.cpp`。
未编译，全部为静态阅读。已核对 `docs/PITFALLS.md`：§14.4(版本握手保持现状)、§45(VarInt 计数夹取)、
§49(切片与游标)、§62.4(`WireLimits` 读后夹取)、§83(StartId 断言改丢弃)、§84(整包上限)、
§106(编码层 noexcept 无 catch) —— 下列缺陷**均不在**这些已记录条目内。

---

## Q1 · 粘包 / 半包：拆包循环不在本仓库

**结论：接收入口拿到的就是「一条完整消息」，`aSize` 是消息长度而不是本次 recv 的长度；
但完成这件事的长度前缀 + 循环拆包代码不在本仓库，无法静态核实。**

证据链（闭合的部分）：

1. 三个接收入口的签名都只接受「一整条」：
   `GameServer.cpp:574` `OnConsume(const void* apData, const uint32_t aSize, const ConnectionId_t)`、
   `TransportService.cpp:208` `OnConsume(const void* apData, uint32_t aSize)`、
   `admin/Transport.cpp:16` 同形。三者都**没有**任何跨调用累积缓冲区、没有「剩余字节」成员、没有
   `while (cursor < size)` 循环 —— 全仓 `OnConsume` 只有这三处定义、零处「再入」调用。
2. 发送侧每一条消息前都塞了一个占位字节，注释写明这是传输层要的：
   `TransportService.cpp:191`、`GameServer.cpp:695`、`GameServer.cpp:711`、`admin/Transport.cpp:77`、`:105`
   全部是 `writer.WriteBits(0, 8); // Write first byte as packet needs it` /
   `// Skip the first byte as it is used by packet`。这只能解释成：**传输层在首字节写帧头（长度前缀），
   并在交付时把它剥掉**，否则 `OnConsume` 里 `factory.Extract` 读到的第一个字节就错位了。
3. 反证「不是流式」：`ClientMessageFactory.cpp:32-53` 与 `ServerMessageFactory.cpp:32-48` 从 reader 里
   `ReadBits(data, 8)` 取 opcode，**读完整个对象就返回**，从不问「还剩多少字节」、也不回写 reader 位置。
   若底层交付的是 recv 块，一次 1.5 个包时第二个包的 opcode 会被当作下一条消息的字段解析，而代码里
   **没有任何地方能发现这一点**（没有 magic、没有长度、没有 end-of-message 校验）。
4. `WithinPacketLimit(aSize)`（`TransportService.cpp:216`、`GameServer.cpp:585`、`admin/Transport.cpp:24`）
   的语义也印证同一点：它判的是「这条消息有多大」。若 `aSize` 是 recv 块大小，这条上限毫无意义。

**所以：一次 recv 拿到 1.5 个包 / 一个包被拆成两次 recv，在本仓库这一层不会发生 —— 前提是
TiltedCore 的 `Client`/`Server` 真的做了长度前缀拆包。这一条无法在本仓库内证实，见「待验证 1」。**

---

## Q2 · 反序列化越界：长度声明**从不**与剩余可读字节比较；WireLimits 是「读完再夹」

**结论：全仓零处「剩余可读字节」检查。`WireLimits` 是读完之后夹取（其头注释自己写明），
而 `resize` 发生在读之前。两者的先后顺序是本条缺陷的核心。**

### Q2.1 没有任何「剩余字节」判据

- `Code/encoding` 全目录没有任何对 `Reader` 剩余长度的调用：全仓 `GetRemaining|Remaining()|BytesLeft|Tell()`
  **零命中**；唯一形似的 `reader.Size()` 在 `Code/client/Games/SaveLoad.cpp:122`，是另一个类型的 reader，
  与网络无关。
- 接收入口把 `aSize` 交给 `ViewBuffer`（`GameServer.cpp:591`、`TransportService.cpp:224`、
  `admin/Transport.cpp:31`）之后**再也不提它**：`ViewBuffer buf((uint8_t*)apData, aSize);` 之后
  `aSize` 在该函数内零次出现。编码层拿到的是一个 `Reader`，它不知道包有多大。
- 因此「`uint16 count` 在读之前有没有和剩余可读字节比较」的答案是：**没有，一处也没有**。
  所有计数都只与一个**编译期常量**比较（`0xFF`/`0x1000`/`kMaxModCount` 等），
  那个常量约束的是「最多分配多少」，不是「包里真的有这么多」。

### Q2.2 WireLimits 的先后顺序，它自己写清楚了

`WireLimits.h:23-27`：

> ```
> // Note the limit of the fix: the clamp runs after the string has been read, so it bounds
> // what the value is used for, not the transient allocation inside the reader. Bounding
> // the packet itself is what kMaxPacketBytes below does, and the only place for it is a
> // receive entry point ...
> ```

即：**`Clamp` 是读后夹取，`kMaxPacketBytes` 是整包上限。两者都不约束「按声明分配」那一次。**
整包上限只把「一次能声明多少」压到 1 MiB（`WireLimits.h:72`），它**不限制包内长度字段的数值**：
一个 20 字节的包仍然可以声明 0xFFFFFFFF 字节的字符串或 65535 条数组。

### Q2.3 最干净的一处：resize 在第一个字节被读之前

`Code/encoding/Messages/StringCacheUpdate.cpp:28-39`：

```cpp
StartId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;   // :28
const auto count = Serialization::ReadVarInt(aReader) & 0xFFFF; // :30
Values.resize(count);                                          // :31  ← 分配已经发生
for (auto i = 0ull; i < count; ++i) { ... ReadString ... }     // :33-39
```

一个 **2 字节**的 `StringCacheUpdate`（`StartId=0`、`count=0xFFFF`）就能让客户端
`resize` 出 65535 个 `TiltedPhoques::String`，随后循环 65535 次去读并不存在的字节。
夹取发生在 `ReadString` 之后（`:36`），对这次分配无效。同一个形状在
`AssignObjectsRequest.cpp:28`、`AssignObjectsResponse.cpp:26`、`NotifyPartyInfo.cpp:36`、
`NotifyPartyJoined.cpp:36`、`ShiftGridCellRequest.cpp:39`、`Mods.cpp:46`、`Factions.cpp:46/:54`、
`QuestLog.cpp`、`Tints.cpp` 都是。

### Q2.4 声明了却没读到的字节：最刺眼的一处

`PluginMessagingRequest.cpp:48-56`（`NotifyPluginMessaging.cpp:44-52` 同形）：

```cpp
const auto size = static_cast<std::uint32_t>(Serialization::ReadVarInt(aReader)); // :48 对端说了算
const auto bounded = std::min<std::uint32_t>(size, kMaxPayloadBytes);             // :49 只夹到 24 KiB
PluginData.resize(bounded);                                                       // :50
for (std::uint32_t i = 0; i < bounded; ++i) { aReader.ReadBits(byte, 8); ... }    // :51-56
```

`size` 声称 24 KiB，包实际只有 10 字节，循环仍然跑 24576 次。`:47` 的注释写着
"Never trust the wire length: a peer can claim more bytes than it sent" —— **代码没有做到它写的事**：
它夹的是「最多分配多少」，不是「实际还有多少」。这正是 Q2 问的那件事。

### Q2.5 三个代表性消息的对称性

| 消息 | 类型 | 写/读字段序列 | 长度校验 |
|---|---|---|---|
| `NotifyChatMessageBroadcast` (`*.cpp:4-20`) | 字符串 | `MessageType`, `PlayerName`, `ChatMessage` / 同序 | 两个 `ReadString` 之后 `:17`/`:19` 各一次 `Clamp`；**读前无任何校验** |
| `NotifyPlayerList` (`*.cpp:11-41`) | 数组(map) | `count`, (`id`, `name`)* / 同序 | `count` 掩到 16 位（`:31`），**但整个函数没有 try/catch**，`Players[id] = username` 的插入在 `noexcept` 里；每项 `Clamp` 在读后 |
| `NotifyProjectileLaunch` (`*.cpp:4-74`) | 浮点 | 8 个 float + 6 个 GameId + VarInt + 7 bool / 同序 | 无长度字段；8 个 float 全部 `ClampFinite`（`:45-47`,`:53-55`,`:63-64`）—— 这一条**是对称且完整的** |

三条都**读写对称**。我把 `Code/encoding/Messages` 与 `Structs` 的 133 个 `DeserializeRaw/Deserialize`
与对应 `Serialize` 做了机械比对：字段序列**零处不对称**；`WriteBits`/`ReadBits` 的位宽序列
**零处不一致**（唯一的差异是局部变量名，如 `ActivateRequest.cpp` 的 `PreActivationOpenState`/`preActivationOpenState`）。
所以 Q2 的问题不在对称性，只在「读之前不判剩余」。

---

## Q3 · 未知 / 越界 opcode，以及版本不匹配

**结论：opcode 处理是正确的（三道判据都齐）；版本不匹配发生在整包解析之后，因此它保护不了字段布局。**

### Q3.1 opcode

- 枚举位宽 8 位（`Opcodes.h:3` `enum ClientOpcode : unsigned char`、`:63` `ServerOpcode`），
  共 53 个 client / 56 个 server 值（`kClientOpcodeMax`/`kServerOpcodeMax` 不计）。
- 第一道：工厂判范围。`ClientMessageFactory.cpp:37-38` `if (data >= kClientOpcodeMax) return {nullptr};`；
  `ServerMessageFactory.cpp:37-38` 同形。越界 opcode（53..255 / 56..255）被直接拒绝。
- 第二道：工厂判「槽位是否为空」。`ClientMessageFactory.cpp:50-51` / `ServerMessageFactory.cpp:45-46`
  显式判空 `std::function`，避免 `bad_function_call` 在 `noexcept` 里 `std::terminate`
  （注释 `:42-49` 逐字说明）。**这一处是修过的，不是缺陷。**
- 第三道：客户端分派判范围与空 handler。`TransportService.cpp:259-271`
  `cOpcode >= kServerOpcodeMax` 与 `!handler` 各有 `spdlog::error` 并 `return`。
- 服务端分派：`GameServer.cpp:610-617` 先判 `!pMessage` 打 `"Couldn't parse packet"` 并 return，
  再 `m_messageHandlers[pMessage->GetOpcode()](...)`。**这一处没有判空 handler**，
  但索引来自消息类自己的 `static constexpr Opcode`（已过范围检查），而 `m_messageHandlers`
  由同一个 `ClientMessageFactory::Visit` 填充（`GameServer.cpp:280`），我逐项比对过
  visitor 列表 53 项 == 枚举 53 项、server 56 == 56，**没有空槽**。故不构成缺陷。
- 失败方向：三种情况都是「丢弃这一包 + 记一行日志」，连接保持。**合理。**

### Q3.2 版本不匹配

- 客户端把 `BUILD_COMMIT` 放进 `AuthenticationRequest.Version`（`TransportService.cpp:352`），
  服务端在 `GameServer.cpp:886-891` 比较不等即 `sendKick(RT::kWrongVersion)`。
- **但这条检查发生在整包已经解析完之后**：`OnConsume` → `factory.Extract`(`GameServer.cpp:610`)
  → `DeserializeRaw` → `HandleAuthenticationRequest`(`:866`) → `:886`。
  也就是说**字段布局不同的两端在版本被判之前就已经按对端的布局读完整包了**。
  这正是 `docs/PITFALLS.md:821-823` §14.4 的结论（「字段增删不体现在任何版本号里」），
  已记录，不重复计为缺陷；这里只确认它在代码中的位置。
- 反向：客户端收到 `kWrongVersion` 时把 `acMessage.Version` 经 `JsonEscape::Escape` 转义后
  拼进 UI JSON（`TransportService.cpp:849-857`），**转义是有的**，无注入。

---

## Q4 · StringCache：字符串表没有任何上限，对端可以撑爆内存

**结论：`m_idToString` / `m_stringToId` 只增不减、无容量判据、服务端从不清空。
服务端被「已认证客户端」撑爆，客户端被「服务端」撑爆。**

### Q4.1 增长点与容量

`StringCache.h:31-33` 三个容器：`Vector<String> m_idToString`、`Set<String> m_wantedStrings`、
`Map<String,uint32_t> m_stringToId`。`StringCache.cpp` 全文**没有任何容量常量、没有上限判断**：
- `Add`(`:26-49`)：`m_stringToId[acValue] = ...; m_idToString.push_back(acValue);` —— 无条件增长。
  只 catch 分配失败（`:44-48`），不判「表已经多大」。
- `AddWanted`(`:51-62`)：`m_wantedStrings.insert(acValue);` —— 无条件增长，同样只 catch 分配失败。
- `Clear`(`:138-143`) 是唯一的收缩点，**服务端零调用点**：全仓 `StringCache::Get().Clear()` 只有
  `Code/client/Services/Generic/StringCacheService.cpp:20` 与 `:25` 两处（ConnectedEvent / DisconnectedEvent）。

### Q4.2 服务端：一个已认证客户端就能无界增长全局表

链条（每一环都有行号）：

1. 客户端发 `ClientReferencesMoveRequest`，`DeserializeRaw`(`ClientReferencesMoveRequest.cpp:25-37`)
   按 `count`（夹到 `kMaxUpdateCount=0x1000`，`:30`）循环，每项 `Updates[serverId].Deserialize`(`:35`)。
2. → `ReferenceUpdate::Deserialize`(`ReferenceUpdate.cpp:35-60`) 再按 `kMaxActionEventCount=0x400` 循环
   `ActionEvents[i].ApplyDifferential`(`:53`)。
3. → `ActionEvent::ApplyDifferential`(`ActionEvent.cpp:150-158`) 在 `kEventName`/`kTargetEventName`
   位置调 `EventName.Deserialize(aReader)`。
4. → `CachedString::Deserialize`(`CachedString.cpp:31-63`) 的 else 分支：`value = ReadString(aReader)`
   （`:57`，夹到 512 字节 `:58`）之后 **`StringCache::Get().AddWanted(*this);`（`:62`）**。
5. → 服务端 `StringCacheService::HandleUpdate`(`Code/server/Services/StringCacheService.cpp:14-38`) 每
   2000 ms（`:17`）调一次 `ProcessDirty()`(`:27`) → `StringCache::ProcessDirty`(`StringCache.cpp:145-156`)
   → 对每个 wanted 字符串 `Add(s)`(`:151`) → 永久进表。

一个 1 MiB 的包（`kMaxPacketBytes` 上限）里可以放约 2000 条互不相同的 512 字节名字，
每次发送都让服务端表长 +2000 且**永不回收**。包大小有上限，**表长没有**。
`kMaxCachedStringBytes=512` 限制的是单条长度，不是条数。

### Q4.3 客户端：服务端同样可以无界增长

`StringCacheUpdate::DeserializeRaw`(`StringCacheUpdate.cpp:21-45`) 每条消息最多 65535 条
（`kMaxValueCount`，`StringCacheUpdate.h:19`），`StringCache::Deserialize`(`StringCache.cpp:108-136`)
只要 `StartId == m_idToString.size()`（`:123`）就全部追加。对端知道它自己发过的 id，
**可以每次都让这个等式成立**，于是每条消息 +65535 条、约 32 MiB（65535 × 512 B）。
客户端表也没有任何上限，且 `Clear` 只在连接/断连事件时触发。

### Q4.4 已修的部分（不重复计缺陷）

`docs/PITFALLS.md` §49（切片 + 游标 32 位）、§83（`assert(StartId == size)` 改成丢弃）都已修复并在本文件核实：
`StringCache.cpp:87` `std::min(m_idToString.size() - aStartId, kMaxValueCount)`、
`:123` `if (aMessage.StartId != m_idToString.size()) return;`、
`StringCacheUpdate.h:23` `uint32_t StartId`、`StringCacheUpdate.cpp:28` `& 0xFFFFFFFF`。
**但这两轮修的都是「切片正确性」，没有人问过「这张表该不该有上限」** —— 这才是 Q4 的问题。

---

## 缺陷清单（按严重度）

- [高] `Code/encoding/StringCache.cpp:26-49`(`Add`) + `:51-62`(`AddWanted`) + `Code/server/Services/StringCacheService.cpp:27` — 服务端全局字符串表无任何容量上限、无清理点（`Clear` 全仓服务端零调用），路径 `ClientReferencesMoveRequest.cpp:35` → `ReferenceUpdate.cpp:53` → `ActionEvent.cpp:150-158` → `CachedString.cpp:62` → `ProcessDirty` 让**一个已认证客户端**每次发包都能加约 2000 条 512 字节唯一字符串，永不回收。
- [中] `Code/encoding/Messages/StringCacheUpdate.cpp:31` — `Values.resize(count)` 在读到任何字符串之前按对端声明的 16 位计数分配；一个 2 字节包即可让客户端分配 65535 个 `String`。同形：`AssignObjectsRequest.cpp:28`、`AssignObjectsResponse.cpp:26`、`NotifyPartyInfo.cpp:36`、`NotifyPartyJoined.cpp:36`、`ShiftGridCellRequest.cpp:39`、`Mods.cpp:46`、`Factions.cpp:46/:54`。
- [中] `Code/encoding/Messages/PluginMessagingRequest.cpp:48-56`（`NotifyPluginMessaging.cpp:44-52` 同形） — `:47` 注释声称 "Never trust the wire length"，实现只把 `size` 夹到 `kMaxPayloadBytes`(24 KiB) 再 `resize`，随后按 24 KiB 循环读字节；声称 24 KiB 而包只有 10 字节时循环仍跑 24576 次。
- [中] `Code/encoding/Messages/RequestObjectInventoryChanges.cpp:37`、`NotifyObjectInventoryChanges.cpp:37`、`NotifyFactionsChanges.cpp:35`、`ClientReferencesMoveRequest.cpp:35`、`ServerReferencesMoveRequest.cpp:35` — 这 5 处 `noexcept` 的 `DeserializeRaw` 内做 `Map::operator[]` 插入（分配），**整个函数体没有 try/catch**，分配失败即 `std::terminate`；`docs/PITFALLS.md` §106 的修复清单只覆盖了 `{AssignObjects,NotifyActorValue,RequestActorValue,NotifyParty,PluginMessaging,StringCacheUpdate}*.cpp`，这 5 个文件在清单外。
- [中] `Code/encoding/Messages/AuthenticationRequest.cpp:19-37` — 未认证连接可达（`GameServer.cpp:610` 解析在 `:866` 认证之前），函数体无 try/catch，而 `:26`/`:28`/`:31` 三次 `ReadString` 与 `:30` `UserMods.Deserialize` 都会分配；分配失败从 `noexcept` 逃出即终止进程。
- [中] `Code/encoding/StringCache.cpp:108-136` + `StringCacheUpdate.cpp:30` — 客户端字符串表同样无上限；服务端每条消息最多推 65535 条（`StringCacheUpdate.h:19`），客户端只要 `StartId` 对得上（`:123`）就全收，单条消息约 32 MiB，且无淘汰。
- [低] `Code/encoding/Messages/NotifyFactionsChanges.cpp:30-31` — `if (count >= kMaxChangeCount) return;` 用的是 `>=`，而写侧 `SerializeRaw`(`:9`) 的 `std::min(size, kMaxChangeCount)` 可以正好写出 `0x400` 条；恰好满批的那一包被整包丢弃。同族其余消息一律用 `std::min` 夹取而非早退。
- [低] `Code/encoding/Differential.h:35-44` — `Serialize` 直接 `m_nextValue.GenerateDifferential(...)`、`Deserialize` 先 `m_nextValue = m_previousValue`；`Prepare()`(`:18-24`) 造出的对象 `m_nextValue` 为空 `optional`，对它调 `Serialize` 是解空 optional 的 UB。当前**零实例化点**（全仓 `Differential<` 只在本文件出现），属潜伏陷阱，不是活缺陷。

## 待验证（缺证据，无法静态闭合）

1. **长度前缀 + 循环拆包到底在哪、是否正确。** `Libraries/TiltedConnect` 在本机是**空目录**
   （`find Libraries/TiltedConnect -type f | wc -l` = **0**；`.gitmodules` 有登记），
   `TiltedCore` 也不在仓内（`xmake.lua:45` `add_requires("tiltedcore 0.2.9")` 从 xmake 源拉取），
   本机 `~/.xmake/packages` 与 `Libraries/` 下均无该包源码。
   **缺的证据**：`Client.hpp`/`Server.hpp` 的 `Update()`/`OnConsume` 调用点 —— 具体要确认三件事：
   (a) recv 到的字节是否先进一个累积缓冲区；(b) 是否循环「读长度前缀 → 攒够 → 交付一整条」；
   (c) 一次交付里若有多条消息，是否连续回调 `OnConsume`。
   在拿到这份源码之前，**「1.5 个包 / 半个包」在本仓库范围内既不能证实也不能证伪**。
   （`docs/PITFALLS.md:20497-20504` §155.6 已为同一原因把「底层传输是否可靠有序」列为未验证，
   本条是它的同源扩展。）
2. **`Buffer::Reader` 读越界时的行为。** `TiltedCore/Serialization.hpp`、`Buffer.hpp` 未随仓库 vendor。
   需要确认 `ReadBits`/`ReadString` 在读到 `ViewBuffer` 末尾之后是：抛异常（⇒ 114 个无 catch 的
   `noexcept` 反序列化函数直接 `std::terminate`）、返回 0（⇒ 静默错解析）、还是裸读越界（⇒ UB）。
   这一条决定了「短包」在本仓库里到底是 terminate 还是 UB，两种都不能只靠读本仓库判定。
   已经能确定的只是：**没有任何一层会先判剩余字节**（Q2.1 的零命中）。
3. **`kMaxPacketBytes` 的依据是否仍成立。** `WireLimits.h:67-72` 的注释断言「服务端序列化进 1<<20、
   客户端进 1<<16，都是固定缓冲区」。`GameServer.cpp:693`(`Buffer buffer(1 << 20)`) 与
   `TransportService.cpp:189`(`Buffer buffer(1 << 16)`) 与注释一致，但 `TiltedPhoques::Buffer::Writer`
   在写满之后是抛、是截断、还是自动扩容，本仓库无法确认。若是自动扩容，注释的前提与
   `kMaxPacketBytes` 的取值依据都需要重写。
4. **`m_pendingMessages` 条数上限。** 属 WS1 已记录项（`docs/audit/WS1-threading.md` Q4），
   本报告不重复。
