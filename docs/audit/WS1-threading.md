# WS1 · 线程安全审计（TransportService / ThreadUtils）

范围：`Code/client/Services/TransportService.h`(242行)、`Code/client/Services/Generic/TransportService.cpp`(949行)、
`Code/base/threading/ThreadUtils.{h,cpp}`(18/96行)。
为回答 Q1/Q5 必须确认「谁调用 FlushPendingMessages」「成员析构顺序」，额外只读了
`World.h:1-70`、`World.cpp:75-145`、`RunnerService.{h,cpp}`、`OverlayClient.cpp:100-190`、`PlayerService.cpp:40-85,255-285`。
未编译，全部为静态阅读。已核对 `docs/PITFALLS.md:181-203`（§5 第4/5条）：已知的是「pump 只解析不分发」「FlushPendingMessages 先 swap 再分发」，
本报告不重复这两条。

---

## Q1 · FlushPendingMessages 的调用者与线程；两条路径会不会重复处理 / 乱序

**调用点只有两处**，都是把 lambda 投进 RunnerService：

- `TransportService.cpp:732` — pump 循环内，`if (HasPendingMessages()) m_world.GetRunner().Queue([this]{ FlushPendingMessages(); });`
- `TransportService.cpp:750` — pump 循环退出后，同一行写法（多一个 `!m_handshakeThreadStop` 前置条件）

lambda 的实际执行点在 `RunnerService.cpp:58-62`（`OnUpdate` 里 `m_runner.Pop(task); task();`），
而 `RunnerService::OnUpdate` 唯一的调用者是 `World.cpp:105` 的 `m_runner.OnUpdate(UpdateEvent(...))`，
`World::Update()` 由帧循环驱动。**结论：FlushPendingMessages 只在游戏线程执行。**

**重复处理：不会。** 每条消息只走一条路——
`OnConsume` 在游戏线程时走 `:239-246`（直接 DispatchMessage），否则走 `:248-249`（push 进队列）；
`FlushPendingMessages` 在 `:302` 用 `batch.swap(m_pendingMessages)` 整体取走，队列被清空，不存在二次投递。
（`m_pendingMessages` 全仓库引用只有 5 处：`h:224`、`cpp:249/299/302/324`，没有别的写入者。）

**乱序：帧内窗口存在，但只差一帧。** `World::Update` 的顺序是 `:105` 先跑 runner 任务（flush），`:106` 才
`trigger(UpdateEvent)` → `HandleUpdate` → `Client::Update` → `OnConsume`（游戏线程直接分发）。
所以同一帧里「旧队列」先于「新到达」；只有 pump 线程恰好在 `:105` 与 `:106` 之间完成一次解析并入队时，
游戏线程才会先分发更新的那条、把更旧的那条压到下一帧。窗口受限于 pump 生命周期，影响为单帧乱序 → **低**。

---

## Q2 · m_dispatchMutex 与 m_clientMutex 的获取顺序 / 死锁

实测锁序（不是注释里写的那套）：

| 路径 | 顺序 |
|---|---|
| 游戏线程 `HandleUpdate`(`:796` try_to_lock 拿到 m_clientMutex) → `PumpConnectionLocked` → `Update()` → `OnConsume`(`:243`) | **m_clientMutex → m_dispatchMutex**（`:243` 是阻塞 lock_guard） |
| 游戏线程 runner 任务 `FlushPendingMessages`(`:294`) → `DispatchMessage` → handler → `Close()`(`:164`) | **m_dispatchMutex → m_clientMutex**（阻塞） |
| 同上 → handler → `Send()`(`:199`) | m_dispatchMutex → m_clientMutex（try_to_lock，不阻塞） |
| pump 线程 `:702` → `Update()` → `OnConsume`(`:248`) | m_clientMutex → m_pendingMutex |
| CEF 消息线程 `OverlayClient.cpp:124/134` → `Close()`(`:164`) | 只取 m_clientMutex |

**没有可证的死锁**，但理由和代码里写的不一样：要成环需要「持 m_dispatchMutex 者等 m_clientMutex」与
「持 m_clientMutex 者等 m_dispatchMutex」同时成立。m_dispatchMutex 的实际持有者**只有游戏线程**
（`:243` 要求线程 id 等于 m_gameThreadId；`:294` 的 flush 任务也跑在游戏线程），
因此上表第 1、2 行是同一个线程：`m_clientMutex` 是 recursive_mutex，同线程重入不阻塞；
而真正持 m_clientMutex 不放的 pump 线程**从不取 m_dispatchMutex**（pump 的 `OnConsume` 走队列）。
所以最坏情况是游戏线程在 `:164` 上等 pump 放锁，而 pump 不会等它 → 有界等待，非死锁。
`m_pendingMutex` 在所有路径上都是最内层且分发前已释放（`:298-303`），ABBA 确实被避开了。

