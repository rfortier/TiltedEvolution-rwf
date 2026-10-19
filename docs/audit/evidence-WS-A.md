# WS-A · 连接生命周期与线程安全审计

范围：`Code/client/Services/TransportService.h`(253)、`Services/Generic/TransportService.cpp`(978)、
`Services/RunnerService.h`(49)、`Services/Generic/RunnerService.cpp`(68)、`World.h`(70)、`World.cpp`(149)、
`TiltedOnlineApp.cpp`(179)、`Code/base/threading/ThreadUtils.{h,cpp}`(18/96)。
为闭合调用点，额外只读了 `OverlayClient.cpp:27-225`、`OverlayService.cpp` 的 3 个 handler、
`InputService.cpp:520-560`、`PluginMessagingService.cpp:255-355`、`DisconnectedEvent.h`。
**未编译、未运行**（本机无 xmake/MSVC，Libraries 子模块为空）。除 WS-A-01 的一个 5 行反证程序外，
结论全部为静态阅读所得；该反证程序已删除。

## 与 WS1 的关系（先说这个，避免重复）

WS1 报出的两条 [高] **在当前源码里已经修好**，本报告不复报：

- `World.h:51-59` 的声明顺序已改成 `m_runner`(:58) 在 `m_transport`(:59) 之前，
  逆序析构使 runner 比 transport 活得久，与注释一致。WS1 说的「注释与实际相反」不再成立。
- 握手泵循环内的投递点已补上守卫：`TransportService.cpp:759` `if (!m_handshakeThreadStop.load() && HasPendingMessages())`。

WS1 的 Q3（Send 静默丢包）、Q4 的两条无界增长、Q2 的错误注释三条仍在，本报告不重复。

新增条目全部落在 WS1 没有覆盖的四块：**断开/重连后的语义状态**、**m_handshakeThreadActive 的退出语义**、
**异常路径**、**析构残留**。

---

# 一、可闭合缺陷（纯静态可证）

## WS-A-01 · 重连后失败原因丢失：`m_attemptErrorReported` 在认证成功时被清零，失败死因随之蒸发

- **类别**：连线路由 / 状态机（跨线程标志语义）
- **严重度**：**高**
- **位置**：`Code/client/Services/Generic/TransportService.cpp:854-858`（写）、`:547`（读）、`:515-521`（门）

关键代码（`:849-868` 与 `:499-547`）：

```cpp
// :851
m_handshakePending = false;
if (acMessage.Type == AR::kAccepted)
{
    m_connected = true;
    m_attemptErrorReported = false;      // :857  ← 上一次尝试的失败原因在这里被抹掉
    m_attemptDisconnectReported = false; // :858
    ...
}
...
// :499-502
const bool cWasAttempting = m_connected || m_handshakePending;
m_connected = false;
m_handshakePending = false;
...
// :547
m_dispatcher.trigger(DisconnectedEvent(m_attemptErrorReported.load()));
```

- **问题（在什么条件下出错）**：
  1. 第一次尝试连 A 失败 → `ArmConnectionWatchdog` 置 `m_attemptErrorReported=false`（`:365`），
     失败时置 `true`（`:612` 超时 / `:960` 服务端拒绝），UI 正确地收到 `IsError=true` 并自动重连。
  2. 自动重连连上 B 并**认证成功** → `:857` 把 `m_attemptErrorReported` 清成 `false`。
  3. 过一会儿链路自己断了（`kProblemDetectedLocally` / `kClosedByPeer` / 服务器踢人），
     `OnDisconnected` 带着 `cWasAttempting==true` 走到 `:547`，读到的是 **`:857` 残留的 false**。
  4. 于是 `DisconnectedEvent(false)` 上报给 UI，`OverlayService::OnDisconnectedEvent` 把它原样
     传给 JS 的 `isError`，而 `client.service.ts:367` 的重连分支是 `isError && ...`（见 PITFALLS §14.3）。
     **结果：一次「无错误断开」被当成「连接尝试失败」，自动重连不触发，玩家掉线后停在断线界面。**

  这正好和 `:857-858` 那两行自己的意图相反：`:849-851` 刚说「握手结束，看门狗不许再打」，但
  `m_attemptErrorReported` 在 `:171-178` 的注释里被定义为「**这一次尝试**的失败原因已上报」，
  而它同时又被 `:602` 当作看门狗的一次性门闩。一次成功连接之后，这两个语义分叉了：
  作为门闩它该清（下一次尝试要能报警），作为「上一次尝试的结局」它该保留到下一次 `ArmConnectionWatchdog`。
  `ArmConnectionWatchdog`（`:365-366`）本来就会重置这两个标志，所以 `:857-858` 是多余的，
  而它多余的那一次清零恰好抹掉了唯一能回答「刚才那条 `DisconnectedEvent` 该不该是 error」的信息。

