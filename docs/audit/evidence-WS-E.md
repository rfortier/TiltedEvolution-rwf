# WS-E 审计报告 —— 服务器面板与启动器

**范围**:server_ui 的 log / metrics / players / ui / launch / core,以及 server_runner 的 DediRunner / DediCoreBackend / DashboardHost / main。

## 发布路径 vs 非发布路径(重要)

`Code/server_ui/xmake.lua:3` 原文 "DELIBERATELY NOT REFERENCED BY Code/xmake.lua" —— **server_ui 自身的 target** 不进发布构建。但 `Code/server_runner/xmake.lua:62-73` **无条件**把它的纯逻辑源编进发布二进制:

~~~lua
-- Code/server_runner/xmake.lua:62-73
add_defines("SERVERUI_ENABLED=1")
add_includedirs("../server_ui/src")
add_files(
    "../server_ui/src/log/**.cpp",
    "../server_ui/src/launch/**.cpp",
    "../server_ui/src/core/**.cpp",
    "../server_ui/src/metrics/**.cpp",
    "../server_ui/src/players/**.cpp",
    "../server_ui/src/ui/DashboardModel.cpp",
    "../server_ui/src/ui/Sparkline.cpp",
    "DediCoreBackend.cpp",
    "RunnerLaunch.cpp")
~~~

结论:LogRing / LogModel / LaunchMode / ConsoleHost / ServerCore / MetricsSampler / LinuxProcParsers / PlayerRegistry / BanList / ModerationActions / DashboardModel / Sparkline **全部在发布二进制里**;只有 `tests/*` 与 `ui/DashboardImGui.cpp` 不在。**因此本报告不把任何一条当作"非发布路径"下调严重度。**

**已读的既有审计文档**:docs/audit/WS1-threading.md、WS2-hooks.md、WS3-protocol.md、WS4-papyrus.md(均无 server_ui / server_runner 条目);docs/PITFALLS.md 中与 DediRunner 相关的 §55(命令行 `std::sto*` 抛异常)、§110.2、§128(帧 delta 无上界)**已记录,本报告不重复**。

---

## 【可闭合】纯静态可证

### WS-E-01 · 编译期错误:Windows 分支引用只在 Linux 头里定义的常量 · **高** · 发布路径

**位置**:`Code/server_runner/main.cpp:412`;`Code/server_runner/DashboardHost.h:41-49`(缺定义);`Code/server_runner/DashboardHostLinux.h:50`(全仓唯一处定义)

~~~cpp
// main.cpp:387-420
#if defined(_WIN32)
        const int result = server_runner::RunDashboardWindow(
            model, hostActions, L"Skyrim Together Next - Dedicated Server", 1280, 800);
#else
        ...
        const int result = server_runner::RunDashboardWindowLinux(...);
#endif

        core.RequestStop();

        if (result == server_runner::kDashboardWindowUnavailable)   // 两个分支汇合之后
        {
            spdlog::error("The dashboard window could not be opened. ...");
        }
        serverui::ReleaseConsole();
        return result;
~~~

~~~cpp
// DashboardHostLinux.h:50   —— 全仓唯一定义处
constexpr int kDashboardWindowUnavailable = 1;
~~~

**问题**:`kDashboardWindowUnavailable` 只在 `DashboardHostLinux.h:50` 定义。Windows 分支包含的是 `DashboardHost.h`(`main.cpp:36`),该文件只声明 `RunDashboardWindow`(`:48`),**没有这个常量也没有别名**。而 `main.cpp:412` 位于 `#if/#else` **汇合之后**,不受平台门控。全仓 grep 该符号共 7 处:`DashboardHostLinux.h:50/54`、`DashboardHostLinux.cpp:94/109/130/145`、`main.cpp:412` —— Windows 侧零定义。

**后果**:`SERVERUI_ENABLED` + `SERVERUI_DASHBOARD_IMGUI` + `_WIN32`(正是 `server_runner/xmake.lua:83` 的默认 Windows 发布配置)这一翻译单元编译失败。**README 第 28-29 行宣传的 GUI 面板在 Windows 上编不出来。**