**但 `:159-163` 的注释是错的**：它声称「游戏线程不可能在持有 m_clientMutex 的同时等 m_dispatchMutex，
因为 HandleUpdate 用的是 try_to_lock」。try_to_lock 只作用于**获取 m_clientMutex 那一刻**；
拿到之后 `:800 PumpConnectionLocked` → `:592 Update()` → `OnConsume` → `:243` 是**阻塞**的 lock_guard。
这条注释是目前唯一「两把锁不成环」的书面论证，而它的前提不成立 → **中**。

---

## Q3 · Send() 静默丢包的调用方覆盖

`Send` 失败路径（`:199-201`）：`try_to_lock` 失败 → 直接 `return false`，**全程没有一行日志**。
`Code/client` 下 `Send` 调用点 74 处，**只有 3 处检查返回值**：
`CharacterService.cpp:219`、`CharacterService.cpp:1809`、`PluginMessagingService.cpp:277`。
其余（含 `PlayerService.cpp:272` 的 `PlayerRespawnRequest`、`OverlayClient.cpp:162` 聊天、`OverlayClient.cpp:184` 传送）全部丢弃返回值。

持续丢包且无人知晓的确定场景：**握手期间 pump 线程每次迭代都整段持有 m_clientMutex**（`:702-715` 包住整个
`PumpConnectionLocked()`，其中含 `Client::Update()`，注释 `:183-186` 亦自承「可以持有一整个 Client::Update」）。
此时 CEF 消息线程的聊天/传送 `Send` 必然 `try_to_lock` 失败；连接建立后该窗口消失，但任何与游戏线程
`HandleUpdate` 撞车的 `Send` 仍然静默丢弃。因为不记日志，丢包在玩家侧表现为「消息偶尔不发」而无任何证据 → **中**。

---

## Q4 · m_pendingMessages 容量

**没有上限。** `cpp:249` 是唯一的写入点（`push_back`），`:299/302/324` 只读，全仓库无 `resize`/`clear`/容量判断。
增长窗口由 `:688` 的循环条件 `m_handshakePending && !m_handshakeThreadStop` 界定：
认证应答一到 `m_handshakePending` 即被清零（`:822`），pump 退出，所以**无限增长只发生在「握手未完成 + 游戏线程卡住」期间**。
`WireLimits::WithinPacketLimit`(`:216`) 只限制单包大小、不限制条数，因此一个在应答认证前持续推送消息的服务端
（或卡在加载画面的客户端 + 正常服务端）可以把它撑到任意大 → OOM 向量成立但窗口受限 → **中**。

同一条路径上还有第二个无界容器：`:730-732` **每 8ms 迭代一次就投一个 flush 任务**进 RunnerService，
判据 `HasPendingMessages()` 只在游戏线程真正 flush 之后才变 false，所以游戏线程卡住时 runner 队列按 125 个/秒增长，
每个任务都是一个捕获 `this` 的 `std::function`。加载画面 60s ≈ 7500 个任务 → **中**。

---

## Q5 · 析构顺序与 HandshakeThreadMain 的 use-after-free

`StopHandshakePump` 确实 join 了（`:657-658` `if (m_handshakeThread.joinable()) m_handshakeThread.join();`），
`~TransportService` 第一句就调它（`:147`）。线程本身不会在 join 后继续跑。**但捕获 `this` 的 lambda 会。**

1. **`:730-732` 缺 stop 守卫（高）** —— 循环内的投递点没有 `!m_handshakeThreadStop` 判断，
   而循环末尾 `:735` 才重新检查停止位。时序：`:688` 条件通过 → `:730` `HasPendingMessages()` 为真 →
   `:732` 把 `[this]{ FlushPendingMessages(); }` 投进 runner → `:735` 发现要停 → 退出 → `~TransportService` join 并销毁对象。
   该 lambda 留在 runner 队列里，下一次 `World.cpp:105` 执行时对已析构对象调 `FlushPendingMessages`（读 `m_dispatchMutex`/`m_pendingMessages`）→ UAF。
   **`:745-747` 的注释恰好描述了这个 hazard，并把守卫加在了 `:748` 的循环后投递点——循环内的那处漏了。**
   （走 `BeginConnect:637` 的 join 不会 UAF，只是白投一个任务；要真正踩到需要 `~TransportService`。）