- **证据**：
  - 写：`TransportService.cpp:365-366`（Arm 重置）、`:612`（超时置 true）、`:857`（**成功时误清**）、`:960`（拒绝置 true）。
  - 读：`TransportService.cpp:547` 唯一读点；`DisconnectedEvent.h:18-24` 把该值命名为 `IsError`；
    `OverlayService.cpp:464-465` `ExecuteAsync("disconnect", pArgs)` 把它送进 JS。
  - 同一段代码在 `:509-514` 已经为「失败后紧跟着的那次 teardown」论证过为什么必须仍报 disconnect，
    但没论证「成功连接之后的 teardown」为什么该报 error。
  - 顺带：`:857-858` 同时清 `m_attemptDisconnectReported`（这一条是**正确且必需**的，
    否则 A 尝试已经上报过 disconnect 会把 B 会话的第一次 teardown 吞掉）。两个标志被一起清，
    但只有其中一个需要清——这正是缺陷的来源。

- **最小改法**：删掉 `:857` 这一行（`m_attemptErrorReported = false;`），保留 `:858`。
  下一轮 `ArmConnectionWatchdog:365` 会负责重置它。

---

## WS-A-02 · 握手泵异常退出时不清 `m_handshakeThreadActive`，传输被永久冻结

- **类别**：异常路径 / 线程生命周期
- **严重度**：**中**
- **位置**：`Code/client/Services/Generic/TransportService.cpp:690-802`（尤其 `:792-793` 与 `:798-801`）

关键代码：

```cpp
// :792-793
if (!m_handshakeThreadStop.load())
    m_handshakeThreadActive = false;   // 只有走完 try 块才会执行

spdlog::info("handshake pump: finished for {}, ...", ...);
}
catch (...)                              // :798
{
    Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory in TransportService::HandshakeThreadMain");
}                                        // :801  ← active 仍是 true，且线程到此结束
```

配合 `:819-820`：

```cpp
void TransportService::HandleUpdate(const UpdateEvent& acEvent) noexcept
{
    ...
    if (m_handshakeThreadActive.load())
        return;                          // 帧线程直接放弃泵，永远
```

- **问题**：`m_handshakeThreadActive` 是「有第二个线程正在使用 transport」的唯一判据。
  它只在两个地方被清：`StopHandshakePump:684`（但那只在 `BeginConnect` 和 `~TransportService` 里调）
  和 `:792-793`（正常退出路径）。**任何从 `:697-801` try 块里抛出的异常都会跳过 `:792`**，
  线程结束但标志留在 `true`。
  后果：此后每一次帧更新都在 `:819-820` 提前返回，`PumpConnectionLocked()` 再也不被调用——
  `Update()` 不跑、看门狗不跑、`Send()` 的 `IsConnected()` 保持旧值。
  玩家看到的是「连接还在，但什么都收不到、超时也不报」，而且**没有任何日志**说明原因
  （catch 只报 "ran out of memory"，不提 active 仍为 true）。
  只有再点一次 Connect（`BeginConnect:669` 重新置 true）或退出才会恢复。

  可达的抛点都在 try 块内且都会分配内存：`:706`/`:711`/`:726`/`:741-742`/`:795` 的 spdlog 格式化字符串、
  `:708` `Client::ConnectByIp/Connect`（未知实现）、`:761` `GetRunner().Queue`（`std::function` 分配）、
  `:767` `sleep_for`。作者显然预期会有异常——否则不会写这个 catch——但清理动作放在了 try 里。

- **证据**：`:669`（置 true）、`:684`（StopHandshakePump 清）、`:792-793`（正常路径清）、`:801`（异常路径不清）、
  `:819-820`（唯一消费者，读到 true 就永不泵）。同一文件里 `PumpConnectionLocked:623` 和 `FlushPendingMessages:339`
  都在 catch 里给了明确的返回值，说明作者懂这个模式，`:792` 只是漏了。