**最小改法**:在 `DashboardHost.h` 补一行 `constexpr int kDashboardWindowUnavailable = 1;`(与 Linux 头同值同注释)。

---

### WS-E-02 · 错误被丢弃:服务端停止监听后主循环仍满速空转并显示 Running · **中** · 发布路径

**位置**:`Code/server_runner/DediCoreBackend.cpp:39-48`;`Code/server_runner/DediRunner.cpp:138-152`;`Code/server_ui/src/core/ServerCore.cpp:523`、`:410-412`

~~~cpp
// DediCoreBackend.cpp:39-48   —— bool 返回值被丢弃
void DediCoreBackend::Update()
{
    if (m_runner != nullptr)
        m_runner->TickOnce();
}
// DediRunner.cpp:138-152
bool DediRunner::TickOnce()
{
    if (m_pServerInstance == nullptr || !m_pServerInstance->IsListening())
        return false;                  // 服务器已不在监听
    m_pServerInstance->Update();
    ...
    return m_pServerInstance->IsListening();
}
~~~
~~~cpp
// ServerCore.cpp:523   —— 循环条件只有 stop 标志
while (!m_stopRequested.load(std::memory_order_acquire))
...
// ServerCore.cpp:410-412  —— state 与 tps 都不来自服务器
    snapshot->state = m_state.load(std::memory_order_acquire);
    snapshot->tps = MeasureTps(aFrameEnd);
~~~

**问题**:`TickOnce()` 的 bool 是这条链上**唯一**能知道"服务器还在不在跑"的信号,它被 `Update()` 的 `void` 吞掉。`ServerCore` 的循环条件只有 `m_stopRequested`,**`ServerCore.cpp` 全文 0 处调用 `IsListening()`**(grep 确认)。于是 `TickOnce()` 返回 false 后:`m_pServerInstance->Update()` 不再执行,但循环继续按 tick 周期 `RecordTickStamp`(`:567`)+`PublishFrame`(`:574`)。

**后果**:`snapshot->tps` 由 core 自己的 tick 时间戳算出(`:381-397`),与服务器死活无关,继续显示满值;`snapshot->state` 取自 `m_state`,也没人改它。面板于是稳定显示 **Running + TPS 满值 + 玩家 0**,**且整条链上没有一行日志**(无 catch、无 log)。监听失败(端口占用)或服务端自行停服后,运维无法从面板察觉。

**与 WS-F 的关系**:`.audit/WS-F.md:519-532`(CTN-01)记录的是 `DediRunner::TickOnce` **内部** `uv_run` 的 fast_queue 自旋挂死;本条记录的是 `TickOnce` 的**返回值**被上游丢弃。两条不重叠:WS-F 的缺陷让服务器卡住,本条的缺陷让"卡住/已停"这件事在面板上不可见。

**最小改法**:`DediCoreBackend::Update()` 改为 `return m_runner->TickOnce();`,把 bool 写进 `ICoreBackend::Update` 契约;core 连续 N 次 false 后把 state 置 `Stopped` 并写一条 error 日志。

---

### WS-E-03 · `--headless` 走了 GUI 的解绑控制台分支,与 ConsoleHost 自身契约相反 · **低** · 发布路径

**位置**:`Code/server_runner/main.cpp:265-268`;`Code/server_runner/RunnerLaunch.cpp:63-67`;`Code/server_ui/src/launch/ConsoleHost.cpp:138-148`

~~~cpp
// main.cpp:265-268
    if (plan.wantsConsole)
        serverui::EnsureConsoleForMode(serverui::LaunchMode::Cli);
    else
        serverui::EnsureConsoleForMode(serverui::LaunchMode::Gui);   // Headless 也落到这里
~~~
~~~cpp
// RunnerLaunch.cpp:63-67
    case LaunchPlan::Mode::Headless:
        plan.wantsDashboard = false;
        plan.wantsConsole = false; // file logging only
        plan.constructRunner = true;
        break;