2. **`World.h:51-59` 声明顺序与注释相反（高）** —— 注释写「Transport before runner … 这让 runner 比 transport 活得久」，
   但 C++ 成员按**声明逆序**析构：`m_transport`(`:58`) 先声明、`m_runner`(`:59`) 后声明
   ⇒ **m_runner 先析构，m_transport 后析构**，即 transport 比 runner 活得久，与注释恰好相反。
   于是 `~TransportService` 执行 `StopHandshakePump` 的 join 期间，pump 线程仍可能执行 `:732`/`:750` 的
   `m_world.GetRunner().Queue(...)`，而 RunnerService（及其 TaskQueue）已经析构 → 对已销毁队列调用 `Add`。
   这正是注释声称已经修好的那个问题，实际顺序没修。修法是把 `m_runner` 声明到 `m_transport` 之前。

`ThreadUtils.{h,cpp}`：96 行，只有 `SetThreadName`/`SetCurrentThreadName` 两个函数，
`SetThreadNameWindows` 里 `GetProcAddress` 结果存进 `static const`（`:44-46`，函数内静态初始化线程安全），
其余全是无状态调用，**无线程安全问题**；本模块与 TransportService 无交叉。

---

## 缺陷清单（按严重度）

- [高] `TransportService.cpp:730-732` — 循环内投递 `[this]` flush lambda 没有 `m_handshakeThreadStop` 守卫，join 后该任务仍会执行 → 对已析构的 TransportService 调成员函数（UAF）。`:748` 有守卫，`:732` 漏了。
- [高] `World.h:51-59` — 声明顺序使 `m_runner` 先于 `m_transport` 析构（逆序析构规则），与 `:51-55` 注释声称的不变量相反；`~TransportService` 的 join 期间 pump 可向已析构的 RunnerService 投递。
- [中] `TransportService.cpp:159-163` — 注释断言「游戏线程不会持 m_clientMutex 等 m_dispatchMutex」，但 `:796` 拿到锁后 `:243` 是阻塞获取；结论（无死锁）仍成立，但书面论证的前提是错的。
- [中] `TransportService.cpp:199-201` — `Send` 失败静默无日志；74 个调用点仅 3 处检查返回值（`CharacterService.cpp:219/1809`、`PluginMessagingService.cpp:277`）。握手期间 pump 整段持锁（`:702-715`），CEF 线程的聊天/传送必然被丢。
- [中] `TransportService.cpp:248-249` — `m_pendingMessages` 无容量上限；握手未完成 + 帧循环卡住时服务端可无界推送，`WithinPacketLimit`(`:216`) 只限单包不限条数。
- [中] `TransportService.cpp:730-732` — 每个 8ms 迭代投一个 flush 任务，判据只依赖游戏线程消费，帧循环卡住时 runner 队列无界增长（≈125 个/秒）。
- [低] `World.cpp:105-106` + `TransportService.cpp:239-246` — pump 在 flush 与 UpdateEvent 之间解析入队的消息，会被游戏线程随后直接分发的新消息越过，产生单帧乱序（不重复处理）。

## 待验证（缺证据，无法静态闭合）

1. `~World` 是否真的会在运行期执行：`World` 是 `entt::locator<World>::emplace()` 的进程级单例（`World.cpp:135-137`），
   只在静态析构期销毁。上面两条 [高] 的**机制**已闭合，**触发频率**取决于进程退出时是否仍有 pump 在跑。
   需要运行期证据（退出时打印 `m_handshakeThreadActive`）才能定级。
2. `TiltedCore/Client.hpp`、`TaskQueue.hpp/.cpp` 未随仓库 vendor（`**/Client.hpp`、`**/TaskQueue.*` 均无匹配），
   `Client::Update()` 内部是否还会回调 `OnConsume`、`TaskQueue::Add` 是否有隐式上限，均无法从本仓库静态确认。
3. Q2 的「无死锁」结论建立在「m_dispatchMutex 只被游戏线程持有」之上；若将来有非游戏线程直接调用
   `DispatchMessage`/`FlushPendingMessages`，`:164` 与 `:243` 立刻构成 ABBA 死锁。这是约束而非当前缺陷。