- **最小改法**：把 `:792-793` 那两行挪到 `catch(...)` 块里再写一份，或改成在函数入口用
  `struct Guard { std::atomic<bool>& f; ~Guard(){ if(!stop) f=false; } }` 这类 RAII 清理。

---

## WS-A-03 · `BeginConnect` 是 `noexcept` 却没有 try/catch：`std::thread` 构造失败直接 terminate

- **类别**：异常路径 / 被忽略的返回值
- **严重度**：**中**
- **位置**：`Code/client/Services/Generic/TransportService.cpp:663-672`

```cpp
void TransportService::BeginConnect(const std::string& acEndpoint) noexcept   // :663
{
    // Two pumps would put two threads inside Client at once. Only ever one.
    StopHandshakePump();                                                      // :666

    m_handshakeThreadStop = false;
    m_handshakeThreadActive = true;                                           // :669 ← 先置位

    m_handshakeThread = std::thread([this, acEndpoint]() { HandshakeThreadMain(acEndpoint); }); // :671 ← 可以抛
}
```

- **问题**：`std::thread` 的构造函数在无法创建线程时抛 `std::system_error`（线程表耗尽、
  `CreateThread` 失败、内存不足）。`BeginConnect` 声明为 `noexcept` 且**整个函数没有 try/catch**——
  这是本文件里唯一一个这么大的、既 noexcept 又裸奔的函数（对比 `:355`、`:591`、`:690` 都有 catch），
  所以异常会一路穿到 `std::terminate`，**整个游戏进程被杀**。对 MOD 里的客户端来说这等同于崩溃。

  即使不 terminate（假设上层兜住），`:669` 已经把 `m_handshakeThreadActive` 置为 true 而线程没起来，
  后果与 WS-A-02 完全相同：transport 永久冻结。

- **证据**：`:663` 的 `noexcept`；`:664-672` 无 try/catch；调用点 `OverlayClient.cpp:121-126`
  （CEF 消息线程）与 `TransportService.h:84` 的声明。同文件其他 8 个 noexcept 函数全部带 `catch (...)`，
  唯独这里没有。

- **最小改法**：把 `:668-671` 包进 `try { ... } catch (...) { m_handshakeThreadActive = false; m_handshakeThreadStop = true; Diagnostics::ReportFailure(...); }`。

---

## WS-A-04 · `~TransportService` 只 join 线程，不清理 `m_pendingMessages` 与 runner 队列里的 `[this]` 回调

- **类别**：析构顺序 / 挂起的 lambda
- **严重度**：**中**
- **位置**：`Code/client/Services/Generic/TransportService.cpp:148-153`；`TransportService.h:234-235`；
  `World.cpp:75-82`（`~World` 的顺序）；`RunnerService.cpp:53-62`

```cpp
TransportService::~TransportService() noexcept
{
    // Nothing may be mid-Update() once World is being torn down, otherwise the
    // pump would be calling into services that are already half destroyed.
    StopHandshakePump();           // :152 只处理线程
}
// m_pendingMessages 里已解析但未分发的 ServerMessage 直接随 vector 析构丢弃；
// runner 队列里可能仍有 [this]{ FlushPendingMessages(); } —— 无人取消。
```