~~~
~~~cpp
// ConsoleHost.cpp:138-148
    case LaunchMode::Gui:
        // A GUI launch must never leave a console behind, whichever way the
        // process got one.
        DetachFromConsole();      // -> FreeConsole()
        return;
    case LaunchMode::Headless:
        // No console interaction at all. Do not allocate, do not attach, and do
        // not detach either: if a console was handed to us by the parent we
        // simply never write to it. Logging is file based.
        return;
~~~

**问题**:`LaunchPlan` 有四态且 `wantsConsole` 是独立字段,但 `main` 用一个布尔把它压回 Gui / 非 Gui 两态。`--headless` 的 `wantsConsole == false` → 被送进 `LaunchMode::Gui` → `DetachFromConsole()` → `GetConsoleWindow() != nullptr` 时 `FreeConsole()`。**这正是 ConsoleHost 自己写的 Headless 契约明令禁止的动作**("do not detach either")。

**后果**:`--headless` 在带控制台的 /SUBSYSTEM:CONSOLE 构建里会主动解绑进程控制台;`main.cpp:96` 的 `stdout_color_sink_mt` 写句柄随之失效,控制台输出被静默丢弃(文件日志仍在)。行为与"文件日志 only、不动控制台"的既定契约不符。

**最小改法**:让 `LaunchPlan` 直接携带 `serverui::LaunchMode`,`main` 原样传给 `EnsureConsoleForMode`,由 ConsoleHost 按枚举分派。

---

### WS-E-04 · 一次采样失效会让包速率显示成天文数字 · **低** · 发布路径

**位置**:`Code/server_ui/src/core/ServerCore.cpp:458-464`;`Code/server_ui/src/ui/DashboardModel.cpp:147-167`

~~~cpp
// ServerCore.cpp:458-464 —— 每帧都是新的 CoreSnapshot
    if (metrics.network.valid)
    {
        snapshot->netInBytesPerSec = metrics.network.inBytesPerSec;
        ...
        snapshot->packetsIn = metrics.network.inPacketsTotal;
        snapshot->packetsOut = metrics.network.outPacketsTotal;
    }
    // valid == false 时 packetsIn/Out 保持 CoreSnapshot 默认值 0
~~~
~~~cpp
// DashboardModel.cpp:147-167
    const uint64_t packets = acSnapshot.packetsIn + acSnapshot.packetsOut;
    if (!m_havePrevPackets) { m_packetsPerSec = 0.0; m_havePrevPackets = true; }
    else if (packets >= m_prevPackets && aNowMs > m_prevPacketsMs)
    {
        const double deltaSec = static_cast<double>(aNowMs - m_prevPacketsMs) / 1000.0;
        m_packetsPerSec = static_cast<double>(packets - m_prevPackets) / deltaSec;
    }
    else
        m_packetsPerSec = 0.0;      // 计数器"归零"被当成 reset
    m_prevPackets = packets;         // 同时把基线也清零
~~~

**问题**:`PublishFrame` 每帧都新建 `CoreSnapshot`,`network.valid == false` 的那一帧 `packetsIn/Out` 是 0。`DashboardModel` 把这个 0 当成"计数器 reset",于是 `m_packetsPerSec = 0` **并且 `m_prevPackets = 0`**。下一帧 `valid` 恢复时,delta 变成"从 0 涨到当前的全部累计包数",除以约 16 ms 的帧间隔。

**后果**:面板显示一个无意义的巨大包速率(量级 1e9～1e10 /s)。触发条件:Windows 上 `GetIfEntry2` 对缓存的所有接口都失败(`MetricsSampler.cpp:198-203` 会把 `m_interfaceListValid` 置 false),`anyInterface` 保持 false → `valid == false`;Linux 上 `ParseNetDev` 找不到任何非 lo 接口时同样返回 false(`LinuxProcParsers.cpp:195-199`)。**"采样中途失效"和"计数器真的归零"被当成同一件事。**

**最小改法**:`CoreSnapshot` 里带 `networkValid` 标志(或让 `DashboardModel` 收到 0 时**不更新基线**),只在两个有效采样之间算速率。

---

### WS-E-05 · `DediCoreBackend::IsListening()` 从不被调用,且语义与名字不符 · **低(信息)** · 发布路径

**位置**:`Code/server_runner/DediCoreBackend.cpp:56-63`;`Code/server_runner/DediRunner.h:53`

~~~cpp
// DediCoreBackend.cpp:56-63
bool DediCoreBackend::IsListening()
{
    // A pure query. It must NOT tick ...
    return m_runner != nullptr && m_runner->IsValid();
}
// DediRunner.h:53
bool IsValid() const noexcept { return m_pServerInstance != nullptr; }
~~~

**问题**:两件事叠在一起。① **它从未被调用** —— `ServerCore.cpp` 里 `IsListening` 出现 0 次;`ICoreBackend::IsListening`(`ServerCore.h:157`)是纯虚接口但 core 从不调用。② 名字说"在监听",实现是"对象构造成功" —— 真正的监听状态是 `m_pServerInstance->IsListening()`(`DediRunner.cpp:140` 用的正是它,`Code/server/main.cpp:49-52` 转发到 `GameServer::IsListening()`)。**端口绑定失败时 `IsValid() == true` 而真实的 `IsListening() == false`。**

**后果**:接口上留着一个会骗人的答案,而 WS-E-02 缺的恰好就是"服务器还在不在跑"这个信息。有人日后用它修 WS-E-02,会得到一个恒真的守卫,把缺陷焊死。

**最小改法**:给 `DediRunner` 加 `bool IsListening() const` 转发 `m_pServerInstance->IsListening()`,本函数改用它;或删掉这个 override 直到有调用者。

---

### WS-E-06 · "不进发布构建"的自述与 runner 的构建配置矛盾 · **信息(非缺陷)**

`Code/server_ui/xmake.lua:1-11` 声明这块"DELIBERATELY NOT REFERENCED BY Code/xmake.lua ... nothing here enters the shipping build",但 `Code/server_runner/xmake.lua:62-73` 无条件编译了其中七个来源目录。**两块说法互相矛盾**:纯逻辑确实在发布二进制里,只有 `tests/` 与 `ui/DashboardImGui.cpp` 不在。记在这里是为了让下一位审计者**不要**把 server_ui 的缺陷一律按非发布路径下调。

---

## README 两个声称的核实

### 声称 (a):「显式 `--gui` 但窗口打不开时进程报错退出而非静默降级」

**判定:代码意图成立,并在 Linux 分支上完整;但 Windows 分支因 WS-E-01 编不过,当前不可交付。**

~~~cpp
// main.cpp:404-420
        core.RequestStop();

        // No automatic downgrade. If the window could not be opened, the mode
        // the user (or the environment probe) selected is simply unavailable, and
        // silently continuing as a console server would hide that: a deployment
        // monitoring for a GUI would look healthy while running something else.
        if (result == server_runner::kDashboardWindowUnavailable)
        {
            spdlog::error(
                "The dashboard window could not be opened. The server was NOT started. "
                "Use --cli or --headless to run without a window.");
        }

        serverui::ReleaseConsole();
        return result;
~~~

- **没有回落路径(已逐条核对)**:`main.cpp:288-438` 的 `if (plan.wantsDashboard)` 块内**所有**出口都是 `return` —— `:302`(生成配置)、`:314`(`core.Start()` 失败)、`:420`(带窗口退出码)、`:428`(Win32 面板)、`:435`(非 Windows 无 GUI)。注释 `:291-294` 与 `:406-411` 也明写 "there is no automatic downgrade any more" / "No automatic downgrade"。**与 README 第 29 行后半句一致。**
- **不成立的部分**:① WS-E-01 使 Windows 构建连这段判定都编不出来;② Windows 宿主 `RunDashboardWindow` 只有 0/1 两个返回值(`DashboardHost.cpp:180`、`:188`、`:272`),"窗口打不开"与"其它非 0 结局"不可区分;③ 退出码 1 同时被 `main.cpp:314`(`core.Start()` 失败)与 `:435`(非 Windows 无 GUI)使用,运维无法从退出码区分"窗口打不开"与"服务器起不来"。
- **结论**:声称在 Linux 分支成立;Windows 分支是"注释写对了、符号写错了"。