- **问题（三个独立后果，都在 `~World` 期间）**：
  1. **残留消息被静默丢弃**。`FlushPendingMessages` 只在游戏线程跑（`World.cpp:105`），
     而 `~World` 一旦开始就不会再有 `World::Update`。`:759-762` / `:777-780` 投进 runner 队列的
     `[this]{ FlushPendingMessages(); }` 只能留在队列里，`m_pendingMessages` 里的 `UniquePtr` 随
     vector 析构释放。对「退出游戏」而言无害（进程要没了），但如果将来 `World` 支持重建，
     这些消息就永久消失——而 `m_droppedPendingMessages`（`h:216-219`）**不计数**这条路径，
     日志上看起来一切正常。
  2. **runner 队列里的 `[this]` 回调没有撤销机制**。`RunnerService` 没有任何 `Cancel`/`Clear` API
     （`RunnerService.h:33-48` 只有 `Queue/OnUpdate/Trigger`），`TaskQueue` 也不支持按来源删除。
     所以「把 `[this]` 扔进一个活得比自己久的队列」这件事本身没有回收手段，只能依赖
     `m_runner` 比 `m_transport` 后析构（现在成立，见 WS1 已修复项）。
  3. **`World::~World` 的清理跑在所有成员析构之前**：`World.cpp:75-82` 的 `PluginMessagingService::Get().Shutdown()`
     函数体先执行，之后才是 `m_pluginMessagingConnection`/`m_transport`/`m_runner` 等成员的逆序析构。
     如果 `Shutdown()` 内部（或任何插件在解注册回调里）走到 `transport.Close()`，会命中一个仍然存活、
     但其 `m_dispatcher`（`World.h:49`）也仍存活的 transport——这条路径**目前不构成缺陷**，
     但它是唯一能让人误以为「shutdown 之后 transport 还安全」的地方；实际安全性完全取决于
     `Shutdown()` 不做 transport 操作。这一点无法从本仓库静态闭死（见「运行时依赖」R-2）。

- **证据**：`:148-153`；`m_pendingMessages` 全仓库只有 5 处访问（`cpp:271/273/277/327/330/352`），
  全在运行期路径，没有任何一处出现在析构里；`RunnerService.h:33-48` 无清理 API；
  `World.cpp:75-82` 注释「a plugin callback must never be reachable once the world is gone」。

- **最小改法**：`~TransportService` 里 `StopHandshakePump()` 之后加一句
  `{ std::lock_guard<std::mutex> l(m_pendingMutex); m_pendingMessages.clear(); }`，
  并让 `FlushPendingMessages` 在对象析构后不可达（把 `[this]` 换成 `std::weak_ptr` 或加一个
  `m_shuttingDown` 原子位，`FlushPendingMessages` 入口先查它）。

---

## WS-A-05 · 队列溢出丢「最旧」，对非幂等的协议消息是丢状态而不是丢快照

- **类别**：队列丢消息
- **严重度**：**中**
- **位置**：`Code/client/Services/Generic/TransportService.cpp:271-277`（`kMaxPendingMessages` 定义在 `:49`）

```cpp
if (m_pendingMessages.size() >= kMaxPendingMessages)   // :271  kMaxPendingMessages = 4096
{
    m_pendingMessages.erase(m_pendingMessages.begin()); // :273  丢最旧
    ++m_droppedPendingMessages;
}
m_pendingMessages.push_back(std::move(pMessage));       // :277
```

- **问题**：`:266-270` 给出的理由是「最新的消息承载会话在等的状态，最旧的已被后续快照取代」。
  这个理由**只对快照类消息成立**（`NotifyPlayerList`、`CharacterSpawn` 之类）。
  但这条队列里同时流着**非快照、非幂等**的消息，它们的语义是「一次事件」而不是「一个状态」：
  `CharacterService.cpp:887` 的` OwnershipEpoch` 转移（丢掉就是所有权错乱，且 `acMessage.OwnershipEpoch <= currentEpoch`
  的去重会让重发的旧包也被拒）、派对邀请（`PartyService` 的 `m_invitations` 是 add-only）、
  以及**认证应答**本身——`HandleAuthenticationResponse` 走的就是这条队列（`:851` 说明它确实从
  `DispatchMessage` 进来）。极端情况下把认证应答丢掉，会话就停在「transport 已连、永不认证」，
  超时后又被 `kHandshakeTimeout` 报成 "no_reason"。

  触发条件收紧一点看：`m_pendingMessages` 只在**非游戏线程**写（`:254` 的线程判据），
  而下一次连线的 pump 在 `:724` 会因为「帧循环 1 秒内活着」立刻 break，所以队列要靠
  「游戏线程停 1 秒以上（加载画面）且服务端仍在推」才能涨到 4096。这确实罕见，但
  `kMaxPendingMessages` 恰好是按「全部都是快照」这个假设取的值，而**没有任何按 opcode 区分的保护**。

- **证据**：`:266-277`；`kMaxPendingMessages`（`:49`）全仓库只此一处使用；
  `HasPendingMessages`（`:348-353`）不区分 opcode；
  `DispatchMessage`（`:280-307`）对丢失的 opcode 顺序没有任何补偿。