### 声称 (b):「图形不可用时自动降级控制台」

**判定:成立,但只覆盖"启动期探测"这一层;README 的措辞比代码承诺得多。**

~~~cpp
// LaunchMode.cpp:261-276
    // One rule for both platforms: default to the GUI when a display can
    // actually be reached, otherwise the console.
    // ...
    // This decides ONCE, at startup, from the environment. It cannot know
    // whether the window will really open (broken X forwarding, a driver
    // refusing a GL context, Wayland without the right protocol), and it does
    // not try: if the window then fails, the server reports that and exits
    // non-zero rather than switching modes behind the operator's back. Run with
    // --cli or --headless to select the console deliberately on such a host.
    return IsGraphicalEnvironment() ? LaunchMode::Gui : LaunchMode::Cli;
~~~

- **成立的部分**:默认模式由上面的三元表达式决定。Windows 侧判据是 `IsServiceSession()`(`:159-167`,读 `SESSIONNAME` 是否等于 "Services")与 `HasInteractiveWindowStation()`(`:135-155`,`GetUserObjectInformationA(UOI_FLAGS)` 取 `WSF_VISIBLE`,再 `OpenInputDesktop`);非 Windows 侧判 `DISPLAY` / `WAYLAND_DISPLAY` 非空(`:226-232`)。探测为假 → `Cli` → 控制台路径正常启动。**这一层与声称一致**,且 `launch_mode_test.cpp:80-90` 覆盖了三种环境。
- **不成立的部分**:注释自己写清楚了 —— 探测**无法**知道窗口会不会真的打开。所以**默认模式下窗口失败同样报错退出、不降级**。README 第 29 行的措辞容易被读成"只有显式 `--gui` 才不降级",而代码是"任何 Gui 决策失败都不降级"。这是**文档精度**问题,不是代码缺陷。
- **README 未区分的第三处差异**:没有 imgui 包时,Windows 走 `main.cpp:421-428` 退回无依赖的 Win32 面板并返回它自己的结果 —— 这**确实换了一种 GUI 继续跑**;而 `main.cpp:429-435`(非 Windows 且无 GUI)直接 `return 1`,**不降级到控制台**。两条路径行为不同,README 一句话没说。

---

## 明确排除的假设(经实测/精读后判定不成立)

为免下一位审计者重走,记录我验证过并**否决**的四个候选与否决依据。

1. **`LogRing::Push` 的 relaxed CAS 会造成撕裂记录 —— 不成立。**
   `LogRing.cpp:65` 的 `compare_exchange_weak(pos, pos+1, relaxed)` 看起来只有 `:88` 的 release 才发布数据,但 `diff == 0` 要求 `state == pos`,`Drain` 交还时写的是 `pos + m_slotCount`(`:128`),因此同一槽的两个相邻 ticket 之间**不可能**出现 `state == pos` 的窗口。
   **实测**:`.audit/_probe/lr2.cpp`,4 生产者 × 300k、payload 全为同一字符,消费者逐字节校验 —— `seen=327952 mixed=0` / `seen=462712 mixed=0`,两轮均无混合或撕裂;另用 `lr.cpp` 校验 `len == strlen(text)` 与内容一致:`bad=0`。**不是缺陷。**

2. **`ServerCore::ApplySettings` 超时后 `future` 析构会阻塞 —— 不成立。**
   `std::future` 析构**不等待**共享状态就绪(会阻塞的只有 `std::async` 返回的 future)。**实测**(`.audit/_probe/fut2.cpp`):promise 未满足时销毁 future — 0 ms;promise 先销毁则 `get()` 抛 `Broken promise`。`ServerCore.cpp:253-258` 的超时返回是即时的。**不是缺陷。**

3. **`LogModel::Append` 的 `text.assign(ptr, len)` 会越界 —— 不可达。**
   `LogModel.cpp:75` 确实信任 `len`,但两个调用源都自洽:`LogRing::Drain` 保证 `len == strlen(scratch)`(`LogRing.cpp:113-125`,并由上述 `bad=0` 实测确认);`DashboardModel::EchoLine`(`:436-437`)用 `size()`/`data()` 同源。唯一理论缺口是 `size_t -> uint32_t` 截断,需要 > 4 GiB 的单条命令。**不作为缺陷报告。**

4. **`DashboardModel` 的空 viewport 会吞掉滚动 —— 不可达。**
   `DashboardImGui.cpp:400-402` 用 `std::max(1.0f, listHeight / rowHeight)` 把 viewport 钳到 **≥ 1**,因此 `SetLogViewportRows(0)` 在真实消费路径上不会发生。**不作为缺陷报告。**

---

## 【运行时依赖】

### WS-E-R1 · `--gui` 在"变量存在但显示服务已死"的主机上 · **中**

`IsGraphicalEnvironment()` 在 Linux 上对 `DISPLAY=:0` 但 X 服务器已崩、或 `DISPLAY` 指向不可达 TCP 目标时仍返回 true(只判变量非空,`LaunchMode.cpp:226-232`)。此时默认 Gui → 窗口创建失败 → 按声称 (a) 退出。**是否真的退出、退出码是否可观察,取决于 `DashboardHostLinux.cpp:94/109/130/145` 那 4 个 `return kDashboardWindowUnavailable` 是否覆盖了全部失败形态**;该文件只在 Linux 编译,本机无法验证。

### WS-E-R2 · UI 线程的 drain 循环无上界 · **低**

`main.cpp:356-362` 的 `for(;;)` 一直 `Drain` 到空才返回。单次 `Drain` 有界(`≤ SlotCount()`),但**整个循环没有条目上限**。若生产者速率持续高于消费者的拷贝速率,这一帧的 UI 线程会被拖住。`LogRing` 用丢弃而非阻塞保护生产者,理论上会收敛;实际是否收敛取决于真实日志量与 CPU 竞争,本机无法端到端验证。

### WS-E-R3 · `/proc/self/stat` 字段布局假设 · **低**

`LinuxProcParsers.cpp:94` 循环 **12** 次恰好落在 utime/stime(代码正确);但紧邻注释 `:90-93` 写的是 "the loop must run thirteen times" —— **注释与代码不符**(照注释改成 13 会取到 stime 之后的字段)。`ReadU64` 只保证"能解析出一个数",不保证"解析的是 utime";若内核改变 ')' 之后的布局会静默取错值。

---

## 已确认无问题的关键不变量