- **最小改法**：溢出时不要一律丢最旧——把 `AuthenticationResponse` 和一个显式的
  「不可丢 opcode」集合挑出来（跳过它们再删），或统一改成「溢出即 `Close()` 并上报
  `ConnectionErrorEvent`」，让状态机知道已经不一致。

---

## WS-A-06 · `HasPendingMessages()` 与投递之间的检查-使用窗口（残留的单帧延迟）

- **类别**：队列 / 竞态（良性但真实）
- **严重度**：**低**
- **位置**：`TransportService.cpp:759-762` 与 `:777-780`

```cpp
if (!m_handshakeThreadStop.load() && HasPendingMessages())     // :759 先判
    m_world.GetRunner().Queue([this]() { FlushPendingMessages(); });  // :761 再投
...
if (!m_handshakeThreadStop.load() && HasPendingMessages())     // :777
    m_world.GetRunner().Queue([this]() { FlushPendingMessages(); });  // :779
```

- **问题**：`HasPendingMessages()` 内部取 `m_pendingMutex` 后立即释放（`:350-352`），
  投递是下一句。两者之间如果 pump 自己又解析了一条（同线程，不可能）——真正的窗口是
  `:724` 的 `break`：当游戏线程活着时 pump 立刻 break，**走向 `:777` 的兜底投递前要先过
  `:777` 的 `!m_handshakeThreadStop`**，正常情况下能投出。所以这条不产生丢消息，
  只产生「本可以在这一帧 flush 的消息多等一帧」——具体是游戏线程恰好在 `:724` 判断成立
  与 `:777` 之间完成了 flush，则 `:777` 看到空队列、不投递，而下一条消息要等下一次
  `World::Update` 里的 pump 逻辑。因为 `FlushPendingMessages` 本身幂等（空队列返回 false），
  这只是 WS1 已记录的单帧乱序的一个同源变体。
- **证据**：`:348-353`（锁作用域只包住 `empty()`）、`:759-762`、`:777-780`、`:330`。
- **最小改法**：不值得改（幂等 flush 已覆盖）；若要彻底消除，`:777` 改成无条件投递一次。

---

## WS-A-07 · `GetLocalPlayerId()` 返回无同步的引用，插件线程直接读

- **类别**：数据竞争（跨线程成员）
- **严重度**：**低**
- **位置**：`Code/client/Services/TransportService.h:51`（定义）、`PluginMessagingService.cpp:323`（跨线程读点）

```cpp
// TransportService.h:51
const uint32_t& GetLocalPlayerId() const noexcept { return m_localPlayerId; }
// 写入：TransportService.cpp:834 (HandleConnected, 游戏线程) / :839 (HandleDisconnected, 游戏线程)

// PluginMessagingService.cpp:323  —— 该函数由插件在任意线程调用
message.sender.connectionID = static_cast<STRPM::ConnectionID>(transport.GetLocalPlayerId());
```

- **问题**：`m_localPlayerId` 是普通 `uint32_t`（`h:147`，连默认初始化都没有——构造里 `:116`
  赋 `NULL`，若构造中间抛异常则它是未初始化的）。同文件所有其他跨线程标量都是 `std::atomic`
  （`m_connected`、`m_handshakePending`、`m_droppedPackets`……），唯独这个不是。
  `PluginMessagingService` 明确承诺「a plugin may send from any thread it likes」
  （`PluginMessagingService.h:141`），于是插件线程与游戏线程对同一个非原子 `uint32_t` 构成
  正式的数据竞争（C++ 内存模型意义下的 UB）。在 x86-64 上对 4 字节对齐变量的撕裂读实际不会发生，
  所以**现实后果几乎为零**，但按标准它是 race。
  返回 `const&` 还让调用方能把这个引用带出去，延长了竞争窗口。
- **证据**：`h:51`、`h:147`、`cpp:116/834/839`、`PluginMessagingService.h:141`、`PluginMessagingService.cpp:323/351`。
  写点全部在游戏线程（`HandleConnected`/`HandleDisconnected` 只由 dispatcher 触发）。
- **最小改法**：`std::atomic<uint32_t> m_localPlayerId{0};` + `uint32_t GetLocalPlayerId() const { return m_localPlayerId.load(); }`（去掉引用返回）。

---

## WS-A-08 · `RunnerService::OnUpdate` 的 catch 会吞掉「已经弹出、正在执行」的任务，且把它报成 OOM