1. **`LogRing::Drain` 的 scratch arena 不会越界** —— `:101` `cap = min(aMaxRecords, m_slotCount)` 与 `:114-118` 的 `scratchUsed += len + 1` 上界 `m_slotCount * (kPayloadBytes + 1)`(`:34`)精确吻合。**实跑** `log_ring_test` 子测试 (e):`maxBatch=8 (limit 8) maxBytes=3656 (limit 3720)`。
2. **满环丢弃而非自旋** —— `LogRing.cpp:70-75` 在 `diff < 0` 时直接返回 false。实跑子测试 (d):`accepted=4 slots=4 dropped=12`。
3. **`Push` 入参校验完整** —— `:48` 同时拒绝 `aLen > kPayloadBytes` 与 `(aLen != 0 && acData == nullptr)`,空指针进不了 `memcpy`。
4. **`LogModel` 绝对索引不回绕** —— `m_nextAbs` 单调 `uint64_t`;`EvictOldest`(`:92-98`)、`RebuildVisible`(`:113-122`)、`Export`(`:157-159`)都用 `m_nextAbs - m_stored`,而 `m_stored <= m_capacity` 由 `:67-68` 维持。
5. **`ContainsFold` 无越界** —— `:21-24` 先判 `needle.size() > haystack.size()`,内层 `i + j` 上界恰为 `haystack.size() - 1`。
6. **`Sparkline` 环形索引自洽** —— `m_capacity >= 1`(`:9`)前提下 `Push`(`:14-27`)/`IndexOf`/`At` 正确;`NormalizedAt` 对 `aMin > aMax` 交换、`span <= 0` 返回 0.5、结果钳到 [0,1](`:90-103`)。实跑 sparkline_test 全过(含容量 1 与 100 次回绕)。
7. **`MetricsSampler` 除法守卫齐备** —— `:159/:221/:265/:273/:316` 五处除法前均判 `wallUs > 0` / `wallSeconds > 0.0`;`cores == 0` 时 ceiling 回落 100(`:165/:276`)、`percentOfMachine` 置 0(`:167/:278`);累积量倒退时显式跳过并重建基线(`:221-222`、`:316-318`)。
8. **采样节流有效** —— `:125-130` 在 `m_hasBaseline` 之后按 `kRecommendedIntervalMs` 返回缓存,不会把 delta 窗口压到微秒级。
9. **`LinuxProcParsers` 边界正确** —— `ReadU64` 上限饱和(`:44-47`);`ParseSelfStat` 用**最后一个** ')' 定界以容纳 comm 内括号(`:62-73`);`ParseNetDev` 把 `lineEnd` 当上界传给 `ReadU64`(`:178`)故不跨行;16 字段不齐则整行丢弃(`:176-186`)。实跑 linux_proc_test 的 18 条断言全过。
10. **`BanList` 转义与原子保存** —— `EscapeField/UnescapeField`(`:41-86`)成对处理四种转义,未知转义保留两字符不丢数据;`Save`(`:239-267`)写 `.tmp` 再 `rename`,失败时删临时文件;`DecodeEntry` 要求恰好 5 字段(`:118`)、两标识全空则拒(`:132-133`)。实跑 players_test 通过(含真实文件往返)。
11. **`ModerationActions` 溢出守卫** —— `ParseDurationMinutes`(`:47-68`)逐字符累加并每步比较 `kMaxMinutes = UINT64_MAX / 60000`(`:39/:59`),故 `:160` 的 `nowMs + minutes * kMsPerMinute` 不会回绕;`Ban` 拒绝自封(`:112-113`)与空 endpoint(`:119-120`)。
12. **`ServerCore` 发布/消费无竞争** —— `m_snapshot`/`m_players` 用 `std::atomic<std::shared_ptr<const T>>`,`Snapshot()`(`:144`)/`Players()`(`:166`)各一次 acquire load;`m_nameArena[2]` 双缓冲(`:186-212`)保证上一次 `Players()` 发出的 `const char*` 在这次调用后仍可读。
13. **`ServerCore` 唤醒不会丢** —— `WakeCore()`(`:331-342`)先取 `m_wakeMutex` 再 `notify_all`,与 `:595` 的带谓词 `wait_until` 配对;`m_pendingWork` 在同一临界区内**先加后 push**(`:326-327`),不会出现"消费到未计数的工作"导致无符号回绕(该回绕会 100% CPU 空转,注释 `:320-325` 已记录)。
14. **`~ServerCore` 不会 join 自己** —— `RequestStop`(`:115-116`)判 `this_thread::get_id() == m_thread.get_id()` 后只置标志。
15. **ring/logger 析构顺序正确** —— `main.cpp:282` 的 `logRing` 声明在 `:285` 的 `LogInstance logger` 之前,故析构在其后;`spdlog::flush_every` 的后台线程不会在 ring 释放后 push(`:271-282` 注释)。
16. **测试不是假测试(逐条核过并实跑)** ——
    - `log_ring_test.cpp:170` `got + dropped == total`、`:245-251` 无丢弃时逐条 `got[i] == got[i-1]+1`、`:405` `n <= slots`、`:417` `bytes <= arenaBytes`;实跑 `[a]` 得 `pushed=400000 received=86234 dropped=313766`,并发压力是真的。
    - `sparkline_test.cpp:69` 的期望值是随 ring 状态变化的参考公式 `i + 1 - Size + k`,不是恒等式。
    - `metrics_test.cpp:143-153` 断言"busy > idle + 10 个百分点"与"busy > 0",常量实现过不了。
    - `linux_proc_test.cpp` 18 条既有正例也有反例(`:87-91`、`:132-146`)。
    - `launch_mode_test.cpp` 51 条覆盖优先级、别名、两种配置写法、缺失/空/未知值。
    - **未发现恒真断言。** 本机实跑 7 个测试二进制(log_ring / log_model / sparkline / dashboard_model / linux_proc / players / launch_mode)**全部 exit=0**。