- **类别**：异常路径 / 队列丢消息
- **严重度**：**低**
- **位置**：`Code/client/Services/Generic/RunnerService.cpp:53-67`

```cpp
constexpr size_t kMaxTasksPerUpdate = 256;
try
{
    std::function<void()> task;
    for (size_t ran = 0; ran < kMaxTasksPerUpdate && m_runner.Pop(task); ++ran)
    {
        if (task)
            task();                       // :61  任务自己抛 → 跳到 catch
    }
}
catch (...)
{
    Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory in RunnerService::OnUpdate");  // :66
}
```

- **问题**：`:61` 的 `task()` 是**任意业务代码**（`TransportService::FlushPendingMessages`、
  `PartyService::CreateParty`、`MagicService::StartRevealingOtherPlayers`……）。
  其中任一个抛出，异常被这里的 `catch (...)` 吞掉，那条任务连同它的副作用一起消失，
  日志却写「ran out of memory」——与真实原因无关，把排查方向指向错误的地方。
  `:51-52` 的注释说「A task that throws is dropped and the ones behind it stay queued」，
  行为描述是对的，但**日志文本是错的**，而且`:64-67` 的 catch 同时兼着「`Pop` 里 OOM」和
  「`task()` 里任意异常」两种完全不同的情况。
- **证据**：`:53-67`；`:47-52` 的注释；所有 `Queue` 调用点（`OverlayClient.cpp:58/60/64/70/75/80/148/173/189`、
  `TransportService.cpp:761/779`、`DiscoveryService` 的 `OnConnected`）。
- **最小改法**：把 `task()` 单独包一层 try/catch 并报真实异常与任务类型；
  外层 catch 只留给 `Pop` 的分配失败。

---

## WS-A-09 · 握手超时路径先发事件再 `Close()`，事件订阅者若抛出则 teardown 被跳过

- **类别**：异常路径
- **严重度**：**低**
- **位置**：`TransportService.cpp:602-619`

```cpp
if (m_handshakePending && !m_attemptErrorReported &&
    std::chrono::steady_clock::now() >= m_handshakeDeadline)   // :602-603
{
    spdlog::error("Handshake with {} timed out after {}s, aborting", ...);  // :605
    m_attemptErrorReported = true;                                            // :612
    ConnectionErrorEvent errorEvent;
    errorEvent.ErrorDetail = "{\"error\": \"no_reason\"}";
    m_dispatcher.trigger(errorEvent);                                         // :616 ← 回调链可抛
    Close();                                                                  // :618 ← 被跳过
}
```

- **问题**：`:616` 的触发器会同步调用 `OverlayService::OnConnectionError` → `m_pOverlay->ExecuteAsync`，
  以及任何第三方订阅者。若其中任何一个抛出，`PumpConnectionLocked` 的 `catch (...)`（`:625-629`）
  会把它吞掉并 `return false`，于是 `Close()` **不执行**：transport 保持打开、
  `m_handshakePending` 仍为 true（只有 `Close()`→`OnDisconnected` 才会清它），
  而 `m_attemptErrorReported` 已被置 true（`:612`），看门狗**再也不会触发**。
  净结果：连接永远停在 pending，无任何后续日志，超时机制被自己的异常处理废掉。
  触发需要订阅者抛异常，概率低，但这是 `:612` 先置位、`:618` 后清理这种「先记账后动手」模式的固有风险。
- **证据**：`:602-619`、`:623`（返回 `m_handshakePending`，此时仍 true → pump 继续空转）、`:625-629`。
  `OverlayService.cpp:537-542` 是 `ConnectionErrorEvent` 唯一的订阅者。
- **最小改法**：`Close()` 提到 `m_dispatcher.trigger(errorEvent)` 之前，或把 trigger 单独包 try/catch。

---

# 二、运行时依赖（成立与否取决于引擎/运行时行为）

## R-1 · `AbandonAttempt` 依赖「`Client::Close()` 不回调 `OnDisconnected`」才不泄漏上游握状态