## 未覆盖区域

1. **`Code/server_runner/DashboardHostLinux.cpp`(145+ 行)未读** —— 本机 Windows,该文件只在 `is_plat("linux")` 下编译;无法判断 4 处 `return kDashboardWindowUnavailable` 之前的资源清理是否完整(GLFW 窗口 / GL 上下文 / ImGui 上下文 / VBO 的释放顺序),也无法验证 WS-E-R1。
2. **`Code/server_ui/src/ui/DashboardImGui.cpp`(673 行)只读了 390-444 行** —— 读到的部分(`SetLogViewportRows` 钳到 ≥1、`LogRowAt` 空指针检查、`Text("%s", record->text)` 不按 `len` 读)没有问题;其余约 600 行(主题、玩家表右键菜单、配置面板)未审。
3. **`Code/server_runner/gui/ServerGui.cpp`(无 imgui 包时的 Windows 面板)未审查** —— 它是 `main.cpp:421-428` 分支的实现,与 WS-E-01 无关(两分支 `#elif` 互斥),但面板自身未看。
4. **`Code/server/main.cpp:114-141` 的 `QueryServerPlayers` 只读了注释与主体结构** —— 注意它自述是**跨线程无锁读取玩家管理器**(`:109-113`:"the unlocked cross-thread read documented in docs/design/SERVER-UI-ARCHITECTURE.md (finding F10)"),属**已在设计文档记录**的既有问题,不属本批新增,未展开。
5. **Windows 专属运行路径无法实测** —— 本机无 MSVC / D3D11 / iphlpapi 开发环境。`MetricsSampler` 的 Windows 分支(`:143-239`)、`ConsoleHost` 的 `AttachConsole/AllocConsole/freopen_s`(`:109-168`)、`LaunchMode` 的 `GetUserObjectInformationA/OpenInputDesktop`(`:135-155`)只有静态判据。**尤其:`freopen_s` 在 MinGW 下是否存在**(注释 `ConsoleHost.cpp:56-57` 自称 "MinGW also provides it")未验证;若不存在,那是一个与 WS-E-01 同性质的 MinGW 编译错误。
6. **未做长时间 soak** —— `ServerCore` 的 tick 追赶逻辑(`:586-605`)在真实 Windows 定时器粒度下的长期漂移未测;WS-E-02 的"面板显示 Running 但服务器已死"是静态链路推断(循环条件与 state 来源已逐行核对),**未在真机端到端复现端口占用场景**。
7. **环境限制(影响取证手段,不影响结论)** —— 本机 GCC 16 缺 `libtsan`(`ld.exe: cannot find -ltsan`);`ops_bash` 在本环境启动即失败(`bash: fatal error - CreateFileMapping ... Win32 error 5`),`win_bash` 因无全权限被拒,PATH 上无 `rg`。全程改用 pwsh + grep 工具;并发结论改用构造性反例程序(lr.cpp / lr2.cpp)取得,未用 TSan。