- **位置**：`TransportService.cpp:554-565`
- **为何闭不死**：`AbandonAttempt` 先把 `m_handshakePending` 与 `m_attemptDisconnectReported` 都置位，
  再 `Close()`。整个设计建立在 `Client::Close()` **不会**同步回调 `OnDisconnected` 之上——
  因为一旦回调，`OnDisconnected` 在 `:515` 看到 `m_attemptDisconnectReported==true` 就直接 `return`，
  **既不报 disconnect，也不做任何 teardown**，随后 `ArmConnectionWatchdog` + `BeginConnect` 在
  `Client` 尚未真正关断的 uv 握上再起一次连接。
  `Client.hpp` / `Client.cpp` 未随仓库 vendor（`**/Client.hpp` 在本仓库 0 匹配），
  `Client::Close` 的语义无法静态确认。
- **判定所需的运行期证据**：在 `OnDisconnected` 入口打一行 infolog，然后走一次
  `abandonAttempt`（UI 的自动重连路径），看它是否会打印。

## R-2 · `World::~World` 的 `Shutdown()` 与 transport 的时序

- **位置**：`World.cpp:75-82`
- **为何闭不死**：`Shutdown()` 是本仓库代码，但它调的是 `PluginMessagingService` 单例；
  该单例持有的 `m_pWorld`（`PluginMessagingService.h:134`）在 `~World` 期间已经指向正在销毁的对象。
  `Shutdown()` 本身不碰 transport 目前**看起来**成立，但「插件在解注册回调里调 `GetTransport().Close()`」
  这条路径（`PluginMessagingService.h:83` 的注释承认回调可以在锁外被调用）无法静态排除。
- **判定所需证据**：`Shutdown()` 是否只在锁内摘除监听器、是否调用任何回调。

## R-3 · `m_gameThreadId` 是否真的是游戏线程

- **位置**：`TransportService.cpp:118-123`
- 该值在 `TransportService` 构造时取 `std::this_thread::get_id()`，注释断言
  「World is constructed there」。`World::Create()` 由 `TiltedOnlineApp::BeginMain`（`TiltedOnlineApp.cpp:95`）调用，
  而 `OverlayService`（含 CEF）在 `World.cpp:42` 构造。若 CEF 的初始化在别处触发回调，
  或 `BeginMain` 不是在游戏的 `WinMain` 线程上跑，`OnConsume` 的线程判据（`:254`）会把
  本该直接分发的包错误地入队（良性，多一帧延迟）或反之（**让 pump 线程直接分发**，即 WS1 记下的跨线程碰游戏内存）。
  两点都无法在本仓库静态确认。
- **判定所需证据**：`m_gameThreadId` 与 `InputService` 里 `::GetCurrentThreadId()` 的对比日志。

## R-4 · `Client::Update()` 内部是否会重入 `OnConsume` 并在持 `m_clientMutex` 时取 `m_dispatchMutex`

- **位置**：`TransportService.cpp:621`（`Update()`）与 `:258`（`OnConsume` 游戏线程分支）
- WS1 已论证「无死锁」的完整论证建立在「`m_dispatchMutex` 只被游戏线程持有」之上。
  若 `Client::Update()` 在 pump 线程上（`:733`，此时持 `m_clientMutex`）同步回调 `OnConsume`，
  而该包恰好以游戏线程身份进入 `:254` 分支——不可能，因为 `:254` 判的是当前线程——
  所以真正需要确认的是：pump 线程上的 `OnConsume` 会不会因为其他原因走到 `:258`。
  静态上不会。保留为待验证，不构成缺陷。

---

# 三、已确认无问题的关键不变量

1. **不重复处理**：`OnConsume` 两条路径互斥——`:254` 判当前线程，`:259` 走直接分发并持 `m_dispatchMutex`，
   `:263-277` 走入队。`FlushPendingMessages` 用 `batch.swap`（`:330`）整体取走，队列清空。
   `m_pendingMessages` 在 .cpp 里共 6 处访问（`:271/273/277/327/330/352`），
   **每一处都在 `m_pendingMutex` 之下**（锁在 `:263`、`:326`、`:350`），没有裸访问。
2. **`StopHandshakePump` 之后线程一定会退出**：`:717`、`:764`、`:724` 三个出口都会重新读
   `m_handshakeThreadStop`，最坏只多睡一次 8ms（`:767`）。`:686-687` 的 `joinable()` 判断 +
   `join()` 不会漏。
3. **`begin/stop` 不会同时有两个 pump**：`BeginConnect:666` 先 `StopHandshakePump()`，
   而后者 join 完才返回；`m_handshakeThread` 因此总是处于 joinable 但已退出、或已 join 的状态。
4. **`m_handshakeThreadActive` 不会因为「旧线程晚清」而误清新线程的位**：`:792` 有
   `!m_handshakeThreadStop` 守卫，注释 `:787-791` 的理由与实际代码一致。
   （但这条守卫对 **异常路径** 无效，见 WS-A-02。）
5. **`OnDisconnected` 的「先读后清」是对的**：`:499` 在 `:501-502` 之前读 `m_connected || m_handshakePending`，
   保证重入时第二次进入能看到第一次已经清掉的标志，不会重复上报。
6. **`RunnerService` 用 `Pop` 而非 `Drain`**：`RunnerService.cpp:58` 是正确的规避，
   `RunnerService.h:42-47` 记录的 `TaskQueue::Drain` 自死锁 hazard 不会在这个客户端上发生。
   单次 256 条的上限（`:53`）足以让帧不被自己的队列拖住。
7. **`ThreadUtils` 没有线程安全问题**：96 行里只有两个无状态函数；
   `SetThreadNameWindows` 的 `static const auto pSetThreadDescription`（`:44-46`）是函数内静态初始化，
   C++11 起线程安全。与 TransportService 无交叉。
8. **`World.h:51-59` 的析构顺序已正确**（WS1 的 [高] 已由 `bebf2dc8` 修好）：
   `m_runner`(:58) 先声明、`m_transport`(:59) 后声明 ⇒ 逆序析构时 transport 先死、runner 后死，
   `~TransportService` 的 join 期间 pump 还能安全地往 runner 队列里投。
9. **`HandleUpdate` 的 `m_handshakeThreadActive` 早退（`:819`）在正常路径下不会永久冻住**：
   正常退出的 pump 一定会在 `:792-793` 清掉它（`catch` 路径除外，见 WS-A-02）。
10. **`Send` 的 `try_to_lock` + 计数（`:204-216`）是对的**：不阻塞帧、失败有 `m_droppedPackets` 计数、
    心跳日志（`:580-583`）会把它打出来。WS1 说的「无日志」在当前代码里已有计数，
    只是仍无 per-packet 日志（不重复报告）。

---

# 四、未覆盖区域

1. **`TiltedCore` 未 vendor**：`Client.hpp`、`TaskQueue.hpp/.cpp` 在本仓库 0 匹配
   （`find . -name "Client.hpp" -o -name "TaskQueue*"` 无结果）。因此以下四点无法静态确认，全部影响上面的定级：
   `Client::Close()` 是否同步回调 `OnDisconnected`（R-1）；
   `Client::Update()` 是否内部重入 `OnConsume`（R-4）；
   `TaskQueue::Add/Pop` 的实现与容量（影响 WS-A-02/WS-A-04 的实际后果）；
   `Client::Connect/ConnectByIp` 的返回值语义（`:708-712` 只记一条 warn，不改变状态）。
2. **`OverlayService::ExecuteAsync` → CEF IPC 的路由**：只确认了 `OnDisconnectedEvent`/`OnConnectionError`
   会往 CEF 送消息，没有追 `ExecuteAsync` 是否可能同步抛（WS-A-09 的前提）。
3. **`BehaviorVar::InitAsync`（`BehaviorVar.cpp:769` 的 `std::thread`）与 `DiscordService.cpp:206` 的
   分离线程**：两者都在 `World` 构造期起线程，都不在本次指定文件内；仅确认它们不是 `TransportService`
   的线程，未审计它们自身对 `World` 的访问。
4. **`InputService` 的 `WM_TIMER` 驱动**（`InputService.cpp:526-556`）：本报告只确认它是
   `World::Update()` 的第二个驱动源（与 `TiltedOnlineApp.cpp:133` 并列），且与游戏 vm tick 共享
   `World::Update` 的单帧节流（`World.cpp:93-97`）。这两个驱动是否在同一线程、`m_hasTicked`/`m_lastFrameTime`
   的非原子访问是否构成竞争，属于 InputService 的审计范围，本次未展开。
5. **时间预算**：本工作流按 45 分钟收尾，`TransportService.cpp` 的 30-160 行（构造函数与常量）
   与 842-978 行（认证应答的 JSON 构造）只做了针对性精读，没有逐行复核。