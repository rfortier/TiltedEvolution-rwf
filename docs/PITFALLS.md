# 踩坑记录与已知问题清单（提交前必读）

> 本文件是 SkyrimTogetherNext（TiltedEvolution fork）在 1.5.97 / 1.6.x / 1.7.x 联机修复过程中，
> **所有踩过的坑 + 已修复/未修复 bug** 的留档。目的只有一个：**后续每次修改、每次提交前，
> 先对照 §0 核对清单和正文，不要再重复踩坑。**
>
> 更新约定：每次修复一个新 bug 或踩一个新坑，**当场**追加到本文件；正文以"现象 → 根因 → 修复 →
> 教训"四段式记录，并带上 commit hash 作为可追溯锚点。

---

## 0. 提交前核对清单（每次 commit 前逐条过）

改动涉及哪类，就过哪几项；全绿再提交。

- [ ] **没动子模块** `Libraries/{TiltedUI,TiltedHooks,TiltedReverse,TiltedConnect}`。改了 = 白改
  （CI 前 `git submodule update --init --force --recursive` 会丢弃本地改动）。overlay/input 修复必须放 `Code/`。
- [ ] **没在本地编译**。Windows/MSVC 目标只能在 GitHub CI（`windows.yml`，push `main` 触发）验证；
  本机 xmake 只读配置。`build/`、`.xmake/` 是本地产物，可删、勿提交。
- [ ] **改动若碰了结构体偏移** → 先判"锚点式 pad"还是"相对式 pad"（§2），锚点式**禁止用「上游值−8」**；
  改完更新两分支的 `static_assert`，让下次漂移在编译期炸出来，而不是静默读邻居。
- [ ] **若同步过上游** → 跑了"新增语句行存活率"扫描 + `git diff 5a948fcd HEAD -- Code/client/Games/Skyrim/Forms/`
  找"fork 有 HEAD 无"成员（§3）。
- [ ] **若新增/改动消息类** → 核对 `T::Opcode` 全仓无重复（`ServerMessageFactory.cpp` 的
  `s_serverMessageExtractor[T::Opcode]` 是注册入口，visitor 顺序无关）。
- [ ] **若改动 UI↔C++ 桥接** → 新 JS→C++ 调用必须在 `ProcessHandler.cpp::OnContextCreated` 注册；
  C++→JS 带参数必须 `ExecuteAsync(name, pArgs)` 传 `CefListValue`（§6）。
- [ ] **若给帧循环加/换驱动源** → 复刻原驱动源的**门控条件**（`SkyrimVM::inactive` / 玩家存在性），
  否则启动期会提前跑整套 `Update()`（§5，v1.0.32 启动闪退就是这么来的）。
- [ ] **若改了 `xmake.lua` 的 `set_xmakever()`** → 与 `windows.yml` 的 `xmake-version:` 同步。
- [ ] **若新增 `GameFiles/` 内容** → `GameFiles/Skyrim/fomod/ModuleConfig.xml` 的
  `requiredInstallFiles` 跟进，否则漏装且 `IsDefaultModlist` 误报"非原版安装"。
- [ ] **若改 FOMOD 文案** → 中英**两条分支都要改**（`可选组件` 与 `Optional components` 是两份
  独立 XML，改一份忘一份就会让另一种语言显示旧文案）；XML 必须 UTF-8 **无 BOM**，
  且任何"读-改-写"脚本都要**显式指定编码**（§18.3）。
- [ ] **若改了 TS/SCSS** → 独立 tsc 验证（比对改动前后错误数）；本项目 `rem()` 是两参数
  `rem($base,$pixels)`，别当 CSS 的单参数 `rem()` 用（§6）。
- [ ] **版本握手**：客户端/服务端必须**同一 commit** 构建（握手比对 `BUILD_COMMIT`），新旧不能混用。
- [ ] **`.workbuddy/` 不提交**（已在 `.gitignore`）；commit 前 `git status` 确认干净。

---

## 1. 硬约束（绝对不能违反）

- **禁止本地编译**（Windows/MSVC 目标）。验证一律走 GitHub CI（`windows.yml`，`windows-2022`/x64，
  push `main` 触发）。报错 → 静态分析 + 看 Actions。
- **禁止改子模块**（见 §0 第一条）。
- `gh` CLI 本机不可用；GitHub REST 匿名常 403 限流。绕行：
  - CI 状态看徽章 `.../actions/workflows/windows.yml/badge.svg?branch=main&t=$(date +%s)`
    （失败约 4 分钟显现）。
  - `github.com` 走不通时 `api.github.com` 往往通：`api.github.com/repos/<o>/<r>/actions/runs`、
    `/releases`。
  - 有凭据时 `TOKEN=$(sed -n 's#https://[^:]*:\([^@]*\)@github.com#\1#p' ~/.git-credentials)`
    加 `Authorization: Bearer $TOKEN` 查 Actions API。
- `IS_MASTER` 由 xmake `before_build` 从 **git 分支名**推导，CI 从 `main` 构建 → 发布版。
  release 里 `#if (!IS_MASTER)` 全被裁掉（F6/F7/F8 快捷连接、debug 菜单内容都不存在）。

## 2. 结构体偏移（最容易反复咬人）

**没有通用公式。先判"锚点式"还是"相对式"。**

- 相对式 pad（`pad_ptrs2[8]` vs `[9]`）：自动跟随，可用「上游值 −8」。
- **锚点式 pad（`pad1[0x580 - sizeof(Actor)]`）：锚点一个人决定整条尾巴，必须独立定值。**
  用"上游值 −8"必然重复计算 → 字段整体漂一个 qword（读成邻居）。
  → 这就是 2026-09-19 新游戏闪退的根因（`c0abe640` 修）。

`PlayerCharacter.h` legacy 真值（**照抄，别重算**）：objectives `0x580` / pSkills `0x9B0` /
locationForm `0xAC8` / baseTints `0xB10` / overlayTints `0xB28` / sizeof `0xBE0`；
AE 每项 +`0x10`。上游 `6f963497` 抬过 `pad1`，**同步时不要把 legacy 锚点跟着改**。

`SkyrimVM::inactive`（`GameVM.h`）：legacy **0x680** / AE **0x690**（差 0x10）。
`SkyrimVM::virtualMachine`（同文件）：legacy **0x200** / AE **0x210**（同样差 0x10）。

> 这个坑到目前为止咬了**三次**：① `PlayerCharacter::pad1`（`c0abe640`）；
> ② `SkyrimVM::inactive`（`b0082844`）；③ `SkyrimVM::virtualMachine`（§12.3）。
> 三次都是"合并时把上游值当成绝对值吹进 legacy 分支"。
> 上游的"修正"是**对 AE 而言**的修正——合并时先问一句
> “这是绝对值还是相对增量”。

查证手段（照抄，别重算）：
1. 逐字节哈希：`[ "$(git rev-parse v1.0.18:$f)" = "$(git rev-parse HEAD:$f)" ]`。
2. 提取 legacy 分支比对（**遇 `#else` 必须清空已收集 body**，否则取到非 legacy 侧）。
3. `static_assert` 集合对账，一次揪出所有被动过值的头文件。
4. 或 `git log -S"<pad 字面量>" --oneline -- <file>` → `git show <sha>:<file>` 照抄断言表。
5. legacy 分支还要核对**是否缺上游新增成员**（如 `TESObjectCELL::cellFlags[5]` 被拆成
   三个字段 + `IsAttached()`）⇒ 只有 1.5.x 构建失败。

判据：读到 `0xFF7FFFFF`（`-FLT_MAX`，落在 `0xFF000000..0xFF7FFFFF` 未映射哨兵带）
≈ "读偏了一个字段"，不是野指针。

## 3. 上游同步 / 合并（每次 merge 后的高危覆盖点）

- `Code/client/main.cpp`：上游硬编码 `kSupportedGameVersions = 1.7.104.0` 且不匹配就 `exit(4)`，
  fork 已改为 `1.5./1.6./1.7.` 前缀判断，合并会把它冲掉。
- `TiltedOnlineApp.cpp`：`LoadScriptExtender()` **全树只能一处调用**（两处 → papyrus native 注册异常）。
- **游戏表单头文件里 fork 新增的成员会被同名文件静默覆盖**（不在冲突列表里）：
  吃过 `TESObjectARMO::IsBodyPiece()`、`TESNPC::outfits[2]`、`VersionDb.h` 整文件。
  必做：`git diff 5a948fcd HEAD -- Code/client/Games/Skyrim/Forms/` 找"fork 有 HEAD 无"成员。
- **消息协议**：注册靠消息类静态成员 `T::Opcode`；真风险是两个类声明同一 Opcode。
- `xmake.lua` 的 `set_xmakever()` 必须与 `windows.yml` 的 `xmake-version:` 同步。
- 上游新增 `GameFiles/` 内容 → `GameFiles/Skyrim/fomod/ModuleConfig.xml` 的 `requiredInstallFiles` 跟进。
- **自动合并会静默吃掉声明和调用点** ⇒ 冲突解决完不能只 grep 函数名；用"新增语句行存活率"
  比对 fork/上游各自相对分叉点 `5a99a0b6` 的新增行是否还在。本次合并后最值钱的扫描是
  **"局部变量完整性"**（抓半截 hunk 拼接）。
- `.gitmodules` 幽灵 submodule：删掉残留 `[submodule]` 条目（`66d61839`），否则拉远程报错。

### 3.1 同步上游 v1.8.2（2026-09-26，`e0002073` → `0ffb80b0`）

上游 3 个提交：#899（按"原始 NPC + 房主选中的模板"重建等级化 NPC 基础）、
#893（排除各派系的监狱储物容器）、#900（合并）。逐文件对齐后的**判断**：

- **`TESObjectREFR::SetBaseForm` → `SetObjectReference`**：上游只是把虚函数改名。
  fork 侧只有 `Actor.cpp` 一处调用，跟着改即可；`static_assert` 表没动（名字不改布局）。
- **`TESObjectREFR::SetLeveledCreature`（id 20231）/ `GarbageCollector`（单例 400329、
  方法 36460）/ `CreateTemplateActorBase`（14375）**：**1.5.97 映射表只认 14375**，另三个全缺。
  上游写法在这里会踩两个坑，都已按 fork 逻辑绕开：
  1. `GarbageCollector::Get()` 用 `POINTER_SKYRIMSE` 解引用单例 ⇒ 未映射时拿到**零返回桩**，
     `*stub()` 就是**解引用地址 0**（而且是在每次等级化 NPC 重建时都踩）。
     已改为 `FindAddressById` 直查，未映射返回 `nullptr`，
     调用点判空后**保留**旧临时 base（泄漏一个临时 form，好过崩）。
  2. 上游用 `GetLeveledPick() == pPick` 判定"重建完成"，而该比较读的正是
     `SetLeveledCreature` 写进去的 extra data ⇒ 1.5.x 上**永不相等**，
     `WaitingFor3D` 状态会**每帧重新 Disable/Enable**（不是不生效，是死循环）。
     已加 `LeveledNpcSystem::CanRecordPick()`（`static` 缓存一次），不可写时退回
     fork 原有的"base 直接置为 pick"路径，并把完成判定切回 `baseForm == pPick`。
  > 教训同 §2：**上游的"修正"是对 AE 而言的**。这次不是偏移，是"这个 id 在 1.5.x 根本不存在"。
  > 判据：新引入的 id 先去 `versionlib-ae-to-se-1-5-97-0.map` 里查一行，
  > 缺了就必须问"缺了会怎样"，而不是"反正有桩兜底"。
- **`ModManager::factions`（TES.h）**：不是拍脑袋的偏移。form 数组从 0x10 起、
  每项 0x18、按 FormType 索引：Faction(11) → `0x10 + 0x18*11 = 0x118`，
  已有的 Quest(77) → `0x748`，与上游值**互相印证**。三个 `static_assert` 同时钉住。
- **`TESFaction` 补齐 CrimeData**：成员全在 form 记录本体里，**不在** ExtraDataList 后面，
  所以 1.5.x 那 8 字节差**不经过这里**，一套偏移通吃两个分支（已写进注释）。
- **`Tools/missing_1_5_97_ids.txt`**：本次新增三个未映射 id（`20231`、`36460`、
  单例 `400329`），覆盖率 **3069/3080（99.6%）**，清单同步更新。
- **覆盖率数字本身曾经是错的**：`gen_ae_to_se_map.py::collect_codebase_ids()` 用
  `*.[ch]pp` 通配，**301 个 `.h` 一个都没扫到**，还把**注释掉的调用**当成活的
  （`TESActorBaseData.cpp` 里就有一处），所以旧口径 3075 既漏 8 个又虚增 3 个。
  已改为显式列 `cpp/hpp/h` 三种后缀、先剥注释、并把 `FindAddressById` 计入。
  > 教训：**"覆盖率"是个口径问题，不是事实问题**。对不上时先跑生成器本身
  > （`importlib` 直接调 `collect_codebase_ids()`），别自己写正则——自写的松口径
  > 会得出 3078、3080 这种和工具都对不上的数。

## 4. launcher vs SKSE 差异

1. 页面保护：SKSE 下 `.text` 是 RX，直写会 AV → 需临时 RWX 窗口（`GamePatch.h`）。
2. 未映射 ID → `VersionDbPtr` 返回共享 7 字节空桩；把补丁点当锚点会破坏它
   ⇒ 锚点必须用不降级的 `FindAddressById`（`GamePatch::Anchor`）。
3. 运行时 DLL 与镜像距离 ~20GB ⇒ 写 rel32 静默截断，症状 "jumped to code that is not there"，
   调用方显示成正常游戏函数（P0，`00ec7306`）。修法：先跳镜像附近桩
   （`ff 25 00000000` + 8 字节绝对地址），桩用 `RipAllocateN` 分配。`TP_HOOK`/MinHook 不受影响。

## 5. 帧循环与联机传输（连接挂死 / 不同步 / 启动闪退都在这）

**调用链唯一**：`SkyrimVM64.cpp::HookVMUpdate`（id 53926 → 1.5.97 RVA `0x921F10`）
→ `TiltedOnlineApp::Update()` → `World::Update()` → `trigger(PreUpdateEvent)` +
`RunnerService::OnUpdate` 抽干队列 + `trigger(UpdateEvent)`。
**`Client::Update()`（uv_run + RunCallbacks + 收包）全仓唯一调用点 = `TransportService.cpp:308 HandleUpdate`**
⇒ 网络推进完全挂在游戏 tick 上。

**诊断公式**（连接挂死）：
> connect 已打出 `connecting to ... giving up after 10s`，但接下来 15 秒零日志，
> **连 `Handshake ... timed out after 10s, aborting` 都没有** ⇒ `HandleUpdate` 一次没跑
> ⇒ tick 没驱动 Update。再叠加"按 Disconnect 也没有 `Disconnected from server (4): aborted`"
> ⇒ uv_loop 从没被泵过（否则 `uv_cancel` 回调必出声）。主线程还活着 ⇒ 不是泵里死循环。

**关键教训（按时间顺序，每一条都是坑）**：

1. **握手无超时 / 取消被 UI 阻断 / 重复连接叠加**（`66851aaf`）：`TransportService` 只在
   `Client::OnDisconnected`（Steam 状态回调）才知道失败；DNS 无响应或服务端不回认证时无任何
   计时器兜底；取消按钮 `[disabled]="connecting"` 且 `cancel()` 不调 `disconnect()`；
   `ProcessConnectMessage` 不先 `Close()` 就 `Connect()`。修法 = `ArmConnectionWatchdog` 10s 看门狗 +
   取消真正中止 + 先 Close 再 Connect。
2. **去重标志别合用一个 `m_attemptOutcomeReported`**（`83f3f89e` 引入，`6acd6ef8` 修正）：
   失败路径"先报错误、后 Close() 同步回调 OnDisconnected"，共用标志会被先到的置位 →
   `DisconnectedEvent` 仍不发。必须按通道拆 `m_attemptErrorReported` / `m_attemptDisconnectReported`。
3. **`ExecuteAsync("disconnect")` 不传参 = JS 收到 `undefined`**：`OverlayService::OnDisconnectedEvent`
   一直是 `ExecuteAsync("disconnect")` 无参，JS `onDisconnect(isError)` 靠 `isError` 判断自动重连 →
   **自动重连分支自 web UI 诞生起就是死代码**（上游 `dev` 同款缺陷，fork 独修）。
   修法：`DisconnectedEvent` 加 `bool IsError`，`pArgs->SetBool(0, ...)` 转发。
4. **不碰游戏内存的动作别走 `RunnerService::Queue`**（v1.0.28）：`Close()`/`Connect()`/
   `ArmConnectionWatchdog()` 已在 CEF 消息线程直接同步调用。
5. **传输泵搬离帧循环**（`5efab47f`/`d9f01e7e`）：handshake 期间专用线程 + `std::recursive_mutex`，
   pump **只解析不分发**（`OnConsume` 比对线程 id，非游戏线程则入 `m_pendingMessages`），
   再 `m_world.GetRunner().Queue` 交还游戏线程。两条不变量别破坏：
   - pump 只在帧循环缺席时跑（`HandleUpdate` 先盖 `m_lastGameThreadTickMs` 再判所有权）；
   - `FlushPendingMessages` 先 swap 出来再分发，避免 ABBA 死锁。
6. **1.5.97 两个帧钩子一次都没被调用**（v1.0.32，`a9ad774f`）：`HookVMUpdate`(0x921F10) 和
   `HookMainLoop`(0x5B2FF0) 心跳整场为 0，但钩子"0 did not land" ⇒ 这两个地址库条目**不在执行路径上**
   （map 是从历史符号 join 来的，疑似当时就贴错）。修法 = 帧循环改由**窗口定时器**驱动
   （`InputService::WndProc` 首条消息 `SetTimer(16ms)`，`WM_TIMER` 里 `World::Get().Update()`，
   `return 1` 吞掉消息）。`World::Update()` 加 8ms 自节流 + `m_hasTicked` 去重。
7. **新驱动源无门控 → 启动闪退**（`83bb37be`，v1.0.32 的修复引入）：定时器在**首条窗口消息**
   （启动期）就 armed，第一次 tick 把整套 `Update()` 在玩家还不存在时跑起来 →
   `DiscoveryService::VisitForms()` 第 251 行 `visitor(PlayerCharacter::Get())` 未判空，
   `[NULL+0x14]`（`TESForm::formID` 在偏移 0x14）read fault。AE 天然安全是因为 `HookVMUpdate`
   只在 `SkyrimVM::inactive==0` 时跑。修法 = 给定时器补等价门控
   `if (s_pOverlay && s_pOverlay->GetInGame()) World::Get().Update()`，并给第 251 行补判空。
   **教训：给帧循环加新驱动源时，必须复刻原驱动源的门控条件。**
8. **跨线程 libuv**：`ProcessConnectMessage` 在 CEF 消息线程调 `Client::Connect()`，
   而 uv loop 归游戏线程所有。字面 IP 走 `ConnectByIp()` 完全绕开 `uv_getaddrinfo`（`IsLiteralIPV4()`）。
9. **`WndProc` 转发语义**：`WindowsHook` 回调返回 0 = 转发给游戏，非 0 = 吞掉。
   `WM_TIMER` 要 `return 1`，其他分支照旧 `return 0`。

## 6. UI ↔ C++ 桥接（编译期零报错，全靠记忆）

- **新增 JS→C++ 调用必须在 `ProcessHandler.cpp::OnContextCreated` 注册**，否则
  `skyrimtogether.<fn>()` 是 `undefined`。`OverlayV8Handler::Dispatch` 把 V8 函数名原样当
  ui-event 名转发 ⇒ 名字必须逐字一致。
- **C++→JS 带参数时 `ExecuteAsync(name)` 不传 `CefListValue` = 参数全是 `undefined`。**
  加事件参数要同时改：事件结构体 + `pArgs->SetXxx` + `ExecuteAsync(...,pArgs)` + JS 形参 +
  mock + typings。
- **多通道标志位别合用一个 `m_attemptXxxReported`**（同 §5.2）。
- 日志证据：`OverlayClient.cpp:36-39` 的 4 行 ui_event info 在分发**之前**打印 →
  它们出现而目标动作首行不出现 ⇒ 事件到了、lambda 没执行。
- **TS 改动验证**：`skyrim_ui` 无本地 node_modules，用 /tmp 独立 tsc + 手写 stub `.d.ts`；
  `tsconfig.base.json` 未开 strict，比对"改动前后错误数是否相同"才算有效验证。
- **SCSS `rem()` 是两参数**（`rem($base,$pixels)`，`src/styles/functions/rem.scss`），
  全项目无其他组件用过它；误写单参数 `rem(0.5)` 会报 `Missing argument $pixels`（`7b592f2d`）。
- **中文 IME**：`InputService.cpp` WndProc 加 `WM_IME_*`，`GCS_RESULTSTR` 经
  `ImmGetCompositionStringW` 取结果串，逐 UTF-16 code unit 走 `ProcessKeyboard(...,KEYEVENT_CHAR,...)`；
  组合期间 `s_imeComposing=true` 抑制 WM_INPUT 的 ToUnicodeEx。链接用 `#pragma comment(lib,"imm32.lib")`。
- **往 `if/else if` 链里插新事件类型前先确认落点**：`if (aType != X && ...) {} else if (active) {}`
  会让 `X` 静默掉进 `else`（实例 `InputService.cpp::ProcessKeyboard` 的 `KEYEVENT_CHAR`）。
- **字符事件要发"产生的文本"而非虚拟键**（`5cbe1720`），且不要发未初始化的字符（`d912fc54`）。

## 7. 日志分诊速查

- **`cef_debug.log` 不能默认是本站的**（CEF 示例/Adobe CEP/MS Dynamics 都用这名字）。
  **判据不是文件名里的模块名**：CEF 编了 `//chrome` 那一层（`chrome/browser/chrome_browser_main_win.cc`
  等），所以 `chrome\browser\*`、`RunDeElevated`、`Windows.AutoDeElevateResult` 出现在**本站自己的**
  `cef_debug.log` 里完全正常（§13 就是靠这些行抓到的；旧版判据把这几条判成"非本站"，是错的）。
  可靠判据：**文件位置**（游戏根目录 `logs/`，或 `st_boot.log` 记的游戏根）+ **时间戳与
  `tp_client.log` 对得上**。本站 CEF = 官方 `cef 141.0.11`（`Libraries/TiltedUI/xmake.lua:9`）。
- **从日志地址反查符号**：同份日志里找一条已带"模块名+偏移"的行拿模块基址
  （如 `SkyrimSE.exe+0xc02260 (0x7ff73a442260)` ⇒ 基址 `0x7ff738840000`），**不要用崩溃地址反推**。
  再 grep `versionlib-ae-to-se-<ver>.map`（`<AE id> <SE RVA>`）反查。
- `HookAudit` 三条 ship 级结论：**`0 did not land` 不等于挂载成功**（未映射 ID 会挂到共享空桩，
  另有专门 error 行提示）；重复 target 说明两个 id 落同一地址；共享 `ff 25` 说明别的 mod 也在钩。
- 版本核对：凡"某版本还是不行"，先核对日志时间戳是否**晚于**该 release 发布时间。
- 同 mod 内重复钩子是上游继承缺陷，**不要擅自删**（`TESObjectREFR.cpp` id 19708 与
  `Actor.cpp` id 37525 同为 `HookAddInventoryItem`，1.5.97 里两个 id 都映射到 `0x28e680`）。
- 游戏主线程是**纯事件驱动**打日志（加载 ~450 行/秒、空闲 ~0.05 行/秒）。"长时间无日志"≠ tick 停了，
  要证明 tick 存活得靠专门的仪器化心跳，不能只看有没有日志。
- **MO2 的 `mo_interface.log`/`usvfs-*.log` 是 MO2 自身日志，不含崩溃信息**；正确诊断文件是
  游戏根目录 `logs/tp_client.log`、`crash_*.dmp`、`st_client_error.log`、`st_boot.log`、`cef_debug.log`。

## 8. 崩溃报告与地址库自身缺陷（`e73f52c9` 修过一轮）

- **`CrashHandler.cpp` 崩溃 hexdump 在"跳空"时为空**：`ExceptionAddress - 0x200` 在地址 < 0x200 时
  无符号回绕 → `SafeReadBytes` 返回 0 → 最该看清现场的那类崩溃（rel32 跳空）反而打不出字节。
  修法：`>= 0x200` 才回退，否则从地址本身开始。
- **转储失败被记成成功**：`CreateFileA` 失败返回 `INVALID_HANDLE_VALUE`(-1) 而非 NULL，
  `if (!hDumpFile)` 判不出来。要对 `INVALID_HANDLE_VALUE` 判定 + 检查 `MiniDumpWriteDump` 返回值。
- **`VersionDb.h` 截断的地址库会崩在读库里**：`read<T>()` 返回未初始化栈值且从不看流状态 →
  空/截断文件可能碰巧解析成 format 1/2/5 继续跑；`ptrSize` 为 0 时整数除零直接打掉进程。
  修法：值初始化 + `ptrSize` 限 4/8 + `addrCount` 限界 + `file.eof()` 早停 → 干净 `return false`。

---

## 9. 已修复 bug 清单（按 commit 时间线，hash 可追溯）

| commit | 版本 | bug / 修复 |
|---|---|---|
| `c819d684` | v1.0.18 前 | F2 开关与 overlay 页面状态脱钩（`m_active` vs JS `active$`/`inGame$`），F2 只见光标 |
| `d374a1a5` | — | 合并上游 TiltedEvolution dev（18 冲突文件），引入后续一串回归 |
| `9da3a2ac` | — | 合并引入/暴露的 4 个缺陷 |
| `ec4e828f` | — | `OnNotifySubtitle` 缺 `isLeader` 声明（编译错） |
| `b15c3284` | — | 合并遗留 3 处不一致（xmake 版本对账、fomod 漏装 QuestPatches.esp 等） |
| `66d61839` | — | `.gitmodules` 幽灵 submodule 残留 |
| `c03200fc` | — | `VersionDb.h` 被合并"半截拼接"（−64 行）致客户端构建失败 |
| `f1ef89d2` | — | `InventoryService` 的 `isLeader`（第三处）被合并吃掉 |
| `9a20cf20` | — | 裸体 NPC 自愈片段被合并吃掉 |
| `d810d3da` | v1.0.19 | FOMOD 安装引导加作者/项目地址 |
| `00ec7306` | — | **rel32 越界截断**（P0）：运行时 DLL 离镜像 ~20GB，字节补丁跳进未映射内存 |
| `f361242e` | — | FOMOD 引导的 `fomod/info.xml`（作者/版本/网站） |
| `3ba69aa5` | v1.0.20 | 过时文档整理（RELEASE-AND-MO2 等） |
| `c0abe640` | — | **锚点式 pad 重复计算**：新游戏闪退，`locationForm` 读成邻居 |
| `7e154b06` | v1.0.21 | 记录 1.5.97 审计基线 |
| `e73f52c9` | v1.0.23 | CrashHandler hexdump 回绕 + 转储误报成功 + VersionDb 截断除零 |
| `66851aaf` | — | 连接握手加 10s 看门狗 + 取消真正中止 + 先 Close 再 Connect |
| `be53b6cd` | v1.0.24 | 取消的握手上报为 disconnect |
| `f9b81d21` | v1.0.25 | 错误清掉 overlay 的 connecting 状态 |
| `b0082844` | v1.0.26 | `SkyrimVM::inactive` 改读 1.5.97 真实偏移 |
| `83f3f89e` | — | 失败连接尝试上报为 disconnect（**去重标志误合并，后被 6acd6ef8 修**） |
| `6acd6ef8` | v1.0.27 | 拆 `m_attemptErrorReported`/`m_attemptDisconnectReported`；修复"自动重连死代码" |
| `b7abf355` | v1.0.28 | 修正 ProcessConnectMessage 上方注释；不碰内存动作移出 Runner 队列 |
| `5efab47f` | v1.0.29 | 连接尝试拥有独立 pump（handshake 专用线程 + recursive_mutex） |
| `d9f01e7e` | v1.0.30 | pump 只解析不分发，解析结果交还游戏线程（消除跨线程碰游戏内存） |
| `f30b30c4` | — | 连接结果弹窗 + 菜单常显 + 中文 IME 输入 |
| `7b592f2d` | v1.0.31 | UI 构建失败：`rem()` 少传参数 |
| `a9ad774f` | v1.0.32 | 帧循环改 WM_TIMER 驱动（1.5.97 两个帧钩子从不执行） |
| `83bb37be` | **未打 tag** | **启动闪退**：定时器无门控，启动期提前跑 `Update()`；补 `GetInGame()` 门控 + 判空 |
| `45577fa4` | v1.0.34 | **4 条 1.5.x id 映射错误**（§11）：`400475` 指到 TimeData、`37525`/`19708` 撞同一地址（就是那条 `already claimed`）；生成器 join 键改 `(class, symbol)` + 拒绝写重复表 |
| `3197edde` | v1.0.35 | **`SkyrimVM::virtualMachine` 偏移**（§12.3）：legacy 应为 `0x200` 而非上游的 `0x210`；按目标分叉 + 各自 `static_assert` |
| `f11dbc26` | v1.0.36 | **CEF 在提权进程里自动去提权**（§13）：退出码 `38`，F2 只见光标、菜单永不出现；**并且多拉出一个原版 `SkyrimSE.exe`**（§13.2，同一根因）；`OnBeforeCommandLineProcessing` 追加 `do-not-de-elevate`，并把退出码翻译成人话 |
| `a9ea65cf` | v1.0.37 | **遗留项结案**（§14）：F3 阻塞点已由 WM_TIMER 消除（§14.1，证据=`update events` 计数在涨）+ 补 toggle/draw 日志；i18n 五语言补到 0 缺键 + 修 4 处坏占位符（§14.2）；重连/握手维持 fork 现状（§14.3/§14.4） |
| `2015a2b2` | v1.0.38 | **全项目审计与 revive**（§15）：删死代码（`Games/Renderer.cpp` + `Skyrim/Renderer.h` 整对、`ValidateAuthParams`、vivox 构建分支、幽灵错误码）；修 `/settime` 被拒时**客户端静默无反馈**（`NotifySetTimeResult` 从未被订阅） |
| `3bf0294a` | v1.0.39 | **每帧开销导致加载慢/人物卡顿**（§16）：`WndProc` 每条消息注入鼠标位置（含自制 16ms `WM_TIMER`）、`BehaviorVar::Patch` 每角色刷全部动画变量（载入期 **501 行/秒**）；另删无生产者的 `ActorSpawnedEvent`；**F6/F7/F8 全配置裁掉，只留 F2/F3** |
| `2635ea36` | v1.0.41 | **FOMOD 引导中英双语 + 精简**（§18）：FOMOD 没有 i18n，改用条件旗标 `lang` + `<visible>/<flagDependency>` 门控两条语言分支；顺带修掉**发布包里的中文会变乱码**——`release.yml` 盖章版本号时 `Set-Content` 未指定编码，PS 5.1 按 ANSI 往返 |
| `280c0518` | v1.0.40 | **hook 冲突真身是 EngineFixes**（§17）：全日志只有 1 处冲突（原文误作 2 处），`0xc02260`=id 68115=`GameHeap::Allocate`；`HookAudit` 只认 `e9`，跟不下 `ff 25` thunk 才报"无人可跟"——扩成 `BranchTarget` 并点名模块。另记 SKSE 路径缺 `_initterm_e` 哨兵（§17.4，待办） |
| `9ad19de2` | **未打 tag** | **CI 自 `49378750` 起全红，且不会自愈**（§24.1）：`dc77b99b` 把 `.vcpkg/downloads` 加进缓存路径，而"Set up vcpkg"的守卫判的是**目录存在**——缓存命中即创建该目录 → 跳过 clone → 下一行跑不存在的 `bootstrap-vcpkg.bat`。最后一次绿灯 `2579c599` 结束时（`12:18:05Z`）写下的缓存**毒化了之后每一次**。判据改为"checkout 存在"，克隆改到 `RUNNER_TEMP` 再并入 |
| `a8b0f76e` | **未打 tag** | **打包演练首次执行即失败，且是发布阻断**（§24.2）：清单把 `IEDSyncTogether.esp` 列为 **artifact**（"从插件构建输出拷"），但全流程无人生产它——上游在 `build-vortex.ps1` 的 `Write-MinimalPlugin` 里生成，而 CI 对每个插件**只跑 CMake**。`release.yml` 调同一脚本、同一 `ArtifactsRoot`，故推 tag 也会死在这。改为 **payload**（本仓库自持，与 STRPM 的 ini 同构），提交的副本与脚本输出**逐字节相同**（110 B，`9811c7bd…`） |
| `f1cb45dd` | **未打 tag** | **i18n 七个语言补到 0 缺键**（§25.1）：cs/de/ja/ko/no/ru/tr 各缺 5~11 条，全库 13 个语言文件现已**无一缺键**；逐键核对含 `{{占位符}}` 与英文串一致 |
| `cd0d90dd` | **未打 tag** | **编码测试从"只编译"改为"真的跑"**（§25.2）：CI 新增 "Run the encoding tests"，编译后、打包前执行 `TPTests.exe` 并**阻断**；实测 step 21 = success |
| `（待提交）` | **未打 tag** | **World 成员析构顺序与注释相反**（§161.1）：注释写「Transport before runner … makes the runner outlive the transport」，实际 `m_transport`(:58) 先声明、`m_runner`(:59) 后声明，逆序析构使 **runner 先死**；`~TransportService` join 握手泵期间，泵仍可向已析构的 `RunnerService` 队列投递 `[this]` lambda。改为 `m_runner` 在前、`m_transport` 在后，初始化列表同步（消 `-Wreorder`） |
| `（待提交）` | **未打 tag** | **握手泵循环内投递缺 stop 守卫**（§161.1）：`TransportService.cpp:730` 的 `GetRunner().Queue([this]{...})` 没有 `!m_handshakeThreadStop` 前置条件，而循环后同款投递(:748)有——`:745-747` 的注释恰好描述了该 hazard。泵线程在 sleep 后越过 while 检查仍会投递，对象随后析构 ⇒ 悬空回调。补上守卫 |
| `（待提交）` | **未打 tag** | **`Hook_WndProc` 异常可穿出引擎窗口过程**（§161.2）：`BSGraphicsRenderer.cpp:71` 无 `try/catch`，转调 `InputService::WndProc`（零 catch）。引擎消息泵之上没有 C++ 调用者能接住 ⇒ `std::terminate`。整体包 `try/catch(...)`，异常路径返回 `DefWindowProc`（**不是 0**，返回 0 会吞消息致窗口假死） |
| `（待提交）` | **未打 tag** | **`Hook_Renderer_Init` 解引用可能不存在的渲染系统**（§161.2）：`World::Create` 吞掉构建失败（`World.cpp:140-143`）后，`ctx().at<RenderSystemD3D11>()` 仍会抛。只把该 lookup 包 `try/catch`，失败置 `g_sRs=nullptr` + `Diagnostics::ReportFailure`，并在其后加 `if (!g_sRs) return;`——`Renderer_Init` 引擎调用**不包**（包了会掩盖真实崩溃） |
| `（待提交）` | **未打 tag** | **字节补丁不校验指令边界**（§161.2）：`GamePatch.h` 的 `Jump`/`PutCall` 硬编码写 5 字节，而 `ValidatePatchSite` 只 `memcpy` 2 字节、只判首字节是否 `E9`/`FF 25`，**从不检查首条指令是否够 5 字节**；带 `acExpected` 的严格重载(:415) 无调用者。新增 `InstructionCovers()` + 可注册的 `InstructionLengthFn`：无解码器时仅 warn（行为不变），注册后长度 < 5 则拒绝写入。**未硬编码指令长度表**——仓库内无 hde64/Zydis/Capstone 源码 |
| `（待提交）` | **未打 tag** | **8 处反序列化按对端计数在读取前 resize**（§161.3）：`WireLimits` 是**读后夹取**（`WireLimits.h:23-27` 自述），全仓零处「剩余可读字节」检查，`StringCacheUpdate.cpp:31` 等处在读到第一个元素前就 `resize(count)`。新增 `kMaxElementCount=1<<16`，统一改为「读 count → 比较 → 超限 clear+return → resize」。**选 reject 而非 clamp**：clamp 会让 reader 停在保留项末尾而流里仍有剩余，后续字段全部错位 |
| `（待提交）` | **未打 tag** | **`Mods`/`Factions` 的 count 原本是 clamp**（§161.3）：同样理由改 reject，`Mods.cpp:43-51`、`Factions.cpp:44-53/:61-67`；`Factions` 两个 list 任一超限**两个都清**（第二个 list 在流里位于第一个之后）。`Code/tests/encoding.cpp:1160-1177` 断言同步由「被 clamp 到 `kMaxModCount`」改为「空列表」——这是行为契约变更，非迁就代码 |
| `（待提交）` | **未打 tag** | **`AuthenticationRequest` 未认证可达且无异常保护**（§161.3）：首个消息、解析 3 次 `ReadString` + `UserMods.Deserialize` 全是分配，函数标 `noexcept` 却无 `try/catch` ⇒ 分配失败即 terminate。整体包 `try/catch`，失败清空 `Token/Version/Username/UserMods.ModList`——清 `Version` 是关键：`GameServer.cpp:886` 先比 `Version` 再看 `Token`，解析失败因此导向「版本不符 → kick」，而非「无 mod + 正确密码 → 直接进服」 |
| `（待提交）` | **未打 tag** | **服务端全局 `StringCache` 无容量上限**（§161.3）：一个已认证客户端可持续送入唯一字符串，服务端 `Clear()` **零调用点**，直到 OOM。`Add` 原有的 `try/catch` 回滚**是对的、保留不动**，缺的是上限。新增 `kMaxCachedStrings=1<<18`，超限返回 `kInvalidId` 复用现有失败路径；**不做 LRU**——`id` 必须是发送方前缀，这是按 id 指名的基础 |
| `（待提交）` | **未打 tag** | **`NotifyFactionsChanges` 用 `>=` 丢弃满批合法包**（§161.3）：写侧 `std::min(Changes.size(), kMaxChangeCount)` 可正好写出 `0x400`，读侧却用 `count >= kMaxChangeCount` 早退 ⇒ **每一个满批更新都被整包丢掉**。改为 `>`。这条与其余 8 处的「超限才拒绝」判据对齐 |
| `（待提交）` | **未打 tag** | **`GetStackFrameVariable` 返回值未判空即解引用**（§161.4）：`BSScript.cpp:285`/`:321` 拿到 `pArg1` 后直接 `pArg1->ExtractComplexType<Actor>()`，判空判的是**结果** `pActor` 而不是 `pArg1`。改为 `pArg1 ? pArg1->ExtractComplexType<Actor>() : nullptr`——失败语义与既有的 null-actor 路径完全一致（`Set<bool>(false)` + `return true`），不改变对外行为。全仓仅这两处调用点 |
| `（待提交）` | **未打 tag** | **`Send()` 丢包不可观测**（§161.1）：`try_to_lock` 失败时静默 `return false`，全仓 72 处调用只有 3 处检查返回值。**丢包本身是设计**（注释明说「the next snapshot carries the state」），所以**不改控制流**——改为把丢弃计数累加到 `m_droppedPackets` 并挂到既有的 `PumpHeartbeat` 日志上。握手期间泵整段持锁时，原本「全丢」和「全发」在日志里长得一模一样 |
| `（待提交）` | **未打 tag** | **`LookUpActorParameterType` 有路径不返回值**（§161.4）：函数返回 `void*`，但 `catch(...)` 分支结束后**直接落到函数末尾**——非 void 函数走完末尾是 UB，而调用方把这个值直接存进 `parameters.data[0].pType`。补 `return nullptr;` |
| `（待提交）` | **未打 tag** | **`NativeFunction` 基类 ctor 提前 return 使 `parameters` 未初始化**（§161.4）：引擎 ctor `104653` 未映射时直接 `return`，基类成员（含 `parameters`）保持未初始化，派生类 ctor 随后 `parameters.data[0].pType = ...` **沿垃圾指针写入**。对照同一结构体析构函数的注释「degrade to a leak」——这一处不是 leak，是写野指针。修复：ctor 内先显式把 `parameters` 置空，派生类写入前检查 `parameters.data && parameters.size > 0`。**证据**：`104653` 在 1.5.97 有真实映射（`st_overrides_all.tsv` → `0x1250c80`，capstone `sim=1.00`），且该 ctor 会调 `ae105361(a1+48, a5, 0)` 初始化 `parameters`——所以引擎 ctor 不跑时它确实是空的 |
| `（待提交）` | **未打 tag** | **`GetVarNameForStackIndex` 用 `capacity` 当边界**（§161.4）：`aIndex < parameters.capacity` 判的是**分配量**，而只有 `size` 个条目有名字，读到 capacity 范围会返回备用槽里的垃圾。改为 `parameters.data && aIndex < parameters.size`，与 `GetParam` 的判据对齐 |
| `（待提交）` | **未打 tag** | **`m_pendingMessages` 无容量上限**（§161.1）：游戏线程是唯一消费者，帧循环长时间不跑（加载画面、别人的 mod 死锁）时队列随服务端发送无限增长。加 `kMaxPendingMessages = 4096`，**溢出时丢最旧的**——最新的消息才承载会话在等的状态，最旧的已被后续快照取代。同时 `m_droppedPendingMessages` 计数并入心跳日志 |
| `（待提交）` | **未打 tag** | **消息工厂 `Extract` 是 `noexcept` 却不兜异常**（§161.3）：`ServerMessageFactory::Extract`/`ClientMessageFactory::Extract` 直接调 `DeserializeRaw`，而全仓 **16 个消息文件**的 `DeserializeRaw` 有 `ReadString`/`resize`/`push_back` 却没有自己的 `try/catch`——因为帧本身是 `noexcept`，上层也没地方接。三个接收入口（`TransportService::OnConsume`、`GameServer::OnConsume`、`AdminApp::OnConsume`）都只校验包大小，**不 catch**。一个 peer 撑爆内存 = 进程终止。在 `Extract` 里统一兜底，把分配失败降级为「解析失败的包」 |
| `（待提交）` | **未打 tag** | **服务端与 admin 分发表不检查空槽**（§161.3）：`GameServer.cpp:617` 与 `admin/Transport.cpp:40` 直接 `m_messageHandlers[opcode](...)`，**无范围检查、无空槽检查、无 try/catch**。而客户端 `TransportService.cpp:288-301` 三项俱全——同一判据在不同模块应用不一致。`Visit` 未命名的 opcode 在槽里留下**空的 `std::function`**，调用即抛 `std::bad_function_call`，在没有 catcher 的帧里就是 `std::terminate`。对端跑不同 build 是走到空槽的常规途径。补齐范围 + 空槽检查，并显式包含 `Opcodes.h` 而非依赖传递包含 |
| `（待提交）` | **未打 tag** | **Papyrus native 首次未注册即本进程永久失效**（§161.4）：`PAPYRUS_FUNCTION` 的 `s_p##name` 是函数内 `static`，`m_pFunction` 在构造时锁死，`Get()` 此后不再调用。若首次调用早于 `BindEverythingToScript`，该 native 永久返回默认值（`IsDead→false`、`GetOpenState→kNone`、`GetCombatTarget→nullptr`），`Capture` 之后注册进来也无回填路径。改为 `m_pFunction` mutable + `Resolve()` 重解析，命中即缓存；失败按调用次数退避（每 64 次查一次表），既不让未注册的 native 把每次调用变成查表，也能接住迟到注册 |
| `（待提交）` | **未打 tag** | **每 250ms × 每个本地 actor 一次 Papyrus VM 调用**（§161.4）：`ActorValueService.cpp:303` 的 `pActor->IsDead()` 走 `PAPYRUS_FUNCTION`（`Actor.cpp:1049-1054`），循环体无 per-actor 闸门 ⇒ VM 调用 = `4×N_local` 次/秒。改为轮转窗口：每次最多 64 个 actor，游标为 view 偏移（actor 增删安全），扫到末尾回绕。单次 VM 调用数上界 64 与 actor 总数无关；死亡检测延迟上界 `ceil(M/64)×250ms`（≤64→250ms，100→500ms，1000→4s）已写进注释 |

## 10. 未修复 / 遗留问题（open，按优先级）

- **【P2，未修，见 §161.3】反序列化无「剩余可读字节」检查**：全仓零处 `Remaining()/BytesLeft/Tell` 命中，
  `Buffer::Reader` 读越界后的行为（抛 / 返回 0 / UB）**未知**——它随 `tiltedcore` 由 xmake 拉取，不在本仓库。
  本轮只在 12 处计数型 `resize` 前加了上限；**逐字段的越界读仍未防**。要定论需要 vendor `tiltedcore` 的
  `Buffer::Reader` 源码。在此之前别把「已加 `kMaxElementCount`」当成「越界读已解决」。

- **【P2，未修，见 §161.2】字节补丁的指令边界校验默认关闭**：`InstructionCovers()` 已就位，但仓库内**没有**
  可用的 x86 长度解码器（hde64/Zydis/Capstone 无源码；MinHook 只是 `add_packages("minhook")` 的二进制包，
  工作区无 `MinHook.h`；`Libraries/TiltedReverse` 是空目录）。**无解码器时只 warn、行为与修复前一致。**
  要真正生效需在启动时调用 `SetInstructionLengthDecoder(&hde64_length)`。
  **不要**为了「看起来修好了」手写指令长度表——它会在最要命的 legacy/REX 前缀、ModRM 扩展、VEX 上**静默**出错。

- **【P2，未修，见 §161.5】TCP 粘包/半包无法静态审计**：长度前缀 + 循环拆包的代码**不在本仓库**
  （`Libraries/TiltedConnect` 空目录，`tiltedcore` 由 xmake 拉取）。三个 `OnConsume` 收到的 `aSize`
  已是一条完整消息的长度，本仓库内无累积缓冲区、无拆包循环。**既不能证实也不能证伪**，
  需要 vendor 源码才能定论。本条是「不知道」，不是「已修」。

- **【P3，未修，见 §161.4】`Variable::GetObject()` 对 `type>kMax` 且非数组的标签漏 `return`，穿透返回 `data.pObj`。
  **故意不修**：`Type` 是 `uint64_t` 且 `kMax=16`，数组编码方式没有静态依据，改动等于猜编码。

- **【已知取舍，勿改，见 §161.2】`GamePatch.h:384-395`「目标已有分支只 warn 仍照写」**：这**不是缺陷**。
  原注释写明理由——走这里的补丁是窗口样式、菜单解冻、线程命名，在用户的 load order 上一直能工作；
  改成硬失败是把**可见冲突**换成**静默缺功能**。**不要**把它改成硬失败。

- **【待验证，见 §161.4】两个 Papyrus native 无签名来源**：`ObjectReference::Reset`（2 参数，`Actor.cpp:1083-1085`）
  与 `ObjectReference::GetAnimationVariableInt`（`TESObjectREFR.cpp:1065`）在 `stubs/*.psc` 与
  `dump_addresses.py` 里**都没有对应声明**。失败模式安全（变 no-op，不崩），但若 `GetAnimationVariableInt`
  确实不存在，`Actor.cpp:1133` 的 `if(isLevitating)` 恒假，吸血鬼领主变身不再切 `LevitationToggle`
  动画——**静默功能缺失**。需要 1.5.x 的真实 native 表才能定论。

- **【P1，已结案，见 §11】**0x921F10 / 0x5B2FF0 **没有贴错**，是真实 1.5.97 地址；两个钩子不执行
  是因为 SKSE 路径下游戏主循环由 PE 入口直接进入，而该入口不经过被钩的那条调用链。结论已定，
  不要再"修"这两个 id。
- **【P1，已结案，见 §11】**`hook target ... already claimed` 的真身是**映射表把两个 id 指到同一
  地址**（`37525`/`19708` 都落 `0x28e680`），不是 `RipAllocateN` 桩冲突。已修表 + 增强日志。
- **【已结案，见 §17】`hook target ... already held a branch` 的真身是 EngineFixes**，不是无名 mod。
  全日志只有 **1 处**（§16.5 曾凭印象写成"两处"）。`HookAudit` 只认 `e9`、跟不下 `ff 25` thunk，
  才报"no relative jump to follow"；现已扩为 `BranchTarget` 并在告警里点名模块。
  **下次实机日志该行会显示 `EngineFixes.dll+0x...`**——看到它属预期，不是新问题。
- **【已结案，见 §26.5】EF 互操作的 `_initterm_e` 哨兵只在 launcher 路径装**：
  `HookFormAllocateSentinelInit()` 仅由 `immersive_launcher/loader/ExeLoader.cpp:334` 调用，
  SKSE 插件路径没有等价调用点。**查清后确认这是设计如此、不是漏装**：
  该路径下 SKSE 已先把 EF（含 preloader）加载完，我们的 hook 装在那条 `ff 25` thunk 之上，
  通过 trampoline 链到 EF 的分配器，本来就不需要事后修复。
  哨兵只对 launcher 路径（我们的 hook 先装、EF 后加载）有意义。
  结论已写进 `Games/Memory.cpp` 的注释；**代码未改**。
- **【P1，已观测未触发，见 §12.5】传输泵兜底分发**：tick 彻底不来的会话里，handshake pump 超 500ms
  会自己分发（`frame loop did not take the queued messages within 500ms` warn），**会碰游戏内存**。
  v1.0.34 两场实机会话里**一次未触发**：WM_TIMER 让帧循环始终活着，pump 一发现
  帧循环在就交还。保留现状，**但别删**——它是 tick 真死时唯一能连上的路径。
- **【已结案，见 §14.1】F3 调试菜单在 1.5.97 走不通**：阻塞点（`UpdateEvent` 不触发）已由 WM_TIMER
  驱动消除，**证据见 §14.1**（不是"应该好了"）。F3 本来就**不在** `IS_MASTER` 里；F6/F7/F8 保持裁掉。
  加了 toggle/draw 两行日志，下次实机一看就知道卡在哪一段。
- **【已结案，见 §14.2 + §25.1】i18n 缺键**：es/fr/nl/pl/zh-CN 五个语言补到 0 缺键；顺带修了 4 处
  **坏占位符**（`{{x }`、`{{x}`、大小写不符），它们会让聊天窗直接显示花括号原文。
  **§15.5 遗留的 cs/de/ja/ko/no/ru/tr 七个语言已于 `f1cb45dd` 补齐到 0 缺键**（§25.1），
  全库 13 个语言文件现在**没有一个缺键**——这一项不要再当待办重开。
- **【保持修复，勿丢】自动重连 + `DisconnectedEvent`**：fork 版是有参的（`IsError`），
  上游 `dev` 仍是无参 + 空结构体。同步时按 §3 的"新增语句行存活率"扫描，别被合回去。
- **【保持现状】版本握手强制同 commit**：`TransportService.cpp:268` 发 `Version=BUILD_COMMIT`，
  `GameServer.cpp:856` 要求相等否则 `kWrongVersion` 踢出。**这是有意设计，不是缺陷**：
  协议按 commit 演进，放开等于让不兼容的双方进同一个世界。详见 §14.4。
- **【已修，见 §11 + §12.3】`SkyrimVM::Get()`（id 400475）启动期读到 null**：两个独立缺陷叠加。
  ① 映射表把 `400475` 指到了 `TimeData::s_instance` 的 `0x1ec0a80`（§11，已修）；
  ② `SkyrimVM::virtualMachine` 在 1.5.97 上是 `0x200` 而不是上游的 `0x210`（§12.3，已修）。
  启动期读到 null 本身是正常的（VM 单例尚未建立）。
- **【观察项】客户端日志 0 字节**：曾出现从非 SKSE bootstrap 路径启动（launcher）导致
  `st_boot.log` 为空的情况，排查前先问清"这次是怎么启动的"（MO2 / 直接 SKSE / launcher）。
- **【已结案，见 §19.1/§19.2/§19.3/§19.4】`st_deploy_error.log` 的 `failed: d3dcompiler_47.dll (error 2)`**：
  文件**不缺**。① `DeployFile` 末尾清理一个全仓无人创建的 `.str_old`，把错误码冲成
  `ERROR_FILE_NOT_FOUND`；② 调用方在整轮遍历后才读 `GetLastError()`，真实错误被冲掉；
  ③ 真触发者是**内容相同但 mtime 更新**（18 个文件里只有它），旧判据纯看时间戳，于是
  每次启动都重复制一份 4.7MB 的 `.str_new` 且永远顶不上。三条都已修。
- **【已结案，见 §21.1】帧循环定时器量子**：`kWorldUpdateTimerIntervalMs` 曾为 `16`，
  在 Windows 默认 15.625ms 粒度下被**向上取整到 31.25ms**，所以实测 28.8 tick/s
  **不是"只跑到理论上限的一半"，而是上限本就只有 32/s**（§16.5 的 62.5/s 算错了）。
  **v1.1.0 已改为 `8`**（落在单 tick 内 = 15.625ms，~64/s），改动与理由见 §21.1；
  现值在 `Code/client/Services/Generic/InputService.cpp` 的 `kWorldUpdateTimerIntervalMs`。
  §19.5 的"本轮不改"是**当时**的结论，已被 §21.1 取代——不要再把它当待办重开。
- **【已查，不修，见 §19.7】`BehaviorVar` 的"multiple behavior replacers"与选错物种**：
  日志里狼/麋鹿被认成 Cow，看着像 bug，但**逐条验证后决定不动**（换了三种"更聪明"的
  判据都**没有变好**，其中两种更差），原因与判据见 §19.7。
- **【已结案，见 §30】OStimTogether 的 Add Actor 同意门控缺两个 `.pex`** —— 已用开源编译器
  在 CI 里编译并打包（`Code/plugins/papyrus/` + `windows.yml` 的编译步骤）。
  下面这段保留作历史记录：
  包内只有 `OStimTogether.dll`；`OSKSE.pex` / `OStimTogetherNative.pex` 不在包里。
  **影响面只有这一个功能**：DLL 照常注册 native（`main.cpp:255`），OStim 用回自己的原版
  `OSKSE.psc` 正常工作，只是**没有 Add Actor 同意门控**——优雅降级，不是坏包。
  **为什么 CI 编不出来**：`compat/OStimUIConsent/compile-ui-consent.ps1` 要 `PapyrusCompiler.exe`
  （来自已安装的 Skyrim/CK），CI runner 没有；`.pex` 是编译字节码（magic `FA57C0DE`），
  `Data/Scripts/` 下 68 个文件**全是 `.pex`**，游戏没有运行时编译器，拷 `.psc` 进去无效。
  **为什么用户也拿不到**（2026-09-26 实测，别再重复走这两条死路）：
  ① 上游 `Caelvanost/OStimTogether` **没有任何 release、没有任何 tag**
  （`releases.atom` 0 条、`tags.atom` 0 条、API `/releases` 返回 `[]`）；
  ② 这两个 `.pex` 在上游**全部 ref 的历史里从未存在过**
  （`git log --all --diff-filter=A -- "*.pex"` 为空，`git ls-tree -r` 全 ref 扫描 0 命中）；
  ③ `compat/` 是 submodule 内容、**从不进包**（payload 只取 `package/Data`），
  所以 “用 `compile-ui-consent.ps1` 自行编译” 这句对用户同样是空指针。
  原先 FOMOD 中文描述里那段 “注意:本包只含 OStimTogether.dll…” 两条指引都是死路，
  **已删除**（`plugins.json` 的 `introduction.zh`，中英文现已一致）；**不要再加回去**。
  **将来那次编译之前必须先补的缺口**：`OSKSE.psc` 用到 `nioverride.*`
  （第 4/5/13/20/22 行：`HasNodeTransformScale` / `GetNodeTransformScale` /
  `RemoveNodeTransformPosition` / `AddNodeTransformPosition` / `UpdateNodeTransform`），
  但 `Dependencies/Source/` 只 stub 了 7 个文件、**没有 `nioverride.psc`**，全盘也搜不到
  → **照现在的脚本编译必然因未知类型失败**，须先补 stub（或指向 RaceMenu 源码）。
  **拿到 `.pex` 之后怎么接线**：放进仓库自有位置 `Code/plugins/packaging/`
  （与 `IEDSyncTogether.esp` 同一先例，见 `plugins.json` 的 payloadNote 与 `../` 规则），
  在 `plugins.json` 的 OStimTogether `payload` 加一条 `dest: "scripts/"`。
  ⚠️ **顺序不能颠倒**：`Add-PluginPayload.ps1` 对缺失的 payload 源是 `throw`，
  先接线再补文件会**直接把 CI 构建打挂**。
- **【观察项，等一次 CI 真机验证】本轮新并入的三个插件尚未经过一次真实 CI 构建**：
  `AnimSyncTogether` / `DAVSyncTogether` / `TradeTogether` 的源码、契约、payload 与快照
  都已就位（§29），但**本机没有 MSVC/vcpkg，编不了**，`windows.yml` 的六个 consumer
  只有 CI 能验证。首次 push 后重点看 `Build companion plugins` 那一步：
  ① `DAVSyncTogether` 多一个 `nlohmann_json` 依赖；
  ② ~~`TradeTogether` 的 `builtin-baseline` 可能与 `--depth 1` 打架~~ **已查实并已修**：
  确实会炸（vcpkg 的 builtin registry 路径没有 fetch 回退），已在 `windows.yml` 的
  setup 步骤按 manifest 反推 baseline 并逐个 fetch，详见 **§29.5**；
  ③ `AnimSyncTogether` 用 `commonlibsse-ng-flatrim`，与另外几个的 `commonlibsse-ng` 不同包，
  首次会把该包也编一遍（缓存冷启动更慢，属预期不是故障）。

---

## 11. 1.5.97 地址库审计（2026-09-22，对着真实 `SkyrimSE.exe` 1.5.97.0 做的）

**方法**：不用猜测、不用工具链。三份独立证据交叉：
① 真实 1.5.97 `SkyrimSE.exe`（34,769,792 字节，`FileVersion 1.5.97.0`）的 PE 反汇编；
② 仓库自带的 `version-1-5-97-0.bin`（778,674 条 SE id）与 `versionlib-1-6-1170-0.bin`（428,461 条 AE id）；
③ `git tree @ 8eaca858^`（AE 迁移前一版，**硬编码 1.5.97 绝对地址**）这是本仓库自己的真值。

`8eaca858` 的 diff 是可直接对账的：`- 0x141EC3B78` → `+ 0x141F5E378`，符号名不变。
把两侧按 **文件名 + 符号名** join（不是只按符号名），就是一张 1.5.97 真值表。

### 11.1 结论一：`0x921F10` / `0x5B2FF0` **没有贴错**

两个地址在真实二进制里就是它们声称的函数，且互为上下游：

```
WinMain 0x5ACBD0
  └─ call 0x5AF3D0        帧循环：PeekMessageA/TranslateMessage/DispatchMessageA + Sleep(0x32)
       └─ call 0x5B2FF0   （循环体内，每帧一次）
            └─ call 0x921F10   rcx = [0x141EC3B78]（SkyrimVM 单例）
```

- `0x921F10` 入口 `mov rax,rsp / mov [rcx+0x750],1`，体内 `movzx r15d,[rsi+0x680]` 
  与 1.6.1170 的 `SkyrimVM::Update`（`ae id 53926`）同形，`+0x680` 正是 1.5.97 的 `inactive`
  （与 §2 的 `GameVM.h` 断言一致）。**这个地址是对的。**
- `0x5B2FF0` 在 1.5.97 里是 `0x5AF3D0` 帧循环体内的每帧调用，`0x921F10` 的唯一调用者。
  与 1.6.1170 的 `ae id 36564` 相对位置一致（两边都是"帧循环里的每帧钩子"）。**这个地址也是对的。**

**那为什么心跳整场为 0？** 因为 `0x5ACBD0` 这条链**只从 PE 入口 `0x134B228` 走**
（`entry → 0x134B05C → 0x5ACBD0`）。而 **launcher 路径是 `loader.GetEntryPoint()` 直接跳游戏入口，
SKSE 路径则由 SKSE 自己驱动游戏主循环**两条实际路径都不经 `TiltedOnlineApp::GetMainAddress()`
（它用的是另一个 id `36544 → 0x5ACBD0`，**全仓无人调用**）。

> 教训：**"钩子 0 did not land" ≠ 地址错**。地址正确、字节也写进去了，函数只是不在实际执行的
> 路径上。判定"贴错"之前必须先证明该函数在真实调用链里；否则会把对的地址改成错的。
> 这也是 WM_TIMER 方案（§5.6）能站住脚的原因：它绕开的是"谁驱动帧循环"，不是"地址对不对"。

### 11.2 结论二：`already claimed` 的真身 = 映射表把两个 id 指到同一地址

`hook target 0x7ff739ace680 already claimed` 反推的 RVA 是 `0x128e680`（基址 `0x7ff738840000`，
由同日志另一条带模块名的行得到）。**该 RVA 不在 1.5.97 库里**，所以当时怀疑 `RipAllocateN` 桩冲突。

真实原因是**映射表自己的重复**：`versionlib-ae-to-se-1-5-97-0.map` 里两个 id 指向同一偏移，
两个钩子于是落在同一函数上，`HookAudit::Record` 报"已被占用"。

1.5.97 地址库本身**零重复**（778,674 条 id ↔ 778,674 个互异偏移），所以映射表里任何重复都是**错**的：

| 偏移 | 被哪两个 id 认领 | 后果 |
|---|---|---|
| `0x28e680` | `19708`（TESObjectREFR::AddInventoryItem）、`37525`（Actor::AddInventoryItem） | **就是那条 `already claimed`**；Actor 侧钩子落到了 REFR 的函数上 |
| `0x54cb70` | `34140`（ActorMagicCaster::InterruptCast）、`34408`（MagicCaster::InterruptCast） | 同上，钩错函数 |
| `0x1ec0a80` | `400447`（TimeData::s_instance）、`400475`（SkyrimVM::Get 单例） | **`SkyrimVM::Get()` 读到 TimeData**，直接踩坏 §5.5 的整条更新链 |
| `0x2f27188` | `403350`、`403560` | 两者代码库都未引用。但地址库是单射的，重复即错，已删 `403350`（见 §11.5） |

两个库都是**单射**（`1.5.97` 778674 条 id ↔ 778674 个互异偏移；
`1.6.1170` 428461 ↔ 428461），所以正确的 id 映射也必须单射——
**重复就是 bug，不是风格问题**。

### 11.3 结论三：另外两处也错了（真值表比对发现）

按 **(文件, 符号)** join 对账 114 个同名点，除上述外还有 2 处不符真值：

| ae_id | 符号 | 错值 | 真值 | 真值依据 |
|---|---|---|---|---|
| `14953` | `TESTexture::Construct` 的 ctor | `0x938b40` | `0x1a0bc0` | 快照 `0x1401A0BC0`；`8eaca858` diff 同符号 `0x1401A0BC0` → `0x1401AC180` |
| `400475` | `SkyrimVM::s_instance` | `0x1ec0a80` | `0x1ec3b78` | 快照 `0x141EC3B78`；且 4 个连续 8 字节槽在两边一一对应 |

`400475` 的判定尤其干净——1.6.1170 与 1.5.97 的这段是**等距**的：

```
ae 400473 0x20fba60 ──0x18──> ae 400476 0x20fba78      (1.6.1170)
se 514313 0x1ec3b68 ──0x18──> se 514316 0x1ec3b80      (1.5.97)
                        
            400475 必须落 0x1ec3b70 槽；表里却写 0x1ec0a80（se_id 514287）
```

### 11.4 根因：生成器的 join 键是**裸符号名**，而符号名会重名

`Tools/Scripts/gen_se_map_from_history.py` 把两侧都塞进 `{符号名: 值}` 字典，**后写覆盖先写**。
而 `s_instance`、`s_constructor`、`s_addInventoryItem`、`s_interruptCast`、`ctor`、`s_start`、
`s_equipFunc`、`s_unequipFunc`、`s_singleton` 这些名字在树里**各属于多个类**：

- `s_instance` 在 1.5.97 快照里有 **7 个互异地址**（UI / SkyrimVM / TimeData / WeatherManager /
  Renderer / FormManager / QuestCallbackManager）；
- `s_addInventoryItem` 有 2 个（Actor `0x5e6f20`、TESObjectREFR `0x28e680`）；
- `s_interruptCast` 有 3 个。

join 时只能留下一个，其余全错。**这是"贴错"的真正来源**，而不是历史符号 join 本身不可信。

> **硬性规则（后续同步/重生成地图时必守）**：
> 1. join 键必须是 **(文件, 符号)**，不是裸符号名；
> 2. 生成后必须过两道断言：**映射表内偏移不得重复**（1.5.97 库零重复）、
>    **相邻 AE id 的映射结果不得严重逆序**；
> 3. 改完 `PlayerCharacter.h` 那类锚点偏移后，同一批 id 要重跑这两道断言。

### 11.5 不只是 1.5.97：10 张表全中

生成器用 `--se-bins-dir` 把 1.5.97 的结果**链式**推到其余 1.5.x
（`ae id → 1.5.97 rva → se id → 该版本 rva`），所以这 4 个错误
**10 张表全有**，只是偏移各不相同：

| 版本 | `37525` 错值 | `34140` 错值 | `400475` 错值 | `14953` 错值 |
|---|---|---|---|---|
| 1.5.3 | `0x28e750` | `0x54b610` | `0x1eda000` | `0x9372b0` |
| 1.5.16 | `0x28e7d0` | `0x54cab0` | `0x1ee6a00` | `0x938760` |
| 1.5.23 | `0x28e800` | `0x54cae0` | `0x1ee6a00` | `0x938790` |
| 1.5.39 | `0x28e8e0` | `0x54d020` | `0x1ee7a80` | `0x938ff0` |
| 1.5.50 / 53 / 62 | `0x28e870` | `0x54cd60` | `0x1ee7a80` | `0x938d30` |
| 1.5.73 / 80 | `0x28e680` | `0x54cb70` | `0x1ec0a80` | `0x938b40` |
| 1.5.97 | `0x28e680` | `0x54cb70` | `0x1ec0a80` | `0x938b40` |

另外删掉一条**无法定值**的：`403350` 与 `403560` 同报 `0x2f27188`。
`403560` 是对的——它与 `403559`/`403566`/`403567`/`403568` 在两个库里都是紧凑 8 字节串，
且 `403560→403566` 在两边都是 `0x30`（`0x3187780→0x31877b0` 与 `0x2f27188→0x2f271b8`）。
`403350` 则无法定值：它在 1.6.1170 的 `0x3186d98` 周围全是未映射的，
括号两端（`403340→0x2f26740`、`403447→0x2f26b7c`）之间有 78 个候选槽位。

故**删除而不猜**：未映射会退化到共享零返回 stub 并在日志里**喊出来**，
而错误映射是**静默指向邻居函数**——正是本次审计要消灭的那类失败。

修法：先把 1.5.97 修对，再用**同一条链**重算其余 9 张
（正确的 se id 在 1.5.x 内是稳定的，这正是生成器本来就依赖的性质）。
修完 40/40 条新值均能在**对应版本自己的** `version-1-5-*.bin` 里找到。

### 11.6 本次改动

| 文件 | 改动 |
|---|---|
| `GameFiles/.../versionlib-ae-to-se-1-5-*.map`（**10 张**） | 各修 4 行（共 40 行） |
| `Tools/Scripts/gen_se_map_from_history.py` | join 键改为 **(class, symbol)**；新增两道断言：**偏移不得重复**、相邻 id 不得大量逆序；不通过则**拒绝写文件** |
| `Tools/ida/st_overrides_all.tsv` | 修正同样 4 条，并在表头记下原因（它是重生成时的输入，不改会复发） |
| `Code/client/HookAudit.cpp` | 碰撞 error 补 `FormatModuleOffset`（模块+偏移），并在两处打印 **ae id** |
| `docs/PITFALLS.md` | §10 两条 P1 结案；本 §11 |

`HookAudit` 现在会把 `hook target SkyrimSE.exe+0x28e680 (0x..., id 19708) is already claimed`
一次说清“哪个函数、哪个 id”，不必再靠反推基址。

### 11.7 复核用的命令（照抄）

```powershell
# 1. 真值表：AE 迁移前的硬编码 1.5.97 地址
git show "8eaca858^:Code/client/SkyrimVM64.cpp" | Select-String POINTER_SKYRIMSE

# 2. 映射表里有没有重复偏移（1.5.97 库零重复 ⇒ 重复即错）
python -c "import collections;m={};
[m.__setitem__(int(l.split()[0]),int(l.split()[1],0)) for l in open('GameFiles/Skyrim/SKSE/Plugins/versionlib-ae-to-se-1-5-97-0.map') if l.strip() and not l.startswith('#')];
r=collections.defaultdict(list);[r[v].append(k) for k,v in m.items()];
print({hex(v):i for v,i in r.items() if len(i)>1})"

# 3. 反汇编某个 RVA（VA = 0x140000000 + RVA）
& D:\clangllvm\bin\llvm-objdump.exe -d --start-address=0x140921f10 --stop-address=0x140921f80 `
    --x86-asm-syntax=intel "<游戏目录>\SkyrimSE.exe"

# 4. 谁调用某函数（.text 里扫 call rel32）
#    见 §11.1 的调用链；同理可对任意 RVA 求调用者
```

---

## 12. v1.0.34 实机联机日志分析（2026-09-23，1.5.97 + MO2 + 整合包）

日志：`tp_client.log` 4485 行，两次会话（00:01–00:04 与 00:13–00:20），
均 `authentication accepted`，第二次跑满 7 分钟无崩溃。

### 12.1 §11 的修复被实机证实

| 验证项 | 结果 |
|---|---|
| `already claimed` 重复钩子 | **0 次**（v1.0.33 前每次会话必现） |
| `id 37525` 落点 | `SkyrimSE.exe+0x5e6f20` ✅ 正是 §11.3 修的值 |
| `id 19708` 落点 | `SkyrimSE.exe+0x28e680` ✅ 未受影响 |
| 钩子汇总 | `63 recorded, 0 did not land` ✅ |
| 同步功能 | 本地角色指派 257、远程角色 257、物体 322、所有权转移 217 |

### 12.2 §11.1 的结论被实机证实

**`vm tick heartbeat` 与 `main loop heartbeat` 在整个 4485 行日志里一次都没有出现**，
而 `timer-driven update heartbeat` 有 111 次采样（tick 到 22500）。这正面证实了 §11.1：

> 那两个地址是**对的**，函数只是不在实际执行的路径上。WM_TIMER 驱动是必需的，不是权宜之计。

### 12.3 新发现：`virtualMachine` 在 1.5.97 上不是 `0x210`，是 `0x200`

日志每次会话都报一次：

```
the SkyrimVM singleton at 0x7ff7377a7e80 holds the papyrus vm at +0x200,
not the +0x210 this build assumes; every other offset into this struct is
suspect for the same reason
```

**根因**：与 §2 的锚点式 pad 同一类错误。`v1.0.18`（fork 定版基线）用的是 `0x200`，
合并 `d374a1a5` 时按"adopt upstream value"规则改成了上游的 `0x210` ——
但 `0x10` 正是这个结构体在 AE/SE 之间的差值（`inactive` 同样是 `0x680` vs `0x690`），
**两个目标必须各定各值**。

**独立证据**（不依赖日志，来自真实 1.5.97 二进制）：

1. `SkyrimVM::Update`（`0x921F10`）自身 **5 处**读 `[rsi+0x200]`，其中一处带 null 检查；
2. 紧随其后被调用的 `0x922A90` 读 `[rcx+0x200]` 再 `jmp [rax+0x28]` ——
   正是对该指针的虚表调用；
3. `v1.0.18` 基线即 `0x200`。

**为什么一直没炸**：`SetVirtualMachine()` 会用游戏递给 papyrus 钩子的指针覆盖静态量，
单例字段只是"钩子还没跑时"的 fallback；读错偏移得到的是非空但不像 vm 的指针，
被 `LooksLikeVirtualMachine()` 拦掉 —— 于是 papyrus 路径**静默降级**，
而不是崩溃。修法：按 `SKYRIM_TARGET_LEGACY` 分叉，legacy `0x200` / AE `0x210`，
并各自 `static_assert`。

> **教训（再次）**：上游的"修正"是**对 AE 而言**的修正。fork 同时供两个目标时，
> 任何被上游动过的偏移都要先问一句"这是绝对值还是相对增量"，再决定跟不跟。
> 本次与 §2 的 `PlayerCharacter::pad1`、§5.6 的 `SkyrimVM::inactive` 是**同一个坑的第三次**。

### 12.4 日志里其余噪声的分诊（都不是 fork 的 bug）

| 现象 | 判定 |
|---|---|
| `Failed to create Discord instance (4)` ×2 | Discord 未运行。非致命，已有处理 |
| `multiple behavior replacers have the same signature` ×27 | **用户整合包装了两个动画 mod**（Cow/Deer/Goat/Horker/Horse/SabreCat/Skeever/Wolf 各被声明 2 次）。代码已优雅降级（`choosing the first one`），非 fork bug |
| `patch 'window style' skipped` / `patch 'skip startup movie' skipped` | 设计如此：无 1.5.x 验证偏移就不打（§4） |
| `id 36548 is not mapped` | 已知未映射项，见 `Tools/missing_1_5_97_ids.txt` |
| `hook target SkyrimSE.exe+0xc02260 already held a branch (ff 25 ...)` ×1 | **别的 mod 也在钩同一函数**（id 68115），`HookAudit` 正确识别并报告。属预期，非 bug |
| `BehaviorVar::LoadReplacerFromDir` 数百行 | 正常加载日志，info 级 |

### 12.5 P1 遗留项：传输泵兜底分发**从未触发**

§10 记的"handshake pump 超 500ms 会自己分发、会碰游戏内存"，本次两场会话
`did not take the queued messages` **出现 0 次**。原因是 WM_TIMER 让帧循环始终活着，
pump 一发现帧循环活着就立刻交还（日志里可见
`handshake pump: frame loop is live, handing ... back to it`）。

> 结论：该风险只在"帧循环彻底不来"时成立，而当前构建已不存在这种会话。
> 保留现状，但**别删**这条兜底 —— 它是 tick 真死时唯一能连上的路径。

### 12.6 复核用的命令（照抄）

```powershell
# 有没有重复钩子（应为 0）
Select-String -Path tp_client.log -Pattern "already claimed"

# 两个帧钩子是否真的从不执行（应为空）
Select-String -Path tp_client.log -Pattern "vm tick heartbeat|main loop heartbeat"

# 定时器驱动是否活着（应有大量采样）
Select-String -Path tp_client.log -Pattern "timer-driven update heartbeat" | Measure-Object

# 兜底分发是否触发过（应为 0）
(Select-String -Path tp_client.log -Pattern "did not take the queued messages").Count

# virtualMachine 偏移告警（修好后应为空）
Select-String -Path tp_client.log -Pattern "holds the papyrus vm"
```

---

## 13. v1.0.36 日志分析：CEF 自动去提权（F2 只见光标，菜单永不出现）

**现象**：`D:\sktest\new`（2026-09-14 ~ 09-20 共 10 场会话）每场都是 `renderer init` 之后立刻：

```
Overlay could not be initialized
CEF failed to initialize, exit code 38. See 'cef_types.h' for description
```

之后 `overlay render pump is live`、`overlay in-game state: true` 照常打印，按 F2 也**只有软件光标**
（光标是 D3D11 sprite batch 每帧画的，与页面死活无关，所以它"有反应"恰恰说明不了问题）。
菜单永远出不来。

**根因**：`38` = `CEF_RESULT_CODE_NORMAL_EXIT_AUTO_DE_ELEVATED`（`cef_types.h`）。
Chromium 在**浏览器进程**里检测到"进程已提权、且 UAC 开着"就重新拉起一个未提权的自己，
然后**让当前进程退出**：`chrome_browser_main_win.cc::MaybeAutoDeElevate`，
判据是 `base::win::UserAccountIsUnnecessarilyElevated()`（= `TokenElevationTypeFull`）。

游戏这次是**以管理员身份**启动的，于是 CEF 把 `SkyrimSE.exe` 又拉了一份、自己退出：

- 新进程拿不到游戏的 D3D11 设备/swapchain/窗口，overlay 页面永远建不起来；
- 老进程里 `CefInitialize()` 返回 false，`m_pOverlay` 没有页面，只剩渲染泵和光标在跑。

**为什么拖了一周才找到**：`cef_debug.log` 里其实写着 `RunDeElevated: Started process, PID: 5848`，
但 §7 旧的"排除判据"把 `RunDeElevated`/`Windows.AutoDeElevateResult` 判成"非本站日志"
（理由：本站不编 `//chrome`）——**这个理由本身就是错的**，CEF 编了 chrome 那一层，这些行就是本站的。
判据已改（见 §7）。

**修法**（`Code/client/Services/Generic/OverlayService.cpp`）：把 `OverlayApp` 派生一层
`SkyrimTogetherOverlayApp`，在 `OnBeforeCommandLineProcessing` 里追加 `do-not-de-elevate`。
它是 Chromium 的 `kNoRestartSwitches` 之一，**在"要不要重拉"判断之前**就被检查，
所以 CEF 留在原进程里跑，页面才建得起来。顺带把退出码翻译成人话，`38` 直接点名"去提权"。

> **为什么不顺着 CEF 让它去提权**：重拉的目标是 `SkyrimSE.exe` 自己，而 D3D11 设备、swapchain、
> 窗口句柄都不跨进程；新进程不可能接管游戏画面，只会多一个空转的 SkyrimSE。

> **教训**：日志排除判据必须能拿**证据**站住，不能靠"我们没编那个模块"的推测。
> 一条错的判据会把最该看的文件判成噪声，代价是 10 场会话。

### 13.1 复核用的命令（照抄）

```powershell
# 是不是被去提权（有这两条就是）——注意它出现在本站自己的 cef_debug.log 里
Select-String -Path cef_debug.log -Pattern 'RunDeElevated|AutoDeElevateResult'

# 客户端侧的证据：初始化失败 + 退出码
Select-String -Path tp_client.log -Pattern 'CEF failed to initialize'

# 确认游戏进程真的提权了（以管理员跑 PowerShell 才会是 True）
([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
```

### 13.2 同一个 bug 的另一张脸：多出来的那个 `SkyrimSE.exe`

用 MO2 的 `SkyrimTogether.exe` 入口启动时，任务管理器里会**同时出现两个进程**，
关掉 `SkyrimSE.exe` 那个之后游戏照常跑。这不是两个游戏，是同一个 bug 的另一半：

1. 入口是 MO2 的 6 号项 `mods/Skyrim Together Next/launcher/SkyrimTogether.exe`（immersive launcher）。
   它**不启动** `SkyrimSE.exe`，而是把游戏映像映射进自己的进程（`ExeLoader`），
   所以任务管理器里那个进程叫 `SkyrimTogether.exe`，但它**就是**游戏本体。
2. 为了骗过游戏和 mod，launcher 把 `GetModuleFileNameW(NULL)` / `LdrGetDllFullName` 挂钩，
   让它们返回**游戏真实路径**（`Code/immersive_launcher/stubs/FileMapping.cpp`）。
3. Chromium 的 `MaybeAutoDeElevate` 用 `base::PathService::Get(base::FILE_EXE)` 取"自己"的路径 ——
   它落到 `GetModuleFileName(NULL)`，**正好踩中这个挂钩**，拿到 `D:/game/SkyrimSE/SkyrimSE.exe`。
4. 于是 `CreateProcessWithTokenW` 拉起的不是 launcher，而是**一个干净的、没挂钩的原版 `SkyrimSE.exe`**。

所以那个多出来的进程是"原版游戏自己"：没有 launcher、没有 usvfs、没有联机。关掉它当然不影响主进程。
`AllowSetForegroundWindow failed: Access is denied (0x5)` 就是这一步的残响（§13 的日志第 4 行）。

> **一个根因，两个症状**：`do-not-de-elevate` 一旦生效，重拉这一步根本不发生，
> **两个现象一起消失**。所以"多一个 SkyrimSE"和"F2 只有光标"不要去分别修。

> **副产品**：这个 bug 还顺手解释了为什么"以管理员运行"会看起来时好时坏 ——
> 提权 + UAC 开启才触发；不满足条件时 Chromium 直接放行，一切正常。
---

## 14. v1.0.36 遗留项结案（F3 / i18n / 重连 / 握手）

### 14.1 F3 调试菜单：**已通**，证据在这里

旧结论是"`DebugService::OnUpdate` 依赖 `UpdateEvent`，而 1.5.97 上它从不触发"。
那个阻塞点**已经不存在**了，而且是可证的，不用靠"应该好了"：

`TransportService::HandleUpdate(UpdateEvent)` 第一件事就是 `++m_pumpTicks`，
而每 5 秒打印一次的 `transport pump heartbeat` 会把这个计数写进日志。
所以**只要这个计数在涨，就说明 `UpdateEvent` 真的在分发**——它就是 F3 那条路径的同一条链路。

```
# 实机日志（D:\sktest\LOGS，2026-09-23，1.5.97 + MO2）
[00:02:19] transport pump heartbeat: update events 1, ...
[00:02:24] transport pump heartbeat: update events 194, ...   <- 在涨
[00:02:29] transport pump heartbeat: update events 388, ...
```

对照：`D:\sktest\new`（09-14，WM_TIMER 之前的版本）里这个计数**恒为 0**，
`timer-driven update heartbeat` 也是 0 次——两处互相印证，不是单点观测。

**F3 与 `IS_MASTER` 的关系（旧描述有误导）**：`GetAsyncKeyState(VK_F3)` 那段**在** `#if (!IS_MASTER)` **外面**，
release 里照样执行。被裁掉的是 F6/F7/F8 和 Debuggers 菜单里的若干子窗口。
**按指示保持裁掉**：F6 直连 `127.0.0.1:10578`、F7 建/退队、F8 空实现，
这些是开发期捷径，发布版留着只会让玩家误触。

> **代价与取舍**：本次**没有**改 F3 的行为，只加了两行日志（toggle 一行、首帧绘制一行）。
> 理由是改行为需要实机验证，而本机按 §0 不能编译；加日志是零风险且能让下次实机直接定位。
> 若下次实机看到 `debug menu toggled` 但看不到 `debug menu drawing`，问题在 ImGui 渲染泵，
> 与 F3/UpdateEvent 无关。

### 14.2 i18n：五个语言补到 0 缺键 + 4 处坏占位符

`en.json` 是基准（126 键）。补键后各语言相对 `en` 的缺键数：

| 语言 | 补前 | 补后 |
|---|---|---|
| es | 13 | 0 |
| fr | 15 | 0 |
| nl | 36 | 0 |
| pl | 12 | 0 |
| zh-CN | 12 | 0 |

**顺手抓到的真 bug（不在原任务清单里）**：4 个语言的占位符写坏了，
transloco 匹配不上 `{{name}}`，于是**把花括号原文显示给玩家**：

| 文件 | 键 | 坏 | 修成 |
|---|---|---|---|
| de | `SERVICE.CLIENT.CONNECTION_LOST` | `{{remainingReconnectionAttempt }` | `{{remainingReconnectionAttempt}}` |
| nl | `SERVICE.CLIENT.CONNECTION_LOST` | `{{remainingReconnectionAttempt }` | `{{remainingReconnectionAttempt}}` |
| ru | `SERVICE.CLIENT.CONNECTION_LOST` | `{{RemainingReconnectionAttempt}` | `{{remainingReconnectionAttempt}}` |
| fr | `COMPONENT.SERVER_LIST.SERVER_COUNT` | `{{count}` | `{{count}}` |

ru 那处还错在**大小写**（`Remaining` vs `remaining`）——占位符名必须与 `en` 完全一致，
因为参数名由 TS 侧 `pushSystemMessage(key, params)` 决定，不由翻译决定。

**改法**：用脚本按 `en` 的键集合**只增不改**地补，`JSON.parse` → 补键 → 按原格式写回。
已用"语义 diff"复核：**removed=0，changed 只有上表 4 条**（其余是 JSON 重新序列化导致的逗号/缩进位移，无内容变化）。
编码（CRLF、无 BOM）逐文件复核过。

> `cs/de/ja/ko/no/ru/tr` 仍有缺键（5~11 条），**本次未动**：任务只点名了五个语言。
> 它们缺的是同一批键，补法照抄即可。

### 14.3 自动重连 + `DisconnectedEvent`：fork 修复仍在，别被同步吃掉

上游 `dev` 至今是 `ExecuteAsync("disconnect")` 无参 + `DisconnectedEvent` 空结构体。
fork 现状（**保持**）：

- `Code/client/Events/DisconnectedEvent.h`：有 `bool IsError{false}` 和 `DisconnectedEvent(bool)`；
- `OverlayService.cpp:393`：`OnDisconnectedEvent` 组 `CefListValue` 把 `IsError` 传下去；
- `OverlayClient.cpp:47`：`disconnect` 分支；另有 `abandonAttempt` 分支（`ProcessAbandonAttemptMessage`）；
- `client.service.ts:367`：`isError && _remainingReconnectionAttempt > 0` 才重连，
  重连前先 `abandonAttempt()` 拆掉旧 transport（否则 teardown 会误报一次 disconnect）。

**同步时的检查点**（§3 的通用扫描之外，专查这四处）：

```powershell
Select-String -Path Code\client\Events\DisconnectedEvent.h -Pattern 'IsError'
Select-String -Path Code\client\Services\Generic\OverlayService.cpp -Pattern 'ExecuteAsync\("disconnect", pArgs\)'
Select-String -Path Code\skyrim_ui\src\app\services\client.service.ts -Pattern 'abandonAttempt'
```

三处都在 = fork 修复完好。任何一处没了，就是被上游合回去了。

### 14.4 版本握手：**保持现状**（这是设计，不是缺陷）

`TransportService.cpp:268` 发 `request.Version = BUILD_COMMIT`；
`GameServer.cpp:856` 在 `acRequest->Version != BUILD_COMMIT` 时 `kWrongVersion` 踢出。

**为什么不该放开**：消息按 `T::Opcode` 二进制直传（§3），字段增删不体现在任何版本号里。
放宽到"同大版本即可"，等于让字段布局不同的两端进同一个世界，
结果不是"能玩但有小 bug"，而是**静默的错位读**——比连不上难查得多。

**真正的成本**在体验侧，且已经被现有代码兜住了：

- 客户端把服务端版本和本地 `BUILD_COMMIT` 一起塞进 `wrong_version` 的 `data` 里（`TransportService.cpp:666`），
  UI 有 `COMPONENT.CONNECT.ERROR.VERSION_MISMATCH` 词条（各语言都有）；
- 服务器日志有 `tried to connect with client ... - Version mismatch`。

> **给维护者的建议（未实施）**：如果以后要降低门槛，正确做法是**给协议加显式版本号并只做前向兼容的追加**，
> 而不是放松 `BUILD_COMMIT` 比较。在那之前，这条 P3 **保持原样**。

---

## 15. v1.0.38 全项目审计与 revive

审计口径：**只删能证明"没有任何引用"的东西**。本机不能编译（§0），所以每一处删除都靠
全仓 grep 到 0 引用 + 确认不是间接包含/虚函数实现/模板实例化，而不是靠"看着像没用"。

### 15.1 删掉的死代码（附证据）

| 删掉的东西 | 证据 |
|---|---|
| `Code/client/Games/Renderer.cpp`（63 行） | `BGSRenderer::Get()`/`GetDevice()` 全仓调用点**只有这个文件自己**；`HookPresent`/`HookCreateViewport` 从没被挂过（文件里自己写着 `// unused, never hooked`），而它们调用的 `RealRenderPresent`/`RealCreateViewport` 恒为 `nullptr`——真挂上去就是空指针调用。文件末尾的 `Initializer` 是个**空 lambda**。 |
| `Code/client/Games/Skyrim/Renderer.h`（58 行） | 只被上面那个文件 `#include`，别处 0 次。它定义的 `BGSRenderer`/`ViewportConfig`/`WindowConfig` 也随之无人使用。 |
| `GameServer::ValidateAuthParams` | 声明+定义都在，**调用点 0 处**，函数体是 `return false;`。 |
| `Code/client/xmake.lua` 的 vivox 分支 | `Services/Vivox/` 目录**不存在**，`Vivox` target 也不存在；`TP_VIVOX` 宏全仓 0 次引用。留着只会让 `has_config("vivox")` 一开就构建失败。 |
| `error.service.ts` 的 `'set_time_public_server'` | 在 `ErrorEvent` 联合里，但**没有任何生产者**（C++ 侧 `ErrorInfo` 只发 8 种错误码，没有它），`en.json` 里也没有对应词条——一条永远不可能触发的类型。 |

**替代关系已确认**：`Games/Skyrim/BSGraphics/BSGraphicsRenderer.h` 才是活的渲染路径
（它注释里就写着 "former ViewportConfig / former WindowConfig"），删掉的那对是 AE 迁移前的遗留。
删后全仓复查：`Games/Renderer.h` 0 引用、`BGSRenderer::` 0 引用。

### 15.2 修掉的真 bug：`/settime` 被拒时客户端**没有任何反馈**

服务端 `CommandService::OnSetTimeCommand` **每条路径都会回** `NotifySetTimeResult`
（`kSuccess` 或 `kNoPermission`），消息也注册进了 `ServerMessageFactory`——
但**客户端从来没有订阅过它**（全仓 0 处 `sink<NotifySetTimeResult>`）。

后果：非管理员用 `/settime` 时，服务端日志有记录、玩家屏幕**什么都没有**、时间也不动。
玩家无法区分"我没权限"和"这命令不存在"。

修法（`Code/client/Services/CommandService.{h,cpp}`）：按本项目既有写法接上 sink，
`kNoPermission` 时用 `OverlayService::SendSystemMessage` 明确告诉玩家原因（与
`TeleportCommandResponse` 失败时的处理方式一致）。`kSuccess` 不额外提示——时间已经跳了，
那本身就是反馈。

### 15.3 查过但**故意没动**的（避免误删）

- **`Code/tests/`（5 个 TEST_CASE）**：CI 只 `xmake -y` 构建，**从不执行测试**。
  这是"验证资产没接上"，不是死代码；删掉只会让以后更难验证。**保留，并在 §16 记为待接。**
- **`AdminService`（`Code/server/Services/`）**：头文件自己写着 `currently not in use`，
  但它在 `World.cpp` 里**被实例化并挂进了 spdlog sink**，删掉会改日志行为。保留。
- **`Differential.h`**：文件名没被 `#include` 过，但 `Differential<T>` 在
  `tests/encoding.cpp` 的 "Differential structures" 用例里被使用（模板，靠 `RTTI.h` 间接引入）。**不是死代码。**
- **`ahkpWorld.h` / `TESShout.h` 等"没人 include"的头**：类型经 `RTTI.h` 的
  `extern template struct RttiLocator<T>` 与 `RTTI.cpp` 的显式实例化使用。
  **按文件名 grep 会误判成孤儿**——这类头必须按符号再查一遍，别用文件名判断。
- ~~**`ActorSpawnedEvent`**：有前向声明、无 sink~~ → **已删**（v1.0.39，`3bf0294a`，见 §9）：
  复核确认全仓 0 生产者、0 消费者，随 §16 的每帧开销一起移除。本条是**过期记录**。
- **`PlaceActorInWorld`**：唯一调用点被注释掉了（`//PlaceActorInWorld();`）。
  是 F8 的调试功能，属于"未实现"而非"冗余"。保留。
  → **已结案**（§26.7）：确认是未完成的实验（造出来的 actor 既 `SetRemote` 又 `SetPlayer`，
  `m_actors` 无任何读者），**保持注释状态**，恢复属于新功能。
- ~~**`#if 0` 块（22 处）**：多数是上游留下的调试开关…**不动。**~~
  → **已逐块判读完毕**（§26.2）：**删 15 留 7**。当年"删除收益极低"的判断
  只对了一半——其中两处（`Sky.cpp` 的天气判据、`GLM_Bindings` 的 vec4）
  是**读起来像开关、实际不是**的东西，留着会误导下一个读者；而战斗/地图那 4 处
  恰恰**是**开关，动不得。
  **保留的 7 处**：F6/F7/F8、旧战斗瞄准 ×3、地图菜单、imgui 上游 ×2。
  判据见 §26.2 末尾：**看块外有没有为它留位置**。

> **教训**：这个项目里"看起来没人用"和"真的没人用"差别很大，因为大量类型是
> 通过 `RTTI.h` 的显式模板实例化和 `entt` 反射间接使用的。
> 删之前必须**按符号**而不是**按文件名**查引用。

### 15.4 审计用的命令（可复现）

```powershell
# 1. 文件级：某符号在全仓还有没有引用（把 SYMBOL 换掉）
Get-ChildItem -Recurse -Path Code -Include *.cpp,*.h,*.hpp | Select-String -Pattern 'SYMBOL'

# 2. 头文件是不是孤儿：先按文件名，再按符号复核（第 2 步才是决定性的）
Get-ChildItem -Recurse -Path Code -Include *.cpp,*.h,*.hpp | Select-String -Pattern '#include.*NAME\.h'

# 3. 空函数体（真 stub）
Get-ChildItem -Recurse -Path Code\client,Code\server -Include *.cpp |
  Select-String -Pattern '::\w+\([^;]*\)\s*(noexcept)?\s*$' -Context 0,1
```

### 15.5 本次审计**未做**的（留给下一轮，别当成已修）

1. ~~**`Code/tests/` 从不执行**~~ → **已完成**（`cd0d90dd`，见 §25.2）：前提条件（catch2 目标能在
   CI 跑通）已由 §24.1 修复后的绿灯构建证实，CI 现新增 "Run the encoding tests"，
   在编译后、打包前执行 `TPTests.exe` 并**阻断**。实测 **21:Run the encoding tests=success**。
2. ~~**`ActorSpawnedEvent` 定义了但没接线**~~ → **已关闭**：该文件已在 v1.0.39（`3bf0294a`）删除，
   无需再定 dispatch/消费方。已在本轮（2026-09-25）核对：`Code/client/Events/` 下无此文件。
3. ~~**`PlaceActorInWorld`（F8）**：调用点被注释，恢复需要实机验证会不会崩。~~
   → **已结案**（§26.7）：不需要实机验证，因为**它本来就不是"接上就能用"的开关**——
   函数造出的 actor 同时被标成 remote 和 player，且 `m_actors` 无人消费。**保持注释。**
4. ~~**其余语言的缺键**：`cs/de/ja/ko/no/ru/tr` 仍缺 5~11 条~~ → **已完成**（`f1cb45dd`，见 §25.1）：
   七个语言全部补到 **0 缺键**，且**逐键核对**（含 `{{占位符}}` 与英文串的一致性）。
5. ~~**22 处 `#if 0`**：未逐个判断哪些是上游残留、哪些是有意保留的调试开关。~~
   → **已完成**（§26.2）：22 处逐块判读，**删 15 留 7**，`Code` 下现剩 **7** 处。
   留下的全是"改 0 为 1"型开关（F6/F7/F8、旧战斗瞄准 ×3、地图菜单、imgui 上游 ×2）。

> 记在这里的原因：审计结论如果不写清"哪些查过、哪些没查"，
> 下一轮会把"查过且故意保留"重新当成"没查"再查一遍。

---

## 16. v1.0.39 卡顿与加载变慢（2026-09-23 场次实测）

### 16.1 先量，再改：tick 速率就是卡顿本身

日志自带两个独立计数器（§14.1 引入），可以直接算出真实帧循环速率：

```
12:50:30.503  timer-driven update heartbeat: tick 1
13:01:48.720  timer-driven update heartbeat: tick 17100
=> 678.2 秒 / 17100 tick = 25.2 tick/s
```

而定时器是 **16ms**（`InputService.cpp:42`），理论上限是 **62.5 tick/s**。
实测只有 40%。这条消息循环跑在**游戏主线程**上（`Hook_WndProc` 是渲染器的 WndProc），
所以"tick 慢"和"人物一卡一卡"是同一件事：主线程被占，游戏自己的帧也被拖。

### 16.2 找到的两处每帧固定开销

| 位置 | 问题 | 为什么之前没发现 |
|---|---|---|
| `InputService::WndProc`（`InputService.cpp:576`） | **每条**窗口消息都 `GetCursorPos` + `ScreenToClient` + `InjectMouseMove`。`WM_TIMER` 也是窗口消息，所以覆盖层关着的时候，每帧仍往 CEF 的 IPC 队列塞一次鼠标位置——一次同步的跨进程跳转，纯浪费。 | 代码本身"看起来"没错：`ProcessMouseMove` 内部确实有 `if (active)` 检查。但**取坐标和跨进程注入的开销在检查之外**，检查只挡住了后半段。 |
| `BehaviorVar::Patch`（`BehaviorVar.cpp:367`） | 对**每个** modded 生物把**全部**动画变量按 `info` 级别逐条打印。 | 注释写的是"每个 modded 生物**类型**"，实际是每个**生物**。载入一个满是 modded 生物的 cell 时，单个线程在一秒内写了 **501 行**。 |

第二处的实测分布（`12:52:07` 这一秒）：

```
tid 27428 x437   <- 全部是 BehaviorVar 的变量清单
tid 11000 x63
tid 6232  x1
```

这 437 行发生在 `Finished loading, triggering visit cell`（`12:52:07.902`）之后，
也就是**读盘/过场动画期间**——正好对上"启动和过场动画都变慢了"。

**改法**：变量清单降到 `debug` 级（一行汇总保留在 `debug`，计数用循环累加而不是 `SortedMap::size()`，
因为后者在本项目里没有任何使用先例，不能拿构建去赌）；鼠标位置只在覆盖层真的开着时才取。

### 16.3 F2/F3 定为唯一在线按键（按需求"定死"）

`F6`/`F7`/`F8` 原本包在 `#if (!IS_MASTER)` 里。`IS_MASTER` 由 **git 分支名**推导
（`xmake.lua:86`），所以**任何非 main 分支构建都会带上它们**：F6 直连 `127.0.0.1:10578`，
F7 建/退队，F8 函数体是空的。

现在改成 `#if 0`：**所有配置一律裁掉**，代码保留成一个块（下次要用把 `0` 改 `1`，别删）。
`F2`（菜单）和 `F3`（调试菜单栏）是仅有的两个在线按键——F3 在 `IS_MASTER` **之外**，本来就是通的。

### 16.4 顺手删掉的无生产者事件

`Code/client/Events/ActorSpawnedEvent.h`：全仓 0 生产者、0 消费者，
只在 `DiscoveryService.h` 留了个前向声明。已删（连同前向声明）。
注意它和 `ActorAddedEvent`/`ActorRemovedEvent` **不是一回事**，后两者是活的（`CharacterService` 在用）。

### 16.5 下一轮继续查的方向（本次未做）

1. **tick 速率仍未达标**：本轮只摘掉两处无条件开销，25.2/s 里剩下多少来自
   `VisitForms` 每帧遍历全部 high-process 句柄 + `RunRemoteUpdates` 每帧遍历四个 view，
   **需要一份改后的实机日志再算一次 tick/s** 才能确认。
2. **`VisitCell`/`VisitForms` 每 `PreUpdateEvent` 都跑**，`VisitForms` 里还留着上游的
   `TODO: GetById performance in loop?`。按 tick 降频（例如每 N 帧一次）是明显的下一步，
   但会改变 actor 发现延迟，必须实机验证。
3. ~~两处 hook 冲突~~ → **已在 v1.0.40 追查完毕，见 §17**。原文说"两处"是**错的**：
   全日志只有 **1 处**（`already held a branch` ×1，`already claimed` ×0），
   而且它不是"别的 mod"，是 **EngineFixes 的 MemoryManager**，代码里本来就有专门的互操作。

> **方法教训**："看起来有 `if (active)` 守卫"不等于"没开销"——
> 要看守卫**罩住了哪一段**。本次两处都是"守卫在里面，开销在外面"。

---

## 17. v1.0.40 hook 冲突追查：不是别的 mod，是 EngineFixes（2026-09-23 场次）

### 17.1 先把数量数对：是 1 处，不是 2 处

§16.5 当时凭印象写了"两处"。逐条数过日志后：

```
already held a branch : 1
already claimed       : 0      <- v1.0.34 修掉的那个，这份日志里已经没有了
hook did not land     : 0
is gone               : 0
hooks: 63 recorded, 0 did not land, 1 shared with another mod
```

**一条日志里的同一种串只要见过一次，就不该按印象写成"两处"**——数量本身就是结论的一部分。

### 17.2 冲突的真身：`ff 25` thunk，指向 EngineFixes

唯一那条告警：

```
hook target SkyrimSE.exe+0xc02260 (0x7ff7bf7f2260) already held a branch
  (ff 25 00 00 00, leading to no relative jump to follow)
```

三个事实一对上，真身就出来了：

1. **`0xc02260` 是 id 68115 = `GameHeap::Allocate`**（`Tools/ida/st_overrides_all.tsv:581`、`Games/Memory.cpp:72`）。
2. `ff 25` 是 `jmp qword ptr [rip+rel32]`，**一个六字节 thunk**，真正的目标地址存在紧接其后的 8 字节里。
   老代码的 `RelativeJumpTarget()` **只认 `e9`**，所以跟不下去，只能写"no relative jump to follow"。
3. `Games/Memory.cpp:78` 的 EF 互操作**认的就是这个签名**：
   `if (*opcodeBytes == 0x25FF) // 'jmp' opcode 'FF 25' ... shift = 6;`
   ——注释明写 EF 用 `ff 25` 换掉分配器。`EngineFixes.dll` 也确实装着
   （`D:\game\SkyrimSE\Data\SKSE\Plugins\EngineFixes.dll`，5.6.0.0，`MemoryManager = true`）。

所以这不是"无名 mod 抢了我们的函数"，**是已知且已被专门兼容的 EngineFixes**。

### 17.3 改动：`RelativeJumpTarget` → `BranchTarget`

`HookAudit.cpp` 里那个只认 `e9` 的 helper 扩成两形态：

| 形态 | 含义 | 处理 |
|---|---|---|
| `e9 <rel32>` | 相对跳转（也是 MinHook 自己写的） | `aFrom + 5 + rel32` |
| `ff 25 <rel32>` | `jmp qword ptr [rip+rel32]` | 目标在 `aFrom + 6 + rel32` 处**再解一次引用** |

读那一格用 `SafeReadCode()`——**未映射地址不能让日志自己崩掉**，这是本文件从第一天起的规矩。
告警文案也改了：落在 `EngineFixes.dll` 就是它，其余模块才是真冲突。

**效果**：下一次实机日志里这一行会从
`leading to no relative jump to follow` 变成 `leading to EngineFixes.dll+0x...`——
同一个现象，从"未知第三方"变成"已知且已兼容"，一眼可判。

### 17.4 顺带发现：EF 互操作在 SKSE 路径上没有装哨兵

`Memory.cpp` 的 EF 修复由 `Hook_initterm_e` 在 `_initterm_e` 之后执行，
而 `HookFormAllocateSentinelInit()`（装 `_initterm_e` 的 IAT 钩）**只被
`immersive_launcher/loader/ExeLoader.cpp:334` 调用**——那是 **launcher** 路径。
本次日志走的是 **SKSE 插件**路径（`st_boot.log`：`[bootstrap] loaded from ...SkyrimTogetherSKSE.dll`），
该路径下没有等价调用点。

**本次不改**：加这个哨兵会改变 hook 安装时序，而本轮的日志恰好显示 `0 did not land`——
现有顺序是好的，**在没有实机验证手段的前提下动它风险大于收益**。记为待办（§10）。

> **方法教训**："别的 mod 有名有姓"和"不知道是谁"是两种完全不同的结论，
> 而区分它们往往只差**多解一次引用**。把 `ff 25` 当成"跟不下去"，等于把已经写进代码的
> 兼容性知识又还回去了。

---

## 18. v1.0.41 FOMOD 引导中英双语与精简（2026-09-23 场次）

### 18.1 FOMOD 没有 i18n，只能用条件旗标自己做

FOMOD 5.0 **没有任何翻译机制**：`ModuleConfig.xml` 里的 `name`/`description` 就是字面量。
MO2 自己的按钮、步骤标题、组头都来自它的 `.qm` 翻译文件（`translations/installer_fomod_*.qm`），
**不是我们能翻的**——所以"引导界面加 i18n"能动的只有**我们自己的选项文案**。

做法是把"选语言"变成 FOMOD 原生的**条件旗标**，再用步骤可见性分支：

```xml
<installStep name="Language / 语言">          <!-- 第一步：只设旗标，不装任何文件 -->
  <group name="Wizard language / 引导语言" type="SelectExactlyOne">
    <plugin name="中文">   <conditionFlags><flag name="lang">zh</flag></conditionFlags> ...
    <plugin name="English"><conditionFlags><flag name="lang">en</flag></conditionFlags> ...
```

```xml
<installStep name="可选组件">
  <visible><flagDependency flag="lang" value="zh" /></visible>   <!-- 只在 lang=zh 时出现 -->
```

**`SelectExactlyOne` 是关键**：它保证 `lang` 单值，于是**两条语言分支永远只显示一条**。
换成 `SelectAny` 会同时显示中英两步，等于没做。

### 18.2 精简：描述去水，联机说明挪到常显字段

**先说清旧的到底是什么样**，免得又按印象写：旧版就 **1 步**、**2 个插件**，
两个插件都**确实装文件**（`VerifyScript` / `launcher`）——它们不是空勾选框。
真正的问题是**描述太长**：逐字量过（`<description>` 的字符数）：

| | 旧 | 新（中文） |
|---|---|---|
| 步骤数 | 1 步 | 3 步（语言 + 中/英各一步，**任一时刻只走 2 步**） |
| 启动校验脚本 | 3 段 / 131 字 | 3 段 / **108 字** |
| 独立启动器 | **5 段 / 170 字** | 3 段 / **126 字** |
| 插件名 | `启动校验脚本(仅 1.6.x 勾选)`、`独立启动器(不用 MO2 启动 SKSE 时才需要)` | 去掉冗余括注：`启动校验脚本(仅 1.6.x)`、`独立启动器(不用 MO2 时才需要)` |

删掉的是**重复劝退**（"所以默认不安装。玩 1.6.x 且希望有这个提醒的,再勾选。"——前一句已经说了），
以及把"用法:安装后把该文件夹里的文件复制到游戏根目录"这种**装完才用得上**的步骤
压成一行。**没有删任何事实**：1.5.x 会误报的原因、MO2 用户不需要它，都还在。

联机方式（房主开服、其他人游戏内按 F2 填 `IP:10578`）挪到 `info.xml` 的
`<Description>`——那是 MO2 向导**左上角常显**的字段（`descriptionText`），
比塞在某个步骤里更容易被看到，而且**不占步骤数**。

### 18.3 顺带修的：盖章脚本没指定编码

`release.yml` 给 `info.xml` 盖版本号时原来是：

```powershell
(Get-Content $path) -replace '<Version>[^<]*</Version>', "<Version>$ver</Version>" | Set-Content $path
```

`info.xml` **含中文**（本文件刚又加了一段联机说明），而 `Get-Content`/`Set-Content`
在**不带 `-Encoding` 时按 shell 的默认编码**走：

- GitHub 的 Windows runner 用 **`pwsh` 7**，默认 UTF-8 → 现状没事；
- 同一段脚本若由 **Windows PowerShell 5.1** 执行，默认是 **ANSI（CP936）** →
  中文被写成乱码，且**文件不再是合法 UTF-8**（实测：`E6 9C` 在 index 692 处无法解码）。

已改成 `.NET` 显式 UTF-8（无 BOM）读写，并**用 `Join-Path $PWD` 取绝对路径**：
`.NET` 的 `File.ReadAllText` 按**进程工作目录**解析相对路径，**不认 PowerShell 的
`Push-Location`**——模拟时这一步直接抛 `DirectoryNotFoundException`，不是理论风险。

### 18.4 美化：`/fomod/screenshot.png`

MO2 的 C++ 安装器里**硬编码**了三个路径（`installer_fomod.dll` 的字符串表）：

```
/fomod/info.xml   /fomod/ModuleConfig.xml   /fomod/screenshot.png
```

第三个是向导里的**预览图**（`screenshotLabel` + `screenshotExpand`，点开有全屏查看器
`FomodScreenshotDialog`）。我们此前**没有这个文件**，所以向导左栏一直空着。
现在放进仓库自带的 `branding/steam_library_card_st.png`（600×900，项目自己的素材）。

**没选 `logo.png`**：它 76% 像素透明、且是**浅色金属**（平均亮度 56），
在 MO2 的亮色主题（`Paper Light` 底色 `#F6F6F6`）上等于看不见；
`steam_library_card_st.png` 是不透明深色底（平均亮度 15），深浅主题都成立。

### 18.5 验证方式（没有实机 MO2 向导，靠三份独立证据）

1. **官方 XSD 校验**：`http://qconsulting.ca/gemm/ModConfig5.0.xsd`（`ModConfig5.0.xsd`
   的 `xs:redefine` 基架）逐元素校验，**0 错误**。这同时钉住了
   `installStep` 的子元素**顺序必须是 `visible` 在前、`optionalFileGroups` 在后**。
2. **MO2 二进制的字符串表**：`installer_fomod.dll` 里同时存在
   `visible`/`flagDependency`/`conditionFlags`/`SelectExactlyOne`/
   `FomodInstallerDialog::testCondition`/`readCompositeDependency` —— 说明这些**都被实现**。
3. **安装器选择**：同目录还有 `installer_fomod_csharp.dll`（`org.holt59`），
   但它要 **`script.cs`**（`InstallerFomodCSharp.findScriptFile`），我们包里没有 `.cs`，
   所以 MO2 走的是 **C++ 那个**（其 `isArchiveSupported` 只认 `fomod/ModuleConfig.xml`）。
   `ModOrganizer.ini` 里两者 `prefer=true`，靠脚本文件区分。

> **方法教训**：`Set-Content` 不带 `-Encoding` 时，"能跑通"和"编码正确"是两件事——
> 前者取决于**谁在跑**（`pwsh` 7 还是 5.1），后者才取决于**代码写了什么**。
> 对含非 ASCII 的文件做读-改-写，编码必须写死在脚本里。


---

## 19. v1.1.0 自部署的假失败与定时器分辨率（2026-09-23 场次）

> **版本归属（2026-09-25 更正）**：本节原题为 "v1.0.42"，但**该版本从未打 tag**。
> 本节三条修复与定时器量化全部由 `03cf2cbb`（tag **v1.1.0**）落地——可用
> `git log -S'SameContents' --all` 验证：全仓只有这一个提交。
> v1.0.41 里 `DeleteFileW(old.c_str())` **仍在**，即那条被本节判为 bug 的清理
> 在 v1.0.41 上**尚未修**；照 "v1.0.42" 去找 tag 会落空。

本轮日志：`D:\sktest\new`（1.5.97 + MO2 + 整合包，673 行，一场 5 分钟会话，
`authentication accepted` 且跑满全程无崩溃）。§12.6 的复核项**全绿**：
`already claimed` 0、`vm tick heartbeat|main loop heartbeat` 0、
`did not take the queued messages` 0、`holds the papyrus vm` 0、崩溃 0。
所以本轮的问题不在联机链路，而在**自部署**和**帧循环速率**这两处。

### 19.1 `st_deploy_error.log` 的 "error 2" 是伪造的（已修）

**现象**：`st_deploy_error.log` 只有两行，两场会话一模一样：

```
[deploy] non-critical failures; continuing
[deploy] failed: d3dcompiler_47.dll (error 2)
```

`error 2` = `ERROR_FILE_NOT_FOUND`，于是所有人都会去找"缺哪个文件"。
**但文件根本不缺**：`D:\game\SkyrimSE\d3dcompiler_47.dll` 存在，4741480 字节。

**根因**（两个缺陷叠加，缺一不可）：

1. `DeployFile` 的失败路径**最后**是 `DeleteFileW(<target>.str_old)`，
   而全仓**没有任何地方创建 `.str_old`**（已 grep 确认）。删除一个不存在的文件
   必然失败，并把线程 last error 覆盖成 `ERROR_FILE_NOT_FOUND`（实测：
   `DeleteFileW(missing) -> False err 2`）。
2. 调用方在**整轮遍历之后**才读 `GetLastError()`：
   `swprintf_s(buf, L"failed: %s (error %lu)", rel.c_str(), GetLastError())`。
   中间隔了几十个文件系统调用，于是**真实错误被冲掉**，只剩那两行。

**修法**：`DeployFile` 增加 `DWORD& aError` 出参，在**出错的那一步原地**记录
（`std::error_code::value()` 优先，回退 `GetLastError()`）；调用方改读它；
顺手删掉那个只用来污染错误码的 `.str_old` 清理。

### 19.2 假失败的真正触发者：**内容相同但 mtime 更新**（已修）

**为什么偏偏是 `d3dcompiler_47.dll`**：把游戏根目录与 MO2 payload 逐个比对
（18 个文件，SHA-256 + mtime）后，**只有它一个**满足"payload 比目标新"：

```
file                              identical  target mtime    payload mtime   payload newer
d3dcompiler_47.dll                True       09-19 17:36:32  09-23 08:56:54  True
其余 17 个文件                     True       09-23 08:5x:xx  09-23 08:5x:xx  False
```

**字节完全相同**，只是 MO2 解包时给 payload 盖了**更新的 mtime**。而旧判据是
"payload 不旧于目标就跳过"（纯时间戳），于是每次启动都判定"要更新"→ 复制一份
`.str_new` → `DeleteFileW(target)` 因游戏已加载该 dll 而失败 → 保留 `.str_new`
→ 下次启动**重复同一件事**。实机残留证据：`.str_new` 与目标**同哈希**，
且目标 mtime 停留在 09-19、payload 是 09-23。

**修法**：判据改成**内容优先**——大小相同且 payload 看着更新时，才逐块比对内容
（`SameContents`，64KB 分块，250MB 的 `libcef.dll` 也不会整份进内存）；
内容一致就认作已是最新，把目标 mtime 顶到 payload 的（下次走廉价分支），
并**顺手删掉那个永远没人消费的 `.str_new`**。mtime 相同则直接跳过，不做无谓读盘。

**为什么不用「直接删掉 `.str_old` 那行就完事」**：那只能让错误码变对，
`d3dcompiler_47.dll` 仍会**每次启动都被复制一遍**（白白写 4.7MB），
且游戏根目录仍会不断堆积 `.str_new`。19.1 修的是**报告**，19.2 修的是**行为**。

### 19.3 换文件必须用 `MoveFileExW`，不能先删后移（实测）

旧代码 `if (DeleteFileW(target)) return MoveFileW(...)` 是**危险顺序**：
删除会成功，而随后的移动会失败，游戏根目录就此**少一个 dll**。
（`libcef.dll` 走这条路径就是"启动直接挂"。）

改成 `MoveFileExW(tmp, target, MOVEFILE_REPLACE_EXISTING)`，并在本机实测了
锁定语义（独占句柄模拟已加载的 dll）：

```
MoveFileExW over LOCKED target   -> False err 5 (ACCESS_DENIED)
  target still exists: True
  target bytes intact: True      <- 失败时目标**原封不动**，不会丢文件
  staged file preserved: True    <- 待替换副本留着，下次启动顶上
MoveFileExW over UNLOCKED target -> True
  staged file consumed: True
```

### 19.4 `.str_new` 以前**根本没人顶上**（已修）

原注释写"the next launch picks it up"，但**没有任何代码去捡它**——
既没有"启动时把 `.str_new` 提上来"的分支，也没有清理。这正是 19.2 里
`.str_new` 能在游戏根目录躺好几天的原因。

现在补上两条：**先尝试顶替**（复用已存在的、且**不旧于 payload 且同大小**的
`.str_new`，省掉每次启动重复复制几百 MB），**顶替不掉再复制**。

> 为什么要校验大小：截断的 `.str_new`（正是上面那个 size 检查存在的原因）
> 若仅凭 mtime 就顶替上去，会把一个好的 dll 换成半截的。

### 19.5 帧循环真实速率：**31.25ms 的定时器量子**（已定位，未改）

实测本轮会话（`timer-driven update heartbeat`，剔除一次载入尖峰）：

| 量 | 值 |
|---|---|
| 整场平均 | 28.8 tick/s |
| 稳态中位间隔 | **31.27 ms** |
| 定时器请求间隔 | 16 ms（`InputService.cpp:42`） |

31.25 ms 不是噪声，是**算术必然**：Windows 默认定时器粒度是 **15.625 ms**，
`SetTimer` 的间隔会**向上取整到粒度的整数倍**：

```
16 / 15.625 = 1.024  -> 进位到 2 个粒度 = 31.25 ms = 32.0 tick/s
 8 / 15.625 = 0.512  -> 进位到 1 个粒度 = 15.625 ms = 64.0 tick/s
```

本机实测复现（`SetTimer` + 消息泵）：请求 16ms → 中位 **29.7ms（33.7/s）**；
请求 8ms → 中位 **15.8ms（63.2/s）**；`timeBeginPeriod(1)` **对结果无影响**
（33.5 / 63.6），因为 16ms 的取整**不是分辨率不够，而是量子对齐**。

**结论**：§16.5 遗留的"tick 速率仍未达标（25.2/s，理论上限 62.5/s）"里，
**有一半是算错了上限**——16ms 请求在默认粒度下**根本到不了 62.5/s**，只能到 32/s。
当前 28.8/s 距 32/s 的量子上限只差 ~10%，剩下那点才是真正的每帧开销。

**本轮不改**：把 `kWorldUpdateTimerIntervalMs` 从 16 改成 8（或 15）能翻倍到
~64/s，但它同时改变 `World::Update()` 的调用频率（`World.cpp:72` 还有个 8ms
的自限流），会牵动 actor 发现延迟与同步节奏，**必须实机验证**才能动。
记入 §10 待办。

### 19.6 复核用的命令（照抄）

```powershell
# 1. payload 与游戏根目录是否真的不同（内容 + mtime，两个都要看）
#    只比 mtime 会得出"要更新"的错误结论，只比内容会看不出触发条件
#    （对每个文件算 SHA-256 并打印两边 mtime）

# 2. 目标是不是被占用（独占打开失败 = 被游戏加载）
try { $fs=[System.IO.File]::Open('D:\game\SkyrimSE\d3dcompiler_47.dll','Open','ReadWrite','None'); $fs.Close(); 'not locked' }
catch { 'LOCKED: ' + $_.Exception.Message }

# 3. `.str_old` 到底有没有生产者（应为空 -> 那行清理纯属污染错误码）
git grep -n "str_old"

# 4. 定时器量子：请求 16ms 与 8ms 各测一次中位间隔（本机实测 29.7ms / 15.8ms）
```

> **方法教训**：`GetLastError()` 是**线程级**状态，只在**紧邻**失败调用的地方有意义。
> 把它读在几十个文件系统调用之后，等于读到一个随机数——本例里它稳定地返回
> `ERROR_FILE_NOT_FOUND`，把"文件被占用"报成了"文件不存在"，
> 而**文件名又是对的**，于是看起来完全可信。诊断信息出错比没有诊断信息更贵。

### 19.7 查过但**故意不改**的：`BehaviorVar` 选错物种（狼被当成牛）

**现象**：日志里狼（`DC556`）和麋鹿（`DC553`）都被判成 `Cow`：

```
BehaviorVar::Patch: found match, behavior hash b0b931cbfefb2d2b (found on formID dc556)
  has original behavior Cow signature iState_CowDefault,!iState_DogRun
```

**为什么签名会撞**：`Cow` 的签名是 `iState_CowDefault,!iState_DogRun`。
用**真实的** `DumpAnimationVariables` 输出逐条代入（狼的 110 个变量）：
狼**确实**含 `iState_CowDefault`、**确实**不含 `iState_DogRun`，
所以签名判据本身**没写错**——它只是**不足以区分**狼/牛/鹿/山羊等一群动物。
同一次 dump 满足 **8 个**replacer 的签名（Cow/Deer/Goat/Horker/Horse/SabreCat/Skeever/Wolf）。

**试过但否决的三个"更聪明"判据**（都以日志里的**真实物种名**做真值）：

| 判据 | 正确率 | 结论 |
|---|---|---|
| **现状**：`matchedReplacers[0]`（目录顺序第一个） | **7/9** | 基线 |
| 覆盖最多"该物种同步变量名" | 7/9 | **没变好** |
| 缺失最少 → 再看覆盖 | 7/9 | **没变好** |
| 先看"最多同步变量"再看覆盖 | 3/9 | **明显更差** |

三个替代判据在**同样的 8 候选**上给出的答案与现状**同分或更差**，
没有一条能稳定把狼判成 Wolf。**没有证据支持改**，按 YAGNI 不动——
改了只会把 7/9 换成另一个 7/9，却要冒引入回归的风险。

**真正的限制**：`iState_*` 这一类变量在**多个物种的图里同时存在**（狼的 dump 里
有 `iState_CowDefault`、`iState_WolfDefault`、`iState_DeerDefault`、`iState_DogDefault`），
所以**单靠变量名集合无法定位物种**。要真正修，得换一个能唯一标识物种的量
（例如基对象的 editorID / 模板），那是**新功能**而不是 bug 修复，不在本轮范围。

> **方法教训**："日志看起来不对"不等于"代码错了"。本例里**签名判据是对的**，
> 错的是"这些签名互不重叠"这个**假设**。动手前先用真值表量一次现状准确率，
> 否则会把 7/9 的基线改成 3/9 还自以为在修 bug。
> 反过来，`multiple behavior replacers have the same signature` 这条
> `critical` 日志**正是该假设被打破的正确报警**，应当保留。

---

## 20. v1.1.0 联机崩溃（SkyrimSE.exe+0x23d000 空指针）与加载期日志（2026-09-25 场次）

### 20.1 现象

`D:\sktest\new\tp_client.log` 末场（10:30 启动）在 10:35:03.114 崩溃：

```
10:35:03.113 [tid 6352] Spawn Actor: FF000A0A, and NPC hr
10:35:03.114 [tid 6352] Spawn Actor: FF000AA0, and NPC hr
10:35:03.114 [critical] VectoredExceptionHandler: crash occurred!
WriteCrashReport: faulting instruction is in SkyrimSE.exe+0x23d000
WriteCrashReport: registers: rax=0x80000001 rbx=0x22c3e25a2a0 rcx=0x0 ...
WriteCrashReport: access type read (code 0), target address 0x40 (unmapped 0x40)
```

调用栈：`#05 SkyrimSE.exe+0x23d000` ← `#06 +0x23d12f` ← `#07 +0xd2cdcf` ← … ←
`#12 +0xc0d6bd` ← KERNEL32/n兽ntdll（线程入口）。`#00/#01` 两个
`SkyrimTogetherRuntime_1_5.dll+0xa8feb / +0xa7f37` 是 **VEH 崩溃处理器自身**，
不是肇事帧——别被它误导成"我们代码里崩的"。

### 20.2 根因

`0x23d000` 是个 17 字节的小 getter：

```
14023d000: f6 41 40 01   test byte ptr [rcx + 0x40], 1
14023d004: 74 03         je  0x14023d009
14023d006: 33 c0         xor eax, eax
14023d008: c3            ret
14023d009: 48 8b 81 20 01 00 00  mov rax, [rcx + 0x120]
14023d010: c3            ret
```

`rcx = 0` ⇒ 读 `[0x40]` ⇒ AV。调用方 `0x23d12a`（在 `0x23d070` 内）执行的是
`mov rcx, r14; call 0x23d000`，而 `r14` 来自
`mov rcx,[rax+0x140]; test rcx,rcx; je → r14 = r12(=0)`——即**引擎自己的代码
把 null 传给了自己的 accessor**。

**这不是我们 hook 的函数**：`0x23d000`(SE id 17859) 与 `0x23d070`(17865) 都不在
`versionlib-ae-to-se-1-5-97-0.map` 的 3698 条里，我们既没 hook 也没调用它们。
崩的是**引擎在被我们搞坏的状态上继续跑**。

真正的前置状态：**同一个远程玩家被生成了两个 actor**。

- 10:31:56.451 `CharacterSpawnRequest, server id: 1E, form id: FF000A0A`
- 10:33:09.461 `CharacterSpawnRequest, server id: 1E, form id: FF000AA0` ← 同一 server id，**不同** form id
- 10:35:03.113/114 两个 actor 的 3D 背靠背建立 → 引擎在自身 reference 更新里解引用 null

第二条请求之所以没被 `OnCharacterSpawn` 的重复守卫拦下，是因为第一条的实体在
`10:31:56.705` 起就**已经查不到了**：紧接着是 18 条
`ActorValueService::OnHealthChangeBroadcast: could not find actor server id 1E`。
即第一次 spawn 建的实体中途丢了 `RemoteComponent`，守卫自然失效。

### 20.3 修复

两处，都在**客户端**，都不碰引擎：

1. `CharacterService::RunSpawnUpdates()`——`remoteComponent.CachedRefId` 已经是非 0
   （说明这个 server id **已经生成过 actor**）时，绝不再调 `CreateCharacterForEntity()`。
   原逻辑是"查不到就建"，而 `TESForm::GetById()` 在引擎注册新引用期间会**正常地**
   失败一两帧，于是又建了一个。丢掉的 actor 交给服务器的 spawn 请求重建，不由这条路径补。

2. `CharacterService::OnCharacterSpawn()`——重复守卫除了查 `RemoteComponent`，
   再查一次 `LocalComponent`：本机已拥有的 actor 被当成远程 spawn 再插一次，
   同样会造出"一个 server id 两个 actor"。

### 20.4 加载/过场卡顿：把热路径日志降到 debug

10:30:25 那一秒写了 **220 行**日志，其中 64 行是 `hook target ...`、约 80 行是
BehaviorVar 的物种名列表。这些都在主线程同步写盘。三处每实体/每 hook 的
`spdlog::info` 降为 `debug`：

| 位置 | 原日志 | 频率 |
| --- | --- | --- |
| `Actor.cpp` `HookSpawnActorInWorld` | `Spawn Actor: ...` | 每个引用 3D 更新一次 |
| `CharacterService::RunRemoteUpdates` | `applied 3D for actor...` | 每个远程实体一次 |
| `CharacterService::ProcessNewEntity` | `New entity remotely managed...` | 每个新实体一次 |
| `HookAudit::Report` | `hook target ...` | 每个 hook 一次(64) |

> **教训**：崩溃日志里的栈要**先分清哪些帧是崩溃处理器**。
> `VectoredExceptionHandler` / `WriteCrashReport` 永远在最内层，
> 把它们当肇事帧会把排查方向整个带偏。真正的肇事帧是**第一条落在
> 游戏模块里的**那个（这里是 `SkyrimSE.exe+0x23d000`）。
>
> 另外：**"查不到就建"是一类危险的幂等性假设**。只要那个"查"会在引擎
> 尚未完成注册时合理地失败，就必须用"是否已经建过"（`CachedRefId != 0`）
> 而不是"现在查得到吗"来做判据。

---

## 21. v1.1.0 联机流畅度与同步的算法级改进（2026-09-25 场次）

### 21.1 定时器量化：联机更新率实际只有请求值的一半

`SetTimer` 会把间隔**向上**取整到 15.625 ms 的系统 tick 的整数倍：

| 请求 | 实际 | 更新率 |
| --- | --- | --- |
| 16 ms | 2 tick = 31.25 ms | ~32/s |
| 8 ms | 1 tick = 15.625 ms | ~64/s |

远端玩家的位置是按这个节拍采样的，所以 16 ms 让插值每 31 ms 才推进一次，
在 60 fps 画面里表现为可见的"一步一顿"。改为 **8 ms**，更新率翻倍。

### 21.2 插值：线性 → Catmull-Rom，并允许有界外推

原实现的两个算法缺陷：

1. **线性插值只有 C0 连续**。相邻线段的方向在快照边界上是突变，
   瞄准角速度不连续，看起来就是"折线感"。
2. **`delta` 被钳到 1.0**。丢包时播放头越过最新样本，角色被钉在最后
   已知位置**冻结**，下一个包到达时再**跳**过去——冻结+跳变比轻微过冲
   难看得多。

改为：

- **均匀 Catmull-Rom**（p0/p1/p2/p3），平滑线段交界处的方向；
- 窗口边界处的 p0/p3 **夹到线段端点**，退化为线性而不是外推失控；
- `delta` 上限放宽到 **1.25**，沿最后一段速度做有界外推；
- `delta` 下限补 **0**，防止播放头落后窗口起点时**反向**外推。

窗口裁剪改为"保留活动线段前一个样本"（`size > 3`），保证 p0 存在；
并加了 `kMaxSamples = 8` 的硬上限，防时钟重同步时窗口失控。

> **验证方法**：把裁剪+选段逻辑抽成纯函数，用 9 组代表性输入（含播放头
> 落在中间、落在两端之外、窗口只有 2 个样本、样本数超上限）跑一遍，
> 逐个核对选出的 p0/p1/p2/p3 与 delta。**纯逻辑改动完全可以离线验证**，
> 不必等实机。

### 21.3 移动更新：O(更新数 × 实体数) → O(实体数 + 更新数)

`OnReferencesMoveRequest` 原本对**每一条** update 都做一次
`std::find_if` 遍历整个 view。一个繁忙格子里每个在范围内的 actor 都是一条
update，于是这个热路径成了**平方复杂度**，而它每 100 ms 就跑一次。

改为**先建一次索引**（serverId → entity），再逐条 O(1) 查表。

### 21.4 审计发现的两个真实空指针崩溃

**（a）`CharacterService::OnActorAdded`**——发现扫描上报的 form id 可能
**已经解析不出 actor**（扫描与本次调用之间 actor 卸载，或临时引用已被引擎
回收）。而 `acEvent.FormId == 0x14`（玩家）分支**无条件解引用**返回的
指针 —— 在这个竞态上就是一次空指针写。已补 `if (!pActor) return;`。

**（b）同函数**在命中 `RemoteComponent` 索引后**重新取了一次 actor**，
把上面刚校验过的指针丢掉了。改为复用已校验的指针。

> **审计教训**：`TESForm::GetById` 是**可能返回 null 的查询**，不是断言。
> 同一个函数里"取一次、校验、然后再取一次"，第二次取值会让编译器
> （和人）失去第一次的证明。**要么只取一次，要么每次取完都判空。**
> 全仓库还有 ~50 处 `Cast<Actor>(TESForm::GetById(...))`，
> 本次只修了有**明确无保护解引用**的两处，其余需要逐个确认调用上下文。

### 21.5 全仓库 `GetById` 解引用审计（v1.1.0 续）

对上一条遗留的 ~50 处做了**全量机械审计**：用脚本抓
`var = Cast<T>(TESForm::GetById(...))`，然后向后看 10 行内**第一次
解引用** `var->` 的位置，若在此之前没有 `if (!var)` / `var &&` /
`var ?` 形式的守卫，就报出来。

**结果**：全仓库 123 处此类取值，61 处已有守卫，脚本报出 62 处可疑，
人工核对后确认 **19 处是真无守卫**，其余是 C++17 `if (T* x = ...)`
初始化语句或已被注释掉的代码——**正则抓不到初始化语句**，这是本次
审计的已知假阳性来源。

修掉的真实缺陷（按危害排序）：

| 位置 | 问题 | 触发条件 |
| --- | --- | --- |
| `Actor::Create` | `GetById(0x14)` 取玩家后**连解引用三次** | 主菜单 / 玩家尚未建立 |
| `PlayerService` ×5 处 | `0x100F19`(KillMove)、`0xB8EC1`(WorldEncounters) 取全局后直接写 `->f` | 加载顺序缺这两条记录；**断线路径也写**，所以退出时崩 |
| `PartyService` | 同上 `0xB8EC1` | 同上 |
| `Actor::SetEssential` | `GetById(0xDB1)`(PlayerFaction) 后 `SetFactionRank` | 同上 |
| `Actor::Spawn` | 基表单取不到仍 `Actor::Create(nullptr)` | 基表 id 无效 |
| `ObjectService` 锁变更 | `GetById(acEvent.FormId)` 后直接 `GetParentCellEx()` | form id 来自**事件**而非活引用，排队执行时对象可能已消失 |
| `InventoryService` 收武器轮询 | **每帧**遍历本地实体时解引用可能已死的 actor | actor 在遍历途中卸载 |
| `QuestService` 场景事件 | `GetById(sceneFormId)` 后直接 `->owningQuest` | 场景已卸载；**同一文件的阶段事件处理器已经判空** |
| `ContainerDebugView` ×2 | 排队 lambda 解引用**几秒前捕获**的 actor | 队列执行时已卸载 |
| `CharacterService::OnActorAdded` ×1 | 命中索引后**重复取值**丢掉已校验的指针 | 见 21.4(b) |

**修法**：给硬编码 id 起名（`kKillMoveGlobalId` 等）并把
`if (T* p = Cast<T>(...))` 作为**统一写法**——校验和取值在同一条语句里，
不可能只做一半。

> **审计教训（补充）**：
> 1. **"已知可达"不等于"一定可达"**。这些 id 都是原版固定记录，作者假设
>    它必然存在——但加载顺序可以改变这一点，而**崩溃发生在退出路径**上，
>    最难复现。
> 2. **正则审计会有假阳性，但不会漏掉真阳性**（在保守写法下）。62 报
>    19 真看着效率低，但比人工翻 123 处可靠。**关键在于明确写出假阳性
>    的成因**（C++17 初始化语句），否则下一轮审计会重复踩。
> 3. 修 `Cast<T>(TESForm::GetById())` 时**优先用初始化语句**而不是
>    "先声明、再 if"——后者留出了"有人插一行解引用"的位置。

---

## 22. v1.1.0 二次审计：链式解引用与"cell 可能在 load 中为空"

### 22.1 现象与根因

第一轮审计只看了 `var = Cast<T>(GetById(...))` 这种**赋值式**取值。
第二轮换了个判据：找 **`A()->B()->` 链式解引用**——一次表达式里
连续解引用两个"查询结果"。

抓到 3 处，其中 2 处是真缺陷：

| 位置 | 问题 | 触发条件 |
| --- | --- | --- |
| `DiscoveryService::VisitInteriorCell` | `PlayerCharacter::Get()->GetParentCellEx()->formID` | **loading 中**：玩家存在但 parentCell 为空 |
| `DiscoveryService::VisitExteriorCell` | `GetCellFromCoordinates` 回退后仍可能为空就取 `->formID` | 目标格尚未 attach |
| `PlayerCharacter` `HookSetBeastForm` | 游戏 hook 里直接 `PlayerCharacter::Get()->GetExtension()` | hook 在玩家对象建立前触发 |
| `OverlayService::Reload` | `GetOverlayApp()->GetClient()->GetBrowser()` | CEF 尚未产生 app 时从 UI 调用 |

`VisitCell()` 只在**入口**判了 `if (!pPlayer) return;`，进到
`VisitInteriorCell` 之后就不再判了——这是典型的"守卫只做了一半"。

### 22.2 为什么第二轮才抓到

第一轮的正则要求 **`变量 = Cast<T>(...)`** 的形态，链式写法
（`F()->G()->h`）里根本没有中间变量，**两轮判据正交**。

> **审计教训**：审计的**判据决定覆盖率**，不是"审计了一遍"就完事。
> 交换判据（赋值式 → 链式；返回值 → 参数；单函数 → 跨帧）比
> 在同一判据上加大力度有效得多。本轮两次交换都立刻抓到新缺陷。
>
> 另一个具体教训：**loading 期间"对象存在但成员为空"是常态而不是异常**。
> `PlayerCharacter::Get() != nullptr` **不蕴含** `GetParentCellEx() != nullptr`。
> 玩家对象在 load 全程存活，它的 cell 却在换——所有 `Get()->GetX()->`
> 的写法都要按"load 中"来审。

---

## 23. v1.1.0 三次审计：容器迭代中改动、以及"审计本身引入的缺陷"

### 23.1 新判据

第三次换的判据是**容器生命周期与迭代**：

| 判据 | 命中 | 真缺陷 |
| --- | --- | --- |
| `for (x : container)` 体内 `container.erase/clear/destroy` | 10 | 0 |
| `m_world.get<T>(e)` 取引用后同类型 `emplace/remove<e>` | 13 | 0 |
| `A()->B()->` 链式解引用 | 3 | 2（见 §22） |
| `try_get<>` 直接 `->` | 0 | 0 |
| `size()` 与负数比较 | 0 | 0 |

**全部 10 处"迭代中改动"逐个核对后都是安全的**，因为都遵循了
**先收集、后改动**（`toDestroy` / `readyEntities` 两个中间容器）或
**在循环外 clear**。这说明代码库在这条判据上已经是干净的——
**没有命中不等于没审，等于这条判据下确实没有缺陷**。

### 23.2 审计自己引入的缺陷（重要）

第二轮我给 `Actor::Create` 加了玩家判空，但**加在了 `auto pActor = New();` 之后**：

```cpp
auto pActor = New();              // 已经分配
pActor->SetSkipSaveFlag(true);
...
if (!pPlayer) return nullptr;     // 提前返回 -> pActor 泄漏
```

`New()` 走 `GameHeap::Allocate` + `ActorExtension` 构造，**不是**无副作用的
查询。把守卫插在分配之后就制造了一个**新缺陷**，而且它只在"玩家不存在"
这条冷路径上泄漏——**正好是最难观察到的那条**。

已改为**先判空、再分配**。同时发现 `DebugService` 里
`Actor::Create()` 的返回值**从来没判过空**就连续三次解引用，
且 `PlayerCharacter::Get()` 与 `baseForm` 也未判——一并补上。

> **教训（本轮最重要的一条）**：**修 null 崩溃时，守卫必须放在副作用之前**。
> "判空 + 提前返回"只在**该 return 之前没有任何已发生的副作用**时才等价于
> "不做这件事"。看到 `New()` / `Create()` / `emplace` / `Open()` /
> 任何 `allocate` 样式的调用，守卫要插在它**上面**。
>
> 附带一条：**"函数返回值可空"这个契约变了，所有调用点都要重审**。
> `Actor::Create` 以前实际上不会返回 null（内部直接解引用玩家），
> 我让它能返回 null 之后，23 个调用点里 `DebugService` 那一处就会崩。
> **改契约和改实现是两件事，不能只做后者。**

---

## 24. 2026-09-25 傍晚：CI 全红与打包阻断（两处都是"成功的那次埋的雷"）

这两条是同一轮里先后暴露的，且都不是"谁写错了逻辑"，而是**验证基础设施自己
把自己锁死**。共同点：**上一次成功运行留下的产物，成了下一次必然失败的输入**。

### 24.1 vcpkg 缓存命中反而跳过自己的 checkout

**现象**：`49378750`、`5afa14fb`、`85873946` 三次 push 全红，且都在
**step 9 `Set up vcpkg`**、37 / 44 / 60 秒内死掉（三次实测）——**编译前**。而更早的失败都在
step 11/19（真的在编译）。

**根因**：`dc77b99b` 为了让冷缓存不必重编 commonlibsse-ng，把
`.vcpkg/downloads` 加进了 `actions/cache` 的路径。但"Set up vcpkg"的守卫判的是
**目录**：

```powershell
if (Test-Path $vcpkg) { "restored from cache" } else { git clone ... }
& "$vcpkg/bootstrap-vcpkg.bat"   # ← 这个文件从没被取下来
```

缓存命中会**创建** `.vcpkg/downloads`，于是目录判据为真 → 跳过 clone →
下一行执行一个不存在的 bat → 退出码 1。

**为什么不会自愈**：缓存一旦写下就一直在，之后每次 run 都命中、都走同一分支。

**证据（三条独立）**：
1. 全仓唯一一个 `Windows-vcpkg-*` 缓存创建于 `2026-09-25T12:18:05Z`，
   **正是最后一次绿灯 `2579c599` 结束的瞬间**——是那次成功构建自己写的；
2. 该时间点之后的每一次 run 都死在 step 9，之前没有一次死在 step 9；
3. 本机复现判据：造一个只有 `downloads/` 的 `.vcpkg`，旧判据为真而
   `bootstrap-vcpkg.bat` 不存在。

**修法（`9ad19de2`）**：
```powershell
if (Test-Path (Join-Path $vcpkg 'bootstrap-vcpkg.bat')) { ... } else {
  $scratch = Join-Path $env:RUNNER_TEMP 'vcpkg-clone'
  git clone --depth 1 ... $scratch          # 不能直接 clone 进 .vcpkg
  Get-ChildItem -Force -LiteralPath $scratch | Move-Item -Destination $vcpkg -Force
}
```

**为什么必须绕到 scratch**：`git clone` **拒绝非空目标目录**
（`destination path already exists and is not an empty directory`），
而 `.vcpkg` 里恰恰躺着刚恢复的 downloads——那正是缓存存在的理由。
直接 clone 会把"文件缺失"换成"目录非空"，**同样是红的**。

**验证**：`Build windows #142` = **Success 13m 45s**（修复前 #140 = 1m 0s），
`plugin-artifacts` 2.88 MB 上传成功（step 13，远在 step 9 之后）。

> **教训**：**缓存的守卫必须判"缓存提供了什么"，而不是"目标路径在不在"。**
> `Test-Path <dir>` 对"部分恢复"和"完整就绪"给出同一个答案，
> 而这两者需要完全不同的后续动作。凡是被缓存的路径，都要问一句
> "命中之后，我下一步真正需要的那个文件/目录，是缓存给的吗？"

### 24.2 打包演练第一次真正跑起来，就抓到一个发布阻断

`49378750` 加 step 28 的**全部理由**是"release.yml 只在 tag 跑、而发布已冻结，
打包路径从未被执行过"。它加完之后**一次都没跑成**（被 §24.1 挡在前面）。
§24.1 修好，它立刻失败——**这正是它存在的意义**。

**报错**：
```
PLUGIN STAGING FAILED (2)
  - IEDSyncTogether produced no 'IEDSyncTogether.esp' under plugin-artifacts/IEDSyncTogether
  - missing staged artifact: (OptionalPlugins)/IEDSyncTogether/IEDSyncTogether.esp
```

**根因**：`Code/plugins/plugins.json` 把 `IEDSyncTogether.esp` 列在 **`artifacts`**
下，语义是"从该插件自己的构建输出里拷"。但**全流程没有任何一步生产它**：

| 谁生成 | 在哪 | CI 跑不跑 |
|---|---|---|
| `Write-MinimalPlugin`（TES4 + CNAM/SNAM） | `plugins/IEDSyncTogether/build-vortex.ps1` | **不跑**——CI 对每个插件只调 `cmake -S/-B/--build` |
| `IEDSyncTogether.dll` | CMake | 跑 |

`git grep 'build-vortex' .github/` = **空**。所以这个 artifact 条目要求一个
**不可能存在的文件**。

**这不是"演练脚本的问题"**：`release.yml:104` 调的是**同一个脚本、同一个
`ArtifactsRoot`**。**即使解冻、推 tag，release 也会死在打包这一步。**
换句话说，冻结文件里写的"插件工作尚未落地"，被这条失败**实证**了。

**这个 esp 是载荷，不是装饰**：`IEDBridge.cpp` 与 `RemoteIEDRenderer.cpp`
都把 `"IEDSyncTogether.esp"` 当 plugin key 传给 IED 的 Papyrus 调用
（`CreateItemActor` / `SetItemFormActor` / `AddActorBlock` …），
少了它插件什么也不做。

**修法（`a8b0f76e`）**：改为 **payload**——本仓库自持的文件，
与 `STRPluginMessagingAPI.ini` **完全同构**（那处的 `payloadNote` 已经写明理由：
插件仓库是 pinned submodule 且本仓库不推它们，所以"必须存在才能打包"的文件
只能放在这里）。

**逐字节验证（不是"看起来对"）**：
1. 从 `build-vortex.ps1` **抽出** `Write-Subrecord`/`Write-MinimalPlugin` **原样执行**，
   与提交文件比对 → 两侧 `110 B / sha256 9811c7bd…` **一致**；
2. 结构自洽：`TES4` 签名、`dataSize=86` 与文件长度 `24+86=110` 相符、
   `HEDR/CNAM/SNAM` 三条子记录**恰好消费到 EOF**；
3. git **存为二进制**（`--numstat` 报 `- -`），暂存 blob 的 sha256 与工作文件相同
   → 行尾规则改不动它；
4. **端到端**：按 CI 真实产物集（仅 CMake 输出、`plugin-artifacts` 里没有 esp）
   跑打包 → 成功，zip 内 `OptionalPlugins/IEDSyncTogether/IEDSyncTogether.esp` 字节正确。

**验证**：`Build windows #142` step 28 `Verify the packaging pipeline (dry run)` = **success**
（`plugin-artifacts` digest `3149d8f8…`）。

> **教训**：**`payload` 与 `artifacts` 的分界线不是"文件类型"，而是"谁生产它"。**
> 凡是上游在 CMake 之外（自己的 build 脚本、手工、CreationKit）产出的文件，
> 在 CI 里**等于不存在**，必须走 `payload` 并由本仓库持有。
> 这条已写进 `plugins.json` 的 `$comment`，因为**下一个插件会重犯**。

### 24.3 复核用的命令（照抄）

```powershell
# 1. 缓存是不是"成功那次写的"（比对缓存创建时间与最后一次绿灯的结束时间）
#    在 Actions 页 / API: /actions/caches 看 created_at

# 2. 哪些 run 死在哪个 step（失败步骤比失败信息更快定位）
#    /actions/runs/<id>/jobs -> jobs[].steps[] 里 conclusion=failure 的 name

# 3. CI 到底跑不跑某个插件自己的构建脚本（应为空）
git grep -n 'build-vortex' -- .github/

# 4. 某个 esp 的逐字节真值：拿插件脚本原样跑一遍再比
#    见 §24.2 的验证步骤 1（抽出 Write-MinimalPlugin 后 Invoke-Expression）

# 5. 清单里某个文件是 payload 还是 artifact
python -c "import json;d=json.load(open('Code/plugins/plugins.json',encoding='utf-8'));[print(p['id'],'payload=',[x['source'] for x in p.get('payload',[])],'artifacts=',[x['from'] for x in p.get('artifacts',[])]) for p in d['plugins']]"
```

---

## 25. 2026-09-25 晚：清掉 §15.5 的两条遗留（i18n 与"从不执行的测试"）

§15.5 当时把"本次审计**未做**的"列成清单，理由是"审计结论如果不写清哪些查过、
哪些没查，下一轮会把'查过且故意保留'重新当成'没查'再查一遍"。本节结掉其中两条。

### 25.1 i18n：七个语言补到 0 缺键

§14.2 当时只把 **es/fr/nl/pl/zh-CN** 补到 0 缺键，**cs/de/ja/ko/no/ru/tr** 留了
5~11 条，§15.5 记了"补法照 §14.2 抄"。现已补齐，**全库 13 个语言文件无一缺键**。

补的是玩家在**最难受的时刻**会看到的那批：

| 键 | 什么时候出现 | 原先缺 |
|---|---|---|
| `COMPONENT.CONNECT.SUCCESS.TITLE/MESSAGE` | 连上服务器后的弹窗 | cs,de,ja,ko,no,ru,tr |
| `COMPONENT.ROOT.STATUS.OFFLINE/CONNECTING/ONLINE` | 连接状态标签 | cs,de,ja,ko,no,ru,tr |
| `COMPONENT.ROOT.REVEAL_PLAYERS` | 同面板的按钮 | cs,ja,ko,no |
| `COMPONENT.CHAT.SET_TIME_ARGUMENT_COUNT / SET_TIME_INVALID_ARGUMENTS` | `/settime` 拒绝时的提示 | cs,de,ja,ko,no |
| `SERVICE.COMMANDS.NOT_ADMIN` | 指令被拒的原因 | cs,de,ja,ko,no |
| `SERVICE.ERROR.ERRORS.BAD_UGRIDSTOLOAD / NON_DEFAULT_INSTALL` | 启动期两条长警告 | cs,de,ko |

后两条尤其值得点名：ja/no/ru/tr/es/pl/zh-CN **本来就有**，只有 **cs/de/ko** 在显示
英文的 uGridsToLoad / 非原版安装警告。

**做法**：插入顺序跟随 `en.json`，文件格式**原样保留**（CRLF、无 BOM、
`ensure_ascii=False`、结尾换行）。动手前先对七个文件各做一次
"读入→重写→比对"，**确认重写结果与原文逐字节相同**才批量插入——
否则 diff 里会混进成千上万行无关的行尾变化，把真正的改动淹掉。

**验证**：逐键与 `en.json` 比对（缺键/多键分开报）+ 每个 `{{占位符}}` 与该键英文串
的占位符集合比对（**0 处不一致**）。es/fr/pl/nl/zh-CN 本来就各多 1~5 个键，
那是它们自己的内容，不在本次范围、未动。

### 25.2 编码测试：从"每次编译"到"真的执行"

`TPTests`（catch2，5 个 `TEST_CASE`）**每次构建都被编译，从未在任何地方运行**：
CI 只 `xmake -y` 构建，而 playable-build 工作流又特意把 `*Tests.exe` 从载荷里剔掉。
**一个从不运行的测试，等于一个已经坏掉的测试。**

§15.5 留这条时写了前提："**要接上得先确认 catch2 目标能在 CI 跑通**"。
这个前提已由 §24.1 修复后的绿灯构建**证实**（`*Tests.exe` 确实被产出——
playable-build 的 `Remove-Item ... *Tests.exe` 就是它存在的证据）。

**改动**：`windows.yml` 新增 **"Run the encoding tests"**，位置在**最后一次 C++ 编译之后、
打包之前**（step 21），**阻断**而非警告——只警告的闸门等于没有闸门。

**为什么值得阻断**：这 5 个用例覆盖的是**整个协议依赖的序列化往返**
（`ClientMessageFactory`/`ServerMessageFactory` 的 opcode 提取、各类消息的
`Serialize`→`DeserializeRaw` 相等性）。**往返错了不是编译错误，是 desync。**

**踩到的坑（值得记）**：我第一版用 `Start-Process -PassThru` 拿 `$proc.ExitCode`
来判断成败。**在本机 Windows PowerShell 5.1 上**，`.ExitCode` 对**成功和失败的子进程
都返回空**，而 PowerShell 里 **`$null -ne 0` 为 True** —— 于是**测试全过也会把这一步判失败**。
改用本文件已有的 `$log = & $exe 2>&1; $code = $LASTEXITCODE` 形式后正确
（用真 exe 实测 exit 0 / exit 1 都如实反映）。

> **教训**：**不要用 `Start-Process` 的 `.ExitCode` 做 CI 判据**，
> 至少要先确认它真的会填值；`$null` 参与数值比较是**静默**的错，
> 不会报错、只会把结果判反。判"外部程序成败"用 `&` + `$LASTEXITCODE`。
>
> **这条的边界要说清**：本机只有 **PowerShell 5.1**，而 CI 用的是 **pwsh 7**
> （`shell: pwsh`）。所以"runner 上 `.ExitCode` 也是空"**我没有验证过**，
> 是**推断**。改动的依据不是"runner 会坏"，而是：**已确认 5.1 上它会坏，
> 而 `&` + `$LASTEXITCODE` 是本文件其它步骤一直在用、且 CI 已实证可用的形式**——
> 换成一个已证实可用的写法，成本为零，没有理由留着未证实的那个。

**验证**：`Build windows #143` = **Success 13m 50s**，其中
**step 21 `Run the encoding tests` = success**、**step 29 打包演练 = success**。
即：测试**确实跑了，而且全过**——这是这条遗留项从"只编译"变成"真跑"的实证。

**复核时发现的第二个坑（同一处，已修）**：第一版只判 `$LASTEXITCODE`，
但**退出码 0 本身不证明跑过任何测试**——catch2 在**没有任何用例匹配**时
打印 `No tests ran`（源码 `catch_reporter_console.cpp`：`totals.testCases.total() == 0`）
**并且退出 0**。于是"用例被清空 / 目标没编进去 / 过滤器匹配到零个"
和"全部通过"**长得一模一样**，而**"从不运行的测试"正是这一步要防的那件事**。
现在补了一条：`$code -eq 0` 且输出里**没有** `All tests passed` 即判失败。
四种真实输出形态都验过：

| 输出 | 退出码 | 判定 |
|---|---|---|
| `All tests passed (N assertions in M test cases)` | 0 | PASS |
| `No tests ran` | 0 | **BLOCKED: 无结果** |
| 空输出 | 0 | **BLOCKED: 无结果** |
| `test cases: 1 \| 0 passed \| 1 failed` | 1 | BLOCKED: 失败 |

> 表中 `N` 是**占位**：真实断言数由 catch2 在运行时算出，本机没有 Windows
> 构建环境、跑不了这个二进制，所以**没有实测值**。判据只匹配
> `All tests passed` 这个前缀，与 N 无关，因此不需要那个数字。

> **教训**：**"进程成功退出" ≠ "做了它该做的事"。**
> 判一个闸门有没有真的跑起来，要看**它自己的成功标志**（这里 `All tests passed`），
> 不能只看退出码。凡是"零工作量也返回成功"的工具（catch2、多数测试框架、
> `grep` 无匹配、`for` 空集合），这条都成立。

### 25.3 本轮改动自身的复核结论（2026-09-25 晚，逐项重审）

按"重审一遍"的要求把本轮**自己的改动**又过了一遍，结论如下（有问题的已当场修）：

| 项 | 复核方法 | 结论 |
|---|---|---|
| i18n 七个文件 | `git show f1cb45dd~1:<f>` 与现状**逐键比对**（缺/删/改三类分开报） | **纯增**：0 删除、0 改值；各语言多出 5~11 个键（cs/ko 11、de 10、ja/no 9、ru/tr 5）✅ |
| i18n 长文案 | 解码后比对**换行结构**（行数 + 空行位置），并检查值内**无裸 CR** | 7 个文件与 en 结构一致；pl/zh-CN 的 14 行是**上游既有**且**本次未动** ✅ |
| `.esp` 逐字节 | 抽出插件 `Write-MinimalPlugin` 原样执行后比对 | 110 B / `9811c7bd…` **两侧一致** ✅ |
| `.esp` 入包 | 按 CI 真实产物集跑打包 + 解 zip 校验 | 落地 `OptionalPlugins/IEDSyncTogether/IEDSyncTogether.esp`，字节正确 ✅ |
| 清单改动 | 双向核对：`../` 源可解析、payload/artifact 目标**无重叠**、dest **未变** | 全部通过；esp 已不在 artifact 列表 ✅ |
| `release.yml` 稀疏检出 | 本地建仓复现 `sparse-checkout --no-cone` 三路径 | `Code/plugins/**` **覆盖** `Code/plugins/packaging/`，esp 可见 ✅ |
| vcpkg 修法 | 重查 60 次 run：**只有 3 次**死在 step 9，最早一次 `12:43:52Z` | 全部在缓存写入（`12:18:05Z`）**之后**，与根因判断一致 ✅ |
| 测试步骤 | 四种输出形态喂给新判据 | 通过/无结果/失败三态**都能正确区分** ✅ |

**复核中改掉的两处（都是我自己的）**：

1. **§25.2 与 windows.yml 里写了"runner 上 `.ExitCode` 也是空"——这是未验证的推断。**
   本机只有 **PowerShell 5.1**，CI 用的是 **pwsh 7**。已改成如实表述：
   只声称"5.1 上实测为空"，并说明换写法的真正理由是
   **`&` + `$LASTEXITCODE` 是本文件既有、且 CI 已实证可用的形式**。
2. **测试闸门漏掉"零用例也算通过"**（见 §25.2 末），已补 `All tests passed` 判据。

> **教训**：**写进账本/注释的每一句"事实"都要能指着证据。**
> 这次两处措辞都是**顺手把本机结论外推到了 runner**——它不影响正确性，
> 但会让下一个读者以为有实机证据。账本的价值全在"可追溯"，
> 一句无法追溯的断言就足以让它整体贬值。

### 25.4 复核用的命令（照抄）

```powershell
# 1. i18n 缺键（应输出 none）
#    逐键比对 + 占位符比对，见 §25.1 的验证描述

# 2. CI 里测试步骤到底跑没跑（看 step 名与结论，不看整体绿灯）
#    /actions/runs/<id>/jobs -> jobs[].steps[] 里 name 含 'encoding tests' 的那条

# 3. 测试二进制是否真的被产出（playable-build 剔掉它 = 它存在）
git grep -n 'Tests.exe' -- .github/

# 4. `Start-Process` 的 ExitCode 陷阱，本机 30 秒复现
$p = Start-Process -FilePath 'powershell.exe' -ArgumentList '-NoProfile','-Command','exit 0' -NoNewWindow -PassThru -Wait
"ExitCode=[$($p.ExitCode)]  (null -ne 0) = $($null -ne 0)"
```

---

## 26. 2026-09-26 场次：F8 / `#if 0` / EF 哨兵 / BehaviorVar 四项结案，以及**一次真实的文件误删**

本轮起点是 §15.5 遗留清单的最后三条 + §19.7 的物种判据。**逐条查完后
三项结案、一项维持不动**，但过程中查出一个**已经进了 main 的真实缺陷**，
以及我自己在 §26.2 里犯并当场纠正的一次误判。

### 26.1 先说缺陷：34 个地址库文件在 `dc77b99b` 里被误删

`dc77b99b`（"plugins: fix what the review of the three commits turned up"）
声称的改动是 5 个文件（release.yml / windows.yml / plugins.json /
STRPluginMessagingAPI.ini 搬家 / ModuleConfig.xml 文案）。实际它**同时删掉了
`GameFiles/Skyrim/SKSE/Plugins/` 下全部 34 个文件**：

```
39 files changed, 47 insertions(+), 37015 deletions(-)
  GameFiles/Skyrim/SKSE/Plugins/version-1-5-97-0.bin | Bin 1490796 -> 0 bytes   (×10)
  GameFiles/Skyrim/SKSE/Plugins/versionlib-1-6-*.bin | Bin ...      -> 0 bytes   (×13)
  GameFiles/Skyrim/SKSE/Plugins/versionlib-ae-to-se-1-5-*.map | 3699 -------    (×10)
```

**这是打包路径上的实打实缺口，不是洁癖问题**：

1. `windows.yml:476-478` 的 artifact 路径是 **`GameFiles/Skyrim/`**，release 的
   package job 直接拿它当打包输入 —— 少了这 34 个文件，**打出来的包没有地址库**；
2. `VersionDb::Load`（`Code/client/VersionDb.h:205`）在
   `Data/SKSE/Plugins/` 里按 `versionlib-<ver>.bin` → `version-<ver>.bin` 顺序找；
3. 找不到 → `RunTiltedInit` 走 `ShowAddressLibraryError` → **`exit(4)`**。

也就是：**装了这个包的玩家一启动就弹"地址库失败"然后退出**。而本仓库自己的
文档正是这么承诺的 —— `docs/RELEASE-AND-MO2.md:52`、`docs/LAN-RADMIN-GUIDE.md:18`、
`docs/AUDIT-1.5.97-vs-1.0.18.md:27` 都写着"随包附带"。

**为什么"审查"没看出来**：这次误删**藏在一次移动里**。同一个 commit 把
`GameFiles/Skyrim/SKSE/Plugins/STRPluginMessagingAPI.ini` **移到**
`Code/plugins/packaging/`（那是有意为之，见该 commit 正文），于是
`GameFiles/Skyrim/SKSE/Plugins/` 这个目录在 diff 里"本来就该动"，
同目录下另外 34 个文件的 `D` 就跟着混过去了。

> **教训（新的判据）**：**搬家一个文件时，要单独数一遍被搬目录里剩下的文件。**
> 目录级 diff 会把"我故意的删除"和"我手滑的删除"混成一片，
> 而它们唯一的区别就是**同目录还有别的条目在动**。

**修法**：`git checkout dc77b99b~1 -- <那 34 个文件>`，逐文件比对 blob 哈希。

**验证（逐字节，不是"看起来对"）**：

```powershell
$bad=0
foreach ($f in (git diff --cached --name-only -- GameFiles/Skyrim/SKSE)) {
  $prev = git rev-parse "dc77b99b~1:$f"; $staged = git rev-parse ":$f"
  if ($prev -ne $staged) { $bad++; "DIFF $f" }
}
# files checked: 34   mismatches: 0
```

抽查三例（bin 与 map 各一，另加一个 AE 库）：
`version-1-5-97-0.bin f8ad64b3…`、`versionlib-ae-to-se-1-5-97-0.map 9be2dddd…`、
`versionlib-1-6-1170-0.bin 7671773e…` —— **删除前与本次暂存的 blob 哈希相同**。

### 26.2 `#if 0`：22 处逐块判读完毕，删 20 留 2

`§15.5` 第 5 条"未逐个判断"本轮做完了。判据只有两条：
**① 块内内容在别处是否还活着；② 它是不是一个"改 0 为 1 就能恢复"的开关。**

| 块 | 内容 | 判读 | 处理 |
|---|---|---|---|
| `BSThread.cpp` ×2 | `THREADNAME_INFO`/`Hook_RaiseException`/`Hook_CreateThread` + havok 线程名 patch | 无引用、上游实验残留；patch 需要 `Anchor(57704)` 的硬编码偏移 | **删** |
| `CombatController.cpp:80` | `POINTER_SKYRIMSE(TUpdateTarget, 33236)` + `TP_HOOK` | **有意禁用**：提交 `61ebb6e9` 标题就是 "Disable old combat targeting system (to test)"，块外还留着 `HookUpdateTarget`/`RealUpdateTarget` 供改回 | **留** |
| `MapMenu.cpp:10` | `GamePatch::Nop(pHookLoc+0x53/0x9D/0x9F)` | **有意禁用**：块首注释写明 "Disabled because the mapmenu in first person breaks / I fix that later"；`kAllowList` 里的 `MapMenu` 同样被注掉，两处互为印证 | **留** |
| `UI.cpp:92` | `spdlog::info("Menu requested {}") ` | 纯日志；同文件另有 `UIMessageQueue__AddMessage` 的日志探针可用 | **删** |
| `BSScript.h:326` | `EventArguments` 模板，自带注释"Bad PoC code" | 无引用，且模板 `using Tuple = std::tuple<EventArguments...>` 本身写错 | **删** |
| `Sky.cpp` ×3 | 三处 `s_shouldUpdateWeather` 判据 + 两行 debug 日志 | **这个是真开关**：debug 天气窗的 "Toggle weather updates" 写这个变量，而三处读取全在 `#if 0` 里 → **开关从来没生效过**。见 §26.3 | **启用判据、删日志** |
| `ComponentView.cpp:18` | `CalculateFloatingQuestMarkerAlpha` | **引用未定义标识符 `this`、`result`、`FLOAT_100_0`、`fsqrt` 没 include** —— 这代码**编译不过**，所以它从来没被编译过，恢复=修 bug 而不是开开关 | **删** |
| `CombatService.cpp` ×2 | `OnHitEvent`（把命中写进 `CombatComponent` + `SetCombatTargetEx`）、`RunTargetUpdates`（200ms 节流的计时器） | **有意禁用**，与上面两块同属 `61ebb6e9`；`CombatController::UpdateTarget` 里还留着"若挂了 `CombatComponent` 就不要再选目标"的**消费方**代码，删掉生产者等于把这条链路截断 | **留** |
| `DebugService.cpp:200` | F6/F7/F8 三键 | **这是有意保留的开关**：§16.3 明写"所有配置一律裁掉…下次要用把 0 改 1，别删" | **留** |
| `DiscordService.cpp:184` | `set_log_hook` | 调试开关 | **删**（§15.3 曾以它为例说"删除收益极低"——本轮按"是否有引用"重判，结论翻转） |
| `ImGuiDriver.cpp:96` | 五条 `ImGuiStyle` | 纯外观 | **删** |
| `imgui_impl_win32.cpp/.h` ×2 | 官方示例里让用户复制到 .cpp 的那行 forward declaration | **上游文件，不是我们的代码** | **留** |
| `Launcher.cpp:41` | `if (!g_context) __debugbreak();` | 调试断言 | **删** |
| `GLM_Bindings.cpp:75` | vec4 四则运算绑定 | `vec2`/`vec3` 有、`vec4` 没有；**在别处并没有活着的副本** | **删**（见 §26.4） |
| `ScriptBindings.cpp` ×2 | `ModsComponent`/`Entry`/`TModList` 绑定 | 见 §26.4 | **删** |
| `ScriptService.cpp:204` | `RegisterExtensions` | 头文件里**本来就是注释状态**（`ScriptService.h:39`），无声明无调用 | **删** |

结果：`Code` 下 `#if 0` 由 **22 → 7**：**删 15 留 7**。

留下的 7 处，每一处都是**"改 0 为 1"型开关**，且块外还留着它需要的另一半：

| 留下的块 | 位置 | 块外留着的另一半 |
|---|---|---|
| F6/F7/F8 | `DebugService.cpp:200` | §16.3 明写"别删" |
| 旧战斗瞄准 ×3 | `CombatController.cpp:80`、`CombatService.cpp:165/204` | `HookUpdateTarget`/`RealUpdateTarget`、`CombatComponent` 的消费方 |
| 地图菜单 ×1 | `MapMenu.cpp:10` | `UI.cpp:80` 里同样被注掉的 `kAllowList` 条目 |
| imgui 上游 ×2 | `imgui_impl_win32.cpp/.h` | 不是我们的代码 |

**过程记录（本轮自己犯的错）**：我第一遍把上面那 4 处**战斗/地图**的块判成了
"上游残留、无引用"，并按这个判断跑脚本删掉了。**判据错在哪**：
我只查了"符号还有没有别的引用"，没有查"**块外是不是还留着它成对的另一半**"。
`CombatController` 块外留着 `HookUpdateTarget` + `RealUpdateTarget`，
`CombatService` 块外留着 `CombatController::UpdateTarget` 里的 `CombatComponent` 消费方，
`MapMenu` 块外留着 `UI.cpp` 里同样被注掉的条目 —— **这三对都是"开关"的签名**，
而 `DiscordService`/`Launcher`/`ImGuiDriver` 那些**没有**另一半，才是真残留。

已用 `git checkout HEAD -- <三个文件>` 完整还原，`git status` 显示这三个文件
**不再出现在改动列表里**（即回到与 HEAD 逐字节相同）。

> **教训（补充 §26.1 那条）**：判断 `#if 0` 是"开关"还是"垃圾"，**不看块里写了什么，
> 看块外有没有为它留位置**。留了 `Real*` 指针、留了消费方、留了同样被注掉的兄弟条目，
> 就是开关；什么都没有，才是垃圾。

### 26.3 三处新注释与代码不符（本轮自己引入的，已改）

新写的注释里有三句"事实"**指不出证据**，按 §25.3 的同一条规矩当场改掉：

| 位置 | 原话 | 为什么不成立 | 改成 |
|---|---|---|---|
| `Sky.cpp` | "made the toggle inert **for SetWeather and ForceWeather** while UpdateWeather honoured it" | `git show HEAD:...` 显示**三处读取全在 `#if 0` 里**，UpdateWeather 也**没有**生效 | 三处都失效；并说明 `s_shouldUpdateWeather` **只**由 debug 天气窗写，而该窗在 `#if (!IS_MASTER)` 内 → 对 release 版无影响 |
| `ScriptBindings.cpp` | "bound members `ModsComponent` **no longer has**" | `Code/server/Components/ModsComponent.h` 里 `AddStandard/AddLite/AddServerMod/GetStandardMods/GetLiteMods/GetServerMods/IsInstalled/Entry/TModList` **全都还在** | 改为"这段从未被编译过" |
| `GLM_Bindings.cpp` | "vec2 and vec3 bind the arithmetic operators **here**" | 它们绑在**各自的** `BindVec2/BindVec3` 里，不在 vec4 这一节 | 改为"vec4 缺的正是这两个函数里绑的那组运算" |

> 三句都是"读起来对、查一下不对"。**注释里的事实必须能指着代码**，
> 这条已经连着两轮抓到东西了（§25.3、本轮）。

### 26.4 两块"删除"的额外代价，先算清再删

- **`ScriptService::RegisterExtensions`**：删掉后 `BindTypes`/`BindStaticFunctions`
  是否变成"只有声明没有调用"？
  `BindTypes` 在 `ScriptBindings.cpp` **自己内部被调**（`BindTypes(aState)`），
  `BindStaticFunctions` 同理 —— **不是死函数**，删掉的是那段永远不编译的包装。
- **`vec4` 四则运算**：删掉后 `vec4` 会不会**比以前更弱**？
  不会 —— 被删的块**从未被编译**（`#if 0`），`vec4` 的现状就是它的全部能力。
  真正缺的（vec4 运算）**是待办而不是回归**，注释里如实写明。

### 26.5 EF 哨兵：**不改代码，只写清为什么不用改**

`§10` 与 `§17.4` 都记着"EF 互操作的 `_initterm_e` 哨兵只在 launcher 路径装"。
本轮把两条路径的**加载顺序**查清楚了，结论是 **SKSE 路径本来就不需要它**：

| 路径 | 顺序 | 哨兵是否必要 |
|---|---|---|
| launcher（`ExeLoader.cpp:334`） | 我们的 hook 先装 → EF 之后加载 → **可能覆盖**我们的 `GameHeap::Allocate` hook | **必要**（`Hook_initterm_e` 就是干这个） |
| SKSE 插件（`STClient_Bootstrap`） | SKSE 已把 EF（含 preloader）加载完 → 我们才装 hook | **不需要**：hook 装在那条 `ff 25` thunk **之上**，通过 trampoline 链到 EF 的分配器 |

证据是这条链**运行时自检**出来的，不是推断：`HookAudit` 对同一目标打印
`already held a branch … leading to EngineFixes.dll+0x…`，§17.2 的实机日志正是它。

**改动**：只在 `Memory.cpp` 的 `HookFormAllocateSentinelInit` 上方补了这段
"哪条路径装、为什么另一条不用装"的注释，**一行业务逻辑都没动** ——
与 §17.4 的"无实机验证手段时风险大于收益"一致。
`§10` 里那条【P3，待办】据此**结案**（是"设计如此"而非"漏装"）。

### 26.6 BehaviorVar 物种判据：**维持 §19.7 的结论，不动**

`§19.7` 已经论证过：狼/麋鹿被认成 `Cow` 的根因是**签名判据不足以区分物种**
（狼的 110 个变量同时满足 8 个 replacer 的签名），三个"更聪明"的判据实测
**7/9 → 7/9 / 7/9 / 3/9**，没有一条更好。要真修得换一个能唯一标识物种的量
（editorID / 模板）——**那是新功能**。

本轮复核确认：`BehaviorVar.cpp:414-432` 的 `matchedReplacers[0]` 与那条
`critical` 日志**都还在、都符合 §19.7 的描述**，代码没有漂移。
**结论不变：YAGNI，不修。** 这条不要再当待办重开。

### 26.7 F8 / `PlaceActorInWorld`：**保留，但不接线**

现状：函数体在（`DebugService.cpp:82`，判空齐全），**唯一调用点被注释**
（`:242` 的 `//PlaceActorInWorld();`）。§15.5 第 3 条要求"恢复需要实机验证会不会崩"。

本轮判定 **恢复调用点的前置条件不成立**，理由是代码本身：

1. 函数开头 `if (m_actors.size()) return;` —— 只能生成**一个**；
2. `Actor::Create` 造出来的 actor 会 `GetExtension()->SetRemote(true)`（`Actor.cpp:222`），
   而 `PlaceActorInWorld` 随后**又** `SetPlayer(true)`（`DebugService.cpp:104`）；
3. `m_actors` 是 `GamePtr<Actor>`，**全仓除这三行外没有任何读者**。

也就是说这个"调试功能"造出来的东西**既不是玩家也不是远程玩家**，且没人消费它。
它是**未完成的实验**，不是"接上就能用"的开关。**保持注释状态**，
与 §15.3 的"保留"一致；要恢复必须先决定它想造什么（新功能）。

### 26.8 复核用的命令（照抄）

```powershell
# 1. 误删清单（本轮缺陷）
git show --stat --format='' dc77b99b | Select-String 'SKSE'
git show --raw  --format='' dc77b99b -- GameFiles/Skyrim/SKSE | Select-Object -First 5   # 全是 D，无 R

# 2. 逐字节复原验证（应输出 mismatches: 0）
$bad=0
foreach ($f in (git diff --cached --name-only -- GameFiles/Skyrim/SKSE)) {
  $prev = git rev-parse "dc77b99b~1:$f"; $staged = git rev-parse ":$f"
  if ($prev -ne $staged) { $bad++; "DIFF $f" }
}
"files checked: " + (git diff --cached --name-only -- GameFiles/Skyrim/SKSE | Measure-Object).Count
"mismatches: $bad"

# 3. #if 0 计数（应只剩 2 处，且都在预期位置）
git grep -c '#if 0' -- Code

# 4. 打包确实吃 GameFiles（误删为何是发布阻断）
Select-String -Path .github/workflows/windows.yml -Pattern 'GameFiles/Skyrim' -Context 2,1
```


### 26.9 发布冻结解除（2026-09-26）

`RELEASE-FREEZE.md` 已删除，README 中英两处横幅同步移除。**冻结文件里的解除前提
本轮已逐条核实**，不是"感觉可以了"：

| 冻结文件写的前提 | 本轮证据 |
|---|---|
| "插件工作已落地" | `a8b0f76e` 把 `IEDSyncTogether.esp` 从 `artifacts` 改成 `payload`（§24.2）；`48fd32ac` 把传输选择显式化；`dc77b99b` 修掉评审出的六处缺陷 |
| "闸门通过" | 本机跑完 `plugins.yml` 的**全部** gate，**5/5 通过**（见下） |
| "打包演练能过" | §24.2 记录的 `Build windows #142` step 28 `Verify the packaging pipeline (dry run)` = success |

**本机实跑的闸门（`plugins.yml` 的四个 job 全在内）**：

```
python Code/plugins/tools/strpm_contract.py check        -> CONTRACT OK (61 符号, pin 8fdcf481…)
python Code/plugins/tools/plugin_snapshot.py verify      -> SNAPSHOT OK (4 个插件快照与 pin 一致)
python Code/plugins/tools/check_exports.py               -> EXPORT CHECK OK (4 个入口点全部导出)
python Code/plugins/tools/check_transport_compat.py      -> TRANSPORT COMPATIBILITY OK (新 opcode 追加, chat 索引未动)
python Code/plugins/tools/merge_fomod.py check           -> FOMOD OK (与 plugins.json 同步, 过 ModConfig5.0.xsd, 45 个源)
```

外加 `Tools/Packaging/*.ps1` 语法解析通过、`plugins.json` 可解析（4 个插件）。

**保留的部分**：`release.yml` 里的 `freeze-check` job 与打包步骤里的第二道
`Test-Path` **都留着**。冻结文件自己写着"删除该文件就是全部开关"，
所以 guard 留着不会有副作用——它只在文件重新出现时才拦人，而那正是我们要的。

> **注意**：本轮**只解冻，不打 tag**。打 tag 会立刻触发 release 工作流并发布版本，
> 那是一个独立的决定。



---

## 27. 2026-09-26 第二轮：插件层专项、空指针清扫、F2 唯一化、发布 v1.1.2

§26 是同一场的上半段（F8 / `#if 0` / EF 哨兵 / 冻结解除）。这一段是**全仓复查 + 插件层
专项**，判据是**新的一条**：**同一个指针在同一份文件里被判空过，就不该在别处被直接解引用。**

### 27.1 服务端：一个可被远程打崩的空指针

`GameServer::HandleAuthenticationRequest` 在版本、人数、SKSE/MO2、密码四道检查之后
调 `PlayerManager::Create(aConnectionId)`。而 `Create` 是：

```cpp
const auto itor = m_players.find(aConnectionId);
if (itor == std::end(m_players)) { ... return insertedItor.value().get(); }
return nullptr;      // 这条连接已经有玩家行了
```

**没有任何"已认证"守卫**，所以同一条连接上的第二个 `AuthenticationRequest`（重试、
重复包、或恶意客户端）会拿到 `nullptr`，紧接着 `pPlayer->SetEndpoint(...)` 就是空解引用。

**验证**：`HandleAuthenticationRequest` 里四道检查都不拦重复请求；`m_messageHandlers`
对 `AuthenticationRequest` 也没有前置条件。**修法**：判空并拒绝重复认证。

### 27.2 插件层：注册了却永远不会被调用的两个回调

这是本轮最重的一处，因为**两个随包发布的插件都依赖它**。

| 接口 | 框架侧 | 插件侧 | 结论 |
|---|---|---|---|
| `registerListener`（ProxyResolver） | 只存进 `m_mappingListeners`，**从不触发** | `OStimTogether`（`STRPMTransport.cpp:246-259`）、`IEDSyncTogether`（`STRPMAdapter.cpp:132-142`）都注册了 | **真缺陷** |
| `setLogCallback` | 只存，从不调用 | 只在接口结构里声明，无人调用 | 存而不用 |
| `setLocalDisplayName` | 只存，从不读 | `OStimTogether` 真的调了 | 见 §27.3 |

**为什么这是缺陷而不是"设计如此"**：OStimTogether 用它维护
`_connectionByProxy` 反向映射，而它自己的注释写着这条映射是
**"Required when the local OStim thread contains a dynamic STR proxy"**；
IEDSyncTogether 则在注册成功后打印 `STRPM ProxyResolver listener registered`。
两边都当它工作。

**修法**：在 `PluginMessagingService::Initialize` 里挂
`on_construct<PlayerComponent>` / `on_destroy<PlayerComponent>`，由它们触发
`kAdded` / `kRemoved`。

**为什么这两类事件就是全部迁移（不是猜的）**：查过 `FormIdComponent` 的全部写入点——
`emplace_or_replace` 只在 `CharacterService.cpp:250`（实体创建时一次），
移除只在 `:280`/`:887`（拆除时）。**一个实体的 proxy FormID 在它存活期间不会变**，
所以 `kUpdated` 在这个框架里不可达，`kAdded`/`kRemoved` 覆盖了全部迁移。

**回调在锁外调用**：与 `OnPluginMessage` 同一理由——插件若在自己的回调里回呼框架，
非递归锁会死锁。`Shutdown()` 里**先断开 observer 再清表**，否则单例会对着已销毁的
registry 被回调。

### 27.3 `setLocalDisplayName`：**有意不上线**，已注明

OStimTogether 会把自己的玩家名传进来。框架存了却不用——**这是对的**：
`NotifyPluginMessaging::SenderDisplayName` 由服务端从 `Player::GetUsername()` 填，
是**登录时认证过的名字**。若让插件改名，任何插件都能冒充别人。已把这条理由写进代码。

### 27.4 空指针清扫（判据：同文件别处判过空）

| 位置 | 问题 | 依据 |
|---|---|---|
| `PlayerService::RunRespawnUpdates` | `pPlayer->actorState.IsBleedingOut()` 不判空 | 同文件 `:309` 判过 |
| `PlayerService::RunBeastFormDetection` | `pPlayer->race` 前不判 `pPlayer` | 同上 |
| `PlayerService::RunDifficultyUpdates` | `PlayerCharacter::Get()->SetDifficulty` | 同上 |
| `PlayerService::RunLevelUpdates` | `static uint16_t oldLevel = PlayerCharacter::Get()->GetLevel();` 静态初始化只在首帧跑一次，载入期首帧没有玩家 | 同上 |
| `DiscoveryService::DetectGridCellChange` | `pCell` 经 `GetParentCellEx()` 与坐标回退**都可能为空**，下一行读 `pCell->formID` | 同文件 `:194` 对同一调用判过空 |
| `CalculateHealthPercentage` | 每帧被传 `PlayerCharacter::Get()`，函数内不判 | 调用点 `:609` 是每帧路径 |
| `WeatherService` ×3 | `Sky::Get()->`（`OnWeatherChange` 由服务端消息驱动） | 同文件 `:58`/`:148` 判过 |
| `OverlayService` ×2、`PartyService` ×2、`InputService` ×1 | `GetOverlayApp()->`：该指针在 `Create()` 之前为空，而组队事件正是连接后立刻会到 | `OverlayService.cpp:224` 判过 |
| `ObjectService` ×3 | `pObject->baseForm->formType` | `EntitiesView.cpp:69/110` 判过 `baseForm` |
| `InventoryService`（每帧裸体检查）、`MagicService`、`CharacterService`、`Actor.cpp` | `baseForm->GetName()` | 同上 |
| `EntitiesView` ×1 | **死守卫**：先写 `"UNNAMED"`，下一行无条件 `sprintf_s` 覆盖它 | 自己的代码 |

`baseForm` **确实可为空**——这不是推测：仓库自己的调试视图里就写着
`if (!pActor->baseForm)`（`EntitiesView.cpp:69`）与 `if (!pRefr || !pRefr->baseForm)`
（`:110`）。同一份代码里两处判、12 处不判，只能有一边是错的。

### 27.5 服务端限流桶：随玩家一起清理

`PluginMessagingService::m_buckets` 以 `PlayerId` 为键，而 `PlayerId` 来自
`GenerateId()` 的**单调递增原子计数**（`Player.cpp:4-10`）——**永不复用**。
`AllowMessage` 只插入不删除，于是服务器开得越久这个 map 越大。
已在 `GameServer::OnDisconnection` 的玩家行移除处一起清掉。

### 27.6 快捷键：只保留 F2

按需求把除 F2 外**所有游戏内快捷键**注释或禁用（逐条见 CHANGELOG 表格）。
其中两条值得记：

- **右 Ctrl 是 F2 的别名**（`IsToggleKey` 里的 `VK_RCONTROL`），已移除；
  `docs/LAN-RADMIN-GUIDE.md:67` 同步改掉（文档里写着"F2 或 右Ctrl"）；
- **F3 的注释里原先写着"F2 和 F3 是仅有的两个在线按键"**——本轮 F3 也关掉后
  这句话变成错的，已一并改掉。

F3 关掉后 `m_showDebugStuff` 只剩 `toggleDebugUI` 这一个写者（CEF 绑定），
**调试菜单从"按键开"变成"纯 opt-in"**，代码没有被变成不可达——这一点写进了注释。

### 27.7 复核用的命令（照抄）

```powershell
# 1. 服务端重复认证（代码路径：四道检查之后才是 Create）
Select-String -Path Code\server\GameServer.cpp -Pattern 'PlayerManager\(\)\.Create' -Context 0,6

# 2. 插件层两个回调是否真的被触发（应只剩 Initialize 里的注册 + FireProxyMapping 的调用）
git grep -n 'm_mappingListeners' -- Code
git grep -n 'm_logCallback' -- Code

# 3. F2 唯一性：游戏内按键只应剩 F2（其余都在 #if 0 / if(false) 里）
git grep -n 'GetAsyncKeyState' -- Code
git grep -n 'IsToggleKey' -- Code

# 4. 本机跑插件闸门（plugins.yml 的全部内容，5/5）
python Code/plugins/tools/strpm_contract.py check
python Code/plugins/tools/plugin_snapshot.py verify
python Code/plugins/tools/check_exports.py
python Code/plugins/tools/check_transport_compat.py
python Code/plugins/tools/merge_fomod.py check
```


---

## 28. 2026-09-26 第三轮：v1.1.2 首发失败的两处修复

打了 `v1.1.2` 之后 CI 红了两次，**两次都修在这里**。记下来是因为两条都属于
"本地怎么测都测不出来、只有 tag 跑才暴露"的类型。

### 28.1 编译错：`entt::scoped_connection` 没有 `disconnect()`

```
Code\client\Services\PluginMessagingService.cpp(87): error C2039: 'disconnect': is not a member of 'entt::scoped_connection'
Code\client\Services\PluginMessagingService.cpp(88): error C2039: 'disconnect': is not a member of 'entt::scoped_connection'
```

§27.2 给 observer 加的清理调用写成了 `disconnect()`。**entt 3.10 的
`scoped_connection` 只有 `release()`**（`signal/sigh.hpp:334`：
`void release() { conn.release(); }`），`disconnect` 在 `sink` 上而不是在连接对象上。

**为什么本机没抓到**：项目规定不在本地编译（§1），而这一处的 API 名字我**没有
对照真实头文件**就写了。全仓唯一一次同类调用就是我自己写的这两行——
`git grep -n '\.disconnect()' -- Code` 在客户端只有它们，所以"跟现有写法保持一致"
这条经验在这里失效了。

**修法**：`release()`。

> **教训**：`release()` vs `disconnect()` 这种"两个近义名字"的 API，
> **必须去读第三方头文件**再写，不能靠印象。本轮为此把 entt 3.10 的
> `sigh.hpp` 拉下来逐行核对过，顺手确认了另外三件事（见 §28.3）。

### 28.2 打包阻断：`package` job 少检出了 submodule

编译修好后，tag 跑到 **`Assemble the mod package`** 又失败：

```
PLUGIN STAGING FAILED (4)
  - submodule not checked out: plugins/STRPluginMessagingAPI
  - submodule not checked out: plugins/OStimTogether
  - submodule not checked out: plugins/MorphSyncTogether
  - submodule not checked out: plugins/IEDSyncTogether
```

**根因**：`release.yml` 的 `package` job 用的是 **sparse-checkout 且不开 submodule**，
注释还写着理由——"插件已经在上一个 job 编好了，artifact 里都有"。**这句话只对了一半**：

| 打包输入 | 来自哪 |
|---|---|
| `*.dll` 等**编译产物** | `plugin-artifacts`（上一个 job 传的）✅ |
| 每个插件的 **payload**（OStim/Morph 的 `package/Data` 树、IED 的 ini） | **submodule 里的仓库内容** ❌ 没检出 |

`Add-PluginPayload.ps1` 对每个插件先查 `plugins/<id>` 在不在，不在就记一条
`submodule not checked out`，四条全中 → 打包中断。

**验证**（不是推断）：按脚本自己的解析规则把 manifest 里 **17 条 payload 源**
逐条 `Test-Path` 一遍——**17/17 全部存在**，也就是说这些文件本来就在仓库里，
只是那个 job 没把它们检出来。

**修法**：那个 job 改成**完整检出 + `submodules: true`**，并把注释里那句错的理由改掉。

**试过但否决的另一条路**：保留 sparse 模式、把 `plugins` 加进 pattern 列表。否决理由
是实测出来的：**非 cone 模式的 sparse-checkout 会把 `.gitmodules` 一起排除掉**
（本机用临时仓库复现过），文件没了以后 `submodules: true` 会**静默地什么都不检出**。
这个 job 每个 tag 只跑一次，多花几分钟换掉这个失败模式是划算的。

### 28.3 顺手核对的三件事（都对着 entt 3.10 源码）

| 用法 | 结论 |
|---|---|
| `on_construct<T>()` / `on_destroy<T>()` | 存在，`registry` 上的模板成员 |
| `connect<&Class::Method>(this)` | 存在，两个重载之一（`connect<Candidate>(value_or_instance)`） |
| observer 回调签名 `(entt::registry&, entt::entity)` | 与仓内既有先例一致（`ActorValueService`/`OverlayService`/`WeatherService`） |
| `emplace_or_replace<T>` 的语义 | **组件已存在时走 `patch`（只触发 `on_update`），不存在时才 `emplace`（触发 `on_construct`）** |

最后一条对本轮的功能有实际影响，单独记在下面。

### 28.4 `kAdded` 事件的可达性（对着上一条核过）

§27.2 用 `on_construct<PlayerComponent>` 触发 `kAdded`。因为
`emplace_or_replace` 只在组件**原本不存在**时才触发 `on_construct`，
所以 `kAdded` 恰好对应"这个 peer 第一次拿到 proxy"，语义是对的。

三个调用点（`CharacterService.cpp:422`、`:605`、`:1626`）都在
`FormIdComponent` 已经存在之后才 `emplace_or_replace<PlayerComponent>`，
所以 handler 里 `try_get<FormIdComponent>` 一定拿得到——这也是 handler 里
那两层判空仍然保留的原因（另一个线程/时序下不保证，判空是免费的）。

### 28.5 本轮踩到的一个自己的坑（记下来免得再犯）

为了验证 sparse-checkout 的 pattern 语义，我在**本仓库目录里**直接跑了
`git sparse-checkout set ...`。它会写 `core.sparseCheckout=true` 并给
**1672 个文件**打上 skip-worktree 位、把它们从工作区删掉——包括 `docs/`。
`git status` 当时只显示一处修改，**看起来什么都没发生**，很容易就这么提交了。

已用 `git sparse-checkout disable` 复原（skip-worktree 归零、`docs/` 回来），
工作区现在只有预期的 `release.yml` 一处改动。

> **教训**：验证 sparse-checkout 语义要在 `git clone` 出来的**临时仓库**里做，
> 不能在工作仓库里跑。这个命令改的是 `$GIT_DIR/info/sparse-checkout` 与索引标志，
> 不是"只影响一次命令"。

### 28.6 复核用的命令（照抄）

```powershell
# 1. 编译错本身（应只剩 release()，没有 disconnect()）
git grep -n '\.disconnect()' -- Code
git grep -n '\.release()'     -- Code/client/Services/PluginMessagingService.cpp

# 2. package job 到底检出了什么
Select-String -Path .github/workflows/release.yml -Pattern 'submodules|sparse-checkout' -Context 0,3

# 3. manifest 里每条 payload 源是否真的存在（应输出 missing: 0）
#    核心是按 Add-PluginPayload.ps1 的规则解析 ../ 前缀后 Test-Path

# 4. 工作区有没有被 sparse-checkout 动过（应输出 0）
(git ls-files -t | Where-Object { $_ -match '^S ' } | Measure-Object).Count

# 5. 读 CI 失败原因（本机 curl 不通，python 通）
#    api.github.com/repos/<o>/<r>/actions/runs -> jobs -> jobs/<id>/logs
#    （logs 会 302 到 blob，重定向后不要再带 Authorization）
```

---

## 29. 2026-09-26 场次：把 `D:\sktest` 剩下三个插件并入（4 → 7 个插件仓库）

`D:\sktest` 下有 7 个插件仓库，本项目原先只整合了 4 个。本轮把剩下的三个按
**同一条既有配方**并入：`AnimSyncTogether`、`DAVSyncTogether`、`TradeTogether`。

### 29.1 整合方式（照 §24/§27 的既有配方，不是新发明）

| 环节 | 落点 |
| --- | --- |
| 子模块 | `.gitmodules` + 索引 gitlink（`160000`） |
| 消费者契约 | `strpm_contract.py` 的 `CONSUMERS` + `contract.json`（`dump` 重生成） |
| 打包清单 | `Code/plugins/plugins.json` 的 `plugins[]`（payload/artifacts/introduction） |
| CI 构建 | `windows.yml` 的 `$specs` 列表 |
| 耐久快照 | `snapshots/plugins/<id>/`（`plugin_snapshot.py snapshot`） |
| 安装向导 | `merge_fomod.py generate` 重生成 `ModuleConfig.xml` |
| 文档计数 | README.md / README_EN.md / PLUGIN-SOURCES.md / STRPM/README.md 的"四个/三个"改"六个/七个" |

新增的 `flag`：`anim` / `dav` / `trade`（与既有 `strpm`/`ostim`/`morph`/`ied` 无冲突，已校验唯一）。

### 29.2 三个插件各自的 payload 判断（都不是"想当然"）

- **AnimSyncTogether**：`config/Rules/*.rules` 必须是 **payload**，不能算 artifact。
  CMake 确实会把 `config/Rules` 拷进自己的 `build/package`，但 CI 的 artifact 收集
  只抓 `*.dll,*.pex,*.esp`（`windows.yml`），**走 build/ 永远送不到用户手里**。
  而规则文件不是装饰：它决定同步哪些图谱变量与动画事件，**没有它这个插件什么也不同步**。
- **DAVSyncTogether**：**无 payload**，只有一个 DLL。它的变体规则来自 DAV 本体
  （`DAVConfigIndex.cpp` 读 `Data/SKSE/Plugins/DynamicArmorVariants/*.json`），本包不带。
  注意它的 vendored 头与权威头 **SHA-256 完全一致**（`8fdcf481...`），是唯一逐字节相同的一个。
- **TradeTogether**：`package/Data` 是**已跟踪**内容（ini + 2 个 `.pex` + 1 个 `.esp`），
  与 OStimTogether 同一形态，直接 `source: package/Data, dest: "."`。
  它的 `UdpTransport.cpp` 是**遗留字段**不是活传输：`TradeTogether.ini` 明写
  "uses STR Plugin Messaging exclusively"，`Config.h` 也注明 UDP 仅为兼容旧配置文件保留。

### 29.3 本机环境的一个真实限制（`git submodule add` 用不了）

`git submodule add` 在本机**必然失败**，与网络无关：

```text
      0 [main] sh (32020) D:\Git\usr\bin\sh.exe: *** fatal error -
      CreateFileMapping ... Win32 error 5.  Terminating.
```

msys 的 `sh.exe` fork 被沙箱挡掉（`CreateFileMapping` 拒绝），而 `git submodule add`
内部要起 shell。**`git clone` 不受影响**，所以本轮用等价的手工路径完成：

```powershell
# 1. 普通 clone
git clone <url> plugins/<id>
# 2. 把 .git 挪进子模块布局（与其他四个一致）
Move-Item plugins/<id>/.git .git/modules/plugins/<id>
git config --file .git/modules/plugins/<id>/config core.worktree "../../../../plugins/<id>"
Set-Content plugins/<id>/.git -Value "gitdir: ../../.git/modules/plugins/<id>" -NoNewline -Encoding ascii
# 3. 登记 .gitmodules 与索引
git config --file .gitmodules submodule.plugins/<id>.path plugins/<id>
git config --file .gitmodules submodule.plugins/<id>.url  <url>
git add .gitmodules plugins/<id>
```

> **判据**：`git ls-files -s plugins/` 全部是 `160000`，且 `git -C plugins/<id> rev-parse HEAD`
> 能解析——两点都成立才算真子模块，否则只是个"嵌进去的仓库"（git 会警告 embedded git repository）。

### 29.4 验证（不是"闸门绿了"就算数）

1. **契约闸门真的会拦**：往 `TradeTogether` 的 vendored 头注入 `kMaxChannelLength 96→128`，
   `strpm_contract.py check` 立刻 `C4 [TradeTogether] constant value mismatch` 并 exit 1；
   还原后回到 `CONTRACT OK`，且 `git -C plugins/TradeTogether status` 干净（逐字节还原）。
2. **打包演练跑通全流程**：造出 CI 形态的 `<artifactsRoot>/<pluginId>/*.dll`，
   `Add-PluginPayload.ps1` 输出 `PLUGIN STAGING OK / payload 19 / artifacts 8 / plugins 7`；
   三个新插件的落地树分别是
   `SKSE/Plugins/AnimSyncTogether.dll` + `AnimSyncTogether/Rules/HelmetToggle2.rules`、
   `SKSE/Plugins/DAVSyncTogether.dll`、
   `TradeTogetherMCM.esp` + `Scripts/*.pex` + `SKSE/Plugins/TradeTogether.ini` + DLL。
3. **二进制没被 git 动过**：快照里的 `.pex`/`.esp` 与子模块内**逐字节相同**，
   `.pex` magic 仍是 `FA 57 C0 DE`（`text: auto` 不碰二进制）。
4. **`merge_fomod.py check --stage` 只剩 3 条**（`SkyrimTogetherRuntime` / `VerifyScript` / `launcher`），
   这三个是**框架构建产物**、本机没有 `build/`，与插件无关——CI 里由 `release.yml` 先构建再打包。

### 29.5 本轮**自己引入**的一个发布阻断：pinned `builtin-baseline` 在 CI 里取不到

**现象**（本机用真 `vcpkg.exe` 复现，不是推测）：

```text
error: while checking out baseline from commit
       'ef3a5a82e39424b7f5740c4576f027c3173767f4',
       failed to `git show` versions/baseline.json.
       This may be fixed by fetching commits with `git fetch`.
```

**根因**：vcpkg 解析 manifest 的 `builtin-baseline` 走的是
`BuiltinGitRegistry` → `git_checkout_baseline()` → 只跑 `git show <sha>:versions/baseline.json`，
**没有任何 fetch 回退**（`registries.cpp:455-511`）。而 CI 的 vcpkg 是
`git clone --depth 1`（`windows.yml` 的 setup 步骤），**只有 1 个 commit**，
所以只要 baseline 不是那个 tip，对象就不在本地。

> 注意与 **git registry** 的区别：`GitRegistry` 有回退，
> `git_show` 失败后会 `git_fetch` 再重试（`registries.cpp:791-804`）。
> 所以 colorglass 那两个 baseline（`9eae9f03…` / `bbd09a56…`）**不需要管**，
> vcpkg 自己会取——本机实测两个 SHA 都能 `git fetch` 成功。

**为什么以前没炸**：原先四个插件（OStim/Morph/IED/STRPM）**只用 colorglass git registry**，
那条路有回退。`AnimSyncTogether` 与 `TradeTogether` 是**第一批 pin `builtin-baseline` 的插件**
（`AnimSyncTogether` 的 `vcpkg-configuration.json` default-registry + `TradeTogether` 的 `vcpkg.json`），
所以这个雷是**本轮整合引入的**，push 后第一次 CI 必然红。

**修法**（已落地，`windows.yml` 的 setup 步骤）：在 bootstrap 之前，
**从各插件 manifest 反推** baseline 并逐个 fetch，而不是写死 SHA——
插件以后改 baseline 不会让这段失效：

```powershell
if (Test-Path (Join-Path $vcpkg '.git')) {
  $baselines = @()
  foreach ($manifest in Get-ChildItem 'plugins/*/vcpkg.json' -ErrorAction SilentlyContinue) {
    $pinned = (Get-Content -LiteralPath $manifest.FullName -Raw | ConvertFrom-Json).PSObject.Properties['builtin-baseline']
    if ($pinned -and $pinned.Value) { $baselines += $pinned.Value }
  }
  foreach ($sha in ($baselines | Sort-Object -Unique)) {
    git -C $vcpkg fetch --quiet --depth 1 origin $sha
    if ($LASTEXITCODE -ne 0) { throw "failed to fetch the vcpkg baseline pinned by a plugin manifest: $sha" }
  }
}
```

**验证**：对一个真 `--depth 1` 克隆跑上面这段 → `git show` 从 exit 128 变为 exit 0。

### 29.6 复核用的命令（照抄）

```powershell
# 1. 七个子模块都解析得动
git ls-files -s plugins/            # 期望 7 行，全部 160000

# 2. 五个闸门
python Code/plugins/tools/merge_fomod.py check
python Code/plugins/tools/strpm_contract.py check
python Code/plugins/tools/plugin_snapshot.py verify
python Code/plugins/tools/check_exports.py
python Code/plugins/tools/check_transport_compat.py

# 3. 打包演练（造桩 artifact 后应输出 plugins 7）
#    注意本机是 Windows PowerShell 5.1，没有 pwsh，直接 & 调脚本

# 4. 清单里每条 payload 源是否真的存在（应输出 missing: 0）
```

---

## 30. 2026-09-26 场次：用开源编译器在 CI 里补上 OStim 的两个 Papyrus 脚本

§29 结案时留的最后一个洞——`OSKSE.pex` / `OStimTogetherNative.pex` 编不出来——
本轮解决。结论：**完全可以在 GitHub CI 里编，不需要 Skyrim、不需要 CK、不分发 Bethesda 任何东西。**

### 30.1 方案：russo-2025/papyrus-compiler（不是 Caprica 那条死路）

| | Caprica（§29 评估过，否决） | russo-2025/papyrus-compiler（采用） |
| --- | --- | --- |
| 语言 / 年代 | C++ / FO4 时代，MSVC2015-only | V / 2025 起活跃维护 |
| 目标 | Fallout 4 反编译产物 | **Skyrim SE/AE**，README 明写 |
| 发布 | 无 release，需自己编 | **有正式 Windows release** |
| 本机可跑 | 否（无 MSVC） | **是**，单文件 `papyrus.exe` |

pin 在 `Code/plugins/papyrus/Compile-OStimConsentScripts.ps1` 顶部：
`2026.03.15`（V 0.0.4），SHA-256 `67b44c77d00a5cda986bec6af5c228a56abe6ec1fadcb4cfea5c858e6941140e`。
脚本会**校验归档哈希**，所以 pin 是有意义的而不是装饰。

### 30.2 真正的难点不是编译器，是 header 解析

编译器 `-h` **每个目录一次、不递归**，所以每个目录只能补前面没声明的东西。最终顺序：

```text
Data/Scripts/Source          被编译的两个脚本
Dependencies/Source          插件自带的 OStim/UIExtensions stub（故意遮蔽 OStim 原脚本）
Code/plugins/papyrus/stubs   基础类型：Actor/Form/ModEvent/NiOverride...（本仓库自有）
```

**为什么最后一层必须放在本仓库而不是子模块**：`plugins/OStimTogether` 是 gitlink
（`git ls-files -s` 只有一行 `160000`），往里加文件**父仓库看不见也提交不了**。
而 `Dependencies/Source` 只有 7 个 OStim stub，**一个基础类型都没有** ——
`Actor`、`Form`、`ModEvent`、`Utility`、`Game`、`GlobalVariable`、`Topic`、`VoiceType`、
`ObjectReference`、`Quest`、`Debug`、`NiOverride` 全缺。

> `NiOverride` 是 §29 就点名的那个缺口。它的 6 个签名**逐字抄自 RaceMenu 的 NiOverride**，
> 不是猜的：`HasNodeTransformScale` / `GetNodeTransformScale` /
> `RemoveNodeTransformPosition` / `AddNodeTransformPosition` / `UpdateNodeTransform` /
> `ApplyNodeOverrides`。签名写错不会编译失败，只会在运行时调错原生函数——所以必须抄准。

### 30.3 接线方式（复用既有通路，没有新机制）

1. `Code/plugins/papyrus/Compile-OStimConsentScripts.ps1` 编译到
   `plugins/OStimTogether/compat/OStimUIConsent/package/Data/Scripts/`（**默认值**，
   正好是插件自己 `build-vortex.ps1:101` / `build-fomod.ps1:59` 期望的路径）；
2. CI 里用 `-OutputDir plugins/OStimTogether/build/papyrus` 覆盖，写进**插件构建树**；
3. `windows.yml` 的 artifact 收集本来就扫 `plugins/$id/build` 的 `*.pex`，**自动带走**；
4. `plugins.json` 里把两个 `.pex` 声明成 **artifacts**（不是 payload）→ 落 `scripts/`。

步骤位置**故意排在插件构建之后**：它和 CMake 共用 `plugins/OStimTogether/build`，
放最后就不会被任何后续清理动作抹掉。

### 30.4 验证（对着真实产物，不是"应该能行"）

1. **真的编出来了**：`OSKSE.pex` 3894 B / `OStimTogetherNative.pex` 719 B，exit 0。
2. **是合法字节码**：magic `FA 57 C0 DE`、major 3 / minor 2、`game_id: skyrim` ——
   与仓库既有 `SkyrimTogetherUtils.pex` 头部一致。
3. **反汇编证明逻辑在**：`OSKSE.pex` 里能看到
   `callstatic OStimTogetherNative.BeginAddActorConsent`、
   `callstatic OStimTogetherNative.PollAddActorConsent`、
   字符串 `Waiting for consent` / `Scene request declined`；
   `OStimTogetherNative.pex` 里两个函数带 `flags(0x03): [Global, Native]` ——
   这正是 DLL 侧 `RegisterFunction("BeginAddActorConsent", "OStimTogetherNative")` 要对上的东西。
4. **走通打包全流程**：跑真实 `Add-PluginPayload.ps1`，输出
   `PLUGIN STAGING OK / payload 19 / artifacts 10 / plugins 7`，
   staged 树里 `OptionalPlugins/OStimTogether/scripts/` 下两个 `.pex` 就位、magic 正确。
5. **没有名字冲突**：本仓库 12 个 stub 与 `Dependencies/Source` 的 7 个、与两个编译目标，
   大小写不敏感地全无重名（Windows 文件系统大小写不敏感，这条必须查）。

### 30.5 两个已知的、不影响正确性的细节

- **产物里带编译机信息**：debug info 含 `user_name` / `machine_name` / `compilation_time`。
  上游 README 写了 `-no-debug-info`，但**这个 release 的二进制不认这个参数**（传了直接
  `Missing or incorrect argument`）。所以 CI 产物会带 runner 的用户名。
  功能无影响；如果将来要可复现构建，得等上游把该 flag 发出来。
- **`-original` 参数**能改用 Bethesda 原始编译器（zip 里附了 `Original Compiler/`），
  但那条路要分发 Bethesda 的 `PapyrusCompiler.exe` + `TESV_Papyrus_Flags.flg`，
  正是本轮要避免的事。**不要用。**

### 30.6 复核用的命令（照抄）

```powershell
# 1. 本机编译（无需网络，用已解包的编译器）
Code/plugins/papyrus/Compile-OStimConsentScripts.ps1 -CompilerPath <path-to>/papyrus.exe -OutputDir .px/out

# 2. 产物是不是合法 PEX（期望 FA 57 C0 DE）
#    读头部 4 字节，或用编译器自身 read

# 3. 同意门控逻辑是否真的进了字节码
#    papyrus.exe read <pex> | Select-String "BeginAddActorConsent|PollAddActorConsent"

# 4. 打包全流程（应输出 artifacts 10 / plugins 7）
Tools/Packaging/Add-PluginPayload.ps1 -Stage <stage> -ArtifactsRoot <artifacts>
```

---

## 31. 2026-09-26 场次：OCum 的 esp 出货却不带脚本（v1.1.3）

### 31.1 现象：一个"引用了不存在脚本"的 esp

`OStimTogether_OCum.esp`（250 字节，TES4 头）随 OCum 子选项出货，它的 VMAD 明确写着：

```text
QUST → EDID = OSTogetherOCumIntegrationQuest
     → VMAD → OStimTogetherOCum      ← 它引用的脚本
```

**但 `OStimTogetherOCum.pex` 全仓库不存在**，只有 `.psc` 源码。后果：

- Papyrus 找不到脚本 → `OnInit()` 永不执行 → `RegisterIntegration()` 永不执行；
- DLL 侧 `main.cpp:89-95` 用 `DispatchMethodCall2(handle, "OStimTogetherOCum",
  "RegisterIntegration", ...)` **按名字派发** → 必然失败，只有一行日志。

### 31.2 根因：插件自己的检查被流水线绕过了

插件仓库里**有两道**保护，**都没生效**：

| 保护 | 位置 | 为什么没生效 |
| --- | --- | --- |
| 编译脚本 | `optional/OCumIntegration/compile-ocum-integration.ps1` | 要 `PapyrusCompiler.exe`，CI 没有 |
| 缺文件即 throw | `build-fomod.ps1:42-43` | **本仓库不跑它**，只 `stages package/Data` |

所以缺口**静默出货**：`Add-PluginPayload.ps1` 复制 payload 目录时，
目录里有什么就发什么，**少了文件不报错**。

> **教训**：payload 是"复制目录里现成的东西"，artifact 是"从构建产物里找指定文件"。
> 一个**必须存在**的文件应该声明成 artifact —— 缺了会 `missing staged artifact` 直接失败；
> 声明成 payload 则只会静默少发。这正是 OCum 漏掉的原因。

### 31.3 第二个缺陷：打包脚本**忽略子选项的 artifacts**

`Add-PluginPayload.ps1` 原来只在**插件级**处理 `artifacts`；子选项循环里**只读 `payload`**。
所以就算在 `subOptions[].artifacts` 里声明了 `.pex`，也会被**静默忽略** ——
声明了却什么都不发生，比不声明更危险。已修：子选项现在同样支持 `artifacts`，
并做与插件级一致的 `missing staged artifact` 校验。

### 31.4 第三个缺陷：4 个 `Form` + 1 个 `Game` stub 缺失

`OStimTogetherOCum.psc` 用了 `UnregisterForModEvent` / `UnregisterForUpdate` /
`IsPluginInstalled`，而 `Code/plugins/papyrus/stubs/` 里没有。补的签名
**逐字取自游戏自己的 `Data/Scripts/Source/`**（本机 `D:\game\SkyrimSE\` 就有）：

```papyrus
Function RegisterForModEvent(string eventName, string callbackName) native
Function UnregisterForModEvent(string eventName) native
Function RegisterForUpdate(float afInterval) native
Function UnregisterForUpdate() native
bool Function IsPluginInstalled(string name) native global
```

> 注意 `IsPluginInstalled` **不在** Grimy 的 `Game.psc` 里（它是 SKSE 扩展的），
> 只有游戏本体的源码有。抄签名要去**游戏本体**的 Source，不是第三方整合包。

### 31.5 接线（复用 §30 的同一条通道）

`Code/plugins/papyrus/Compile-OCumIntegrationScript.ps1`，形状与 `Compile-OStimConsentScripts.ps1`
一致（同样 dot-source `Resolve-PapyrusCompiler.ps1`，同一 pin）：

```text
Data/Scripts/Source          被编译的脚本
Dependencies/Source          插件自带的 OActor stub
Code/plugins/papyrus/stubs   基础类型（Quest/Form/Game/Debug...）
```

CI 里紧跟同意门控那一步，输出到同一个 `plugins/OStimTogether/build/papyrus`；
清单里声明为 **OCumAscended 子选项的 artifact** → `scripts/OStimTogetherOCum.pex`。

### 31.6 验证（对着真实产物）

1. **编出来了**：`OStimTogetherOCum.pex` 1018 B，exit 0，magic `FA 57 C0 DE`。
2. **反汇编证明逻辑在**：`callmethod UnregisterForModEvent`（3 个事件名）、
   `callmethod UnregisterForUpdate`、`callstatic Game.IsPluginInstalled "OCum.esp"`、
   以及两行 `Debug.Trace` 字符串都在。
3. **走通打包**：`PLUGIN STAGING OK / payload 19 / artifacts 11 / plugins 7`，
   子选项落地树为 `OStimTogether_OCum.esp` + `scripts\OStimTogetherOCum.pex`，magic 正确。
4. **新校验真的会拦**：删掉 staged 的 `.pex` 后 `-VerifyOnly` 报
   `missing staged artifact: (OptionalPlugins)/OStimTogether__OCumAscended/scripts/OStimTogetherOCum.pex`
   并 exit 1。

### 31.7 复核用的命令（照抄）

```powershell
# 1. 编译（用已解包的编译器）
Code/plugins/papyrus/Compile-OCumIntegrationScript.ps1 -CompilerPath <path>/papyrus.exe -OutputDir .pt/out

# 2. 逻辑是否进字节码
#    papyrus.exe read <pex> | Select-String "UnregisterForModEvent|IsPluginInstalled"

# 3. 子选项 artifact 校验（删掉文件后应 exit 1）
Tools/Packaging/Add-PluginPayload.ps1 -Stage <stage> -ArtifactsRoot <artifacts> -VerifyOnly
```

## 32. 2026-09-26 场次：服务端一条聊天命令即可打崩，以及"装上就是死的"插件数据同步

> **锚点**：本节两条修复 + 门禁 + 文档都在**同一个提交**里，标题为
> `fix: guard an admin-session null deref, and stop the transport docs from lying`，
> 其父提交是 `3e9f0c6c`。查证用 `git log 3e9f0c6c..HEAD`。
> （不写自身 hash：本文件也在那个提交里，写进去就自我指涉、每次 amend 都失效。）
> 本轮继续全仓审计，重点是**插件接口与插件层**。两条都是**真缺陷**，判据仍是 §27 的那条
> （同一指针在同一文件里被判空过，就不该在别处被直接解引用），外加一条新的：
> **"文档声称的默认值"必须与"实际出货的默认值"逐字一致。**

### 32.1 现象：任意玩家一条 `/settime` 就能打崩专用服务器

`CommandService::OnSetTimeCommand` 对**每一个**在线玩家取管理员身份：

```cpp
const auto* pAdmin = PlayerManager::Get()->GetByConnectionId(session);   // 可能为 nullptr
if (pAdmin->GetId() == cPlayerId) { ... }                                 // 直接解引用
```

这是全仓库**唯一**一处对 `GetByConnectionId` 的返回值不判空就解引用的地方。

### 32.2 根因：认证流程存在"会话已建立、玩家行还没有"的合法中间态

`GameServer::HandleAuthenticationRequest` 的顺序是：

| 行 | 动作 |
| --- | --- |
| `GameServer.cpp:896` | `m_adminSessions.insert(session)` —— **会话先进表** |
| `GameServer.cpp:947` | `kModsMismatch` 等检查失败 → **提前 return** |
| `GameServer.cpp:977` | `PlayerManager::Create(...)` —— **玩家行才建立** |
| `GameServer.cpp:999` | `HandlePlayerJoin` 取消 → **提前 return** |

关键在于：**整个认证流程里没有任何一处**在失败路径上回头清理这个集合。
`m_adminSessions` 只在 `GameServer::OnDisconnection`（`:612`）里被擦除，
那是**另一条**路径、**另一个**时刻。所以 `:947`（行根本没建）与 `:983`（重复认证）
之后，集合里就留下了一个**查不到玩家行**的连接 id。

> **本条的证据边界（不猜）**：`Kick` 是否**同步**触发 `OnDisconnection`，取决于
> `TiltedCore` 的 `Server` 基类，而 `Libraries/TiltedCore` 在本机**未检出**
> （`git ls-tree` 里它甚至不是子模块，是 `add_requires("tiltedcore 0.2.9")` 从
> xmake 源拉取的），因此**无法本地核实**。若 `Kick` 同步，则 `:998` 的
> `Kick` 会顺带擦除集合、`:999` 再删行，窗口关闭；若 `Kick` 是排队/异步的，
> 窗口就真实存在。
>
> **所以判据不建立在 `Kick` 的语义上**，而建立在**代码库自己的结论**上：
> 同一个集合的**其余所有**读者都判了空，而且 `GameServer.cpp:496-502` 明确打印
> `"Admin session not found: {}"` 并 `continue` —— 这就是代码库自己承认
> "会话可能没有对应玩家行"。一处漏判空，就足以是缺陷，与窗口能否被构造无关。

**为什么以前没炸**：`m_adminSessions` 的**其余所有读者**都判了空 ——
`GameServer.cpp:496-502`（还专门为这一情形打了 `"Admin session not found"`）、
`:1096`、`:1110`，以及 `PartyService_Bindings.cpp:12-16`。只有这一处漏了。

> 附带记录（**未改动**，因为够不到）：`Player_Bindings.cpp:37` 把
> `GetByConnectionId(aSelf.GetConnectionId())` 的返回值**直接**交给
> `PartyService::IsPlayerLeader`，而后者（`PartyService.cpp:50-52`）**不判空**就
> `apPlayer->GetParty()`。这里是**自查找**（参数取自 `aSelf` 自己的连接 id），
> 正常情况下必然命中自己，除非 Lua 脚本持有一个断线后的**悬垂** `Player` 引用。
> 无法证明可达，故只记录、不修改。

### 32.3 修复

改为取到即判、失败即 `continue`，并加注释写明这个中间态为什么存在
（`Code/server/Services/CommandService.cpp`）。

### 32.4 现象：七个随包插件的数据同步"装上就是死的"

这是本轮最重的一处：**框架侧的一切都是对的，出货的 ini 把七个插件全都导到了错误的传输上。**

链路（每一环都已用源码核实）：

1. 七个插件**无一例外**都按名字找门面 DLL，**没有任何一个插件提到过框架运行时**：
   `AnimSyncTogether`/`DAVSyncTogether`/`TradeTogether` 用 vendored 头里的
   `LoadFromModule()` 默认值；`IEDSyncTogether`(`STRPMAdapter.cpp:40-48`)、
   `MorphSyncTogether`(`UdpTransport.cpp:73`)、`OStimTogether`(`STRPMTransport.cpp:11`)
   直接写死 `L"STRPluginMessagingAPI.dll"`。
2. 于是走的永远是**门面**，而门面的 `Broker::Start()` 在 `backendMode != kUdp` 时
   **先试桥接**：`TryStartStrBridge()`（`STRPluginMessagingAPIRuntime.cpp:512-517`）。
3. 出货的 `Code/plugins/packaging/STRPluginMessagingAPI.ini` 里
   `Mode=Auto` + `STRBridgeModule=STRPluginMessagingBridge.dll`。
4. `LoadStrBridgeModule` **先 `GetModuleHandleW`**，而桥接是 SKSE 插件、早已加载
   ⇒ 必然命中；`STRPM_QueryTransportInterface` 也由桥接导出。
5. 桥接的 `Start()` 只要把接收分发器和 bootstrap 线程起来就返回 `kOk`
   （解析是**懒加载**的），于是 `_activeBackend = kStrBridge`、`_running = true`。
6. `STRPM_ENABLE_UDP_BACKEND=0`，所以 `#if !STRPM_ENABLE_UDP_BACKEND` 分支直接
   `return false`，**UDP 回退在编译期就不存在**。

**致命的一环在桥接的接收半**：`Send` 被**两个**条件同时门控
（`STRPluginMessagingBridge.cpp:1128`）：

```cpp
if (!g_transportInstance.load() || !g_receiveResolverReady.load())
    return STRPM::Result::kNotConnected;
```

而 `g_receiveResolverReady` 只有一个写入点（`:993`），要求
`STRPMBridgeReceive::Start()` 成功；它先调 `ResolveOnConsumeAddress()`，那里是
`EnumerateRuntimeMemory(GetModuleHandleW(nullptr))`（`STRPluginMessagingBridgeReceive.cpp:433`），
并过滤 `mbi.AllocationBase == module`——**只扫 SkyrimSE.exe 自己那块分配**。
这一条过滤就足以让它在**本框架内永远不可能就绪**：

- 本框架的 `TransportService` 住在 `SkyrimTogetherRuntime.dll` 这个**独立模块**里，
  它的 `AllocationBase` 不等于 SkyrimSE.exe ⇒ **根本不在扫描范围内**；
- 而且这是**唯一的**一条理由：桥接找的 RTTI 名 `.?AUTransportService@@` 恰好**就是**
  全局命名空间里 `struct TransportService` 的 MSVC 修饰名（`?AU` + 名字 + `@@`），
  本框架的类正是全局作用域 `struct`（`Code/client/Services/TransportService.h:26`，
  头文件与实现都没有 `namespace`）⇒ **名字能对上，是模块范围把它排除掉了**。
  换句话说：桥接的锚点设计对本框架是"认得出、但够不着"。

（发送半的锚点在本框架里**是存在的**——`OverlayClient.cpp:153` 确实有那个**宽**字面量
`L"Send chat message of type {}: '{}' "`，`TransportService::Send` 里也确有
`Buffer buffer(1 << 16)`；桥接为 MSVC 生成的那份会把锚点换成宽字面量，所以
**发送半可能真的解析成功**。但这不改变结论：`Send` 要求两半**同时**就绪，
接收半永远不就绪 ⇒ 永远 `kNotConnected`。）

**结论**：出货配置下，七个插件每一次 `send()` 都返回 `kNotConnected`，
**同步静默失效**，且不报错、不崩溃，日志里只有桥接自己那句
"receive resolver waiting for NotifyChatMessageBroadcast runtime RTTI"。

### 32.5 已修：文档与门禁（这两项是纯事实修正，可验证）

- `docs/COMPANION-PLUGINS.md` 原文写着"**packaged ini names the framework runtime,
  which is why a plugin that loads the facade by name still ends up on the native
  transport**"——**与出货的 ini 完全相反**。这句话是 `976f9702` 改 ini 时漏改的，
  而它恰恰是读者据以判断"我的插件走哪条传输"的那一句。已改写为事实，并写明
  **七个插件都落在桥接上**；
- `Code/plugins/tools/check_transport_compat.py` 增加两个断言，把**散文与出货值绑死**：
  ① 文档必须出现 ini 里 `STRBridgeModule` 的**实际取值**；② 文档不得再声称
  "names the framework runtime"。已做**反向验证**：分别注入这两种回归，门禁都
  `exit 1` 并指名道姓；恢复后 `exit 0`。

### 32.6 未决项：单份 ini 表达不了两个运行时名

真正的修法是让插件直接查框架运行时（框架 `Code/client/Services/PluginMessagingExport.cpp`
**已经导出了同样的四个入口点**，包括 `STRPM_QueryTransportInterface`）。但
`STRBridgeModule` 只有**一个**键，而运行时按游戏版本有两个名字
（`SkyrimTogetherRuntime.dll` / `SkyrimTogetherRuntime_1_5.dll`），
门面的加载器又是**精确模块名**匹配。两条候选修法，**结论不同**——第一条本轮已能
**证否**（下详），第二条需要实机验证：

| 修法 | 影响面 | 为什么本轮没做 |
| --- | --- | --- |
| 默认改回 `SkyrimTogetherRuntime.dll` | 1.6.x/1.7.x **恢复可用**；1.5.x 更糟（见下） | **已证明会载入错误 ABI 的 DLL**，不只是"仍然死" |
| 由 bootstrap 按 `IsLegacyGame()` **改写** ini 的那一行 | 两个版本都对 | bootstrap 要写用户的 `Data/SKSE/Plugins/`，MO2 虚拟文件系统下可能落到覆盖层或直接失败，风险大于收益 |

**为什么"改回 `SkyrimTogetherRuntime.dll`"比 `976f9702` 以为的更危险**（本轮新查明，
这正是不能简单回退的原因）：

1. `DeployRuntime` 把 `Data/SkyrimTogetherRuntime/` **整个镜像进游戏根目录**
   （`main.cpp:571` + `:393`），而打包脚本**强制要求该目录里同时存在两个 DLL**
   （`New-STNModPackage.ps1:153-157`）。所以**任何**版本的安装，游戏根目录里
   `SkyrimTogetherRuntime.dll` 与 `SkyrimTogetherRuntime_1_5.dll` **都在**；
2. bootstrap 只按 `IsLegacyGame()` `LoadLibraryW` **其中一个**（`main.cpp:468`），
   另一个**仍在磁盘上**；
3. 门面的 `LoadStrBridgeModule` 在 `GetModuleHandleW` 未命中后，会走
   `LoadLibraryW(modulePath)`（`STRPluginMessagingAPIRuntime.cpp:850`），
   而 Windows 对**裸模块名**的搜索顺序**第一站就是 exe 所在目录**。

⇒ 在 **1.5.x** 上把 ini 写成 `SkyrimTogetherRuntime.dll`，`GetModuleHandleW` 因为
bootstrap 加载的是 `_1_5` 而**未命中**，紧接着 `LoadLibraryW` **会把旁边那个
1.6.x/1.7.x 的运行时映像载入 1.5.x 进程** —— 那是一份用**不同结构体布局**编译的
DLL（`SKYRIM_TARGET_LEGACY=1` 才切布局，见 `main.cpp:57-60`）。

**后果的精确边界**（不夸大）：那份运行时**不是**被 bootstrap 启动的，所以
`World::Create()` / `RunTiltedApp()` 都没跑，它内部的 `m_pWorld` 始终为空；
于是 `PluginMessagingService::Send` 在 `if (!m_pWorld)` 处返回 `kNotAvailable`
（`PluginMessagingService.cpp:185-186`）。**所以这不是"必然崩溃"**，
而是"**载入了一份本不该存在的跨版本映像，插件拿到一个永远不可用的传输**"。
本轮**不做这个改动**的理由是：它把一个**已证明的静默失效**换成一个
**未被测试过的跨版本映像载入**——后者在实机上是否真的无害（静态初始化器、
地址库单例、其它插件的 hook 是否会被这份映像干扰）**无法在本机验证**，
而前者至少是"不崩"。

> **所以 `976f9702` 的结论对、理由错**：它以为命名单个运行时的后果是"在另一个游戏版本上
> 静默找不到传输"，实际后果是**把错误 ABI 的 DLL 拉进进程**。
> 这让"默认指向桥接"从"两害相权"变成"**唯一不引入新崩溃面的选择**"——
> 代价是插件同步失效（静默、不崩），换来的是不崩。

**注意**：插件仓库是**钉死的子模块，本仓库不推送**，所以"让插件去查框架运行时"
这条路**在本仓库内不可行**（改了也进不了 CI 与发布）。

### 32.7 教训

> **一个"两个版本各有一个名字"的东西，不能让一个只有一个槽位的配置文件去默认它。**
> `976f9702` 的推理本身没错（运行时确实两个名字），但它把结论定成了
> "那就默认桥接吧"——而桥接在**本框架内结构性地收不到任何东西**。
> 于是为了修 1.5.x 的一个空档，代价是**所有版本**的插件同步全部失效。
> 正确的默认应该是"**在本框架内能被满足的那个**"，哪怕它只覆盖一个游戏版本。

> **文档里的"默认值"必须由门禁绑到实际出货值上。** 这次两处偏差
> （散文说 A、ini 是 B）能存活，就是因为没有任何检查同时读这两个文件。
> 凡是"文档向读者承诺一个可观测行为"的地方，都值得一条这样的断言。

## 33. 2026-09-27 场次：把"不可推送的子模块"从"修不了"变成"能修"

### 33.1 上一轮为什么停手

§32 把两处插件缺陷记成"确认存在但不可修"，理由是插件仓库是钉死的子模块、
本仓库不推送。本轮先把这条理由**验证**了一遍，而不是继续假设：

```text
$ git push --dry-run https://github.com/Caelvanost/DAVSyncTogether.git HEAD:refs/heads/__probe
remote: Permission to Caelvanost/DAVSyncTogether.git denied to komAAmok.
fatal: unable to access ...: The requested URL returned error: 403
```

确实没有推送权限。但"没有推送权限"**不等于**"这个缺陷修不了"——
它只否掉了**一条**修法。同一轮里另外两个事实才是关键：

| 事实 | 命令 | 结果 |
| --- | --- | --- |
| 上游没有比 pin 更新的提交 | `git ls-remote <url> HEAD` | `cd4ec0ec…` == 当前 pin |
| CI 会丢弃工作树里的一切改动 | `windows.yml:46` | `git submodule update --init --force --recursive --depth=1` |

第一条否掉了"等上游修"。第二条解释了**为什么"直接在子模块里改一行"是一种幻觉**：
改动在 CI 上会被 `--force` 抹掉，在本地则是一次永远进不了包的编辑。

### 33.2 修法：补丁作为本仓库的普通内容

新增 `Code/plugins/patches/<plugin>/*.patch` 与
`Code/plugins/tools/plugin_patches.py`。补丁是本仓库的**普通文件**，
照常 review、照常进历史、照常随发布走；由 CI 在插件被 configure/build
**之前**应用到子模块工作树上。

| 命令 | 用途 | 位置 |
| --- | --- | --- |
| `apply` | 应用全部补丁（**幂等**） | `windows.yml`，checkout 之后、build 之前 |
| `verify` | 每个补丁都必须仍能打到 pin 上 | `plugins.yml`，与其它门禁并列 |
| `check` / `revert` | 本地查看 / 回到 pin | 本地 |

关键设计是 `verify` 用 `git apply --check --cached`——对**索引**而不是工作树。
索引里始终是 pin 的内容，所以"补丁已经应用了"**掩盖不了**"这个补丁已经配不上新的
pin"。这正是重新 pin 插件时最容易犯、也最难发现的错。

### 33.3 用这套机制修掉的第一处缺陷：DAV 启动期崩溃

`DAVConfigIndex::Load()`（`DAVConfigIndex.cpp:95`）里
`recursive_directory_iterator` 的构造在**循环内的 `try` 之外**，
而 `Load()` 由 `main.cpp:55` 从 `OnSKSEMessage(kDataLoaded)` 直接调用，
`main.cpp` 又没有 handler ⇒ `filesystem_error` 逃出 SKSE 消息回调即 `std::terminate`。

触发条件很平常：`Data/SKSE/Plugins/DynamicArmorVariants` **存在但不是目录**
（残留的同名文件、权限问题、符号链接环）。注意 `:87` 的 `exists()` 拦不住它——
`exists()` 对文件同样返回 true。

**本机实测**（g++ 13, `-std=c++20`，把该循环原样抄成最小复现）：

```text
=== run UNPATCHED (the shipped shape) ===
  exit=-1073741795        # 0xC000041D，未捕获异常穿过回调
=== run PATCHED ===
  iteration unavailable: Not a directory
  exit=0
```

修法：改用 `error_code` 重载（`skip_permission_denied`），构造失败与枚举中断
都只记一行日志。三种情形都实测过：root 是文件 → 记 `error` 并返回；
root 是正常目录 → 照常索引；root 不存在 → 走原有分支。

### 33.4 第二处：删掉一个"勾了也没用"的选项

`IEDSyncTogether` 的 `RelayHost` 子选项唯一作用是打开旧 UDP 传输。
但该传输**根本没被编译**：`CMakeLists.txt` 的 SOURCES 里没有
`UdpTransport.cpp` / `StrTransport.cpp` / `SyncService.cpp` / `Config.cpp`。
于是随包的 `IEDSyncTogether.ini` 与这个子选项都是**惰性的**。

处理方式分两种，按"读者会不会被骗"来定：

- **删掉子选项**。它是一次安装时的**选择**，而那个选择**不可能生效**；
  留着就是让人以为自己在配置什么。
- **保留 ini，但把"它现在是惰性的"写进清单 `payloadNote`**。
  ini 是上游文件，上游一旦把 `Config.cpp` 加进构建它就会生效，
  删掉反而会在那时变成一个缺失的 bug。写清楚比删掉好。

### 33.5 连带修好的一处门禁自伤

补丁一应用，`plugin_snapshot.py verify` 就报
`snapshot differs in 1 file(s)`。原因是它比对的是**工作树**，
而补丁让工作树**合法地**不等于 pin。

改成用 `git show <pin>:<path>` 读**pin 那个提交**的内容。它量的仍然是
"备份是否还等于被构建的那个提交"，而不是"工作树是否干净"。两种状态
（已打/未打）都实测 `exit 0`，篡改快照仍然被抓住（`exit 1`）。

> **教训**：一个门禁如果断言的是"工作树必须等于 X"，那么任何"合法地改变工作树"
> 的新机制都会把它变成假警报。要断言的是**被测对象之间的关系**，
> 不是某个实现细节的当前状态。

### 33.6 教训

> **"没有权限"只否掉一条路，不否掉目标。** 上一轮把"推不了子模块"直接推成了
> "修不了缺陷"，跳过了"那这个修复能不能作为**本仓库的内容**存在"。
> 遇到"外部依赖改不了"时，正确的问题是"**修复本身**能不能换个地方住"，
> 而不是"那个地方让不让我改"。

> **子模块里的一行改动不是修复，是一次会消失的编辑。** 判断一个改动是否真实，
> 要看它能不能活到 CI 上、活到包里。`--force` 的 checkout 会把幻觉照出来。

### 33.7 新机制自己踩的坑：补丁必须钉成 LF

补丁是 `*.patch`，而 `.gitattributes` 开头就是 `* text=auto`。于是
Windows 检出（`core.autocrlf=true`）会把补丁的每一行变成 CRLF，
而 `git apply` 是**逐字节**比对上下文行的——补丁在**恰好是 CI 运行的那个平台**上
变成打不上的。实测（把补丁转成 CRLF 后）：

```text
verify with CRLF patch: exit=1     # 门禁失败
apply  with CRLF patch: exit=0     # 但 apply 竟然"成功"了
```

这个组合是最坏的一种：**门禁红、构建绿**。人看到红会去改门禁，而不是去查编码。

修法与既有三个文件同款，在 `.gitattributes` 里钉死：

```text
Code/plugins/patches/**/*.patch text eol=lf
```

验证方式不是"看一眼属性"，而是**让 git 真的重新检出一次**再数字节：
`git checkout -- <patch>` 之后 `CRLF=0 bareLF=47`。

> **教训**：本仓库是 Windows 优先、而 `git apply` 是逐字节的，
> 所以**任何新引入的、会被 `git apply` 读的文本文件**都必须显式钉 LF。
> `* text=auto` 的默认值在 Linux 上无害，在这里是定时炸弹。

## 34. 2026-09-27 场次：同步上游 #901（未发布的 dev 提交）

### 34.1 为什么是 dev 而不是 tag

上游最新的 tag 仍是 `v1.8.2`（`0ffb80b0`），而 `dev` 已经比它多 2 个提交：

| 提交 | 内容 | 本仓库 |
| --- | --- | --- |
| `fbf72883` | **#901 远端 actor 的 Havok 控制器时间步长** | 本轮已并入 |
| `589b5f5b` | CI 双重 zip（`upload-artifact@v7` 的 `archive:` 键） | **不适用** |

第二个不适用是查过的、不是跳过的：本仓库用 `upload-artifact@v4`，5 处
`upload-artifact` 里只有 `release.yml:149` 的 path 含 `.zip`（那是发布流程本身），
且全仓库没有 `release-assets` 这个路径——上游那个 bug 的前提在本仓库不存在。

### 34.2 #901 修的是什么

远端 actor 跳过原生移动处理，于是新建的 Havok controller 停在
`stepInfo.deltaTime == 0`。插值位置更新照样重置速度，
**除以 0 得到非有限加速度**，再经接触质量修正传播出去 ——
实机表现是"物体和 NPC 碰撞后、踩到散落物品后**异常滑行**"。

修法两处：远端移动处理里刷新 controller 时序；强制位置更新前刷新，
拿不到有效步长时**推迟** controller 定位（只更新引用/3D，让插值稍后补上）。

### 34.3 1.5.x 的偏移是**读出来的**，不是抄的

新增 3 个文件里 `hkStepInfo` 是纯数据布局，与版本无关。真正的问题是
`bhkCharacterController::stepInfo` 在 1.5.x 上是不是也在 `0x80`。

**本机有一份真实的 1.5.97 `SkyrimSE.exe`**（`D:\game\SkyrimSE\`，
`FileVersion 1.5.97.0`），所以这件事可以查而不是猜。做法：

1. 在 `.text` 里搜"加载物理步长全局 + 求倒数 + 经**同一寄存器**写 `+0x88` 与 `+0x8C`"
   这一模式。全镜像（22 MB `.text`）**只命中一处**：RVA `0xDBFDD4` / `0xDBFDE4`；
2. 反查字节确认指令：

   ```text
   F3 0F 10 0D DF 85 04 01   movss xmm1,[rip+0x10485DF]  -> 0x1E083A8  (物理步长全局)
   F3 0F 11 8E 88 00 00 00   movss [rsi+0x88],xmm1                   (deltaTime)
   F3 0F 5E C1               divss xmm0,xmm1                        (取倒数)
   F3 0F 11 86 8C 00 00 00   movss [rsi+0x8C],xmm0                   (invDeltaTime)
   ```

3. 用 `.pdata` 定出所属函数 `0xDBF630`，再确认**两个 RVA 在
   `version-1-5-97-0.bin` 里都是真实符号**：函数 = id `76436`，
   全局 `0x1E083A8` = id `512261`。两头都有名字，不是插值出来的近似值。

`0x88/0x8C` 就是 `deltaTime`/`invDeltaTime`，所以 `stepInfo` 在 `0x80` ——
与上游对 1.6.x/1.7.x 的断言**一致**。**该成员在两代之间没有移动**，
所以这里不像 `Actor` 那样需要 `#ifdef SKYRIM_TARGET_LEGACY` 分支，
一条 `static_assert` 对两个目标都成立。

### 34.4 两个新 id 在 1.5.97 上都没有映射，但**性质不同**

同一对 id 在 1.6.317/640/1170/1179 与 1.7.99/104 的库里**全部有映射**，
所以 1.6.x / 1.7.x 走的就是上游原路径；只有 1.5.x 需要改写。而两者的改写方式**相反**：

- `39856`（`AIProcess::GetCharController`）是**函数 id**。
  `VersionDb.h:60-62` 写明桩的契约是"**调用它**返回 0 而不是崩"，
  所以按本仓库既有写法用 `POINTER_SKYRIMSE` + `ThisCall` **正确且一致**，
  未映射时自然得到 `nullptr`，两个调用点都判空跳过刷新。
- `389089`（物理步长那个 **float 全局**）是**数据 id**，**不能**这样写：
  桩是一段**可执行内存**（`48 31 C0 0F 57 C0 C3`），
  `*stub()` 是**把它自己的机器码当 float 读**。

  实测那几个字节解码为 `1.895e-29`，恰好被 `UpdateDeltaTime` 的
  `<= 0.0001` 规则挡掉。**但这是桩的编码碰巧，不是契约**——
  换一段解码成大有限值的桩，它就会被当成合法步长直接写进 controller。
  故改用 `FindAddressById` 显式解析、失败传 0，
  与 `GarbageCollector::Get()`、`LeveledNpcSystem::CanRecordPick()`
  处理各自未映射数据 id 的做法一致。

> **教训**：`POINTER_SKYRIMSE` 对未映射 id 是安全的，**但只对函数 id 安全**。
> 数据 id 走同一条路会解引用桩的机器码。区分"这个 id 是拿来调用的还是拿来读的"
> 是 1.5.x 适配里必须先问的一句话。

### 34.5 顺带修掉一处**我自己引入**的空指针面

上游的 `ForcePosition` 里没有 `GetExtension()`，而本仓库的
`Actor::GetExtension()`（`Actor.cpp:285-298`）在既非 `ExActor` 也非
`ExPlayerCharacter` 时**返回 `nullptr`**。照搬上游会在
"本来就是要让它更安全的这条路径上"新增一个解引用。
已按同文件 `HookSetPosition`（`:201`）的既有写法判空。

### 34.6 验证方式

`hkStepInfo` 的退化阶梯用 g++ 编成可执行，并**分进程**跑了两种映射状态：

| 状态 | 结果 |
| --- | --- |
| 1.6.x/1.7.x，id 有映射 | `ok=1 dt=0.0166667 inv=60.000` |
| 1.5.x，id 无映射、有移动步长 | `ok=1 dt=0.0200000 inv=50.000`（退回移动步长） |
| 1.5.x，两者都没有 | `ok=1 dt=0.0100000 inv=100.000`（保留 controller 里已有的值） |

**分进程是必须的**：`PhysicsDeltaTime()` 缓存答案（"只问一次"），
同一进程内换映射不会重算——这一点也是实测撞出来的，不是设计时想到的。

另用 g++ 把改过的 `AIProcess::GetCharController` / `ForcePosition` 逻辑
连同一份最小 shim 编译运行过，确认签名、调用约定与判空分支无误。
（shim 本身修了三轮才对：`VersionDbPtr` 是模板、`TP_THIS_FUNCTION` 定义的是
**函数指针类型**故 `Get()` 返回指针的指针、`NiRefObject` 是 0x10 字节的
多态基类。这三处错都在 shim 里，源码没动过。）

## 35. 2026-09-27 场次：把"文档更新"从习惯变成门禁

### 35.1 起因：一条规则如果不检查，就等于没有

`CODE_GUIDELINES.md` 原来只规定了命名与提交前缀，**没有任何一条要求改代码时
同步改文档**。于是文档只能靠人记得——而"人记得"在版本表上先失效了。

这一轮把规则写进 `CODE_GUIDELINES.md`（新节 **Documentation is part of the
change**：每次版本修改必须在同一提交内更新文档），并且**同时**写了执行它的门禁
`Tools/Scripts/check_docs.py`。规则与门禁必须一起落地：只写规则的后果是
下一个人照样可以不改，而没人会知道。

### 35.2 门禁抓到的四处真实问题

写门禁之前先想清楚"什么算错"，然后让门禁自己去找。它第一次运行就报出四处，
**全部属实**：

| 问题 | 事实 |
| --- | --- |
| `v1.1.3` 打了 tag 却没有 CHANGELOG 小节 | 该版本内容**只存在于两个 README 的版本表里** |
| `v1.1.4` 在两个 README 的版本表里都没有行 | 版本表停在 1.1.3 |
| 覆盖率是旧口径 | 4 个文档写着 3080/3069/11，生成器给的是 **3082/3069/13** |
| 推荐版本落后两版 | 横幅写 `1.1.1`，最新 tag 是 `1.1.4` |

第一条最值得记：**`v1.1.3` 是一个"无人可读"的发布**。它打了 tag、出了 Release、
玩家能下载，但仓库里没有任何一份文件说明这个版本做了什么——除非有人想到去翻
README 的版本表。这不是笔误，是"文档更新靠自觉"这个模式的必然结果。

### 35.3 门禁本身写错过两次，都是**自测**发现的

**第一版太松**：只检查"文档里出现过正确数字"。反向验证时把 README 里的
`3082` 改成 `3080`，门禁**照样通过**——因为我新加的 1.1.4 版本表行里
正好也写着 `3069/3082`，于是"出现过正确值"成立，旧值被掩盖了。
这恰好就是它要防的那种漂移，**却由它自己复现了一遍**。

改成"枚举文档里**所有**覆盖率形状的数字，每一个都必须等于当前值"。

**第二版又太紧**：`Tools/ida/README.md` 里有一段**有意保留的历史记录**
（"Status at the time of that run: 3033/3058 … Superseded"），被当成现行值报错；
而且 `3698/3698 offsets validated` 是**另一个指标**（映射条数校验），
被我按 `a/b` 的形状误判成 id 覆盖率。

两处修法不同，值得分开记：

- 历史口径**不该删**——它记录了当时的口径，是留档。所以规则不是"数字必须对"，
  而是"**这句话必须说明自己是历史**"。门禁改为检查前后 3 行里有没有
  `superseded` / `at the time` / `当时` / `已过时` 这类标记；
- **`n/n` 不一定是覆盖率**。同一份文档里 `3698/3698` 是"映射条数全部校验通过"，
  与"代码引用的 id 解析了多少"是两件事。门禁改为排除两边相等的 pair。

> **教训**：写门禁时**必须做反向验证**，而且要用"看起来最像真话"的注入方式。
> 我第一版的漏洞是"在同一个文档里同时存在新旧两个值"——那正是现实中最常见的
> 漂移形态，也是我最初没想到要测的形态。

### 35.4 顺带把一条判据写给了读者

§34 里"未映射 id 是**函数**还是**数据**，处理方式相反"这条判据，原本只在提交信息
和本文件里。这一轮把它写进了两个 README 与 `docs/LAN-RADMIN-GUIDE.md`：
函数 id 的桩"调用即返回 0"可以安全调用；数据 id 不能走同一条路，
因为桩是一段可执行内存，当数据读会读到它自己的机器码。
读者不需要知道 §34 的细节，但需要知道这条区别存在。

### 35.5 发版流程

`docs/RELEASE-AND-MO2.md` 的"发版"一节把 `check_docs.py` 放在 `git tag`
**之前**，并写明它抓到过什么。理由写在那一节里：这个检查不是形式主义，
它报出来的每一条都曾经是真的。

### 35.6 tag 命名空间是**共享**的，名字本身说明不了归属

写 `tags-have-sections` 时才发现：**上游与本仓库用同一套 tag 名**。
上游 TiltedEvolution 发布 `v1.0.x` / `v1.1.x` / `v1.2.0` … `v1.8.x`，
而本仓库合并过上游的提交，所以本地克隆**两套 tag 都在**。`v1.1.x` 在两边**都存在**，
但指向完全不同的提交：

| tag | 本仓库 | 上游 |
| --- | --- | --- |
| `v1.1.0` | `03cf2cbb` | `fd8748f0` |
| `v1.1.1` | `1161cd8a` | `cd7c6493` |
| `v1.1.2` 起 | 本仓库独有 | 不存在 |

所以"这个 tag 是不是我们的"**不能靠名字判断**，必须显式列出本仓库自己的系列
（`OWN_TAG_SERIES = ('1.1.',)`）。只有这些系列才要求有 CHANGELOG 小节——
要求上游的 tag 也有本仓库的小节是错的。

> **教训**：fork 会继承上游的 tag，而 tag 名不携带"谁打的"这个信息。
> 任何"遍历所有 tag"的检查都必须先回答"哪些 tag 归我管"，
> 并且把这个答案**写成常量**而不是散在判断里——否则下一个系列
> （`1.2.`）出现时，它会**静默跳过**而不是报错。

### 35.7 还有一个"通过得太容易"的坑

`tags-have-sections` 依赖 `git tag`，而 `actions/checkout@v4` **默认不取 tag**。
那样循环会跑零次、报告成功——**因为它看不见要检查的东西**。
这正是这门禁存在的理由本身，所以不能只靠"应该没问题"：

- CI 侧：`plugins.yml` 的 checkout 显式加 `fetch-depth: 0` 与 `fetch-tags: true`；
- 门禁侧：若**一个本仓库的 tag 都看不见**，直接报错
  （"refusing to pass vacuously"），而不是当作没有 tag 可查。

> **教训**：一个"遍历集合"的检查，必须先断言**集合非空**。
> 否则输入消失时，它会从"没发现问题"变成"没问题"。

## 36. 2026-09-27 场次：KiLoader / KreatE 的「不兼容」其实是我们的辅助进程误加载了 ENB 代理

**报障**：装了 KiLoader（以及依赖它的 KreatE）之后启动游戏弹窗
`KiLoader initialization failed`，细节是两条 `Couldn't open logging file`，
第二条 `std::system_error: The process cannot access the file because it is being used by
another process`。结论被直接写成「本框架不兼容 KiLoader」。

上游有同样的报告（TiltedEvolution#766，2025-02 提出、至今 open），
维护者的回复是「无法承诺支持 KiLoader，建议改用 Community Shaders」。
**这个结论在本仓库里是错的**，而且错得不必要。

### 36.1 判据：报错里的进程名不是游戏进程

截图里最关键的一处字样是 **`KiLoaderTPProcess`**：

```
IOException(Couldn't open logging file
    C:\Users\Admin\AppData\Local\KiLoaderTPProcess\Logs\KiLoader.log)
IOException(Couldn't open logging file
    D:\GAMES\Nolvus V6\STOCK GAME\Data\KiLoader\KiLoader.log)
    std::system_error: The process cannot access the file because it is being
    used by another process
```

`TPProcess.exe` 是**本框架的 CEF 辅助进程**（`Code/tp_process/`，
在 `OverlayApp::Initialize` 里作为 `browser_subprocess_path` 交给 CEF）。
KiLoader 的日志路径按**当前进程名**推导，所以 `KiLoaderTPProcess` 这个目录名
说明 KiLoader 是在**我们的辅助进程里**被拉起来的，不是游戏进程。

> **教训**：报错里的进程名/路径里出现**我们自己的可执行文件名**时，
> 「两个 mod 不兼容」这个结论就已经站不住了 —— 先查「谁把那个框架带进了我们的进程」。

### 36.2 链路：游戏根目录的代理 DLL 赢了 System32

辅助进程 `TPProcess.exe` 位于**游戏根目录**。Windows 的默认搜索顺序是
**exe 所在目录 → 系统目录 → PATH**，所以游戏根目录里的任何同名 DLL 都会
优先于 `System32` 被加载。

而 `libcef.dll` 的延迟导入表里有 `dxgi.dll`、`d3d11.dll`、`d3d12.dll`、`dcomp.dll`
（自写 PE 解析确认；`Code/tp_process` 只静态依赖 `libcef.dll`/`KERNEL32.dll`/`ADVAPI32.dll`）。
这四个正是 ENB、ReShade、SpecialK、Community Shaders 用来挂自己的**代理 DLL 名**。

于是链路是：

1. 辅助进程启动 → `libcef.dll` 按名延迟加载 `d3d11.dll`；
2. 游戏根目录的 ENB 代理胜出 → ENB 初始化 → 带起 `enbseries\` 下的卫星 DLL；
3. `KiLoaderSatelliteENB` 拉起**第二份 KiLoader**；
4. 第二份去开游戏进程**整场都占着**的 `Data\KiLoader\KiLoader.log` → 启动即失败；
5. 弹窗被读成「Skyrim Together 和 KiLoader 冲突」。

**这解释了为什么只有装了 ENB/ReShade 类框架的整合包会报**：没有代理 DLL 时，
游戏根目录没有 `d3d11.dll`，第 2 步就不会发生。

### 36.3 修法与两个实测细节

修法：`Code/tp_process/main.cpp` 在 `CefExecuteProcess` **之前**，按**完整路径**
加载 `System32` 的四个代理名（见下）。加载器在搜索任何目录**之前**
先按**基名**匹配已加载模块，所以随后的按名加载（延迟导入就是按名）直接命中
它们，不再落到游戏根目录。

**钉住的四个名字**：`dxgi.dll`、`d3d11.dll`、`d3d12.dll`、`dcomp.dll`。选它们的依据是
直接读 `libcef.dll` 的**延迟导入表**：只有这四个既是延迟导入、又是图形框架会占用的
代理名；其余延迟导入（USER32/SHELL32/MFPlat…）不可能被 mod 占名。

**细节一：顺序必须 `dxgi` 在前**。`System32\d3d11.dll` **静态导入** `dxgi.dll`，
先加载 `d3d11` 会让它自己的依赖解析到游戏根目录的代理，加载失败。
本机用一份同名的假代理复现（C# + P/Invoke，见 36.4）：

```
app dir 里放假的 d3d11.dll + dxgi.dll，不预加载：
  bare d3d11.dll -> FAILED err=193 (ERROR_BAD_EXE_FORMAT)
  bare dxgi.dll  -> FAILED err=193
先预加载 d3d11、再 dxgi：
  preload d3d11 -> FAILED err=193      <- 它自己的 dxgi 依赖被劫持
先预加载 dxgi、再 d3d11：
  preload dxgi  -> C:\WINDOWS\system32\dxgi.dll
  preload d3d11 -> C:\WINDOWS\system32\d3d11.dll
  bare dxgi     -> C:\WINDOWS\system32\dxgi.dll   <- 已钉住
  bare d3d11    -> C:\WINDOWS\system32\d3d11.dll  <- 已钉住
```

**细节二：不能改用 `SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32)`**。
本框架自己的 overlay 运行时（`libEGL.dll`、`libGLESv2.dll`、`vk_swiftshader.dll`、
`d3dcompiler_47.dll`）同样部署在游戏根目录并**按名**加载，收紧搜索路径会把
「辅助进程误加载 ENB」换成「overlay 起不来」。只钉这四个名字即可，
因为 `libcef.dll` 的延迟导入表里，只有这四个是第三方会占用名字的图形 DLL。

> **未走的路**：删掉游戏根目录的代理、让 ENB 换代理名 —— 都是在要求玩家改自己的
> mod 列表；把辅助进程挪出游戏根目录 —— 要动 CEF 的 `browser_subprocess_path`
> 与打包布局，收益不抵风险。

### 36.4 复核用的命令（照抄）

```powershell
# 1. 辅助进程到底静态依赖什么（只有 libcef/KERNEL32/ADVAPI32 才是对的）
#    用自写 PE 解析读导入表；`dumpbin /imports` 需要 VS 环境

# 2. libcef.dll 的延迟导入里有没有第三方会占用的图形 DLL 名
#    -> dxgi.dll / d3d11.dll / d3d12.dll / dcomp.dll 都在

# 3. 应用目录优先级：把假 DLL 放在 exe 旁边，看按名加载落到哪里
#    （本机实测：落在 exe 旁边，报 193；预加载 System32 后落到 System32）

# 4. 真机确认代理是否存在
Get-ChildItem '<game root>' -Filter '*.dll' |
    Where-Object { $_.Name -match 'd3d11|dxgi|dinput8|version|winmm' }
```

### 36.5 顺带记下：KiLoader 是什么

KiLoader 不是普通 SKSE 插件，而是一个**插件加载框架**：它有自己的插件 API、
有「卫星 DLL」（`KiLoaderSatelliteENB.dll` / `KiLoaderSatelliteSKSE.dll`，
分别挂在 ENB 与 SKSE 上），并**按进程名**管理日志与配置。KreatE 与 AELAS 是
跑在它上面的插件。正因为它是**按进程**存在的框架，同一台机器上出现第二份实例
时才会撞日志文件 —— 这是它如实报告了被强加的碰撞，不是它的缺陷。


## 37. 2026-09-27 场次：两处「刚进游戏就闪退」，一处来自我们的运行时，一处来自 RaceMenu 的版本错配

**报障**：MO2 + SKSE 打开游戏「一小会儿就直接闪退」。日志目录里能读到三种崩溃，
其中两种各有三次以上复现，全部是 `EXCEPTION_ACCESS_VIOLATION`，
且**两次都读到了一个明显不是地址的偏移量**：

```
# A（三次）skee64.dll+0x81be8                  mov eax, [r14+0x14]   target 0x15
# B（一次）  SkyrimTogetherRuntime_1_5.dll+0x16CFA8  mov eax, [r15+0x14]   target 0x14
```

两处都是 `0x14` —— 这是 `TESForm::formID` 的偏移。也就是说：
**代码把一个空指针（或一个根本不是指针的东西）当成 `TESForm*` 去读它的 formID。**
两次的成因完全不同，必须分开修。

### 37.1 判据：先看「目标地址」是哪个常量，不要先看模块名

崩溃日志里最有信息量的一行不是模块名，而是：

```
access type read (code 0), target address 0x15 (unmapped 0x15)
```

`0x15 = 0x14 + 1`，`0x14 = 0x14 + 0`。这两个数字直接说明了**被当成指针的那个值本身**
是 `1` 和 `0`：

| 崩溃 | 寄存器值 | 被误当成 | 真实含义 |
| --- | --- | --- | --- |
| A | `r14 = 1` | `TESObjectREFR*` | `OverlayType::Spell` 枚举值 |
| B | `r15 = 0` | `TESForm*` | 空指针 |

**教训**：`target address` 落在 `0x14` 附近时，先假设「有人把一个整数/枚举当指针传了」，
再回去找那个调用点；只看模块名会得出「第三方 DLL 有 bug」的错误结论。

### 37.2 崩溃 A：RaceMenu 的接口是**按名字**取的，布局却没人核对

链路：

1. 插件发 `kMessageExchangeInterface`，拿回 `IInterfaceMap`；
2. `QueryInterface("Overlay")` —— **只按名字查表，不校验布局**；
3. 插件用自己那份 `IOverlayInterface` 声明去调用；
4. 声明与 DLL 的 vtable 顺序不一致 → **调用了隔壁的方法**。

实测这台机器上的 `skee64.dll`（FileVersion 4.0.0.0）里，
`.?AVOverlayInterface@@` 的 vtable 有 **23** 个槽位，
槽位 `[11]` 的函数地址是 `0x180081B00`，其中 `0x180081BE8` 正是崩溃点，
而该函数开头就是 `testq %rdx,%rdx / je`，随后 `mov 0x14(%rdx),%eax`：

```
180081B00  testq %rdx, %rdx
180081B03  je    0x180081d20      # rdx == 0 时安全返回
...
180081BE8  movl 0x14(%r14), %eax  # <<< 崩溃：rdx 被当成 TESObjectREFR*
```

插件声明的是**版本 2** 的布局，槽位 `[11]` 是 `GetOverlayCount(OverlayType, OverlayLocation)`；
DLL 实现的却是**版本 1**，槽位 `[11]` 是 `RevertHeadOverlays(TESObjectREFR*, bool, bool)`。
于是 `GetOverlayCount(Spell, Body)` 把 `rdx = 1`（`OverlayType::Spell`）
当成 `TESObjectREFR*` 传了进去 → 读 `[1+0x14]` = `0x15` → 闪退。

两个布局的差异（前 15 槽）：

```
槽位  插件（v2）声明的           DLL（v1）实际实现的
 2    Revert                      Save
 3    HasOverlays                 Load
 4    AddOverlays                 Revert
 5    RemoveOverlays              HasOverlays
...
11    GetOverlayCount             RevertHeadOverlays   <<< 崩溃点
12    GetOverlayFormat            RevertHeadOverlay
13    RegisterInstallCallback     SetupOverlay
14    UnregisterInstallCallback   UninstallOverlay
```

**为什么以前没炸**：`GetOverlayCount(Normal, ...)` 的 `rdx = 0`，
而 v1 的 `RevertHeadOverlays` 开头恰好有 `testq %rdx,%rdx; je`，
所以四次 `Normal` 调用**全部安全返回**，只有 `Spell`（`rdx = 1`）才踩中。
这就是「能进游戏、玩一小会儿才崩」的原因。

> **教训**：跨 DLL 的接口只要**按名字**获取，就必须**同时**校验版本。
> `GetVersion()` 在每种布局里都是槽位 `[1]`，是唯一可以在调用任何其它方法之前安全调用的方法 ——
> 拿它当门禁，代价是一次比较，收益是不会有下一次「调用到了隔壁方法」。

**修法**（仓库自有补丁，见 §33 的补丁机制）：

- 两个插件各自的 `IOverlayInterface` / `IOverrideInterface` / `IBodyMorphInterface`
  声明上增加 `kSupportedVersion`，并在 `Initialize()` 里**先查版本再采用**；
  版本不足就**拒绝该接口并打印明确错误**，而不是硬着头皮调用。
- `IBodyMorphInterface` 是**只追加**的接口（版本 5 只在末尾加了 `AddMorphShapeCallback`），
  所以版本 4 的声明对版本 5 仍然有效 —— 门禁写成 `>= 4` 而不是 `== 5`。
- 新增门禁 `Code/plugins/tools/check_skee_abi.py`：把两个插件声明的方法**顺序**
  与 RaceMenu 的真实顺序逐槽比对，并要求每份声明都带版本门禁。
  已实测：删掉 `RevertOverlay`、或对调 `GetOverlayCount`/`GetOverlayFormat`、
  或移除版本门禁，三种改动都会让它失败。

### 37.3 崩溃 B：引擎用**空指针**调用装备钩子来清空槽位

这条在我们自己的运行时里。`Code/client/Games/Skyrim/EquipManager.cpp` 挂了六个
`EquipManager` 钩子，其中**法术/龙吼**四个直接读形参：

```cpp
void* TP_MAKE_THISCALL(UnEquipSpellHook, EquipManager, Actor* apActor, TESForm* apSpell, MagicEquipData* apData)
{
    if (!apActor)
        return nullptr;

    const auto pExtension = apActor->GetExtension();
    if (pExtension->IsRemote() && !ScopedEquipOverride::IsOverriden())
        return nullptr;

    if (pExtension->IsLocal() && ... )
    {
        EquipmentChangeEvent evt{};
        evt.ActorId = apActor->formID;
        evt.ItemId = apSpell->formID;                  // <<< apSpell 可能是空指针
        evt.EquipSlotId = apData->pEquipSlot->formID;  // <<< pEquipSlot 也可能是
        ...
```

只有 `apActor` 判了空。**引擎在清空一只手时会用空形式调用这些钩子**，
于是 `apSpell->formID` 读到 `[0+0x14]` → 崩溃。
反汇编确认：崩溃指令 `0x18016CFA8 mov 0x14(%r15),%eax`，
对应源码就是 `apSpell->formID` 这一行（`r15` 由第 4 个参数 `r9` 传入）。

> **注意**：**物品**那两个钩子（`EquipHook`/`UnEquipHook`）里的 `pSlot` 早就写了
> `apData->pSlot ? apData->pSlot->formID : 0`，说明这个坑**当时是知道的**，
> 只是没有推广到法术/龙吼那四个 —— 同一个文件里两种写法并存，就是漏改的信号。

**修法**：六个钩子统一改成

1. 形参为空时**原样转发给原函数**（`ThisCall(RealXxx, ...)`），不返回 `nullptr` ——
   返回 `nullptr` 会让引擎以为「这次装备被取消了」，与清空槽位的行为不符；
2. `pEquipSlot` 用与物品钩子相同的 `?: 0` 三元写法。

### 37.4 这两个 bug 为什么能一起活这么久

它们都**不会在启动时暴露**：

- A 需要 `OverlayType::Spell` 才会踩中，而 `Normal` 恰好被 DLL 自己的空指针检查兜住；
- B 需要引擎用空形式清手，而这取决于玩家当时在做什么。

所以「能进游戏、玩一小会儿才崩」是它们的**共同指纹**，
而不是「内存不够」或「MOD 装多了」。下次看到这个描述，先按 §37.1 找 `target address`。

### 37.5 拒绝接口只是**一半**：被拒绝之后，还有代码在解引用它

上面的修法有一个**副作用**，而且它和原崩溃一样是「玩一小会儿才崩」那一类：
**门禁把接口置空，但置空不等于所有人都不再碰它。**

链条是这样的（三个事实缺一不可）：

1. `MorphSyncService::Start()` 在 `_bodyMorph == nullptr` 时**直接返回**，
   所以同步线程不启动、`TickOnGameThread()` 一次都不跑 —— 看起来已经安全了。
2. 但 `main.cpp` 的 `kDataLoaded` 里，**传输层是独立启动的**：

   ```cpp
   UdpTransport::GetSingleton().Start();          // 先起
   MorphSyncService::GetSingleton().Start();      // 后起，可能什么都没做
   TngSync::GetSingleton().Start();
   ```

   `Start()` 失败**不会**回滚传输层，于是远端包照收。
3. 收包的落点是同一个服务对象上的 `HandleUdpPacket()` → `HandleMorphPacket()` →
   `TryApplyRemote()`，而 `TryApplyRemote()` 的**漂移分支**里写着：

   ```cpp
   _bodyMorph->ClearMorphs(actor);              // <<< 没有判空
   _bodyMorph->SetMorph(actor, ...);            // <<< 没有判空
   _bodyMorph->ApplyBodyMorphs(actor, false);
   _bodyMorph->UpdateModelWeight(actor, false);
   ```

   另一处 `CaptureMorphs()` **判了空**（`if (!actor || !_bodyMorph)`），
   所以哈希比较不会崩 —— 也正是这个「同一条路径上有人判、有人没判」的差别，
   让漏洞看起来像已经修好了。`TickOnGameThread()` 和 `CaptureMorphs()` 都判空，
   只有**网络上来的这条路**没判。

> **判据**：给接口加门禁时，**不能只问「拒绝之后谁不用它」，还要问「谁还在用它」**。
> 这一处就是反例：`Start()` 的早退让人以为整条链都停了，
> 而实际停的只是**本机自己产生数据的那一半**，收到远端数据的那一半还在跑。

**修法**（补丁 `0002-guard-the-refused-bodymorph-interface.patch`）：
`TryApplyRemote()` 一进来就判 `_bodyMorph` 并返回。

选「进门就判」而不是「在四个解引用前各判一次」，是因为这四个调用是**一笔事务**：
只做其中的 `SetMorph` 而不做 `ApplyBodyMorphs`，会把代理留在一个**改了一半**的形态上 ——
比什么都不做更糟。要么整段生效，要么整段不做。

**为什么不用 `Start()` 失败时干脆不启动传输层**：那样会把**外观探测**一起关掉，
而探测是诊断远端外观问题的唯一信息来源；而且传输层还承载 TNG 同步，
把「RaceMenu 版本旧」升级成「整个插件不工作」，代价比一个判空大得多。

### 37.6 那份旧 RaceMenu 到底缺了哪一半（实测，不是推测）

有人问：既然门禁会拒绝旧接口，能不能**同时**兼容旧版、再编一份新版的？
要回答它，先把那份 `skee64.dll`（`FileVersion 4.0.0.0`，2020-10-04，整合包里那份）
**逐个接口的 `GetVersion()` 读出来** —— 它在每种布局里都是槽位 `[1]`，
是唯一能在碰其它任何方法之前安全调用的方法：

| 接口 | 该 DLL 报告 | 插件要求 | 门禁结果 |
| --- | --- | --- | --- |
| `IBodyMorphInterface` | **4** | `>= 4` | **通过** |
| `IOverlayInterface` | 1 | `>= 2` | 拒绝 |
| `IOverrideInterface` | 1 | `>= 2` | 拒绝 |

**这张表就是答案的一半**：插件的**主功能（BodyMorph 同步）在这份旧 RaceMenu 上照常工作** ——
`IBodyMorphInterface` 报告的正是版本 4，而门禁写的是 `>= 4`。
被关掉的**只有覆盖层那一半**：阴毛/体毛覆盖层同步、面部覆盖层捕获。

> 佐证：插件声明的 `IBodyMorphInterface` 是 22 个方法，加基类 3 个 = **25 个槽位**；
> 而这份 DLL 的 `.?AVBodyMorphInterface@@` vtable 实测就是 **25 个函数指针**（版本 4）——
> 逐槽吻合。说明插件里那份声明**确实抄自真实的 RaceMenu 头**，`kSupportedVersion = 4` 不是猜的。

再把 v1 的 23 个 `IOverlayInterface` 槽位**逐个按它引用的字符串分类**，得到：

- 引用那 8 个节点名字面量的只有槽位 6/7/8、11、21/22；
- 6/7/8 是遍历全部部位的大家伙，**21/22 把格式当参数传给安装函数**，11 是那个崩过的 Face 回滚；
- **没有任何一个槽位会「返回」节点名格式**，也没有静态指针表（在 `.rdata`/`.data` 里找过，0 张）。

也就是说**没有任何一个槽位能当 `GetOverlayFormat` 用**（`GetOverlayCount` 的对应物同样没找到）。
这里说的是「按现成的槽位拼不出来」，不是「DLL 里绝对没有等价逻辑」——
等价逻辑要存在，也只能藏在那些不引用字面量的槽位里，而那样它的格式来源就无从得知。
一个 v1 兼容层因此**拼不出来**（不是「调用隔壁方法」那种错位），只能自己硬编码那 8 个字面量、
再用**已经写好的场景图扫描**（`ParseBodyOverlaySlot` 那条回退路径）去数槽位 ——
那是**照着反汇编重写一套语义**，不是移植。

**结论：先升级 RaceMenu，不要手写 v1 兼容层。**

1. 新版 RaceMenu 的 Overlay/Override 报 v2+，**零新增代码**就能拿回覆盖层；
2. 手写兼容层的失败形态**正是本轮修掉的那个**：布局或语义猜错 = 崩在玩家机器上；
   而我们对 v1 的了解只能来自反汇编 —— 没有头文件，这台机器上也**只有这一份 DLL** 可对照；
3. 拒绝接口的代价只是一行 `error` 日志 + 没有覆盖层，**morph 同步不受影响**。

要确认自己装的是哪一代，看插件本来就打的这两行即可：

```text
MST APPEARANCE interfaces READY overlay=1 overlayVersion=1 override=1 overrideVersion=1 ...
MST RaceMenu BodyMorph interface READY version=4
```

> **未验证的一环**：这台机器上只有这一份旧 DLL，也无网络，所以**没有实测过任何一份报 v2 的
> `skee64.dll`** —— 「新版报 v2」来自仓库既有取证与插件声明，不是本轮实测。
> 升级后若日志仍打印 `overlayVersion=1`，那就说明 v2 并不存在，届时正确的做法是
> 把声明改成 v1 并修同步路径，而不是维持一个永远拒绝的门禁。

**实测**：这四处解引用是**虚调用**，所以「`Start()` 早退了」根本兜不住 ——
空指针上的虚调用要读 `[0 + 槽位]`，就是访问违例本身。用一个复刻同一形状的探针
（把 `_bodyMorph` 换成纯虚接口的空指针）编译运行：

```text
没有判空 -> exit 0xC0000005 (STATUS_ACCESS_VIOLATION)
加了判空 -> exit 0
```

> **反向验证**（照抄即可复现，两处都试过）：
>
> 1. 把工作树里新加的判空**删掉**（补丁文件不动），`plugin_patches.py check` 会点名：
>    `MorphSyncTogether/0002-guard-the-refused-bodymorph-interface.patch is not applied`，退出码 1；
> 2. 把补丁文件里**任意一行上下文**改一个字，`plugin_patches.py verify` 会报
>    「does not apply to the pinned commit - the submodule was re-pinned without
>    rewriting the patch」，退出码 1（见 §33）。

---

## 38. 2026-09-27 场次：自定义种族的头看不见 —— 一行「为别人好」的上游回退

### 38.1 报告里那句最有用的话

issue 原文是「自定义种族带上自带头部 `.nif`/`.tri` 时头是隐形的」，
但真正定位问题的是**下一句**：

> Nolvus v6 uses custom meshes .nif/.tri for vanilla races (HPH High Poly Head and
> Expressive Facegen) so it probably isn't related to custom .nif or .tri files.
> It probably relates to using custom races? Because vanilla races with custom head
> meshes work fine

把这两句并起来看，变量**不是** `.nif`/`.tri`，**也不是**「自定义种族」这四个字，
而是「**这张脸是不是在本机生成的**」：

| 场景 | 头网格从哪来 | 磁盘上有对应 FaceGenData 吗 | 结果 |
| --- | --- | --- | --- |
| 原版种族 + 自定义头部网格 | race 自带的 `.nif` | **有**（原版就有烘焙资源） | 正常 |
| 自定义种族 + 自带头部网格 | race 自带的 `.nif` | **没有** | **头隐形** |
| 远端玩家的任意种族 | 网络传过来、本机重建 | **没有** | 受影响的那一批 |

报告者后面还给了致命的一条：**STR rfortier 1.7.1.42 上一切正常**。
版本边界一旦被说清楚，剩下的就是找「1.7.1 和 1.1.x 之间，谁动了这条路」。

### 38.2 一行代码，两个方向

1.7.1 的 `TiltedOnlineApp::Update()` 里有：

```cpp
// Every frame make sure we won't use preprocessed facegen
POINTER_SKYRIMSE(uint32_t, bUseFaceGenPreprocessedHeads, 378620);
*bUseFaceGenPreprocessedHeads = 0;
```

上游 PR **#764**（commit `7e09f8b4`，`Now respects bUseFaceGenPreprocessedHeads flag.`）
把这**两行删掉了**，PR 正文自己写了动机：

> Closes #92 Closes #448 ... fixes things like neck seams and black faces

v1.8.0 发布说明同样直说：

> Re-enabled `bUseFaceGenPreprocessedHeads`; this fixes occasional mismatched heads,
> face discoloration and facegen mods

也就是说**同一行开关，两个方向各有各的受害者**：

- 强制关掉（1.7.1 的做法）→ **没有烘焙资源的角色头是空的**（本 issue）；
- 保持打开（#764 之后）→ **有烘焙资源但脸被改过的角色**脖子接缝、黑脸、头部错配。

任何「二选一」的修法都必然得罪另一边，所以我们不能只是把那两行加回去。

### 38.3 版本边界必须自己验，不能凭「应该是」

判据不是「读到过 PR」，而是**每个 tag 是否含那个 commit**：

```powershell
foreach ($r in @('v1.0.40','v1.0.41','v1.1.0','v1.1.1','v1.1.5','HEAD','upstream/master','v1.7.1','v1.8.0')) {
  git merge-base --is-ancestor 7e09f8b4 $r
  if ($LASTEXITCODE -eq 0) { "$r CONTAINS the revert" } else { "$r does NOT contain the revert" }
}
```

结果：1.0.40 / 1.0.41 / 1.1.0 / 1.1.1 / 1.1.5 / HEAD / upstream master 与 dev / **v1.8.0 全为「含」**，
**只有 v1.7.1 是「不含」** —— 与「1.7.1 好、1.1.x 坏」**逐字吻合**。

> 这类判断**必须**用 `merge-base --is-ancestor` 逐个 ref 跑。
> 用「在某个版本的文件里 grep 一下」代替，会漏掉 revert 之后又被别的提交改回来的情况。

### 38.4 一次排错：为什么不是 tint 纹理那条路

我们本来就有 `FaceGenSystem`，而且它在**远端玩家**身上真的会跑，
所以第一反应是「那里的 tint 纹理生成坏了」。逐条对下来，**它不是原因**：

1. `FaceGenSystem::Update` 只在 `FaceGenComponent` 存在时才被调用，
   而那个组件由 `FaceGenSystem::Setup` 建立，后者在 `acTints.Entries.empty()` 时**直接返回**；
2. `Update` 还要求 `pActorBase->GetHeadPart(1)` **和**一个蒙版着色器（`NiMaskedShaderRTTI`）同时成立，
   两道门都过不去时它根本不执行；
3. 最要紧的是：**tint 是给已经存在的头部几何体贴图层**，而本 issue 是**头部节点压根没建出来**。
   一个画在不存在的东西上的 bug，不可能让头消失。

教训：**先确认「哪个阶段没跑」，再去查那个阶段的代码**。
现象是「看不见」时，不区分「没生成几何体」和「生成了但贴图是错的」，
就会在 tint 那条路上白查很久。

### 38.5 仓库里躺着一个专为此写的函数，却没人调用

`Code/client/Games/Skyrim/Actor.cpp` 里一直有：

```cpp
void Actor::QueueUpdate() noexcept
{
    auto* pSetting = INISettingCollection::Get()->GetSetting("bUseFaceGenPreprocessedHeads:General");
    const auto originalValue = pSetting->data;
    pSetting->data = 0;

    TP_THIS_FUNCTION(TQueueUpdate, void, Actor, bool);
    POINTER_SKYRIMSE(TQueueUpdate, QueueUpdate, 40255);

    TiltedPhoques::ThisCall(QueueUpdate, this, true);

    pSetting->data = originalValue;
}
```

**它把开关临时关掉、调用引擎更新、再放回去** —— 正是「按需强制一次运行时生成」。
但把 `v1.0.40` / `v1.0.41` / `v1.1.1` / `v1.1.5` / `HEAD` / `upstream/master` / `upstream/dev` / `v1.7.1` / `v1.8.0`
**全部 ref** 都 grep 过：**只有它自己的定义，没有任何调用点**。
它之所以变得没用，是因为当年选择了「每帧全局清零」这条路 ——
全局清零在的时候，谁都不需要按需调用它；
而 #764 把全局清零删掉之后，**没有任何东西想起它来**。

> 这就是「有实现、没接线」的典型：**死代码不会报错，也不会过期**。
> 它会在几年后看起来像「已经处理过了」，让下一个人以为这条路已经走通。

### 38.6 修法：把「清零」收窄到真正需要它的角色

判据是 `WaitingFor3D`：

- 它由 `OnCharacterSpawn` 在**外观来自网络**时挂上（`CharacterSpawnRequest`）；
- 它的 `SpawnRequest.FormId == GameId{}` 表示**这个 base 是在本机从缓冲区重建的**
  （`TESNPC::Create` 路径），也就是「磁盘上不可能有对应烘焙网格」；
- 它被移除的那一刻，正是引擎把 3D 建好的那一刻（`GetNiNode()` 非空）。

于是窗口开在「需要」与「建好」之间，**由这个组件自己推导**，不靠配对开关：

```cpp
// CharacterService::OnUpdate，每帧
UpdateRemoteFaceGenWindow();

// 两个生成路径上，在把 actor 交给引擎之前
SetRemoteFaceGenForce(true);
pActor = Actor::Create(pNpc);
```

三个容易写错的地方，当时都踩了一遍：

1. **顺序**：先 `Actor::Create` 再开窗是**错的** —— 身体要到**下一帧**才建，
   而读取这个开关的是那一帧的引擎代码。门禁因此专门查这个先后顺序；
2. **还原值**：必须**存原值再存回去**，不能写死成 `1`。
   这个 INI 不是我们拥有的，玩家自己关过就得保持关着；
3. **不要加超时**：一开始加了个「开窗时记录 15 秒截止」的兜底，随即发现
   它**对第二个才开始等待的角色是错的**（那个 actor 一来，截止时间早就过了，
   窗口在它第一帧就被关掉 —— 隐形的头又回来了）。
   最终**故意不加超时**：最坏情况（某个 actor 永远没建出 3D、开关一直关着）
   正好**就是 1.7.1 的行为**，也就是报告者说「好」的那个版本。

> 想清楚「最坏情况是否可接受」，比硬造一个「看起来更严谨」的兜底更重要 ——
> 那个兜底会引入一个更难查的、只在第二个玩家进服时出现的新 bug。

### 38.7 门禁

`Tools/Scripts/check_facegen_scope.py` —— 读源码前**先剥注释**
（否则 `TiltedOnlineApp.cpp` 那段解释「为什么删掉」的长注释会被当成「代码里还有」），
然后查：全局清零没回来；每个写这开关的地方都「先存后还原」；
窗口按「来自网络」定界；窗口在 `Actor::Create` **之前**打开；
每帧关窗；取 INI 前判空；**以及引擎那趟生成过程真的被请求过**（§38.8）。

**反向验证过十种改坏方式**，每一种都被它抓到（含 `FormId` 比较被反过来这一种 ——
它最初**逃过**了第一版门禁，因为那版只查标记、不查比较，见 §38.7）。

### 38.8 后续：这一版**没有修好**，因为「拨对开关」不等于「让读开关的代码跑起来」

`ca24d5ce` 发出去之后，报告者回报**依旧看不到队友的脸**。回头看，
§38.6 的四条（写入目标、写入时机、还原方式、窗口定界）**每一条都是对的**，
但整个修法**少了一半**：

> `bUseFaceGenPreprocessedHeads` 只决定引擎那趟**脸部生成过程内部**走哪条分支。
> 它**不会让那趟过程发生**。

客户端从头到尾**没有任何一处要求引擎为这个 actor 跑一趟生成**。于是：

- 引擎按自己的节奏去建头，**在开关已经被还原之后**才走到读开关那一行；
- 而 `UpdateRemoteFaceGenWindow()` 又在同一个 tick、`WaitingFor3D` 被摘掉的那一刻把开关放了回去。

**开关确实被按下过，但那趟会读它的过程要么跑在还原之后，要么根本没人叫它。**

#### 取证：引擎自己是怎么做这件事的

对 `SkyrimSE.exe`（1.5.97）反汇编，五条独立读数：

| 查证 | 地址 | 读数 |
| --- | --- | --- |
| 引擎自己的「要一张运行时脸」怎么写 | `0x14031c153` | `GetSetting` → 存 `+0x8` → 写 `0` → **调 `Actor::QueueUpdate`** → 还原 |
| `Actor::QueueUpdate` 的地址 | `0x140693110` | AE id **40255**，正是 `Actor.cpp:211` 里那个 |
| 生成过程在哪读开关 | `0x140363e38`、`0x140363e61` | `movzbl 0x141dea030`，在 `0x140363de0` 内 |
| 生成过程从哪进 | `0x140363de0` ← `0x140650df0`（AE 39395） | 而 `0x140650df0` **是 `QueueUpdate` 的直接被调方**（深度 1） |
| 上一版的写入目标 | `FindAddressById(411155)` = `base+0x301d758` | 与引擎 `mov 0x2d015c7(%rip),%rcx # 0x14301d758` 同一个单例 ⇒ **没写错** |

结论：**引擎自己的做法就是「按下开关 → 调 `Actor::QueueUpdate` → 还原」**，
和仓库里那个 `Actor::QueueUpdate()` 一模一样。而它**从来没有被任何地方调用过**
（v1.0.40/1.0.41/1.1.x/HEAD/upstream master/dev/v1.7.1/v1.8.0 全 grep 过，只有定义）。

#### 最难堪的地方：上一版**专门写了理由**说明为什么不接线它

`ca24d5ce` 的提交信息里有：

> It stayed dead rather than being wired up: it scopes the flag to a single engine call,
> but the engine builds the body over several frames, which is why WaitingFor3D is polled at all.
> Wiring it in would have wrapped the wrong window.

这段推理把「引擎多帧建身体」与「不能只调一次生成」当成了同一件事，但
**`QueueUpdate` 本身就是那趟生成过程的入口** —— 它跟多帧建身体不是互斥的两件事。

> **死代码不会报错，也会让人替它编出一个不接线它的理由。**
> 一个「有实现、没接线」的函数被翻出来之后，最危险的动作是**给它写一段解释它为什么该继续死着**：
> 那段话会在下一次排错时被当成「已经论证过了」，把真正的调用点继续挡在门外。
> 正确动作是：**去引擎里看它到底做什么**，再决定接不接。

#### 修法

在 `RunRemoteUpdates` 里、`WaitingFor3D` **还没被摘掉**的那一刻（窗口还开着），
对「外观来自网络」的 actor 补上这一趟请求：

```cpp
if (waitingFor3D.SpawnRequest.FormId == GameId{})
    pActor->QueueUpdate();
```

三处配套，每一处都是上一版同类判据的延伸：

1. **定界与窗口同源** —— 同样用 `SpawnRequest.FormId == GameId{}`，本地加载序里解析出来的 base 保留烘焙头；
2. **顺序** —— 必须在 `m_world.remove<WaitingFor3D>` **之前**，否则开关已还原，这趟生成又走烘焙分支；
   门禁因此专门查这个先后（`remove<WaitingFor3D>` 的位置 vs `QueueUpdate()` 的位置）；
3. **`Actor::QueueUpdate` 补判空** —— 它现在在活路径上，`INISettingCollection::Get()` 或 `GetSetting`
   返回空时原来会直接解引用；这个函数从没被调用过，所以这个空指针从没暴露。

#### 教训

**「把开关拨对」和「让读开关的那段代码跑起来」是两件事。**
上一版把全部注意力放在「写哪个地址、什么时候写、怎么还原」上，每一条都验得很干净，
却从没问过「谁会让引擎去读它」。

判断一个「状态开关」类修法是否完整，问三个问题：

1. 这个值**写在哪**？（上一版验了）
2. 谁**读**它，读的时候这个值是不是我想要的那个？（上一版**没验**）
3. 那个读者**什么时候被叫起来**，是我叫的还是它自己按别的节奏来的？（上一版**没验**）

第 2、3 问才是「修好了没有」的分界线。一个只在「写」这一侧做取证、且取证很扎实的修法，
看起来会比它实际的可信度高得多 —— 因为**证据的密度不等于证据覆盖了整条链**。

## 39. 2026-09-27 场次:OStimNG 前置化、1.5.x 的 6 字节指令、以及"装上就是死的"三个根因

> **锚点**:本节的修复分别落在
> `Code/plugins/patches/STRPluginMessagingAPI/0001-prefer-the-framework-transport.patch`、
> `Code/plugins/patches/OStimNG/0001-verify-the-patch-site-before-writing.patch`、
> `Code/plugins/patches/OStimTogether/0002-support-ostimng-7-5-1-and-name-the-missing-prerequisite.patch`,
> 以及新门禁 `Code/plugins/tools/check_ostimng_versions.py`。
> 查证用 `git log -- Code/plugins`。

### 39.1 "OStim Together 没激活"的根因:前置根本没装,不是配置错

**证据链(全部来自本机实机日志与安装目录,不是推断)**:

| 检查 | 命令/位置 | 结果 |
| --- | --- | --- |
| 插件是否加载 | `skse64.log` | **只有** `OStimTogether.dll`(handle 84),`OStim.dll` **一次都没出现** |
| OStim 是否安装 | MO2 的 `mods/`(21 个) | **没有任何 OStim/OSA/SexLab** |
| 游戏根目录 | `D:\game\SkyrimSE\Data\SKSE\Plugins\OStim.dll` | **不存在** |
| 插件自己的日志 | `OStimTogether.log` | `OStim.dll module not found` + 十余条 `unavailable`/`missing` |

关键在于**插件照常 READY**:`STRPM READY`、`PHASE SYNC TRANSPORT READY`、
`PAPYRUS CONSENT bridge READY` 全都打印了,而 OStim 相关的每个子系统都自行关闭。
玩家看到的就是"加载了但什么都没发生",日志里没有任何一条面向玩家的说明。

**这不是本仓库的缺陷,而是三处"不告知"**。修法按可见性排序:

1. `OStimBridge::Initialize` 在找不到 `OStim.dll` 时从 `error` 升为 `critical`,
   并直接写明要装 OStim Standalone、要确认 `OStim.dll` 与 `OStim.esp` 已启用;
2. FOMOD 向导把 OStim Together 从固定 `Recommended` 改为**依赖型**
   (`dependencyType`),按 `SKSE/Plugins/OStim.dll` 的 `Active`/`Inactive`/缺失
   分别给 Recommended / CouldBeUsable / NotUsable。这正是 schema 里
   `CouldBeUsable`("装了但没激活")存在的理由;
3. 中英双语描述都点名前置。

> **教训**:一个插件"每个子系统都优雅降级"时,**降级路径本身必须有人告诉用户**。
> 十二条 `warn` 加起来的信息量,不如一条指名道姓的 `critical`。

### 39.2 OStimNG 在 1.5.80/1.5.97 上会改坏一条 6 字节指令

`GameHooks.h` 三处 hook 用 `write_call<5>`/`write_branch<5>`,CommonLibSSE 的这两个
模板**只覆盖 5 字节**,且用 `disp32` 反推"原函数地址"。逐字节核对真实
`SkyrimSE.exe`(1.5.97):

| 站点 | id | SE 偏移 | 首字节 | 真实指令 | 结果 |
| --- | --- | --- | --- | --- | --- |
| IsThirdPerson | 36541 | `+0x132` | `E8` | `call rel32`(5B) | 安全 |
| **GetHeading** | 36541 | `+0x152` | `FF` | **`call [rax+0x520]`(6B)** | **改坏** |
| PackageStart | 36404 | `+0x47` | `E9` | `jmp rel32`(5B) | 安全 |

在 GetHeading 处:`FF 90 20 05 00 00` 是 6 字节间接调用,而 `write_call<5>` 只写 5 字节,
末尾那个 `00` 成为孤立尾巴;并且它把 `90 20 05 00` 当成 rel32,算出"原函数"
= `0x63a8e7`——**纯属巧合落在 `.text` 里**,所以 `check_patch_site_boundary`
(它只拒绝跨越指令边界的写)不会救,因为写进去的 5 字节本身没越界。

**修法**:每处先核对首字节是否等于期望 opcode,不符则跳过并 `warn`。已用真实二进制
验证:GetHeading 跳过,另两处照常安装。

> **教训**:`RELOCATION_ID` 只保证**函数入口**在地址库里有映射;它**不保证**
> "入口 + 硬编码偏移" 落在一条可覆盖的分支指令上。偏移是上游对某个具体版本的假设,
> 换版本必须重新逐字节核对。

### 39.3 "1.5.x 到 1.7.x"从声称变成门禁

`REL::ID()` 在 id 缺失时**不是软失败**:CommonLibSSE-NG 的 `REL/IDDB.cpp` 走
`report_id_lookup_failure`,加载即中止。所以"支持某版本区间"等价于
"该区间的地址库里 id 覆盖齐全",而这是可以逐条核对的:

- `RELOCATION_ID(se, ae)` 的 SE id 必须在**每一个** pre-AE 地址库里(10 个);
- 其 AE id 必须在**每一个** AE 地址库里(14 个)。

`check_ostimng_versions.py` 做这件事,当前 33 个站点全通过,并已反向验证
(改一个不存在的 id → exit 1 并指名文件行号)。

> **为什么两个 family 都要查**:只在 1.5.97 上通过、在 1.6.1170 上中止,正是这个门禁
> 要拦的回归。只查一个版本会**以通过的形式掩盖**另一个版本的加载即崩。

### 39.4 七个插件的同步"装上就是死的":门面只看 ini,而 ini 指向一条死路

§32.4 已经查明链路,本轮用**实机日志**再次确认:

- `STRPluginMessagingBridge.log` 反复打印
  `receive resolver: TransportService RTTI type descriptor not uniquely resolved`;
- `OStimTogether.log`:`backend=str-bridge bridgeAvailable=1 bridgeActive=1 knownPeers=0`。

即:桥接的接收端在 `SkyrimSE.exe` **自己的分配区**里找 `TransportService` 的 RTTI,
而本框架的 `TransportService` 住在独立模块,解析器永不就绪 ⇒ 每次 `send()`
`kNotConnected` ⇒ **静默失效**。

**修法(本轮新增,§32.6 当时列为"未决")**:给门面打补丁,在按 ini 加载模块**之前**
先探测**已加载**的框架运行时。关键约束:

> **只用 `GetModuleHandleW`,绝不用 `LoadLibraryW`。**

因为两个运行时是不同 ABI 编译的,且任何安装后**两个 DLL 都躺在游戏根目录**;
盲目 `LoadLibraryW` 会把跨 ABI 映像拉进进程——这正是 §32.6 证否"默认改回
`SkyrimTogetherRuntime.dll`"的理由。探测不到时行为与原先完全一致,所以
"未修改的 STR Reborn 服务器 + 独立桥接"场景不受影响。

该行为由 `check_transport_compat.py` 的新断言绑定:文档必须描述"已加载运行时探测",
补丁必须同时提到两个运行时名,**且新增代码里不得出现 `LoadLibraryW`**(只看新增行、
且剥掉注释,否则解释"为什么不用它"的注释会误报)。三种回归都反向验证过。

### 39.5 OStimNG 作为前置进入仓库结构:源码进来,256 MB 资源不进来

`plugins/OStimNG` 是第八个子模块,登记在 `prerequisites` 而**不是** `plugins`:

- 进 `plugins` 会把它送进 staging 流水线,而它 `payload` 为空 ⇒
  `Add-PluginPayload.ps1` 直接 `throw`("staged plugin is empty"),打包全断;
- 它不是随包插件,是**前置**:玩家自己装 OStim,本仓库只负责钉版本、可打补丁、可校验。

**快照只备份代码**:`snapshotScope` 显式声明只备份 `skse/`、`flash/` 等
(703 个文件 2.4 MB),而不是 2985 个文件 256 MB。是否备份由"本项目是否消费它"决定,
不由体积阈值决定。

**顺带修掉一个不一致**:`plugin_snapshot.py` 里 `snapshot` 用 ".git 存在"、
`verify` 用 "CMakeLists.txt 存在" 判断子模块是否检出。OStimNG 的项目在
`skse/CMakeLists.txt`,于是 `verify` 永远报"未检出"。现统一为
`is_checked_out()`——子模块项目不总在根目录。

> **教训**:同一份清单被两个函数用**两种写法**判断同一件事,就是一次迟早会发生的
> 假失败。判断"子模块在不在"不该假设它的内部布局。

### 39.6 本轮新增的门禁与反向验证

| 门禁 | 拦什么 | 反向验证 |
| --- | --- | --- |
| `check_ostimng_versions.py` | id 缺失导致加载即中止 | 改一个 id → exit 1 |
| `check_transport_compat.py`(扩展) | 文档与补丁脱节、误用 `LoadLibraryW` | 改文档 / 改补丁 → 均 exit 1 |
| `plugin_snapshot.py`(`snapshotScope`) | 把 256 MB 资源写进历史 | `verify` 认 8 个子模块 |
| `plugin_patches.py`(含 prerequisites) | 前置的补丁不被应用 | 7 个补丁全 verify |

## 40. 2026-09-27 场次:三条流水线全红,而本地 13 个门禁全绿

v1.1.6 打 tag 后 `Build windows` / `Release build` / `Playable Skyrim Together Build`
三条都死在 `Build the OStimNG prerequisite`,`Plugin gates` 死在
`Verify the plugin source snapshots`。**四个失败,两处根因,本地一个都复现不了** ——
这就是本节的主题:门禁量的是不是它以为自己量的东西。

### 40.1 一处笔误红三条:PowerShell 先解析整块,再执行

```powershell
Write-Host "COMMONLIB_SSE_FOLDER=$env:COMMONLIB_SSE_FOLDER      ← 没有收尾引号
Write-Host "COMMONLIB_SSE_FOLDER=$env:COMMONLIB_SSE_FOLDER"     ← 完整的那一行在下一行
```

PowerShell **不是逐行解析**的:它先把整个脚本解析成 AST,再执行。一个未闭合的字符串
让**整块**语法失败,于是这一步在跑任何命令之前就退出 1 —— 日志里连一行 `cmake`
输出都没有,annotation 只有 `Process completed with exit code 1.`。

而 `windows.yml` 是被 `release.yml` 与 `windows-playable-build.yml` 通过 `uses:` 复用的
reusable workflow,**同一段 `run:` 被三条流水线共用**,所以一处笔误三条全红。

> **教训**:`run:` 块里的 PowerShell 是**只在 runner 上才存在**的代码。本地没有任何门禁
> 读 `.github/workflows/`,于是它能带着一个语法错误一路通过 review、通过 tag、
> 直到把三条流水线一起打红。

**修法**:新增 `check_workflow_shell.py`,用 PowerShell 自己的解析器
(`[Parser]::ParseInput`)校验 `.github/workflows/` 里**全部 35 个 pwsh 块**。
GitHub 会在 PowerShell 看到块之前替换 `${{ ... }}`,而那不是合法 PowerShell,所以先替换成
字面量再解析 —— 否则会误报 4 个块。**反向验证**:把那个收尾引号删掉,门禁精确报出
文件、步骤名与行号并 exit 1。

### 40.2 断言了一条**在钉住的提交里已经被删掉**的路径

```powershell
$commonlib = 'plugins/OStimNG/skse/extern/CommonLibSSE-NG'
if (-not (Test-Path (Join-Path $commonlib 'CMakeLists.txt'))) { throw ... }
```

OStimNG 的 `skse/CMakeLists.txt` 第 67 行 `add_subdirectory("$ENV{COMMONLIB_SSE_FOLDER}")`,
第 68 行 `get_target_property(COMMONLIB_SRC_DIR CommonLibSSE SOURCE_DIR)`,
第 91 行 `include(${COMMONLIB_SRC_DIR}/cmake/CommonLibSSE.cmake)` —— 它要的是
**CommonLibSSE-NG 的源码检出**,不是插件链的那份 vcpkg 包(包里没有 `cmake/CommonLibSSE.cmake`, 
而且 `SOURCE_DIR` 是构建树属性)。

这条路径过去由 OStimNG 自己的 gitlink 提供,而 `f20db82`("updated compiler",
OStimNG 转向 vcpkg 的那一次)**删掉了 `skse/extern/CommonLibSSE-NG`**。
我们钉的 `3954683b` 在它之后 —— 于是这条断言在钉住的树里**必然抛**。

**修法**:把修订钉在 `Code/plugins/plugins.json` 的 `commonLibSseNg`,紧挨着它必须匹配的
OStimNG pin,由 CI 读取并 `fetch`。选 `v8.1.0`(`3c0f5a87`)有据:OStimNG 的 `126fc52` 标题就是
"fixed build configs, skseapi definition and trampoline for **commonlib 8.1.0**";
钉住的源码用到 `SKSE_EXPORT`、`GetTrampoline`、`SkyrimVM::GetVMRuntimeData()`、
`MessageBoxMenu::QueueMessage`、`RE::GFxValue`、`RE::GPtr` —— 逐个在 `v8.1.0` 的
`SKSE/Interfaces.h`、`SKSE/Trampoline.h`、`RE/S/SkyrimVM.h`、`RE/M/MessageBoxMenu.h` 里核对过。

### 40.3 同一步里另外四个**也会**让它失败的配置错误

修好上面两处之后这一步仍然构建不了 —— 它们是独立的四处:

| 原来 | 为什么不行 | 现在 |
| --- | --- | --- |
| `cmake -S ... -B plugins/OStimNG/build -A x64` | 手写 `-A` 生成 VS 工程,而 OStimNG 的预设用 **Ninja**;`-A` 与预设不能共存 | `cmake -S plugins/OStimNG/skse --preset release` |
| 同上 | 没有 `VCPKG_OVERLAY_PORTS`。OStimNG 依赖 `clib-util`,而它在**上游 vcpkg 里不存在**(pinned baseline 与 master 的 `ports/clib-util/vcpkg.json` 都是 404),只有 `skse/cmake/ports/` 那份 overlay 有 | 预设自带 `${sourceDir}/cmake/ports/` |
| 构建 `plugins/OStimNG/build` | 预设的 `binaryDir` 是 `${sourceDir}/build/${presetName}`,实际产物在 `skse/build/release` | 构建前断言 `CMakeCache.txt` 就在那棵树里 |
| `Get-ChildItem 'plugins/*/vcpkg.json'` | **只找一层**。OStimNG 的 manifest 在 `plugins/OStimNG/skse/`,于是它 pin 的 `e3ed4186` 从不被 fetch;而 vcpkg 解析 `builtin-baseline` 只跑 `git show <sha>:versions/baseline.json`、**没有 fetch 回退**,在 `--depth 1` 的 vcpkg 里必然 `failed to \`git show\` versions/baseline.json`(§29.5 记的正是这个错) | 改为 `Get-ChildItem -Path 'plugins' -Recurse -Filter 'vcpkg.json'`;overlay ports 没有 `builtin-baseline`,被 `if ($pinned -and $pinned.Value)` 跳过 |

最后一条**和 §39.5 是同一个形状**:"子模块的项目在哪"又一次被假设成了一层深。
§29.5 当时把"从 manifest 反推 baseline"设计成抗重钉的,但反推本身**只找一层**,
于是 OStimNG 一来它就漏了 —— 抗住了"值会变",没抗住"位置会变"。

另外补上 `extern/openvr`:CommonLibSSE-NG 在 `ENABLE_SKYRIM_VR` 打开时把
`extern/openvr/headers` 加进包含路径、并链接 `extern/openvr/lib/win64/openvr_api.lib`,
而 OStimNG 第 62–64 行强制打开 SE/AE/VR 三个运行时。它是 CommonLibSSE-NG 自己的子模块。

**不能用 `submodule update`**:上面那个检出是 `git init` 加一次单提交 `fetch` 出来的,
没有记录子模块 URL,`update` 会以 `no url found for submodule path` 失败。改为从树本身读
出 gitlink sha(`git ls-tree HEAD extern/openvr`)和 URL(`.gitmodules` 的
`submodule.openvr.url`)再直接 `fetch` —— 同样的信息,但不依赖上层仓库是怎么克隆的。

> **教训**:预设不是"另一种写法",它是**构建输入的一部分**。手写 `-A x64` 看起来等价,
> 实际丢掉了生成器、triplet、静态 CRT 与 overlay ports 四样东西。

### 40.4 备份少了一个文件:嵌套 `.gitignore` 优先于根目录的否定规则

```text
snapshots/plugins/OStimNG/.gitignore:553:gfxfontlib.swf
```

快照**原样保留插件的 `.gitignore`**(它本身是被备份的源码,且门禁要求它与钉住提交逐字节一致),
而嵌套 `.gitignore` **优先于**根目录 `.gitignore` 里的 `!/snapshots/**`。于是
`gfxfontlib.swf`(111 KB,在 `flash/` 下)**在本地存在、却从未进过 git**:

```text
本地  verify → 看工作区 → 文件在 → 绿
CI    verify → 干净检出 → 文件不在 → 红
```

**同一个门禁在两台机器上量的是两件事**,而这一整类问题都是同一个形状。

**修法**:`snapshot` 用 `git add --force` 暂存 —— 这是 git 里**唯一**能无视 ignore 规则的
机制 —— 并把被覆盖的规则记进 `UPSTREAM.json` 的 `ignoredRulesOverridden`;
`verify` 改问 **"git 是否记录了这个文件"**(`git ls-files`)而不是"磁盘上有没有"。

> **教训**:一个备份目录里,"文件在磁盘上"**不是**正确的不变量。备份的定义是
> "它在别人的 clone 里也在",所以唯一该问的是 git 记录了什么。

**反向验证**:① 把文件从索引里移除、留在磁盘上(原始 bug)→ 新门禁 exit 1 并指名文件;
② 把文件从磁盘删掉(CI 现象)→ 旧检查也会 exit 1;③ 恢复后两者都绿。

### 40.5 顺带修掉三处同类问题

- `plugin_snapshot.py update` 仍在用"根目录有没有 `CMakeLists.txt`"判断子模块是否检出,
  于是 OStimNG 每次都被静默跳过(`not checked out, skipped`)—— 而这正是重钉时必须覆盖的
  那一个前置。§39.5 说"统一为 `is_checked_out()`",但当时**只改了 `snapshot` 与 `verify`**,
  第三个调用点漏了。这是同一个教训的第二次出现:**同一件事有两个写法,就有一处会漏**。
- `UPSTREAM.json` 的 `upstream` 读 `.git/config`(`submodule.<name>.url`),而该条目**只在
  子模块初始化后**才存在,于是 OStimNG 那份记成了空字符串。改读 `.gitmodules` —— 受版本
  控制的普通内容,在任何 clone 里都一样。
- `snapshot` 每次重写 8 份 `UPSTREAM.json`,把 `ignoredRulesOverridden` 写成空数组,
  产生 8 份无意义 diff。改为只在非空时写入。

### 40.6 本轮新增的门禁与反向验证

| 门禁 | 拦什么 | 反向验证 |
| --- | --- | --- |
| `check_workflow_shell.py` | `.github/workflows/` 里任何 pwsh 块语法不通过 | 删掉收尾引号 → 精确到文件/步骤/行号,exit 1 |
| `check_ostimng_build.py` | 清单没钉 CommonLibSSE-NG,或 CI 步骤不再读它 | 删 pin / 换成 tag / 改字段名 → 三种都 exit 1 |
| `plugin_snapshot.py`(verify) | 快照文件在磁盘上但**未被 git 记录** | 从索引移除 → exit 1 |

### 40.7 这一轮真正的教训

四个失败,本地 13 个门禁**全部通过**。两处根因都属于同一类:
**门禁量的东西和它以为自己量的东西不是同一个**。

| 门禁 | 以为在量 | 实际在量 |
| --- | --- | --- |
| 本地"跑一遍 CI 的步骤" | 那段 PowerShell 能跑 | 它**能不能被解析**(没人解析过它) |
| `plugin_snapshot.py verify` | 备份里有这些文件 | **工作区**里有这些文件 |
| (不存在) | OStimNG 能被构建 | 断言的那条路径**是不是真的存在** |

所以补的不只是两个修复,而是两个门禁:一个**解析**工作流里的脚本,
一个**绑定**清单与 CI 步骤。它们各自对应上表的一行。

## 41. 2026-09-28 场次:引导页里 OStim Together 是灰的 —— 一条永远不可能成立的判断

> **锚点**:本节的修复落在 `Code/plugins/plugins.json`(`detectFile`)、
> `Code/plugins/tools/merge_fomod.py`(门禁)、
> `Code/plugins/patches/OStimTogether/0002-...patch`(拒绝文案)、
> 以及新门禁 `Code/plugins/tools/check_ostim_support_message.py`。

### 41.1 根因:MO2 的 `fileDependency` 只认插件文件

**现象**:MO2 引导页里 `OStim Together` **灰的、点不动**,而且
**先把前置装好也照样是灰的**。

MO2 自己的日志把原因写全了(`mo_interface.log`):

```text
A dependency on non esp/esm/esl SKSE/Plugins/OStim.dll will always find it as missing.
[fomodinstallerdialog.cpp:392] "缺少前置：文件 'SKSE/Plugins/OStim.dll' 应为 已激活，当前为 缺少！"
```

§39.1 的第 2 条修法把向导改成"依赖型",但 `detectFile` 写的是
`SKSE/Plugins/OStim.dll`。**MO2 只把 `fileDependency` 拿去比对插件列表**
(esp/esm/esl),非插件文件它**永远判为缺失**——于是 `Active` 与 `Inactive`
两个 pattern 都不可能命中,`defaultType NotUsable` 永远生效。

关键在于:**这条判断跟"前置装没装"完全无关**,所以"提前装好还是灰的"
正是它该有的表现,不是玩家操作错。

**本机反证**(三样全齐,向导照旧报缺):

| 检查 | 位置 | 结果 |
| --- | --- | --- |
| 前置是否安装 | MO2 `mods/OStim Standalone/` | 已装,`OStim.dll` 7.5.1.2 |
| 插件是否加载 | `skse64.log` | `OStim.dll ... loaded correctly (handle 85)` |
| 插件是否激活 | `profiles/Default/plugins.txt` | `*OStim.esp` 已激活 |

**修法**:`detectFile` 改指 `OStim.esp`(前置真实携带的插件文件),
`merge_fomod.py` 增加门禁拒绝非 esp/esm/esl 的 `detectFile`,
反向验证:改回 `OStim.dll` → exit 1 并指名 `plugins.json`。

> **教训**:`fileDependency` 的 `state` 只对插件文件有定义。"检测前置是否安装"
> 这件事在 FOMOD 里**做不到通用文件级判断**——只能挑前置携带的那个 esp。
> 一个"永远不成立"的条件比"没有条件"更糟:它看起来像在保护用户。

### 41.2 拒绝信息比代码晚了一个版本

`SelectGraphLayout` 接受 `7.5.1.x` 之后(补丁 0002),拒绝路径打印的仍是
`supported versions are 7.4.0.3 (7.4c) and 7.5.0.2 (7.5b)` ——
一个**真被拒绝**的玩家,读到的建议比他手上的版本还旧。

拒绝信息是这条路径上**唯一**会被读到的东西,却没有任何东西与之绑定:
编译器不管散文,文档也不描述选择器。

**修法**:补丁 0002 内把文案改为 `7.4.0.3 (7.4c), 7.5.0.2 (7.5b) and 7.5.1.x`,
并新增 `check_ostim_support_message.py`,把**文案与选择器双向绑死**。

> **改在哪很重要**:`plugins/OStimTogether` 是子模块,CI 用
> `git submodule update --init --force` 检出,**工作树里的改动会被丢弃**。
> 这条文案属于上游固定内容,只能改**补丁文件**。验证方式是把补丁
> `revert` 掉,确认子模块 `git status` 为空——空才说明"本地看到的改动
> 全部来自补丁",而不是某次手工编辑的残留。

**反向验证**(四种漂移,每种都必须 exit 1):

| 漂移 | 结果 |
| --- | --- |
| 文案漏掉 7.5.1.x(**本次的真实缺陷**) | exit 1,指名"选择器接受 7.5.1.x,文案没写" |
| 选择器不再接受 7.5.1.x,文案仍承诺 | exit 1 |
| 文案写了一个没人接受的版本 | exit 1 |
| 拒绝路径完全不写版本 | exit 1 |

> **教训**:一条"只给人看"的字符串,如果它的内容**必须与代码一致**,
> 那它就是可校验的声明,应该由门禁绑定——否则它只会越来越旧。

### 41.3 本轮新增的门禁与反向验证

| 门禁 | 拦什么 | 反向验证 |
| --- | --- | --- |
| `merge_fomod.py`(扩展) | 非 esp/esm/esl 的 `detectFile` 让入口永久灰掉 | 改回 `OStim.dll` → exit 1 |
| `check_ostim_support_message.py` | 拒绝信息与选择器脱节 | 四种漂移 → 均 exit 1 |

### 41.4 顺带查清:为什么"用 OStimNG 源码编译"解决不了这个问题

用户提出的方案是"从 `D:\sktest\OStimNG` 编译出前置,不依赖前置 mod"。
逐条核对后**三条都不成立**,记在这里免得再走一遍:

1. **编译产物就是 `OStim.dll`**,而它**已经在位且已加载**(见 §41.1 的表)。
   灰掉的原因从来不是它不在。
2. **运行时依赖删不掉**:`OStimTogether` 是 OStim 的 API 消费者,
   源码里 16 处 `GetModuleHandleW(L"OStim.dll")`。
   "不依赖前置"等于把 OStim 重写进 OStimTogether,那是另一个项目。
3. **本机编不了**:`OStimNG/skse/CMakeLists.txt` 要求
   `COMMONLIB_SSE_FOLDER` + `VCPKG_ROOT` + MSVC。实测本机
   **无 `cl.exe`、无 VC 工具集、无 vcpkg**,且检出里
   `skse/vendor`、`skse/extern/CommonLibSSE-NG`、`skse/imgui` 都不存在。

顺带确认 `OStimNG/data/OStim.esp` 与已安装的 `OStim.esp`
**SHA256 完全相同**——源码里那份 esp 就是正品,不需要额外处理。

## 42. 2026-09-28 场次:1.6.1170 上 esp 与 logo 都在,但 F2 菜单打不开

> **锚点**:本节的修复落在 `Code/skse_bootstrap/main.cpp`,
> 新门禁 `Code/plugins/tools/check_skse_bootstrap.py`。

### 42.1 根因:那个 DLL 一次都没被加载过

**现象**:游戏 1.6.1170 + SKSE 2.2.6。Creations 菜单能看到 esp,主菜单有 logo,
**按 F2 毫无反应**。

`skse64.log` 里一行定案:

```text
plugin SkyrimTogetherSKSE.dll (00000000  00000000) no version data 0 (handle 0)
```

**`handle 0` 表示 SKSE 从未把它加进加载列表**。对照同一份日志里正常加载的插件:

```text
plugin AnimSyncTogether.dll (00000001 AnimSyncTogether 000C0010) loaded correctly (handle 1)
plugin OStim.dll (00000001 OStim 07050012) loaded correctly (handle 85)
```

正常的是 `loaded correctly (handle N)`,`N >= 1`。

### 42.2 为什么"没加载"会表现成"装好了但没反应"

`SkyrimTogetherSKSE.dll` **不是**联机运行时,它是一个 **bootstrap**:
负责把 `SkyrimTogetherRuntime.dll` 部署到游戏根目录,再 `LoadLibrary` 起来
(`Code/skse_bootstrap/main.cpp`)。所以链条是:

```text
SKSE 不加载 bootstrap
  -> SKSEPlugin_Load 从不执行
  -> 运行时从不被部署
  -> F2 菜单永不出现
```

而 esp 与主菜单 logo 是**普通 mod 文件**,由游戏自己读,与 SKSE 无关 ——
所以"看起来装好了"。这个"一半正常"的表现正是最容易被误诊成
"版本不兼容"或"mod 冲突"的形状。

### 42.3 SKSE 2.2.x 没有 `SKSEPlugin_Query` 回退

SKSE 自己的源码(`ianpatt/skse64`,`v2.2.6` 标签,`skse64/PluginManager.cpp`)
`ScanPlugins`:

```cpp
auto * version = (const SKSEPluginVersionData *)GetResourceLibraryProcAddress(
    resourceHandle, "SKSEPlugin_Version");
if(version) { ... CheckPluginCompatibility ... m_plugins.push_back(plugin); }
else        { LogPluginLoadError(plugin, "no version data", 0, false); }
```

**只看 `SKSEPlugin_Version` 这一个导出名**;缺失即跳过,**没有任何 `Query` 回退**。
而本 DLL 在 v1.1.7 里的导出表(实测 PE 导出表)只有两个:

```text
SKSEPlugin_Load
SKSEPlugin_Query
SKSEPlugin_Version   *** ABSENT ***
```

**为什么本地测不出来**:SKSE 2.0.x(`v2.0.20`)的 `ScanPlugins` 里
**根本没有 `SKSEPlugin_Version` 这个字符串**,它仍然 `GetProcAddress(handle,
"SKSEPlugin_Query")`。所以同一个 DLL 在 1.5.97 上一直是好的 ——
**这个缺陷只在 1.6.x 上存在,而本地跑的是 1.5.97**。

### 42.4 同一份包里,只有它没有这个导出

把 v1.1.7 包里每个 SKSE 插件 DLL 的导出表读出来对照:

| DLL | `SKSEPlugin_Version` | 1.6.1170 上结果 |
| --- | --- | --- |
| AnimSyncTogether | 有(版本无关) | loaded correctly |
| DAVSyncTogether | 有(版本无关) | loaded correctly |
| IEDSyncTogether | 有(版本无关) | loaded correctly |
| MorphSyncTogether | 有(版本无关) | loaded correctly |
| OStimTogether | 有(版本无关) | loaded correctly |
| STRPluginMessagingAPI | 有(显式列表) | loaded correctly |
| TradeTogether | 有(版本无关) | loaded correctly |
| **SkyrimTogetherSKSE.dll** | **无** | **`no version data`(handle 0)** |

八个插件都声明了版本无关,**只有框架自己的 bootstrap 漏了**。

### 42.5 修法,以及为什么选"版本无关"而不是版本列表

补丁在 `Code/skse_bootstrap/main.cpp` 增加导出,声明:

```cpp
kVersionIndependent_Signatures       // 1 << 1
kVersionIndependentEx_NoStructUse    // 1 << 0
```

两个标志**缺一不可**,而且失败症状与本次完全相同(都是"没有 F2"):

| 缺哪个 | SKSE 的回答 |
| --- | --- |
| `NoStructUse`(且无 `StructsPost629`) | `disabled, only compatible with versions earlier than 1.6.629` |
| `Signatures`(且无 `AddressLibraryPostAE`) | 1.7.99+ 要求 `AddressLibraryV5`,答 `must be recompiled for new address library` |

**为什么不用显式版本列表**:本 DLL 不解析任何引擎地址、不碰任何游戏结构,
只做文件部署和 `LoadLibrary`。列一份表意味着**每出一版游戏都要改代码重编译**,
而且会**误拒本包本来就支持、只是没人想起来加的运行时**
(包里实际带着 `versionlib-1-6-1179-0.bin`、`versionlib-1-6-659-0.bin`,
以及 1.6.678 Epic —— 都不在最初的列表里)。真正的版本门禁在
`SkyrimTogetherRuntime.dll` 内部:它通过地址库解析地址,没有对应库就拒绝。
八个同包插件也都是这么声明的。

### 42.6 验证:不是"看着对",是编译出来读导出表

本机没有 MSVC,用 w64devkit 的 g++ 做了两件事:

1. **语法与静态断言**:`-std=c++20 -Wall -Wextra -fsyntax-only` → exit 0。
   过程中编译器**抓到了我自己写的两个真错**:对非 `constexpr` 的导出变量做
   `static_assert`(已改为断言具名常量)。
2. **编成真 DLL,解出导出表与数据**:

```text
exports: SKSEPlugin_Load, SKSEPlugin_Query, SKSEPlugin_Version
dataVersion           = 1
name                  = 'SkyrimTogether'
versionIndependence   = 0x2   Signatures=True
versionIndependenceEx = 0x1   NoStructUse=True
```

再按 SKSE 2.2.6 的 `CheckPluginCompatibility` 逐条复演,全部 PASS,
结论为 SKSE 会记 `loaded correctly`。

> **教训**:一个"加载失败但不报错"的插件,症状会和"版本不兼容"、
> "mod 冲突"、"配置错"全都长得一样。**先读 `handle` 号**:
> `handle 0` 是"SKSE 根本没碰它",不是"它跑了但没干活"。

### 42.7 本轮新增的门禁与反向验证

| 门禁 | 拦什么 | 反向验证 |
| --- | --- | --- |
| `check_skse_bootstrap.py` | 导出缺失/被修饰/结构体换序/标志不全 | 8 种失败模式 → 均 exit 1 |

八种模式包含:导出整个缺失(**本次真实缺陷**)、导出落在命名空间里(名字被修饰)、
`versionIndependence` 与 `Ex` 换序、两个标志都缺、只缺 `Signatures`、
只缺 `NoStructUse`、`SKSEPlugin_Query` 不再导出(会让 1.5.97 直接失效)、
**把标志只写在注释里**(门禁先剥注释,专门拦这种"看起来设了"的假通过)。

---

## 43. 2026-09-28 场次:修好 F2 之后同步依然是死的 —— 同一类缺陷的另两副面孔

§42 修完后,1.6.1170 上 `SkyrimTogetherSKSE.dll` 能加载了。但**插件层的同步
仍然是死的**,桥接日志里反复刷同一行、永不成功:

```text
send resolver pass failed in 4453 ms; waiting for mapped STR 1.8.0 runtime
ProxyResolver waiting: spans=20 transport=no loaded=no unloaded=no
```

### 43.1 先判因果:这一行是"下游症状",不是独立缺陷

链条逐段取证:

| 环节 | 证据 |
| --- | --- |
| bootstrap 被 SKSE 跳过 | `no version data 0 (handle 0)`(§42) |
| → 运行时从未部署 | `skse64.log` 里**没有** `SkyrimTogetherRuntime` 任何一行 |
| → 门面找不到框架运行时 | `GetLoadedFrameworkRuntime()` 用 `GetModuleHandleW` 探测两个名字,均未加载 → 返回 `nullptr` |
| → 门面退回加载 bridge | `STRPluginMessagingAPIRuntime.cpp:908` 的三元表达式 |
| → bridge 扫不到 `TransportService` | 它只扫 `SkyrimSE.exe` 自己的分配,而框架的 `TransportService` 在**另一个模块**里 |

所以这一行**不需要单独修**:bootstrap 修好后运行时会被部署,门面走框架传输,
bridge 根本不会被用到。**先修上游,再回头确认下游是否自愈**——这次它自愈了。

### 43.2 但顺着"同包插件准入"往下查,又揪出两条

把 SKSE 两族的准入规则写成回放脚本,对**每个手写插件 × 每个随包运行时**跑一遍,
得到下面这张表(✅ = 加载,❌ = 被跳过):

| 插件 | 1.5.97 | 1.6.317 | 1.6.640 | 1.6.1130 | 1.6.1170 | 1.6.1179 | 1.7.99 | 1.7.104 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 六个 CommonLib 伴侣插件 | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| `STRPluginMessagingAPI` | ✅ | ❌ | ❌ | ❌ | ✅ | ❌ | ❌ | ❌ |
| `STRPluginMessagingBridge` | ❌ | ❌ | ❌ | ❌ | ✅ | ❌ | ❌ | ❌ |
| `SkyrimTogetherSKSE`(修复前) | ✅ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ | ❌ |

**复演脚本第一版是错的**,值得记下来:我先按 SKSE `master` 的规则写,结果
六个**真实日志里明明加载成功**的插件被判 `must be recompiled for new address
library`。原因是 master 多了一条 `GetBuildTime()` 启发式
(`1748217600` = 2025-05-26 之后的构建豁免),而**用户跑的 v2.2.6 里根本没有
这段代码**。**拿 master 的规则去判一个旧版本 bin 的行为,会造出一整片假阳性。**

### 43.3 缺陷 A:`STRPluginMessagingBridge.dll` 没有 `SKSEPlugin_Query`

SKSE 2.0.x 的 `ScanPlugins`(`v2.0.20`,第 269-311 行)要求**两个都在**:

```cpp
plugin.query = GetProcAddress(plugin.handle, "SKSEPlugin_Query");
plugin.load  = GetProcAddress(plugin.handle, "SKSEPlugin_Load");
if(plugin.query && plugin.load) { ... } else {
    _MESSAGE("plugin %s does not appear to be an SKSE plugin", pluginPath.c_str());
}
```

本仓库**自己 1.5.97 机器的日志里就有这行**,只是当时没被当成缺陷:

```text
plugin D:\game\SkyrimSE\Data\SKSE\Plugins\\STRPluginMessagingBridge.dll does not appear to be an SKSE plugin
```

导出表实测对比(从 v1.1.7 发布包解出):

```text
DLL                          Query   Load   Version   结论
STRPluginMessagingAPI         True  True     True    ok
STRPluginMessagingBridge     False  True     True    1.5.97 上被跳过
SkyrimTogetherSKSE            True  True    False    1.6.x/1.7.x 上被跳过
```

### 43.4 缺陷 B:三个门面的 `compatibleVersions` 只写了 `1.6.1170`

另一条更隐蔽。三个门面 DLL 声明 `versionIndependence = 0`,而
`compatibleVersions` 里只有 `{ kRuntime_1_6_1170, 0 }`。SKSE 的规则是:

```cpp
bool versionIndependent = version.versionIndependence
                        & (kVersionIndependent_AddressLibraryPostAE
                         | kVersionIndependent_Signatures);
if(!versionIndependent) {
    // 与 RUNTIME_VERSION 精确比对,不是"同一大版本"
}
```

`versionIndependence = 0` → `versionIndependent = false` → **精确比对**。
于是 `1.6.317 / 640 / 1130 / 1179` 与 `1.7.99 / 104`(本包**每一个都带了
地址库**)全部被判:

```text
disabled, incompatible with current version of the game
```

**为什么 1.6.1170 上是好的**:列表恰好命中它。**这个缺陷只有换一个游戏版本
才暴露** —— 本次会话第三次撞上"本地测不出"。

放大效应:门面是**所有伴侣插件取传输层的入口**(它们用 `LoadLibraryW`
按名加载)。门面在一条游戏线上被跳过 → 那条线上所有插件的同步一起失效。

### 43.5 修法

子模块只走补丁(`Code/plugins/patches/STRPluginMessagingAPI/0002-*.patch`):

1. 三个 DLL 改为**版本无关**:`kVersionIndependent_Signatures` +
   `kVersionIndependentEx_NoStructUse`;
2. bridge 与 diagnostic 补上 `SKSEPlugin_Query`(返回 `true`,与 API 门面一致);
3. 清掉 `kRuntime_1_6_1170` 列表与不再需要的前置声明。

**为什么版本无关是如实描述,而不是为了让门禁通过**:实测门面源码
**不包含任何游戏头文件**(`#include <RE/...>` 零处)、**不出现任何 `RE::`
类型**、**没有任何硬编码地址**(唯一的 `0x...` 是插件版本号常量)。它只做
签名/锚点扫描。列版本表反而会**误拒本包本来就支持的运行时**。

> **注意 `struct SKSEInterface;` 的坑**:把 `Query` 加进 bridge 时,我顺手删掉了
> 文件里的前置声明。编译器**当场拦下**——`SKSEPlugin_Load` 用的是**不带命名空间
> 的裸名** `const SKSEInterface*`。删掉就必须同时改 `Load` 的签名。
> 这类改动**必须过一遍真实编译器**,肉眼看"加了 Query 就对了"会漏。

### 43.6 新增门禁 `check_skse_entry_points.py`,以及它的两次自我返工

把 SKSE 两族的准入规则**回放**在每个手写插件 × 每个随包运行时上。

两个设计决定:

- **按"SKSE 看到的名字"分组,不按文件**。API 门面的版本数据在
  `...SKSEMetadata.cpp`、`Query`/`Load` 在 `...APIRuntime.cpp`,按文件分组会
  把同一个 DLL 拆成两半,各自"缺一半导出"而假报错。
- **运行时列表从随包的 `versionlib-*.bin` 推导**,不写死。新增一个地址库,
  门禁自动开始要求插件覆盖它。
- **先剥注释再匹配**。本次三条缺陷里有两条,旁边就写着**正确描述该规则的
  注释**;不剥注释会"照着说明通过"。

门禁自己写错过两次,都靠反向验证暴露:第一次把 `SKSEPlugin_Load`(它**不带
插件名字面量**)当成需要名字的导出,导致四个插件全报"缺 Load";第二次在
**版本无关**的插件上仍去比对版本列表——而 SKSE 在版本无关时**根本不读这个
列表**。

### 43.7 反向验证

| 场景 | 结果 |
| --- | --- |
| 还原 `STRPluginMessagingAPI` 补丁 | **FAILED (5)** —— 桥接缺 `Query`、三个列表不覆盖 |
| 把 bootstrap 的 `SKSEPlugin_Version` 改名(还原 §42 的缺陷) | **FAILED (6)** —— 含"no exported SKSEPlugin_Version" |
| 补丁就位 | **OK** —— 4 个手写插件、13 个随包运行时 |

### 43.8 顺带查清、确认无害的一项

客户端 AE 地址库覆盖度按运行时逐一统计(3082 个代码 id,**当时口径**;
后续上游 v1.8.3 同步删掉了 `AnimationExperiments.cpp`,该口径变为 **3063**,
见 §110.6):

| 运行时 | 覆盖 | 缺失 |
| --- | --- | --- |
| 1.5.97 | 3069/3082 | 13(已在 `Tools/missing_1_5_97_ids.txt` 记录) |
| 1.6.1170 | 3079/3082 | 3 |
| 1.6.1179 | 3078/3082 | 4 |
| 1.7.104 | 3079/3082 | 3 |

缺的 4 个全是 **RTTI 定位器**:`392519 NiObjectNET`、`394235 CreationClubMenu`、
`398437 NiParticleSystem`、`400159 CCallbackBase`。它们**只在 1.5.97 的
ae-to-se 映射表里存在**,在 1.6.1130+ 与 1.7.x 的库里没有 —— 正是
"本地能跑、玩家那边不行"的形状。

#### 别再去 `Tools/ida` 里翻这 4 个 id 了(2026-09-28 已排查)

项目里确实有 1.6.1170 的逆向产物,但**范围不含 RTTI**,原因是覆盖面而不是数据质量:

| 文件 | 规模 | 采集口径 | 含这 4 个? |
| --- | --- | --- | --- |
| `Tools/ida/st_target_meta_1170.json` | 91 条 | **代码里真正调用的目标** | ❌ |
| `Tools/ida/st_targets_workbench.json` | 93 条 | 同上(`ae_id` + `var`) | ❌ |
| `Tools/ida/st_overrides_all.tsv` | — | **值是 1.5.97 的 RVA** | 有,但方向相反 |

那 91/93 条是按调用点采集的(`setWornData`、`setHealth`、`setChargeData`、
`s_constructor`……),**RTTI 定位器不是调用点**,所以从来没进过逆向范围。

**而且即便逆向出来也补不上**,因为它不是"没逆向出来",是**地址库里根本没有
这条 id**。实测:

```text
邻居:  392518 = 0x203B928    392519 = (整条不存在)    392520 = 0x203B970
```

是 **id 空洞,不是偏移为 0**;`394235`、`398437` 同样(`428461` 个 id 里有
`4903` 个空洞是常态)。且 `1.6.659` 的偏移 `0x1EA3228` 在 `1.6.1170` 里
**任何 id 下都不存在** —— 不是重编号,是官方库没给编号。

**为什么 1.5.97 那边有**:`versionlib-ae-to-se-1-5-97-0.map` 是**本仓库自己
生成**的(`st_overrides_all.tsv` 就是它的输入之一),想加什么就加什么;
而 `versionlib-*.bin` 是 **SKSE 官方发布的文件**,改它会让哈希对不上,
`CheckAddressLibrary` 也可能不认。

所以这 4 个 id 在 1.6.1130+/1.7.x 上**在不改官方库的前提下无法补**。真要
`Cast` 到这几个类型,只能换不依赖 RTTI 的判定方式(vtable 比较等),
不能指望补地址库。

**但实测确认当前无害**:把全部 37 个 `Cast<...>` 目标类型与三套运行时逐一比对,
**每一个都能解析**;这 4 个类型既不是 `Cast<>` 的目标也不是来源
(`Cast<T,U>` 会用到**源类型**的 RTTI,所以要按实参查而不是只看返回类型),
且 `RttiLocator<T>::Get()` 对它们零调用。`Cast()` 本身有 null 守卫。

**结论:记为潜伏项,不修。** 一旦有人写 `Cast<NiObjectNET>`,它会**静默返回
`nullptr`** 而不是崩溃 —— 这正是需要在评审时盯住的那类改动。

> **教训**:§42 与 §43 是**同一个根因家族**的三种表现,而三次都满足
> "在开发机上完全正常"。凡是"按游戏版本/运行时分支"的声明与导出,
> 都必须用**回放规则 + 每个随包运行时**去验,不能靠本地跑一遍。
> 判据是**导出表与准入规则**,不是"我这儿好好的"。

---

## 44. 2026-09-28 场次:一条聊天消息打崩专用服务器 —— 判据是「旁边三处都判了空」

> **锚点**:本节的修复落在 `Code/server/Services/OverlayService.cpp`。

### 44.1 缺陷:`GetCharacter()` 是 optional,被直接解引用

```cpp
// Code/server/Services/OverlayService.cpp(修复前)
auto [canceled, reason] = m_world.GetScriptService().HandleChatMessage(
    *acMessage.pPlayer->GetCharacter(), acMessage.Packet.ChatMessage);
```

`Player::GetCharacter()`(`Code/server/Game/Player.h`)返回的是
`std::optional<entt::entity>`,而 `m_character` **只在
`CharacterService::OnCharacterCreated`(`CharacterService.cpp:659`)里被赋值** ——
也就是角色创建成功之后。

于是「已连接、但还没建好角色」的玩家(刚连上、角色创建失败、角色被删后重连)
发一条聊天消息,这里就解引用一个**空的 optional**。这是未定义行为:MSVC
release 下 `optional::operator*` 不检查,取到的是未初始化的存储,脚本层再拿
这个实体去 `ctx().at<T>()` 就崩。

**这不是「玩家操作不当」**:`SendChatMessageRequest` 是任何已连接客户端都能发的
报文,与是否建好角色无关。

### 44.2 判据:同一个调用在别处都判了空,只有这一处没判

| 位置 | 代码 | 判空 |
| --- | --- | --- |
| `WeatherService::OnWeatherChange` | `if (!acMessage.pPlayer->GetCharacter()) return;` | 是 |
| `PlayerService::OnPlayerRespawnRequest` | `if (!character) return;` | 是 |
| `OverlayService::HandleChatMessage` | `*acMessage.pPlayer->GetCharacter()` | **否(本次缺陷)** |

这和 v1.1.4 修掉的 `CommandService::OnSetTimeCommand` 是**同一类**:
一条远程报文触发服务端空指针,而**判据不是「我觉得这里可能空」,是「同一份代码里
旁边的调用都判了」**。这条判据可机械执行,不依赖对玩法时序的猜测。

### 44.3 修法:只跳过脚本回调,不吞掉消息

```cpp
if (const auto character = acMessage.pPlayer->GetCharacter())
{
    auto [canceled, reason] = m_world.GetScriptService().HandleChatMessage(
        *character, acMessage.Packet.ChatMessage);
    if (canceled)
        return;
}

sendPlayerMessage(acMessage.Packet.MessageType, acMessage.Packet.ChatMessage, acMessage.pPlayer);
```

`onChatMessage` 拿到的是**实体**,传一个默认构造的实体等于把垃圾指针交给脚本,
所以这一段必须跳过;而下面的广播不需要调用者自己的角色 ——
`sendPlayerMessage` 内部对 `GetCharacter()` **已经判了空**(只影响 `kLocalChat`
的范围查询)—— 所以该玩家的**全局/队伍聊天照常送达**。

**修崩溃,不改行为**:如果直接把整个函数 `return` 掉,等于「没建好角色就不能说话」,
那是把一次崩溃换成一次静默的功能缺失。

### 44.4 复核命令(照抄)

```powershell
# 1. 服务端所有 optional 解引用点(应只剩三处,且都在自己的判空分支里)
Select-String -Path Code/server/**/*.cpp -Pattern '\*[A-Za-z_]+->GetCharacter\(\)'

# 2. GetCharacter 的赋值点(应只有一处,即角色创建成功之后)
Select-String -Path Code/server/**/*.cpp -Pattern 'SetCharacter'

# 3. 客户端同口径:`.value()` 必须有前置 has_value()
Select-String -Path Code/client/Services/**/*.cpp -Pattern '\.value\(\)'
```

复核结果:客户端 16 处 `.value()` 全部有前置 `has_value()` 守卫(或等价判空);
服务端 3 处 `*...GetCharacter()` 全部落在自己的判空分支内。

> **教训**:`std::optional` 的解引用在 release 下**不报错、不返回 nullptr**,
> 它取到的是垃圾值 —— 比裸指针的 null 更难查,因为崩溃点离缺陷点很远
> (这里崩在脚本层)。**同一个调用在仓库里出现多次时,「其他几处判没判」就是
> 最省事也最可靠的判据。**

## 45. 2026-09-29 场次:一个畸形包打崩专用服务器,以及一整类「线上计数与实体数不一致」 —— 判据是「写几条就只循环几条,读到的计数先夹紧再进 resize」

> **锚点**:本节的修复落在 `Code/encoding/**`(39 个文件)。

### 45.1 缺陷:计数写在线上,但读侧不设上界、写侧循环写全量

第一副面孔 —— **异常从 `noexcept` 帧里逃出**,未认证连接即可打崩服务端:

```cpp
// Code/encoding/Structs/Factions.cpp(修复前)
void Factions::Deserialize(TiltedPhoques::Buffer::Reader& aReader)
{
    auto npcCount = Serialization::ReadVarInt(aReader);
    if (npcCount > 0x1FF)
        throw std::runtime_error("Too many NPC factions received !");   // ← std::terminate
    NpcFactions.resize(npcCount);
```

整条调用链都是 `noexcept`:`AssignCharacterRequest::DeserializeRaw` 与
`CharacterSpawnRequest::DeserializeRaw` 都是 `noexcept` 虚函数
(`Code/encoding/Messages/Message.h:24`、`:45`),`CharacterData::Deserialize`
同理,`CODE_GUIDELINES.md:47` 也明文 "Don't use exceptions"。
**异常从 `noexcept` 帧里逃出 = `std::terminate` = 进程直接死,连崩溃转储都没有。**

第二副面孔 —— **合法流量**就能触发,计数与循环写的条目数不一致:

```cpp
// Code/encoding/Structs/Mods.cpp(修复前)
const uint16_t modCount = std::min(ModList.size(), size_t(4096)) & 0xFFFF;
aWriter.WriteBits(modCount, 13);
for (auto& entry : ModList)      // ← 写的是全部条目,不是 modCount 条
```

读侧只消费 `count` 条,多写的字节被当成**下一条消息**解析 ⇒ **整条流从此失步**。
`Tints` 更直接:`aWriter.WriteBits(Entries.size(), 8)` **没有 clamp**,
超过 255 个条目即静默截断失步。

第三副面孔 —— **复用对象上的累积**:`insert()` / `push_back()` 都不清空,
消息对象复用一次就把上一包的内容**再应用一遍**。

### 45.2 判据:仓库里早就有正确写法,同族缺陷却没跟上

| 位置 | 写法 | 判定 |
| --- | --- | --- |
| `Code/encoding/Messages/NotifyFactionsChanges.cpp:27-28` | `if (count >= kMaxChangeCount) return;`(早退,不抛) | ✅ |
| `Code/encoding/Messages/PluginMessagingRequest.cpp:40-42` | `std::min<uint32_t>(size, kMaxPayloadBytes)`,注释 "Never trust the wire length: a peer can claim more bytes than it sent." | ✅ |
| `Code/encoding/Structs/Factions.cpp:39` | `throw std::runtime_error` | ❌ 本次缺陷 |
| `Code/encoding/Structs/Mods.cpp:22` | 13 位计数 + `for (auto& entry : ModList)` | ❌ 本次缺陷 |
| `Code/encoding/Structs/Tints.cpp:29` | `WriteBits(Entries.size(), 8)` 无 clamp | ❌ 本次缺陷 |

两条**可机械执行**的判据:

1. **`WriteBits(x.size() & MASK, N)` 后面跟 `for (auto& e : x)`** ⇒ 计数与实体数必然不一致。
2. **`ReadVarInt` 出来的计数直接进 `resize`/`assign`/循环** ⇒ 无上界(畸形包可致巨量分配)。

可达性判据:**解析发生在认证之前** ——
`GameServer::OnConsume`(`Code/server/GameServer.cpp:592`)先 `factory.Extract(reader)`,
`:600` 才 `m_messageHandlers[...]`,而 `GetByConnectionId` 判空与 `Kick` 都在 handler 内部
⇒ **未认证连接就能触发**。

### 45.3 修法:读侧读完计数立刻夹紧(或早退),写侧写几条就只循环几条

```cpp
// Code/encoding/Structs/Factions.cpp(修复后)
const auto npcCount = std::min<uint64_t>(Serialization::ReadVarInt(aReader), kMaxFactionCount);
NpcFactions.resize(npcCount);
```

```cpp
// Code/encoding/Structs/Mods.cpp(修复后)
for (size_t i = 0; i < modCount; ++i)      // ← 与 13 位计数一致
```

**绝不抛异常**:`noexcept` 链里只能 `std::min` 夹紧或早退。
上界常量**全部有出处**,不是随手取的数:`kMaxActorValueCount = 164` 就是游戏的
`kActorValueCount`;`0xFF` / `0x1FF` / `0xFFFF` 是**线上计数本身的位宽**;
`0x1000` / `0x40` 是 VarInt 计数的宽上界(玩家背包、附魔效果、行为图变量都在几百以内)。
共 21 个新常量、39 个文件。

顺带修掉三处同族缺陷:`AnimationVariables::String_to_VectorBool`
(`Code/encoding/Structs/AnimationVariables.cpp:33-42`)的位表外层循环**不检查 `chars` 边界**
(字符串偏短即**读越界**);`Inventory::Entry::Deserialize` 与 `Inventory::Deserialize`
的 `push_back` 不清空;`MagicEquipment::Deserialize` 在三个 flag 全为假时
**保留上一包的法术**(等于把收起来的法术又装回去,与
`Code/client/Games/Skyrim/Actor.cpp:809-834` 的「空 GameId = 这只手没法术」矛盾)。

**修崩溃,不改行为**:被夹紧的是**畸形包**,合法流量走的分支一条没变。

### 45.4 复核命令(照抄)

```powershell
# 1. 写侧:计数与循环不一致的模式应当零命中
Select-String -Path Code\encoding\Messages\*.cpp,Code\encoding\Structs\*.cpp `
  -Pattern 'WriteBits\(.*\.size\(\)|WriteVarInt\(aWriter, .*\.size\(\)\)'

# 2. 读侧:所有 ReadVarInt 计数点,逐个确认已 clamp 或早退
Select-String -Path Code\encoding\Messages\*.cpp,Code\encoding\Structs\*.cpp `
  -Pattern 'ReadVarInt\(aReader\)'

# 3. 异常:encoding 层不应再有任何 throw
Select-String -Path Code\encoding\**\*.cpp,Code\encoding\**\*.h -Pattern 'throw '

# 4. 改动面与文档门禁
git diff --shortstat -- Code
python Tools\Scripts\check_docs.py
```

复核结果:`WriteBits(...size()...)` 与 `WriteVarInt(aWriter, ...size())` **零命中**;
34 个 `ReadVarInt` 读点逐个核对 —— 计数语义的全部有上界,非计数的
(Id / Tick / DiscordId / MessageType / 堆叠 Count)本就不需要;
`Code/encoding` 已无任何 `throw`(全 `Code` 仅剩
`Code/immersive_launcher/stubs/DllBlocklist.cpp:131-132` 的注释);
`git diff --shortstat -- Code` = `39 files changed, 396 insertions(+), 120 deletions(-)`;
`python Tools\Scripts\check_docs.py` → `DOC CHECK OK` / `10 changelog version(s)`。

> **教训**:「计数写在线上」这件事**天然要求读写两侧对同一个上界达成一致**,
> 而 C++ 里这两侧隔着几百行、还分居不同文件 —— 于是它成了最容易单边漂移的地方。
> 判据因此可以简化成一句话:**线上有几个 bit 的计数,就只允许有几个 bit 的实体。**
> 另外两条通用教训:(1)`noexcept` 链里的任何 `throw` 都不是「错误处理」而是
> **`std::terminate`**,所以守卫只能夹紧或早退;(2)解析**先于**认证是常态,
> 所以解码器必须按「对端是敌手」来写,不能按「对端是自己人」来写。

## 46. 2026-09-29 场次:玩家断开连接时销毁了别人的角色 —— 判据是「`optional<entt::entity>` 绝不能 `value_or(0)`」

> **锚点**:本节的修复落在 `Code/server/GameServer.cpp`(`OnDisconnection`)。

### 46.1 缺陷:用 `0` 冒充「这个玩家没有角色」

```cpp
// Code/server/GameServer.cpp(修复前)
entt::entity playerCharacter = pPlayer->GetCharacter().value_or(static_cast<entt::entity>(0));

// Cleanup all entities that we own
auto ownerView = m_pWorld->view<OwnerComponent>();
for (auto entity : ownerView)
{
    if (entity == playerCharacter)      // ← 没有角色时,这里等于 entity == 0
    {
        m_pWorld->GetDispatcher().enqueue(CharacterRemoveEvent(World::ToInteger(entity)));
        continue;
    }
    ...
}
```

`Player::GetCharacter()` 返回 `std::optional<entt::entity>`,在 `CharacterService`
创建角色**之前一直是空**。把它 `value_or(0)` 拍平成一个整数之后,「没有角色」
与「角色恰好是 0 号实体」就**不可区分**了。

**EnTT 里 `0` 不是空值**。空值是「entity 位全 1」:

```cpp
// skypjack/entt v3.10.0, src/entt/entity/entity.hpp
static constexpr entity_type entity_mask = 0xFFFFF;          // :35
static constexpr version_type version_mask = 0xFFF;          // :36
static constexpr entity_type reserved = entity_mask | (version_mask << entity_shift);   // :73
static constexpr entity_type to_entity(const value_type value) { return (to_integral(value) & entity_mask); }   // :91
```

而 `0` 恰恰是 `registry::create()` 交出的**第一个实体**(`construct(0, 0) == 0`)——
在服务端就是**第一个玩家的角色**。于是:

| 断开的玩家 | `playerCharacter` | `entity == playerCharacter` 命中谁 | 走的分支 |
| --- | --- | --- | --- |
| 有角色 | 自己的实体 | 自己 | `CharacterRemoveEvent` ✅ |
| **没有角色** | **`0`** | **0 号实体(通常是别人的角色)** | **`CharacterRemoveEvent`** ❌ |

第二条本该走 `OwnershipTransferEvent`(把角色转交给别人),实际却**把别人的角色
直接销毁了** —— 那个玩家会看到自己的角色凭空消失,或客户端开始报找不到实体。

**触发条件很平常**:玩家在**还没建好角色**的时候断开 —— 连上后被踢、角色创建失败、
或角色已被删除后掉线。

### 46.2 判据:「有角色 / 没角色」在同一个函数里分得很清楚,只有这一处拍平成了整数

| 位置 | 写法 | 判定 |
| --- | --- | --- |
| `Code/server/Services/OverlayService.cpp:75` | `if (const auto character = acMessage.pPlayer->GetCharacter())` | ✅ |
| `Code/server/Services/PlayerService.cpp:157` | `if (!pPlayer->GetCharacter()) return;` | ✅ |
| `Code/server/Services/CommandService.cpp:100` | `auto character = pTargetPlayer->GetCharacter(); if (character)` | ✅ |
| `Code/server/GameServer.cpp:636` | `GetCharacter().value_or(static_cast<entt::entity>(0))` | ❌ 本次缺陷 |

两条**可机械执行**的判据:

1. **`optional<entt::entity>` 绝不能 `value_or(0)`** —— EnTT 里没有「0 号空实体」
   这个概念, `0` 是**第一个活实体**;
2. **`optional` 一旦被 `value_or` 拍平,`optional` 承载的那个信息就永久丢了** ——
   后面所有比较都退化成整数比较,而整数里**没有**「缺失」这个取值。

全仓 `value_or(` **只有这一处**(`Select-String 'value_or\('` 于 `Code` 全目录)。
`static_cast<entt::entity>(0)` / `entt::entity{0}` / `entt::null` 同样**零命中** ——
说明这不是「仓库习惯」,而是一次孤立的写法。

### 46.3 修法:保留 `optional`,在比较处判空

```cpp
// Code/server/GameServer.cpp(修复后)
// A player that disconnects before it ever got a character has no entity to
// remove. Zero is not a null identifier here: it is the first entity the
// registry hands out, so a player without a character would otherwise match
// whichever entity happens to carry that id and destroy someone else's actor.
const auto playerCharacter = pPlayer->GetCharacter();

// Cleanup all entities that we own
auto ownerView = m_pWorld->view<OwnerComponent>();
for (auto entity : ownerView)
{
    if (playerCharacter && entity == *playerCharacter)
    {
        m_pWorld->GetDispatcher().enqueue(CharacterRemoveEvent(World::ToInteger(entity)));
        continue;
    }
    ...
}
```

**修崩溃,不改行为**:有角色的玩家断开时走的路径一条没变 —— `playerCharacter`
为真时 `*playerCharacter` 就是原来的那个实体。

同一提交里顺带删掉一处死代码:`Code/server/Services/CharacterService.cpp` 的
`OnReferencesMoveRequest` 里 `const auto movementCopy = movementComponent;`
**拷贝之后全函数再无引用**(逐行确认过)。

### 46.4 复核命令(照抄)

```powershell
# 1. optional 拍平:应当零命中
Get-ChildItem -Path Code -Recurse -Include *.cpp,*.h |
  Select-String -Pattern 'value_or\('

# 2. 「0 号空实体」这个写法本身:应当零命中
Get-ChildItem -Path Code -Recurse -Include *.cpp,*.h |
  Select-String -Pattern 'static_cast<entt::entity>\(0\)|entt::entity\{0\}|entt::null'

# 3. 有角色 / 没角色的三处正确写法仍在
Get-ChildItem -Path Code\server -Recurse -Include *.cpp |
  Select-String -Pattern 'GetCharacter\(\)'

# 4. 改动面与文档门禁
git diff --shortstat -- Code
python Tools\Scripts\check_docs.py
```

复核结果:`value_or(` **零命中**;`static_cast<entt::entity>(0)` / `entt::entity{0}` /
`entt::null` **零命中**;`GetCharacter()` 的 14 个引用点逐个核对,解引用全部落在
自己的判空分支内;`git diff --shortstat -- Code` = `2 files changed, 6 insertions(+), 4 deletions(-)`;
`python Tools\Scripts\check_docs.py` → `DOC CHECK OK` / `10 changelog version(s)`。

> **教训**:`std::optional` 存在的意义就是「**没有值**」这个状态本身,
> 而 `value_or` 会**把这个状态抹掉**。凡是把 `optional` 拍平成一个「不可能取到的
> 哨兵值」的地方,都要先问一句:**这个哨兵值真的不可能出现吗?** 这里 `0` 不但可能,
> 而且是**第一个**被分配出去的实体 —— 哨兵与合法值撞在一起,缺陷就变成了「偶尔
> 销毁别人的角色」,离缺陷点很远,查起来极难。
> 判据可以简化成一句话:**`optional` 拍平的地方,就是信息丢失的地方。**

## 47. 2026-09-29 场次:游戏读盘时解引用空 cell —— 判据是「`GetParentCellEx()` 允许返回空」

> **锚点**:本节的修复落在 `Code/client/Games/Skyrim/TESObjectREFR.cpp`(`MoveTo`),以及四处直接写 `parentCell->` 的调用点。

### 47.1 缺陷:cell 在 load 中为空,而 `MoveTo` 把它读穿

```cpp
// Code/client/Games/Skyrim/TESObjectREFR.cpp(修复前)
// Skyrim: MoveTo() can fail, causing the object to be deleted
void TESObjectREFR::MoveTo(TESObjectCELL* apCell, const NiPoint3& acPosition) const noexcept
{
    ScopedReferencesOverride recursionGuard;
    ...
    TiltedPhoques::ThisCall(s_internalMoveTo, this, GetNullHandle(), apCell, apCell->worldspace, acPosition, rotation);
    //                                                              ^^^^^^^^^^^^^^^^^^ 无判空
}
```

`apCell->worldspace` 是这个函数的第 4 个实参,**没有判空** —— 传进来的 cell 为空就是空指针解引用。

这个函数有 9 个调用点,其中三处传的正是 `PlayerCharacter::Get()->parentCell`:

| 调用点 | 传的 cell | 判定 |
| --- | --- | --- |
| `Code/client/Services/Generic/CharacterService.cpp:602`(`OnCharacterSpawn`) | `PlayerCharacter::Get()->parentCell` | ❌ |
| `Code/client/Services/Generic/CharacterService.cpp:1627`(`CreateCharacterForEntity`) | `PlayerCharacter::Get()->parentCell` | ❌ |
| `Code/client/Services/Generic/CharacterService.cpp:2116`(`RunSpawnUpdates`) | `PlayerCharacter::Get()->parentCell` | ❌ |
| `Code/client/Services/Generic/CharacterService.cpp:1291` | 取 cell 后**判空**才调 | ✅ |
| `Code/client/Services/Generic/CommandService.cpp:77` | 取 cell 后**判空**才调 | ✅ |
| `Code/client/Services/Generic/OverlayService.cpp:539` | 取 cell 后**判空**才调 | ✅ |
| `Code/client/Games/Skyrim/PlayerCharacter.cpp:129` | 取 cell 后**判空**才调 | ✅ |

**为什么 cell 会是空的**:玩家对象在整段 load 里都存活,它的 cell 却在换。
`PlayerCharacter::Get()` 返回非空 **不蕴含** `parentCell` 非空。

同一族还有四处**直接**写 `parentCell->formID`:

| 位置 | 场景 | 写法 |
| --- | --- | --- |
| `Code/client/Services/Generic/TransportService.cpp:307` | `OnConnected`(玩家点「连接」) | `pPlayer->parentCell->formID` |
| `Code/client/Services/Generic/ObjectService.cpp:127` | `OnCellChange` | `pPlayer->parentCell->formID` |
| `Code/client/Services/Generic/CharacterService.cpp:1359-1363` | `RequestServerAssignment` | 同一表达式解引用**两次** |
| `Code/client/Services/Debug/Views/EntitiesView.cpp:191` / `QuestDebugView.cpp:125` | 「Teleport to me」按钮 | `pPlayer->parentCell->formID` |

**可达性(诚实口径)**:`OnConnected` 由玩家手点连接触发,`parentCell` 为空只在
load / 换 cell 的窗口内出现 —— 要崩需要握手恰好在这个窗口里完成,不是必然复现。
但 `MoveTo` 那一处**与可达性无关**:`GetParentCellEx()` 的契约就允许返回空,
`Code/client/Games/Skyrim/PlayerCharacter.cpp:117` 自己就按可空处理(`if (!pCell) { SetNoBleedoutRecovery(true); return pos; }`)。

### 47.2 判据:仓库自己的注释与法条

`Code/client/Games/Skyrim/TESObjectREFR.h:160-162` 的注释就写着:

```cpp
// The actual parent cell is the field below at offset 0x60; use GetParentCellEx() when the
// effective cell is needed.
```

而 `GetParentCellEx()`(`Code/client/Games/Skyrim/TESObjectREFR.cpp:500-503`)是:

```cpp
TESObjectCELL* TESObjectREFR::GetParentCellEx() const noexcept
{
    return parentCell ? parentCell : GetSaveParentCell();
}
```

**回退的 `GetSaveParentCell()` 本身可以返回空** —— 所以 `GetParentCellEx()` 不是「安全取值」,
它是「**取到最有效的那个,可能仍是空**」。

| 位置 | 写法 | 判定 |
| --- | --- | --- |
| `Code/client/Services/Generic/CharacterService.cpp:1285-1291` | 取 cell → 判空 → 打日志 `return` | ✅ |
| `Code/client/Services/Generic/CommandService.cpp:58-77` | 取 cell → 判空 → 提示 `return` | ✅ |
| `Code/client/Services/Generic/OverlayService.cpp:520-539` | 取 cell → 判空 → 提示 `return` | ✅ |
| `Code/client/Services/Generic/CharacterService.cpp:1739` | `if (!pCell || !pCell->IsAttached())` | ✅ |
| `Code/client/Systems/AnimationSystem.cpp:118` | `if (const auto pCell = pActor->parentCell)` | ✅ |
| `Code/client/Games/Skyrim/TESObjectREFR.cpp:385-395` | `apCell->worldspace`,无判空 | ❌ **本次缺陷** |
| `TransportService` / `ObjectService` / `CharacterService:1359` | `parentCell->formID`,无判空 | ❌ 同族 |

`docs/PITFALLS.md` 的 §22(`:1559-1591`)已经写过这条法条:

> `PlayerCharacter::Get() != nullptr` **不蕴含** `GetParentCellEx() != nullptr`。
> 玩家对象在 load 全程存活,它的 cell 却在换 —— 所有 `Get()->GetX()->` 的写法都要按「load 中」来审。

两条**可机械执行**的判据:

1. **`parentCell->` 直接解引用 = 缺陷** —— 取 cell 必须走 `GetParentCellEx()`,且结果判空;
2. **「取一次、判一次」** —— 同一个表达式里出现两次 `parentCell->formID`
   (`CharacterService.cpp:1359-1363` 的 `GetServerModId(..., ...->formID)` 与错误日志各一次)
   说明取 cell 的动作没有落在一个局部量上,判空就无处可放。

### 47.3 修法:一处单点守卫 + 四处改走 `GetParentCellEx()`

```cpp
// Code/client/Games/Skyrim/TESObjectREFR.cpp(修复后)
void TESObjectREFR::MoveTo(TESObjectCELL* apCell, const NiPoint3& acPosition) const noexcept
{
    // The cell is read through below, and GetParentCellEx() is allowed to come
    // back empty while the game is between cells.
    if (!apCell)
        return;

    ScopedReferencesOverride recursionGuard;
    ...
}
```

修 `MoveTo` 一处,九个调用点全部受益 —— 这是**单点缺陷、多点共享**时最省的修法。

`Code/client/Services/Generic/TransportService.cpp:307-310`:

```cpp
    // The player is between cells during a load, so the effective cell can be
    // empty here; an unset cell id only costs the first cell change message.
    if (const auto* pParentCell = pPlayer->GetParentCellEx())
        modSystem.GetServerModId(pParentCell->formID, request.CellId);
```

`Code/client/Services/Generic/CharacterService.cpp:1359`:

```cpp
    const auto* pParentCell = pActor->GetParentCellEx();
    const uint32_t cParentCellFormId = pParentCell ? pParentCell->formID : 0;

    if (!m_world.GetModSystem().GetServerModId(cParentCellFormId, message.CellId))
    {
        spdlog::error("Server cell id not found for cell id {:X}", cParentCellFormId);
        return;
    }
```

`Code/client/Services/Generic/ObjectService.cpp:126`:

```cpp
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    if (!pPlayer)
        return;

    // The player is between cells during a load, so the effective cell can be
    // empty here and the cell id below would be built from a null cell.
    TESObjectCELL* pCell = pPlayer->GetParentCellEx();
    if (!pCell)
        return;
```

三个 `MoveTo` 调用点把 `PlayerCharacter::Get()->parentCell` 换成
`PlayerCharacter::Get()->GetParentCellEx()` —— 非空时两者**完全等价**,为空时由上面的守卫兜住。

两个调试视图用 `if (pCell)` 而不是提前 `return`:

```cpp
        auto* pPlayer = PlayerCharacter::Get();
        const auto* pCell = pPlayer ? pPlayer->GetParentCellEx() : nullptr;
        if (pCell)
            m_world.GetRunner().Trigger(MoveActorEvent(acFormId, pCell->formID, pPlayer->position));
```

**为什么不能写成 `if (!pCell) return;`**:这两个函数是 ImGui 的绘制函数,提前 `return`
会把**当前帧剩下的界面元素整段跳过** —— 修空指针不能顺手把界面拆掉。

**修崩溃,不改行为**:`parentCell` 非空时 `GetParentCellEx()` 就是 `parentCell` 本身,
正常路径一个字节都没变;为空时原本是崩溃。

### 47.4 复核命令(照抄)

```powershell
# 1. parentCell 直接解引用:剩下的应当全部已有守卫
Get-ChildItem -Path Code\client -Recurse -Include *.cpp,*.h |
  Select-String -Pattern 'parentCell->|parentCell\.'

# 2. MoveTo 的守卫仍在
Get-ChildItem -Path Code\client -Recurse -Include *.cpp,*.h |
  Select-String -Pattern 'void TESObjectREFR::MoveTo' -Context 0,6

# 3. 改动面与文档门禁
git diff --stat -- Code
python Tools\Scripts\check_docs.py
```

复核结果:`parentCell->` 剩余 11 处**全部已有守卫**(`Projectile.cpp:84`、`TESObjectREFR.cpp:338/340/346/347`、
`FormDebugView.cpp:42/48`、`PlayerDebugView.cpp:67`、`CharacterService.cpp:1360`、`DiscoveryService.cpp:154`、
`TransportService.cpp:310`);`git diff --stat -- Code` = `6 files changed, 31 insertions(+), 9 deletions(-)`;
`python Tools\Scripts\check_docs.py` → `DOC CHECK OK` / `10 changelog version(s)`。

> **教训**:引擎的字段**允许为空**这件事,只有两处会告诉你 —— 头文件里那句
> 「use `GetParentCellEx()` when the effective cell is needed」,以及**回退函数自己可以返回空**。
> 「非空的对象」与「它里面的字段非空」是两件事:`PlayerCharacter::Get() != nullptr`
> 不蕴含 `GetParentCellEx() != nullptr`,而后者又不蕴含 `GetSaveParentCell() != nullptr`。
> 判据可以简化成一句话:**取 cell 的地方必须判空,而判空必须落在一个局部量上** ——
> 表达式里写两次 `->formID`,判空就无处可放。

## 48. 2026-09-29 场次:主菜单与读盘期间解引用「还没有玩家」 —— 判据是「`PlayerCharacter::Get()` 允许返回空」

> **锚点**:本节的修复落在 `Code/client/Games/`(`Forms.cpp`、`References.cpp`、`Skyrim/TESObjectREFR.cpp`)与 `Code/client/Services/Generic/`(`CharacterService.cpp`、`PlayerService.cpp`、`TransportService.cpp`、`MapService.cpp`、`CommandService.cpp`、`OverlayService.cpp`),共 9 个文件 18 处。

### 48.1 缺陷:18 处 `PlayerCharacter::Get()->`

```cpp
// Code/client/Games/Skyrim/PlayerCharacter.cpp:44-49
PlayerCharacter* PlayerCharacter::Get() noexcept
{
    POINTER_SKYRIMSE(PlayerCharacter*, s_character, 401069);
    return *s_character.Get();
}
```

它**可以返回空** —— 主菜单上、读盘期间都没有玩家。而下面这些写法把判空的位置挤没了:

```cpp
// Code/client/Services/Generic/PlayerService.cpp(修复前)
void PlayerService::OnDisconnected(const DisconnectedEvent&) noexcept
{
    PlayerCharacter::Get()->SetDifficulty(m_previousDifficulty);
    ...
}

// Code/client/Games/Forms.cpp:53-68(修复前)
void TESNPC::Initialize() noexcept
{
    auto pPlayerBaseForm = Cast<TESNPC>(PlayerCharacter::Get()->baseForm);

    attackDataForm.attackDataMap = pPlayerBaseForm->attackDataForm.attackDataMap;
    npcClass = pPlayerBaseForm->npcClass;
    ...
}

// Code/client/Games/References.cpp:60(修复前)
float multiplier = s_getDifficultyMultiplier(PlayerCharacter::Get()->difficulty, ActorValueInfo::kHealth, isPlayer);

// Code/client/Games/Skyrim/TESObjectREFR.cpp:919-926(修复前)
    PlayerCharacter* pPlayer = PlayerCharacter::Get();

    if (!pPlayer->IsItemInInventory(objectId))
```

| 位置 | 触发方式 | 说明 |
| --- | --- | --- |
| `TransportService.cpp:275`(`OnConnected`) | 玩家在主菜单/读盘时点「连接」 | 握手可以在没有玩家时完成,`:280` 立刻解引用 |
| `PlayerService.cpp:86`(`OnDisconnected`) | 主菜单上掉线 | 断线包不要求玩家存在 |
| `PlayerService.cpp:376`(`ToggleDeathSystem`) | 由 `:89` 的断线路径调用 | 同上 |
| `CharacterService.cpp:2086`/`:2119`(`RunSpawnUpdates`) | 每帧 | 读盘期间仍在跑 |
| `CharacterService.cpp:602`/`:621`/`:1630` | 收到 `CharacterSpawnRequest` | 玩家为 null 时照样会到 |
| `Forms.cpp:55`(`TESNPC::Initialize`) | `CharacterService.cpp:541`/`:1608` 建 NPC | 没有玩家就没有「玩家基础形态」可抄 |
| `References.cpp:60`(`CalculateRealDamage`) | 任何一次伤害计算 | 伤害钩子不检查玩家 |
| `TESObjectREFR.cpp:919`(`AddOrRemoveItem`) | `InventoryService.cpp:213`/`:223`(默认 `aIsSettingInventory=false`) | 任务物品分支 |

### 48.2 判据:仓库自己早就在两处写下了这句话

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

| 位置 | 写法 | 判空 |
| --- | --- | --- |
| `OverlayService.cpp:615` | `CalculateHealthPercentage(PlayerCharacter::Get())` | ✅(函数内判) |
| `PlayerService.cpp:218` | `PlayerCharacter* pPlayer = PlayerCharacter::Get(); if (!pPlayer) return;` | ✅ |
| `PlayerService.cpp:309`/`:326`/`:359` | `if (auto* pPlayer = PlayerCharacter::Get())` | ✅ |
| `OverlayService.cpp:210`(`Render`) | `bool inGame = pPlayer && pPlayer->GetNiNode();` | ✅ |
| `PlayerService.cpp:86` | `PlayerCharacter::Get()->SetDifficulty(...);` | 否(本次缺陷) |
| `CharacterService.cpp:602` | `pActor->MoveTo(PlayerCharacter::Get()->GetParentCellEx(), ...);` | 否(本次缺陷) |
| `TransportService.cpp:275` | 取到局部量后 `:280` 就解引用 | 否(本次缺陷) |
| `Forms.cpp:55` | `Cast<TESNPC>(PlayerCharacter::Get()->baseForm)` | 否(本次缺陷) |
| `References.cpp:60` | `s_getDifficultyMultiplier(PlayerCharacter::Get()->difficulty, ...)` | 否(本次缺陷) |
| `TESObjectREFR.cpp:919` | `if (!pPlayer->IsItemInInventory(objectId))` | 否(本次缺陷) |

**可机械执行的判据**:`PlayerCharacter::Get()->` 出现一次就是一处缺陷 ——
表达式里根本没有放判空的位置。判空必须落在一个局部量上,写成
`if (PlayerCharacter* pPlayer = PlayerCharacter::Get())`。

### 48.3 修法:取一次到局部量,判空

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

    request.DiscordId = m_world.ctx().at<DiscordService>().GetUser().id;

    auto* pNpc = pPlayer ? Cast<TESNPC>(pPlayer->baseForm) : nullptr;
    if (pNpc)
        request.Username = pNpc->fullName.value.AsAscii();
    else
        request.Username = "Some dragon boi";
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

```cpp
// Code/client/Games/Forms.cpp(TESNPC::Initialize)
    PlayerCharacter* pPlayer = PlayerCharacter::Get();
    auto pPlayerBaseForm = pPlayer ? Cast<TESNPC>(pPlayer->baseForm) : nullptr;

    if (pPlayerBaseForm)
    {
        attackDataForm.attackDataMap = pPlayerBaseForm->attackDataForm.attackDataMap;
        npcClass = pPlayerBaseForm->npcClass;
        ...
    }
    spellList.Initialize();
```

其余六处同构:`TESObjectREFR.cpp` 用 `if (pPlayer && !pPlayer->IsItemInInventory(objectId))`;
`PlayerService.cpp` 五处用 `if (PlayerCharacter* pPlayer = PlayerCharacter::Get())`;
`CharacterService.cpp` 八处(其中 `RequestServerAssignment` 用 `const PlayerCharacter*`,因为
`GetTints()` 是 const 成员);`MapService.cpp` 两处、`CommandService.cpp`/`OverlayService.cpp` 各一处。

**为什么返回 `aDamage` 而不是乘 1.0**:原行为就是「拿不到难度倍率时伤害不放大」,
返回 `aDamage` 与之一致;乘 1.0 会引入一个原本不存在的语义。

**修崩溃,不改行为**:指针非空时走的还是原来那条路;为空时原本是崩溃。传送的两处
仍保留原来的失败提示(`"Teleporting to player failed."`)。

### 48.4 复核命令(照抄)

```powershell
# 1. PlayerCharacter::Get()-> 应当零命中
Get-ChildItem -Path Code\client -Recurse -Include *.cpp,*.h |
  Select-String -Pattern 'PlayerCharacter::Get\(\)->'

# 2. 全部取用点(人工核对每一处都有局部量判空)
Get-ChildItem -Path Code\client -Recurse -Include *.cpp,*.h |
  Select-String -Pattern 'PlayerCharacter::Get\(\)'

# 3. 改动面与文档门禁
git diff --stat -- Code
python Tools\Scripts\check_docs.py
```

复核结果:`PlayerCharacter::Get()->` **零命中**(修前 18 处);
`git diff --stat -- Code` = `9 files changed, 98 insertions(+), 34 deletions(-)`;
`python Tools/Scripts/check_docs.py` → `DOC CHECK OK` / `10 changelog version(s)`。

> **教训**:引擎的「全局单例」在这套代码里**没有一个能当非空用** ——
> `PlayerCharacter::Get()` 在读盘与主菜单为空,`GetParentCellEx()` 在换 cell 的窗口里为空,
> 两者都不是异常状态,而是**每局游戏都会经过的正常状态**。
> 判空的位置只有一个:把它取到局部量,然后判它;写成 `X::Get()->field` 的那一刻,
> 判空就已经无处可放了。另外,修这种缺陷时**不要改变函数原本的返回值语义** ——
> `return aDamage` 与 `return aDamage * 1.0f` 看起来一样,后者却是在偷偷改行为。

## 49. 2026-09-29 场次:字符串缓存超过 65535 条后,新玩家永久收不全 —— 判据是「游标只能推进真正写出去的那几条」

> **锚点**:本节的修复落在 `Code/encoding/StringCache.cpp` 与 `Code/encoding/Messages/StringCacheUpdate.h`。

### 49.1 缺陷:写出去的条数被 16 位截断,游标却推到了缓存末尾

字符串缓存是**增量同步**的,每个玩家在服务端记一个「我已经收到第几条」的游标
(`Player::GetStringCacheId()`,发完由 `StringCacheService::HandleUpdate` 写回):

```cpp
// Code/server/Services/StringCacheService.cpp:30-37
    for (const auto pPlayer : m_world.GetPlayerManager())
    {
        auto startId = pPlayer->GetStringCacheId();
        auto update = stringCache.Serialize(startId);
        pPlayer->SetStringCacheId(startId);   // ← 游标由 Serialize 就地推进

        pPlayer->Send(update);
    }
```

而 `Serialize` 原本这样推进游标:

```cpp
// Code/encoding/StringCache.cpp(修复前)
StringCacheUpdate StringCache::Serialize(uint32_t& aStartId) const noexcept
{
    StringCacheUpdate update;
    update.StartId = aStartId;

    if (aStartId < m_idToString.size())
    {
        auto itor = std::begin(m_idToString);
        std::advance(itor, aStartId);

        update.Values.insert(std::begin(update.Values), itor, std::end(m_idToString));

        aStartId = m_idToString.size();   // ← 无条件推到末尾
    }

    return update;
}
```

一个 `StringCacheUpdate` 能带多少条,由**线上计数的位宽**决定 ——
写侧是 VarInt,读侧却是 16 位:

```cpp
// Code/encoding/Messages/StringCacheUpdate.cpp
    StartId = Serialization::ReadVarInt(aReader) & 0xFFFF;
    const auto count = Serialization::ReadVarInt(aReader) & 0xFFFF;   // ← 最多 65535 条
    Values.resize(count);
```

于是**第 65536 条之后的内容写不出去,游标却已经推到了末尾**。接收侧只收到前 65535 条,
本地缓存长度停在 65535;下一次增量同步时服务端按已经推到末尾的游标认为「这个人收全了」,
**一条都不再发** ⇒ 两边 id 永久错位,而且**不会自愈**,只有重启服务器才会好一阵子。

### 49.2 判据:写出去几条,游标就只能推进几条

| 位置 | 写法 | 判定 |
| --- | --- | --- |
| `Code/encoding/StringCache.cpp:59`(修复前) | `aStartId = m_idToString.size();` 而只写出去 16 位能装下的部分 | ❌ 本次缺陷 |
| `Code/encoding/StringCache.cpp:74-76`(接收侧) | `assert(aMessage.StartId == m_idToString.size());` —— 断言本身就要求**连续性** | 这就是判据(2026-10-13 更正:这句断言本身就是同一族的第四个缺陷,见 §83 —— 它的实参来自线上,而 `releasedbg` 下断言是活的;现在改成「不连续就丢弃这条消息」) |
| `Code/encoding/Structs/Mods.cpp:24`(v1.1.9 已修) | 写侧 `for (size_t i = 0; i < modCount; ++i)` | ✅ 同一条规则的写侧版本 |

**可机械执行的判据**:凡是「按游标/偏移切片发送」的地方,
**推进量必须等于写出去的条数** ——
写成 `cursor = container.size();` 的那一刻,就已经把「截断」悄悄变成了「永久丢失」。
仓库的接收侧判据正是这条不变量:`StartId` 必须**恰好等于**本地缓存长度,
一旦发送侧跳过一段,两边就再也对不上。(2026-10-13 更正:它当初写成 `assert`,
而 `StartId` 是对端填的字段 —— 对端一跳,终止的是接收方的进程。见 §83。)

### 49.3 修法:切一片出去,游标只推进这一片

```cpp
// Code/encoding/StringCache.cpp(修复后)
    if (aStartId < m_idToString.size())
    {
        // One message carries at most kMaxValueCount entries, and the receiver
        // requires the ids to be contiguous: a message whose start id does not line
        // up is dropped. Send a slice and advance the cursor by exactly what was
        // written, so the rest goes out on the next call instead of being skipped
        // and leaving the two caches out of step.
        const auto count = std::min<size_t>(m_idToString.size() - aStartId, StringCacheUpdate::kMaxValueCount);

        auto itor = std::begin(m_idToString);
        std::advance(itor, aStartId);

        auto end = itor;
        std::advance(end, count);

        update.Values.insert(std::begin(update.Values), itor, end);

        aStartId += static_cast<uint32_t>(count);
    }
```

上界常量放在消息头里,因为它**本来就是线上计数的位宽**,不是随手取的数:

```cpp
// Code/encoding/Messages/StringCacheUpdate.h
    // The count is written as a VarInt but read back masked to 16 bits, so this is
    // the most entries one message can carry. The producer has to stop here too.
    static constexpr size_t kMaxValueCount = 0xFFFF;
```

**修截断,不改行为**:游标没走到末尾时,下一次增量同步照旧把剩下的发出去 ——
接收侧收到的内容与修复前完全一致,只是**分成了多条消息**。

### 49.4 同源的第二层截断:游标字段本身只有 16 位

只修「切片」是不够的 —— 修完游标,下一层截断立刻露出来,而这一层更安静:

```cpp
// Code/encoding/StringCache.cpp:50-51
    StringCacheUpdate update;
    update.StartId = aStartId;        // aStartId 是 uint32_t

// Code/encoding/Messages/StringCacheUpdate.h(修复前)
    uint16_t StartId{0};             // ← 字段只有 16 位
```

游标是 `uint32_t`(函数签名 `StringCache::Serialize(uint32_t& aStartId)`,
服务端游标存在 `Player::m_stringCacheId` 也是 `uint32_t`),字段却是 `uint16_t`,
所以 `update.StartId = aStartId;` 是一次**隐式窄化**。缓存一旦超过 65535 条:

| 层 | 修复前 | 后果 |
| --- | --- | --- |
| 切片(§49.1) | 写出去的条数被 16 位计数截断,游标却推到末尾 | 后加入的玩家**永久**收不全 |
| 字段宽度(本节) | `StartId` 被 16 位截断,接收侧判据永不成立 | debug 构建每条都炸,release 构建 id **静默错位** |

接收侧那条断言 `Code/encoding/StringCache.cpp:78 assert(aMessage.StartId == m_idToString.size());`
正是**同时**检验这两层的判据:它要求「发来的起点 == 我现在的长度」,
游标跳过头(§49.1)或起点被截断(本节)都会让它失败。

```cpp
// Code/encoding/Messages/StringCacheUpdate.h(修复后)
    uint32_t StartId{0};

// Code/encoding/Messages/StringCacheUpdate.cpp(修复后)
    StartId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
```

**向后兼容**:`StartId` 走的是 VarInt,对小于 65536 的值**编码字节完全相同**,
所以放宽字段宽度**不改线上格式**;新旧版本混连时,老版本发来的起点语义不变。
另外 `StartId` 的消费者全仓只有 `StringCache` 自己(`Player::GetStringCacheId`/`SetStringCacheId`、
`StringCacheService::HandleUpdate`、`GameServer.cpp:1017-1022` 的首次全量推送),
**没有任何第三方依赖 16 位宽度**。

### 49.5 复核命令(照抄)

```powershell
# 1. 不该再有「无条件推到末尾」的游标推进
Select-String -Path Code\encoding\StringCache.cpp -Pattern 'aStartId = '

# 2. 上界常量与读侧掩码应当一致
Select-String -Path Code\encoding\Messages\StringCacheUpdate.* -Pattern 'kMaxValueCount|0xFFFF'

# 3. 游标字段不能比游标窄
Select-String -Path Code\encoding\Messages\StringCacheUpdate.h -Pattern 'StartId'
Select-String -Path Code\encoding\Messages\StringCacheUpdate.cpp -Pattern 'StartId'

# 4. 改动面与文档门禁
git diff --stat -- Code
python Tools\Scripts\check_docs.py
```

复核结果:`aStartId = m_idToString.size();` **零命中**,只剩
`aStartId += static_cast<uint32_t>(count);`;`StringCacheUpdate.h` 里是 `uint32_t StartId{0};`
(不再是 `uint16_t`),`StringCacheUpdate.cpp` 里是 `& 0xFFFFFFFF`;
`python Tools/Scripts/check_docs.py` → `DOC CHECK OK` / `10 changelog version(s)`。

> **教训**:计数写在线上有多少位,**发送侧就必须按多少位切** ——
> 这一族在 v1.1.9 已经修过「写侧循环写全量」和「读侧不设上界」两副面孔,
> 这次是第三副:**游标**。前两副是「这一包解析错」,第三副更隐蔽 ——
> 包本身完全合法,错的是**下一次**该发什么,而且错得不会自愈。
> 判据只有一条:凡是靠游标/偏移做增量发送的地方,推进量必须等于写出去的条数;
> 写成 `cursor = container.size();` 就等于把「这一包截断」升级成「这段数据永久丢失」。

## 50. 2026-09-29 场次:远端动画回放队列没有上界 —— 判据是「同族的插值窗口早就有了 kMaxSamples」

> **锚点**:本节的修复落在 `Code/client/Systems/AnimationSystem.cpp` 与 `Code/client/Services/Generic/CharacterService.cpp`。

### 50.1 缺陷:一个只有一条出口、却有两处入口的队列

`Code/client/Components/RemoteAnimationComponent.h` 里的 `List<ActionEvent> TimePoints`
是远端角色的动作回放队列。入口两处,都不看长度:

```cpp
// Code/client/Services/Generic/CharacterService.cpp:689
animationComponent.TimePoints.push_back(action);

// Code/client/Systems/AnimationSystem.cpp:104
aAnimationComponent.TimePoints.insert(aAnimationComponent.TimePoints.end(), acReplay.Actions.begin(), acReplay.Actions.end());
```

出口只有一处:`Code/client/Systems/AnimationSystem.cpp:86` 的 `actions.pop_front();`,条件是
**队首那一条已经到期**(`:46 if (it != std::end(actions) && it->Tick <= aTick)`)。而在这之前
还有一道更早的返回:

```cpp
// Code/client/Systems/AnimationSystem.cpp:49(修复前)
if (!apActor->animationGraphHolder.IsReady())
{
    // Animation graph not ready, keep the action in queue and try again later
    return;
}
```

再加上唯一的调用点 `Code/client/Services/Generic/CharacterService.cpp:1983` 附近的

```cpp
if (!pActor)
    continue;
```

——actor 解析不出来时**连 Update 都不调用**。于是「远端角色刚 spawn、3D 还没加载完」
「动画图还没就绪」「换 cell 的窗口里 form 查不到」这三种**正常状态**下,队列只进不出,
而且没有任何一处会裁剪它。喂入速度是服务端 `ProcessMovementChanges` 的 50 Hz × 每条动作
(`update.ActionEvents = animationComponent.Actions;` 是整份拷贝),积攒的是完整的
`ActionEvent`(`Code/encoding/Structs/ActionEvent.h`:两个 `CachedString` + 一份
`AnimationVariables`,后者上界 `kMaxVariableCount = 0x1000`)。

### 50.2 判据:同族的插值窗口有硬上界,动画回放队列没有

| 位置 | 内容 | 判定 |
| --- | --- | --- |
| `Code/client/Systems/InterpolationSystem.cpp:19` | `constexpr size_t kMaxSamples = 8;` + `:48-51` 注释「Keep the window bounded. Newest samples win; dropping from the front is what makes this safe against a play head that has stopped advancing.」 | ✅ 同族已有的上界 |
| `Code/client/Components/InterpolationComponent.h:24` | `List<TimePoint> TimePoints;` 受上面那条上界约束 | ✅ |
| `Code/client/Components/RemoteAnimationComponent.h` | `List<ActionEvent> TimePoints;` **没有对应上界** | ❌ 本节缺陷 |
| `docs/PITFALLS.md:1485` | 早已记录插值窗口上限(「并加了 `kMaxSamples = 8` 的硬上限,防时钟重同步时窗口失控」) | 判据出处 |

判据一句话:**一个会被持续喂入、而出口可能被正常状态挡住的队列,必须有一个「只看最近 N 条」的兜底出口。**

### 50.3 修法:裁剪放在所有提前返回之前

```cpp
// Code/client/Systems/AnimationSystem.cpp
namespace
{
// Hard bound on the replay queue. It normally drains one action per frame, but
// nothing drains it while the actor has no 3D yet or its animation graph is not
// ready, and every snapshot in range keeps appending to it. Newest actions win,
// the same policy the interpolation window uses.
constexpr size_t kMaxQueuedActions = 256;
} // namespace

void AnimationSystem::Update(World& aWorld, Actor* apActor, RemoteAnimationComponent& aAnimationComponent, const uint64_t aTick) noexcept
{
    auto& actions = aAnimationComponent.TimePoints;

    while (actions.size() > kMaxQueuedActions)
        actions.pop_front();

    // An unresolvable actor has nothing to replay, but its queue still has to be
    // trimmed above, or it grows with every snapshot that mentions it.
    if (!apActor)
        return;
```

`Code/client/Services/Generic/CharacterService.cpp` 的调用点同步改成**无条件调用**:

```cpp
// Called even when the actor cannot be resolved: the queue has to be
// trimmed either way, and the snapshots keep appending to it.
AnimationSystem::Update(m_world, pActor, animationComponent, tick);
```

上界取 `256`:服务端的 `ActionReplayCache` 上界只有 `kReplayCacheMaxSize = 32`
(`Code/server/Game/Animation/ActionReplayCache.h:19`),256 条已远超任何合法回放链,
而「最新优先」与插值窗口的策略一致。

### 50.4 复核命令(照抄)

```powershell
# 1. 上界只有一处定义,裁剪在所有提前返回之前
Select-String -Path Code\client\Systems\AnimationSystem.cpp -Pattern 'kMaxQueuedActions|pop_front|if \(!apActor\)'

# 2. 调用点不再被 if (!pActor) 跳过
Select-String -Path Code\client\Services\Generic\CharacterService.cpp -Pattern 'AnimationSystem::Update|TimePoints.push_back'

# 3. 改动面与文档门禁
git diff --stat -- Code
python Tools\Scripts\check_docs.py
```

复核结果:`AnimationSystem.cpp` 里 `kMaxQueuedActions` 只在 `:30` 定义一次,`:37-38` 是裁剪,
`:42` 是 `if (!apActor)`;`CharacterService.cpp` 里 `AnimationSystem::Update` 只在 `:1989` 调用一次、
且前面没有 `if (!pActor)`;`python Tools/Scripts/check_docs.py` → `DOC CHECK OK` / `10 changelog version(s)`。

> **教训**:队列的上界不能靠「正常情况下它会自己排空」来保证 ——
> 排空的条件(actor 解析得出来、动画图就绪)恰恰是**加载窗口里不成立**的那一条。
> 判据是横向的:同一个文件族里,插值窗口有 `kMaxSamples`,动作回放队列就该有对应的上界;
> 只要一个容器「入口在别人手里、出口被正常状态挡着」,就必须有兜底裁剪,
> 而且裁剪必须放在**所有提前返回之前** —— 放在之后,等于把「actor 解析不出来」
> 这种最常见的触发条件排除在外,兜底也就形同虚设。

## 51. 2026-09-29 场次:队伍里的三处空指针 —— 判据是「同文件里的另一处写法」

> **锚点**:本节的修复落在 `Code/server/Services/PartyService.cpp`、`Code/server/Services/QuestService.cpp`、`Code/server/GameServer.cpp`。

### 51.1 缺陷:查询接口自己造了一个空队伍,调用方再解引用它

三处解引用,一个根因:**把「组件里存着一个 id」当成「队伍一定存在」**。

第一处,`Code/server/Services/PartyService.cpp:62-71`(修复前):

```cpp
PartyService::Party* PartyService::GetPlayerParty(Player* const apPlayer) noexcept
{
    auto& inviterPartyComponent = apPlayer->GetParty();
    if (inviterPartyComponent.JoinedPartyId)
    {
        return &m_parties[*inviterPartyComponent.JoinedPartyId];
    }

    return nullptr;
}
```

`m_parties` 是 `TiltedPhoques::Map<uint32_t, Party>`(`Code/server/Services/PartyService.h:126`)。
`operator[]` 在 key 不存在时**默认构造一个 `Party` 并插入**。于是「id 已失效」的玩家调用它,拿到的是
一个**成员为空、`LeaderPlayerId` 为 0 的假队伍**而不是 `nullptr`,并且 `m_parties` 里多出一条
永远清不掉的记录(`PartyService.h:89-96` 的 `Party` 里 `uint32_t LeaderPlayerId;` **没有默认初始化器**)。

第二处,`Code/server/Services/QuestService.cpp:40-42`(修复前):

```cpp
    PartyService::Party* pParty = partyService.GetPlayerParty(pPlayer);
    Player* pLeader = isLeader ? pPlayer : m_world.GetPlayerManager().GetById(pParty->LeaderPlayerId);
    auto& dedupHistory = pParty->GetQuestStageDedupHistory();
```

拿到返回值后**直接解引用,全程没有判空**。

第三处,`Code/server/GameServer.cpp:755` 的 `SendToLeader`(修复前):

```cpp
    if (const_cast<Player*>(apLeader)->GetParty().JoinedPartyId != acPartyComponent.JoinedPartyId)
```

`apLeader` 从未判空,而它全仓**只有一个调用点** —— 就是第二处传进来的 `pLeader`。

### 51.2 判据:三处都在「同一个文件里的另一处写法」上露馅

| 位置 | 写法 | 判定 |
| --- | --- | --- |
| `Code/server/Services/PartyService.cpp:36-43` `GetById` | `m_parties.find(...)`,未命中返回 `nullptr` | ✅ |
| `Code/server/Services/PartyService.cpp:62-71` `GetPlayerParty` | `m_parties[...]` | ❌ |
| `Code/server/Services/WeatherService.cpp:23-25` / `:39-41` | `auto* pParty = ...GetPlayerParty(...); if (!pParty) return;` | ✅ |
| `Code/server/Services/CharacterService.cpp:782-784` | `if (!pParty \|\| std::find(pParty->Members...) == end) return reject(...)` | ✅ |
| `Code/server/Services/QuestService.cpp:210-211` `OnQuestSceneChanges` | `if (!partyComponent.JoinedPartyId.has_value()) return;` | ✅ |
| `Code/server/Services/QuestService.cpp:40-42` `OnQuestChanges` | 直接解引用 `pParty` | ❌ |

**触发路径是真实的,不是理论上的**:客户端只在进了队伍时才发 `RequestQuestUpdate`
(`Code/client/Services/Generic/QuestService.cpp` 的三处发送点 `:72`/`:108`/`:158` 都带
`!m_world.Get().GetPartyService().IsInParty()` 门禁),但**已经在途的请求**遇上玩家退队/被踢就会以
「没有队伍」到达服务端,而服务端对此不做校验。`GameServer::Kick` 只标记断开,真正的 `OnDisconnection`
要等下一轮事件循环 —— 窗口就出在这里。

### 51.3 修法:只跳过依赖队伍的那部分

`GetPlayerParty` 改用 `find`:

```cpp
        // find, not operator[]: indexing would silently insert a brand new party
        // with a leader id of zero whenever the id no longer resolves, and every
        // caller would then treat that empty party as a real one. Reporting "no
        // party" is what the callers already handle.
        const auto itor = m_parties.find(*inviterPartyComponent.JoinedPartyId);
        if (itor != std::end(m_parties))
            return &itor->second;
```

`OnQuestChanges` 里 `pLeader` 与 `SceneMaster` 改用三目表达式,`dedupHistory` 的引用**下移到 `if` 块内**:

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

**为什么是 `inParty && pParty != nullptr` 而不是只判 `pParty`**:`IsPlayerInParty` 是「语义上在队伍」的门
(纯看 `JoinedPartyId.has_value()`),`pParty != nullptr` 只是防解引用。`inParty` 为假时 `partyId` 是 `nullopt`,
块内 `partyComponent.JoinedPartyId == partyId` 会退化成 `nullopt == nullopt` 而误匹配,所以这道门不能放开。

**为什么任务日志的记账留在 `if` 之外**:`Player::GetQuestLogComponent()` 全仓只有两个消费者 ——
`Code/server/Services/CharacterService.cpp:658` 的赋值与 `Code/server/Services/QuestService.cpp:50` ——
单人/未组队玩家的事件同样要被记录,不能因为「不在队伍」整体跳过。`SceneMaster` 写 0 也与
`Code/server/Services/PartyService.h:81` 的既有注释 `// PlayerIDs start at 1, so zero is "not set."` 一致。

`SendToLeader` 按同函数既有风格加一条早退:

```cpp
    // The leader can be gone already: the caller resolves it from the party's
    // stored id, and a party outlives the player row it names until the leave
    // event runs. Send to nobody rather than dereferencing null.
    if (!apLeader)
    {
        spdlog::warn("Party leader no longer exists, canceling broadcast.");
        return;
    }
```

### 51.4 复核命令(照抄)

```powershell
# 1. 查询接口不再用 operator[]
Select-String -Path Code\server\Services\PartyService.cpp -Pattern 'GetPlayerParty|m_parties\[|m_parties.find'

# 2. dedupHistory 的引用只在 if 块内
Select-String -Path Code\server\Services\QuestService.cpp -Pattern 'pParty|dedupHistory|inParty &&'

# 3. 广播助手先判空再用
Select-String -Path Code\server\GameServer.cpp -Pattern 'SendToLeader|Party leader no longer exists'

# 4. 改动面与文档门禁
git diff --stat -- Code
python Tools\Scripts\check_docs.py
```

复核结果:`PartyService.cpp:71` 是 `m_parties.find`、`:76` 是 `return nullptr;`;
`QuestService.cpp:46` 是 `pParty`、`:47` 是三目表达式 `pLeader`、`:58` 是三目表达式 `SceneMaster`、
`:117` 是 `if (inParty && pParty != nullptr)`、`:119` 是块内的 `auto& dedupHistory`;
`GameServer.cpp:766-770` 是 `if (!apLeader)` 早退;`python Tools/Scripts/check_docs.py` → `DOC CHECK OK`。

> **教训**:「查一个可能不存在的对象」这类接口,**返回值类型就是契约** ——
> 返回指针的接口,调用方就会(也应当)写 `if (!p)`;一旦实现里用了 `operator[]`,
> 它就从「查询」变成了「查询并创建」,而所有调用方都还按旧契约在写判空,
> 于是**判空永远为假、假对象被当成真对象用**。判据不在文档里,在同一个文件里:
> 同一个 `m_parties` 上,`GetById` 用 `find`、`GetPlayerParty` 用 `[]`,两处不一致就是缺陷本身。
> 反过来,写修复时也别顺手扩大范围 —— 「拿到 null 就整体早退」会把不属于队伍的功能一起关掉
> (这里是 quest log 记账),正确做法是把**依赖那个对象的部分**圈进 `if` 块,而不是提前 return。

## 52. 2026-09-29 场次:「走过循环」不等于「送出去了」,以及引擎给的指针不判空

> **锚点**:本节的修复落在 `Code/client/Systems/FaceGenSystem.cpp`、`Code/server/Services/CharacterService.cpp`。

### 52.1 缺陷:动作队列在实体循环结束之后被无条件清空

`ProcessMovementChanges`(`Code/server/Services/CharacterService.cpp`)的实体循环跑完之后,对**全部**实体清空动作队列:

```cpp
// Code/server/Services/CharacterService.cpp(修复前,:1013-1017)
    m_world.view<AnimationComponent>().each([](AnimationComponent& animationComponent)
    {
        // Remove actions we've sent
        animationComponent.Actions.clear();
    });
```

注释写的是「we've sent」,但这个 `view` **不区分**「送出去了」和「只是走过了循环」。两条路径都只走循环、从不发送:

| 路径 | 位置 | 发生了什么 |
| --- | --- | --- |
| 实体提前跳过 | `:986 if (movementComponent.Sent == true) continue;` | **没有新移动、但队列里有新动作**(刚挥砍/施法,而 `Sent` 还是上一轮的 `true`) |
| 没有玩家在范围内 | `:994 if (!cellIdComponent.IsInRange(...)) continue;` 内层全部落空 | 有移动,但没有任何人会收到 `message.Updates[entity]`,于是 `:1009 update.ActionEvents = animationComponent.Actions;` 从未执行 |

而 `animationComponent.Actions` 全仓**只有两个读者**:这里的读出(`:1009`)与这里的清空(`:1016`),
写入只有 `:413 animationComponent.Actions.push_back(animationComponent.CurrentAction);` 一处 ——
所以清空就是**永久丢失**,不是「下一轮补发」。

**触发场景是日常的**:某个玩家在房间里对着墙挥砍,此时没有别人在范围内;随后另一个玩家走进来 ——
他看到的只是那个角色**静止站着**,那一次挥砍永远不会出现在他屏幕上。

### 52.2 判据:同一个文件里,factions 早就改成了「送出才清」

同一个 `ProcessMovementChanges` 的**上一节**(factions)已经修过一模一样的形状:

```cpp
// Code/server/Services/CharacterService.cpp:924-945
    bool queued = false;
    ...
        queued = true;
    ...
    // Only clear the flag once the change actually went out. An actor that nobody is near right now
    // stays dirty, so the next player to come into range still gets it instead of keeping whatever
    // factions it spawned with.
    if (queued)
        characterComponent.SetDirtyFactions(false);
```

判据就是**同文件内两种写法不一致**:factions 有 `queued`,动作队列没有。

### 52.3 修法:同一个 `queued` 模式 + 与回放缓存同宽的上界

```cpp
        bool actionsQueued = false;

        for (auto pPlayer : m_world.GetPlayerManager())
        {
            ...
            update.ActionEvents = animationComponent.Actions;

            actionsQueued = true;
        }

        // Only drop the queued actions once they actually went out. An actor that nobody is near right
        // now keeps them, so the next player to come into range still gets the swing or the cast
        // instead of the animation simply never happening on their screen. The backlog stays bounded
        // the same way the replay cache is: newest wins.
        if (actionsQueued)
        {
            animationComponent.Actions.clear();
        }
        else if (animationComponent.Actions.size() > kMaxPendingActionEvents)
        {
            animationComponent.Actions.erase(animationComponent.Actions.begin(), animationComponent.Actions.end() - kMaxPendingActionEvents);
        }
```

新增上界(`:48`):

```cpp
// An actor that nobody is near keeps its pending action events until somebody is, so that backlog has
// to be bounded the same way the replay cache is. Newest wins.
constexpr uint32_t kMaxPendingActionEvents = 32;
```

**为什么「送出才清」必须配一个上界**:清空原本顺带承担了「没人靠近也不会涨」这个职责,
改成条件清空之后,一个**长期没有玩家在范围内**的实体(例如玩家挂机时的召唤物、或房主一个人跑图)
会让队列一直攒下去。上界取 32 与 `Code/server/Game/Animation/ActionReplayCache.h:19` 的
`static constexpr uint32_t kReplayCacheMaxSize = 32;` 一致 —— 两处都是「最近 N 个动作」,语义相同。

**为什么不在 `Sent == true` 时也清**:那一支的正确行为恰恰相反 —— `Sent` 为真只说明
**移动**没变,不代表**动作**送过了;此时队列里可能正是这一轮新 push 进去的动作。

### 52.4 缺陷:蒙版着色器分支里两个引擎指针都不判空

`FaceGenSystem::Update`(`Code/client/Systems/FaceGenSystem.cpp`)的蒙版分支:

```cpp
// Code/client/Systems/FaceGenSystem.cpp(修复前,:86-90)
BSMaskedShaderMaterial* pMaterial = static_cast<BSMaskedShaderMaterial*>(pLightingShader->material);

BSFixedString name("");
auto pTexture = CreateTexture(name);
pTexture->buffer = CreateResourceView(s_textureHolder.Get(), 512, 512);
```

- `BSLightingShaderProperty::material` 是裸 `void*`(`Code/client/Games/Skyrim/NetImmerse/BSShaderProperty.h:11`),
  **蒙版着色器可以没有材质**;它在 `:126` 被写穿:`pMaterial->renderedTexture = pTexture;`。
- `CreateTexture` 的签名是 `using TCreateTexture = NiRenderedTexture*(__fastcall)(BSFixedString& aName);`(`:56`),
  **以返回空表示失败**;它在 `:90` 就被写穿。

两者都在 `FaceGenSystem::Update` 里,而这个函数是 `noexcept` —— 空指针就是直接 fault,
异常都没机会抛出。

**判据同样在同一个文件里**:`GetHeadTriBasedGeom`(`:22-51`)把这条链上**每一个**引擎返回值都判了空
(`if (pFaceNode && pActorBase)`、`if (pFacePart)`、`if (pHeadNode)`、`if (pGeometry) return pGeometry;`),
只有进入蒙版分支之后这两处没有。

修法:两处各自判空,**先 `DecRef` 再 return**(着色器在上面 `:80` 刚 `IncRef` 过,直接 return 会漏掉引用计数):

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

**为什么是 return 而不是继续**:两者都是这一帧拿不到的引擎资源,跳过去之后 `aFaceGenComponent.Generated`
保持 `false`(`:128` 才置真),下一帧自然会重试 —— 这与函数开头 `if (aFaceGenComponent.Generated) return;`(`:67`)
的既有节奏一致。

### 52.5 复核命令(照抄)

```powershell
# 1. 动作队列:清空只在 queued 分支内,上界在 else 分支
Select-String -Path Code\server\Services\CharacterService.cpp -Pattern 'kMaxPendingActionEvents|actionsQueued|Actions\.clear\(\)'

# 2. 清空不再无条件出现在循环之后
Select-String -Path Code\server\Services\CharacterService.cpp -Pattern 'view<AnimationComponent>\(\)\.each'

# 3. FaceGen:两处判空都在使用之前,且都先 DecRef
Select-String -Path Code\client\Systems\FaceGenSystem.cpp -Pattern 'if \(!pMaterial\)|if \(!pTexture\)|DecRef\(\)|renderedTexture|pTexture->buffer'

# 4. 改动面与文档门禁
git diff --stat -- Code
python Tools\Scripts\check_docs.py
```

复核结果:`CharacterService.cpp:48` 是 `constexpr uint32_t kMaxPendingActionEvents = 32;`、
`:993` 是 `bool actionsQueued = false;`、`:1017` 是 `actionsQueued = true;`、`:1024` 是 `if (actionsQueued)`、
`:1026` 是块内的 `animationComponent.Actions.clear();`、`:1028` 是 `else if (animationComponent.Actions.size() > kMaxPendingActionEvents)`、
`:1030` 是 `erase(begin(), end() - kMaxPendingActionEvents)`;原先那个无条件的
`m_world.view<AnimationComponent>().each(...)` 已不再存在。
`FaceGenSystem.cpp:92-96` 是 `if (!pMaterial) { pShaderProperty->DecRef(); return; }`、
`:101-105` 是 `if (!pTexture) { pShaderProperty->DecRef(); return; }`、`:107` 才是 `pTexture->buffer = ...`、
`:143` 才是 `pMaterial->renderedTexture = pTexture;`;`python Tools/Scripts/check_docs.py` → `DOC CHECK OK`。

> **教训**:「清理」这个动作**必须挂在「真的做完了」上**,而不是挂在「循环跑完了」上。
> 这两件事在**正常状态**下才碰巧相等 —— 一旦有 `continue`(跳过没变化的实体)或空的内层循环
> (没人在范围里),它们就分开了,而代码里没有任何东西会提醒你。
> 反过来说,**把「无条件清理」改成「条件清理」时,一定要接住原来由清理顺带承担的边界职责** ——
> 这里就是「没人靠近也不会涨」,所以必须同时补上界,否则修掉的是丢失、换来的是无界增长。
>
> 另一条:凡是**引擎给的返回值**(虚表里取出来的指针、以空表示失败的工厂函数),
> 都得当成可空处理。判据不在文档里,在**同一个函数里已经判空的那几处** ——
> `GetHeadTriBasedGeom` 层层判空、`CreateTexture` 的调用点却不判,两处不一致就是缺陷本身。
> 还有一条容易漏的细节:这种早退如果发生在 `IncRef()` 之后,就必须先 `DecRef()` 再 return,
> 否则引用计数会一帧一帧地涨。

## 53. 2026-09-29 场次:插件的缓存跨存档活着 —— 判据是「同一串清理里唯独它缺席」

> **锚点**:本节的修复落在 `plugins/OStimTogether/src/AddonBridge.h`、`AddonBridge.cpp`、
`main.cpp`,交付形式是 `Code/plugins/patches/OStimTogether/0003-clear-the-remote-addon-cache-on-load.patch`。

### 53.1 缺陷:两个以 FormID 为键的缓存全仓没有清理路径

`AddonBridge` 缓存**远程**玩家同步过来的 addon 状态,一级键是 `RE::FormID`:

```cpp
// plugins/OStimTogether/src/AddonBridge.h
std::unordered_map<RE::FormID, std::unordered_map<std::string, CachedOverlayState>> _remoteOverlays;
std::unordered_map<RE::FormID, std::unordered_map<std::string, CachedObjectState>> _remoteObjects;
```

写入两处、读取两处,**没有任何 `erase`/`clear`**:

| 位置 | 作用 |
| --- | --- |
| `AddonBridge.cpp:531` | `auto& cached = _remoteOverlays[actor->GetFormID()][cacheKey];` |
| `AddonBridge.cpp:617` | `_remoteObjects[actor->GetFormID()][key] = CachedObjectState{...};` |
| `AddonBridge.cpp:686-687` | `if (const auto it = _remoteOverlays.find(actorFormID); it != _remoteOverlays.end())` |
| `AddonBridge.cpp:690-691` | `if (const auto it = _remoteObjects.find(actorFormID); it != _remoteObjects.end())` |

`grep _remoteOverlays|_remoteObjects|ClearRemoteState|ResetRemoteState` 全仓的命中就只有上面
这 6 处 + 头文件 2 处声明 —— **没有第 7 处**。

FormID 在**新存档里会被重新分配**,所以旧存档留下的条目会在读档后命中的是**另一个**角色:
远程角色的 OCum 覆盖贴图与 OStim 物体状态会短暂按上一个存档的样子渲染。

### 53.2 判据:同一个进程里,同一串清理里唯独它缺席

`plugins/OStimTogether/src/main.cpp:212-223` 的 `case SKSE::MessagingInterface::kPreLoadGame:`
逐条清理了 **9 个**单例:

```cpp
OStimTogether::EquipmentLock::GetSingleton().ClearAllOStimTargets();
OStimTogether::EquipmentLock::GetSingleton().ClearManual();
OStimTogether::DefaultOutfitGuard::GetSingleton().RestoreAll();
OStimTogether::CoopSessionManager::GetSingleton().Reset();
OStimTogether::OStimBridge::GetSingleton().ResetRemoteState();
OStimTogether::FreeScenePhaseSync::GetSingleton().Reset();
OStimTogether::ParticipantAlignmentSync::GetSingleton().Reset();
OStimTogether::FreeSceneSelfOriginLock::GetSingleton().Reset();
OStimTogether::OCumStateSync::GetSingleton().Reset();
OStimTogether::PPAIntegration::GetSingleton().Reset();
```

**唯独 AddonBridge 缺席** —— 而它正是这一层里**唯一**缓存跨玩家状态的那个。
同构先例不止一处:`OStimBridge::ResetRemoteState()`(`OStimBridge.cpp:1623-1649`)一次清掉
15 个成员 map(`_remoteMirrorThreads`/`_remoteToLocal`/`_localToRemote`/`_localAlignmentMasks`/
`_localSelfIndices`/`_sceneCenters`/`_authoritativeActorPoses`/`_pendingWallStarts`/
`_strProxyPoseGuardAfter`/`_lastSTRProxyPoseGuardLog`/`_pendingInitialAnimationReplay`/
`_lastAnimationRefresh`/`_lastDirectEventLog`/`_wallRootProbeUntil`/`_lastWallRootProbeLog`/
`_lastForcedEvents`),`MorphSyncService::Reset()`(`MorphSyncService.cpp:229-247`)同样清三个 map。

**判据是「同一串清理里唯独它缺席」**:一个缓存没有清理路径,单看它自己是看不出来的 ——
只有把它放回「同族都在哪里被清」的清单里,缺席才显形。

### 53.3 修法:补一个 ClearRemoteState,并挂进同一串清理

```cpp
// plugins/OStimTogether/src/AddonBridge.h
// Drops every cached remote addon state. The cache is keyed by FormID
// and a new game hands those ids out again, so state captured for one
// save would otherwise be replayed onto whoever now carries that id.
void ClearRemoteState();

// plugins/OStimTogether/src/AddonBridge.cpp
void AddonBridge::ClearRemoteState()
{
    std::scoped_lock lock(_stateMutex);
    _remoteOverlays.clear();
    _remoteObjects.clear();
}

// plugins/OStimTogether/src/main.cpp(kPreLoadGame 分支末尾)
OStimTogether::AddonBridge::GetSingleton().ClearRemoteState();
```

**为什么持锁**:两个 map 会被 `ScheduleRemoteStateReapply` 在游戏线程上读(`AddonBridge.cpp:675-687`
把两个 map 拷成局部副本),而 `ClearRemoteState` 在 SKSE 消息回调里调用 —— 不持锁就是数据竞争。
`std::scoped_lock` 与文件里其它锁点写法一致。

**为什么不是 `Reset()`**:这个类的公开面只有 `Register`/`ProcessEvent`/`HandleRemotePacket`/
`ScheduleRemoteStateReapply`,缓存之外没有别的状态要复位,所以只清缓存、名字也照实叫。

### 53.4 交付:补丁而不是子模块提交

`plugins/OStimTogether` 是**本项目不拥有、也无法推送**的子模块,而 CI 跑的是
`git submodule update --init --force --recursive --depth=1` —— **工作树里对子模块的任何改动都会
被丢弃**。所以修复必须做成 `Code/plugins/patches/OStimTogether/0003-clear-the-remote-addon-cache-on-load.patch`
(本仓库内容,随仓库评审/版本化/打包),由 `plugin_patches.py apply` 在配置与构建前打到子模块工作树上。

补丁正文就是 `git -C plugins/OStimTogether diff -- <那三个文件>` 的原样输出
(标准 `diff --git` / `--- a/` / `+++ b/` / `@@` 段),新增行里带解释性注释。

### 53.5 复核命令(照抄)

```powershell
python Code\plugins\tools\plugin_patches.py verify
python Code\plugins\tools\plugin_patches.py check
Select-String -Path plugins\OStimTogether\src\AddonBridge.cpp -Pattern 'ClearRemoteState|_remoteOverlays\.clear|_remoteObjects\.clear'
Select-String -Path plugins\OStimTogether\src\main.cpp -Pattern 'ClearRemoteState'
Select-String -Path Code\plugins\patches\OStimTogether\*.patch -Pattern 'ClearRemoteState'
python Tools\Scripts\check_docs.py
```

复核结果:`plugin_patches.py verify` → `PATCH VERIFY OK` / `9 patch(es) apply to their pinned commits`;
`check` → `PATCH CHECK OK`;`AddonBridge.cpp:639-644` 是 `ClearRemoteState()`(锁内两次 `clear()`)、
`:531`/`:617`/`:686-691` 四个读写点未变;`main.cpp:223` 是新增的 `ClearRemoteState()` 调用;
`Code/plugins/patches/OStimTogether/0003-clear-the-remote-addon-cache-on-load.patch` 覆盖三个文件;
`python Tools/Scripts/check_docs.py` → `DOC CHECK OK`。

> **教训**:一个**缓存**的缺陷不在它的读写逻辑里,而在「**谁负责清它**」这个没人写下来的约定里 ——
> 键是 FormID、EntityId、ConnectionID 这类**会被重新分配**的 id 时,缓存的生命周期就**必须**短于
> 一次读档;判据不是去看这个类,而是去列「同族在什么地方被清」,看谁不在名单上。
> 还有一条流程上的:子模块里的修复**只存在于工作树就等于不存在**,CI 的
> `submodule update --force` 会把它抹掉;要么做成补丁进本仓库,要么它永远进不了发行包。

## 54. 2026-09-29 场次:判据的**形状**决定了清扫的边界 —— 「同一表达式内解引用」漏掉「先取值、后解引用」

> **锚点**:本节的修复落在 `Code/client/Services/Generic/MagicService.cpp`、
> `Code/client/Services/Generic/PlayerService.cpp`。

### 54.1 缺陷:慢速法术的 hack 把「已经判过空的目标」换成玩家,玩家不在时就是空

`MagicService::OnNotifyAddTarget`(`Code/client/Services/Generic/MagicService.cpp`)在解析阶段
每一步都判了空 —— 法术 `:392`、效果 `:406`、目标 `:413`、施法者 `:421-428` ——
然后慢速法术的 hack 把目标**整个换掉**:

```cpp
// Code/client/Services/Generic/MagicService.cpp(修复前,:454-458)
    // This hack is here because slow time seems to be twice as slow when cast by an npc
    if (pEffect->IsSlowEffect())
        pActor = PlayerCharacter::Get();

    pActor->magicTarget.AddTarget(data, acMessage.ApplyHealPerkBonus, acMessage.ApplyStaminaPerkBonus);
```

`:413-419` 刚刚为 `pActor` 判过空并且为空时**入队重试**,`:456` 一行就把这个保证作废。
同一段代码里还有第二处:`:433 acMessage.CasterId && (pCaster = PlayerCharacter::Get());`,
它把 `pCaster` 写成空,而这个值经 `:440 data.pCaster = pCaster;` 一路交给引擎
(`MagicTarget::AddTarget` → `RealAddTarget`,见 `Code/client/Games/Skyrim/Magic/MagicTarget.cpp:45-58`)。

**可达性**:`PlayerCharacter::Get()` 是 `POINTER_SKYRIMSE(PlayerCharacter*, s_character, 401069)`
(`Code/client/Games/Skyrim/PlayerCharacter.cpp:46`),主菜单与读档期间为空;而这条路径**不需要玩家存在**
就能走到 —— `OnNotifyAddTarget` 由收包触发(`m_notifyAddTargetConnection`,`MagicService.cpp:45`),
而且 `ApplyQueuedEffects`(`:531-640`)每帧(`:55`)会把队列里没送出去的旧事件**重放**,
那时玩家可能已经不在。`IsSlowEffect()` 只看效果的 archetype
(`Code/client/Games/Skyrim/Magic/EffectItem.cpp:16-19`:`eArchetype == kSlowTime`)⇒ 任何远程
Slow Time 系法术都能走到。

### 54.2 缺陷:倒地补偿在 1.5 秒之后才用那个玩家指针

```cpp
// Code/client/Services/Generic/PlayerService.cpp(修复前,:293-300)
    if (m_knockdownStart && cNow >= m_knockdownDeadline)
    {
        PlayerCharacter* pPlayer = PlayerCharacter::Get();

        PlayerCharacter::SetGodMode(true);
        m_godmodeStart = true;

        pPlayer->currentProcess->KnockExplosion(pPlayer, &pPlayer->position, 0.f);
```

`m_knockdownStart` 是 `:278-279`(在 `RunRespawnUpdates` 里,该函数 `:230-232` **已经**判过空)
设下的,而这一支要等 `m_knockdownDeadline`(1.5 秒)**之后**才执行 —— 这 1.5 秒里玩家可能已经回主菜单
或去读档。窗口比 54.1 窄,但同一条路径:取值时还在、解引用时不在。

### 54.3 判据:上一轮清扫的验收条件是一条**形状**判据

上一个空指针提交(`75ceca79 fix: stop dereferencing a player that does not exist yet`)的验收条件原文是:

```
Verified - 'PlayerCharacter::Get()->' across Code/client: no matches, was 18.
```

那是一条**形状**判据:只覆盖「在同一个表达式里就解引用」。本轮把同一个问题重问成
「**取值之后**才解引用的站点」:

```powershell
Select-String -Path Code\**\*.cpp -Pattern '= PlayerCharacter::Get\(\);'
```

`Code/**/*.cpp` 命中 **31 处** —— 全部在上一轮的判据之外。逐个读完的结果:大部分是安全的
(布尔短路 `pPlayer && pPlayer->GetNiNode()`、`if (!pPlayer) return;`、`pPlayer ? ... : nullptr`、
唯一调用点已判空的 `DiscoveryService::VisitExteriorCell`、只在调试菜单里手动触发的 `Code/client/Debug/**`、
以及 `QuestService.cpp:57` 那种**在 `/* */` 注释块里**的死代码),但 `MagicService.cpp:433/456` 与
`PlayerService.cpp:295` 是真的漏网 —— 它们是「取值 → 用别的分支 → 再解引用」,取值与解引用之间隔了
几十行,不在「同一表达式」这个形状里。

**教训的形状**:一次清扫的**判据**就是它的边界。判据写成语法形状(`X()->`),清扫就只覆盖那个形状;
要覆盖语义(「这个指针可能为空,而它在被解引用之前有没有被检查过」),判据必须写成
「取值点 + 使用点」而不是单个表达式。

### 54.4 修法:拿到非空才覆盖,原值保留

三处都改成守卫式,并且**不引入新的早退**:

```cpp
// Code/client/Services/Generic/MagicService.cpp(修复后,:433-439)
        // The player is null on the main menu and while a save is loading, where there is nothing to redirect the
        // cast to. The caster resolved and checked above is kept instead of being overwritten with a null.
        if (acMessage.CasterId)
        {
            if (auto* pPlayer = PlayerCharacter::Get())
                pCaster = pPlayer;
        }
```

```cpp
// Code/client/Services/Generic/MagicService.cpp(修复后,:461-468)
    // This hack is here because slow time seems to be twice as slow when cast by an npc
    if (pEffect->IsSlowEffect())
    {
        // The player is null on the main menu and while a save is loading, where the effect stays on the target
        // resolved above rather than being moved onto a player that is not there.
        if (auto* pPlayer = PlayerCharacter::Get())
            pActor = pPlayer;
    }
```

```cpp
// Code/client/Services/Generic/PlayerService.cpp(修复后,:295-313)
        PlayerCharacter* pPlayer = PlayerCharacter::Get();

        // The player is null on the main menu and while a save is loading. This branch runs a second and a half
        // after the knockdown was recorded, so the save can have been left in the meantime. The pending ragdoll is
        // dropped rather than applied to a player that is not there; the invulnerability window below is unaffected.
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

**为什么不早退**:慢速法术那两个 hack 只是「把效果挪到玩家身上」,玩家不在时**留在已经解析并判过空的
目标身上**就是正确的降级 —— 丢掉整条远程法术反而是新的行为改变(还会让 `:458` 的
`AddTarget` 整个不执行,同一条包里的其它效果也一起丢)。`PlayerService` 那处的
`m_knockdownStart = false;` 特意留在 `if` 之外,是为了让「玩家不在」的这一次也把倒地状态收尾,
不至于每帧重试同一个分支。

### 54.5 复核命令(照抄)

```powershell
Select-String -Path Code\client\Services\Generic\MagicService.cpp -Pattern 'PlayerCharacter::Get'
Select-String -Path Code\client\Services\Generic\PlayerService.cpp -Pattern 'PlayerCharacter::Get'
Select-String -Path Code\**\*.cpp -Pattern 'PlayerCharacter::Get\(\)->'
python Tools\Scripts\check_docs.py
```

复核结果:`MagicService.cpp:437` 与 `:466` 都是 `if (auto* pPlayer = PlayerCharacter::Get())` 守卫、
`:470` 的 `pActor->magicTarget.AddTarget(...)` 之前 `pActor` 仍由 `:413-419` 判空;
`PlayerService.cpp:300` 是 `if (pPlayer)` 守卫、`:312 m_knockdownStart = false;` 在守卫之外;
`PlayerCharacter::Get()->` 仍为 **0 命中**;`python Tools/Scripts/check_docs.py` → `DOC CHECK OK`。

> **教训**:一次清扫的**验收条件**就是这次清扫的**边界**,而它往往是一句语法形状
> (`X()->` / `if (!p)` / `: ` 后面直接跟解引用)。形状判据会**漏掉跨语句的同类缺陷** ——
> 取值与解引用被中间几十行分开时,它看不见。所以下一轮要把判据从形状升级为语义:
> 先列出**全部取值点**(`= X::Get();`),再逐个回答「到下一次解引用之间,有没有一条路径让它是空」。
> 另外一条:这类「为修 A 而覆盖掉刚才的判空」的代码(54.1 的 `pActor = PlayerCharacter::Get();`)
> 是**判空保证的局部作废** —— 改动一个已经判过空的变量时,应当问「我这一步是否把它的保证丢了」。


## 55. 2026-09-29 场次:启动期的解析错误没有第二个机会 —— 一个参数打错,服务器在监听之前就死了

> **锚点**:本节的修复落在 `Code/components/console/CommandSettingsProvider.cpp`。

### 55.1 缺陷:命令行设置解析会抛,而整条启动路径上没有人接

**现象**:给专用服务器带上 `--uPort=abc`(或任何写错的数值参数),进程在打印任何一行日志之前就退出,
不监听端口、不写崩溃转储;把参数去掉一切正常。

`Code/components/console/CommandSettingsProvider.cpp`(修复前)有两个抛点。第一个在解析循环里:

```cpp
for (auto i = 1; i < argc; ++i)
{
    std::string value = argv[i];
    value = value.substr(value.find_first_not_of('-'));
    ...
}
```

第二个在写入设置项的 switch 里:

```cpp
case SettingBase::Type::kBoolean: setting->data.as_boolean = args[pName] == "TRUE" || args[pName] == "true"; break;
case SettingBase::Type::kInt: setting->data.as_int32 = std::stol(args[pName]); break;
case SettingBase::Type::kUInt: setting->data.as_uint32 = std::stoul(args[pName]); break;
case SettingBase::Type::kInt64: setting->data.as_int64 = std::stoll(args[pName]); break;
case SettingBase::Type::kUInt64: setting->data.as_uint64 = std::stoull(args[pName]); break;
case SettingBase::Type::kFloat: setting->data.as_float = std::stof(args[pName]); break;
```

- `std::string::substr(npos)` 抛 `std::out_of_range`:参数是空串、或**全由 `-` 组成**时
  `find_first_not_of('-')` 返回 `npos`。
- `std::stol` / `std::stoul` / `std::stoll` / `std::stoull` / `std::stof` 对解析不了的输入抛
  `std::invalid_argument`(`abc`、空串)或 `std::out_of_range`(超出该类型能表示的范围)。

### 55.2 判据:这条路径上没有第二个机会,而另外两条同字段的路径早就不抛

调用链:`Code/server_runner/main.cpp:233 std::make_unique<DediRunner>(argc, argv)`
→ `Code/server_runner/DediRunner.cpp:34 CreateGameServer(m_console, [this, argc, argv]() { LoadSettings(argc, argv); })`
→ `Code/server/main.cpp:100 aCallback();`
→ `Code/server_runner/DediRunner.cpp:53 LoadSettingsFromCommand(m_console, argc, argv);`。

**判据 A —— 全仓 `catch` 只有 8 处,这条链上 0 处**:`grep "catch" Code` 的 8 处分别是
`Code/immersive_launcher/stubs/DllBlocklist.cpp:174`、`Code/server/Services/ScriptService.cpp:121`、
`Code/server/Services/ScriptService.cpp:213`、`Code/client/CrashHandler.cpp:503`、
`Code/client/Games/Skyrim/CellRespawnOverrides.cpp:101` 等,`Code/server_runner/**` 与
`Code/server/main.cpp` **各 0 处**。异常要逃出的是 `main()` 的调用栈。

**判据 B —— 写同样五个字段的另外两条路径都用不抛的解析器**:

| 路径 | 解析器 | 解析失败的行为 |
| --- | --- | --- |
| `set` 控制台命令 `Code/components/console/ConsoleRegistry.cpp:141-145` | `ConvertStringValue(value.c_str(), pSetting->data.as_int32)` | 保持原值 |
| `.ini` 载入 `Code/components/console/IniSettingsProvider.cpp:122-126` | `GetIniValue` → `ConvertStringValue<TVal>(szValue, a_nDefault)` | 保持原值 |
| 命令行载入 `Code/components/console/CommandSettingsProvider.cpp`(修复前) | `std::stol` / `std::stoul` / `std::stoll` / `std::stoull` / `std::stof` | **抛,进程结束** |

`Code/components/console/ConsoleUtils.h:20-26` 的注释原文是 "on error, you get the default"。

**判据 C —— 只有这一条没有第二次机会**:`.ini` 出错只是某个值保持默认,服务器照常起来;`set`
出错同理,用户看得见报错再改一次。命令行是**唯一**一条「参数写错 = 服务器起不来」的路径,
而它恰好用了会抛的那族解析器。

**可触发面**:全仓 `Console::Setting` 共 22 处,全部在 `Code/server/**` 与
`Code/server_runner/DediRunner.cpp:20`。用户能从命令行写到的数值项有
`GameServer:uPort`(`Code/server/GameServer.cpp:28`)、`GameServer:uMaxPlayerCount`(`:29`)、
`Gameplay:uDifficulty`(`:39`)、`GameServer:uQuestHistoryExpiration`
(`Code/server/Services/QuestService.cpp:17`)、`Gameplay:fGoldLossFactor`
(`Code/server/Services/PlayerService.cpp:24`)。

### 55.3 修法:跳过没有非横线字符的参数,数值改走同族的 ConvertStringValue

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
// Code/components/console/CommandSettingsProvider.cpp(修复后,:50-59)
// ConvertStringValue rather than the std::sto* family: those throw on a value they
// cannot parse, and this runs from the runner constructor where nothing catches, so a
// typo in an argument would take the server down before it ever listens. The "set"
// console command already writes these same fields this way.
case SettingBase::Type::kBoolean: setting->data.as_boolean = args[pName] == "TRUE" || args[pName] == "true"; break;
case SettingBase::Type::kInt: setting->data.as_int32 = ConvertStringValue(args[pName].c_str(), setting->data.as_int32); break;
case SettingBase::Type::kUInt: setting->data.as_uint32 = ConvertStringValue(args[pName].c_str(), setting->data.as_uint32); break;
case SettingBase::Type::kInt64: setting->data.as_int64 = ConvertStringValue(args[pName].c_str(), setting->data.as_int64); break;
case SettingBase::Type::kUInt64: setting->data.as_uint64 = ConvertStringValue(args[pName].c_str(), setting->data.as_uint64); break;
case SettingBase::Type::kFloat: setting->data.as_float = ConvertStringValue(args[pName].c_str(), setting->data.as_float); break;
```

**为什么不是 `try`/`catch`**:`CODE_GUIDELINES.md:47` 明文 "Don't use exceptions, don't use STL code
that can throw, use the nothrow version if available or the unsafe version.";而且这里要的语义是
「这个参数不认识,就跳过它」,不是「这个参数不认识,就整体失败」—— 一条写错的参数不该让同一行里
`--uMaxPlayerCount=32` 这种**正确的参数**也一起失效。`ConvertStringValue` 失败时保留
`setting->data` 里的原值(该设置项的默认值),与 `.ini` 载入、`set` 命令的行为完全一致。

### 55.4 复核命令(照抄)

```powershell
Select-String -Path Code\components\console\CommandSettingsProvider.cpp -Pattern 'std::sto'
Select-String -Path Code\components\console\CommandSettingsProvider.cpp -Pattern 'find_first_not_of|ConvertStringValue'
python Tools\Scripts\check_docs.py
```

复核结果:`std::sto` → **0 命中**;`find_first_not_of` 命中修复点一处、`ConvertStringValue` 命中
五个数值 case 加注释;`python Tools/Scripts/check_docs.py` → `DOC CHECK OK`。

> **教训**:启动期的错误**没有第二次机会** —— 同一个解析器放在运行期,写错只是「这次没生效」,
> 放在启动期就是「服务没起来」,而两者在代码里长得**一模一样**(同一批字段、同一个 switch)。
> 所以「这个解析器会不会抛」不是一个孤立的问题,要先问「它跑在哪条路径上、那条路径上有没有人接」。
> 另一条:一个仓库里对**同一批字段**存在两种解析器时,要问「为什么这里不用那个不抛的」——
> 这里答案是「没人写过」,而不是「这里需要抛」。
## 56. 2026-09-29 场次:一个容器同时有「查」和「建」两种语义时,查询方会造出它要查的东西

> **锚点**:本节的修复落在 `Code/server/Services/PartyService.cpp` 与 `Code/server/Services/QuestService.cpp`。
> 本次没有子模块改动,三个文件都在主仓库里,不需要 `Code/plugins/patches/**` 补丁。

### 56.1 缺陷:踢人接口不看对方在不在自己队里,而查询接口会凭空建队

**现象**:服务端的队伍系统有**两个**独立的故障,都由同一批客户端可控的 id 触发。

1. **跨队踢人**:任何一个队伍的队长,可以用 `PartyKickRequest` 把**别的队伍**里的玩家踢出去,
   只要猜得到对方的 `PlayerId`。被踢的玩家莫名其妙地离开队伍。
2. **队伍凭空出现**:`GetPlayerParty()` 这类「查询」接口用的是 `m_parties[id]`。`JoinedPartyId`
   还在、但队伍已经被销毁时,这个下标会**静默插入**一个全新的空队伍 —— 于是每个调用者都会把
   这个空队伍当成真队伍继续用,而它的 `LeaderPlayerId` 是**未初始化的**。

### 56.2 判据:同一个文件里两种写法并存,其中一种是错的

`Code/server/Services/PartyService.cpp` 里同时存在两种取队伍的方式。`GetById()`(`:36-43`)一直用
`find` 并判 `end`:

```cpp
const PartyService::Party* PartyService::GetById(uint32_t aId) const noexcept
{
    auto itor = m_parties.find(aId);
    if (itor != std::end(m_parties))
        return &itor->second;
    return nullptr;
}
```

而另外七处用的是 `m_parties[...]`(修复前行号):`:160` `OnPartyChangeLeader`、`:194` `OnPartyKick`、
`:228` `OnPlayerJoin`、`:271` `OnPartyInvite`、`:318` `OnPartyAcceptInvite`、`:362`
`RemovePlayerFromParty`,以及 `:62-77` 的 `GetPlayerParty()` 本身。**「同一个文件里两种写法并存」
本身就是判据** —— 它说明其中一种不是有意选的,只是没人回头统一过。

判据一,`erase(std::find(...))` 没有配对 `end()`:

```cpp
// Code/server/Services/PartyService.cpp(修复前)
Party& party = m_parties[id];                  // 队伍不存在时会插入一个空队伍
auto& members = party.Members;
members.erase(std::find(std::begin(members), std::end(members), apPlayer));  // 找不到时 erase(end())
```

`std::find` 找不到时返回 `end()`,而 `vector::erase(end())` 是**未定义行为**(空 vector 上会把
size 减成 `SIZE_MAX`)。跨队踢人时 `apPlayer` 本来就不在 `members` 里,所以这条路径**必然**走到
`erase(end())` —— 而它恰好是 `if (members.empty())` 成立的那条分支。

判据二,`operator[]` 插入的 `Party` 里 `LeaderPlayerId` 是未定义值:

```cpp
// Code/server/Services/PartyService.h:89-96(修复前)
struct Party
{
    uint32_t LeaderPlayerId;   // 没有默认初始化
    TiltedPhoques::Vector<Player*> Members;
    ...
};
```

所以 `if (party.LeaderPlayerId == player->GetId())` 是在**读未初始化内存**。这里没有用「未初始化就
崩」来定性 —— 它可能恰好等于请求者的 id,也可能不等于,两者都不是可用语义。

判据三,`OnPartyKick` 少一道门。它原本只有两道:`if (!pKick)`(目标存在)与
`if (party.LeaderPlayerId == player->GetId())`(请求者是队长)。**没有一道门问过「pKick 在不在请求者
的队伍里」**,而 `PartyMemberPlayerId` 是从线里直接读出来的:

```cpp
// Code/encoding/Messages/PartyKickRequest.cpp:11
PartyMemberPlayerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
```

同一批消息的另外三个字段也全是这样读的(`PartyInviteRequest.cpp:12`、
`PartyAcceptInviteRequest.cpp:12`、`PartyChangeLeaderRequest.cpp:11`),所以这四个 handler 的输入
都是**任意 32 位值**,唯一的门就是 `GetById` 能否解析出 `Player*`。

判据四,对照另外两个 handler 看它们做对了什么:`OnPartyChangeLeader`(`:163-172`)在 `party.Members`
里**找得到才改**(目标必须是本队成员);`OnPartyInvite`(`:271-276`)先判请求者在队里、再判它是队长。
`OnPartyKick` 是这一族里唯一没有成员归属校验的。

### 56.3 修法:七处 `operator[]` 一次改净,并补上归属校验

七处 `m_parties[...]` 全部改成 `find`/`GetById` + 判空,**只有** `OnPartyCreate` 里 `:117` 那一处
保留下标 —— 它的 key 刚由 `m_nextId++` 生成,必然不存在,那一次插入是有意的:

```cpp
// Code/server/Services/PartyService.cpp(修复后,GetPlayerParty)
// find, not operator[]: indexing would silently insert a brand new party
// with a leader id of zero whenever the id no longer resolves, and every
// caller would then treat that empty party as a real one. Reporting "no
// party" is what the callers already handle.
const auto itor = m_parties.find(*inviterPartyComponent.JoinedPartyId);
if (itor != std::end(m_parties))
    return &itor->second;
...
return nullptr;
```

```cpp
// Code/server/Services/PartyService.cpp(修复后,RemovePlayerFromParty)
const auto partyItor = m_parties.find(id);
if (partyItor != std::end(m_parties))
{
    Party& party = partyItor->second;
    auto& members = party.Members;

    // Only a member can leave. erase(end()) is undefined behaviour, and a party can be asked to
    // remove a player it does not contain: the kick handler takes the target id off the wire.
    const auto memberItor = std::find(std::begin(members), std::end(members), apPlayer);
    if (memberItor != std::end(members))
    {
        members.erase(memberItor);
        ...
```

```cpp
// Code/server/Services/PartyService.cpp(修复后,OnPartyKick)
const auto* pParty = GetById(*inviterPartyComponent.JoinedPartyId);
if (pParty != nullptr && pParty->LeaderPlayerId == player->GetId())
{
    // The target id arrives straight off the wire, so it can name a member of somebody else's
    // party. Only a leader may kick, and only a player in their own party.
    const auto& kickPartyComponent = pKick->GetParty();
    if (!kickPartyComponent.JoinedPartyId || *kickPartyComponent.JoinedPartyId != *inviterPartyComponent.JoinedPartyId)
    {
        spdlog::error("[PartyService]: Player {} is not in the party of {}", message.PartyMemberPlayerId, player->GetId());
        return;
    }

    spdlog::debug("[PartyService]: Kicking player {} from party", pKick->GetId());
    RemovePlayerFromParty(pKick);
    BroadcastPlayerList(pKick);
}
```

`OnPlayerJoin`(`:242-259`)那处的 `continue` 是有意的,不是 `break`:那个循环在找「第一个仍在队里的
玩家」,把一个「`JoinedPartyId` 有值但队伍已经没了」的玩家当成「找到」会**提前收尾**。

`Code/server/Services/QuestService.cpp:223` 的 `pParty` 未判空即解引用一并修掉 —— 那是
`GetPlayerParty()` 改成返回 `nullptr` 之后**唯一**没跟上的调用点:

```cpp
// Code/server/Services/QuestService.cpp(修复后,OnQuestSceneChanges)
// The component and the service can disagree: a party id that no party carries any more leaves
// this pointer null, and the scene master below would be read through it.
PartyService::Party* pParty = partyService.GetPlayerParty(pPlayer);
if (pParty == nullptr)
    return;

auto& dedupHistory = pParty->GetQuestStageDedupHistory();
```

同一行上原本还有一句 `Player* pLeader = isLeader ? pPlayer : ...GetById(pParty->LeaderPlayerId);`,
在 `OnQuestSceneChanges` 里**从未被使用**(全文件 `pLeader` 只有三处:本行赋值、`:47` 的三目赋值、
`:201` 的唯一使用点,后者在 `OnQuestChanges` 里)⇒ 直接删除,而不是给它补判空。

### 56.4 复核命令(照抄)

```powershell
Select-String -Path Code -Include *.cpp,*.h,*.hpp,*.inl -Pattern 'erase\(std::find'
Select-String -Path Code\server\Services\PartyService.cpp -Pattern 'm_parties\['
Select-String -Path Code\server\Services\PartyService.cpp -Pattern 'is not in the party of'
Select-String -Path Code\server\Services\QuestService.cpp -Pattern 'pParty == nullptr'
python Tools\Scripts\check_docs.py
```

复核结果:`erase(std::find` → **0 命中**;`m_parties[` → 只剩 `OnPartyCreate` 的 1 处
(`Code/server/Services/PartyService.cpp:117`);`is not in the party of` → 1 处(`:209`);
`QuestService.cpp` 的 `pParty == nullptr` → 2 处(`:47` 的三目、`:224` 的新判空);
`python Tools/Scripts/check_docs.py` → `DOC CHECK OK`。

> **教训**:一个容器同时承担「查」和「建」两种语义时(`operator[]` 两者都做),**查询方会造出它要查
> 的东西** —— 而且造出来的那个对象往往缺少不变量(这里是未初始化的 `LeaderPlayerId`),于是「查不到」
> 这个本该被上层处理的情况,被悄悄换成了「查到了一个空的」。这类缺陷的判据不在调用点,而在**同一个类里
> 两种写法并存**:`GetById` 用 `find`、`GetPlayerParty` 用 `operator[]`,两者对「不存在」的回答不同,
> 而调用者只能按其中一种写。修法不是给某一处加判空,是把**所有**查询路径统一到会回答「没有」的那一种。
> 另一条:「从线里读出来的 id」永远要问一句「它属于谁」。`OnPartyKick` 查了「这个人存在吗」和「我是队长吗」,
> 却没查「这个人在我的队里吗」—— 前两个问题都能用**请求者自己的**状态回答,只有第三个需要把两个对象
> 放在一起看,而正是它决定了 `erase` 的下标合不合法。
## 57. 2026-09-29 场次:注解解析只判了「正例」,其余全部落进越界分支 —— 而这条注解能从网络重放

> **锚点**:本节修复落在 OStimNG 子模块(`plugins/OStimNG`,pin
> `3954683bbdfcd34b2f9157012ed0c446d6eefd1e`)。该子模块本项目不拥有、也无法推送,所以按既有做法
> 做成 `Code/plugins/patches/OStimNG/0002-guard-the-animation-event-path.patch`。
> **`snapshots/**` 一个字都没动** —— 它存的是 pin 的原样,`plugin_snapshot.py verify` 会卡住。

### 57.1 缺陷:`OStimEvent` 只认「2 个字段」这一种正例

**现象**:OStimNG 场景里,一条字段数不是 2 的 `OStimEvent` 注解会让插件读越界;任何一段数字
写错的注解会让 `std::stoi` 抛异常,而异常从 `RE::BSTEventSink` 的事件槽逃出去 ⇒ `std::terminate`。

**根因**(`plugins/OStimNG/skse/src/Core/Thread.cpp:849-856`,修复前行号):

```cpp
std::vector<std::string> payloadVec = stl::string_split(a_event->payload.c_str(), ',');
if (payloadVec.size() == 2) {
    int actorIndex = GetActor(actor)->index;
    callEvent(payloadVec[0], actorIndex, std::stoi(payloadVec[1]), actorIndex);
} else {
    callEvent(payloadVec[0], std::stoi(payloadVec[1]), std::stoi(payloadVec[2]), std::stoi(payloadVec[3]));
}
```

注解有两个合法形状,写在 `data/SKSE/Plugins/OStim/list of annotations.txt:14-19`:
`OStimEvent.[event],[target]` 与 `OStimEvent.[event],[actor],[target],[performer]`。
代码把第一个判成正例,**把「不是它」当成了「就是第二个」**。

`stl::string_split`(`skse/src/Util.cpp:48-61`)会丢掉尾部的空 token:

```cpp
// plugins/OStimNG/skse/src/Util.cpp:48-61
std::vector<std::string> string_split(const std::string& text, char sep) {
    std::vector<std::string> tokens;
    size_t start = 0, end = 0;

    while ((end = text.find(sep, start)) != std::string::npos) {
        tokens.push_back(text.substr(start, end - start));

        start = end + 1;
    }

    if (text.substr(start) != "") tokens.push_back(text.substr(start));

    return tokens;
}
```

于是 `""` → size 0、`"a,"` → size 1、`"a,b,c"` → size 3 —— 三种都不是 2,三种都落进 `else`,
前两种读 `payloadVec[1..3]` 越界,第三种读 `payloadVec[3]` 越界。**这条 `else` 是唯一出口**,
它同时承担了「第二种合法形状」和「所有不合法输入」两种语义。

### 57.2 判据:payload 由谁决定,以及同一个仓库里已有的正确写法

**判据一,payload 不是只有本地动画图能给。** 注解文本确实写在 behaviour 数据里
(`data/Nemesis_Engine/mod/ostim/0_master/#0106.txt:1222`),但同一条 tag 也能**从网络重放**:

```cpp
// plugins/AnimSyncTogether/src/STRPMClient.cpp:436-437
const RE::BSFixedString inputEvent{ tag.c_str() };
const bool replayed = actor->NotifyAnimationGraph(inputEvent);
```

唯一过滤是 `:394` 的 `SyncRules::...ShouldSyncEvent(tag)`。⇒ 远端客户端能决定这段 payload,
它不是一个「只可能被作者写错」的输入。**这一条决定了缺陷的等级**,不是「作者手滑」而是
「对端可控」。

**判据二,同一个文件里已经有判空的正确写法。** `skse/src/Papyrus/PapyrusScenes.h` 里
`:18-28` 的 `GetActor` 先判 `thread` 再判 `actor`,而紧邻的 `:10` / `:15` 两个入口**一个都不判**:

```cpp
// plugins/OStimNG/skse/src/Papyrus/PapyrusScenes.h(修复前)
void AddActor(RE::StaticFunctionTag*, int64_t a_threadId, RE::Actor* a_actor) {
    auto tm = Threading::ThreadManager::GetSingleton();
    tm->GetThread(a_threadId)->AddActor(a_actor);          // :10 不判
}

void RemoveActor(RE::StaticFunctionTag*, int64_t a_threadId) {
    auto tm = Threading::ThreadManager::GetSingleton();
    tm->GetThread(a_threadId)->RemoveActor();              // :15 不判
}

Threading::ThreadActor* GetActor(int64_t a_threadId, RE::Actor* a_actor) {
    auto thread = Threading::ThreadManager::GetSingleton()->GetThread(a_threadId);
    if (thread) {                                          // :19 同一个文件,做对了
        auto actor = thread->GetActor(a_actor);
        if (actor) {
            return actor;
        }
    }
    logger::error("Actor {} not found in thread {}", a_actor->GetDisplayFullName(), a_threadId);
    return nullptr;
}
```

**判据三,同构的另一条入口做了范围校验。** `skse/src/Papyrus/PapyrusThread.h:275-281`:

```cpp
void CallEvent(RE::StaticFunctionTag*, int threadID, std::string eventName, int actor, int target, int performer) {
    Threading::Thread* thread = Threading::ThreadManager::GetSingleton()->GetThread(threadID);
    if (thread) {
        int size = thread->getActors().size(); // need to convert to unsigned int before comparing or shit goes sideways
        if (actor < 0 || actor >= size || target >= size || performer >= size) {
            return;
        }
```

`ProcessEvent` 这条**同构的入口**不做(它只判 `a_event` 与 `a_event->holder`,见 `:828-830`)。
⇒ 判据是**「同一个仓库里已经有做对的写法」**,不是「看起来像不像会空」。

**判据四,`GetActor` 明确会返回 nullptr。** `skse/src/Core/Thread.cpp:458-463`:

```cpp
ThreadActor* Thread::GetActor(int a_position) {
    for (auto& i : m_actors) {
        if (i.first == a_position) return &i.second;
    }
    return nullptr;
}
```

而 `ProcessEvent` 里有 **10 处** `GetActor(actor)->X()`(修复前行号:`:848 :858 :860 :864 :868
:870 :872 :874 :876 :898 :900 :902 :904 :907`)。`GetActor(actor)` 的输入是**事件里的 holder**,
它来自 `a_event->holder`,与 `m_actors` 里有没有这个人无关。

### 57.3 修法:一个不抛异常的解析器,加每个入口先判空

新增一个不抛异常的注解数字解析器(用 `std::from_chars`,不经过异常):

```cpp
// plugins/OStimNG/skse/src/Core/Thread.cpp(修复后,:26-53)
#include <charconv>

namespace {
    // Animation graph annotations are plain text, and the OStimEvent tag can be
    // replayed from the network, so payload numbers are parsed without throwing:
    // a malformed annotation must not escape a noexcept event sink as an
    // exception.
    bool parseAnnotationInt(const std::string& a_text, int& a_out, int a_base = 10) {
        const char* begin = a_text.data();
        const char* end = begin + a_text.size();

        while (begin != end && (*begin == ' ' || *begin == '\t')) {
            ++begin;
        }

        if (begin != end && *begin == '+') {
            ++begin;
        }

        if (a_base == 16 && end - begin >= 2 && begin[0] == '0' && (begin[1] == 'x' || begin[1] == 'X')) {
            begin += 2;
        }

        if (begin == end) {
            return false;
        }

        int value = 0;
        const auto result = std::from_chars(begin, end, value, a_base);
        if (result.ec != std::errc{}) {
            return false;
        }

        const char* rest = result.ptr;
        while (rest != end && (*rest == ' ' || *rest == '\t')) {
            ++rest;
        }

        if (rest != end) {
            return false;
        }

        a_out = value;
        return true;
    }
}
```

`OStimEvent` 分支改成**只接受 2 与 4 两种字段数**,其余丢弃 —— 注意两个形状各自解析成功才调用:

```cpp
// plugins/OStimNG/skse/src/Core/Thread.cpp(修复后,:908-935)
ThreadActor* threadActor = GetActor(actor);
if (!threadActor) {
    return RE::BSEventNotifyControl::kContinue;
}

if (tag == "OStimClimax") {
    threadActor->climax();
} else if (tag == "OStimEvent") {
    // The annotation is either event,target or
    // event,actor,target,performer. Anything else is malformed and is
    // dropped instead of being indexed past the end of the split.
    std::vector<std::string> payloadVec = stl::string_split(a_event->payload.c_str(), ',');
    if (payloadVec.size() == 2) {
        int target = 0;
        if (parseAnnotationInt(payloadVec[1], target)) {
            callEvent(payloadVec[0], threadActor->index, target, threadActor->index);
        }
    } else if (payloadVec.size() == 4) {
        int actorIndex = 0;
        int targetIndex = 0;
        int performerIndex = 0;
        if (parseAnnotationInt(payloadVec[1], actorIndex) && parseAnnotationInt(payloadVec[2], targetIndex) && parseAnnotationInt(payloadVec[3], performerIndex)) {
            callEvent(payloadVec[0], actorIndex, targetIndex, performerIndex);
        }
    }
}
```

同一族在这次一并修掉的还有:

- `ProcessEvent` 里 10 处 `GetActor(actor)->` 全部走函数入口取一次的 `threadActor`;同一段里
  9 处 `std::stoi` 全部换成 `parseAnnotationInt`(`OStimUndressPartial` / `OStimRedressPartial`
  走 base 16;`OStimSetLooking` 两段各自解析成功才 `insert`;`OStimBendSoS` 解析成功才
  `offsetSoSBend`;`OStimPeak` 还加了 `action >= 0 && action <= 255` 的范围校验,
  因为 `Graph::Action::Action::actionIndex` 是 `uint8_t`,`std::stoi` 的结果直接隐式截断)。
- `initContinue` 的 `GetActor(0)->getActor().getLightLevel()` 与 `loop()` 的
  `GetActor(0)->getActor().isInSameCell(...)` 各改成先落局部再判空。`loop()` 每帧可达
  (`skse/src/Core/ThreadManager.cpp:47 it.second->loop();`)。
- `RemoveActor()` 开头加 `if (m_actors.empty()) { return; }`,再对 `GetActor(index)` 判空 ——
  修复前它算的是 `int index = m_actors.size() - 1;`(空 `std::map` 上是 `SIZE_MAX`)。
- `NodeHandler::handlePeakAnnotation` 与 `nodeChanged` 各自把 `thread->getCurrentNodeInternal()`
  先落局部再判空。`m_currentNode` 的声明带 `= nullptr`(`skse/src/Core/Thread.h:125`),
  而 `handlePeakAnnotation` 从 `ProcessEvent` 的 `OStimPeak` 分支进来,**与 `Thread::loop()` 的
  `if (!m_currentNode) { return; }` 无关** —— 那条守卫在另一个调用栈上。
- `NodeHandler::threadEnd()` 在 `delete this` 之前先清掉宿主指针:

  ```cpp
  // plugins/OStimNG/skse/src/Threading/Thread/NodeHandler.cpp(修复后,:85-89)
  void NodeHandler::threadEnd() {
      EventUtil::invokeListeners(threadEndListeners);
      if (thread) {
          thread->clearNodeHandler();
      }
      delete this;
  }
  ```

  `nodeHandler` 是 `Thread` 的 **private** 成员(`skse/src/Core/Thread.h:149`),外部写不到,
  所以在 `Thread.h:116` 旁边加了一个 public 的 `inline void clearNodeHandler() { nodeHandler = nullptr; }`。
  修复前 `delete this` 之后 `Thread::nodeHandler` 仍是**悬垂指针**,而 `getNodeHandler()` 是
  public、`ProcessEvent` 的 `OStimPeak` 分支还会用它。
- `LoopPeakHandler::speedChanged()` 逐级判空并给下标做范围校验:

  ```cpp
  // plugins/OStimNG/skse/src/Threading/Thread/Peak/Loop/LoopPeakHandler.cpp(修复后)
  void LoopPeakHandler::speedChanged() {
      if (!nodeHandler) {
          return;
      }

      Threading::Thread* thread = nodeHandler->getThread();
      if (!thread) {
          return;
      }

      Graph::Node* currentNode = thread->getCurrentNodeInternal();
      if (!currentNode) {
          return;
      }

      int speed = thread->getCurrentSpeed();
      if (speed < 0 || speed >= static_cast<int>(currentNode->speeds.size())) {
          return;
      }

      scale = currentNode->speeds[speed].displaySpeed;
  }
  ```

  修复前是一行三重解引用加一个不判范围的下标:
  `scale = nodeHandler->getThread()->getCurrentNodeInternal()->speeds[nodeHandler->getThread()->getCurrentSpeed()].displaySpeed;`
- `PeakSoundPlayer` 的构造函数逐级判空后才注册监听(`SoundPlayerParams::actor` 默认是 `nullptr`)。
- `ThreadActor.cpp:402` 的 `thread->GetActor(i)->getActor()` 前插判空并 `continue`。
- `AlignMenu.cpp:110` 把 `GetSingleton()->currentThread->GetActor(selectedSlot)->getActor().getName()`
  拆成三步判空,名字先落到 `std::string` 再构造 `RE::GFxValue`(三目表达式两侧类型不同会编译不过;
  局部名用 `alignThread` 是因为同文件 `:83` 已经有 `currentThread`)。

### 57.4 复核命令(照抄)

```powershell
git -C plugins/OStimNG show 3954683bbdfcd34b2f9157012ed0c446d6eefd1e:skse/src/Core/Thread.cpp | Select-String -Pattern 'GetActor\(actor\)->|std::stoi|payloadVec.size'
python Code\plugins\tools\plugin_patches.py verify
python Code\plugins\tools\plugin_patches.py apply
python Code\plugins\tools\plugin_patches.py check
python Code\plugins\tools\plugin_snapshot.py verify
python Tools\Scripts\check_docs.py
```

复核结果:pin 上 `GetActor(actor)->` **10 处**、`std::stoi` **9 处**、`payloadVec.size() == 2` 1 处
(这一条正是 `plugin_patches.py verify` 的输入 —— 它用 `git apply --check --cached` 对着 **pin**
而不是工作树校验,所以工作树已经被 `apply` 过也能照跑);
`PATCH VERIFY OK`(**10** 个补丁)、`PATCH CHECK OK`、`SNAPSHOT OK`(8 个插件)、
`python Tools/Scripts/check_docs.py` → `DOC CHECK OK`。

补丁自身:`Code/plugins/patches/OStimNG/0002-guard-the-animation-event-path.patch`,
**8 个文件、16 个 hunk、+202/-51 行、LF-only、无 BOM**。改后 `Thread.cpp` 里
`GetActor(actor)` 只剩 1 处(新增的那次赋值)、`std::stoi` **0 处**。

> **教训**:「只判正例,其余全落 `else`」的写法,把一个**枚举**问题(这段文本属于哪个形状)
> 换成了**布尔**问题(它是不是形状 A)。而 `else` 里写的是形状 B 的解析 —— 于是「不是 A」被
> 静默当成「是 B」,越界读的就是 B 才有的那几个字段。判据不在 `if` 那一行,而在**`else` 的
> 入口有没有自己的条件**:当 `else` 分支假设了比 `if` 更强的前提时,它必须把这个前提**写出来**。
> 另一条:同一个仓库里往往已经有做对的同构写法(`PapyrusScenes.h` 的 `GetActor`、
> `PapyrusThread.h` 的范围校验),**「别处怎么写的」比「这里看起来会不会空」可靠得多**。

## 58. 2026-09-29 场次:线上读到的日历字段直接当了循环上界 —— 判据是「这个值接下来会被谁拿去数数」

> **锚点**:本节修复落在 `Code/encoding/Structs/TimeModel.h`、`Code/encoding/Structs/TimeModel.cpp`
> 与 `Code/tests/encoding.cpp`。同一个字段还被 `Code/server/Services/CalendarService.cpp` 用作
> 「谁的时间最靠前」的比较基准并**无条件**写回服务端日历,所以这里同时是**崩溃面**与**失步面**。

### 58.1 缺陷:五个字段从线上原样读入,读侧一个范围校验都没有

**现象**:专用服务器上,一个客户端在认证包里把日历字段写成 `Month = 0xFFFFFFFF`,服务端在
**接受他进服的那一瞬间**就会卡在 `GetTimeInDays()` 的月份循环里空转约 **42.9 亿次**;写成
`Day = 0xFFFFFFFF` 则卡在 `Update()` 的进位 `while` 里约 **1.4 亿次**。两者都让整个 tick
线程停住,所有玩家一起掉线。

**根因**(修复前):

```cpp
// Code/encoding/Structs/TimeModel.cpp(修复前,:17-21)
TimeScale = Serialization::ReadFloat(aReader);
Time = Serialization::ReadFloat(aReader);
Day = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
Month = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
Year = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
```

`& 0xFFFFFFFF` 对 `uint32_t` 是**恒等操作**,这五行等于什么都没做。而 `Month` 直接成了
循环上界:

```cpp
// Code/common/DateTime.cpp:38-46
float DateTime::GetTimeInDays() const noexcept
{
    float totalDays = static_cast<float>(m_timeModel.Year) * 365.f;
    for (uint32_t i = 0u; i < m_timeModel.Month; i++)
        totalDays += GetNumberOfDaysByMonthIndex(i);

    totalDays += m_timeModel.Day;
    totalDays += (m_timeModel.Time / 24.f);
    return totalDays;
}
```

`Day` 则是 `Update()` 里 `while (m_timeModel.Day > GetNumberOfDaysByMonthIndex(m_timeModel.Month))`
的减法次数 —— 每轮只减 31,所以要跑 `Day / 31` 次。同一族还有 `Time` 与 `TimeScale`:两者
都没有 NaN/Inf 检查,却被下面这两行拿去做「浮点转无符号整数」:

```cpp
// Code/common/DateTime.cpp:20-21
m_timeModel.Day += static_cast<uint32_t>(m_timeModel.Time / 24.f);
m_timeModel.Time = std::fmod(m_timeModel.Time, 24.f);
```

NaN 与负值在这里都是**未定义行为**(`static_cast<uint32_t>(NaN)`),不是「夹成 0」——MSVC
在 `/O2` 下可以把它折叠成任意值,而 `Day` 的初值是客户端给的。

### 58.2 可达性:解析发生在认证之前,而认证包绕开了那道判空

认证包在认证**之前**就被解析:`Code/server/GameServer.cpp:572 OnConsume` → `:592
factory.Extract(reader)`。`BindMessageHandlers` 里那道「拿不到 player 行就踢人」的判空
(`:262`)被 `AuthenticationRequest` 的专属分支(`:281-286`,`HandleAuthenticationRequest`
直连)绕开了。

服务端没设密码时(`:33-34` 两个 `Console::StringSetting` 默认空串,`:183
m_isPasswordProtected = strcmp(sPassword.value(), "") != 0`),`:905` 的
`acRequest->Token == sPassword.value()` 就是 `"" == ""`,判定为真。于是任何一个 `Version`
对得上(`Code/client/main.cpp:88-90` 只认 `1.5.`/`1.6.`/`1.7.` 前缀)、服务端没满、
SKSE/MO2 标志合格的客户端都能走到 `:1053` 的
`PlayerJoinEvent(pPlayer, acRequest->WorldSpaceId, acRequest->CellId, acRequest->PlayerTime)`。

**判据**:这里不是「认证之后才可信」的普通字段。判据是**「这个值接下来会被谁拿去数数」** ——
`Month` 会被 `for` 数、`Day` 会被 `while` 数、`Time`/`TimeScale` 会被转成整数再数,
那么它在**进循环之前**就必须已经被夹住,而不是指望调用方去夹。同族的 `Factions.cpp` 已经
按这个判据修过一轮(`std::min<uint64_t>(Serialization::ReadVarInt(aReader), kMaxFactionCount)`),
`TimeModel` 是同一族里漏掉的那个。

### 58.3 顺带的数据污染:一个客户端可以改写全服日历

`Code/server/Services/CalendarService.cpp:36-64 OnPlayerJoin` 拿客户端数据算
`playerHasFurthestTime`,一旦为真就把客户端的 `Day/Month/Year` **无条件**抄进服务端的
`m_dateTime`:

```cpp
// Code/server/Services/CalendarService.cpp:40-52
bool playerHasFurthestTime = acEvent.PlayerTime.GetTimeInDays() > m_dateTime.GetTimeInDays();
...
m_dateTime.m_timeModel.Day = acEvent.PlayerTime.m_timeModel.Day;
m_dateTime.m_timeModel.Month = acEvent.PlayerTime.m_timeModel.Month;
m_dateTime.m_timeModel.Year = acEvent.PlayerTime.m_timeModel.Year;
```

紧接着 `:54-58` 把服务端的 `m_dateTime` 抄进 `timeMsg.timeModel` 并
`:60-63 GameServer::Get()->SendToPlayers(timeMsg)` 广播给**所有人**。所以这条路径既是崩溃面
也是失步面:夹住范围同时也把这条路上的越界值挡掉了。

### 58.4 修法:边界声明在结构体上,夹在**读**侧

按 `Code/encoding/Structs/Factions.h:18-20` 的既有做法 —— 常量挂结构体、注释写清「为什么是夹
不是抛」:

```cpp
// Code/encoding/Structs/TimeModel.h(修复后)
// The engine keeps the month and the day zero based and the hour inside a single day,
// and the server refuses a time scale above this when one is set by hand. Clamp to them
// instead of throwing: Deserialize is called from noexcept frames, and every one of these
// values ends up as a loop bound or as the operand of a float to integer cast.
static constexpr float kMaxTimeScale = 1000.f;
static constexpr float kMaxTime = 24.f;
static constexpr uint32_t kMaxDay = 31u;
static constexpr uint32_t kMaxMonth = 11u;
static constexpr uint32_t kMaxYear = 999u;
```

**边界不是新发明的,全部取自服务端自己已有的判断**:`CalendarService::SetTimeScale` 的
`aScale >= 0.f && aScale <= 1000.f`(`:135-152`)、`SetDate` 的 `aMonth >= 0 && aMonth < 12`
与 `aYear >= 0 && aYear <= 999`(`:85-100`)、`std::fmod(Time, 24.f)`(`DateTime.cpp:21`)。
这是「同一个仓库里已经有做对的写法」这条判据的又一次应用 —— 服务端**手工**设置时间时早就
在用这些边界,只有**线上**那条路没用。

`Code/encoding/Structs/TimeModel.cpp` 里新增该目录**首个匿名命名空间**(`Code/encoding/**`
此前一个都没有),两个 helper 都标 `noexcept`:

```cpp
// Code/encoding/Structs/TimeModel.cpp(修复后)
namespace
{
// A value that is not a finite number cannot be clamped into a range, and both a NaN and an
// infinity would poison every arithmetic operation the caller performs on it afterwards.
float ClampFloat(float aValue, float aMax) noexcept
{
    if (!std::isfinite(aValue))
        return 0.f;

    return std::min(std::max(0.f, aValue), aMax);
}

uint32_t ClampUint(uint32_t aValue, uint32_t aMax) noexcept
{
    return std::min(aValue, aMax);
}
}
```

`Deserialize` 末尾追加:

```cpp
TimeScale = ClampFloat(TimeScale, kMaxTimeScale);
Time = ClampFloat(Time, kMaxTime);
Day = ClampUint(Day, kMaxDay);
Month = ClampUint(Month, kMaxMonth);
Year = ClampUint(Year, kMaxYear);
```

**两处刻意的取舍**:

- **`std::min`/`std::max` 而不是 `std::clamp`**。全仓 `std::clamp` **零命中**
  (`git grep -n 'std::clamp' Code` 只剩 `Code/client/Games/Skyrim/RTTI.cpp:5258`/`:5260`
  两条 RTTI **符号名**里的 `Clamp`),而 `std::min`/`std::max` 有现成先例
  (`Code/encoding/Structs/Mods.cpp:21`、`Code/encoding/Structs/Factions.cpp:44`)。
  跟着仓内既有写法走,而不是引入新惯用法。
- **`Serialize` 一个字都没改**。夹在**读**侧:客户端发出去的是它自己的原值,而
  `ServerTimeSettings` 的广播在两端也各过一次 `Deserialize`,所以服务端→客户端这条方向
  同样被夹住。改写侧反而会让服务端把它自己的越界值原样转发。

### 58.5 复核命令(照抄)

```powershell
git grep -n 'ReadVarInt(aReader)' -- Code/encoding/Structs/TimeModel.cpp
git grep -n 'kMaxTimeScale\|kMaxTime\|kMaxDay\|kMaxMonth\|kMaxYear' -- Code
git grep -n 'std::clamp' -- Code
git grep -n 'std::isfinite' -- Code/encoding
python Tools\Scripts\check_docs.py
```

复核结果:修复前 `TimeModel.cpp` 是 3 处 `ReadVarInt(aReader) & 0xFFFFFFFF`;
`std::clamp` 全仓 0 处(仅 RTTI 符号名);`Code/encoding` 里 `std::isfinite` 只有本处新增的
1 处;夹紧常量在 `Code/encoding/Structs/TimeModel.h` 声明、`TimeModel.cpp` 使用、测试
`Code/tests/encoding.cpp` 引用;`python Tools/Scripts/check_docs.py` → `DOC CHECK OK`。

**验证**:`Code/tests/encoding.cpp:549 TEST_CASE("TimeModel wire bounds", "[encoding.time_model]")`
用 `Serialization::WriteFloat/WriteVarInt` 手工造畸形包,三段:
NaN + `1.0e30f` + 三个 `0xFFFFFFFF` → `0.f` / `kMaxTime` / `kMaxDay` / `kMaxMonth` / `kMaxYear`;
`-20.f` / `-1.f` / 三个 `0` → `0.f` / `0.f`;以及 `20.f` / `12.f` / `4` / `7` / `201` 走
`Serialize`→`Deserialize` 回环相等。CI 的 `.github/workflows/windows.yml:594-638` 会跑
`TPTests.exe` 并要求输出含 `All tests passed`。

> **教训**:「从线里读出来的数」要问的第二个问题是**「它接下来会被谁拿去数数」** ——
> 第一个问题是「它是不是越界」,而这类缺陷恰恰是**没越界也可能致命**(`Month = 0xFFFFFFFF`
> 本身只是个数字,致命的是它当了 `for` 的上界)。所以判据不是「这个字段合法范围是多少」,
> 而是**「它下游有没有循环、下标、或浮点转整数」**:有,就必须在**读的那一刻**夹住,而不是
> 指望每个调用方各夹一次 —— 这个仓库里 `GetTimeInDays` 只有一个调用方,但那个调用方在
> `OnPlayerJoin` 的**第一行**,而且它前面还站着 `playerHasFurthestTime` 这个看起来无害的比较。
> 另一条:`& 0xFFFFFFFF` 这类「看起来在做校验」的掩码比没有更危险 —— 它让代码**读起来**像
> 已经处理过了,而它在 `uint32_t` 上是恒等操作。写的时候要问一句「这个掩码在**这个类型**上
> 到底做了什么」。

## 59. 2026-09-29 场次:一个「增量」字段既是值又是判决 —— 读侧不夹,它就是远程处决开关

> **锚点**:本节修复落在 `Code/encoding/Messages/RequestHealthChangeBroadcast.h`/`.cpp`、
> `Code/encoding/Messages/NotifyHealthChangeBroadcast.h`/`.cpp`、
> `Code/server/Services/ActorValueService.cpp` 与 `Code/tests/encoding.cpp`。
> 同一个字段在接收端被**加**到血量上,加完的结果又被拿去**判决死亡**,所以它同时是
> **崩溃面**(NaN 进 `ForceActorValue`)与**失步面**(服务端账本符号与客户端相反)。

### 59.1 缺陷:血量增量从线上原样读入,读侧一个值域校验都没有

**现象**:一个客户端发一条 `RequestHealthChangeBroadcast`,把 `DeltaHealth` 写成 `-1e38`
或 `-inf`,范围内**所有**客户端都会把那个 actor 的血量算成非正数,然后
`pActor->Kill()` —— 非玩家 actor 一律被远程处决;写成 `NaN` 则让 `ForceActorValue`
收到 NaN。

**根因**(修复前):

```cpp
// Code/encoding/Messages/RequestHealthChangeBroadcast.cpp(修复前,:12-13)
Id = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
DeltaHealth = Serialization::ReadFloat(aReader);
```

字段在接收端的用法(客户端 `Code/client/Services/Generic/ActorValueService.cpp:295-305`):

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

### 59.2 判据:一个字段同时是「值」和「判决依据」时,它的合法范围由下游的判决决定

判据不是「这个字段的合理范围是多少」,而是**「它加完之后会被谁拿去判决」**。
`DeltaHealth` 加完立刻进 `health <= 0.f` 这个比较 —— 于是「能加出非正数的量」就是
「能杀人的量」,而 `-1e38` 在任何血量上都满足它。同族的第二个判据是
**「非有限值会在下游变成什么」**:NaN 过 `ForceActorValue` 会污染引擎侧的 actor value,
而 `NaN <= 0.f` 为假(所以不 Kill)—— 同一个字段的两种畸形值走两条不同的坏路。

**同一个仓库里已经有做对的写法**:`Code/encoding/Structs/TimeModel.cpp:15` 的
`if (!std::isfinite(aValue))`、`Code/client/Games/Skyrim/Havok/hkStepInfo.h:22` 的
`if (!std::isfinite(aDeltaTime) || aDeltaTime <= 0.0001f)`、以及
`Code/encoding/Messages/PluginMessagingRequest.cpp:39-42` 的
「Never trust the wire length」+ `std::min<T>(...)` 钳位。本节只是把这三条既有写法
用在一个新字段上。

### 59.3 可达性:这个 handler 是同类四个里唯一没有所有权门的

`Code/server/Services/ActorValueService.cpp` 的四个 handler 里,`:31`(`OnActorValueChanges`)、
`:58`(`OnActorMaxValueChanges`)、`:110`(`OnDeathStateChange`)都写了
`IsCurrentOwner(acMessage.pPlayer, message.OwnershipEpoch)`,**只有 `:77-100`
`OnHealthChangeBroadcast` 没有**。`ActorValuesList[24]`(`ActorValueInfo::kHealth = 24`)
在服务端全仓只有 `:89`/`:90` 两处访问,且这个 handler 里的注释
`// TODO(cosideci): should server side health not be updated?` 说明作者自己也存疑。

### 59.4 顺带的语义不一致:服务端账本的符号与客户端相反

服务端 `:90` 修复前是 `currentHealth - message.DeltaHealth`,而客户端 `:295` 是 `+ acMessage.DeltaHealth`。
客户端的发送侧(`Code/client/Games/Skyrim/Actor.cpp:1093`/`:1142`/`:1169`)发的是
「受伤为负、治疗与回血为正」,所以服务端的减号会把**每一次受伤都记成加血**。
这份账本不是死数据:它经 `InitialActorValues`/`AllActorValues` 分发给新加入的玩家
(`Code/server/Services/CharacterService.cpp:109`/`:688`/`:883`),再由客户端
`Code/client/Games/Skyrim/Actor.cpp:836-843 Actor::SetActorValues` 逐键
`ForceActorValue(ForceMode::DAMAGE, ...)` 写回引擎 —— 即**新玩家看到的血量会反向漂移**。

### 59.5 修法:边界声明在结构体上,夹在**读**侧,两个方向都夹

按 `Code/encoding/Structs/Factions.h:18-20` 与 §58 的既有做法 —— 常量挂结构体、注释写清
「为什么是夹不是抛」:

```cpp
// Code/encoding/Messages/NotifyHealthChangeBroadcast.h(修复后,:10-12)
// The receiver adds this to an actor's health and kills the actor when the result
// stops being positive, so an unbounded value is a remote kill switch. A single hit
// cannot plausibly exceed this, and a peer that claims more gets a clamped hit.
static constexpr float kMaxHealthDelta = 100000.f;
```

```cpp
// Code/encoding/Messages/RequestHealthChangeBroadcast.cpp(修复后,:18-22)
// Never trust the wire: a NaN or an infinity would follow this value into every
// arithmetic operation the receiver performs on it.
if (!std::isfinite(DeltaHealth))
    DeltaHealth = 0.f;
else
    DeltaHealth = std::min(std::max(DeltaHealth, -kMaxHealthDelta), kMaxHealthDelta);
```

`Code/encoding/Messages/NotifyHealthChangeBroadcast.cpp` 同形 —— **两个方向都夹**:客户端
发来的请求与服务端广播的通知各自过一次 `Deserialize`,只夹一侧等于没夹。

**一处刻意的取舍:不加所有权门。** 客户端对「打远程玩家」本来就不发广播
(`Code/client/Games/Skyrim/Actor.cpp:1085-1099`,hittee 是远程玩家时直接 `return wouldKill;`),
而 NPC 的所有权归 spawn 方,贸然加 `IsCurrentOwner` 会挡掉合法的 NPC 受击广播 ——
那属于玩法决策,不在「稳定性/兼容/同步」的审计范围内。**值域校验是稳定性的边界,所有权
校验是玩法的边界**,两者不能混着改。

```cpp
// Code/server/Services/ActorValueService.cpp(修复后,:89-90)
auto currentHealth = actorValuesComponent.CurrentActorValues.ActorValuesList[24];
actorValuesComponent.CurrentActorValues.ActorValuesList[24] = currentHealth + message.DeltaHealth;
```

减号改成加号,与客户端 `Code/client/Services/Generic/ActorValueService.cpp:295` 对齐。

### 59.6 复核命令(照抄)

```powershell
git grep -n 'kMaxHealthDelta' -- Code
git grep -n 'DeltaHealth' -- Code/encoding/Messages
git grep -n 'IsCurrentOwner\|ActorValuesList\[24\]' -- Code/server/Services/ActorValueService.cpp
git grep -n 'std::isfinite' -- Code/encoding
python Tools\Scripts\check_docs.py
```

复核结果:修复前两个 `.cpp` 里 `DeltaHealth` 只有 `WriteFloat`/`ReadFloat` 各一处,
读侧无任何判断;修复后 `kMaxHealthDelta` 在两个结构体里各声明一次、两个 `.cpp` 各使用一次、
测试引用两处;`IsCurrentOwner` 在 `ActorValueService.cpp` 里仍是 `:31`/`:58`/`:110` 三处
(本处**刻意不加**);`Code/encoding` 里 `std::isfinite` 由 1 处(`TimeModel.cpp:15`)增至 3 处;
`python Tools/Scripts/check_docs.py` → `DOC CHECK OK`。

**验证**:`Code/tests/encoding.cpp:609 TEST_CASE("Health change broadcast wire bounds", "[encoding.health_change]")`
用 `Serialization::WriteFloat/WriteVarInt` 手工造畸形包,四段:
NaN → `0.f`(且 `Id` 仍为 `42`);`-1.0e30f` → `-kMaxHealthDelta`;`NotifyHealthChangeBroadcast`
收 `1.0e30f` → `kMaxHealthDelta`;以及 `Id = 7` / `-12.5f` 走 `Serialize`→`Deserialize`
(先 `reader.ReadBits(trash, 8)` 弹掉 opcode)回环相等。CI 的
`.github/workflows/windows.yml:594-638` 会跑 `TPTests.exe` 并要求输出含 `All tests passed`。

> **教训**:一个字段的「合法范围」不是它的类型决定的,是**它下游的判决**决定的 ——
> `float` 能表示 `-1e38`,但「加到血量上再判死」这个下游只接受「不可能加出非正数的量」。
> 所以审这类字段要顺着它往下读**两跳**:第一跳看它被怎么算(`+ acMessage.DeltaHealth`),
> 第二跳看算出来的东西被拿去判什么(`health <= 0.f` → `Kill()`)。只读第一跳会得出
> 「它只是个血量增量」的结论。另一条:**同一个值在两个方向上的畸形后果不同** —— NaN 走
> 「污染引擎」这条路,`-inf` 走「判决成立」这条路,所以 `std::isfinite` 与幅值钳位
> **两样都要有**,缺一样就漏一半。最后一条:同文件的兄弟 handler 是最好的对照 ——
> 四个里三个都写了所有权门,唯独第四个没写,这种「不对称」比任何外部标准都更能说明
> 该不该加;但**该加什么要分清**:值域校验属于稳定性,所有权校验属于玩法,前者必须加、
> 后者要问清楚再动。

## 60. 2026-09-29 场次:一个常量管「能收几条」,另一个管「下标能到哪」 —— 它们此前被当成了同一个东西

> **锚点**:本节修复落在 `Code/encoding/Structs/ActorValues.h`/`.cpp` 与四个消息
> (`RequestActorValueChanges`/`NotifyActorValueChanges`/`RequestActorMaxValueChanges`/
> `NotifyActorMaxValueChanges`)的 `.cpp`,以及 `Code/tests/encoding.cpp`。
> 这条线上流的是「引擎 actor value 表的下标 + 值」,下标被原样交给引擎,所以它同时是
> **崩溃面**(越界下标进 `SetValue`/`ForceCurrent`)与**失步面**(被污染的账本会随角色数据
> 复制给每一个后进服的玩家)。

### 60.1 缺陷:条目数夹住了,键与值原样过

**现象**:一个客户端发一条 `RequestActorValueChanges`,把 `Values` 的键写成 `0xFFFFFFFF`,
范围内的客户端会拿这个键去 `SetActorValue`/`ForceActorValue`,也就是直接写进引擎表的
下标;值是 `NaN`/`inf` 时,污染的是那个 actor value 之后**每一次**读取 —— 血量、速度、
抗性都由同一张表供给。同一个 `ActorValues` 结构体还随角色数据走,所以一份被污染的账本
会被复制给每一个新加入的玩家。

**根因**(修复前):`Code/encoding/Structs/ActorValues.cpp:46-69` 与四个消息的 `DeserializeRaw`
是同一形状 —— 只夹了**条目数**,键与值直接插入:

```cpp
// Code/encoding/Structs/ActorValues.cpp(修复前,:57-59)
const uint32_t key = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
auto value = Serialization::ReadFloat(aReader);
ActorValuesList.insert({key, value});
```

结构体上唯一的上限常量 `Code/encoding/Structs/ActorValues.h:16 kMaxActorValueCount = 164`
是**条目数**上限,与键的取值范围无关 —— 这正是本节的核心:两个概念共用一个常量名,
于是「夹了」的错觉成立。接收端 `Code/client/Services/Generic/ActorValueService.cpp:333-346`
只按键**挑分支**,没有任何范围校验:

```cpp
// Code/client/Services/Generic/ActorValueService.cpp:341-346
if (key == ActorValueInfo::kStamina || key == ActorValueInfo::kMagicka || key == ActorValueInfo::kHealth)
{
    pActor->ForceActorValue(ActorValueOwner::ForceMode::DAMAGE, key, value);
    continue;
}
pActor->SetActorValue(key, value);
```

`Code/client/Games/Skyrim/Actor.cpp:732-735 SetActorValue` 转发给
`actorValueOwner.SetValue(aId, aValue)`,而合法下标只有
`Code/client/Games/Skyrim/Forms/ActorValueInfo.h:174 kActorValueCount = 164` 个。

### 60.2 判据:「夹了计数」与「夹了下标」是两件事,`& 0xFFFFFFFF` 不是校验

判据不是「这个字段夹过没有」,而是**「它接下来会被当成什么用」**:`count` 被拿去当循环
上界,`key` 被拿去当**表下标**。同一段代码里前者夹了、后者没夹,读起来却像都处理过了 ——
因为 `const auto count = std::min<uint64_t>(...kMaxActorValueCount)` 就在上面两行。

第二条判据:**掩码不是校验**。`Serialization::ReadVarInt(...) & 0xFFFFFFFF` 在
`uint32_t` 上是恒等操作(§58 的教训里已经记过一次),它看起来像「把值收进 32 位」,
实际什么都没挡。四个消息的 `Id`/`OwnershipEpoch` 也是同一写法。

**同一个仓库里已经有做对的写法**:`Code/encoding/Structs/TimeModel.cpp:15` 的
`if (!std::isfinite(aValue))`(§58)、`Code/encoding/Messages/PluginMessagingRequest.cpp:39-42`
的「Never trust the wire length」+ `std::min<T>(...)`(§59 已引)。本节只是把这两条
用在一个**下标**上。

### 60.3 可达性:能触发的是「那个 actor 的当前所有者」,不是任意客户端

`RequestActorValueChanges`/`RequestActorMaxValueChanges` 在服务端 handler 表里
(`Code/server/Services/ActorValueService.cpp:17-18`),两者都有 `IsCurrentOwner` 门
(`:31`/`:58`)—— 也就是说能触发它的是那个 actor 的当前所有者。但客户端自己的发送侧
(`Code/client/Services/Generic/ActorValueService.cpp:131`)只发 `0..163` 的键,
所以越界键只可能来自被改过的客户端或半途失步的流。

### 60.4 修法:上界声明在结构体上,判据是一个静态函数,夹在读侧

```cpp
// Code/encoding/Structs/ActorValues.h(修复后,:18-22)
// The keys are indices into that same table and the engine is handed them as is,
// so a peer that names an index past the end reaches code which never checks it.
static constexpr uint32_t kMaxActorValueId = static_cast<uint32_t>(kMaxActorValueCount - 1);

static bool IsValidActorValue(uint32_t aId, float aValue) noexcept;
```

```cpp
// Code/encoding/Structs/ActorValues.cpp(修复后,:18-21)
bool ActorValues::IsValidActorValue(uint32_t aId, float aValue) noexcept
{
    return aId <= kMaxActorValueId && std::isfinite(aValue);
}
```

四个消息的 `.cpp` 与 `ActorValues::Deserialize` 的两段循环共用这一个判据:

```cpp
// Code/encoding/Messages/RequestActorValueChanges.cpp(修复后,:39-47)
const uint32_t key = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
const auto value = Serialization::ReadFloat(aReader);

// Both fields are read above so that an entry which is dropped here still
// consumes its bytes and the rest of the stream stays aligned.
if (!ActorValues::IsValidActorValue(key, value))
    continue;

Values.insert({key, value});
```

**一处刻意的取舍:被丢弃的条目仍然要消费掉自己的字节。** 键与值都先读出来再判,
`continue` 而不是 `break` —— 提前退出会把「一个坏键」换成「整包从错误偏移读起」,
后者更糟。这也是 `Inventory.cpp:186`/`:192` 的既有形状(跳过无效条目但不中断循环)。
`Serialize` 侧未改,与 `TimeModel`/血量增量的修复同一策略:夹在读侧。

**另一处取舍:不新增所有权门。** 与 §59 同:`IsCurrentOwner` 属于玩法边界,
值域/下标校验属于稳定性边界。

### 60.5 复核命令(照抄)

```powershell
git grep -n 'kMaxActorValueId\|IsValidActorValue' -- Code
git grep -n 'IsValidActorValue' -- Code/encoding/Messages
git grep -n 'ActorValuesList.insert\|Values.insert' -- Code/encoding
git grep -n 'kActorValueCount' -- Code
python Tools\Scripts\check_docs.py
```

复核结果:`IsValidActorValue` 声明 1 处、定义 1 处、调用 6 处(四个消息各 1、
`ActorValues::Deserialize` 两段各 1);`Code/encoding` 里 `std::isfinite` 由 3 处增至 4 处
(`ActorValues.cpp` 新增一处);`& 0xFFFFFFFF` 的键读取点仍是 6 处(掩码未动,只在其后加了判断);
`python Tools/Scripts/check_docs.py` → `DOC CHECK OK`。

**验证**:`Code/tests/encoding.cpp:670 TEST_CASE("Actor value wire bounds", "[encoding.actor_values]")`
手工造包覆盖越界键 `0xFFFFFFFF`、`0x12345678`、`NaN` 值与 `inf` 值,并在包尾放一个
`0xDEADBEEF` 哨兵 —— 读完之后它必须原样读得到,以此证明被丢弃的条目确实消费了自己的字节;
另有一段回环一次正常的两键集合。CI 的 `.github/workflows/windows.yml:594-638` 会跑
`TPTests.exe` 并要求输出含 `All tests passed`。

> **教训**:当同一段代码里已经有一个 `std::min(..., kMaxXxxCount)` 时,最容易犯的错是
> 把「夹过了」当成整条路都夹过了 —— 那个常量管的是**能收几条**,和**下标能到哪**是两件事。
> 判据要落到「这个数接下来被当成什么用」:当循环上界、当表下标、当长度、当判决依据,
> 四种用法对应四个不同的合法域。第二条:`& 0xFFFFFFFF` 这类掩码在 `uint32_t` 上是恒等
> 操作,它读起来像校验、实际不是 —— 判断一个字段有没有被校验,要看有没有**分支**,
> 不是看有没有**运算**。第三条:上界常量要挂在**结构体**上而不是某一个 `.cpp` 里,
> 因为同一个结构体会被四个消息、服务端账本与两个方向的 `Deserialize` 共用;挂在结构体上,
> 后来的人改一处就够,挂在 `.cpp` 里则必然漏掉其余五处。

## 61. 2026-09-29 场次:一个宏既叫「断言」又不中止 —— 判据是「它失败之后控制流去了哪」

> **锚点**:`Code/client/Utils.h:11-15` 的 `TP_ASSERT` 与 `Code/client/Utils.h:28-34` 的
> `Utils::Assert`。它被用在 9 处,其中 4 处后面没有任何真分支。这是崩溃面,不是失步面:
> 一个只写日志的宏,在 release 构建里等于不存在。

### 61.1 缺陷:宏的名字叫断言,行为是日志

**现象**:`Code/client/Games/Skyrim/Projectiles/Projectile.cpp:101` 与 `:106`、
`Code/client/Games/Skyrim/TESObjectREFR.cpp:521` 与 `:617` 各自写着 `TP_ASSERT(指针, "...")`,
紧接着就解引用同一个指针。读代码的人(包括写下它们的作者)会认为「判过了」。

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
它不 `abort`、不 `throw`、不返回失败;`__debugbreak()` 也只在挂了调试器时执行。
文件上方作者自己写着 `// TODO: should this be debug only? I removed the check since
debug is broken, can only use releasedbg` —— 也就是说这条「断言」在唯一的构建里
连调试器路径都没有。`Utils::Assert` 的直接调用者全仓只有这个宏(`git grep "Utils::Assert"`
1 命中),所以它没有任何其他语义可以依赖。

### 61.2 判据:「它失败之后控制流去了哪」

审查一个判空/断言时,唯一有用的问题是**失败之后控制流去了哪**。`TP_ASSERT` 的答案
是「原地继续」。同族的三种形状在这里可以直接排序:

- `if (!p) return;` / `continue;` —— 真门。
- `if (!p) { log; return; }` —— 真门,且有证据。
- `if (!p) log;`(或宏展开成这个) —— **不是门**,后面每一行都在解引用。

按这条判据重扫 `Code/client`,9 个 `TP_ASSERT` 里 5 个后面本来就有真 `if`
(`Projectile.cpp:20`→`:21`、`:30`→`:31`、`TESObjectREFR.cpp:529`→`:530`、`:559`→`:560`,
`:626` 是值断言不是指针),**4 个没有**:`Projectile.cpp:101`、`Projectile.cpp:106`、
`TESObjectREFR.cpp:521`、`TESObjectREFR.cpp:617`。同一次扫描还带出 `TESObjectREFR.cpp:538` 的
`pEffectItem->pEffectSetting->formID` —— 同一表达式里连着两次解引用,而前一行的 `TP_ASSERT`
只判了 `pEffectItem`。

### 61.3 可达性:四条指针来源全部允许为空

**判据**:引擎给出的指针,要去它自己的定义里看返回语句有没有可能给出 `nullptr`。

- `Code/client/Games/Skyrim/Projectiles/Projectile.cpp:99` 的 `ThisCall(RealLaunch, ...)`
  把结果写进出参,返回值未判。
- `Code/client/Games/Skyrim/TESObjectREFR.cpp:126-140 TESObjectREFR::GetByHandle`
  显式写成 `TESObjectREFR* pResult = nullptr;` → 引擎调用 → `if (pResult) ...` → `return pResult;`。
  **它自己就用 `if (pResult)` 承认了可能为空。**
- `Code/client/Games/Skyrim/RTTI.h:17-27` 的 `Cast<>` 在 RTTI 未映射时**恒**返回 `nullptr`,注释逐字:
  `// guard against unmapped rtti ids (legacy game versions): the game's dynamic cast dereferences`。
  这一条对 1.5.x / 1.6.x / 1.7.x 的兼容性尤其要紧 —— 未映射就是旧版游戏。
- `Code/client/Games/Skyrim/Forms/EnchantmentItem.cpp:39` 的 `EnchantmentItem* pItem = nullptr;`,
  创建被引擎拒绝时原样返回。

附魔那两条路径**线上可达**:`Inventory::Entry.ExtraEnchantId` 随 `NotifyInventoryChanges` /
`NotifyObjectInventoryChanges` 过线(`Code/encoding/Structs/Inventory.cpp:64`),
`GetExtraDataFromItem` 里 `arEntry.ExtraEnchantId.ModId == 0xFFFFFFFF` 走 `EnchantmentItem::Create`,
否则走 `Cast<EnchantmentItem>(TESForm::GetById(modSystem.GetGameId(...)))` —— 两条分支都能给出空指针,
随后直接进引擎的 `Code/client/Games/Skyrim/ExtraData/ExtraDataList.cpp:130-135 SetEnchantmentData`
(纯 `ThisCall`,没有判空)。

### 61.4 修法:把日志换成分支,并且想清楚「不发」和「发错」哪个更糟

```cpp
// Code/client/Games/Skyrim/Projectiles/Projectile.cpp(修复后,:99-124)
    auto result = TiltedPhoques::ThisCall(RealLaunch, apThis, arData);

    if (!result)
    {
        spdlog::error("No projectile handle returned.");
        return nullptr;
    }

    TESObjectREFR* pObject = TESObjectREFR::GetByHandle(result->handle.iBits);
    Projectile* pProjectile = Cast<Projectile>(pObject);

    if (pProjectile)
        Event.Power = pProjectile->fPower;
    else
        spdlog::error("No projectile found, announcing the launch data power instead.");

    World::Get().GetRunner().Trigger(Event);
```

**一处刻意的取舍**:拿到空的 `pProjectile` 时**不跳过事件**。`Event.Power` 在
`Projectile.cpp:90` 已经从 `arData.fPower` 取过一次初值,此时留在那个值上仍然是一个合理的
抛射物,而**少发一个抛射物比发一个数值不准的更糟** —— 前者让同场玩家的世界里凭空少一发,后者
只是一发的伤害/速度偏一点。所以 `World::Get().GetRunner().Trigger(Event);` 留在原位。

另外三处:

- `GetItemFromExtraData`(`Code/client/Games/Skyrim/TESObjectREFR.cpp:521-562`)把 `pEnchantment`
  的整段(`GetServerModId`、`formID & 0xFF000000`、`listOfEffects` 遍历)移进 `if (pEnchantment)`,
  并把 `if (!pEffectItem) continue;` 收紧成 `if (!pEffectItem || !pEffectItem->pEffectSetting) continue;`。
- `arEntry.ExtraEnchantCharge` / `ExtraEnchantRemoveUnequip` **仍在 `if` 之外**赋值 —— 它们来自
  `pExtraEnchantment` 本身,与 `pEnchantment` 是否为空无关。收紧它们是把门修错了地方。
- `GetExtraDataFromItem`(`Code/client/Games/Skyrim/TESObjectREFR.cpp:634-641`)把 `SetEnchantmentData`
  包进 `if (pEnchantment)`。

### 61.5 复核命令(照抄)

```powershell
# 1. 宏的定义与它调用的函数
node -e "const s=require('fs').readFileSync('Code/client/Utils.h','utf8');console.log(s.split(/\r?\n/).slice(8,36).join('\n'))"

# 2. 全部 TP_ASSERT 站点,以及每个站点后面三行
node -e "const fs=require('fs');for(const f of ['Code/client/Games/Skyrim/Projectiles/Projectile.cpp','Code/client/Games/Skyrim/TESObjectREFR.cpp']){const L=fs.readFileSync(f,'utf8').split(/\r?\n/);L.forEach((t,i)=>{if(/^\s*TP_ASSERT\(/.test(t))console.log(f+':'+(i+1)+' -> '+[L[i+1],L[i+2]].map(s=>(s||'').trim()).join(' || '))})}"

# 3. 指针来源是否允许为空
git grep -n 'pResult = nullptr' -- Code/client/Games/Skyrim/TESObjectREFR.cpp
git grep -n 'return nullptr' -- Code/client/Games/Skyrim/RTTI.h

# 4. 大括号平衡(三个文件都应为 0)
node -e "const fs=require('fs');for(const f of ['Code/client/Games/Skyrim/Projectiles/Projectile.cpp','Code/client/Games/Skyrim/TESObjectREFR.cpp']){const s=fs.readFileSync(f,'utf8');console.log(f,(s.match(/\{/g)||[]).length-(s.match(/\}/g)||[]).length)}"

# 5. 文档门禁
python Tools/Scripts/check_docs.py
```

复核结果:宏的失败路径只有 `spdlog::critical` 与 `IsDebuggerPresent()` 分支,没有 `abort`/`return`;
重扫后 9 个 `TP_ASSERT` 站点**每一个后面都紧跟真 `if`**(`Projectile.cpp:20`/`:30`/`:106`/`:116`、
`TESObjectREFR.cpp:536`/`:571`,以及 `:521`/`:629` 的注释与其后的 `:528 if (pEnchantment)` /
`:634 if (pEnchantment)`);`TESObjectREFR.cpp` 的 `TP_ASSERT` 文本计数由 5 增至 6(新增注释里引用了宏名);
三个文件大括号平衡均为 0。

**验证**:这四处是引擎指针判空,`Code/tests` 只覆盖 `Code/encoding`,且 `Projectile::Launch` /
`GetItemFromExtraData` / `GetExtraDataFromItem` 都依赖引擎与 `World::Get()` —— **没有可写的单元测试**,
以静态复核为准。`python Tools/Scripts/check_docs.py` 全绿。

> **教训**:第一条:审查判空时不要看有没有 `if`,要看**失败之后控制流去了哪** —— 一个展开成
> `if (!p) log;` 的宏,在读代码时和真门长得一模一样,只有展开之后才看得出后面每一行都在解引用。
> 第二条:宏的名字不是它的语义。叫 `ASSERT` 的东西在本仓里既不 `abort` 也不返回,`IS_MASTER`
> 又把 `#if (!IS_MASTER)` 的代码整段剥掉,所以「release 里它还在不在」要按**展开结果**判,不能按
> 名字判。第三条:把日志改成分支时,顺带问一句「这个分支是继续更糟还是停止更糟」——
> `Projectile.cpp` 的答案是「事件照发、数值退回初值」,而不是「整条路 `return`」。
## 62. 2026-09-29 场次:线上字符串的长度由发送方决定 —— 判据是「这个值读完会被谁用掉」

> **锚点**:`Code/encoding` 里 30 处 `Serialization::ReadString` 中未被夹取的 26 处,以及
> `Code/server/Services/OverlayService.cpp:34` 与 `Code/server/Scripting/GameServer_Bindings.cpp:20`/`:29`
> 三个 `std::regex` 构造点。新增 `Code/encoding/WireLimits.h` 与 `Code/encoding/ChatSanitize.h`。
> 这一节同时是**内存面**(长度由对端决定)与**可用性面**(`noexcept` 里构造可抛类型)。

### 62.1 缺陷:字符串的「长度」字段没有上界,而它的下游有

**现象**:线上的字符串编码是「长度 + 这么多字节」,长度是发送方写什么就是什么。接收方
读完才知道有这么大一块,而且不知道整包到底多大。于是 `Username`、`ChatMessage`、`Text`、
`SoundFilename`、`AppearanceBuffer`、`ModList[i].Filename` 以及缓存字符串的长度全部由对端
决定,而它们还会继续往下走:进 CEF 渲染、进 `TESNPC::Deserialize`、进
`SubtitleManager::ShowSubtitle`、进服务端整个会话都持有的 per-player map。

**根因**(修复前):30 处 `ReadString` 里只有 4 处带夹取,全部集中在插件消息通道
(`NotifyPluginMessaging.cpp:24`/`:30`、`PluginMessagingRequest.cpp:29`/`:31`),其余 26 处是裸读:

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

最后一段是同一节里最贵的组合:**条目数**是 16 位(`65535`),而**每个条目里的字符串**没有
上界,所以一次 `Mods::Deserialize` 就能产出 65535 个任意长的文件名,而 `Mods` 是
`AuthenticationRequest` 的一个字段。

### 62.2 判据:「这个值读完会被谁用掉」

审查线上字段时,唯一有用的问题不是「它夹过没有」,而是**「它读完会被谁用掉」**。同一个
结构里两个字段的答案可以完全不同:`Mods` 的 `modCount` 被拿去当循环上界(所以它是「条目数」
问题),`Filename` 被拿去当文件名并驻留(所以它是「长度」问题)。`& 0xFFFF` 看起来像校验,
实际只是把 64 位裁成 16 位 —— 它规定了**能有多少个**,没有规定**每个多大**。

下游的性质决定上限该取多大,这也是本节里 `kMaxAppearanceBytes` 不是拍出来的原因:
`Code/client/Games/Forms.cpp:20-36` 的 `TESNPC::Serialize` 写进一个 `char buffer[1 << 15]`
的栈缓冲,所以合法发送方**根本产不出**更长的东西,`1 << 15` 是这条线上真实存在的上界。

### 62.3 缺陷:`noexcept` 函数里构造可抛的 STL 类型

**现象**:服务端剥聊天文本里的 HTML 标签用的是
`std::regex escapeHtml{"<[^>]+>\\s+(?=<)|<[^>]+>"}` 加 `std::regex_replace`。构造点在
`Code/server/Services/OverlayService.cpp:29-37` 的 `sendPlayerMessage` 里 —— 那个函数是
`noexcept` 的,而且它每个聊天包都要重编译一次正则,位置就在把消息转发给所有人的路上。

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
`CODE_GUIDELINES.md:47` 要求「Don't use exceptions, don't use STL code that can throw」,而
`std::regex` 的构造与 `std::regex_replace` 都要分配。在 `noexcept` 的函数里分配失败不是
「这条聊天丢了」,是 `std::terminate` —— 一条聊天消息换来整个进程结束。

### 62.4 修法:夹取而不是丢包,并把边界写进注释

新增 `Code/encoding/WireLimits.h`:每个字段的上限是具名常量,配合
`inline void Clamp(TiltedPhoques::String& aValue, const uint32_t aMaxBytes) noexcept`;
22 个文件在读完之后调用它。上限取的是**宽松到没有任何合法发送方会碰到**的值:
`kMaxDisplayNameBytes = 64`(与 `NotifyPluginMessaging` 已用的值一致)、
`kMaxChatMessageBytes = 4096 + 512`(2026-10-10 更正:这个字段同时是插件层的传输通道,
见 §79 —— 原先的 `512` 会把 STRPM 信封截断,而截断后的 hex 仍然是合法 hex,接收方于是解出一个更短的载荷),
`kMaxDialogueTextBytes = 1024`、`kMaxSoundPathBytes = 260`、
`kMaxFileNameBytes = 260`、`kMaxVersionBytes` / `kMaxTokenBytes = 64`、
`kMaxAppearanceBytes = 1 << 15`、`kMaxCachedStringBytes = 512`。

**为什么夹取而不是丢包**:为一个字段丢掉整包,会把「显示得不对」升级成「掉线」,而
`String::resize` 向下不会抛。同理,`ChatSanitize::StripHtmlTags` 保持原正则的**每一条语义**
(包括 `"<>"` 与未闭合 `<` 都算文本这种边角),因为改行为比改实现危险得多。

**修法的边界也要写下来**:夹取发生在字符串读完之后,它约束的是「值怎么用」,不是 reader
内部的瞬时分配 —— 真正的整包上限只能加在接收入口(`GameServer::OnConsume` /
`Client::OnConsume`),只有它们知道包的真实大小。这句话写在 `Code/encoding/WireLimits.h`
的头注释里,免得下一个人以为这条线上已经没有任何无界分配了。

(**2026-10-13 更正**:上面这句判据写对了,但实现**一直到 §84 才存在** —— 两个接收入口在那之前
一次都没有判过 `aSize`,而 `WireLimits.h` 的头注释又逐字写着「这句话写在头注释里」,
于是缺口看起来像已经补上了。见 §84。)

新代码放 `Code/encoding` 而不是放在两个服务端调用者旁边,是因为 `Code/tests/xmake.lua`
只 `add_deps("SkyrimEncoding")` —— 那是测试目标唯一能看到的库。

### 62.5 复核命令(照抄)

```powershell
# 1. 全部 ReadString 站点与紧跟其后的那一行
node -e "const fs=require('fs');const walk=d=>fs.readdirSync(d,{withFileTypes:true}).flatMap(e=>e.isDirectory()?walk(d+'/'+e.name):(e.name.endsWith('.cpp')?[d+'/'+e.name]:[]));for(const f of walk('Code/encoding')){const L=fs.readFileSync(f,'utf8').split(/\r?\n/);L.forEach((t,i)=>{if(t.includes('ReadString('))console.log(f+':'+(i+1)+' | '+t.trim()+' >> '+((L[i+1]||'').trim()))})}"

# 2. 未夹取的站点数(应为 0)
node -e "const fs=require('fs');const walk=d=>fs.readdirSync(d,{withFileTypes:true}).flatMap(e=>e.isDirectory()?walk(d+'/'+e.name):(e.name.endsWith('.cpp')?[d+'/'+e.name]:[]));let bad=0;for(const f of walk('Code/encoding')){const L=fs.readFileSync(f,'utf8').split(/\r?\n/);L.forEach((t,i)=>{if(t.includes('ReadString(')&&!((L[i+1]||'').includes('WireLimits::Clamp')||t.includes('WireLimits::Clamp')||t.includes('ReadString(aReader);')===false)){if(!(L[i+1]||'').includes('WireLimits::Clamp')){bad++;console.log('UNCLAMPED '+f+':'+(i+1)+' '+t.trim())}}})}console.log('unclamped='+bad)"

# 3. 服务端不再有 std::regex
git grep -n 'regex' -- Code/server

# 4. 两个新头文件存在且是 CRLF
node -e "const fs=require('fs');for(const f of ['Code/encoding/WireLimits.h','Code/encoding/ChatSanitize.h']){const s=fs.readFileSync(f,'utf8');console.log(f,s.length,'cr='+(s.match(/\r/g)||[]).length)}"

# 5. 大括号平衡(两个新头 + 三个改动文件都应为 0)
node -e "const fs=require('fs');for(const f of ['Code/encoding/WireLimits.h','Code/encoding/ChatSanitize.h','Code/server/Services/OverlayService.cpp','Code/server/Scripting/GameServer_Bindings.cpp','Code/tests/encoding.cpp']){const s=fs.readFileSync(f,'utf8');console.log(f,(s.match(/\{/g)||[]).length-(s.match(/\}/g)||[]).length)}"

# 6. 文档门禁
python Tools/Scripts/check_docs.py
```

复核结果:`Code/encoding` 里 30 处 `ReadString` 全部有夹取或本身就是已夹取的插件通道;
`git grep -n 'regex' -- Code/server` 由 7 命中(3 个构造点 + 3 个 `#include <regex>` + 1 条文档注释)
降到 **0**;`Code/server/Services/OverlayService.cpp` 122 → 119 行,
`Code/server/Scripting/GameServer_Bindings.cpp` 46 → 44 行;
`Code/tests/encoding.cpp` 756 → 881 行,11 个 `TEST_CASE`、87 条 `REQUIRE` 全部以 `;` 结尾,
大括号平衡 0。

**验证**:`ChatSanitize::StripHtmlTags` 与旧正则做了穷举对拍 —— 把它逐字移植成 JS,字母表
`['<','>','a',' ','\t','\n']`,穷举长度 0–6 的**全部 55998 个字符串**外加 17 个手写探针,
**0 处不一致**(`"<b>hello</b>"→"hello"`、`"<b> <i>x</i>"→"x"`、`"<b> keep <i>x</i>"→" keep x"`、
`"a <> b"→"a <> b"`、`"<>"→"<>"`、`"x <b> y"→"x  y"`、`"<b> "→" "`);这 11 例连同幂等性一起
写进 `Code/tests/encoding.cpp:850 TEST_CASE("Chat sanitize", "[encoding.chat_sanitize]")`。
`WireLimits::Clamp` 的四段行为(短串不动、恰好等于上限不动、超长被截、正常包往返不变)
写进 `Code/tests/encoding.cpp:761 TEST_CASE("Wire string limits", "[encoding.wire_limits]")`。
CI 的 `.github/workflows/windows.yml:594-638` 会跑 `TPTests.exe` 并要求 `All tests passed`。
`python Tools/Scripts/check_docs.py` 全绿。

> **教训**:第一条:读线上字段时问「这个值读完会被谁用掉」,不要问「它夹过没有」——
> `& 0xFFFF` 规定了能有多少个条目,完全没有规定每个条目多大,同一个结构里这两个问题
> 的答案可以相反。第二条:`noexcept` 是一条承诺,不是装饰 —— 在 `noexcept` 函数里构造
> `std::regex`/`std::string`/任何会分配的类型,等于把「丢一条消息」写成「整个进程结束」,
> 而这条路径恰好是每个聊天包都走的。第三条:替换一段有边角的逻辑(正则的 `[^>]+`、
> `\s+(?=<)` 的空白吞噬)时,先把它变成可穷举的形式对拍,再动实现 —— 55998 个字符串的
> 穷举比对比任何一段「看起来一样」的代码评审都便宜。
## 63. 2026-09-29 场次:UI 在构造函数里解析一个寿命比自己长的值 —— 判据是「解析失败之后,还有没有界面」

> **锚点**:`Code/skyrim_ui/src/app/components/connect-password/connect-password.component.ts:142`、
> `Code/skyrim_ui/src/app/components/server-list/server-list.component.ts:189` 两处裸 `JSON.parse`,
> 修复落在新增的 `Code/skyrim_ui/src/app/services/store.service.ts:34-48 getJsonArray<T>()`。
> 这是**可用性面**:崩溃是「游戏没了」,而这里是「面板没了,而且只有玩家自己知道」。

### 63.1 缺陷:解析发生在构造期,抛出等于不渲染

**现象**:连服务器密码面板与服务器列表面板整块空白 —— 列表不渲染、收藏不显示,控制台
里是一条 `Unexpected token` / `Unexpected non-whitespace character after JSON`,而**清掉
localStorage 就恢复正常**。两个面板读的是不同的键,却会一起坏,因为它们踩的是同一个形状。

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
`localStorage` 的寿命比写它的那个构建长,而这两个键是**追加式**写的(`savedServerList`
在每次连接成功时被重写)。所以「两个数组被拼在一起」或者「写到一半被中断」都会留下一个
**仍然存在、但已经不是合法 JSON** 的值。关键是它抛在**组件构造期**:构造函数没有完成,
Angular 就不会有这个组件,面板不是显示空列表,是根本不渲染。

### 63.2 判据:解析失败之后,还有没有界面

同一个 `JSON.parse` 放在不同位置,后果完全不同。放在一个事件处理器里,抛出只是那一次操作
失败;放在构造函数里,抛出是**整个组件不存在**。所以审查持久化读取时该问的不是「这个值
会不会坏」——它一定会坏——而是**「解析失败之后还剩什么」**。

这条判据也解释了为什么修复不是就地包一层 `try/catch`:两处解析的意图完全相同(「这个键
要么是一个数组,要么当它不存在」),写进 `StoreService` 才只有一处需要维护,下一个读数组
的人也不必再想一遍。`Array.isArray(parsed)` 是同一个判据的另一半 —— 能解析成对象、字符串
或数字的值同样会让 `.find` / `for...of` 出问题,「是合法 JSON」不等于「是我们要的形状」。

### 63.3 可达性

两个键都由 `store.service.ts` 的 `set` 写入,读取点只有这两个组件:
`connect-password.component.ts:149`(取密码)与 `:170`(存密码)、
`server-list.component.ts:192`(载入收藏)。触发条件不需要玩家做任何异常操作 —— 换一个构建,
或者在写入过程中关掉游戏就够。

### 63.4 修法:把「读一个数组」变成一个具名操作

`store.service.ts:34-48` 新增
`public getJsonArray<T>(key: string, valueIfNull: T[]): T[]`:先 `this.get(key, null)` 判
`null`,再 `try { const parsed = JSON.parse(value); return Array.isArray(parsed) ? (parsed as T[]) : valueIfNull; } catch { return valueIfNull; }`。
两个组件改为调用它;`SavedServer` 接口就近声明在 `connect-password.component.ts:20-24`,
收藏列表复用已有的 `Code/skyrim_ui/src/app/models/server.ts` 的 `Server`。

### 63.5 复核命令(照抄)

```powershell
# 1. 全仓还剩几处 JSON.parse(应为 2,且都在 try/catch 内)
git grep -n 'JSON.parse' -- Code

# 2. 两个组件里已经不再有裸 JSON.parse
git grep -n 'JSON.parse' -- Code/skyrim_ui/src/app/components; Write-Host "exit=$LASTEXITCODE"

# 3. getJsonArray 的调用点
git grep -n 'getJsonArray' -- Code/skyrim_ui

# 4. README 版本行仍然以 ' |' 结尾且是 CRLF
node -e "const fs=require('fs');for(const [f,n] of [['README.md',110],['README_EN.md',104]]){const l=fs.readFileSync(f,'utf8').split('\n')[n-1];console.log(f,l.length,JSON.stringify(l.slice(-3)))}"

# 5. 文档门禁
python Tools/Scripts/check_docs.py
```

复核结果:`git grep -n 'JSON.parse' -- Code` 只剩 `client.service.ts:579`(已在
`try { … } catch { error = { error: 'no_reason' }; }` 内,`:577-584`)与
`store.service.ts:42`(新增的 `getJsonArray` 内);两个组件的裸解析各减少 2 处与 1 处;
`git grep -n 'JSON.parse' -- Code/skyrim_ui/src/app/components` 退出码 1(0 命中)。

**验证**:`getJsonArray<SavedServer>('savedServerList', [])`、
`getJsonArray<SavedServer>(…)`、`getJsonArray<Server>('favoriteServerList', [])` 三处调用点;
`Server` 来自 `Code/skyrim_ui/src/app/models/server.ts`,字段含 `ip`/`port`/`isFavorite`,
与收藏列表的用法一致。`python Tools/Scripts/check_docs.py` 全绿。

> **教训**:第一条:持久化读取的审查对象不是「这个值会不会坏」(它一定会坏),而是
> **「解析失败之后还剩什么」** —— 同一行 `JSON.parse` 在事件处理器里只是那次操作失败,
> 在构造函数里是整个组件不存在。第二条:`localStorage` 是**跨构建**的,写它的代码可能
> 已经不在了,所以「我们自己写的值一定合法」从来不是理由,尤其是追加式写入的键。
> 第三条:当两处代码问的是同一个问题(「这个键要么是数组,要么不存在」),把它变成一个
> 具名操作放进服务里 —— 就地包 `try/catch` 会让第三个人再想一遍,而他想的那一遍未必
> 会记得 `Array.isArray`。
## 64. 2026-09-29 场次:原因送到了,但载荷不是 JSON —— 判据是「这个值是谁写的」

> **锚点**:`Code/client/Services/Generic/TransportService.cpp:675` 与 `:686` 两处手拼 JSON,
> 修复落在新增的 `Code/encoding/JsonEscape.h`,同族第二处是同一函数 `:692-703` 的
> `case AR::kClientModsDisallowed:`。这是**可诊断性面**:连接没成功是次要的,玩家拿不到理由才是主要的。

### 64.1 缺陷:值直接写在引号之间,而值是对端写的

**现象**:认证被拒时玩家看到的是「服务器没有理由地拒绝了连接」。服务器**把原因送过来了**
(`wrong_version` / `mods_mismatch` / `client_mods_disallowed` / `wrong_password` / `server_full`),
它写在 `ConnectionErrorEvent::ErrorDetail` 里,经 `OverlayService::OnConnectionError` 交给 CEF,
UI 用 `JSON.parse` 读出来。载荷本身是**手工拼接**的字符串。

**根因**(修复前):
```cpp
// Code/client/Services/Generic/TransportService.cpp:675(修复前)
ErrorInfo += fmt::format("\"expectedVersion\": \"{}\", \"version\": \"{}\"", acMessage.Version, BUILD_COMMIT);

// Code/client/Services/Generic/TransportService.cpp:686(修复前)
ErrorInfo += fmt::format("[\"{}\",\"{}\"]", m.Filename.c_str(), m.Id);
```
`acMessage.Version` 是**服务端**的版本字符串,`m.Filename` 是**服务端回传的** mod 文件名。
两者都刚过线、都由对端决定,却被原样放进引号之间 —— 一个 `"` 就提前结束字符串,整个文档不再是 JSON,
`client.service.ts:579` 的 `JSON.parse` 抛进 catch,只剩 `error = { error: 'no_reason' };`。

同一段里还有第二处,形状完全不同:`case AR::kClientModsDisallowed:` 里
```cpp
// Code/client/Services/Generic/TransportService.cpp:697-700(修复前)
if (acMessage.MO2Active)
    if (acMessage.SKSEActive)
        ErrorInfo += ",";
ErrorInfo += "\"MO2\"";
```
外层 `if` 只包住了**逗号那一行**,数组元素 `"MO2"` 在它之外**无条件**执行。
于是只装了 SKSE 的玩家被告知「请移除 MO2」—— 一个他根本没装的 mod 管理器。
服务端早就把两个真实 flag 回传了(`Code/server/GameServer.cpp:896-897`),拼数组时用的却是错的。

### 64.2 判据:这个值是谁写的

审查拼接式载荷时该问的不是「这个值会不会包含引号」,而是**「这个值是谁写的」**。
同一个 `fmt::format` 模板里,`BUILD_COMMIT` 是本侧编译期常量(不可能含引号),
`acMessage.Version` 与 `m.Filename` 是对端写的(可以含任何东西)—— 决定要不要转义的不是字段名,
是**写入者**。这条判据把「三十个字段」压缩成一次分类,而分类结果不需要逐字段判断。

长度上限与内容转义是两个正交的问题,这里正好各占一半:`AuthenticationResponse.cpp:19` 已经
把 `Version` 夹到 64 字节、`Mods.cpp:46` 已经把 `Filename` 夹到 260 字节 —— **长度夹住了,内容没有**。
把「已经过了 `WireLimits::Clamp`」当成「已经安全」是这次漏掉它的原因。

### 64.3 可达性

不需要玩家做任何异常操作,也不需要恶意客户端。`BUILD_COMMIT` 取自
`Code/common/BuildInfo.h.in:4`,服务端与客户端的构建号本来就不同 —— 而版本不一致**正是**
`wrong_version` 这条分支存在的理由,所以走到那三行拼接是这条分支的**正常路径**。
mod 文件名由对端在 `AuthenticationRequest` 里上报,允许包含文件系统允许的任何字符。

### 64.4 修法:转义而不是拒包

新增 `Code/encoding/JsonEscape.h`:`Escape(const TiltedPhoques::String&)` 按 RFC 8259 section 7
转义引号、反斜杠与全部 < 0x20 的字节,其余原样保留。`TransportService.cpp` 的三处拼接改走它,
`kClientModsDisallowed` 分支改成 `first` 标志驱动的数组(与同函数 `kModsMismatch` 分支的写法一致)。

**为什么是转义而不是拒包**:这些字符串已经过了长度上限,文件名允许包含文件系统允许的任何字符,
而一个需要转义的名字仍然是**必须展示给玩家**的名字 —— 为它丢掉整个包,会把「显示得不对」
升级成「掉线」。这与 `WireLimits.h` 里「夹取而不是丢包」是同一条理由。

放在 `Code/encoding` 而不是 `TransportService.cpp` 旁边,是因为那是测试目标链接的静态库
(`Code/tests/xmake.lua` 只 `add_deps("SkyrimEncoding")`),而 `Code/encoding/xmake.lua` 的
`add_headerfiles("**.h")` 会自动收进新头文件。

### 64.5 复核命令(照抄)

```powershell
# 1. 全仓还剩几处手拼 JSON 的认证错误载荷(应为 0)
git grep -n 'expectedVersion' -- Code/client

# 2. 三处拼接都走 JsonEscape(应为 3)
git grep -c 'JsonEscape::Escape' -- Code/client/Services/Generic/TransportService.cpp

# 3. kClientModsDisallowed 分支里 MO2 现在受 if 约束
git grep -n -A2 'if (acMessage.MO2Active)' -- Code/client/Services/Generic/TransportService.cpp

# 4. JsonEscape.h 仍是 CRLF 且无 BOM
node -e "const b=require('fs').readFileSync('Code/encoding/JsonEscape.h');console.log(b.length,'bom',b[0]===0xEF,(b.toString('utf8').match(/\r\n/g)||[]).length)"

# 5. 文档门禁
python Tools/Scripts/check_docs.py
```

复核结果:`git grep -n 'expectedVersion' -- Code/client` 只剩 `TransportService.cpp` 里那一行
(`ErrorInfo += "\"expectedVersion\": \"";`,字符串本身,不再有 `fmt::format`);
`JsonEscape::Escape` 在该文件命中 3 处;`if (acMessage.MO2Active)` 之后两行是 `{` 与
`if (!first) ErrorInfo += ",";`;`JsonEscape.h` 2738 字节、无 BOM、77 个 CRLF。

**验证**:`Code/tests/encoding.cpp:882 TEST_CASE("JSON escaping of the authentication error payload", "[encoding.json_escape]")`
覆盖转义表(空串 / 普通文本 / `"` / `\\` / `\b\f\n\r\t` / 其余控制字符走 `\u00xx` / 非 ASCII 原样)、
一个试图提前闭合文档的版本字符串(`1.6.640", "data": {"injected": true}`),以及**全部 256 个字节**
逐个过一遍:结果里每个 `"` 前面必须有奇数个反斜杠,且不能残留任何 < 0x20 的字节。
`python Tools/Scripts/check_docs.py` 全绿。

> **教训**:第一条:审查拼接式载荷时,决定要不要转义的**不是字段名,是写入者** ——
> 「本侧编译期常量」与「对端刚过线的东西」在同一个 `fmt::format` 模板里长得一模一样。
> 第二条:**长度上限不是内容安全**。这一轮之前刚刚给 26 个线上字符串加了 `WireLimits::Clamp`,
> 而它约束的是「值有多长」,不是「值里有什么」;把前者当成后者,会让一个已经修过一遍的地方
> 看起来像是已经审过了。第三条:同一个 `switch` 里两个分支写同一种数组,一个用 `first` 标志、
> 一个用「判断逗号」——**同文件同函数的两种写法不一致就是判据**,因为其中一个必然写错了。
## 65. 2026-09-29 场次:插件的会话表只增不减,而它压在每次按键的路径上 —— 判据是「清理循环的筛子比终态集合窄」

> **锚点**:本节的修复落在 `plugins/OStimTogether/src/CoopSessionManager.h`、`CoopSessionManager.cpp`、
`AddActorConsentGate.cpp`,交付形式是 `Code/plugins/patches/OStimTogether/0004-drop-finished-owner-sessions.patch`。

### 65.1 缺陷:三条写入路径、零条删除路径,唯一的清空点在读档

`CoopSessionManager::_ownerSessions` 是一个 `unordered_map<uint64_t, OwnerSession>`,键是自增的
会话号。全仓写入只有三处:

| 位置 | 作用 |
| --- | --- |
| `CoopSessionManager.cpp:311` | 直连会话(`BeginDirectStartIntent`,按住场景开始键 + 准星对着远程代理) |
| `CoopSessionManager.cpp:423` | 预检捕获(`BeginOwnerPreflight`,紧接着 `:424 _pendingOwnerByThread[preflightThreadID] = sessionID;`) |
| `AddActorConsentGate.cpp:80` | 加演员闸门(`BeginAddActorConsent`,`_ownerSessions[gateID] = std::move(gate);`) |

而删除只有一处,在 `Reset()`(`CoopSessionManager.cpp:193-205`)里:

```cpp
// plugins/OStimTogether/src/CoopSessionManager.cpp:196
_ownerSessions.clear();
```

`Reset()` 的唯一调用者是 `plugins/OStimTogether/src/main.cpp:216`(`SKSE::MessagingInterface::kPreLoadGame`),
也就是说**整张表一个存档只清一次**。失败与结束路径全都不删,只翻标志位:
`CancelOwnerSession`(`CoopSessionManager.cpp:707`)、`StartApprovedOwnerSession` 的启动失败分支
(`:679`)、`PollAddActorConsent` 的两条终态分支(`AddActorConsentGate.cpp:151`/`:181`)。

触发频率不是「偶发」:`plugins/OStimTogether/compat/OStimUIConsent/Data/Scripts/Source/OSKSE.psc:87-108`
的轮询器 `While GateState == 0 / Utility.Wait(0.10) / GateState = OStimTogetherNative.PollAddActorConsent(GateID)`
会一直轮询到返回非 0 —— 也就是说**每打开一次加演员菜单就必然命中一条终态分支**,每条都留下一份
永不回收的状态。

### 65.2 判据:清理循环的筛子比「终态集合」窄

这张表**有**清理循环,所以「没有清理」这个判据是看不见它的。清理循环在头文件里、且整个函数体内联:

```cpp
// plugins/OStimTogether/src/CoopSessionManager.h:41-57(修复前)
void CleanupCompletedDirectSessions()
{
    std::scoped_lock lock(_mutex);
    for (auto& [sessionID, session] : _ownerSessions) {
        if (session.directStartIntent &&
            !session.active &&
            !session.canceled &&
            session.activeThreadID >= 0) {
            session.canceled = true;
            _activeOwnerByThread.erase(session.activeThreadID);
        }
    }
}
```

筛子里的 `session.directStartIntent` 把它限制成「只收直连会话」—— 但三条写入路径里有两条
(`:423` 的预检捕获、`AddActorConsentGate.cpp:80` 的闸门)**永远不满足这个条件**。于是这个循环
在运行,表还是在长。**判据不是「有没有清理」,而是「清理的条件是否覆盖了所有能到达终态的条目」**:
把写入点列成一张表,再拿清理循环的条件逐个去套,套不进去的那些就是泄漏 —— 这比读清理循环本身
有用得多。

第二层是**它长在哪条路径上**。这个清理循环的调用点在按键回调里:

```cpp
// plugins/OStimTogether/src/Input.cpp:162-169
auto& coop = CoopSessionManager::GetSingleton();
coop.CleanupCompletedDirectSessions();

if (coop.TryGateDirectSceneStart(key)) {
```

而 `TryGateDirectSceneStart` 在 `CoopSessionManager.cpp:249` 又调了一次同表的全表遍历
`HasPendingDirectIntent()`(`:222-234`,`std::any_of` 扫同一个 map)。**一次按键、两遍全表扫描,**
而表长只增不减 —— 于是「内存慢慢涨」同时是「按键越来越钝」,后者才是玩家能感觉到的那个。

### 65.3 修法:终态分支真删,清理循环放宽到「所有已结束的会话」

```cpp
// plugins/OStimTogether/src/CoopSessionManager.h:46-64(修复后)
void CleanupCompletedDirectSessions()
{
    std::scoped_lock lock(_mutex);
    for (auto it = _ownerSessions.begin(); it != _ownerSessions.end();)
    {
        auto& session = it->second;
        if (!session.active && !session.restarting && !session.canceled &&
            session.activeThreadID >= 0)
        {
            _activeOwnerByThread.erase(session.activeThreadID);
            SKSE::log::trace("OSTNET COOP SESSION CLOSED session={} thread={}",
                it->first,
                session.activeThreadID);
            it = _ownerSessions.erase(it);
            continue;
        }
        ++it;
    }
}
```

条件从 `directStartIntent && !active && !canceled && activeThreadID >= 0` 放宽为
`!active && !restarting && !canceled && activeThreadID >= 0`(新增 `!restarting` 挡住「正在重启、
马上要复用」的会话),命中即 `_activeOwnerByThread.erase(...)` + `_ownerSessions.erase(it)`。
`CancelOwnerSession` 在锁内改成先取快照、再在快照上写 `canceled`、然后删表:

```cpp
// plugins/OStimTogether/src/CoopSessionManager.cpp:706-716(修复后)
const auto it = _ownerSessions.find(sessionID);
if (it == _ownerSessions.end())
{
    return;
}
snapshot = it->second;
snapshot.canceled = true;
// Dropped, not flagged. Every caller and every routing lookup
// treats a session it cannot find exactly as it treats a canceled
// one, so keeping the entry only kept it alive for the rest of the
// save - and the table is scanned on the scene-start key.
_ownerSessions.erase(it);
```

**为什么删掉是安全的**:所有调用方与所有路由查询(`RouteOwnerPayload`、`HandleControlRequest`、
`HandleThreadStart`)对「`find` 不到」与「`canceled == true`」的处理完全一致 —— 两者都走早退。
`_approvedReplayArmed` 不需要手动重置:`HandleThreadStart` 在 `find` 未命中时会走
`CoopSessionManager.cpp:1106 _approvedReplayArmed.reset();` 自愈(旧代码同样不重置)。

`AddActorConsentGate.cpp` 的三条终态分支各加一行 `_ownerSessions.erase(gateIt);`(`:152` 父会话
已失效、`:174` 已折进父会话、`:194` 拒绝)。`gateIt` 是 `const auto` 迭代器,
`erase(const_iterator)` 合法,且三处之后该分支都不再使用 `gate`。

**一处必须跟着改的语义**:`RouteOwnerPayload` 原来依赖「表里还留着一个 `canceled` 条目」来抑制
已结束线程的原始流量 —— 条目一删,`find` 就会失败,而失败分支是 `return false`,那会让
`InterceptOutgoing`(`:808-816`,只对 `START|`/`NODE|`/`SPEED|`/`STOP|` 四个前缀转发)回落到
`STRPMTransport::SendRaw` 的**全员广播**。所以该分支改成:

```cpp
// plugins/OStimTogether/src/CoopSessionManager.cpp:782-789
// The mapping can outlive the session: a scene that never
// started drops its entry, and a finished one is dropped on
// the scene-start key. This thread was still one we captured,
// so its raw traffic has to stay suppressed instead of being
// relayed to every peer the way an unknown thread's is.
return pending;
```

**这条才是这次改动里最容易踩的一脚**:把「标记」换成「删除」时,凡是**读过那个标记**的地方都
要从「读标记」改成「读不到就按标记为真的语义走」,否则一次清理会变成一次数据面回归。

### 65.4 交付:补丁而不是子模块提交

`plugins/OStimTogether` 是**本项目不拥有、也无法推送**的子模块,而 CI 跑的是
`git submodule update --init --force --recursive --depth=1` —— **工作树里对子模块的任何改动都会
被丢弃**。所以修复必须做成 `Code/plugins/patches/OStimTogether/0004-drop-finished-owner-sessions.patch`
(本仓库内容,随仓库评审/版本化/打包),由 `plugin_patches.py apply` 在配置与构建前打到子模块工作树上。

补丁正文就是 `git -C plugins/OStimTogether diff -- <那三个文件>` 的原样输出
(`7` 个 hunk / `3` 个文件 / `44` 增 `14` 删),新增行里带解释性注释。

### 65.5 复核命令(照抄)

```powershell
python Code\plugins\tools\plugin_patches.py verify
python Code\plugins\tools\plugin_patches.py check
Select-String -Path plugins\OStimTogether\src\CoopSessionManager.h -Pattern '_ownerSessions.erase|CleanupCompletedDirectSessions'
Select-String -Path plugins\OStimTogether\src\CoopSessionManager.cpp -Pattern '_ownerSessions.erase|return pending;'
Select-String -Path plugins\OStimTogether\src\AddActorConsentGate.cpp -Pattern '_ownerSessions.erase'
Select-String -Path Code\plugins\patches\OStimTogether\0004-drop-finished-owner-sessions.patch -Pattern '^\+.*_ownerSessions.erase'
python Tools\Scripts\check_docs.py
```

复核结果:`plugin_patches.py verify` → `PATCH VERIFY OK` / `11 patch(es) apply to their pinned commits`;
`check` → `PATCH CHECK OK`;`apply` 幂等(`patches: 0 applied, 11 already present`)。
`CoopSessionManager.h:46-64` 是新的迭代器版清理循环(`:60 it = _ownerSessions.erase(it);`),
`CoopSessionManager.cpp:682`/`:716` 是两处真删,`AddActorConsentGate.cpp:152`/`:174`/`:194` 是闸门的三处真删,
`CoopSessionManager.cpp:789` 是 `return pending;`。三个文件大括号平衡都是 0
(`CoopSessionManager.cpp` 1274 行 323/323、`CoopSessionManager.h` 231 行 37/37、
`AddActorConsentGate.cpp` 200 行 42/42)。`python Tools/Scripts/check_docs.py` → `DOC CHECK OK`。

> **教训**:第一条:一个容器**有**清理循环,不等于它有清理 —— 判据是「把写入点列成表,拿清理条件
> 逐个去套」,套不进去的就是泄漏。只读清理循环本身,永远看不出它的筛子比终态集合窄。
> 第二条:**「标脏」和「删除」不是同一件事**。只要还有一个读者在读那个标记,删除就会改变行为 ——
> 换掉标记之前,先把所有读者列出来,看「读不到」是否等价于「读到脏」。这次 `RouteOwnerPayload`
> 就是那个不等价的地方,而它一次按键都不在附近,只在数据面上。
> 第三条:增长的表如果**长在热路径上**,它的代价不是内存,是每次输入的延迟;判据是「谁在按键
> 回调里遍历它」。同一次按键遍历同一张表两遍(`Input.cpp:163` + `CoopSessionManager.cpp:249`),
> 两遍都该被合并或换掉。

## 66. 2026-10-01 场次:三处「读侧越过写侧已知上界」的读取 —— 判据是「这个数在写侧有上界吗,读侧用的是同一个吗」

> **锚点**:本节的修复落在 `Code/encoding/Structs/Mods.h`/`Mods.cpp`、`Code/server/Services/CharacterService.cpp`、
> `Code/server/Services/QuestService.cpp`、`Code/client/Services/Generic/QuestService.cpp`,交付形式是普通提交
> (与测试 `Code/tests/encoding.cpp` 同批)。

### 66.1 缺陷:三处「对端说了算」的读取各自越过了自己的边界

三处缺陷彼此独立,共同点是**读侧按对端给的数走,而写侧早就知道真正的上界**。

| 位置 | 缺陷 |
| --- | --- |
| `Code/encoding/Structs/Mods.cpp:38-39` | 13 位计数允许 8191,写侧封顶 4096,读侧照数 `resize` |
| `Code/server/Services/CharacterService.cpp:577-579` | 转发字幕时漏抄 `TopicFormId`,对端收到的话题恒为 0 |
| `Code/server/Services/QuestService.cpp:109-113` | 未知 `Status` 走 `default:` 不 `return`,把未初始化的 `notify.Status` 转发给全队 |

前两处的根因是**同一条流上的两个宽度不一致**:

```cpp
// Code/encoding/Structs/Mods.cpp:38-39(修复前)
const size_t modCount = data & 0xFFFF;
ModList.resize(modCount);   // 13 位读允许 8191,而写侧 :22 封顶 4096
```

```cpp
// Code/server/Services/CharacterService.cpp:577-579(修复前)
NotifySubtitle notify{};
notify.ServerId = message.ServerId;
notify.Text = message.Text;      // TopicFormId 从未被写,线上恒为 0
```

第三处不是「值错了」而是「值根本没被写」:

```cpp
// Code/server/Services/QuestService.cpp:109-113(修复前)
default:
    spdlog::error("{}: unknown quest status {} quest: {:X}, stage: {}, status {}, by {} {:X}",
                  __FUNCTION__, notify.Status, message.Id.LogFormat(), notify.Stage, notify.Status, ...);
    break;   // 不 return;notify.Status 在这条路径上从未被赋值
```

注意这条日志自己就说明了问题:它打印的是 `notify.Status` 两次,而**请求里的 `message.Status` 一次都没打印**。
日志想报「未知状态」,却把那个未知状态丢掉了,只留下一个未初始化的副本。

### 66.2 判据:写侧有上界,读侧必须用同一个上界;写侧有字段,读侧必须逐个抄

**判据一(宽度对称)**:凡是从线上读出一个计数、再拿它驱动 `resize`/`reserve`/循环的,先去找**写侧**
写这个计数的那一行 —— 如果写侧有 `std::min(..., kMax)` 或硬编码封顶,而读侧没有,那么两者对同一条流
的解释必然不同:写侧永远发不出超过 `kMax` 的条目,读侧却会为一个更大的数分配内存,并在包已耗尽后继续读。
本仓的标准写法是**两侧共用同一个常量**:

```cpp
// Code/encoding/Structs/Factions.cpp:39-42(范式)
// Never trust the wire length, and never throw: this runs inside a noexcept frame.
const auto npcCount = std::min<uint64_t>(Serialization::ReadVarInt(aReader), kMaxFactionCount);
NpcFactions.resize(npcCount);
```

**判据二(字段对称)**:服务器 handler 里构造的转发消息,凡是与请求**同名**的字段,要么显式抄,
要么在注释里写明为什么不抄。判据不是「读了代码觉得没问题」,而是**把请求类型与通知类型的字段列出来逐个对**——
这次就是靠这个对出来的:三处 handler 里只有一处漏抄。

**判据三(`default:` 分支的出口)**:`switch` 的 `default:` 里只要有一条语句会**继续往下走**(不 `return`),
就必须确认「这条路径上后面要用的每个字段都已被赋值」。本仓的消息类型几乎都有**用户提供的**默认构造函数
(`X() : ServerMessage(Opcode) {}`),按 C++ 的规则它们**不是聚合**,`X x{};` **不会零初始化**成员:

```cpp
// Code/encoding/Messages/NotifyQuestUpdate.h:10-13
NotifyQuestUpdate() : ServerMessage(Opcode) {}
// :28-32
GameId Id; uint32_t SceneMaster{0}; uint16_t Stage; uint8_t Status; uint8_t ClientQuestType;
```

只有 `SceneMaster` 有默认成员初始化器,`Stage`/`Status`/`ClientQuestType` 没有,而 `Status` 只在
四个已知分支里被赋值。⇒ 未知 `Status` 这条路径上,`notify.Status` 是**不确定值**,不是 0。
对照 `Code/encoding/Structs/GameId.h:7 GameId() = default;` —— 首次声明即 defaulted,**不是**用户提供,
所以 `GameId x{};` 仍然零初始化。两者的区别只在「那个 `{}` 是谁写的」。

### 66.3 可达性

第一条在**认证包解析路径**上,即未认证连接可达:`Code/server/GameServer.cpp:572-602 OnConsume` 是
先 `factory.Extract(reader)` 解析、再按 opcode 分派到 handler,认证发生在 `HandleAuthenticationRequest` 里,
而 `Mods::Deserialize` 在 `AuthenticationRequest::DeserializeRaw` 里(`AuthenticationRequest.cpp:30`)。
一个 8191 的计数会让服务器或客户端为 8191 个 `Mods::Entry`(每项含一个最长 260 字节的 `String`)分配内存。

第二条是**每个玩家说台词时的正常路径**,不崩,只是信息被丢掉:客户端 `CharacterService.cpp:1219` 把
`acEvent.TopicFormID` 写进请求,服务器转发时丢掉,客户端 `CharacterService.cpp:1233` 拿 `acMessage.TopicFormId`
去 `TESForm::GetById`,于是永远拿到 0 对应的表单(或空)。

第三条需要一个**伪造包**:客户端自己的三个发送点只会写 0..3(其中 `Code/client/Services/Generic/QuestService.cpp:188`
写的是 `stageId == RequestQuestUpdate::StageUpdateForced`,是布尔值,只能得到 0 或 1),而
`RequestQuestUpdate::DeserializeRaw` 对 8 位的 `Status` 没有范围校验 ⇒ 任意 8 位值都能到 `default:`。

### 66.4 修法

`Mods` 新增与写侧同源的常量(位置与 `Factions.h`/`ActorValues.h` 一致,放在成员之后):

```cpp
// Code/encoding/Structs/Mods.h:33-35
static constexpr uint64_t kMaxModCount = 4096;
```

两侧都改用它:`Mods.cpp:22` 的写侧 `std::min<uint64_t>(ModList.size(), kMaxModCount)`,
`Mods.cpp:40` 的读侧 `std::min<uint64_t>(data & 0xFFFF, kMaxModCount)`。
`CharacterService.cpp:580` 补一行 `notify.TopicFormId = message.TopicFormId;`。
`QuestService.cpp:109-117` 的 `default:` 改为打印**请求里的** `message.Status`/`message.Stage` 并 `return`。
顺带把客户端 `QuestService.cpp:332-338` 的 `default:` 里那句写在 `break;` 之后的 `spdlog::error` 提到 `break;` 之前
(它原先永远不可达,未知状态在客户端不留任何日志)。

### 66.5 复核命令(照抄)

```powershell
git grep -n "kMaxModCount" -- Code/encoding
git grep -n "TopicFormId" -- Code/server/Services/CharacterService.cpp Code/encoding/Messages/NotifySubtitle.h
git grep -n "unknown quest status" -- Code/server/Services/QuestService.cpp
git grep -n "unknown remote status" -- Code/client/Services/Generic/QuestService.cpp
python Tools/Scripts/check_docs.py
```

复核结果:`kMaxModCount` 命中三处(`Mods.cpp:22`、`Mods.cpp:40`、`Mods.h:35`,写读两侧同一个常量);
`TopicFormId` 在服务器侧命中 `Code/server/Services/CharacterService.cpp:580:    notify.TopicFormId = message.TopicFormId;`;
`unknown quest status` 在 `QuestService.cpp:114` 单行命中且已改用 `message.Status`/`message.Stage`;
`unknown remote status` 在 `Code/client/Services/Generic/QuestService.cpp:334` 单行命中(现在可达);
`check_docs.py` → `DOC CHECK OK`。

**验证**:`Code/tests/encoding.cpp:937 TEST_CASE("Mods count is bounded by the writer bound", "[encoding.mods_count]")`
断言 `Mods::kMaxModCount == 4096ull`、4096+7 条 mod 往返后只剩 4096 条、以及**手工写入计数 8191 的包**
被夹到 4096 且最后一条 entry 的 id 正确;`Code/tests/encoding.cpp:980 TEST_CASE("Subtitle topic travels in both directions", "[encoding.subtitle]")`
让 `SubtitleRequest` 与 `NotifySubtitle` 各走一次 `Serialize` → 弹 8 位 opcode → `DeserializeRaw`,
断言 `TopicFormId` 往返保持。

> **教训**:第一条:**「这个字段有默认值」和「这个字段被赋过值」是两回事,而它们看起来一模一样。**
> `uint32_t SceneMaster{0};` 与 `uint16_t Stage;` 在同一个结构体里相邻两行,前者是「一定安全」,
> 后者是「构造点没写就是不确定值」—— 唯一的区分办法是把**构造点的赋值清单**与**字段清单**对一遍,
> 而不是读构造点觉得「该写的都写了」。
> 第二条:**宽度不一致的读法,在正常流量下永远不报错。** 写侧封顶 4096、读侧允许 8191,
> 只要对端守规矩,两边都好好的 —— 它只在「对端不守规矩」时暴露,而那正是这条链要防的。
> 所以判据不能是「跑起来没问题」,必须是「把写侧那一行的上界找出来,看读侧是不是同一个」。
> 第三条:**日志里打错了变量,等于把唯一的线索也删了。** `default:` 分支打印了两次 `notify.Status`
> (未初始化值),一次都没打印 `message.Status`(真正未知的那个)。当一条日志本身就在说「我看到了未知的东西」,
> 它打印的必须是**那个未知的东西**,而不是从它派生出来的、可能已经被覆盖的副本。

## 67. 2026-10-02 场次:两个插件的最高频日志点都开着同步刷盘 —— 判据是「这条日志一秒能打多少次,而它每次都落盘吗」

> **锚点**:本节的修复落在 `plugins/AnimSyncTogether`(`src/main.cpp`、`src/AnimationInputProbe.cpp`、
> `src/AnimationProbe.cpp`、`README.md`),交付形式是 `Code/plugins/patches/AnimSyncTogether/0001-...patch`
> —— 该插件是子模块,CI 的 `git submodule update --init --force --recursive` 会丢弃工作树改动,只有补丁能落地。

### 67.1 缺陷:每一条 info 都同步刷盘,而两条最热的日志就在这个等级上

`AnimSyncTogether` 的日志初始化是 `set_level(trace)` + `flush_on(trace)`。`flush_on(N)` 的语义是
「等级 ≥ N 的每条消息立刻刷盘」,而 trace 是最低等级 ⇒ **每一条日志都同步落盘**,与等级设置无关。

本机 10 分钟会话的实测日志(路径见 67.5)把频率钉死了:

| 位置 | 10 分钟行数 | 触发时机 |
| --- | --- | --- |
| `plugins/AnimSyncTogether/src/AnimationInputProbe.cpp:76` | 39834(92.1%) | 每一次本地动画图输入 |
| `plugins/AnimSyncTogether/src/AnimationProbe.cpp:228` | 3407(7.9%) | 每一个动画图事件 |

同一份日志里 **零条 trace**:43271 行全部是 `[info]`。所以 `flush_on(trace)` 与 `flush_on(info)`
在这台机器上的效果**完全相同** —— 43271 次同步刷盘 / 600 秒 ≈ **72 次每秒**,最忙的一秒(23:55:48)
是 290 行。这两条日志都是 `SKSE::log::info`,而且是**无条件**的:

```cpp
// plugins/AnimSyncTogether/src/AnimationInputProbe.cpp:76-80(修复前)
SKSE::log::info(
    "AnimInput actor=00000014 localPlayer=true event='{}' result={} tracked={}",
    name,
    result,
    tracked);
```

```cpp
// plugins/AnimSyncTogether/src/AnimationProbe.cpp:228(修复前)
SKSE::log::info(
    "AnimEvent actor={:08X} base={:08X} name='{}' localPlayer={} remoteConnection={} tag='{}' payload='{}'",
    formID, base ? base->GetFormID() : 0, actor->GetName(), isLocalPlayer, connectionID.value_or(0), tag, payload);
```

### 67.2 判据:`tracked` 这个字段本来就写着「这条线该不该留」

同一份日志里 `AnimInput` 的 `tracked=` **39834 行全是 `false`**,`result=` 只有 529 行是 `true`。
也就是说这条被无条件打印的行,**从来没有一次**描述过一个被同步的事件:真正要同步的事件走的是
`AnimationInputProbe.cpp:85 if (result && tracked)` 之后那条 `AnimTxInput` 路径,而那条路径有它自己的日志。
同理 `AnimEvent` 的 3407 行里 `localPlayer=` 全是 `true`、`remoteConnection=` 全是 `0`,
它描述的是**本机玩家**的图事件,不是任何一个远端代理。

判据因此不是「日志太多」,而是:**一条日志的每一个字段都在说「这次不用管」,它却按 info 打印并同步刷盘。**
把这两条降到 `debug` 之后,默认等级下它们不再产生任何 I/O,而排查时把等级调回 `debug`/trace 仍能拿到同样形状的行。

### 67.3 可达性:它落在渲染/输入主线程上,每个动画图输入一次

`AnimationInputProbe` 装的是 `RE::VTABLE_PlayerCharacter[3]` 的 vfunc(`src/AnimationInputProbe.cpp:27`
`playerGraphVTable.write_vfunc(0x1, PlayerNotifyAnimationGraph)`),`AnimationProbe` 装的是动画图事件 sink。
两者都在**本地玩家的动画图输入/事件路径上同步执行**,也就是每次移动、攻击、受击、换装备。
`basic_file_sink_mt` 的刷盘是阻塞写 + 加锁;它每次都要抢一个 mutex 再进内核。

这正好命中用户四项硬要求里的第四条(**不得让加载、过场动画、与 NPC/场景交互变慢**):
一秒钟 72 次同步刷盘、峰值一秒 290 次,全部发生在主线程的动画路径上。
本仓主程序对同一件事早有结论 —— `Code/client/TiltedOnlineApp.cpp:75-80`:

```cpp
// Code/client/TiltedOnlineApp.cpp:75-80
spdlog::flush_every(std::chrono::seconds(1));
// A crash inside a hook loses everything logged in the last second, which
// is exactly the second worth reading. Warnings and worse go to disk
// immediately; the info stream stays buffered so per-frame logging does
// not turn into per-frame file I/O.
logger->flush_on(spdlog::level::warn);
```

插件走的是**完全相反**的配置,而它比主程序更贴近每帧路径。

### 67.4 修法:改成主程序同款的 flush 策略,并把两条每事件日志降一级

`src/main.cpp` 三行改动(与 `Code/client/TiltedOnlineApp.cpp:75-80` 同一策略):

```cpp
// plugins/AnimSyncTogether/src/main.cpp:28-33(修复后)
log->set_level(spdlog::level::info);
// Trace level plus flush_on(trace) pushed every line to disk before the game
// thread could continue, and two of those lines fired on every animation graph
// event. Warnings stay immediate; the info stream is flushed once a second.
spdlog::flush_every(std::chrono::seconds(1));
log->flush_on(spdlog::level::warn);
```

`set_level(info)` 与 `flush_on(warn)` 一起意味着:`info` 行先进内存缓冲、最多晚一秒落盘;
`warn`/`err`/`critical` 仍然立刻落盘(所以崩溃时最后那一秒的告警不会丢)。
`AnimationInputProbe.cpp:76` 与 `AnimationProbe.cpp:228` 由 `SKSE::log::info` 改为 `SKSE::log::debug`,
并在各自上方留一行说明为什么它不该在默认等级上。

`README.md` 的 `## Logging` 节列了这些标记名,所以同一批改掉文案:`AnimInput`/`AnimEvent` 标注为
`debug` 级,并说明 Helmet Toggle 的那几条诊断(`GPMAStateTx`/`GPMAStateApply`/`GPMAStateReset`/`GPMAStateAutoReset`/
`GPMAHelmetMarkerCleanup`/`ClipSelection`)仍按默认等级打印 —— 它们本来就是低频的。

### 67.5 复核命令(照抄)

```powershell
python Code\plugins\tools\plugin_patches.py verify
python Code\plugins\tools\plugin_patches.py check
git -C plugins/AnimSyncTogether grep -n "flush_on\|flush_every\|set_level"
git -C plugins/AnimSyncTogether diff --stat
python Tools/Scripts/check_docs.py
```

复核结果:`PATCH VERIFY OK` / `12 patch(es) apply to their pinned commits`;`PATCH CHECK OK`;
`set_level`/`flush_every`/`flush_on` 命中 `src/main.cpp:28`/`:32`/`:33` 三行(且 `flush_on(trace)` 已无命中);
`git diff --stat` = 4 files changed, 20 insertions(+), 5 deletions(-);`check_docs.py` → `DOC CHECK OK`。

**验证**:该补丁已应用到 `plugins/AnimSyncTogether` 工作树,`plugin_patches.py` 的 `is_applied`
(`git apply --check --reverse`)与 `cmd_verify` 的 `git apply --check --cached` 两条路径都通过 ——
后者是对**索引**(= pin 的内容)校验,所以「已经应用」不能掩盖补丁与 pin 脱节。
实测证据来自本机 `Documents/My Games/Skyrim Special Edition/SKSE/AnimSyncTogether.log`:
10 分钟 43271 行、其中 `AnimationInputProbe.cpp:76` 39834 行、`AnimationProbe.cpp:228` 3407 行、
`tracked=` 全为 `false`、`localPlayer=` 全为 `true`、`remoteConnection=` 全为 `0`、零条 trace。

> **教训**:第一条:**`flush_on` 的参数是「刷盘起点」,不是「日志等级」。** `set_level(trace)` + `flush_on(trace)`
> 读起来像「只刷 trace」,实际是「每条都刷」。要看一条日志的真实成本,必须把两个设置**分开**读:
> 等级决定打不打,flush_on 决定打出来之后要不要立刻进内核。
> 第二条:**一条日志的字段表就是它的频率说明书。** `tracked=false` 占 100%、`remoteConnection=0` 占 100%,
> 说明这条线在正常游戏里**一次都没有描述过它要描述的对象**;这种行不该待在默认等级上。
> 判断「该不该降级」不需要猜频率,把日志按字段值分组数一遍就有答案。
> 第三条:**同一个问题在本仓已经有过判例,就不要重新发明配置。** `Code/client/TiltedOnlineApp.cpp:75-80`
> 已经把「每秒 flush + warn 立即落盘」和它的理由写在注释里了;插件只要照抄这个策略,
> 连注释要说什么都是现成的。

## 68. 2026-10-03 场次:同一个插件把「每条日志都刷盘」和「每次变化都列全部明细」一起开着 —— 判据是「这条日志描述的是状态变化,还是状态快照」

> **锚点**:本节的修复落在 `plugins/MorphSyncTogether`(`src/main.cpp`、`src/AppearanceProbe.h`、
> `src/Config.h`、`package/Data/SKSE/Plugins/MorphSyncTogether.ini`、`README.md`),交付形式是
> `Code/plugins/patches/MorphSyncTogether/0003-stop-flushing-every-log-line-and-disable-the-appearance-detail-log.patch`
> —— 该插件是子模块,CI 的 `git submodule update --init --force --recursive` 会丢弃工作树改动,只有补丁能落地。

### 68.1 缺陷:一条 info 在默认配置下同步刷盘,而它一次采样最多打 45 条

`MorphSyncTogether` 的日志初始化与 §67 的 `AnimSyncTogether` 逐字同构:`src/main.cpp:21` 是
`spdlog::set_level(spdlog::level::trace);`,`src/main.cpp:22` 是 `spdlog::flush_on(spdlog::level::trace);`。
`flush_on(N)` 的参数是**刷盘起点**而不是日志等级,trace 又是最低等级 ⇒ **每条日志都同步落盘**。
本机 10 分钟会话的实测日志(路径见 68.5)把频率钉死了:

| 位置 | 10 分钟行数 | 触发时机 |
| --- | --- | --- |
| `plugins/MorphSyncTogether/src/AppearanceProbe.cpp:814` | 1433(76.9%) | 每次外观采样,每个 detail 一行 |
| `plugins/MorphSyncTogether/src/UdpTransport.cpp:195` | 187(10.0%) | 每次 STRPM 发送失败 |
| `plugins/MorphSyncTogether/src/MorphSyncService.cpp:469` | 94(5.0%) | 每次 morph 快照广播 |
| `plugins/MorphSyncTogether/src/TngSync.cpp:177` | 93(5.0%) | 每次 TNG 尺寸广播 |
| `plugins/MorphSyncTogether/src/AppearanceProbe.cpp:794` | 36(1.9%) | 每次外观哈希变化 |

同一份日志里 **零条 trace、零条 debug**:1864 行 = `info` 1675 + `warning` 187 + `error` 2,
而两条 error 都是启动期一次性的接口版本告警。所以 `flush_on(trace)` 与 `flush_on(info)` 在这台机器上
效果**完全相同** —— 586 秒里 1864 次同步刷盘,其中 1433 次来自同一条日志。

那 1433 行不是「一秒一条」,而是「一次采样一簇」:

```cpp
// plugins/MorphSyncTogether/src/AppearanceProbe.cpp:812-819(循环本身未改动,变的是它的闸门默认值)
if (_verbose) {
    for (const auto& detail : data.details) {
        SKSE::log::info(
            "MST APPEARANCE DETAIL scope={} label=\"{}\" actor={:08X} {}",
            scope,
            label,
            actor->GetFormID(),
            detail);
    }
}
```

### 68.2 判据:紧挨着它的那条 CHANGE 已经把同一份状态总结成一行了

同一个函数在 DETAIL 之前 18 行已经打过一次 `MST APPEARANCE CHANGE`(`src/AppearanceProbe.cpp:771-787`,
一行 14 个字段:哈希、tint、overlay、各类节点计数、`details=` 的**个数**),它只在 `changed` 时触发。
DETAIL 则在**每一次**采样时把 `details` 里每个元素单独打一行 —— 同一份状态被描述了两遍,
一遍是总结,一遍是逐元素展开。

实测的展开规模:

| 量 | 值 |
| --- | --- |
| `MST APPEARANCE DETAIL` 行数 / `MST APPEARANCE CHANGE` 行数 | 1433 / 36 |
| `scope=` 的 distinct 值 | 1(全是 `LOCAL`) |
| `actor=` 的 distinct 值 | 1(全是 `00000014`,即本地玩家) |
| distinct payload / 总行数 | 131 / 1433 |
| 探测次数 / 每次 detail 数 | 58 次,min 1 / avg 24.71 / max 45 |

也就是说这条日志**从来没有描述过第二个作用域或第二个角色**,而且每次探测都在重复上几次已经说过的内容。
判据因此不是「日志太多」,而是:**一条日志描述的是状态快照而不是状态变化,它就不该待在默认等级上。**
真正的状态变化由 CHANGE 那一行负责,而它一共只有 36 行。

这个闸门的三处默认值原本全是开的 —— 这是「出厂即开启」而不是「用户打开的」:

```cpp
// plugins/MorphSyncTogether/src/AppearanceProbe.h:164(修复前)
bool _verbose{ true };
```

```ini
; plugins/MorphSyncTogether/package/Data/SKSE/Plugins/MorphSyncTogether.ini:32(修复前)
Verbose=1
```

第三处是 `src/Config.h:39 bool appearanceProbeVerbose{ true };`。三处必须同改:ini 的值经
`src/Config.cpp:126 cfg.appearanceProbeVerbose = ReadBool(L"AppearanceProbe", L"Verbose", cfg.appearanceProbeVerbose);`
覆盖成员默认值,只改头文件等于没改。`Verbose` 与 `MST APPEARANCE DETAIL` 在插件的 README、fomod、
`build-vortex.ps1` 与父仓任何 check 脚本里都是 **0 命中**,所以改默认值不需要同步任何文档或打包清单。

### 68.3 可达性:它在游戏主线程上,而一次 tick 最多打 45 条

`MorphSyncService::SyncLoop` 按 `_config.syncIntervalMs`(默认 1000 ms)切成 100 ms 片轮询,每轮调
`QueueTick()`,后者用 `SKSE::GetTaskInterface()->AddTask(...)` 把 `TickOnGameThread()` 投到**游戏主线程**;
`TickOnGameThread()` 的第一件事之一就是 `src/MorphSyncService.cpp:290`
`AppearanceProbe::GetSingleton().ProbeLocalPlayer(player);`。所以那次「一次 45 行」的循环,
连同它的 45 次 `basic_file_sink_mt` 阻塞刷盘,全部发生在主线程的一个 tick 里。

同一节的另外两条也在这条链上:`src/MorphSyncService.cpp:469` 与 `src/TngSync.cpp:177` 的 `Send`/日志
都在 `TickOnGameThread` 内;`src/UdpTransport.cpp:195` 的 warn 由它们同步触发。实测那 187 条 warn 全是
`result=not-connected`(未连接时每次发送失败都打一条),跨 128 个不同的秒。

这命中用户四项硬要求里的第四条(**不得让加载、过场动画、与 NPC/场景交互变慢**):
一秒最多 50 行、单次 tick 最多 45 行,每行都抢锁进内核。本仓主程序对同一件事早有结论
—— `Code/client/TiltedOnlineApp.cpp:75-80`:

```cpp
// Code/client/TiltedOnlineApp.cpp:75-80
spdlog::flush_every(std::chrono::seconds(1));
// A crash inside a hook loses everything logged in the last second, which
// is exactly the second worth reading. Warnings and worse go to disk
// immediately; the info stream stays buffered so per-frame logging does
// not turn into per-frame file I/O.
logger->flush_on(spdlog::level::warn);
```

### 68.4 修法:照抄主程序的 flush 策略,并把「逐明细」闸门的默认值翻过来

`src/main.cpp` 只改 `flush_on` 那一行 —— `set_level(trace)` **保留不动**,因为 Morph 的全部 3 个
`SKSE::log::trace` 站点都落在「已经决定不发」的分支上(`MST MORPH APPLY SKIP` / `MST TNG APPLY SKIP` /
`MST STRPM RX`),把等级抬到 info 是行为变化,而这次的缺陷是 flush 策略:

```cpp
// plugins/MorphSyncTogether/src/main.cpp:22-26(修复后)
// flush_on(trace) pushed every line to disk before the game thread could
// continue, and a single appearance probe emits up to 45 of them in one
// tick. Warnings stay immediate; the info stream is flushed once a second.
spdlog::flush_every(std::chrono::seconds(1));
spdlog::flush_on(spdlog::level::warn);
```

`_verbose` 的默认值在三处一起翻成 false:`src/AppearanceProbe.h:164`、`src/Config.h:39`、
`package/Data/SKSE/Plugins/MorphSyncTogether.ini`(`Verbose=0`,并在上方三行注释里写明「一次探测最多 45 行、
每行都曾在游戏线程上落盘」)。想排查时把它设回 1 即可,行文形状与原来完全一样。
`README.md` 的 `## Expected log` 节末尾补一段说明这一点。

### 68.5 复核命令(照抄)

```powershell
python Code\plugins\tools\plugin_patches.py verify
python Code\plugins\tools\plugin_patches.py check
git -C plugins/MorphSyncTogether grep -n "flush_on\|flush_every\|set_level"
git -C plugins/MorphSyncTogether grep -n "_verbose{ \|appearanceProbeVerbose{ \|Verbose="
git -C plugins/MorphSyncTogether diff --stat
python Tools/Scripts/check_docs.py
```

复核结果:`PATCH VERIFY OK` / `13 patch(es) apply to their pinned commits`;`PATCH CHECK OK`;
`flush_every`/`flush_on(warn)` 命中 `src/main.cpp:25`/`:26` 且 `flush_on(spdlog::level::trace)` 已无命中;
`_verbose{ false }` / `appearanceProbeVerbose{ false }` / `Verbose=0` 三处一致;
`git diff --stat` = 9 files changed, 79 insertions(+), 10 deletions(-)(其中本节只占 5 个文件,
其余 4 个是既有的 0001/0002);`check_docs.py` → `DOC CHECK OK`。

**验证**:该补丁已应用到 `plugins/MorphSyncTogether` 工作树,`plugin_patches.py` 的 `is_applied`
(`git apply --check --reverse`)与 `cmd_verify` 的 `git apply --check --cached` 两条路径都通过 ——
后者是对**索引**(= pin 的内容)校验,所以「已经应用」不能掩盖补丁与 pin 脱节。
实测证据来自本机 `Documents/My Games/Skyrim Special Edition/SKSE/MorphSyncTogether.log`:
586 秒 1864 行(info 1675 / warning 187 / error 2)、零条 trace 与 debug、
`AppearanceProbe.cpp:814` 1433 行、`scope=` 与 `actor=` 各只有一个值、distinct payload 131/1433、
58 次探测里单次最多 45 行、最忙的一秒 50 行。

> **教训**:第一条:**同一个插件可以同时犯 §67 的两个变体,而它们是两个独立开关。**
> `flush_on` 决定「打出来之后要不要立刻进内核」,日志的**闸门默认值**决定「打不打」;
> 只看 logger 初始化会漏掉后者,只看日志内容会漏掉前者。
> 第二条:**一条日志描述的是「变化」还是「快照」,决定了它该不该默认打开。**
> 同一次状态变化,CHANGE 那行是 36 条、DETAIL 那些行是 1433 条,信息量却更少 ——
> 判据是「把这条日志按 distinct payload 分组数一遍,如果绝大多数行在重复上几次的内容,它就不是事件日志」。
> 第三条:**默认值写在几个地方,就要改几个地方。** `_verbose` 的默认值同时存在于成员初始化、
> `Config` 结构体和随包发布的 ini 里,而 ini 覆盖前两者 —— 只改头文件,出厂行为一点都不会变。

## 69. 2026-10-01 场次:血量广播的两端都只留下了钳制、没留下读取 —— 判据是「断言能不能区分『读进来了』和『碰巧等于』」

> **锚点**:本节的修复落在 `Code/encoding/Messages/RequestHealthChangeBroadcast.cpp`、
> `Code/encoding/Messages/NotifyHealthChangeBroadcast.cpp` 与 `Code/tests/encoding.cpp`,交付形式是仓库内的直接提交
> (不是插件补丁 —— 这两个文件属于主仓,CI 直接编译)。

### 69.1 缺陷:钳制块是在替换读取那一行时写进去的,于是钳制作用于一个未初始化的成员

| 位置 | 作用 |
| --- | --- |
| `Code/encoding/Messages/RequestHealthChangeBroadcast.cpp:12`(修复前) | 读完 `Id` 直接进钳制,`DeltaHealth` 的 4 个字节从不被消费 |
| `Code/encoding/Messages/NotifyHealthChangeBroadcast.cpp:12`(修复前) | 服务端 → 客户端方向同构 |
| `Code/encoding/Messages/RequestHealthChangeBroadcast.h:25` | `float DeltaHealth;` 没有默认成员初始化器 |
| `Code/server/Services/ActorValueService.cpp:90` / `:95` | 把这个值写进 `ActorValuesList[24]`,再原样放进转发包 |
| `Code/client/Services/Generic/ActorValueService.cpp:295-305` | 加进目标血量,结果不再为正时对非玩家 actor 调 `pActor->Kill()` |

两个文件在修复前的形状完全一样(只差基类与 opcode):

```cpp
// Code/encoding/Messages/RequestHealthChangeBroadcast.cpp:11-24(修复前)
void RequestHealthChangeBroadcast::DeserializeRaw(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    ClientMessage::DeserializeRaw(aReader);

    Id = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;

    // Never trust the wire: a NaN or an infinity would follow this value into every
    // arithmetic operation the receiver performs on it.
    if (!std::isfinite(DeltaHealth))
        DeltaHealth = 0.f;
    else
        DeltaHealth = std::min(std::max(DeltaHealth, -kMaxHealthDelta), kMaxHealthDelta);
}
```

写侧一直是 `Serialization::WriteFloat(aWriter, DeltaHealth);`(`:8`),而
`git grep -n ReadFloat` 在这两个文件上**零命中** —— 也就是说读取语句不是被改坏,而是**在加入钳制的
那次提交里被整行替换掉了**。于是这条链上出现了三个各自独立的后果:

1. 线上那 4 个字节从不被消费(对该消息来说后面没有字段,所以不会连带错位);
2. `if (!std::isfinite(DeltaHealth))` 检查的是**未初始化的成员**;
3. 这个不确定值随后被当成真实血量增量用掉。

成员为什么真的是未初始化的:`RequestHealthChangeBroadcast.h:24-25` 是 `uint32_t Id;` 与
`float DeltaHealth;`(**两者都没有 `{}` 初始化器**),构造器是
`RequestHealthChangeBroadcast() : ClientMessage(Opcode) {}`,`Message.h:9-31` 的基类只保存 opcode,
工厂则是 `ClientMessageFactory.cpp:17-22` 的 `MakeUnique<T>()` + `DeserializeRaw(aReader)`。
⇒ 这不是「读到了 0」,是「读都没读」。

### 69.2 判据:断言只检查钳制的结果,而钳制对未初始化内存是恒等

`Code/tests/encoding.cpp:620` 的 `TEST_CASE("Health change broadcast wire bounds", "[encoding.health_change]")`
**是这个缺陷的同一次提交里新增的**,它当时断言三件事:

| 输入 | 断言 |
| --- | --- |
| `quiet_NaN()` | `REQUIRE(nan.DeltaHealth == 0.f)` |
| `-1.0e30f` | `REQUIRE(clamped.DeltaHealth == -RequestHealthChangeBroadcast::kMaxHealthDelta)` |
| `1.0e30f` | `REQUIRE(notification.DeltaHealth == NotifyHealthChangeBroadcast::kMaxHealthDelta)` |

三条断言全部只依赖**钳制**。当接收对象刚被构造出来时,它的 `DeltaHealth` 恰好是 0 的话:
`!std::isfinite(0.f)` 为假、`std::min(std::max(0.f, -1e5f), 1e5f)` 也是 0 —— 但 `0.f` 与 `1.0e30f`
这两个输入走的是钳制的**另两条分支**,它们同样不看线上读到的东西。换句话说:**这三条断言在
「读取完全缺失」和「读取正常」两种实现上都会通过**,只要那块内存不是 NaN 或越界值。

这就是判据的落点:**一条针对「读进来的值被怎么处理」的断言,必须能让「根本没读」失败。**
做法是在调用 `DeserializeRaw` 之前给接收对象写一个**有限的、钳制分支的恒等值**:

```cpp
// Code/tests/encoding.cpp:630-638(修复后)
RequestHealthChangeBroadcast nan{};
// Seeded with a finite sentinel: the clamp would happily keep it, so the read itself
// is the only thing that can turn this into the zero the assertion expects.
nan.DeltaHealth = 1.f;
Buffer::Reader reader(&buff);
nan.DeserializeRaw(reader);

REQUIRE(nan.Id == 42);
REQUIRE(nan.DeltaHealth == 0.f);
```

`1.f` 满足 `std::isfinite`、也落在 `±kMaxHealthDelta` 之内,所以钳制对它是**恒等**。于是断言成立的
唯一原因只剩读取那一行:把它删掉,`REQUIRE(nan.DeltaHealth == 0.f)` 立刻变成 `1.f == 0.f`。
四个接收对象(`nan`、`clamped`、`notification`、`received`)都按同一写法加了哨兵。

### 69.3 可达性:发送侧一直在送真实伤害,所以它表现为「血量乱跳/秒杀」而不是「永远 0」

发送侧是热的:`Code/client/Services/Generic/ActorValueService.cpp:189-202` 里,`|delta| < 1.0f` 的伤害
攒进 `m_smallHealthChanges`(`:189`),其余立刻发 `RequestHealthChangeBroadcast`(`:198-202`);
`RunSmallHealthUpdates`(`:207-233`)每 250 ms 把攒下的批量发出去并清表。
服务端 `Code/server/Services/ActorValueService.cpp:77-100 OnHealthChangeBroadcast` 把它写进
`actorValuesComponent.CurrentActorValues.ActorValuesList[24]`(`:90`)并转发(`:95`);
客户端 `Code/client/Services/Generic/ActorValueService.cpp:286-312` 读回来,结果 `<= 0.f` 且目标不是玩家时
`pActor->Kill()`。⇒ 未初始化的增量直接决定「范围内的非玩家 actor 死不死」。

另外这条路径是全仓唯一**没有 ownership 检查**的 actor value handler:`:31`/`:58`/`:110` 三处都有
`IsCurrentOwner`。这不改变本节的性质(值本身就该有界),但它解释了为什么这个值一旦不对,影响面是
「任何在范围内的 actor」。

### 69.4 修法:补回读取那一行,并让用例无法在缺失读取时通过

`DeserializeRaw` 只加一行,位置在 `Id` 之后、钳制之前 —— 钳制逻辑与既有断言都不用动:

```cpp
// Code/encoding/Messages/RequestHealthChangeBroadcast.cpp:15-17(修复后)
Id = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
DeltaHealth = Serialization::ReadFloat(aReader);

// Never trust the wire: a NaN or an infinity would follow this value into every
```

`NotifyHealthChangeBroadcast.cpp:16` 逐字相同。用例侧只加哨兵赋值与两行注释,断言一个字没改。

顺带做了一次机械普查,确认这一族已经封闭:对 `Code/encoding/Messages/**` 与 `Code/encoding/Structs/**`
的全部 `.cpp` 提取 `SerializeRaw` 的标识符集合与 `Deserialize*` 的标识符集合做差集。原始差集 44 个文件命中,
但绝大多数是读侧用临时变量赋值造成的误报(形如
`uint64_t tmp; aReader.ReadBits(tmp, N); Field = tmp & MASK;`,脚本看不到 `tmp` → `Field` 那一步),
按头文件里真实声明的成员名过滤后只剩 17 个成员级候选,逐一读源码后**只有这两个文件是真缺陷**;
反向差集(读了但从不写)在同样范围上是 **0 命中**。

### 69.5 复核命令(照抄)

```powershell
git grep -n "ReadFloat" Code/encoding/Messages/RequestHealthChangeBroadcast.cpp Code/encoding/Messages/NotifyHealthChangeBroadcast.cpp
git grep -n "ReadFloat" Code/encoding/Messages/ | Measure-Object -Line
git grep -c "Seeded with a finite sentinel" Code/tests/encoding.cpp
git diff --stat
python Tools/Scripts/check_docs.py
```

复核结果:两个文件各命中 `:16`;`Code/tests/encoding.cpp` 的哨兵注释为 4 处;
`check_docs.py` → `DOC CHECK OK`。C++ 测试由 CI 执行(`windows.yml:585-638` 的
`Run the encoding tests` 要求退出码 0 **且**输出里含 `All tests passed`)。

**验证**:`5660971b` 的 diff 证明读取那一行是在加入钳制时被替换掉的(不是后来改坏的);
`git merge-base --is-ancestor cd0d90dd 5660971b` 与 `fb4ff630` 的先后关系说明测试门禁**早于**该缺陷存在,
所以这次的假绿不是门禁没跑,而是断言与实现同时只覆盖了钳制的一半。

> **教训**:第一条:**「加一层校验」和「删一行读取」在 diff 里只差一行,而它们在语义上差的是整个包。**
> 改写一个读取点时,替换的是**赋值**那一句,不是**读取**那一句 —— 把 `ReadFloat` 写进钳制块里当参数,
> 而不是把钳制块套在 `ReadFloat` 外面,就会得到这个形状。
> 第二条:**断言必须能失败。** 只断言「输出被夹到边界内」的用例,对「输入根本没读」也是绿的;
> 给接收对象写一个落在钳制恒等区间的有限哨兵,才能把「读进来了」变成断言成立的唯一解释。
> 第三条:**`noexcept` 链上的不确定值不是「小问题」,它是把控制权交给了内存布局。**
> 这个值一路走到 `ForceActorValue` 与 `pActor->Kill()`,中间没有任何一处能发现它是垃圾。
## 70. 2026-10-01 场次:二十处「对端说了算」的浮点直接进了引擎 —— 判据是「这个 float 是有限数吗,而它接下来会被谁用掉」

> **锚点**:本节的修复落在 `Code/encoding/WireLimits.h` 与 `Code/encoding/Messages/` 下的六个 `.cpp`
> (`AddTargetRequest.cpp`、`NotifyAddTarget.cpp`、`SyncExperienceRequest.cpp`、`NotifySyncExperience.cpp`、
> `ProjectileLaunchRequest.cpp`、`NotifyProjectileLaunch.cpp`),交付形式是仓库内的直接提交
> (不是插件补丁 —— 这些文件属于主仓,CI 直接编译)。

### 70.1 缺陷:读侧对「非有限数」没有任何处置,而这批字段全都要交给引擎

| 位置 | 作用 |
| --- | --- |
| `Code/encoding/Messages/AddTargetRequest.cpp:23` / `NotifyAddTarget.cpp:23`(修复前) | `Magnitude` 裸读,进 `MagicTarget::AddTargetData.fMagnitude` |
| `Code/encoding/Messages/SyncExperienceRequest.cpp:12` / `NotifySyncExperience.cpp:12`(修复前) | `Experience` 裸读,直接进 `PlayerCharacter::AddSkillExperience` |
| `Code/encoding/Messages/ProjectileLaunchRequest.cpp:44-46`/`:52-54`/`:62-63`,`NotifyProjectileLaunch.cpp` 同(修复前) | 八个浮点裸读,填 `Projectile::LaunchData` |
| `Code/encoding/WireLimits.h:74-77`(修复后) | 新增 `inline float ClampFinite(const float aValue) noexcept` |

六个文件在修复前都是裸读,形状完全一样:

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

`NaN` 不是「很大的数」,它是「没有数」:它参与任何算术之后仍然是 `NaN`,参与任何比较永远为假,
所以它落到哪个引擎字段上,哪个字段就再也不可比较、不可夹紧、不可存储;`±Inf` 是同一件事的另一半。
这三批字段各有一个落点,而**没有一个落点在读侧做过检查**:

1. `Magnitude` → `Code/client/Services/Generic/MagicService.cpp:450 data.fMagnitude = acMessage.Magnitude;`
   → `pActor->magicTarget.AddTarget(data, acMessage.ApplyHealPerkBonus, acMessage.ApplyStaminaPerkBonus);`(`:470`)。
2. `Experience` → `Code/client/Services/Generic/CharacterService.cpp:1094-1104 OnNotifySyncExperience`
   → `pPlayer->AddSkillExperience(PlayerCharacter::LastUsedCombatSkill, acMessage.Experience);`
   → `Code/client/Games/Skyrim/PlayerCharacter.cpp:74-87` 把它原样交给 `RealAddSkillExperience`,
   再回读 `float deltaExperience = newExperience - oldExperience;` —— 两个 `NaN` 相减还是 `NaN`。
3. 那八个浮点 → `Projectile::LaunchData`(`Code/client/Games/Skyrim/Projectiles/Projectile.h:18-50`,
   `static_assert(sizeof(Projectile::LaunchData) == 0xA8);` 在 `:65`),`Projectile::Launch`
   (`Code/client/Games/Skyrim/Projectiles/Projectile.cpp:37`)把 `fPower` 写进弹丸实体,其余进弹道解算。

同族的既有先例**全仓只有两处**,而且都在 `Code/encoding/` 里:`RequestHealthChangeBroadcast.cpp:20` 的
`std::isfinite` 夹紧(`kMaxHealthDelta = 100000.f`)与 `Structs/TimeModel.cpp:11-15` 的 `ClampFloat`。
`Code/encoding/WireLimits.h:23-26` 的头部注释本来就写着「读侧钳制而不是拒绝」该怎么落。

### 70.2 判据:非有限值**替换**而不是夹进区间,而有限的合法值必须原样通过

判据分两半,缺任何一半都会写出一个错的修复:

- **非有限值没有区间可言。** 没有任何区间能让 `NaN` 变得有意义,而且这批字段里没有哪一个像血量增量那样
  带「正负即判决」的语义(那个字段的正负决定目标死不死,所以它必须夹到 `±kMaxHealthDelta`),
  所以这里取**替换**为 `0.f`。
- **有限值不能动。** 发送方真的能产生 `99999.f` 的落点与 `-1234.5f` 的角度,把有限值夹进某个「合理范围」
  等于每一发弹道都被悄悄挪位 —— 那是把稳定性修复变成新的失步。

```cpp
// Code/encoding/WireLimits.h:65-77(修复后)
// A float that arrives over the wire and is handed to the engine. A NaN or an infinity
// is not a large number but the absence of one: it survives every arithmetic operation
// it takes part in, so a peer that sends one turns whatever it lands in into a value
// nobody can compare, clamp or store. The engine checks nothing, and so did the readers
// that filled these fields.
//
// A non finite value is replaced rather than clamped into a range: no range makes a NaN
// meaningful, and unlike a health delta none of these fields carries a sign dependent
// decision that a bound would have to respect.
inline float ClampFinite(const float aValue) noexcept
{
    return std::isfinite(aValue) ? aValue : 0.f;
}
```

用例同时钉住这两半:`Code/tests/encoding.cpp` 新增
`TEST_CASE("A non finite wire float is replaced where it is read", "[encoding.finite_floats]")`,
先直接断言 `ClampFinite` 对 `1.5f`/`-1.5f`/`0.f` 恒等、对 `NaN`/`+Inf`/`-Inf` 给 `0.f`,
再对六个消息各做一次整包往返,最后用一条 `REQUIRE(received == sent)` 把「合法弹道原样通过」也钉住。
**每个接收对象都先写哨兵 `1.f`** —— 理由与 §69.2 相同:没有哨兵,「读都没读」和「读到了 0」在断言上无法区分。

### 70.3 可达性:三个家族的发送侧都只由「本地真实事件」驱动,但读侧从不检查发送方是谁

`Magnitude` 的发送侧是 `Code/client/Services/Generic/MagicService.cpp:328 request.Magnitude = acEvent.Magnitude;`,
服务端 `Code/server/Services/MagicService.cpp:62 notify.Magnitude = message.Magnitude;` 是纯转发
—— `Code/server/Services/MagicService.cpp` 的四个 handler(`OnSpellCastRequest:24-38`、
`OnInterruptCastRequest:40-51`、`OnAddTargetRequest:53-70`、`OnRemoveSpellRequest:72-85`)**没有任何 ownership 判定**。
`Experience` 的发送侧 `RunExperienceUpdates` 只在自己队里发(`if (!World::Get().GetPartyService().IsInParty()) return;`),
服务端 `Code/server/Services/CharacterService.cpp:548-558 OnSyncExperienceRequest` 按队伍转发;
弹道由 `Code/server/Services/CombatService.cpp:15-56` 转发给 `SendToPlayersInRange`,**字段逐个原样拷贝,零校验**。
⇒ 一个自制的客户端不需要攻破任何东西:它只要在包里把这二十个字段之一写成 `NaN`,接收方就会把它交给引擎。
这与 §59 的判据同源(「一个增量字段既是值又是判决」),只是这里的后果不是秒杀,而是**让引擎的某个数永久失效**。

### 70.4 修法:在既有落点加一个替换函数,六个文件各改第 2 行 include 与 N 处读取

`WireLimits.h` 是 `Code/encoding` 里既有的「wire 侧钳制」落点(22 个消息 `.cpp` 第 2 行 `#include <WireLimits.h>`),
新增函数放在 `Clamp` 之后、`} // namespace WireLimits` 之前;六个 `.cpp` 沿用同一 include 风格,二十处读取改成:

```cpp
// Code/encoding/Messages/ProjectileLaunchRequest.cpp:45-47(修复后,其余 18 处同构)
OriginX = WireLimits::ClampFinite(Serialization::ReadFloat(aReader));
OriginY = WireLimits::ClampFinite(Serialization::ReadFloat(aReader));
OriginZ = WireLimits::ClampFinite(Serialization::ReadFloat(aReader));
```

`CastingSource` 与 `Area` 仍是 `Serialization::ReadVarInt(aReader) & 0xFFFFFFFF`(整数,不是 float),
本批未动 —— 它们的缺陷是另一类(见 §66 的判据),不在这一节的范围内。

### 70.5 复核命令(照抄)

```powershell
git grep -n "ClampFinite" Code/encoding/WireLimits.h Code/encoding/Messages/
git grep -c "ReadFloat" Code/encoding/Messages/
git grep -n "ReadFloat" Code/encoding/Messages/ | Select-String -NotMatch "ClampFinite"
git grep -n "finite_floats" Code/tests/encoding.cpp
git diff --stat
python Tools/Scripts/check_docs.py
```

复核结果:`ClampFinite` 命中 `WireLimits.h:74`(定义)与六个消息文件共 20 处读取;
`ReadFloat` 在 `Code/encoding/Messages/` 上共 28 行,其中 20 行是本批、4 行是 actor value 一族
(`NotifyActorValueChanges.cpp:40`/`RequestActorValueChanges.cpp:40`/`NotifyActorMaxValueChanges.cpp:40`/
`RequestActorMaxValueChanges.cpp:40`,走 `ActorValues::IsValidActorValue` 的 `std::isfinite`)、
2 行是血量一族(`RequestHealthChangeBroadcast.cpp:16`/`NotifyHealthChangeBroadcast.cpp:16`,§69 的 `kMaxHealthDelta` 夹紧)、
2 行是 `Percentage`(`NotifyPlayerHealthUpdate.cpp:15`/`RequestPlayerHealthUpdate.cpp:13`,消费侧
`Code/client/Services/Generic/OverlayService.cpp:543` 的 `acMessage.Percentage >= 0.f ? … : 0.f` 把 `NaN` 变成 `0.f`);
过滤掉 `ClampFinite` 之后**没有一条裸读属于这六个文件**。`check_docs.py` → `DOC CHECK OK`。
C++ 测试由 CI 执行(`windows.yml:585-638` 的 `Run the encoding tests` 要求退出码 0 **且**输出里含 `All tests passed`)。

**验证**:六处修改前的内容与修改后的内容都逐字核对过;新增用例的哨兵 `1.f` 落在 `ClampFinite` 的恒等区间内,
所以删掉任意一处 `ClampFinite` 都会让对应的 `== 0.f` 断言失败(该位置在 `ClampFinite` 之前是裸读,
删掉包装后 `NaN` 会原样留下,断言同样失败)。`REQUIRE(received == sent)` 则保证修复没有把有限值也改掉。

> **教训**:第一条:**「引擎会检查」和「读侧检查过」是两件不同的事,而 diff 里只看得到后者。**
> 这二十处读取与它们各自的引擎落点之间,隔着一整个 `Code/client/Services` —— 判据必须落在**读取那一行**上,
> 而不是落在「下游大概会判空」上。
> 第二条:**「夹紧」和「替换」不是同一种修复,选择依据是这个值的语义,不是这个值的类型。**
> 血量增量带「正负即判决」,所以它必须夹到边界;落点、角度、强度、经验都不带这种语义,
> 所以对它们来说任何区间都是凭空发明的 —— 而凭空发明的区间会改掉合法数据。
> 第三条:**先例只有一个的时候,它就不是先例,是例外。**
> 这一族此前只有血量与 `TimeModel` 两处钳制,而 `ReadFloat` 在 `Code/encoding/Messages/` 上有 28 处 ——
> 把「有一个地方做了」当成「这一类都做过了」,就是这一节二十处缺陷能一起活到现在的原因。
## 71. 2026-10-04 场次:五处「对端选定的 id 与索引」都没有被判定过有效性 —— 判据是「这个值有效吗,而它接下来被用来做什么」

> **锚点**:本节的修复落在 `Code/client/Services/Generic/MagicService.cpp`、`Code/client/Services/Generic/CombatService.cpp`、
> `Code/encoding/Messages/TeleportRequest.h`、`Code/client/Services/Generic/OverlayClient.cpp` 与 `Code/tests/encoding.cpp`,
> 交付形式是仓库内的直接提交(不是插件补丁 —— 这四个文件属于主仓,CI 直接编译)。

### 71.1 缺陷:五个使用点各自假设了「送来的东西一定有效」,而同一个文件里就写着它不一定的理由

| 位置 | 送来的东西 | 使用点假设了 |
| --- | --- | --- |
| `Code/client/Services/Generic/MagicService.cpp:149` | 远程 id 解析出的 actor | `Cast<Actor>` 一定有值 |
| `Code/client/Services/Generic/MagicService.cpp:159-170` | 上一步的 actor 的施法槽 | `GenerateMagicCasters` 之后 `casters[0]` 一定有值 |
| `Code/client/Services/Generic/MagicService.cpp:172-180` | `CastingSource` | 它非负 |
| `Code/client/Services/Generic/CombatService.cpp:125-152` | 弹丸基类与施法源 | 前者有值、后者落在 `[0,4)` |
| `Code/encoding/Messages/TeleportRequest.h:23` | 玩家 id | 它装得进 16 位 |

**「一定有值」这件事,本仓自己在两个地方写清楚了它不成立。**

```cpp
// Code/client/Games/Skyrim/RTTI.h:17-27
template <class T, class U>
T* Cast(U* apPtr)
{
    const void* pBaseRtti = internal::RttiLocator<std::remove_cv_t<U>>::Get();
    const void* pTargetRtti = internal::RttiLocator<std::remove_cv_t<T>>::Get();

    if (pBaseRtti == nullptr || pTargetRtti == nullptr || apPtr == nullptr)
        return nullptr;

    return reinterpret_cast<T*>(internal::DynamicCast.Get()((void*)apPtr, 0, pBaseRtti, pTargetRtti, 0));
}
```

```cpp
// Code/client/Games/Skyrim/Actor.cpp:915-924
void Actor::GenerateMagicCasters() noexcept
{
    using CS = MagicSystem::CastingSource;

    for (int i = 0; i < 4; i++)
    {
        if (casters[i] == nullptr)
            casters[i] = Cast<ActorMagicCaster>(GetMagicCaster(static_cast<CS>(i)));
    }
}
```

第一个是「RTTI 没有映射就返回空」,第二个是「引擎不给就仍然空」—— 而 `casters` 与 `magicItems` 都是定长四元数组
(`Code/client/Games/Skyrim/Actor.h:359-360` 的 `ActorMagicCaster* casters[4];` / `MagicItem* magicItems[4];`)。
于是 `Code/client/Services/Generic/MagicService.cpp:149-170`(修复前)是:

```cpp
// Code/client/Services/Generic/MagicService.cpp:149-170(修复前,OnNotifySpellCast)
Actor* pActor = Cast<Actor>(pForm);
pActor->GenerateMagicCasters();

// Only left hand casters need dual casting (?)
pActor->casters[CS::LEFT_HAND]->SetDualCasting(acMessage.IsDualCasting);

if (acMessage.CastingSource >= 4)
{
    spdlog::warn("{}: could not find casting source {}", __FUNCTION__, acMessage.CastingSource);
    return;
}

pSpell = pActor->magicItems[acMessage.CastingSource];
```

同一个函数在 16 行之后又给了自己一个反例 —— 它**知道** `CastingSource` 可能越界,只是只判了上界。
而 `CastingSource` 是**带符号**的:`Code/encoding/Messages/NotifySpellCast.h:22 int32_t CastingSource;`、
`NotifyInterruptCast.h:20`、`NotifyProjectileLaunch.h:42`、`ProjectileLaunchRequest.h:45` 四处同型。
读侧 `NotifySpellCast.cpp:18 CastingSource = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;` 保住的正是低 32 位,
所以一个负值能原样穿过整条链:`Code/server/Services/MagicService.cpp:31 notify.CastingSource = message.CastingSource;`
是逐字转发,全仓没有任何一处判定过 `>= 0`。`-1` 于是索引到 `magicItems[-1]`,即数组前面那四个字节。

`Code/client/Services/Generic/MagicService.cpp:300-308`(修复前,`OnNotifyInterruptCast`)是同一族的第二处,形状完全相同:

```cpp
// Code/client/Services/Generic/MagicService.cpp:300-308(修复前,OnNotifyInterruptCast)
Actor* pActor = Cast<Actor>(pForm);
pActor->GenerateMagicCasters();
```

第三、四处在 `Code/client/Services/Generic/CombatService.cpp:125-152`(修复前,`OnNotifyProjectileLaunch`):

```cpp
// Code/client/Services/Generic/CombatService.cpp:125 / :140-152(修复前)
launchData.pProjectileBase = TESForm::GetById(cProjectileBaseId);

launchData.eCastingSource = (MagicSystem::CastingSource)acMessage.CastingSource;
```

第五处不在 `Code/client/Services/` 里,它是一条字段宽度与它要装的东西不匹配的链:

```cpp
// Code/encoding/Messages/TeleportRequest.h:20 / TeleportRequest.cpp:12(修复前)
uint16_t PlayerId{};                                        // 字段 16 位
PlayerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF; // 读侧按 32 位掩码
```

### 71.2 判据:被调用方写明的失效条件,在调用点被判定了吗

这一节没有新的机制,只有一条判据:**凡是「由对端选定的 id 或索引」,使用之前必须判定它指向的东西真的存在。**
四条失效条件是本仓自己写下来的,不需要猜:

1. `Cast<T>` 在 `pBaseRtti`/`pTargetRtti`/`apPtr` 任一为空时返回空(`Code/client/Games/Skyrim/RTTI.h:23-24`);
2. `GenerateMagicCasters` 只写「引擎返回了非空」的槽(`Code/client/Games/Skyrim/Actor.cpp:921-922`);
3. `CastingSource` 的声明类型是 `int32_t`,而 `magicItems` 是四元数组 —— 有效区间是 `[0, 4)`;
4. 玩家 id 由 `Code/server/Game/Player.cpp:1-30` 的 `static constinit std::atomic<uint32_t> s_counter{1};` 单调发号,
   **从 1 开始、不回收、不回绕**,所以它没有 16 位的上界。

**这一族在本仓是「多数守住了、少数没守住」,所以判据有现成的对照组。**

```cpp
// Code/client/Games/Skyrim/Projectiles/Projectile.cpp:56-68(同族已守住的写法)
if (arData.pShooter)
{
    Actor* pActor = Cast<Actor>(arData.pShooter);
    if (pActor)
    {
        ActorExtension* pExtendedActor = pActor->GetExtension();
        if (pExtendedActor->IsRemote())
        {
            apThis->handle.iBits = 0;
            return apThis;
        }
    }
}
```

`Code/client/Services/Generic/CombatService.cpp:112-116` 对 `Cast<TESObjectCELL>` 判空、
`Code/client/Services/Generic/OverlayService.cpp:524-529` 与 `Code/client/Services/Generic/CommandService.cpp:62-67` 对 `Cast<TESWorldSpace>` 判空 ——
也就是说这五个使用点不是「仓库里没人这么干」,而是**漏掉了**。

### 71.3 可达性:五处都不需要「恶意客户端」,只需要一个被改过的客户端

- **远程施法/打断施法**:`Code/encoding/Messages/SpellCastRequest.h:22` 与 `InterruptCastRequest.h:20` 的 `CastingSource` 都是
  `int32_t`,服务端 `Code/server/Services/MagicService.cpp:24-51` 只做逐字转发(`notify.CastingSource = message.CastingSource;`)
  再 `SendToPlayersInRange`。任何一个改过的客户端送 `-1` 或一个不存在的 `CasterId`,收到这个包的每个客户端就走进上面那两段。
- **远程弹道**:`Code/server/Services/CombatService.cpp:15-56` 把 22 个字段逐字转发,`ProjectileBaseID` 与 `CastingSource` 都不例外,
  而 `Code/client/Services/Generic/CombatService.cpp` 是唯一的消费者。
- **覆层传送**:`Code/client/Services/Generic/OverlayClient.cpp:167-173` 是 `TeleportRequest` 的**唯一发送方**,
  它的输入来自 CEF 覆层的 `player.id`;`Code/encoding/Messages/NotifyPartyInfo.h:25 Vector<uint32_t> PlayerIds{};` 送到覆层的本来就是 32 位
  (`Code/client/Services/Generic/PartyService.cpp:126`/`:168` 取,`:137-141` 转发),`OverlayClient.cpp:170` 却把它赋给一个 `uint16_t`。
  服务端 `Code/server/Services/OverlayService.cpp:90` 用这个值 `GetById` —— 第 65536 个玩家之后,按下的按钮指向**另一个人**。

五条里没有一条能打崩服务器:**前四条是客户端侧的无效解引用与越界索引,第五条是一次「送错人」。**
这正是它们能活到现在的原因 —— 它们不在崩溃报告里,只在「偶尔怪怪的」里。

### 71.4 修法:把「有效吗」这一句判断放在使用点之前

1. `Code/client/Services/Generic/MagicService.cpp:151-157` 与 `:302-306`:`Cast<Actor>` 之后加
   `if (!pActor) { spdlog::warn(...); return; }`。
2. 同文件 `:161-167`:再加一道 `if (!pActor->casters[CS::LEFT_HAND])`,位置在 `GenerateMagicCasters()` 之后、
   `SetDualCasting` 之前。
3. 同文件 `:172` 与 `:282`:`if (acMessage.CastingSource >= 4)` 补成 `if (acMessage.CastingSource < 0 || acMessage.CastingSource >= 4)`。
4. `Code/client/Services/Generic/CombatService.cpp:146-152`:把 C 风格强转换换成同一段范围判定 + `static_cast`;
   `:127-131` 在 `pProjectileBase` 赋值之后判空。
5. `Code/encoding/Messages/TeleportRequest.h:23`:`uint16_t PlayerId{};` → `uint32_t PlayerId{};`;
   `Code/client/Services/Generic/OverlayClient.cpp:170`:`request.PlayerId = static_cast<uint32_t>(aEventArgs->GetInt(0));`。

第 5 条不需要改线格式:写侧一直是 `Serialization::WriteVarInt(aWriter, PlayerId);`(`TeleportRequest.cpp:5`),
读侧一直是 32 位掩码,`& 0xFFFFFFFF` 与 `WriteVarInt` 在 16 位以内的 id 上产生完全相同的字节。
改动只发生在**字段宽度**这一层,所以它对已存在的存档、已连接的小 id 玩家都是透明的。

### 71.5 复核命令(照抄)

```powershell
git grep -n "Cast<Actor>\|GenerateMagicCasters\|casters\[" Code/client/Services/Generic/MagicService.cpp
git grep -n "CastingSource" Code/client/Services/Generic/MagicService.cpp Code/client/Services/Generic/CombatService.cpp
git grep -n "pProjectileBase" Code/client/Services/Generic/CombatService.cpp
git grep -n "uint16_t PlayerId\|uint32_t PlayerId" Code/encoding/Messages/TeleportRequest.h
git grep -n "request.PlayerId" Code/client/Services/Generic/OverlayClient.cpp
git grep -n "teleport_request" Code/tests/encoding.cpp
python Tools/Scripts/check_docs.py
```

复核结果:`MagicService.cpp` 上 `Cast<Actor>` 之后紧跟判空(共两处,`:153` 与 `:302`),`casters[` 的唯一解引用点
`:170` 之前有 `:163` 的判空;`CastingSource` 在本仓客户端侧共有三处范围判定,其中 `:172`/`:282` 是本批补上下界的,
`CombatService.cpp:146` 是第三处;`pProjectileBase` 的赋值 `:125` 与判定 `:127` 相邻;
`TeleportRequest.h` 上 `uint16_t PlayerId` 已无命中,`uint32_t PlayerId` 命中 `:23`。

**验证**:`Code/tests/encoding.cpp` 新增 `TEST_CASE("A teleport request carries the whole player id", "[encoding.teleport_request]")`,
对 `0`/`1`/`0xFFFF`/`0x10000`/`0x10001`/`0xFFFFFFFF` 各做一次整包往返并断言 `received.PlayerId == aPlayerId` 与 `received == sent`;
`0x10000` 与 `0x10001` 正是 16 位字段会塌成同一个值的两个 id —— 把字段改回 `uint16_t` 会让这两条断言失败。
其余四处(两个 Cast 判空、施法槽判空、施法源范围)在 `Code/client/Services/Generic/` 里,判定的是运行期才可能为空的引擎指针,**没有能在无游戏进程的单元测试里造出来的前置条件**,
所以它们的证据是逐字核对 + 同族对照组(`Projectile.cpp:56-68`、`CombatService.cpp:112-116`、`OverlayService.cpp:524-529`),
C++ 编译由 CI 执行(`windows.yml`)。`python Tools/Scripts/check_docs.py` 全绿。

> **教训**:第一条:**「多数地方守住了」不是「这一族守住了」。**
> `Cast<T>` 之后的判空在本仓有几十处,`Projectile.cpp:56-68` 与 `CombatService.cpp:112-116` 都在其中 ——
> 而正是这份密度让漏掉的那几处读起来像是「这里不需要」。
> 第二条:**判据要写在「值被使用的那一行」,不是写在「值被赋值的那一行」。**
> `CastingSource` 的 `>= 4` 判定在修复前就存在,它离使用点只隔八行,却因为漏了下界而完全无效;
> 一个判定了半个区间的检查比没有检查更难发现,因为它会让读者以为这里已经检查过了。
> 第三条:**字段宽度不是实现细节,它是接口的一部分。**
> `TeleportRequest::PlayerId` 的 16 位与它的读侧 32 位掩码放在一起时,缺陷是自证的;
> 而两者分处两个文件、中间隔着一次 CEF 的 `int`,于是它活了很久 —— 直到有人把「id 从哪来」追到发号器。
## 72. 2026-10-05 场次:四个插件的最高频日志点也都在同步刷盘 —— 判据是「这个 logger 让哪一级消息立刻进内核,而它的等级设置开着哪一级」

> **锚点**:本节的修复落在 `plugins/DAVSyncTogether`、`plugins/IEDSyncTogether`、`plugins/TradeTogether`、
> `plugins/OStimTogether` 四个子模块的 `src/main.cpp`,交付形式是四个补丁:
> `Code/plugins/patches/DAVSyncTogether/0002-stop-flushing-every-log-line-to-disk.patch`、
> `Code/plugins/patches/IEDSyncTogether/0001-stop-flushing-every-log-line-to-disk.patch`、
> `Code/plugins/patches/TradeTogether/0001-stop-flushing-every-log-line-to-disk.patch`、
> `Code/plugins/patches/OStimTogether/0005-stop-flushing-every-log-line-to-disk.patch`
> —— 这四个插件都是子模块,CI 的 `git submodule update --init --force --recursive` 会丢弃工作树改动,只有补丁能落地。

### 72.1 缺陷:§67 与 §68 修掉的是两个插件,而同一行配置在另外四个插件里逐字存在

`flush_on(N)` 的参数是**刷盘起点**而不是日志等级(§67.2 已经论证过),所以 `flush_on(trace)` 等价于「每条都刷」。
§67 与 §68 各修了一个插件,但这一行不是那一个插件的写法,而是整层的写法:

| 插件 | 初始化位置 | `set_level` | `flush_on`(修复前) |
| --- | --- | --- | --- |
| `DAVSyncTogether` | `src/main.cpp:9-29` | `trace`(`:27`) | `trace`(`:28`) |
| `IEDSyncTogether` | `src/main.cpp:10-24` | `trace`(`:22`) | `trace`(`:23`) |
| `TradeTogether` | `src/main.cpp:8-23` | `trace`(`:19`) | **`info`**(`:20`) |
| `OStimTogether` | `src/main.cpp:35-52` | `trace`(`:50`) | `trace`(`:51`) |

四个插件的写法里只有一处不同:前三个用 `spdlog::` 的全局函数,`TradeTogether` 用 `logger->` 的成员函数
(`plugins/TradeTogether/src/main.cpp:19-22`),而它的刷盘起点写的是 `info` 而不是 `trace`。
这一处不同不是「它比较轻」:`info` 是这四份代码里最高频的那个等级,`trace` 那一级在实测日志里一条都没有出现过。

**四个插件的日志初始化里都没有 `spdlog::flush_every(...)`。** 参照组是主程序与 §67/§68 已经修好的两个插件:

```cpp
// Code/client/TiltedOnlineApp.cpp:75-80(本仓既有策略)
spdlog::flush_every(std::chrono::seconds(1));
logger->flush_on(spdlog::level::warn);
```

```cpp
// Code/plugins/patches/MorphSyncTogether/0003-...(§68 已交付的同款修法)
-        spdlog::flush_on(spdlog::level::trace);
+        // flush_on(trace) pushed every line to disk before the game thread could
+        spdlog::flush_every(std::chrono::seconds(1));
+        spdlog::flush_on(spdlog::level::warn);
```

### 72.2 判据:刷盘起点是一个数,而等级是另一个数 —— 两个都要读,而且要看它写在谁身上

1. `flush_on(N)` 的语义是「任何 level >= N 的消息,打完立刻进内核」(`spdlog` 的 `flush_on` 就是
   「达到这个等级就 flush」),所以 `flush_on(trace)` 覆盖全部消息,`flush_on(info)` 覆盖 info 及以上。
2. `set_level(L)` 决定哪一级**打得出来**。`set_level(trace)` 意味着这一层里所有的 trace/debug 调用点都是活的,
   而四个插件的源码里确实有这些调用点(`IEDSyncTogether/src/RemoteIEDRenderer.cpp:473`、
   `OStimTogether/src/CoopSessionManager.h:56`、`src/FreeScenePhaseSync.cpp:428`/`:608`、
   `src/EquipmentLock.cpp:531`、`src/NetworkProbe.cpp:504`)。
3. `flush_every` 是**默认 logger** 的全局设置,`flush_on` 是**某一个 logger** 的设置 —— 两者的接收者不同,
   所以 `TradeTogether` 那一份不能照抄前三份:它的 `flush_on` 必须留在 `logger->` 上,而 `flush_every` 必须写在
   `spdlog::set_default_logger(std::move(logger));` **之后**,否则它作用在一个还不是默认 logger 的对象上。

判据因此是一句话:**这个 logger 让哪一级消息立刻进内核,而它的等级设置开着哪一级。**
两个答案都在同一段初始化里,不需要跑起来才知道。

### 72.3 可达性:四个插件的实测日志都很小,而这不是「无害」的证据

本机 10 分钟会话的日志(路径见 72.5)按体量排:

| 日志 | 体量 | 行数 | 状态 |
| --- | --- | --- | --- |
| `AnimSyncTogether.log` | 7345371 B | 43271 | §67 已修 |
| `MorphSyncTogether.log` | 356845 B | 1864 | §68 已修 |
| `DAVSyncTogether.log` | 17981 B | 100 | 本节 |
| `OStimTogether.log` | 7049 B | 44 | 本节 |
| `TradeTogether.log` | 3483 B | 31 | 本节 |
| `IEDSyncTogether.log` | 958 B | 6 | 本节 |

剩下四个的体量比前两个小两到四个数量级 —— **但那一次会话没有触发它们的功能**(没有交易、没有换装、
没有 DAV 变体、没有 IED 重挂),所以这六个数字里只有前两行能当频率证据用。
四个插件的日志调用点计数说明它们一旦被触发就不会安静:
`DAVSyncTogether` 36 处(info 21)、`IEDSyncTogether` 96 处(info 44)、`TradeTogether` 80 处(info 42)、
`OStimTogether` 396 处(info 231,其中 `src/OStimBridge.cpp` 一个文件 74 处)。
**⇒ 判据不能是「实测日志大不大」,只能是「这一行配置让每条消息落盘吗」。**

### 72.4 修法:只改刷盘策略,不动等级、不动日志点

四个补丁都只改一段初始化,`set_level(trace)` 全部保留 —— 抬等级是行为变化(会让 trace 调用点消失),
而这次的缺陷是刷盘策略,两者必须分开改:

```cpp
// plugins/DAVSyncTogether/src/main.cpp:26-32(修复后,另外两个 trace 插件逐字同款)
spdlog::set_default_logger(std::move(log));
spdlog::set_level(spdlog::level::trace);
// flush_on(trace) wrote every line to disk before the emitting thread
// could continue, and the level above enables trace by default.
// Warnings stay immediate; the rest is flushed once a second.
spdlog::flush_every(std::chrono::seconds(1));
spdlog::flush_on(spdlog::level::warn);
```

`TradeTogether` 是唯一一份需要区别对待的(见 72.2 第 3 条):

```cpp
// plugins/TradeTogether/src/main.cpp:19-26(修复后)
logger->set_level(spdlog::level::trace);
// flush_on(info) wrote every line to disk before the emitting thread
// could continue. Warnings stay immediate; the rest is flushed once a
// second, after the logger is registered below.
logger->flush_on(spdlog::level::warn);
logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [TradeTogether] [%l] [%s:%#] %v");
spdlog::set_default_logger(std::move(logger));
spdlog::flush_every(std::chrono::seconds(1));
```

**没有降任何一条日志的等级。** 这四个插件里「每次收发都会打」的 warn 确实存在
(`IEDSyncTogether/src/STRPMAdapter.cpp:224`/`:514`/`:539`、`src/UdpTransport.cpp:772`、
`OStimTogether/src/AddonBridge.cpp:268`/`:277`/`:283`/`:484`/`:494`/`:510`/`:587`/`:598`、
`TradeTogether/src/Trade.cpp:1055`/`:1063`/`:1080`/`:1110`/`:1165`、`DAVSyncTogether/src/DAVNetworkService.cpp:144`/`:156`),
但它们是「对端可控输入触发」的路径 —— 降级会让现场只剩沉默。新策略下它们本来就是立即落盘的,这不需要改。
同理,已经按 `500ms` 节流的热点(`OStimTogether/src/FreeSceneSelfOriginLock.cpp:13` 的 `kLogInterval`、
`IEDSyncTogether/src/SyncService.cpp:181-213` 的 `changed`/`resendDue` 双重门)也不需要动。

### 72.5 复核命令(照抄)

```powershell
python Code\plugins\tools\plugin_patches.py verify
python Code\plugins\tools\plugin_patches.py check
git -C plugins/DAVSyncTogether grep -n "flush_on\|flush_every\|set_level"
git -C plugins/IEDSyncTogether grep -n "flush_on\|flush_every\|set_level"
git -C plugins/TradeTogether grep -n "flush_on\|flush_every\|set_level"
git -C plugins/OStimTogether grep -n "flush_on\|flush_every\|set_level"
python Code\plugins\tools\plugin_snapshot.py verify
python Tools/Scripts/check_docs.py
```

复核结果:`PATCH VERIFY OK` / `17 patch(es) apply to their pinned commits`(13 → 17);`PATCH CHECK OK`;
`flush_on(spdlog::level::trace)` 在四个插件的 `src/main.cpp` 上已无命中,`flush_every(std::chrono::seconds(1))` 命中
`DAVSyncTogether/src/main.cpp:31`、`IEDSyncTogether/src/main.cpp:26`、`OStimTogether/src/main.cpp:54`、
`TradeTogether/src/main.cpp:26`;`flush_on(spdlog::level::warn)` 命中 `:32`/`:27`/`:55`/`:23`(前三个是 `spdlog::`,
`TradeTogether` 是 `logger->`);`plugin_snapshot.py verify` → `SNAPSHOT OK` / `8 plugin(s)`
(`pinned_blob()` 读的是 pin 而不是工作树,所以本节的工作树改动不会让这条门禁变红);`check_docs.py` → `DOC CHECK OK`。

**验证**:四个补丁已应用到各自子模块的工作树,`plugin_patches.py` 的两条独立路径都通过 ——
`is_applied`(`git apply --check --reverse`,查工作树)与 `cmd_verify`(`git apply --check --cached`,查索引 = pin 的内容)。
工作树是 CRLF、pin 是 LF(四个子模块 `core.autocrlf=true`),补丁按 `Code/plugins/patches/**/*.patch text eol=lf`
写成纯 LF,`git diff` 生成的正是 blob 的 LF 形式。
插件 README 不受影响:这四个 README 里唯一提到 `trace` 的地方是 `IEDSyncTogether/README.md:111`
(`If IED resets the same object again later, trace logs show:`),而它引的 `REMOTE IED WATCHDOG recorrected:` 本来就是 trace 级。

> **教训**:第一条:**同一行配置可以横向复制到整个插件层,而每个副本都略有不同。**
> §67 与 §68 修掉的两行,在另外四个插件里逐字存在,只有 `TradeTogether` 把刷盘起点写成了 `info` ——
> 而这一处不同恰好是最重的那个:`trace` 级在实测日志里一条都没出现过,`info` 是唯一一直有输出的等级。
> 修一个插件时顺手看一眼同层的其它插件,比事后逐个发现便宜得多。
> 第二条:**`flush_every` 和 `flush_on` 不是同一个对象的设置,所以顺序和接收者都要对上。**
> 前者是默认 logger 的全局设置,后者是某一个 logger 的设置;把 `flush_every` 写在
> `set_default_logger` 之前,或者把它写成 `logger->flush_every`,都会得到一个静默失效的配置。
> 第三条:**用 pin 的内容覆盖子模块工作树,会静默抹掉已经应用在它上面的补丁 hunk,而 `verify` 不会发现。**
> 本次生成补丁时用 `git show HEAD:src/main.cpp` 覆盖了 `plugins/OStimTogether/src/main.cpp`,
> 把补丁 0003 已应用的 `ClearRemoteState()` 一起抹掉,`plugin_patches.py apply` 才在 0003 上报
> `PATCH FAILED ... error: patch failed: src/AddonBridge.cpp:636`。
> `verify` 用 `--cached` 查的是 pin,所以它当时仍然是全绿的 —— 这道门禁证明不了工作树完好。
> 恢复方式是把该文件 `git checkout --` 回 pin,再把 0003 的 hunk 手工补回去(`:220` 的
> `OStimTogether::PPAIntegration::GetSingleton().Reset();` 之后一行,工作树是 CRLF 所以锚点要带 `\r\n`)。
> **⇒ 覆盖子模块工作树之前,先确认这个文件上有哪些已应用的补丁。**

## 73. 2026-10-06 场次:管理员这道门由对端自称的 id 判定、被拒绝的调用仍然改状态、一个 `assert` 长在收包路径上 —— 判据是「这个值是谁写的,而它接下来决定了什么」

> **锚点**:三处修复,全部在 `Code/` 里(非子模块,直接改源码):
> `Code/server/Services/CommandService.cpp`(管理员判定 + 尊重返回值)、
> `Code/server/Services/CalendarService.cpp`(先判后写 + 倍率的界)、
> `Code/encoding/Messages/NotifySetTimeResult.h`(新增枚举值)、
> `Code/client/Services/Generic/CommandService.cpp`(把两条拒绝分开提示)、
> `Code/encoding/Structs/CachedString.cpp`(去掉 `assert(false)`);
> 测试在 `Code/tests/encoding.cpp`。

### 73.1 缺陷:三处,都在收包路径上

| 位置 | 作用 |
| --- | --- |
| `Code/server/Services/CommandService.cpp:30`(修复前) | 用 `Packet.PlayerId` 判断发送者是不是管理员 |
| `Code/server/Services/CalendarService.cpp:68`(修复前) | 校验之前就写 `TimeScale`,失败分支不撤销 |
| `Code/encoding/Structs/CachedString.cpp:45`(修复前) | 对端可控的 id 落空即 `assert(false)` |

第一处:

```cpp
// Code/server/Services/CommandService.cpp(修复前)
const auto cPlayerId = static_cast<uint32_t>(acMessage.Packet.PlayerId);

for (const auto session : GameServer::Get()->GetAdminSessions())
{
    const auto* pAdmin = PlayerManager::Get()->GetByConnectionId(session);
    if (!pAdmin)
        continue;

    if (pAdmin->GetId() == cPlayerId)   // ← 「某个管理员的 id 等于我自称的 id」
    {
        const auto cHours = static_cast<int>(acMessage.Packet.Hours);
        const auto cMinutes = static_cast<int>(acMessage.Packet.Minutes);

        m_world.GetCalendarService().SetTime(cHours, cMinutes, m_world.GetCalendarService().GetTimeScale());

        response.Result = NotifySetTimeResult::SetTimeResult::kSuccess;
        acMessage.pPlayer->Send(response);

        return;
    }
}
```

第二处:

```cpp
// Code/server/Services/CalendarService.cpp(修复前)
bool CalendarService::SetTime(int aHours, int aMinutes, float aScale) noexcept
{
    m_dateTime.m_timeModel.TimeScale = aScale;      // ← 先写

    if (aHours >= 0 && aHours <= 23 && aMinutes >= 0 && aMinutes <= 59)
    {
        // encode time as skyrim time
        auto minutes = static_cast<float>(aMinutes) * 0.17f;
        minutes = floor(minutes * 100) / 1000;
        m_dateTime.m_timeModel.Time = static_cast<float>(aHours) + minutes;

        SendTimeResync();

        GameServer::Get()->GetWorld().GetScriptService().HandleSetTime(aHours, aMinutes, aScale);
        return true;
    }
    return false;                                   // ← 拒绝了,但上面已经写了
}
```

第三处:

```cpp
// Code/encoding/Structs/CachedString.cpp(修复前)
const auto cValue = StringCache::Get()[cId];
if (cValue)
{
    *this = *cValue;
}
else
{
    // If we receive an id but is not in our local cache, then something fucked up
    assert(false);
}
```

### 73.2 判据:授权要问「这次连接是谁」,写入要在校验之后,断言不能长在对端输入上

1. **`Packet.PlayerId` 是发送者自己填的。** 客户端在 `Code/client/Services/Generic/OverlayClient.cpp:163`
   把 `m_transport.GetLocalPlayerId()` 写进 `SetTimeCommandEvent`,
   `Code/client/Services/Generic/CommandService.cpp:35` 把它原样上线(`request.PlayerId = acEvent.PlayerId;`)。
   而玩家 id 是**公开**的:`NotifyPlayerJoined` 会把每个玩家的 id 广播给所有人。
   于是这个判定等价于「我知道某个管理员的 id」。
2. **同一个 handler 里就有对照组。** 下面那个队长分支用的是真指针:
   `pPartyService->IsPlayerLeader(acMessage.pPlayer)`,
   而 `Code/server/Services/PartyService.cpp:50` 是从队伍表里读 `LeaderPlayerId == apPlayer->GetId()`。
   一个 handler 里两种判定,一种用真身份、一种用对端字段 —— 这个不一致本身就是判据。
3. **能回答「这次连接是谁」的只有 `acMessage.pPlayer`。** 它由 `Code/server/GameServer.cpp:254-279` 的
   handler 生成器构造:先 `GetByConnectionId(aConnectionId)` 判空并 `Kick`,
   再 `trigger(PacketEvent<T>(pRealMessage.get(), pPlayer))` —— 所以 `pPlayer` 非空,
   且它的 `GetConnectionId()` 就是这次连接,不是包里的任何字段。
4. **`m_adminSessions` 存的本来就是 connection id。** `Code/server/GameServer.h:124` 是
   `TiltedPhoques::Set<ConnectionId_t> m_adminSessions;`,
   `:92 AddAdminSession(ConnectionId_t)`、`:94 RemoveAdminSession(ConnectionId_t)`,写入点是
   `Code/server/GameServer.cpp:909`(`m_adminSessions.insert(aConnectionId)`,管理员密码通过时)与
   `:429`/`:440`(`addadmin` 控制台命令),唯一删除点是 `:612`(断线)。
   **这个集合从头到尾就没有存过 `PlayerId`** —— 旧代码是查完它之后再去比另一个空间里的数。
5. **「拒绝」必须发生在任何写入之前。** `TimeScale` 是整个世界的时间倍率:
   `Code/common/DateTime.cpp:35` 的 `GetDeltaTime` 拿它乘每帧毫秒数,`Update` 再把它累加进 `m_timeModel.Time`,
   最后 `static_cast<uint32_t>(m_timeModel.Time / 24.f)` 转成天数。
   「时/分不合法」这条拒绝路径却把倍率改掉了,而且**绕过了这个字段在别处的两道界**:
   `Code/server/Services/CalendarService.cpp:148`(`SetTimeScale`)要求 `aScale >= 0.f && aScale <= 1000.f`,
   `Code/encoding/Structs/TimeModel.cpp:47`(`ClampFloat(TimeScale, kMaxTimeScale)`)同样要求 `0..1000`。
   只有这一处是裸写。
6. **`aScale` 可以是非有限值。** `NaN` 参与算术后仍是 `NaN`,夹进任何区间都还是 `NaN`,
   所以判据不是「在不在范围内」而是「这是不是一个数」—— 与 `WireLimits::ClampFinite` 同一条:
   非有限值**替换**而不是夹取。`NaN` 一路进 `DateTime::Update`,落在 `static_cast<uint32_t>(NaN / 24.f)` 上,
   而把 `NaN` 转成无符号整数是**未定义行为**。
7. **两个调用点都不看返回值。** `Code/server/Services/CommandService.cpp:60` 与 `:76`(修复前)都是
   「调 `SetTime` 然后无条件 `response.Result = kSuccess`」——
   于是「参数不合法」在协议层与「成功」不可区分,客户端只能什么都不显示。
   对照组:`Code/server/GameServer.cpp:379-389` 的控制台 `SetTime` 命令**看了返回值**并打
   `out->error("Hour must be between 0-23 and minute must be between 0-59")`;
   `Code/server/Scripting/GameServer_Bindings.cpp:35-37` 的 Lua 绑定**先夹再调**。
8. **`assert` 的活性要按构建模式读。** 全仓没有 `NDEBUG`(`git grep -rn NDEBUG` 零命中),
   `xmake.lua:27` 只有 `mode.debug`/`mode.releasedbg`/`mode.release`,
   而 `.github/workflows/windows.yml:11-14` 的 `build-mode` 默认值是 `releasedbg`
   —— **发布流水线用的那个模式里断言是活的**。
9. **这条路径整条是 `noexcept`。** `DeserializeRaw` 是 `noexcept` 虚函数,
   所以「抛出去让人接」不是一个选项:`assert` 失败 = `abort` = 整个进程。
10. **它是活流量,不是死代码。** `CachedString` 是 `ActionEvent::EventName` / `TargetEventName`
    (`Code/encoding/Structs/ActionEvent.h:19-20`),而 `ActionEvent::ApplyDifferential` 由
    `Code/encoding/Structs/ReferenceUpdate.cpp:43 Actions[i].ApplyDifferential(aReader);` 调用,
    `ReferenceUpdate` 又装在 `ClientReferencesMoveRequest` 的 `Map<uint32_t, ReferenceUpdate> Updates` 里
    —— 每帧都在跑。

**判据可机械执行**:

| 形状 | 判定 |
| --- | --- |
| 授权判定读的是包里的字段,而不是 `acMessage.pPlayer` | 缺陷 |
| 同一个 handler 里另一个分支用的是真指针 | 上面那条的判据 |
| 成员赋值出现在参数校验之前,失败分支不撤销 | 缺陷 |
| `assert(` 的实参来自线上解析结果 | 缺陷 |
| 同一字段在别处有界、只有这一处裸写 | 上面那条的判据 |

### 73.3 可达性

第一处:**任何玩家**,在聊天框打 `/settime 12 00`。
`Code/skyrim_ui/src/app/services/chat/commands.ts:20-44` 的 UI 侧只校验「两个参数」与「0-23 / 0-59」,
不校验权限 —— 权限完全由服务端这道门决定。

第二处:**管理员**(或私服队长)打 `/settime 99 99`。
`SetTimeCommandRequest::Hours`/`Minutes` 是 `uint8_t`,所以 `99` 原样到达 `SetTime` 并走失败分支。
注意这**不是**「只有管理员能触发的缺陷」:被拒绝的那一次已经把倍率改成了 `aScale`,
而 `aScale` 来自 `m_world.GetCalendarService().GetTimeScale()` —— 也就是说,第二次非法调用会把
「上一次非法调用写进去的值」再写一遍。真正的后果是**倍率与配置值永久不一致**,且没有任何日志。

第三处:**任何已认证玩家**,发一个 `ClientReferencesMoveRequest`,其中某个 `ActionEvent` 的 `EventName`
用一个**服务端缓存里不存在的 id**(字段 `cHasId` 写 `true`,后面跟任意 `VarInt`)。
服务端 `releasedbg` 构建下 `abort`。

### 73.4 修法

一、管理员判定改成问这次连接本身:

```cpp
// Code/server/Services/CommandService.cpp(修复后)
if (GameServer::Get()->GetAdminSessions().contains(acMessage.pPlayer->GetConnectionId()))
{
    const auto cHours = static_cast<int>(acMessage.Packet.Hours);
    const auto cMinutes = static_cast<int>(acMessage.Packet.Minutes);

    // SetTime refuses hours, minutes and a scale it cannot apply, and the reply
    // has to say which of the two happened: answering success for a call that
    // changed nothing left the player waiting for a calendar resync that was
    // never coming.
    if (m_world.GetCalendarService().SetTime(cHours, cMinutes, m_world.GetCalendarService().GetTimeScale()))
        response.Result = NotifySetTimeResult::SetTimeResult::kSuccess;
    else
        response.Result = NotifySetTimeResult::SetTimeResult::kInvalidTime;

    acMessage.pPlayer->Send(response);

    return;
}
```

整个 `for` 循环随之消失 —— 不再需要「遍历 + 判空 + `continue`」。
`TiltedPhoques::Set` 有 `.contains()`(同族先例 `Code/server/Game/Animation/ActionReplayCache.cpp:66`)。
`Packet.PlayerId` 仍是线上字段,但服务端不再读它(`Code/encoding/Messages/SetTimeCommandRequest.h` 不动,
因为协议只许追加,删字段会改线格式)。

二、`CalendarService::SetTime` 改成先判后写,并把界写全:

```cpp
// Code/server/Services/CalendarService.cpp(修复后)
if (!std::isfinite(aScale) || aScale < 0.f || aScale > TimeModel::kMaxTimeScale)
    return false;

if (aHours < 0 || aHours > 23 || aMinutes < 0 || aMinutes > 59)
    return false;

m_dateTime.m_timeModel.TimeScale = aScale;

// encode time as skyrim time
auto minutes = static_cast<float>(aMinutes) * 0.17f;
minutes = std::floor(minutes * 100) / 1000;
m_dateTime.m_timeModel.Time = static_cast<float>(aHours) + minutes;

SendTimeResync();

GameServer::Get()->GetWorld().GetScriptService().HandleSetTime(aHours, aMinutes, aScale);
return true;
```

界用的是 `TimeModel::kMaxTimeScale` 而不是再写一个字面量 `1000.f`:
这个常量的注释原文就是「the server refuses a time scale above this when one is set by hand」
—— 它本来就是为这一处准备的。顺带把该文件里裸写的 `floor(` / `ceil(` 补上 `std::` 限定,
并显式 `#include <cmath>`(这个头此前是靠传递包含进来的;`Code/server/` 里 `#include <cmath>` 零命中),
与 `Code/server/Services/PluginMessagingService.cpp:12-14` 的「std 头放最后」风格一致。

三、两个调用点尊重返回值,并把「参数不合法」与「没有权限」分成两个结果:

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

**追加在末尾而不是插在中间**:枚举按字节上线(`Code/encoding/Messages/NotifySetTimeResult.cpp:5-6` 写 8 位、
`:14` 读 8 位),而 `Code/encoding/Opcodes.h:57-59` 的注释要求「Appended, never inserted」;
`Code/plugins/tools/check_transport_compat.py` 也只约束 opcode 的相对顺序、不看枚举值。
客户端据此分两条提示:

```cpp
// Code/client/Services/Generic/CommandService.cpp(修复后)
if (acMessage.Result == RT::kNoPermission)
    m_world.GetOverlayService().SendSystemMessage("Only an admin, or the party leader on a private server, can change the time.");
else
    m_world.GetOverlayService().SendSystemMessage("That is not a time. Use /settime HH MM, with hours 0-23 and minutes 0-59.");
```

四、`assert(false)` 换成把名字留空:

```cpp
// Code/encoding/Structs/CachedString.cpp(修复后)
*this = TiltedPhoques::String();
```

空名字不是「静默吞掉」:消费这个字段的 `Code/server/Game/Animation/ActionReplayCache.cpp:66` 与 `:81`
本来就是拿名字去查固定表(`AnimationEventLists::kExitSpecial.contains(...)` / `kIgnore.contains(...)`),
查不到即返回 `false`,空串走的是同一条路。
修掉的是**进程终止**,不是这个字段的语义。

### 73.5 复核命令(照抄)

```powershell
python Tools\Scripts\check_docs.py
python Code\plugins\tools\plugin_patches.py verify
python Code\plugins\tools\plugin_patches.py check
python Code\plugins\tools\check_transport_compat.py
git grep -n "assert(false)" -- Code/encoding/
git grep -n "GetAdminSessions()" -- Code/
git grep -n "SetTimeResult" -- Code/
git grep -n "Packet.PlayerId" -- Code/server/
git grep -n "kMaxTimeScale" -- Code/
```

复核结果:`check_docs.py` → `DOC CHECK OK`;`plugin_patches.py verify` → `PATCH VERIFY OK` /
`17 patch(es) apply to their pinned commits`;`check` → `PATCH CHECK OK`;`check_transport_compat.py` →
`TRANSPORT COMPATIBILITY OK`(`client opcodes: 53 in use` / `server opcodes: 56 in use`);
`assert(false)` 在 `Code/encoding/` 已无命中(2026-10-13 更正:`Code/encoding/StringCache.cpp:78`/`:85`
那两处当时被判为「发送侧与接收侧缓存连续性」的不变量断言而排除在外,这个判断是错的 ——
断言的实参 `StartId` 是对端填的字段,见 §83,现已改成丢弃而不是终止);
`GetAdminSessions()` 只剩定义(`Code/server/GameServer.h:90`)与本节这一处读者;
`SetTimeResult` 的三个值各只有一处生产者/消费者。

**验证**:`Code/tests/encoding.cpp` 新增两个用例:

- `TEST_CASE("A refused set time names which refusal it was", "[encoding.set_time_result]")`(`:1232-1262`)
  先断言三个枚举值的字节依次是 `0` / `1` / `2`(证明 `kInvalidTime` 是追加的),
  再对三者各做一次整包往返并 `REQUIRE(received == sent)`。
- `TEST_CASE("A cached string with an unknown id reads as empty", "[encoding.cached_string]")`(`:1264-1304`)
  先造一个「有 id、但 id 不在缓存里」的包,断言名字变成空串而不是进程死;
  再往缓存里放一条真实字符串,断言同一个字段仍然解析得出内容 —— 证明兜底没有把命中路径一起吃掉。

`Code/server/Services/CommandService.cpp` 与 `Code/server/Services/CalendarService.cpp` 是服务端 C++,
本机不编译(§0/§1:Windows/MSVC 只由 GitHub CI 验证)。

> **教训**:第一条:**一个字段只要发送者能自己填,它就不能用来回答「你是谁」。**
> `Packet.PlayerId` 的语义是「我(客户端)的 id」,不是「服务端认为我是谁」;
> 两者在正常客户端上恰好相等,而这个巧合让判定看起来是对的 —— 直到有人把它当成身份用。
> 第二条:**「拒绝」必须发生在任何写入之前。**
> `SetTime` 的失败分支只写了 `return false`,而上面那行赋值照样生效;
> 于是「失败」这个词只是返回值的样子,调用方看见它、状态却已经变了。
> 判据是顺序,不是有没有校验:这个函数在修复前**有**校验,只是校验晚了一行。
> 第三条:**`assert` 是给「代码自己错了」用的,不是给「对端不守约」用的。**
> 对一个可以被任何对端构造出来的输入断言,等于把进程的存活交给对端;
> 而 `releasedbg` 是发布流水线的默认模式,`noexcept` 又拿走了「抛出去让人接」这条退路。

## 74. 2026-10-07 场次:插件层一次节点变化起两个线程、三张只增不减的对端可控缓存 —— 判据是「这个键是谁选的,而它什么时候被删掉」

> **锚点**:两处修复,都在插件层,交付方式是补丁(插件是子模块,本仓不拥有、也不能 push):
> `Code/plugins/patches/OStimTogether/0006-coalesce-the-addon-state-reapply-and-bound-the-remote-caches.patch`
> 改 `plugins/OStimTogether/src/AddonBridge.cpp` 与 `src/AddonBridge.h`,
> `Code/plugins/patches/OStimTogether/0007-bound-the-resolver-caches.patch`
> 改 `plugins/OStimTogether/src/ActorResolver.h` 与 `src/ActorResolverSTRPM.cpp`;
> 两者由 `Code/plugins/tools/plugin_patches.py apply` 在 CI 配置阶段应用。

### 74.1 缺陷:三处,两处没有上限,一处没有合并

| 位置 | 作用 |
| --- | --- |
| `plugins/OStimTogether/src/AddonBridge.cpp:639`(修复前) | 每次调用起 2 个 detached 线程,同一 actor 的重复请求不合并 |
| `plugins/OStimTogether/src/AddonBridge.cpp:531`(修复前) | 覆盖层缓存按「对端选的 channel」增长,没有上限 |
| `plugins/OStimTogether/src/AddonBridge.cpp:617`(修复前) | 物体缓存按「对端选的 channel 与 objectType」增长,没有上限 |
| `plugins/OStimTogether/src/ActorResolverSTRPM.cpp:662`(修复前) | 名字缓存按「对端给的显示名」增长,没有上限 |
| `plugins/OStimTogether/src/ActorResolverSTRPM.cpp:928`(修复前) | 解析缓存按「发送者与线程与下标」增长,没有上限 |

第一处:

```cpp
// plugins/OStimTogether/src/AddonBridge.cpp(修复前)
const auto schedule = [this, actorFormID, reasonCopy](std::chrono::milliseconds delay, const char* phase)
{
    std::thread([...]()
    {
        std::this_thread::sleep_for(delay);
        auto* tasks = SKSE::GetTaskInterface();
        if (!tasks) return;
        tasks->AddTask([...]() { /* 读缓存、重放、打一行 info */ });
    }).detach();
};

schedule(std::chrono::milliseconds(250), "T250");
schedule(std::chrono::milliseconds(1100), "T1100");
```

第二、三处:

```cpp
// plugins/OStimTogether/src/AddonBridge.cpp(修复前)
auto& cached = _remoteOverlays[actor->GetFormID()][cacheKey];   // :531
_remoteObjects[actor->GetFormID()][key] = CachedObjectState{...}; // :617
```

第四、五处:

```cpp
// plugins/OStimTogether/src/ActorResolverSTRPM.cpp(修复前)
_strpmRemoteByName[name] = formID;   // :662
_resolved[key] = resolved.chosen;    // :928
```

### 74.2 判据:对端能选的键必须有上限,而后台线程的数量不能由对端决定

1. **两个调用点都在游戏线程上,所以线程数是同步事件数的两倍。**
   `plugins/OStimTogether/src/AddonStateRepair.cpp:86` 在
   `OStim::ThreadInterface::registerNodeChangedListener` 的回调里对每个非玩家 actor 调一次;
   而节点变化来自 `plugins/OStimNG/skse/src/Core/Thread.cpp:337`
   (`ChangeNode` 末尾的 `EventUtil::invokeListeners(nodeChangedListeners);`)——
   `ChangeNode` 全仓 20 个调用点,一次场景里每切一个动画节点就触发一轮。
   `plugins/OStimTogether/src/OStimBridge.cpp:3941` 是第二个调用点,在停止场景时对同一批 actor 再调一轮。
2. **两个阶段读的是「执行时刻」的缓存,不是「排程时刻」的快照。**
   线程体持 `_stateMutex` 从 `_remoteOverlays`/`_remoteObjects` 现拷两份副本再重放
   (工作树 `AddonBridge.cpp:741-753`),所以一个已经排队的 pair 天然覆盖了它排队期间到达的所有状态;
   再排一对只是把同样的工作重复一遍。
3. **键的形状有上限,数量没有。** `IsSafeAddonToken` 只约束长度 ≤128 与字符集
   (`AddonBridge.cpp:31-43`),`count`/`seq` 也只约束到 64 —— 一个对端仍然可以用任意多个合法 token
   把 `_remoteOverlays`/`_remoteObjects` 撑开;这两张表的唯一清理点是 `ClearRemoteState()`,
   而它只在 `main.cpp:227` 的 `kPreLoadGame` 被调一次。
4. **缓存满 = 一次 miss,不是失败。** `ResolveRemotePlayerByName`(`ActorResolverSTRPM.cpp:789-820`)
   查不到 `_strpmRemoteByName` 就回落到 `ResolveParticipant` 的 ProcessLists 名称扫描;
   `_resolved` 更是没有任何读者。所以给它们设界不会改变行为,只是把内存交还给游戏。

### 74.3 可达性

第一处:**任何进入 OStim 多人场景的玩家**。场景每换一个动画节点,插件为每个非玩家 actor
起 2 个 detached 线程;一次场景几十个节点就是几十到上百个线程的创建与销毁,
全部发生在游戏线程同步调用的回调里。这不是「崩服」级别的缺陷,是**过场与场景交互卡顿**级别的:
线程创建要拿内核锁,而回调本身在游戏线程上。

第二、三处:**同一个对端**,用任意多个 ≤128 字符的合法 channel 名(不限于 `ocum`)反复发包。
每包都会往 `_remoteOverlays` 里插一个新键,会话内没有上限。

第四处:**同一个对端**,每发一包 `HandleSTRPMPacket` 就会写一次 `_strpmRemoteByName`
(`ActorResolverSTRPM.cpp:1166`,只要 `connectionID != 0` 且 proxy 解析成功);
第五处:每个 `sender|thread|index` 三元组一次,而 `sender` 由对端在载荷里自选。

### 74.4 修法

一、重放调度合并成一个可自愈的守卫:

```cpp
// plugins/OStimTogether/src/AddonBridge.cpp(修复后)
constexpr auto kReapplyGuardWindow = std::chrono::seconds(5);
...
{
    const auto now = std::chrono::steady_clock::now();
    std::scoped_lock lock(_stateMutex);

    const auto it = _reapplyPending.find(actorFormID);
    if (it != _reapplyPending.end() && it->second > now) {
        return;
    }

    _reapplyPending[actorFormID] = now + kReapplyGuardWindow;
}
```

守卫带过期时间而不是只用一个 set:`schedule` 的末阶段(`"T1100"`)负责 `_reapplyPending.erase(actorFormID)`,
而它在线程里 `sleep_for` 之后执行 —— 如果这中间游戏读了档,`kPreLoadGame` 会重置十个子系统,
那个线程可能再也跑不到 erase。5 秒比 250ms + 1100ms 长得多,所以正常路径下它永远是末阶段先删、
下一次调用重新排队;只有被读档打断的那一对才会让守卫自然过期。

二、两张远程缓存按 actor 设上限:

```cpp
// plugins/OStimTogether/src/AddonBridge.cpp(修复后)
constexpr std::size_t kMaxRemoteChannelsPerActor = 16;
constexpr std::size_t kMaxRemoteObjectsPerActor = 16;
...
auto& channels = _remoteOverlays[actor->GetFormID()];
if (!channels.contains(cacheKey) && channels.size() >= kMaxRemoteChannelsPerActor) {
    SKSE::log::warn("OSTNET ADDON OVR RX channel limit sender={} actor={:08X} channels={}", sender, actor->GetFormID(), channels.size());
    return;
}
auto& cached = channels[cacheKey];
```

物体分支同形(`kMaxRemoteObjectsPerActor` / `"OSTNET ADDON OBJ RX object limit ..."`)。
先判 `contains` 再判上限:已有的 channel 永远还能更新,被拒的只是**新**键。

三、两个解析缓存设界,且按各自的语义设界:

```cpp
// plugins/OStimTogether/src/ActorResolver.h(修复后)
static constexpr std::size_t kMaxResolvedEntries = 512;
static constexpr std::size_t kMaxRemoteNames = 512;
```

名字缓存满了就跳过这一个名字(`SKSE::log::warn("OSTNET STRPM RESOLVE name cache full ...")`),
因为它是「省一次扫描」的缓存,满了只是多扫一次;解析缓存满了直接 `_resolved.clear()`,
因为它没有任何读者,清空不改变任何行为。

### 74.5 复核命令(照抄)

```powershell
python Code\plugins\tools\plugin_patches.py verify
python Code\plugins\tools\plugin_patches.py check
python Code\plugins\tools\plugin_patches.py apply
python Tools\Scripts\check_docs.py
git -C plugins/OStimTogether grep -n "ScheduleRemoteStateReapply" -- src/
git -C plugins/OStimTogether grep -n "_reapplyPending" -- src/
git -C plugins/OStimTogether grep -n "kMaxRemoteChannelsPerActor" -- src/
git -C plugins/OStimTogether grep -n "kMaxRemoteNames" -- src/
```

复核结果:`plugin_patches.py verify` → `PATCH VERIFY OK` / `19 patch(es) apply to their pinned commits`;
`check` → `PATCH CHECK OK`;`apply` → `patches: 0 applied, 19 already present`;
`check_docs.py` → `DOC CHECK OK`。
`ScheduleRemoteStateReapply` 只剩定义(`AddonBridge.cpp:682`)与两个调用点(`AddonStateRepair.cpp:86`、`OStimBridge.cpp:3941`);
`_reapplyPending` 的 5 处命中就是「一处声明、一处读、一处写、一处删、一处声明于头文件」;
两个上限常量各只有一处定义与一处判定。

**验证**:插件是 CMake 工程、本机不编译(`plugins/OStimNG/skse/AGENTS.md` 明写不要构建 C++/CMake),
所以这一批的证据是补丁门禁 + 逐字命中,不是编译产物。
两处修改都不引入新的 include:`src/PCH.h` 已含 `<chrono>`、`<mutex>`、`<thread>`、`<unordered_map>`、`<unordered_set>`。

> **教训**:第一条:**后台线程的数量不能由对端或事件频率决定。**
> 一次节点变化起两个线程本身不是错,错在「同一个 actor 的请求可以无限叠加」;
> 而合并的正确性来自一个事实:两个阶段读的是执行时刻的缓存,所以排队中的那一对已经覆盖了后来的请求。
> 第二条:**「有校验」不等于「有上限」。**
> `IsSafeAddonToken` 把每个键的形状管得很好,而键的**数量**没人管;
> 判据要问的是「这个键是谁选的,而它什么时候被删掉」,而不是「这个键合法吗」。
> 第三条:**合并用的守卫必须能自己过期。**
> 只用一个 set 的实现依赖「末阶段一定会执行」,而它在一个 `sleep_for` 之后的 detached 线程里 ——
> 中间读一次档,这个假设就没了,守卫会把那个 actor 永久锁死。

## 75. 2026-10-08 场次:三个插件的对端可控缓存只增不减,以及一个「门禁全绿却装不上」的补丁 —— 判据是「这个键是谁选的,而门禁是在哪种工作树上跑的」

> **锚点**:三处修复 + 一处交付修复,都在插件层,交付方式是补丁(插件是子模块,本仓不拥有、也不能 push):
> `Code/plugins/patches/IEDSyncTogether/0002-bound-the-live-peer-table-and-the-decoded-object-list.patch`
> 改 `plugins/IEDSyncTogether/src/STRPMAdapter.h`、`src/STRPMAdapter.cpp`、
> `src/LocalIEDState.h`、`src/LocalIEDState.cpp`(2026-10-10 更正:本节原先记的是
> `src/SyncService.h` 与 `src/SyncService.cpp` —— 那两个文件在本插件的 `CMakeLists.txt`
> 里从来没进过 `SOURCES`、也没有任何 `#include` 链到达,补丁打上去等于没打,见 §78),
> `Code/plugins/patches/MorphSyncTogether/0004-bound-the-remote-snapshot-tables.patch`
> 改 `plugins/MorphSyncTogether/src/MorphSyncService.h`、`src/MorphSyncService.cpp`、
> `src/MorphSyncServiceBodyHair.cpp`、`src/TngSync.h`、`src/TngSync.cpp`,
> `Code/plugins/patches/OStimTogether/0008-bound-the-phase-sync-tables.patch`
> 改 `plugins/OStimTogether/src/FreeScenePhaseSync.h` 与 `src/FreeScenePhaseSync.cpp`;
> 以及重写的 `Code/plugins/patches/OStimTogether/0006-coalesce-the-addon-state-reapply-and-bound-the-remote-caches.patch`。
> 四者由 `Code/plugins/tools/plugin_patches.py apply` 在 CI 配置阶段应用(`.github/workflows/plugins.yml:150`、`windows.yml:208`)。

### 75.1 缺陷:六处,五处是同一类,一处是「补丁自己装不上」

| 位置 | 作用 |
| --- | --- |
| `plugins/IEDSyncTogether/src/STRPMAdapter.h:59`(修复前) | 远端快照表按「对端自选的 connection id」增长,没有上限 |
| `plugins/IEDSyncTogether/src/LocalIEDState.h:33`(修复前) | 解码后的对象表按「对端自选的行数」增长,没有上限 |
| `plugins/MorphSyncTogether/src/MorphSyncService.h:130`/`:131`(修复前) | 两张远端快照表按「对端自选的显示名」增长,没有上限 |
| `plugins/MorphSyncTogether/src/TngSync.h:52`(修复前) | 同上的第三张表,key 是同一个显示名 |
| `plugins/OStimTogether/src/FreeScenePhaseSync.h:292`/`:293`(修复前) | 远端准备表与 ready 去重表按「对端 connection + 对端自选的线程号」增长 |
| `Code/plugins/patches/OStimTogether/0006-…patch`(修复前) | 补丁是拿 pin 当基线生成的,于是把 `0003` 的 hunk 又抄了一遍 |

第一处:

```cpp
// plugins/IEDSyncTogether/src/STRPMAdapter.cpp(修复前)
self->_remoteStates[senderID] = RemoteSnapshot{      // :555,insert-or-default,key 由对端选
```

```cpp
// plugins/IEDSyncTogether/src/LocalIEDState.cpp(修复前)
for (const auto line : Split(payload, '\n')) {       // :80
    ...
    if (line.starts_with("O|")) {
        const auto fields = Split(line.substr(2), '|');   // :97,之后逐行 push_back
```

第二、三处:

```cpp
// plugins/MorphSyncTogether/src/MorphSyncService.cpp(修复前)
auto& snapshot = _remoteSnapshots[sender];        // :667
auto& snapshot = _remotePubicSnapshots[sender];   // :742
```

第四处:

```cpp
// plugins/MorphSyncTogether/src/TngSync.cpp(修复前)
auto& state = _remoteStates[sender];              // :221
```

第五处:

```cpp
// plugins/OStimTogether/src/FreeScenePhaseSync.cpp(修复前)
_remotePreps.push_back(prep);                     // :597-606,只有同 key 覆盖
_lastReadyToken[readyKey] = prep.token;           // :497
```

### 75.2 判据:对端能选的键必须有上限,而「上限」要按每张表自己的语义来设

1. **两张表的 key 都是对端写的,而且都不做数量约束。**
   IED 的 `_remoteStates` 以 `message->sender.connectionID` 为键(`STRPMAdapter.cpp:562`),
   而 `senderID` 直接取自 STRPM 递过来的消息头(`:544`),长度与取值都由对端决定;
   载荷那侧同理 —— `LocalIEDState` 是「一行一个对象」的文本,
   `LocalIEDState.cpp:80-97` 对每一行 `O|` 都解一个对象,**行数没有任何上限**。
   (本节原先记的是 `StrTransport.cpp:302-309` 的 `IEDST|v1|id={}…` ——
   那条路径在本插件的 `CMakeLists.txt` 里根本没进过 `SOURCES`,见 §78。)
   Morph 的 `from=` 来自 `plugins/MorphSyncTogether/src/UdpTransport.cpp:219-222`
   (`message.sender.displayName` 优先,否则 `connection-<id>`),过一道 `SanitizeField`
   (`:26-37`,只把 `|`/CR/LF 换成 `_`、空则 `"Player"`,**没有长度上限**),
   再在 `:234-238` 拼进 `{}from={}|{}`。
2. **上限要问的是「谁把它变小」。** Morph/TNG/OStim 的四张表全仓没有 `erase`,
   唯一清空点是读档:`MorphSyncService.cpp:241-242`(Morph `Reset()`)、
   `TngSync.cpp:126`(TNG `Reset()`)、`FreeScenePhaseSync.cpp:270`/`:457`。
   IED 这张表**有**回收路径(`STRPMAdapter.cpp:475` 的 `_remoteStates.erase(event.connectionID)`,
   `:187`/`:210`/`:484` 的三处 `clear()`),但那条路径由**对端是否被解析成 proxy** 驱动 ——
   键还是对端选的,所以上限仍然要自己设。
   同族的 `_assemblies` 是唯一有 prune 的表 ——
   `MorphSyncService.cpp:356-366` `// Drop incomplete chunk assemblies after 15 seconds.` ——
   而它旁边两张快照表没有,这正是判据的形状:**同一把清理里,谁缺席**。
3. **表越大,游戏线程上的每次 tick 越贵。** Morph 的 `TickOnGameThread` 把两张表的 key 并成
   `std::unordered_set<std::string>` 再逐 key 走 `ResolveRemoteProxyByName`
   (`MorphSyncService.cpp:337-354`),而后者是 ProcessLists 全表扫描 + `EqualsInsensitive` + `GetDistance`。
   IED 侧的形状不同但同源:对象的解码结果在**游戏线程**上被渲染
   (`RemoteIEDRenderer`),对端多写一行,游戏线程就多渲染一个对象。
   所以这不是「内存慢慢涨」,是**每 750ms/1s 一次的 O(表大小 × 演员数)扫描**与逐对象渲染。
4. **满表 = 一次 miss,不是失败。** Morph/IED/TNG 的表是「上一包是什么」的缓存,
   丢一包等于等下一包;`_lastReadyToken` 只是抑制重复发包,清掉最多多发一包。
   所以设界不改变行为。
5. **判据用在交付上:门禁全绿 ≠ 补丁装得上。**
   `plugin_patches.py verify` 用 `git apply --check --cached`(对 **index**,即 pin),
   `check` 用 `git apply --check --reverse`(对 **工作树**,只问「是否已应用」)。
   两条都不看「这个补丁从 pin 出发能不能按顺序落到工作树上」——
   那只有 `apply`(CI 跑的那条)会看。

### 75.3 可达性

第一处(两张表):**同一个对端**,反复用不同的 `sender.connectionID` 发 IED 状态包 ——
每个新 id 都是一条新记录;或者**同一包内**多写若干行 `O|`,每行一个对象。
两条路径都没有上限,而解码与渲染都落在游戏线程上。

第二、三、四处:**同一个对端**,把 `displayName` 换成任意字符串(或干脆留空,让
`connection-<id>` 当 key)。每完成一次分片重组就插一条;`TickOnGameThread` 每秒遍历全表。

第五处:**同一个对端**,每发一包 `PHASE_PREP|` 就写一次 `_remotePreps`
(`FreeScenePhaseSync.cpp:579-606`),`connectionID` 与 `ownerThread` 都由对端选;
`_lastReadyToken` 的 key 是 `fmt::format("{}|{}", prep.ownerConnectionID, prep.ownerThreadID)`(`:492`)。

第五处(交付):**每一次 CI 的插件作业**。`0006` 从 `8214f64d` 起就是坏的,
只是本地工作树一直处在「已应用」状态,`verify` 与 `check` 都看不出来。

### 75.4 修法

一、五张表各自设上限,满了就丢包(而不是丢表)。IED 两张:

```cpp
// plugins/IEDSyncTogether/src/STRPMAdapter.h(修复后)
static constexpr std::size_t kMaxRemoteStates = 64;
```

```cpp
// plugins/IEDSyncTogether/src/STRPMAdapter.cpp(修复后)
if (!self->_remoteStates.contains(senderID) && self->_remoteStates.size() >= kMaxRemoteStates) {
    SKSE::log::warn(
        "STRPM IED state RX dropped: peer table full size={} sender={}",
        self->_remoteStates.size(),
        senderID);
    return;
}
```

同批还有第二处,在解码侧:载荷是「一行一个对象」,行数由对端选,
`src/LocalIEDState.cpp` 逐行 `push_back` 而没有计数上限,所以同一张上限也放在那里 ——
`src/LocalIEDState.h` 加 `static constexpr std::size_t kMaxObjects = 512;`,
`src/LocalIEDState.cpp` 在 `O|` 分支里 `if (state.objects.size() >= LocalIEDState::kMaxObjects) { return std::nullopt; }`,
整包拒收而不是截断(少了一半对象的状态会被渲染成「完整」)。

Morph 两张表同形(`kMaxRemoteSnapshots` / `kMaxRemotePubicSnapshots`,都是 64),
TNG 一张(`kMaxRemoteStates = 64`)。
先判 `contains` 再判上限:**已有的键永远还能更新,被拒的只是新键**。

二、`_remotePreps` 满了丢最旧的一条,`_lastReadyToken` 满了整体清空 —— 两张表的语义不同:

```cpp
// plugins/OStimTogether/src/FreeScenePhaseSync.h(修复后)
static constexpr std::size_t kMaxRemotePreps = 32;
static constexpr std::size_t kMaxReadyTokens = 256;
```

`_remotePreps` 是有序的、按到达时间排队,所以 `_remotePreps.erase(_remotePreps.begin())`
丢掉的正是最不可能还在飞的那一条;`_lastReadyToken` 没有顺序语义,清空只值一包。

三、重写 `0006`:把它从「pin → 最终」改成「`0003` 已应用 → 最终」。
生成方式是**先把已应用的工作树 `git add -A` 进 index,再 `git diff`**
(index→工作树),落盘后再 `git reset` 把 index 还原成 pin,让 `verify` 仍然有意义。
重写后 `0006` 从 6567 B 变成 5780 B,少了 `ClearRemoteState` 的整段 hunk 与
`AddonBridge.h` 的 `@@ -26,6 +26,11 @@` —— **那两段本来就是 `0003` 的内容。**

### 75.5 复核命令(照抄)

```powershell
python Code\plugins\tools\plugin_patches.py verify
python Code\plugins\tools\plugin_patches.py apply
python Code\plugins\tools\plugin_patches.py check
python Code\plugins\tools\plugin_snapshot.py verify
python Code\plugins\tools\check_transport_compat.py
python Tools\Scripts\check_docs.py
git -C plugins/IEDSyncTogether grep -n "kMaxRemoteStates\|kMaxObjects" -- src/
git -C plugins/MorphSyncTogether grep -n "kMaxRemoteSnapshots\|kMaxRemotePubicSnapshots\|kMaxRemoteStates" -- src/
git -C plugins/OStimTogether grep -n "kMaxRemotePreps\|kMaxReadyTokens" -- src/
```

复核结果:`verify` → `PATCH VERIFY OK` / `22 patch(es) apply to their pinned commits`;
**`apply` → `patches: 22 applied, 0 already present`(从干净工作树,这是 CI 的那条路径)**;
`check` → `PATCH CHECK OK`;`plugin_snapshot.py verify` → `SNAPSHOT OK` / `8 plugin(s) have a durable copy matching the pinned commit`;
`check_transport_compat.py` → `TRANSPORT COMPATIBILITY OK`;`check_docs.py` → `DOC CHECK OK`。
另有一项更强的复核:**22 个补丁从 pin 依次应用后,九个被改文件与手工编辑的版本逐字节相同**
(行数:`SyncService.h` 66 / `SyncService.cpp` 422 / `MorphSyncService.h` 141 /
`MorphSyncService.cpp` 1145 / `MorphSyncServiceBodyHair.cpp` 399 / `TngSync.h` 61 /
`TngSync.cpp` 346 / `FreeScenePhaseSync.h` 303 / `FreeScenePhaseSync.cpp` 1041;
`AddonBridge.cpp` 791 / `AddonBridge.h` 106)。

> **补丁数(2026-10-10 更正)**:上面记的 22 是当时的数。IED 的 `0002` 之后被整条重写
> (打到真正被编译的 `src/STRPMAdapter.{h,cpp}` 与 `src/LocalIEDState.{h,cpp}` 上),
> 行数随之变成 `STRPMAdapter.h` 66 / `STRPMAdapter.cpp` 583 /
> `LocalIEDState.h` 47 / `LocalIEDState.cpp` 146,而补丁**总数仍是 23**(重写不是新增);
> 那份「九个文件逐字节相同」的复核已用新补丁重做过一遍,见 §78。

**验证**:插件是 CMake 工程、本机不编译(`plugins/OStimNG/skse/AGENTS.md` 明写不要构建 C++/CMake),
所以这一批的证据是补丁门禁 + 逐字节比对,不是编译产物。
四处修改都不引入新的 include:三个 `src/PCH.h` 已含 `<cstdint>`、`<unordered_map>`、`<mutex>`、`<chrono>`;
`std::size_t` 来自它们已含的 `<cstdint>`/`<string>`。

> **教训**:第一条:**「这个键合法吗」和「这个键能有几个」是两个问题。**
> `SanitizeField` 把分隔符管得很好、`IsSafeAddonToken` 把字符集管得很好,
> 而两张表的**条数**没人管;判据要问的是「这个键是谁选的,而它什么时候被删掉」。
> 第二条:**同族的表要用同一把尺子量。**
> `_assemblies` 有 15 秒 prune 而它旁边两张快照表没有 ——
> 缺陷的形状就是「同一处清理里,谁缺席」,而不是「这个容器看起来危险吗」。
> 第三条:**门禁要跑在它实际会跑的那种工作树上。**
> `verify` 对 index、`check` 对工作树,两者都不会发现「补丁的基线选错了」;
> 只有从 pin 出发的 `apply` 会 —— 而它正是 CI 的那条路。
> 生成补丁前先 `git add -A` 让 index 等于基线,再用 `git diff`,落盘后 `git reset`。
## 76. 2026-10-09 场次:一张行数由客户端决定的实体表,以及它旁边那个只写不读的指针 —— 判据是「这张表什么时候变小」与「谁读这个字段」

> **锚点**:两处修复,都在服务端,加上同一批最后 6 处未夹取的线上浮点:
> `Code/server/Services/ObjectService.h`(新增 `kMaxObjectEntities`)、
> `Code/server/Services/ObjectService.cpp:64-68`/`:100-107`/`:127-129`(计数 + 拒绝 + 告警)、
> `Code/server/Components/ObjectComponent.h`(删掉 `pLastSender` 与带参构造)、
> `Code/encoding/Messages/RequestPlayerHealthUpdate.cpp:14`、
> `Code/encoding/Messages/NotifyPlayerHealthUpdate.cpp:16`、
> `Code/encoding/Structs/Inventory.cpp:19`/`:22`/`:63`/`:76`、
> `Code/encoding/Structs/AnimationVariables.cpp:149`;
> 测试在 `Code/tests/encoding.cpp` 的 `[encoding.finite_floats]`(`:1203-1300`)。

### 76.1 缺陷:一张实体表,和它里面一个没人读的指针

| 位置 | 作用 |
| --- | --- |
| `Code/server/Services/ObjectService.cpp:96`(修复前) | `m_world.create()` 没有计数、没有上限,实体数完全由客户端决定 |
| `Code/server/Components/ObjectComponent.h:12-17`(修复前) | `Player* pLastSender` 只写不读,而它指向的 `Player` 会在掉线时被释放 |
| `Code/encoding/Messages/RequestPlayerHealthUpdate.cpp:13`(修复前) | 血量百分比裸读,接收侧只挡负数 |
| `Code/encoding/Messages/NotifyPlayerHealthUpdate.cpp:15`(修复前) | 同上,同一个值的下行方向 |
| `Code/encoding/Structs/Inventory.cpp:18`/`:21`(修复前) | 附魔效果的 `Magnitude`/`RawCost` 裸读,直接写进引擎 `EffectItem` |
| `Code/encoding/Structs/Inventory.cpp:62`/`:75`(修复前) | `ExtraCharge`/`ExtraHealth` 裸读,而它们下游的门是「大于零」 |
| `Code/encoding/Structs/AnimationVariables.cpp:149`(修复前) | 动画变量浮点裸读,读不读由对端填的位图决定 |

第一处:

```cpp
// Code/server/Services/ObjectService.cpp(修复前)
else
{
    const auto cEntity = m_world.create();          // :96
    m_world.emplace<FormIdComponent>(cEntity, object.Id);
    auto& objectComponent = m_world.emplace<ObjectComponent>(cEntity, acMessage.pPlayer);
    objectComponent.CurrentLockData = object.CurrentLockData;
    m_world.emplace<CellIdComponent>(cEntity, object.CellId, object.WorldSpaceId, object.CurrentCoords);
    auto& inventoryComp = m_world.emplace<InventoryComponent>(cEntity);
    inventoryComp.Content = object.CurrentInventory;   // 对端给的 ≤ 4096 条 × 每条 ≤ 64 个效果
    ...
}
```

第二处:

```cpp
// Code/server/Components/ObjectComponent.h(修复前)
struct ObjectComponent
{
    ObjectComponent(Player* apLastSender)
        : pLastSender(apLastSender)
    {
    }

    Player* pLastSender;
    LockData CurrentLockData{};
};
```

### 76.2 判据:先问「这张表什么时候变小」,再问「谁读这个字段」

1. **这张表只有一条回收路径,而它由「房间里还有人吗」决定。**
   `ObjectService::OnPlayerLeaveCellEvent`(`Code/server/Services/ObjectService.cpp:31-56`)是
   **全仓唯一销毁对象实体的地方**,而它开头就是:

   ```cpp
   for (Player* pPlayer : m_world.GetPlayerManager())
   {
       if (pPlayer->GetCellComponent().Cell == acEvent.OldCell)
           return;                       // 还有人待在这个 cell 里
   }
   ```

   ⇒ 只要有一个玩家留在这个 cell 里,它里面的对象就永远活着;而写入端没有速率限制、
   `AssignObjectsRequest` 每个包最多 255 个对象(`Code/encoding/Messages/AssignObjectsRequest.cpp:7`
   的 `kMaxObjectCount = 0xFF` 只管单包),**总数没有任何上界**。

2. **同一个系统里的另一条容量路径早就有上限。**
   `Code/server/GameServer.cpp:877` 在
   `m_pWorld->GetPlayerManager().Count() >= uMaxPlayerCount.value_as<uint32_t>()` 时直接拒绝入服
   (默认 8,声明在 `GameServer.cpp:29`),拒绝方式就是 `return`;插件层同族的每一张对端可控表
   (`kMaxRemoteSnapshots` / `kMaxRemotePubicSnapshots` / `kMaxRemoteStates` / `kMaxRemotePreps` /
   `kMaxReadyTokens`)这一轮之前也全部补上了上限。**缺席的只有这张实体表。**

3. **「对端声称的 cell」不能拿来当校验条件 —— 合法流量下它也会不一致。**
   `Code/client/Services/Generic/DiscoveryService.cpp` 只有 `gameCenterGrid` 变化才发
   `ShiftGridCellRequest`,而 `gameCurrentGrid` 单独变化时只发 `CellChangeEvent`;并且
   `Code/client/World.cpp` 里 `ObjectService` 的 `CellChangeEvent` sink 注册在 `:47`、
   `PlayerService` 在 `:55` ⇒ 客户端**总是先发** `AssignObjectsRequest`、之后才更新自己的
   `CellIdComponent`。所以这一条不做 —— 严格相等会打断正常同步。

4. **`pLastSender` 的判据是「谁读它」。**
   `git grep -rn pLastSender -- Code` 只有 3 处命中(构造参数、初始化、字段本身),**没有任何读者**;
   而 `Player` 在 `Code/server/GameServer.cpp:610-670 OnDisconnection` 里被
   `m_pWorld->GetPlayerManager().Remove(pPlayer)` 释放,对象实体却要活到 cell 空掉为止 ⇒
   这个指针在玩家掉线后必然悬垂。**没有人读的字段,删掉它比给它补一处清理更安全。**

5. **6 处浮点的判据是「这个值接下来决定了什么」。**
   `ExtraCharge` 在 `Code/client/Games/Skyrim/TESObjectREFR.cpp:611` 过的是
   `if (arEntry.ExtraCharge > 0.f)` 才 `SetChargeData`,`ExtraHealth` 在 `:657` 同形地进 `SetHealth`
   —— **`+inf` 比 `0.f` 大,所以它过得去**;`Magnitude`/`RawCost` 在
   `Code/client/Games/Skyrim/Forms/EnchantmentItem.cpp:24-27` 直接写进引擎的 `EffectItem`;
   血量百分比在 `Code/client/Services/Generic/OverlayService.cpp:543` 只做
   `acMessage.Percentage >= 0.f ? acMessage.Percentage : 0.f` 的三元,**同样只有 `+inf` 过得去**;
   `AnimationVariables::ApplyDiff` 的浮点读不读由对端填的位图决定(`:143-149`)。

### 76.3 可达性

- 第一处:任何**已认证**的客户端,在自己所在的 cell 里正常走一遍 `OnCellChange` 就会发这个包;
  反复发、或每次改一个 `object.Id`,就能持续新建实体。每个实体带一份对端给的 `Inventory`,
  而 `Inventory::Deserialize` 的上界是 `kMaxEntryCount = 0x1000` 条、每条最多
  `kMaxEffectCount = 0x40` 个效果 ⇒ **单包就能让服务端多分配一份最多 4096×64 个效果的内存**。
- 第二处:`pLastSender` 不产生崩溃(没人读它),但它让「这个字段还能用」成为一句空话 ——
  下一个想用它的人拿到的就是悬垂指针。修法是把负债删掉。
- 6 处浮点:`RequestPlayerHealthUpdate` 由客户端 `RunPlayerHealthUpdates` 每 500ms 发一次
  (`Code/client/Services/Generic/OverlayService.cpp:600-626`),`Inventory` 经
  `NotifyInventoryChanges` / `AssignObjectsRequest` 过线,`AnimationVariables` 随
  `ClientReferencesMoveRequest` 每帧过线。

### 76.4 修法

一、实体表按「已有键永远仍可用、被拒的只是新键」的既有范式加上限:

```cpp
// Code/server/Services/ObjectService.h(修复后)
static constexpr std::uint32_t kMaxObjectEntities = 64 * 1024;
```

```cpp
// Code/server/Services/ObjectService.cpp(修复后)
uint32_t objectCount = 0;
for (auto it = std::begin(view); it != std::end(view); ++it)
    ++objectCount;

uint32_t refusedCount = 0;
...
else if (objectCount >= kMaxObjectEntities)     // :100
{
    ++refusedCount;
}
else
{
    const auto cEntity = m_world.create();
    ++objectCount;                              // :107,只有真的建了才加
    ...
}
```

饱和时只记一条告警,不抛异常、不改已建实体的行为:

```cpp
// Code/server/Services/ObjectService.cpp:127-129(修复后)
if (refusedCount != 0)
    spdlog::warn("Object table is full ({} objects), {} of {} requested objects from player {} were refused",
                 objectCount, refusedCount, acMessage.Packet.Objects.size(), acMessage.pPlayer->GetId());
```

常量取 `64 * 1024`:实体本身 4 字节 ⇒ 约 256 KiB,而任何真实 cell 的容器与门都在几十到几千量级。
被拒的实体不进 `response`,客户端 `Code/client/Services/Generic/ObjectService.cpp:200-243`
对「回包里没有这一项」的处理本来就是「不建本地映射」⇒ **少同步一个对象,而不是记错一个对象**。
`ObjectService.h` 顺带补了 `#include <cstdint>`(`Code/server/Pch.h` 没有 `<algorithm>`,
而 `std::uint32_t` 要显式可见;写法与 `Code/server/Services/PluginMessagingService.h:44-47` 一致)。

二、删掉只写不读的字段:

```cpp
// Code/server/Components/ObjectComponent.h(修复后)
struct ObjectComponent
{
    LockData CurrentLockData{};
};
```

`#include <Game/Player.h>` 保留 —— 它是组件头里唯一 include 它的地方,别的 TU 靠它传递拿完整
`Player`;`Code/server/Services/ObjectService.cpp:111` 随之变成
`m_world.emplace<ObjectComponent>(cEntity);`。

三、6 处浮点统一走 `Code/encoding/WireLimits.h:74` 已有的 `ClampFinite`:

```cpp
// Code/encoding/Structs/Inventory.cpp(修复后)
Magnitude   = WireLimits::ClampFinite(Serialization::ReadFloat(aReader));   // :19
RawCost     = WireLimits::ClampFinite(Serialization::ReadFloat(aReader));   // :22
ExtraCharge = WireLimits::ClampFinite(Serialization::ReadFloat(aReader));   // :63
ExtraHealth = WireLimits::ClampFinite(Serialization::ReadFloat(aReader));   // :76
```

```cpp
// Code/encoding/Structs/AnimationVariables.cpp:149(修复后)
Floats[i] = WireLimits::ClampFinite(TiltedPhoques::Serialization::ReadFloat(aReader));
```

`RequestPlayerHealthUpdate.cpp` / `NotifyPlayerHealthUpdate.cpp` 同形,四个文件各补一行
`#include <WireLimits.h>`(写在第 2 行、紧跟消息头,与
`Code/encoding/Messages/AddTargetRequest.cpp:1-2` 同形)。**非有限值替换为 `0.f` 而不是夹进区间**
—— 没有任何区间能让 `NaN` 有意义;而有限的合法值原样通过(`ExtraCharge` 真的会是 `0.5f` 这种值)。

### 76.5 复核命令(照抄)

```powershell
python Tools\Scripts\check_docs.py
python Code\plugins\tools\plugin_patches.py verify
python Code\plugins\tools\plugin_patches.py check
python Code\plugins\tools\plugin_snapshot.py verify
python Code\plugins\tools\check_transport_compat.py
git grep -rn "pLastSender" -- Code                 # 期望 exit 1 / 零命中
git grep -rn "kMaxObjectEntities" -- Code
git grep -rn "ClampFinite" -- Code/encoding
git grep -n "objectCount\|refusedCount" -- Code/server/Services/ObjectService.cpp
git grep -n "ExtraCharge == 0.f\|ExtraHealth == 0.f\|RawCost == 0.f\|Floats\[3\] == 2.5f" -- Code/tests/encoding.cpp
```

复核结果:`check_docs.py` → `DOC CHECK OK` / `coverage 3069/3082 stated consistently in 4 doc(s)`;
`plugin_patches.py verify` → `PATCH VERIFY OK` / `22 patch(es) apply to their pinned commits`;
`check` → `PATCH CHECK OK`;`plugin_snapshot.py verify` → `SNAPSHOT OK` / `8 plugin(s) have a durable
copy matching the pinned commit`;`check_transport_compat.py` → `TRANSPORT COMPATIBILITY OK`
(`client opcodes: 53 in use` / `server opcodes: 56 in use`,**这一批没有动线格式**);
`pLastSender` **零命中**;`kMaxObjectEntities` 命中 `ObjectService.h:37` 与
`ObjectService.cpp:100`;`ClampFinite` 在 `Code/encoding` 命中 28 处(含定义 `WireLimits.h:74`)。

**验证**:本机不编译(`docs/PITFALLS.md` §0/§1),所以这一批的证据是逐字命中 + 门禁 + 用例断言,
不是编译产物。新增的五个断言块全部把接收对象先写成有限哨兵 `1.f`,使「读的时候替换了」成为
断言成立的唯一原因;`AnimationVariables` 那一条用 `GenerateDiff`/`ApplyDiff` 走真正的位图路径,
并用 `2.5f` 证明有限的合法值原样通过。服务端两处修改都不引入新的头(`ObjectService.h` 补的
`<cstdint>` 是唯一新增)。

> **教训**:第一条:**先问「这张表什么时候变小」,再问「它能变多大」。**
> 只在「房间空了」时回收,就等于「只要有人待着就一直涨」;
> 而 `AssignObjectsRequest` 确实是合法包、`object.Id` 也确实在 2^64 的取值空间里 ——
> 缺陷不在「值非法」,而在「行数没人管」。
> 第二条:**只写不读的字段是负债,不是接口。**
> `pLastSender` 没有读者,所以它不产生崩溃,只让「这个字段还能用」成为一句空话;
> 删掉它比给它补一处失效通知便宜,也比等下一个使用者踩上去便宜。
> 第三条:**「下游会不会挡住」要用下游真正的条件去试。**
> `ExtraCharge`/`ExtraHealth` 的下游门是 `> 0.f`,而 `+inf` 比 `0.f` 大 ——
> 一道「大于零」的门对非有限值等于不存在。

## 78. 2026-10-10 场次:一个补丁改的文件从来没进过编译,而三条门禁都看不见它 —— 判据是「这个文件在不在构建闭包里」

> **锚点**:两处,都在插件层,交付方式是补丁(插件是子模块,本仓不拥有、也不能 push)。
> 第一处是**补丁换靶**:`Code/plugins/patches/IEDSyncTogether/0002-bound-the-live-peer-table-and-the-decoded-object-list.patch`
> 取代已删除的 `0002-bound-the-remote-snapshot-table.patch`
> (原版改 `plugins/IEDSyncTogether/src/SyncService.{h,cpp}`),新靶是
> `plugins/IEDSyncTogether/src/STRPMAdapter.{h,cpp}`(新常量 `:58-61`,收包守卫 `:555-561`)
> 与 `plugins/IEDSyncTogether/src/LocalIEDState.{h,cpp}`(新常量 `:32-36`,解码守卫 `:96-98`)。
> 第二处是门禁:`Code/plugins/tools/plugin_patches.py` 的 `verify` 多了两条不变量
> —— 每个补丁改的文件必须真的被编译(`:99-207`),文档点名的补丁路径与
> `// plugins/<id>/<file>(修复后)` 标记必须真实存在(`:354-411`)。

### 78.1 缺陷:上限加在一个从来没被编译的文件上

| 位置 | 作用 |
| --- | --- |
| `plugins/IEDSyncTogether/CMakeLists.txt:15-32`(pin `ad4710a6`) | `SOURCES` 只列 16 项,`src/SyncService.cpp`、`src/SyncService.h` 不在其中 |
| `ace777c16633ad4d368c5e9b5b58e7b83104dd7a`(2026-08-19,`Build v0.6.0 STRPM-only runtime`) | `CMakeLists.txt` +3/-15 —— 15 行源码从 `SOURCES` 里被删掉,文件留在树里 |
| `0002-bound-the-remote-snapshot-table.patch`(修复前,已删除) | 改的正是这两个文件 ⇒ 那条上限从未进过任何二进制 |
| `plugins/IEDSyncTogether/src/STRPMAdapter.h:63` / `src/STRPMAdapter.cpp:562`(修复前) | `_remoteStates[senderID] = RemoteSnapshot{…}`,键是对端填的 `ConnectionID`,表没有行数上界 |
| `plugins/IEDSyncTogether/src/LocalIEDState.cpp:80-141`(修复前) | 逐行解 `O|`,行数由对端决定,`objects` 没有元素数上界 |

第一处,`SOURCES` 与 `src/` 的差集(逐字取自 `plugins/IEDSyncTogether/CMakeLists.txt` 与 `src/` 实有文件):

```text
SOURCES(16): PCH.h main.cpp FormIdentity.{cpp,h} IEDBridge.{cpp,h} LocalIEDState.{cpp,h}
             LocalCaptureProbe.{cpp,h} RemoteIEDRenderer.{cpp,h} STRPMAdapter.{cpp,h}
             include/IEDSyncTogether/Interface.h include/STRPluginMessagingAPI/STRPluginMessagingAPI.h
src/(25) 中不在 SOURCES 里的 13 个:
             src/Config.{cpp,h} src/ProxyResolver.{cpp,h} src/StrServerDiscovery.{cpp,h}
             src/StrTransport.{cpp,h} src/SyncService.{cpp,h} src/UdpTransport.{cpp,h}
             integration/ied-dev/IEDSyncTogetherBridge.h
```

第二处,`src/LocalIEDState.cpp` 的解码循环(修复前,行数由对端决定):

```cpp
// plugins/IEDSyncTogether/src/LocalIEDState.cpp(修复前)
if (!line.starts_with("O|")) {                            // :93
    return std::nullopt;
}

const auto fields = Split(line.substr(2), '|');           // :100
...
state.objects.emplace_back(std::move(object));            // :140,每次循环追加一行
```

**现象**:补丁把 `_remoteSnapshots` 的行数上限加在 `src/SyncService.h` / `src/SyncService.cpp` 上,
`plugin_patches.py verify`、`plugin_patches.py check`、`check_transport_compat.py` 三条门禁全绿,
而那条上限**不在任何二进制里**。同一批里真正需要上界的两张表
(`src/STRPMAdapter.h` 的 `_remoteStates`、`src/LocalIEDState.cpp` 解出的 `objects`)**一个都没修** ——
`docs/PITFALLS.md` §75 的标题写着「三个插件的对端可控缓存只增不减」,而 IED 那一栏当时是空的。

**根因**:`git apply` 的绿灯只证明「补丁的上下文行能对上 pin」,它不问这个文件**有没有被编译**。
补丁改的文件在树里、在 pin 里、在 `git grep` 里都能命中,唯一不在的地方是构建。

### 78.2 判据:两条,都可机械执行

第一条:**「补丁能应用」和「补丁改的东西被编译」是两件事,判据是可达性而不是成员关系。**

从 `CMakeLists.txt` 里所有 `.cpp/.cxx/.cc/.h/.hpp` token 出发,沿 `#include "…"` 做闭包,
补丁的每个 `--- a/<path>` 目标都必须在闭包里。
「在 `SOURCES` 里」既不是充分条件也不是必要条件:`MorphSyncTogether` 就不写
`src/AppearanceProbe.cpp` 与 `src/MorphSyncService.cpp`,它们由 `src/AppearanceProbeBodyHair.cpp` /
`src/MorphSyncServiceBodyHair.cpp` 改名入口点后 `#include` 进来 —— 只查 `SOURCES` 会把这两个文件误判成死代码。
含 `GLOB` 的工程(`OStimNG` 走 `skse/cmake/lib/autoCollectSources.cmake`)无法静态枚举,门禁整体跳过:
报「不知道」比报一条假的死代码便宜。

第二条:**文档里的路径和补丁目录里的文件名是两个可以各说各话的字符串。**

本轮新门禁第一次实跑就报出 **7 条、零误报**:4 条是文档点名的补丁路径已经不存在
(`README.md`、`README_EN.md`、`CHANGELOG.md`、`docs/PITFALLS.md` 各 1),
3 条是 `plugins/IEDSyncTogether/src/SyncService.{h,cpp}(修复后)` 这种标记还在,
而那个文件已经没有任何补丁会改它。**标记比它出自的补丁活得久**,这就是判据。
两处刻意的省略写法(含 `...` 的路径)未被误报 —— 判据要允许「这里本来就是省略」。

### 78.3 可达性

第一处:`RefreshProxyMitigation()`(`plugins/IEDSyncTogether/src/SyncService.cpp:302-379`)每 750 ms
对每个 snapshot 调一次 `FindRemotePlayerProxy`,而每次都会重建 `FindRemotePlayerProxies()`
的四张 `ProcessLists` 句柄数组扫描,外加每个候选一次 `GetName()` 与一次 `GetDistance()`。
单看源码这是本插件最像「热路径缺陷」的一段 —— 它**不在任何发布产物里**:
`src/SyncService.cpp` 不在 `SOURCES`,也没有任何 `#include` 链从被编译的文件到达它。
**审计一个插件的「热路径」之前,先问这个文件有没有被编译。**

第二处:门禁的两条新不变量都只挂在 `cmd_verify` 上,`cmd_check` 仍然只判「补丁是否已应用」。
这是有意的:`check` 要在**已经被 `apply` 过的工作树**上跑,而那种工作树里补丁改的文件当然存在;
`verify` 才是对 pin 的静态断言。两条判据的宿主不同,不能互换。

### 78.4 修法

- `Code/plugins/tools/plugin_patches.py:99-211` 新增 `build_roots` / `cmake_source_seeds` /
  `include_closure` / `patch_targets` / `uncompiled_patch_targets`,接进 `cmd_verify:422-425`;
  只对源码后缀判定,含 `GLOB` 的工程返回 `None` 直接跳过。
- 同文件 `:354-411` 新增 `document_lines` / `patched_targets` / `unrecorded_patch_paths` /
  `unrecorded_source_markers`,接进 `cmd_verify:419-420`;只扫 `README.md`、`README_EN.md`、
  `CHANGELOG.md`、`docs/PITFALLS.md`,含 `...` 的刻意省略写法跳过,
  标记里的 `:NN` / `:NN-MM` 行号后缀先剥掉再比对。
- 补丁换靶(新文件 2735 B / 64 LF / 4 hunk):

```cpp
// plugins/IEDSyncTogether/src/STRPMAdapter.h(修复后)
static constexpr std::size_t kMaxRemoteStates = 64;       // :61

// plugins/IEDSyncTogether/src/STRPMAdapter.cpp(修复后)
if (!self->_remoteStates.contains(senderID) && self->_remoteStates.size() >= kMaxRemoteStates) {   // :555
    SKSE::log::warn("STRPM IED state RX dropped: peer table full size={} sender={}",              // :557
                    self->_remoteStates.size(), senderID);
    return;
}
self->_remoteStates[senderID] = RemoteSnapshot{          // :562,已有的键永远仍可更新

// plugins/IEDSyncTogether/src/LocalIEDState.h(修复后)
static constexpr std::size_t kMaxObjects = 512;          // :36

// plugins/IEDSyncTogether/src/LocalIEDState.cpp(修复后)
if (state.objects.size() >= LocalIEDState::kMaxObjects) {   // :96
    return std::nullopt;
}
```

  解码侧刻意**整包拒收而不是截断**:少几个对象的快照会被渲染成「完整快照」,
  截断把一个上界问题换成一个静默的错帧问题。
- 文档随之更正:`docs/PITFALLS.md` §75 的锚点/缺陷表/判据/可达性/修法/复核命令、§77.2 判据行、
  `CHANGELOG.md` 三处、两份 README 的补丁路径。

### 78.5 复核命令(照抄)

```powershell
python Tools\Scripts\check_docs.py
python Code\plugins\tools\plugin_patches.py verify
python Code\plugins\tools\plugin_patches.py check
python Code\plugins\tools\plugin_snapshot.py verify
python Code\plugins\tools\check_transport_compat.py
git -C plugins/IEDSyncTogether grep -n "kMaxRemoteStates\|kMaxObjects" -- src/
git -C plugins/IEDSyncTogether grep -n "SyncService" -- CMakeLists.txt
git -C plugins/IEDSyncTogether log --oneline -1 ace777c16633ad4d368c5e9b5b58e7b83104dd7a
git -C plugins/IEDSyncTogether apply --check --cached ..\..\Code\plugins\patches\IEDSyncTogether\0002-bound-the-live-peer-table-and-the-decoded-object-list.patch
git -C plugins/IEDSyncTogether apply --check --reverse ..\..\Code\plugins\patches\IEDSyncTogether\0002-bound-the-live-peer-table-and-the-decoded-object-list.patch
```

复核结果:`check_docs.py` → `DOC CHECK OK` / `coverage 3069/3082 stated consistently in 4 doc(s)`;
`plugin_patches.py verify` → `PATCH VERIFY OK` / `23 patch(es) apply to their pinned commits`
(**补丁总数仍是 23** —— 这一批是重写不是新增);`check` → `PATCH CHECK OK`;
`plugin_snapshot.py verify` → `SNAPSHOT OK` / `8 plugin(s) have a durable copy matching the pinned commit`;
`check_transport_compat.py` → `TRANSPORT COMPATIBILITY OK`(`client opcodes: 53 in use` /
`server opcodes: 56 in use`,**这一批没有动线格式**);
`kMaxRemoteStates` 命中 `STRPMAdapter.h:61` 与 `STRPMAdapter.cpp:555`,
`kMaxObjects` 命中 `LocalIEDState.h:36` 与 `LocalIEDState.cpp:96`;
`git grep -n "SyncService" -- CMakeLists.txt` **exit 1 / 零命中**;
`git log -1 ace777c` → `ace777c16633ad4d368c5e9b5b58e7b83104dd7a` / `2026-08-19` /
`Build v0.6.0 STRPM-only runtime` / `CMakeLists.txt | 18 +++---------------` / `1 file changed, 3 insertions(+), 15 deletions(-)`;
最后两条 `git apply --check`:`--cached`(对 index = pin)exit 0,`--reverse`(对工作树)exit 0
⇒ 补丁既「对 pin 可应用」又「已经在工作树里」;不带 `--cached`/`--reverse` 的 `git apply --check`
对工作树**必然失败**(`error: patch failed: src/STRPMAdapter.cpp:552`),因为改动已经在里面。
另:把新补丁的 `--- a/` 目标喂给 `plugin_patches.uncompiled_patch_targets` 复现闭包判定,
`plugins/IEDSyncTogether` 的闭包 = 16 个 seed,无 include 扩展;同一函数对**已删除**的旧补丁报
`edits src/SyncService.cpp, which this plugin's build never compiles …`。

**验证**:本机不编译(`docs/PITFALLS.md` §0/§1),所以这一批的证据是构建闭包的静态判定 + 逐字命中 + 门禁,
不是编译产物。两条新不变量都在**首次实跑就报出真问题**(7 条),不是事后补的说明;
换靶后的补丁对 pin 可应用、对工作树已应用,两条 `git apply --check` 互为反向断言。

> **教训**:第一条:**「补丁能应用」与「补丁改的东西被编译」是两条判据,而前者的绿灯很容易让人以为后者也绿了。**
> `git apply --check` 只比对上下文行,它甚至不知道 CMake 存在;
> 一个文件可以既在 pin 里、又在树里、又永远不进任何二进制 —— 判据必须落在构建闭包上。
> 第二条:**门禁要报的是「不知道」,不是「猜」。**
> 含 `GLOB` 的工程无法静态枚举,门禁整体跳过而不是按名字猜成员关系 ——
> 假阳性会让下一次真阳性被忽略,而「跳过」是可以解释的。
> 第三条:**审计一个「热路径」之前,先问这段代码有没有被编译。**
> `RefreshProxyMitigation()` 的四表扫描看起来是本插件最贵的循环,而它所在的文件
> 从 2026-08-19 起就不在 `SOURCES` 里 —— 白读一小时,换来的是这一条判据。

## 79. 2026-10-10 场次:一个上限把插件层的传输通道截断了,而截断后的 hex 仍然是合法 hex —— 判据是「这个字段除了聊天还有谁在用」

> **锚点**:`Code/encoding/WireLimits.h:44` 的 `kMaxChatMessageBytes` 从 `512` 改成 `4096 + 512`,
> 以及 `Code/plugins/tools/check_transport_compat.py:153-198` 新增的门禁 —— 它把这个上限与
> `plugins/STRPluginMessagingAPI/extras/str-server-resources/strpm-chat-relay/main.lua:8` 的
> `MAX_ENVELOPE_LENGTH` 绑在一起。测试在 `Code/tests/encoding.cpp:838-886`。
>
> **这一节修的是本仓自己上一轮引入的回归**(§62 是同一个字段的上限第一次被加上,
> 那时只按「一行聊天」定大小)。

### 79.1 缺陷:一个字段被两个互不知情的契约共用,而小的那个静默生效

**现象**:`ChatMessage` 这一个线上字段同时承担两件事。对玩家它是聊天行;
对 7 个伴生插件它是**唯一的传输通道** —— 桥把 `STRPM|v2|...` 信封塞进聊天行,
服务端的中继脚本把信封读出来、再转发给目标玩家。两条契约的尺寸相差 8 倍:

```lua
-- plugins/STRPluginMessagingAPI/extras/str-server-resources/strpm-chat-relay/main.lua
local MAX_ENVELOPE_LENGTH = 4096                                        -- :8
...
if string.len(message) > MAX_ENVELOPE_LENGTH then
  print("[STRPM] Dropped oversized envelope")                           -- :150-153
  return
end
```

而 `Code/encoding/Messages/SendChatMessageRequest.cpp` 按「一行聊天」夹取:

```cpp
// Code/encoding/Messages/SendChatMessageRequest.cpp(修复前)
ChatMessage = Serialization::ReadString(aReader);
WireLimits::Clamp(ChatMessage, WireLimits::kMaxChatMessageBytes);   // :16,当时是 512
```

`Code/encoding/Messages/NotifyChatMessageBroadcast.cpp:18-19` 在广播方向夹同一个字段,
而**两个 `SerializeRaw` 都不夹** ⇒ 发送方写多长,线上就有多长,夹取只发生在接收方。

**根因**:夹取发生在**中继脚本之前**。服务端的收包链是
`Code/server/GameServer.cpp:572-602 OnConsume` → `ClientMessageFactory::Extract`
(`Code/encoding/Messages/ClientMessageFactory.cpp:17-20` 里 `ptr->DeserializeRaw(aReader)`)→
`Code/server/Services/OverlayService.cpp:60-81 HandleChatMessage` →
`Code/server/Services/ScriptService.cpp:164-167` 的 `CallCancelableEvent("onChatMessage", ...)`,
而中继脚本正是挂在 `onChatMessage` 上的。于是 >512 字符的信封在脚本看到它之前就已经短了一截。

**为什么没有任何东西报错**:被切掉的是 `payload=<hex>` 的**尾巴**,而信封的字段顺序是
`msg|seq|channel|target|flags|part|parts|payload`,中继追加的 `sender`/`senderPlayerId`/
`senderName`/`serverTick` 在最后 ⇒ 截断后 `part`/`parts` 仍在,`validateEnvelope` 通过:

```lua
-- plugins/STRPluginMessagingAPI/extras/str-server-resources/strpm-chat-relay/main.lua:89
if fields["payload"] == nil or string.match(fields["payload"], "^[0-9A-Fa-f]*$") == nil then
  return false, "invalid payload"
end
```

任何 hex 前缀都匹配 `^[0-9A-Fa-f]*$`。接收侧也只多判一条奇偶:
`plugins/STRPluginMessagingAPI/src/STRPluginMessagingBridgeReceive.cpp:798` 的
`if ((text.size() & 1u) != 0) return std::nullopt;` —— 偶数长度的截断 hex 被**照常解码**,
于是消费方拿到一个更短、内容错误的载荷,而不是「这条消息被丢了」。

### 79.2 判据:这个字段除了它字面上的用途,还有谁在用

两条,都可机械执行:

1. **给一个字段定上限之前,先列出这个字段的全部消费者。** 判据不是「这个值叫什么名字」,
   而是「还有哪个子系统把它当传输层用」。`ChatMessage` 的名字只说了聊天,
   而 `plugins/STRPluginMessagingAPI/src/STRPluginMessagingBridge.cpp:31` 的
   `constexpr std::size_t kMaxEnvelopeChars = 3500;` 早就说明了它还有第二个用途 ——
   这个常量与 `512` 同时存在于仓库里,矛盾是可见的。
2. **跨文件的数值契约必须由门禁绑定。** 三个数值住在三个互不引用的文件里
   (C++ 头、Lua 资源、C++ 桥),谁都不知道另外两个 ⇒ 只靠读代码迟早再次漂移。

### 79.3 可达性

**这条路径是活的,而且默认就开着。** `plugins/STRPluginMessagingAPI/CMakeLists.txt:9-13` 的
`option(STRPM_ENABLE_UDP_BACKEND ... OFF)` 关掉的是**旧 UDP 后端**,与这里无关:
发布构建走的是 STR 桥(`STRPluginMessagingBridge.cpp` 的 VEH 注入),信封经真实聊天包进出。
`kMaxPayloadBytes = 24 * 1024`(`plugins/STRPluginMessagingAPI/include/STRPluginMessagingAPI/STRPluginMessagingAPI.h:21`)
⇒ 单个合法载荷可达 24 KiB,hex 后 48 KiB,远超中继的 4096 ⇒ 中继自己会先丢;
**能穿过中继的信封最大就是 4096 字符**,而 4096 > 512 ⇒ 中间那一整段(512~4096)全是静默截断区。

Morph 的快照分块常量给出了这段区间的真实占用
(`plugins/MorphSyncTogether/src/MorphSyncService.cpp:441` 的 `constexpr std::size_t kChunkLimit = 2200;`):
一次形变快照被切成 2200 字符的块,每块再包一层 `MORPH|hash=|seq=|count=|props=` ⇒
**单个信封常态就在 512 与 4096 之间**,不需要恶意构造就能命中。

### 79.4 修法:把上限绑到中继的契约上,并让门禁守住它

`Code/encoding/WireLimits.h:33-44` —— 常量改成中继的信封上限加中继自己追加的元数据长度,
并把「为什么不是 512」写进注释(不写就会被下一个人按聊天行改回去):

```cpp
// Code/encoding/WireLimits.h
// This field is also the companion-plugin transport. The STRPM bridge packs a
// "STRPM|v2|..." envelope into a chat line, the server relay script reads that envelope
// back out, accepts up to 4096 characters, and then appends its own sender metadata
// before relaying it. ...
inline constexpr uint32_t kMaxChatMessageBytes = 4096 + 512;
```

`Code/plugins/tools/check_transport_compat.py:153-198` 新增一段:读 `main.lua` 的
`MAX_ENVELOPE_LENGTH`(`:172`)与 `WireLimits.h` 的 `kMaxChatMessageBytes`(`:174`),
要求后者**严格大于**前者 —— 中继会在信封后面追加 `|sender=|senderPlayerId=|senderName=|serverTick=`。

### 79.5 复核命令(照抄)

```powershell
# 1. 门禁本身(含反向验证:把上限改回 512 应当 exit 1)
python Code\plugins\tools\check_transport_compat.py

# 2. 两个数值的当前取值
node -e "const fs=require('fs');const w=fs.readFileSync('Code/encoding/WireLimits.h','utf8');console.log(w.match(/kMaxChatMessageBytes\s*=\s*[^;]+;/)[0]);const l=fs.readFileSync('plugins/STRPluginMessagingAPI/extras/str-server-resources/strpm-chat-relay/main.lua','utf8');console.log(l.match(/MAX_ENVELOPE_LENGTH\s*=\s*\d+/)[0]);"

# 3. 这个字段的全部消费者(夹取点 + 桥的信封上限)
git grep -n "kMaxChatMessageBytes" -- Code
git grep -n "kMaxEnvelopeChars\|MAX_ENVELOPE_LENGTH"
```

**验证**:本机不编译(§0/§1),所以证据是逐字命中 + 门禁 + 反向验证,不是编译产物。
`check_transport_compat.py` 全绿(`client opcodes: 53 in use, plugin request at index 52` /
`server opcodes: 56 in use, plugin notify at index 55` / `chat opcodes  : send 38, broadcast 36`);
把 `WireLimits.h` 改回 `512` 后门禁立刻 exit 1 并给出上面那段文案,改回 `4096 + 512` 后恢复 exit 0。
`Tools/Scripts/check_docs.py` 全绿。`Code/tests/encoding.cpp:838-886` 新增的断言要求
一个「中继上限 + 元数据」大小的信封在 `SendChatMessageRequest` 与 `NotifyChatMessageBroadcast`
两个方向都原样往返,并显式断言它 `> 512` —— 上限一旦按聊天行定大小,这条就红。

> **教训**:第一条:**给字段定上限时,要问的不是「这个值多大」,而是「这个字段被当成什么在用」。**
> `ChatMessage` 的名字只说了聊天,而同一个仓库里 `kMaxEnvelopeChars = 3500` 已经写着它还是传输层;
> 两个常量同时存在却没人对比,于是小的那个静默生效。
> 第二条:**截断一个 hex 字符串不会让它变成非法 —— 它会变成一个更短的合法值。**
> 所以「下游会校验」在这里是空的:校验器能看出的只有奇偶,看不出内容少了一半。
> 任何「长度由对端决定、而内容是自描述编码」的字段都有这个性质。
> 第三条:**跨文件的数值契约要进门禁,不能靠注释。**
> 这三个数字住在三个互不引用的文件里,注释只能提醒读到这里的人;
> 门禁能在任何人改其中一个时立刻说话 —— 而它这次报的是本仓上一轮自己引入的回归。

## 77. 2026-10-10 场次:两张键由对端选、长度也由对端选的表 —— 判据是「这个键是谁写的,而它的长度谁管」

> **锚点**:两处修复,都在插件层,交付方式是补丁(插件是子模块,本仓不拥有、也不能 push):
> `Code/plugins/patches/TradeTogether/0002-bound-the-peer-name-and-request-id-tables.patch`
> 改 `plugins/TradeTogether/src/StrMessagingTransport.cpp`(新常量 `:14-20`,收包守卫 `:520-538`,
> 解析缓存 `:261-265`)与 `plugins/TradeTogether/src/Trade.cpp`(新常量 `:21-27`,
> 满表早退 `:757-763`,请求 id 长度校验 `:1068-1070`)。

### 77.1 缺陷:两张表,键的内容与长度都由对端决定

| 位置 | 作用 |
| --- | --- |
| `plugins/TradeTogether/src/StrMessagingTransport.cpp:517`(修复前) | `_namedConnections[*peerName] = a_message->sender.connectionID;` —— 键是包内 `from` 字段 hex 解码后的字符串,长度没有任何上界 |
| `plugins/TradeTogether/src/StrMessagingTransport.cpp:510`(修复前) | 收包守卫只判 `peerName && !peerName->empty()`,没有键长上界 |
| `plugins/TradeTogether/src/Trade.cpp:1053`(修复前) | `requestID` 只有 `requestID->empty()` 一道门,没有长度上界 |
| `plugins/TradeTogether/src/Trade.cpp:749-766`(修复前) | `recentIncoming.emplace(a_requestID, …)` 无条件插入,表本身没有行数上界 |

第一处,键长:

```cpp
// plugins/TradeTogether/src/StrMessagingTransport.cpp(修复前)
std::optional<std::string> peerName;
if (const auto encodedFrom = Protocol::ReadField(packet, "from"))
{
    peerName = Protocol::HexDecode(*encodedFrom);       // :514-515
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

`Protocol::HexDecode`(`plugins/TradeTogether/src/Protocol.cpp:37-54`)只判「长度是偶数」与「每个字符是十六进制」,
**没有任何长度上限**,而它拿到的 `encodedFrom` 来自一个上限为 `STRPM::kMaxPayloadBytes = 24 * 1024`
的载荷 ⇒ 单个键最长约 12 KiB。

第二处,行数:

```cpp
// plugins/TradeTogether/src/Trade.cpp(修复前)
if (state.recentIncoming.contains(a_requestID))
{
    return;
}
state.recentIncoming.emplace(a_requestID, std::chrono::steady_clock::now());   // :749-751
```

`a_requestID` 是 `HandleNetworkPacket` 从包里读出来的(`Trade.cpp:1050`,现 `:1065`),
而同一次解析里 `from`/`to` **是有 256 上限的**(`Trade.cpp:1061`: `from->size() > 256 || to->size() > 256`)——
同一次解析、同一个包里两个字段,一个有上界一个没有,这个不一致本身就是判据。

### 77.2 判据

- **`_namedConnections` 什么时候变小?** 只有三处清空:`HandleProxyMapping` 的 `kCleared` 分支
  (`:184`)、`Start()`(`:317`)、`Stop()`(`:402`),其余只有一处
  `std::erase_if(_namedConnections, … e.second == a_event->connectionID)`(`:194`)——
  **按连接号删,不按名字删**。所以一个只在载荷里换个 `from` 就能写入新键的对端,
  在一整局游戏里可以一直加行,而唯一的容量信号是「游戏结束」。
- **这张表被谁遍历?** `ResolveConnectionIDForPlayerName`(`:233`)先持 `_proxyMutex`
  线性扫 `_namedConnections` 做大小写不敏感比较(`:241`),而它的唯一调用点是
  `SendTo`(`:420`)—— **游戏线程上的发送路径**。行数与键长一起决定这条路径有多贵。
- **`recentIncoming` 什么时候变小?** 只有 `Update()` 里的按龄清扫(`:976-983`,
  `historyLifetime = std::max(requestTimeout * 2, std::chrono::milliseconds(60000))`),
  而 `HandleIncomingRequest` 的插入发生在**「本机是否正忙」这道判断之前**(`:754-766` 先插入,
  `:768` 才判 `state.session || state.pending`)⇒ 被自动拒绝的请求同样占一行,
  60 秒窗口内的行数完全由对端决定。
- **同族的修法已经在仓里**:`IEDSyncTogether/0002` 的 `kMaxRemoteStates = 64` 与 `kMaxObjects = 512`、
  `MorphSyncTogether/0004` 的 `kMaxRemoteSnapshots`/`kMaxRemotePubicSnapshots`/`kMaxRemoteStates = 64`、
  `OStimTogether/0007` 的 `kMaxResolvedEntries`/`kMaxRemoteNames = 512`、
  `0008` 的 `kMaxRemotePreps = 32`/`kMaxReadyTokens = 256` —— 全部是「先判 `contains` 再判上限,
  已有的键永远仍可更新,被拒的只是新键」。这一批缺的只有这两张。

### 77.3 可达性

- 两张表都在**已连接的 STRPM 通道**上:发送方只要能往 `tradetogether.offer.v1` 这个频道发一条
  以 `TTNET|v1|` 开头的载荷即可(`SameChannel` 只比频道名,`HandleMessage` 只判前缀)。
  这是普通联机流量,不需要任何权限、不需要成为队友、也不需要本机正在交易。
- 第一处的写入条件是 `a_message->sender.connectionID != 0`(即发送方已拿到连接号),
  换一个 `from` 就是一个新键 ⇒ 单条 24 KiB 载荷可以写入约 12 KiB 的键。
- 第二处只需要包里有 `type=REQUEST`、`from`/`to` 过 256 校验、`to` 等于本机名字、`from` 不等于本机名字
  (`Trade.cpp:1067-1073`)⇒ 每秒若干条就能在 60 秒窗口里堆到任意行数。
- 两者的代价都落在**游戏线程**:`ResolveConnectionIDForPlayerName` 的线性扫在发送路径上,
  `Update()` 的按龄清扫在每帧输入事件与每个网络包上(`Input.cpp:52`、`Trade.cpp:976`/`:998`/`:1047`)。

### 77.4 修法

一、键长与行数在**写入它的那一层**夹住(不是在消费它的 Trade 层 —— Trade 层那道 256 校验
发生在 transport 已经写入之后):

```cpp
// plugins/TradeTogether/src/StrMessagingTransport.cpp:14-20(修复后)
// The peer name is a field the sender fills in, and the only thing that
// ever removes a row is the proxy mapping event for its connection, so
// both the key length and the row count are bounded here. A name longer
// than the Trade layer accepts can never match a lookup, and a full
// table drops the row instead of growing without limit.
constexpr std::size_t kMaxPeerNameBytes = 256;
constexpr std::size_t kMaxNamedConnections = 64;
```

```cpp
// plugins/TradeTogether/src/StrMessagingTransport.cpp:520-538(修复后)
if (peerName && !peerName->empty() && peerName->size() <= kMaxPeerNameBytes &&
    !Protocol::EqualsInsensitive(*peerName, GetLocalPlayerName()))
{
    ...
    if (a_message->sender.connectionID != 0)
    {
        std::scoped_lock lock(_proxyMutex);
        if (_namedConnections.contains(*peerName) ||
            _namedConnections.size() < kMaxNamedConnections)
        {
            _namedConnections[*peerName] = a_message->sender.connectionID;
        }
        else
        {
            spdlog::warn(
                "TradeTogether STRPM peer name table is full: size={} nameBytes={}",
                _namedConnections.size(),
                peerName->size());
        }
    }
}
```

`kMaxPeerNameBytes = 256` 与 Trade 层自己的 `Trade.cpp:1061` 上限**取同一个值**:更长的名字在那边
必然被丢掉,在这边也永远匹配不上任何一次 `ResolveConnectionIDForPlayerName`,所以拒绝它不丢信息。
`kMaxNamedConnections = 64` 与同族的 `kMaxRemoteSnapshots = 64` 同宽。

解析缓存那条写入路径同样夹住(`:261-265`),否则一次代理解析就能越界写入两个键:

```cpp
// plugins/TradeTogether/src/StrMessagingTransport.cpp:261-265(修复后)
std::scoped_lock lock(_proxyMutex);
if (_namedConnections.size() < kMaxNamedConnections)
{
    _namedConnections[actorName] = connectionID;
    _namedConnections[std::string(a_playerName)] = connectionID;
}
```

二、请求 id 的长度与历史表的行数在同一个文件里夹住:

```cpp
// plugins/TradeTogether/src/Trade.cpp:21-27(修复后)
// The request id is a field the sender fills in, and a row of
// recentIncoming is only removed once it ages out, so both the id
// length and the row count are bounded here. A local id is always the
// 32 characters MakeRequestID formats, and a full table drops the id
// instead of growing without limit.
constexpr std::size_t kMaxRequestIDBytes = 64;
constexpr std::size_t kMaxRecentIncoming = 256;
```

`kMaxRequestIDBytes = 64` 是本地 `MakeRequestID()` 产出的 32 字符(`Trade.cpp:100-108`,
`fmt::format("{:08X}{:016X}{:08X}", …)`)的两倍,给合法发送方留了余量;
满表早退放在 `contains` 之后、插入之前:

```cpp
// plugins/TradeTogether/src/Trade.cpp:754-766(修复后)
if (state.recentIncoming.contains(a_requestID))
{
    return;
}
if (state.recentIncoming.size() >= kMaxRecentIncoming)
{
    spdlog::warn(
        "Trade request history is full: size={} request={}",
        state.recentIncoming.size(),
        a_requestID);
    return;
}
state.recentIncoming.emplace(a_requestID, std::chrono::steady_clock::now());
```

```cpp
// plugins/TradeTogether/src/Trade.cpp:1068-1070(修复后)
if (!type || !requestID || requestID->empty() ||
    requestID->size() > kMaxRequestIDBytes ||
    !encodedFrom || !encodedTo)
{
    spdlog::warn("Malformed trade packet ignored");
    return;
}
```

三、交付:两个文件都在 `plugins/TradeTogether` 子模块里,而 CI 的
`git submodule update --init --force --recursive --depth=1` 会丢弃只改工作树的改动,
所以修复以 `Code/plugins/patches/TradeTogether/0002-bound-the-peer-name-and-request-id-tables.patch`
交付(`0001` 只改 `src/main.cpp`,所以这一份的基线就是「`0001` 已应用」)。

### 77.5 复核命令(照抄)

```powershell
python Tools\Scripts\check_docs.py
python Code\plugins\tools\plugin_patches.py verify
python Code\plugins\tools\plugin_patches.py check
python Code\plugins\tools\plugin_snapshot.py verify
python Code\plugins\tools\check_transport_compat.py
git grep -n "kMaxPeerNameBytes\|kMaxNamedConnections" -- plugins/TradeTogether/src
git grep -n "kMaxRequestIDBytes\|kMaxRecentIncoming" -- plugins/TradeTogether/src
git grep -n "peerName->size()" -- plugins/TradeTogether/src/StrMessagingTransport.cpp
git -C plugins/TradeTogether apply --check --cached ..\..\Code\plugins\patches\TradeTogether\0002-bound-the-peer-name-and-request-id-tables.patch
git -C plugins/TradeTogether apply --check --reverse ..\..\Code\plugins\patches\TradeTogether\0002-bound-the-peer-name-and-request-id-tables.patch
```

复核结果:`check_docs.py` → `DOC CHECK OK` / `coverage 3069/3082 stated consistently in 4 doc(s)`;
`plugin_patches.py verify` → `PATCH VERIFY OK` / `23 patch(es) apply to their pinned commits`;
`check` → `PATCH CHECK OK`;`plugin_snapshot.py verify` → `SNAPSHOT OK` / `8 plugin(s) have a durable
copy matching the pinned commit`;`check_transport_compat.py` → `TRANSPORT COMPATIBILITY OK`
(`client opcodes: 53 in use` / `server opcodes: 56 in use`,**这一批没有动线格式**);
`kMaxPeerNameBytes` 命中 `StrMessagingTransport.cpp:19`/`:520`,`kMaxNamedConnections` 命中
`:20`/`:529`/`:262`;`kMaxRequestIDBytes` 命中 `Trade.cpp:26`/`:1069`,`kMaxRecentIncoming` 命中
`Trade.cpp:27`/`:757`;
最后两条 `git apply --check`:`--cached`(对 index = pin)exit 0,`--reverse`(对工作树)
exit 0 ⇒ 补丁既「对 pin 可应用」又「已经在工作树里」,这正是 `verify` 与 `check` 各自的判据;
不带 `--cached`/`--reverse` 的 `git apply --check` 对工作树**必然失败**
(`error: patch failed: src/StrMessagingTransport.cpp:11`),因为改动已经在里面。

**验证**:本机不编译(`docs/PITFALLS.md` §0/§1),所以这一批的证据是逐字命中 + 门禁 + 补丁对 pin 的可应用性,
不是编译产物。两处修改都不新增 include(`plugins/TradeTogether/src/PCH.h:16-34` 已有
`<chrono> <cstdint> <string> <unordered_map>`),`Trade.cpp` 里 `std::size_t` 此前零命中,新常量是首次使用。

> **教训**:第一条:**同一次解析里两个字段一个有上界一个没有,这就是判据。**
> `Trade.cpp` 的 `from`/`to` 早就有 256 上限,而同一个包里的 `request` 只有「非空」;
> 写代码时先问「这个字段的下游拿它当什么用」,再问「和它并列的字段都限了什么」。
> 第二条:**上限要加在写入它的那一层,不是消费它的那一层。**
> Trade 层的 256 校验发生在 transport 已经把键写进 `_namedConnections` 之后,
> 于是「下游会挡住」是一句空话 —— 挡住的只是下游的使用,不是上游的增长。
> 第三条:**「有回收路径」不等于「有上界」。**
> `recentIncoming` 有按龄清扫,看起来是自愈的;但 60 秒的窗口里行数完全由对端决定,
> 而清扫本身每帧都要遍历整张表 —— 回收路径的**周期**决定了窗口内能堆多高。

## 80. 2026-10-11 场次:中继的四个身份字段可以被对端抢注 —— 判据是「同一个名字,中继写在尾、对端写在头,而读取器取的是第一个」

> **锚点**:一处修复,在插件层,交付方式是补丁(插件是子模块,本仓不拥有、也不能 push):
> `Code/plugins/patches/STRPluginMessagingAPI/0003-read-the-relays-own-sender-fields-from-the-tail.patch`
> 改 `plugins/STRPluginMessagingAPI/src/STRPluginMessagingBridgeReceive.cpp`(新读取器 `:776-797`,
> 两个换名的调用点 `:854`/`:855`)、
> `plugins/STRPluginMessagingAPI/src/STRPMProxyResolverBridge.h`(旧 `ReadField` 被整体替换为
> `ReadRelayField` `:681-702`,两个调用点 `:882`/`:883`)、
> `plugins/STRPluginMessagingAPI/extras/str-server-resources/strpm-chat-relay/main.lua`
> (`RESERVED_FIELDS` `:13-17`、判定函数 `:88-95`、调用点 `:183-187`)与
> `plugins/STRPluginMessagingAPI/docs/OFFICIAL_STR_CHAT_TUNNEL.md:86-95`。
> 门禁在 `Code/plugins/tools/check_transport_compat.py`(新增一段,三部分)。

### 80.1 缺陷:同一个名字在信封里出现两次,而两端的「谁赢」是相反的

中继脚本每转发一封信封,都把服务端认证过的身份追加在**尾部**:

```lua
-- plugins/STRPluginMessagingAPI/extras/str-server-resources/strpm-chat-relay/main.lua(修复前 :180-185)
local relayEnvelope =
    message ..
    "|sender=" .. tostring(senderConnectionId) ..
    "|senderPlayerId=" .. tostring(senderPlayerId) ..
    "|senderName=" .. safeField(player:GetUsername()) ..
    "|serverTick=" .. tostring(gameServer:GetTick())
```

而 `message` 整段是发送方自己拼的(`plugins/STRPluginMessagingAPI/src/STRPluginMessagingBridge.cpp:866-876`、
`:895-899`)。中继脚本的字段表是**后者胜**:

```lua
-- main.lua(修复前 :30-41)
local function splitFields(envelope)
  local fields = {}
  ...
      fields[key] = value                                   -- :37,后出现的覆盖先出现的
```

接收侧桥的读取器是**前者胜**:

```cpp
// plugins/STRPluginMessagingAPI/src/STRPluginMessagingBridgeReceive.cpp(修复前 :758-774)
std::optional<std::string_view> ReadField(std::string_view packet, std::string_view key)
{
    ...
        if (token.size() > key.size() && token.starts_with(key) && token[key.size()] == '=')
            return token.substr(key.size() + 1);              // :769,第一个匹配就返回
}
```

| 位置 | 作用 |
| --- | --- |
| `src/STRPluginMessagingBridgeReceive.cpp:758-774`(修复前) | `ReadField` 取第一个匹配 —— 对端写在头部的副本胜出 |
| `src/STRPluginMessagingBridgeReceive.cpp:869`(修复前) | `pending.senderName = senderName ? … : fallbackPlayerName` 收下这个值 |
| `src/STRPluginMessagingBridgeReceive.cpp:936-937`(修复前) | `message.sender.connectionID` / `message.sender.displayName` 交给所有消费方 |
| `src/STRPMProxyResolverBridge.h:681-697`(修复前) | 同体的第二份 `ReadField` |
| `src/STRPMProxyResolverBridge.h:886`(修复前) | `ObserveSender(*connectionID, *playerId)` 把伪造值写进 `g_connectionToPlayer` |
| `extras/str-server-resources/strpm-chat-relay/main.lua:82-111`(修复前) | `validateEnvelope` 只看 `msg`/`seq`/`channel`/`payload`/`flags`/`part`/`parts`/`target`,**从不看字段集合** |

### 80.2 判据

- **这四个字段是谁写的?** 头部的 `message` 由发送方拼,`sender`/`senderPlayerId`/`senderName`/`serverTick`
  由中继追加 —— 一个字段有两个作者,而两者位置不同。
- **读的人取第一个还是最后一个?** 桥取第一个(`return token.substr`),`main.lua` 取最后一个
  (`fields[key] = value`)。中继自己不受影响,因为它**不读** `fields["sender"]`,
  它用的是 `player:GetConnectionId()` 与 `player:GetId()`(修复前 `:168-169`)—— 会话真值。
  受害者是**接收方客户端**。
- **抢注出来的值合法吗?** 合法。`senderName` 是一个合法字符串,`sender` 是一个合法连接号,
  信封仍然过 `validateEnvelope` —— 多一个 `|sender=` 不是畸形,只是没人问过。
- **谁能被冒充?** 所有以 displayName 为键的表:
  `plugins/MorphSyncTogether/src/UdpTransport.h:48`、`src/TngSync.h:58`、
  `src/MorphSyncService.h:130-134`、`plugins/IEDSyncTogether/src/STRPMAdapter.h:63`、
  `plugins/TradeTogether/src/StrMessagingTransport.cpp:517`。其中
  `plugins/MorphSyncTogether/src/MorphSyncService.cpp:1012` 的
  `if (!rawName || !EqualsInsensitive(rawName, name)) return;` 会把伪造的名字解析到
  **一个真实存在的代理角色**(`:1040-1042` 取最近的一个)⇒ 外观 / TNG / 交易的身份可以按名字冒充。
- **`payload` 也是保留字段吗?** 不是,而且**不能**一起翻。identity 通道
  (`main.lua:176-178`,修复前)用 `replacePayload(message, hexEncodeAscii(tostring(senderPlayerId)))`
  覆写的是**第一个** `|payload=`(修复前 `:70-80` 的 `string.gsub(..., 1)`),
  中继写进去的 PlayerId 就落在第一个上。把 `ReadField` 整体翻成后者胜,
  客户端在尾部补一个 `|payload=` 就能顶掉它 —— 「认证过的身份」会从另一个口子漏掉。

### 80.3 可达性

- 只要在**已连接的 STRPM 通道**上发一条以 `STRPM|v2|` 开头的聊天行即可,
  信封头部的字段顺序由发送方自己决定 ⇒ 在 `|channel=` 之前插一个 `|senderName=Someone` 不需要任何权限。
- 中继不拒(修复前 `validateEnvelope` 不看字段集合)、桥不拒(第一个匹配就返回),
  于是伪造名一路到 `message.sender.displayName`。
- 第二处(`STRPMProxyResolverBridge.h`)走的是 `ObserveIncomingEnvelope`,
  它在 `OnConsume` 的异常处理里被调用(`:942-951`,`EXCEPTION_BREAKPOINT` + `address == g_onConsumeAddress`),
  `return EXCEPTION_CONTINUE_SEARCH` ⇒ 伪造的 `sender`/`senderPlayerId` 会污染 `g_connectionToPlayer`(`:773`)。
- 第三个消费者 `src/STRPluginMessagingBridgeSKSE.cpp:83-120` 的 `ReceiveIdentityAnnouncement`
  在 `:94` 调 `ObserveSender(message->sender.connectionID, playerId)`:
  **载荷**里的 PlayerId 是中继替换过的真值,而**绑定用的 connectionID** 来自可抢注的 `sender` 字段 ——
  「authenticated identity payload」只认证了一半。

### 80.4 修法

一、保留字段单独按「尾部」读,其余字段一个都不动:

```cpp
// plugins/STRPluginMessagingAPI/src/STRPluginMessagingBridgeReceive.cpp:776-797(修复后)
// The relay appends its own sender metadata to the tail of every envelope it
// forwards, and the part in front of that tail comes from a peer. ReadField
// returns the first match, so a peer that writes |sender=... early keeps it and
// the relay's own value is never read. Read the relay's fields from the tail.
std::optional<std::string_view> ReadRelayField(std::string_view packet, std::string_view key)
{
    std::optional<std::string_view> found;
    ...
        if (token.size() > key.size() && token.starts_with(key) && token[key.size()] == '=')
            found = token.substr(key.size() + 1);             // 记下最后一个,循环结束才返回
    ...
    return found;
}
```

桥里只有两处换名 —— 另外七个字段(`msg`/`channel`/`seq`/`flags`/`part`/`parts`/`payload`)
都是客户端写、中继不改的,仍走 `ReadField`:

```cpp
// plugins/STRPluginMessagingAPI/src/STRPluginMessagingBridgeReceive.cpp:853-855(修复后)
const auto channel = ReadField(envelope, "channel");
const auto sender = ReadRelayField(envelope, "sender");
const auto senderName = ReadRelayField(envelope, "senderName");
```

`src/STRPMProxyResolverBridge.h` 里两个读取点**都**是保留字段,没有客户端字段的读者,
所以原来那份 `ReadField` 被整体替换成 `ReadRelayField`(`:681-702`),
`ObserveIncomingEnvelope` 的两行换成 `ReadRelayField(envelope, "sender")` / `"senderPlayerId"`(`:882-883`)。

二、中继自己判一次 —— 这一条挡的是**转发**,上面两条挡的是**读取**:

```lua
-- main.lua:13-17(修复后)
local RESERVED_FIELDS = { "sender", "senderPlayerId", "senderName", "serverTick" }
...
-- main.lua:88-95(修复后)
local function reservedFieldPresent(fields)
  for _, name in ipairs(RESERVED_FIELDS) do
    if fields[name] ~= nil then
      return name
    end
  end
  return nil
end
...
-- main.lua:183-187(修复后),在追加字段之前、在 validateEnvelope 之后
  local reserved = reservedFieldPresent(fields)
  if reserved ~= nil then
    print("[STRPM] Dropped envelope carrying reserved field " .. reserved)
    return
  end
```

三、子模块文档 `docs/OFFICIAL_STR_CHAT_TUNNEL.md:86-95` 补上漏写的 `senderPlayerId`,
并写明这四个名字是保留名、中继会拒收已带保留名的信封、桥从尾部读、头部由对端写。

四、交付:四个文件都在 `plugins/STRPluginMessagingAPI` 子模块里,而 CI 的
`git submodule update --init --force --recursive --depth=1` 会丢弃只改工作树的改动,
所以修复以 `Code/plugins/patches/STRPluginMessagingAPI/0003-read-the-relays-own-sender-fields-from-the-tail.patch`
交付(7076 字节 / 156 行 LF / 9 个 hunk;基线是「0001 与 0002 已应用」,
因为 0002 也改这个子模块的 `src/`)。

### 80.5 复核命令(照抄)

```powershell
python Code\plugins\tools\check_transport_compat.py
python Code\plugins\tools\plugin_patches.py verify
python Code\plugins\tools\plugin_patches.py check
python Tools\Scripts\check_docs.py

# 两个读取器的当前形态(前者胜 / 后者胜)
git grep -n "ReadRelayField\|found = token.substr" -- plugins/STRPluginMessagingAPI/src
# 保留字段的三处落点:声明、判定函数、调用点(调用点必须在追加之前)
git grep -n "RESERVED_FIELDS\|reservedFieldPresent\|reserved ~= nil" -- plugins/STRPluginMessagingAPI/extras

# 补丁对 pin 可应用、且已经在工作树里
git -C plugins/STRPluginMessagingAPI apply --check --cached ..\..\Code\plugins\patches\STRPluginMessagingAPI\0003-read-the-relays-own-sender-fields-from-the-tail.patch
git -C plugins/STRPluginMessagingAPI apply --check --reverse ..\..\Code\plugins\patches\STRPluginMessagingAPI\0003-read-the-relays-own-sender-fields-from-the-tail.patch
```

**验证**:本机不编译(`docs/PITFALLS.md` §0/§1),所以这一批的证据是逐字命中 + 门禁 + 反向验证,
不是编译产物。`check_transport_compat.py` 新增的一段是三部分:三个文件缺一即失败;
`main.lua` 必须声明 `RESERVED_FIELDS` 且**真的调用** `reservedFieldPresent(fields)`
(必须匹配带 `= ` 的调用行 —— 只匹配 `reservedFieldPresent(fields)` 会命中函数定义而漏判,
这是第一轮反向验证没报错的原因)且调用位置早于 `"|sender="` 追加行;
两个 C++ 文件的 `ReadRelayField` 函数体必须含 `found = token.substr` 且不得含 `return token.substr`。
反向验证过两轮:把桥的 `found = token.substr(...)` 改回 `return token.substr(...)` ⇒
`TRANSPORT COMPATIBILITY FAILED (1)` + 「ReadRelayField returns the first match again; the peer writes
the head of the envelope and the relay writes the tail, so the first match is the forged one」;
把 `local reserved = reservedFieldPresent(fields)` 改成 `local reserved = nil` ⇒
`TRANSPORT COMPATIBILITY FAILED (1)` + 「declares RESERVED_FIELDS but never calls reservedFieldPresent,
so a reserved field in a peer envelope is forwarded unexamined」;两次恢复后回到 exit 0。
补丁按 0001 → 0002 → 0003 的顺序对 pin `62143fd` 全部应用成功,工作树 9 个 ` M`。

> **教训**:第一条:**同一个名字可以在一份报文里出现两次,而「谁赢」是两个相反的实现细节。**
> 中继的 Lua 表是后者胜,接收侧桥的读取器是前者胜 —— 两个都对,合起来就是可抢注。
> 定这类字段时先问「这个字段是**谁写的**、写在哪一段」,再问「读的人取第一个还是最后一个」。
> 第二条:**「服务端认证过了」不等于「客户端读到的就是认证值」。**
> 中继确实把真值追加进去了,认证没有失效;失效的是**读取**,而读取发生在别人的机器上。
> 第三条:**修一个「取第一个」的读取器时,要看同一个函数里有没有别的字段依赖「取第一个」。**
> 这里 `payload` 就依赖 —— 翻整体的方向会把 identity 通道的认证从另一个口子放走。

## 81. 2026-10-11 场次:Morph 的三张表,键由对端选、键长也由对端选,而唯一的清理点是读档 —— 判据是「这个键是谁选的,而它什么时候被删掉」

> **锚点**:一处修复(三张表),在插件层,交付方式是补丁(插件是子模块,本仓不拥有、也不能 push):
> `Code/plugins/patches/MorphSyncTogether/0005-bound-the-peer-supplied-probe-keys-and-the-sender-name-table.patch`
> 改 `plugins/MorphSyncTogether/src/UdpTransport.cpp`(常量 `:14-21`,写入点 `:233-252`)、
> `plugins/MorphSyncTogether/src/AppearanceProbe.h`(常量 `:172-179`)、
> `plugins/MorphSyncTogether/src/AppearanceProbe.cpp`(键长门禁 `:727-732`、
> 快照表写入点 `:755-763`、外观缓存写入点 `:1184-1192`)。
> 三者由 `Code/plugins/tools/plugin_patches.py apply` 在 CI 配置阶段应用
> (`.github/workflows/plugins.yml:150`、`windows.yml:208`)。

### 81.1 缺陷:三张表,三个由对端填的键,而唯一的清理点在读档

第一张在 `UdpTransport`,键是**显示名**:

```cpp
// plugins/MorphSyncTogether/src/UdpTransport.cpp(修复前)
std::string sender = message.sender.displayName && *message.sender.displayName ?
    message.sender.displayName :                                  // :219-220,对端自报
    fmt::format("connection-{}", message.sender.connectionID);
sender = SanitizeField(std::move(sender));                        // :222
{
    std::scoped_lock lock(_senderMutex);
    _senderConnections[sender] = message.sender.connectionID;      // :226,每次收包无条件写
}
```

第二、三张在 `AppearanceProbe`,键是显示名加一个**临时的 proxy FormID**:

```cpp
// plugins/MorphSyncTogether/src/AppearanceProbe.cpp(修复前)
Probe(fmt::format("REMOTE:{}:{:08X}", sender, actor->GetFormID()), // :483-488
      "REMOTE", sender, actor, nullptr);
...
auto& state = _snapshots[key];                                    // :748
...
auto& cache = _appearanceCaches[key];                             // :1169
```

| 位置 | 作用 |
| --- | --- |
| `plugins/MorphSyncTogether/src/UdpTransport.cpp:219-222`(修复前) | 键 = 对端自报的 `displayName`,否则 `connection-<id>` |
| `plugins/MorphSyncTogether/src/UdpTransport.cpp:26-37`(修复前) | `SanitizeField` 只把 `|`/CR/LF 换成 `_`、空则 `"Player"` —— **没有长度上限** |
| `plugins/MorphSyncTogether/src/UdpTransport.cpp:226`(修复前) | 每次收包 `_senderConnections[sender] = …`,无 `contains` 判定、无行数上限 |
| `plugins/MorphSyncTogether/src/UdpTransport.cpp:163-166`(修复前) | 全文件唯一的 `_senderConnections.clear()`,在 `Stop()` 里 |
| `plugins/MorphSyncTogether/src/AppearanceProbe.cpp:748`/`:1169`(修复前) | 两张表的 `operator[]` 插入点 —— 全文件仅有的两个 |
| `plugins/MorphSyncTogether/src/AppearanceProbe.cpp:458-464`(修复前) | 唯一的清理点 `Reset()`,两个 `clear()` |

### 81.2 判据

1. **这个键是谁选的?** 两张表的键里都有对端的 `displayName`。
   `UdpTransport.cpp:219-220` 直接取 `message.sender.displayName`(STRPM 消息头,由对端填);
   `AppearanceProbe.cpp:483-488` 的 `sender` 来自 `MorphSyncService.cpp:337-346` 收集的
   `senderSet` —— 那是两张远端快照表的键,同样是显示名。
2. **这个键的长度谁管?** 没人。`SanitizeField`(`:26-37`)只做字符替换,不截断;
   而它的返回值在 `:234-238` 被拼进 `"{}from={}|{}"` —— 一个 100 KB 的名字会让这个包也变成 100 KB,
   但**写进表里的那一条**才是留下来的东西:名字越长,它越不可能再被匹配上,却照样占着一行。
3. **这张表什么时候变小?** `_senderConnections` 只有 `Stop()` 会清,而 `Stop()` 的唯一调用者是
   `~UdpTransport()`(`:21-24`);`src/main.cpp:36` 只调 `Start()`,
   `:40-43` 的 `kPreLoadGame` 只重置 `MorphSyncService` 与 `TngSync`
   ⇒ **进程起来之后它永远不清**。两张探针表只有 `Reset()` 会清,调用者是
   `MorphSyncService.cpp:245`(同样是 `kPreLoadGame`)⇒ 只在读档时清。
4. **新的键会被节流挡住吗?** 不会。`AppearanceProbe.cpp:730-731` 的节流门禁是
   `const auto it = _snapshots.find(key); if (it != _snapshots.end() && …)` ——
   一个**从没见过的**键一定走到 `operator[]`。节流保护的恰恰不是这条路径。
5. **贵在哪一步?** `MorphSyncService.cpp:337-354` 每 tick 把两张表的键并成一个
   `std::unordered_set<std::string>` 再逐键走 `ResolveRemoteProxyByName`
   (`:981-1045`:ProcessLists 全表扫描 + `EqualsInsensitive` + `GetDistance`)。
   这不是「内存慢慢涨」,是**每秒一次的 O(表大小 × 演员数)扫描**。
6. **满表 = 一次 miss,不是失败。** 三张表都是「上一包是什么」的缓存:丢一包等于等下一包,
   名字配不上任何 proxy 本来也就是 `nullptr`。所以设界不改变行为。

### 81.3 可达性

**同一个对端**,每包换一个 `displayName`(或干脆留空,让 `connection-<id>` 当键、每包换一个 connection id)。
每包插一行,没有回收;探针那两张表还会在每次 `TickOnGameThread` 上被遍历一遍。
`_assemblies` 是同一个文件里唯一有 prune 的表(`MorphSyncService.cpp:356-366`,
`// Drop incomplete chunk assemblies after 15 seconds.`),而它旁边的表没有 ——
这正是判据的形状:**同一把清理里,谁缺席**。

### 81.4 修法

一、`UdpTransport` 加两个常量,写入前先判键长、再判行数(已存在的键仍可更新):

```cpp
// plugins/MorphSyncTogether/src/UdpTransport.cpp:14-21(修复后)
// The sender name is a display name the remote side supplies, and the only
// thing that ever removes a row is Stop(), which nothing calls after Start(),
// so both the key length and the row count are bounded here. ...
constexpr std::size_t kMaxSenderNameBytes = 256;
constexpr std::size_t kMaxSenderConnections = 64;
```

```cpp
// plugins/MorphSyncTogether/src/UdpTransport.cpp:233-252(修复后)
if (sender.size() > kMaxSenderNameBytes) {
    SKSE::log::warn("MST STRPM RX dropped: sender name too long bytes={}", sender.size());
    return;
}
{
    std::scoped_lock lock(_senderMutex);
    if (!_senderConnections.contains(sender) &&
        _senderConnections.size() >= kMaxSenderConnections) {
        SKSE::log::warn(
            "MST STRPM RX dropped: sender table full size={} sender=\"{}\"",
            _senderConnections.size(), sender);
        return;
    }
    _senderConnections[sender] = message.sender.connectionID;
}
```

二、`AppearanceProbe` 同理,三个常量(键长一份、两张表各一份上限):

```cpp
// plugins/MorphSyncTogether/src/AppearanceProbe.h:172-179(修复后)
static constexpr std::size_t kMaxProbeKeyBytes = 256;
static constexpr std::size_t kMaxProbeSnapshots = 256;
static constexpr std::size_t kMaxAppearanceCaches = 128;
```

键长门禁放在 `Probe()` 最前(`AppearanceProbe.cpp:727-732`,在取 `now` 之前),
两张表的行数上限各自贴在自己的 `operator[]` 前面(`:755-761` 与 `:1184-1190`)。
三处都是「满了丢这一次采样」,不是「清表」。

三、交付:三个文件都在 `plugins/MorphSyncTogether` 子模块里,而 CI 的
`git submodule update --init --force --recursive --depth=1` 会丢弃只改工作树的改动,
所以修复以 `Code/plugins/patches/MorphSyncTogether/0005-bound-the-peer-supplied-probe-keys-and-the-sender-name-table.patch`
交付(4838 字节 / 113 行 LF / 6 个 hunk / 3 个文件;基线是「0001 到 0004 已应用」,
因为 0001 与 0004 也改这个子模块的 `src/`)。

**注意这两张表为什么能改对**:编译闭包陷阱在这里是**反着的** ——
`src/AppearanceProbe.cpp` 与 `src/MorphSyncService.cpp` 都不在
`plugins/MorphSyncTogether/CMakeLists.txt` 的 `SOURCES` 里,
它们是被 `src/AppearanceProbeBodyHair.cpp:9` / `src/MorphSyncServiceBodyHair.cpp:11` 用
`#include "…cpp"` 编进去的(前者还先 `#define` 了两个名字,见 `0004` 的五个目标文件)。
所以改 `AppearanceProbe.cpp` 是有效的,而 §78 那条门禁(`plugin_patches.py:99-211`)
正是为了确认这一点才存在 —— 它现在能从 `AppearanceProbeBodyHair.cpp` 顺着 `#include` 走到 `AppearanceProbe.cpp`。

### 81.5 复核命令(照抄)

```powershell
python Code\plugins\tools\plugin_patches.py verify
python Code\plugins\tools\plugin_patches.py check
python Tools\Scripts\check_docs.py

# 三张表的常量与写入点(每张表都应当有一个 contains 判定紧挨着 operator[])
git grep -n "kMaxSenderNameBytes\|kMaxSenderConnections" -- plugins/MorphSyncTogether/src/UdpTransport.cpp
git grep -n "kMaxProbeKeyBytes\|kMaxProbeSnapshots\|kMaxAppearanceCaches" -- plugins/MorphSyncTogether/src/AppearanceProbe.h
git grep -n "_snapshots\[\|_appearanceCaches\[\|_senderConnections\[" -- plugins/MorphSyncTogether/src

# 补丁对 pin 可应用、且已经在工作树里
git -C plugins/MorphSyncTogether apply --check --cached ..\..\Code\plugins\patches\MorphSyncTogether\0005-bound-the-peer-supplied-probe-keys-and-the-sender-name-table.patch
git -C plugins/MorphSyncTogether apply --check --reverse ..\..\Code\plugins\patches\MorphSyncTogether\0005-bound-the-peer-supplied-probe-keys-and-the-sender-name-table.patch
```

**验证**:本机不编译(`docs/PITFALLS.md` §0/§1),所以证据是逐字命中 + 门禁 + 顺序应用,不是编译产物。
`plugin_patches.py verify` 从 24 个补丁变成 25 个并全绿;该补丁的目标三个文件都能从
`AppearanceProbeBodyHair.cpp` / `MorphSyncServiceBodyHair.cpp` 的 `#include` 链到达
(§78 那条门禁 `uncompiled_patch_targets` 报的是「文件不在构建闭包里」,它没报 ——
反过来说明闭包走得通)。按 0001 → 0002 → 0003 → 0004 → 0005 的顺序对 pin `017c7b0c` 全部应用成功,
工作树 13 个 ` M`。

> **教训**:第一条:**「键由对端选」要连着问「键有多长」,因为这两件事通常由同一段代码负责 —— 而它只做了其中一件。**
> `SanitizeField` 看起来像一道净化,它净化的却是**分隔符**,不是长度。
> 第二条:**一个只增不减的表,要看它唯一的清理点是谁在什么时候调。**
> 这里两张表的答案是「读档」,第三张表的答案是「析构」—— 而那个析构函数在进程起来之后再也不会被调用。
> 判据不是「有没有清理代码」,是「这行清理跑得到吗」。
> 第三条:**节流不等于上限。** `Probe()` 的节流门禁查的是「这个键在不在表里」,
> 所以它对**新键**完全无效 —— 越是攻击者想要的路径,越是节流管不着的那条。

## 82. 2026-10-12 场次:一个 cell 里第 256 个之后的容器与门永远不同步 —— 判据是「写侧夹住了计数,那被夹掉的那些谁负责」

> **锚点**:一处修复(客户端一个发送点 + 两个常量上移),以及它旁边那个**不用改**的服务端:
> `Code/encoding/Messages/AssignObjectsRequest.h:24-27`、`Code/encoding/Messages/AssignObjectsResponse.h:23-26`
> (常量从两个 `.cpp` 的 file-scope `static` 上移为消息类的 `static constexpr size_t kMaxObjectCount = 0xFF;`)、
> `Code/client/Services/Generic/ObjectService.cpp:197-201`(切片闸)与 `:206-207`(收尾不再无条件发)、
> `Code/encoding/Messages/AssignObjectsRequest.cpp:4` / `AssignObjectsResponse.cpp:4`(删掉各自的常量与注释)。
> 测试在 `Code/tests/encoding.cpp:1462-1537`(`[encoding.assign_objects]`)。

### 82.1 缺陷:写侧夹住了计数,而超出的那部分没有任何人负责

线上字段是 8 位,所以编码层夹住它 —— 这一段本身没有错:

```cpp
// Code/encoding/Messages/AssignObjectsRequest.cpp(修复前 :7-15)
static constexpr size_t kMaxObjectCount = 0xFF;   // :7  file-scope,只有写侧看得见

const auto count = std::min<size_t>(Objects.size(), kMaxObjectCount);   // :11
aWriter.WriteBits(count, 8);                                            // :12

for (size_t i = 0; i < count; ++i)                                      // :14 只写 count 条
    Objects[i].Serialize(aWriter);
```

包永远是合法的:计数与实际写出的条目数一致,读侧消费 `count` 条之后游标正好落在包尾。
问题在于**发送侧不知道有这道夹取**:

```cpp
// Code/client/Services/Generic/ObjectService.cpp(修复前)
Vector<TESObjectREFR*> objects = pCell->GetRefsByFormTypes(formTypes);   // :160 全量
AssignObjectsRequest request{};
for (TESObjectREFR* pObject : objects)
{
    ...
    request.Objects.push_back(objectData);                               // :194 只 push
}
m_transport.Send(request);                                               // :197 只发一次
```

超过 255 条的那些既没进这个包,也没有第二个包。

| 位置 | 修复前 |
| --- | --- |
| `AssignObjectsRequest.cpp:7` | `kMaxObjectCount` 是 file-scope `static`,发送侧看不见这个约定 |
| `ObjectService.cpp:194` | 唯一的 `push_back`,不判满 |
| `ObjectService.cpp:197` | 唯一的 `Send`,循环外一次,不问多少条 |
| `AssignObjectsResponse.cpp:7` | 同一个常量抄了第二份,而它的生产者不可能超过请求侧 |

### 82.2 判据:「写侧夹住了计数」之后要问「被夹掉的那部分谁负责」

这一族里有两种形状,只有第二种会炸:

- **写侧与读侧不一致**(§49 修的那一类):写侧只写计数低位、循环却写全部条目 ⇒ 读侧少消费、
  剩余字节被当下一条消息解析 ⇒ **整条流失步**,表现为「连不上」「同步乱掉」。
- **写侧与读侧一致,只是内容没全进去**(本节):包合法、流不失步,丢的是**服务端从没听说过的那几个对象**。

第二种之所以无声,是因为两端都把「没有它」当成正常状态:
客户端 `Code/client/Services/Generic/ObjectService.cpp:200-243` 逐条 `GetGameId` + `Cast<TESObjectREFR>`,
命中才 `CreateObjectEntity`,不命中本来就不建本地映射 —— 于是「服务端拒绝了它」与「这个对象根本不存在」
在客户端看来是同一件事。服务端那边,这个对象从来没有被 `m_world.create()` 过,连一行日志都不会有。

判据要问的是:**这个上限是谁定的,超出它的那部分由谁负责。**
这里定上限的是编码层(它只对「这个字段装不下」负责),超出的那部分是发送方(它连上限是多少都不知道),
两边互不知情,中间就是那段静默。

### 82.3 可达性

`pCell->GetRefsByFormTypes({FormType::Container, FormType::Door})` 返回的是 cell 里**全部**容器与门。
`docs/PITFALLS.md:9823`(§76)自己写着「任何真实 cell 的容器与门都在几十到几千量级」,
`CHANGELOG.md` 里同一段的估算是「约 256 KiB,远大于任何真实 cell 的容器与门」——
也就是说 255 这个数看着绰绰有余,而「几千」那一半正是它够不着的地方。

触发条件不是攻击,是**正常游玩**:进一个容器/门较多的 cell(大型室内、主城某个区域、
装了增加容器与门的 mod 的 cell),第 256 个之后的门锁不上、容器内容两边不一致,
而且每次重进都是同样一批 —— 因为顺序稳定,被丢的永远是最靠后的那一批。

### 82.4 修法

一、常量上移到两个消息头,让发送侧能读到同一个数:

```cpp
// Code/encoding/Messages/AssignObjectsRequest.h:24-27(修复后)
// The count field is 8 bits wide, so a well behaved sender can never exceed this.
// The writer clamps to it, which means anything past it would be dropped without a
// word, so a sender that has more to say sends another packet instead.
static constexpr size_t kMaxObjectCount = 0xFF;

Vector<ObjectData> Objects{};
```

形状照 `Code/encoding/Structs/Tints.h:30-33`(常量在成员之前)与
`Code/encoding/Messages/ClientReferencesMoveRequest.h:27-30`(常量在成员之后)—— 两种都在仓内。
`AssignObjectsResponse.h:23-26` 同样上移(它的读者是测试与将来可能的分片)。

二、发送侧按同一个数切片,并且**只在有东西的时候才发**:

```cpp
// Code/client/Services/Generic/ObjectService.cpp:197-207(修复后)
if (request.Objects.size() == AssignObjectsRequest::kMaxObjectCount)
{
    m_transport.Send(request);
    request.Objects.clear();
}

request.Objects.push_back(objectData);
...
if (!request.Objects.empty())
    m_transport.Send(request);
```

判满放在 `push_back` **之前**(与 `StringCacheUpdate` 的切片同形),
收尾的 `!empty()` 是必须的:一个空 cell、或整个 cell 的对象都被 `ShouldSyncObject` 过滤掉时,
原来的无条件 `Send` 会送出一个 `Objects` 为空的合法包。

三、**服务端不改**,理由是关系式而不是直觉:

`response.Objects` 只在该 handler 的两处 push —— 命中分支 `Code/server/Services/ObjectService.cpp:98`、
新建分支 `:123`,拒绝分支 `:100-103` 不 push ⇒ `response.Objects.size() <= acMessage.Packet.Objects.size()`。
请求侧切片之后,响应侧永远到不了 255,`AssignObjectsResponse` 的夹取再也触发不到。
**一次改动同时修好两个方向,但只改生产者的那一侧。**

`Code/client/Services/Generic/TransportService.cpp:150-178 Send` 每包一次 `Buffer buffer(1 << 16);`,
所以切片把每 cell 的发送次数从 1 提到 `⌈N/255⌉`;255 条 `ObjectData` 的实际字节数远小于 64 KiB,
这个放大可以忽略。

### 82.5 复核命令(照抄)

```powershell
python Tools\Scripts\check_docs.py

# 常量只有一个出处,且发送侧真的在读它
git grep -n "kMaxObjectCount" -- Code

# 唯一的 push_back 与唯一的 Send 之间应当有一个判满分支
git grep -n "request.Objects.push_back\|m_transport.Send(request)" -- Code/client/Services/Generic/ObjectService.cpp

# 服务端只有两处 push,且都在请求条目的循环里
git grep -n "response.Objects.push_back" -- Code/server/Services/ObjectService.cpp
```

**验证**:本机不编译(`docs/PITFALLS.md` §0/§1),所以证据是逐字命中 + 用例,不是编译产物。
`Code/tests/encoding.cpp:1462-1537` 的 `[encoding.assign_objects]`:断言 `kMaxObjectCount == 0xFF`;
造 258 条 `ObjectData`,第一片 255 条往返后 `Objects.size() == kMaxObjectCount` 且 `back().Id` 是第 255 条,
第二片 3 条往返后 `back().Id` 是最后一条(即「原来会被丢掉的那三条现在真的到了」);
最后把 258 条整批交给一个请求,断言写侧仍只发 255 条、尾条是第 255 条 ——
把「发送侧必须切片」这个前提钉在用例里,删掉切片这条断言就会红。

> **教训**:第一条:**「写侧夹住了计数」不等于「被夹掉的东西有人管」。**
> 夹取让包合法,而**合法正是它不报错的原因** —— 判据要问「这个上限是谁定的,超出它的那部分由谁负责」。
> 第二条:**同一个约定出现在两个文件里时,它迟早会变成两个各自为政的字面量。**
> 常量留在 `.cpp` 的 file-scope `static` 里,是因为当时只有写侧一个读者;
> 发送侧要按它切片时,它就必须上移到消息头 —— 而「上移」这个动作本身就是「多了一个读者」的证据。
> 第三条:**修一侧就够的时候,把「为什么另一侧不用改」的关系式写出来。**
> `response.size() <= request.size()` 这句话就是「不用对称地改两遍」的全部理由;写不出这句话,就该两边都改。

## 83. 2026-10-13 场次:字符串缓存的起点是对端填的字段,而它被拿去喂 assert —— 判据是「这个断言的实参是谁选的」

> **锚点**:一处修复,没有别的文件需要跟着改:
> `Code/encoding/StringCache.cpp:76-104`(`Deserialize` 的两个 `assert` 换成「不连续就丢弃这条消息」)、
> `Code/encoding/StringCache.cpp:55-59`(写侧那句「the receiver asserts that the ids are contiguous」同步改掉)、
> `Code/encoding/Messages/StringCacheUpdate.cpp:23`(实参的来源:`StartId` 是对端填的字段)、
> `Code/encoding/StringCache.cpp:26-36`(`Add` 在值已存在时早退,这是「丢弃」不破坏不变量的依据)、
> `Code/client/Services/Generic/StringCacheService.cpp:28-31`(唯一调用点,它无法回报失败)、
> `Code/client/Services/Generic/TransportService.cpp:110-123`(`handlerGenerator`,决定这段代码在**游戏线程**上跑)、
> `Code/encoding/Structs/CachedString.cpp:44-52`(同族先例,判据与修法都来自它)。
> 测试在 `Code/tests/encoding.cpp:575-647`(`[encoding.string_cache_selfheal]`)。
> 就地更正:`docs/PITFALLS.md` §49.2 的判据表与判据段、§49.3 的代码块引文、§73 的排除语 ——
> 这三处都曾把同一段代码当「正确的不变量断言」引用。

### 83.1 缺陷:断言校验的是对端填的字段,失败就终止进程

```cpp
// Code/encoding/StringCache.cpp(修复前 :75-87)
void StringCache::Deserialize(const StringCacheUpdate& aMessage) noexcept
{
    // We should only receive contiguous updates
    assert(aMessage.StartId == m_idToString.size());          // :78

    for (auto& value : aMessage.Values)
    {
        const auto expectedId = m_idToString.size();
        auto id = Add(value);

        assert(id == expectedId);                             // :85
    }
}
```

两个实参都不是本地选的。`StartId` 是包里的字段:

```cpp
// Code/encoding/Messages/StringCacheUpdate.cpp:23
    StartId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
```

而 `Add` 的返回值也不总是「新分配的 id」—— 值已经在缓存里时它**早退**并返回旧 id:

```cpp
// Code/encoding/StringCache.cpp:26-36
uint32_t StringCache::Add(const TiltedPhoques::String& acValue) noexcept
{
    if (auto id = this->operator[](acValue))
        return *id;                                          // 早退,不触碰任何容器

    const auto allocatedId = m_idToString.size();
    m_stringToId[acValue] = allocatedId & 0xFFFFFFFF;
    m_idToString.push_back(acValue);

    return allocatedId;
}
```

于是 `:85` 那条断言在「同一条消息里出现重复值」时也会失败 —— 而重复值是一条**完全合法**的消息。

| 位置 | 修复前 | 后果 |
| --- | --- | --- |
| `StringCache.cpp:78` | `assert(aMessage.StartId == m_idToString.size());` | 对端填一个不等于本地长度的 `StartId` ⇒ 接收方进程终止 |
| `StringCache.cpp:85` | `assert(id == expectedId);` | 一条含重复值的消息 ⇒ 同上 |
| `StringCache.cpp:55-59` | 写侧注释「the receiver asserts that the ids are contiguous」 | 注释描述的是一个会把进程杀掉的契约,而它看起来像文档 |

### 83.2 判据:断言的实参必须是自己算出来的,否则它不是断言而是攻击面

同族先例已经把判据写死了 —— `Code/encoding/Structs/CachedString.cpp:44-52` 的九行注释:

> A peer only writes an id for a string it believes we already hold, so an id outside our cache
> means the two caches disagree - not that this code is wrong. It used to assert(false), which
> aborts the process: this frame is noexcept, so there is no way to fail softly, and the input is
> a packet any peer can send (the default build is releasedbg, where the assert is live).

把它拆成可机械执行的三条:

- **实参来源**:`assert` 的每一个实参要么由本地计算、要么由本地校验过。`StartId` 两者都不是。
- **失败路径**:所在函数是 `noexcept` ⇒ 断言失败不是「这条消息丢了」,是 `std::terminate`。
- **构建配置**:全仓 `NDEBUG` **零命中**,CI 默认 `build-mode: releasedbg` ⇒ 断言在发布构建里也是活的。

这三条同时成立,`assert` 就等价于「任何一个对端都能让本进程退出」。

### 83.3 可达性:从收到包到断言,中间没有任何一道门

- `Code/encoding/Opcodes.h:103 kStringCacheUpdate` 是服务端消息,`Code/encoding/Messages/ServerMessageFactory.h:45`/`:73` 注册了它。
- `Code/client/Services/Generic/TransportService.cpp:110-123` 的 `handlerGenerator` 对 `ServerMessageFactory::Visit` 的每个类型注册:
  `m_messageHandlers[T::Opcode] = [this](UniquePtr<ServerMessage>& apMessage){ const auto pRealMessage = TiltedPhoques::CastUnique<T>(std::move(apMessage)); m_dispatcher.trigger(*pRealMessage); };`
  ⇒ 消息一到就 `trigger`,**在游戏线程上**。
- `Code/client/World.cpp:19`/`:56` 注册 `StringCacheService`,它订阅 `StringCacheUpdate`。
- `Code/client/Services/Generic/StringCacheService.cpp:28-31`:

```cpp
void StringCacheService::HandleStringCacheUpdate(const StringCacheUpdate& acMessage) noexcept
{
    StringCache::Get().Deserialize(acMessage);
}
```

⇒ 没有长度判断、没有认证门槛(字符串缓存包在认证之后,但**任何已连接的对端**都能发),
也没有返回值可供上层判断成败 —— 所以「失步」必须在缓存自己内部收敛,不能靠调用方处理。

`Code/client/Services/StringCacheService.h:10` 写着「This service is currently not in use.」,
但服务**仍然注册、仍然订阅**,所以那句话不能当作「不可达」的理由(§73 已就同类误判更正过一次)。

### 83.4 修法:不连续就丢弃这条消息,缓存原样保留

```cpp
// Code/encoding/StringCache.cpp(修复后 :76-104)
void StringCache::Deserialize(const StringCacheUpdate& aMessage) noexcept
{
    // The sender numbers these entries with its own cursor, and that cursor is a field of
    // the packet: nothing here chose it and nothing here checks it. It used to be an
    // assert, which aborts the process - this frame is noexcept, so there is no way to
    // fail softly, and the default build is releasedbg, where the assert is live. It is
    // the same defect CachedString.cpp had: an id the peer picked, tested with an assert.
    //
    // A message that does not line up is dropped instead. Both checks sit before the
    // mutation they guard, so returning leaves a cache that is still a contiguous prefix
    // of the sender's: the entries already applied landed at exactly their own ids, and
    // Add returns early without touching anything when the value is already known. An id
    // the cache does not hold still reads as "not one of the names I know" - the consumers
    // of a cached name already test it against a fixed list and fall through when it
    // matches nothing.
    if (aMessage.StartId != m_idToString.size())
        return;

    for (auto& value : aMessage.Values)
    {
        const auto expectedId = m_idToString.size();
        const auto id = Add(value);

        // A value that is already in the cache reuses its old id, so a duplicate inside
        // one message does not land at the end either. That is the same disagreement.
        if (id != expectedId)
            return;
    }
}
```

写侧那句注释同步改成描述**现在的**契约(`Code/encoding/StringCache.cpp:55-59`):

```cpp
        // One message carries at most kMaxValueCount entries, and the receiver
        // requires the ids to be contiguous: a message whose start id does not line
        // up is dropped. Send a slice and advance the cursor by exactly what was
        // written, so the rest goes out on the next call instead of being skipped
        // and leaving the two caches out of step.
```

**为什么是「丢弃」而不是「清空缓存」**:第一版写的是 `Clear()`(清空 ⇒ 后续每个 id 都查不到 ⇒
已修的 `CachedString::Deserialize` 分支把名字置空),测试也按那个方向写好了。改判的理由是代价:
服务端**每 2000 ms** 才推一次增量(`Code/server/Services/StringCacheService.cpp:17 constexpr auto cDelayBetweenSnapshots = 2000ms;`),
清空会把一次失步放大成**两秒的同步空洞**;而丢弃只损失这一条。

**为什么丢弃是安全的**:`m_idToString` 在任何 `return` 点上都仍是发送侧缓存的一个**连续前缀**。
依据只有两条 —— `StartId` 的判断在**任何变更之前**;`Add` 在值已存在时**早退且不触碰任何容器**。
两条都写在上面那段代码里,所以「下一次发送的切片仍然对得上」不是希望,是可推导的。

注意重复值那条分支**不是回滚**:`{"gamma","gamma"}` 的第一条 `Add` 成功(`Size` 2→3)才轮到第二条返回旧 id,
所以丢弃之后缓存里有 `gamma`。这一点写进了测试的注释,免得后来者以为 `return` 撤销了什么。

**为什么不打日志**:`Code/encoding` 层没有任何日志设施 —— 全目录 3 个 `#include <iostream>`
(`StringCache.cpp:3`、`Structs/AnimationGraphDescriptorManager.cpp:2`、`Structs/AnimationVariables.cpp:5`)
**一处使用都没有**,也没有 spdlog;编码层内的丢弃只能由调用方打,而调用方拿不到这个信息
(见 §83.3 的签名)。所以这次改动是静默的。

### 83.5 复核命令(照抄)

```powershell
# 1. 编码层不该再有任何 assert
git grep -n "assert(" -- Code/encoding/

# 2. StartId 的来源(对端填的字段,不是本地算的)
git grep -n "StartId" -- Code/encoding/Messages/StringCacheUpdate.cpp Code/encoding/StringCache.cpp

# 3. 唯一调用点与它的签名(它无法回报失败)
git grep -n "Deserialize(acMessage)" -- Code/client/Services/Generic/StringCacheService.cpp

# 4. Add 的早退分支还在(丢弃不破坏不变量的依据)
git grep -n "return \*id;" -- Code/encoding/StringCache.cpp

# 5. 改动面与文档门禁
git diff --stat -- Code
python Tools\Scripts\check_docs.py
```

复核结果:`assert(` 在 `Code/encoding/` **零命中**(`Code/encoding/StringCache.cpp` 从 2 处降到 0);
`StringCacheUpdate.cpp:23` 仍是 `& 0xFFFFFFFF` 的裸读(字段宽度不是本次要改的东西);
`StringCacheService.cpp:30` 仍是那一行 `StringCache::Get().Deserialize(acMessage);`;
`StringCache.cpp:29` 的 `return *id;` 仍在 ⇒ 丢弃路径不会留下半个缓存。

**验证**:本机不编译(`docs/PITFALLS.md` §0/§1),所以证据是逐字命中 + 用例,不是编译产物。
`Code/tests/encoding.cpp:575-647` 的 `[encoding.string_cache_selfheal]` 有四段:

- `StartId = 5` 而本地长度是 2 ⇒ 断言 `Size() == 2`、`alpha`/`beta` 都还在、`gamma` **没进去**
  (把「丢弃而不是清空」钉在用例里:改成 `Clear()` 这条断言就红)。
- 紧接着造一个「id 9 不在缓存里」的 `CachedString` 包,断言名字读成空串 —— 这是「丢弃之后消费者仍然安全」的证据。
- 重复值 `{"gamma","gamma"}` ⇒ 断言 `Size() == 3` 且 `gamma` 在,即**第一条落了地**,`return` 不是回滚。
- 对齐的消息(`StartId = 1`,`{"beta","gamma"}`)照旧生效 ⇒ 断言三条全在,证明这次改动没有把正常路径一起关掉。

> **教训**:第一条:**`assert` 不是「文档化的前置条件」,是「谁都能触发的进程退出」。**
> 判断一条断言该不该留,只问一句:它的实参是**谁**选的?本地算的,留;对端填的,换掉。
> 第二条:**注释会替错误作证。** 写侧那句「the receiver asserts that the ids are contiguous」
> 把「对端一跳就终止进程」描述成了一条设计约定,而它读起来完全像文档 ——
> 改掉实现时必须回头把引用它的注释、判据表、代码块引文一起改掉,否则下一轮审计会再把它当成正确的先例引用一次。
> 第三条:**「丢弃」和「清空」都叫「失败处理」,但代价差两个数量级。**
> 失败处理的粒度要跟「重来一次的代价」对齐:这里重来要等 2000 ms,所以粒度必须是这一条消息,不是整个缓存。

## 84. 2026-10-13 场次:整包尺寸没有任何上限,而包内每一个长度字段都被当成真的 —— 判据是「谁在分配之前知道这个包有多大」

> **锚点**:一处上限,两个调用点,一条门禁:
> `Code/encoding/WireLimits.h:67-79`(`kMaxPacketBytes = 1 << 20` 与 `WithinPacketLimit`)、
> `Code/server/GameServer.cpp:575-588`(`OnConsume` 在 `ViewBuffer buf(...)` 之前判尺寸)、
> `Code/client/Services/Generic/TransportService.cpp:183-193`(同一个形状,在 `ViewBuffer buf(...)` 之前)、
> `Code/server/GameServer.cpp:676` 与 `Code/client/Services/Generic/TransportService.cpp:165`(上限的依据:两个发送侧各自的固定缓冲区)、
> `Code/plugins/tools/check_transport_compat.py:202-245`(新门禁:常量下界 + 两个入口真的调用了它)。
> 测试在 `Code/tests/encoding.cpp:1612-1638`(`[encoding.packet_limit]`)。
> 就地更正:`docs/PITFALLS.md` §62.4 —— 那段判据本身是对的,但它在「实现是否存在」这件事上把人骗了(见 §84.2)。

### 84.1 缺陷:接收入口从不问这个包有多大

```cpp
// Code/server/GameServer.cpp(修复前 :572-602)
void GameServer::OnConsume(const void* apData, const uint32_t aSize, const ConnectionId_t aConnectionId)
{
    ViewBuffer buf((uint8_t*)apData, aSize);          // :574 aSize 唯一一次被使用
    Buffer::Reader reader(&buf);

    const ClientMessageFactory factory;
    auto pMessage = factory.Extract(reader);          // :593 从这里开始相信包内的一切
    if (!pMessage)
    {
        spdlog::error("Couldn't parse packet from {:x}", aConnectionId);
        return;
    }

    m_messageHandlers[pMessage->GetOpcode()](pMessage, aConnectionId);
}
```

客户端 `Code/client/Services/Generic/TransportService.cpp:180-206` 是同一个形状(`:183` 建 `ViewBuffer`、
`:186` 解析、`:204-205` 塞进 `m_pendingMessages`),管理端 `Code/admin/Transport.cpp:12-25` 是第三处。

| 位置 | 修复前 | 后果 |
| --- | --- | --- |
| `GameServer::OnConsume` | `:574` 之后直接 `:593` 解析 | 未认证连接可让服务端按包内声明分配 |
| `TransportService::OnConsume` | `:183` 之后直接 `:186` 解析 | 服务端(或被冒充的服务端)可让客户端按声明分配 |
| `WireLimits.h:23-26` | 逐字写着「整包上限只能在接收入口加」 | 读起来像已经加过了 |

### 84.2 判据:上限必须加在「知道尺寸」和「开始分配」之间的那一步

包内每一个长度字段都是「这个包里有这么多字节」的**声明**,而解析器按声明分配:
`Mods::Deserialize` 的 8191 计数(§66.3)、`Inventory::Deserialize` 的 4096 条、每条 `ReadString` 的长度前缀。
`Code/encoding/WireLimits.h` 的那些字符串上限全部**在读完之后**才夹取 —— 头注释 `:18-21` 自己写明了理由
(「discarding the whole packet over one bad field turns a cosmetic problem into a disconnect」),
也写明了它的边界 `:23-26`:「the clamp runs after the string has been read, so it bounds what the value is used for,
not the transient allocation inside the reader.」

- **判据一:谁在分配之前知道尺寸?** 只有接收入口 —— 它拿到的是 `aSize`,而编码层拿到的是一个已经包好的 `ViewBuffer`。
  所以这条上限只能加在那里,加在任何更靠下的地方都太晚。
- **判据二:上限的值由谁决定?** 由**我们自己的发送侧**决定,不是由「合法流量看起来多大」决定。
  两个发送侧都在固定缓冲区里序列化(服务端 `1 << 20`、客户端 `1 << 16`),所以「我们自己的对端能发出的最大包」
  有一个可写下来的定义,取两者的大者。写得比它小,门禁会开始丢弃**合法**流量,而丢包的后果看起来像失同步。
- **判据三:这个上限的失败方向是什么?** 是静默丢包。把 `1 << 20` 改成 `1 << 10` 不会让任何测试变红,
  只会让某些报文在正常对局里凭空消失 —— 所以这条常量需要门禁盯它的**下界**,光有用例盯上界不够。

### 84.3 可达性:未认证连接就能到达

`GameServer::OnConsume` 是先解析、再按 opcode 分派,而认证发生在 `HandleAuthenticationRequest` 里
(`Code/server/GameServer.cpp:849-880`)—— 也就是说 `factory.Extract(reader)` 与它后面按包内声明做的分配,
**全部发生在认证之前**。§66.3 已经把这一条写下来了(`docs/PITFALLS.md:7977-7982`):
「第一条在**认证包解析路径**上,即未认证连接可达」。

客户端侧的对应事实是 `OnConsume` 跑在握手泵线程上(`Code/client/Services/Generic/TransportService.cpp:498` 起),
`m_pendingMessages` 本身没有条数上限、只有握手超时兜底 ⇒ 一个不发大包但发很多包的对手,与一个大包是两条不同的路径,
本条只堵后者。

### 84.4 修法:上限放进编码层,判断放在两个入口的最前面

```cpp
// Code/encoding/WireLimits.h
// The largest packet this framework can produce. Every sender serialises into a fixed
// buffer before handing the bytes to the transport - the server into 1 << 20, the client
// into 1 << 16 - so a packet larger than the larger of those is one no peer of ours wrote.
// The receive entry points check this before they build a reader over the bytes, which is
// the one place a bound can run before the allocation instead of after it.
inline constexpr uint32_t kMaxPacketBytes = 1 << 20;

// Whether a packet of this size can have come from a peer. Split out from the constant so
// the boundary itself is testable: Code/tests/xmake.lua only sees this library.
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

    ViewBuffer buf((uint8_t*)apData, aSize);
```

**为什么上限放在 `Code/encoding` 而不是放在两个调用者旁边**:`Code/tests/xmake.lua` 只 `add_deps("SkyrimEncoding")`,
那是测试目标唯一能看到的库 —— 放服务端就永远没有用例。§62.4 已为 `WireLimits.h` 立过同一条判据。

**为什么拆出一个 `WithinPacketLimit`**:边界本身要可测。留成常量比较,测试只能测常量;留成函数,
测试能测「等于上限通过、上限加一不通过」。这也是本仓库里第一次给「尺寸判断」本身写用例。

**为什么是丢弃而不是夹取**:与字符串那些上限的取舍**相反**,理由是处理的对象不同 ——
字符串截短之后仍然是那个字符串的近似,包截短之后不是包。

**为什么不改管理端**(`Code/admin/Transport.cpp:12-25`):它是独立桌面二进制、全文零 spdlog、失败分支是裸 `return;`,
不在「游戏本体/插件层」的判据范围内。

**为什么顺手加一条门禁**:这条上限有一个静默失败方向(见判据三)。`check_transport_compat.py` 现在读
`WireLimits.h` 里的 `kMaxPacketBytes`,断言它不低于 `1 << 20`,并断言两个接收入口真的调用了 `WithinPacketLimit`。
只断言「常量存在」是不够的:常量可以存在而没有任何调用点,那正是这条缺陷原本的样子。

### 84.5 复核命令(照抄)

```powershell
git grep -n "kMaxPacketBytes" -- Code
git grep -n "WithinPacketLimit" -- Code
git grep -n "ViewBuffer buf" -- Code/server/GameServer.cpp Code/client/Services/Generic/TransportService.cpp
git grep -n "Buffer buffer(1 << " -- Code/server Code/client
python Code\plugins\tools\check_transport_compat.py
python Tools\Scripts\check_docs.py
```

**验证**:本机不编译(`docs/PITFALLS.md` §0/§1),证据是逐字命中 + 新用例 + 门禁的反向验证。
`Code/tests/encoding.cpp:1612-1638` 的 `[encoding.packet_limit]` 断言 `WithinPacketLimit(0)` / `(1)` /
`(kMaxPacketBytes)` 通过、`(kMaxPacketBytes + 1)` 与 `uint64_t` 最大值不通过、`kMaxPacketBytes >= (1u << 20)`、
以及它大于三个字符串上限。`check_transport_compat.py` 的**反向验证**是把客户端那处调用临时换成
`aSize > (1u << 20)`,门禁随即输出
`TRANSPORT COMPATIBILITY FAILED (1) - Code/client/Services/Generic/TransportService.cpp TransportService::OnConsume does not call WireLimits::WithinPacketLimit; the size of the packet is known only there, so without it the bound does nothing`,
恢复后回到 `TRANSPORT COMPATIBILITY OK`。

> **教训**:第一条:**「上限写在文档里」和「上限存在」是两件事。**
> `WireLimits.h` 的头注释逐字写了「整包上限只能在接收入口加,这句话写在头注释里」,写了很久,
> 而两个入口一次都没判过尺寸 —— 一段把缺口描述得非常准确的注释,读起来和一段修好的代码一样让人放心。
> 第二条:**安全上限的失败方向是「静默丢包」,不是「崩溃」。**
> 所以它需要一个盯**下界**的门禁,而用例只能盯上界:把 `1 << 20` 改成 `1 << 10`,
> 所有用例都还是绿的,只是某些报文在正常对局里开始凭空消失。
> 第三条:**同一个「太大」,在字符串层和包层的正确取舍是相反的。**
> 判据不是「夹取更温和」,而是「截断之后剩下的东西还有没有意义」:字符串还有,包没有。

## 85. 2026-10-15 场次:任务日志的条数上限写侧在用、读侧被忽略,而读侧拿它 resize —— 判据是「同一份常量有没有被两边都读到」

> **锚点**:一个常量,两处改动:
> `Code/encoding/Structs/QuestLog.h:20-25`(`kMaxEntryCount` 从文件作用域挪进结构体,读侧从此看得见)、
> `Code/encoding/Structs/QuestLog.cpp:35-56`(`Deserialize` 先夹取再逐条 `push_back`)、
> `Code/server/Services/QuestService.h:27-32`(服务端任务日志表的 `kMaxQuestLogEntries = 4096`)、
> `Code/server/Services/QuestService.cpp:71-87`(满了只拒绝新增那一条)。
> 测试在 `Code/tests/encoding.cpp:1641-1704`(`[encoding.quest_log]`)。
> 就地更正:本节补上 §84 没有覆盖的那一半 —— §84 是「上限不存在」,这一节是「上限存在但只被一边读到」。

### 85.1 缺陷:同一个常量,写侧读它,读侧不读它

```cpp
// Code/encoding/Structs/QuestLog.cpp(修复前)
// The count field is 16 bits wide, so a longer log could not be received anyway:
// clamp it, or the reader consumes only the low 16 bits worth of entries and then
// misparses the rest of the packet.
static constexpr size_t kMaxEntryCount = 0xFFFF;          // :13 文件作用域

void QuestLog::Serialize(TiltedPhoques::Buffer::Writer& aWriter) const noexcept
{
    const auto count = std::min<size_t>(Entries.size(), kMaxEntryCount);   // :27 写侧引用了它
    aWriter.WriteBits(count, 16);
}

void QuestLog::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    uint64_t temp;
    aReader.ReadBits(temp, 16);

    const size_t count = temp & 0xFFFF;
    Entries.resize(count);                                // :45 读侧照着线上的数分配

    for (auto& e : Entries)                               // :47 才开始读条目字节
    {
        e.Id.Deserialize(aReader);
        aReader.ReadBits(temp, 16);
        e.Stage = temp & 0xFFFF;
    }
}
```

注释本身是对的(`:10-12` 说的就是「夹取它」),常量本身是对的(值就是位宽),写侧也确实夹了。
缺的只有一行 —— 读侧那一行没引用这个常量。两字节的包就能让接收侧按 65535 条去 `resize`,
在任何一条条目字节被读到之前。一条 `Entry` 是一个 `GameId`(两个 `uint32_t`)加一个 `uint16_t`,
而 `GameId` 的两个字段在线上各是一个 VarInt ⇒ 声明 65535 条时实际需要的字节远少于 65535 × 12。

### 85.2 判据:检查「常量有没有被两边都读到」,不是「有没有这个常量」

普查办法:把 `Code/encoding/**` 每个 `.cpp` 里 `::Serialize*` 与 `::Deserialize*` 的函数体切出来,
看每个 `kMax*` 出现在哪一侧。172 个文件里 19 个只在单侧出现,其中 16 个是「只在写侧、读侧用位宽天然夹住」
(`AssignObjectsRequest`/`AssignObjectsResponse`/`NotifyObjectInventoryChanges`/`RequestObjectInventoryChanges`/
`RequestFactionsChanges`/`NotifyPartyInfo`/`NotifyPartyJoined`/`NotifyPlayerList`/`NotifyPluginMessaging`/
`PluginMessagingRequest`/`ShiftGridCellRequest`/`StringCacheUpdate`/`StringCache`/`ActionReplayChain`/`Tints`),
3 个是「只在读侧、标量范围夹取」(`NotifyHealthChangeBroadcast`/`RequestHealthChangeBroadcast`/`TimeModel`)。
**只有 `QuestLog.cpp` 是写侧夹了、读侧不夹而读侧还要 `resize` 的。**

- **判据一:两侧夹的不是同一件事,所以可以取不同的值。** 写侧夹的是「线上能不能表示」,所以用位宽 `0xFFFF`;
  读侧夹的是「该不该按这个数分配」,所以用量级 `0x1000`。两个决定都要写出来,不能让一个常量同时承担。
  量级取 `0x1000` 的依据是日志行数的来源 —— `Code/client/Services/Generic/CharacterService.cpp:1439` 遍历的
  `PlayerCharacter::objectives` 是游戏自己维护的数组(`Code/client/Games/Skyrim/PlayerCharacter.h:156`),
  真实存档在几百以内;`Inventory` 对 VarInt 计数取的也是 `0x1000`(`Code/encoding/Structs/Inventory.h:80-85`)。
- **判据二:读侧的分配必须跟着「真正读到的条目」走,不能跟着「声明的条数」走。**
  夹取只回答了「这一包最多分配多少」,没回答「包短于声明时那些没被读到的条目是什么」——
  `resize` 出来的条目是默认值,会被下游当成真的日志内容。`clear()` + 逐条 `push_back` 是
  `Inventory.cpp:124-136` 与 `Tints.cpp:43-71` 已经在用的形状。
- **判据三:`Deserialize` 不保证被调进一个全新的对象。** 追加语义会让复用的对象每收一包长一截,
  所以 `clear()` 与夹取是同一个修复的两半,不是顺手加的(`Tints.cpp:50-51` 上写过同一条注释)。
- **判据四:常量放在哪里也是修复的一部分。** 文件作用域的 `static constexpr` 在 `Code/tests` 里看不见,
  而这条上限唯一的回归手段就是「断言常量 + 断言夹取后的 `size()`」。所以它必须挪进结构体。

### 85.3 可达性:两字节的包,以及服务端那张只增不减的表

`QuestLog::Deserialize` 全仓只有一个活调用点:`Code/encoding/Messages/AssignCharacterRequest.cpp:51`
(`QuestContent.Deserialize(aReader)`),而 `AssignCharacterRequest` 是客户端在**进入一局游戏时**发的东西。
另一处 `Code/encoding/Structs/CharacterData.cpp:51` 在死结构里(无实例化点)。

服务端那一半是另一条路径:`Code/server/Services/QuestService.cpp:73` 的 `entries.emplace_back(message.Id, message.Stage)`
是这张表**唯一的增长点**,而唯一的删除点是 `:102-107` 的 `Stopped` 分支(`if (questIt != entries.end()) entries.erase(questIt);`)。
键是客户端挑的 `GameId`,客户端一直造新 id 就一直涨;整张表没有读者 ——
`AssignCharacterResponse.h:29-42` 的成员里没有 `QuestLog` 字段,`GetQuestLogComponent()` 除了两个写者之外零调用
(`Code/server/Scripting/Player_Bindings.cpp:23-24` 里那两行是注释掉的)。这和 `ObjectService` 那张表是同一类,
修法照抄 `Code/server/Services/ObjectService.h:31-37` 立下的四条规则:
类作用域 `static constexpr` 上限、先数已有行数、已存在的键照常服务(上限判断只挂在「新增」那个分支上)、
满表时一行 `spdlog::warn` 而正常回复照发。

### 85.4 修法

```cpp
// Code/encoding/Structs/QuestLog.h
    // The count travels in 16 bits, so a peer can name up to 65535 entries while the
    // writer never sends more than this. Clamp the read side to the same bound: the
    // count drives the resize there, and Deserialize runs inside noexcept frames, so
    // throwing is not an option either. A log stays in the hundreds at the very most,
    // so the bound costs a legitimate sender nothing.
    static constexpr size_t kMaxEntryCount = 0x1000;
    Vector<Entry> Entries{};
```

```cpp
// Code/encoding/Structs/QuestLog.cpp
void QuestLog::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    uint64_t temp;
    aReader.ReadBits(temp, 16);

    // Never trust the wire length, and never throw: this runs inside a noexcept frame.
    const auto count = std::min<size_t>(temp & 0xFFFF, kMaxEntryCount);

    // The message is not guaranteed to arrive into a fresh object: append semantics
    // would silently grow the list on every reuse.
    Entries.clear();

    for (size_t i = 0; i < count; ++i)
    {
        Entry entry;
        entry.Id.Deserialize(aReader);
        aReader.ReadBits(temp, 16);

        entry.Stage = temp & 0xFFFF;
        Entries.push_back(entry);
    }
}
```

```cpp
// Code/server/Services/QuestService.h
    // A quest log row is keyed by a GameId the client picks, and the only thing that ever
    // retires one is a Stopped update for that same id: a client that keeps inventing ids
    // grows this table for as long as it stays connected. Bounded here like every other
    // table whose key the peer picks. A full log refuses the new id instead of growing
    // without limit; updates for ids already in it still go through.
    static constexpr std::uint32_t kMaxQuestLogEntries = 4096;
```

```cpp
// Code/server/Services/QuestService.cpp:71-87
    else if (entries.size() >= kMaxQuestLogEntries)
    {
        spdlog::warn("{}: quest log is full ({} entries), refusing to track quest {:X} from {} {:X}", __FUNCTION__, entries.size(), message.Id.LogFormat(),
                     playerTypeString, pPlayer->GetId());
    }

    else
    {
        entries.emplace_back(message.Id, message.Stage);
        questIt = std::prev(entries.end());
        spdlog::debug("{}: started/discovered quest: {:X}, stage: {}, by {} {:X}", __FUNCTION__, message.Id.LogFormat(), notify.Stage, playerTypeString, pPlayer->GetId());
    }
```

**为什么服务端拒绝的是「新增」而不是「整包」**:下面那段 switch 与转发逻辑对一条没被记录过的 quest id
本来就是合法输入(注释 `:61-64` 说的就是「发现一条进行中的任务」),而 `:102-107` 的 `Stopped` 分支对
`questIt == entries.end()` 已经有处理 ⇒ 拒绝新增不会让任何后续分支走到没走过的路径。

### 85.5 复核命令(照抄)

```
python Tools\Scripts\check_docs.py
python Code\plugins\tools\check_transport_compat.py
python Code\plugins\tools\plugin_patches.py verify
python Code\plugins\tools\plugin_patches.py check
```

**验证**:本机不编译(`docs/PITFALLS.md` §0/§1),证据是逐字命中 + 新用例 + 常量两侧同源。
`Code/tests/encoding.cpp:1641-1704` 新增
`TEST_CASE("A quest log cannot make the reader allocate what it never sent", "[encoding.quest_log]")`:

- 写侧喂 `kMaxEntryCount + 7` 条,读回来正好 `kMaxEntryCount` 条,首尾内容对得上。
- 手写一个声明 `0xFFFF` 条、实际只写 `kMaxEntryCount` 条的包,读侧停在 `kMaxEntryCount` ⇒ 夹取生效且流被消费干净。
- 一个已经装了两条旧条目的对象收到只声明一条的包之后只剩一条 ⇒ 替换语义,不是追加。

> **教训**:第一条:**「常量存在」不等于「常量在生效」。**
> §84 是上限根本不存在;这一节是上限存在、名字对、值对、注释也对,只是**读侧那一行没引用它**。
> 一段准确的注释加一个正确的常量,读起来和一段修好的代码一样让人放心 —— 所以检查的对象不是常量,
> 而是「用到它的地方是不是两边都有」。
> 第二条:**`resize(线上的数)` 与 `push_back(读到的条目)` 在正常流量下完全一样。**
> 两者只在包短于声明时分岔,而那正是对手能造出来的唯一形状 —— 用正常流量做回归永远测不出区别。
> 第三条:**上限的「值」和上限的「位置」是两个决定。**
> 写侧夹的是「线上能不能表示」,所以用位宽;读侧夹的是「该不该按这个数分配」,所以用量级。
> 两者可以不同,但必须都写出来;而常量放哪一层,决定了它能不能被用例盯住。

## 86. 2026-10-16 场次:插件层的浮点图变量只有发送侧判了有限,接收侧把线上的数直接喂给动画图 —— 判据是「这个值两端都判过吗」

> **锚点**:一处修复,在插件层,交付方式是补丁(插件是子模块,本仓不拥有、也不能 push):
> `Code/plugins/patches/AnimSyncTogether/0002-reject-a-graph-variable-value-the-sender-would-never-produce.patch`
> 改 `plugins/AnimSyncTogether/src/STRPMClient.cpp`(新守卫 `:482-492`,`#include <cmath>` `:10`)。
> 同一条判据的仓库级先例是 §70(`Code/encoding/WireLimits.h` 的 `ClampFinite`)。

### 86.1 缺陷:同一条通道,发送侧判 `isfinite`,接收侧不判

`plugins/AnimSyncTogether` 的图变量同步是一条 STRPM 通道的两端。发送侧判了:

```cpp
// plugins/AnimSyncTogether/src/GraphVariableSync.cpp:68
if (!holder->GetGraphVariableFloat(variableName, value) || !std::isfinite(value) || !HasChanged(rule, value)) {
    break;
}
```

接收侧没有任何有限性判断,线上的 `float` 直接进引擎 setter:

```cpp
// plugins/AnimSyncTogether/src/STRPMClient.cpp(修复前 :459-546)
void STRPMClient::ApplyGraphVariableMessage(
    STRPM::ConnectionID senderConnectionID,
    const std::string& name,
    std::uint32_t variableType,
    bool boolValue,
    std::int32_t intValue,
    float floatValue)                       // :459 线上来的 float
{
    const auto type = static_cast<GraphVariableType>(variableType);   // :471
    if (!SyncRules::GetSingleton()->ShouldSyncGraphVariable(name, type)) { ... }

    switch (type) {
    case GraphVariableType::kFloat:
        applied = actor->SetGraphVariableFloat(variableName, floatValue);   // :531 直接进引擎
        break;
    }
```

`floatValue` 的来路是 `packet->graphVariableFloat`(`:291`),而 `packet` 是 STRPM 载荷**逐字节强转**出来的:

```cpp
// plugins/AnimSyncTogether/src/STRPMClient.cpp(修复前 :256-292)
if (!message || !userData || !message->data || message->size != sizeof(SyncPacket)) {   // :260 只判长度
    return;
}
const auto* packet = static_cast<const SyncPacket*>(message->data);                     // :264 内容不判
```

也就是:长度对、版本对,载荷里的任意 4 字节就会被当成一个 `float` 交给 `SetGraphVariableFloat`。
`NaN` 与 `±inf` 全部合法通过 —— 它们只是浮点数的取值,不是畸形字节。

| 位置 | 作用 |
| --- | --- |
| `src/STRPMClient.cpp:68`(修复前) | 发送侧:`!std::isfinite(value)` 直接 `break`,不发 |
| `src/STRPMClient.cpp:471`(修复前) | 接收侧:`variableType` 被强转成枚举,只用来选分支 |
| `src/STRPMClient.cpp:531`(修复前) | 接收侧:`floatValue` 进 `SetGraphVariableFloat` |
| `src/STRPMClient.cpp:544`(修复前) | 紧随其后的 `OARClient::ClearConditionStateData(actor)` —— 值已经被行为图读走 |

### 86.2 判据

- **这个值两端都判过吗?** 发送侧判了,接收侧没判。发送侧的判断保护的是发送侧自己 ——
  一个值从线上回来时,它只经过了长度检查(`:260`)。判据不是「有没有人判过」,而是「两端都判过吗」。
- **判断放在哪一行?** 必须放在 `ShouldSyncGraphVariable` **之后**。
  `plugins/AnimSyncTogether/src/SyncRules.cpp:126-132` 要求 `it->second == type`,
  也就是规则表里这条变量真的是 `float` 才算命中;放在它之前,`type` 就只是对端填的 `variableType`,
  一个自称 `float` 的包就能走到 `isfinite` 判断里。**先证明「这个字段是什么」,再判「这个值合不合法」。**
- **丢弃还是夹取?** 丢弃。一个不是有限数的图变量没有「接近的值」可以夹 ——
  与 §70 那些有量纲的量(血量、附魔强度)不同,这里的发送侧自己都会拒绝发送它,
  收下它就等于承认这一包是数据。
- **谁能触发?** 任何能往这条通道发一包的玩家。通道是 `caelvanost.animsynctogether.anim.v1`,
  没有身份门槛;接收侧对发送者的唯一检查是 `resolver_->resolve(senderConnectionID, …)`,
  那一步在 `:494-500`,**在本判断之后** —— 也就是说判断不依赖能否解析出 proxy。

### 86.3 可达性

- **默认配置下不可达**:`plugins/AnimSyncTogether/config/Rules/HelmetToggle2.rules:9-10` 只声明了两个 `int`
  (`iGPMAAnimationType` / `iGPMAOffsetType`),`kFloat` 分支走不到。
- **但它是一个文档化的功能**:`plugins/AnimSyncTogether/README.md:60` 的规则语法里就写着 `float fCustomVariable`,
  `src/SyncRules.cpp:293-294` 的 `float` 指令就是给它用的 ⇒ 任何写了 `float` 规则的用户都在这条路径上。
- **这就是它比 §70 更难发现的原因**:§70 那些浮点长在默认就启用的收包路径上,这一处长在一个**只由规则文件开关**的分支上,
  实机跑默认配置永远看不到它。
- **同族的既有做法**:整个插件层的 `std::isfinite` 有 26 处(MorphSyncTogether / OStimTogether 为主),
  本条补上的是唯一一处**接收侧**漏判。

### 86.4 修法

在规则表确认过 `type` 之后、解析 proxy 之前插入守卫:

```cpp
// plugins/AnimSyncTogether/src/STRPMClient.cpp(修复后 :482-492)
// The sender refuses to transmit a value that is not finite, so a peer sending one
// is not sending data. The animation graph would carry it into clip selection and
// into the OAR conditions that read the variable, and nothing downstream rejects it.
if (type == GraphVariableType::kFloat && !std::isfinite(floatValue)) {
    SKSE::log::warn(
        "GraphVarRx rejected sender={} name='{}' type=float value={:.6f} reason='not a finite number'",
        senderConnectionID,
        name,
        floatValue);
    return;
}
```

- 只对 `kFloat` 判:`kBool` / `kInt` 走的是 `bool` 与 `std::int32_t`,线上没有非有限值这一说,
  对它们调 `isfinite` 只会让 `type` 判断失去意义。
- `#include <cmath>` 加在 `src/STRPMClient.cpp:10`(插件的 `src/PCH.h` 只有 `<array> <filesystem> <memory> <span> <string> <string_view>`,
  没有 `<cmath>`,而 `GraphVariableSync.cpp:5` 自己就 `#include <cmath>` ⇒ 同族做法)。
- 补丁只改一个文件、两个 hunk(`+13/-0`),打在 pin `a432ec34` 上。

### 86.5 复核命令(照抄)

```
python Code\plugins\tools\plugin_patches.py verify
python Code\plugins\tools\plugin_patches.py check
python Code\plugins\tools\check_transport_compat.py
python Tools\Scripts\check_docs.py
```

**验证**:本机不编译(`docs/PITFALLS.md` §0/§1),证据是逐字命中 + 补丁打在 pin 上 + 门禁全绿。

- `python Code\plugins\tools\plugin_patches.py verify` → `PATCH VERIFY OK` / `26 patch(es) apply to their pinned commits`。
- `git apply --check --cached` 对该补丁 exit 0(打在 pin `a432ec34` 的索引上),`--reverse` 对当前工作树 exit 0。
- 同族对照:整个插件层的 `SetGraphVariableFloat` 只有 4 处,另外 3 处
  (`plugins/OStimNG/skse/src/GameAPI/GameActor.cpp:74`、`:102`、`plugins/OStimTogether/src/FreeScenePhaseSync.cpp:953`)
  写的是本地算出来的常量或本地状态,不是线上的字节 ⇒ 接收侧只有这一处。

> **教训**:第一条:**「发送侧判过」不是「这个值安全」。**
> 发送侧的判断保护的是发送侧自己;一个值从线上回来时,它只经过了长度检查。
> 判据是「这个值两端都判过吗」,而不是「有没有人判过」。
> 第二条:**判定的位置比判定本身更重要。**
> 同一个 `isfinite`,放在 `ShouldSyncGraphVariable` 之前是错的 —— 那时 `type` 还是对端自称的;
> 放在之后,`type` 才由本地规则表担保。**先证明「这个字段是什么」,再判「这个值合不合法」。**
> 第三条:**「引擎会兜住」在这个仓库里已经被推翻过很多次,这次也一样。**
> `SetGraphVariableFloat` 之后没有第二道门,它写进去的东西会被选剪辑与 OAR 条件读走;
> 而这一处比 §70 那些更难发现,因为它长在一个只由规则文件开关的分支上。

## 87. 2026-10-17 场次:整包尺寸这条上限有两个接收入口在用、第三个没用,而注释里写的就是「只有两个」 —— 判据是「这条上限的入口数,和入口的总数对得上吗」

> **锚点**:三处改动,都在本仓(不是插件层,所以不涉及补丁交付):
> `Code/admin/Transport.cpp:12-28`(新增门禁 + `#include <WireLimits.h>` / `#include <spdlog/spdlog.h>`)、
> `Code/plugins/tools/check_transport_compat.py:211-215`(`PACKET_GATE_SITES` 2 项 → 3 项)、
> `Code/encoding/WireLimits.h:23-27`(注释从「两个入口」改成「三个」)。
> 上游是 §84(`68811616`),它给前两个入口加了同一道门,并在文档里写下了「不改管理端」。

### 87.1 缺陷:同一道上限,三个接收入口,两个有门

`Code/encoding/WireLimits.h` 的 `kMaxPacketBytes` 存在的理由是「每个长度字段都是对端对『包里有多少字节』的主张,
而 `WireLimits` 里那些字符串上界全部**跑在读取之后**,挡不住读取时的分配」。所以它只能放在**知道整包尺寸的地方**,
也就是接收入口。§84 的注释把这件事写成了:

```cpp
// Code/encoding/WireLimits.h(修复前 :23-27)
// Note the limit of the fix: the clamp runs after the string has been read, so it bounds
// what the value is used for, not the transient allocation inside the reader. Bounding
// the packet itself is what kMaxPacketBytes below does, and the only place for it is the
// receive entry point (GameServer::OnConsume and Client::OnConsume), which see the
// packet size.
```

「the receive entry point」是单数,括号里点了两个。全仓的接收入口是**三个**:

| 入口 | §84 之前 | §84(`68811616`) | 本条(`41f2f8a0` 之后) |
| --- | --- | --- | --- |
| `Code/server/GameServer.cpp:573 GameServer::OnConsume` | 无判断 | `WireLimits::WithinPacketLimit` | 不变 |
| `Code/client/Services/Generic/TransportService.cpp:181 TransportService::OnConsume` | 无判断 | `WireLimits::WithinPacketLimit` | 不变 |
| `Code/admin/Transport.cpp:16 AdminApp::OnConsume` | 无判断 | **漏了** | `WireLimits::WithinPacketLimit` |

漏掉的那一处把对端说的 `aSize` 直接交给 `ViewBuffer`,再交给 `ServerAdminMessageFactory::Extract`,
中间没有任何上界:

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

而门禁当时也只看两个 —— `Code/plugins/tools/check_transport_compat.py:210-213` 的 `PACKET_GATE_SITES` 是两份记录
(注释、门禁名单),它们互相对齐,但都**没有被和真实入口数核对过**。

### 87.2 判据

- **一条上限的作用域 = 接收入口的个数。** 不是「我们今天关心的进程个数」。
  §84 的取舍(把范围限定在游戏本体/插件层)写进了文档而不是代码,而文档不会被编译器或门禁提醒 ——
  这是它比写错代码更危险的地方。
- **「现在不可达」不是「不该有这道门」。** 管理端离活缺陷的距离是一行 `//`:`Code/server/GameServer.cpp:593-607`
  的 admin 分派整段被注释掉(`// TODO: ClientAdminMessageFactory`)。而生产侧还活着 ——
  `Code/server/Services/AdminService.cpp:22-31 sink_it_` 至今把**每一条服务端日志**打包成 `ServerLogs`
  发给所有 admin(`:30` 的 `ForEachAdmin`),消费侧 `Code/admin/Transport.cpp:128-131`
  的 `HandleMessage(const ServerLogs&)` 也还在。把 `//` 去掉的那一刻这个入口就是活的。
- **同一个常量对三个入口都成立,靠的是「取大者」这条既有判据,不是巧合。**
  管理端自己的出包上界是 `1 << 16`(`Code/admin/Transport.cpp:75`、`:103` 两个 `Send` 重载都建
  `TiltedPhoques::Buffer buffer(1 << 16)`),服务端给 admin 发消息走 `Code/server/GameServer.cpp:708` 的 `1 << 20`。
  两者取大 ⇒ `kMaxPacketBytes = 1 << 20` 覆盖两端,不需要为管理端另立一个常量。
- **丢弃分支要有话说。** 管理端此前**没有任何日志设施**:`Code/admin` 与 `Code/admin_protocol` 两个目录里
  `spdlog` 出现 0 次(唯一命中是 `Code/admin/xmake.lua:26` 那个包名)。§84 的「不改管理端」正是拿这一点当理由之一
  (「没有日志设施、失败分支是裸 `return;`」)。这一条反过来:一个静默丢弃的分支如果什么都不说,
  门禁就成了唯一的证据,而门禁只证明调用**存在**,不证明它被**走到**。

### 87.3 可达性

- **不是活缺陷**:`.github/workflows/windows.yml` 只构建 `:578 xmake build SkyrimTogetherClientDllLegacy`
  与 `:643 xmake install -o distrib` ⇒ `Admin` 目标 CI 从不构建;`Code/server/GameServer.cpp:593-607`
  的 admin 分派被注释 ⇒ 管理端当前接不到服务端消息。
- **但两条通道都还在**:生产侧 `AdminService::sink_it_` 挂在服务端 spdlog sink 上(`Code/server/World.cpp:24-25`),
  消费侧 `Code/admin/Transport.cpp:128-131` 的 handler 仍在表里。
- **`<WireLimits.h>` 在管理端可包含**:`Code/encoding/xmake.lua:5 add_includedirs(".", "../", {public = true})`
  + `Code/admin/xmake.lua:18 add_deps("SkyrimEncoding")`;`spdlog` 也已在
  `Code/admin/xmake.lua:24-31 add_packages(...)` 里 ⇒ **构建脚本不需要改**。

### 87.4 修法

`Code/admin/Transport.cpp` 的接收入口改成先判尺寸再建 reader(现在 `:24-28`):

```cpp
// Code/admin/Transport.cpp(修复后 :18-28)
    // Checked before a reader is built over the bytes and before anything is parsed out of
    // them, for the same reason the game's two receive entry points do it: every length
    // field inside a packet is a claim about how much of it is there, and the bounds in
    // WireLimits.h all run after the read, which is too late for the allocation inside the
    // reader. The peer on the other end serialises into a fixed buffer before handing the
    // bytes to the transport, so a larger packet is not one it wrote.
    if (!WireLimits::WithinPacketLimit(aSize))
    {
        spdlog::warn("Dropping a {} byte admin packet: larger than any packet this framework can produce", aSize);
        return;
    }
```

门禁从两份名单变成三份(`Code/plugins/tools/check_transport_compat.py:211-215`):

```python
PACKET_GATE_SITES = (
    ('Code/server/GameServer.cpp', 'GameServer::OnConsume'),
    ('Code/client/Services/Generic/TransportService.cpp', 'TransportService::OnConsume'),
    ('Code/admin/Transport.cpp', 'AdminApp::OnConsume'),
)
```

`Code/encoding/WireLimits.h:25-27` 的注释同步改成「a receive entry point (GameServer::OnConsume,
TransportService::OnConsume and AdminApp::OnConsume)」—— 注释和门禁名单从此指的是同一组人。

### 87.5 复核命令(照抄)

```powershell
cd D:\sktest\SkyrimTogetherNext
python Code\plugins\tools\check_transport_compat.py
python Code\plugins\tools\plugin_patches.py verify
python Code\plugins\tools\plugin_patches.py check
python Code\plugins\tools\strpm_contract.py check
python Tools\Scripts\check_docs.py
```

**验证**:本机不编译(`docs/PITFALLS.md` §0/§1),证据是逐字命中 + 门禁 + 反向验证。

- `python Code\plugins\tools\check_transport_compat.py` → `TRANSPORT COMPATIBILITY OK`。
- **反向验证过**(关键,与 §84 同法):把 `AdminApp::OnConsume` 里的 `if (!WireLimits::WithinPacketLimit(aSize))`
  临时换成 `if (aSize > (1u << 20))`,门禁输出
  `TRANSPORT COMPATIBILITY FAILED (1) - Code/admin/Transport.cpp AdminApp::OnConsume does not call WireLimits::WithinPacketLimit;
  the size of the packet is known only there, so without it the bound does nothing`,exit 1;还原后重新 `OK`。
  这条证据同时证明了两件事:门禁真的读到了新加的第三项,且它查的是**函数体里有没有这个调用**、不是「文件里有没有这个名字」。
- 门禁取函数体用的是 `source.find(f'{entry}(')` … `source.find('\n}\n', start)`
  (`Code/plugins/tools/check_transport_compat.py:240-242`)⇒ 被检查函数的最后一个 `}` 必须独占一行。
  `Code/admin/Transport.cpp` 满足(函数体 `:16-41`,第 `:41` 行是独占一行的 `}`)。

> **教训**:第一条:**「只有两个入口」这句话本身没有被门禁检查过。**
> 注释、门禁名单、真实入口数是三份互相独立的记录。§84 当时让前两份对齐了,但两份都是手写的 ——
> 判据是「这条上限的入口数,和入口的总数对得上吗」,而一份手工维护的名单,它的价值等于它被核对过的次数。
> 第二条:**「现在不可达」和「不该有这道门」是两回事。**
> 这一处离活缺陷的距离是一行 `//`,而它旁边就写着 `// TODO`;
> 把范围排除写进文档(§84 的「不改管理端」)比写进代码更危险,因为文档不会被编译器或门禁提醒。
> 第三条:**同一个值在两个进程里都对,理由可以不同。**
> 管理端自己只发 `1 << 16`,服务端给它发 `1 << 20`;`kMaxPacketBytes` 同时覆盖两者,
> 是「取大者」这条既有判据让它一次成立 —— 换成一个「管理端看起来应该更小」的直觉值就会开始静默丢包。

## 88. 2026-10-18 场次:服务端的日志 sink 把 admin 协议的帧发给游戏连接 —— 判据是「收件人集合里的成员,是不是这个协议的端点」

> **锚点**:两处改动,都在本仓(不涉及补丁交付):
> `Code/server/Services/AdminService.cpp:21-44`(`sink_it_` 不再构造 `ServerLogs`,并删掉
> `#include <AdminMessages/ServerLogs.h>`)、
> `Code/plugins/tools/check_transport_compat.py:249-315`(两半新门禁:编号必须仍然重叠 + sink 里不许出现这两个名字)。
> 上游是 §87(`662b8044`),它给管理端的**接收入口**补了尺寸门,并把生产侧的这半写成了「还活着」——
> 本节关掉的就是那半。

### 88.1 缺陷:生产侧一直在造一帧,而它没有合法收件人

`Code/server/Services/AdminService.cpp`(修复前 `:22-31`)把**每一条**服务端日志格式化成一个
admin 协议的 `ServerLogs`,再交给 `Code/server/GameServer.h:73-78` 的 `ForEachAdmin` 广播:

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

它挂在**默认 logger 的 sink 列表**上(`Code/server/World.cpp:24-25`),而默认 logger 是服务端自己的
(`Code/server_runner/main.cpp:71-78` 建的 `globalOut`,`Code/server/main.cpp:114 set_default_logger`)⇒
**每一条 spdlog 调用都会走到这里**,包括 `spdlog::warn`/`spdlog::info` 那些热路径。

### 88.2 判据:「收件人集合」的语义,和这个协议端点的定义,是同一件事吗

1. **`m_adminSessions` 的语义是「管理员凭据」,不是「管理端连接」。** 它有两个写入点,都在**游戏**握手路径上:
   `Code/server/GameServer.cpp:925`(`adminPasswordUsed` ⇒ 游戏客户端用管理员密码登录)、
   `Code/server/GameServer.cpp:430`/`:441`(`addadmin` 控制台命令按用户名解析出 `Player*`,再取
   `pPlayer->GetConnectionId()`)。两个写入点的输入都是**游戏连接**。
2. **管理端连不进来。** `Code/admin/Transport.cpp:43-51 AdminApp::OnConnected` 建的
   `AuthenticationRequest` **只填了 `Token`**;`Version` 留空,而 `Code/server/GameServer.cpp:885`
   的 `acRequest->Version != BUILD_COMMIT` 发生在 `:918` 看令牌**之前** ⇒ 管理端永远拿不到席位。
   **所以这个集合里没有管理端**,而它是这帧唯一的收件人集合。
3. **两套协议的编号重叠。** `Code/admin_protocol/Opcodes.h:13 kServerLogs = 1`,
   `Code/encoding/Opcodes.h:11 kAssignCharacterResponse = 1`。协议不靠端口或首字节区分 ——
   `Code/server/GameServer.cpp:608-616` 用 `ClientMessageFactory`,`Code/client/Services/Generic/TransportService.cpp:115`
   用 `ServerMessageFactory`,**两端都只按编号查表**,所以编号就是协议身份。
4. **这一帧落在游戏客户端上是「合法解析」。** `Code/encoding/Messages/ServerMessageFactory.cpp:32-42`
   的 `Extract` 先判 `data >= kServerOpcodeMax`(57)再查表,而 `1 < 57` ⇒ 它会解出
   `AssignCharacterResponse`。解出来的字段被 `Code/client/Services/Generic/CharacterService.cpp:358-430`
   直接消费:`Cookie` 用来在 `WaitingForAssignmentComponent` 里找实体、`AllActorValues`、
   `CurrentInventory`、动作列表全部来自这串字节。
5. **失败模式是静默的,不是崩溃。** `Code/client/Services/Generic/CharacterService.cpp:365-369`
   找不到 cookie 时只打一条 `"Never found requested cookie"` 然后 `return;`。也就是说,
   错帧的可见证据**只有一条日志**,而那条日志自己就是被灌进去的帧的产物。
6. **修法的判据是「谁该收这一帧」,不是「谁不该收」。** 今天答不出「管理端连接」这个集合
   (`Code/server/GameServer.cpp:593-607` 的 admin 分派整段被注释),所以正确动作是**不构造**,
   而不是挑一个过滤条件。等那条分派从注释里出来,判据才有对象。

### 88.3 可达性

- **今天不可达,且有两个独立的理由**:管理端进不来(第 2 条),而且服务端也把 admin 的接收分派注释掉了。
- **但生产侧是活的**:`Code/server/World.cpp:24-25` 的 `sinks().push_back` 每次启动都会执行;
  `Code/server/Services/AdminService.h:13` 自己写着「This service is currently not in use.」——
  这句话描述的是**收包**那一半,不是这一半。
- **sink 不能删**:它被 `World` 持有(`Code/server/World.h:55 TiltedPhoques::SharedPtr<AdminService> m_spAdminService;`),
  删掉要动 `World.cpp:24-25` 与头文件;`docs/PITFALLS.md:873-874` 已就同一处下过结论:
  「删掉会改日志行为。保留。」

### 88.4 修法

`sink_it_` 只留一行 `(void)msg;` —— 帧不构造,日志流不动:

```cpp
// Code/server/Services/AdminService.cpp(修复后 :21-44,注释略)
void AdminService::sink_it_(const spdlog::details::log_msg& msg)
{
    // ... 11 行注释:收件人集合是游戏连接、管理端进不来、编号重叠、这帧没有收件人 ...
    (void)msg;
}
```

- **为什么不是「给 `ForEachAdmin` 加过滤」**:过滤条件要回答「这个连接是不是管理端」,而这个问题今天没有答案
  (见 88.2 第 6 条)。
- **为什么不是「删掉 sink」**:见 88.3 第三条。
- **顺带修掉一笔每日志开销**:原实现在**每一条 spdlog 调用**上做一次 `formatter_->format` +
  `fmt::to_string`,而同一条日志在控制台/文件 sink 里**已经格式化过一次**。这一笔开销的规模与日志量成正比,
  且落在服务端线程上 —— 与「不拖慢」那条要求同向。
- **`#include <AdminMessages/ServerLogs.h>` 一并删掉**:它现在是本文件唯一不再需要的头。

### 88.5 复核命令(照抄)

```powershell
# 1. sink 里不许再出现这两个名字(注释先被门禁剥掉,所以注释不算)
Select-String -Path Code\server\Services\AdminService.cpp -Pattern 'ServerLogs|ForEachAdmin'
# 期望:0 命中(唯一允许的命中是注释,门禁按代码行判)

# 2. 编号必须仍然重叠
Select-String -Path Code\admin_protocol\Opcodes.h -Pattern 'kServerLogs'
Select-String -Path Code\encoding\Opcodes.h -Pattern 'kAssignCharacterResponse'

python Code\plugins\tools\check_transport_compat.py
```

**验证**:本机不编译(`docs/PITFALLS.md` §0/§1),证据是逐字命中 + 门禁 + 双向反向验证。

- `python Code\plugins\tools\check_transport_compat.py` → `TRANSPORT COMPATIBILITY OK`。
- 门禁两半各自反向验证过:把 `ServerLogs logs;` 放回 `sink_it_` 的函数体 → 
  `TRANSPORT COMPATIBILITY FAILED (1)` + `Code/server/Services/AdminService.cpp builds or broadcasts an admin frame again...`,
  exit 1;把 `kServerLogs` 改成 `= 9` → `FAILED (1)` + `Code/admin_protocol/Opcodes.h kServerLogs is 9 and
  Code/encoding/Opcodes.h kAssignCharacterResponse is 1...`,exit 1。两次还原后都回到 `OK`。
- 第二半查的是**定义**而不是**提及**:新写的 11 行注释里 `ServerLogs` 与 `ForEachAdmin` 各出现一次,
  门禁先剥掉 `//` 之后的内容,因此不误报 —— 这条也被上面那次反向验证证实(反向验证注入的是代码行)。

> **教训**:第一条:**一个集合的名字描述的是它怎么被填满的,不是它将被用来做什么。**
> `m_adminSessions` 的每个成员都是「用管理员密码登录的游戏连接」,而它被当成「管理端连接的集合」用。
> 判据是「收件人集合里的成员,是不是这个协议的端点」,不是「这个集合的名字听起来对不对」。
> 第二条:**两套协议只靠编号区分时,编号重叠就是一种耦合。**
> `kServerLogs = 1` 与 `kAssignCharacterResponse = 1` 在各自的头文件里都对,合起来却意味着
> 「这条帧能被另一端合法解析」。门禁因此同时钉住两边:谁改编号,谁就被告知这条注释不再成立。
> 第三条:**「注释掉的分派」不等于「那半条链路是死的」。**
> 收包那半确实被注释了,发包这半还挂在 logger 的 sink 列表上、每次启动都注册。
> §87 把这一处写成「生产侧还活着」当作**不放过管理端的理由**,这一节则把它当**缺陷本身**处理 ——
> 两者并不矛盾:一条链路的两端要分别判活。

## 89. 2026-10-19 场次:线上浮点还剩下两处裸读,一处直接写进引擎的转向、一处进 TintMask 的 alpha —— 判据是「这个字段的下游有没有第二道门」

> **锚点**:三处改动,都在本仓(不涉及补丁交付):
> `Code/encoding/Structs/Movement.cpp:38`(`Direction` 的读取)、
> `Code/encoding/Structs/Tints.cpp:67`(`Entry::Alpha` 的读取)、
> `Code/tests/encoding.cpp:1431-1503`(四个新断言块)+ `:47`/`:49`(两条新 include)。
> 上游是同一条判据的 §70(`Code/encoding/WireLimits.h` 的 `ClampFinite`),本节是它的收尾。
> 测试在 `Code/tests/encoding.cpp` 的 `TEST_CASE("A non finite wire float is replaced where it is read", "[encoding.finite_floats]")`。

### 89.1 缺陷:两个字段用 `ReadBits` + `reinterpret_cast` 读,于是 §70 的夹取一处也没覆盖到

§70 给 `Code/encoding` 里二十多处「对端说了算的浮点」补了 `WireLimits::ClampFinite`,判据是
**「引擎 setter 之前是最后一道门」**。但那条清扫是按 `Serialization::ReadFloat` 找读点的,
而线上浮点有**两条**读法 —— 另一条是 `aReader.ReadBits(tmp, 32)` 取 32 位再重解释:

```cpp
// Code/encoding/Structs/Movement.cpp(修复前 :36-39)
    uint64_t tmp = 0;
    aReader.ReadBits(tmp, 32);
    uint32_t tmp32 = tmp & 0xFFFFFFFF;
    Direction = *reinterpret_cast<float*>(&tmp32);
```

```cpp
// Code/encoding/Structs/Tints.cpp(修复前 :66-67)
        aReader.ReadBits(buffer, 32);
        tmp = buffer & 0xFFFFFFFF;
        entry.Alpha = *reinterpret_cast<float*>(&tmp);
```

全仓 `reinterpret_cast<float*>` 共 6 处,其中 4 处读的是引擎自己的内存
(`Code/client/Games/Skyrim/TESObjectREFR.cpp:243`/`:314` 的图变量表、
`Code/client/Services/Debug/Views/AnimDebugView.cpp:164` 的调试视图、
`Code/client/Games/Skyrim/Interface/Menus/HUDMenuUtils.cpp:12` 的引擎调用参数),
剩下这 2 处读的才是线上字节。

### 89.2 判据:「这个字段的下游有没有第二道门」—— 两处都没有

| 字段 | 读点(修复前) | 下游 | 第二道门 |
| --- | --- | --- | --- |
| `Movement::Direction` | `Code/encoding/Structs/Movement.cpp:38` | `Code/client/Services/Generic/CharacterService.cpp:683 point.Direction = movement.Direction;` → `Code/client/Systems/InterpolationSystem.cpp:112 apActor->currentProcess->middleProcess->direction = second.Direction;` | **无**:`:110` 只判了 `currentProcess`/`middleProcess` 是否为空 |
| `Tints::Entry::Alpha` | `Code/encoding/Structs/Tints.cpp:67` | `Code/client/Systems/FaceGenSystem.cpp:119 tints[i]->alpha = tintsEntries[i].Alpha;` | **无**:`FaceGenSystem` 唯一的门是 `:153 if (acTints.Entries.empty()) return;` |

1. **两处都是「直写引擎」而不是「参与判断」。** `Direction` 落在引擎 middle process 的朝向字段上,
   `Alpha` 落在引擎 `TintMask::alpha` 上,接着被 `CreateTints`(`Code/client/Systems/FaceGenSystem.cpp:107-141`)
   交给引擎混合。中间没有任何一处做比较或夹取。
2. **§70 的「替换而不是夹进区间」在这里同样成立。** 没有任何区间能让 `NaN` 变得有意义;
   这两个字段也都不带符号相关的判断(不像血量增量那样需要一个下界),所以不需要为它们单独定范围。
3. **判据不是「这个值合法吗」,而是「谁在这个值之后还看它一眼」。** `InterpolationSystem` 拿 `Direction`
   算的是朝向、`FaceGenSystem` 拿 `Alpha` 交给引擎混合 —— 两个消费者都是**终点**,不是门。

### 89.3 可达性

- **两处都可达,且都在普通流量上。** `Movement` 是 `ReferenceUpdate::UpdatedMovement` 的成员
  (`Code/encoding/Structs/ReferenceUpdate.h:25`),由 `ClientReferencesMoveRequest` 每 20 ms 一批
  (`Code/server/Services/CharacterService.cpp:960-1042` 的 50 Hz 快照)搬运;`Tints` 在
  `AssignCharacterRequest`/`CharacterSpawnRequest`/`CharacterData` 三条路径上
  (`Code/encoding/Messages/AssignCharacterRequest.h:9`、`CharacterSpawnRequest.h:7`、
  `Code/encoding/Structs/CharacterData.h:9`)。
- **本机不编译,所以「谁在真正发 NaN」不可判定。** 与 §70 的六处同一条:这条修复的判据不是
  「有没有人这么发」,而是「发了以后有没有人挡」—— 而两处都没有人挡。
- **两个结构体此前没有任何往返测试**:`Code/tests/encoding.cpp` 全文零 `Movement`/`Tints` 字样
  (只有 `Mods`/`QuestLog`/`Inventory`/`AnimationVariables`/`TimeModel` 等有),
  所以「读侧会不会替换」这件事此前连一个断言都没有。

### 89.4 修法

两处统一走既有的 `WireLimits::ClampFinite`(`Code/encoding/WireLimits.h:99`),两个文件各补
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

测试在 `Code/tests/encoding.cpp` 的 `[encoding.finite_floats]` 里新增四个块(`:1431-1503`),
接收对象一律先写有限哨兵(`Direction = 1.f`),让「读的时候替换了」成为断言成立的唯一原因:

- `Movement` 的 `Direction = NaN` 读回 `0.f`;`Direction = -2.75f` 读回 `-2.75f` 且 `received == sent`
  —— 后一条证明**有限的合法值原样通过**。
- `Tints` 的 `Alpha = +inf` 读回 `0.f`;`Alpha = 0.375f` / `Color = 0x11223344` / `Type = 7` 整包 `==`
  —— 后一条同时钉住字段顺序没被改动。

### 89.5 复核命令(照抄)

```powershell
cd D:\sktest\SkyrimTogetherNext
# 1. 两处读取都在 ClampFinite 里
Select-String -Path Code\encoding\Structs\Movement.cpp, Code\encoding\Structs\Tints.cpp -Pattern 'ClampFinite'
# 期望:各 1 命中(Movement.cpp:43 / Tints.cpp:72)

# 2. 全仓「读了浮点但没有夹取」应只剩引擎自己的内存
Select-String -Path Code\encoding\Structs\*.cpp -Pattern 'reinterpret_cast<float' | Select-String -NotMatch 'ClampFinite'
# 期望:0 命中

python Tools\Scripts\check_docs.py
```

**验证**:本机不编译(`docs/PITFALLS.md` §0/§1),证据是逐字命中 + 门禁 + 往返测试。

- `git grep -rn "ClampFinite" -- Code/encoding` 现在 **30 处**命中(含定义 `WireLimits.h:99`);
  修改前是 28 处,新增的正是这两处。
- `python Tools\Scripts\check_docs.py` → `DOC CHECK OK` / `coverage 3069/3082 stated consistently in 4 doc(s)` /
  `3698 map entries; id manifest agrees with the real gap` / `10 changelog version(s), all present in both READMEs`。
- `Code/encoding` 的线格式**不变**:`Direction`/`Alpha` 都是定宽 32 位字段,写侧一个字节都没动,
  所以没有新 opcode、没有新字段,`check_transport_compat.py` 的输入不变。
- 新增两条 include(`:47 <Structs/Movement.h>`、`:49 <Structs/Tints.h>`);`Code/tests/xmake.lua`
  只 `add_files("*.cpp")` + `add_deps("SkyrimEncoding")`,不需要改。

> **教训**:第一条:**一条判据的覆盖面,等于它被用来找读点的那种写法。**
> §70 是按 `Serialization::ReadFloat` 找的,于是「用 `ReadBits` + `reinterpret_cast` 读浮点」这条
> 第二条读法整条漏网。判据是「这个字段的下游有没有第二道门」,而扫读点时用的是「它长什么样」。
> 第二条:**同一个类型在同一个文件里可以有两种读法,而它们的危险程度一样。**
> `Movement.cpp` 里 `Position`/`Rotation` 走 `NetQuantize`(定点,对端选不出 NaN),
> 紧接着的 `Direction` 走裸 32 位 —— 前者安全不代表后者安全,安全性属于字段而不是文件。
> 第三条:**「谁在这个值之后还看它一眼」比「这个值合不合法」更能定位终点。**
> `InterpolationSystem` 与 `FaceGenSystem` 都不是门,是终点:它们拿到什么就交给引擎什么,
> 所以判据要问的是「从读到用之间有没有人比较过」,而不是「这个数看起来正不正常」。
## 90. 2026-10-19 场次:IED 的远端变换把对端的 `nan` 原样写进引擎节点,而它唯一的调用点恰恰在 NaN 时不会被调用 —— 判据是「拒绝一个状态」和「夹住一个字段」在这里不是同一个修复

> **锚点**:本仓一个文件 —— `Code/plugins/patches/IEDSyncTogether/0003-reject-a-transform-value-the-sender-would-never-produce.patch`
> (943 B / 25 行 / `index 26d1461..0e0879d`),它改的是子模块的 `src/LocalIEDState.cpp`(两个 hunk:`@@ -2,6 +2,7 @@`、`@@ -31,7 +32,11 @@`)
> 与 `plugins/IEDSyncTogether/src/RemoteIEDRenderer.cpp:238-246`(落点,本轮未改)。
> 上游是同一条判据的 §89(「这个字段的下游有没有第二道门」);本节是它在插件层的第一次落地。
> 测试:本机不编译,故为逐字命中 + 补丁三重校验 + 累积应用差分(见 §90.5)。

### 90.1 缺陷:`std::stof` 把 `"nan"` 当合法输入,而落点一个守卫也没有

链路的四段,每一段都逐字可取:

| 环节 | 位置 | 逐字 |
| --- | --- | --- |
| 捕获 | `plugins/IEDSyncTogether/src/LocalCaptureProbe.cpp:15-18` | `float Q(float value) { return std::round(value * 1000.0f) / 1000.0f; }` —— 量化**不剥非有限值**(`round(NaN * 1000.0f)` 仍是 `NaN`) |
| 编码 | `plugins/IEDSyncTogether/src/LocalIEDState.cpp:53-67` | `fmt::format("O\|{}…\|{:.6f}\|{:.6f}\|{:.6f}\|{:.6f}", …, object.position[0], object.position[1], object.position[2], object.scale)` + `for (const auto value : object.rotationMatrix) out += fmt::format("\|{:.6f}", value);` —— **fmt 对 `NaN` 输出文本 `nan`,对 `inf` 输出 `inf`** |
| 解码 | `plugins/IEDSyncTogether/src/LocalIEDState.cpp:31-37`(修复前) | `if constexpr (std::is_floating_point_v<T>) { try { out = static_cast<T>(std::stof(std::string(text))); return true; } catch (...) { return false; } }` —— `std::stof` 接受 `"nan"`/`"inf"`/`"infinity"`,**不抛**;`catch` 只拦 `std::invalid_argument`/`std::out_of_range` |
| 落点 | `plugins/IEDSyncTogether/src/RemoteIEDRenderer.cpp:238-246` | `object->local.translate.x = captured.position[0];` / `:241 object->local.scale = std::clamp(captured.scale, 0.01f, 100.0f);` / `:246 object->local.rotate.entry[row][col] = captured.rotationMatrix[index++];` —— translate 三行与 rotate 九格**无守卫**,`:241` 是这一块里**唯一**有守卫的一行 |

`std::clamp` 在这里挡不住:`NaN` 与任何值比较都为假,`clamp` 原样返回 —— 而它恰好在唯一「看起来守住了」的那一行上。

### 90.2 判据:这里该「拒绝一个状态」,而不是「夹住一个字段」

§70/§89 的修法是把非有限值替换成 `0.f`。这里不能照搬,三条理由:

1. **没有替代值。** 位置三项、九个旋转项、缩放各是独立语义的浮点;`0.f` 对一个位置是「原点」、
   对一个缩放是「零尺寸」——都是**合法但错误**的变换,不是「这个字段没有值」。§70/§89 那些字段
   拿 `0.f` 只是「不生效」(一个不存在的法术强度、一个不混合的 alpha);这里写的是节点的局部变换。
2. **失败形态已经存在,而且已经打过日志。** `plugins/IEDSyncTogether/src/STRPMAdapter.cpp:527-535`:
   `auto state = DecodeLocalIEDState(payload); if (!state) { SKSE::log::warn("STRPM IED state RX decode failed: sender={} name=\"{}\" bytes={}", …); return; }`
   —— 在解码层拒掉,上层走的就是这条**既有**路径:整份 state 作废、一条 warn、不新增日志点、不改签名、不改线格式。
3. **一份 state 是一份快照,不是一串独立字段。** 21 个字段描述同一个对象集合;其中一项是 `nan` 时,
   「其余项照写」并不比「整份丢掉」更接近对端真实外观 —— 发送侧的 `NaN` 来自引擎自己给出的坏变换,
   那种情况下对端看到的本来就不是一个可用姿态。

### 90.3 可达性:下游的守卫反而让它**更难**触发,这是降低而不是消除

- **唯一调用点**是 `plugins/IEDSyncTogether/src/RemoteIEDRenderer.cpp:444 ApplyRawTransform(remoteObject, tracked.object);`,
  在 `:392 void RemoteIEDRenderer::WatchdogTick()` 里,由 `:440-442 if (!needsCorrection) { continue; }` 把守;
  `needsCorrection` 由 `:418-421` 的三个 delta 比较得出,三个 delta 分别来自
  `:207-219 MaxMatrixDelta`(`delta = std::max(delta, std::abs(matrix.entry[row][col] - expected[index++]));`)、
  `:221-230 MaxPositionDelta`(`std::max({ std::abs(point.x - expected[0]), … })`)与 `:417 std::abs(remoteObject->local.scale - tracked.object.scale)`。
- **整条 delta 链是 `std::max`/`std::abs` 的比较链,而 `NaN` 参与比较恒为假** ⇒ 含 `NaN` 的那一项对 delta 贡献为 0
  ⇒ `needsCorrection` 得 `false` ⇒ **修正不会发生**。只有「同一对象里还有一项是有限的超阈值值」时才会进 `ApplyRawTransform`,
  于是 `NaN` 那一项跟着一起写进节点。判据因此不能是「反正走不到」—— 走得到,只是需要一项有限值陪着。
- **入口在普通流量上**:`plugins/IEDSyncTogether/src/STRPMAdapter.cpp:507-581 void STRPM_CALL STRPMAdapter::OnMessage(const STRPM::Message* message, void* userData)`
  —— `:513-516` 只判 `message->size > STRPM::kMaxPayloadBytes`,然后 `:527` 解码、`:553-567` 写入 `_remoteStates`、`:580 self->QueueRemoteApply(senderID, false);`。
  与 §89 同一条:**本机不编译,「谁在真正发 NaN」不可判定**;判据是「发了以后有没有人挡」,而修复前一个都没有。
- **合法发送侧自己就会产生 `nan` 文本**:`plugins/IEDSyncTogether/src/LocalCaptureProbe.cpp:77-88` 把引擎给的
  `object->local.translate.*` / `object->local.rotate.entry[row][col]` / `object->local.scale` 经 `Q()` 量化后直接放进
  `CapturedIEDObject`,再经 `EncodeLocalIEDState` 的 `{:.6f}` 打成文本。所以这**不是**「只有伪造包才做得到」。

### 90.4 修法:一个文件、两个 hunk,把 `return true;` 换成 `return std::isfinite(out);`

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

- 整型分支(`std::from_chars` + `result.ptr == last`)**一字未动**,它本来就拒掉一切非数字。
- `:134-144` 的 `if (!ParseNumber(fields[8], object.position[0]) || …) { return std::nullopt; }`
  与九个旋转项那段循环照原样生效 ⇒ **不需要新增任何一个判断点**。
- `plugins/IEDSyncTogether/src/PCH.h`(28 行)里**没有 `<cmath>`**,`LocalIEDState.cpp` 原本只额外
  `#include <charconv>` ⇒ 这一行 include 是必需的,不能靠预编译头带进来。
- 两个 hunk 都在 0002 的 hunk(`@@ -93,6 +93,9 @@`)之前 ⇒ 按 `patch_files()` 的 `sorted(glob('*.patch'))` 累积应用时行号不漂移。
- 补丁的 pre-image 必须是 **pin 内容**:`git rev-parse :src/LocalIEDState.cpp` = `26d146145ce40693fa2634e605008b385a3ff384`
  (与 0002 补丁头的 `index 26d1461..` 一致)。`plugin_patches.py:439-442` 的注释说明了为什么:
  「Checked against the index, not the working tree. The index holds the pinned content even when the patch is already
  applied to the working tree, so "already applied" cannot hide a patch that has stopped matching the pin.」

### 90.5 复核命令(照抄)

```powershell
cd D:\sktest\SkyrimTogetherNext
# 1. 补丁能打到 pin(退 0)、与工作区一致(反向退 0)、已应用(前向退 1)
Push-Location plugins\IEDSyncTogether
git apply --check --cached ..\..\Code\plugins\patches\IEDSyncTogether\0003-reject-a-transform-value-the-sender-would-never-produce.patch
git apply --check --reverse ..\..\Code\plugins\patches\IEDSyncTogether\0003-reject-a-transform-value-the-sender-would-never-produce.patch
Pop-Location

# 2. 累积应用差分:pin + 0001 + 0002 + 0003 必须逐字节等于工作树
Push-Location plugins\IEDSyncTogether
foreach ($p in (Get-ChildItem ..\..\Code\plugins\patches\IEDSyncTogether\*.patch | Sort-Object Name)) { git apply --cached $p.FullName }
git diff --stat      # 期望:空
git reset -q         # 必须还原索引,否则 verify 会误报
Pop-Location

# 3. 门禁
python Code\plugins\tools\plugin_patches.py verify   # 期望:PATCH VERIFY OK / 27 patch(es)
python Code\plugins\tools\plugin_patches.py check    # 期望:PATCH CHECK OK
python Code\plugins\tools\plugin_snapshot.py verify  # 期望:SNAPSHOT OK
python Tools\Scripts\check_docs.py                    # 期望:DOC CHECK OK
```

**验证**:本机不编译(`docs/PITFALLS.md` §0/§1),证据是逐字命中 + 三重校验 + 累积差分。

- 三重校验实跑:`--check --cached` 退 **0**、`--check --reverse` 退 **0**、前向 `--check` 退 **1** 报
  `error: patch failed: src/LocalIEDState.cpp:2` / `error: src/LocalIEDState.cpp: patch does not apply`(已应用时的预期)。
- **累积应用差分(本轮新增的手法,值得照抄)**:把三个补丁依次 `git apply --cached` 打到 pristine 索引,三个都退 0;
  随后 `git diff --stat`(**工作树 vs 累积索引**)输出**空** —— 即「pin + 三个补丁」逐字节等于工作树,
  补丁集既没漏改也没多改;`git diff --cached --stat`(**累积索引 vs pin**)= 5 files changed, 31 insertions(+), 2 deletions(-)。
  这一步能抓到「补丁只打了一部分」这类 `verify` 看不见的问题。
- 验证完必须 `git reset -q`:否则 `git add -A` 之后 `plugin_patches.py verify` 会因 `--cached` 读到已打补丁的索引而报
  `does not apply to the pinned commit - the submodule was re-pinned without rewriting the patch`(这个坑 §85 记过)。
- `python Code\plugins\tools\plugin_patches.py verify` → `PATCH VERIFY OK` / **27** patch(es) apply to their pinned commits;
  `… check` → `PATCH CHECK OK`;`python Code\plugins\tools\plugin_snapshot.py verify` → `SNAPSHOT OK`。
- **线格式不变**:`DecodeLocalIEDState` 的输入仍然是同一串分隔文本,只是非有限值从「被接受」变成「整份拒绝」,
  所以没有新字段、没有新 opcode,`strpm_contract.py` 与 `check_transport_compat.py` 的输入都不变。

> **教训**:第一条:**「下游有守卫」不等于「这个值被挡住了」——要问的是守卫在看什么。**
> `ApplyRawTransform` 确实有唯一的调用点、也确实有 `needsCorrection` 把守,但那道门是用 `std::max`/`std::abs` 比较搭起来的,
> 而 `NaN` 参与比较恒为假 ⇒ 门在 `NaN` 上**永远开着**,只是它通向的是「不修正」。判据要问的是「守卫比较的是什么」,
> 而不是「这里有没有守卫」。
> 第二条:**同一个函数里「看起来守住了」的那一行,可能正是唯一守住的地方 —— 于是它把其余几行的问题掩盖了。**
> `RemoteIEDRenderer.cpp:241` 的 `std::clamp(captured.scale, …)` 让这一块看起来有防线,而 `std::clamp` 恰好不剥 `NaN`,
> 且它的三个邻居(translate 三行、rotate 九格)连 `clamp` 都没有。判据是「逐行看,而不是逐块看」。
> 第三条:**「替换成 `0.f`」与「拒绝整个对象」的取舍,取决于这个值在语义上有没有一个「不生效」的替身。**
> §70/§89 的字段(法术强度、alpha、朝向)拿 `0.f` 只是不生效;位置与旋转拿 `0.f` 是一个合法但错误的变换。
> 所以同一族缺陷在插件层的第一处落地用的**不是**同一个修法,而这不是不一致 —— 是判据在问一个更早的问题:
> 「这个字段有没有替身」。
## 91. 2026-10-19 场次:Morph 的快照只夹了块数,没夹单块大小也没夹总条数 —— 判据是「上界要绑到自己的发送侧能产出什么,而不是绑到传输层允许什么」
> **锚点**:`Code/plugins/patches/MorphSyncTogether/0006-bound-a-single-morph-chunk-and-the-values-a-snapshot-may-carry.patch`
> (4486 B / 98 行,三个文件、五个 hunk)。测试在 `plugins/MorphSyncTogether/src/MorphSyncServiceBodyHair.cpp:60`(活实现)
> 与 `plugins/MorphSyncTogether/src/MorphSyncService.cpp:585`(被 `#include` 进来的副本),两份必须同形改。
> 就地更正:上一版把上界写进 token 循环内部(`if (values.size() >= kMaxMorphValues) { break; }`),
> 那样「拒绝」仍然要先付出整段的解析代价;改成解析**之前**的预扫描(`std::count(chunk.begin(), chunk.end(), ';') + 1` 累加),
> 代价降到与块大小同阶。

### 91.1 缺陷

`plugins/MorphSyncTogether/src/MorphSyncServiceBodyHair.cpp:50-63` 的入口门逐字:

```cpp
        if (hash == 0 || !sequence || !count || !props || *count == 0 || *count > 128 || *sequence >= *count) {
            SKSE::log::warn("MST MORPH RX invalid packet sender=\"{}\"", sender);
            return;
        }
```

它夹住了**块数**(`*count > 128`),但没有夹:

| 没夹住的东西 | 一个对端能把它撑到多大 | 后果 |
| --- | --- | --- |
| 单块字节数(`props->size()`) | 传输层的 `STRPM::kMaxPayloadBytes` = 24 KiB(而合法发送侧只有 2200) | 128 × 24 KiB ≈ 3 MB 文本进入 `_assemblies` |
| 整份快照的 value 条数 | 每条最短约 4 字符 ⇒ 约 9 万条 | 每条 `Split` + `HexDecode` + `std::stof`,每个被接受的 value **两次** `std::string` 分配 |
| 上述解析发生在哪个线程 | —— | `plugins/MorphSyncTogether/src/UdpTransport.cpp:277-285` 的 `tasks->AddTask([packet = std::move(packet)]() { … HandleUdpPacket(…) })` ⇒ **游戏线程** |

### 91.2 判据

上界的参照物是**本插件自己的发送侧能产出什么**,不是**传输层允许什么**。

- `plugins/MorphSyncTogether/src/MorphSyncService.cpp:441` 的 `constexpr std::size_t kChunkLimit = 2200;` 是发送侧自己写的切块常量;
  `:443-460` 逐 value 造 `fmt::format("{},{},{:.9g}", HexEncode(name), HexEncode(key), value)` 并在
  `:451 if (!current.empty() && current.size() + extra > kChunkLimit)` 处切块 ⇒ **合法的 `props` 永远不超过 2200 字符**。
- 一份快照就是一个 actor 自己的 morph 列表,条数由本地 RaceMenu 决定,不由对端决定。
- `docs/PITFALLS.md:10164-10167`(§79)已经写下过这个数字:「一次形变快照被切成 2200 字符的块 …… **单个信封常态就在 512 与 4096 之间**」。

⇒ 24 KiB 这个数字是**传输层**的,任何合法流量都用不到它;拿它当上界等于把上界交给对端。

### 91.3 可达性

- 入口链:`plugins/MorphSyncTogether/src/UdpTransport.cpp:211 OnMessage` → `:222 HandleMessage`(`:233` 键长门 256、`:242-251` `_senderConnections` 行数门 64)
  → `:254-263 auto packet = fmt::format("{}from={}|{}", kGameplayPrefix, sender, payload);` → `:275 QueueMessage`
  → `:277-285 SKSE::GetTaskInterface()->AddTask(…)` ⇒ 游戏线程。
- 活 handler 是 `plugins/MorphSyncTogether/src/MorphSyncServiceBodyHair.cpp:50` 那份;**死副本**在 `plugins/MorphSyncTogether/src/MorphSyncService.cpp:575`
  (`MorphSyncServiceBodyHair.cpp:5-15` 用四个 `#define` 把 `#include "MorphSyncService.cpp"`(`:11`)里的四个符号改名成 `…Legacy`,
  而 `HandleUdpPacketLegacy`(`MorphSyncService.cpp:550`)全仓零调用点)。两份是同一段源码的两个副本 ⇒ 补丁必须同时覆盖。
- 解析出来的 value 随后走 `TryApplyRemote`(`MorphSyncServiceBodyHair.cpp:277-397`),`:359-365` 逐条 `_bodyMorph->SetMorph(actor, value.morphName.c_str(), value.morphKey.c_str(), value.value);`。
- 既有的两条表上界(`kMaxRemoteSnapshots` / `kMaxRemotePubicSnapshots` = 64,由 0004 落)只限**行数**,不限每行的工作量。

### 91.4 修法

```cpp
        // The chunk count is bounded above, but neither the size of a single
        // chunk nor the number of morph values a snapshot may carry is: a peer
        // that ignores the chunker can send 128 chunks of the largest packet the
        // transport allows, and the apply path then walks every value it holds.
        // Both are bounded by what this plugin's own sender can produce -
        // BroadcastSnapshot chunks at kChunkLimit (2200 characters), so a chunk
        // twice that size cannot come from it, and a snapshot is one actor's own
        // morph list. A packet over the first cap and a snapshot over the second
        // are refused instead of being applied in part.
        static constexpr std::size_t kMaxMorphChunkBytes = 4096;
        static constexpr std::size_t kMaxMorphValues = 4096;
```

单块超限并入**已有的**入口拒绝分支(与 `*count > 128` 同形):

```cpp
        if (hash == 0 || !sequence || !count || !props || *count == 0 || *count > 128 || *sequence >= *count ||
            props->size() > kMaxMorphChunkBytes) {
```

总条数超限在解析**之前**预扫描,并把这一份 assembly 从 `_assemblies` 里删掉(它已经被判定为不可能来自本插件的发送侧):

```cpp
            std::size_t totalValues = 0;
            for (const auto& chunk : it->second.chunks) {
                if (!chunk.empty()) {
                    totalValues += static_cast<std::size_t>(std::count(chunk.begin(), chunk.end(), ';')) + 1;
                }
            }

            if (totalValues > kMaxMorphValues) {
                _assemblies.erase(it);
                SKSE::log::warn(
                    "MST MORPH RX snapshot dropped: more than {} values sender=\"{}\"",
                    kMaxMorphValues,
                    sender);
                return;
            }
```

五条要点:

1. `plugins/MorphSyncTogether/src/PCH.h` 已经含 `<cmath>`/`<algorithm>`/`<string>` 等 ⇒ 新代码不需要新增 include(与 §90 那条 IED 补丁不同)。
2. `MorphSyncService.h` 的常量块刻意放在 `static std::optional<std::string> ReadField(…)` 声明之后(pin `:111`),
   **不**贴着 0004 加的那两行表上界(pin `:128`)—— 因为 `plugin_patches.py:439-442` 要求每个补丁都能独立打到 pristine 索引上,
   两个 hunk 落在同一段上下文里会互相破坏。
3. pre-image 一律是 pin 内容,不是「pin + 已有补丁」。生成方式:`git show :<path>` 取 pin 文本 → 文本替换 → `git diff --no-index` 两个临时目录。
4. 日志字符串里的 `\"{}\"` 必须转义(照抄 0004 的 `sender=\"{}\"`),否则 `plugin_patches.py:241-258` 的 `FORMAT_FIELD` 检查会把它当格式串问题。
5. `*count > 128` 这道旧门**不删**:它挡的是「块数」,新加的两道门挡的是「单块大小」与「总条数」,三者正交。

### 91.5 复核命令(照抄)

```powershell
# 三重校验(cwd = plugins/MorphSyncTogether)
git apply --check --cached Code/plugins/patches/MorphSyncTogether/0006-bound-a-single-morph-chunk-and-the-values-a-snapshot-may-carry.patch   # 退 0
git apply --check --reverse <同一个补丁>   # 退 0
git apply --check <同一个补丁>             # 退 1,报 error: patch failed: src/MorphSyncService.h:110

# 累积验证:六个补丁依次打到 pristine 索引,再比工作树
Get-ChildItem Code/plugins/patches/MorphSyncTogether/*.patch | Sort-Object Name | ForEach-Object { git -C plugins/MorphSyncTogether apply --cached $_.FullName }
git -C plugins/MorphSyncTogether diff --stat          # 必须为空
git -C plugins/MorphSyncTogether diff --cached --stat # 13 files changed, 242 insertions(+), 12 deletions(-)
git -C plugins/MorphSyncTogether reset -q             # 还原索引

# 门禁
python Code\plugins\tools\plugin_patches.py verify    # PATCH VERIFY OK / 28 patch(es)
python Code\plugins\tools\plugin_patches.py check     # PATCH CHECK OK
python Code\plugins\tools\plugin_snapshot.py verify   # SNAPSHOT OK
python Tools\Scripts\check_docs.py                     # DOC CHECK OK
```

**验证**:`git apply --check --cached` 退 **0**、`--check --reverse` 退 **0**、前向 `--check` 退 **1** 且报
`error: patch failed: src/MorphSyncService.h:110` / `… BodyHair.cpp:57` / `… MorphSyncService.cpp:572`;
累积应用六个补丁后 `git diff --stat`(工作树 vs 累积索引)输出**空**;`git diff --cached --stat`(累积索引 vs pin)
= `13 files changed, 242 insertions(+), 12 deletions(-)`;四条门禁全绿(`PATCH VERIFY OK` / 28 patch(es)、`PATCH CHECK OK`、
`SNAPSHOT OK`、`DOC CHECK OK`)。**本机不编译**,所以「一个对端会不会这么发」不可判定。

> **教训**:第一条:**上界的参照物选错,比没有上界更危险 —— 因为它看起来已经守住了。**
> `*count > 128` 是一道真的门,它把「块数」这一维夹死了,于是这段代码读起来是有防线的;
> 但它参照的是**传输层**允许什么,而传输层的 24 KiB 是给整条通道用的,不是给一个 chunk 用的。
> 判据要问的是「这个数字是谁定的」:发送侧自己写的 `kChunkLimit = 2200` 才是「一个 chunk 能有多大」的权威。
> 第二条:**「先解析再判断」和「先判断再解析」是两种不同的修复,代价差一个数量级。**
> 第一版把条数门写进 token 循环内部,读到第 4097 条才 break —— 拒绝的代价仍然是 O(整份快照)。
> 改成对 `chunks` 做一次 `std::count` 预扫描之后,拒绝的代价降到 O(块大小),而语义完全相同。
> 第三条:**同一个符号在一份源码里出现两次时,补丁必须同时覆盖两份,哪怕其中一份不可达。**
> `MorphSyncService.cpp` 的那份 `HandleMorphPacket` 被 `#define` 改名后只由同样零调用点的 `HandleUdpPacketLegacy` 使用;
> 只改 `MorphSyncServiceBodyHair.cpp` 会让「两份副本」在下一次有人恢复 legacy 路径时分叉。
## 92. 2026-10-20 场次:插件层有一个线程入口声明了 noexcept,而它调用的每一步都在分配 —— 判据是「noexcept 是一条承诺,不是装饰」
> **锚点**:`Code/plugins/patches/STRPluginMessagingAPI/0004-keep-an-allocation-failure-out-of-the-chat-ui-observers-thread-entry.patch`
> (3999 B / 81 行,一个文件、三个 hunk)。就地更正:§62 已经写过「`noexcept` 里构造可抛的 STL 类型」,
> 那一节的落点是 `Code/server/Services/ChatService.cpp` 的 `std::regex`;**这一节是同一个判据在插件层的第一次落地**,
> 而且形状更隐蔽 —— 分配不在 `noexcept` 函数体内,而在它调的**下一层**,并且那一层自己也写着 `noexcept`。

### 92.1 缺陷

`plugins/STRPluginMessagingAPI/src/STRPMChatUiSuppressBootstrap.h`(pin 120 行)的线程入口逐字:

```cpp
        inline DWORD WINAPI Worker(void*) noexcept
        {
            std::optional<std::uintptr_t> executeAsyncAddress;

            for (std::uint32_t attempt = 0;
                 attempt < 120 && !STRPMChatUiSuppress::detail::g_stopRequested.load();
                 ++attempt)
            {
                if (!executeAsyncAddress)
                {
                    if (auto* allocationBase = ResolveExternalStrAllocationBase();
                        allocationBase != nullptr)
                    {
                        executeAsyncAddress =
                            STRPMChatUiSuppressV3::ResolveExecuteAsync(allocationBase);
                    }
                }
```

这个循环里**每一步都在分配**:

| 调用 | 分配在哪 | 声明 |
| --- | --- | --- |
| `ResolveExternalStrAllocationBase()`(`:9`) | `EnumerateMemory()` 的 `result.push_back`(`STRPMChatUiSuppress.h:167`)、`FindBytes()` 的 `result.push_back`(`:211`)、`std::vector<void*> externalAllocations`(`:28`)、`std::sort`/`erase(unique)`(`:37-38`) | 自己写着 `noexcept` |
| `STRPMChatUiSuppressV3::ResolveExecuteAsync()`(`STRPMChatUiSuppressV3.h:461`) | 又一遍 `EnumerateMemory`/`FindBytes`/`FindRipXrefs`、`std::unordered_map<std::uintptr_t, std::size_t> votes`(`:482`) | **非** noexcept |
| `STRPMChatUiSuppressV3::Arm()`(`:514`) | `AddVectoredExceptionHandler` 的注册表节点 | **非** noexcept |

三处全在 `noexcept Worker` 的调用链上 ⇒ `std::bad_alloc` 逃出线程入口就是 `std::terminate`,
**没有崩溃转储**(`docs/PITFALLS.md:4629-4697` §45)。

### 92.2 判据

**`noexcept` 是一条承诺,不是装饰** —— 它承诺「这个函数不会让异常穿过它的边界」,
而兑现这个承诺的方式只有两种:自己不抛,或者自己接住。

这条缺陷的判据不是「`Worker` 有没有分配」,而是**「`Worker` 的调用链上有没有人分配」**:

- 第一层检查(「`Worker` 体内没有 `push_back`/`resize`」)会**漏掉**它 —— 分配都在被调者体内。
- 第二层检查(「被调者是不是 `noexcept`」)也会漏掉它 —— `ResolveExternalStrAllocationBase()` 恰恰写着 `noexcept`,
  而它调的是两个非 noexcept 且都会 `push_back` 的函数。此时 terminate 发生在**被调者内部**,
  `Worker` 的 catch(如果有)够不着 ⇒ 「加一层 try/catch」和「去掉错误的那层 `noexcept`」是两件事,都要做。
- 第三层(「这条路径可达吗」)才是修不修的分界 —— 见 §92.3。

### 92.3 可达性

- `STRPMChatUiSuppressBootstrap.h:95 inline void Start() noexcept` 用 `:98-104 CreateThread(nullptr, 0, &detail::Worker, nullptr, 0, nullptr)` 起线程;
- `Start()` 的唯一调用点是 `plugins/STRPluginMessagingAPI/src/STRPluginMessagingBridgeSKSE.cpp:299`,
  就在 `:289 extern "C" __declspec(dllexport) bool SKSEPlugin_Load(const SKSEInterface*)` 内(紧随 `:300` 的 `STRPMProxyResolverBootstrapV2::Start()`、`:301` 的 `STRPMProxyResolverTrace::Start()`)
  ⇒ **每次加载这个 SKSE 插件都会起这条线程,没有任何配置能关掉它**。
- 该文件**全文件零 try/catch**;整个 `plugins/STRPluginMessagingAPI` 里只有 `STRPMChatUiSuppress::detail::Snapshot`(`STRPMChatUiSuppress.h:113-143`)、
  `STRPMChatUiSuppressV3::detail::ReadString`(`:213-235`)、`ProbeDeserializeEnvelope`(`:237`)三处有 try/catch,且都只包 `resize`。
- **同族先例证明这是偏离,不是风格**:`STRPMProxyResolverBootstrapV2.h:248` 与 `STRPMProxyResolverTrace.h:301` 的 `Worker` 都**没有** `noexcept`,
  且各自的 `Start()`(`:319` / `:329`)都用 try/catch 兜住线程创建(`:325-334` / `:336`);`STRPluginMessagingBridge.cpp:142 ReceiveDispatchLoop`、
  `:946 BootstrapLoop`、`STRPluginMessagingBridgeSKSE.cpp:193 IdentityWorker`、`STRPluginMessagingDiagnosticSKSE.cpp:387 DiagnosticWorker`、
  `STRPluginMessagingBridgeReceive.cpp:984 CaptureDispatchLoop` 全部非 noexcept。全项目只有两个 `DWORD WINAPI Worker(void*) noexcept`,见下。
- **不可达的同形副本不修**:`STRPMChatUiSuppress.h:508` 的 `Worker` 有同一个形状(`:516` 调 `ResolveStrAllocationBase()`,
  而后者在 `:218` 也写着 `noexcept` 却调非 noexcept 的 `EnumerateMemory`/`FindBytes`),但 `STRPMChatUiSuppress::Start()`(`:542`)**零调用点** ——
  全仓 1758 个源文件扫描,`&detail::Worker` 只出现在 `:550`(即 `Start()` 自身内);`STRPMChatUiSuppressV2.h` 也无人 include(只有它自己 `:3`)。
  与 §91 那份「同一段源码的两个副本」不同:这里是**两份独立实现**,其中一份死 ⇒ 只修活的那一份。

### 92.4 修法

两处改动,都不动 `noexcept` 本身(`Worker` 是 `CreateThread` 的回调,`noexcept` 是这里唯一诚实的声明):

```cpp
-        inline void* ResolveExternalStrAllocationBase() noexcept
+        inline void* ResolveExternalStrAllocationBase()
```

```cpp
             for (std::uint32_t attempt = 0;
                  attempt < 120 && !STRPMChatUiSuppress::detail::g_stopRequested.load();
                  ++attempt)
             {
-                if (!executeAsyncAddress)
+                try
                 {
```

接住之后打一行日志并退出(`STRPMChatUiSuppress::detail::Log` 只 `fopen_s`/`fprintf`/`fclose`,不分配),
**不重试**:这个循环等的是运行时解包(最多 120 次 × 1 秒),内存耗尽时重试只是再走一遍同样的分配。

三条要点:

1. `CODE_GUIDELINES.md:47` 写着「Don't use exceptions」,所以这里的 try/catch 要按**已有先例**解释 ——
   `Snapshot`(`STRPMChatUiSuppress.h:121-128`)、`ReadString`(`STRPMChatUiSuppressV3.h:221-228`)、
   `ProbeDeserializeEnvelope`(`:254-262`)都用同一形状,目的都是「不让异常穿过一个 `noexcept` 边界」。
2. 只去掉 `ResolveExternalStrAllocationBase` 的 `noexcept`,**不**顺手给 `EnumerateMemory`/`FindBytes` 加 try/catch ——
   它们本来就不是 `noexcept`,加在 `Worker` 那一层才能一次覆盖三个分配点。
3. 新注释里点名「`ResolveExternalStrAllocationBase` above is not noexcept」是刻意的:下一个人看到「有人把 `noexcept` 删了」时,
   需要知道这是为了让 `Worker` 的 handler 够得着,而不是漏写。

### 92.5 复核命令(照抄)

```powershell
# 三重校验(cwd = plugins/STRPluginMessagingAPI)
git apply --check --cached Code/plugins/patches/STRPluginMessagingAPI/0004-keep-an-allocation-failure-out-of-the-chat-ui-observers-thread-entry.patch  # 退 0
git apply --check <同一个补丁>          # 退 0(尚未应用到工作区时)
git apply <同一个补丁> && git apply --check --reverse <同一个补丁>   # 应用后退 0

# 门禁
python Code\plugins\tools\plugin_patches.py verify    # PATCH VERIFY OK / 29 patch(es)
python Code\plugins\tools\plugin_patches.py check     # PATCH CHECK OK
```

**验证**:`git apply --check --cached` 退 **0**(能打到 pin);前向 `git apply --check` 退 **0**(未应用时干净);
应用后工作区从 120 行变 **137 行**(CRLF,5345 B),`git apply --check --reverse` 退 **0**(与工作区一致);
`PATCH VERIFY OK` / **29** patch(es)、`PATCH CHECK OK`。**本机不编译**(`docs/PITFALLS.md` §0/§1),
所以「这条线程会不会真的耗尽内存」不可判定;判据不是「会不会发生」,而是「发生之后进程还剩不剩得下一条记录」。

> **教训**:第一条:**`noexcept` 的检查要顺着调用链走,不是看函数体。**
> 这一处的分配一个都不在 `Worker` 体内,全在被调者体内;只看函数体会得出「这个入口很干净」的结论。
> 第二条:**被调者也写着 `noexcept` 时,问题不是「有没有 promise」,而是「谁先 terminate」。**
> `ResolveExternalStrAllocationBase()` 的 `noexcept` 把 terminate 关在了**它自己内部**,
> 于是 `Worker` 那一层后来加的 handler 永远收不到 —— 去掉那层 `noexcept` 和加 catch 是两件必须一起做的事。
> 第三条:**「同族的其它实现都长什么样」是判断偏离最快的尺子。**
> 这个目录里另外五个线程入口、两个 bootstrap 的 `Worker` 全部非 noexcept,只有这一处例外;
> 找到例外之后,「这是风格还是失误」就不再需要争论 —— 惯例本身已经写在代码里了。
## 93. 2026-10-20 场次:STRPM 桥的接收解析器写着 noexcept,而它调用的四个查找函数全都在建 vector —— 判据是「noexcept 的检查要顺着调用链走」
> **锚点**:`Code/plugins/patches/STRPluginMessagingAPI/0005-keep-an-allocation-failure-out-of-the-receive-resolver-and-the-load-path.patch`
> (8360 B / 174 行,两个文件、两个 hunk)。§92 是同一个判据的第一次落地,那一处的分配在被调者体内、**被调者也写着 `noexcept`**;
> 这一节的两处形状都不同:第一处的四个被调者**都不写** `noexcept`(承诺只在调用者身上),
> 第二处干脆不叫 `noexcept` 这个名字的问题 —— 它顶着 Win32 的名字,调用点以为自己在调 `LoadLibraryW`。

### 93.1 缺陷

第一处,`plugins/STRPluginMessagingAPI/src/STRPluginMessagingBridgeReceive.cpp`(pin 1222 行)的接收解析器逐字:

```cpp
        bool ResolveOnConsumeAddress() noexcept
        {
            const auto module = GetModuleHandleW(nullptr);
            if (!module)
                return false;

            const auto spans = EnumerateRuntimeMemory(module);
            const auto typeDescriptor = FindTransportTypeDescriptor(spans);
```

它自己 `noexcept`,**体内零 try/catch**,而它调的四次查找全都在建容器:

| 调用 | 分配在哪 | 声明 |
| --- | --- | --- |
| `EnumerateRuntimeMemory()`(`:240`) | `std::vector<MemorySpan> result;`(`:242`)、`result.push_back({ base, size, readable, executable })`(`:264`) | 非 noexcept |
| `FindTransportTypeDescriptor()`(`:311`) | 底下的 `FindBytes()` 建 `std::vector<std::uintptr_t> result;`(`:279`)、`result.push_back(...)`(`:304`)、`std::sort`/`erase(std::unique(...))`(`:318-319`) | 非 noexcept |
| `FindCompleteObjectLocator()`(`:325`) | `std::vector<std::uintptr_t> matches;`(`:331`)、每个 1 MiB 块一个 `std::vector<std::uint8_t> snapshot;`(`:345`)、`matches.push_back(address)`(`:362`) | 非 noexcept |
| `FindTransportVftable()`(`:374`) | `matches`(`:379`)、每块一个 `snapshot`(`:393`)、`matches.push_back(vftable)`(`:419`) | 非 noexcept |

⇒ `std::bad_alloc` 从 `ResolveOnConsumeAddress` 里逃出去就是 `std::terminate`,**没有崩溃转储**(§45,`docs/PITFALLS.md:4629-4697`)。
pin 侧的对照读数:同一个文件里五个被调者逐条都是 `noexcept=no try=0 catch=0`,只有 `ResolveOnConsumeAddress` 是 `noexcept=YES try=0 catch=0`
——「分配在被调者、承诺在调用者」这一句在 pin 上可以直接数出来。

第二处,`plugins/STRPluginMessagingAPI/src/STRPMRuntimeLoadRedirect.h`(pin 56 行)的末尾逐字:

```cpp
#define LoadLibraryW STRPMRuntimeLoad::LoadLibraryAdjacentToBroker
```

`LoadLibraryAdjacentToBroker`(`:15`)写着 `noexcept`,而它体内三步都在分配:
`std::wstring adjacentPath(brokerPath, length);`(`:34`)、`adjacentPath.resize(separator + 1);`(`:37`)、`adjacentPath += modulePath;`(`:38`)
⇒ 同一个形状。判据不在它自己身上,而在**它替的是谁**:这个头以 `/FI`(强制包含)挂在 `STRPluginMessagingAPIRuntime` 上
(`plugins/STRPluginMessagingAPI/CMakeLists.txt:401-409`),于是 `plugins/STRPluginMessagingAPI/src/STRPluginMessagingAPIRuntime.cpp:876`/`:882`/`:892`
三处**看起来在调 Win32 `LoadLibraryW`**,实际调的是这个替身;调用点没有任何一处能处理 throw。

### 93.2 判据

**`noexcept` 的检查要顺着调用链走,而不是看函数体。** 第一层检查(「函数体内有没有 `push_back`/`resize`」)会**漏掉**第一处:
分配一个都不在 `ResolveOnConsumeAddress` 体内。第二层检查(「被调者是不是 `noexcept`」)在这一处**不会**漏,但它回答的是另一个问题:
四个被调者都不写 `noexcept` ⇒ 它们自己就会 `terminate`,所以能救的只有**调用者这一层的一个 catch**。

与 §92 的分界值得写清楚,因为两处的修法相反:

- §92 那处,被调者 `ResolveExternalStrAllocationBase()` **写着 `noexcept`** ⇒ terminate 被关在**被调者内部**,调用者那一层后加的 handler 永远收不到,
  所以「去掉那层 `noexcept`」和「加 catch」是两件必须一起做的事。
- 这一处,被调者**不写** `noexcept` ⇒ 一个函数级 catch 就够,`noexcept` 本身**不动**。
- 分界线是**承诺写在哪一层**,不是「有没有分配」。

第二处的判据与第一处无关,单独一条:**一个被 `#define` 顶替掉的函数,它的契约由调用点对那个名字的期待决定,不由它自己的声明决定。**
`LoadLibraryW` 的每一个调用点都在写「这个调用不会抛」,替身既然占了这个名字,就得自己接住。
这条也只有顺着调用链才看得出来 —— `#include` 图上根本没有这条边(`/FI` 是编译选项,不是源码里的依赖)。

### 93.3 可达性

- 第一处的链:`plugins/STRPluginMessagingAPI/src/STRPluginMessagingBridge.cpp:946 void BootstrapLoop()`(非 noexcept)
  → `:979 if (!g_receiveResolverReady.load())` → `:991 if (STRPMBridgeReceive::Start(&DeferredReceiveThunk, nullptr))`
  → `STRPluginMessagingBridgeReceive.cpp`(pin)`:1143 bool Start(STRPM::ReceiveCallback, void* userData) noexcept`
  → (pin)`:1161 if (!ResolveOnConsumeAddress())`(工作树 `:1199`)。
  线程由 `:1015 bool StartBootstrap()` 用 `:1024 g_bootstrapThread = std::thread(&BootstrapLoop);` 起(`:1022-1030` 有 try/catch),
  调用点在 `:1076 if (!StartBootstrap())`(在 `STRPM::Start()` 内)⇒ **每次 SKSE 插件加载都会走这条链**。
  并且失败不是终点:`BootstrapLoop` 每 `:36 constexpr auto kBootstrapRetryDelay = std::chrono::milliseconds(750);` 重试一次,**无上限、无退避**
  ⇒ 内存吃紧时这条分配路径会被反复走。
- 第二处的链:`plugins/STRPluginMessagingAPI/src/STRPluginMessagingAPIRuntime.cpp:854 static HMODULE LoadStrBridgeModule(const std::wstring& modulePath)`
  的三处调用(`:876`/`:882`/`:892`),都在 `STRPluginMessagingAPIRuntime` 的加载路径上,一次加载走一次。
  **严重性低于第一处**:调用频率是「每次插件加载一次」,而且参数是硬编码的模块名
  ⇒ 这里修的是「同一形状的第二处落点」,不是「高频路径上的必然缺陷」。
- 编译闭包:`plugins/STRPluginMessagingAPI/CMakeLists.txt:15-19 option(STRPM_BUILD_STR180_BRIDGE_PROBE … ON)`、
  `:352-356 add_library(STRPluginMessagingBridge SHARED ${STRPM_BRIDGE_SOURCE} src/STRPluginMessagingBridgeReceive.cpp src/STRPluginMessagingBridgeSKSE.cpp)`;
  `.github/workflows/windows.yml:189` 逐字 `cmake -S plugins/STRPluginMessagingAPI -B plugins/STRPluginMessagingAPI/build -A x64 -DSTRPM_ENABLE_UDP_BACKEND=OFF -DSTRPM_BUILD_STR180_BRIDGE_PROBE=ON`
  ⇒ 两个文件都真的进二进制(第二个靠 `/FI` 而不是 `#include`)。
- 同族先例:这个目录里 try/catch 只出现在四处,且都只包**一个会分配的步骤** ——
  `STRPluginMessagingBridgeReceive.cpp:165-197 SnapshotProcessMemory`(`:174-182` 逐字 `try { snapshot.resize(size); } catch (...) { snapshot.clear(); return false; }`)、
  `STRPMChatUiSuppress.h:113-143 Snapshot`、`STRPMChatUiSuppressV3.h:221-228`/`:254-262`、`STRPMChatUiSuppressBootstrap.h:55`(§92 刚加的)。
  这一节把同一形状用在**函数级**,理由见 93.4 第 2 条。

### 93.4 修法

两处改动:

```cpp
         bool ResolveOnConsumeAddress() noexcept
         {
-            const auto module = GetModuleHandleW(nullptr);
+            try
             {
+                const auto module = GetModuleHandleW(nullptr);
                 ...
+            }
+            catch (...)
+            {
+                Log("receive resolver: ran out of memory while resolving TransportService::OnConsume");
+                return false;
+            }
```

```cpp
             if (length > 0 && length < std::size(brokerPath)) {
+                try {
                 std::wstring adjacentPath(brokerPath, length);
                 ...
+                } catch (...) {
                 }
             }
```

四条要点:

1. **`noexcept` 本身不动**,两处都不动。第一处的调用者 `Start` 也是 `noexcept`,并且把 `false` 读作「还没解析出来」;
   去掉 `ResolveOnConsumeAddress` 的 `noexcept` 只会把 terminate 挪到调用方 —— 这与 §92 正好相反,那里的 `noexcept` 挡在了 handler 前面,必须去掉。
2. **包的是函数体,不是单个 `resize`**:分配点分散在四个被调者里,只有函数级 catch 能一次覆盖;
   步骤级写法在这里要写四遍,而拒绝的代价完全相同(失败即 `return false`,调用方下一轮再试)。
3. 第二处失败后**落到本来就存在的** `:46 return ::LoadLibraryW(modulePath);` —— 不新增分支、不新增返回值:
   替身做不到「就近加载」时,行为与它没被替换时完全一致。
4. 日志走文件本地的 `Log(...)`(`STRPluginMessagingBridgeReceive.cpp:126-134`,只 `fopen_s`/`fprintf`/`fclose`),**不分配**;
   接住之后**不重试** —— 内存耗尽时重试只是再走一遍同样的分配,重试由 750 ms 的 `BootstrapLoop` 自然完成。

### 93.5 复核命令(照抄)

```powershell
# 三重校验(cwd = plugins/STRPluginMessagingAPI)
git apply --check --cached Code/plugins/patches/STRPluginMessagingAPI/0005-keep-an-allocation-failure-out-of-the-receive-resolver-and-the-load-path.patch  # 退 0
git apply --check <同一个补丁>          # 退 0(尚未应用到工作区时)
git apply <同一个补丁> && git apply --check --reverse <同一个补丁>   # 应用后退 0

# 累积校验(五个补丁依次打到 pristine 索引上)
foreach ($p in Get-ChildItem Code/plugins/patches/STRPluginMessagingAPI/*.patch) { git apply --cached $p.FullName }   # 逐个退 0
git diff --stat          # 无输出(工作区与累积索引一致)
git diff --cached --stat # 11 files changed, 300 insertions(+), 91 deletions(-)
git reset -q

# 门禁
python Code\plugins\tools\plugin_patches.py verify    # PATCH VERIFY OK / 30 patch(es)
python Code\plugins\tools\plugin_patches.py check     # PATCH CHECK OK
```

**验证**:`git apply --check --cached` 退 **0**(能打到 pin);前向 `git apply --check` 退 **0**(未应用时干净);
`git apply` 退 **0**,`git apply --check --reverse` 退 **0**(与工作区一致);
打到 pristine 索引后 `git show :<path>` 与手写新文本**逐字节相等**(两个文件都比对过);
累积校验 `0001`…`0005` 依次退 **0**,`git diff --stat` 无输出,`git diff --cached --stat` = **11 files changed, 300 insertions(+), 91 deletions(-)**;
`STRPluginMessagingBridgeReceive.cpp` pin 1222 行 → 应用后 **1260 行**(工作区此前 1245 行,因为 `0003` 已在),`STRPMRuntimeLoadRedirect.h` pin 56 行 → 应用后 **65 行**;
`PATCH VERIFY OK` / **30** patch(es)。**本机不编译**(`docs/PITFALLS.md` §0/§1),
所以「这条路径会不会真的耗尽内存」不可判定;判据不是「会不会发生」,而是「发生之后 terminate 发生在哪一层、还能不能留下一条记录」。

> **教训**:第一条:**`noexcept` 的检查要顺着调用链走 —— 但「顺着走」之后要看承诺写在哪一层,修法正好相反。**
> §92 的被调者写着 `noexcept`,terminate 关在被调者内部,所以要去掉那层 `noexcept` 再加 catch;
> 这一处的被调者不写 `noexcept`,一个函数级 catch 就够,`noexcept` 一个都不动。
> 第二条:**被 `#define` 顶替掉的函数,契约由调用点对那个名字的期待决定。**
> `LoadLibraryAdjacentToBroker` 自己写着 `noexcept`,但它占的是 `LoadLibraryW` 的名字,而 `LoadLibraryW` 的每一个调用点都在假设「不会抛」;
> 这条边在 `#include` 图上不存在(`/FI` 是编译选项),所以「谁在调我」这件事必须查 CMake,不能查源码。
> 第三条:**同族的先例既是判据也是修法模板,但要看清先例包的是「步骤」还是「函数」。**
> 这个目录里四处 try/catch 都只包一个 `resize`,因为它们各自只有一个分配点;
> 这里四个被调者、至少四个容器构造点,所以包函数体,而拒绝的语义与步骤级完全相同 —— 都只是 `return false`。
## 94. 2026-10-20 场次:OStimNG 的 Scene API 七个入口都写着 noexcept,而每个函数体都在分配、指针参数又从不判空 —— 判据是「声明在别人手里的 noexcept 撤不掉,只能自己接住」
> **锚点**:`Code/plugins/patches/OStimNG/0003-keep-an-allocation-failure-out-of-the-scene-api-entries.patch`
> (11507 B / 225 行,一个文件 `skse/src/ModAPI/OstimNG-API-Main.cpp`、一个 hunk `@@ -6,106 +6,149 @@`)。
> §92 和 §93 是同一个判据的前两次落地,那两次的 `noexcept` 都写在本仓(或本插件)自己的函数上,撤掉只是本地决定;
> 这一节的 `noexcept` 写在**一份公共头里的虚函数声明**上,而消费方持有一份逐字副本 —— 承诺不在自己手里,修法就只剩「自己接住」。

### 94.1 缺陷

`plugins/OStimNG/skse/src/ModAPI/OstimNG-API-Main.cpp`(pin 132 行)里七个入口逐个写着 `noexcept`,其中六个的整个函数体都在分配。
第一个逐字:

```cpp
        APIResult SceneInterface::StartScene(const char* pluginName, RE::TESObjectREFR* furniture,
                                             const char* startingAnimation, Actor* actors[256],
                                             uint32_t* threadID) noexcept {
            std::vector<Actor*> actorList; 
            for (int i = 0; i < 256; i++)
            {
                if (!actors[i]) break; 
                actorList.emplace_back(actors[i]); 
            }

            if (actorList.size() < 1) return Result::Invalid; 
```

`noexcept` 不是写在这里的 —— 它写在 `plugins/OStimNG/skse/src/ModAPI/OstimNG-API-Scene.h:30-46` 的虚函数声明上:

```cpp
    class ISceneInterface
    {
    public:
        [[nodiscard]] virtual APIResult StartScene(const char* pluginName, RE::TESObjectREFR* furniture, const char* startingAnimation, Actor* actors[256], uint32_t* threadID) noexcept = 0;
```

而消费方 `plugins/OStimTogether/src/OStimAPI/ModSceneControl.h:19` 持有一份逐字副本(`:22` 注释逐字「Exact OStim Scene ModAPI V1 vtable prefix through StopScene. No virtual destructor.」),
`:24-29`/`:31`/`:39`/`:48`/`:58-60` 是同一批 `noexcept = 0;` ⇒ **这一侧的 `noexcept` 撤不掉**。

六个入口的分配点:

| 入口(pin 行号) | 分配在哪 | 被调者声明 |
| --- | --- | --- |
| `StartScene`(`:9`) | `std::vector<Actor*> actorList;`(`:12`)、`actorList.emplace_back`(`:16`)、`convertVector`(`:22`)、`params.startingNodes.push_back`(`:25`)、`startThread`(`:29`)、`SKSE::log::info`(`:32`) | 全非 noexcept |
| `StartCoupleScene`(`:35`) | `convertVector({ dom, sub})`(`:41`)、`getNodeById`(`:42`)、`push_back`(`:44`)、`startThread`(`:48`) | 全非 noexcept |
| `StartThreesomeScene`(`:54`) | `convertVector`(`:60`)、`getNodeById`(`:61`)、`push_back`(`:63`)、`startThread`(`:67`) | 全非 noexcept |
| `StartFoursomeScene`(`:73`) | `convertVector`(`:79`)、`getNodeById`(`:80`)、`push_back`(`:82`)、`startThread`(`:86`) | 全非 noexcept |
| `StopScene`(`:92`) | `thread->stopFaded()`(`:96`) → `Thread.cpp:694 std::thread fadeThread = std::thread([&] { … });` | 非 noexcept |
| `SetAutoMode`(`:99`) | `thread->startAutoMode()`(`:103`) → `ThreadAutoControl.cpp:162 std::string action = …;`、`:163 getRandomNode(...)` | 非 noexcept |
| `TryGetAutoMode`(`:106`) | **无** —— `ThreadManager::GetThread()`(`Core/ThreadManager.cpp:108`)只取 `std::shared_lock`,`Thread::isInAutoMode()`(`Core/Thread.h:163`)inline 返回 `bool` | 非 noexcept,但体内零分配 |

被调者的分配点逐条:

```cpp
    std::vector<GameActor> GameActor::convertVector(std::vector<RE::Actor*> actors) {   // GameAPI/GameActor.cpp:10,形参按值
        std::vector<GameActor> ret;                                                     // :11
        for (RE::Actor* actor : actors) ret.push_back(actor);                            // :13
```

```cpp
    int startThread(ThreadStartParams& params) {                                        // Core/ThreadStarter/ThreadStarter.cpp:13
        std::vector<GameAPI::GameActor> actorSet;                                        // :20
        GameAPI::Game::notification("actor " + actor.getName() + " is not eligible for OStim");  // :24(字符串拼接)
        std::vector<Trait::ActorCondition> conditions = Trait::ActorCondition::create(params.actors);  // :42
```

`ThreadStartParams` 在 `plugins/OStimNG/skse/src/Core/ThreadManager.h:17 int startThread(ThreadStartParams params);` 与 `:50 int startThreadNoLock(ThreadStartParams params);` 都是**按值** ⇒ 一次 `startThread` 还要多拷一整包。

同一批入口的第二个缺陷:**指针参数从不判空**。`StartScene` 的 `:15 if (!actors[i]) break;` 在 `actors` 本身为 null 时直接解引用 null;
`*threadID = Threading::startThread(params);`(`:29`/`:48`/`:67`/`:86`)与 `*autoMode = thread->isInAutoMode();`(`:110`)同理;
`startingAnimation` 更绕一层:`Graph::GraphTable::getNodeById(std::string id)`(`plugins/OStimNG/skse/src/Graph/GraphTable/GraphTableNodes.cpp:144`)**按值**吃 `std::string`,传 null 就是从 null 构造 `std::string`(UB)。

### 94.2 判据

**`noexcept` 写在声明上还是定义上,决定了修法能不能从这一侧做。**
§92 的 `noexcept` 在一个 handler 上,去掉它 terminate 就消失,所以那一节去掉;§93 的 `noexcept` 在一个内部函数上,去掉只会把 terminate 挪给调用方,所以那一节保留、加 catch;
这一节的 `noexcept` 在**一份本仓不拥有的公共头的虚函数声明**上,消费方 `ModSceneControl.h` 还持有一份逐字副本 ——
去掉它会让定义与声明的异常规格不一致(定义写 `noexcept`、声明不写,或反过来,都是 ODR 层面的不一致),
而**保留声明、任由异常逃出去**,terminate 就落在调用方 `plugins/OStimTogether/src/CoopSessionManager.cpp:655` 那一帧上(该文件 1275 行的四处 try/catch 在 `:85-87`/`:99-101`/`:877-919`/`:990-1009`,与 `:607`/`:693` 两个入口都不相干)。
⇒ 承诺撤不掉,那就**自己接住**。

**第二个判据:同族里谁判了指针、谁没判,是最快的一把尺子。**
兄弟 API `plugins/OStimNG/skse/src/ModAPI/OstimNG-API-Thread.cpp`(685 行,39 个 `noexcept` 函数)每一个指针参数都先判:
`:297 if (!outData) return;`、`:374 if (!outData) return false;`、`:394 if (!data) return APIResult::Invalid;`、`:426 if (!query || !buffer || bufferSize == 0) return 0;`、`:567 if (!actorFormIDs || actorCount == 0) return false;`。
Scene 侧是这唯一一处两者都缺的。

### 94.3 可达性

- 唯一调用点 `plugins/OStimTogether/src/CoopSessionManager.cpp:655 const auto result = _sceneControl->StartScene(kPluginName, furniture, snapshot.nodeID.c_str(), actors.data(), &newThreadID);`,
  在 `:611 void CoopSessionManager::StartApprovedOwnerSession(std::uint64_t sessionID)`(非 noexcept,体内无 try)内;
  `:631 std::array<RE::Actor*, 256> actors{};`、`:654 std::uint32_t newThreadID = 0;` ⇒ 三个指针参数都非 null,但 `snapshot.nodeID` 可能为空串(空串**不是** UB,只是查不到节点)。
  重试路径 `:687-697 void CoopSessionManager::RetryStartApprovedOwnerSession(std::uint64_t)` 用 `std::thread([this, sessionID]() { sleep_for(kRestartRetryDelay); … tasks->AddTask([this, sessionID]() { StartApprovedOwnerSession(sessionID); }); }).detach();`
  ⇒ 失败会被重试,与 §93 的 750 ms 循环同形。
- `StopScene` 三个调用点 `plugins/OStimTogether/src/CoopSessionManager.cpp:456`、`:927`、`plugins/OStimTogether/src/ProxyOnlyThreadCleanup.cpp:160`;`plugins/OStimTogether/src/OStimBridge.cpp:2863 StopScene(`;
  **三个 `StartXScene` 与 `SetAutoMode`/`TryGetAutoMode` 全仓零调用点** —— 它们是给别的插件用的公开 ABI,所以这一节修的是「公开入口自己该守住的东西」,不是「本仓某条路径必然触发」。
- 接口获取:`plugins/OStimTogether/src/CoopSessionManager.cpp:154 reinterpret_cast<void*>(GetProcAddress(module, "RequestPluginAPI_Scene"))`、`OStimBridge.cpp:1185`、`ProxyOnlyThreadCleanup.cpp:36`;
  持有者 `CoopSessionManager.h:202 OStimModAPI::Scene::ISceneInterface* _sceneControl{ nullptr };`、`OStimBridge.h:347`、`ProxyOnlyThreadCleanup.h:29`。
- 编译闭包:`plugins/OStimNG/skse/CMakeLists.txt:15 include(cmake/lib/autoCollectSources.cmake)`、`:18 add_src_folder(src)`、`:71-83 file(GLOB_RECURSE sources …)`、`:96 SOURCES ${headers} ${sources}`,
  `plugins/OStimNG/skse/cmake/lib/autoCollectSources.cmake:1-14 add_src_folder(path)` 用 `file(GLOB_RECURSE src_files "${path}/*.cpp")` ⇒ `ModAPI/OstimNG-API-Main.cpp` 确实被编译;
  但 OStimNG 是 GLOB 源列表,`plugin_patches.py:191-211 uncompiled_patch_targets` 对它返回 `[]`(跳过闭包检查)⇒ 这一节的闭包结论只能靠读 CMake 得到,门禁不替我们看。
- 异常开关:OStimNG 全部 7 个 CMakeLists/.cmake 文件里 `EHsc`/`fno-exceptions`/`_HAS_EXCEPTIONS`/`target_compile_options`/`CMAKE_CXX_FLAGS` 命中数 **0** ⇒ 异常没被关掉,`catch (...)` 可用;
  本仓的 `CODE_GUIDELINES.md:47`「Don't use exceptions」约束的是控制流,不是「不让异常穿过一个 `noexcept` 边界」。
- 同族先例:`plugins/OStimNG/skse/src/Util/VectorUtil.cpp:11`/`:22` 是 OStimNG 自己的 `try { … } catch (const std::exception& ex) {}`;全目录 `plugins/OStimNG/skse/src` 普查 try=3 catch=2;
  本仓另一侧四处先例见 §93.3。

### 94.4 修法

```cpp
         APIResult SceneInterface::StartScene(const char* pluginName, RE::TESObjectREFR* furniture,
                                              const char* startingAnimation, Actor* actors[256],
                                              uint32_t* threadID) noexcept {
+            if (!pluginName || !actors || !threadID) return Result::Invalid;
+            try {
             std::vector<Actor*> actorList; 
                 ...
+            }
+            catch (...) {
+                SKSE::log::error("Scene API: StartScene failed with an exception");
+                return Result::Failed;
+            }
```

```cpp
-            Graph::Node* node = Graph::GraphTable::getNodeById(startingAnimation);
+            Graph::Node* node = startingAnimation ? Graph::GraphTable::getNodeById(startingAnimation) : nullptr;
```

```cpp
            if (!thread) return Result::Invalid;
+            if (!thread || !autoMode) return Result::Invalid;   // TryGetAutoMode 只改这一行,不加 try
```

四条要点:

1. **`noexcept` 一个都不动**。声明在本仓不拥有的头里、消费方还持逐字副本,单边撤销只会造成「声明与定义的异常规格不一致」;
   而 `noexcept` 不进 vtable 布局,所以保留它对 ABI 是零成本,代价只是必须自己接住。
2. **六个入口加 try、第七个不加**,理由是实读的:`TryGetAutoMode` 只取一次 `std::shared_lock` 再 inline 读一个 `bool`,体内零分配;
   给零分配的函数套 try/catch 会让读者以为那里有分配点。
3. **接住之后返回 `Result::Failed`,不重试**:调用方 `CoopSessionManager.cpp:687-697` 本来就有 `RetryStartApprovedOwnerSession`,
   这里再退避只是把同一段分配走两遍(与 §93 第 4 条同一条理由)。
4. **`startingAnimation` 不改 `getNodeById` 的签名**,只在四个调用点加三目守卫:改签名会动到 `GraphTable.h:47` 这一层公开接口,
   而三目之后本来就落进已有的 `if (node)` 分支 —— 改的是「不制造 UB」,不是「换一个语义」。

### 94.5 复核命令(照抄)

```powershell
# 三重校验(cwd = plugins/OStimNG)
git apply --check --cached Code/plugins/patches/OStimNG/0003-keep-an-allocation-failure-out-of-the-scene-api-entries.patch  # 退 0
git apply --check <同一个补丁>          # 退 0(尚未应用到工作区时)
git apply <同一个补丁> && git apply --check --reverse <同一个补丁>   # 应用后退 0
git apply --check <同一个补丁>          # 已应用时退 1:error: patch failed: skse/src/ModAPI/OstimNG-API-Main.cpp:6

# 累积校验(三个补丁依次打到 pristine 索引上)
foreach ($p in Get-ChildItem Code/plugins/patches/OStimNG/*.patch) { git apply --cached $p.FullName }   # 逐个退 0
git diff --stat          # 无输出(工作区与累积索引一致)
git diff --cached --stat # 10 files changed, 339 insertions(+), 121 deletions(-)
git reset -q

# 门禁
python Code\plugins\tools\plugin_patches.py verify    # PATCH VERIFY OK / 31 patch(es)
python Code\plugins\tools\plugin_patches.py check     # PATCH CHECK OK
```

**验证**:`git apply --check --cached` 退 **0**(能打到 pin);前向 `git apply --check` 退 **0**(未应用时干净);
`git apply` 退 **0**,`git apply --check --reverse` 退 **0**(与工作区一致);再次前向 `--check` 退 **1** 并报 `error: patch failed: skse/src/ModAPI/OstimNG-API-Main.cpp:6`;
打到 pristine 索引后 `git show :skse/src/ModAPI/OstimNG-API-Main.cpp` 与手写新文本**逐字节相等**(`git hash-object pin/a.cpp` = `git rev-parse :<path>` = `75bc52c4ef4fbabc8d4bffdd2cf32d65c938a76b`);
累积校验 `0001`/`0002`/`0003` 依次退 **0**,`git diff --stat` 无输出,`git diff --cached --stat` = **10 files changed, 339 insertions(+), 121 deletions(-)**;
`OstimNG-API-Main.cpp` pin 132 行 → 应用后 **175 行**。**本机不编译**(`docs/PITFALLS.md` §0/§1,`plugins/OStimNG/skse/AGENTS.md` 逐字「do not attempt to build c++ or cmake」),
所以「这条路径会不会真的耗尽内存」不可判定;判据不是「会不会发生」,而是「发生之后 terminate 落在哪一层、还能不能留下一条记录」。

> **教训**:第一条:**`noexcept` 写在谁手里,决定了修法能不能从这一侧做。**
> §92 的写在 handler 上 ⇒ 去掉;§93 的写在内部函数上 ⇒ 保留 + catch;这一节的写在一份公共头的虚声明上、消费方还持逐字副本 ⇒ 只能自己接住。
> 同一条判据(「检查要顺着调用链走」)在三次落地里给出了三种修法,差别全在「承诺属于谁」。
> 第二条:**同族里谁判了指针、谁没判,比任何风格争论都快。**
> `OstimNG-API-Thread.cpp` 39 个 `noexcept` 函数每一个指针参数都先判,而 Scene 侧七个入口一个都不判 —— 找到这个例外之后,「这是失误」就不再需要论证。
> 第三条:**判空要看参数被拿去做了什么,不是看它是不是指针。**
> `startingAnimation` 的危险不在于「可能为 null」,而在于 `getNodeById` **按值**吃 `std::string` —— 从 null 构造 `std::string` 是 UB;
> 所以修法落在调用点(三目),而不是去改一个公开接口的签名。
## 95. 2026-10-20 场次:OStimNG 的菜单在后台线程删掉线程对象之后仍然解引用它 —— 判据是「判空要判在解引用那一刻,不是判在函数入口」
> **锚点**:`Code/plugins/patches/OStimNG/0004-keep-a-dead-thread-out-of-the-menus-that-dereference-it.patch`
> (9466 B / 210 行,7 个文件 14 个 hunk:`UIState.h`/`UIState.cpp`/`ThreadManager.cpp`/`AlignMenu.cpp`/`SceneMenu.cpp`/`SceneOptions.cpp`/`SearchMenu.cpp`)。

### 95.1 缺陷

UI 层持有两个 `public` 裸指针,`plugins/OStimNG/skse/src/UI/UIState.h:49-50` 逐字:

```cpp
        Threading::Thread* currentThread;
        Graph::Node* currentNode;
```

两个都没有初始化器(`:51 private:` 才开始私有段)。清空只清一半 —— `plugins/OStimNG/skse/src/UI/UIState.cpp:106-110` 逐字:

```cpp
    void UIState::HandleThreadRemoved(Threading::Thread* thread) {
        if (currentThread == thread) {
            currentThread = nullptr;
        }
    }
```

**`currentNode` 一次都不清**,而它记的是 `SetThread` 那一刻从线程里取出的节点(`UI/UIState.cpp:78-79`:`currentThread = thread; currentNode = thread->getCurrentNodeInternal();`)。

清空写在后台线程里。`plugins/OStimNG/skse/src/Core/ThreadManager.cpp:204-219` 的 `stopThreadNoLock` 顺序逐字:

| 行 | 语句 |
| --- | --- |
| `:209` | `UI::UIState::GetSingleton()->HandleThreadRemoved(thread);` |
| `:210` | `m_threadMap.erase(threadID);` |
| `:211` | `thread->close();` |
| `:212` | `delete thread;` |

它唯一的调用点在 `:51`,位于 `:38` 起的后台线程 `m_excitementThread` 的循环里(`:44 if (!GameAPI::Game::isPaused())` → `:50-53 for (ThreadId& threadID : threadStopQueue) stopThreadNoLock(threadID);`),
而 UI 的读取发生在游戏线程 ⇒ **写指针的线程不是读指针的线程,两边都没有同步**。

同一条族里还有一条比 nullptr 更糟的路径:`plugins/OStimNG/skse/src/Core/ThreadManager.cpp:133-142` 的 `UntrackAllThreads` 逐字
`for (auto& entry : m_threadMap) { delete entry.second; } m_threadMap.clear();` —— **完全绕过 `HandleThreadRemoved`**,
留下的是非空悬垂指针。它的唯一调用者 `plugins/OStimNG/skse/src/Serial/Manager.cpp:290-299 Revert(...)` 是读档/回滚,注释逐字「this is a force close due to the user loading another save」。

### 95.2 判据

**第一条:判空的位置比判空的存在更重要。** 全树 15 处解引用这两个指针时不判空,其中 5 处落在延迟执行的 lambda 里:
`UI/OStimMenu.h:13-19` 的 `Update()` 逐字 `while (!_taskQueue.empty()) { auto& task = _taskQueue.front(); task(); _taskQueue.pop(); }`,
`:63-67 QueueUITask` 只入队,`UI/OStimMenu.cpp:69-74 AdvanceMovie` 才调 `Update()` ⇒ **队列里的 lambda 在下一帧执行**;`SKSE::GetTaskInterface()->AddTask(...)` 同理。
「帧 N 入队 → 后台线程在 N 与 N+1 之间停线程 → 帧 N+1 执行」这三步就是一次空指针解引用。

(**2026-10-20 更正**:上面记的「全树 15 处」里已修 14 处,第 15 处 `plugins/OStimNG/skse/src/ModAPI/OstimNG-API-Thread.cpp:439 SearchScenes` **没有修** —— 该文件在 pin 里的 blob 是 CRLF(26928 B / 685 个 CR),而 `.gitattributes:38 Code/plugins/patches/**/*.patch text eol=lf` 要求补丁本身纯 LF,同法生成的补丁对索引 `git apply --check --cached` 退 **1** 并报 `error: patch failed: skse/src/ModAPI/OstimNG-API-Thread.cpp:437`,只有对工作树的前向 `--check` 才退 0 ⇒ **不可打补丁**;且该入口全仓**零调用点**,按「不可达不修」记为不修。)

**第二条:同族里谁判了、谁没判,比任何风格争论都快。** 同一个 `plugins/OStimNG/skse/src/ModAPI/OstimNG-API-Thread.cpp` 里 24 处 `GetThread(threadID)` **每一处都紧跟判空**;
`UI/Scene/SceneMenu.cpp:152-159` 的 `UpdateSpeed` 逐字先 `auto thread = UI::UIState::GetSingleton()->currentThread; if (!thread) { return; }`,再取 `currentNode` 并同样判空;
`UI/Align/AlignMenu.cpp:83-85`、`UI/UIState.cpp:15-18`、`UI/UIState.cpp:85-97 NodeChanged` 也都是先判后用。⇒ 这 15 处是 **24:1** 的惯例差。

**第三条:入口处的门守的是另一件事。** `Core/ThreadManager.cpp:144-146 AnySceneRunning()` 逐字 `return m_threadMap.size() > 0;` —— **不是**「`currentThread` 非空」,
所以 `UIState::HandleControl` 首行那道门拦不住这一族;而且 `close()` 只在 `playerThread` 时才 `UI::EndControlledScene()`(`Core/Thread.cpp:738-744`),
`EndControlledScene()` 又只做 `HideMenus()` + `CloseActiveMenu()`(`UI/UIState.h:82-85`),**都不碰这两个指针** ⇒ 指针为空的窗口比菜单关闭更早。

**第四条:返回值也要判,不只是参数。** `getFurnitureTypeInternal()` 的默认值是裸空 —— `plugins/OStimNG/skse/src/Core/Thread.h:226` 逐字 `Furniture::FurnitureType* furnitureType = nullptr;`;
同族 `Core/ThreadActor.cpp:538` 已按「可能为空」写(`if (Furniture::FurnitureType* furnitureType = thread->getFurnitureTypeInternal()) {`),而 `SceneMenu.cpp:138`/`SearchMenu.cpp:154` 直接 `->isChildOf(...)`。

### 95.3 可达性

- 停线程:键盘/脚本触发 `Thread::queueThreadStop`(`Core/Thread.cpp:684`,唯一调用者)→ `ThreadManager::queueThreadStop`(`:127-131`)→ 后台线程 `:51` → `stopThreadNoLock`。**不需要任何异常输入**,正常的「结束场景」就是这条路径。
- 迁移:`ThreadManager::migrateThread`(`:230-431`)在 `:351` 也调 `HandleThreadRemoved`,但它的 `delete` 在 `:388-427` 的 detached 线程里,中间隔着 `:390 sleep_for(FADE_OUT_MS = 500)` + `:402 sleep_for(startDelayMs)` ⇒ **指针为空的窗口至少半秒**,窗口内任何已入队的菜单 lambda 都会读到空指针。
- 读档:`Serial/Manager.cpp:290-299 Revert(...)` → `UntrackAllThreads`(`:133-142`)⇒ 悬垂指针,直到下一次 `SetThread`。
- 全仓 `delete` 一个 `Thread` 只有三处(`Core/ThreadManager.cpp:138`/`:212`/`:394`),`UIState::currentThread` 是唯一非拥有型读者 ⇒ 不需要第四方持有者的假设就能触发。

### 95.4 修法

不改任何公开签名,只把「用之前先取局部、取到之后再判空」补上:

1. `UIState.h:49-50` 两个成员就地补 `= nullptr` —— 单例是静态存储,补初值消掉的是「读初值前的未定义」,不是这一族的主要触发路径。
2. `UIState.cpp:106-110` 在清 `currentThread` 的同一个分支里补 `currentNode = nullptr;` —— `currentNode` 只在 `SetThread` 里被写成某个线程的节点,所以线程没了它就没有归属。
3. `ThreadManager.cpp:137-139` 让 `UntrackAllThreads` 在 `delete` 之前逐个调 `UI::UIState::GetSingleton()->HandleThreadRemoved(entry.second);`,与 `stopThreadNoLock` 同一形状 —— 这样三条销毁路径(停止/回滚/迁移)都经过同一个出口。
4. `AlignMenu.cpp` 的 `LoadCurrentAlignment`/`ToggleActor`/`Increment` 各取一次局部指针再判空;`ToggleActor` 另加 `currentNode->actors.empty()` —— 原式 `selectedSlot > currentNode->actors.size() - 1` 在空容器上是无符号回绕,判空之后仍然会越界。
5. `SceneMenu.cpp` 的 `BuildMenuData` 把 `currentNode`/`currentThread`/`getFurnitureTypeInternal()` 各取一次局部并判空(`getFurnitureTypeInternal()` 提到循环外),`ChangeAnimation`/`ChangeSpeed` 的 lambda 内先取局部再判空。
6. `SceneOptions.cpp` 的 `auto& thread = UI::UIState::GetSingleton()->currentThread;` 改成 `auto thread = …` 并紧跟 `if (thread == nullptr) continue;` —— **引用绑定的空指针不会因为后面判空而变得安全**,`auto&` 与 `auto` 在这里是两个不同的东西。
7. `SearchMenu.cpp` 的 `Search` 在 `thread == nullptr` 时 `AssignData(data); return;` —— 保留「无结果」的既有语义,而不是让菜单停在上一次的结果上。

### 95.5 复核命令(照抄)

```powershell
# 三重校验(cwd = plugins/OStimNG)
git apply --check --cached Code/plugins/patches/OStimNG/0004-keep-a-dead-thread-out-of-the-menus-that-dereference-it.patch  # 退 0
git apply --check <同一个补丁>          # 退 0(尚未应用到工作区时)
git apply <同一个补丁> && git apply --check --reverse <同一个补丁>   # 应用后退 0
git apply --check <同一个补丁>          # 已应用时退 1:error: patch failed: skse/src/Core/ThreadManager.cpp:135

# 累积校验(四个补丁依次打到 pristine 索引上)
foreach ($p in Get-ChildItem Code/plugins/patches/OStimNG/*.patch) { git apply --cached $p.FullName }   # 逐个退 0
git diff --stat          # 无输出(工作区与累积索引一致)
git diff --cached --stat # 16 files changed, 387 insertions(+), 143 deletions(-)
git reset -q

# 门禁
python Code\plugins\tools\plugin_patches.py verify    # PATCH VERIFY OK / 32 patch(es)
python Code\plugins\tools\plugin_patches.py check     # PATCH CHECK OK
```

**验证**:`git apply --check --cached` 退 **0**(能打到 pin);前向 `git apply --check` 退 **0**;`git apply` 退 **0**(带两行 `warning: 2 lines add whitespace errors.`),`git apply --check --reverse` 退 **0**;
再次前向 `--check` 退 **1** 并报 `error: patch failed: skse/src/Core/ThreadManager.cpp:135`;
补丁内 7 个 pin blob 的哈希与 `git rev-parse :<path>` **逐个相等**(`UIState.h` `0d0a5d5f`、`UIState.cpp` `51494985`、`ThreadManager.cpp` `2be63d4d`、`AlignMenu.cpp` `64a36861`、`SceneMenu.cpp` `68d56baa`、`SceneOptions.cpp` `67ad326e`、`SearchMenu.cpp` `6efe1b58`);
累积校验 `0001`…`0004` 依次退 **0**,`git diff --stat` 无输出,`git diff --cached --stat` = **16 files changed, 387 insertions(+), 143 deletions(-)**;
`git apply` 的两行空白告警逐字来自 pin 里本就带尾空白的行(`if(results[i]->actors.size() == thread->getActorCount() ` 行尾 1 空格、`});    ` 行尾 4 空格),不是本补丁引入的。
**本机不编译**(`docs/PITFALLS.md` §0/§1,`plugins/OStimNG/skse/AGENTS.md` 逐字「do not attempt to build c++ or cmake」),
所以「后台线程与菜单读取会不会真的撞上」不可判定;判据不是「会不会发生」,而是「发生之后读到的是空指针还是已析构对象」。

> **教训**:第一条:**判空要判在解引用那一刻,不是判在函数入口。**
> 这一族的窗口不在函数里,而在「入队」与「执行」之间 —— 只要中间可能跨帧或跨线程,入口处的判空就只是一张过期的收据。
> 第二条:**先取局部再判空,顺序反了等于没判。**
> `auto& thread = …currentThread;` 之后再 `if (thread == nullptr)` 是无效的:引用一旦绑定就不可改;`state->currentThread->…` 连判空的机会都没有。
> 第三条:**同一个缺陷往往有两条销毁路径,只修看得见的那条等于没修。**
> `stopThreadNoLock` 通知了 UI,`UntrackAllThreads` 没有 —— 后者是读档,更常见;修法是让三条路径都经过同一个出口,而不是在每处读取点各判一次。
## 96. 2026-10-20 场次:OStimNG 的输入回调在两次查询之间丢掉线程,而家具搜索把选项上限算出来却不用 —— 判据是「判空和用指针之间不能再隔一次查询」

> **锚点**:两处修复,同一份插件、两种形状:
> `Code/plugins/patches/OStimNG/0005-keep-a-deleted-player-thread-out-of-the-input-handler.patch`
> (3465 B / 54 行,单文件 `skse/src/Events/EventListener.cpp`、2 个 hunk;`796ffd3`→`1952eec`,pin 199 行 → 应用后 212 行)、
> `Code/plugins/patches/OStimNG/0006-honour-the-message-box-option-limit-when-listing-furniture.patch`
> (712 B / 14 行,单文件 `skse/src/Furniture/Furniture.cpp`、1 个 hunk;`223b6bf`→`ba08674`)。

### 96.1 缺陷

**第一处:同一个问题被问了两遍,而两个答案之间隔着一次删除。** `plugins/OStimNG/skse/src/Events/EventListener.cpp:81` 逐字

```cpp
            if (Threading::ThreadManager::GetSingleton()->playerThreadRunning()) {
```

`:83` 再逐字

```cpp
                Threading::Thread* playerThread = Threading::ThreadManager::GetSingleton()->getPlayerThread();
```

两次调用各自持一次 `m_threadMapMtx` 的 `shared_lock`(`plugins/OStimNG/skse/src/Core/ThreadManager.cpp:148-156 playerThreadRunning` 与 `:117-125 getPlayerThread`),**两次之间不持有任何锁**;
而 `getPlayerThread()` 在表里找不到 `isPlayerThread()` 的条目时返回 `nullptr` —— 它自己不判空。`:84` 立刻解引用这个指针:

```cpp
                Furniture::selectFurniture(playerThread->getActorCount(), playerThread->getCenter(), MCM::MCMTable::furnitureSearchDistance(), 96.0f, true, [playerThread](GameAPI::GameObject furniture) {
                    playerThread->changeFurniture(furniture, nullptr);
                });
```

同族里另外四处连这道门都没有:`:149 Threading::ThreadManager::GetSingleton()->getPlayerThread()->stopFaded();`、
`:151 …->increaseSpeed()`、`:155 …->decreaseSpeed()`、`:160 …->pullOut()`。

**第二处:算出来的上限没有读者。** `plugins/OStimNG/skse/src/Furniture/Furniture.cpp:149` 逐字

```cpp
        int max = std::min(static_cast<int>(furniture.size()), GameAPI::Game::getMessageBoxOptionLimit() - 1);
```

而 `:150` 逐字 `for (int i = 0; i < furniture.size(); i++) {` —— `max` 算完之后一次都没被读过。
`:151`/`:152` 把 `furniture[i].first->name` 与 `furniture[i].second` 成对塞进 `options`/`objects`,于是按钮数与 `objects` 长度都等于 `furniture.size()`。
上限本身属于游戏本体:`plugins/OStimNG/skse/src/GameAPI/Game.h:17` 逐字 `inline static int getMessageBoxOptionLimit() { return 9; }`,
而 `plugins/OStimNG/skse/src/GameAPI/Game.cpp:56 messagebox->buttonText.push_back(text.c_str());` 往 `RE::MessageBoxData` 里逐条塞按钮。

### 96.2 判据

**第一条:判空和用指针之间不能再隔一次查询。** 「线程在跑」是一句关于某一刻的话,而指针必须与这句话取自同一刻。只隔 16 行就是正确写法 ——
`EventListener.cpp:165` 逐字 `Threading::Thread* thread = Threading::ThreadManager::GetSingleton()->getPlayerThread();`、`:166 if (thread) {`;
`plugins/OStimNG/skse/src/Core/Thread.cpp:624-640 Thread::stopFaded()`(pin 行号;工作区在 `:687-703`,因为 0001–0004 先给该文件加了 63 行)里那条 detached 线程逐字 `Thread* thread = ThreadManager::GetSingleton()->getPlayerThread(); if (thread) { thread->stop(); }`;
`plugins/OStimNG/skse/src/Core/Thread/ThreadFurniture.cpp:15-26` 同形。⇒「取一次局部、就地判空」是这个文件的既定写法,上面 5 处是例外。

**第二条:异步出口会把按值捕获的指针搬进后续帧。** `Furniture.cpp:146 if (alwaysAsk || MCM::MCMTable::selectFurniture()) {` 在调用点传 `alwaysAsk = true` 时恒真,于是 `:155` 走 `GameAPI::Game::showMessageBox(..., callback)`;
`plugins/OStimNG/skse/src/GameAPI/Game.cpp:31-58` 的两条出口**都是异步**(UIExtensions 路径 `:45 vm->DispatchStaticCall("OSKSE", "UIExtMessageBox", args, scriptCallback);` 之后 `:47 return;`;否则 `:58 RE::MessageBoxMenu::QueueMessage(messagebox);`)⇒ `:84` 捕获的指针在后续帧才被解引用。
而它捕获之后真的会解引用:`plugins/OStimNG/skse/src/Core/Thread/ThreadFurniture.cpp:10-11` 逐字 `void Thread::changeFurniture(GameAPI::GameObject furniture, Graph::Node* node) {` / `if (!furniture || this->furniture == furniture) return;` —— 第一件事就是读 `this->furniture`。

**第三条:算出来的上限必须出现在唯一的消费者身上。** 全树 `std::min(static_cast<int>` 只有 `Furniture.cpp:149` 一处命中 ⇒ 这不是「参数顺序抄错」,是「压根没用」。
两个兄弟用了:`plugins/OStimNG/skse/src/Core/ThreadStarter/DebugNPCThreadStarter.cpp:33` 与 `plugins/OStimNG/skse/src/Core/ThreadStarter/PlayerThreadStarter.cpp:108` 都逐字 `int max = std::min<int>(GameAPI::Game::getMessageBoxOptionLimit() - 1, actors.size());`,并在 `while (i < max)` 里读它。

**第四条:上限的归属决定它能不能被绕过。** `Game.cpp:56` 逐条 `push_back` 进 `RE::MessageBoxData::buttonText` ⇒ 按钮数是游戏本体画的,插件这一侧多塞一条就是多一个按钮。

### 96.3 可达性

- 0005:不需要任何异常输入。`Thread::stop()`(`plugins/OStimNG/skse/src/Core/Thread.cpp:620-622`)只做一件事 —— `ThreadManager::GetSingleton()->queueThreadStop(m_threadId);`;
  真正删除在后台线程 `m_excitementThread`(`Core/ThreadManager.cpp:38-56`)的循环里,那里 `:51 for (ThreadId& threadID : threadStopQueue) stopThreadNoLock(threadID);`;
  而输入回调在游戏线程(`plugins/OStimNG/skse/src/Main.cpp:66 RE::BSInputDeviceManager::GetSingleton()->AddEventSink(Events::EventListener::GetSingleton());`)⇒ 按下「结束场景」键的那一刻,就是这条窗口。
- 0006:只需要搜索半径(`MCM::MCMTable::furnitureSearchDistance()`)内、同一层上超过 8 件可用家具 —— `Furniture::findFurniture`(`Furniture.cpp:15-`)用 `util::iterate_attached_cells` 扫整个 cell。

### 96.4 修法

不改任何公开签名:

1. 0005 五处一律「取局部 → 判空 → 用局部」;`:84` 那条额外**去掉按值捕获**,改成回调触发时重新 `getPlayerThread()` 再判空 —— 一个可能已经析构的指针不该被搬进后续帧。
2. 0006 `:150` 改为 `for (int i = 0; i < max; i++) {`。`options` 与 `objects` 同步截断,`:156 if (result == 0 || result > objects.size())` 的边界判断因此保持成立(两者长度始终一致)。

### 96.5 复核命令(照抄)

```powershell
# 三重校验(cwd = plugins/OStimNG)
git apply --check --cached Code/plugins/patches/OStimNG/0005-*.patch   # 退 0
git apply --check        Code/plugins/patches/OStimNG/0005-*.patch   # 退 0(尚未应用到工作区时)
git apply                Code/plugins/patches/OStimNG/0005-*.patch && git apply --check --reverse Code/plugins/patches/OStimNG/0005-*.patch   # 应用后退 0
git apply --check        Code/plugins/patches/OStimNG/0005-*.patch   # 已应用时退 1:error: patch failed: skse/src/Events/EventListener.cpp:81

# 累积校验(六个补丁依次打到 pristine 索引上)
foreach ($p in Get-ChildItem Code/plugins/patches/OStimNG/*.patch) { git apply --cached $p.FullName }   # 逐个退 0
git diff --cached --stat # 18 files changed, 408 insertions(+), 151 deletions(-)
git reset -q

# 门禁
python Code\plugins\tools\plugin_patches.py verify    # PATCH VERIFY OK / 34 patch(es)
python Code\plugins\tools\plugin_patches.py check     # PATCH CHECK OK
```

**验证**:`0005`/`0006` 各自 `git apply --check --cached` 退 **0**、前向 `git apply --check` 退 **0**;
合并应用后 `git apply --check --reverse` 退 **0**,再次前向 `--check` 退 **1** 并报 `error: patch failed: skse/src/Events/EventListener.cpp:81` 与 `error: patch failed: skse/src/Furniture/Furniture.cpp:147`;
两个补丁里的 pin blob 哈希与 `git rev-parse :<path>` 逐个相等(`skse/src/Events/EventListener.cpp` `796ffd3`、`skse/src/Furniture/Furniture.cpp` `223b6bf`);
累积校验 `0001`…`0006` 依次退 **0**,`git diff --cached --stat` = **18 files changed, 408 insertions(+), 151 deletions(-)**。
**本机不编译**(`docs/PITFALLS.md` §0/§1,`plugins/OStimNG/skse/AGENTS.md` 逐字「do not attempt to build c++ or cmake」),
所以「两次查询之间线程会不会真的被删掉」不可判定;判据不是「会不会发生」,而是「发生之后 `:84` 读的是空指针还是已析构对象」—— 修复前两条都有。

> **教训**:第一条:**「它在跑」是一句关于某一刻的话,指针必须取自同一刻。**
> `playerThreadRunning()` 与 `getPlayerThread()` 之间不持有锁,等于把两句不同时刻的话拼成一句用;`stopThreadNoLock` 的 `delete` 正好落在这条缝里。
> 第二条:**同一个族里正确写法常常就在十几行之外,找到它就是找到了判据。**
> `EventListener.cpp:165-166`、`Core/Thread.cpp:624-640`、`Core/Thread/ThreadFurniture.cpp:15-26` 都是「取局部 + 判空」,只有这 5 处例外 —— 例外本身就是证据。
> 第三条:**算了不用的上限,比没有上限更容易骗过审阅。**
> `int max = std::min(...)` 摆在那里,看上去已经封顶;而唯一的消费者读的是 `furniture.size()`。查「这个值有没有读者」比查「这个值算得对不对」更快。
## 97. 2026-10-20 场次:DAV 解码一件盔甲的激活 addon 列表时,长度由对端决定而每个元素都要过一次引擎查找 —— 判据是「上限要绑到自己的发送侧能产出什么」

> **锚点**:`Code/plugins/patches/DAVSyncTogether/0003-bound-the-active-armor-addon-list-a-single-payload-may-carry.patch`
> (1405 B / 33 行,2 个文件各 1 个 hunk;`src/NetworkProtocol.cpp` `6d0b2bf`→`a9bc4fe`、`src/NetworkProtocol.h` `ff1e445`→`c6ce500`)。

### 97.1 缺陷

**列表长度由对端决定,而解码器不设上限,并且每个元素都要过一次引擎查找。**
`plugins/DAVSyncTogether/src/NetworkProtocol.cpp:142-186 DecodeArmorState` 解
`DAVSTATE|armo=…|state=…|variant=…|active=…` 的 `active=` 字段,`:168-183` 逐字

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

循环次数只由 `active=` 有多长决定。每个 token 走 `:84-104 DecodeIdentity`,其中 `:100-102` 逐字

```cpp
            if (auto* resolved = identity.Resolve()) {
                identity.runtimeFormID = resolved->GetFormID();
            }
```

而 `FormIdentity::Resolve()`(`plugins/DAVSyncTogether/src/FormIdentity.cpp:18-29`)在 `plugin` 非空时逐字
`return dataHandler->LookupForm(localFormID, plugin);` ⇒ **一个 token 一次按「插件名 + 局部 ID」的引擎查找**,两个入参都是对端给的。

全 DAV 只有两个 `kMax*` 常量,都来自 STRPM 头:
`plugins/DAVSyncTogether/include/STRPluginMessagingAPI/STRPluginMessagingAPI.h:21 kMaxPayloadBytes = 24 * 1024` 与同文件的 `kMaxChannelLength`;
`plugins/STRPluginMessagingAPI/src/STRPluginMessagingBridgeReceive.cpp:901` 逐字
`if (totalHex > static_cast<std::size_t>(STRPM::kMaxPayloadBytes) * 2)`、`:920` 逐字
`if (!decoded || decoded->size() > STRPM::kMaxPayloadBytes)` —— 那两道闸门盯的是**载荷字节数**,不是**列表条数**。

### 97.2 判据

**第一条:上限要绑到自己的发送侧能产出什么,而不是绑到传输层允许什么。** 发送侧就在同一个文件:
`NetworkProtocol.cpp:116-140 EncodeArmorState` 的 `:126-131` 逐字

```cpp
            for (std::size_t i = 0; i < armor.activeArmorAddons.size(); ++i) {
                if (i != 0) {
                    active += ',';
                }
                active += EncodeIdentity(armor.activeArmorAddons[i]);
            }
```

而这个 vector 由 `plugins/DAVSyncTogether/src/DAVProbe.cpp:175-182` 填:

```cpp
            std::vector<RE::FormID> activeRuntimeAddons;
            if (const auto it = activeAddons.find(armor->GetFormID()); it != activeAddons.end()) {
                activeRuntimeAddons = it->second;
                for (const auto runtimeID : it->second) {
                    state.activeArmorAddons.push_back(MakeFormIdentity(runtimeID));
                }
            }
            SortAndUniqueIdentities(state.activeArmorAddons);
```

`activeAddons` 来自 `DAVProbe::VisitArmorNodes`(`:391-412`):递归走 `player->Get3D()` 的节点树,按节点名里解析出的 `(ARMA)/(ARMO)` 归到该件盔甲名下。
⇒ 一件盔甲的 addon 数 = **这件盔甲当前挂了多少个 ARMA 节点**,原版与常见 mod 都在个位到十几个的数量级;64 已是它的数倍。
传输层那个 24 KiB 换算下来约一万两千个 token,只能用来判「包大不大」,判不了「一件盔甲该有几个 addon」。

**第二条:同族的修法已经写好了三遍。** `Code/plugins/patches/IEDSyncTogether/0002-bound-the-live-peer-table-and-the-decoded-object-list.patch` 的
`static constexpr std::size_t kMaxObjects = 512;` 与 `kMaxRemoteStates = 64;`、
`Code/plugins/patches/MorphSyncTogether/0006-bound-a-single-morph-chunk-and-the-values-a-snapshot-may-carry.patch` 的 `kMaxMorphValues`、
`Code/plugins/patches/TradeTogether/0002-bound-the-peer-name-and-request-id-tables.patch` —— 四处同一句话:**列表长度由对端给,解码器必须自己封顶。**

**第三条:拒收还是截断,取决于短一截的那个东西下游当不当它完整。** `plugins/DAVSyncTogether/src/DAVNetworkService.cpp:164-174` 逐字

```cpp
        bool activeValid = true;
        for (auto& identity : message->activeArmorAddons) {
            auto* resolved = identity.Resolve();
            auto* addon = resolved ? resolved->As<RE::TESObjectARMA>() : nullptr;
            if (!addon) {
                activeValid = false;
                identity.runtimeFormID = 0;
            } else {
                identity.runtimeFormID = addon->GetFormID();
            }
        }
```

`activeValid` 只问「列表里每个元素解没解开」,**不问「列表全不全」**;短一截照样让 `:185 const bool valid = resolvedArmor != nullptr && activeValid;` 为真。
⇒ 与 IED 的 `kMaxObjects` 同类:整包拒收。

**第四条:拒收路径本来就存在,不需要新增分支。** `DAVNetworkService.cpp:154-158` 逐字

```cpp
        auto message = DecodeArmorState(received.payload);
        if (!message) {
            SKSE::log::warn("DAVST STRPM RX malformed payload connection={} sequence={}", received.connectionID, received.sequence);
            return;
        }
```

### 97.3 可达性

- 需要 `active=` 字段极长且逗号密集,并且其中每个 token 都能被 `DecodeIdentity` 解出来(`:93` 逐字 `if (!plugin || plugin->empty() || !local)` 会把解不出的 token 变成整包拒收)。
  单包 24 KiB 上限把规模限制在约一万两千个 token 量级 ⇒ **是「有界但仍可观」的放大,不是无限增长**;这也正是它必须按「自己发送侧的数量级」而不是「传输层的字节数」封顶的原因。
- 落点不需要异常输入:`DAVNetworkService.cpp:165-174` 会对解码结果**再逐元素 `Resolve()` 一遍** ⇒ 同一份列表过一次引擎查找两次(解码时一次、应用前一次)。

### 97.4 修法

不改任何公开签名、不动编码侧:

1. `plugins/DAVSyncTogether/src/NetworkProtocol.h:15` 的 `RemoteArmorState` 里加
   `static constexpr std::size_t kMaxActiveArmorAddons = 64;`(带四行理由注释);
2. `src/NetworkProtocol.cpp:172` 的 `if (!token.empty()) {` 之后加
   `if (result.activeArmorAddons.size() >= RemoteArmorState::kMaxActiveArmorAddons) { return std::nullopt; }`。

### 97.5 复核命令(照抄)

```powershell
# 三重校验(cwd = plugins/DAVSyncTogether)
git apply --check --cached Code/plugins/patches/DAVSyncTogether/0003-*.patch   # 退 0
git apply --check        Code/plugins/patches/DAVSyncTogether/0003-*.patch   # 退 0(尚未应用到工作区时)
git apply                Code/plugins/patches/DAVSyncTogether/0003-*.patch && git apply --check --reverse Code/plugins/patches/DAVSyncTogether/0003-*.patch   # 应用后退 0
git apply --check        Code/plugins/patches/DAVSyncTogether/0003-*.patch   # 已应用时退 1:error: patch failed: src/NetworkProtocol.cpp:170

# 累积校验(三个补丁依次打到 pristine 索引上)
foreach ($p in Get-ChildItem Code/plugins/patches/DAVSyncTogether/*.patch) { git apply --cached $p.FullName }   # 逐个退 0
git diff --stat          # 空
git diff --cached --stat # 4 files changed, 49 insertions(+), 3 deletions(-)
git reset -q

# 门禁
python Code\plugins\tools\plugin_patches.py verify    # PATCH VERIFY OK / 35 patch(es)
python Code\plugins\tools\plugin_patches.py check     # PATCH CHECK OK
```

**验证**:`0003` 的 `git apply --check --cached` 退 **0**、前向 `git apply --check` 退 **0**;
应用后 `git apply --check --reverse` 退 **0**,再次前向 `--check` 退 **1** 并报
`error: patch failed: src/NetworkProtocol.cpp:170` 与 `error: patch failed: src/NetworkProtocol.h:14`;
补丁里的两个 pin blob 哈希与 `git rev-parse :<path>` 相等(`src/NetworkProtocol.cpp` `6d0b2bf`、`src/NetworkProtocol.h` `ff1e445`);
累积校验 `0001`…`0003` 依次退 **0**,`git diff --stat` 空、`git diff --cached --stat` = **4 files changed, 49 insertions(+), 3 deletions(-)**。
**本机不编译**(`docs/PITFALLS.md` §0/§1),所以「对端到底能不能构造出这么长的 `active=`」不可判定;
判据不是「会不会发生」,而是「发生之后每一件盔甲会不会多出一万次引擎查找」。

> **教训**:第一条:**上限要绑到自己的发送侧能产出什么,而不是绑到传输层允许什么。**
> 同一个文件里 `EncodeArmorState` 写着这个列表是怎么来的,`VisitArmorNodes` 写着它有多少个元素;24 KiB 只说明包能多大。
> 第二条:**「下游有守卫」要问守卫盯的是什么量。**
> `STRPluginMessagingBridgeReceive.cpp:901`/`:920` 盯的是载荷字节数,它对「列表里有几个元素」一言不发 —— 守卫盯着长度,不等于拦得住条数。
> 第三条:**拒收还是截断,取决于短一截的那个东西下游当不当它完整。**
> `activeValid` 只问每个元素解没解开;列表短一截它照样为真,所以这里只能整包拒收。
## 98. 2026-10-20 场次:STRPM 的两份日志重定向冒充 fopen_s 却继承它的 noexcept,而它们改用的 std::filesystem 每写一行都要分配 —— 判据是「替换一个 C 函数时,noexcept 要按替换后的实现重判」

> **锚点**:`Code/plugins/patches/STRPluginMessagingAPI/0006-keep-an-allocation-failure-out-of-the-log-redirect.patch`
> (7300 B / 169 行,2 个文件各 1 个 hunk;`src/STRPMLogRedirect.h` `3e63576`→`bf44fb5`、`src/STRPMDiagnosticLogRedirect.h` `48d002c`→`bec2293`)。

### 98.1 缺陷

**一个函数把自己写成 `noexcept`,而它替换掉的那个函数本来不抛、它自己会抛。**
`plugins/STRPluginMessagingAPI/src/STRPMLogRedirect.h:19-22` 逐字

```cpp
inline errno_t STRPMRedirectFopen(
    FILE** file,
    const char* filename,
    const char* mode) noexcept
```

`:76` 逐字 `#define fopen_s STRPMRedirectFopen` —— 从这一行往下,这个文件里的 `fopen_s` 不再是 C 运行时的那个。
`plugins/STRPluginMessagingAPI/CMakeLists.txt:423` 逐字
`/FI${CMAKE_CURRENT_SOURCE_DIR}/src/STRPMLogRedirect.h` 把它强制包含进 `STRPluginMessagingBridge` 的每一个 TU;
`:415` 用同样的 `/FI` 把 `src/STRPMDiagnosticLogRedirect.h` 注入 `STRPluginMessagingDiagnostic`。
`git grep -n "LogRedirect" -- *.cpp *.h CMakeLists.txt` 只命中 `CMakeLists.txt` 的那三行(408/415/423)
⇒ **没有任何源文件 `#include` 它们,改这两份头文件等于改两个目标的全部日志出口。**

### 98.2 判据

**判据一(载重):`noexcept` 是对实现写的,不是对签名继承的。**
被替换的 `fopen_s` 不抛;替换它的这个函数在 `:39` 逐字

```cpp
            std::filesystem::path logDirectory(documents);
```

之后还有 `:42-44` 的三次 `logDirectory /= L"My Games" / L"Skyrim Special Edition" / L"SKSE"`、
`:47 std::filesystem::create_directories(logDirectory, error);`、`:50 const auto logPath = logDirectory / L"STRPluginMessagingBridge.log";` ——
**四次分配,全部发生在一次 `fopen_s` 调用之内**,而它把原签名的 `noexcept` 一起继承了下来。

**判据二:删 `noexcept` 不构成修法,因为上一层也是 `noexcept`。**
经这一个宏走到那四次分配的九个函数全是 `noexcept`:

| 文件:行 | 函数 |
| --- | --- |
| `src/STRPluginMessagingBridgeReceive.cpp:124` | `void Log(const char* text) noexcept`(`:129` 即那次 `fopen_s`) |
| `src/STRPluginMessagingBridgeReceive.cpp:134` | `void LogAddress(const char* label, std::uintptr_t value) noexcept` |
| `src/STRPluginMessagingBridgeSKSE.cpp:43` | `void LogIdentity(const char* format, ...) noexcept` |
| `src/STRPMProxyResolverBridge.h:126` | `void Log(const char* format, ...) noexcept` |
| `src/STRPMChatUiSuppress.h:73` / `:83` | `inline void Log(const char* text) noexcept` / `LogAddress(…) noexcept` |
| `src/STRPMChatUiSuppress.h:508` | `inline DWORD WINAPI Worker(void*) noexcept` |
| `src/STRPMChatUiSuppressV3.h:282` / `:361` | `inline void MarkPendingEnvelope() noexcept` / `LogExecuteAsyncResolution(…) noexcept` |

异常只是从被调者挪到调用者,`std::terminate` 与「没有崩溃转储」两件事一件不少。
同族先例就在隔壁:`src/STRPMRuntimeLoadRedirect.h` 用同样的宏替换 `LoadLibraryW`,既有补丁 `0005` 给它选的也是
`try { … } catch (...) { }` 加落到本来就有的兜底,而不是删 `noexcept`。

**判据三:换掉的如果是「谁都不会去 catch」的位置,就只能就地收口。**
日志是最后一站 —— 没有调用者会为一行日志处理 `std::bad_alloc`。这九个函数又都跑在**每一行日志**上,
不是启动一次、也不是每个包一次。

### 98.3 可达性

- 落点不需要异常输入:每一次日志写入都会经过这四次分配,包括 `Worker` 的候选扫描(`src/STRPMChatUiSuppress.h:516-531`)与
  `src/STRPMProxyResolverBridge.h` 的 `Log`(`:741`/`:803`/`:831`/`:972`/`:1024`/`:1028`/`:1103`/`:1114`/`:1129`/`:1151`)。
- 诊断侧更窄:唯一 TU `src/STRPluginMessagingDiagnosticSKSE.cpp` 全文件 24 个函数**一个 `noexcept` 都没有**,
  危险只来自 `STRPMDiagnosticRedirectFopen` 自己;但它同样必须包,否则终止点落在函数体内。
- 桥侧 `src/STRPMChatUiSuppressV2.h` 是死文件(`git ls-files` 全树只有 `:5 namespace STRPMChatUiSuppressV2` 一处提及,无 `#include`),
  它的 `:125` 站点不构成可达路径。

### 98.4 修法

两份头文件各把 `SHGetKnownFolderPath` 守卫块里的那一段包进 `try { … } catch (...) { }`,
失败时落到函数本来就有的 `:71 return ::fopen_s(file, filename, mode);` —— **不改去向,只让失败不再终止**。

1. `CoTaskMemFree(documents)` 原来紧跟在 `std::filesystem::path logDirectory(documents);` 之后,那行一旦抛出 `documents` 就泄漏;
   所以释放移进 `try` 并紧跟 `documents = nullptr;`,`try` 之后再补 `if (documents != nullptr) CoTaskMemFree(documents);` —— 既不泄漏也不双重释放。
2. 两份文件的缩进风格不同(`src/STRPMLogRedirect.h:53`/`:59` 是同行花括号,`src/STRPMDiagnosticLogRedirect.h:53-54`/`:60-61` 是另起一行),
   补丁按原文各自保留。

### 98.5 复核命令(照抄)

```powershell
# 三重校验(cwd = plugins/STRPluginMessagingAPI)
git apply --check --cached Code/plugins/patches/STRPluginMessagingAPI/0006-*.patch   # 退 0
git apply --check        Code/plugins/patches/STRPluginMessagingAPI/0006-*.patch   # 退 0(尚未应用到工作区时)
git apply                Code/plugins/patches/STRPluginMessagingAPI/0006-*.patch && git apply --check --reverse Code/plugins/patches/STRPluginMessagingAPI/0006-*.patch   # 应用后退 0
git apply --check        Code/plugins/patches/STRPluginMessagingAPI/0006-*.patch   # 已应用时退 1:error: patch failed: src/STRPMLogRedirect.h:36

# 累积校验(六个补丁依次打到 pristine 索引上)
foreach ($p in Get-ChildItem Code/plugins/patches/STRPluginMessagingAPI/*.patch) { git apply --cached $p.FullName }   # 逐个退 0
git diff --stat          # 空
git diff --cached --stat # 13 files changed, 386 insertions(+), 141 deletions(-)
git reset -q

# 门禁
python Code\plugins\tools\plugin_patches.py verify    # PATCH VERIFY OK / 36 patch(es)
python Code\plugins\tools\plugin_patches.py check     # PATCH CHECK OK
```

**验证**:`0006` 的 `git apply --check --cached` 退 **0**、前向 `git apply --check` 退 **0**;
应用后 `git apply --check --reverse` 退 **0**,再次前向 `--check` 退 **1** 并报
`error: patch failed: src/STRPMLogRedirect.h:36` 与 `error: patch failed: src/STRPMDiagnosticLogRedirect.h:36`;
补丁里两个 pin blob 哈希与 `git rev-parse :<path>` 相等(`src/STRPMLogRedirect.h` `3e63576`、`src/STRPMDiagnosticLogRedirect.h` `48d002c`);
累积校验 `0001`…`0006` 依次退 **0**,`git diff --stat` 空、`git diff --cached --stat` = **13 files changed, 386 insertions(+), 141 deletions(-)**。
**本机不编译**(`docs/PITFALLS.md` §0/§1),所以「这条路径到底会不会耗尽内存」不可判定;
判据不是「会不会发生」,而是「发生之后这行日志还剩不剩得下一条记录」。

> **教训**:第一条:**替换一个 C 函数时,`noexcept` 要按替换后的实现重判,不能按被替换的签名继承。**
> `fopen_s` 不抛,`STRPMRedirectFopen` 会抛,而 `:22` 的 `noexcept` 是从前者那里抄来的 —— 签名相同不等于承诺相同。
> 第二条:**删 `noexcept` 之前先看上一层有没有。**
> 这九个调用者全是 `noexcept`,删掉被调者的那个只是把终止点往上挪一层,`std::terminate` 与「没有崩溃转储」一件不少。
> 第三条:**`#define` 顶替标准函数的地方,判据要落在「它现在到底做了什么」上。**
> `:76` 那一行之后 `fopen_s` 已经不是 C 运行时的 `fopen_s`;`git grep` 只命中 `CMakeLists.txt` 的 `/FI` 三行,说明没有任何源文件能提示你这一点。

## 99. 2026-10-20 场次:STRPM 桥的映射函数写着 noexcept,而其中三个跨 DLL 调用会分配的导出、五个自己建容器 —— 判据是「noexcept 的边界画在哪一层,就要把 try 写在哪一层」

> **锚点**:`Code/plugins/patches/STRPluginMessagingAPI/0007-keep-an-allocation-failure-out-of-the-proxy-resolver-mapping-table.patch`
> (12270 B / 282 行,单文件单 hunk `@@ -729,136 +729,204 @@`;`src/STRPMProxyResolverBridge.h` `55eabd3`→`34dcb17`)。

### 99.1 缺陷

**一个 `noexcept` 函数调用了另一个 DLL 里会抛的函数,而那个函数没写 `noexcept`。**
`plugins/STRPluginMessagingAPI/src/STRPMProxyResolverBridge.h:732-746` 逐字

```cpp
        void ReportResolvedMapping(
            STRPM::ConnectionID connectionID,
            STRPM::ProxyFormID formID) noexcept
        {
            if (const auto report = GetReportProxyMapping())
            {
                const auto result = report(connectionID, formID);
```

`report` 由 `:727-729` 的 `GetModuleHandleW(L"STRPluginMessagingAPI.dll")` + `GetProcAddress(module, "STRPM_ReportProxyMapping")` 取得,
落在 `src/STRPMProxyResolverRuntime.h:254 STRPM_EXPORT STRPM::Result STRPM_CALL STRPM_ReportProxyMapping(…)` ——
**这个导出不写 `noexcept`**,而体内 `:107` 逐字 `std::vector<Listener> listeners;`、`:136` 逐字 `listeners = _listeners;`。
`:748`/`:754` 的两个薄壳与 `:261`/`:267` 两个导出同形(`:153`/`:177`、`:167`/`:184`)。
⇒ **抛出点在 API DLL,终止点在桥 DLL。**

同一批里另外四个是自己建容器:`:760-783 ObserveSender`(`:773 g_connectionToPlayer[connectionID] = playerId;`)、
`:785-808 ObservePlayerProxy`(`:792 std::vector<STRPM::ConnectionID> connections;`、`:795`、`:799`)、
`:810-836 RemovePlayerProxy`(`:815`、`:827`)、`:848-862 FlushMappings`(`:850`、`:857`)。

### 99.2 判据

**判据一(载重):`noexcept` 的边界画在哪一层,异常就在哪一层的帧里变成 terminate。**
这八个函数的最外层调用者是 `src/STRPMProxyResolverBridge.h:932 LONG CALLBACK ExceptionHandler(EXCEPTION_POINTERS*)`、
`src/STRPMProxyResolverTrace.h:201` 的同名函数、以及两个 `void Worker(std::stop_token token)`(`Bridge.h:1088`、`src/STRPMProxyResolverBootstrapV2.h`)——
**四个调用者全都不写 `noexcept`**。终止点之所以存在,只因为桥侧把 `noexcept` 写在了里面这八个函数上。

**判据二:删 `noexcept` 不是修法,因为上一层有的写了、有的没写。**
`src/STRPMProxyResolverTrace.h:158 bool CaptureLifecycleCall(CONTEXT* context) noexcept` 体内 `:185`/`:194` 调 `ObservePlayerProxy`/`RemovePlayerProxy`;
它的唯一调用者 `:265` 在非 `noexcept` 的 `ExceptionHandler` 里 ⇒ 删掉被调者的 `noexcept`,终止点就落在 `CaptureLifecycleCall` 这一帧,
`std::terminate` 与「没有崩溃转储」(`docs/PITFALLS.md` §45)一件不少。
同一文件里既有的四处先例(`:203-218 SnapshotProcessMemory`、`:627-647 ReadString`、`:652-679 PeekRawEnvelope`、`:1139-1153 Start()`)
给的都是同一个形状:`try` + `catch (...)` + 记日志 + 返回失败值,**不重试**。

**判据三:薄壳也要包,因为它才是跨边界那一跳的执行者。**
`ReportResolvedMapping`/`RemoveResolvedMapping`/`ClearRuntimeMappings` 三个函数体内没有任何容器,
只看「谁分配」会漏掉它们;但异常正是在它们自己的帧里穿过 `noexcept` 边界的。
⇒ 本次包的函数是 **八个**,不是五个。

### 99.3 可达性

| 函数 | 调用点 | 调用点所在的函数是否 `noexcept` |
| --- | --- | --- |
| `ReportResolvedMapping` | `Bridge.h:782`/`:807`/`:861` | 是(三个调用者都在 `noexcept` 函数体内) |
| `RemoveResolvedMapping` | `Bridge.h:780`/`:835` | 是 |
| `ClearRuntimeMappings` | `Bridge.h:845` | 是 |
| `ObserveSender` | `Bridge.h:886`、`src/STRPluginMessagingBridgeSKSE.cpp:91` | 是 / 否 |
| `ObservePlayerProxy` | `Trace.h:185`、`Bridge.h:990` | 是(`CaptureLifecycleCall`) / 否(`ExceptionHandler`) |
| `RemovePlayerProxy` | `Trace.h:194`、`Bridge.h:996` | 是 / 否 |
| `ClearMappings` | `Bridge.h:1029`(`kDisconnected` 分支) | 否 |
| `FlushMappings` | `Bridge.h:1132`、`src/STRPMProxyResolverBootstrapV2.h:312` | 否 |

四条链都可达:`ObserveIncomingEnvelope`(`Bridge.h:864-887 noexcept`)由 `:949` 的 `EXCEPTION_BREAKPOINT` 分支调用;
`FlushMappings` 在两个 Worker 的第二段每秒循环里(`Bridge.h:1132` 后跟 `SleepInterruptible(token, std::chrono::seconds(1))`)。

### 99.4 修法

在 `:760` 前插 8 行理由注释,再给八个函数各包 `try { … } catch (...) { Log(…); }`,失败只记一行日志、不重试。
函数体其余部分逐字照抄 pin,只整体缩进 4 格。三个薄壳的 catch 里没有可用的局部量,只能带上 `connectionID`。

### 99.5 复核命令(照抄)

```powershell
# 三重校验(cwd = plugins/STRPluginMessagingAPI)
git apply --check --cached ../../Code/plugins/patches/STRPluginMessagingAPI/0007-*.patch   # 退 0
git apply --check        ../../Code/plugins/patches/STRPluginMessagingAPI/0007-*.patch   # 退 0(尚未应用到工作区时)
git apply                ../../Code/plugins/patches/STRPluginMessagingAPI/0007-*.patch && git apply --check --reverse ../../Code/plugins/patches/STRPluginMessagingAPI/0007-*.patch   # 应用后退 0
git apply --check        ../../Code/plugins/patches/STRPluginMessagingAPI/0007-*.patch   # 已应用时退 1:error: patch failed: src/STRPMProxyResolverBridge.h:729

# 累积校验(七个补丁依次打到 pristine 索引上)
foreach ($p in Get-ChildItem ../../Code/plugins/patches/STRPluginMessagingAPI/*.patch) { git apply --cached $p.FullName }   # 逐个退 0
git diff --stat          # 空
git diff --cached --stat # 13 files changed, 526 insertions(+), 213 deletions(-)
git reset -q

# 门禁
python Code\plugins\tools\plugin_patches.py verify    # PATCH VERIFY OK / 37 patch(es)
python Code\plugins\tools\plugin_patches.py check     # PATCH CHECK OK
```

**验证**:`0007` 的 `git apply --check --cached` 退 **0**、前向 `git apply --check` 退 **0**;
应用后 `git apply --check --reverse` 退 **0**,再次前向 `--check` 退 **1** 并报
`error: patch failed: src/STRPMProxyResolverBridge.h:729` 与 `error: src/STRPMProxyResolverBridge.h: patch does not apply`;
补丁里的 pin blob 哈希与 `git rev-parse :src/STRPMProxyResolverBridge.h` 相等(`55eabd3`);
累积校验 `0001`…`0007` 依次退 **0**,`git diff --stat` 空、`git diff --cached --stat` = **13 files changed, 526 insertions(+), 213 deletions(-)**。
**本机不编译**(`docs/PITFALLS.md` §0/§1),所以「这条路径到底会不会耗尽内存」不可判定;
判据不是「会不会发生」,而是「发生之后这八个帧里哪一个是最后一道 `noexcept`」。

> **教训**:第一条:**`noexcept` 的边界画在哪一层,就要把 `try` 写在哪一层。**
> 三个薄壳体内一行容器都没有,可异常正是在它们自己的帧里撞上 `noexcept` 的 —— 只看「谁分配」会漏掉它们。
> 第二条:**跨 DLL 的 noexcept 要两边一起看。**
> 桥侧写着 `noexcept`、API 侧的导出没写,签名不同不代表承诺相同;`GetProcAddress` 之后那一次调用就是边界。
> 第三条:**删 `noexcept` 之前,先数清上一层的调用者里有几个也写了 `noexcept`。**
> `CaptureLifecycleCall` 写了,两个 VEH 处理器和两个 Worker 没写 —— 删掉被调者的,终止点只会换个帧。
## 100. 2026-10-21 场次:服务端的邀请过期清理遍历的是一个没有任何实体携带的组件 —— 判据是「全树只有这一个 view 指向没有挂载点的组件」

> **锚点**:`Code/server/Services/PartyService.cpp:88-110`(9 insertions / 3 deletions,单 hunk `@@ -85,10 +85,16 @@ void PartyService::OnUpdate(const UpdateEvent& acEvent) noexcept`)。

### 100.1 缺陷

`PartyService::OnUpdate` 里那段循环的唯一作用是每 10 秒把过期的队伍邀请擦掉,而它遍历的是一个**没有任何实体携带**的组件:

```cpp
// Code/server/Services/PartyService.cpp:88(修复前)
    auto view = m_world.view<PartyComponent>();
    for (auto entity : view)
    {
        auto& partyComponent = view.get<PartyComponent>(entity);
        auto itor = std::begin(partyComponent.Invitations);
        while (itor != std::end(partyComponent.Invitations))
```

`view<T>()` 遍历的是「携带 `T` 的实体集合」。没有实体携带 `T`,循环体一次都不执行 ⇒ **邀请从来没有被擦除过一次**。

### 100.2 判据

判据不是「这段循环写得对不对」,而是「**全树还有没有第二个 view 指向没有挂载点的组件**」。对着 `Code/` 全树取差集(见 100.5 的脚本),结果只有这一条:

| 事实 | 实测 |
| --- | --- |
| 全树 `emplace/emplace_or_replace/try_emplace/replace/insert_or_assign/emplace_back<PartyComponent>` | **0 处** |
| 全树 `view<PartyComponent>` | **1 处** |
| 被 `view` 的组件总数 | 22 个,差集里只有 `PartyComponent` |
| 组件现在的宿主 | `Code/server/Game/Player.h:54 PartyComponent m_party;` —— **`Player` 的成员,不是实体组件** |
| `Invitations` 的读者 | 只有这个 view;客户端 `Code/client/Services/PartyService.h:30` 有一对同名的,是另一个成员 |

同族正确写法有 **29 处**:`GetParty()` 的读者 `GameServer.cpp:788/810/845`、`PartyService.cpp:47/52/64/116/138/163/204/212/249/260/284/285/332/333/391`、`QuestService.cpp:39/175/215/231`、`MapService.cpp:26/37`、`OverlayService.cpp:45/47/118`、`CharacterService.cpp:556`、`WeatherService.cpp:34` —— **全部走 `Player*`**。

### 100.3 可达性

写入点是 `Code/server/Services/PartyService.cpp:303`(`OnPartyInvite` 里 `inviteePartyComponent.Invitations[pInviter] = cExpiryTick;`),读取点是 `:330`(`OnPartyAcceptInvite` 里 `if (selfPartyComponent.Invitations.count(pInviter) == 0) return;`)。
上界由写者决定,不由传输决定:键是 `Player*`,一个队伍的玩家数 ≤ `uMaxPlayerCount`(默认 8),玩家断线时整个 `Player`(含 `m_party`)随 `PlayerManager::Remove` 销毁 ⇒ **这不是无界增长,是过期语义失效**:邀请在 `cExpiryTick = GetTick() + 60000` 之后仍然被服务端接受。
客户端 `Code/client/Services/Generic/PartyService.cpp:66-74` 早因本地 `m_invitations` 过期而拒发,但服务端没有对应门禁。

### 100.4 修法

改成遍历玩家管理器,与其余 29 处读者一致(擦除循环体逐字照抄 pin,只把取容器的两行换掉):

```cpp
// Code/server/Services/PartyService.cpp:95(修复后)
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

`GetPlayerManager()` 的 range-for 在本仓另有 **25 处**先例(`Code/server/GameServer.cpp:328/722/731/762/805/837/1049` 等);`PlayerManager::Iterator`(`Code/server/Game/PlayerManager.h:9-26`)**只前进不删除**,所以遍历中改 `Player` 的成员是安全的。

### 100.5 复核命令(照抄)

```powershell
# 1. 组件有没有挂载点(应输出 0 处;修复后的注释行不算)
git grep -n -E '(emplace|emplace_or_replace|try_emplace|replace|insert_or_assign|emplace_back)<[^>]*PartyComponent' -- .

# 2. 全树还有几个 view 指向它(应只剩 PartyService.cpp 里这一处;修复后不再有 view)
git grep -n -E 'view\s*<\s*PartyComponent' -- .

# 3. 唯一曾经的挂载点是被谁删的
git log --oneline -S 'view<PartyComponent>'
git log --oneline -S 'emplace<PartyComponent>'

# 4. 同族正确写法有多少(应 29 处,全部走 Player*)
git grep -n 'GetParty()' -- Code/server

# 5. 差集脚本:被 view 的组件 - 有挂载点的组件 = 空
#    git ls-files -- Code 逐文件 git show :<rel>,剥 CRLF 后取两个集合的差
```

**验证**:`git diff --stat` = `Code/server/Services/PartyService.cpp | 12 +++++++++---` / **1 file changed, 9 insertions(+), 3 deletions(-)**;唯一 hunk `@@ -85,10 +85,16 @@`。
`Code/tests/xmake.lua` 的 target 是 `TPTests`、依赖 `SkyrimEncoding`,**不含 `SkyrimTogetherServer`**,所以服务端的改动在本仓测试里覆盖不到。
**本机不编译**(§0/§1),所以判据是静态的:全树差集只剩这一条,而 `view` 的语义就是「携带该组件的实体集合」。

> **教训**:第一条:**`view<T>()` 遍历的不是「T 的实例」,是「携带 T 的实体」—— 组件搬到别处(成员、`ctx()`、脚本层)时,view 不会报错,它只是安静地空转。**
> 第二条:**判据要能机械执行:`view` 的组件集合减去 `emplace` 的组件集合,差集必须为空。**
> 本轮就是这个差集里唯一的一条,而它藏在一个每 10 秒跑一次、写得很整齐的循环里。
> 第三条:**`git log -S` 是找「谁漏改了一行」的最快尺。**
> `663a187e` 改了成员类型、访问器、广播签名,只漏了 `:88` 这一行;`git log -S 'view<PartyComponent>'` 只给出写下它的那次提交,说明它从未被改过。
## 101. 2026-10-21 场次:客户端 STRPM 桥的 19 个入口全写着 noexcept,其中 8 个函数体在分配,而它们唯一的出口是同一个 DLL 里的 C 导出 —— 判据是「导出外面没有能接住异常的调用者,所以接住要写在函数体里」

> **锚点**:`Code/client/Services/PluginMessagingService.cpp`(161 insertions / 67 deletions / 30 个 hunk,405 行 → 499 行)。

### 101.1 缺陷

`Code/client/Services/PluginMessagingService.cpp`(pin 405 行)里 **19 个函数签名**都带 `noexcept`,`try`/`catch` 是 **0/0**。其中 8 个函数体在分配:

```cpp
// Code/client/Services/PluginMessagingService.cpp:63-64(pin,修复前)
void PluginMessagingService::FireProxyMapping(STRPM::ProxyMappingEventType aType, STRPM::ConnectionID aConnectionId,
                                              STRPM::ProxyFormID aOldFormId, STRPM::ProxyFormID aNewFormId) noexcept
{
    ...
    TiltedPhoques::Vector<MappingListener> listeners;   // :74
    {
        std::scoped_lock lock(g_pluginMessagingMutex);

        for (const auto& listener : m_mappingListeners)
            listeners.push_back(MappingListener{ listener.Callback, listener.UserData });   // :79
    }
```

`Code/client/Services/PluginMessagingService.cpp:123`(pin)逐字 `STRPM::Result PluginMessagingService::RegisterChannel(const char* acpChannel, STRPM::ReceiveCallback aCallback, void* apUserData, STRPM::ListenerHandle* apOutHandle) noexcept`,体内 `:141` 逐字 `m_listeners.push_back(Listener{ acpChannel, aCallback, apUserData });`、`:142` 逐字 `m_channels[handle] = acpChannel;`。

八个帧逐个(pin 行号 / 体内分配点 / 谁调它):

| 函数 | pin 行 | 体内分配点 | 调用者 |
| --- | --- | --- | --- |
| `FireProxyMapping` | `:63-84` | `:74` + `:79` | entt observer(`OnPlayerComponentAdded`/`OnPlayerComponentRemoved`) |
| `RegisterChannel` | `:123-148` | `:141` + `:142` | `PluginMessagingExport.cpp:30` |
| `UnregisterChannel` | `:150-170` | `:158 const auto channel = it->second;` | `PluginMessagingExport.cpp:31` |
| `Send` | `:172-226` | `:192 request.Channel =` + `:217 request.PluginData.assign` | `PluginMessagingExport.cpp:32` |
| `SetLocalDisplayName` | `:247-260` | `:258 m_localDisplayName = acpDisplayName;` | `PluginMessagingExport.cpp:36` |
| `ResolveProxy` | `:290-324` | `:306 auto view = m_pWorld->view<FormIdComponent, PlayerComponent>();` | `PluginMessagingExport.cpp:56` |
| `RegisterProxyMappingListener` | `:326-341` | `:339 m_mappingListeners.push_back(...)` | `PluginMessagingExport.cpp:57` |
| `OnPluginMessage` | `:359-405` | `:386 TiltedPhoques::Vector<Delivery> deliveries;` + `:396`/`:400` | `Code/client/World.cpp:65` 的 dispatcher sink |

### 101.2 判据

判据不是「这里会不会耗尽内存」,而是「**这 8 个帧里哪一个是最后一道 `noexcept`**」。它们没有一个是内部调用 —— 全部被 `Code/client/Services/PluginMessagingExport.cpp` 取地址后交给插件:

| 位置 | 写法 |
| --- | --- |
| `Code/client/Services/PluginMessagingExport.cpp:75` | `STRPM_EXPORT STRPM::Result STRPM_CALL STR_QueryPluginMessagingInterface(...) noexcept` |
| `:80` | `STR_QueryPluginMessagingProxyResolver(...) noexcept` |
| `:146` | `STR_QueryPluginMessagingDiagnostics(...) noexcept` |
| `:151` | `STRPM_QueryTransportInterface(...) noexcept` |
| `:25-37` / `:54-65` / `:125-140` | 三个函数局部 static `s_interface` 的 14 个 lambda,**逐个都写 `noexcept`** |

导出是 C ABI,插件经 `GetProcAddress` 直接调 —— **外面没有能接住异常的调用者**。抛出点(本文件)与 `noexcept` 边界(同 DLL 的导出)之间没有任何一层写 `try`,这与 §99 的跨 DLL 情形不同:那里是两个模块各写各的,这里是同一个模块里从函数体一路裸奔到 `extern "C"`。

同族先例(载重):插件侧那 7 个子模块**没有任何一处 `try`/`catch`**(逐个实测 `try=0 catch=0`),而它们全部经这四个导出进来 ⇒ 这 8 个帧是这条链上唯一能放 `catch` 的地方。

### 101.3 可达性

触发频率由调用方决定,不由这条路径决定:

| 帧 | 触发时机 |
| --- | --- |
| `FireProxyMapping` | 每次玩家实体出现/消失各一次 |
| `ResolveProxy` | 每个插件每收到一个载荷调一次(七个插件的接收路径全部经它) |
| `OnPluginMessage` | 每个入站载荷一次 |

`FireProxyMapping` 的四个调用帧自己也都写着 `noexcept`:`Code/client/Services/Generic/CharacterService.cpp:358 OnAssignCharacter`、`:481 OnCharacterSpawn`、`Code/client/Services/Generic/OverlayService.cpp:434 OnPlayerComponentRemoved`、`Code/client/Services/Generic/WeatherService.cpp:110 OnPlayerComponentRemoved`。

### 101.4 修法

给这 8 个函数体各包一层 `try { … } catch (...) { Log("…"); return <既有失败值>; }`,catch 里只记一行、**不重试**。`noexcept` 一处未改 —— 删掉它只会把终止点挪到导出那一帧。

`Code/client/Services/PluginMessagingService.cpp:142-174`(修复后)逐字

```cpp
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

`RegisterChannel` 是唯一需要回滚的一处:两条表都分配,而 `m_channels` **没有任何枚举者**,半写下的行会永远持有一个 channel 名。`handle` 提到 `try` 外初始化(`:140 std::uint64_t handle = 0;`),catch 里重新取锁擦掉那一行(`scoped_lock` 已析构,不重入)。
`ResolveProxy` 的 catch 只记一行**不 return**,落到既有的 `:397 *apOutFormId = STRPM::kInvalidProxyFormID; return STRPM::Result::kTargetNotFound;` —— 「没找到」本来就是正常状态。

失败值一律沿用契约里**既有**的码(`kNotAvailable` / `kTransportError` / `kTargetNotFound`):七个插件各持一份 `ResultToString`,`plugins/DAVSyncTogether/src/DAVNetworkService.cpp:181`、`plugins/TradeTogether/src/StrMessagingTransport.cpp:440`、`plugins/AnimSyncTogether/src/STRPMClient.cpp:375` 都只看 `kOk`/`kTargetNotFound`/`kNotAvailable`,**没有一处区分「没找到」与「分配失败」** ⇒ 新增一个码的收益为零。

### 101.5 复核命令(照抄)

```powershell
# 1. 签名一处未动(应仍 19),try/catch 从 0/0 变成 9/9
git show :Code/client/Services/PluginMessagingService.cpp | Select-String -Pattern 'noexcept' | Measure-Object
Select-String -Path Code\client\Services\PluginMessagingService.cpp -Pattern '\btry\b|\bcatch\b' | Measure-Object

# 2. 八个帧的体内分配点(逐字)
git show :Code/client/Services/PluginMessagingService.cpp | Select-String -Pattern 'listeners.push_back|m_channels\[handle\]|m_listeners.push_back|it->second|request.Channel =|PluginData.assign|m_localDisplayName =|view<FormIdComponent|m_mappingListeners.push_back|Vector<Delivery>'

# 3. 导出那一层全是 noexcept,且外面没有调用者
Select-String -Path Code\client\Services\PluginMessagingExport.cpp -Pattern 'noexcept|STRPM_EXPORT'

# 4. 插件侧没有一处 try/catch(七个都要 0/0)
#    plugins/<name>/src/*.cpp 逐个 Select-String -Pattern '\btry\b|\bcatch\b'

# 5. 编译闭包:两个文件都在 add_files("**.cpp|Games/Skyrim/**") 覆盖范围内
Select-String -Path Code\client\xmake.lua -Pattern 'add_files'
```

**验证**:`git diff --numstat` = `161	67	Code/client/Services/PluginMessagingService.cpp` / **30 个 hunk** / 405 行 → **499 行**;`noexcept` 出现次数 **19 → 19**,`try`/`catch` **0/0 → 9/9**(第 9 个 `try` 是 `Log` 那一行的注释词,不计入函数体)。
`Code/client/xmake.lua:21` 的 `add_files("**.cpp|Games/Skyrim/**")` 覆盖本文件与 `PluginMessagingExport.cpp`,两者都在 `SkyrimTogetherClient` 与 `SkyrimTogetherClientLegacy` 的编译闭包内。
`Code/tests/xmake.lua` 的 target 是 `TPTests`、依赖 `SkyrimEncoding`,**不含 `SkyrimTogetherClient`**,所以客户端的改动在本仓测试里覆盖不到。
**本机不编译**(§0/§1),所以判据是静态的:8 个函数体各自的分配点是逐字读出来的,而它们的调用者全是 `noexcept` 或 C 导出。

> **教训**:第一条:**`extern "C"` 导出是异常的终点站 —— 外面没有调用者,所以 `catch` 只能写在函数体里。**
> 这 8 个函数没有一个是内部调用,「调用者会处理」这句话在 C ABI 边界上不成立。
> 第二条:**同一个 DLL 里也要数 `noexcept` 的层数,不能因为「都是自己人」就假定有人接。**
> 抛出点和边界都在本仓,中间那 30 个 hunk 里一层 `try` 都没有 —— §99 是跨模块,这一节是跨帧。
> 第三条:**失败之后留下的半成品,要和失败本身一起设计。**
> `RegisterChannel` 的两条表都分配,而 `m_channels` 没有任何枚举者:先写 handle 再写 listener,失败时就只能靠 catch 里的 `erase` 收尾。
## 102. 2026-10-21 场次:服务端的任务去重历史只有时间上界、没有条数上界,而它的键由客户端逐包任选 —— 判据是「同一层楼的任务日志已经为同一件事写了 4096」

> **锚点**:`Code/server/Services/PartyService.h:58` 与 `Code/server/Services/QuestService.cpp:348-377`(2 files changed, 30 insertions(+) / 2 个 hunk:`@@ -50,6 +50,13 @@ struct QuestStageDedupHistory`、`@@ -350,5 +350,28 @@ void QuestStageDedupHistory::Add(const QuestOrSceneId& acQuestOrSceneId, const Q`)。

### 102.1 缺陷

`Code/server/Services/PartyService.h`(pin 141 行)里的 `struct QuestStageDedupHistory` 只有一条退役路径:

```cpp
// Code/server/Services/PartyService.h:79-80(pin,修复前)
void inline Expire() noexcept;
Container m_cache{};       // Short, time-ordered, duplicates valid (do they ever happen?)
```

而 `Expire()` 只看时钟 —— `Code/server/Services/QuestService.cpp:309-321`(pin)逐字:

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

窗口是 `Code/server/Services/QuestService.cpp:17` 的 `Console::Setting uQuestHistoryExpiration("GameServer:uQuestHistoryExpiration", "Time in milliseconds to retain quest progression changes for deduplication", 30000U);` —— **30 秒**。

三处 `Add` 都是**无条件**调用,且键完全由发送方逐包决定:

| 调用点(pin) | 上下文 |
| --- | --- |
| `Code/server/Services/QuestService.cpp:170` | leader 分支,`else` 的判定是 `FoundWPlayerId` ⇒ 走到这里就是「这一行还没有」 |
| `Code/server/Services/QuestService.cpp:203` | member 分支,`dedupHistory.Add(...)` 在 `bFound` 判定之后无条件执行 |
| `Code/server/Services/QuestService.cpp:258` | scene BEGIN,`Add(message.SceneId, 0, RequestQuestUpdate::StatusCode::SceneUpdate, pPlayer->GetId())` |

键的取值空间:`Code/encoding/Messages/RequestQuestUpdate.h` 的 `GameId Id; uint16_t Stage; uint8_t Status; uint8_t ClientQuestType;`,而 `Code/encoding/Structs/GameId.h` 是 `{uint32_t BaseId; uint32_t ModId;}` —— **128 位,客户端可以每包换一个**。⇒ 30 秒窗口内能堆多少行,只取决于对端的发送速率。

### 102.2 判据

判据不是「4096 够不够」,而是「**同一层楼里已经有一张同类的表,它写了上界,而这张没写**」。

`Code/server/Services/QuestService.h:27-32`(pin)逐字:

```cpp
    // A quest log row is keyed by a GameId the client picks, and the only thing that ever
    // retires one is a Stopped update for that same id: a client that keeps inventing ids
    // grows this table for as long as it stays connected. Bounded here like every other
    // table whose key the peer picks. A full log refuses the new id instead of growing
    // without limit; updates for ids already in it still go through.
    static constexpr std::uint32_t kMaxQuestLogEntries = 4096;
```

两张表逐条对照:

| 事实 | 任务日志(`QuestService.h`) | 去重历史(`PartyService.h`) |
| --- | --- | --- |
| 键的来源 | `GameId`,客户端任选 | `GameId`,客户端任选 |
| 退役路径 | 只有同 id 的 Stopped | 只有 `Expire()`,看时钟 |
| 上界 | `kMaxQuestLogEntries = 4096` | **无** |

满表行为也照抄:`Code/server/Services/QuestService.cpp:71 else if (entries.size() >= kMaxQuestLogEntries)` → `:78 spdlog::warn("{}: quest log is full ({} entries), refusing to track quest {:X} from {} {:X}", __FUNCTION__, entries.size(), message.Id.LogFormat(), playerTypeString, pPlayer->GetId());` → **仍然转发**,只拒绝新键。

全树对照(证明这不是「没人管」而是「漏了一张」):

```powershell
git grep -n -E "kMax[A-Za-z]+\s*=" -- Code/server
# GameServer.cpp:26  constexpr size_t kMaxServerNameLength = 128u;
# CharacterService.cpp:48  constexpr uint32_t kMaxPendingActionEvents = 32;
# ObjectService.h:37  static constexpr std::uint32_t kMaxObjectEntities = 64 * 1024;
# PluginMessagingService.h:44/45  kMaxPayloadBytes = 24 * 1024 / kMaxChannelBytes = 96
# QuestService.h:32  static constexpr std::uint32_t kMaxQuestLogEntries = 4096;
# (本条新增) PartyService.h:58  static constexpr uint32_t kMaxEntries = 4096;
```

速率限制只有一处,与本条路径无关:`git grep -n -iE "ratelimit|rate_limit|tokenbucket|kBurst" -- Code/server` 只命中 `Code/server/Services/PluginMessagingService.{h,cpp}` 的令牌桶(`kBurstCapacity = 256`)。

### 102.3 可达性

包从 `Code/server/GameServer.cpp:261-275` 的 handler lambda 进(该 lambda **不带 `noexcept`**):`:263 auto* pPlayer = m_pWorld->GetPlayerManager().GetByConnectionId(aConnectionId);` → `:274 m_pWorld->GetDispatcher().trigger(PacketEvent<T>(pRealMessage.get(), pPlayer));`,由 `Code/server/Services/QuestService.cpp:23 m_questUpdateConnection = aDispatcher.sink<PacketEvent<RequestQuestUpdate>>().connect<&QuestService::OnQuestChanges>(this);` 接到 `:27 void QuestService::OnQuestChanges(const PacketEvent<RequestQuestUpdate>& acMessage) noexcept`。

链上唯一的门是 `Code/server/GameServer.cpp:584 if (!WireLimits::WithinPacketLimit(aSize))` 的单包大小检查 —— **没有任何按速率的门**。每个包都会走到 `Add`。

### 102.4 修法

`Code/server/Services/PartyService.h:53-58`(修复后):给这张表和隔壁取同一个数,注释里点名它为什么存在。

```cpp
    // A row is keyed by a GameId the client picks, and the only thing that ever retires one is
    // Expire(), which looks at time alone: a client that keeps inventing ids grows this deque for
    // the whole expiration window. Bounded here like the quest log next door, with the same
    // number, because it is the same kind of table. A full history stops recording new ids and
    // says so; the entries already in it keep deduplicating.
    static constexpr uint32_t kMaxEntries = 4096;
```

`Code/server/Services/QuestService.cpp:348-377`(修复后)逐字:

```cpp
void QuestStageDedupHistory::Add(const QuestOrSceneId& acQuestOrSceneId, const QuestStage acQuestStage,
                                 const QuestStatus acQuestStatus, const PlayerId& acPlayerId,
                                 const TimeStamp acTimeStamp)
{
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
}
```

查重那一半**不是为了省内存,是为了不改变语义**:`Found`/`FoundWPlayerId` 的判定全部发生在 `Add` 之前(`:160`、`:178`、`:202`、`:256`),它们只问「有没有这一行」,第二行对它们没有任何区别;`Add` 的读者只有 `Code/server/Services/QuestService.cpp` 里那三处,`GetQuestStageDedupHistory()` 也没有仓库外调用者。

**第一版写的是 `if (existing != m_cache.end()) return;`(重复即丢弃),被 `:256` 否掉了。** 那里的注释逐字:

```cpp
    // Scene Begin
    // Set as SceneMaster if there isn't one for this scene.
    // Checking for a recent scene change in history keeps us unstuck in case scene end event is missed
    // Or in the case where the scene changes without a scene end, we should get a new scenemaster.
    // Neither of those should happen, but, Bethesda + STR == flakes
```

`:256 bool needSceneMaster = dedupHistory.NeedSceneMaster() || !dedupHistory.Found(message.SceneId, 0, RequestQuestUpdate::StatusCode::SceneUpdate);` 问的是「**最近**有没有这个 scene 的变更」—— 直接丢弃会让「玩家重进同一场景」这一正是它要覆盖的用例看到一条陈旧行。终稿的 `erase` 加落到末尾的 `emplace_back` 是**刷新那一行的窗口**,不是丢掉它。

### 102.5 复核命令(照抄)

```powershell
# 1. 上界已就位,且与隔壁同值
git grep -n -E "kMaxEntries|kMaxQuestLogEntries" -- Code/server
# PartyService.h:58  static constexpr uint32_t kMaxEntries = 4096;
# QuestService.h:32  static constexpr std::uint32_t kMaxQuestLogEntries = 4096;
# QuestService.cpp:71  else if (entries.size() >= kMaxQuestLogEntries)
# QuestService.cpp:369  else if (m_cache.size() >= kMaxEntries)

# 2. Add 的三个调用点与 Found/FoundWPlayerId 的四个判定点全在同一个文件
git grep -n -E "dedupHistory\.(Add|Found|FoundWPlayerId|NeedSceneMaster)\(" -- Code

# 3. 键是客户端任选的 128 位值,退役路径只有时钟
git show :Code/encoding/Structs/GameId.h
git show :Code/encoding/Messages/RequestQuestUpdate.h
git show :Code/server/Services/QuestService.cpp | Select-String -Pattern 'Expire\(\)|pop_front'

# 4. 这条路径上没有任何速率限制
git grep -n -iE "ratelimit|rate_limit|tokenbucket|kBurst" -- Code/server

# 5. 本文件在编译闭包内
Select-String -Path Code\server\xmake.lua -Pattern 'add_files|set_pcxxheader'
```

**验证**:`git diff --stat` = `Code/server/Services/PartyService.h | 7 +++++++` / `Code/server/Services/QuestService.cpp | 23 +++++++++++++++++++++++` / **2 files changed, 30 insertions(+)**,两个 hunk;`PartyService.h` 141 行 → 148 行,`QuestService.cpp` 354 行 → 377 行。
`Code/server/xmake.lua` 的 `add_files("**.cpp")` 覆盖 `QuestService.cpp`,`add_headerfiles("**.h")` 覆盖 `PartyService.h` ⇒ 两者都在 `SkyrimTogetherServer` 的编译闭包内。
`Code/tests/xmake.lua` 的 target 是 `TPTests`、依赖 `SkyrimEncoding`,**不含 `SkyrimTogetherServer`**,所以服务端的改动在本仓测试里覆盖不到。
**本机不编译**(§0/§1),所以判据是静态的:`Add` 的三个调用点、`Found`/`FoundWPlayerId` 的四个判定点、全树唯一的速率限制都是逐条读出来的。

> **教训**:第一条:**同一层楼里有一张同类的表已经写了上界,另一张没写,那多半不是「这张不需要」,而是「写的时候漏了」。**
> 两张表的键都是对端任选的 `GameId`,退役路径都只有一条 —— 差别只在有没有人想起它。
> 第二条:**「有时间上界」不等于「有上界」。**
> `Expire()` 的 30 秒窗口只在「键是有限的」时才等价于条数上界;键由对端逐包决定时,窗口只是把「无限」换成了「每 30 秒清一次」。
> 第三条:**给表加上界之前,先确认这个上界不会改掉它的语义。**
> 第一版的「重复即 return」看着更省,却把 `:256` 那句「a recent scene change」变成了「历史上曾经有过」—— `erase` 加移到队尾才是这条历史的原意。
## 103. 2026-10-22 场次:服务端脚本层的 14 个 `noexcept` 里 13 个在分配,而全仓唯一有 `catch` 的文件把边界放在了调用者身上 —— 判据是「`noexcept` 的检查要顺着调用链走」

> **锚点**:`Code/server/Services/ScriptService.h:49-68`、`Code/server/Services/ScriptService.inl:3-71`、`Code/server/Services/ScriptService.cpp:63-297`(3 files changed, 168 insertions(+) / 64 deletions(-),7 个 hunk:`@@ -1,42 +1,71 @@`、`@@ -46,14 +46,27 @@ struct ScriptService`、`@@ -62,43 +62,67 @@ ScriptService::ScriptService(World& aWorld, entt::dispatcher& aDispatcher)`、`@@ -168,7 +192,11 @@ std::tuple<bool, String> ScriptService::HandleChatMessage(const entt::entity aSe`、`@@ -189,7 +217,6 @@ void ScriptService::HandlePlayerQuit(ConnectionId_t aConnectionId, Server::EDisc`、`@@ -206,14 +233,13 @@ void ScriptService::OnUpdate(const UpdateEvent& acEvent) noexcept`、`@@ -223,13 +249,49 @@ void ScriptService::OnPlayerEnterWorld(const PlayerEnterWorldEvent& acEvent) noe`)。

### 103.1 缺陷

`Code/server/Services/ScriptService.h`(pin 76 行)、`ScriptService.cpp`(pin 235 行)、`ScriptService.inl`(pin 42 行)三个文件里 **14 个函数声明**写着 `noexcept`,其中 **13 个的函数体在分配**。唯一不分配的是 `:42 void OnPlayerEnterWorld(const PlayerEnterWorldEvent& acEvent) noexcept` —— 它的整个函数体是注释掉的。

两个模板是分配最密的地方:

```cpp
// Code/server/Services/ScriptService.inl:3-12(pin,修复前)
template <typename... Args>
std::tuple<bool, String> ScriptService::CallCancelableEvent(const String& acName, Args&&... args) noexcept
{
    m_eventCanceled = false;

    auto& callbacks = m_callbacks[acName];   // :8  缺失即插入 ⇒ map 节点 + String 键 + Vector
    for (auto& callback : callbacks)
    {
        sol::protected_function pf{callback};   // :12 包装回调 ⇒ 分配
```

`:8` 是 `Map::operator[]`,对端没注册过的事件名会**插入一行**;`:12` 的 `sol::protected_function` 是 sol 的引用包装;`:17-18`/`:38-39` 的 `sol::error err = result;` 持一个 `std::string`;`:22`/`:25` 的 `std::make_tuple` 里有一个 `String`。**两个模板体内 `try`/`catch` 是 0/0。**

`:cpp` 侧另外八处:析构(`:68 m.clear()`、`:71 collect_garbage()`,析构是**隐式 `noexcept`**)、`Initialize`(`:80 luaVm.globals()`、`:82-83 open_libraries` 开 8 个库、`:87 luaVm["package"]["path"]`、`:91-101` 每个 manifest 建一条 path)、`HandlePlayerQuit`(`:171 std::string reason;` + 6 次赋值)、`OnUpdate`(`:211` 调 `CallEvent`)、`AddEventHandler`(`:228 m_callbacks[String(acName)].push_back(acFunction);`,两次分配)、`CancelEvent`(`:234 m_cancelReason = aReason;`),以及六个单行入口 —— 它们都在自己的 `noexcept` 函数体里把字面量建成临时 `String`。

而 `catch` 只有两处,且两处都**不在分配那一层**:`:121 catch (std::exception& exception)` 在 `LoadScript` 里(那个函数**不** `noexcept`,它的 catch 是对的),`:213 catch (sol::error& exception)` 在 `OnUpdate` 里、**在调用者身上**。

### 103.2 判据

判据不是「这里会不会耗尽内存」,而是「**全仓唯一有 `try` 的那个文件,它的边界只覆盖了两条路径**」。

`git grep --cached -n -E "\bcatch \(" -- Code/server` 在整个 `Code/server` 只命中 **2 处**,两处都在本文件(`:121`、`:213`),其余 **0 处**。`docs/PITFALLS.md:6179-6180`(§55)记的是同一件事:「全仓 `catch` 只有 8 处」,其中就有这两处。

同族先例是同一栋楼里刚修完的两处:

| 场景 | `noexcept` 边界在哪 | catch 写在哪 |
| --- | --- | --- |
| §101 客户端 STRPM 桥 | `extern "C"` 导出,外面没有调用者 | 函数体里 |
| §92/§93 插件补丁 | ABI 虚函数,消费方持逐字副本 | 函数体里 |
| 本节 服务端 `ScriptService` | `Code/server/GameServer.h:103 void OnUpdate() override;`、`:104 void OnConsume(...)`、`:106 void OnDisconnection(...)` 三者**都不带 `noexcept`** | **仍然写在函数体里** |

第三格是本节最需要解释的一格:它明明可以靠「删掉 `noexcept`」把责任推给上游,但没有。理由是承诺写在本文件身上,而调用链上确实有一个只接 `sol::error` 的 catch —— 那正是「边界放错了位置」的证据:`OnUpdate` 的 `:209-216` catch 在调用者身上,而分配在被调者体内(`docs/PITFALLS.md:12389-12394` §92.2 的判据逐字:「这条缺陷的判据不是「`Worker` 有没有分配」,而是**「`Worker` 的调用链上有没有人分配」**」)。

还有一条只在本节出现的判据:**临时对象的构造发生在调用者那一侧**。

```cpp
// Code/server/Services/ScriptService.cpp:168-171(pin,修复前)
std::tuple<bool, String> ScriptService::HandleCharacterMove(const entt::entity aNpc) noexcept
{
    return CallCancelableEvent("onCharacterMove", aNpc);
}
```

`:4` 的形参是 `const String&`,`"onCharacterMove"` 是 `const char*` ⇒ 那个 `String` 是在**调用者的函数体里、被调者的 `try` 块之前**构造的。在被调者体内新加 catch 结构性地够不着它。所以本节的第一步不是加 `try`,是**改签名**。

### 103.3 可达性

七个入口各有至少一条不需要管理员的路径:

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

### 103.4 修法

三个文件,13 个函数体。`noexcept` **一处不删**。

第一步,改签名 —— 把唯一那次分配挪进 `try`:

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

`HandlePlayerQuit` 的 `std::string reason` 同理:

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

第二步,catch 放到**分配所在的那一层**:

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

第三步,**报告本身不能再失败**。`xmake.lua:47` 钉的是 `"spdlog v1.13.0"`,全仓没有定义 `SPDLOG_NO_EXCEPTIONS`,所以 `spdlog::error` 的格式化会分配:

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

析构也补上了(`Code/server/Services/ScriptService.cpp:63-83`),因为析构是隐式 `noexcept`,而 `m.clear()`/`collect_garbage()` 都是 Lua 调用:

```cpp
    try
    {
        auto &lockedState = m_lua.Lock().Get();

        for (auto& m : m_sandboxes)
        {
            m.clear();
        }

        lockedState.collect_garbage();
    }
    catch (...)
    {
        ReportFailure(__FUNCTION__, "ran out of memory while tearing down the Lua state");
    }
```

### 103.5 复核命令(照抄)

```powershell
# 1. 全仓只有本文件有 catch,且它原来只有 2 处
git grep --cached -n -E "\bcatch \(" -- Code/server
# Code/server/Services/ScriptService.cpp:121  catch (std::exception& exception)
# Code/server/Services/ScriptService.cpp:213  catch (sol::error& exception)

# 2. 14 个 noexcept 声明一处未删,工作区只多出 ReportFailure 这一个签名
git diff -U0 -- Code/server/Services/ScriptService.h Code/server/Services/ScriptService.inl Code/server/Services/ScriptService.cpp |
    Select-String -Pattern '^[-+].*noexcept'

# 3. 两个模板的调用点全部传字面量(没有一处需要 String)
git grep -n -E "CallCancelableEvent\(|CallEvent\(" -- Code/server

# 4. 六个入口、两个模板、三个服务各自的 noexcept
git grep -n -E "noexcept" -- Code/server/Services/ScriptService.h
git grep -n -E "noexcept" -- Code/server/Services/CharacterService.h Code/server/Services/OverlayService.h Code/server/Services/CalendarService.h

# 5. 调用链上游不是 noexcept 边界(所以「删掉 noexcept」也说得通,只是承诺在本文件身上)
Select-String -Path Code\server\GameServer.h -Pattern 'void On(Update|Consume|Disconnection)'

# 6. Initialize 的进程终止链:启动期、监听之前、构造函数里
git grep -n "GetScriptService().Initialize" -- Code
git grep -n "m_pServerInstance->Initialize" -- Code
git grep -n "make_unique<DediRunner>" -- Code

# 7. 编译闭包:三个文件同属一个编译单元
Select-String -Path Code\server\xmake.lua -Pattern 'add_files|add_headerfiles|set_pcxxheader'
Select-String -Path Code\server\Services\ScriptService.h -Pattern '#include "ScriptService.inl"'
```

**验证**:`git diff --stat` = `Code/server/Services/ScriptService.cpp | 138 ++++++++++++++++++++++++---------` / `Code/server/Services/ScriptService.h | 17 +++-` / `Code/server/Services/ScriptService.inl | 77 +++++++++++++------` / **3 files changed, 168 insertions(+), 64 deletions(-)**,7 个 hunk;`ScriptService.inl` 42 行 → 71 行,`ScriptService.h` 76 行 → 89 行,`ScriptService.cpp` 235 行 → 297 行。
`noexcept` 的**声明一处未动**(14 → 14,新增的第 15 个是 `ReportFailure`);`catch` 从 **2 处 → 8 处**,`try` 从 **2 处 → 8 处**,新增的 6 对全在分配点所在的那一层。
`Code/server/xmake.lua:12 add_headerfiles("**.h")`、`:13 add_files("**.cpp")` 覆盖三个文件;`.inl` 经 `Code/server/Services/ScriptService.h:89 #include "ScriptService.inl"` 进入同一个编译单元。
`Code/tests/xmake.lua` 的 target 是 `TPTests`、依赖 `SkyrimEncoding`,**不含 `SkyrimTogetherServer`**,所以服务端的改动在本仓测试里覆盖不到。
**本机不编译**(§0/§1),所以判据是静态的:13 个函数体的分配点是逐字读出来的,`Code/server/GameServer.h:103/104/106` 三个覆写都不带 `noexcept` 也是逐字读出来的。

> **教训**:第一条:**同一个文件里 14 个 `noexcept` 有 13 个在分配,那不是 13 个缺陷,是一个放错了位置的边界。**
> `LoadScript` 的 catch 是对的、`OnUpdate` 的 catch 是错的 —— 差别不在有没有写 catch,而在它守的是不是分配发生的那一层。
> 第二条:**`const char*` 到 `String` 的转换,发生在调用者的函数体里,不在被调者的 `try` 里。**
> 六个入口全都 `return CallCancelableEvent("onCharacterMove", aNpc);`,而形参是 `const String&` ⇒ 临时对象在进函数之前就建好了,被调者新加的 catch 结构性地够不着它。改签名是修法的第一步,不是加 `try`。
> 第三条:**失败的报告本身也会失败。**
> `spdlog v1.13.0` 没有定义 `SPDLOG_NO_EXCEPTIONS`,格式化会分配 —— 在一个 `noexcept` 的 catch 里直接调 `spdlog::error`,等于把 terminate 从原来的位置挪到处理它的位置上。
## 104. 2026-10-23 场次:服务端 211 个 `noexcept` 里只有 6 个写了 `catch`,其中 27 个在自己分配、44 个在格式化日志 —— 判据是「失败的报告本身也会失败」

> **锚点**:`Code/server/**` 18 个文件、新增 `Code/server/Diagnostics.h`(27 行)(18 files changed, 2196 insertions(+) / 1560 deletions(-),483 个 hunk);`Code/server/Pch.h:29`、`Code/server/GameServer.h:92-107`、`Code/server/GameServer.cpp:1149-1170`、`Code/server/Components/ModsComponent.cpp:5-45`、`Code/server/Game/PlayerManager.cpp:16-60`、`Code/server/Game/Animation/ActionReplayCache.cpp:5-122`、`Code/server/Services/{ActorValueService,AdminService,CharacterService,CombatService,InventoryService,MagicService,ObjectService,OverlayService,PartyService,PlayerService,PluginMessagingService,QuestService}.cpp`。

### 104.1 缺陷

`Code/server/**` 共 100 个 `.cpp/.h/.inl`,里面有 **211 个 `noexcept` 函数定义**,体内含 `catch` 的只有 **6 个**。用一个「等长清空注释与字面量 → 花括号逐字符配平取函数体」的脚本数出来的是两族,它们有重叠:

```
家族一:体内自己分配内存(27 个定义)
  Code/server/Components/ModsComponent.cpp:5  AddStandard          m_standardMods.emplace(acpFilename, Entry{id, 1})
  Code/server/Components/ModsComponent.cpp:20 AddLite              m_liteMods.emplace(acpFilename, Entry{id, 1})
  Code/server/Game/Animation/ActionReplayCache.cpp:5  FormRefinedReplayChain  Vector<ActionEvent>{m_actions.begin() + 1, m_actions.end()}
  Code/server/Game/Animation/ActionReplayCache.cpp:18 AppendAll            m_actions.push_back(action)
  Code/server/Game/Animation/ActionReplayCache.cpp:31 RefineReplayCache    action.EventName = String{*instantAnimName}
  Code/server/Game/Animation/ActionReplayCache.cpp:64 IsExitAction         String lowercaseName{acAction.EventName}
  Code/server/Game/PlayerManager.cpp:27       Create               m_players.emplace(aConnectionId, MakeUnique<Player>(aConnectionId))
  Code/server/GameServer.cpp:1149             SanitizeUsername     String username = acUsername;
  Code/server/GameServer.h:92                 AddAdminSession      m_adminSessions.insert(acSession)
  Code/server/Services/ActorValueService.cpp:23 / :50 / :77   ActorValuesList[id] = value / notify.Values = message.Values
  Code/server/Services/CharacterService.cpp:251 / :374 / :587 / :904 / :960   push_back / 8 次 emplace / Map<Player*, ...>
  Code/server/Services/ObjectService.cpp:31 / :60            Vector<entt::entity> toDestroy / response.Objects.push_back
  Code/server/Services/PartyService.cpp:113 / :226 / :273 / :320 / :439 / :463 / :487   m_parties[partyId] / push_back / Players[id] = ...
  Code/server/Services/QuestService.cpp:27   OnQuestChanges       dedupHistory.Add(...)

家族二:体内只有日志格式化,自己不含分配标记(44 个定义,共 106 条 spdlog 调用)
  Code/server/Game/PlayerManager.cpp:16                    PlayerManager::Get
  Code/server/Services/CharacterService.cpp  x11           :190 :332 :362 :445 :495 :508 :560 :573 :681 :745 :794
  Code/server/Services/InventoryService.cpp  x3            :28 :99 :148
  Code/server/Services/MagicService.cpp      x4            :24 :40 :53 :72
  Code/server/Services/ActorValueService.cpp x1            :102 OnDeathStateChange
  Code/server/Services/OverlayService.cpp    x1            :29  sendPlayerMessage
  Code/server/Services/PartyService.cpp      x1            :190 OnPartyKick
  Code/server/Services/PlayerService.cpp     x1            :153 OnPlayerRespawnRequest
  Code/server/Services/PluginMessagingService.cpp x1        :54  OnPluginMessage
  Code/server/Services/QuestService.cpp      x1            :309 QuestStageDedupHistory::Expire
  Code/server/Services/CombatService.cpp     x1            :15  OnProjectileLaunchRequest
  Code/server/Services/AdminService.cpp      x1            :14  HandleShutdown
```

家族二比家族一隐蔽:它看起来「只是打了一行日志」。

### 104.2 判据

| 对照 | 事实 | 结论 |
| --- | --- | --- |
| 本仓已有的正确形状 | `Code/server/Services/ScriptService.cpp:252-263`(commit `e7253f3b`)的 `ReportFailure` 是 `noexcept` + 内层 `try/catch(...)` | 修法不是新发明,是把那个形状搬给别的服务用 |
| 「同族其它实现长什么样」 | `git grep -n -E "\bcatch \(" -- Code/server` 在本次修改前是 **14 处**,其中 2 处在 `ScriptService` | 整个服务层没有第二处边界 |
| 日志本身会分配 | `Code/server/xmake.lua:47` 固定 `spdlog` v1.13.0;`git grep -rn -E "SPDLOG_ACTIVE_LEVEL\|SPDLOG_LEVEL\|SPDLOG_NO_EXCEPTIONS" -- Code xmake.lua` **零命中** | `spdlog::error` 把参数格式化进一个新建的 buffer ⇒ 在 `noexcept` 的 catch 里直接调它,等于把 terminate 挪到处理它的位置 |
| 日志级别不是编译期属性 | `Code/server_runner/main.cpp:43 Console::StringSetting sLogLevel{"sLogLevel", "Log level to print", "info"}`、`:74 globalOut->set_level(level::from_str(sLogLevel.value()))` | 「`debug` 默认不打」是运行期设置,44 个定义里的 `debug` 行同样要守 |
| 「下游有守卫」不等于「值被挡住了」 | `Code/server/GameServer.h:103/104/106` 的 `OnUpdate`/`OnConsume`/`OnDisconnection` 三个覆写**都不带 `noexcept`** | 但 handler 自己是 `noexcept` ⇒ 抛出仍在 handler 里 terminate,上游非 `noexcept` 救不了它(`docs/PITFALLS.md:12389-12394` §92.2 的「检查要顺着调用链走」) |
| 承诺写在本文件身上 | 与 §103 同一格:可以不删 `noexcept` 把责任推给上游,但 `ScriptService` 那次已经决定不推 | 本次同样**一个 `noexcept` 都不删** |

还有一条只在本节出现的判据:**回退值必须让语义仍然成立,而不是让编译通过**。

`ModsComponent::AddStandard` 第一版我写成 `catch { return 0; }`,那是错的:id 会被 `Code/server/GameServer.cpp:988-997` 原样写进 `Mods::Entry::Id` 交给客户端,而 `Code/client/Systems/ModSystem.cpp:88-101` 用它建 `m_serverToGame` / `m_liteToServer` / `m_standardToServer` 三张反查表 —— 两个 mod 共用一个 id,客户端由它派生的每一个 form id 都会撞车,比「这一条没去重」坏得多。正确写法是**先把 id 取出来**(`const auto id = m_seed++;` 不分配),只把 `emplace` 包进 `try`,失败时丢掉去重行、照常返回唯一 id:

```cpp
// Code/server/Components/ModsComponent.cpp:14-29(修复后)
const auto id = m_seed++;

try
{
    m_standardMods.emplace(acpFilename, Entry{id, 1});
}
catch (...)
{
    Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory while recording a client mod");
}

return id;
```

服务端除这个 `find` 之外**没有任何读者**(`git grep -n -E "GetStandardMods|GetLiteMods"` 零命中),所以丢一行去重只影响同一个文件名第二次出现时不再复用 id。

### 104.3 可达性

这 71 个站点全落在三条路径上,没有一条需要管理员:

- **tick 线程**:`Code/server/GameServer.cpp:557-571 OnUpdate` → `:567 dispatcher.trigger(UpdateEvent{cDeltaSeconds})`,五个 sink(`CalendarService.cpp:18`、`CharacterService.cpp:53`、`PartyService.cpp:24`、`ScriptService.cpp:57`、`StringCacheService.cpp:10`)。`CharacterService::OnUpdate` 再转调 `ProcessFactionsChanges`(2 秒一次)与 `ProcessMovementChanges`(20 ms 一次)—— 后者每个 tick 为每个在线玩家建一张 `Map<uint32_t, ServerReferencesMoveRequest>`,并把 `animationComponent.Actions` 复制进每一条 update。
- **收包**:`Code/server/GameServer.cpp:573-618 OnConsume` → `:616 m_messageHandlers[pMessage->GetOpcode()](pMessage, aConnectionId)`。任何一个已连接玩家都能发:聊天、任务更新、任务场景、队伍创建/邀请/接受/踢人/换队长、背包/装备、移动、法术/投射物、物件分配。
- **断连**:`Code/server/GameServer.cpp:626-686 OnDisconnection` → `:645 trigger(PlayerLeaveEvent)` → `PartyService::OnPlayerLeave` → `RemovePlayerFromParty` + `BroadcastPlayerList`;后者是 O(n²) 条消息、每条一个 `Map<uint32_t, String>`(`PartyService.cpp:510-542`)。

触发条件是堆耗尽。**本机不编译**(§0/§1),所以每一条都是静态读数,不是复现。

### 104.4 修法

新增 `Code/server/Diagnostics.h`(27 行),把 `ScriptService` 那个私有静态提升成一个每个服务都能用的自由函数:

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

它经 `Code/server/Pch.h:29 #include <Diagnostics.h>` 进入所有编译单元。选 `Pch.h` 而不是逐文件 include,是因为 `Code/server/xmake.lua:9 set_pcxxheader("Pch.h")` 已把它设成强制包含,而 `Code/server/Pch.cpp` 是**空文件** —— 不这么写就会有一个不包含它的 TU。

18 个文件、71 个函数体加了 `try { ... } catch (...) { Diagnostics::ReportFailure(__FUNCTION__, "..."); }`。`noexcept` 211 → 212,新增的那一个是 `Diagnostics::ReportFailure` 自己。非 void 的补了显式回退:

| 函数 | catch 里的回退 | 语义 |
| --- | --- | --- |
| `PlayerManager::Get` | `return nullptr;` | 与「没有实例」同一个答案,调用方本来就处理 |
| `PlayerManager::Create` | `return nullptr;` | `GameServer.cpp:1000-1013` 已把这个答案当成「该连接已有玩家」,拒绝第二次认证 |
| `ModsComponent::AddStandard`/`AddLite` | 无(先取 id) | 见 §104.2 末 |
| `GameServer::SanitizeUsername` | `return String{};` | 退化成空名 ⇒ 查找失败并打印;唯一调用点是 `GameServer.cpp:436` 的控制台命令 |
| `ActionReplayCache::RefineReplayCache` / `IsExitAction` | `return false;` | 「不是退出动作」是安全答案:动作留在缓存里,客户端不重置动画图 |
| `CharacterService::CanClaimOwnership` / `TransferOwnership` | `return false;` | 「不能认领」/「没转移成功」,调用方不会广播一次只做了一半的交接 |
| `CharacterService::Serialize` / `CreateCharacter` / `ObjectService::OnAssignObjectsRequest` | 无(保持已写入的部分) | 见下 |

三个「写到一半」的位置特意**不回滚**,理由写进了代码注释:

- `CreateCharacter`(`CharacterService.cpp:694-796`):失败时**不** `m_world.destroy(cEntity)`。到最后一个 `emplace` 时 `pPlayer->SetCharacter(cEntity)`(`:773`)可能已经执行,销毁一个 `Player` 还指着的实体正是 `GameServer.cpp:652-655` 那段注释警告的悬垂引用。半建的 actor 对任何要求它没拿到的组件的 view 都不可见,客户端收不到响应会再问一次。
- `Serialize`:请求里已复制的字段保留,仍照常发出。
- `OnAssignObjectsRequest`:已创建的对象留在世界里,不发响应,客户端重问整张表。

`ScriptService` 那份私有静态**保持不动**。行为与新自由函数完全一致,把 8 个调用点改名是纯改名、零行为差,只会让 diff 变大。

### 104.5 复核命令(照抄)

```powershell
# 1. 总数与两族分配点(等长清空注释/字面量 + 花括号配平;本仓无 rg,用 node)
node -e "..."   # 见本节正文的脚本要点:codeMask + stripBindings + PAYLOAD 正则

# 2. 修前修后的四个读数
git stash; node -e "<census>"   # noexcept defs=211 guarded=6 allocUnguarded=27 logUnguarded=44
git stash pop; node -e "<census>"   # noexcept defs=212 guarded=70 allocUnguarded=0  logUnguarded=0

# 3. 全仓 try/catch 计数
git grep -c -E "\btry\b" -- Code/server   # 修前 11 / 修后 75
git grep -c -E "\bcatch\b" -- Code/server # 修前 14 / 修后 80

# 4. 守卫调用点
git grep -n "Diagnostics::ReportFailure" -- Code/server   # 63 处,17 个文件

# 5. 日志会分配(spdlog 未被配置成不抛)
git grep -rn -E "SPDLOG_ACTIVE_LEVEL|SPDLOG_LEVEL|SPDLOG_NO_EXCEPTIONS" -- Code xmake.lua   # 零命中
Select-String -Path Code\server_runner\main.cpp -Pattern "sLogLevel|set_level"

# 6. 编译闭包
Select-String -Path Code\server\xmake.lua -Pattern "pcxxheader|add_headerfiles|add_files"
Get-Item Code\server\Pch.cpp | Select-Object Length   # 0 字节 ⇒ 必须有强制包含

# 7. 上游不是 noexcept 边界(所以「删掉 noexcept」也说得通,只是承诺在本文件身上)
Select-String -Path Code\server\GameServer.h -Pattern "void On(Update|Consume|Disconnection)"
```

**验证**:`git diff --numstat -- Code/server` = **18 files changed, 2196 insertions(+), 1560 deletions(-)**,483 个 hunk;`Code/server/Diagnostics.h` 是唯一的新文件(27 行 / 961 字节 / LF / 尾换行)。逐文件行数:`ModsComponent.cpp` 47→75、`ActionReplayCache.cpp` 102→145、`PlayerManager.cpp` 121→145、`GameServer.cpp` 1158→1170、`GameServer.h` 133→147、`Pch.h` 56→58、`ActorValueService.cpp` 126→167、`AdminService.cpp` 49→59、`CharacterService.cpp` 1043→1226、`CombatService.cpp` 57→66、`InventoryService.cpp` 162→188、`MagicService.cpp` 86→119、`ObjectService.cpp` 199→219、`OverlayService.cpp` 120→164、`PartyService.cpp` 499→598、`PlayerService.cpp` 218→227、`PluginMessagingService.cpp` 116→127、`QuestService.cpp` 378→406。

同一个普查脚本在 `HEAD` 与工作树上各跑一次:

| 读数 | `HEAD` | 现在 |
| --- | --- | --- |
| `noexcept` 定义数 | 211 | 212 |
| 体内含 `catch` | 6 | 70 |
| 体内有分配标记且无 `catch` | 27 | **0** |
| 体内有 `spdlog::` 且无 `catch` | 44(106 条日志) | **0**(0 条) |
| `Code/server` 全文 `try`/`catch` | 11 / 14 | 75 / 80 |

`Diagnostics::ReportFailure` 调用点 **63 个,17 个文件**:`CharacterService.cpp` 18、`PartyService.cpp` 10、`OverlayService.cpp` 5、`ActorValueService.cpp` 4、`ActionReplayCache.cpp` 4、`MagicService.cpp` 4、`InventoryService.cpp` 3、`QuestService.cpp` 3、`ModsComponent.cpp` 2、`PlayerManager.cpp` 2、`ObjectService.cpp` 2,`GameServer.cpp`/`GameServer.h`/`AdminService.cpp`/`CombatService.cpp`/`PlayerService.cpp`/`PluginMessagingService.cpp` 各 1。
`Code/server/xmake.lua:12-13 add_headerfiles("**.h")` / `add_files("**.cpp")` 覆盖新头与全部 18 个文件;`:9 set_pcxxheader("Pch.h")` 保证 `Diagnostics.h` 进入每个 TU。每个改过的文件都做过花括号/圆括号/方括号配平与行尾检查;`Code/server/Services/PluginMessagingService.{cpp,h}` 本来就没有文件尾换行,与 `HEAD` 一致,未动。
**本机不编译**(§0/§1),所以判据是静态的:分配点是逐字读出来的,`noexcept` 与 `try`/`catch` 的数量是脚本数出来的。

> **教训**:第一条:**`noexcept` 是一条承诺,不是装饰;承诺写在哪个函数身上,`catch` 就得写在哪个函数体里。**
> 211 个定义里 205 个没有 catch,不是 205 个缺陷,是一整个服务层把边界放错了位置。上游 `OnConsume`/`OnUpdate`/`OnDisconnection` 不带 `noexcept` 也救不了 handler —— 它在 handler 里就 terminate 了。
> 第二条:**日志是分配,`debug` 也不例外。**
> `spdlog` 没有被配置成 `SPDLOG_NO_EXCEPTIONS`,级别是运行期设置(`sLogLevel` 默认 `info`,但那是默认值,不是代码性质)。44 个「只打了一行日志」的函数体是这一节里最容易被放过的族。
> 第三条:**回退值要能让语义继续成立,不是让编译通过。**
> `AddStandard` 的 `return 0` 会让两个 mod 共用一个 id,而客户端拿它去建三张反查表;正确做法是先把不分配的 id 取出来,只把分配的那一步包进 `try`。「失败时返回什么」和「失败时哪一步不能回滚」是同一个问题的两面。
## 105. 2026-10-24 场次:客户端 698 个 `noexcept` 只有 9 个写了 `catch`,142 个在自己分配或格式化日志 —— 判据是「承诺写在哪个函数身上,`catch` 就得写在哪个函数体里」

> **锚点**:`Code/client/**` 42 个文件、新增 `Code/client/Diagnostics.h`(27 行)(42 files changed, 5561 insertions(+) / 4126 deletions(-),1318 个 hunk);`Code/client/TiltedOnlinePCH.h:50-52`、`Code/client/Components/LocalAnimationComponent.h:22-34`、`Code/client/Games/GamePatch.h`、`Code/client/Games/Skyrim/{Actor,TESObjectREFR,PlayerCharacter}.cpp`、`Code/client/HookAudit.cpp`、`Code/client/Services/**`、`Code/client/Systems/**`、`Code/client/Utils.{h,cpp}`、`Code/client/VersionDb.h`、`Code/client/World.cpp`。

### 105.1 缺陷

`Code/client/**` 共 446 个 `.cpp/.h/.inl`,里面有 **698 个 `noexcept` 函数定义**,体内含 `catch` 的只有 **9 个**(其中 8 个是 `Services/PluginMessagingService.cpp` 自己的 `Log()`,来自 `cd74a67e`)。同一个脚本数出 **142 个**定义在自己分配内存或调用 `spdlog`/`fmt::format`。`Code/client` 全文的 `try`/`catch` 是 **18 / 14**,`Code/encoding` 与 `Code/admin` 是 **0 / 0**。

这与上一节(`941bd606`,`Code/server` 211 → 212 个 `noexcept`、6 → 70 个 `catch`)是同一族缺陷的另一半:那一批只覆盖了 `Code/server`。

### 105.2 判据

| 对照 | 事实 | 结论 |
| --- | --- | --- |
| 本仓已有的正确形状 | `Code/server/Diagnostics.h`(`941bd606` 新增,27 行)与更早的 `ScriptService::ReportFailure`(`e7253f3b`) | 修法不是新发明,是让客户端用同一个 helper |
| 「同族其它实现长什么样」 | 客户端已有的 9 个 catch 里 8 个在 `Services/PluginMessagingService.cpp` 的 `Log()`(`cd74a67e`) | 全客户端只有那一处边界 |
| 日志本身会分配 | `Code/client/xmake.lua` 没有定义 `SPDLOG_NO_EXCEPTIONS`;`TiltedOnlineApp.cpp:75 spdlog::flush_every(std::chrono::seconds(1))`、`:80 logger->flush_on(spdlog::level::warn)` | 与 §103 第三条教训同一格:失败的报告本身也会失败 |
| 「下游有守卫」不等于「值被挡住了」 | `Code/client/Services/TransportService.h` 的 `OnConsume`/`OnConnected`/`OnDisconnected`/`OnUpdate` 四个覆写**都不带 `noexcept`** | 但 handler 自己是 ⇒ 抛出仍在 handler 里 terminate,上游非 `noexcept` 救不了它 |
| **客户端没有 terminate 兜底** | `git grep -rn -E "set_terminate\|std::terminate\|SetUnhandledExceptionFilter\|AddVectoredExceptionHandler" -- Code/client` **零命中** | `CrashHandler` 装的是 `LPTOP_LEVEL_EXCEPTION_FILTER`,而 terminate 走 `abort()`,不经过它 —— 这一族在客户端连 dump 都没有。服务端同样零命中,所以 §104 的判据在这里更硬 |
| `#if 0` 里的分配不算 | 3 个站点(`DebugService::OnUpdate`、`CombatService::OnHitEvent`、`CombatService::RunTargetUpdates`)的全部命中都在 `#if 0` 块内 | 逐行核对后不改:`OnUpdate` 的 10 行活代码不分配,另外两个函数体整个是 `#if 0` |

还有一条只在本节出现的判据:**「同一个文件的其它行尾是什么」决定了 patch 能不能落**。

`Code/client` 的工作树里混着 CRLF 与 LF,而这与 `HEAD` 无关:`Code/client/CrashHandler.cpp` 与 `Code/client/Services/PluginMessagingService.cpp` 在**未修改**的情况下工作树是 CRLF、blob 是 LF(`core.autocrlf=true` + `.gitattributes:2 * text=auto` 的效果);`Code/client/Systems/LeveledNpcSystem.h`、`Games/Skyrim/Components/TESActorBaseData.cpp`、`Games/Skyrim/Forms/TESFaction.h`、`Games/Skyrim/Misc/GarbageCollector.{cpp,h}` 在未修改的情况下就是 **LF**。所以判据是 `git diff --numstat` 的删除行数,不是 `file` 的输出:一次整文件行尾重写会让 `del` 等于文件行数。42 个改动文件里没有一个满足这一条(最大的 `CharacterService.cpp` 是 `+1548 -1186` / 2572 行)。

### 105.3 可达性

142 个站点落在四条路径上,没有一条需要管理员:

- **收包**:`Code/client/Services/TransportService.cpp:181-220 OnConsume`(服务端能发的每一种包)→ `:222 DispatchMessage` → 每个服务的 `OnNotify*`。这条路径上有 `CharacterService.cpp` 的 30 个定义,是客户端最密的一处。
- **tick**:`TransportService::HandleUpdate` → 各服务的 `Run*`(`RunRemoteUpdates`、`RunSmallHealthUpdates`、`RunNakedNPCBugChecks`、`ApplyQueuedEffects`、`RunExperienceUpdates`、`ApplyCachedWeaponDraws`、`RunWeatherUpdates`、`ProcessLeveledConforms`)。
- **握手泵**:`TransportService::HandshakeThreadMain` / `PumpConnectionLocked` / `PumpHeartbeat` —— 按 `TransportService.h` 的注释,这条路径上的线程**不是游戏线程**。
- **hook 回调**:`HookAudit::Record/Report/Verify`、`GamePatch.h` 的 6 个字节补丁 helper、`Actor`/`TESObjectREFR`/`PlayerCharacter` 等引擎侧 helper。

触发条件是堆耗尽。**本机不编译**(§0/§1),所以每一条都是静态读数,不是复现。

### 105.4 修法

新增 `Code/client/Diagnostics.h`(27 行),与 `Code/server/Diagnostics.h` 逐字同形,经 `Code/client/TiltedOnlinePCH.h:50-52` 进入每个编译单元:

```cpp
// Code/client/TiltedOnlinePCH.h:48-53
#include <spdlog/spdlog.h>

// The client has no shared diagnostic header of its own: every catch handler in
// this target reports through the same helper the server uses.
#include <Diagnostics.h>
#include <glm/glm.hpp>
```

选 PCH 而不是逐文件 include:`Code/client/xmake.lua:17 set_pcxxheader("TiltedOnlinePCH.h")` 已把它设成强制包含,而 `TiltedOnlinePCH.cpp` 只有一行 `#include <TiltedOnlinePCH.h>` —— 不这么写就会有一个不包含它的 TU。

42 个文件、142 个函数体加了 `try { ... } catch (...) { Diagnostics::ReportFailure(__FUNCTION__, "..."); }`。`noexcept` 698 → 699,新增的是 `Diagnostics::ReportFailure` 自己。非 void 的补了显式回退:

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

三处**故意不改**,理由逐行核对过:`DebugService::OnUpdate` 的 10 行活代码只有窗口查找与一次挂起的 `MoveTo`,不分配;`CombatService::OnHitEvent` 与 `RunTargetUpdates` 的函数体整个是 `#if 0`(`RunTargetUpdates` 的 `toRemove.push_back` 也在里面)。给它们加 catch 只会把活代码包进一个空壳。

### 105.5 复核命令(照抄)

```powershell
# 1. 修前修后的四个读数(脚本:等长清空注释/字面量 + 花括号配平 + #if 0 行集)
git stash; node -e "<census>"   # defs=698 guarded=9  live=142 dead=0
git stash pop; node -e "<census>"   # defs=699 guarded=149 live=3   dead=0

# 2. 那 3 个 live 命中的逐行核对(必须全部落在 #if 0 内)
Select-String -Path Code\client\Services\Debug\DebugService.cpp -Pattern "#if 0"
Select-String -Path Code\client\Services\Generic\CombatService.cpp -Pattern "#if 0|#endif"

# 3. 全仓 try/catch 计数
git grep -c -E "\btry\b" -- Code/client   # 修前 18 / 修后 158
git grep -c -E "\bcatch\b" -- Code/client # 修前 14 / 修后 164

# 4. 客户端没有 terminate 兜底
git grep -rn -E "set_terminate|std::terminate|SetUnhandledExceptionFilter|AddVectoredExceptionHandler" -- Code/client   # 零命中

# 5. 编译闭包
Select-String -Path Code\client\xmake.lua -Pattern "pcxxheader|add_headerfiles|add_files"
Get-Item Code\client\TiltedOnlinePCH.cpp | Select-Object Length   # 一行 include ⇒ 必须有强制包含

# 6. 行尾:判据是删除行数,不是 file 的输出
git diff --numstat -- Code/client   # 没有任何一行的 del 等于该文件行数
git show :Code/client/CrashHandler.cpp | Select-Object -First 1   # 未修改,blob 与工作树行尾不同
```

**验证**:`git diff --numstat -- Code/client` = **42 files changed, 5561 insertions(+), 4126 deletions(-)**,1318 个 hunk;`Code/client/Diagnostics.h` 是唯一的新文件(27 行 / LF / 尾换行)。逐文件行数:`Components/LocalAnimationComponent.h` 24→36、`Games/GamePatch.h` 281→341、`Games/Misc/BSScript.cpp` 225→234、`Games/Skyrim/Actor.cpp` 1504→1596、`Games/Skyrim/CellRespawnOverrides.cpp` 158→168、`Games/Skyrim/Forms/EnchantmentItem.cpp` 51→61、`Games/Skyrim/Forms/TESObjectCELL.cpp` 41→51、`Games/Skyrim/Magic/EffectItem.cpp` 44→54、`Games/Skyrim/Misc/BSScript.cpp` 326→335、`Games/Skyrim/Misc/GameVM.cpp` 141→150、`Games/Skyrim/PlayerCharacter.cpp` 294→312、`Games/Skyrim/Projectiles/Projectile.cpp` 196→206、`Games/Skyrim/TESObjectREFR.cpp` 1201→1267、`HookAudit.cpp` 252→282、`Services/Debug/DebugService.cpp` 513→533、`Services/Debug/Views/AnimDebugView.cpp` 409→418、`Services/Debug/Views/ComponentView.cpp` 185→194、`Services/Generic/ActorValueService.cpp` 407→457、`Services/Generic/CharacterService.cpp` 2210→2572、`Services/Generic/CombatService.cpp` 263→272、`Services/Generic/CommandService.cpp` 83→92、`Services/Generic/DiscoveryService.cpp` 353→380、`Services/Generic/InventoryService.cpp` 433→483、`Services/Generic/MagicService.cpp` 753→843、`Services/Generic/ObjectService.cpp` 497→578、`Services/Generic/OverlayService.cpp` 627→697、`Services/Generic/PapyrusService.cpp` 67→86、`Services/Generic/PartyService.cpp` 189→229、`Services/Generic/PlayerService.cpp` 399→408、`Services/Generic/QuestService.cpp` 443→461、`Services/Generic/TransportService.cpp` 778→861、`Services/Generic/WeatherService.cpp` 228→264、`Systems/AnimationSystem.cpp` 187→196、`Systems/InterpolationSystem.cpp` 162→171、`Systems/LeveledNpcSystem.cpp` 83→93、`Systems/ModSystem.cpp` 109→119、`TiltedOnlineApp.cpp` 172→180、`TiltedOnlinePCH.h` 85→89、`Utils.cpp` 95→105、`Utils.h` 78→88、`VersionDb.h` 589→599、`World.cpp` 135→144。

| 读数 | `HEAD` | 现在 |
| --- | --- | --- |
| `noexcept` 定义数 | 698 | 699 |
| 体内含 `catch` | 9 | 149 |
| 体内有分配/日志标记且无 `catch` | 142 | **3**(命中全在 `#if 0` 内) |
| `Code/client` 全文 `try`/`catch` | 18 / 14 | 158 / 164 |

`Diagnostics::ReportFailure` 调用点 **139 个,41 个文件**:`CharacterService.cpp` 30、`Actor.cpp` 9、`MagicService.cpp` 9、`ObjectService.cpp` 8、`TransportService.cpp` 8、`TESObjectREFR.cpp` 7、`OverlayService.cpp` 7、`GamePatch.h` 6、`ActorValueService.cpp` 5、`InventoryService.cpp` 5、`PartyService.cpp` 4、`WeatherService.cpp` 4,其余各 1–3。
每个改过的文件都做过花括号/圆括号配平、`try`/`catch` 缩进配对与行尾检查。`Code/client/xmake.lua:13-14 add_headerfiles("**.h")` / `add_files("**.cpp")` 覆盖新头与全部 42 个文件;`:17 set_pcxxheader("TiltedOnlinePCH.h")` 保证 `Diagnostics.h` 进入每个 TU。
**本机不编译**(§0/§1),所以判据是静态的。

> **教训**:第一条:**服务端修完了不等于这一族修完了。**
> §104 覆盖了 `Code/server`,客户端 698 个 `noexcept` 一个没动 —— 同一族缺陷按目录切,切完一处必须问「还有哪些 target 有同样的形状」。
> 第二条:**`CrashHandler` 不接 terminate。**
> `LPTOP_LEVEL_EXCEPTION_FILTER` 走的是 SEH,而 terminate 走 `abort()`。客户端与服务端都没有 `set_terminate`,所以这一族在两边都没有 dump。
> 第三条:**`#if 0` 里的分配不算缺陷,但必须逐行证明它是 `#if 0`。**
> 三个站点里 `RunTargetUpdates` 有一个真的 `push_back`,只看标记会误判;把 `#if 0` 区间算成行集、再看每个命中行号是否落在里面,才是判据。
## 106. 2026-10-25 场次:编码层 503 个 `noexcept` 一个 `catch` 都没有,25 个在按对端声称的条数分配 —— 判据是「这一层不记日志,所以 `catch` 只能就地恢复」

> **锚点**:`Code/encoding/**` 22 个文件(22 files changed, 560 insertions(+) / 305 deletions(-),115 个 hunk);`Code/encoding/StringCache.h:13-18`、`Code/encoding/StringCache.cpp:26-105`、`Code/encoding/Messages/{AssignObjects,NotifyActorValue,RequestActorValue,NotifyParty,PluginMessaging,StringCacheUpdate}*.cpp`、`Code/encoding/Structs/{ActorValues,Inventory,Factions,Mods,QuestLog,ReferenceUpdate,Tints,ActionReplayChain}.cpp`。

### 106.1 缺陷

`Code/encoding/**` 共 352 个 `.cpp/.h`,里面有 **503 个 `noexcept` 函数定义**,体内含 `catch` 的 **0 个**,全文 `try`/`catch` 计数 **0 / 0**。同一个脚本数出 **25 个**定义在按线上的长度分配:20 个 `resize`、4 个 `insert`、1 个 `push_back`。

这一层是前两节的底座:`Code/client/xmake.lua:41`、`Code/server/xmake.lua:44`、`Code/admin/xmake.lua:18`、`Code/tests/xmake.lua:9` 四个 target 都 `add_deps("SkyrimEncoding")`。客户端 `TransportService::OnConsume` 与服务端 `GameServer::OnConsume` 解析的每一个包都要先过这里的 `DeserializeRaw`。

```
Code/encoding/Messages/AssignObjectsRequest.cpp:22   Objects.resize(count);            // 8 位条数
Code/encoding/Messages/NotifyPartyInfo.cpp:32        PlayerIds.resize(count);          // 8 位条数
Code/encoding/Messages/StringCacheUpdate.cpp:26      Values.resize(count);             // 16 位条数
Code/encoding/Structs/Mods.cpp:41                    ModList.resize(modCount);         // 13 位条数
Code/encoding/Structs/Factions.cpp:42/:50            NpcFactions/ExtraFactions.resize // 9 位条数
Code/encoding/Structs/ActorValues.cpp:71/:85         ActorValuesList.insert({key,value})
Code/encoding/StringCache.cpp:32/:40                 m_stringToId[acValue] = ...; push_back
```

### 106.2 判据

| 对照 | 事实 | 结论 |
| --- | --- | --- |
| **这一层没有日志,而且是有意的** | `git grep -rn "spdlog\|fmt::" -- Code/encoding` **零命中**;`Code/encoding/xmake.lua:18 add_packages("hopscotch-map", "glm", "tiltedcore")` 没有 `spdlog`;`Code/tests/xmake.lua:10-15` 也不加 | 前两节用的 `Diagnostics::ReportFailure` 在这里**用不了** —— 加它会改这一层的依赖图,还会把 spdlog 拖进只依赖 `SkyrimEncoding` 的 `TPPTests`。这是本节与前两节唯一的判据差异 |
| 同层已有的恢复形状 | `Code/encoding/Structs/CachedString.cpp:37-47` 在 `noexcept` 里用 `*this = TiltedPhoques::String();` 恢复,注释逐字「It used to assert(false), which aborts the process: this frame is noexcept, so there is no way to fail softly」 | 修法不是新发明:同一层早就用「就地恢复」而不是「上报」 |
| `WireLimits.h` 的自我说明 | `:81-83`「Resizing down cannot throw」 | 唯一留在原样的站点正是向下 `resize`,不是遗漏 |
| 上界的值由我们自己的发送侧决定 | 条数分别来自 8/13/16 位与 VarInt,而 `kMaxObjectEntities`/`kMaxEntryCount`/`kMaxActorValueCount` 等上界已先夹过一遍 | 这里要防的不是「超长」,是「夹过之后仍然会分配的那一次失败」 |
| 「下游有守卫」不等于「值被挡住了」 | `Messages/ClientMessageFactory.cpp:32-42` 与 `ServerMessageFactory.cpp` 的 `Extract` 都 `noexcept`,直接调 `s_*MessageExtractor[opcode](aReader)`,每个 extractor 里 `ptr->DeserializeRaw(aReader)` | 抛出点在里面一层,工厂的 `noexcept` 挡不住它 |

还有一条只在本节出现的判据:**回退值不能是新造的语义,得是「空消息本来就会产生的那个答案」**。

每一个 `catch` 里的恢复都是「这条消息什么都没带」:

```cpp
// Code/encoding/Structs/ActorValues.cpp:92-97(修复后)
catch (...)
{
    ActorValuesList.clear();
    ActorMaxValuesList.clear();
}
```

两张表在 `try` 的开头就被 `clear()` 过,所以这里的 `clear()` 只是把状态还原成「这条消息没写任何 actor value」—— 与发送方发了一条空消息完全同形。这样就不需要引入任何一种「部分成功」的状态,也不需要调用方多写一个分支。

唯一的例外是 `StringCache::Add`,因为它的返回值被 `Deserialize` 当作游标比较:

```cpp
// Code/encoding/StringCache.h:13-18(新增常量)
static constexpr uint32_t kInvalidId = 0xFFFFFFFFu;

// Code/encoding/StringCache.cpp:44-48
catch (...)
{
    m_stringToId.erase(acValue);   // 撤掉先写的那张表,缓存仍是发送方的前缀
    return kInvalidId;             // 任何游标都不可能等于它
}
```

`StringCache::Deserialize:107-140` 逐条比较 `Add` 的返回值和自己的游标,不相等就停下整条消息。返回一个不可能相等的值,正好让它走那条本来就存在的分支,而不是新加一条。

### 106.3 可达性

25 个站点全部落在两条路径上,没有一条需要管理员:

- **收包解析**:`ClientMessageFactory::Extract`(`Code/encoding/Messages/ClientMessageFactory.cpp:32`,服务端 `GameServer.cpp:609` 调用)与 `ServerMessageFactory::Extract`(客户端 `TransportService.cpp:200` 调用)都 `noexcept`,都调 `DeserializeRaw`。任何一个已连接的对端都能触发 8 个 `Messages/*` 站点。
- **状态与结构体**:`Structs/{ActorValues,Inventory,Factions,Mods,QuestLog,ReferenceUpdate,Tints,ActionReplayChain}.cpp` 与 `StringCache.cpp`。`StringCache::Add` 还有两个非收包调用点:`Code/server/GameServer.cpp:1043`(`StringCache::Get().Serialize(startId)`)与 `Code/encoding/Structs/CachedString.cpp:27/:62`(`AddWanted`)。

触发条件是堆耗尽。**本机不编译**(§0/§1),所以每一条都是静态读数。

### 106.4 修法

21 个函数体加 `try { ... } catch (...) { <就地恢复> }`。`noexcept` 503 → 503,一个都没删,也没有新增。

| 站点 | catch 里的恢复 | 语义 |
| --- | --- | --- |
| `AssignObjectsRequest` / `AssignObjectsResponse` / `ShiftGridCellRequest` | `Objects.clear()` / `Cells.clear()` | 空表 = 「没有分配任何物件 / 没有要访问的格子」 |
| `NotifyPartyInfo` / `NotifyPartyJoined` | `PlayerIds.clear()` | 队长字段在列表之前读,保留;只丢成员表 |
| `NotifyPluginMessaging` / `PluginMessagingRequest` | `PluginData.clear()` | 空载荷 = 插件眼里的一条丢失消息 |
| `StringCacheUpdate::DeserializeRaw` | `Values.clear()` | `StartId` 已读,接收侧对不上游标就丢整条 |
| `ActionReplayChain` | `Actions.clear()` | 图重置标志已读,重置动画图并回放零个动作 |
| `ActorValues` / 四个 `*ActorValueChanges` | 两张表都 `clear()` | 「没有任何 actor value」 |
| `Factions` / `QuestLog` / `Tints` / `Inventory::Deserialize` | 对应表 `clear()` | 空表是安全答案,调用方遍历后什么都不做 |
| `Inventory::Entry::Deserialize` | `EnchantData.Effects.clear()` | 无附魔的物品,附魔 id 与充能已读并保留 |
| `Inventory::AddOrRemoveEntry` | 无(失败即未改变) | 唯一会分配的是 `push_back`,失败后表原样 |
| `Mods::Deserialize` | `ModList.clear()` | 客户端不建 id 映射,服务端下次握手重发 |
| `ReferenceUpdate` | `ActionEvents.clear()` | 位移已读并保留,只丢动作 |
| `StringCache::AddWanted` | 无 | 记不下的名字下次引擎再读时重新想要 |
| `StringCache::Serialize` | `update.Values.clear()` | 游标只在 insert 成功后前进,下次调用重发同一片 |
| `StringCache::Add` | `m_stringToId.erase(acValue); return kInvalidId;` | 见 §106.2 末 |

`WireLimits::Clamp`(`:84-88`)是**唯一留在原样**的站点,理由写在它自己的注释里:`resize` 只在 `aValue.size() > aMaxBytes` 时发生,是向下收缩。

### 106.5 复核命令(照抄)

```powershell
# 1. 修前修后的三个读数(等长清空注释/字面量 + 花括号配平)
git stash; node -e "<census>"   # defs=503 guarded=0  live=25
git stash pop; node -e "<census>"   # defs=503 guarded=25 live=1

# 2. 那一处剩下的为什么不是缺陷
Select-String -Path Code\encoding\WireLimits.h -Pattern "Resizing down cannot throw" -Context 0,4

# 3. 这一层不依赖 spdlog(所以前两节的 helper 用不了)
git grep -rn -E "spdlog|fmt::" -- Code/encoding   # 零命中
Select-String -Path Code\encoding\xmake.lua -Pattern "add_packages"
Select-String -Path Code\tests\xmake.lua -Pattern "add_packages" -Context 0,8

# 4. 同层已有的恢复形状
git grep -n "fail softly" -- Code/encoding

# 5. 全仓 try/catch 计数
git grep -c -E "\btry\b" -- Code/encoding   # 修前 0 / 修后 25
git grep -c -E "\bcatch\b" -- Code/encoding # 修前 0 / 修后 26

# 6. 编译闭包与四个消费者
Select-String -Path Code\encoding\xmake.lua -Pattern "pcxxheader|add_headerfiles|add_files"
git grep -rn "SkyrimEncoding" -- Code
```

**验证**:`git diff --numstat -- Code/encoding` = **22 files changed, 560 insertions(+), 305 deletions(-)**,115 个 hunk。逐文件行数:`Messages/AssignObjectsRequest.cpp` 29→40、`AssignObjectsResponse.cpp` 29→38、`NotifyActorMaxValueChanges.cpp` 50→61、`NotifyActorValueChanges.cpp` 50→61、`NotifyPartyInfo.cpp` 39→48、`NotifyPartyJoined.cpp` 39→48、`NotifyPluginMessaging.cpp` 44→54、`PluginMessagingRequest.cpp` 49→58、`RequestActorMaxValueChanges.cpp` 50→61、`RequestActorValueChanges.cpp` 50→61、`ShiftGridCellRequest.cpp` 42→51、`StringCacheUpdate.cpp` 36→46、`StringCache.cpp` 143→175、`StringCache.h` 31→37、`Structs/ActionReplayChain.cpp` 45→55、`Structs/ActorValues.cpp` 88→99、`Structs/Factions.cpp` 56→66、`Structs/Inventory.cpp` 215→243、`Structs/Mods.cpp` 51→61、`Structs/QuestLog.cpp` 56→65、`Structs/ReferenceUpdate.cpp` 46→56、`Structs/Tints.cpp` 77→86。

| 读数 | `HEAD` | 现在 |
| --- | --- | --- |
| `noexcept` 定义数 | 503 | 503 |
| 体内含 `catch` | 0 | 25 |
| 体内有分配标记且无 `catch` | 25 | **1**(`WireLimits::Clamp`,向下 `resize`) |
| `Code/encoding` 全文 `try`/`catch` | 0 / 0 | 25 / 26 |

**这一层仍然不依赖 `spdlog`**:`git grep -rn -E "spdlog|fmt::" -- Code/encoding` 改前改后都是零命中,`Code/encoding/xmake.lua:18` 与 `Code/tests/xmake.lua:10-15` 的包列表一个字没动。`Code/tests/encoding.cpp` 的 8 个 `StringCache` 测试全部依赖 `Add` 返回真实 id(其中 `:1595-1596` 直接 `REQUIRE(cId == 0)`),新常量只在 `catch` 路径上返回,成功路径的返回值不变。
每个改过的文件都做过花括号/圆括号配平、`try`/`catch` 缩进配对与行尾检查(22 个文件全部 CRLF,改后仍然 CRLF)。`Code/encoding/xmake.lua:9 set_pcxxheader("EncodingPch.h")` 与 `:6-7 add_headerfiles("**.h")`/`add_files("**.cpp")` 覆盖全部 22 个文件。
**本机不编译**(§0/§1),所以判据是静态的。

> **教训**:第一条:**「上一节用的修法」不是可以照抄的,先问这一层有没有那个依赖。**
> 服务端与客户端都能 `spdlog::error`,所以那两节的 `catch` 可以上报;`Code/encoding` 有意不依赖 spdlog,照抄会改依赖图并把 spdlog 拖进 `TPPTests`。同一族缺陷在不同层里的正确修法可以不同,判据是那一层的约束,不是上一节的文件。
> 第二条:**回退值要挑「空输入本来就会产生的那个答案」,不要新造语义。**
> `clear()` 不是「兜底」,是把状态还原成这条消息什么都没带 —— 调用方本来就处理空表,所以不需要新增任何分支。唯一需要新常量的是返回值被当作游标比较的 `StringCache::Add`,而那个常量的语义也来自同一条规则:它必须等于一个**不可能出现的游标**。
> 第三条:**向上收缩的 `resize` 不抛,这是留下它的理由,不是漏看。**
> `WireLimits::Clamp` 自己写着「Resizing down cannot throw」。判据要写进文档,否则下一个审计的人会把这一处当成遗漏再改一遍。
## 107. 2026-10-26 场次:同一个审计脚本漏掉的 9 个 `noexcept` 分配点 —— 判据是「先问脚本漏了什么 token,再问剩下的算不算缺陷」

> **锚点**:`Code/client/Games/Skyrim/Actor.cpp:748-773`、`Code/client/Games/Skyrim/CellRespawnOverrides.cpp:101-107`、`Code/client/Services/Generic/{ActorValueService,DiscoveryService,ObjectService,RunnerService}.cpp`、`Code/client/SkyrimVM64.cpp:68-81`、`Code/client/Systems/AnimationSystem.cpp:101-119`、`Code/components/es_loader/ESLoader.cpp:34-79`(9 files changed, 260 insertions(+) / 135 deletions(-),52 个 hunk)。

### 107.1 缺陷

前三节(§104 服务端、§105 客户端、§106 编码层)用的是同一个脚本,分配标记是 `push_back|emplace|insert\s*\(|resize|reserve|make_shared|make_unique|new|String|Vector|Map|spdlog`。这一节把扫描换成**逐行、只看函数体、屏蔽注释与 `#if 0`** 之后,又找出 9 个:

| 站点 | 漏掉的 token | 分配点 |
| --- | --- | --- |
| `Code/client/Games/Skyrim/Actor.cpp:748 Actor::GetEssentialActorValues` | `insert` | `ActorValuesList.insert({i, value})` ×3 + `ActorMaxValuesList.insert(...)` ×3 |
| `Code/client/Services/Generic/ActorValueService.cpp:42 CreateActorValuesComponent` | `insert` | 两个 map 各 `kActorValueCount` 次 `insert` |
| `Code/client/Services/Generic/ActorValueService.cpp:103 BroadcastActorValues` | `insert` | 每 tick 每个 actor 两次 `insert` + 两次 `Send` |
| `Code/client/Services/Generic/DiscoveryService.cpp:266 VisitForms` | `insert` | `m_forms.insert(formId)` 每个未发现 actor 一次 |
| `Code/client/Services/Generic/ObjectService.cpp:73 GetPlayerStashContainers` | `insert` | `containers.insert(...)` 每个阵营最多两次 |
| `Code/client/Systems/AnimationSystem.cpp:101 AddActionsForReplay` | `insert` | `TimePoints.insert(..., begin, end)` |
| `Code/client/SkyrimVM64.cpp:68 DescribeOffset` | `std::to_string` | 返回值里的 `std::to_string` 与 `std::string("<unreadable>")` |
| `Code/client/Services/Generic/RunnerService.cpp:12 Queue` | `std::function` | 形参按值传入 ⇒ 调用点构造 + `m_runner.Add` 建节点 |
| `Code/components/es_loader/ESLoader.cpp:34 BuildRecordCollection` | `MakeUnique` | `MakeUnique<RecordCollection>()` + `fs::is_directory` |

前六个是同一个 token 的两种写法各被漏掉一半:第一轮写 `\binsert\b`,把 `insert({key, value})` 命中、把 `map.insert(...)` 也命中,于是被当成噪声收紧成 `insert\s*\(`;收紧之后 `insert({...})` 又漏了。**两轮各漏一半,谁也没发现。**

### 107.2 判据

| 对照 | 事实 | 结论 |
| --- | --- | --- |
| 前一批的修法可以直接复用 | `Code/client/Diagnostics.h`(`f0877c1b`)经 `Code/client/TiltedOnlinePCH.h:52` 进入客户端的每个 TU | 8 个客户端站点用 `Diagnostics::ReportFailure`,与 §105 同形 |
| `es_loader` **不在**那条 include 链上 | 它有自己的 xmake target(`Code/components/es_loader/xmake.lua` 里 `component("ESLoader")`)和自己的强制包含 `stdafx.h`,后者只 `#include <spdlog/spdlog.h>` | 这一处用本地 `try`/`catch` + `spdlog::error`,与 §106 的编码层同理:**先问这一层能不能用那个 helper** |
| 「同名不同层」要分开看 | `Actor::GetEssentialActorValues` 与 `ActorValueService::CreateActorValuesComponent` 都写 `ActorValuesList` | 前者返回快照、后者写组件,回退语义不同,注释分别写清楚 |
| `es_loader` 里其余 8 个站点**不是**缺陷 | `ESLoader.cpp:49-54` 的 `LoadFiles()` 调用被 `/* */` 注释掉,`BuildRecordCollection` 在 `:47` 直接 `return MakeUnique<RecordCollection>();`;`pluginFile.LoadFile`(`:127`)与 `IndexRecords` 的唯一调用点都在 `LoadFiles` 内 | `TESFile::LoadFile`/`IndexRecords`、`RecordCollection::GetFormType`、`Chunks::ParseValue`、`CONT`/`GMST`/`TES4::ParseChunks`、`TESFile::GetFormIdPrefix` 全在注释块之后的死路径上,与 §104 的 `#if 0` 三站点同类,**判为不改** |

还有一条只在本节出现的判据:**「脚本没报」不等于「脚本查过」。**

这一节的全部价值不在那 9 个 `catch`,在于发现了审计工具本身的两种系统性漏检:

1. **token 表的宽严是两次独立的判断,而两次都可能错。** 宽的时候把 `insert` 命中,收窄之后漏掉 `insert({...})` —— 同一族调用在同一个文件里两种写法并存时,任何一版正则都会漏一半。正确做法不是「找到更好的正则」,是**逐行扫描 + 多个 token 并存 + 把命中行号打出来人工看一遍**。
2. **「函数体」的边界要屏蔽注释,而注释有两种。** `// ...` 一行就完,`/* ... */` 可以跨几十行 —— `ESLoader::BuildRecordCollection` 的活代码只有 6 行,后面跟着 6 行被注释掉的调用。只屏蔽 `//` 的脚本会把 `LoadFiles()` 当成活代码,于是把 `LoadFile`/`IndexRecords`/`ParseChunks` 一整条死路径报成 8 个缺陷。

### 107.3 可达性

8 个客户端站点全在帧循环或收包路径上,没有一条需要管理员:

- **tick**:`ActorValueService::OnUpdate` → `BroadcastActorValues`(每 tick 遍历全部本地 actor);`DiscoveryService::OnUpdate` → `VisitForms`(每 tick 遍历 `ProcessLists::highActorHandleArray`);`AnimationSystem` 的 replay 入队;`RunnerService::OnUpdate` → `m_runner.Drain()`。
- **hook 回调**:`SkyrimVM64.cpp:68 DescribeOffset` 由 `HookVMUpdate` 调用(`:136`),每 `kTickHeartbeatEvery` 个 tick 一次。
- **启动**:`ESLoader::BuildRecordCollection` 由 `Code/server/World.cpp:47` 在 `World` 构造时调用一次。

触发条件是堆耗尽。**本机不编译**(§0/§1),所以每一条都是静态读数。

### 107.4 修法

9 个函数体加 `try`/`catch`。`noexcept` 一个都没删,也没有新增。回退值:

| 站点 | catch 里的恢复 | 语义 |
| --- | --- | --- |
| `Actor::GetEssentialActorValues` | 两张表 `clear()` | 空快照 = 「没有本质 actor value 变化」 |
| `ActorValueService::CreateActorValuesComponent` | 两张表 `clear()` | 组件已建但为空 = 「还不知道任何 actor value」 |
| `ActorValueService::BroadcastActorValues` | 无(组件只在 `insert` 成功后写) | 没送出去的值得下个 tick 重试,不会记下一个服务端没被告知的值 |
| `DiscoveryService::VisitForms` | 无(`m_forms` 保留已发现的) | 这个 tick 没访问到的 actor 就是「还没发现」,下个 tick 从 `m_forms` 接着走 |
| `GetPlayerStashContainers` | `return Set<const TESObjectREFR*>{};` | 空集 = 「没有容器是赃物箱」,与上面「没有 ModManager」分支同一个答案 |
| `AnimationSystem::AddActionsForReplay` | 无 | 组件保留已有的 time point、`ReplayCount` 不动 ⇒ 不会重复入队 |
| `DescribeOffset` | `return std::string("<unavailable>");` | 心跳行照常打印,只是这个数字读不出来 |
| `RunnerService::Queue` | 无 | 没排上的任务就是不执行,队列里不留半条 |
| `ESLoader::BuildRecordCollection` | `return nullptr;` | 与「Data 目录不存在」同一个答案,调用方 `World.cpp:48` 于是不 emplace 任何 server mod |

`ESLoader` 那一处的报告是本地写的,而且**自己再套一层 `try`/`catch`**:

```cpp
// Code/components/es_loader/ESLoader.cpp:63-78
catch (...)
{
    // This component has its own precompiled header (stdafx.h) and its own xmake
    // target, so the shared Diagnostics helper the client and the server use is not
    // on its include path. spdlog is, so the report is made here, under the same
    // inner guard: reporting a failure must not become the failure.
    try
    {
        spdlog::error("{}: ran out of memory while building the record collection", __FUNCTION__);
    }
    catch (...)
    {
    }

    return nullptr;
}
```

与 `Code/server/Diagnostics.h` 和 `Code/client/Diagnostics.h` 里那句注释同一个理由。

### 107.5 复核命令(照抄)

```powershell
# 1. 逐行扫描,带完整 token 表(含 MakeUnique/std::to_string/std::function/fstream/filesystem)
node -e "<census>"   # LIVE unguarded = 9(修前)/ 0(修后,只剩 §106 已判的 Clamp)

# 2. 屏蔽两种注释:# 与 /* */
Select-String -Path Code\components\es_loader\ESLoader.cpp -Pattern "/\*" -Context 2,8

# 3. es_loader 有自己的 target 与 PCH
Get-Content Code\components\es_loader\xmake.lua
Select-String -Path Code\components\es_loader\stdafx.h -Pattern "spdlog|Diagnostics"

# 4. 死路径的证据:LoadFiles 的唯一调用点在注释块里
Select-String -Path Code\components\es_loader\ESLoader.cpp -Pattern "LoadFiles"
Select-String -Path Code\components\es_loader\ESLoader.cpp -Pattern "pluginFile.LoadFile|IndexRecords"

# 5. 全仓口径
node -e "<global census>"   # noexcept defs=1514 bodies-with-catch=252  LIVE unguarded=9
```

**验证**:`git diff --numstat -- Code` = **9 files changed, 260 insertions(+), 135 deletions(-)**,52 个 hunk。逐文件:`client/Games/Skyrim/Actor.cpp` +18/-6(1596→1608)、`client/Games/Skyrim/CellRespawnOverrides.cpp` +4/-0(168→172)、`client/Services/Generic/ActorValueService.cpp` +75/-49(457→483)、`client/Services/Generic/DiscoveryService.cpp` +59/-46(380→393)、`client/Services/Generic/ObjectService.cpp` +25/-14(578→589)、`client/Services/Generic/RunnerService.cpp` +13/-1(21→33)、`client/SkyrimVM64.cpp` +12/-1(182→193)、`client/Systems/AnimationSystem.cpp` +16/-4(196→208)、`components/es_loader/ESLoader.cpp` +38/-14(151→175)。

这 9 个文件的 `try`/`catch` 计数从 **29 / 29 到 39 / 39**,新增 10 对(9 个函数体 + `ESLoader` 里那层嵌套的报告守卫);`Diagnostics::ReportFailure` 新增调用点 9 个。

全仓普查(**15 个 target、`noexcept` 定义 1514 个、体内含 `catch` 252 个**)在换成逐行 + 完整 token 表之后,剩下的未守站点只有:

- `Code/encoding/WireLimits.h:84 Clamp` —— 向下 `resize`,§106 已判;
- `Code/components/es_loader` 的 8 个死路径站点 —— 本节已判。

每个改过的文件都做过花括号/圆括号配平、`try`/`catch` 缩进配对与行尾检查;`Code/client/Games/Skyrim/CellRespawnOverrides.cpp` 与 `Code/components/es_loader/ESLoader.cpp` 都补上了报告,不留空 `catch`。
**本机不编译**(§0/§1),所以判据是静态的。

> **教训**:第一条:**审计脚本本身要审计。**
> 三轮下来「全仓 211 + 698 + 503 个 `noexcept`」是脚本给的数字,而同一批脚本漏了 9 个站点 —— 原因是 `insert` 的宽严两版各漏一半。结论不是「正则写得更准」,是**每次审计都要换一种扫描口径重跑一遍,并对差异逐条解释**;两次结果一致才是收敛,一次结果只是采样。
> 第二条:**`/* */` 和 `#if 0` 一样会藏死代码,而它更隐蔽。**
> `#if 0` 有配对、能算行集;`/* */` 只有一对符号,几十行的块和一行注释长得一样。`ESLoader::BuildRecordCollection` 的活代码 6 行、注释 6 行,把注释当活代码会凭空造出 8 个「缺陷」。
> 第三条:**判据要落在「这一层能不能用那个 helper」上,不是「上一节怎么修的」。**
> 同一批里 8 个客户端站点用 `Diagnostics::ReportFailure`,`es_loader` 那一个只能用本地 `spdlog::error` + 自己的 `try` —— 差别不在缺陷,在这一层的 include 链和 xmake target。

---

## 108. 2026-10-10 场次:1.6.x 上勾选任一可选插件后启动即闪退 —— 判据是「这个模块被加载了,不等于它被选中了」

> **锚点**:`Code/plugins/patches/STRPluginMessagingAPI/0008-do-not-binpatch-the-game-when-the-framework-owns-it.patch`
> (`src/STRPMFrameworkRuntime.h` + 四个 resolver 文件)。

### 108.1 现象与范围

**现象**:游戏 1.6.x,MO2 加载 SKSE,只要在安装向导里勾了**任意一个**可选插件
(OStimTogether / MorphSyncTogether / IEDSyncTogether / AnimSyncTogether /
DAVSyncTogether / TradeTogether),打开游戏**直接闪退**;一个都不勾则正常。

**为什么是「任意一个」**:六个插件的 `payload` 各不相同,但它们**都要**
`STRPluginMessagingAPI`,而该插件的 `artifacts` 里有**两个** DLL
(`Code/plugins/plugins.json:63-72`):

```json
{ "from": "STRPluginMessagingAPI.dll",     "dest": "SKSE/Plugins/STRPluginMessagingAPI.dll" },
{ "from": "STRPluginMessagingBridge.dll",  "dest": "SKSE/Plugins/STRPluginMessagingBridge.dll" }
```

`rationale` 写的就是「Installed whenever any companion plugin is selected」。所以
**勾选数从 0 变成 1** 时,新进进程的不是那一个插件,而是**这三个 DLL**。缺陷在
第二个上,与用户勾了哪个插件无关 —— 这正是它看起来像「插件各自有 bug」的原因。

向导侧的同一件事,数出来是**十二处**:

```powershell
Select-String -Path GameFiles\Skyrim\fomod\ModuleConfig.xml -Pattern 'OptionalPlugins/STRPluginMessagingAPI'
# 61, 90, 103, 116, 129, 142, 322, 351, 364, 377, 390, 403  —— 十二个分支各一行
<folder source="OptionalPlugins/STRPluginMessagingAPI" destination="" priority="0" />
```

十二 = 六个可选插件 × 两个语言分支(中/英各一份 `ModuleConfig.xml`)。**没有任何一个
分支能绕开它**,这也就是「勾任意一个都闪退」的字面解释。

### 108.2 根因:bridge 在 `SKSEPlugin_Load` 里就开扫内存、装 VEH、置 TF、写 INT3

`STRPluginMessagingBridgeSKSE.cpp` 的 `SKSEPlugin_Load` 原本无条件调三个 resolver:

```cpp
STRPMChatUiSuppressBootstrap::Start();
STRPMProxyResolverBootstrapV2::Start();
STRPMProxyResolverTrace::Start();
```

三者在加载阶段做的事(逐条读出来的,不是推断):

| 行为 | 证据 |
| --- | --- |
| 遍历**整个地址空间**的已提交可读/可执行 span | `STRPluginMessagingBridge.cpp:351 EnumerateMemory`,`:371` 过滤 `MEM_COMMIT` |
| 按字符串锚点 `"Send chat message of type {}: '{}' "` 在**所有**可读 span 里搜 | `:388 FindAnchorCopies`,`:399 std::memcmp` |
| 按 RIP 相对 xref 反查调用点 | `:405 FindRipRelativeXrefs` |
| 装**向量化异常处理器** | `:630 AddVectoredExceptionHandler`,`STRPMProxyResolverTrace.h:315` |
| 在**活线程**上置 Trap Flag(`0x100`) | `STRPMProxyResolverTrace.h:225/261/281` |
| 把**游戏函数首字节**改成 `0xCC`(INT3) | `:637 PatchByte(address, 0xCC)`,经 `VirtualProtect` 写 `PAGE_EXECUTE_READWRITE` |
| 恢复时按 `g_originalTransportByte` 写回 | `:594` |

这条链**设计上就是给 STR 1.8.0 那个二进制用的** —— `kTargetBuild` 写死
`"Skyrim Together Reborn 1.8.0"`(`:27`),`kSendChatOpcode = 38` 写死(`:29`),
`FunctionContainsImmediate32(*targetBounds, 0x00010000u)` 的注释写的是
「STR 1.8.0 TransportService::Send constructs Buffer(1 << 16)」(`:554`)。

### 108.3 为什么「框架自己答同一个查询」这件事没有让它停下来

框架运行时**导出同名入口**并且答得更好(`Code/client/Services/PluginMessagingExport.cpp`):

| 查询 | 框架运行时 | bridge |
| --- | --- | --- |
| `STRPM_QueryTransportInterface` | `:151`,走 `PluginMessagingService` 自己的会话 | 扫内存找 STR 的 `TransportService::Send` |
| `STR_QueryPluginMessagingProxyResolver` | `:80`,读框架自己的组件 | 靠 VEH 抓 `ConnectionID -> FormID` |

而且 `0001-prefer-the-framework-transport.patch` 已经让**门面优先挑框架运行时**
(`GetLoadedFrameworkRuntime()`,只 `GetModuleHandleW` 不 `LoadLibraryW`)。所以
框架用户在 1.6.x 上**永远走不到 bridge** —— 它的 `Start()` 根本不会被调用。

**缺陷是:没被选中,不等于没被加载。** SKSE 会把 `Data/SKSE/Plugins` 下每个
DLL 都加载并调用 `SKSEPlugin_Load`;`STRPluginMessagingBridge.dll` 就装在那里。
于是上面那张表里的**七件事全都在启动期跑了一遍**,而结果没有任何人使用。

### 108.4 为什么它正好在 1.6.x 上炸、而本机(1.5.97)测不出

锚点字符串是**两个运行时都有**的:

```
Code/client/Services/Generic/OverlayClient.cpp:153   spdlog::info(L"Send chat message of type {}: '{}' ", ...)
```

所以 `FindAnchorCopies` 在框架自己的运行时里**必定命中**。接着
`FindRipRelativeXrefs` 会在框架代码里找到一条指向该锚点的 xref,`GetFunctionBounds`
给出函数边界,`EnumerateDirectCalls` 找到调用点,`:554` 再按「谁含 `0x10000` 立即数」
挑一个当 `TransportService::Send` —— 这是**在别人的二进制上按 STR 1.8.0 的形状猜地址**。
猜中与否只影响崩在哪一步:

| 猜的结果 | 后果 |
| --- | --- |
| 挑出唯一候选 | `ArmTransportCaptureBreakpoint` 把该函数首字节写成 `0xCC`,游戏下次执行到它就是 `EXCEPTION_BREAKPOINT`;VEH 若因地址不符而 `CONTINUE_SEARCH`,就是未处理异常 → 闪退 |
| 挑不出唯一候选 | `ResolveProcessChatMessage` 返回 false,循环每 750ms 重扫一次**整个地址空间**(`kBootstrapRetryDelay`,`:36`),启动期持续占用 CPU 并不断 `VirtualQuery` |
| 命中但时机不对 | `:610` 抓 `Rcx` 当 `this`,`:618` 把 `Rip` 改回函数入口重执行 —— 在非 STR 的 `Send` 上重执行,语义已变 |

**为什么本机 1.5.97 看不到**:本地没有 1.6.x 实机;而**静态门禁全部是绿的** ——
十个 gate 一个不落地通过(见 108.6)。这条缺陷在**任何一条门禁的口径之外**:
`check_exports.py` 查的是「契约名有没有被导出」,`check_skse_entry_points.py` 查的是
「三个入口在不在」,`check_transport_compat.py` 查的是 opcode 序号。**没有一条问
「这个模块在什么条件下才会真的动手」。**

### 108.5 修法:把「框架在不在」这件事查在**动手的那一刻**

补丁加一个 `src/STRPMFrameworkRuntime.h`,里面是唯一一处判定:

```cpp
inline bool STRPMFrameworkRuntimeIsMapped() noexcept
{
    return GetModuleHandleW(L"SkyrimTogetherRuntime.dll") != nullptr ||
           GetModuleHandleW(L"SkyrimTogetherRuntime_1_5.dll") != nullptr;
}
```

**只 `GetModuleHandleW`,绝不 `LoadLibraryW`**:运行时一个游戏版本族一个名字,两个
ABI 不同,按名加载会把**跨 ABI 的映像**拉进进程。这与门面自己那句注释是同一条规则。

判定**不是查一次就完**,而是查在四个「即将动手」的点上,因为**这四处各有各的时机**:

| 站点 | 时机 | 为什么不能只在加载期查 |
| --- | --- | --- |
| `STRPluginMessagingBridge.cpp` `BootstrapLoop` | 每轮重扫前 | 循环会重试,运行时可能在循环开始之后才出现 |
| `STRPMProxyResolverBootstrapV2.h` `Worker` | 每轮重扫前 | 同上;它每 2 秒一轮,`SleepInterruptible` 可被打断 |
| `STRPMProxyResolverTrace.h` `Worker` + `ArmStepOver` + `RearmEntryHookIfNeeded` | 装 VEH 前 / 每次下 TF 前 | 这三处是**在异常处理器里**被调用的,那时 worker 早已跑过 |
| `STRPMChatUiSuppress.h` `PatchByte` | 每次写 `0xCC` 前 | 全仓 INT3 的唯一出口,四个写入点都经过它 |

最后一处是关键:**`PatchByte` 是这条链上唯一的写口**,所以判在它里面等于一次性盖住
所有写路径(聊天 UI 候选点、发送捕获断点、单步重挂),不必逐个改调用者。

**恢复必须仍然放行**:`if (value == 0xCC && ...)` —— 只拦「装断点」,不拦「拆断点」。
否则一个在运行时出现**之前**就装好的断点永远拆不掉,那个 `0xCC` 会留在游戏代码里。
这是本次唯一一处「同一条路径要放行一半」的地方,写清楚理由而不是顺手一起拦。

### 108.6 两条补丁门禁**口径不同**,而它们都是对的

本轮撞上一个值得记下来的门禁结构:

| 门禁 | 它跑在什么树上 | 它问什么 |
| --- | --- | --- |
| `plugin_patches.py verify` | **索引**(钉住的原始内容) | 这个补丁**单独**能不能贴到钉住的 commit 上 |
| `plugin_patches.py apply`(CI) | **工作树**,按文件名顺序 | 贴完前面几个之后,这个还能不能贴 |

两者都必要,而且**没有一个能替代另一个**:`verify` 防的是「pin 动了而补丁没重写」,
`apply` 防的是「补丁之间互相打架」。第一版补丁**同时踩了两个坑**,而且是**交替踩的**:

1. 先按工作树(已贴 0001-0007)生成 → `verify` 报「does not apply to the pinned commit」;
2. 改成按钉住内容生成 → `apply` 在 0002 那一步之后失败,因为 0002 改了同一批行;
3. 最终解法**不是二选一**,是**换文件**:把判定放进 `src/STRPMFrameworkRuntime.h`
   这个**没有任何既有补丁碰过**的新头文件,再让四处调用点分别 include 它 ——
   四个调用文件里 `STRPMChatUiSuppress.h` / `STRPMProxyResolverBootstrapV2.h` /
   `STRPMProxyResolverTrace.h` / `STRPluginMessagingBridge.cpp` **也都是既无补丁碰过的**。
   于是两份口径同时成立。

> **判据**:写补丁前先列一张「哪些文件已经被既有补丁占着」的表
> (`Select-String -Path patches/*.patch -Pattern '^--- a/(.+)$'`),
> 然后**优先把改动放进没被占的文件**。绕不开时,补丁必须按**最终顺序**生成,
> 并且要手工跑一次「干净树 + 按名排序全贴」——那是 CI 真正做的事。

### 108.7 验证(可复跑)

```powershell
# 1. 补丁对钉住的 commit 干净可用,且改的文件确实在构建闭包里
python Code/plugins/tools/plugin_patches.py verify
#    PATCH VERIFY OK / 39 patch(es) apply to their pinned commits

# 2. 补丁当前是否都贴在树上
python Code/plugins/tools/plugin_patches.py check
#    PATCH CHECK OK

# 3. 【关键】CI 真正做的事:干净树 + 按文件名排序全贴
#    verify 只证明「每个补丁单独能贴到钉住内容上」,证明不了「它们互相不打架」。
#    这一步是本轮唯一能发现补丁冲突的测试,务必手工跑一次:
$sc = "$env:TEMP\strpm-ci"; Remove-Item $sc -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $sc | Out-Null
Push-Location plugins/STRPluginMessagingAPI
cmd /c "git archive HEAD | tar -x -C $sc"
Pop-Location
Push-Location $sc
git init -q .; git add -A; git -c user.email=a@b -c user.name=a commit -qm base
Get-ChildItem "$PWD\..\..\Code\plugins\patches\STRPluginMessagingAPI\*.patch" |
  Sort-Object Name | ForEach-Object { git apply $_.FullName; "exit=$LASTEXITCODE $($_.Name)" }
Pop-Location
#    期望 8/8 exit=0

# 4. 十个门禁全绿(本机)
foreach ($t in 'plugin_patches.py','check_skse_entry_points.py','check_ostimng_versions.py',
               'check_ostimng_build.py','check_skse_bootstrap.py','check_exports.py',
               'check_skee_abi.py','check_transport_compat.py','check_workflow_shell.py',
               'check_ostim_support_message.py') { python "Code/plugins/tools/$t" }

# 5. bridge 的 CMakeLists 会 READ 该文件再 string(REPLACE),所以锚点必须原样还在:
#    七个 [=[...]=] 字面量在改动前后逐个比对,无一条被破坏
```

**改动只落在五个文件上,而它们此前都没有被任何补丁碰过**(这是 108.6 那条判据的结果):

| 文件 | 改了什么 |
| --- | --- |
| `src/STRPMFrameworkRuntime.h` | 新增,唯一的判定函数 |
| `src/STRPMChatUiSuppress.h` | `PatchByte` 拦下装断点(全仓 INT3 的唯一出口) |
| `src/STRPMProxyResolverBootstrapV2.h` | `Worker` 每轮重扫前停手 |
| `src/STRPMProxyResolverTrace.h` | `Worker` 装 VEH 前、`ArmStepOver`/`RearmEntryHookIfNeeded` 下 TF 前停手 |
| `src/STRPluginMessagingBridge.cpp` | `BootstrapLoop` 每轮重扫前停手、`PatchByte` 拦下装断点 |

**未改而经确认不可达/已被覆盖的写点**:`STRPMProxyResolverBridge.h` 的
`ArmHook`/`ConfigureHooks` 只由已被守卫的 `BootstrapV2::Worker` 调用;
它自己那个 `Start()`(`:1212`)全仓**零调用者**,是死代码;`STRPMChatUiSuppressV2.h`
全仓**无人 include**;`STRPMChatUiSuppressV3.h` 的三处写全部经
`STRPMChatUiSuppress::detail::PatchByte`,即上表第二行。

> **教训**:**「被加载」和「被选中」是两件事,而启动期的副作用只该挂在后者上。**
> 本次三个 DLL 里,两个是「被选中才做事」,第三个把**扫描内存 + 装 VEH + 置 TF +
> 写 INT3** 挂在了加载上。用户勾选一个可选插件,买到的是这三个 DLL 全进进程 ——
> 这就是「勾任何一个都闪退、一个都不勾就正常」的形状。
> 判据要问的不是「这段代码对不对」,是**「它什么时候跑,而那时候谁需要它」**。
> 第二条:**门禁的覆盖面要按「什么条件触发」补,不是按「什么名字存在」补。**
> 十个 gate 全绿而缺陷在线,因为它们问的都是静态存在性;这一条问的是触发条件。
> 第三条:**判据要落在「动手的那一刻」,不是「加载的那一刻」。**
> 加载顺序不由我们选(门面与 bootstrap 是两个同目录 DLL),所以任何
> 「加载时查一次、之后一直信任」的写法都有洞。四处判定各查各的时机,
> 其中一处(`PatchByte`)是唯一写口,判在那里等于一次盖住所有写路径。

---

## 109. 2026-10-10 场次:插件层同一类缺陷的四个新面孔,以及帧循环上一处每 tick 的整表复制

> **锚点**:`Code/plugins/patches/OStimNG/0007-keep-the-thread-api-off-null-pointers-and-off-its-own-lock.patch`、
> `Code/client/Services/DiscoveryService.{h,cpp}`。

### 109.1 判据:同族先例就在同一个文件里,而这三处没跟着写

上一节(§108)的判据是「被加载 ≠ 被选中」。这一节是另一条:**同一个抽象层的兄弟函数
已经判过的空,新加的入口要判同样的空** —— 而这里的「兄弟」就在**同一个仓库的同一个
目录**里,不是别的项目。

**OStimNG 的 ModAPI 是给外部插件用的 C++ 入口**(`skse/src/ModAPI/`)。它读写三个
**各自独立、各自可为空**的裸指针:

| 指针 | 谁把它置空 | 证据 |
| --- | --- | --- |
| `UIState::currentThread` | 线程结束时 `HandleThreadRemoved` | `UI/UIState.cpp:106-108` |
| `UIState::currentNode` | 与 thread **不同步**,可单独非空 | `UI/UIState.h:49-50` |
| `Thread::furnitureType` | 默认 `nullptr`,无家具的线程一直是空 | `Core/Thread.h:38`,`:220` |

**仓库内自带的 UI 三个入口全都判了,而且判的个数不一样**:

```cpp
UI/Scene/SceneMenu.cpp:130     if (currentNode == nullptr || currentThread == nullptr) return;
UI/Search/SearchMenu.cpp:153   if (thread == nullptr) { AssignData(data); return; }
UI/Search/SearchMenu.cpp:160   && furnitureType != nullptr && furnitureType->isChildOf(...)
UI/Align/AlignMenu.cpp:87      if (currentThread == nullptr || currentNode == nullptr) return;
```

而 ModAPI 的对应入口**只判了其中一个**。逐处列出(改前):

| 站点 | 判了 | 漏了 | 后果 |
| --- | --- | --- | --- |
| `GetNavigationCount` `:134` | `currentNode` | `furnitureType` | `getFurnitureTypeInternal()->isChildOf(...)` 空解引用 |
| `GetNavigationOptions` `:169` | `currentNode` | `furnitureType`、`nav.nodes` 为空 | 同上 + `nodes.back()` 越界 |
| `SearchScenes` `:447` | **一个都没判** | `currentThread`、`furnitureType` | `thread->getActorCount()` 空解引用 |
| `PapyrusThread::GetFurnitureType` `PapyrusThread.h:205` | `thread` | `furnitureType` | 脚本可随时调用 |

`SearchScenes` 那处最直接:`UI/Search/SearchMenu.cpp:147` 的树内同名功能**判了**
(`:153` 判 thread、`:160` 判 furnitureType),而 ModAPI 那份**两个都没判** ——
搜索框在场景结束后仍然开着,`HandleThreadRemoved` 把 `currentThread` 置空,
下一次敲键盘就是一次空读。

**修法**:加一个 `ResolveSceneContext()` 把三个指针**一起**取、一起判
(`operator bool` 要求 thread 与 node 同时非空),四个入口改用它;
`furnitureType` 单独再判一次(它合法地可以为空,不能当成"上下文无效");
`nav.nodes.empty()` 单独跳过一个导航项。返回空而不是崩溃:导航列表为空、
搜索结果为空,都是这两个查询本来就能表达的答案。

### 109.2 回调在自己的锁里被调用 —— 一个「注销自己」就能自锁

`NotifyEvent` / `NotifyControlInput` 原本**持着 `callbackMutex` 遍历并调用**:

```cpp
std::lock_guard<std::mutex> lock(callbackMutex);
for (auto& registration : eventCallbacks)
    registration.callback(eventType, threadID, registration.userData);   // ← 在锁里
```

而同一层的 `UnregisterEventCallback` `:258` 与 `UnregisterControlCallback` `:279`
**也要拿同一把锁**。于是一个在事件回调里注销自己的外部插件 —— 这是最自然的写法,
「我收到结束了,不用再通知我」—— 就是**同线程重入 `std::mutex`**,`std::mutex`
不可重入 ⇒ **死锁**。

**修法**:锁内**只拷贝**一份回调列表,锁外调用。拷贝是这两处唯一的分配,
所以两个函数补 `noexcept` + `try/catch`,与 §92-§101 同一口径(报告自己也不能抛)。

### 109.3 同步形态的 `MigrateThread` 在主线程上等自己

`MigrateThread` 的契约(`ModAPI/OstimNG-API-Thread.h:271`)写着「Without callback:
blocks until migration completes; returns new thread ID」。实现用 promise/future 等
`onComplete`。而迁移的完成路径是:

```cpp
ThreadManager.cpp:393   GameAPI::Game::runSynced([...]{ closingThread->close(); delete closingThread; ... });
GameAPI/Game.h:20       runSynced(func) { SKSE::GetTaskInterface()->AddTask([func]{func();}); }
```

`AddTask` 的任务队列**由游戏主线程抽干**。所以**在主线程上**调同步形态:
`future.get()` 等一个只有主线程才能跑的 lambda,而主线程正卡在 `future.get()` ——
**永久死锁**,表现就是游戏卡死。

**修法**:加 `Util/MainThread.h`,在 `SKSEPlugin_Load`(`Main.cpp:96`,SKSE 保证主线程)
记录线程 id;同步分支在阻塞前判一次,是主线程就**直接返回 -1 并记 error**,把调用者
导向回调形态。**没有**顺手把迁移启动起来 —— 契约是「返回新 id」,返回不了就没有
通道把结果交给调用者,而在它以为失败时偷偷把场景迁走比失败更糟。

### 109.4 帧循环:`VisitForms` 每 tick 整表复制一遍「已发现的 actor」

`DiscoveryService::VisitForms` 挂在 `PreUpdateEvent` 上(`:53`),即**每个 tick**
(`World::Update`,`World.cpp:96`)。原实现第一行是:

```cpp
static Set<uint32_t> s_previousForms;
s_previousForms = m_forms;        // ← 每 tick 一次整表赋值
```

两处缺陷叠在一起:

1. **`static` 局部变量活过 `World`**。`World` 是 `entt::locator` 里的单例
   (`World.cpp:131`),第二个会话/第二次 `World::Create` 时 `m_forms` 是空的,而
   `s_previousForms` 还留着上一场的 actor 集合 —— 于是那一整批会被当成
   「这一 tick 消失了的 actor」,逐个走 `ActorRemovedEvent`。
2. **每 tick 一次与 actor 数成正比的分配**。`Set` 是
   `TiltedPhoques::Set`(分配器感知容器,`TiltedOnlinePCH.h:75`),
   `operator=` 重建整个容器。帧循环按 §21.1 是 **8ms 定时器**(~64-125/s),
   于是这条赋值每秒跑几十到上百次,而它跑在**游戏主线程**上。

**修法**:改成成员 `m_previousForms`(`clear()` + 逐条 `insert`),容量跨 tick 保留 ——
首次几 tick 之后这条路径不再分配;`static` 消失,跨会话残留随之消失。

> **判据**:「这个变量该是成员还是 static」不是风格问题 —— **static 的寿命比它填自的
> 对象长**,而这里被填的是每个会话重建的 `World`。同一个判据在 §53(插件缓存跨存档
> 活着)已经用过一次。

### 109.5 验证与未做

- 三个补丁文件都由 `git diff` **原始重定向**生成,再用
  `plugin_patches.py verify` 对**索引里的钉住内容**逐个 `git apply --check --cached`
  通过(§107 的教训:手写的 hunk 上下文会与 `git diff` 的不同而 apply 失败);
  `check` 确认全部贴在树上。
- `bridge` 的 `CMakeLists.txt` 会 `file(READ)` 再 `string(REPLACE)` 源文件,
  所以改完逐个比对了七个 `[=[...]=]` 字面量锚点,无一条被破坏(比对脚本读
  `git show :<file>` 的原始内容作为基线,避免把既有差异算成自己弄坏的)。
- **本机不编译**(§0/§1),以上判据都是静态的;1.6.x 实机复现需要 CI 出包后验证。

> **教训**:第四条:**审计要按「同一抽象层的兄弟函数判了什么」横向扫,不是只纵向读缺陷。**
> 本节四处有三处的正解**就在同一个仓库的相邻文件里**(`SceneMenu.cpp` 判了两个、
> `SearchMenu.cpp` 判了两个、`ThreadActor.cpp:538` 判了 `furnitureType`)。
> 新入口漏判,不是因为不知道要判,是因为**加的时候只看了自己**。

---

## 110. 2026-10-11 场次:同步上游 v1.8.3 —— 一处「回显当权威」的 NPC 复制,以及三处「照抄等于空操作」

> **锚点**:`Code/client/Services/Generic/CharacterService.cpp`、
> `Code/client/Services/Generic/QuestService.cpp`、
> `Code/server_runner/DediRunner.cpp`、`Code/server/main.cpp`、
> `Code/client/Services/Generic/DiscoveryService.cpp`。
> 门禁:`Tools/Scripts/check_docs.py`、`Tools/Scripts/check_facegen_scope.py`。

### 110.1 判据:同步上游要按**提交**判定,不能按**版本**拉取

本仓库是上游的魔改(多游戏版本、1.5.x 翻译表、插件层与 UI 中文化),同一批文件
两边的写法已经不同。所以「同步上游」在这里的正确形状是:

1. 列出 `v1.8.2..<上游新 tag>` 的**全部提交**;
2. 每个提交判成 **已同步 / 需改写 / 不适用** 三种之一,**并写出理由**;
3. 「需改写」的按本仓库的写法落地,不照抄结构。

`v1.8.3` 是 12 个提交(`4917189f` 是合并提交)。其中 8 个直接落地、1 个早已修过、
2 个不适用、**1 个照抄等于空操作**(见 §110.4)。判定的产物写进 `CHANGELOG.md`,
不写进 README(§0 的文档分工:README 只回答「装哪个、什么能用」)。

### 110.2 「回显当权威」——#906 的根因不是重复,是**过期**

上游 #906 的说明是「owner 应用了过期的分配状态」。**过期**这两个字是关键,
把它当成「重复发现」就会修错地方。

自 `1f488f6f`(本仓库同样包含)起,owner 收到 `AssignCharacterResponse` 时会用
服务器回传的 `actorData` 对齐新分配到的 actor。但那份数据**只是自己请求快照的回显** ——
服务器没有额外信息,它只是把你发上去的东西发回来。往返期间本地发生的变化,
回显里**没有**。

受害链条(上游点名 WE23 的「拾荒者」士兵,它们在读盘时就被 `WEAliasScript` 杀掉):

```text
本地: actor 已死
  -> 回显: IsDead = false (请求发出时的快照)
  -> ReconcileActorData: IsDead() != false -> Respawn()  (= Resurrect + Reset)
  -> 引用被重置、3D 被重载、等级化临时 actor 被重掷
  -> owner 交还实体
  -> 该 actor 被当成【新】实体重新发现
  -> 每轮往返重复一次
  -> 每个交还的实体又被交给另一个玩家留一份新副本
  -> 几百个 NPC 后崩游戏
```

**修法分两处,方向不同**:

| 位置 | 修法 | 为什么 |
| --- | --- | --- |
| `ReconcileActorData` | `Respawn()` 只在**不是本地 owner** 时执行 | 非 owner 没有本地权威,必须跟随服务器 |
| `OnAssignCharacter` owner 分支 | **不再**用回显覆盖本地状态 | owner 才是权威,回显过期就该被丢掉 |

只改一处都不够:只改前者,owner 分支仍会把 `IsDead` 写进 `LocalComponent`,
下一次差分又会把它当成「本地变化」上报;只改后者,非 owner 的路径仍然会
因为别的回显字段触发 `Respawn()`。

**本仓库的额外差异**:owner 分支里原本有一对 `SetRemote(true/false)` 夹着
`ReconcileActorData`(fork-only,`f0877c1b` 加的空指针防护改动附近)。
它存在的唯一理由就是那次对齐,所以随该调用**一并去掉** —— 保留它会让
`SetRemote(true)` 之后的 `emplace_or_replace<LocalComponent>` 处于
「自认为远端、实际是本地」的中间态。

### 110.3 删一个死文件会让**覆盖率数字**变化 —— 门禁当场抓到

`3941ddd0`(#905)删掉 `Code/client/Games/Skyrim/AnimationExperiments.cpp`(429 行)。
删之前确认过:8 个符号(`RePerformAction`/`RePerformIdleAction`/`RePerformComplexAction`/
`ReSendAnimationEvent`/`ReHandleEvent`/`ComputeResult`/`g_animErrorCode`/`0x13482`)
除该文件外**零引用**,而且它是 `hkEventType::hkEventType` 与
`hkEventContext::hkEventContext` 两个构造函数的**唯一**定义处 —— 所以删文件必须
同时把这两处内联回去(上游把 `hkEventType` 的构造改成成员初始化列表)。

**但真正被门禁抓到的不是这个**,而是:

```text
python Tools/Scripts/check_docs.py
  - Tools/missing_1_5_97_ids.txt: header says coverage 3069/3082, the generator says 3050/3063
  - README.md:116: quotes 3069/3082 as if current ...
```

那个文件里有 **19 个地址库 id 引用**(它自己的 `POINTER_SKYRIMSE` 调用),
删掉之后 `gen_ae_to_se_map.py::collect_codebase_ids()` 扫到的总数从
**3082 掉到 3063**、已解析从 **3069 掉到 3050**。
**缺失的 id 集合没变,仍然是那 13 个** —— 该文件贡献的全是**已映射**的 id。

这正是 §43.8 / §1.1.4 那条口径修正门禁的价值:**覆盖率是个派生数字,
删代码会让它变,而变的方向不一定是变坏**。四个文档(`README.md`/`README_EN.md`/
`Tools/ida/README.md`/`docs/LAN-RADMIN-GUIDE.md`)都要跟着改,
`Tools/missing_1_5_97_ids.txt` 的表头也要改,并且要在清单里**写明这次为什么变**
(否则下一个人会以为是覆盖率退步)。

### 110.4 照抄等于空操作 —— `a89d8185` 与本仓库的分支名

上游 `a89d8185` 把「你的安装不是纯原版」这条警告包进 `#if (IS_MASTER)`。
上游的 `IS_MASTER` 定义是:

```lua
bool_to_number[branch == "master"],     -- 上游 v1.8.3
```

而本仓库的 `xmake.lua` 是:

```lua
bool_to_number[branch == "master" or branch == "main"],   -- 本仓库
```

**本仓库的分支就叫 `main`**,所以 `IS_MASTER` 恒为 1 —— 照抄上游这个改动,
警告**照样每次都弹**,改动等于没做。这类「两边同名宏的取值域不同」是同步上游时
最容易漏的一类空操作:**改动的文本对得上,语义对不上**。

更根本的是**定位冲突**:那条警告的文案是「我们**不**建议使用 mod」,
而本仓库的 README 第 18 行写的是「提供更多游戏版本和 Mod(**Mod 整合包**)的兼容」。
判定依赖的默认列表里有 `_ResourcePack.esl`(AE 免费 CC 内容,**1.5.97 上根本不存在**),
所以它也不适合当阻断条件。

**本仓库的处置**:保留判定、去掉弹窗,改成一条 `info` 日志。
判定仍有价值(有人报不稳定时,日志里能看到他跑的不是默认列表),
但不再把整合包玩家拦下来。

### 110.5 上游的「新字段」在本仓库无法验证时,用本仓库已有的判据

`a3f62ba7`(#909 组队提示)给 `TESQuestStartStopEvent` 加了两个字段:

```cpp
struct TESQuestStartStopEvent
{
    uint32_t formId;
    bool started;   // <- 上游新增
    bool failed;    // <- 上游新增
};
```

这两个字段来自**引擎结构体布局**,本仓库同时支持 1.5.97 与 AE,
而本机只有 1.5.97 的 `SkyrimSE.exe` 可反汇编(§38 的取证范围),
**无法在两边都验证**这个布局。照抄的后果是:如果 AE 上布局不同,
读 `apEvent->started` 会读到别的字节,提示就会在不该弹的时候弹。

**本仓库的替代判据是已有的 `pQuest->IsStopped()`** —— 它已经在同一个函数里
用于构造 `RequestQuestUpdate::Stopped/Started`,是**活路径上已验证**的判据:

```cpp
// 只有「开始」才提示;stop 不是进度
if (!pQuest->IsStopped())
    NotifyOverlayOfQuestUpdate(pQuest);
```

> **教训**:同步上游遇到「上游给引擎结构体加了字段」时,先问**本仓库有没有
> 等价且已验证的判据**。有就用自己的 —— 结构体字段的**可验证性**取决于
> 你手里有几个版本的可执行文件,不是取决于上游写了没有。

### 110.6 验证与未做

- `python Tools/Scripts/check_facegen_scope.py` → `FACEGEN SCOPE CHECK OK`
  (脸部开关那条门禁未受影响)。
- `python Tools/Scripts/check_docs.py` → `DOC CHECK OK` /
  `coverage 3050/3063 stated consistently in 4 doc(s)`。
- 14 个语言文件逐个解析:新增 4 个键(`COMPONENT.NOTIFICATIONS.OPEN_GUIDE` /
  `DISMISS` / `DONT_SHOW`、`SERVICE.PARTY_HINT.MESSAGE`)**一个不缺**,
  且每个键的 `{{...}}` 占位符与英文**逐键一致**(占位符错位是 i18n 最常见的静默缺陷)。
  上游只改了 de/en/ru 三个文件,其余语言会退回英文,本仓库补齐了全部 14 个。
- 所有改动的 `.cpp/.h` 逐个统计花括号收支,均为 0。
- **本机不编译**(§0/§1):无 `xmake`/`cl`/`vcpkg` 工具链。以上全部是静态判据,
  实机验证需要 CI 出包后进行 —— 尤其是 §110.2 的复制修复,
  判据是「WE23 拾荒者不再每轮往返复制一份」,只能实机观察。
- **未做**:`--generate-config` 的入口重构改了跨 DLL 的导出签名
  (`CreateGameServer` 去掉回调参数、新增 `RegisterServerSettings`),
  **两个二进制必须同一次构建产出** —— 否则旧 runner 会去 `GetProcAddress`
  一个不存在的签名。本仓库的版本握手(`CheckBuildTag`)会在启动时拦下这种混用,
  但这一条只在 CI 出包后才能真正验证。

> **教训**:同步上游的第一件事是**列出提交并逐个判定**,不是拉一个合并。
> 本节 12 个提交里,有 1 个照抄等于空操作(分支名导致宏取值不同)、
> 1 个定位与本仓库冲突(警告文案 vs 整合包支持)、
> 1 个依赖无法验证的引擎结构体布局(改用本仓库已有判据)、
> 1 个根本不适用(CI 上传的不是 `.zip` 路径)。
> **「上游改了」和「本仓库该改」是两件事。**

---

## 111. 2026-10-11 场次:全项目审计 —— 用「引擎自己的实现」当契约,抓出 15 处单例解引用

> **锚点**:`Code/client/Games/INISettingCollection.cpp`、
> `Code/client/Games/Skyrim/Actor.cpp`、`Code/client/Games/Skyrim/TESObjectREFR.cpp`、
> `Code/client/Games/Skyrim/PlayerCharacter.cpp`、
> `Code/client/Services/Generic/{DiscoveryService,ObjectService,QuestService,MagicService,CharacterService}.cpp`、
> `Code/client/Systems/AnimationSystem.cpp`、`Code/client/Services/Debug/**`。
> 门禁:`Tools/Scripts/check_null_accessors.py`。

### 111.1 判据:审计要有**可判定**的线,不能靠读代码找味道

「审计整个项目」如果不先定下**什么算缺陷**,就会变成通读一遍然后凭印象列几条。
本节用的三条线都能给出**是/否**的答案,而且每条都落在仓库已有的数据上:

| 线 | 输入 | 输出 | 结论 |
| --- | --- | --- | --- |
| 地址库覆盖 | 3063 个 id × 24 个 `.bin` | 每个运行时缺哪些 id | 1.5.x 缺 13、AE 缺 4,**全部已记录** |
| patch 偏移 | 10 个带 1.5.97 偏移的站点 × 真机 exe | 每个站点的字节 | **全部通过** |
| 引擎实现当契约 | 反汇编引擎自己的同名函数 | 与本仓库实现的差异 | **抓到真缺陷** |

第三条是关键:**仓库里凡是「重写了一遍引擎某个函数」的地方,引擎那份就是现成的契约**。
不用猜它该不该判空、该返回什么 —— 反汇编读一遍就有答案。

### 111.2 `INISettingCollection::GetSetting` 抄漏了引擎的空表保护

引擎自己的实现(`SkyrimSE.exe+0x3104d0`,AE id `411155` 指向的那个单例):

```asm
1403104f4  cmpq $0x0,0x8(%rax)   ; head.next == 0 ?
140310ff  cmpq $0x0,(%rax)       ; head.setting == 0 ?
140310503  je   140310521        ; 两个都是 0 -> 直接 return NULL
...
140310521  xor  %eax,%eax        ; return NULL
```

本仓库的版本:

```cpp
while (pCurrent)
{
    if (_stricmp(acpName, pCurrent->setting->name) == 0)   // setting 为空 -> 崩
```

**同一趟反汇编顺带确认了结构体偏移是对的**:`lea 0x118(%rcx)` 取链表头、
`0x8(%rax)` 取 `next`、`(%rax)` 取 `setting`、`0x10(%rdi)` 取 `setting->name`,
四条与 `Games/TES.h` 里 `INISettingCollection` / `Setting` 的声明**逐条对上**。

> **这正是「抄漏」而不是「抄错」**:偏移全对,漏的是**一个分支**。
> 只核对偏移的审计查不出它 —— 必须读控制流。

### 111.3 单例访问器自己会返回空,而调用点直接解引用

```cpp
INISettingCollection* INISettingCollection::Get() noexcept
{
    POINTER_SKYRIMSE(INISettingCollection*, settingCollection, 411155);
    return *settingCollection.Get();     // 改前
}
```

两个**互相独立**的空来源:

1. **该 id 在旧版运行时上没有映射**时,`VersionDbPtr` 返回的是**共享可执行桩**,
   桩体是 `48 31 C0 0F 57 C0 C3`(`xor rax,rax` / `xorps xmm0,xmm0` / `ret`)。
   调用它得到 `rax = 0`,于是 `*stub` 就是**读地址 0** —— 不是「读到垃圾」,是**崩**。
   同一个坑在 `GarbageCollector::Get()` 与 `bhkCharacterController::PhysicsDeltaTime()`
   已经被踩过并各自改成 `FindAddressById` + 判空(见 §38 与 `missing_1_5_97_ids.txt`)。
2. **引擎还没填那个单例指针** —— 它要等 ini 设置建好,所以启动早期本来就是空。

改后返回空,三个调用点各自判空。

### 111.4 判据是**树内不一致**,不是风格偏好

「要不要给每个 `X::Get()` 判空」不是可以争论的风格问题。本仓库**自己已经**给出了答案:

```text
PlayerCharacter::Get()   56 处调用,56 处判空   -> 这个仓库把它当可空
ModManager::Get()        13 处调用,12 处判空   -> 第 13 处是缺陷
EquipManager::Get()       4 处判空 / 7 处不判
```

也就是说:**同一个访问器,同一棵树里,绝大多数调用点判了空,漏判的那几处就是缺陷** ——
因为「可空」这个判断已经被这些调用点自己做出了。这条判据不需要理解引擎启动顺序,
只需要数一遍。共 15 处,逐处补齐(清单见 `CHANGELOG.md`)。

### 111.5 三个游戏版本共用同一份调用点代码

这是本节与 §38/§110 的连接点:1.5.x、1.6.x、1.7.x 走**三条不同的地址库路径**
(format 1 + ae-to-se 映射表 / format 2 / format 5),但**共用同一份调用点代码**。
单例有没有建好取决于**引擎启动顺序**,与游戏版本无关 —— 所以:

- 这三类缺陷在**三个版本上都成立**;
- 而**旧版运行时更严重**:那里未映射的 id 返回可执行桩,`*stub` 读地址 0;
  在 AE 上同一个 id 若没映射则返回**空指针**,解引用同样是崩,但**原因不同**。

### 111.6 门禁与变异测试

`Tools/Scripts/check_null_accessors.py`:

- 读「已知可为空的访问器」名单(每条都带**理由**,理由必须来自引擎或本仓库);
- 拒绝任何**在同一语句里解引用且没有判空**的调用点;
- **反向查**:豁免条目一旦不再需要就报错 —— 免得名单烂成一张万能通行证;
- 另有一张 `NOT_NULLABLE` 表:`BehaviorVar::Get()` 是**懒分配**(`if (!single) single = new ...`),
  **不会**返回空,所以它的调用点不需要判空。这张表也查:一旦它的函数体长出
  `return nullptr`,门禁报错要求把它移到可空名单里。

**变异测试**(§107 的教训:门禁要能证明自己会失败):在隔离副本里把一处守卫改回
未判空形式 → 门禁报错并指出**文件:行号**;改回守卫 → 通过。

### 111.7 验证与未做

- 三条静态线全部跑完:24 个运行时的 id 覆盖、10 个 1.5.97 patch 站点的真机字节、
  引擎 `GetSetting` 的完整反汇编。
- 12 条门禁全绿:`check_docs.py`、`check_facegen_scope.py`、`check_null_accessors.py`,
  以及 `Code/plugins/tools/` 下 9 条(含 `check_ostimng_versions.py` 的
  33 个 `RELOCATION_ID` 覆盖检查 —— 它证明的是**插件层**在每个运行时上的 id 覆盖)。
- 所有改动的 `.cpp/.h` 花括号收支为 0。
- **本机不编译**(§0/§1),以上是静态判据 + 真机反汇编核对;实机验证需要 CI 出包。
- **未做**:`ExtraDataList.h` 在 `SKYRIM_TARGET_LEGACY` 下分支却没有自己的
  `static_assert`,它的尺寸只被 `TESObjectREFR` 的 `sizeof` 断言**间接**覆盖。
  目前够用(改动它必然破坏那个断言),但不如直接断言直白。

> **教训**:「重写了一遍引擎某个函数」的地方,**引擎那份就是契约**。
> 本节 §111.2 的缺陷偏移全对、只漏了一个分支 —— 只核对偏移的审计永远查不出它。
> 反过来,§111.4 那 15 处不需要理解引擎:**数一遍同一访问器的调用点就够了**,
> 因为「可空」这个判断已经被大多数调用点自己做出了。

---

## 112. 2026-10-11 场次:发布版**不写崩溃转储**,而文档让玩家交它 —— 判据是「宏的名字不是它的语义」

> **锚点**:`Code/client/CrashHandler.cpp`、`xmake.lua`、
> `.github/workflows/plugins.yml`。
> 门禁:`Tools/Scripts/check_crash_diagnostics.py`。

### 112.1 判据:宏的名字不是它的语义,要按**展开结果**判

`IS_MASTER` 由 xmake 的 `before_build` 从 **git 分支名**推导:

```lua
-- xmake.lua
bool_to_number[branch == "master" or branch == "main"],   -- 本仓库
```

上游只写 `branch == "master"`,因为上游只有一条发布分支。本仓库把 `main`
也并进来,而 `main` 是**唯一的发布分支** —— 于是 `IS_MASTER` 在**每个出货构建里都是 1**。

**这不是推断,是实测**(§111 的方法:结论要落在能读的数据上):

| 证据 | 结果 |
| --- | --- |
| 随包 DLL 里嵌的构建标记 | `Build: v1.1.8 main EVO` → 分支 = `main` |
| `#if (!IS_MASTER)` 里的字符串在同一个 DLL 里 | `Components` **不存在**、`Show selected entity in world` **不存在**、`Crash client` **不存在** |

也就是说:**「宏叫什么」和「宏展开成什么」是两件事**,而只有后者能拿来推理。

### 112.2 缺陷:转储被这个宏关掉了,而文档让玩家交它

```cpp
#if (IS_MASTER)
    volatile static bool bMiniDump = false;   // 出货构建 -> 不写转储
#else
    volatile static bool bMiniDump = true;    // 开发构建 -> 才写
#endif
```

上游写这段时,「`IS_MASTER` = 这是发布版」成立,所以「发布版不写转储」是个
**有意的选择**(发布版不想在玩家机器上留大文件)。本仓库把这个前提改掉了
(`main` 变成发布分支),于是同一个 `#if` 的含义变成
**「玩家跑的那个构建永远不写转储」**。

而 `docs/PITFALLS.md` 第 7 节写着:

> 正确诊断文件是游戏根目录 `logs/tp_client.log`、`crash_*.dmp`、`st_client_error.log`、`st_boot.log`、`cef_debug.log`

**两边各自都自洽,所以谁也没发现**:文档承诺一件代码做不到的事,代码做一件文档没写的事。
这正是 §110 记过的那类「文档与代码分歧」,只是这次分歧藏在**预处理宏的语义**里。

> **教训**:宏的**名字**不是它的**语义**。改一个宏的定义域(这里:把 `main`
> 加进 `IS_MASTER`)会**静默改变**每一处 `#if` 的含义 —— 包括那些离改动点很远、
> 看起来毫无关系的 `#if`。本节的缺陷离 `xmake.lua` 有几千行。

### 112.3 顺带:清理函数的判据宽到会删玩家的文件

```cpp
if (entry.path().string().find("crash") != std::string::npos)   // 改前
    DeleteFileA(entry.path().string().c_str());
```

它在**整条路径**上做子串匹配,所以 `CrashLogger.dll`、名为 `Crash Fixes` 的目录、
以及任何路径里带 "crash" 的文件**都会被删掉**。一个崩溃处理器删玩家文件,
比留一个旧转储糟糕得多。改成只匹配自己写的那种名字
(`crash_<UTC 时间戳>.dmp`),删除失败也不再影响流程。

### 112.4 门禁,以及「门禁必须真的会跑」

`Tools/Scripts/check_crash_diagnostics.py` 同时读**三处**并交叉核对:

| 检查 | 为什么 |
| --- | --- |
| 转储没有被任何构建宏包住 | 被包住 = 某些构建不写,而文档没说 |
| `bMiniDump` 初值不是假 | 假 = 这个构建不写转储 |
| 清理判据不是子串匹配 | 子串匹配 = 会删无关文件 |
| **文档提到的产物代码里存在** | 文档与代码必须一致 —— 这条才是抓到本缺陷的那条 |
| `xmake.lua` 仍把 `main` 算作 `IS_MASTER` | 上面那条注释的前提;前提变了注释就是错的 |

**变异测试**(§107 的教训):把原版 `#if` 与子串清理还原回去 → 门禁报 **5 条**,
其中一条正是「`docs/PITFALLS.md` 让读者交 `crash_*.dmp`,而 `CrashHandler.cpp` 从不写」;
改回修好的版本 → 通过。

### 112.5 另一件事:上一轮的新门禁**没有被 CI 调用**

`check_null_accessors.py` 上一轮加好了、本地跑绿了,但 `.github/workflows/plugins.yml`
里**没有它的运行步骤**,也没进 `paths` 触发列表 —— 也就是说**它永远不会跑**。
一个不跑的门禁等于没有门禁,而且比没有更糟:它会让人以为这一类缺陷已经被守住了。

现在两个新门禁(`check_null_accessors.py`、`check_crash_diagnostics.py`)都接进了
`push` 与 `pull_request` 两条 `paths` 列表,并各自加了运行步骤;
`check_workflow_shell.py`(39 个 PowerShell 块)与 YAML 解析都通过。

> **教训**:加门禁时,**「它在 CI 里跑吗」和「它会失败吗」是两个必须各自证明的问题**。
> 本节 §112.4 证明的是第二个,§112.5 证明的是第一个 —— 上一轮只证明了第二个。

### 112.6 验证与未做

- 13 条门禁全绿:4 条 `Tools/Scripts/`(`check_docs`、`check_facegen_scope`、
  `check_null_accessors`、`check_crash_diagnostics`)+ 9 条 `Code/plugins/tools/`。
- 两个新门禁都做了变异测试,都能失败。
- `check_workflow_shell.py` 通过(39 个 PowerShell 块),`plugins.yml` YAML 可解析。
- **本机不编译**(§0/§1),所以「转储确实能写出来」这一步只能等 CI 出包后
  制造一次崩溃来验证;本节证明的是**它不再被宏关掉**,以及**文档与代码一致**。
- **未做**:`DebugService.cpp` 里还有 5 处 `#if (!IS_MASTER)`,剥掉的是调试菜单内容
  (Components/Network/Forms/Inventory/Animations/Player 窗口、崩溃客户端按钮、
  窗口排布)。这些**是有意剥掉的**,不是缺陷 —— 但 `CrashHandler.cpp` 那个 `#if`
  在**同一份代码里长得一模一样**,这就是它藏得住的原因。已确认没有第三处
  `#if (!IS_MASTER)` 在关掉诊断类产物:`CharacterService.cpp` 的三处只关掉
  `ReplayedActionsDebugComponent`(调试视图用,读取侧 `try_get` 本来就是空的),
  `Launcher.cpp` 那处只关掉调试控制台。

> **教训**:同一份代码里**长得一样**的两个 `#if`,可以一个有意、一个是缺陷。
> 区分它们不能靠读 `#if` 本身,要靠问「它关掉的是**诊断**还是**功能**」——
> 关掉功能的可能是有意的,关掉诊断的**不是**:诊断没有「生产环境不需要」这一说,
> 因为需要诊断的恰恰是生产环境。

---

## 113. 2026-10-11 场次:门禁「绿是因为它没在看」—— 一个插件的 id 覆盖从来没被检查过

> **锚点**:`Code/plugins/tools/check_ostimng_versions.py`。
> 同 §112.5 的主题(门禁自身的可靠性),但这次的问题不是「不跑」,是「跑了但没看全」。

### 113.1 判据:把「谁在用地址库」和「门禁扫了谁」分别数一遍,再对照

这一类缺陷**读代码读不出来**,因为门禁本身写得没问题 —— 它的扫描根写死成一个目录:

```python
NG_SRC = ROOT / 'plugins' / 'OStimNG' / 'skse'   # 改前:只有一个插件
```

判据是**两个独立计数的对照**:

| 计数 | 怎么得到 | 结果 |
| --- | --- | --- |
| 谁在用地址库 | 每个插件目录下数 `RELOCATION_ID` / `REL::ID` | OStimNG **33**,**OStimTogether 8** |
| 门禁扫了谁 | 读门禁的扫描根 | **只有 OStimNG** |

差的这 8 个站点(4 组不同的 id 对)就是**从未被任何门禁检查过**的部分。
`OStimTogether` 里 `55712/56243` 一组就出现在 4 处
(`FreeSceneAlignmentFix.cpp:98`、`FreeScenePhaseSync.cpp:49`、
`FreeScenePhaseSyncCompile.cpp:21`、`OStimBridge.cpp:366`)。

### 113.2 为什么这算缺陷,而不是「覆盖不全」

因为**失败模式不是软的**。`REL::ID()` 在 id 缺失时走 `REL/IDDB.cpp` 的
`report_id_lookup_failure`,**直接中止游戏**。所以:

- 「本仓库支持 1.5.x–1.7.x」这句话,对 OStimTogether 而言**从来没有被校验过**;
- 而门禁的绿灯会让人**以为**它已经被守住了 —— 这比没有门禁更糟。

**先核对现状,再动手**(§111 的方法):把那 4 组 id 对全部 24 个地址库跑一遍:

| SE | AE | 站点 |
| --- | --- | --- |
| 55693 | 56224 | `ActorResolverSTRPM.cpp:61`、`OStimBridge.cpp:340` |
| 53950 | 54774 | `EquipmentLock.cpp:60` |
| 55712 | 56243 | 4 处 |
| 55706 | 56237 | `OStimBridge.cpp:421` |

**全部完整** —— 也就是说当前**没有活缺陷**,缺的是**守护**。
这两件事必须分开说:本节修的是「下次改坏没人拦」,不是「现在就坏着」。

### 113.3 修法:多插件清单 + 一条防「静默退化」的反向检查

扫描根改成一张**显式清单**:

```python
SCANNED = {
    'OStimNG/skse': 'OStimNG (prerequisite)',
    'OStimTogether/src': 'OStimTogether',
}
```

同时加一条**反向检查**,这是本节最值得留下的部分:

> 清单里的插件若**被检出却贡献了 0 个站点**,本身就是失败。

理由是两种情况的**扫描结果完全一样**:子模块没检出,和子模块里真的没有站点。
前者是正常的(CI 没 init 子模块),后者意味着**这个检查正在静默地什么都不查**。
不区分它们,门禁就会在某次子模块重组之后安静地退化成空转 —— 而它**仍然打印 OK**。

### 113.4 变异测试,以及「变异必须真的生效」

在隔离副本里把 `OStimTogether/src/OStimBridge.cpp` 的
`RELOCATION_ID(55706, 56237)` 改成不存在的 `999999`:

```text
PLUGIN VERSION CHECK FAILED (1)
  - version-1-5-97-0.bin: 1 SE id(s) absent, e.g. [999999] at ['OStimTogether:OStimBridge.cpp:421']
```

**指名了插件、文件与行号** —— 正是此前完全没被扫的那个插件。改回真 id 则通过。
站点数 **33 → 41**,检查的插件 **1 → 2**。

> **变异测试本身有个坑,本节踩到了**:第一次做的时候,`Set-Content` 写
> `plugins/OStimTogether/...` **被沙箱拒绝**(子模块不可写),而脚本没有检查写入结果 ——
> 于是门禁跑的是**没被改过的**文件,打印 OK,我差点把它当成「门禁没抓到」。
> 第二次改在**工作区内的隔离副本**上做,并且**先断言变异已生效**
> (`contains RELOCATION_ID(999999, ...) == true`)再跑门禁。
> **教训**:变异测试必须验证「变异真的应用了」,否则「门禁通过」既可能是
> 门禁不行,也可能是变异没生效 —— 而后者会让你误判门禁是好的。

### 113.5 验证与未做

- 13 条门禁全绿;`check_ostimng_versions.py` 现在是 41 个站点、2 个插件。
- 门禁的输出标题从 `OSTIMNG VERSION CHECK` 改为 `PLUGIN VERSION CHECK`
  (它已不只管 OStimNG);文件名与 CI 步骤名保留,以免打断 git 历史与 CI 引用,
  但模块 docstring 已写明实际范围。
- 隔离副本已删除,`git status -- plugins/` 只有 ` m` 子模块标记 ——
  **真实子模块内容未被改动**。
- **未做**:其余 6 个插件(`AnimSyncTogether`、`DAVSyncTogether`、`IEDSyncTogether`、
  `MorphSyncTogether`、`TradeTogether`、`STRPluginMessagingAPI`)**不使用**
  `RELOCATION_ID`/`REL::ID`(实测 0 处),它们只走 `SKSE::` 接口与 `GetProcAddress`,
  所以不在本门禁的范围内 —— 这一点已用计数确认,不是假设。

> **教训**:门禁的可靠性有三个**各自独立**的问题,本节与 §112 各证了一个:
> ①**它会跑吗**(§112.5:上一轮的新门禁根本没接进 CI);
> ②**它会失败吗**(§112.4、§113.4:变异测试);
> ③**它看全了吗**(本节:扫描根写死成一个插件,另一个插件的 8 个站点从未被看)。
> 第三个最隐蔽 —— 前两个都通过的门禁,仍然可能因为**扫描范围**而整体空转,
> 而且它照样打印 OK。防它的办法是**把范围写成显式清单,并让「清单里的东西贡献了 0 项」本身失败**。

---

## 114. 2026-10-11 场次:`ExtraDataList` 的读侧判空、写侧不判 —— 判据是「同一成员的读写两侧必须同意」

> **锚点**:`Code/client/Games/Skyrim/ExtraData/ExtraDataList.cpp`、
> `Code/client/Games/Skyrim/TESObjectREFR.cpp`。
> 门禁:`Tools/Scripts/check_allocation_guards.py`。

### 114.1 判据:找「本仓库自己重写了引擎结构」的地方,逐对比较同一成员

§111 的判据是「引擎那份就是契约」,适用于**转发调用**引擎函数的包装。
但本仓库还有另一类东西:**自己重写了一遍引擎的结构与它的方法**。
`ExtraDataList` 就是——它不是 `POINTER_SKYRIMSE` 转发,而是本仓库声明的
`struct` 加上本仓库实现的 `New`/`Add`/`Contains`/`SetType`/`GetByType`/`GetCount`。

这类地方没有「引擎那份」可对照,但有**内部的契约**:

> **同一个成员,读侧和写侧必须同意它「空」是什么意思。**

`ExtraDataList` 有三个这样的成员(`data` / `bitfield` / `lock`),
每个都被多个函数解引用。把每个成员的**读写两侧**摆在一起,缺陷就显形了。

### 114.2 缺陷:读侧返回「空集」,写侧直接崩

```cpp
bool ExtraDataList::Contains(ExtraDataType aType) const
{
    if (bitfield)                                   // 读侧:判空
    {
        ...
        return (element >> (value % 8)) & 1;
    }
    return false;                                   // 空 -> 「没有任何位被设置」
}

void ExtraDataList::SetType(ExtraDataType aType, bool aClear)
{
    uint32_t index = static_cast<uint8_t>(aType) >> 3;
    uint8_t& flag = bitfield->data[index];          // 写侧:直接解引用  <- 崩
```

两侧操作**同一个成员**。读侧已经把「`bitfield` 为空 = 空集」这个语义**定下来了**,
写侧却不同意 —— 于是同一个列表,**读它是「空」,写它是崩**。

而写侧是**活路径**:`ExtraDataList::Add` → `SetType(aType, false)`,
`Add` 在**物品栏同步**上(`TESObjectREFR::GetExtraDataFromItem` 建列表时调它)。

> **判据的可操作形式**:列出结构体的每个成员 → 找出所有解引用它的函数 →
> 看其中**有没有一个判了空**。有一个判了,其余不判的**就是缺陷** ——
> 因为「这个成员可以为空」这个判断已经被那个函数自己做出了。
> 这与 §111.4 的「树内不一致」是同一条判据,只是范围缩到一个结构体内。

### 114.3 缺陷:`New()` 两次分配都不判,而且**立刻写穿**

```cpp
ExtraDataList* pExtraDataList = Memory::Allocate<ExtraDataList>();
pExtraDataList->data = nullptr;                       // 第一次失败 -> 崩
pExtraDataList->lock.m_counter = pExtraDataList->lock.m_tid = 0;
pExtraDataList->bitfield = Memory::Allocate<ExtraDataList::Bitfield>();
memset(pExtraDataList->bitfield, 0, 0x18);            // 第二次失败 -> 崩
return pExtraDataList;
```

**「`Memory::Allocate` 会不会返回空」不需要猜** —— 同一个仓库里已经按
「可能为空」处理过它了。`TESObjectREFR.cpp:705` 那段:

```cpp
if (pExtraDataList->data == nullptr)
{
    Memory::Delete(pExtraDataList->bitfield);
    Memory::Delete(pExtraDataList);
    pExtraDataList = nullptr;
}
```

它**明确地** `Memory::Delete` 两个指针并把结果置空 —— 也就是说这个仓库
已经把 `New()` 的产物当成**可为空**的东西在用。而 `New()` 自己却在
第一次分配之后就无条件写 `->data`。

更关键的是:**唯一的调用者本来就处理了空返回值**:

```cpp
pExtraDataList = ExtraDataList::New();
if (arEntry.ExtraCharge > 0.f)
    pExtraDataList->SetChargeData(...);     // 改前:不判空,直接解引用
```

所以这里崩掉**不是契约不允许**,是没判。

### 114.4 缺陷:`Add()` 存进一个空节点

```cpp
apNewData->next = pNext;   // apNewData 可能为空
```

链表里一旦有空节点,之后 `GetByType` 的遍历与 `GetCount` 的计数**都会解引用它**。
而 `Add` 的返回类型**本来就是 `bool`**(成功/失败),判空后返回假是现成的语义,
不需要新造。

### 114.5 顺带核对:位图下标在界内(不是猜,是算)

`Contains`/`SetType` 用 `value >> 3` 作位图下标,位图是 `0x18 = 24` 字节。
把 `ExtraDataType` 的**全部 18 个枚举值**算一遍:

```text
最大值 Soul = 0x9C = 156  ->  index = 156 >> 3 = 19  <  24   全部在界内
```

所以这里**没有越界写**。记下来是因为「位图下标」是这类结构最容易越界的地方,
而**算一遍比读一遍便宜**。

### 114.6 门禁:按函数体判,不认行号

`Tools/Scripts/check_allocation_guards.py` 按**函数**读**函数体**(用括号配平切出来,
不是按行号或正则匹配一行),要求函数体里解引用的指针**要么在同体内判过**,
要么由构造函数保证。它同时**反向查**:清单里的函数若不再存在,报错 ——
否则这个门禁会在函数改名后**静默地什么都不查**(§113 的同一类问题)。

**变异测试**:把 `New`/`Add`/`SetType` 三个函数改回**修前的原文** →
门禁报 **3 条**并逐条指名:

```text
ALLOCATION GUARD CHECK FAILED (3)
  - ...: New() dereferences `pExtraDataList` without checking it, but Memory::Allocate returns null on failure
  - ...: Add() dereferences `apNewData` without checking it, but the caller may pass a null node
  - ...: SetType() dereferences `bitfield` without checking it, but the bitfield allocation may have failed
```

改回修好的写法 → 通过。**变异真的生效**这一点是先断言的(§113.4 的教训)。

### 114.7 验证与未做

- 14 条门禁全绿(5 条 `Tools/Scripts/` + 9 条 `Code/plugins/tools/`)。
- 三个新门禁(`check_null_accessors`、`check_crash_diagnostics`、
  `check_allocation_guards`)**都接进了 CI 的 `push` 与 `pull_request` 两条
  `paths` 触发列表**,并各自有运行步骤 —— §112.5 的教训现在对三个都成立了。
- `check_workflow_shell.py` 与 YAML 解析通过。
- 隔离副本已删除;`git status` 只剩预期的新文件。
- **本机不编译**(§0/§1):以上是静态判据。`New()` 的两次分配失败要真复现需要
  构造内存压力,本节证明的是**判空存在且门禁会拦住删除它的改动**。

> **教训**:本仓库有两类「引擎相关代码」,判据不同:
> **转发引擎函数**的(§111)——**引擎那份就是契约**,反汇编读一遍;
> **自己重写引擎结构**的(本节)——没有外部契约,但有一条**内部契约**:
> **同一成员的读侧与写侧必须同意「空」是什么意思**。
> 后者更隐蔽,因为两侧往往相隔几十行、写在不同的函数里,
> 而且**各自看都说得通**。

---

## 115. 2026-10-11 场次:每条消息的「写」与「读」必须走同一串线上操作 —— 以及一次被自己的假阳性骗到的审计

> **锚点**:`Code/encoding/Messages/**`、`Code/encoding/Opcodes.h`、
> `Code/encoding/Messages/{Client,Server}MessageFactory.h`。
> 门禁:`Tools/Scripts/check_wire_symmetry.py`。

### 115.1 判据:每一条消息**被定义两次**,而没有任何东西把两者绑在一起

线上格式的每条消息都有一个 `SerializeRaw` 和一个 `DeserializeRaw`。
两者不是同一个函数、不共享宏、**编译器也不检查它们一致**:

```cpp
void SpellCastRequest::SerializeRaw(...)   const noexcept { WriteVarInt(CasterId); ... }
void SpellCastRequest::DeserializeRaw(...)       noexcept { CasterId = ReadVarInt(...); ... }
```

于是判据很直接:**把两侧的操作序列抽出来,逐条对照**。112 个 `.cpp` 里
110 条是真消息(另 2 个是工厂)。

### 115.2 为什么这是稳定性缺陷,而不是洁癖

因为这类不一致**不报错**:

- 写了一个字段却没读 → **后面每个字段整体错位一个宽度**;
- 读了没写的 → 读走的是**下一个字段的字节**;
- 同样的字段换个顺序 → **两边都错**。

关键在于**收包方通常还有字节**(一个包不止那几个字段),所以症状不是
「解不出来」,而是**值读错了** —— 一个坐标、一个 form id、一个计数。
**流还保持着同步**,下一次解析照样对得上。这正是它难找的原因:
没有异常、没有日志、没有解码失败,只有一个**看起来合法但内容是错的**数值。

这与 §45 记的「写几条就只循环几条」是**同一族的另一端**:
那边是写侧写了比计数更多的条目(读侧提前收工、剩余字节被当下一条消息解析),
这边是两侧对**字段顺序本身**不一致。

### 115.3 判据要按「操作」而不是按「字段名」

第一版抽取脚本按**字段名**比对,报了 **17 条不一致**。逐条读下来**全是假阳性**:

| 假阳性 | 为什么名字不同是合法的 |
| --- | --- |
| `WriteVarInt(count)` vs `ReadVarInt(key)` | 读侧用局部变量接计数 |
| `kvp.first` / `kvp.second` vs `Updates[serverId]` | 读侧用 map 下标,写侧用迭代器 |
| `value` vs `Values[i]` | 循环里下标与元素名不同 |
| `Magnitude` 只出现在写侧 | 读侧被 `WireLimits::ClampFinite(...)` 包了一层 |

**决定字节数的是操作的种类与顺序,不是字段的名字。** 改成按操作抽取后:

```text
写: WriteVarInt(Tick) WriteVarInt(count) WriteVarInt(kvp.first) kvp.second.Serialize
读: ReadVarInt(Tick)  ReadVarInt(count)  ReadVarInt(cServerId)  Updates[cServerId].Deserialize
    -> 都是 VarInt VarInt VarInt Nested
```

**循环体塌缩成一次迭代**:两侧迭代同一个计数,重复次数不携带序列之外的信息。

> **教训**:审计脚本的**假阳性会让人怀疑判据本身**。17 条假阳性里没有一条是真缺陷,
> 如果就此认为「这类比对没法做」,就会错过一个**真正能守住的契约**。
> 正确做法是**逐条读掉假阳性**,再问「剩下的抽取规则哪里不够准」——
> 而不是降低标准或者放弃。

### 115.4 审计结果:110 条全部一致 —— 加的是**守护**,不是修 bug

把抽取改对之后 **110 条全部通过**。也就是说**当前没有活缺陷**。
这两件事必须分开说(§113.2 也是这个道理):

- **不是**「修了一条坏消息」;
- **是**「这条契约此前没有被任何门禁守住,现在守住了」。

### 115.5 门禁与四向变异测试

`Tools/Scripts/check_wire_symmetry.py` 同时检查:

1. 每条 `Messages/*.cpp` **两个半对都在**(只写不读 = 无法往返);
2. 两侧的**线上操作序列**逐条相同。

四个变异,每个都**先断言变异真的生效**(§113.4 的教训):

| 变异 | 门禁输出 |
| --- | --- |
| 写了不读 | `the write side performs 1 more operation(s) than the read side` |
| **只改一侧**的顺序 | `same operations, DIFFERENT ORDER` |
| 读了不写 | `the read side performs 1 more operation(s) than the write side` |
| 只定义半对 | `defines only one half of the pair - DeserializeRaw is missing` |
| 未变异的对照组 | **通过** |

> **变异测试这次也踩了一个坑,而且性质与 §113.4 不同**:第一次做「顺序」变异时,
> 我**把两侧同时交换了** —— 那样两侧**依然一致**,门禁**正确地**放行,
> 而我差点把它读成「门禁抓不到顺序问题」。
> **真实的缺陷形状是「只改一侧」**:两侧都改是**一致的重构**,不是缺陷。
> 这提示变异测试不仅要断言「变异生效了」,还要问
> **「这个变异是不是一个真实的缺陷形状」** —— 否则你测的是门禁的假阴性,
> 而它其实是对的。

### 115.6 顺带核对:opcode 三处互相一致

同一个「定义在多个地方、没人绑在一起」的问题也存在于 opcode 上 ——
一个 opcode 要出现在**三处**:`Opcodes.h` 的枚举、工厂的 `#include`、
工厂的 visitor 注册。核对结果:

| 检查 | 结果 |
| --- | --- |
| 客户端 52 + 服务端 55 个 opcode 取值**唯一** | 通过 |
| 全部 **≤ 255**(线上是 `unsigned char`) | 通过 |
| 每个 opcode 都有对应的消息头 | 通过 |
| 每个消息头**既被 include 也被注册**进对应方向的工厂 | 通过 |
| 注册的类**属于该方向**(拿另一个方向的 opcode 即为错) | 通过 |

**注册漏一个不会崩** —— 只会让那条消息**静默不被处理**。
所以这一条同样是「值得核对而不是假设」,而不是「显然没问题」。

### 115.7 验证与未做

- 15 条门禁全绿(6 条 `Tools/Scripts/` + 9 条 `Code/plugins/tools/`)。
- 四个新门禁**都接进了 CI 的 `push` 与 `pull_request` 两条 `paths` 触发列表**,
  并各自有运行步骤(§112.5 的教训)。
- 隔离副本已删除;`git status` 只剩预期的新文件。
- **本机不编译**(§0/§1),以上是静态判据。`check_wire_symmetry.py` 证明的是
  **两侧的契约一致**,不证明**线上的字节数正确** —— 后者要实机联机才能验。

> **教训**:「同一个东西定义在两个地方」是**缺陷的温床**,而且这类缺陷的共同形状是
> **不报错**:写/读两侧不一致 → 值错;opcode 注册漏一处 → 消息静默丢弃。
> 两者都**不会崩、不会打日志**,只会让人以为「这个功能有时候不灵」。
> 对策不是更仔细地看,而是**把两处抽出来自动对照** ——
> 并且**抽取规则要按语义(操作)而不是按字面(名字)**,
> 否则假阳性会多到让人放弃这条判据。

---

## 116. 2026-10-11 场次:上一轮的门禁**自己犯了它要防的那个错** —— 判据是「把范围按规则数,而不是按第一个想到的地方写」

> **锚点**:`Tools/Scripts/check_wire_symmetry.py`。
> 主题与 §113 相同(门禁的**范围**),但这次的犯错者是**我自己**,而且是在
> **刚写完那条教训的下一轮**。

### 116.1 事件:扫描根写成了「第一个想到的地方」

§115 加 `check_wire_symmetry.py` 时,扫描根是:

```python
MESSAGES = ROOT / 'Code' / 'encoding' / 'Messages'   # 改前:只有这一个目录
```

选它的理由是「消息在那里」——**这是按「第一个想到的地方」写的,不是按规则写的**。
本轮把规则问清楚:**还有哪些目录定义同样的成对函数?**

| 目录 | `.cpp` | 同时定义成对函数 | 函数名 |
| --- | --- | --- | --- |
| `Code/encoding/Messages` | 112 | 110 | `SerializeRaw` / `DeserializeRaw` |
| **`Code/encoding/Structs`** | **26** | **23** | `Serialize` / `Deserialize` |

`Structs` 里的 `LockData`、`Inventory`、`ActorValues`、`QuestLog`…
是**消息内部**的结构体。它们和消息**完全同构**:一对函数、决定字节数、
**没有任何东西把两侧绑在一起**。**23 对,占总量三分之一,从未被扫。**

### 116.2 这是 §113 的同一个错误,而这次是我自己犯的

§113 记的是 `check_ostimng_versions.py` 只扫 OStimNG 一个插件,漏掉
OStimTogether 的 8 个站点。当时写的教训是:

> 防它的办法是**把范围写成显式清单,并让「清单里的东西贡献了 0 项」本身失败**。

**然后我在同一轮之后又犯了一次**,只是换了个文件。这说明它不是「一次疏忽」,
而是**写门禁时的默认思维**:看到「这里有这种东西」就写死那个路径,
不去问「**这条规则适用于哪些地方**」。

> **教训**:写门禁时,**「扫哪里」这个问题必须按规则回答,不能按发现地点回答**。
> 具体做法:先写出**规则**(「凡定义成对函数的 `.cpp`」),再**用规则去搜一遍**
> 全仓(这里是 `grep -l '::Serialize(' ` 与 `::Deserialize(` 的目录分布),
> 最后把结果写成清单。**只写发现地点,等于把「还有别处」这件事留给了运气。**

### 116.3 修法:清单 + 防范围腐化,而不是只补一个目录

只把 `Structs` 加进扫描根**不够** —— 下一次还会有第三个目录。所以:

1. `SCANNED` 写成**显式清单**,同时容纳两种拼写
   (`Messages` 用 `SerializeRaw`/`DeserializeRaw`,`Structs` 用 `Serialize`/`Deserialize`);
2. 加**防范围腐化**的反向检查:**清单里的目录若贡献 0 对,本身就是失败**。

第 2 条是关键,理由与 §113.3 相同:**「目录改名/移动」与「目录里真的没有」
在扫描结果上长得一样**,而后者会让门禁**静默退化成空转却照样打印 OK**。

覆盖从 **110 对升到 133 对(+21%)**。审计结果 **133 对全部一致** ——
仍然**没有活缺陷**,补的是守护(§115.4 的区分在这里同样成立)。

### 116.4 变异测试:扩到两个目录,并专门测「范围腐化」

每个变异都**先断言真的生效**(§113.4 的教训):

| 变异 | 门禁输出 |
| --- | --- |
| `Messages` 里写了不读 | `the write side performs 1 more operation(s) than the read side` |
| **`Structs` 里只改一侧的顺序** | `same operations, DIFFERENT ORDER` |
| **`Structs` 里改位宽(写 16 / 读 8)** | `only written: ['Bits16']` / `only read: ['Bits8']` |
| 未变异的对照组(两个目录各一份) | **通过** |
| **把 `Structs` 清空** | `Code/encoding/Structs contributed no Serialize/Deserialize pair` |

**位宽那条是新增的能力**:改前的门禁只扫 `Messages`,而 `Structs` 里
`WriteBits(x, 8)` 与 `ReadBits(x, 8)` 这种**位级**操作在 `Messages` 里较少 ——
也就是说这个盲区**恰好落在最容易出位宽错误的地方**。

**最后一条是本轮最该留下的**:把 `Structs` 清空会失败 ——
**这正是上一轮本可以抓到我的那条检查**。它的价值不在于这一次,
而在于**下一个目录**。

### 116.5 顺带核对(均无缺陷)

| 检查 | 结果 |
| --- | --- |
| 61 处 pad 声明的算术(`uint8_t padX[0xNN - 0xMM]`) | 无一处非正大小 |
| 4 处用 `sizeof(X)` 的 pad | 都写在明确注释了「锚点式」的地方 |
| 两个随包运行时 DLL 的大小 | 相同(6,409,728 字节) |
| 两个 DLL 的版本特定标记 | 两边都在,无「只更新一个构建」 |
| 手动 `Lock()`/`Unlock()` | 全仓 2 处,`GetByType` 的两条返回路径**都**配了 `Unlock` |
| `ExtraDataList::GetByType` 里三次 `IsOverriden()` | 同一个 thread_local 计数、同一线程内不变 |

### 116.6 验证与未做

- 15 条门禁全绿;`check_wire_symmetry.py` 现在 133 对、2 个目录。
- 6 条 `Tools/Scripts/` 门禁**都确认被 CI 调用**(查的是**运行步骤**,
  不是文件是否存在 —— §112.5 的教训)。
- 隔离副本已删除;`git status` 只剩预期的新文件。
- **本机不编译**(§0/§1)。本节证明的是**两侧契约一致**,不证明**线上字节数正确**。

> **教训**:本节与 §113 是**同一条教训的两次出现**,而第二次的犯错者是我自己、
> 时间就在第一次之后。所以这条要写得比 §113 更硬:
> **门禁的「扫哪里」必须由规则推出,并由「贡献 0 项即失败」守住。**
> 一个只扫「发现地点」的门禁,和没有门禁的区别只在于**它会打印 OK**,
> 而那比没有更糟 —— 它让人以为已经被守住了。

---

## 117. 2026-10-11 场次:偏移量的**实测证据**签在仓库里,却没有任何东西把代码和它绑在一起

> **锚点**:`Code/client/Games/GamePatch.h`、13 处 `GamePatch::At(...)` 调用、
> `Tools/ida/st_patch_offsets_1597.tsv`。
> 门禁:`Tools/Scripts/check_patch_offsets.py`。

### 117.1 判据:找出「只有文档意义、编译器管不着」的证据文件

§113 与 §116 的判据是「门禁扫得够不够全」。本轮换一个方向问:
**仓库里有哪些文件是「证据」,却没有代码或门禁读它?**

`Tools/ida/st_patch_offsets_1597.tsv` 就是这样一个:

```text
# ae_id  ae_offset  se_offset  note                          site
77226    0x175      -          instruction not in an aligned run   window style
52510    0x84e      0x929      aligned on jne (+0 into it)         stats menu appear
82082    0x682      0x6a4      aligned on call (+0 into it)        menu unfreeze (call site)
34452    0x374      0x397      HAND ALIGNED: ...                   projectile null check (jump out)
```

它是 `Tools/ida/patch_offsets_1597.py` 把 1.6.x 与 1.5.97 两份反汇编
**逐指令对齐**的产物,对没对齐成功的行**写明原因**。`GamePatch.h` 的注释指向它。

但**没有任何门禁读它**:

```text
git grep -ln 'st_patch_offsets_1597' -- Tools/Scripts Code/plugins/tools
  (空)
```

### 117.2 缺陷形状:证据与主张各说各话

偏移量以**字面量**散在 8 个文件、13 处调用里:

```cpp
GamePatch::At(pProcessMessage, {0x84E, 0x929, "1.5.97"}, "stats menu appear")
//                             ^^^^^  ^^^^^  ^^^^^^^^
//                             modern legacy 实测版本
```

- **tsv 是证据** —— 「哪一行的 1.5.x 偏移有实测支持」;
- **代码是主张** —— 它决定这个补丁**到底写不写**。

两者**没有共同的检查者**,所以可以各自漂移。而**危险的方向只有一个**:

> **调用点带着一个没有任何行支持的 1.5.x 偏移。**
> 那是把**没人量过的数字写进正在运行的游戏**。

反方向(tsv 有、代码不用)**不是缺陷** —— 1.5.x 上主动放弃一个纯外观补丁是有意的,
`frame end` 正是这样(1.5.x 直接 hook 锚点本身,不在函数内部打补丁)。
但它值得**报告**,因为那通常意味着「行更新了、调用点没跟上」。

> **教训**:一条偏移量有三个属性 —— **数值**、**它量在哪个版本上**、
> **它的证据在哪**。前两个写在代码里,第三个写在文档里,而**只有前两个会被编译器检查**。
> 证据一旦不进检查,**它就是注释**:可以过期、可以互相矛盾,而且没有任何征兆。

### 117.3 先核对现状:13 处逐条对照

| 站点 | 代码 modern | tsv ae | 代码 legacy | tsv se | 结论 |
| --- | --- | --- | --- | --- | --- |
| window style | `0x174 + 1` = 0x175 | 0x175 | – | – | 一致(行注明 1.5.x 未对齐) |
| dinput cooperative level | 0x57 | 0x57 | 0x57 | 0x57 | 一致 |
| stats menu appear | 0x84E | 0x84e | 0x929 | 0x929 | 一致 |
| stats menu background | 0xA10 | 0xa10 | 0xAEC | 0xaec | 一致 |
| stats menu update | 0x1040 | 0x1040 | 0xFBC | 0xfbc | 一致 |
| stats menu controls a / b | 0x46 / 0x4A | 同 | 0x46 / 0x4A | 同 | 一致 |
| menu unfreeze | 0x682 | 0x682 | 0x6A4 | 0x6a4 | 一致 |
| favorites menu numbering | 0x15 | 0x15 | 0x15 | 0x15 | 一致 |
| projectile null check | 0x374 | 0x374 | 0x397 | 0x397 | 一致 |
| skip startup movie | 0xFE | 0xfe | – | – | 一致(1.5.x 未对齐) |
| tasklet thread names | 0x63 | 0x63 | – | – | 一致(该 id 在 1.5.97 未映射) |
| **frame end** | 0x9 | 0x9 | **–** | **0x9** | **tsv 有实测偏移,代码不用** |

**13 处里 12 处完全一致,1 处是上面说的「有意不用」** ——
也就是说**当前没有活缺陷**,缺的是守护(§113.2、§115.4 的同一种区分)。

### 117.4 两个命名空间不同,这是核对时最容易卡住的地方

tsv 的行名由对齐工具写,**带限定词**;代码用**短名**:

| tsv 行名 | 代码站点名 |
| --- | --- |
| `menu unfreeze (call site)` | `menu unfreeze` |
| `projectile null check (jump out)` | `projectile null check` |
| `stats menu controls a` / `... b` | 两处都叫 `stats menu controls` |

所以门禁按「**行名 = 站点名 或 站点名 + 限定词**」匹配,
并在**多行都匹配时用偏移量定行** —— `stats menu controls` 的两处
(0x46 与 0x4A)就是这样各自对上 `... a` 与 `... b` 的。

**第一次跑门禁时报了 10 条错,全部是我的解析器不够**:
正则停在偏移列表的 `}` 上、没吃到后面的站点名;`0x55 + 2` 这种
「指令 + 操作码长度」的写法没求值;上面这个命名空间差异没处理。
**逐条读掉之后才归零** —— 与 §115.3 同一个过程。

### 117.5 门禁与变异测试

`Tools/Scripts/check_patch_offsets.py`:

1. **机械枚举**每一处 `GamePatch::At(...)`(不认行号),解析锚点、modern、
   legacy、实测版本、站点名;
2. **modern 必须等于 tsv 的 `ae_offset`**;
3. **legacy 只在 tsv 那一行确实有 `se_offset` 时才允许出现**,否则报错并
   **引述该行的原因**;
4. 有 legacy 时**实测版本必须是 `1.5.97`**,与 tsv 的前提一致;
5. **反向查**:两张豁免表里的站点若在 tsv 里不再存在,报错 —— 免得豁免活得比理由长。

三个变异,**每个都先断言真的生效**:

| 变异 | 门禁输出 |
| --- | --- |
| 1.5.x 偏移改成 tsv 没有的 `0x999` | `uses 1.5.x offset 0x999 but ... records 0x929` |
| 给「tsv 明确没有 1.5.x 偏移」的站点**加上**一个 | `applies a 1.5.x offset (0x100) that ... does not record - the row says 'instruction not in an aligned run'. A number nobody measured must not be written into a running game` |
| modern 改成 tsv 没有的 `0x375` | `uses modern offset 0x375, which no ... row records for it (recorded: 0x374, 0x379)` |
| 未变异的对照组 | **通过** |

### 117.6 顺带核对:39 个插件补丁全部可应用

`Code/plugins/tools/plugin_patches.py verify` 通过:39 个补丁**全部**能干净地
应用到固定的子模块提交上。**一个应用不上的补丁等于一个静默消失的修复** ——
因为 CI 的 `git submodule update --init --force` 会丢弃工作区改动,
所以修复只能靠补丁活着(该文件自己的 docstring 就是这么写的)。

### 117.7 验证与未做

- 16 条门禁全绿(7 条 `Tools/Scripts/` + 9 条 `Code/plugins/tools/`)。
- 7 条 `Tools/Scripts/` 门禁**都确认被 CI 调用**,且**都在 `push` 与
  `pull_request` 两条 `paths` 列表里**(§112.5 的教训)。
- 隔离副本已删除;`git status` 只剩预期的新文件。
- **本机不编译**(§0/§1)。本门禁证明的是**代码与实测证据一致**,
  **不证明**那些偏移在真机上写对了位置 —— 后者要实机跑 1.5.97 才知道。

> **教训**:一份**签入仓库的实测证据**,如果不进任何门禁,它的地位和注释一样:
> 会过期、会与代码矛盾,而且**矛盾时没有任何征兆**。
> 而这里的方向性很重要:**证据比代码强**(代码可以随便改,证据是量出来的),
> 所以门禁要问的是「**代码的主张有没有证据支持**」,
> 而不是「证据有没有被代码用到」—— 后者会把有意的放弃当成错误。

---

## 118. 2026-10-11 场次:**活缺陷** —— 覆盖表里同一个 id 两个值,而读取方静默取最后一个

> **锚点**:`Tools/ida/st_overrides_all.tsv`、`Code/client/Games/Skyrim/Actor.cpp:1581`、
> `GameFiles/Skyrim/SKSE/Plugins/versionlib-ae-to-se-1-5-97-0.map`。
> 门禁:`Tools/Scripts/check_ae_to_se_overrides.py`。
> **本节是 §117 判据的第一次命中活缺陷**(§111–§117 找到的多是守护)。

### 118.1 判据复用:把 §117 问遍整个 `Tools/ida/`

§117 的判据是「找出只有文档意义、编译器管不着的证据文件」。
本轮把它**逐个问一遍**:`Tools/ida/` 下 23 个文件,哪个有代码/门禁在读?

`st_overrides_all.tsv`(3,692 行)的答案是:**除了一个 README,谁都没读。**

而它其实是**出货物的输入**:

```text
GameFiles/Skyrim/SKSE/Plugins/versionlib-ae-to-se-1-5-97-0.map
  = gen_ae_to_se_map.py --overrides Tools/ida/st_overrides_all.tsv 的输出
```

**1.5.x 运行时实际加载的那张「AE id → 1.5.97 RVA」翻译表,就是从这张表生成的。**
表里 3,678 行是手工核对过的 id → RVA。

### 118.2 缺陷:一个 id 两行,值不同,读取方**静默取最后一个**

```text
L288   35086 0x567a89
L3680  35086 0x567a80
```

读取方(`gen_ae_to_se_map.py::parse_overrides`)是:

```python
table[ae_id] = se_offset     # 后出现的覆盖先出现的 —— 没有任何提示
```

**出货的 map 取到的是「最后一个」** —— 这次恰好是**对的**那个。
但那是**行序的运气**,不是任何检查的结果。把文件排一次序、或把某一行挪个位置,
出货的就是另一个值,**而没有任何东西会报错**。

> **教训**:`table[key] = value` 这种写法**把重复键变成了一个静默的决策**,
> 而决策的依据是**文件里的行序** —— 一个没人认为自己在维护的东西。
> 一个 3,692 行的表,「每个 id 只出现一次」是个**必须被检查的不变量**,
> 因为违反它的后果不是报错,是**安静地选错一个值**。

### 118.3 两个值谁对?这要靠地址库和真实 exe,不能靠读代码

先问 1.5.97 的地址库(`version-1-5-97-0.bin`,778,674 条):

| RVA | 是函数起点? |
| --- | --- |
| `0x567a80` | **是**(SE id 34286) |
| `0x567a89` | **不是** |

再把真实 `SkyrimSE.exe`(1.5.97,34,769,792 字节)反汇编出来:

```asm
140567a80:  48 85 d2           test %rdx,%rdx        <-- 函数真正的开头
140567a83:  0f 84 3d 03 00 00  je 0x140567dc6
140567a89:  48 8b c4           mov %rsp,%rax         <-- 旧值指向这里,函数体内 9 字节
```

`0x567a89` **落在函数体内部**,越过了 `test rdx,rdx / je` 这个序言,
指向「已经假定 rdx 非空」之后的代码。

### 118.4 为什么这不是死数据

`Code/client/Games/Skyrim/Actor.cpp:1581`:

```cpp
POINTER_SKYRIMSE(TApplyActorEffect, s_applyActorEffect, 35086);
```

这是一个**活的**函数指针。在 1.5.x 上,它经 `VersionDbPtr` 拿到的是
**覆盖表 → 出货 map → 1.5.97 RVA**。一个指向函数内部的地址,
**就是一次跳到错误位置的调用** —— 而且它不会立刻崩,会先执行半个序言。

### 118.5 先核对现状:全表只有这一个坏值

把**全部 3,678 行**逐条对 1.5.97 地址库查:

| 检查 | 结果 |
| --- | --- |
| RVA 是函数起点 | 3,677 |
| RVA 不是函数起点 | **1**(就是 35086 的旧值) |
| 落在 `.text` 内却不是函数起点(= 可疑) | **1** |
| 落在 `.text` 之后(数据 / RTTI 定位符,正常) | 0 |

**全表只有这一个坏值** —— 与 §113.2 同样的区分:这是「发现了一处」,
不是「这一类都坏」。所以修法要**同时**修值**和**加不变量检查。

### 118.6 修法与门禁

修:把 L288 改成正确的 `0x567a80`,并**删掉 L3680 那条重复行** ——
两行同值没有意义,留着只会让「一个 id 一行」这个不变量继续被破坏。

`Tools/Scripts/check_ae_to_se_overrides.py` 三条检查各自独立:

| 检查 | 为什么 |
| --- | --- |
| **每个 id 只出现一次** | 重复 = 读取方在**静默选择**,而依据是行序 |
| **每个 RVA 是 `version-1-5-97-0.bin` 里的函数起点** | 该客户端在那个版本上加载的库;`.text` 之后的定位符结构上就是数据,单独放行 |
| **每个值与出货 map 一致** | map 由这张表生成,不一致 = **只重生成了一边** |

### 118.7 变异测试,包括**还原原始缺陷**

| 变异 | 门禁输出 |
| --- | --- |
| **还原原始缺陷**(35086 → `0x567a89`) | `id 35086 maps to 0x567a89, which is inside .text but is not a function start in version-1-5-97-0.bin - a call through it lands mid-function` **+** `id 35086 says 0x567a89 but the shipped map has 0x567a80; the map is generated from this file, so one of the two was regenerated without the other` |
| 还原重复行 | `id 35086 appears on line(s) 288, 3680; the reader keeps the last one, so the value that ships depends on file order` |
| 值差 1 字节(`0xf9e91`) | 同样两条,指名 L14 |
| 未变异的对照组 | **通过** |

**第一条就是本轮的原始缺陷** —— 门禁确实会拦住它,而不是只拦住我编的变异。

### 118.8 验证与未做

- 17 条门禁全绿(8 条 `Tools/Scripts/` + 9 条 `Code/plugins/tools/`)。
- 8 条 `Tools/Scripts/` 门禁**都确认被 CI 调用**,且都在 `push` 与
  `pull_request` 两条 `paths` 列表里(§112.5 的教训)。
- 隔离副本已删除;`git status` 只剩预期的新文件。
- **本机不编译**(§0/§1),而且**本机不跑游戏**:本节证明的是
  「表与地址库、出货 map 三者一致」,**不证明** `TApplyActorEffect`
  在 1.5.97 上真的被调用到并表现正确 —— 那要实机。

> **教训**:本轮是 §117 判据的**第一次命中活缺陷**,而它的形状值得记住:
> **不是「值写错了」,是「同一个东西被写了两遍,而读取方静默选了一个」。**
> 这类缺陷的特点是**没有任何一方看起来是错的** ——
> 两个值都是合法的 32 位整数,map 也是从这张表正确生成的。
> 唯一能发现它的办法是**问一个外部权威**(这里是 1.5.97 地址库):
> 「这个值指向函数开头吗?」—— 而这个问题**只有把表读进来算一遍**才能回答。

---

## 119. 2026-10-11 场次:同一个错误**第三次**出现 —— 门禁只读了「发现缺陷的那个文件」

> **锚点**:`Tools/Scripts/check_ae_to_se_overrides.py`、`Tools/ida/README.md`。
> 主题与 §113、§116 完全相同,这是第三次,而且这次**只隔了一轮**。

### 119.1 事件

上一轮(§118)刚写完 `check_ae_to_se_overrides.py`,并且它**抓到了一个活缺陷**。
本轮按 §113/§116 的判据先问:**这个门禁扫得够全吗?**

它的扫描根是:

```python
OVERRIDES = ROOT / 'Tools' / 'ida' / 'st_overrides_all.tsv'   # 改前:只有一个文件
```

选它的理由是「缺陷是在那个文件里找到的」——**又是按发现地点写的**。

而 `Tools/ida/README.md` 给出的再生命令是:

```text
--overrides Tools/ida/st_overrides.txt \
--overrides Tools/ida/st_overrides_graph.txt \
--overrides Tools/ida/st_overrides_all.tsv \
--out       GameFiles/Skyrim/SKSE/Plugins/versionlib-ae-to-se-1-5-97-0.map
```

**三个文件都是出货 map 的输入**,门禁只读了第三个。

### 119.2 三次同一个错误,这次要写成一条规则

| 节 | 门禁 | 扫描根写成 | 漏掉的 |
| --- | --- | --- | --- |
| §113 | `check_ostimng_versions.py` | 1 个插件 | 另一个插件的 8 个站点 |
| §116 | `check_wire_symmetry.py` | `Messages/` | `Structs/` 的 23 对(总量三分之一) |
| **§119** | **`check_ae_to_se_overrides.py`** | **1 个文件** | **另外 2 个文件(84 行)** |

三次的**犯错机制完全相同**:写门禁时,扫描范围取的是**「我是在哪里发现这个问题的」**,
而不是**「这条规则适用于哪些地方」**。而且三次都发生在**同一个会话里**,
间隔分别是一轮、一轮 —— 说明这不是疏忽,是**默认思维**。

> **教训**:写门禁时,**「扫哪里」必须从规则反推,不能从发现地点正推**。
> 具体动作是两步:**①写出规则**;②**用规则去搜一遍全仓**,把结果写成清单。
> 只写发现地点,等于把「还有别处」交给运气 —— 而运气已经连续输过三次。

### 119.3 先核对现状:三个文件全部干净

| 文件 | 行数 | 重复 id | RVA 不是函数起点 | 在 `.text` 内却不是起点 | 与出货 map 不一致 |
| --- | --- | --- | --- | --- | --- |
| `st_overrides.txt` | 63 | 0 | 0 | 0 | 0 |
| `st_overrides_graph.txt` | 21 | 0 | 0 | 0 | 0 |
| `st_overrides_all.tsv` | 3,678 | 0 | 0 | 0 | 0 |

**三个文件全部干净** —— 补的是**守护**,不是修 bug(§113.2 的区分)。

唯一一处「id 在覆盖文件里、不在 map 里」是 `st_overrides_graph.txt` 的 **14774**
(`BSRandom::GetGenerator`)。README **明确**写着它「not yet folded into the map」,
并且:

> Do not "fix" this by dropping the id from the override file;
> **the file is the evidence log, the map is what ships.**

所以这一条被登记成**有意的例外并写明理由**,而不是当成错误 ——
这与 §117.2 的「证据比代码强、反方向不是缺陷」是同一条判断。

### 119.4 修法:清单 + 防范围腐化,而不是补两个文件名

1. `OVERRIDE_FILES` 写成**显式清单**,三个文件都扫;
2. `ALLOWED_ABSENT` 登记有意的例外(带理由),并**反向查**:
   豁免的 id 若不在文件里了就报错 —— 免得豁免活得比理由长;
3. **防范围腐化**:清单里的文件若**贡献 0 行**即为失败。

第 3 条是这次真正要留下的东西,理由与 §113.3/§116.3 相同:
**「文件被清空」与「文件格式变了」在扫描结果上长得一样**,
而后者会让门禁**静默退化成空转却照样打印 OK**。

覆盖从 **3,678 行 / 1 个文件** 升到 **3,762 行 / 3 个文件**。

### 119.5 变异测试:专门在上一版**从不读**的那两个文件里造缺陷

| 变异 | 门禁输出 |
| --- | --- |
| `st_overrides.txt` 里一个 RVA 改成 `0x1234` | `Tools/ida/st_overrides.txt:4: id 11612 maps to 0x1234, which is inside .text but is not a function start in version-1-5-97-0.bin` **+** `says 0x1234 but the shipped map has 0x10de90; the map is generated from this file, so one of the two was regenerated without the other` |
| `st_overrides_graph.txt` 里制造重复行 | `id 14774 appears on line(s) 18, 19; the reader keeps the last one, so the value that ships depends on file order` |
| 三个真实文件的对照组 | **通过** |
| **把 `st_overrides_graph.txt` 清空** | `contributed no override row` **+** 豁免过期提示 |

**上一版门禁对前两条变异完全不报** —— 这正是补范围的意义,
也是为什么变异测试要**在新增覆盖的地方造**,而不是只在新写的检查上造。

### 119.6 验证与未做

- 17 条门禁全绿(8 条 `Tools/Scripts/` + 9 条 `Code/plugins/tools/`)。
- 隔离副本已删除;`git status` 只剩预期的新文件。
- **本机不编译**(§0/§1):本节证明的是**三个输入文件与地址库、出货 map 三者一致**,
  **不证明**那些 RVA 在真机上被调用到并表现正确。

> **教训**:本节与 §113、§116 是**同一条教训的第三次出现**,所以这条要写成可执行的规则:
> **门禁的扫描范围 = 用规则搜出来的清单,而不是发现地点。**
> 并且**清单里的每一项都必须贡献结果,否则就是失败** ——
> 因为一个扫不到东西的门禁**照样打印 OK**,而那比没有门禁更糟:
> 它让人以为已经被守住了(§116.6 已写过这句,本节是它的第三次验证)。

---

## 120. 2026-10-11 场次:契约头被复制进 6 处,**ABI 是成员顺序**,而没有任何东西比较副本

> **锚点**:`Code/plugins/STRPM/include/STRPluginMessagingAPI/STRPluginMessagingAPI.h`
> 及 5 个插件内的同名副本。
> 门禁:`Code/plugins/tools/check_contract_copies.py`。

### 120.1 按 §119 的规程做范围审计:两条门禁的范围是对的

§119 定下的规程是「扫描范围 = 用规则搜出来的清单,不能是发现地点」。
本轮把它用在 `Code/plugins/tools/` 的 9 条门禁上,先查两条最像「写死单个文件」的:

**`check_skee_abi.py`** 的 `SOURCES` 只有两个文件。规则是什么?
「**凡声明了 `IOverlayInterface` / `IOverrideInterface` / `IBodyMorphInterface` 的文件**」。
用规则搜一遍全仓:

| 文件 | 声明了 | 在 `SOURCES` |
| --- | --- | --- |
| `plugins/MorphSyncTogether/src/SkeeInterfaces.h` | 3 个 | **是** |
| `plugins/OStimTogether/src/RaceMenuOverlayBridge.cpp` | 2 个 | **是** |

**0 个漏掉** —— 范围正确。

**`check_exports.py`**:契约里 4 个名字、运行时导出 4 个符号,
**双向对称、无多余导出** —— 范围也正确。

> **教训**:§119 的规程不是「所有门禁都写错了」,而是「**范围必须被证明,不能假设**」。
> 这两条门禁**经得起这条检查**,本身就是有价值的信息 ——
> 它把「范围对不对」从猜测变成了**已核对**。

### 120.2 顺着的线索:契约头被复制进 6 处

`check_exports.py` 让「契约头」这个词进入了视野。接着问:**这个头有几个副本?**

```text
Code/plugins/STRPM/...             运行时的正本
plugins/STRPluginMessagingAPI/...  独立桥
plugins/AnimSyncTogether/...
plugins/DAVSyncTogether/...
plugins/IEDSyncTogether/...
plugins/TradeTogether/...
```

**6 份。** 而它们的字节大小差到 **1.4 KB**(6,590 ~ 8,010)。
于是问题变成:**它们的 ABI 一致吗?谁在检查?**

答案:**没有任何东西在检查。** 而这正是最需要检查的一类东西 ——
因为这些接口结构体是**函数指针表**:

```cpp
struct Interface
{
    std::uint32_t version{ kInterfaceVersion };
    Result(STRPM_CALL* registerChannel)(const char*, ReceiveCallback, void*, ListenerHandle*);
    Result(STRPM_CALL* unregisterChannel)(ListenerHandle);
    Result(STRPM_CALL* send)(const char*, Target, const void*, std::size_t, std::uint32_t);
    ...
};
```

**ABI 就是成员的顺序。** 而交换 API **检查不了它**:
运行时交回一个指针,插件按**第 N 个槽位**调用 ——
某个副本里插入、删除或重排一个成员,**后面每一次调用都派发到错的函数**。

这与 §114 的 `ExtraDataList` 是**同一族的两个不同面**:
那里是「**同一份**文件里读侧与写侧不一致」,
这里是「**同一个头**的 6 个副本互相不一致」。共同点是
**没有任何机制强制它们一致**,而失败都不报错。

### 120.3 先核对现状:6 个副本的 ABI 全部一致

| 检查项 | 结果 |
| --- | --- |
| 4 个接口结构体的**成员顺序** | **6 个副本全部一致**(20 个布局) |
| 共享常量(`kInterfaceVersion` = 2 等 6 个) | 全部一致 |
| 声明的类型集合(15 个) | 全部一致 |

**没有活缺陷** —— 字节差异来自空白与注释,不是 ABI(§113.2 的区分)。

### 120.4 判据的关键选择:**不要求逐字节相同**

这是本节最该留下的一条。副本**合法地**在两点上不同:

- 空白与注释;
- **带了哪些尾部声明** —— 一个只用 `Interface` 的插件不必带 `ProxyResolverInterface`
  的全部内容,这与 `check_skee_abi.py` 里「Trailing methods a plugin does not call
  may be omitted」是同一个道理。

所以**要求字节相同是错的**:它会把合法的差异报成错误,
而**假阳性会让判据被放弃**(§115.3 的教训)。
正确的问题是「**ABI 一致吗**」,具体拆成三条:

| 检查 | 为什么 |
| --- | --- |
| **成员顺序** | 这就是 ABI;顺序变了,槽位就变了 |
| **共享常量的值** | 两边会拿它比较后才派发;值不同 = 一方按另一方不服务的版本派发 |
| **声明的类型集合** | 免得某个副本悄悄多/少一个类型 |

### 120.5 变异测试

| 变异 | 门禁输出 |
| --- | --- |
| 在 `AnimSyncTogether` 副本里**交换两个成员** | `declares the same members in a DIFFERENT ORDER: [...] vs [...]. The order is the ABI` |
| 在 `DAVSyncTogether` 副本里**改常量**(2→3) | `sets kInterfaceVersion = 3 but the runtime copy sets 2` |
| 在 `TradeTogether` 副本里**删掉一个成员** | `declares 6 member(s) but the runtime copy declares 7: [...] vs [...]. Every call after the difference dispatches through the wrong slot` |
| 未变异的对照组 | **通过** |
| **删掉整个副本文件** | `is missing; IEDSyncTogether cannot be compared against the other copies` |

第三条变异**同时命中了两个结构体**(`Interface` 与 `TransportInterface`)——
说明门禁是**按结构体逐个**比对,而不是只比了第一个。
这与 §115.5「同一个操作序列在多个结构体里各自比对」是同一种设计。

### 120.6 验证与未做

- 18 条门禁全绿(8 条 `Tools/Scripts/` + 10 条 `Code/plugins/tools/`)。
- 新门禁接进了 CI(既有触发路径已覆盖 `Code/plugins/**` 与 `plugins/**`,
  两个输入位置都在,所以无需改 `paths` 列表 —— 这一点是**核对过的**,不是假设)。
- 隔离副本已删除;`git status` 只剩预期的新文件。
- **本机不编译**(§0/§1):本节证明的是**6 个副本的 ABI 一致**,
  **不证明**它们在 MSVC 下的实际布局相同(那要出包后用 `static_assert` 或实机)。

> **教训**:「同一个东西被复制成多份」和 §114 的「同一个成员被读写两侧处理」
> 是同一条判据的两个面:**当一份定义变成多份,一致性就从「编译器保证」变成
> 「没人保证」**。而检查它的时候,**要检查的是那份定义的语义(这里是成员顺序),
> 不是它的字面(字节/空白)** —— 后者既会漏掉真正的漂移(重排后字节数不变),
> 也会把合法的差异报成错误。

---

## 121. 2026-10-11 场次:91 条引擎地址证据无门禁读;而行号会腐烂,**三元组**才是主张

> **锚点**:`Tools/ida/st_target_meta_1170.json`(132 KB,91 条)。
> 门禁:`Tools/Scripts/check_target_meta.py`。

### 121.1 判据复用:把 §117/§118 问遍 `Tools/ida/`

§117 建立的判据是「找出只有文档意义、编译器管不着的证据文件」,
§118 用它命中了一个**活缺陷**。本轮继续问下一个:
`st_target_meta_1170.json` 的答案是**只有写它的 IDA 脚本和一个 README**。

它是**客户端所解析的地址库 id 背后的证据**,91 条,每条给出:
绑定的客户端变量名、绑定的位置、1.6.1170 的绝对地址、
**该函数的反汇编全文**、它调用的函数、开头 16 字节。

### 121.2 三条各自可查的主张,三条都可能无声失效

| 主张 | 失效后果 |
| --- | --- |
| ① `ea_1170` 是那个 id 的地址 | **被审阅的反汇编不是客户端调用的那个函数** |
| ② 客户端**仍然**把那个 id 绑到那个变量 | 记录的反汇编不再描述实际运行的代码 |
| ③ 那个 id 在**每个** AE 运行时上都存在 | 只在一个版本上存在 = 其它版本解析为空(旧版运行时是共享桩) |

第三条尤其重要:文件名叫 `_1170`,但客户端在**每个** AE 构建上都解析同一批 id。

### 121.3 先核对现状:91/91 三条全过

| 检查 | 结果 |
| --- | --- |
| `ea_1170` − 镜像基址 == `versionlib-1-6-1170-0.bin` 的 RVA | **91 / 91** |
| (文件, 变量, id) 三元组仍存在于客户端 | **91 / 91** |
| 91 个 id 在全部 **14 个** AE 运行时上的解析 | **91/91 × 14** |

**没有活缺陷** —— 这份证据是准确的(§113.2 的区分:补的是**守护**)。

### 121.4 本节最该留下的一条:**行号会腐烂,三元组才是主张**

`callsite` 里带行号,而 **91 条里有 46 条**的行号**不再指向那个绑定**
(`ExtraDataList.cpp` 那 6 条各偏了 31 行)。原因很直白:行号是**拍摄时那个修订版**的,
此后文件被编辑过。

于是有两种写法,后果完全不同:

| 按行号查 | 按三元组查 |
| --- | --- |
| 46 条误报「绑定不见了」 | 91 条全部正确解析 |
| 会把**合法的编辑**报成错误 | 只对**真正的漂移**报错 |

**这与 §120.4 的判据是同一条**:检查一份定义的**语义**,不要检查它的**字面**。
行号是字面,`(文件, 变量, id)` 是语义。

> **教训**:证据文件里**任何「方便定位」的字段都会腐烂**(行号、字节偏移、索引),
> 而**它旁边那个「真正的标识」不会**。写门禁时要认准后者;
> 否则你会得到一堆假阳性,而假阳性会让判据被放弃(§115.3)。

### 121.5 第二件值得记的:我的匹配器第一版报了 12 条假阳性

第一版按「变量名 → id」在全文件里找,报了 12 条「变量绑到了别的 id」。
逐条读下来**全是假阳性**:

- **`POINTER_SKYRIMSE` 是函数局部的** —— 同一个名字在不同函数里**合法地**
  绑不同 id:`s_pickUpObject` 在 `Actor.cpp` 是 37521、在 `PlayerCharacter.cpp` 是 40533;
  `s_value` 在 `CombatView.cpp` 里出现三次,分别是 382393 / 382400 / 405282。
  **`callsite` 的作用正是指明是哪一处** —— 我第一版把它丢掉了。
- **`400180` 那条用的是另一种绑定形式**:`RttiLocator<...>(400180)`,
  不是 `POINTER_SKYRIMSE`。而元数据把它的 `var` 标成 **`"rtti"`** —— **标对了**。

所以门禁现在**两种绑定形式都认**,并且**只在 `callsite` 指名的那个文件里**查。

### 121.6 变异测试:三条主张各自被独立命中

| 变异 | 门禁输出 |
| --- | --- |
| `ea_1170` 改 1 字节 | `ea_1170 0x14015ab41 is RVA 0x15ab41, but versionlib-1-6-1170-0.bin records 0x15ab40 for this id; the disassembly that was reviewed is not the function this id resolves to` |
| 把 `var` 改成同文件里另一个名字 | `11612 (setHealth) is recorded against ...ExtraDataList.cpp, but that file binds setHealth to ['11616']; the metadata describes a binding the client no longer has` |
| 加一条**只在 1170 上存在**的 id | `versionlib-1-6-317-0.bin: 1 recorded id(s) do not resolve there, e.g. [415831] …`(同时命中 317 与 318) |
| 未变异的对照组 | **通过** |

第二条的诊断**直接说出正确的 id 是 `11616`** —— 是能拿去改的信息。

### 121.7 验证与未做

- 19 条门禁全绿(9 条 `Tools/Scripts/` + 10 条 `Code/plugins/tools/`)。
- 9 条 `Tools/Scripts/` 门禁**都确认被 CI 调用**,且都在 `push` 与
  `pull_request` 两条 `paths` 列表里。
- 隔离副本已删除;`git status` 只剩预期的新文件。
- **本机不编译、不跑游戏**(§0/§1):本节证明的是**证据与库、与客户端绑定、
  与全部 14 个运行时三者一致**,**不证明**那些函数在真机上的行为正确。

> **教训**:一份证据文件往往同时是**标识**(id、名字、三元组)与**定位辅助**
> (行号、偏移、索引)。**前者是主张,后者是缓存** ——
> 而缓存一定会过期。门禁必须建立在**标识**上,并且**把定位辅助的过期当成正常现象**,
> 否则它会用一堆假阳性掩盖掉真正的漂移。

---

## 122. 2026-10-11 场次:证据与出货物**不一致 2 条**,而判定谁对要靠真机反汇编

> **锚点**:`Tools/ida/st_capstone_report.json`、
> `GameFiles/Skyrim/SKSE/Plugins/versionlib-ae-to-se-1-5-97-0.map`。
> 门禁:`Tools/Scripts/check_capstone_report.py`。

### 122.1 判据继续:问 `Tools/ida/` 每个文件「谁在读你」

`st_capstone_report.json` 的答案是**没有门禁读它**。它是**相似度匹配的中间日志**:
对 91 个 AE id 记录匹配器给出的候选 `se_rva` 与评分,或没找到的原因。
**它是证据,而出货物是 map** —— 两者**没有任何东西对照过**。

对照结果:

| | 数量 |
| --- | --- |
| 有候选 | 49 |
| 与出货 map **一致** | **47** |
| 与出货 map **不一致** | **2** |

不一致的:**id 37511**(`s_payGoldToContainer`,相似度 **0.82**)、
**id 47303**(`actorHasEquippedRangedWeapon_m`,**0.74**)。

### 122.2 判定:两条都用真机反汇编,出货 map 两次都对

**37511** —— 出货值 `0x5e5dd0` 是**薄转发函数**,读 `rcx`/`rdx`/`r8d` 后尾调用,
正是客户端传的三个参数:

```asm
1405e5dd0:  48 8b f9   mov %rcx,%rdi      <-- this
1405e5de2:  41 8b f0   mov %r8d,%esi      <-- amount
1405e5de9:  48 8b da   mov %rdx,%rbx      <-- container
1405e5e0c:  e9 ...     jmp                <-- 尾调用
```

候选 `0x5e3a50` **完全不读 `rdx`** —— 是另一个函数。

**47303** —— 出货值 `0x7bcb40` 测**形态类型 0x29** 加一个 `<= 7` 范围检查,
正是客户端按 `bool Actor::()` 调用的「是否装备远程武器」:

```asm
1407bcb66:  80 78 1a 29   cmpb $0x29,0x1a(%rax)   <-- form type 0x29
1407bcb75:  83 f8 07      cmp $0x7,%eax
```

候选 `0x7bcaf0` 测的是**另一个**形态类型。

> **教训**:两个候选**都是合法的函数起点**(§118 那个「不是函数起点」的判据
> 在这里**不适用**)—— 也就是说**「地址合法」远不足以判定「地址正确」**。
> 要判定正确,只能问**客户端怎么调用它**(参数个数、返回类型、语义),
> 再去反汇编里核对。§118 与本节是同一类判定的两个难度:
> 那里是「指向函数内部」,一眼可辨;这里是**两个都像**,
> 只能靠**调用约定与语义**区分。

### 122.3 这不是运气:**每一条不一致都落在低相似度区间**

| 相似度 | 与出货 map 一致 |
| --- | --- |
| **≥ 0.90** | **31 / 31 全部一致** |
| 0.80 ~ 0.90 | 6 / 7 |
| < 0.80 | 10 / 11 |

**≥0.90 那一档全票一致**,而两条不一致都在最低的两档。
所以这两条否决是**匹配器的低置信度猜测被人工判定推翻**的结果 ——
出货 map 的处置是对的,而**这个规律本身值得固化**:它是「为什么可以相信
那 47 条」的依据。

### 122.4 门禁:把「已否决」登记下来,并固化那条相关性

`Tools/Scripts/check_capstone_report.py`:

| 检查 | 为什么 |
| --- | --- |
| 每条候选**要么与 map 一致,要么登记在 `REJECTED` 并写明理由** | 第三条不一致不可能无声出现 |
| **反向查**:登记为已否决的条目若 map 现在同意了候选 | 免得豁免活得比理由长 |
| **≥0.90 档若有任何不一致就报错** | 本节的两条否决**正建立在「高相似度从不被推翻」之上**;它一旦不成立,推理要重新推导 |

第三条是本节最该留下的:**它把「本节结论所依赖的前提」也变成了被检查的对象**。
一条豁免如果只写「这两条例外」,那么当世界变化时它不会告诉你;
写成「这两条例外 + 它们成立的前提」,前提被破坏时就会报错。

### 122.5 变异测试

| 变异 | 门禁输出 |
| --- | --- |
| 在高相似度档(1.0)造一条不一致 | `16005 (s_isQuestObject): the report resolved 0xf423f (similarity 1.0) but the shipped map has 0x1d6ba0; every candidate at or above 0.9 has been accepted, so this one disagreeing is new` **+** `that band was previously unanimous, so the rejections recorded here need re-deriving` |
| 在低相似度档造**第三条**未登记的不一致 | `68545 (setEnabled): ... (similarity 0.74) but the shipped map has 0xc11c60. Either the map is wrong or the candidate was rejected - if rejected, record it in REJECTED with the reason` |
| 让 map 与某条已否决的候选一致 | `37511: this gate records it as rejected (...), but the map now agrees with the candidate; drop the entry rather than leaving it stale` |
| 未变异的对照组 | **通过** |

### 122.6 验证与未做

- 20 条门禁全绿(10 条 `Tools/Scripts/` + 10 条 `Code/plugins/tools/`)。
- 10 条 `Tools/Scripts/` 门禁**都确认被 CI 调用**,且都在 `push` 与
  `pull_request` 两条 `paths` 列表里。
- **证据文件本身未被修改**(`git diff` 为空)—— 本节修的是**「不一致无人对照」**,
  不是那些数值:两个数值经真机反汇编判定**都是对的**。
- 隔离副本已删除;`git status` 只剩预期的新文件。
- **本机不编译、不跑游戏**(§0/§1):本节的反汇编证据来自随包的 1.5.97
  `SkyrimSE.exe`,证明的是**函数身份**(调用约定与语义),不证明运行时行为。

> **教训**:当证据与出货物不一致时,**先判定谁对,再决定门禁怎么写** ——
> 而判定的手段往往**不是读数值**,是**问一个外部权威**
> (§118 问地址库「这是不是函数起点」,本节问客户端「怎么调用它」+ 反汇编)。
> 另外:**两处都是合法地址**的情况比「指向函数内部」难得多,
> 因为没有任何机械判据能一眼看出错 —— 唯一的路是**核对语义**。

---

## 123. 2026-10-11 场次:地址库回退「取第一个」—— 而 1.5.x 之间 **98.7% 的 id 指向不同地址**

> **锚点**:`Code/skse_client/main.cpp`(`ScanLibraryVersion`)、
> `Code/client/VersionDb.h`(`Load`)。
> 门禁:`Tools/Scripts/check_library_scan.py`。
> **本节是 §111–§122 之后第一次回到「改代码」而不是「加门禁」。**

### 123.1 判据:转到**加载路径** —— 哪个运行时、按哪个版本被加载

前几节的判据都在「证据与出货物是否一致」。本节换到**运行时的入口**:
`Code/skse_client/main.cpp` 用三步解析游戏版本,而第三步是**扫目录**:

```cpp
for (auto it = std::filesystem::directory_iterator(plugins, ec); ...)
{
    if (sscanf_s(name.c_str(), "versionlib-%d-%d-%d-%d.bin", ...) == 4 || ...)
        return buf;                    // 第一个就返回
}
```

它的注释写着前提:**「Data/SKSE/Plugins 为已安装的游戏版本只带一套 bin」**。

**这个前提是错的,而且可以在本机直接反驳**:

| 位置 | `version-*.bin` | `versionlib-*.bin` |
| --- | --- | --- |
| 本仓库自带 | 10 | 13 |
| 装好的游戏 | **10**(`1-5-3` … `1-5-97`) | 0 |

而**目录枚举顺序是未定义的**。装好的游戏按名字排序第一个是
`version-1-5-16-0.bin`。

### 123.2 后果:不是「差一点」,是 98.7% 的 id 指向别处

把 1.5.16 与 1.5.97 逐 id 对比:

| | |
| --- | --- |
| 两者共享的 id | **777,719** |
| 解析到**相同** RVA | 10,075(1.3%) |
| 解析到**不同** RVA | **767,644(98.7%)** |

这正是 `GamePatch.h` 早就写下的那条:

> 十个不同的 1.5.x 地址库随客户端发布,而它们的代码**不可互换**,
> 所以量在 1.5.97 上的偏移不能只因为「都算 legacy」就用到 1.5.3 上。

**同一条事实,两个地方各写了一次,只有一处被守住了**(`GamePatch::Site`
有 `legacyMeasuredOn` 门禁,而这里没有)。

### 123.3 实测确认这条路径会被走到

装好的游戏里 `version-*.bin` 有 10 个,按名字排序第一个是 `version-1-5-16-0.bin`。
而随包的 `st_boot.log` 显示:

```text
boot: entered, exe version='1.5.97.0', resolved='1.5.97.0'
```

**这次是对的 —— 但只是因为 exe 版本资源能读**。
而第三步回退存在的**全部理由**就是「重打包与降级补丁会剥掉或伪造版本资源」。
**换句话说:这条回退路径恰好只在它最可能出错的时候才被用到。**

### 123.4 修法:两个问题,需要不同的信息

| 问题 | 依据 | 为什么不能猜 |
| --- | --- | --- |
| **哪个族**(pre-AE / AE) | 文件名:树里每个 `version-*.bin` 都是 1.5.x,每个 `versionlib-*.bin` 都是 1.6.x/1.7.x。而**这个翻译单元被编译两次**,legacy 构建带 `SKYRIM_TARGET_LEGACY` | 「取全局最高版本」会**两个问题都不回答** |
| **族内哪个版本** | 取**最高**。更新的库是**惰性**的(客户端只解析它认识的 id),更旧的**解析到错地址** | — |

> **教训(本节最该留的一条)**:**我第一版就写错了,并在提交前当场纠正。**
> 第一版取的是「**全局最高版本**」。在装好的 1.5.x 游戏上它恰好给出 `1.5.97.0`
> —— **看起来完全正确**。但在 1.6.1170 的游戏上它会给出 **`1.7.104.0`**,
> 加载一个 1.7 的库。
> **一个在手上这台机器上表现正确的规则,不等于它是正确的规则。**
> 纠正的办法是问:**这个规则在另一种输入下会给出什么?**
> 两个族在 `major.minor` 上**不相交**,所以「族」是一个**独立于版本高低**的维度,
> 不能靠排序回答。

### 123.5 顺带查出的死文件:`versionlib-1-6-1170-0-1.bin`

| | `versionlib-1-6-1170-0.bin` | `versionlib-1-6-1170-0-1.bin` |
| --- | --- | --- |
| 字节 | 795,129 | 795,210 |
| 头部声明版本 | 1.6.1170.0 | 1.6.1170.0 |
| 条目 | 428,461 | 428,309 |
| 共享 id 里解析到**不同** RVA | — | **419,836 / 425,223 = 98.7%** |

自 `28eca3fb`(「适配不同版本」)起被签入,**没有任何代码路径能打开它**:

- `VersionDb::Load` 只格式化 `versionlib-%d-%d-%d-%d.bin` —— **四个数**;
- `ScanLibraryVersion` 也只解析四个;
- 而 `sscanf_s` 对**五段名仍然返回 4** ——
  四个转换在字面量 `.bin` 失配**之前**就成功了。
  所以两个文件**都解析成 `1.6.1170.0`**,而只有四段那个能被打开。

**它是有意加进来的**(与正本同一提交),所以**不删** ——
门禁把它登记为「**不可达**」并写明理由,而不是让它继续以一个「像库的东西」存在。

### 123.6 门禁:四条检查,其中第 4 条查的是**前提本身**

`Tools/Scripts/check_library_scan.py`:

| 检查 | 为什么 |
| --- | --- |
| 保留**最高候选**,不返回第一个匹配 | 目录枚举顺序未定义 |
| **按族过滤**,族来自构建宏而非猜测 | 见 §123.4 的教训 |
| 构建**真的产出** `SKYRIM_TARGET_LEGACY=1` 的运行时 | 否则过滤条件在**每个**构建里都是假,legacy 会去读 AE 库名 |
| 磁盘上 `version-*.bin` **真的都是 1.5.x**、`versionlib-*.bin` 都不是 | **族过滤正建立在这个命名约定上** —— 前提被破坏时,过滤会**静默错**而不是漏 |

第 4 条与 §122.4 是同一个设计:**把结论所依赖的前提也变成被检查的对象**。

**变异测试**:去掉族过滤 → `computes a family but no longer filters on it` **+** 不可达文件提示;
去掉构建宏 → 追加「过滤条件在每个构建里都是假」;对照组通过。

### 123.7 验证与未做

- 21 条门禁全绿(11 条 `Tools/Scripts/` + 10 条 `Code/plugins/tools/`)。
- 11 条 `Tools/Scripts/` 门禁**都确认被 CI 调用**;新门禁的触发路径已含 `Code/skse_client/**`。
- `Code/skse_client/main.cpp` 花括号收支 0、预处理指令配对、`<cstdio>` 已在(用 `sscanf_s`/`sprintf_s`)。
- **本机不编译、不跑游戏**(§0/§1):本节证明的是**选库规则在两种族下都正确**,
  以及**那条回退路径的条件**(版本资源不可读)确实存在;
  **不证明**在真机上触发它之后游戏能正常起来。

> **教训**:写「取一个/取第一个」这类规则时,先问**容器里到底有几个** ——
> 而注释里那句「只有一个」**值得当场数一遍**:本次数出来是 23 个和 10 个。
> 另外:**一个在手上这台机器上表现正确的规则,不等于它正确**;
> 纠正的办法是问它在**另一种输入**下会给出什么。

---

## 124. 2026-10-11 场次:同一个版本查询写了三遍,而启动器那遍**少测一项**

> **锚点**:`Code/immersive_launcher/utils/FileVersion.inl`、
> `Code/client/VersionDb.h`、`Code/skse_client/main.cpp`。
> 门禁:`Tools/Scripts/check_version_queries.py`。

### 124.1 判据复用:§114/§120 的「一份定义变成多份」

前几轮用这条判据查过结构体的读写两侧(§114)、契约头的 6 个副本(§120)。
本轮顺着 §123 的加载路径往上走,发现**同一个资源查询有三份实现**:

| 实现 | 为什么不能共用 |
| --- | --- |
| `immersive_launcher/utils/FileVersion.inl` | 启动器那条链,不链接客户端 |
| `client/VersionDb.h` | 客户端自己的查找 |
| `skse_client/main.cpp` | SKSE 那条链 |

而 §123 顺带查出一件事:**启动器那条链一步回退都没有** ——

```cpp
LC.Version = QueryFileVersion(LC.exePath.c_str());
if (LC.Version.empty())
    DIE_NOW(L"Failed to query game version");
```

于是把三份的守卫摆在一起对照:

| 实现 | `ProductVersion` | `FileVersion` |
| --- | --- | --- |
| 启动器 | `len && ptr` | `len && ptr && *ptr` |
| `VersionDb.h` | `len && ptr && *ptr` | `len && ptr && *ptr` |
| `skse_client` | `ptr && *ptr` | `ptr && *ptr` |

**只有启动器那一份的 `ProductVersion` 少了 `*ptr`** ——
而**同一个文件里紧接着的 `FileVersion` 分支却带着它**。

> **教训**:一个文件**对同一个查询的两半给出不同判据**,是最强的自查信号。
> 它不需要外部参照物就能看出来 —— 这也正是 §114 的「读侧判空、写侧不判」的同一形状,
> 只是这次的「两半」是**同一个函数里的两个分支**。

### 124.2 后果:空串是一个「成功」的查询结果

`VerQueryValue` 可以**成功地**返回一个**空串**:键存在,值为空。
此时:

- 少了 `*ptr` 的分支判定为「拿到了版本」,**返回空串**;
- **第二个分支(那个存在的回退)永远不执行**;
- 调用方 `if (LC.Version.empty()) DIE_NOW(...)` → **启动失败**;
- 而错误信息说「Failed to query game version」—— **指错了原因**,
  `FileVersion` 就在那里可以读。

**修法**:两个键写成一张表、**共用同一个守卫**,四项测试完全一致:
查询成功 / 有长度 / 有指针 / **串非空**。

### 124.3 刻意**不**要求测长度

三份里有两份没测长度,而**它们是对的**:`VerQueryValueA` 会设置长度,
且**非空串本身就意味着长度非零**。

要求它会**把能工作的代码报成坏的** —— 而**假阳性会让判据被放弃**(§115.3)。
所以门禁只强制那一条**真正缺失时会出错**的测试:**拒绝空串**。

### 124.4 顺带核对(三项,都不是缺陷)

| 检查 | 结论 |
| --- | --- |
| `VerQueryValue` 与 `GetFileVersionInfoW` 混用 A/W | **合法**。全仓未定义 `UNICODE`,裸名解析成 `A`;`GetFileVersionInfoW` 只决定**资源块怎么读**,`VerQueryValueA` 始终返回 `char*` |
| 出参类型 `(void**)` vs `(LPVOID*)` | Windows 上 `LPVOID` 就是 `void*`,**同一类型** |
| `FileVersion.inl` 是不是有两个文件 | **只有一个**。`Launcher.cpp` 写 `"Utils/FileVersion.inl"`,git 跟踪小写 `utils/` —— Windows 大小写不敏感,指的是同一个文件 |

第二、三项是**查过才知道不是问题**的:它们的形状都像缺陷,
而「像缺陷」不等于缺陷 —— 这一类要**当场验证**,不能靠印象放过。

### 124.5 门禁:三条,其中第三条在做变异测试时才改成失败

`Tools/Scripts/check_version_queries.py`:

| 检查 | 为什么 |
| --- | --- |
| 每处被守卫的 `VerQueryValue` **必须测「串非空」** | 这是本次缺的那一项 |
| **每个被读到的键都被某个守卫覆盖** | 免得新增键却不加守卫 |
| **三份读的键集合必须一致,且是失败而非提示** | 少读一个键就是**少了回退** |

第三条**第一版写成「提示」**,是我在做变异测试(删掉某个实现的回退)时发现
**它照样通过**才改成失败的 —— **少了回退是缺陷,不是提醒**。
这与 §113.3 的「清单里的东西贡献 0 项即失败」是同一条。

守卫是**按括号配平**取出来的,不是正则抓一行:条件跨行,且调用内部还有括号。

### 124.6 变异测试

| 变异 | 门禁输出 |
| --- | --- |
| **还原原始缺陷**(`ProductVersion` 去掉 `*ptr`) | `the ProductVersion guard is '&& productVersionLen && productVersion', which does not test non-empty. An empty ProductVersion then counts as a version, and the fallback key is never tried` |
| 删掉某个实现里的 `FileVersion` 回退 | `reads ['ProductVersion'] while ... reads ['ProductVersion', 'FileVersion']; a copy with fewer keys has no fallback` |
| 未变异的对照组 | **通过** |

### 124.7 验证与未做

- 22 条门禁全绿(12 条 `Tools/Scripts/` + 10 条 `Code/plugins/tools/`)。
- 12 条 `Tools/Scripts/` 门禁**都确认被 CI 调用**;新门禁的触发路径已含
  `Code/immersive_launcher/**`。
- `FileVersion.inl` 花括号收支 0。
- **本机不编译、不跑游戏**(§0/§1):本节证明的是**三份守卫测的是同一组条件**,
  **不证明**真机上存在一个 `ProductVersion` 为空的版本块 ——
  那种块来自重打包与降级工具,而这正是三份回退**存在的理由**。

> **教训**:查「同一件事写了几遍」时,**先看每份的判据是否一致**,
> 而最快的入手点是**同一份内部的两半** ——
> 如果一个函数对自己的两个分支给出不同判据,那多半是漏了一项,
> 而不是有意为之。本次就是这样找到的。

---

## 125. 2026-10-11 场次:**形似缺陷的三处,逐条查清后都不是** —— 以及两条门禁范围正确

> **锚点**:`Code/client/VersionDb.h`、`Code/immersive_launcher/loader/ExeLoader.cpp`、
> `Code/immersive_launcher/utils/FileVersion.inl`。
> **本节没有修复** —— 它记录的是**核对结果**。

### 125.1 为什么「查了但没缺陷」也值得记

前几节各找到一个缺陷。本轮把 §123/§124 那条加载路径**走完**,
遇到三处**形状都像 bug** 的地方。它们只有**查过**才能说不是,
而「像缺陷」恰恰是最容易让人**凭印象写进报告**或**凭印象跳过**的东西 ——
两种都错。所以本节逐条给出**查法**与**判据**,而不是结论。

### 125.2 ① `_base = GetModuleHandleA(NULL)` 在手动映射下对不对?

`VersionDb.h` 在**三处**把镜像基址设成 `GetModuleHandleA(NULL)`(L264、L443)。
而启动器是**手动映射** exe 的(`ExeLoader::Load`),所以问题很具体:

> 手动映射的镜像**不是**进程的 exe 模块,那么 `GetModuleHandleA(NULL)`
> 会不会返回**启动器**的基址,从而让每个地址都算错?

**查法**:读 `ExeLoader::Load`:

```cpp
bool ExeLoader::Load(const uint8_t* apProgramBuffer)
{
    m_pBinary = apProgramBuffer;
    m_moduleHandle = GetModuleHandleW(nullptr);                 // <- 启动器自己
    ...
    auto* sourceNtHeader = GetTargetRVA<IMAGE_NT_HEADERS>(...); // 启动器自己的头
```

**结论:不是缺陷。** 启动器**就地覆盖自己的镜像头**
(`GetTargetRVA` = 启动器自己的 RVA),所以**启动器模块就是游戏镜像** ——
两种启动方式下 `GetModuleHandleA(NULL)` 都返回正确的基址。
这个设计的效果是:「基址从哪来」这件事**只有一个答案**。

> **教训**:`GetModuleHandle(NULL)` 的含义取决于**谁在跑**。
> 在「手动映射」这种架构里它**看起来**必然错,而实际对不对
> 取决于**映射是不是就地覆盖** —— 这是一个**必须读实现**才能回答的问题,
> 不能靠「手动映射 → 基址肯定不对」这条推理跳过。

### 125.3 ② 两种格式读模块名的方式不同,是不是抄错了?

| 格式 | 读模块名 |
| --- | --- |
| format 1 / 2 | **长度前缀**(`tnLen` 然后 `tnLen` 字节) |
| **format 5** | **固定 64 字节零填充** |

两种读法**不同**,看起来像其中一处抄错。

**查法**:直接读文件头。`versionlib-1-7-104-0.bin`(2,263,132 字节):

```text
format=5   version=1.7.104.0
bytes 20..84 = "SkyrimSE.exe" 后跟 52 个 \0        <- 固定 64 字节,零填充
ptrSize@84=8    reserved@88=0    count@92=565759
96 + 4 * count = 96 + 2263036 = 2263132 == 文件大小   <- 精确吻合
```

**结论:不是缺陷。** format 5 的模块名**确实是固定 64 字节零填充**,
format 1/2 确实是长度前缀 —— **两种格式在这件事上本来就不同**。

> **教训**:`96 + 4*count == filesize` 这条**算术恒等式**比读代码更硬:
> 它一次同时验证了「头部 96 字节」「表项 4 字节」「count 的位置」三件事。
> **当能从文件本身推出一个恒等式时,优先用它**,而不是逐行读解析代码。

### 125.4 ③ `FileVersion.inl` 是不是有两个文件?

上一轮遗留的疑问:`Launcher.cpp` 写 `#include "Utils/FileVersion.inl"`,
而 git 跟踪的是小写 `utils/FileVersion.inl`。

**查法**:`git ls-files` 只有一个,`Get-ChildItem -Recurse -Filter FileVersion.inl`
也只有一个。**Windows 大小写不敏感**,两边指同一个文件。
(顺带:两个哈希相同,因为本来就是同一个文件。)

### 125.5 ④ 两条门禁的范围按 §119 规程核对,都正确

| 门禁 | 它声称的规则 | 核对结果 |
| --- | --- | --- |
| `check_ostim_support_message.py` | 「拒绝信息必须与选择器一致」 | 选择器接受 **3** 种形式(`7.4.0.3` / `7.5.0.2` / `7.5.1.x`),信息**恰好**列这 3 种;两处都在**同一个文件**里,所以单文件范围**是对的** |
| `check_skse_bootstrap.py` | 「bootstrap 必须能被本包支持的 SKSE 版本加载」 | 它**同时**查 `SKSEPlugin_Version`(2.2.x 按导出名查找)与 `SKSEPlugin_Query`/`Load`(2.0.x / 1.5.97 用) |

第二条值得单独说。它的 docstring 记着一个**只在 AE 上出现、在 1.5.97 上完全看不见**
的缺陷:SKSE 2.2.x 按导出名 `SKSEPlugin_Version` 查找,
而那个 dll 只导出了旧式的 `SKSEPlugin_Query` —— 于是
`SKSEPlugin_Load` 从不执行、运行时从不部署、F2 菜单从不打开,
**而 esp 仍在创造菜单里、logo 仍显示在主菜单上**(所以看起来「装上了」)。
它并说明**为什么查源码而不是二进制**:

> 出货的构建跑在 CI 上,而这个失败**在本机的 1.5.x 上是看不见的**。

**这正是本目标「1.5.x / 1.6.x / 1.7.x 兼容性」的核心形状**:
一个版本上完全正常、另一个版本上静默失效。

### 125.6 验证

- 22 条门禁全绿(12 条 `Tools/Scripts/` + 10 条 `Code/plugins/tools/`)。
- 本轮**未改动任何代码**(§125.2–§125.4 三处结论都是「不是缺陷」,
  §125.5 两条门禁范围正确)—— 所以没有新门禁,也没有 `git diff`。
- **本机不编译、不跑游戏**(§0/§1)。§125.2 的结论来自**读实现**,
  §125.3 来自**文件本身的算术恒等式**,§125.4 来自**文件系统与 git 的清单**。

> **教训**:审计的价值**不只在找到的缺陷**。一份「查过、不是缺陷、判据是这些」
> 的记录,能阻止下一个人**重查同一个地方**,也能阻止他**凭形状下结论**。
> 而「形似缺陷」是最危险的一类:它既容易被**误报**(凭印象写进报告),
> 也容易被**误放**(凭印象跳过)。两者都只有**当场验证**才能避免。

---

## 126. 2026-10-11 场次:契约清单**早就写在仓库里**,而我上一轮手写了一份 —— 漏掉 2 个消费者

> **锚点**:`Code/plugins/STRPM/contract.json`、
> `Code/plugins/tools/check_contract_copies.py`。
> **同一错误第四次**(§113 / §116 / §119 / §126)。

### 126.1 事件:仓库里有规则,门禁里有手写清单

查 `Code/plugins/STRPM/` 时发现一个此前没注意的文件 **`contract.json`** ——
一份**机器可读的契约清单**:

```json
{
  "header": "Code/plugins/STRPM/include/.../STRPluginMessagingAPI.h",
  "headerSha256": "8fdcf481…",
  "interfaceVersion": 2,
  "exportNames": [...],
  "interfaceConstants": { ... },
  "interfaceTypes": [...],
  "consumers": {
    "AnimSyncTogether":     { "root": "plugins/AnimSyncTogether",     "vendored": "include/..." },
    "DAVSyncTogether":      { ... },
    "IEDSyncTogether":      { ... },
    "MorphSyncTogether":    { "root": "plugins/MorphSyncTogether",    "vendored": "src/STRPMCompat.h" },
    "OStimTogether":        { "root": "plugins/OStimTogether",        "vendored": "src/STRPMApi.h" },
    "STRPluginMessagingAPI":{ ... },
    "TradeTogether":        { ... }
  }
}
```

**它列了 7 个消费者**,而 §120 的门禁**硬编码了 6 个副本**。
逐条对照后,**两个消费者从未被任何门禁读过**:

| 消费者 | 它 vendor 的文件 |
| --- | --- |
| `MorphSyncTogether` | `plugins/MorphSyncTogether/src/STRPMCompat.h` |
| `OStimTogether` | `plugins/OStimTogether/src/STRPMApi.h` |

> **教训(本次最重的一条)**:§113/§116/§119 的教训是「扫描范围要按规则搜出来」。
> 而这次**规则早就在仓库里了** —— `contract.json` 就是那份规则,
> 我上一轮却**凭记忆手写了一份清单**。
> 所以这条要升级成:**写门禁前先问「这个规则有没有已经被写下来的地方」**。
> 手写清单不只是可能漏 —— 它**绕过了仓库里已有的那份权威**。

### 126.2 第二处:**只认一种调用约定拼写**,于是成员数被数成 0

门禁的成员提取正则只认 `STRPM_CALL`:

```python
re.finditer(r'STRPM_CALL\s*\*\s*(\w+)\s*\)', body)
```

而 `OStimTogether` 直接写 **`__cdecl`**。在 x64 上两者**完全等价**
(`STRPluginMessagingAPI.h:7` 就是 `#define STRPM_CALL __cdecl`),
但正则匹配不到 —— 于是那个文件的**函数指针成员被数成 0**。

**「数成 0」比「数错」更危险**:

| 情况 | 门禁行为 |
| --- | --- |
| 数**错**(数量不同) | **报错** |
| 数成 **0** | 没有东西可比对 → **通过** |

**这是假通过,不是假失败。** 而假通过会让一个**看起来在守着的门禁什么都不守**。

### 126.3 修法:三条都指向「规则来自契约」

1. **消费者清单改为从 `contract.json` 读**(`copies()`):正本取 `header`,
   其余取每个消费者的 `root` + `vendored`;
2. **调用约定宏与直接拼写都认**(`STRPM_CALL|__cdecl|__fastcall|__stdcall`);
3. **新增 `headerSha256` 校验** —— 契约公布了正本的哈希(供消费者判断
   自己 vendor 的是不是同一修订版),而**没有任何东西查它**。

覆盖从 **6 个副本 / 20 个布局**升到 **8 个副本 / 25 个布局**。

### 126.4 查清的两件事:都不是缺陷

| 疑问 | 查法与结论 |
| --- | --- |
| `headerSha256` 报不匹配 | **是行尾问题**。记录的是 **LF 形式**(7,424 字节 = git 存的形式),工作区在 Windows 上是 **CRLF**(7,686 字节),内容相同。所以门禁**按 LF 归一化后再算哈希**,并在 `--list` 里**同时打印三种**(记录值 / LF / 磁盘原样),让归一化**看得见**而不是隐含 |
| `OStimTogether` 用 `__cdecl`,正本用 `STRPM_CALL` | **不是 ABI 差异**。x64 上 `STRPM_CALL` 就是 `__cdecl`(`#define STRPM_CALL __cdecl`) |

> **教训**:哈希与**行尾**是一对老搭档 —— 一个**按字节**算的东西遇到
> **按平台改写行尾**的检出一,必然在某个平台上报错。
> 处理方式不是「跳过这个检查」,而是**明确选一种归一化,并把它打印出来**。

### 126.5 自我纠正:加宽覆盖后 4 条报错**全是假阳性**

加宽后门禁要求每个消费者声明**全部**常量与类型。而消费者**只声明它用到的**
是合法的 —— 契约说明哪些结构体**存在**,不是每个插件都用全部。

核对确认那两个消费者**从未提到**那些名字(`TransportInterface`、`RuntimeStatus`、
`kTransportInterfaceVersion`、`ProxyMappingEvent` 等)。所以规则改成:

| 情况 | 判据 |
| --- | --- |
| **用到却没声明** | **报错** —— 文件只是碰巧因为别的头先被包含才编过 |
| **没用到** | **不要求** |

否则「把能工作的代码报成坏的」,而**假阳性会让判据被放弃**(§115.3)。

### 126.6 变异测试:三个变异都打在**上一版从不读**的那两个副本上

| 变异 | 门禁输出 |
| --- | --- |
| 在 `OStimTogether` 里交换两个成员 | `OStimTogether/Interface declares 8 member(s) but the runtime copy declares 7: … Every call after the difference dispatches through the wrong slot` |
| 在 `MorphSyncTogether` 里改常量 | `MorphSyncTogether sets kInterfaceVersion = 9 but the runtime copy sets 2` |
| 把 `headerSha256` 改成全 0 | `contract.json records headerSha256 000… but the canonical header hashes to 8fdcf481… (LF form). A consumer cannot tell which revision it vendored` |
| 未变异的对照组 | **通过** |

### 126.7 验证与未做

- 22 条门禁全绿;`check_contract_copies.py` 现在 **8 个副本 / 25 个布局**,
  且已在 CI 里(`Code/plugins/**` 与 `plugins/**` 都在触发路径上)。
- 隔离副本已删除;`git status -- plugins/` 只有 ` m` 子模块标记 ——
  **真实子模块内容未被改动**。
- **本机不编译**(§0/§1):本节证明的是**8 个副本的 ABI 一致**,
  **不证明**它们在 MSVC 下的实际布局相同。

> **教训**:本节是**同一错误第四次出现**(§113 一个插件、§116 一个目录、
> §119 一个文件、§126 手写清单),所以教训要写成可执行的两步:
> **①先问「这条规则有没有已经被写下来的地方」(`contract.json` 就是);
> ②再问「规则本身有没有被这个门禁完整覆盖」。**
> 而 §126.2 补了另一条**独立**的教训:**「数成 0」是假通过** ——
> 提取规则写窄了,门禁不会报错,它会**安静地什么都不比**。

---

## 127. 2026-10-11 场次:安装器元数据的版本号**落后四个版本** —— 而它自己的注释就写着「要保持一致」

> **锚点**:`GameFiles/Skyrim/fomod/info.xml`。
> 门禁:`Tools/Scripts/check_package_metadata.py`。

### 127.1 判据:§126 的新问法 —— 「还有哪些机器可读的规则文件没有门禁读?」

§126 发现 `contract.json` 是一份**没人读的规则文件**。本轮把这个问法**系统化**:
列出仓库里所有 `*.json` / `*.toml` / `*.yml`(排除 UI 与依赖清单),逐个问「谁读它」。

三条线索都查了,结论如下:

| 对象 | 结论 |
| --- | --- |
| `Code/plugins/plugins.json`(20 KB,7 插件 + 1 前置) | 被 **5 处**读,其中 `check_ostimng_build.py` 是门禁;**内部交叉引用逐条查过,全部解析**(`requires` 指向存在的 id、8 个子模块目录都在) |
| `Code/plugins/tools/merge_fomod.py` | 由 manifest **生成**向导 XML;有 `check` 模式**且已接进 CI**;实跑 `FOMOD OK … 55 folder sources` |
| **`GameFiles/Skyrim/fomod/info.xml`** | **MO2 向导顶部显示的元数据 —— 没有门禁读它** |

### 127.2 缺陷:注释说「保持一致」,而它落后四个版本

```xml
<!-- The Version element is stamped from the release tag while the release zip
     is assembled (release.yml); the number below is only what a hand-made
     package shows. Keep it in step with the newest tag. -->
<Version>1.1.5</Version>
```

**注释自己写着「要与最新 tag 保持一致」**,而本仓库最新 tag 是 **v1.1.9** ——
**落后四个版本**。

**为什么这是缺陷,而不是「反正发布时会被 stamp」**:

1. 注释自己说了 stamp 只在**发布 zip 组装时**发生,所以**手工打包**用的就是这个数;
2. 而手工打包是**这个仓库支持的路径** ——
   `Tools/Packaging/New-STNModPackage.ps1` 接受 `-Version` 参数,不传就用文件里的值;
3. 最直接的是:**任何读这个文件判断「这个包是哪个版本」的人,会得到四个版本前的答案**。

> **教训**:**「保持一致」是一句注释,而注释不会运行。**
> 本次的缺陷**不是没人写下这条约定** —— 约定写得清清楚楚。
> 缺的是**让约定被执行的那一步**。
> 所以「文件里有一句『记得同步 X』」应当被当成**待加门禁的信号**,
> 而不是当成已经处理过的说明。

### 127.3 修法与门禁:三条,第 1 条有个容易踩的坑

`Tools/Scripts/check_package_metadata.py`:

| 检查 | 说明 |
| --- | --- |
| 版本号 = **本仓库自己那条 tag 序列**的最新值 | **不能取「全局最新 tag」** |
| 描述必须列出运行时**真正接受**的游戏版本,且**不得声称运行时拒绝的版本** | 这是玩家安装前读到的兼容性声明 |
| 向导要显示的字段非空(`Name` / `Author`) | 空字段正是这个文件当初被加进来的原因 |

**第 1 条的坑**:tag 命名空间**与上游共享** —— 上游发 v1.0.x…v1.8.x,
而本仓库 merge 了那些提交,所以本地 clone 里**两家的 tag 都在**
(`git tag` 会同时列出 `v1.8.2` 与 `v1.1.9`)。取「全局最新」会得到 **v1.8.2**,
那是**上游的**版本号,与本仓库无关。

所以这条沿用 `check_docs.py` 里**已经存在的** `OWN_TAG_SERIES`(`1.1.`)与**同一个理由** ——
**这正是 §126 的教训在起作用**:规则已经写在仓库里,就用它,不要再推一遍。

**第 2 条的两个方向都查**,因为它们是同一个声明的两面:

| 方向 | 后果 |
| --- | --- |
| 描述**少了**某个版本 | 那个版本的玩家被告知「这个包不是给你的」 |
| 描述**多了**某个版本 | 承诺一个跑不起来的版本(运行时的前缀门禁会拒绝) |

### 127.4 变异测试

| 变异 | 门禁输出 |
| --- | --- |
| **还原原始缺陷**(版本改回 `1.1.5`) | `says version 1.1.5 but the newest tag of this repository's series is v1.1.9. A hand-made package, or anyone reading the file, then gets a version from an older release` |
| 从描述里去掉 `1.6.x` | `does not name the supported game version prefix(es) ['1.6.']; the runtime accepts ['1.5.', '1.6.', '1.7.'], so a player on one of them is told the package is not for them` |
| 声称支持 `1.9.x` | `claims support for ['1.9'], which the runtime's version gate does not accept (['1.5', '1.6', '1.7'])` |
| 把 `Author` 置空 | `has no <Author>; the wizard shows that field blank` |
| 未变异的对照组 | **通过** |

### 127.5 验证与未做

- 23 条门禁全绿(13 条 `Tools/Scripts/` + 10 条 `Code/plugins/tools/`)。
- 13 条 `Tools/Scripts/` 门禁**都确认被 CI 调用**;新门禁的触发路径已含
  `GameFiles/Skyrim/fomod/**`。
- 隔离副本已删除。
- **本机不编译、不跑游戏**(§0/§1):本节证明的是**元数据与 tag 序列、与运行时的
  版本门禁三者一致**,**不证明** MO2 实际显示这个版本号 —— 那要装一次包。

> **教训**:本轮验证了 §126 定下的新问法**确实能产出结果** ——
> 「还有哪些规则文件没有门禁读」问了一遍,得到一条活缺陷。
> 而这条缺陷的形状值得单独记:**约定写下来了,但没被执行**。
> 因此下一步的问法应该是:
> **「仓库里还有哪些『记得同步 X』『要保持一致』『不要忘记 Y』的注释?」**
> —— 每一句都是一条**声称被维护、实际无人守**的不变量。

---

## 128. 2026-10-11 场次:每帧的 delta **没有上界** —— 而仓库对**不可控数值**早有整套约定

> **锚点**:`Code/client/World.cpp`、`Code/server/GameServer.cpp`、
> `Code/encoding/WireLimits.h`、`Code/tests/encoding.cpp`。
> **本节是 §123/§124 之后又一次改代码。**

### 128.1 先走 §127 定下的路,但没走出来

§127 的教训建议下一步去找**「记得同步 X」类注释**。本轮先照做了,
扫了 276 处候选,逐条读下来**没有一条指向未守的活不变量** ——
它们要么是我自己刚写的文字,要么是散文里的 `mirror`/`same as`,
要么是变量名里的 `duplicate`。

**所以换了一条线**:查**服务端主循环**。

> **教训**:一条建议性的「下一步」不保证有产出。
> 值得记的是**怎么判断它已经走完了**:本次的判据是
> 「276 个候选逐条读过,全部能归入『散文』『变量名』『已处理』三类」。
> **说「没找到」时要说清扫了什么、怎么判的** —— 否则下一个人会重扫一遍。

### 128.2 缺陷:delta 测的是真实时间,而真实时间会因为**进程内卡住**变大

服务端:

```cpp
const auto cDelta = cNow - m_lastFrameTime;
const auto cDeltaSeconds = std::chrono::duration_cast<...>(cDelta).count();
dispatcher.trigger(UpdateEvent{cDeltaSeconds});
```

客户端 `World.cpp` 是**同一个形状**,而且它的注释**自己说出了机制**:

```cpp
// A skipped call leaves m_lastFrameTime untouched, so the next accepted one
// measures its delta from the last real update.
if (m_hasTicked && cDelta < std::chrono::milliseconds(8))
    return;
```

**只挡下界,不挡上界。** 而真实时间的测量**不会因为进程内卡住而变小,只会变大**:
挂起机器、调试器断下、长页错误、一次 hitch —— 都会在下一 tick 送来一个巨大的值。

服务端的循环是 **`while` 里没有任何 sleep**(`DediRunner::RunGSThread`),
所以 delta 完全取决于两次迭代之间**真实**过了多久。

### 128.3 判据:仓库对「不可控数值」早有约定,而 delta 是漏掉的那个

§111/§114 的做法是**先问仓库里有没有已有的判据**。查下来:

| 查法 | 结果 |
| --- | --- |
| 全仓搜 `std::min`/`clamp` 作用在 delta 上 | **0 处** |
| 全仓搜 `kMaxDelta`/`kMaxFrame` 类常量 | **0 处** |
| 而**线上来的浮点**有整套约定 | `WireLimits::ClampFinite`、`kMaxHealthDelta`、`TimeModel::kMaxTimeScale` |

**也就是说:这个仓库已经建立了「不可控的数值必须有界」的约定,
而 delta 是唯一漏掉的那个** —— 而它比线上来的值**更**不可控,
因为它来自真实时间,连对端都不需要出错。

> **教训**:判据不是「这个值看起来会不会太大」,而是
> **「同一仓库里对同类不可控数值是怎么处理的,这里为什么不一样」**。
> 后者能给出**方向**(该加什么界)和**先例**(界放哪里),前者只能给出怀疑。

### 128.4 消费方逐个查过

| 消费方 | 怎么用 delta | 结论 |
| --- | --- | --- |
| `CharacterService::ApplyCachedWeaponDraws` | `m_timer += Delta` 再与 **0.5s / 2.0s** 比较 | 一个巨大的 delta 直接跳过等待 |
| `CalendarService::HandleUpdate` | 读 delta,但推进用的是 `GetTick()` 差值 | **安全** |
| `RunnerService::OnUpdate` | 忽略 delta | **安全** |
| 服务端 `ScriptService::OnUpdate` | `CallEvent("onUpdate", acEvent.Delta)` | **原样交给 Lua** |

`CalendarService` 是**查过才知道安全**的那一类:它接收 delta 却不用它。

### 128.5 修法:界放 `WireLimits.h`,两处测量点都应用

`kMaxFrameDeltaSeconds = 1.0` 与 `ClampFrameDelta()` 加在 `WireLimits.h` ——
**该仓库放这类常量的地方**,而不是新造一个头。

**上界取 1 秒的理由**(两条都是可验证的,不是「感觉合适」):

1. **在正常服务端上不可能触发**:客户端最快 8ms 一 tick,服务端循环无 sleep,
   诚实的一 tick 是毫秒级 —— 所以这个界**不会改变任何正常行为**;
2. **1 秒正是消费方思考的单位**:武器拔出计时器等 0.5s / 2.0s,
   日历按游戏分钟推进 —— 所以截断后的值仍然意味着「大约过了一秒」,
   而不是「什么都没发生」。

**截断而不是丢弃**,理由是**订阅者本来就已经容忍漏 tick**:
客户端的 `m_hasTicked` 路径、服务端的事件循环都会丢工作;
而**丢掉整个 update 会跳过 runner 的 drain**,那正是排队工作所依赖的。

**非有限值与负数一律归零** —— 与 `ClampFinite` 存在的理由相同
(NaN 会在下游每一次比较里存活),而「时钟倒退」不是负的经过时间。

### 128.6 单元测试:22 → 23 个用例

`[encoding.frame_delta]`,按仓库既有风格逐条断言:

| 断言 | 为什么 |
| --- | --- |
| 8ms / 100ms / 0 **原样通过** | 否则这个界会改变**每个**正常服务端的行为 |
| 边界两侧(1.0 通过,1.0001 截断) | 边界本身要被钉住 |
| 30s / 7200s 截到 1.0 | 挂起被限住 |
| `kMaxFrameDeltaSeconds < 2.0` | 否则武器拔出计时器的 2.0s 窗口失去意义 |
| NaN / ±Inf / -5.0 → **0.0** | 坏时钟读作 0,而不是送进下游 |
| `kMaxFrameDeltaSeconds > 0` | 界本身是正的时长 |

### 128.7 验证与未做

- 23 条门禁全绿。四处改动:客户端、服务端、`WireLimits.h`、测试。
- 三个文件花括号收支为 0;`World.cpp` 补上了 `<WireLimits.h>` 包含,
  而 `GameServer.cpp` **本来就有**(这一条是查过的,不是假设)。
- **本机不编译**(§0/§1):本节的判据是**逻辑推演 + 逐条消费方核对 + 单元测试**,
  而单元测试**要 CI 出包才会跑**。**不证明**在真机上挂起一次之后
  服务端行为符合预期 —— 那要实机。

> **教训**:「不可控的数值」不止来自网络。本节的 delta 来自**真实时钟**,
> 而对端**完全不需要做错任何事** —— 挂起机器就够了。
> 所以「这个值有没有界」要问的对象是**它的来源可不可控**,
> 而不是**它是不是别人发来的**。

---

## 129. 2026-10-11 场次:经验值只挡了「非有限」,没挡**量级** —— 而它会被别人的引擎吃下

> **锚点**:`Code/client/Games/Skyrim/PlayerCharacter.cpp`、
> `Code/client/Services/Generic/CharacterService.cpp`、
> `Code/encoding/Messages/{SyncExperienceRequest,NotifySyncExperience}.cpp`。
> 门禁/判据:单元测试 `[encoding.experience_delta]`。

### 129.1 判据:找**累加器**,然后问它的来源可不可控

§128 定下的判据是「问**来源**可不可控,而不是问是不是别人发来的」。
本轮按它去找**累加器**(只增不减、且值会喂给比较或索引的成员),全仓只有 6 处:

```text
BSTScatterTable.h:516/534   m_entry += 1;                              <- 迭代器,安全
CalendarService.cpp:114     m_fadeTimer += updateDelta;                 <- 有上界比较
CharacterService.cpp:1281   m_cachedExperience += acEvent.Experience;   <- ★
CharacterService.cpp:2585   data.m_timer += acUpdateEvent.Delta;        <- 上一轮已修
Record.cpp:19               m_formId += aBaseId;                        <- 构造期,安全
```

**`m_cachedExperience` 是这一族里最特别的一个**:
它是**本地测量出来的数**,却会被**另一个玩家的引擎**直接吃下。

### 129.2 链路:逐段查过,而「界」加在了错误的一侧

```text
PlayerCharacter.cpp:245   float deltaExperience = newExperience - oldExperience;
                          ^^^ 本地测量,无界;来源是引擎的两次读数之差
        ↓ AddExperienceEvent
CharacterService.cpp:1281 m_cachedExperience += acEvent.Experience;     累加,无界
        ↓ SyncExperienceRequest(每 4s 发一次)
服务端 CharacterService.cpp:643  notify.Experience = acMessage.Packet.Experience;
                          ^^^ 原样转发,不检查
        ↓ NotifySyncExperience
接收方 CharacterService.cpp:1300 pPlayer->AddSkillExperience(..., acMessage.Experience);
                          ^^^ 进引擎
```

而**线上那一层只挡了「非有限」**:

```cpp
Experience = WireLimits::ClampFinite(Serialization::ReadFloat(aReader));
```

`ClampFinite` 把 NaN/Inf 换成 0 —— **但一个 `1e30` 会原样通过**。

> **教训**:`ClampFinite` 的名字容易被读成「这个字段已经被处理过了」。
> 它处理的只是**「这个值是不是一个数」**,不是**「这个数是不是合理」**。
> 一个字段有 `ClampFinite` **不等于**它有界 —— 这两件事必须分开问。

### 129.3 为什么「量级」才是问题

引擎把技能已存 xp 当成一个**浮点**,再拿它与**每级的阈值**比较。
一个它**没法比较**的值不会「让技能升很多级」——
它会让那个技能**进入一个后续任何奖励都修不回来的状态**。

而**这个测量本身可以出错,且对端拦不住**:

| 出错方式 | 对端能避免吗 |
| --- | --- |
| 一次奖励把技能的浮点 xp 推到溢出 | **不能** —— 是本地的算术 |
| 两次读取之间发生**读档/存档切换**,让减法失去意义 | **不能** —— 是本地的事件顺序 |

**两者都不是对端做对什么就能避免的**,所以界**必须加在发送侧**。
这是本节与 §128 的连接点:两处的「不可控」都不来自网络,
但 §128 来自**时钟**,本节来自**本地算术与事件顺序**。

### 129.4 修法:界放 `WireLimits.h`,四处应用

| 位置 | 为什么在那里 |
| --- | --- |
| `CharacterService::OnAddExperienceEvent` | **本地测量变成别人游戏里的数**的地方 —— 加在入口 |
| `SyncExperienceRequest::DeserializeRaw` | 线上读入 |
| `NotifySyncExperience::DeserializeRaw` | 线上读入 |
| `CharacterService::OnNotifySyncExperience` | **进引擎前的最后一点** —— 纵深防御 |

**为什么是 `100000.f`**:技能整条进度线是**每级几百点**、单次奖励是**不到一级**,
所以这个数**远在任何真实奖励之上**;它与 `kMaxHealthDelta` **取同一个数**,
因为两者都是「引擎自己的尺度,宽裕地加上界」,而不是游戏常量。

**是区间截断而不是下限**,因为**负的经验是有意义的**(技能可以掉进度)。

### 129.5 单元测试:23 → 24 个用例

| 断言 | 为什么 |
| --- | --- |
| `0` / `1` / `±12.5` **原样通过** | 否则这个界会改变**正常游玩** |
| 边界两侧、**正负都测** | 这是区间截断,不是下限 |
| `±kMax * 2` 截到 `±kMax` | 越界被限住 |
| `float::max()` → `kMax` | 注释里点名的溢出情形 |
| NaN / ±Inf → **0** | 与 `ClampFinite` 同理 |
| `kMax > 1000` | 界要**宽裕**,否则会削掉真实奖励 |
| **线上往返**:写入 `float::max()` 读回 == `kMax`,再写 NaN 读回 == `0` | **线上那一层也真的截断了** |

最后一条是**往返测试**而不只是常量测试 ——
它证明的是「**接收方拿到的值已经被截断**」,而不是「那个函数本身没问题」。

**顺带核对**:`ServerMessage::DeserializeRaw` 的注释是
「We don't read the opcode, the factory will do it」——
所以测试里直接写裸浮点再调 `DeserializeRaw` 是**对齐的**。
这一条是**读实现确认的**,不是假设。

### 129.6 验证与未做

- 23 条门禁全绿。五处改动:`WireLimits.h`、两个消息、`CharacterService`、测试。
- 花括号收支全为 0;`CharacterService.cpp` 补上 `<WireLimits.h>` 包含,
  而**该头在客户端已可达**(`SkyrimEncoding` 是客户端依赖,
  `TransportService.cpp` 早就包含了它)—— 这两条都是查过的。
- **本机不编译**(§0/§1):判据是**链路逐段核对 + 单元测试**,
  而单元测试**要 CI 出包才会跑**。**不证明**真机上真能构造出一次溢出 ——
  那需要一个把技能 xp 推过 `float` 精度的 mod。

> **教训**:一个字段被 `ClampFinite` 处理过,**只说明它「是个数」,
> 不说明它「合理」**。审计「有没有界」时要分别问两句:
> **① 它可能是非有限的吗?② 它的量级可能是荒谬的吗?**
> 本节与 §128 都是第 ② 问的答案,而两次的「不可控来源」都不是网络。

---

## 130. 2026-10-11 场次:十张出货表里**只有一张**被记账 —— 而它们是兄弟,不是副本

> **锚点**:`Tools/Scripts/check_map_coverage.py`(新)、`Tools/missing_ids_by_runtime.tsv`(新)、
> `Code/client/Games/Skyrim/ExtraData/ExtraContainerChanges.h`/`.cpp`、
> `Code/client/Services/Generic/InventoryService.cpp`。

### 130.1 判据:「同一批出货文件之间不一致」本身就是缺陷

上一轮(§127–§129)的问法是「同一个事实被写了几遍」。这一轮换一个更外的角度:
**同一个东西出货了几份,每一份是不是都被声明过。**

1.5.x 运行时**不能**直接读 AE 地址库(两者 id 命名空间不同,`VersionDb::Load`),
它加载的是 `GameFiles/Skyrim/SKSE/Plugins/version-<版本>.bin` **加一张自己生成的翻译表**
`versionlib-ae-to-se-<版本>.map`。**十份库、十张表**,一个 1.5.x 构建一对。

而记账只覆盖了**一对**:`Tools/missing_1_5_97_ids.txt` 声明 1.5.97 的 13 个缺口,
`Tools/Scripts/check_docs.py` 把它和**那一张** map 对照。另外九张 —— 也就是另外九个
**本仓库随包提供地址库、因而在 README 与 FOMOD 里声称支持**的 1.5.x 运行时 ——
**没有任何门禁读过**。

把十张表的 id 集合逐一求差,差异只有一条:

```text
  id   16113  in 7/10  value(s) ['0x1e7570', '0x1e7750', '0x1e7760']
      missing from: 1-5-16-0, 1-5-23-0, 1-5-3-0
```

七张有、三张没有,而**值在七张之间还各不相同**(1.5.39 = `0x1e7750`,
1.5.50–1.5.62 = `0x1e7760`,1.5.73 起 = `0x1e7570`)—— 这一半是证据,一半是警告:
**它不可能被一条 override 覆盖**,所以三张缺的行不能靠「补一条 override 再重跑」解决。
（真正的机制见 §130.3,那里把这条猜测换成了实测。）

### 130.2 缺这一行会怎样:不报错,而是「每次调用返回 0」

`16113` 是 `ExtraContainerChanges::Data::GetArmor` 经由的 id:

```cpp
// Code/client/Games/Skyrim/ExtraData/ExtraContainerChanges.cpp
TP_THIS_FUNCTION(TGetArmor, TESObjectARMO*, ExtraContainerChanges::Data, uint32_t);

POINTER_SKYRIMSE(TGetArmor, s_getArmor, 16113);

return TiltedPhoques::ThisCall(s_getArmor, this, aSlotId);
```

未映射的 id 在 legacy 运行时走的是 `VersionDbPtr::GetPtr` 的兜底:

```cpp
// Code/client/VersionDb.h
if (m_pPtr == nullptr && m_allowUnresolvedStub && VersionDb::Get().IsLegacyFormat())
{
    m_pPtr = VersionDb::Get().GetUnresolvedStub();
    spdlog::warn("address library id {} is not mapped on game {}; every call through it returns zero "
                 "instead of running", m_id, VersionDb::Get().GetLoadedVersionString());
}
```

而那个桩是七个字节(`48 31 C0 0F 57 C0 C3`,即 `xor rax,rax; xorps xmm0,xmm0; ret`)⇒
**`GetArmor(32)` 在那三个运行时上恒为 `nullptr`**,并且只在**第一次**调用时打一条 warn
(`m_pPtr` 会被缓存成桩指针)。

于是链条是:

```text
GetArmor(32) 恒为 nullptr
  -> Actor::IsWearingBodyPiece() 恒为 false            (Actor.cpp:587 唯一的读者)
     -> InventoryService::RunNakedNPCBugChecks        (:471 唯一的上层读者)
        if (!pActor->IsWearingBodyPiece() && pBase)
        {   warn("naked check fires");
            pActor->EquipOutfit();   <-- 对**已经穿着护甲**的 actor 每趟执行一次
        }
```

**不是崩溃,是稳定的行为错误**:每次 `EquipOutfit()` 都真的改装备状态(而装备变化本身就是
要同步的东西),所以症状会是「那三个版本上远程 NPC 的装备一直在被重置」。

### 130.3 为什么补齐 RVA 做不到 —— 实测,不是推测

**第一版判断是错的,而且错在「按名字连接」这个前提上。** 当时的推理是「map 由
`gen_ae_to_se_map.py` 按符号名连接产出,这个符号在那三份名字表里没对应项」。
`Tools/ida/README.md` 里写着另一个生成器,`--se-bins-dir` 那个参数说明真正在跑的是**翻译**:

    python3 Tools/Scripts/gen_se_map_from_history.py \
        --se-bin       .../version-1-5-97-0.bin \
        --overrides    ... \
        --se-bins-dir  GameFiles/Skyrim/SKSE/Plugins \
        --out          .../versionlib-ae-to-se-1-5-97-0.map

也就是说:map 是**穿过 pre-AE id 命名空间的翻译**——一个 AE id 命名一个 pre-AE id,
而每个 pre-AE 库把那个 id 拼成**自己构建**的 RVA。这条规则可以直接验,拿七张**有**这一行的表:

| 运行时 | `version-<构建>.bin[15873]` | 该表的 `16113` | 一致 |
| --- | --- | --- | --- |
| 1.5.39 | `0x1e7750` | `0x1e7750` | ✅ |
| 1.5.50 / 1.5.53 / 1.5.62 | `0x1e7760` | `0x1e7760` | ✅ |
| 1.5.73 / 1.5.80 / 1.5.97 | `0x1e7570` | `0x1e7570` | ✅ |
| **1.5.3 / 1.5.16 / 1.5.23** | **`15873` 不存在** | **不存在** | —— |

**七比七全部相等**,所以 `15873` 就是 `16113` 命名的那个 pre-AE id,而这三张表缺行的原因
当场就清楚了:**那三个库里没有 `15873`**。pre-AE 的 id 空间是**演进**而不是只增:

```text
  1-5-3-0      ids= 791088  shared_with_97= 775712  only_in_97=  2962  only_here= 15376
  1-5-16-0     ids= 794077  shared_with_97= 777719  only_in_97=   955  only_here= 16358
  1-5-23-0     ids= 794170  shared_with_97= 777781  only_in_97=   893  only_here= 16389
  1-5-39-0     ids= 794826  shared_with_97= 778453  only_in_97=   221  only_here= 16373
  1-5-50-0     ids= 796102  shared_with_97= 778674  only_in_97=     0  only_here= 17428
  1-5-97-0     ids= 778674  shared_with_97= 778674  only_in_97=     0  only_here=     0
```

1.5.3 的表**比** 1.5.97 **更大**(791,088 > 778,674,多出 15,376 个 id),同时**没有** 1.5.97 有的
**2,962** 个 —— `15873` 正在其中。所以:

- **没有可翻译的值**,也没有任何 override 能提供它(值必须逐构建量,上表三档值已经证明);
- **重新生成也补不上** —— 生成器的输入(库)里没有这条 id;
- `0x1e7570`(1.5.97 的 RVA)**甚至不是 1.5.3 那个库列出的任何偏移**。

**而这恰恰是好消息**:它说明缺口**不是**「忘了同步」,而是**不可约**的。
而**猜一个 RVA 比缺一个更危险**:缺了,调用落到零返回桩上 —— 行为错,但**进程是活的**;
猜错,就是**调用一个无关函数**。所以三张表的值**不补**。

> **教训**:「按名字连接」这个前提**是从旧文档里读来的**,而仓库里实际在跑的生成器
> (`gen_se_map_from_history.py` + `--se-bins-dir`)**就在同一份 README 的下一段**。
> 判据必须建立在**能跑出数字的那条路径**上;而一旦换成那条路径,同一个问题就从
> 「补不了、只能声明」变成「**七比七可验、原因当场可证**」。


### 130.4 修法

**(a) 缺口声明成文件**:新增 `Tools/missing_ids_by_runtime.tsv`,13 条 `*` 行(公共缺口)
+ 3 条按运行时的行(`1-5-3-0` / `1-5-16-0` / `1-5-23-0` 的 `16113`),每条带原因。

**(b) 后果就地修**:新增 `ExtraContainerChanges::Data::CanQueryArmor()`,
用 `VersionDb::Get().FindAddressById(16113)` **直查**——不是绕开 `VersionDbPtr` 写第二套解析,
而是因为**「没映射」与「映射到了但为空」在 `GetArmor` 的返回值上无法区分**,而这里要问的正是前者。
这与 §3.1 里 `GarbageCollector::Get` 的改法**同一范式**(那里也是「桩的零返回不能当答案」)。

调用点改成:

```cpp
if (ExtraContainerChanges::Data::CanQueryArmor() && !pActor->IsWearingBodyPiece() && pBase)
```

理由写成一行:**重穿是一次「修复」动作,只允许建立在确定的答案上;问不出答案时不动 actor,
比每趟给穿好衣服的人重穿一套好。** 代价是那三个运行时上「裸体 NPC 自动修复」不再生效 ——
这是**有意的降级**,恢复它需要那三个 RVA(登记进 `docs/FOLLOW-UP.md`),而不是需要改回一个
会把「不知道」当成「没有」的判断。

**(c) 门禁 `Tools/Scripts/check_map_coverage.py`**,六条判据:

| 判据 | 为什么 |
| --- | --- |
| `version-*.bin` 与 `versionlib-ae-to-se-*.map` 必须成对 | 只有库 = 一个 id 都翻译不了;只有表 = 没有运行时会加载它 |
| 每张 map 的缺口(`collect_codebase_ids()` 里它不带的 id)**恰好等于**该运行时的声明 | 少声明 = 本轮这个形状;**多声明 = 声明已过期**,读者仍被告知这个运行时解析不了某个其实已修的 id |
| 每条按运行时的行必须在 `JUDGED` 表里有已判读的绑定 | 「桩的 0 落到调用点会怎样」**不可判定**,所以逐个判读登记;新加一条**没有判读**的行**失败**而不是静默通过 |
| 守卫函数体里的 id 必须等于**同一个文件经 `POINTER_SKYRIMSE` 解析的 id** | 同一件事写了两遍就必须一致,否则守卫问的是一个 id、调用点用的是另一个 |
| **十张 map 必须对同一个 AE id 命名同一个 pre-AE 符号**(按 pre-AE id 反查,**不比值**) | §130.3 证明了不变量就是这条:值跨构建不同,符号跨构建相同。它**不需要任何声明**,还能抓到「值合法、却是另一个函数」 |
| **每张 map 必须是单射**(两个 AE id 不能指向同一个 RVA) | 两个库都是单射,生成器**拒绝写出**这样的表;两个 AE id 共用一个 RVA 就是「照抄了邻居的值」的样子 |

第 5 条在这一轮的出货数据上**逐对全过**:9 张兄弟表 × ~3,697 个共享 id,
**0 处符号不一致、0 处「该 RVA 不在对应库里」**;第 6 条十张全为单射。
这条判据的价值在于它**证明了另外九张是被同一条路径生成的** —— 也就是说,§130.1 里
「三张缺、七张有」的差异有了唯一解释(缺的那个 id 在那三个库里不存在),而不是
「某一张被手改过」。

### 130.5 反向验证(变异测试)

五个变异,**每个都先确认已落到磁盘**(§113.4 的教训):

| 变异 | 门禁诊断 |
| --- | --- |
| 删掉 `1-5-23-0` 那一行(=**原始缺陷**) | `versionlib-ae-to-se-1-5-23-0.map does not carry 1 id(s) ... [16113]` |
| `1-5-16-0` 那行改成 map 里**有**的 `16040` | `id 16040 is declared missing on 1-5-16-0 but no binding is judged for it` + `declares [16040] missing from 1-5-16-0, but ... carries them` |
| `1-5-3-0` 改成不存在的运行时名 `1-5-999-0` | `row for runtime 1-5-999-0, which no shipped map matches (...)` |
| 守卫里的探测 id 改成 `16114` | `no CanQueryArmor body naming id 16113` + `no judged binding contributed anything` |
| `GetArmor` 的 `POINTER_SKYRIMSE` id 改成 `16114`(守卫不动) | `CanQueryArmor guards id 16113, but the file resolves [16005, 16114, 16142, 16143] through POINTER_SKYRIMSE` |
| **`1-5-39-0` 表里 `16118` 的值换成同表 `16123` 的值** | `id 16118 names pre-AE id 15883 (0x1e9310), but ... names pre-AE id 15878 (0x1e7ba0) for the same AE id` + 该表不再单射的两个 id |

最后一条是**唯一一个「值仍然合法」的变异**:换过去的 `0x1e9310` 是那个库里**真实存在的偏移**,
所以「地址对不对」这类检查(以及 `check_ae_to_se_overrides.py` 的函数起点判据)**全部看不见它**;
只有第 5 条(命名的是哪个符号)能看见。这也说明为什么第 5 条值得单独存在。

第 1 条**单独复现过一次**(途中还缺 `1-5-3-0` 那行时,门禁只报那一条,指名 `1-5-3-0`)。
第 4/5/6 条还顺带证明了一件事:把一个 id 改成任何一张表都没有的值之后,门禁在**十张表上全部**报
「the client resolves [16114] and nothing declares」—— 也就是说**新引入一个不可解析的 id 会被挡下**,
不只是这三个旧缺口。

### 130.6 教训

> **「随包提供了 N 份同名产物」本身就是 N 条主张,而不是一条。**
> 一张表的覆盖度被声明、被门禁比对,**不能**推出另外九张也一样 ——
> 恰恰相反:它们的**生成路径**决定了差异是常态(这里是从 1.5.97 逐构建翻译)。
> 判据要按**「出货了几份」**数,而不是按**「当初是在哪一份上发现的」**写。
>
> 这是同一个错误的**第五次**:§113(一个插件)、§116(一个目录)、§119(一个文件)、
> §126(一份手写清单)、§130(一张 map)。五次都满足「在开发机上完全正常」:
> **本轮这个缺陷在 1.5.97 上根本不存在** —— 开发机跑的就是 1.5.97。

> **教训(同轮第二条)**:**跨版本不可比的量做不了跨版本判据,但它背后的不变量可以。**
> 十个 RVA 互不相同,所以「表与表的值一致」永远为假、也永远查不出错;
> 而「这条 AE id 命名的是哪个符号」在所有构建上都必须相同 —— 换一个问法,
> 同一份数据就从「无从比较」变成「33,000 次比对零分歧」。
> **先找不变量,再写判据。**

---

## 131. 2026-10-11 场次:§0 的勾选项与 FOLLOW-UP 的「已知但未做」**写对了,但都没在跑**

> **锚点**:`Tools/Scripts/check_branch_layouts.py`(新)、
> `Code/client/Games/Skyrim/ExtraData/ExtraDataList.h`、
> `Code/client/Games/Skyrim/Misc/GameVM.h`、`Code/client/SkyrimVM64.cpp`。

### 131.1 判据:客户端是「同一份源码、两个目标」

`Code/client/xmake.lua` 把同一批源文件编两次:

```text
build_client("SkyrimTogetherClient",       {...})
build_client("SkyrimTogetherClientLegacy", {"SKYRIM_TARGET_LEGACY=1"})
```

而两个目标**对引擎字段的位置并不一致**(1.5.x 的 `Actor`/`TESObjectREFR`/`ExtraDataList`
比 AE 各小 8 字节,`SkyrimVM` 的两个字段各低 0x10)。所以每个「手写引擎偏移」的结构体都要
`#ifdef SKYRIM_TARGET_LEGACY` 分叉,而 §2 已经为它付过**三次**同样的账。

§2 给出的防线是**改完更新两分支的 `static_assert`**;§0 的提交前核对清单里也逐字写着
「改完更新两分支的 `static_assert`,让下次漂移在编译期炸出来,而不是静默读邻居」。
**而没有任何东西在跑这条规则** —— 它是清单上的一个方框。§117 与 §127 已经各付过一次账:
**写下来的约定不被执行,地位等同注释。**

于是先做一次全量测量(剥注释、按花括号配平切出每个 `SKYRIM_TARGET_LEGACY` 条件块,
逐侧取成员名):

```text
分支对总数: 16
其中改变成员布局的: 7      (Actor / BSAnimationGraphManager / ExtraDataList /
                            TESObjectCELL(x2: ReferenceData, LoadedCellData) /
                            GameVM(SkyrimVM) / PlayerCharacter / TESObjectREFR)
```

**两处结论:**

**(a) `docs/FOLLOW-UP.md` 自己记着的那个缺口仍然在,而且形状比记的更弱。**
那里写的是「`ExtraDataList.h` 在 `SKYRIM_TARGET_LEGACY` 下分支却没有自己的 `static_assert`,
其尺寸只被 `TESObjectREFR` 的 `sizeof` 断言**间接**覆盖(§111.7)。目前够用,但不如直接断言直白。」
实测确认:

```cpp
// Code/client/Games/Skyrim/ExtraData/ExtraDataList.h
#ifndef SKYRIM_TARGET_LEGACY
    virtual ~ExtraDataList();      // <-- 这一行就是分支的全部理由:虚表在/不在
#endif
    BSExtraData* data = nullptr;
    Bitfield* bitfield{};
    mutable BSRecursiveLock lock{};
```

虚表的有无让 `data` 在 **0x0 / 0x8** 两个位置,`sizeof` 在 **0x18 / 0x20**,
而**内嵌它的每一个结构**(`Actor`、`TESObjectREFR`、`TESObjectCELL`)都跟着整体后移 8 字节。
「间接覆盖」的真实含义是:**改动会在离编辑点一个结构之外才发现,而且只在那个断言恰好消费了
这次变化时才被发现** —— 若改动只影响一个没被那个断言覆盖的字段,就完全不被发现。

**(b) 同一批事实里最险的那个写了五遍,只有一遍被检查。**
`inactive`(§2:读错一位 → 落到邻居字段 → 看到非零 → 从此不再执行 `World::Update()`,
连带连接一起静默死掉)出现在:

| # | 位置 | 被检查? |
| --- | --- | --- |
| 1 | `GameVM.h` legacy pad 锚点 `0x680 - 0x200 - sizeof(void*)` | 只经由 3 间接 |
| 2 | `GameVM.h` AE pad 锚点 `0x690 - 0x210 - sizeof(void*)` | 只经由 3 间接 |
| 3 | `GameVM.h` 两分支的 `static_assert(offsetof(SkyrimVM, inactive) == …)` | ✅ **只有这里** |
| 4 | `GameVM.h` `kInactiveOffset` | ❌ |
| 5 | `SkyrimVM64.cpp` 探针 `ReadInt32At(apThis, 0x680)` / `0x690` | ❌ |
| 6 | `SkyrimVM64.cpp` 心跳日志里的 `0x680` / `0x690` | ❌ |

第 5/6 条尤其要紧:那两行**存在的唯一目的**就是区分「偏移对」与「偏移错」
(读两个候选各一次,看哪一个是非零),所以它们一旦与原处漂开,就会带着一个
**看起来像证据**的数字去指错方向 —— 与 §124 的「错误信息指错原因」同类。

### 131.2 修法

**(a) 补 `ExtraDataList` 的两分支断言**,把「记在 FOLLOW-UP 里的已知项」关掉:

```cpp
#ifdef SKYRIM_TARGET_LEGACY
static_assert(offsetof(ExtraDataList, data) == 0x0);
static_assert(offsetof(ExtraDataList, bitfield) == 0x8);
static_assert(sizeof(ExtraDataList) == 0x18);
#else
static_assert(offsetof(ExtraDataList, data) == 0x8);
static_assert(offsetof(ExtraDataList, bitfield) == 0x10);
static_assert(sizeof(ExtraDataList) == 0x20);
#endif
```

这些数是**声明自身的算术**,不是量出来的:`BSExtraData*` + `Bitfield*` + `BSRecursiveLock`
(`volatile DWORD m_tid; volatile LONG m_counter;` = 8 字节),AE 再加 vptr。

**(b) 把 `SkyrimVM` 的两个候选值命名,并让所有拷贝由它们推导:**

```cpp
static constexpr int kVirtualMachineOffsetLegacy = 0x200;
static constexpr int kVirtualMachineOffsetModern = 0x210;
static constexpr int kInactiveOffsetLegacy      = 0x680;
static constexpr int kInactiveOffsetModern      = 0x690;
...
#ifdef SKYRIM_TARGET_LEGACY
    uint8_t pad218[kInactiveOffsetLegacy - kVirtualMachineOffsetLegacy - sizeof(void*)];
    int32_t inactive;
#else
    uint8_t pad218[kInactiveOffsetModern - kVirtualMachineOffsetModern - sizeof(void*)];
    int32_t inactive;
#endif
    static constexpr int kInactiveOffset =
#ifdef SKYRIM_TARGET_LEGACY
        kInactiveOffsetLegacy;
#else
        kInactiveOffsetModern;
#endif
```

并在 `#endif` 之后把命名值钉在结构体上:

```cpp
#ifdef SKYRIM_TARGET_LEGACY
static_assert(offsetof(SkyrimVM, virtualMachine) == SkyrimVM::kVirtualMachineOffsetLegacy);
static_assert(offsetof(SkyrimVM, inactive) == SkyrimVM::kInactiveOffsetLegacy);
static_assert(SkyrimVM::kInactiveOffset == SkyrimVM::kInactiveOffsetLegacy);
#else
...
```

**(c) `SkyrimVM64.cpp` 的探针与日志改用同一批名字** —— 于是「它读的两个数」与
「结构体被编译时用的两个数」在语言层面就是同一批,而不是两批恰好相等的数。

**(d) 门禁 `Tools/Scripts/check_branch_layouts.py`**,三条:

| 判据 | 为什么 |
| --- | --- |
| 目标相关的条件块若改变某结构的成员,**该文件里必须有一条 `static_assert` 点到那个类型的名字**(直接 `T`,或限定 `Outer::T`) | 断言写在 `#endif` 之后也能钉住「这次编译的分支」,够用;不够用的是**什么都不点这个类型** |
| 每个 `k*Offset*` 常量必须由 `offsetof()` **算出**,或被某条 `static_assert` **点到名字** | 它是结构体已经算过的那个数的第二份拷贝 |
| 防退化:条件块数为 0、或没有任何一处声明成员 → **失败** | 否则改一次宏名就能让门禁静默空转却照样打印 OK |

### 131.3 反向验证(变异测试)

三个变异,每个都**先确认已落到磁盘**:

| 变异 | 门禁诊断 |
| --- | --- |
| 删掉刚补的 `ExtraDataList` 断言块(= 还原 FOLLOW-UP 记着的原缺口) | `ExtraDataList.h:39: the #ifndef SKYRIM_TARGET_LEGACY branch changes ExtraDataList's layout (members ['~ExtraDataList']) and no static_assert in this file names ExtraDataList` |
| 把 `TESObjectCELL::ReferenceData` 的**两条** `sizeof` 断言改名(限定名路径) | `TESObjectCELL.h:32: ... changes ReferenceData's layout (members ['unk0', 'capacity', ...]) and no static_assert in this file names ReferenceData` |
| 把 `GameVM.h` 两条「常量 ↔ 候选值」断言换成不点名字的写法 | `GameVM.h: kInactiveOffset ... is a second copy of an offset the struct already computes and nothing ties it back` |

**变异测试暴露了门禁自己的一个盲点,已修掉 —— 这是本节最值得记的一条。**
第一版的「成员」模式**不认虚函数**,于是 `#ifndef SKYRIM_TARGET_LEGACY virtual ~ExtraDataList();`
被读成「这一分支没有成员」→ **变异 1 什么都没报、门禁打印 OK**。而虚表的有无**正是**
`ExtraDataList` 分支的全部内容,也正是移动后面每个成员 8 字节的那件事。
归功于 §113.4 的规矩(先确认变异真的生效,再看门禁说不说话),这一条在提交前被抓到;
`virtual` 声明现计入布局,布局计数随之 6 → 7。

> **教训**:**门禁的第一版如果「刚好通过」,要先怀疑它没在看,而不是先庆祝。**
> 这里的盲点形状很典型:我按「数据成员」写模式,而这一处的布局恰恰由**虚函数**决定。

### 131.4 顺带核对、确认无害的三项

- **逻辑分支不是布局**:`ObjectService.cpp:55` 的 `#ifndef SKYRIM_TARGET_LEGACY` 只圈住
  玩家住宅检测(1.5.x 不建模 `LoadedCellData`,与其读错偏移不如跳过),分支体里是语句。
  门禁按「是否在某个类型内部」把它们分开,并把它们**列出来**(`guards logic, not a layout`),
  这样「在类型外面声明了成员」这种真形状不会混在噪声里被忽略。
- **插件侧同样用 vtable 槽位,但与客户端互相印证**:`AnimSyncTogether` 用
  `RE::VTABLE_PlayerCharacter[3].write_vfunc(0x1, …)` 钩 `NotifyAnimationGraph`,
  而客户端自己的 `IAnimationGraphManagerHolder` 把 `SendAnimationEvent`(同一槽位、同一签名)
  声明在**第 1 槽**;两处是彼此独立写下的同一件事,且一致。
  客户端唯一的虚调用是 `IsReady()` 里的 `GetBSAnimationGraph`(第 2 槽),也与 CommonLibSSE-NG
  的顺序一致 ⇒ **当前无活缺陷**。(客户端其余地方一律走 `POINTER_SKYRIMSE` 解析地址,
  `Animation.cpp` 的注释「skip vtable as we never use this directly」就是这个纪律。)
- **原始偏移的普查**:`Code/client` 里所有 `+ 0xNN` 形式的裸偏移只有 4 处 —— `MapMenu.cpp`
  的 3 处 `GamePatch::Nop(pHookLoc + 0x53/0x9D/0x9F)` 整体在 `#if 0` 里(死代码),
  `VersionDb.h:529` 的 `itr->second + 0x140000000` 只是转储用的首选基址。
  ⇒ 这个仓库在**偏移**上确实做到了「要么走结构体 + 断言,要么走地址库 id」。

---

## 132. 2026-10-11 场次:「反正这个字段没人用」—— 而它的值域里有 0

> **锚点**:`Tools/Scripts/check_ui_contract.py`(新)、`Code/encoding/Structs/Mods.h`、
> `Code/server/GameServer.cpp`、`Code/client/Services/Generic/TransportService.cpp`、
> `Code/skyrim_ui/src/app/services/error.service.ts`。

### 132.1 判据:数据字段与哨兵值的**值域**是否相交

服务端拒绝连接时会给客户端一份「你的 mod 列表哪里不对」的清单,而这份清单是
**一个列表由两次比较拼成**:

```cpp
// Code/server/GameServer.cpp(修复前)
Mods modsToRemove;
// (a) 客户端有、服务器没有  -> 客户端必须「删除」
for (const Mods::Entry& mod : userMods)
    if (!modsComponent.IsInstalled(mod.Filename))
        modsToRemove.ModList.push_back(mod);          // <-- 原样回传客户端自己的条目

// (b) 服务器有、客户端没有  -> 客户端必须「安装」
for (const auto& entry : modsComponent.GetServerMods())
    if (it == userMods.end())
    {
        Mods::Entry removeEntry;
        removeEntry.Filename = entry.first;
        removeEntry.Id = 0;                            // <-- 用 0 当「安装」的哨兵
        modsToRemove.ModList.push_back(removeEntry);
    }
```

UI 那一侧逐字:

```ts
// Code/skyrim_ui/src/app/services/error.service.ts(修复前)
const install = mismatched.filter(mod => mod[1] === '0').map(mod => mod[0]);
const remove  = mismatched.filter(mod => mod[1] !== '0').map(mod => mod[0]);
```

**所以整条契约是「`Id == 0` 表示安装」。而 `0` 是一个真实存在的 id** —— 两处都能到:

- 标准 mod:`standardId` 是 load-order 索引,`0` 就是第一个插件;
- **lite(ESL)mod:`liteId` 从 0 开始发号**。服务端自己的加载器写着:

```cpp
// Code/components/es_loader/ESLoader.cpp
uint16_t liteId = 0x0;
...
case 'l':
    plugin.m_liteId = liteId;
    liteId += 0x0001;
```

```cpp
// Code/components/es_loader/TESFile.cpp
void TESFile::Setup(uint16_t aLiteId)
{
    m_liteId = aLiteId;
    m_formIdPrefix = 0xFE000000 + (m_liteId * 0x1000);
}
```

  也就是**第一个 ESL 的 `liteId` 就是 `0`**(form id 前缀 `0xFE000000`)。客户端那侧正是用它:

```cpp
// Code/client/Games/TES.h
[[nodiscard]] uint16_t GetId() const noexcept { return IsLite() ? liteId : standardId; }
```

```cpp
// Code/client/Services/Generic/TransportService.cpp(认证包)
entry.Id = pMod->GetId();
```

**闭合的链条**:服务器 `IsMoPoActive()` 时,**第一号 ESL 不在服务器上**的客户端 → 该条目落在
(a) 组 → `Id` 原样是 `0` → UI 归进**安装**组 ⇒ **玩家被要求「安装」一个他有、而且必须删掉的
mod,并且永远不会被告知删掉它。**

同一形状上一轮也出现过(§v1.1.9 的 MO2:玩家被要求移除一个自己没装的 mod 管理器),
但方向相反:**那次是多说了,这次是说反了。**

### 132.2 修法

1. **哨兵移出值域,并由编译器兜住。** `Code/encoding/Structs/Mods.h`:

```cpp
    static constexpr uint16_t kInstallMarker = 0xFFFF;
    static constexpr uint16_t kRemoveMarker  = 0xFFFE;
    static constexpr uint16_t kMaxLiteId     = 0x0FFF;
    static_assert(kRemoveMarker > kMaxLiteId,
                  "both direction markers must be values no mod id can take, "
                  "or a real id reads as a direction");
```

2. **两次比较分成两个列表**,标记**按列表写一次**(不再逐条「这个 0 是哪种 0」)。
   顺带修掉那条**本身就不准**的日志 —— 它把整份列表说成 "the following mods are installed on
   the client",而其中一半是「服务器有、客户端没有」:

```cpp
spdlog::info("ModPolicy: refusing connection {:x}: the client must remove [{}] and install [{}]", ...);
```

3. **标记不进 UI。** UI 是看不到 `Mods.h` 的那一侧,所以由客户端把它翻译成词再写进 JSON:

```cpp
ErrorInfo += (m.Id == Mods::kInstallMarker) ? "install" : "remove";
```

   `error.service.ts` 与 mock 改成 `[filename, action]`(`action: 'install' | 'remove'`),
   并且**两个词都显式比较** —— `'remove'` 不再写成 `!== 'install'`:
   一个没预料到的词,显式比较会让它**既不算安装也不算删除**(可见的空档),
   `!==` 写法会让它**静默变成「删除」**(不可见的错类)。

4. **门禁 `check_ui_contract.py`**:见 §132.3。这是本仓库**第一条同时读三种语言**
   (C++ / JSON 语言文件 / TypeScript)的门禁 —— overlay 是 CEF 浏览器,两侧之间
   **没有类型、没有 schema**,只有拼出来的 JSON 文本。

### 132.3 门禁的三条判据

| 判据 | 为什么 |
| --- | --- |
| C++ 能发的每个错误码,必须在**每一个**语言文件里有 `SERVICE.ERROR.ERRORS.<CODE>`,并在 UI 的 `ErrorEvent` 联合类型里 | UI 逐字 `'SERVICE.ERROR.ERRORS.' + error.error.toUpperCase()` **拼键**:缺一条 = 把**原始键名**显示给玩家;少一个语言 = 只在那一种语言里坏 |
| 方向词在**两侧都恰好**是 `{install, remove}` | 一侧多一个 = 永远匹配不上;一侧少一个 = 那一类被静默丢掉 |
| 防退化:语言文件 < 2、或一个码都没扫到、或联合类型读不出来 ⇒ **失败** | 改一次目录名/载荷形状就会让门禁静默空转却照样打印 OK |

**顺带查清一件已死的东西**:上游那条「你的安装不是纯原版」警告在本仓库**早已改为只记日志**
(`DiscoveryService.cpp` 的 `IsDefaultModlist` 结果只进 `spdlog::info`,注释写明理由:该检查要求
恰好八个原版/AE 插件,而本仓库面向的正是整合包玩家,弹窗会对每个人误报),于是
`non_default_install` 这个码**已经不可达**,而联合类型成员与 14 份语言文件里的
`SERVICE.ERROR.ERRORS.NON_DEFAULT_INSTALL` 都还在。门禁把它作为**提示**打印(不判失败:
去掉那条警告是有意的,字符串可能回来)。

### 132.4 反向验证(变异测试)

五个变异,每个都**先确认已落到磁盘**:

| 变异 | 门禁诊断 |
| --- | --- |
| 把客户端的方向词**改回数字 id**(= 还原本轮修掉的原始缺陷) | `the mods-mismatch payload carries the direction words [], expected ['install', 'remove']` |
| 加一个**没有翻译**的新错误码 `kicked_by_admin` | `SERVICE.ERROR.ERRORS.KICKED_BY_ADMIN: ... 14 language(s) have no entry: cs.json, de.json, ...` + `is not in the ErrorEvent union` |
| 从联合类型里去掉 `wrong_version` | `error "wrong_version" is sent by ... but is not in the ErrorEvent union` |
| UI 改成比较 `'add'` | `the UI switches on ['add', 'remove'], expected ['install', 'remove']` |
| **只从 `de.json` 删掉 `WRONG_PASSWORD`** | `SERVICE.ERROR.ERRORS.WRONG_PASSWORD: ... 1 language(s) have no entry: de.json` —— **只指名那一个文件** |

最后一条是这条判据的关键方向:**「每一个语言」不能退化成「至少一个语言」** ——
只查 `en.json` 会让「德语玩家看到原始键名」这类缺陷永远通过。

**门禁自己也犯过一次错,并且在第一次运行就被抓住**:联合类型的提取正则写成
`'([a-z][a-z_0-9]*)'`,于是 **`'bad_uGridsToLoad'` 这种驼峰码匹配不上** ——
门禁报告「`bad_uGridsToLoad` 不在联合类型里」,而它明明在第 14 行。
`[A-Za-z]` 一改就对了。**与 §131.3 同形的教训:门禁收窄了一个字符类,就会开始说假话
(而假阳性比假阴性更容易被当成「门禁不行」而放弃)。**

### 132.5 教训

> **「这个字段反正没人用」是错的 —— 只要它是数据字段,它的合法值域里就不许有哨兵。**
> `Id` 在错误路径里确实没有别的读者,所以「借它当方向」看起来无害;
> 但它的值域**包含 0**,而 0 恰好是**最常见**的一个合法值(第一个插件、第一个 ESL)。
> **判据是值域,不是读者数。**
> 现在这件事由 `static_assert(kRemoveMarker > kMaxLiteId, ...)` 兜住:
> 哨兵想和真实 id 撞车,构建就失败。

> **教训(同轮第二条)**:**跨语言边界的契约要按「三处一起看」建门禁,而不是按「其中一处改了什么」。**
> C++ 产生码、语言文件提供串、TS 决定怎么拼键 —— 三处此前各自都有人管,
> **但没有一条同时看它们**,而缺陷恰好只长在**交界**上(缺一条翻译、方向词不一致)。
> 与 §115「一条消息定义两次」同形,只是这里的两侧是两种语言。

---

## 133. 2026-10-11 场次:「本仓库答不了的问题」—— 第三种处理方式

> **锚点**:`Tools/Scripts/check_lite_mod_path.py`(新)、`Code/client/Games/TES.h`、
> `Code/client/Services/Generic/TransportService.cpp`、
> `Code/client/Systems/ModSystem.cpp`、`docs/FOLLOW-UP.md` §6。

### 133.1 判据:一个问题的**两个答案后果相反**,而本仓库只能回答一个

客户端认证包里那份 mod 列表是**唯一**让服务端认识客户端 mod 的东西:服务端 `ModsComponent`
据此发号,客户端 `ModSystem` 再把服务端 id 映射回本机 form id。它的成员资格由这一行决定:

```cpp
// Code/client/Games/TES.h
[[nodiscard]] bool IsLoaded() const noexcept { return standardId != 0xFF; }
```

`0xFF` 是引擎的「**不在标准空间里**」。而 **lite(ESL)文件按定义就不在标准空间里**:

```cpp
[[nodiscard]] bool IsLite() const noexcept { return ((flags >> 9) & 1) != 0; }
[[nodiscard]] uint16_t GetId() const noexcept { return IsLite() ? liteId : standardId; }
```

form id 前缀 `0xFE000000 + liteId * 0x1000`(`ModManager.cpp` 与
`Code/components/es_loader/TESFile.cpp` 各自逐字写着同一个公式)。

**所以 `standardId != 0xFF` 对 lite 文件是真还是假,本仓库没有任何证据** ——
逐条查过:

| 可能的证据来源 | 查证结果 |
| --- | --- |
| 引擎头文件(CommonLibSSE 的 `TESFile`) | **不在这个树里**。全仓唯一的 `TESFile.h` 是 `Code/components/es_loader/` —— 本仓库**自己**给服务端写的加载器模型:它 `Setup(uint8_t aStandardId)` 与 `Setup(uint16_t aLiteId)` 分开赋值、**没有哨兵**(`ESLoader::LoadLoadOrder` 里两个计数器从 0 各自加一),所以它描述的是**我们自己**的模型,不是引擎的行为 |
| 联网查 | 已实测不可用(搜索端点未配置) |
| 实机 | 本机只有 1.5.97,且 §0/§1 禁止本地编译,拿不到可运行产物 |
| 既有日志 | 这个判断**从来没有打过日志**,所以历史日志里也没有答案 |

**两个答案的后果相反:**

| 答案 | 后果 |
| --- | --- |
| lite 文件**通过**(`standardId` 是 `0xFE` 之类) | lite 路径正常工作:`ModSystem::HandleMods` 填 `m_liteToServer`,`GetServerModId` 的 `0xFE` 分支解析 ESL form id |
| lite 文件**被丢掉**(`standardId` 就是 `0xFF`) | 该 ESL 服务端**从来不知道** ⇒ 它加的每个 form **静默失去服务端 id**;开了 mod policy 的服务器还会要求玩家「安装」一个他已经装了的 mod —— **一个他无法解决的拒绝** |

**而且这不是小众问题**:1.6.1130+ 随游戏出货的 **`_ResourcePack.esl` 本身就是 ESL**,而它就是
**`liteId == 0`** 那一个(`DiscoveryService::IsDefaultModlist` 的期望列表里逐字写着它)。
这与 §132 的 mods-mismatch 方向缺陷**同源**:两条都只对**第一个 ESL** 敏感 ——
那次撞的是**方向哨兵**(`Id == 0`),这次怀疑的是**过滤器**(`standardId != 0xFF`)。
同一个结构事实(`liteId` 从 0 发号),两个不同位置的中招。

### 133.2 修法:把它变成**下一次日志就能回答**的形式

`§1.4` 已经为另一个「本机没有证据」的缺口定了做法:**让用户日志自己回答**。这里照做。

**(a) 报出被丢掉的每个文件**(`TransportService.cpp` 构建认证包的那个循环),带三个决定字段:

```cpp
spdlog::info("mod '{}' is not loaded by the standard id rule: isLite={} standardId={:#04x} liteId={:#06x}",
             pMod->filename, pMod->IsLite(), pMod->standardId, pMod->liteId);
```

上限 8 条(`kMaxReportedSkips`):400 个插件的整合包不能把一次连接刷成一面墙,而**八条足够
回答「丢的里面有没有 lite」**。

**(b) 两侧各一行汇总**:

```text
mod list: N reported (L lite), K dropped by the standard id rule
server mod list: M entries, S lite; lite mappings established: T, not found locally: U
```

判定表:

| 观测 | 结论 |
| --- | --- |
| `L > 0` | lite 文件通过 ⇒ **问题关闭**,第 2 条后果不成立 |
| `L == 0`,且被丢文件里有 `isLite=1` | lite 被丢掉 ⇒ **确认缺陷** |
| `S > 0` 而 `T == 0` | 同一个缺陷的另一面(服务端有 lite、客户端一条映射也没建立) |

**(c) 等级必须是 `info`,而这是一条可查的判据。**
`Code/client/TiltedOnlineApp.cpp` 里 `rotatingLogger->set_level(spdlog::level::debug);` 是
**注释掉的**,所以出货构建的等级是 `info` —— 写成 `debug` 就是把答案写进没人看得到的日志。
(§1.4 当年就是被这一点绊住的:它依赖的那行 `spdlog::debug` 从来不打印。)

### 133.3 门禁:守住「可判定性」本身

`Tools/Scripts/check_lite_mod_path.py` **不判引擎行为**(它判不了),只守住两件会**静默退化**的事:

| 判据 | 为什么 |
| --- | --- |
| 丢掉文件的那条日志存在、**按参数**点名 `->IsLite()` / `->standardId` / `->liteId`、且不是 `debug`/`trace` | 少了它问题重新不可判定;等级写低了等于没写 |
| 两侧的计数还在报(`reported` / `dropped by the standard id rule` / `lite mappings established`) | 没有计数只能看到「有文件被丢」,看不到「丢的里面有没有 lite」 |
| lite 路径本身还接着(`m_liteToServer.emplace(` / `hiByte == 0xFE` / `m_liteToServer.find(`) | **与答案无关**:就算答案是「通过」,这条路径断了也一样坏 |
| `Mod` 的 `IsLite()` / `GetId()` / `standardId != 0xFF` 与两条 `static_assert(offsetof(Mod, ...))` 还在 | 日志读的就是这些;改名会让它要么编不过,要么打印别的字段 |
| `docs/FOLLOW-UP.md` 第 6 节仍存在且仍点名该门禁 | **缺口与它的仪器一起退休**,不许只删一个 |

第二条判据的措辞值得记下来:**按参数判,不按格式串判**。格式串可以继续写着 `standardId`
而参数打印的是 `pMod->flags` —— 而**参数才是承载答案的那一半**。

### 133.4 反向验证(变异测试)

四个变异,每个都**先确认已落到磁盘**:

| 变异 | 门禁诊断 |
| --- | --- |
| 那条日志降成 `spdlog::debug` | **两条**:`is not at a level the shipped build prints` + `uses ['spdlog::debug'], which the shipped log level filters out` |
| 参数 `pMod->standardId` 换成 `pMod->flags` | `no longer names ['->standardId']` |
| 删掉 `m_liteToServer.emplace(...)` | `"m_liteToServer.emplace(" is gone - the receive side stops recording which server id a lite mod got` |
| `docs/FOLLOW-UP.md` 的 `## 6.` 改名成 `## 6-X.` | `the open item about lite files (section 6) is gone while the instrument remains` |

**变异 4 当场暴露了门禁自己的漏洞,已修 —— 这是本节最值得抄走的一条。**
第一版的锚是 `'## 6.' in doc`,而它**同时匹配子标题 `## 6.1`**:于是
「**把主干标题改名、正文照样留着**」被读成「记录还在」,**最重要的那个动作(把待验项删掉)
反而不会失败**。改成顶层标题正则(`^## 6\. `,编号后必须有空格)后才抓得住。

> **教训**:门禁的锚要锚在**主张**上,不能锚在**出现过的字符串**上。
> `'## 6.'` 是字符串,`'## 6. '`(顶层标题)才是主张。
> **锚的宽度就是门禁的可信度** —— 与 §131.3(不认虚函数)、§132.4(不认驼峰)同一个形状:
> 收窄一个字符类或少认一种形状,门禁就开始说假话。

### 133.5 行为**故意未改**,理由写在这里

把 `IsLoaded()` 改成恒真(或改成 `IsLite() || standardId != 0xFF`)**不是**修法:

- 恒真会把**被禁用**的插件也上报,服务端于是认为你有你没装的东西 ⇒ mod policy 反向误报;
- `liteId` 在「未加载的第一个 ESL」与「已加载的第一个 ESL」上**都是 0**,所以
  「`liteId != 哨兵`」这种写法需要知道一个**同样没有证据**的哨兵值。

**在拿到证据之前,把判断换成一个同样没有证据的写法,只是把不确定性换个地方放。**
`docs/FOLLOW-UP.md` §6.4 记了拿到日志之后的两个候选修法,以及为什么它们也各有一个前提。

> **教训**:**「我不知道」有第三种处理方式,而且它比「猜」和「不写」都好:
> 把问题变成下一次日志就能回答的形式。**
> 猜测会把不确定性藏进代码(这次两个答案的后果,一个是功能正常、一个是玩家被**永久拒绝**);
> 不写则会让下一个人重新踩一遍。判据是**「这一行日志能不能把问题收敛成是/否」** ——
> 这里一行汇总加三条明细就够。

---

## 134. 2026-10-11 场次:UI 到游戏的桥**三层各写一遍**,而没有任何东西同时看三层

> **锚点**:`Tools/Scripts/check_overlay_bridge.py`(新)、
> `Code/tp_process/ProcessHandler.cpp`、`Code/client/Services/Generic/OverlayClient.cpp`、
> `Code/skyrim_ui/src/app/services/client.service.ts`、`Code/skyrim_ui/src/typings.d.ts`。

### 134.1 判据:一个按钮要走三层,而三层是三份独立的手写清单

| 层 | 文件 | 作用 | 缺了会怎样 |
| --- | --- | --- | --- |
| 1 声明 | `Code/skyrim_ui/src/typings.d.ts`(`interface SkyrimTogether`) | 告诉 TS「overlay 对象有这个方法」 | 没有它 TS 编不过(所以这层**总是**齐全) |
| 2 注册 | `Code/tp_process/ProcessHandler.cpp::OnContextCreated` 的 `SetValue(...)` | 把 JS 函数名交给 overlay 的 IPC | **名字根本不离开渲染进程**:调用在浏览器里抛异常,没有日志、没有半截动作 |
| 3 派发 | `Code/client/Services/Generic/OverlayClient.cpp` 的 `eventName == "…"` if 链 | 把名字映到游戏侧的 `Process…Message()` | 请求**跨过进程边界后被静默丢掉**:不报错、无效果 |

`docs/PITFALLS.md` §0 的提交前清单里逐字写着「新 JS→C++ 调用必须在
`ProcessHandler.cpp::OnContextCreated` 注册」—— **又一个只写在清单上的规则**
(§131、§132、§133 之后的第四次同形)。而这一轮实测发现,**两条缺陷正好落在不同的两层**:

**(a) `openPlayGuide` —— 只缺第 2 层(活缺陷)。** 第 3 层早就写好了:

```cpp
// Code/client/Services/Generic/OverlayClient.cpp
else if (eventName == "openPlayGuide")
    ProcessOpenPlayGuide();
...
void OverlayClient::ProcessOpenPlayGuide()
{
    ShellExecuteW(nullptr, L"open", LR"(https://wiki.tiltedphoques.com/…/playguide)", nullptr, nullptr, SW_SHOWNORMAL);
}
```

按钮也接着(`Code/skyrim_ui/src/app/services/party-hint.service.ts` 把它挂成组队提示的动作),
字符串也译好了(14 种语言的 `COMPONENT.NOTIFICATIONS.OPEN_GUIDE`)——
**只差 `ProcessHandler` 里那一行注册**。后果:玩家单机推任务时弹出的组队提示上,
「打开指南」按钮**点了什么都不发生**;而 mock 实现了它,所以在浏览器里跑 mock 一切正常。

**(b) `reconnect` —— 第 2、3 层都没有(潜伏缺陷)。** typings 声明了、`client.service.ts` 调了、
mock 实现了;C++ **两层都没有**(`OverlayClient` 的派发链里从来没有 `reconnect`)。
按钮本身在 `root.component.html` 里被 **HTML 注释**包着:

```html
<!--
<button … [attr.data-style-active]="(view | async) === RootView.RECONNECT" (click)="reconnect()">
  {{ 'COMPONENT.ROOT.RECONNECT' | transloco }}
</button>
-->
```

而 `View.RECONNECT` 在**任何** TS 代码里都没有被发出过(全仓只有模板里那一处引用)。
所以今天点不到 —— 但那一行代码同时会把重连预算清零:

```ts
public reconnect(): void {
  skyrimtogether.reconnect();                 // ← 对象上没有这个方法
  this._remainingReconnectionAttempt = 0;     // ← 顺带把自动重连关掉
}
```

⇒ 一旦有人把注释放开,这个按钮会**既什么都不做、又让自动重连不再发生**(比没有它更糟)。
这正是 §43.8 那类「记为潜伏项」的形状。

**(c) 门禁自己抓出的第三条:`deactivate`。** 注册了、typings 声明了、UI 包了一层
(`ClientService.deactivate()`),而 **`OverlayClient` 没有分支** ⇒ 请求跨过边界后被静默丢掉。
这条不是我先看到的,是**判据 3 跑第一遍时报出来的**。

### 134.2 修法

1. **`ProcessHandler.cpp` 补 `openPlayGuide` 注册**(一行)。第 3 层不用动 —— 它本来就在。
2. **`reconnect` 改用已注册的原语**。自动重连那条路(`onDisconnect` 里)一直在用两步,
   而且它**是跑通过的生产路径**:

```ts
skyrimtogether.abandonAttempt();                       // 丢掉已经结束的那次尝试
this.connect(this._host, this._port, this._password);  // 用最后一次的地址重连
```

   于是 `typings.d.ts` 里那条 `reconnect()` 也删掉:**overlay 对象没有这个方法,
   而 typings 描述的就是 overlay 对象**。(`ClientService.reconnect()` 是另一回事,
   它是 Angular 侧自己的 API,继续存在,只是改用已注册的原语实现。)
3. **`deactivate` 补分支与实现**:

```cpp
else if (eventName == "deactivate")
    ProcessDeactivateMessage();
...
void OverlayClient::ProcessDeactivateMessage()
{
    SetUIVisible(false);
}
```

   `SetUIVisible` 就是 typings 上那句「Deactivate UI and release control」的**全部内容**
   (`InputService::SetCaptureEnabled(false)` + `OverlayService::SetActive(false)` + 隐藏光标),
   而且**同文件 `ProcessRevealPlayersMessage` 里就是这么直接调的** ——
   所以线程模型与既有写法一致,不需要新造一条队列。

### 134.3 门禁的四条判据(两条失败、两条报告)

| # | 判据 | 结果 |
| --- | --- | --- |
| 1 | typings 声明的每个方法都必须注册 | **失败** |
| 2 | UI 里每个 `skyrimtogether.<名>(` 调用都必须注册 | **失败** |
| 3 | UI **调用**的每个名字都必须有派发分支(`on`/`off` 除外) | **失败** |
| 4 | 反向:派发了却没注册 ⇒ UI 永远到不了那个分支 | **失败** |
| 5 | 注册了但 typings 没声明、UI 也没调 | **报告**(`toggleDebugUI` 由键盘驱动) |
| 6 | 声明了、注册了、但 UI 没人调 | **报告**(通道未使用) |

判据 3 特意限定在「UI **调用**的」名字上:没人调的名字不可能被静默丢掉,所以
**未使用的通道是报告项而不是失败项**;但它同时要求 `deactivate` 这种「UI 真的调了、
却没有分支」失败 —— `client.service.ts` 里那层包装**就是调用点**,这正是判据该看的东西。

**注册判据锚在形式上**:`SetValue("名", CefV8Value::CreateFunction`。
所以**注释里出现名字不算注册** —— 变异测试里注释仍然在,门禁照样报错。

### 134.4 反向验证(变异测试)

四个变异,每个都**先确认已落到磁盘**,合共 7 条诊断:

| 变异 | 门禁诊断 |
| --- | --- |
| 删掉刚补的 `openPlayGuide` 注册(= **还原原始缺陷**) | `typings.d.ts declares openPlayGuide() but ProcessHandler.cpp never registers it` + `client.service.ts:278 calls skyrimtogether.openPlayGuide(), which ... never registers` |
| 给 typings 加一个 `phantomTyped()` | 判据 1 指名它 |
| 在 UI 里加一个 `skyrimtogether.phantomCall()` | 判据 2 指名它 |
| 把 `deactivate` 的分支换成 `phantomAction`(= **还原第三条缺陷**) | 判据 3 指名 `deactivate` 缺分支;判据 4 同时指名 `phantomAction` 无注册、`openPlayGuide` 派发了却没注册 |

### 134.5 教训

> **「三层各写一遍」的桥,必须在**一次**检查里同时看三层 —— 只看一层永远发现不了缺口。**
> 两条缺陷落在**不同的两层**:`openPlayGuide` 缺第 2 层而第 3 层早就写好,
> `reconnect` 第 1 层有、第 2/3 层全无。§115 的「一条消息定义两次」在这里变成三次,
> 而**缺哪一层决定症状**:缺注册 ⇒ **抛异常**(至少看得见);
> 缺派发 ⇒ **静默丢掉**(连错误都没有)。后者才是要花一个下午的那种。

> **教训(同轮第二条)**:**mock 会把「桥没接上」这类缺陷完整遮住。**
> `openPlayGuide` 与 `reconnect` **在 mock 里都实现了**,所以浏览器里跑 mock 一切正常 ——
> 它们只会在真实客户端里坏。**mock 实现了真身不存在的方法时,那不是 mock 更完整,
> 而是契约已经分叉了。** 门禁因此把 UI 调用与 `app/mock/` **分开比对**,
> 让 mock 不能替真身担保。

---

## 135. 2026-10-11 场次:「搬过去就丢」的字段 —— 而通用版门禁**说假话**,所以扔掉

> **锚点**:`Tools/Scripts/check_wire_field_reads.py`(新,作用域两条消息)、
> `Code/encoding/Messages/RequestQuestSceneUpdate.h`/`.cpp`、
> `Code/client/Services/Generic/QuestService.cpp`。

### 135.1 判据:对称性检查证明不了「有人用」

`Tools/Scripts/check_wire_symmetry.py` 的证据是 **133 对读写、顺序一致** —— 它证明的是
**两个方向搬运同一批字段**。它**不问**接收端有没有用到那个值。而「搬过去就丢」是协议里
最安静的一类缺陷:**字节被消费、所有检查通过、字段背后的功能从来没发生过**。

`RequestQuestSceneUpdate`(客户端→服务端,场景开始/结束)就带着这样一个字段:

```cpp
// Code/client/Services/Generic/QuestService.cpp(修复前)
RequestQuestSceneUpdate update;
update.SceneId = sceneGameId;
update.QuestId = questGameId;
update.Stage  = stageId;
update.Status = stageId == RequestQuestUpdate::StageUpdateForced;   // <-- 这里
update.ClientQuestType = …;
update.SceneType = sceneType;
```

`stageId` 是**任务当前的阶段号**(该 lambda 捕获的 `pQuest->currentStage`),
而 `RequestQuestUpdate::StageUpdateForced` 的值是 **5**。于是这一行是
**「这个任务现在正好停在阶段 5」** —— 与它读起来的意思(「这是一次强制更新」)无关。

而服务端的 `OnQuestSceneChanges` **从头到尾没有读过它**:它用 `SceneType`(BEGIN/END)、
`SceneId`、`QuestId`、`Stage`、`ClientQuestType`,以及
`RequestQuestUpdate::StatusCode::SceneUpdate` 作为**去重行标签**(那是枚举值,不是这个字段)。

于是这个字段是:**1 个字节在线上 + 一行读起来像功能、实际是巧合的赋值 + 零个读者。**

### 135.2 先否掉的两个不存在的缺陷(与 §125 同形)

看到 `RequestQuestUpdate::StatusCode` 有 **6** 个值(`StageUpdate, Started, Stopped, Reset,
SceneUpdate, StageUpdateForced`)而 `NotifyQuestUpdate::StatusCode` 只有 **4** 个,先怀疑是
「同一枚举定义两次、值集不一致」。读完服务端那个 `switch` 后否掉:

```cpp
case RequestQuestUpdate::Started: notify.Status = NotifyQuestUpdate::Started; break;
case RequestQuestUpdate::StageUpdateForced: useAggressiveSend = true; [[fallthrough]];
case RequestQuestUpdate::StageUpdate:
    notify.Status = NotifyQuestUpdate::StageUpdate; …
case RequestQuestUpdate::Stopped: … case RequestQuestUpdate::Reset: …
default:
    spdlog::error("… unknown quest status {} …");
    return;
```

- 6 个值里有 5 个被处理;**`StageUpdateForced` 走 `[[fallthrough]]` 进 `StageUpdate` 并置
  `useAggressiveSend`**(绕过去重),正是场景 END 那条路要的;
- `SceneUpdate` 只作为**去重行标签**使用,从不作为 `Status` 出现在请求里;
- `default:` 安全丢弃并举 error(那是上一轮修的);
- **通知方向只会被赋那 4 个值之一** ⇒ 两个枚举值集不同是**对的**:强制状态是**服务端自己**
  生成的(给落后的队员补一次当前阶段:`newUpdate.Status = RequestQuestUpdate::StageUpdateForced;`
  然后回头喂给 `OnQuestChanges`),它不来自客户端。

**枚举值集不同 ≠ 缺陷**;只有当「一个方向能产生、另一个方向映射不了」时才是。

### 135.3 修法:端到端删掉那个字段

结构体字段、`operator==`、`SerializeRaw`、`DeserializeRaw`,以及客户端那一行写入者。

**这是协议改动**:同一个 commit 的客户端与服务端才互通(§0 的版本握手),
所以删一个 8 位字段是安全的 —— 而且删掉的这一位**在两种情况下都没有接收者**。

### 135.4 发布的门禁:作用域**显式**、且能担保

`Tools/Scripts/check_wire_field_reads.py`:**这两条场景消息的每个字段都必须在它到达的那个
handler 里被读**(不是被赋值)。

| 判据 | 为什么 |
| --- | --- |
| 字段必须在注册给该消息的 handler 体内出现为**读**(`.字段`/`->字段` 且后面不是 `=`) | 只赋值不读 = 搬过去就丢 |
| handler 必须还能被找到 | 改名/搬家时失败,而不是静默检查零个字段 |
| `SUBJECT` 里任一条消息**解析不出字段** ⇒ 失败 | 解析器形状变了要报错,不能退化成空转 |
| `EXEMPT` 里的条目若不再对应真实字段 ⇒ 失败 | 豁免不会过期腐烂 |

**作用域写成显式清单**(两条消息 + 两个 handler),而不是全仓扫描 —— 原因见 §135.5。

### 135.5 被扔掉的通用版(负结果)

通用版(全部消息、每个字段)先写出来跑过:

- **按字段名全树搜 ⇒ 没有意义**:`Status`、`Id`、`Stage` 同时属于十几条消息,
  任何一条读自己的字段就会替别的消息担保。这一版**漏报**(实测:它把本轮这个字段判为"有人读",
  因为 `NotifyQuestUpdate::Status` 在客户端被读了)。
- **改成「只在注册给该消息的 handler 体内搜」⇒ 会报假警**:handler 把包**转发给 helper**
  是这个仓库的常见写法,于是第一版把 `CharacterSpawnRequest.FactionsContent`、
  `StringCacheUpdate.Values`、`NotifyProjectileLaunch.DeferInitialization` 等
  **功能明明在用**的字段报成「没人读」(实测 7 条;再跟一层调用后变成 10 条,依然假)。
  `StringCacheUpdate` 尤其刺眼:那条路径上一轮刚修过并写了测试。

**一个会说假话的门禁会被关掉**,所以它不发布。发布的是能做成精确的那一半:
两条场景消息 + 两个**已逐行读完**的 handler —— 对这两条而言,「字段名出现在 handler 体内」
与「有人读它」是等价的,**因为转发链我已经手工核过**。
判据不是「门禁覆盖得多不多」,而是**「它说的话能不能担保」**。

### 135.6 反向验证(变异测试)

三个变异,每个都**先确认已落到磁盘**:

| 变异 | 门禁诊断 |
| --- | --- |
| **把 `Status` 字段加回结构体**(= 还原本轮修掉的缺陷) | `OnQuestSceneChanges never reads RequestQuestSceneUpdate.Status; … the receiver drops it` |
| handler 改名 `OnQuestSceneChanges` → `…Renamed` | `OnQuestSceneChanges was not found; the handler this gate reads RequestQuestSceneUpdate in has been renamed or moved` |
| `EXEMPT` 加一条不存在的 `NotifyQuestSceneUpdate.SceneType_phantom` | `is exempt but no longer a field of NotifyQuestSceneUpdate; drop the exemption` |

变异 1 与 2 同时注入时只报一条(handler 找不到就先 `continue`)—— 记下来,免得下次误判成
「变异没生效」:**判据的短路顺序会遮住后面的判据**,所以变异要**分别**跑。

### 135.7 教训

> **只查「搬运是否对称」的检查,管不到「搬过去有没有人用」。**
> `check_wire_symmetry` 覆盖 133 对读写、一条没漏 —— 而缺陷正好长在它的**判据边界之外**:
> 两侧都忠实地搬运了一个没人读的字节。**每加一条对称性检查,都要问一句
> 「它证明了什么、没证明什么」**,然后把没证明的那部分单独做成一条**能担保**的判据
> (哪怕作用域很小)。

> **教训(同轮第二条)**:**写入者拿一个数据字段当常量比较,是「读起来像功能、实际是巧合」的典型。**
> `stageId == StageUpdateForced`:左边是阶段号、右边是枚举常量 5 —— 它**几乎永远为假**,
> 除了阶段正好是 5 的任务。如果这个字段有读者,这就是一个只在特定阶段才触发的怪 bug;
> 因为它没有读者,它只是一颗哑弹。**两种情况都该删掉这一行** ——
> 区别只在于「删掉之后要不要补功能」。

---

## 136. 2026-10-11 场次:一个设置名对应**两个对象**,而「第一个」由静态初始化顺序决定

> **锚点**:`Tools/Scripts/check_console_settings.py`(新)、
> `Code/server/Services/CommandService.cpp`。

### 136.1 判据:自注册 + 按名字查 ⇒ 重名不是风格问题,是语义分裂

控制台设置的机制是「**构造即注册**」:

```cpp
// Code/components/console/Setting.h
SettingBase(SettingBase*& parent, const char* n, const char* d, Type t, SettingsFlags f)
    : next(parent), flags(f), type(t), name(n), desc(d)
{
    parent = this;          // <-- 挂进一条全局链表
}
static inline SettingBase*& ROOT() noexcept { static SettingBase* root{nullptr}; return root; }
```

`RegisterServerSettings()`(它只是 `aConReg.BindStaticItems()`)把整条链表交给注册表,
`AddSetting` 逐条 `m_settings.push_back(apSetting.get())` —— **没有任何按名字去重**。
而按名字取用时:

```cpp
// Code/components/console/ConsoleRegistry.cpp
SettingBase* ConsoleRegistry::FindSetting(const char* acName)
{
    auto it = std::find_if(m_settings.begin(), m_settings.end(),
        [&](SettingBase* apSetting) { … return std::strcmp(settingName, findName) == 0; });
    if (it == m_settings.end()) return nullptr;
    return *it;                                  // <-- 第一个同名的赢
}
```

三条后果叠起来,构成一个最难查的组合:

1. **静默**:没有日志、没有断言 —— 两个对象都有值,只是其中一个永远是构造时的默认值;
2. **不确定**:构造顺序 = **跨翻译单元的静态初始化顺序**,标准里未指定 ⇒ 谁赢**连构建之间都不稳定**;
3. **看起来正常**:ini 里有那一行、控制台能显示它、改它也不报错 —— 只是可能改到了另一个对象。

### 136.2 实测:21 条声明里恰好一处重名,而两处读者都是真的

| 设置名 | 声明 | 读者 |
| --- | --- | --- |
| `LiveServices:bAnnounceServer` | `Code/server/GameServer.cpp`(与其他 12 条服务器级设置放在一起) | `GameServer::IsPublicServer()`(`return bAnnounceServer;`)—— 公开服务器列表判定;并被 `AllowsAutoPartyJoin()` 读:`return bAutoPartyJoin && !IsPublicServer();` |
| `LiveServices:bAnnounceServer` | `Code/server/Services/CommandService.cpp` | `/settime` 权限:`if (pPartyService->IsPlayerLeader(...) && !bAnnounceServer)`,注释写着「Party leader allowed on private servers only」 |

**两个方向的后果都错**,取决于链接顺序:

- ini 值落进 **CommandService** 的那份 ⇒ `IsPublicServer()` 恒为 `false` ⇒
  **管理员把 `bAnnounceServer` 设为 `true` 也不会有任何效果**(服务器不会被列进公开列表),
  而且 `AllowsAutoPartyJoin()` 在公开服务器上**仍为真**;
- ini 值落进 **GameServer** 的那份 ⇒ **公开服务器上队长仍然能改时间**(权限判定读到的是默认 `false`)。

### 136.3 修法:一处声明 + 一个访问器

`CommandService.cpp` 删掉自己的副本,改读 `GameServer::IsPublicServer()` ——
它本来就 `#include <GameServer.h>`(第 4 行),而这个访问器也是 `AllowsAutoPartyJoin()` 已经在用的,
所以「谁在读它」只剩一个答案。文件顶部留一段注释写明**为什么**这里不能有第二份声明
(那份注释正好也成了下面那条教训的现场)。

### 136.4 门禁:只需要声明字符串,不需要调用图

`Tools/Scripts/check_console_settings.py`:**任何 `Console::Setting` 名字只能声明一次**。
这是**能担保**的判据 —— 它只读声明处的字符串字面量,不依赖类型系统、不依赖调用图
(与 §135.5 那个被扔掉的通用版形成对照:那个判据做不到担保,所以没发布)。
另加防退化下限(少于 15 条声明即失败),否则换个写法(宏、改名、换目录)就能让门禁空转。

### 136.5 门禁自己踩了本仓库已经记录过的坑

第一版**没有剥注释**,而我在 `CommandService.cpp` 里写的说明注释**逐字引用了那条被删掉的声明**:

```cpp
//   Console::Setting bAnnounceServer{"LiveServices:bAnnounceServer", ...};
```

于是门禁把**注释**当成声明,报告「重复仍然存在」,并且指名 `CommandService.cpp:15` ——
也就是注释那一行。这与 §3.1 记下的旧口径是**同一个坑**:
`gen_ae_to_se_map.py` 当年**把注释掉的调用当成活的**,让覆盖率数字虚增。

现已先剥 `//` 与 `/* */` 再匹配。**教训的落点很具体:任何扫源码的门禁,第一步都该是剥注释** ——
因为「我在注释里引用了它」和「它还在」在正则眼里没有区别。

### 136.6 反向验证(变异测试)

| 变异 | 门禁诊断 |
| --- | --- |
| **把重复声明加回 `CommandService.cpp`**(= 还原本缺陷) | `"LiveServices:bAnnounceServer" is declared 2 times (GameServer.cpp:36, CommandService.cpp:24); a name resolves to the first registration, so the others never receive the ini value and keep their default - and which one wins depends on static initialisation order` |
| **同一文件内制造重名**(`Gameplay:bEnablePvp` 的键改成 `Gameplay:bEnableGreetings`) | `"Gameplay:bEnableGreetings" is declared 2 times (GameServer.cpp:41, GameServer.cpp:42)` |
| 把防退化下限抬到 999 | `only 20 Console::Setting declaration(s) were found, below the floor of 999; the declaration shape changed and this gate is no longer looking at the settings it claims to` |

### 136.7 教训

> **在「自注册 + 按名字查」的注册表里,一个名字出现两次不是风格问题,是语义分裂。**
> 两个对象、一个地址、`find_if` 选第一个,而第一个是谁由**静态初始化顺序**决定 ——
> 静默、不确定、且看起来完全正常。判据不是「名字起得一致不一致」,
> 而是**「这个名字被解析到几个对象」**。
> 这条判据可以做成门禁,因为**它只需要声明处的字符串**。

> **教训(同轮第二条)**:**门禁的判据要锚在「代码」上,不能锚在「文本」上 —— 注释也是文本。**
> 剥注释在本仓库的采集器里早就是标准动作(§3.1 的旧口径正因如此虚增了 3 个 id),
> 而我写这条门禁时又漏了一次。**先剥注释,再匹配** —— 这是扫源码门禁的第零步。

---

## 137. 2026-10-11 场次:玩家的四条消息是**英文散文**,而聊天把系统消息**当翻译键**渲染

> **锚点**:`Tools/Scripts/check_ui_contract.py`(第 4 条判据)、
> `Code/client/Services/Generic/CommandService.cpp`、`Code/client/Services/Generic/OverlayService.cpp`、
> `Code/skyrim_ui/src/assets/i18n/en.json`。

### 137.1 判据:同一个字符串,在 `| transloco` 后面是**键**,在别处是**文本**

链路四段,每段都可查:

```cpp
// 1. 客户端发(Code/client/Services/Generic/OverlayService.cpp)
void OverlayService::SendSystemMessage(const std::string& acMessage)
{
    auto pArguments = CefListValue::Create();
    pArguments->SetInt(0, kSystemMessage);   // Code/encoding/ChatMessageTypes.h: kSystemMessage = 0
    pArguments->SetString(1, acMessage);     // <-- 原样送过去
    m_pOverlay->ExecuteAsync("message", pArguments);
}
```

```ts
// 2. TS 收:MessageTypes.SYSTEM_MESSAGE = 0(两侧同一个值)
//    chat.component.ts: messageTypeToClassName(SYSTEM_MESSAGE) -> 'system'
```

```html
<!-- 3. 渲染(chat.component.html) -->
<span class="app-chat-message" *ngIf="message.typeClass === 'system'">{{
  message.content | transloco: message.translationParams
}}</span>
<span class="app-chat-message" *ngIf="message.typeClass !== 'system'">{{
  message.content
}}</span>
```

```ts
// 4. 查不到怎么办(transloco-root.module.ts)
defaultLang: 'en', fallbackLang: 'en',
missingHandler: { useFallbackTranslation: true },
```

⇒ **系统消息的内容是当作翻译键查的**;两边都查不到时 Transloco **把键本身显示出来**。

于是四条 `SendSystemMessage` 字面量(全是英文句子)的行为是:**英文玩家看到正确句子**,
**其余 13 种语言看到的是英文**。

### 137.2 更刺眼的是:其中两条**早就译好了**

| 站点 | 修复前(散文) | 已存在的键 |
| --- | --- | --- |
| `CommandService.cpp`(无权限) | `"Only an admin, or the party leader on a private server, can change the time."` | **`SERVICE.COMMANDS.NOT_ADMIN`**(14 份语言文件全有译文:de「Du musst Administrator sein…」、ja「このコマンドを使用するには管理者権限が必要です」) |
| `CommandService.cpp`(时间非法) | `"That is not a time. Use /settime HH MM, with hours 0-23 and minutes 0-59."` | **`COMPONENT.CHAT.SET_TIME_INVALID_ARGUMENTS`**(客户端**本地**校验同一条件时用的就是它) |
| `CommandService.cpp`(传送失败) | `"Teleporting to player failed."` | 无 —— 本轮新增 `SERVICE.COMMANDS.TELEPORT_FAILED` |
| `OverlayService.cpp`(同上) | 同上 —— **同一个句子写了两遍** | 同一个新键 |

**门禁从 §132 起就一直在指着它**:`check_ui_contract.py` 每次都报告
`SERVICE.COMMANDS.NOT_ADMIN`「没有任何错误码能到达、也没有 UI 源码按名字要它」。
我两次把它读成「不可达的键」,没有追问**「那这条消息现在从哪来」**。
**一条没有调用者的翻译键,往往意味着它的调用者把句子硬编码了。**

### 137.3 修法与「不强造假译文」

- 前两条改用**已存在、已翻译**的键 ⇒ 零翻译成本,14 种语言立刻看到母语;
- 传送失败两条改用新增的 `SERVICE.COMMANDS.TELEPORT_FAILED`;
- 新键**只加进 `en.json`**:另外 13 份文件**不放假译文**(把英文句子填进 `de.json` 是「看起来完整」的谎),
  而 `fallbackLang: 'en'` 本来就是配置好的行为 —— 它们今天看到的也正是这句英文。
  门禁每次运行会**报告**「缺在至少一种语言里」,译者照着补。
  **判据是「玩家不会看到键名」,不是「每种语言都有一行」** —— 后者可以造假,前者不能。

### 137.4 门禁的第 4 条判据

**客户端送进聊天窗的每一个字面量,都必须在回退语言(`en.json`)里是一个键。**

| 判据 | 为什么 |
| --- | --- |
| `SendSystemMessage("…")` 的字面量必须在 `en.json` 里存在 | 聊天用 `content \| transloco` 渲染系统消息,这是键 |
| 找不到任何 `SendSystemMessage` 字面量 ⇒ 失败 | 助手改名/搬家时失败,而不是退化成空转 |
| **报告**:en 里有、别的语言里缺 | 降级为英文是**配置行为**,不是缺陷 —— 报告给译者,不判失败 |

### 137.5 反向验证(变异测试)

| 变异 | 门禁诊断 |
| --- | --- |
| **把无权限那条改回散文**(= 还原本缺陷) | `CommandService.cpp:56 sends "You must be an admin to use that command." to the chat, which renders system messages with content \| transloco; that is a translation key, and en.json has no such key, so what the player sees is the string itself` |
| **从 `en.json` 删掉 `TELEPORT_FAILED`** | `CommandService.cpp:85, OverlayService.cpp:606 sends "SERVICE.COMMANDS.TELEPORT_FAILED" …` —— **两处站点被一次点名** |

### 137.6 教训

> **「玩家看到的那句话」如果是硬编码的,那么产品承诺的 N 种语言里只有 1 种是真的。**
> 这条缺陷之所以能活下来,是因为**它在英文环境下完全正确**:键就是句子、查不到就原样显示,
> 于是开发者(和英文玩家)看到的一切都正常。**判据必须是「渲染路径怎么处理这个字符串」**,
> 而不是「这句话读起来对不对」—— 同一个字符串,在 `| transloco` 后面是**键**,在别处是**文本**。

> **教训(同轮第二条)**:**报告项要当线索读,不能当噪音读。**
> 一个没有调用者的翻译键,先别急着删:它可能正指着一条硬编码的句子。
> §132 起就有这条报告,我两次读成「死键」,直到顺着「那这条消息从哪来」才找到四条散文。

---

## 138. 2026-10-11 场次:证据表里的一个偏移,让「往运行中的游戏写字节」**静默通过**

> **锚点**:`Tools/ida/patch_offsets_1597.py`(`MODERN_ONLY`)、
> `Tools/ida/st_patch_offsets_1597.tsv`、`Tools/Scripts/check_patch_offsets.py`。

### 138.1 先确认代码是对的

```cpp
// Code/client/Games/Skyrim/BSGraphics/BSGraphicsRenderer.cpp
if (auto* pFrameEnd = GamePatch::Anchor(77246, "frame end"))
{
    if (VersionDb::Get().IsLegacyFormat())
    {
        RenderFrameEnd = reinterpret_cast<decltype(RenderFrameEnd)>(pFrameEnd);
        TP_HOOK_IMMEDIATE(&RenderFrameEnd, &Hook_RenderFrameEnd);   // 1.5.x:钩锚点函数本身
    }
    else
    {
        GamePatch::SwapCall(GamePatch::At(pFrameEnd, {9}, "frame end"), StopTimer, &Hook_StopTimer, "frame end");
    }                                                               // 1.6.x:换掉里面那条 call
}
```

⇒ `{9}`(不传 1.5.x 偏移)是**故意**的:pre-AE 平台上走的是另一条路。
所以本轮**不是代码 bug**,而是证据与门禁两侧的两个洞:

### 138.2 洞一:判断写在描述里,就一定会丢

生成器的输入是 `(ae_id, offset, width, what)` 元组,其中一条逐字写着:

```python
(77246, 9, 5, "frame end (call site, 1.6 only)"),
```

**人已经判断了「这个 call site 只有 1.6 有」** —— 但那个判断只是**描述字符串的一部分**,
工具照样去对齐,并在 1.5.97 的同一函数里找到一条 `call`(mnemonic 与长度都吻合),
于是**输出**写下 `0x9`。**输入里的判断没有到达输出。**

### 138.3 洞二:豁免把危险的那个方向一起放过了

`check_patch_offsets.py` 的 `UNUSED_LEGACY_OK` 里列着 frame end,理由写得对:
「表里有、代码不用的偏移不算缺陷」。但它的后果是:

| 改动 | 改之前 | 改之后 |
| --- | --- | --- |
| 把 `0x9` **加回代码**(`{9}` → `{9, 0x9, "1.5.97"}`) | **静默通过**(表记 `0x9`、代码用 `0x9`,两边一致) | **失败**,并引用表里的理由 |
| 表里**多给**一个代码不用的偏移 | 被豁免,完全静默 | 输出 1 条 **note** |

而 `GamePatch::SwapCall` **只检查那个字节是不是 `0xE8`**:

```cpp
if (*pSite != 0xE8) { error(… no call at …); return false; }
int32_t displacement = 0;
std::memcpy(&displacement, pSite + 1, sizeof(displacement));
aOriginal = reinterpret_cast<TFunc>(pSite + 5 + displacement);
```

⇒ 1.5.97 上 +0x9 处那条 `call` **如果不是同一条**,就会被**静默重定向**:
功能坏掉、没有日志、而且发生在**正在运行的游戏进程里**。

### 138.4 修法:让两个产物说到一起

1. **生成器**:`MODERN_ONLY: dict[tuple[int, int], str]`,以 `(ae_id, offset)` 为键
   (不会漂到别的站点),命中时输出 `-` 并写明原因 —— 判断从**描述**变成**结构化字段**;
2. **证据表**:该行改为
   `77246  0x9  -  no 1.5.x patch site: the pre-AE path hooks the anchor itself  frame end (call site, 1.6 only)`;
3. **门禁**:frame end 从 `UNUSED_LEGACY_OK` 移到 `NO_LEGACY_EXPECTED`(理由相符),
   `UNUSED_LEGACY_OK` 清空但保留机制。

**生成器本机跑不了**(需要两份 exe + id 映射 + anchor map),所以第 2 步是按修好的生成器
**逐字**手写的;门禁验的是**表与代码是否一致**,不是生成器能不能运行 —— 这一点必须说清楚,
否则「手写的行」就成了第二个未验证的产物。语法用**内存内 `compile()`** 确认
(不能用 `py_compile`:它会试图写 `__pycache__` 而被沙箱拒绝,报的是 `WinError 5`,不是语法错误 ——
**别把权限错误当成语法错误读**)。

### 138.5 反向验证(变异测试)

| 变异 | 结果 |
| --- | --- |
| **把 1.5.x 偏移加回代码**(当年静默通过的那个改动) | **失败**:`patch 'frame end' applies a 1.5.x offset (0x9) that st_patch_offsets_1597.tsv does not record - the row says 'no 1.5.x patch site: the pre-AE path hooks the anchor itself'. A number nobody measured must not be written into a running game` |
| **把表改回 `0x9`**(产物退化) | 1 条 note:`'frame end': st_patch_offsets_1597.tsv records a 1.5.x offset (0x9) that the call site does not pass; the patch is skipped on 1.5.x`;门禁仍 OK(这个方向不是缺陷,但从「被豁免」变成「被报告」) |

### 138.6 教训

> **「不是缺陷」的豁免,会把它旁边那个**真**缺陷一起豁免掉。**
> `UNUSED_LEGACY_OK` 的理由是对的,但它让「把这个偏移加回代码」也通过 —— 而后者是
> **往正在运行的游戏里写字节**。看豁免要问**它挡住了什么**,不只**它放过了什么**:
> 如果一条判据的两个方向一个失败、一个被豁免,那个被豁免的方向就是**绕过路径**。

> **教训(同轮第二条)**:**工具的输入里有判断,不代表输出里有。**
> `"frame end (call site, 1.6 only)"` 是人的判断,工具照样输出对齐结果。
> **判断写在描述里就一定会丢**,要写成**结构化字段**,否则下游只看到「有偏移」。

---

## 139. 2026-10-11 场次:「卡住了」的那条日志说不出**是谁**;以及 648 条日志调用的机检

> **锚点**:`Tools/Scripts/check_log_format_args.py`(新)、
> `Code/client/Services/Generic/InventoryService.cpp`。

### 139.1 判据:占位符数量 == 实参数量(两个方向都坏,坏法不同)

| 方向 | fmt 的行为 | 后果 |
| --- | --- | --- |
| **占位符 > 实参** | `fmt::format_error` **在写这行的那一刻抛出** | 异常落在当时有效的处理器上;**只在罕见路径触发的日志从没被执行过** ⇒ 一条会崩的日志比没有日志更糟 |
| **实参 > 占位符** | 只格式化它要的参数,**多出来的值一个都不进日志** | 静默:开发者以为打了,实际没有 |

### 139.2 实测:648 条里恰好一条,而它正好是「卡住了」那条

```cpp
// Code/client/Services/Generic/InventoryService.cpp(修复前,同一段四行)
440: spdlog::debug(__FUNCTION__ ": actorId {:X} naked check is irrelevant {}",    pActor->formID, pActorName);
446: spdlog::debug(__FUNCTION__ ": actorId {:X} with naked check deadline {}",    pActor->formID, pActorName);
452: spdlog::debug(__FUNCTION__ ": actorId {:X} naked check deadline expires {}", pActor->formID, pActorName);
455: spdlog::debug(__FUNCTION__ ": but still WaitingForAssignment",               pActor->formID, pActorName);
```

455 行的分支含义是「截止时间到了,而这只 actor **仍然**带着 `WaitingForAssignmentComponent`」——
**整个函数里唯一在说「卡住了」的那条日志**,而它既不打 `actorId` 也不打名字:
`pActorName` 在 432 行取好(`pActor->baseForm->GetName()`)、传进来、然后被丢掉。
⇒ **它是那一段里唯一需要用来定位的日志,恰好说不出是谁。**
这与 §133 是同一条道理的又一处落点:**诊断行必须能回答「是谁」。**

### 139.3 门禁的做法与它的守门

`Tools/Scripts/check_log_format_args.py`:

- 解析 `spdlog::<level>(...)`(含 `logger->` 形式与**跨行调用**);第一个参数是**字符串字面量**
  (允许与 `__FUNCTION__` 拼接 —— 本仓库的写法)时,数占位符:`{}`/`{:X}`/`{:#04x}` 都算,
  **`{{`/`}}` 是字面量花括号,不算**;
- 格式串是**变量**的调用**跳过**(不猜),但 **跳过比例 > 35% 即失败** ——
  否则「跳过」会把这个门禁悄悄掏空(**一个把大部分输入跳过的扫描器,报告的是它自己的覆盖率,不是代码的质量**);
- 解析到的调用 **< 300 条即失败**(调用写法变了要报错,而不是空转);
- **剥注释但保留字符串字面量**(格式串就在字面量里):§136 那条「先剥注释」在这里必须**只剥注释**。

### 139.4 反向验证(变异测试)

| 变异 | 结果 |
| --- | --- |
| **还原 455 行**(去掉两个占位符) | `debug has 0 placeholder(s) and 2 argument(s)` ✓ |
| 把 `main.cpp` 的**跨行**日志改成 4 占位符 / 3 实参 | `info has 4 placeholder(s) and 3 argument(s)` ✓(同时证明跨行调用被正确解析) |
| 在同一条调用里写入 `{{literal braces must not count}}` | **仍然通过** ✓ —— 转义不会误报 |

### 139.5 教训

> **向「罕见路径」写日志的那一行,是最不可能被测到的一行 —— 所以它必须是机械可验证的。**
> 648 条里错 1 条,人眼读是读不出来的;而按「占位符数 == 实参数」这**一条二值判据**扫一遍,
> 它自己就浮出来了。**能被机检的性质,不要交给 review。**
> 这条判据同时消掉了两个方向:一个会崩、一个静默。

---

## 140. 2026-10-11 场次:「还剩几次重连」的占位符**原样打给了玩家**

> **锚点**:`Tools/Scripts/check_i18n_params.py`(新)、
> `Code/skyrim_ui/src/app/services/client.service.ts`。

### 140.1 判据:文本要的占位符,必须在**渲染它的地方**被交进去

Transloco(以及任何模板引擎)的行为是**只替换拿到的参数,拿不到的原样留在文本里**。
于是「键被引用」≠「这句话能读」—— 判据必须落在**渲染路径**上。

### 140.2 实测

```jsonc
// en.json / de.json / ja.json …(14 份语言文件全都有这个占位符)
"SERVICE.CLIENT.CONNECTION_LOST": "… trying to reconnect. {{remainingReconnectionAttempt}} attempts left.",
```

```ts
// Code/skyrim_ui/src/app/services/client.service.ts(全部站点只有这一处)
this._remainingReconnectionAttempt--;                                    // ← 值就在上一行
this.chatService.pushSystemMessage('SERVICE.CLIENT.CONNECTION_LOST');    // ← 一个参数都没交
```

⇒ **每一次自动重连,玩家读到「… trying to reconnect. `{{remainingReconnectionAttempt}}`
attempts left.」** —— 而这条消息的**唯一作用**就是告诉他剩余次数。

**它为什么能活这么久**:

- 代码 review 看的是「键被引用了没有」✓;
- `pushSystemMessage(key, params?)` 的第二个参数**本来就是可选的** ✓;
- 英文玩家看到的**其余部分完全正常** —— 只有中间一段是 `{{...}}` ✓。

三条加起来,只有**「把目录里的占位符与调用点的参数对起来」**这一条判据能抓到它。

### 140.3 门禁的三条判据与它的作用域

| # | 判据 | 覆盖 |
| --- | --- | --- |
| R1 | 一个键的占位符集合在 14 份语言里相同 | 译文漏/改占位符 ⇒ 那种语言显示残缺 |
| R2 | 字面量键的调用点必须交出该键需要的参数 | 传错名或不传 ⇒ 玩家读到 `{{...}}` |
| R3 | 运行时拼的键所需的参数必须由该 code 的**接口**提供 | 面板的键由 code 决定、参数由接口决定 —— **此前没有任何东西把两边对起来** |

R2 覆盖三种写法:`translate('KEY', {a})`、`'KEY' | transloco: {a}`、
`{ message: 'KEY', params: {a} }`/`messageParams`。
**`[text]="'KEY'"` 故意排除在外**:`dropdown.component.html` 自己注入了
`{ length: option.value }`,裸键在**那里**才正确 —— **作用域写不清楚的门禁会把正确写法当缺陷报**。

### 140.4 门禁第一版报了 4 条,3 条是它自己的错

| 报的 | 真相 |
| --- | --- |
| `AVAILABLE_COMMANDS is rendered without ['cmds']` | 代码对:`{ cmds }` 是**简写属性**,`param_names` 只认「名字: 值」 |
| `wrong_version interface carries ['version']` | 接口对:TS 成员用 **`;`** 分隔,只按 `,` 切会漏 |
| 只见 5 个调用点(下限 10) | `selectTranslate(` 没被匹配(`\btranslate` 在它里面**没有词边界**、大小写也不同),**管道**与 **`params:` 在键之后**两种写法完全没覆盖 |
| `client.service.ts:401 … without ['remainingReconnectionAttempt']` | **真的** ✓ |

修的是**解析器**,不是代码。§139 记的是「第一版就通过的门禁值得怀疑」;
**对称的那条是:第一版就失败的门禁同样值得怀疑 —— 先读代码,再决定改哪一边。**
判据再漂亮,解析器的洞也会以「缺陷」的样子出现。

### 140.5 反向验证(变异测试)

| 变异 | 判据 |
| --- | --- |
| **还原修复**(去掉参数) | R2:`… is rendered without ['remainingReconnectionAttempt']` |
| `de.json` 占位符改名 → `{{attempts}}` | R1:`de.json: … needs ['remainingReconnectionAttempt'] but this translation has ['attempts']` |
| `error.service.ts` 的 `expectedVersion` 改名 | R3:`the 'wrong_version' interface carries ['serverVersion', 'version'], but … asks for ['expectedVersion']` |

### 140.6 同轮另外三处「已验证干净」

① **初始化器管线**:36 个 `TiltedPhoques::Initializer` 全在 `Code/client/`;`RunAll()` 只有一处调用
(`TiltedOnlineApp.cpp`);launcher 里注释掉的那行是因为 launcher **没有初始化器** ——
两条启动路径都由客户端 DLL 自己跑那一遍,`VersionDb::Load` 在前、hook 在后 ✓。
② **模板硬编码**:26 个模板 12 处候选,全部在 `app/mock/`、F2 调试面板与 `index.html` 标题里,
**没有一处是玩家可见 UI** ✓。
③ **跨语言占位符一致性**:14 × 全部键,**0 处不一致** ✓。

**记下来的理由**:「已验证干净」也是结论 —— 否则下一轮会把同样的三个探针再跑一遍。

### 140.7 教训

> **「键被引用了」和「这句话玩家能读」是两件事。**
> 键存在、调用点存在、review 也过得去;但**参数没交**时,文案中间的 `{{...}}` 会原样出现在聊天窗里,
> 而英文玩家看到的其余部分完全正常。**判据要落在渲染路径的行为上**(缺参数时打印什么),
> 而不是「有没有引用这个键」。

> **教训(同轮第二条)**:**门禁第一版的失败,先怀疑门禁。**
> 它报的 4 条里 3 条是解析器的洞(简写属性、`;` 分隔、函数名没匹配上)。
> 一个会误报的门禁比没有门禁更危险:它会让人去改**本来正确**的代码。

---

## 141. 2026-10-11 场次:五个 UI 事件监听**没有产出者**,其中一个包住整个 overlay

> **锚点**:`Tools/Scripts/check_overlay_bridge.py`(第 5 条判据)、
> `Code/skyrim_ui/src/app/components/root/root.component.html`、
> `docs/FOLLOW-UP.md` 第 7 节。

### 141.1 判据:§134 那条桥的**反向**

§134 查的是「UI **调用**的名字有没有实现」(call ∈ registered ∈ dispatched)。
本轮查的是「游戏**推**的事件有没有人发」:
`skyrimtogether.on('X')` 必须对应 C++ 的 `ExecuteAsync("X")` / `EmitEvent("X")`。

**两者是同一个形状**(同一个名字写在两处、没有东西同时看它们),**但坏法不同**:

| 缺哪一半 | 症状 |
| --- | --- |
| 调用缺实现(§134) | **抛异常** —— 至少看得见 |
| 事件缺产出者(本节) | **静默** —— handler 永不运行,它驱动的状态永远停在初值 |

### 141.2 实测:29 个监听 × 24 个事件名 → 5 个死监听,0 个无监听的事件

| 监听 | 真相(逐条查过) |
| --- | --- |
| `setName` | 源码里写着「不要」:`//not wanted, we dont sync name changes` |
| `debug` | 源码里写着「不再需要」:`//not needed anymore`;`debugData` 取代了它 |
| `dummyData` | 只喂 F2 调试面板刷新(`onDummyData` 里打印数据的循环**已被注释掉**) |
| `protocolMismatch` | 已被取代:协议不匹配现在走 `triggerError` + `wrong_version`(`TransportService.cpp` 组那个 payload),由错误面板显示 |
| **`openingMenu`** | **整条链都在、产出者从来不存在** —— 本轮按产品决定删除(§151) |

`openingMenu` 的消费端是**两个组件**(本节原先写「整个 overlay」,是错的;订正见 §151):

```html
<!-- Code/skyrim_ui/src/app/components/root/root.component.html, 已删除 -->
<ng-container *ngIf="!(menuOpen$ | async)">
  <app-debug></app-debug>
  <app-group></app-group>
</ng-container>
```

守卫里**只有 `app-debug`(F2 调试面板)与 `app-group`(队伍成员列表)**。
聊天窗(`app-window.app-root-chat`)、通知(`app-notifications`、`app-notification-popup-container`)、
主菜单(`app-root-menu`/`app-root-status`)与弹窗层(`app-root-popups`)都在守卫**之外**。
本节与 `docs/FOLLOW-UP.md` 第 7 节原先都写「包住整个 overlay」,那是**没看代码就下的结论**;
订正与后果见 §151。

而它**永远为假**:

| 检查 | 结果 |
| --- | --- |
| 全仓(任何扩展名)搜 `openingMenu` | 只有 UI 侧三处(`root.component.ts`、`client.service.ts`、`typings.d.ts`) |
| `Code/client/**` + `Code/tp_process/**` 的 `ExecuteAsync`/`EmitEvent` | 24 个事件名,没有它 |
| mock | 也不发它 —— 连浏览器里 mock 都看不出这个行为是空的 |

**真正在隐藏 overlay 的**只有 `OverlayClient::SetUIVisible(false)`,两个调用点
(`revealPlayers` 与 §134 补的 `deactivate`)**都不是菜单钩子**;
`UI::GetMenuOpen`(id 82074)存在但只有 `DebugService` 用它;
`MapMenu`/`SkillsMenu` 声明 `MenuOpenCloseEvent` 是为了它们各自的补丁,不是为了 overlay。

### 141.3 当时为什么不动行为,以及后来怎么收尾的

本轮(2026-10-12)**已经决定并执行**:见 §151。以下是当时的取舍记录,保留原文,
但其中「整个 overlay」的说法是错的(守卫只包住 `app-debug` 与 `app-group`)。

两个方向都改产品行为,而且方向相反:

- **实现产出者** ⇒ 开任何菜单**整个 overlay(含聊天窗、队伍列表)消失** ——
  而一边翻背包一边看队伍聊天是常见用法;哪些菜单算「menu」、聊天窗是否豁免,是产品决定;
- **删掉死通道** ⇒ 运行时**零变化**(`!(false)` 恒真),但丢掉 UI 侧已写好的意图。

所以:① 事实写进 `docs/FOLLOW-UP.md` **第 7 节**(含「决定之后要同步改哪里」的清单);
② 在 `root.component.html` 留下注释 —— 下一个读到它的人不必重新推导;
③ 门禁的 `NO_EMITTER` 里那一行理由写着 **OPEN QUESTION**,把这个状态**锁住**:
将来有人实现了产出者,门禁会因为「豁免不再对应死通道」而失败,逼着两边一起改。

**「不动行为」不是「不记录」** —— 一个没有人知道是空的行为,与一个被记录的空行为,
是两种工程状态。

**后来选了另一条**:§151 记录了这个决定 —— 既然守卫只包住两个组件、代价远小于当时的估计,
就把死通道删掉,而不是留着等一个可能永远不来的产品决定。上面这条「锁住」的做法本身仍然成立,
它正是删改时逼着两边一起改的机械保证。

### 141.4 门禁的第 5 条判据

| 判据 | 为什么 |
| --- | --- |
| UI 订阅的每个事件都必须有 C++ 产出者 | 否则 handler 永不运行、状态永不变 |
| 没有产出者的名字必须在 `NO_EMITTER` 里逐条写明理由 | 「不是缺陷」也要有证据,而且下一个人要能看到 |
| 豁免的名字若不再被任何 UI 订阅 ⇒ 失败 | 豁免会腐烂(§138 同一条) |
| 读不到监听或读不到产出者(任一为 0)⇒ 失败 | 形状变了要报错,不能退化成空转 |

**这一条真正防的是改名**:C++ 把 `ExecuteAsync("setVersion")` 改成别的名字,
UI 的 handler 从此**再也不运行** —— 没有错误、没有日志,而 mock 里一切照旧。

### 141.5 反向验证(变异测试)

| 变异 | 门禁诊断 |
| --- | --- |
| **把 C++ 的 `ExecuteAsync("setVersion")` 改名** | `client.service.ts:137 subscribes to "setVersion", which no C++ code emits; the handler never runs and the state it drives can never change` |
| UI 里加一个没有产出者的监听(`phantomEvent`) | 被指名 |
| `NO_EMITTER` 加一条背后没有监听的豁免(`phantomExempt`) | `NO_EMITTER exempts "phantomExempt", but no UI code subscribes to it; drop the exemption` |

### 141.6 本轮的一次自伤(记下来)

写本节时,我用两次「锚点追加」编辑去改 `CHANGELOG.md`,结果**把 `## v1.1.8(2026-09-28)`
这个版本标题整行删掉了**(第一次编辑吃掉了标题后的换行,第二次又把标题当成锚点替换),
于是 v1.1.8 的正文被并进了 v1.1.9。发现方式是**编辑之后回头看结构**
(打印所有 `## v1.x.y` 标题),从 diff 里确认 `-## v1.1.8(2026-09-28)` 是删除行,再按正文首行定位补回。

**教训**:**「追加」这种编辑要在事后核对被追加处两侧的结构** ——
一次锚点替换能吃掉一个新行,而新行是 Markdown 里唯一的结构。
**和变异测试同一条纪律:改完要看它真的变成了什么。**

### 141.7 教训

> **桥的两个方向要分别检查,而「反向」那半通常没人做。**
> §134 查「UI 调用的名字有没有实现」,本轮查「游戏推的事件有没有人发」。
> 前者缺了会抛异常,后者缺了**静默**:`openingMenu` 的守卫、类型、subject、消费端
> **四样俱全**,唯独没有产出者 —— 而当时以为它包住的是整个 overlay(实际只包住
> `app-debug` 与 `app-group` 两个组件,见 §151 的订正)。
> **对称的检查要成对写**,否则总有一半在无人看管的地方。
>
> **追加一条(§151 补的)**:「包住整个 overlay」这个说法当时**没人去核对** ——
> 它把「实现产出者」的代价说大了一个量级,于是一个只需删十几行的决定被记成了产品决定。
> **影响取舍的事实,要先看代码再写。**

---

## 142. 2026-10-11 场次:索引表**查了范围,没查有没有填** —— 一个字节终止进程

> **锚点**:`Tools/Scripts/check_message_factories.py`(新)、
> `Code/encoding/Messages/{Client,Server}MessageFactory.{h,cpp}`、
> `Code/admin_protocol/AdminMessages/*MessageFactory.cpp`、`Code/tests/encoding.cpp`。

### 142.1 判据:枚举里的每个 opcode 都必须有一个抽取器槽位

四个消息工厂都是「**索引表 + 手写清单**」:

```cpp
static std::function<UniquePtr<ServerMessage>(TiltedPhoques::Buffer::Reader&)> s_serverMessageExtractor[kServerOpcodeMax];

auto s_visitor = CreateMessageVisitor<AuthenticationResponse, …, NotifyPluginMessaging>;  // 手写
ServerMessageFactory::Visit(extractor);        // extractor 写 [T::Opcode]
```

而 `Extract()` 只查了**范围**:

```cpp
if (data >= kServerOpcodeMax) [[unlikely]] return {nullptr};   // 越界 → 安全返回
const auto opcode = static_cast<ServerOpcode>(data);
return s_serverMessageExtractor[opcode](aReader);              // ← 槽位可能是空的
```

**范围检查不是完整性检查。** 清单没点到的 opcode ⇒ 空 `std::function` ⇒
调用抛 `std::bad_function_call` ⇒ 而 `Extract()` 是 **`noexcept`** ⇒ `std::terminate`。
**线上一个字节的 opcode 就能终止进程。**

### 142.2 实测:今天每个槽位都是满的 —— 但挡住它的只有「还没人删掉一个名字」

| 工厂 | 枚举值 | 清单条目 | 槽位 |
| --- | --- | --- | --- |
| `ClientOpcode` | 53 | 53 | 全满 |
| `ServerOpcode` | 56 | **59**(3 个重复) | 全满 |
| `ClientAdminOpcode` | 1 | 1 | 全满 |
| `ServerAdminOpcode` | 2 | 2 | 全满 |

**那三个重复条目**(`NotifyDialogue`/`NotifySubtitle`/`NotifyPlayerDialogue` 各写两遍)就是
**手写清单被人反复编辑的指纹** —— 而「下一次编辑删掉一个名字而不是重复一个」是同一支笔的动作。
所以这是**活的危险**,不是假想:唯一的防线是「还没人删过」。

### 142.3 修法

1. **抽取器兜底**(四个工厂各一处):调用前查槽位,空则返回 `nullptr` ——
   与越界 opcode **同一种行为**(丢一个消息,而不是死一个进程);
2. **门禁** `check_message_factories.py`:把「漏名字」变成构建期失败;
3. 修掉那三个重复条目(**59 → 56**,与 56 个 opcode 一一对应)。

### 142.4 门禁的四条判据(112 个 opcode、四个工厂)

| # | 判据 | 为什么 |
| --- | --- | --- |
| R1 | 枚举里每个 opcode 都要有消息类型填进抽取表 | 空槽位 = `terminate` |
| R2 | 清单里每个类型都要在枚举里有 opcode | 名字陈旧 = 同一个洞的另一面 |
| R3 | **同一类型不能列两次** | 重复是「清单开始漂移」的指纹,而**下一次可能是删而不是重** |
| R4 | 每个声明了 opcode 的消息头都必须被工厂点到 | 能构造、能发送、**永远解不开** |

**怎么把两边的名字对上 —— 不猜名字。** `RemoveSpellRequest` 声明 `kRequestRemoveSpell`、
`AdminShutdownRequest` 声明 `kAdminShutdown`(**不同构**),而客户端与服务端消息**共用一个目录**。
所以门禁读每个消息头里那一行
`static constexpr <哪个枚举>Opcode Opcode = <哪个名字>;`,
由**声明的枚举**决定它属于哪个工厂。

**第一版正是猜了名字**(`kFooBar` ↔ `FooBar`),报了 **116 条假失败**(其中 112 条是把服务端消息
算进了客户端)。**判据第一版报错,先怀疑判据** —— §140 同一条。

### 142.5 反向验证(变异测试)

| 变异 | 门禁诊断 |
| --- | --- |
| **从服务端清单删掉 `NotifyRemoveSpell`**(制造空槽位 = 那个 terminate) | `NotifyRemoveSpell.h declares kNotifyRemoveSpell, but the visitor list does not name NotifyRemoveSpell; its extractor slot stays an empty std::function and Extract() would terminate the process on that opcode` + R4 同时点名「能发不能解」 |
| **客户端清单里把 `PluginMessagingRequest` 写两遍** | `PluginMessagingRequest is listed 2 times in the visitor; a repeat means the list is being edited by hand and the next edit can drop a name instead of repeating one` |

**回归测试**(`Code/tests/encoding.cpp`)钉住**两种**越界(恰好 `kClientOpcodeMax`、远超它的值)
与服务端同类情形,并**同时断言一个真实消息仍然解得开** ——
否则「一律拒绝」也能让测试通过。

### 142.6 教训

> **「查了范围」和「查了完整性」是两件事,而前者看起来像后者。**
> `if (data >= kServerOpcodeMax) return nullptr;` 读起来像一道完整的防线,
> 但它只挡住**枚举之外**的值;**枚举之内**却没人填的槽位同样是空 `std::function`,
> 同样从 `noexcept` 里抛出去。
> **索引表的两种坏法:越界,和没填。** 后者更隐蔽 ——
> 因为它要求**手写清单**与**枚举**保持一致,而这两样东西从来不放在一起。

---

## 143. 2026-10-11 场次:消息「四样俱全」,唯独**没人发送**

> **锚点**:`Tools/Scripts/check_message_use.py`(新)、
> `Code/server/Services/InventoryService.cpp`、`Code/client/Services/Generic/InventoryService.cpp`。

### 143.1 判据:线上的每条消息,要么有人发,要么有人收

§142 保证「每个 opcode 都有抽取器」。本轮查**另一半**:**谁构造它**。

一条消息可以有**四种完整**,而检查通常只做前三种:有 opcode ✓、有抽取器 ✓、有序列化 ✓、
**对面有处理器** ✓ —— 而它**从未被发送过**。

### 143.2 实测:109 条消息里一条真的死了

```
客户端  InventoryService::RunWeaponStateUpdates()
        → 本地 actor 的拔刀状态变了 → 发 DrawWeaponRequest
服务端  InventoryService::OnWeaponDrawnRequest()
        → characterComponent.SetWeaponDrawn(message.IsWeaponDrawn)   ← 只更新自己的副本
        → 结束
```

`NotifyDrawWeapon`(字段恰好是 `Id` + `IsWeaponDrawn`)有 opcode、在访问清单里、
被客户端 `InventoryService.cpp` `#include` 着 —— **全仓没有任何一处构造它**。

**后果**:拔刀状态**只在角色生成时同步一次**
(`CharacterSpawnRequest.IsWeaponDrawn` / `AssignCharacterResponse.IsWeaponDrawn`),
之后的变化**永不广播** ⇒ 玩家 A 当着 B 的面拔刀,**只要 B 早就加载了 A 就永远看不到**,
直到 B 重进格子。**没有报错,功能只是不存在。**

### 143.3 修法

服务端在更新组件后按**同文件里装备广播的既有写法**广播(`SendToPlayersInRange(notify, cOrigin,
acMessage.GetSender())`);客户端补 `sink<NotifyDrawWeapon>` → `OnNotifyDrawWeapon`,
用与 `OnNotifyEquipmentChanges` 相同的 `view<RemoteComponent, FormIdComponent>` 把服务端 id
解析回本机 actor,状态确实不同才调 **`SetWeaponDrawnEx`** —— 生成路径用的同一个函数
(它置 `g_forceAnimation` 强制重放动画,否则本地看不到这次拔刀)。

### 143.4 门禁的判据

| 判据 | 为什么 |
| --- | --- |
| `ClientMessage` 要么被客户端发、要么被服务端收 | 二者皆无 = 死在线上的一条消息 |
| `ServerMessage` 要么被服务端发、要么被客户端收 | 同上 |
| **「有人收、没人发」必须失败** | 这正是本缺陷的形状;第一版把它放过了,见 143.5 |
| 豁免条目必须仍对应真实消息 | 豁免会腐烂(§138/§140) |

### 143.5 门禁第一版有一条**会放过本缺陷本身**的洞

最初只有「**既**没人发**也**没人收」才算失败 ⇒ **「有人收、没人发」直接通过**。
第一次变异我把服务端的 `notify` 改名成 `notifyMUTATION`,门禁报 OK,我一度以为门禁坏了;
实际是**变异是假的** —— 改名不等于不发送,消息照样被构造、照样被发送。
做了**真变异**(把整段广播注释掉)才发现:那条规则下,
`NotifyDrawWeapon` 这种「客户端收、服务端从不发」的形状**会一路通过** ——
而那正是要修的缺陷。补上规则后,真变异立刻被抓住。

> **教训**:**变异要改「行为」,不是改「名字」。**
> 而**当门禁漏掉的恰好是它要抓的那一类时,只有真变异能暴露它** ——
> 假变异会给你一个「门禁没问题」的错觉。

### 143.6 另外两条死通道:记录理由,不动代码

| 消息 | 真相 |
| --- | --- |
| `NotifyPlayerDialogue` | **功能是活的,只是走的不是它**:玩家对话经 `sendPlayerMessage(kPlayerDialogue, …)` 走 **`NotifyChatMessageBroadcast`**(`MessageType = kPlayerDialogue`)广播,所以客户端 `OverlayService::OnPlayerDialogue` 永不运行 |
| `RequestObjectInventoryChanges` / `NotifyObjectInventoryChanges` | **保留但从未接线**:容器背包走 `ObjectService`;两者除 include、前向声明、工厂槽位与 opcode 外无任何引用 |

**都不删 opcode**:`Opcodes.h` 自己写着「**只追加,从不插入** —— 配套插件按索引挂钩这张表,
每个既有 opcode 必须保持编号」;删**表中段**的枚举值会让其后**全部重编号**,正违反这条规则。
所以门禁里逐条写明理由,把状态**锁住**。

### 143.7 教训

> **「消息完整」有四种完整,而检查通常只做前三种。**
> opcode ✓、抽取器 ✓、序列化 ✓、对面有处理器 ✓ —— 依然可以是一个**从未被发送过的消息**:
> 功能不是坏了,是**根本没接上**。
> 判据必须问到**「谁构造它」**:一条没人构造的消息,与一条没人监听的消息,
> 是同一种缺陷的两面。

---

## 144. 2026-10-11 场次:进程内事件总线(**干净**),与一个只发不读的字段

> **锚点**:`Tools/Scripts/check_wire_field_reads.py`(扩展第三主体)、
> `Code/encoding/Structs/ServerSettings.h`。

**结论先行**:本轮**没有缺陷**。两个探针都是干净的;唯一留下的是一个**只发不读**的字段,
记录并锁住。真正值得写下来的是**第一版判据错在哪里** —— 三处错,全都看起来像「发现了缺陷」。

### 144.1 探针一:「订阅了但没人 trigger」——**不可由文本判定**(负结果)

第一版按 `.trigger(` 扫,报出 **30 个**「订阅后从未被触发」,包括 `InventoryChangeEvent`、
`CellChangeEvent`、`ActionEvent` 这些**显然在跑**的东西。三层原因:

| 漏掉的 | 实际写法 |
| --- | --- |
| 参数不是裸标识符 | `trigger(acMessage.Settings)`、`trigger(ConnectedEvent(id))`、`trigger(Type{…})` —— 实测 4 种形状(类型字面量 3、限定名 4、成员访问 3、其余 61) |
| **第二个派发入口** | `World::Get().GetRunner().Trigger(std::move(aEvent))`(`RunnerService.h:25`)—— 它把 `m_dispatcher.trigger(...)` **包进 lambda 入队**,事件类型只在**调用点由实参推导**,文本里根本不出现 |
| 队列 | 同一服务还有 `Queue(std::function<void()>)` |

⇒ **「这个进程内事件有没有被派发」需要调用点的实参类型**,即一次类型感知的扫描 ——
与 §135.5、§140.4 撞的是**同一堵墙**。**判据做不到就不发布**(§135 的既定做法),记下来免得重撞。

### 144.2 探针二:`ServerSettings` 通路 —— **干净**

服务器一次性下发 7 个玩法策略字段(`GameServer.cpp:141-147`);客户端两处收到后
`m_dispatcher.trigger(acMessage.Settings)`(`TransportService.cpp:791`、`:903`)
⇒ `PlayerService::OnServerSettingsReceived` **确实会跑**。

| 字段 | 客户端读者 |
| --- | --- |
| `Difficulty` | `PlayerService.cpp:111,113` |
| `GreetingsEnabled` | `PlayerService.cpp:115` |
| `DeathSystemEnabled` | `PlayerService.cpp:121` |
| `PvpEnabled` | `Actor.cpp:1200`、`MagicTarget.cpp:137` |
| `SyncPlayerHomes` | `ObjectService.cpp:158` |
| `SyncPlayerCalendar` | `CalendarService.cpp:36` |
| `AutoPartyJoin` | **无** |

### 144.3 记录项:`AutoPartyJoin` 只发不读

**不是坏掉的功能**:自动入队完全由服务端决定(`AllowsAutoPartyJoin()` → `PartyService.cpp:136/277`
把玩家加进队伍并逐个发 `SendPartyJoinedEvent`),客户端通过**普通队伍消息**得知结果,
**不需要这个标志**。

**不删**:`ServerSettings` 按固定字段表序列化,删中间一个字段会让其后**全部位移**。
**也不硬造读者**:那是凭空发明一条产品行为。正确做法是**记录 + 锁住**。

### 144.4 判据自己的三处错(本轮真正的收获)

| 错在哪 | 后果 | 修法 |
| --- | --- | --- |
| 结构体解析器只认 `struct X final :` | `struct ServerSettings {` 解析不出字段 | 正则改为 `(?:final\s*)?(?::\|{)` |
| `fields_of()` 从匹配点往后找**第一个** `{` | 拿到的是**内联方法体**(`operator==`)⇒「声明了方法的结构体」字段全看起来不存在 | 从**结构体自己的**左花括号开始 |
| 「在到达处被读」套在**一个函数体**上 | 共享结构体是**按需读取**的 ⇒ `PvpEnabled`/`SyncPlayerHomes`/`SyncPlayerCalendar` **三个假失败** | 主体支持**目录作用域**(handler 名为空 = 整个 `Code/client`) |

### 144.5 反向验证(变异测试)

| 变异 | 结果 |
| --- | --- |
| 把 `PvpEnabled` 的**两个**读者都改成 `if (false)` | **失败**:`Code/client: nothing in that directory reads ServerSettings.PvpEnabled; … the receiver drops it` ✓ |
| 只改**一个**读者 | **通过** —— 两个读者去掉一个仍然有人读,**这正是该有的行为**(顺带证明判据不是「匹配到一次就永远绿」) |

### 144.6 教训

> **判据的「作用域」与「解析器」是最容易出错的两处,而它们出错时看起来都像发现了缺陷。**
> 本轮第一版报了 30 个 + 3 个,全部是判据自己的问题(派发入口漏了一个、作用域套错一层、
> 结构体从错误的 `{` 开始解析)。
> **先证明判据能看见已知为真的东西,再去相信它报出来的东西。**
>
> 推论:**负结果也要写下来**。「这条通路是干净的」与「这条判据做不到」同样是结论 ——
> 前者省掉下一次重复劳动,后者省掉下一次重撞同一堵墙。

---

## 145. 2026-10-11 场次:手写的 SKSE ABI 镜像 —— 断言了**语义**,没断言**布局**

> **锚点**:`Code/skse_bootstrap/main.cpp`、`Tools/Scripts/check_skse_abi.py`(新)。

### 145.1 判据:这个结构体的布局必须与 SKSE64 的逐字段相同

`SksePluginVersionData` 是**手写**的(那个 dll 不链接 SKSE 的头文件),
而 **SKSE 按偏移直接从本 dll 的导出里读它 —— 它永远看不到字段名**。
本轮逐字段核对:**9 个字段、类型、数组长度、`sizeof = 848`,与 SKSE64 `PluginAPI.h` 完全一致** ✓。

**但守卫不完整**:文件里已有 3 个 `static_assert`(标志位 `1 << 1` / `1 << 0`、`kVersion == 1`),
**没有一个断言布局** —— 而布局正是会被人手改坏的东西:

| 改动 | 后果 |
| --- | --- |
| 加字段 / 改数组长度 | 其后**每个字段**偏移全移 ⇒ SKSE 把 name 读成 author、把标志位从版本列表里读出来 |
| 交换两个相邻字段 | 同上,且**类型相同**,肉眼与编译都看不出来 |
| 改一个标志位的值 | SKSE 拒绝加载,报的却是「游戏版本」而不是「插件」 |

**而且静默**:导出仍在、名字仍叫 `SKSEPlugin_Version`,插件照常被扫描 ——
这正是文件顶部注释描述的那个现场(「no version data」)。

### 145.2 修法(两层)

1. **`static_assert` 钉住布局**(新增 4 条):`sizeof == 848`、`offsetof(name) == 8`、
   `offsetof(versionIndependenceEx) == 772`、`offsetof(seVersionRequired) == 844` ——
   与 §131 给 `ExtraDataList`/`GameVM` 加的那批**同一形状**,**编译期**挡住。
   顺带补 `<cstddef>`:`offsetof` 需要它,而这是个**没有 PCH** 的独立 dll
   (客户端那些用 `offsetof` 的文件靠 PCH 传递,**不能照抄**)。
2. **门禁** `check_skse_abi.py`:把 SKSE64 的参考布局**写成数据**,逐字段比对声明。
   判据是「**这个声明是否仍描述 SKSE 实现的接口**」,不是「它是否与它自己一致」——
   后者永远为真。

### 145.3 门禁自己的两个坑

| 坑 | 后果 |
| --- | --- |
| 比常量的**文本**而不是**值** | `1 << 1` 与 `2` 同值,门禁把正确写法报成错(**2 条假失败**) |
| `const char*` 的写法归一化 | `constchar*` vs `const char*` 又是一条假失败 |

⇒ **常量比较要求值,类型比较要归一化空格**。

### 145.4 反向验证(变异测试)

| 变异 | 门禁诊断 |
| --- | --- |
| `char name[256]` → `[260]` | `field 2 is char name[260], but SKSE reads char name[256] there` |
| **交换两个相邻字段**(类型相同) | 两条诊断同时点名;并且**新增的 `offsetof` 断言在编译期也会挡住** |
| `kVersionIndependent_Signatures = 1 << 1` → `1 << 2` | `is 1 << 2, but SKSE checks 2 (1 << 1); a wrong bit is refused with a message about the game version instead of about the plugin` |

### 145.5 教训

> **「手写的外部 ABI 镜像」必须同时钉住**语义**和**布局**,而人们通常只钉前者。**
> 这个结构体已经断言了「标志位该是什么值」,却没断言「字段在哪」——
> 而布局恰恰是**同一类改动**(加字段、改长度、调顺序)最先破坏的东西,
> 且它的失败方式是**静默的**:导出还在、名字还对,SKSE 只是从错误的偏移读到了别的东西。
> **凡是「别人按偏移读我们的内存」的地方,`sizeof` 与 `offsetof` 断言就是那条判据。**

---

## 146. 2026-10-11 场次:插件 C ABI 的**结构体布局**没有被钉住

> **锚点**:`Code/plugins/tools/strpm_contract.py`(新增 C5)、
> `Code/plugins/STRPM/contract.json`(`interfaceLayouts`)。

### 146.1 判据:契约钉住的,要和危险发生的地方对齐

`STRPluginMessagingAPI.h` 是**独立插件仓库各自 vendor 一份**的 C ABI 头文件。
契约工具此前钉住的是:**头文件 sha256**(C1)、**常量值**(C4)、**类型名字**(24 个)。

| 钉住了 | 没钉住 |
| --- | --- |
| 文件的哈希 | —— |
| 常量值 | —— |
| 类型**名字**列表 | **结构体的字段** |

**`interfaceTypes` 只是一串名字,一个字段都没描述。** 于是「改字段顺序 / 加字段 / 改数组长度,
然后重新记录 hash」**全绿通过** —— 而 C1 的 hash **本来就会被有意重新记录**
(编辑 → 重记 → 继续,那是它设计的工作流),所以这不是漏洞利用,是**正常流程**。

### 146.2 为什么这是跨 DLL 的真危险

消费者按**自己 vendor 的那份**头文件编译:

```cpp
STRPM::Target target{ ... };          // 按值传进我们的 send()
const STRPM::Message* message;        // 我们交回去,它逐字段读
STRPM::RuntimeStatus status;          // 同上
```

布局不一致 ⇒ 消费者**把我们的 `sender.displayName` 从错误偏移读出来当指针解引用** ——
崩溃在**它的**插件里,**构建期没有任何错误**(它编的是自己那份拷贝)。

### 146.3 修法:契约新增 C5「接口布局」

1. `struct_layouts()`:抽每个结构体的**字段表**(类型 + 名字 + 数组长度,**按声明顺序**);
2. `dump` 写入 `contract.json` 的 `interfaceLayouts`(**10 个结构体、48 个字段**);
3. `check` 的 C5 **两层**:
   - **主体**:当前头文件布局必须与 pinned 的**逐字段相同** ⇒「改完重新 dump」不再能把布局改动顺手带走;
   - **每个消费者**:其 vendored 拷贝里**双方都声明**的结构体,布局必须与权威头文件相同 ⇒
     这才是**真正的跨 DLL 危险**。

### 146.4 门禁自己的三个坑

| 坑 | 症状 | 修法 |
| --- | --- | --- |
| **正则灾难性回溯** | 嵌套量词(`identifier\|const\|星号` 互相重叠)在不匹配行上指数回溯,**读一个头文件 5 分钟没结束** | 改**线性扫描**(按顶层 `;` 切分再逐条解析)⇒ **0.001 秒** |
| **字段被静默漏掉** | 只认 `类型 名字;` ⇒ `const char* channel` 与**全部函数指针**都没进布局(`Interface` 1 个字段,应 7 个) | 两条分支;星号在类型/名字两侧的写法都归一化 |
| **过滤器把该查的排除了** | 消费者比较里加了 `struct in used`,理由「其余是噪音」—— 而 `Sender` **从不被消费者点名**(它跟着我们交回的 `Message*` 来)⇒ **变异静默通过** | **删掉过滤**:vendored 拷贝声明了的结构体就是 ABI |

### 146.5 反向验证(变异测试)

| 变异 | 结果 |
| --- | --- |
| **重排某消费者 vendored 拷贝里的 `Sender` 字段** | `C5 [OStimTogether] struct Sender disagrees with the authoritative layout` ✓ |
| 同上,但只保留 C5 的「主体」层 | **不会**被发现 —— **两层都必要** |
| 同上,但保留 `struct in used` 过滤 | **不会**被发现 —— 过滤正是漏掉它的地方 |

### 146.6 教训

> **契约钉住的东西,要和危险发生的地方对齐 —— 而人们会钉住容易钉的(哈希、常量、名字),
> 漏掉难钉的(布局)。**
> 哈希只说明「文件变了」,不说明「变了什么」;而且它**本来就会被重新记录**,
> 所以挡不住「改布局 + 重记哈希」这条**正常流程**。
> **类型名字列表**更糟:读起来像在描述接口,实际一个字段都没描述。
> **判据要落在**语义**上(字段是什么、在哪),不是**出处**上(文件是否被改过)。**

> **教训(同轮第二条)**:**过滤「噪音」的守卫,常常正是漏掉真缺陷的那一处。**
> `struct in used` 看起来是精确化,实际把「消费者不点名但会读到的结构体」全部豁免 ——
> 而跨 DLL 边界读的结构体**恰恰大多不被点名**(它们是指针后面跟着来的)。
> **删掉过滤器之后变异才被抓到,这本身就是它一直在放行的证据。**

---

## 147. 2026-10-11 场次:游戏结构体的布局断言(**自洽**),与三次失败的工具

> **锚点**:`Tools/Scripts/check_layout_asserts.py`(新)、`Code/client/Games/**`。

**结论先行**:本轮**没有缺陷**。想查的性质(「断言的字段是否覆盖了代码用到的字段」)
**在文本层面不可判定**;能判定的那部分(**分支内自洽**)是**干净的**,并已做成门禁。

### 147.1 想要的性质做不到,而三次失败都长得像「发现」

第一版抽「每个结构体声明的成员」,输出 `61 个有断言的结构体,0 个解析出成员`。逐层查下去:

| 工具的错 | 伪装成 | 真相 |
| --- | --- | --- |
| `STRUCT` 正则**漏了 `re.M`** | 61 行「有断言但没成员」 | `^\s*struct` 只在文件开头匹配 ⇒ **一个结构体都没找到** |
| 补上后**嵌套类型被当成成员** | `Actor: 13 asserted of 114 members` | 这些头文件有 **153 个嵌套 `struct/class/enum` 声明**、**22 个预处理分支**;「未钉住成员」里是 `SpellItemEntry`/`ActorValueModifiers`/`ContactNormal` 等**嵌套类型名**与 `SKYRIM_TARGET_LEGACY`/`TP_PLATFORM_32` 等**宏** |
| 断言一致性检查**无视分支** | `Actor` 一个文件 **14 条「冲突」** | `#ifdef SKYRIM_TARGET_LEGACY / #else` 的**两套偏移**被当成「同一字段两个值」—— 那是**设计本身** |

```cpp
// Code/client/Games/Skyrim/ExtraData/ExtraDataList.h —— 两套是设计,不是冲突
#ifdef SKYRIM_TARGET_LEGACY
static_assert(offsetof(ExtraDataList, data) == 0x0);
static_assert(offsetof(ExtraDataList, bitfield) == 0x8);
#else
static_assert(offsetof(ExtraDataList, data) == 0x8);
static_assert(offsetof(ExtraDataList, bitfield) == 0x10);
#endif
```

⇒ 「这个结构体的字段是否都被钉住」需要**真正的 C++ 解析**(宏展开 + 嵌套作用域),
与 §135.5、§140.4、§144.1 是**同一类墙**。

### 147.2 能判定的那部分:分支内自洽

**同一个预处理分支内**:一个偏移只能对应一个字段,一个字段只能有一个偏移。
**跨分支允许不同** —— 那正是 legacy/AE 两套布局的意义。

**实测 192 条断言、47 个文件、55 个分支 ⇒ `0` 处分支内不一致** ✓(**已验证干净**)。

### 147.3 门禁与它的边界声明

`Tools/Scripts/check_layout_asserts.py` 跟踪 `#if/#else/#endif` 深度、按分支分组后检查上面两条,
并有下限(少于 100 条断言即失败)。**文档字符串里明确写出它不做什么**(不检查字段覆盖率)
以及**为什么**(153 个嵌套声明 + 22 个分支 ⇒ 文本抽取必然把类型名和宏当字段)。

> **把「做不到」写进工具,比让它安静地报假结果好。**

### 147.4 反向验证(变异测试)

| 变异 | 门禁诊断 |
| --- | --- |
| **同一字段在同一分支断言两个偏移**(复制粘贴忘改字段名) | `ExtraDataList.bitfield is asserted at both 0x8 and 0x10 in the same branch (#ifdef:SKYRIM_TARGET_LEGACY); one of the two is stale` |
| **两个不同字段断言在同一偏移** | `ExtraDataList: bitfield and lock are both asserted at 0x8 in the same branch …; the struct cannot have both there` |

### 147.5 教训

> **工具的每一个 bug 都会伪装成「发现」,而这一轮的工具错了三次。**
> `re.M` 漏了 ⇒ 61 行「无成员」;嵌套类型当成员 ⇒ `Actor` 报 114 个成员;
> 无视分支 ⇒ 14 条「冲突」。**三次都长得像结论,三次都是工具自己的问题。**
> 判据:**在相信它报出来的东西之前,先让它看见一件你已知为真的事** ——
> 本轮若先问「它能不能正确解析我手写核对过的那两个字段」,三次错误一次都不会发生。

> **教训(同轮第二条)**:**性质若在文本层面不可判定,就把它缩小到可判定的那部分,并写清边界。**
> 「字段是否都被钉住」不可判定;**「分支内偏移是否唯一」可判定** ——
> 后者虽窄,却真的抓住了「复制粘贴断言忘改数字」这类错误(两个变异都被抓住)。

---

## 148. 2026-10-11 场次:Papyrus 变量类型标签**定义了两遍**,而两者**零个共同名字**

> **锚点**:`Tools/Scripts/check_papyrus_types.py`(新)、
> `Code/components/es_loader/Records/Chunks.h`、`Code/client/Games/Skyrim/Misc/BSScript.h`。

### 148.1 判据:两个组件必须给同一个标签空间编同样的号

| 位置 | 枚举 | 用途 |
| --- | --- | --- |
| `components/es_loader/Records/Chunks.h` | `Type` | 解析 `.pex` 脚本块(`Chunks.cpp` 里 `case Type::OBJECT_ARRAY`) |
| `client/Games/Skyrim/Misc/BSScript.h` | `BSScript::Variable::Type` | 向 VM 注册 native(`BSScript.cpp` 传 `kBoolean`),VM 也用它回传返回类型 |

```
loader:  INVALID=0  OBJECT=1  WSTRING=2  INT=3    FLOAT=4  BOOL=5   … ARRAY=11..15
client:  kEmpty=0   kObject=1 kString=2  kInteger=3 kFloat=4 kBoolean=5   kMax=16
```

**同一个标签空间、两套拼写、零个共同名字。** 实测 **11 处对应关系全部一致** ✓ ——
**今天是对的,但没有任何东西在维持它**。

### 148.2 为什么这算真缺口

两边各自改一处,**两个枚举都照样编译**,各自也照样自洽;失败形式是
**静默地误解码一个 Papyrus 变量**,而那条路径正是插件/脚本一侧。
与 §145/§146 同一形状:**同一个事实写在两处,没有东西同时看它们**。

### 148.3 门禁的两条判据

| 判据 | 为什么 |
| --- | --- |
| R1 六个标量标签两侧**同值** | 对应关系**不能按名字找**(零个共同名字)⇒ 配对表**写死在门禁里**并附理由 |
| R2 loader 的每个**数组标签**都低于 client 的 `kMax` | client 的 `kMax` 才是「VM 表能到多远」的那份声明 |
| 下限:标量 < 6 或数组 < 4 即失败 | 形状变了要报错,不能退化成空转 |

### 148.4 反向验证(变异测试)

| 变异 | 门禁诊断 |
| --- | --- |
| **删掉 client 侧一个隐式编号的枚举值**(`kFloat`) | **两条**:`FLOAT/kFloat is missing …` **与** `BOOL/kBoolean is missing …` —— 隐式编号里删一项,它**后面的全部重编号**,第二条诊断才是这个改动真正的后果 |
| **client 的 `kMax` 16 → 12** | 四条:`STRING_ARRAY/INT_ARRAY/FLOAT_ARRAY/BOOL_ARRAY … at or above the client's kMax (12)` |

### 148.5 教训

> **「零个共同名字」本身就是一种危险信号。**
> 两个枚举**没有任何一个标识符相同**,所以**任何按名字做的检查都看不见它们的关系** ——
> 而它们编的是同一个号。**当两份定义共用 0 个名字却必须一致时,那份对应关系只能由人写下来**;
> **写下来的那一刻,它才第一次变成可检查的。**

> **教训(同轮第二条)**:**隐式编号的枚举里,「删一项」不是「少一项」,是「后面全变」。**
> 变异 1 同时报出两个名字,正是因为 `kFloat` 之后所有标签都往前挪了一位。
> **要么给必须稳定的标签显式编号,要么用门禁守住位置。**

## 149. 2026-10-11 场次:提交清单(§0)逐条查账 + **一个「无法检查时报告成功」的门禁**

> **锚点**:`Tools/Scripts/check_gate_integrity.py`(新)、
> `Tools/Scripts/check_fomod_languages.py`(新)、`Tools/Scripts/check_xmake_version.py`(新)、
> `Code/plugins/tools/check_workflow_shell.py`(修复)、
> `.github/workflows/plugins.yml`(补 `pip install pyyaml` 与三条路径触发)。

### 149.1 起因:§0 的清单上有规则,而没人执行

§131 与 §134 各自撞到过同一件事 —— `docs/PITFALLS.md` 的 **§0 提交清单**里写着一条规则,
**而代码里没有任何东西在检查它**。于是这一轮把 §0 的 **13 个 `- [ ]` 条目**整个读一遍,
逐条问「**谁在看它**」,分成三类:**已有门禁的**、**能变成门禁的**、**只能靠人读的**。

能判定的部分里,有两条此前**完全没有守护**,而且各自连着一次真实的返工(见 149.2、149.3);
更要紧的是第三类里藏着一个**活缺陷**(见 149.4)。

### 149.2 两条语言分支必须提供同样的东西(FOMOD 安装向导)

`GameFiles/Skyrim/fomod/ModuleConfig.xml` 把整套安装步骤**写了两遍**:
一份 `lang = zh`、一份 `lang = en`。**插件的名字是翻译过的**,所以两边对不上名字;
能对的是**结构与载荷**。新增门禁 `Tools/Scripts/check_fomod_languages.py`:

| 判据 | 为什么 |
| --- | --- |
| R1 两条分支的安装步骤数相同 | 分支少了整段,只有一边的玩家看不到 |
| R2 逐步,两条分支的插件选项数相同 | 步骤对齐了但选项数不同,同样是漏 |
| R3 逐步,两条分支安装的**文件夹与文件完全相同** | **这条才是要害** |
| 下限 `MIN_STEPS = 2` / `MIN_PLUGINS = 4` | 文件被换掉或解析失败要失败,不能退化成空转 |

R3 之所以最关键,是因为**它没有报错**:向导照着自己文件里的内容执行,一切正常,
**加在中文分支里而英文分支没有的插件,英文玩家根本装不上,而且永远不知道自己少了**。

实测(当时)**4 步 / 插件数 6,2,9,2 / 载荷 12,2,9,2 两侧完全一致** ✓。
输出:`FOMOD LANGUAGE CHECK OK / both wizard languages offer the same options and install the same files`。

**三个变异逐条命中**(每个都先读回磁盘确认,再跑门禁;全部回退到**字节相同**):

| 变异 | 门禁诊断 |
| --- | --- |
| 在 zh 分支的 `OStim Together` 里**多加一个文件夹**(`MUTATION_ZH_ONLY`) | R3:`only in en: [], only in zh: ['OptionalPlugins/MUTATION_ZH_ONLY']` |
| 从 zh 分支**删掉整个 `MorphSyncTogether` 插件选项** | R2:`offers 6 plugin option(s) in en and 5 in zh` |
| 把「可选组件」那一步的 `flagDependency` 从 `zh` 改成 `en` | R1:`en declares 5 install step(s) and zh declares 3` |

第三个变异最贴近真实返工:**复制一整段步骤去改语言标记时漏改一个**,于是那一步
**只出现在一种语言里**,而另一种语言的玩家**根本看不到它、也不会收到任何提示**。

**回退这一步本身也踩了坑,值得记下**:第一次用 `git checkout --` 回退,被沙箱拒绝
(`Unable to create '.git/index.lock': Permission denied`),而**失败时文件仍是变异的版本** ——
若不复核哈希,就会把「变异还在」的状态当成已回退,后面所有结论都建立在脏文件上。
改用编辑工具反向替换后**逐字节比对哈希确认**,才算回退成功。

### 149.3 构建要求的 xmake 不能高于 CI 安装的 xmake

这是**两处声明**,而且**不是同一类声明**,所以「让它们相等」既不是规则、也从来做不到:

```text
xmake.lua                        set_xmakever("3.0.0")     <- 下限,xmake 自己会拒绝运行低于它的项目
.github/workflows/windows.yml    xmake-version: 3.1.0      <- 实际装哪个 xmake
```

真正的不变式是**装的 ≥ 要求的**。**把下限抬到 pin 之上,每次构建都会失败**,
而 xmake 报的是「这个项目」而不是「这个工具链」—— **症状指向项目,根因在版本**。
新增门禁 `Tools/Scripts/check_xmake_version.py` 按这个方向检查,并打印读到的两个数:

```text
note: xmake.lua requires >= 3.0.0; workflows install windows.yml 3.1.0
XMAKE VERSION CHECK OK / every workflow installs an xmake at or above the version the build requires
```

两个变异(抬高下限到 `3.2.0`、把 pin 降到 `2.9.9`)都被抓到并已回退。

### 149.4 活缺陷:一个「无法检查时报告成功」的门禁,而且它就在 CI 里、一直是绿的

`Code/plugins/tools/check_workflow_shell.py` 用 `try/except ImportError` 包住 `import yaml`,
失败时打印 `POWERSHELL PARSE CHECK SKIPPED` 并 **`return 0`**。它的注释写着
「CI 会先装好 pyyaml」—— **而跑它的那个作业 `contract` 什么都不装**:
整个 `plugins.yml` 里唯一一句 `pip install pyyaml` 在 **`wizard` 作业**里,而 `wizard` 不跑这个门禁。

```text
contract 作业     pyyaml 从未安装  ->  ImportError  ->  SKIPPED + exit 0  ->  CI 绿色
wizard 作业       装了 pyyaml        (但 wizard 不跑这个门禁)
```

所以它**在 CI 里从来没有解析过任何东西,却一直是绿的** —— 这正是它本该防住的那类缺陷。
而它比「门禁根本不存在」更糟,因为 **「跳过」与「通过」对调用方是完全相同的输出**:
不存在的门禁**看得见**,跳过的不行。

**两处修改,方向相反但都要做**:

1. **门禁改为失败**:`ImportError` 时 `raise SystemExit`,写清 pyyaml 是**需求**而不是优化,
   并明确写出「不要把它降级成跳过」;另补**空语料保护** —— 一个 PowerShell 块都没找到时也失败,
   因为「没找到要检查的东西」与「检查完都没问题」同样无法区分。
2. **CI 满足这个需求**:在 `contract` 作业那一步加上
   `python -m pip install --quiet --disable-pip-version-check pyyaml`,
   并说明为什么装在这里(**在跑这个门禁的作业里装**)。

**两向验证**(本轮关键):

```text
python Code/plugins/tools/check_workflow_shell.py   -> WORKFLOW SHELL CHECK OK          (exit 0)
python .scratch_missing_dep.py <该脚本> yaml         -> WORKFLOW SHELL CHECK CANNOT RUN
                                                        VERDICT: FAIL (correct)
```

后者用一个**只把 `yaml` 变成不可导入**的包装器运行同一个脚本,
证明它在缺依赖时的行为**从「绿」变成了「红」**,而不是只在改动前后看起来不同。

### 149.5 新增门禁 `check_gate_integrity.py`:守住「门禁本身」

149.4 不是孤例,而是**一类**。`docs/FOLLOW-UP.md` 里早就把「门禁的可信度」写成三条
(它会跑吗 / 它会失败吗 / 它看全了吗),**但这三条此前也只是文字**。七条判据:

| 判据 | 防的是什么 |
| --- | --- |
| R1 没有 `except ImportError` 吞掉错误 | **149.4 的活缺陷**:无法检查时返回 0 |
| R2 没有未经登记的 `SKIPPED` 横幅 | 「跳过」与「通过」输出相同,每个跳过都要写明理由 |
| R3 磁盘上每个门禁都被某个工作流调用 | 门禁没人跑 = 带步骤的注释 |
| R4 每个门禁都被「跑它的那个作业」的路径触发覆盖 | 步骤在,但改动它不会启动作业 |
| R5 同一工作流的 `push` 与 `pull_request` 路径列表一致 | 只加一边 = 一个方向有守护、另一个方向没有 |
| R6 **工作流点名的门禁必须在磁盘上存在** | **写这个门禁时在它自己身上发现的,见下** |
| R7 跑门禁的作业必须安装了该门禁依赖的包 | **149.4 的根因**(`IMPORT_TO_PACKAGE` 把 `yaml` 映射到 `pyyaml`) |

**R6 是单向规则自己暴露的缺陷。** 最初的 R3 是「磁盘上的每个门禁都被调用」——**单向**的。
于是把 40 个门禁删到只剩 1 个,它照样打印:

```text
note: 1 gate script(s), 0 never run by CI
GATE INTEGRITY CHECK OK
```

**而工作流里仍然点着 40 个。** 一个**语料可以凭空消失而结论不变**的检查,
正是这个文件整篇在讲的那种缺陷。补成双向,并要求**两个方向各报一个数**:

```text
note: 40 gate script(s), 0 never run by CI
note: 40 gate script(s) named by workflows, 0 missing
```

**两个数必须同时是 40**:第一个掉了说明有门禁没接线,第二个掉了说明有步骤指向不存在的文件。

**顺带查出的四条真实问题**(全部当场修掉):

- `contract` 作业里**没有任何 `pip install`**,而它跑的门禁里有要 pyyaml 的(即 149.4);
- `check_fomod_languages.py`、`check_xmake_version.py`、`check_gate_integrity.py`
  **三条路径触发里都没有** —— 步骤加了,但**改动这三个门禁自己不会启动作业**;
- `.github/workflows/**` 此前不是任何触发路径 —— 而 R5/R7 读的正是这些文件,
  **改工作流不会让检查工作流的门禁跑起来**;
- `plugins.yml` 的 `push`/`pull_request` 两条列表里各有 **6 个重复条目**
  (`Code/client/**` 出现 3 次)。对 GitHub 无害,但它是**手改列表**的指纹,故**报告**而不失败。

### 149.6 反向验证(变异测试,七个变异)

| 变异 | 结果 |
| --- | --- |
| 给一个门禁加上吞掉 `ImportError` 的 `try/except` | CAUGHT |
| 给一个门禁加上 `print("… SKIPPED")` | CAUGHT |
| 新增一个**没有任何工作流点名**的门禁脚本 | CAUGHT |
| 两条路径触发列表里**同时**改掉一个门禁的名字 | CAUGHT |
| **只改 `push`** 那条列表,加一条 | CAUGHT |
| 从 `contract` 作业里**删掉那句 `pip install pyyaml`** | CAUGHT |
| 把 40 个门禁删到只剩 1 个 | CAUGHT(39 条 `does not exist on disk`) |

**其中四个变异第一次跑是 MISSED,而四个里三个是变异自己写得不精确** —— 比结果本身更值得记:

- 变异 3 原本改的是一个**已存在**的门禁脚本,**而工作流仍然按路径点着它**,所以 R3 看不见
  —— 改成**新增**一个脚本才测到;
- 变异 4 原本**只改一条**列表,于是先触发的是 **R5**(两条列表不一致),而 R5 命中时会跳过 R4
  —— 改成**两条一起改**才测到 R4;
- 变异 5 原本把新条目写成 **4 个空格缩进**(YAML 里跑到了别的层级),门禁**直接抛 traceback**
  —— 这里**修的是门禁**:`yaml.safe_load` 包上 `except yaml.YAMLError`,把
  「这个工作流根本解析不了、所以里面每个步骤都是死的」报成一条失败;
- 变异 6 原本给门禁加一句 `import requests`,触发的是「这个包名没登记」而不是「作业没装它」
  —— 改成删掉那句 `pip install pyyaml` 才测到 R7。

最终 `MUTATION TEST OK: all 7 mutations were caught`。

### 149.7 教训

> **「跳过」和「通过」必须是两种不同的输出。**
> 一个在**无法检查时返回 0** 的门禁,和**不存在的门禁**相比更危险:
> 不存在的门禁**看得见**,而它**在 CI 里是绿的**。
> **凡是 `except ImportError` / `except Exception` 之后 `return 0` / `pass` 的地方,
> 都是在把「没查」伪装成「查过了」** —— 缺依赖要**失败**,并且**在跑它的那个作业里装好它**。

> **教训(同轮第二条)**:**一个检查「X 必须被登记」的规则,必须同时检查「登记的东西必须存在」。**
> 单向版本对**语料缩小**完全无感:它遍历的是**它找到的东西**,
> 所以**删掉语料 = 少找到几个 = 仍然全部合规**。
> **凡是「遍历发现集合并要求每个成员满足某性质」的检查,都要配一条反向规则,
> 并且两个方向都要打印计数** —— 两个数一起看,才看得出语料有没有消失。

> **教训(同轮第三条)**:**变异测试也会撒谎,而它撒谎的方式和门禁一样 ——
> 变异没生效、或者生效在别的地方。**
> 所以每个变异都要**先读回磁盘确认**;更要紧的是,当变异 MISSED 时,
> **先怀疑变异,再怀疑门禁,但两个方向都要查**:
> 上面第三个 MISSED 的结果是**门禁真的有问题**(抛 traceback),而另外三个是**变异写歪了**。
> **分不清这两者,就会把「门禁不行」和「测试不行」混为一谈。**

**验证**:40 个门禁全部跑过一遍,**40/40 通过**;
`plugins.yml` 经 `yaml.safe_load` 复核:两条触发列表各 **81 条(74 条唯一)**、集合相等,
`contract` 作业 **46 个步骤 / 40 个门禁步骤**;`check_workflow_shell.py` 复核通过
(每个 PowerShell 块都能解析)。C++ 由 GitHub CI 编译验证(§0/§1:本机不编译)。

## 150. 2026-10-11 场次:Papyrus `native` 的两份手写清单 —— 一个「报了失败,却什么也没回答」的原生函数

> **锚点**:`Tools/Scripts/check_papyrus_natives.py`(新)、`Tools/Scripts/check_shipped_pex.py`(新)、
> `Code/client/Games/Skyrim/Misc/BSScript.cpp`(修)、`Code/client/Games/PapyrusFunctions.cpp`(修)、
> `.github/workflows/plugins.yml`(补两条路径触发 + 两个 `contract` 步骤)。

### 150.1 判据:同一个名字写在语言边界的两侧,而没有任何东西比较它们

§134(overlay 的三层名单)、§143(消息的四样俱全)用的都是这一条判据;这一轮把它用在
**Papyrus 与 C++ 之间**。`.psc` 里的 `Function Foo() global native` 是一句**承诺**,
C++ 里的注册才是**兑现**。两侧的写法完全不同(一个在脚本里、一个在 C++ 里),
**没有任何编译器或链接器会同时看到它们**:

```text
GameFiles/Skyrim/scripts/source/SkyrimTogetherUtils.psc:3
    bool Function IsRemotePlayer(Actor actor) global native

Code/client/Games/Misc/BSScript.cpp:83
    (*apThis)->BindNativeMethod(new BSScript::IsRemotePlayerFunc(
        "IsRemotePlayer", "SkyrimTogetherUtils", PapyrusFunctions::IsRemotePlayer,
        BSScript::Variable::kBoolean));
```

查了 **4 个第一方脚本目录**(`GameFiles/Skyrim/scripts/source`、
`plugins/TradeTogether/Source/Scripts`、`plugins/OStimTogether/compat/OStimUIConsent/Data/Scripts/Source`、
`plugins/OStimTogether/optional/OCumIntegration/Data/Scripts/Source`)与全树 **232 个 C++ 源文件**:
**9 个声明 / 9 个注册,类名与函数名全部一致**。所以这一半是**加守护**,不是改代码。

### 150.2 真缺陷:`return false` 不是「返回 false」,而是「这次调用失败了」

顺着调用链往下读一层,`Code/client/Games/Skyrim/Misc/BSScript.cpp` 里
`IsRemotePlayerFunc::MarshallAndDispatch` 与 `IsPlayerFunc::MarshallAndDispatch` 都是:

```cpp
BSScript::Variable* pArg1 = apStackFrame->GetStackFrameVariable(0, page);
Actor* pActor = pArg1->ExtractComplexType<Actor>();
if (!pActor)
    return false;                       // apResult 从头到尾没有被写过
bool result = ((FunctionType*)pFunction)(pActor);
apResult->Set<bool>(result);
return true;
```

`MarshallAndDispatch` 的返回值是**「这次调用成功了吗」**,不是脚本收到的值。
返回 false ⇒ VM 认为调用失败 ⇒ 用**返回类型的默认值**代替结果。
于是这两个原生函数**看起来**永远返回 `false`,而**没有任何一条日志或错误**:
调用方拿到的是一个合法的 `false`。

**而 `!pActor` 不是异常路径。** 所有出货调用点都先做一次**不检查结果的** `as Actor`:

| 调用点 | 写法 |
| --- | --- |
| `GameFiles/Skyrim/scripts/source/TempleBlessingScript.psc:7` | `Actor actorRef = akActionRef as Actor` → `IsRemotePlayer(actorRef)` |
| `GameFiles/Skyrim/scripts/source/ResourceFurnitureScript.psc:44` | `if SkyrimTogetherUtils.IsRemotePlayer(actorRef)` → `return` |
| `GameFiles/Skyrim/scripts/source/HallofStoriesDiskScript.psc:95,107,119` | `Actor actorRef = triggerRef as Actor` → `IsPlayer(actorRef)` |

`akActionRef`/`triggerRef` 是**激活物**,非 Actor 的激活物转换结果是 `None` ——
**每一次普通激活都会走进那个分支**。而这几处脚本的用法恰好都是**守卫**:
`if IsRemotePlayer(x) → return`,所以守卫**静默失效**,远端玩家可以像本地玩家一样
使用家具、领受神庙祝福。修法:

```cpp
if (!pActor)
{
    // None 是一个「答案」,不是一次「失败」:调用方做了不检查的 as Actor,
    // 非 Actor 的激活物是正常输入。
    apResult->Set<bool>(false);
    return true;
}
```

同一轮的第二个缺陷更小但同类:`Code/client/Games/PapyrusFunctions.cpp:19` 的
`IsPlayer` 第一行是 `spdlog::info("Calling IsPlayer");` —— 脚本每次调用都写一行日志。

### 150.3 新增门禁 `check_papyrus_natives.py`:五条判据,以及它自己踩的三个坑

| 判据 | 为什么 |
| --- | --- |
| R1 每个第一方 `native` 都有同名同类的注册 | 没有注册 ⇒ 调用**永远**返回默认值 |
| R2 每个注册都对应一个存在的声明 | 单向规则对**语料缩小**无感(§149 的教训) |
| R3 C++ 写明参数个数时必须等于 `.psc` 的参数个数 | VM 会拒绝参数个数不符的调用 |
| R4 注册里的类名必须是真实存在的 `Scriptname` | 类名拼错时脚本侧报的是「函数不存在」 |
| R5 任何 `MarshallAndDispatch` 都不许 `return false;` | **150.2 的判据本身** |

**它自己的三个坑,全都是「跑起来」才暴露的**:

1. **R3 一次都没跑过**。参数个数表是**按文件**建的,而这三个原生函数
   **注册**在 `Code/client/Games/Misc/BSScript.cpp`、**构造**在
   `Code/client/Games/Skyrim/Misc/BSScript.cpp` —— 两条路径,于是三条全部报
   「没有基类构造,参数个数未写明」。**一条规则静静地从未执行,而输出里看不出来**;
   改成**全树扫描**才真正开始跑。
2. 键名不匹配:`BIND` 抓到的是 `IsRemotePlayerFunc`,而构造表以 `IsRemotePlayer` 为键
   (去掉 `Func` 后缀才对齐)。
3. R4 里有一句**恒真式**:`cls not in known_classes and cls not in {… for … in declared}`
   —— 两个集合其实是同一个,后半句永远为假。

**而 R5 的第一版恰好漏掉了它自己为之而写的那个缺陷。** 判据写的是
`returns_false and not sets_result`,但**成功路径里也有** `apResult->Set<`,
所以 `sets_result` 恒为真 —— 把 `if (!pActor) return false;` **改回磁盘**,
门禁照样打印 `PAPYRUS NATIVE CHECK OK`。收紧成
`re.search(r"\breturn\s+false\s*;", body)`(花括号配对取函数体、剥掉 `//` 注释 ——
§143 的教训:**被注释掉的代码不是代码**)之后才抓得住。

**四个变异**(每个都先读回磁盘确认、每个都被抓、每个都已还原到字节相同):

| 变异 | 门禁诊断 |
| --- | --- |
| `NativeFunction(…, true, 1)` → `2` | R3:`registers SkyrimTogetherUtils::IsRemotePlayer with 2 parameter(s), but the script declares 1` |
| `"IsPlayer"` → `"IsPlayerMUTATION"` | R1+R2 **同时命中**,两个方向各一条 |
| `"SkyrimTogetherUtils"` → `"…UtilMUTATION"` | R4:`registers into script class '…', which no first-party .psc declares` |
| 把 `if (!pActor) return false;` 改回去 | R5:`returns false - that reports the call as failed and leaves the script with the return type's default` |

### 150.4 第二处同类配对:出货的 `.pex` 与它的 `.psc` 此前**没有任何关系**

`.psc` 是源码,**`.pex` 才是真正出货的东西**。`GameFiles/Skyrim/scripts/` 下
10 个 `.psc` 与 10 个 `.pex` 一一对应;`Tools/Packaging/New-STNModPackage.ps1:105`
打包的是 `.pex`;而**全树没有任何东西读它、比较它** —— `windows.yml` 的编译步骤
**故意写到临时目录、不覆盖 `GameFiles`**。也就是说:
**改了 `.psc` 而没重新编译,出货的是旧脚本,而源码是读者信任的那份。**

判据(新增 `Tools/Scripts/check_shipped_pex.py`):每个 `.psc` 必须有 `.pex`
(大小写不敏感配对 —— `PetFramework_PetQuest.psc` 的出货名是 `petframework_petquest.pex`)、
每个 `.pex` 必须有 `.psc`、**声明的每个名字**(Scriptname / `Function` / `Event`)
必须出现在 `.pex` 的可打印串表里、**每个 ASCII 字符串字面量也必须出现**
(多行字面量**逐行**查 —— `.pex` 里存的就是逐行的一项);
下限 `MIN_SCRIPTS = 8` / `MIN_LITERALS = 30`(空语料必须失败)。
刻意**不查**控制流、算术,以及**既不碰名字也不碰字面量**的编辑,理由写在文件头:
已提交的 `.pex` 出自原始编译器,`SkyrimTogetherVerifyLaunchScript.psc` 当初是
**照着它 `.pex` 里已有的字节**重写的,所以「重新编译得到逐字节相同的产物」本来就做不到。

两个变异都被抓:改一个字面量(`ResourceFurnitureScript.psc:121` 的
`"SoundPlay . NPCHumanWoodChop"`)与改一个事件名(`TempleBlessingScript.psc:5` 的 `OnActivate`)。

### 150.5 教训

- **跨语言的契约没有编译器**。同一个名字写在 `.psc` 和 C++ 里时,两侧各自都能通过
  自己的检查:**脚本能编译、C++ 能编译、链接成功、运行时不报错**,只是那个函数
  永远返回默认值。**凡是「A 声明、B 兑现」的配对,都要有人把两侧放在一起比。**
- **API 的返回值可能是「状态」而不是「值」**。`MarshallAndDispatch` 返回 `bool`,
  很容易读成「这次判断的结果是 false」;它是**「这次调用是否成功」**。
  把失败报给 VM 与把 `false` 交给脚本,对调用方是**同一种观察**——
  这正是它藏了这么久的原因。
- **一条规则可以静静地从未执行,而输出里看不出来**(150.3 第 1 条)。
  §149 修的是「门禁无法检查时报告成功」,这一轮是它的近亲:
  **门禁在检查,但其中一条判据一次也没跑过**,因为它的输入按错误的粒度构建。
  **计数必须打印出来**(`3 native function class(es) state a parameter count`),
  否则「0 条被检查」和「0 条有问题」长得一模一样。
- **一个门禁的第一版可能恰好漏掉它为之而写的缺陷**(§143 也遇到过)。
  这不是「再想一遍」能发现的,只有**把缺陷改回磁盘**、看着门禁打印 OK,才知道。

---

## 151. 2026-10-12 场次:一个包住两个组件的守卫,被记成了产品决定

**锚点**:
- `docs/FOLLOW-UP.md` 第 7 节 —— 本节把它从「开放,待产品决定」改成「已完成,已执行」
- `Code/skyrim_ui/src/app/components/root/root.component.html`(守卫与注释)
- `Code/skyrim_ui/src/app/services/client.service.ts`(监听 / subject / handler)
- `Code/skyrim_ui/src/typings.d.ts`(`OpeningMenuCallback` 与两条 `event:` 重载)
- `Tools/Scripts/check_overlay_bridge.py` 第 5 条判据(事件方向)
- `Code/skyrim_ui/src/app/services/party-hint.service.ts:43`、`Code/skyrim_ui/src/assets/i18n/en.json`
- `Tools/Scripts/check_ui_contract.py` 第 4 条判据(系统消息字面量)

### 151.1 起点:§141 把一个空行为**记录**下来了,但记错了代价

§141 发现:UI 订阅的 `openingMenu` 事件没有任何 C++ 产出者,而 `root.component.html` 用它
包住了一段 UI。当时写下的结论是:

> `openingMenu` 这条值得单独说,因为它的消费端是**整个 overlay**

以及据此得出的取舍:

> **实现产出者** ⇒ 开任何菜单**整个 overlay(含聊天窗、队伍列表)消失**

于是判定「这是产品决定,不是缺陷」,把事实写进 `docs/FOLLOW-UP.md` 第 7 节、
在 `root.component.html` 留下注释、用门禁的 `NO_EMITTER` 豁免把状态**锁住**。

**「不动行为」的做法本身是对的** —— 一个没人知道是空的行为,与一个被记录的空行为,
是两种工程状态(§141 原话)。错的是**那个代价的估计**。

### 151.2 订正:守卫里只有两个组件

核对删除前的文件:

```html
<ng-container *ngIf="inGame$ | async">
  <!-- 9 行注释:menuOpen$ is inert today ... -->
  <ng-container *ngIf="!(menuOpen$ | async)">
    <app-debug></app-debug>

    <app-group></app-group>
  </ng-container>

  <app-notification-popup-container ...>
  <app-notifications ...>
  <div [@controls]="active$ | async" class="app-root-controls">   <!-- 含 app-root-menu / app-root-status -->
  <div [@fadeInOutActive]="active$ | async" class="app-root-popups">
  ...
```

守卫里**只有** `app-debug`(F2 调试面板)与 `app-group`(队伍成员列表)。
**在守卫之外**的是:聊天窗(`app-window.app-root-chat`)、通知(`app-notifications`、
`app-notification-popup-container`)、主菜单(`app-root-menu`/`app-root-status`)
与整个弹窗层(`app-root-popups`)。

⇒ 「实现产出者」的真实代价是「F2 面板与队伍列表消失」,不是「整个 overlay 消失」。
**这句话把代价说大了一个量级**,而它当时没人核对 —— 三份文档(`CHANGELOG.md`、
`docs/PITFALLS.md` §141、`docs/FOLLOW-UP.md` 第 7 节)都照着抄了一遍。

### 151.3 决定:删掉死通道,而不是实现产出者

代价订正之后两条路都不贵,于是按「**空行为的存在成本**」取舍:

- **实现产出者** ⇒ 让 F2 面板与队伍列表在菜单打开时消失 —— 而队伍列表恰恰是玩家
  一边翻背包一边要看的东西,方向本身就存疑;还要先定义「哪些菜单算 menu」,
  是净增的未决问题。
- **删掉死通道** ⇒ 运行时**零变化**(`!(false)` 恒真),代价是丢掉 UI 侧那句写好的意图;
  真要做时重写这条通道只是十几行。

**一个被记录的空行为,比一个不存在的功能更容易被误认为已实现** ——
而它连 mock 都不发,浏览器里也看不出差别。所以删。

删掉的东西(逐条对账,与 `docs/FOLLOW-UP.md` §7.3 的表一致):

| 层 | 落点 |
| --- | --- |
| 守卫 + 说明 | `root.component.html:2-16`(`app-debug`/`app-group` 提到原父节点,位置与顺序不变) |
| 订阅字段 | `root.component.ts` 的 `menuOpen$` |
| 监听 + subject + handler | `client.service.ts` 的 `on('openingMenu')`/`off('openingMenu')`/`onOpeningMenu`/`openingMenuChange`(704→689 行) |
| 类型 | `typings.d.ts` 的 `OpeningMenuCallback` 与两条 `event: 'openingMenu'` 重载 |
| 门禁豁免 | `check_overlay_bridge.py` 的 `NO_EMITTER['openingMenu']` |

### 151.4 门禁怎么保证两边一起删

`check_overlay_bridge.py` 第 5 条判据是**双向**的:

| 判据 | 触发条件 |
| --- | --- |
| UI 订阅的名字必须有 C++ 产出者 | 只删豁免、留着监听 ⇒ 失败 |
| `NO_EMITTER` 豁免的名字必须仍被 UI 订阅 | 只删监听、留着豁免 ⇒ 失败 |

⇒ **两边必须一起删**,这是这次删改唯一的机械保证,也是 §141「用门禁锁住状态」那句话的兑现:
它当初锁住的是「不许悄悄实现」,现在锁住的是「不许删一半」。

**验证**:
- 改前 / 改后均 `OVERLAY BRIDGE CHECK OK` / `58 call(s) verified through all three layers`;
  note 从 6 条降到 5 条,消失的正是 `openingMenu` 那条(其余四条死监听与 `toggleDebugUI` 未声明不变)。
- **变异**:把 `skyrimtogether.on('openingMenu', () => {});` 加回 `client.service.ts`、
  不恢复豁免 ⇒ `OVERLAY BRIDGE CHECK FAILED (1)` /
  `Code/skyrim_ui/src/app/services/client.service.ts:130 subscribes to "openingMenu", which no C++
  code emits; the handler never runs and the state it drives can never change. Either emit it or
  record why it is dead in NO_EMITTER`;还原后回到 OK。
- `git grep -n "openingMenu"` 改后只剩 `CHANGELOG.md` 与 `docs/PITFALLS.md` 的历史行,
  以及 `root.component.html:4` 那条「已被删除」的说明注释。

### 151.5 同一轮的第二处:overlay 侧的系统消息也是散文

§137 修的是 C++ 侧:`SendSystemMessage("英文散文")` 送进聊天,而聊天把系统消息
**当翻译键**渲染(`chat.component.html:12-14` 的 `{{ message.content | transloco }}`)。
当时补的 `check_ui_contract.py` 第 4 条判据**只扫 `Code/client` 与 `Code/server` 的 `.cpp`**。

但 overlay 自己也往同一个聊天里推系统消息(`pushSystemMessage('...')`),
**走的是同一条渲染路径**。实测 10 个调用点里 9 个是点号大写键,1 个是散文:
`Code/skyrim_ui/src/app/services/party-hint.service.ts:43` 的
`'Party hint timer has been reset.'`。

- 修法:`en.json:195` 在 `SERVICE.PARTY_HINT` 段新增 `RESET`,调用点改引用该键。
- **只补 en.json 的依据**(实测):`check_i18n_params.py:146` 是
  `if key not in keys: continue  # a missing key falls back to en, and section 137 reported that separately`,
  即「某语言缺某键」被显式跳过;新键不含 `{{占位符}}`,不进入 `:134` 的 `reference` 集合,
  所以 R1/R2 计数逐字不变(`16 parameterised key(s), 12 call site(s), 3 runtime code(s)`)。
- 门禁扩到 overlay 侧:新增 `UI_SRC` 与
  `SYSTEM_MESSAGE_TS = re.compile(r"pushSystemMessage\(\s*'([^']+)'")`,以及一个**独立的**扫描块 ——
  **不与 C++ 的命中先合并再判空**,因为「某一侧扫到 0 个」正是 helper 被改名/搬走的样子,
  先合并会让另一侧的命中把它盖住(§150.5 那条「按错误的粒度构建输入」的近亲)。

**验证**:改前 / 改后均 exit 0;变异(a)(`:43` 换回散文)⇒ `UI CONTRACT CHECK FAILED (1)` 并精确指名
`party-hint.service.ts:43`;变异(b)(5 个文件的 `pushSystemMessage(` 改成
`// pushSystemMessageRenamed(`)⇒ 触发新增的空扫描防护。

**一条已知边界(如实记下)**:同一个变异如果把调用点**注释掉**而不是改名,门禁**不会**报警 ——
正则的 `\s*` 允许跨行,注释形式依然命中。**这是设计边界,不是漏洞**:该判据要抓的是「整侧扫空」,
即 helper 被改名或搬走;注释掉一个调用点并不使键消失,也不改变玩家看到的东西。
要连注释形式一起抓,得先剥注释或收紧正则 —— 那是另一条规则(「这种调用有没有被注释」),
本轮不做。

### 151.6 教训

- **影响取舍的事实,要先看代码再写。**
  「包住整个 overlay」这句没人核对,却决定了一个取舍,还被抄进三份文档。
  写进文档的**事实**与写进文档的**判断**要分开对待:判断可以讨论,事实必须先看。
- **当一条规则讲的是「渲染路径」时,要把喂它的每一侧都列出来。**
  §137 的判据写对了(「聊天把系统消息当翻译键」),落点却只写了一半 ——
  C++ 与 overlay 两个上游,只覆盖了一个。§141 的桥检查也是同一个形状:
  **桥有两端,检查要成对写**(§141.7)。
- **删掉一个空行为,和实现它,都可以是正确答案 —— 但要知道自己删的是什么。**
  这次如果没核对守卫的范围,就会一直停在「产品决定」上;核对之后发现,
  它既不贵也不重要,只是一个**被记成重要的空行为**。

## 152. 2026-10-12 场次:门禁只认「同一条语句里的判空」,于是 15 处「赋值-解引用」型缺陷一次也没被报出来

**锚点**:
- `Tools/Scripts/check_null_accessors.py`(`test_of()` 的形状表、`SELFTEST_CASES` 的 25 条用例)
- `Code/client/Games/Skyrim/Actor.cpp:650`、`Code/client/Games/Skyrim/Actor.cpp:909`
- `Code/client/Games/Skyrim/Forms/MagicItem.cpp:44`
- `Code/client/Services/Debug/Views/CalendarDebugView.cpp:14`、`Code/client/Services/Debug/Views/CellView.cpp:17`、`Code/client/Services/Debug/Views/ProcessView.cpp:14`、`Code/client/Services/Debug/Views/WeatherView.cpp:17`
- `Code/client/Services/Generic/CalendarService.cpp:74`、`Code/client/Services/Generic/CalendarService.cpp:110`
- `Code/client/Services/Generic/CharacterService.cpp:2533`、`Code/client/Services/Generic/DiscoveryService.cpp:216`、`Code/client/Services/Generic/InventoryService.cpp:299`、`Code/client/Services/Generic/PlayerService.cpp:276`、`Code/client/Services/Generic/TransportService.cpp:410`
- `Code/client/Systems/ModSystem.cpp:94`
- `Code/encoding/Structs/TimeModel.h:12-16`(退化默认值)
- `CHANGELOG.md:1213` —— 上一轮那 15 处是**另一批**站点

### 152.1 判据:上一轮的门禁,在真缺陷面前是绿的

上一轮那份门禁(207 行)只做一件事:`call_sites()` 逐条**语句**找 `accessor\(\)\s*->`,
再把 `GUARD` 正则套在该语句里解引用**之前**的文本上。它没有「赋值 → 使用点」的追踪。

两条可判定的测量:

| 门禁 | 指向的树 | 结果 |
| --- | --- | --- |
| 上一轮那份(单语句窗口) | 改动前的 13 个文件 | **0 个站点** |
| 本轮这份 | **同一份**改动前的树 | **15 个站点** |

也就是说:**装着全部 15 处缺陷的那棵树,上一轮门禁报的是 OK**。15 条逐行等于人工核对清单:

| 访问器 | 站点(赋值行 → 首次解引用行) |
| --- | --- |
| `EquipManager::Get` | `Actor.cpp:650→673`、`Actor.cpp:902→909`、`InventoryService.cpp:299→304`、`PlayerService.cpp:276→279` |
| `ModManager::Get` | `MagicItem.cpp:40→41`、`ModSystem.cpp:91→107` |
| `TimeData::Get` | `CalendarService.cpp:74→75`、`CalendarService.cpp:89→104`、`TransportService.cpp:410→411` |
| `PlayerCharacter::Get` / `ProcessLists::Get` / `Sky::Get` | `CellView.cpp:16→18`、`ProcessView.cpp:16→18`、`WeatherView.cpp:13→15` |
| `TES::Get` | `CharacterService.cpp:2510→2511`、`DiscoveryService.cpp:216→217` |
| `TimeData::Get` | `CalendarDebugView.cpp:12→14` |

### 152.2 订正:15 处从未被报出,不是形状漏认造成的

两种形状漏认的代价是**误报**,不是漏报 —— 这两件事此前被记成了一件。
把门禁的形状表换回上一轮的(不认 `&&`/`||` 右操作数、不认裸 `if (pX)`),跑**当前**树 ⇒ 恰好 **6** 个站点,
全部是**已经正确判空**的站点:

| 站点 | 写法 |
| --- | --- |
| `Code/client/Games/Skyrim/PlayerCharacter.cpp:115` | `if (pTes && pModManager)` —— 变量在 `&&` 右侧 |
| `Code/client/Services/Debug/DebugService.cpp:350` | `if (pProcessLists)` —— 整个条件就是该变量 |
| `Code/client/Services/Generic/DiscordService.cpp:53` | `if (!m_bCustomPresence && pPlayer)` —— `&&` 右侧 |
| `Code/client/Services/Generic/DiscoveryService.cpp:139` | `if (pModManager)` |
| `Code/client/Services/Generic/DiscoveryService.cpp:269` | `if (pModManager)` |
| `Code/client/Services/Generic/PlayerService.cpp:311` | `if (pPlayer)` |

这 6 条不是风格问题:`if (pProcessLists)` 与 `if (pProcessLists != nullptr)` 在 C++ 里是**同一个条件**,
门禁却只认后者 —— 于是它把「写了守卫的代码」报成缺陷,同时把「根本没写守卫的代码」放过去。

反过来也量过:把两种新形状整个去掉,当前树报 **19** 个站点 —— 树里还有 13 处既存代码也依赖这两种形状,
漏认的代价不止那 6 条误报。

### 152.3 本轮补的两种形状

`test_of()` 新增两条(变量必须**独立成立**,不许靠同名子串蒙混):

| 形状 | 正则 | 例 |
| --- | --- | --- |
| 变量是整个条件 | `\b(?:if|while)\s*\(\s*pX\s*\)` | `if (pProcessLists)` / `while (pPlayer)` |
| 变量是 `&&`/`\|\|` 的操作数 | `(?:&&|\|\|)\s*pX\b` | `if (pTes && pModManager)` |

`--selftest` 扩到 **25** 条用例(exit 0),分三组:
- **14 条必须判为「已判空」**:7 条原有(`if (pPlayer && pOther)`、`if (!pPlayer)`、`if (pPlayer == nullptr)`、
  `if (nullptr != pPlayer)`、块守卫、`assert(pPlayer)`、`pPlayer ? ... : ...`) + 3 条 `&&`/`||` 右操作数 + 4 条裸条件。
- **11 条近失必须继续判为「未判空」**:测的是别的变量、同名子串 ×4(`pPlayerExtra`)、
  `if (pPlayer->Foo())`、`if (Foo(pPlayer))`、`pOther = pPlayer;`、解引用**之后**的测试、嵌在更深块里的测试。
- `selftest()` 拒绝任何**没有解引用**该变量的用例,所以正向用例不可能靠「什么都没用」蒙过去。

### 152.4 15 处的修法:空指针时一律走**已存在**的退化路径

| 站点 | 守卫形状 | 空指针时落到哪条既有路径 |
| --- | --- | --- |
| `Actor.cpp:650`(`EquipOutfit`) | `if (!pEquipManager) return;`(在 for 之前) | 整个 default outfit 装备循环跳过 |
| `Actor.cpp:909`(`SetMagicEquipment`) | 赋值同层 `const bool hasEquipManager = pEquipManager != nullptr;`,三处调用 `if (hasEquipManager)` | 三处 `EquipSpell`/`EquipShout` 跳过;`GetGameId` 解析与三条 `spdlog::debug` 照旧,`try`/`catch`/`ReportFailure` 未动 |
| `MagicItem.cpp:44` | `if (auto* pModManager = ModManager::Get())` 包住整个 `runonce` 块 | `runonce` **不消耗**,下次调用重试;`bosFormID` 仍为 0 |
| `CalendarDebugView.cpp:14`、`CellView.cpp:17`、`ProcessView.cpp:14` | `if (!pX) return;`,**在 `ImGui::Begin` 之前** | 整帧不画该窗口(无匹配 `End` 会造成 ImGui 栈失配) |
| `WeatherView.cpp:17` | `if (pSky != nullptr) { ... }` ×2(天气段 + 三个按钮) | 天气段跳过;窗口其余部分照画(`Sky::Get()` 在主菜单/非室外单元为空) |
| `CalendarService.cpp:74`(`ToggleGameClock`) | `if (pGameTime != nullptr) { ... }` | 跳过五个字段快照;`s_gameClockLocked = !aEnable;` 在块外,两条路径都执行 |
| `CalendarService.cpp:110`(`HandleUpdate`) | 同上 ×3(含 fade 的两个分支) | 跳过写回引擎时钟;`m_lastTick` 记账与 fade 计时照旧 |
| `CharacterService.cpp:2533` | `if (!pTES) continue;` | 跳过该实体,循环继续 |
| `DiscoveryService.cpp:216` | `if (pTES == nullptr) { spdlog::warn(...); return; }` | 复用该文件 :265 附近既有的「没有 player cell 可报」warn 文案 |
| `InventoryService.cpp:299` | `if (!pEquipManager) return;` | 装备/卸装同步整段跳过 |
| `PlayerService.cpp:276` | `if (pEquipManager != nullptr) { ... }` | 法术装备跳过;`m_knockdownDeadline`/`m_knockdownStart` 在块外照设 |
| `TransportService.cpp:410` | `if (pGameTime != nullptr) { ... }` | `request.PlayerTime` 保持 `TimeModel{}` 默认(TimeScale 20 / Time 12 / Year 1 / Month 1 / Day 1),即该函数自己写下的「everything below degrades to a default」 |
| `ModSystem.cpp:94` | `pModManager ? pModManager->GetByName(...) : nullptr` | 每个条目落进既有 `else`(`++unmapped;` + `spdlog::error`) |

### 152.5 为什么 `Actor.cpp:909` 不能写成三处 `if (pX != nullptr)`

门禁只把 `depth[j] <= depth[call]` 的测试算作支配(`check_null_accessors.py:240`)。
三处调用各写一个 `if (pEquipManager != nullptr)` 块,守卫所在 block 比赋值行**更深**,门禁照旧报
`Games/Skyrim/Actor.cpp:909: EquipManager::Get() is dereferenced at line 922 without a null check`。
改成赋值同层的布尔才同时满足门禁与阅读 —— 这也是本轮唯一一处被门禁自己纠正过的写法。

### 152.6 验证

- `python Tools/Scripts/check_null_accessors.py` ⇒ `NULL ACCESSOR CHECK OK` /
  `19 nullable accessor(s) checked across 446 source file(s)` /
  `1 accessor(s) verified never-null, 0 exception(s), each still live`,exit 0。
- `python Tools/Scripts/check_null_accessors.py --selftest` ⇒ `NULL ACCESSOR SELFTEST OK (25 shape(s))`,exit 0。
- 改动前基线(13 个文件的改动前副本)对新门禁 ⇒ **15** 条,逐行等于人工清单;当前树 ⇒ **0** 条。
- 13 个文件的花括号净差 baseline 与 current 均为 **0**(无括号失配)。
- 隔离 diff(基线快照 vs 当前文件):19 个 hunk、169 行新增、58 行删除;
  58 行删除**全部**是有意替换的行(三处 `EquipSpell`/`EquipShout`、`MagicItem` 的 `runonce` 与内联 `if`、
  四个 Debug 视图的 `Begin` 与字段读取、`CalendarService` 的十个字段写回、`PlayerService` 的三段法术、
  `TransportService` 的五个 `PlayerTime` 字段、`ModSystem` 的 `GetByName` 条件)。
- **本机不编译**(无 xmake/MSVC),以上判据是静态的。

## 153. 2026-10-12 场次:5 个 RTTI id 在随仓库发布的地址库里**整条不存在**,而 `Cast<NiObjectNET>` 会**静默返回 nullptr**

**锚点**:
- `Tools/Scripts/check_rtti_coverage.py`(新,四条判据)
- `Code/client/Games/Skyrim/RTTI.cpp`(2806 条 `registerRtti_*`,门禁的第一份语料)
- `Code/client/Games/Skyrim/RTTI.h:17-27`(`Cast<T, U>`:两个 id 都在这里解析)
- `Code/client/Games/Skyrim/RTTI.cpp:12`(`aAllowUnresolvedStub=false`)
- `Code/client/Utils.h:54`(`Cast<T>`,38 个实参里唯一不是类型名的那一个)
- `GameFiles/Skyrim/SKSE/Plugins/versionlib-*.bin`(14)+ `versionlib-ae-to-se-*.map`(10)
- `docs/PITFALLS.md` §43.8(来源:它把同一批缺失记成了「潜伏项,不修」)
- `Tools/missing_ids_by_runtime.tsv:39` + `Tools/Scripts/check_map_coverage.py`(400180 的另一侧)
- `.github/workflows/plugins.yml`(push / pull_request 各一条路径触发 + 一条运行步骤)

### 153.1 起点:§43.8 记下了缺失,结论是「不修」,并留了一句「评审时盯住」

§43.8 实测的是:客户端要用的 id 里,有 4 个 RTTI 定位器在 1.6.1130 及以后的
`versionlib-*.bin` 里**整条不存在** —— 不是偏移为 0,是这一行没有:

```
邻居:  392518 = 0x203B928    392519 = (整条不存在)    392520 = 0x203B970
```

它同时说明了为什么补不上:`versionlib-*.bin` 是 SKSE 官方发布产物,改它会让哈希对不上,
`CheckAddressLibrary` 也可能不认;而 `versionlib-ae-to-se-1-5-97-0.map` 是本仓库自己生成的
(输入含 `st_overrides_all.tsv`),所以只有那张表能随便加东西。

结论写成「**记为潜伏项,不修**」,并留下一句:

> 一旦有人写 `Cast<NiObjectNET>`,它会静默返回 nullptr 而不是崩溃 —— 这正是需要在评审时
> 盯住的那类改动

**「评审时盯住」不是一种保证。** 它要求某个人在某个 PR 里恰好记得 §43.8;而这件事的两份
语料(客户端的 RTTI 注册表、随仓库发布的 24 张地址库表)**都已经在仓库里**,门禁读得到,
不需要靠谁的记忆。这一节就是把那句话换成一条会红的判据。

### 153.2 订正:不是 4 个,是 5 个;而「38 个实参」里有 1 个只在注释里

把口径从「代码里用到的 id」换成「RTTI 注册表」之后,数字有一处变化:

| 语料 | 数量 | 缺口的 id |
| --- | --- | --- |
| `RTTI.cpp` 注册的 id | 2806(互异) | — |
| 其中不被全部 24 张表携带的 | **5** | 392519 `NiObjectNET`、394235 `CreationClubMenu`、398437 `NiParticleSystem`、400159 `CCallbackBase`、**400180 `BSWin32GameDataSystemUtility`** |

前 4 个与 §43.8 一致(缺 6/6/6/3 张)。**第 5 个是这次新暴露的**:`400180` 在 14 张
`versionlib-*.bin` 里**全都在**,但在 **10 张 `versionlib-ae-to-se-*.map` 里全都不在** ——
即 pre-AE runtime 那一侧。它其实早就被声明过了(`Tools/missing_ids_by_runtime.tsv:39`,
以及 `Tools/missing_1_5_97_ids.txt`),只是那是**另一条链路**上的账:`check_map_coverage.py`
管的是 map 与声明的双向一致,而这一节管的是 RTTI 注册表与表的差集。同一个 id 出现在两张
账上是正确的,前提是两边说得出对方是谁 —— 所以门禁里 `400180` 的 reason 明写了
「legacy-map 那一侧由 `Tools/missing_ids_by_runtime.tsv` / `check_map_coverage.py` 持有,
两者不矛盾」。

第二处订正关于 `Cast<>` 的计数。任务最初的口径是「38 个不同实参、200 个站点」,那是**不清
注释**的裸扫。按仓库既有惯例先清空注释(`check_facegen_scope.py:62` 的
`strip_comments`,「Drop // and /* */ comments, so a retired call site cannot satisfy a check」)
之后是 **37 个实参 / 196 个站点**,差掉的 4 个站点全在注释里:

| 站点 | 实参 | 状态 |
| --- | --- | --- |
| `Code/client/Games/Skyrim/Actor.cpp:1157` | `TESNPC` | 块注释里的退役站点(`:1176` 还有一处活的) |
| `Code/client/Games/Skyrim/TESObjectREFR.cpp:589` | `ExtraTextDisplayData` | 注释里,**该类型在活代码里一次都不出现** |
| `Code/client/Services/Debug/Views/EntitiesView.cpp:159` | `Actor` | 注释里 |
| `Code/client/Services/Debug/Views/EntitiesView.cpp:160` | `TESObjectCELL` | 注释里 |

差别不是学术性的:不清注释的话,一段注释掉的 `Cast<NiObjectNET>` 也能让第三条判据报红,
而那条线根本不会运行。清空是**保偏移保换行**地替换成空格,所以失败信息里的 `文件:行`
仍然是真实行号。

### 153.3 决定:把「评审时盯住」做成一条判据,而不是把缺口补上

门禁的四条判据:

1. **每个 `Cast<>` 实参在每张表上都解析**(崩溃风险面)。失败时给出实参、它的 id、以及
   哪些表没有它。
2. **缺口与 `GAPS` 双向一致**:未声明的缺口失败(否则一条谁也解析不了的注册会悄悄通过);
   声明了却已经不再是缺口**也**失败(陈旧条目会让读者以为某个 runtime 还缺一个它已经有的 id)。
3. **`GAPS` 里的 id 不得被任何 `Cast<>` 触达**。这条就是 §43.8 那句话的机器化:一旦有人写
   `Cast<NiObjectNET>`,失败信息直接给出 `文件:行`,并说明「在那几个 runtime 上
   `RttiLocator<T>::Get()` 是 null,所以 `Cast()` 在到达游戏之前就返回 nullptr ——
   不是崩溃,是静默 no-op」。
4. **反空跑**:四类语料任一为空即失败(注册 0 条 / `Cast<` 站点 0 个 / 表 0 张 / 实际比较
   0 对),另外 `Cast<` 出现次数与解析出的实参数不等也失败 —— 那意味着实参正则已经不再匹配
   这棵树。`check_gate_integrity.py` 的 R2 存在的理由就是这种「什么都没比却打印 OK」的门禁。

**为什么是「不许被调用」而不是「修掉」**:缺口是官方库里的 id 空洞(`392519` 整行不存在),
不是本仓库能填的东西 —— 改 `versionlib-*.bin` 会让哈希对不上。所以门禁的语义是**记录并按住**:
它不要求缺口消失,它要求缺口**不被使用**;一旦被使用,就红。

**门禁做不到的四件事**(写进了 docstring,免得后来者以为它管得比实际多):

- 它补不上缺口,只能记录;
- 它看不见 `Cast<T, U>` 的**源类型 `U`** —— `U` 是从调用点的指针类型推导的,树里根本没有
  它的名字,只有 `T` 被写下来,所以只有 `T` 被检查;
- 它说不清某个调用点拿到 null 之后会怎样,那仍然是评审问题;
- 它只读 RTTI 表。`POINTER_SKYRIMSE` 那套 id 属于 `check_map_coverage.py` 的语料。

### 153.4 门禁怎么保证两边一起动

`Cast<>` 的实参里有一个不是类型名:`Code/client/Utils.h:54` 的 `Cast<T>`,它在一个
`template <class T> T* GetByServerId(...)` 里。它是**唯一**的一个,门禁按「单个大写字母」
的形状识别它、跳过并计数,而不是把它报成未注册类型 —— 否则这条 note 就会变成一条假失败:

```
  note: 1 Cast<> argument(s) are template parameters rather than type names
  (Code/client/Utils.h:54 is Cast<T> inside template <class T> GetByServerId);
  the id they resolve to is chosen at the call site and cannot be checked here
```

而 `GAPS` 表与判据 3 是**互相锁住**的一对:`GAPS` 里的 id 一旦被某个 `Cast<>` 触达,判据 3
就红;反过来,判据 2 保证 `GAPS` 里的每一条仍然真的是缺口,所以这张表不能靠「先声明了再说」
来消音。

**验证**:
- 改后 `python Tools/Scripts/check_rtti_coverage.py` → `RTTI COVERAGE CHECK OK` /
  `2806 registration(s) x 24 shipped library(ies) = 67344 id/runtime pair(s); 37 distinct
  Cast<> argument(s) over 196 site(s) resolve everywhere; 5 declared gap(s) are real and
  reachable from no cast`。
- **变异 (a)**:在 `Code/client/` 下加一个带 `Cast<NiObjectNET>` 的文件 ⇒
  `RTTI COVERAGE CHECK FAILED (1)` / `Code/client/_rtti_coverage_probe.cpp:6:
  Cast<NiObjectNET> resolves id 392519, which versionlib-1-6-1130-0.bin, … and 2 more do not
  carry; RttiLocator<NiObjectNET>::Get() is null there, so Cast() returns nullptr before it
  ever reaches the game - not a crash, a silent no-op, and the id is declared in GAPS precisely
  because nothing was supposed to reach it`。
- **变异 (b)**:删掉 `GAPS[392519]` ⇒ 报「1 个 id 没被全部表携带且 `GAPS` 没声明」;
  **反向变异 (b')**:给 `GAPS` 加一条 `392214` ⇒ 报「stale, all 24 shipped libraries carry it」。
- **变异 (c)**:把 `CODE` 指向不存在的目录 ⇒ 报「no `Cast<` mention … at all; an empty scan
  means the sources moved, not that nothing casts」;空表目录与空注册表另各报一条。
- 四条 sibling 门禁(`check_map_coverage` / `check_target_meta` / `check_library_scan` /
  `check_gate_integrity`)改后全绿。

## 154. 2026-10-12 场次:三个魔数只验过一个版本 —— 断言钉得住自己,问得动引擎的只有一行日志

**锚点**:
- `Code/client/Games/TES.h:168-178`(说明注释 + 三条 `static_assert`)
- `Code/client/Services/Generic/CharacterService.cpp:2267-2299`(查找失败的 `warn`、首次写入的 `info`)
- `Tools/Scripts/check_facegen_scope.py` 第 7 条判据(偏移断言 + 两行日志的级别与条件性)
- `docs/FOLLOW-UP.md` 第 1 节(状态仍是「待验」:本节把它从「没有判据」变成「有一个判据」)
- `Code/client/TiltedOnlineApp.cpp:71`(`set_level(debug)` 被注释掉 —— debug 不可见的根源)

### 154.1 起点:一个「记录在案」的假设,没有任何东西在维护它

客户端为了把 `bUseFaceGenPreprocessedHeads` 只对**从网络重建的 actor** 拨掉,
自己手走引擎的 INI 设置链表,因此依赖三个结构体偏移:`Setting::data +0x8`、
`Setting::name +0x10`、`INISettingCollection` 链表头 `+0x118`。
这三个数字是**在 SkyrimSE.exe 1.5.97 上量的**,1.6.x/1.7.x 全部未验证。

仓库里 1.6.1170 的逆向产物(`Tools/ida/st_target_meta_1170.json`)**帮不上忙**:
它的采集口径是「代码里真正调用的目标」,`INISettingCollection` / `GameSettingCollection` /
`GetSetting` 一条都不在其中。也就是说,这个假设当时**既没有断言、也没有日志、也没有门禁**
在维护 —— 它以三个裸魔数的形式躺在 `TES.h` 里。

### 154.2 这个缺口最坏的地方不是「可能错」,而是「错了看不出来」

`INISettingCollection::Get()` 经 `POINTER_SKYRIMSE(411155)` 解析,该 id 在
1.6.317/640/1170/1.7.99/1.7.104 上**全部有映射**;`GetSetting` 的返回值在调用点也判了空。
于是偏移错了**不会崩溃**:要么 `GetSetting` 返回空(开关不拨,隐形头照旧 —— 表现为
「bug 没修好」),要么返回错的对象(写坏别的 INI 设置)。两者都只以「功能没生效」出现。

**这让「AE 上没修好」和「AE 上偏移不对」两种解释无法区分。** 这正是要拆掉的东西。

### 154.3 拆法一:让假设在代码里可见(断言,但它不是答案)

```cpp
static_assert(offsetof(Setting, data) == 0x8);
static_assert(offsetof(Setting, name) == 0x10);
static_assert(offsetof(INISettingCollection, head) == 0x118);
```

**这三条断言不关闭缺口,而且必须把这一点写进注释** —— 它们只在编译期检查**本仓库自己的**
布局(`offsetof` 问的是这个翻译单元里的 `struct`,不是引擎里的内存),永远不会去问引擎。
它们的价值只有一条:把「这是按 1.5.97 量的假设」从散落的魔数变成**下一个读代码的人能看见的东西**,
顺带在有人改动结构体时立刻报错。

### 154.4 拆法二:让一行用户日志能判定它(这才是真正问得动引擎的东西)

`SetRemoteFaceGenForce` 里本来就有一行能直接判定,但它是 `spdlog::debug`:

```cpp
spdlog::debug("{}: bUseFaceGenPreprocessedHeads = {} (was {})", __FUNCTION__, pSetting->data, m_faceGenPreprocessedBackup);
```

而 `TiltedOnlineApp.cpp:71` 的 `set_level(spdlog::level::debug)` 是**注释掉的**,
所以发布版永不打印。两处改动:

1. **首次写入提升为 `info`**,并把解析到的 `Setting*` 一起打出来(用 `fmt::ptr`,
   仓库里 `BSGraphicsRenderer.cpp:106` 等处已有先例)。只打一次 —— 用函数内
   `static bool` 守住,因为 `CharacterService.h` 不该为一个日志改动而新增成员。
2. **查找失败路径补 `warn`**。这条路径原本是静默 `return`:

   ```cpp
   if (!pSetting)
       return; // nothing to force, and leaving the state alone keeps the next call honest
   ```

   空指针是引擎对**两种完全不同的情况**给的同一个答案:「这个运行时没有定义这个设置」
   和「这个运行时的 `INISettingCollection` 不是 `TES.h` 断言的布局」。
   不区分它们,「偏移不对」就永远看起来像「设置不存在」。

**判读方法(写进 `docs/FOLLOW-UP.md` §1):** `Setting*` 非空且值为 0/1 → 偏移正确,缺口关闭;
`warn` 出现 → 偏移不对。**这不需要任何反汇编,只需要一名 1.6.x/1.7.x 玩家的一行日志。**

### 154.5 拆法三:让门禁把上面两件事锁住

`Tools/Scripts/check_facegen_scope.py` 加第 7 条判据,比三件事:
① 三个字段的声明还在、三条 `static_assert` 还在(删掉断言 = 「只验过 1.5.97」这句警告从代码里消失,而且没人会发现);
② `SetRemoteFaceGenForce` 里有一行 `spdlog::info`、**格式串点名该设置**、**被 `if` 包住**(否则「只打一次」退化成刷屏);
③ 查找失败路径**既 `warn` 又仍然 `return`**(不能变成穿透后解引用空指针)。

三条判据都做了变异实测:改回 `debug` → `FAILED (1)`;删 `warn` → `FAILED (1)`;删一条 `static_assert` → `FAILED (1)`;还原后全部 exit 0。

### 154.6 教训

- **「编译期断言」和「验证假设」是两件事,必须分开说。** 断言只约束本仓库的布局,
  它对「引擎里的偏移是多少」一个字都没说。如果注释里不把这句话写清楚,下一个读代码的人
  很容易把 `static_assert(offsetof(...) == 0x118)` 当成「这个偏移已经验过了」——
  那正好是这次要消除的误解,而且是**更强版本**的误解(因为它看起来有机器在保证)。
- **静默失效比崩溃更值得为它写日志。** 崩溃会自己找上门;静默失效要靠人主动去看,
  而人只看得到发布版会打印的那些行。所以「哪一行能在发布版里看到」本身就是设计的一部分。
- **同一条路径上的两种失败原因,必须给出可区分的信号。** `if (!pSetting) return;` 是
  一个正确的防御,但它把「设置不存在」和「我们的偏移错了」合并成了同一个可观测结果 ——
  于是这个缺口**从设计上就无法被用户报告回答**。补一行 `warn` 的成本是 3 行,收益是
  把一个只能靠反汇编回答的问题变成一行日志就能回答的问题。

## 155. 2026-10-12 场次:同一个 `STRPM::Message`,框架路径交给插件的是**另一个结构**

**锚点**:
- `Code/plugins/STRPM/include/STRPluginMessagingAPI/STRPluginMessagingAPI.h:50-56`(四个 `MessageFlags` 常量)
- 同文件 `:102-110`(`struct Message`,六项 ABI,`flags` 与 `sequence` 就在其中)
- `Code/client/Services/PluginMessagingService.cpp:214`(`Send` 的第五个形参 `aFlags`)
- 同文件 `:469-470`(旧:接收侧写死 `STRPM::kMessageNone` 与 `0`)
- `Code/encoding/Messages/PluginMessagingRequest.{h,cpp}`、`Code/encoding/Messages/NotifyPluginMessaging.{h,cpp}`(两个线上消息)
- `Code/server/Services/PluginMessagingService.cpp:97-98`(服务端逐字段转发)
- `plugins/STRPluginMessagingAPI/src/STRPluginMessagingAPIRuntime.cpp:703`、`:713-714`、`:725`(对照实现)
- `Code/server/GameServer.cpp:886`(登录版本校验,`RT::kWrongVersion`)
- `Tools/Scripts/check_message_flags_parity.py`(本节新门禁)

### 155.1 现象:插件的诊断日志里 `sequence` 恒为 0

`MorphSyncTogether/src/UdpTransport.cpp:265-270` 与 `OStimTogether/src/STRPMTransport.cpp:444-451` 都会把收到的
`STRPM::Message::sequence` 打进日志(格式串里分别是 `sequence={}` 与 `seq={}`)。
实测这两处都**只是格式化实参,不进任何分支** —— 同文件紧随其后的 `QueueMessage(...)` / SKSE task 投递都不带 sequence。
所以这不是崩溃类缺陷,是**诊断信息缺失**:日志里那个恒 0 的值会让人以为协议根本没走 sequence,
而真正的原因在别处。

### 155.2 定位:同一个结构体,两条后端给出不同的值

契约里 `Message` 有六项,`flags` 与 `sequence` 是最后两项。独立 bridge 全部兑现:

```cpp
// plugins/STRPluginMessagingAPI/src/STRPluginMessagingAPIRuntime.cpp
:703   const auto sequence = _nextSequence.fetch_add(1);
:713   "|seq=" + std::to_string(sequence)
:714   "|flags=" + std::to_string(flags)
:725   if ((flags & STRPM::kMessageAllowLoopback) != 0) { Deliver(...); }
```

框架路径一条都没兑现。三条线索指向同一处,而不是三处独立疏漏:
- `Send(..., std::uint32_t aFlags)` 的 `aFlags` 在函数体内**出现 0 次**;
- 接收侧把两个字段**写死**;
- `kMessageAllowLoopback` 在 `Code/client/**` 里**一次都没出现**。

### 155.3 根因:不是漏赋值,是信封里没有这两个字段

先查线上消息定义:`PluginMessagingRequest` 只有
`Channel / TargetKind / TargetPlayerId / PluginData`,`NotifyPluginMessaging` 只有
`Channel / SenderDisplayName / SenderPlayerId / SenderIsHost / PluginData`。
**两个报文都没有地方放 flags 与 sequence。** 所以接收方除了写死常量,别无选择 ——
把它记成「某处忘了赋值」会指向错误的修法(改一行赋值,线上仍然传不过去)。

### 155.4 修法:随 payload 一起走,服务端不解释

- 两个线上消息各加 `std::uint32_t Flags{ 0 };` 与 `std::uint64_t Sequence{ 0 };`,
  用仓库既有的 `WriteVarInt`/`ReadVarInt`(uint64 字段的既有写法,见
  `Code/encoding/Messages/ClientReferencesMoveRequest.cpp:7`),`SerializeRaw` 与 `DeserializeRaw`
  按**同一顺序**读写,且都追加在 payload 之后(不移动既有字段的偏移)。
- 客户端 `Send` 用 `std::atomic<std::uint64_t> m_nextSequence{ 1 }` 每实例自增打戳,并把 `aFlags` 原样写入请求。
- 服务端只逐字段搬运,仍然不解释这两个值 —— 它是插件 payload 的 router。
- 接收侧改用 `acMessage.Flags` / `acMessage.Sequence`。
- `kMessageAllowLoopback` 在 `Send` 内本地投递一次。**注意重入**:同文件 `:497-498`
  与 `FireProxyMapping` 都刻意「锁内收集、锁外调用」(插件可以在自己的 receive handler 里 send,
  非递归 mutex 会死锁),回环投递必须沿用同一纪律,不能在持锁时调用插件代码。
  本地投递的 `Message.sender` 用本机身份(`transport.GetLocalPlayerId()` + `m_localDisplayName`),
  `isHost` 只能给 false —— 客户端没有「我是主机」判定,独立 bridge 的 `Deliver` 同样给 false。

### 155.5 为什么可以加字段:同版本配对是唯一受支持配置

`Code/server/GameServer.cpp:886`:

```cpp
if (acRequest->Version != BUILD_COMMIT) { ... sendKick(RT::kWrongVersion); return; }
```

客户端与服务端的 `BUILD_COMMIT` 必须**完全相等**才允许登录,不等就是 `RT::kWrongVersion` 踢出;
`CHANGELOG.md:7` 也写明每个 tag 附**版本号相同的两个包**(客户端 mod 与专用服务器)。
跨版本混用不是受支持配置,因此给这两个报文追加尾部字段安全。
**若将来出现跨版本混用的合法路径,必须停下来重新评估,不能沿用这个结论。**

### 155.6 未验证:底层传输的可靠性

`Libraries/TiltedConnect`(以及 `TiltedHooks` / `TiltedReverse` / `TiltedUI`)在本机是**未初始化的空 submodule**
(`.gitmodules` 有登记,目录 0 文件),全仓 grep `SendMessageToConnection|nSteamNetworkingSend|k_nSteamNetworkingSend_Reliable`
**零命中**。因此「底层是否可靠有序」在本仓库源码范围内**未验证**。
结论只到这一步:插件传的 `kMessageReliable|kMessageOrdered` 被框架忽略后,
本机**无法证明**它带来了行为差异,所以本次**只做透传,不改协议语义** ——
既没有把这些位当成保证,也没有把它们当成噪声删掉。

### 155.7 门禁怎么防

`Tools/Scripts/check_message_flags_parity.py`(纯标准库)五条静态断言,任一条不满足即非零退出并打印 `file:line`:

| 判据 | 触发条件 |
| --- | --- |
| R1 `Send` 的 flags 形参在函数体内至少出现一次 | 只出现在签名里 ⇒ 失败 |
| R2 `Send` 把该形参写进请求,且 sequence 右侧不是常量 | `Flags = 0;` 或 `Sequence = 0;` ⇒ 失败 |
| R3 `OnPluginMessage` 的 `message.flags/sequence` 来自收到的消息 | 右侧是 `0` / `STRPM::kMessageNone` 之类常量 ⇒ 失败 |
| R4 两个线上消息都声明两字段,且写序与读序一致 | 少一个字段,或两侧顺序不同 ⇒ 失败 |
| R5 服务端把两字段从请求转发进通知 | 缺 `notify.Flags = request.Flags;` 之类 ⇒ 失败 |

R3 正是本次缺陷的形状:`message.flags = STRPM::kMessageNone;` 这类**常量赋值**被判失败,
所以「改回常量」这种回退会被立刻拦住。

**验证**:
- 修好后:`MESSAGE FLAGS PARITY CHECK OK` / `the flags and the sequence a plugin sends are the ones a receiver is handed`(退出 0);
- 变异体(落盘实测两个,另用内存 harness 测九个,共十一种):`Flags = aFlags;` 改成 `Flags = 0;` ⇒
  `Code/client/Services/PluginMessagingService.cpp:214: Send never copies 'aFlags' into the request ...`(退出 1);
  `message.flags = acMessage.Flags;` 改回 `STRPM::kMessageNone` ⇒
  `Code/client/Services/PluginMessagingService.cpp:527: 'message.flags = STRPM::kMessageNone;' is a constant ...`(退出 1);
  还原后回到退出 0;
- `check_gate_integrity.py` 退出 0(新门禁已被 workflow 点名,且被 path trigger 覆盖)。

## 156. 2026-10-13 场次:14 个门禁在「什么都没比」时报 OK —— 空语料被当成了通过

**锚点**:
- `Tools/Scripts/check_gate_integrity.py`(元门禁的 R2 只登记了一个豁免:`check_ostimng_versions.py`。它守的是**显式跳过**——`print` 里出现字面量 `SKIPPED` 就必须登记;它守不住「循环跑了 0 次然后打印 OK」)
- `Code/plugins/tools/check_skse_bootstrap.py:128-135`
- `Code/plugins/tools/check_exports.py:38,43`
- `Code/plugins/tools/check_contract_copies.py:240,246`
- `Code/plugins/tools/check_transport_compat.py:77,86`
- `Code/plugins/tools/check_skee_abi.py:205,259,267`
- `Code/plugins/tools/check_ostimng_build.py:151-165`
- `Tools/Scripts/check_allocation_guards.py:81-93,152-156,186-188`
- `Tools/Scripts/check_patch_offsets.py:277`
- `Tools/Scripts/check_crash_diagnostics.py:98`
- `Tools/Scripts/check_capstone_report.py:191`
- `Code/plugins/tools/check_ostim_support_message.py:95,99`
- `Tools/Scripts/check_ae_to_se_overrides.py:278-283`
- `Tools/Scripts/check_library_scan.py:175-179`
- `Tools/Scripts/check_package_metadata.py:98-104`

### 156.1 形状:门禁「跑了」,而比较次数是 0

前几轮修的都是**显式跳过**:`except ImportError: print('SKIPPED'); return 0`,元门禁 R2 就是为它写的。
这一轮是另一种形状 —— 门禁**没有跳过任何东西**,它正常走进循环、正常打印 OK,只是**循环体一次都没执行**。

判据代码里长这样:
```python
initializer = re.search(r'SKSEPlugin_Version\s*=\s*\{(.*?)\n\};', code, re.S)
if initializer and not failures:      # 正则没命中 ⇒ 下面三条规则全不跑
    ...三条标志位规则...
print('SKSE BOOTSTRAP CHECK OK')      # 仍然是绿的
```
以及
```python
for constant, symbol in sorted(named.items()):   # named 为空 ⇒ 循环 0 次
    if symbol not in exported:
        failures.append(...)
print('EXPORT CHECK OK')
```

**「数成 0」比「数错」更危险**:数错会打印一条错的行号,人会去看;数成 0 会让门禁打印 OK,而 OK 与「真的比过了」在输出上**完全一样**。

### 156.2 14 个门禁的机制与修法(逐条)

| 门禁 | 空语料的来源 | 修法 |
|---|---|---|
| `check_skse_bootstrap.py` | 正则要求终止符 `};` 在**第 0 列**;把它缩进一格,`initializer` 就是 `None`,而 `if initializer and not failures:` 让三条标志位规则整段跳过 | 正则放宽为 `\n\s*\};`(`:129`);读不到初始化器**报失败**而不是跳过(`:130`) |
| `check_exports.py` | 两处 `re.findall` 都可能返回空,报告循环执行 0 次 | `if not named:`(`:38`)、`if not exported:`(`:43`) |
| `check_contract_copies.py` | `if other is None: continue` 之后**仍然 `checked += 1`**,而 `checked` 没有下限(基线 25);把一份 vendored 头文件清空就掉到 22 却仍然通过 | 逐副本下限(`:240`)+ 全局 `if not checked:`(`:246`) |
| `check_transport_compat.py` | 两条顺序规则被 `if request_index >= 0 and 'kSetTimeCommandRequest' in client:` 门控;锚点改名后整条规则消失,门禁却仍然打印 `new opcodes appended, chat indices unchanged, docs remain accurate` | 改成 `elif request_index >= 0:`(`:77`)、`elif notify_index >= 0:`(`:86`)。**刻意用 `elif`**:只有「opcode 声明着、而锚点标识符没了」才是缺陷,opcode 合法缺席仍然算 skip |
| `check_skee_abi.py` | `if not match: return None` 被调用方读成「这个插件不用那个接口」,而它**仍然 `checked += 1`**;把 `class IOverlayInterface` 改名后会打印 `0 declaration(s) checked against RaceMenu` + `SKEE ABI CHECK OK` | 三层下限:每插件(`:205`)、每接口(`:259`)、全局(`:267`) |
| `check_ostimng_build.py` | `rglob('vcpkg.json')` 在未 checkout 的子模块上静默返回空 | 与仓库已有先例对齐:`check_ostimng_versions.py:126` 在什么都没 checkout 时 skip(元门禁 `ALLOWED_SKIPS` 里唯一那条),但 `:158` 在**插件存在却不贡献站点**时失败。所以 `if not root.is_dir(): continue` + `if not manifests: failures.append(...)`(`:151-165`) |
| `check_allocation_guards.py` | `deref_pattern()` 只认 `p->field` 与 `p[i]` 两种拼写;末尾的反向循环是**空操作**;摘要行算的是 `total = sum(len(f) for f in GUARDED.values())` —— **常量表的长度,却被当成实测值打印** | `:81-93` 三条交替(`p->`/`p[`、`(*p).`、裸 `*p`);`:102,130,158` 把 `examined` 与 `compared` 分开统计;`:152` 换成真正的语料下限 `if not failures and compared == 0:`;`:186-188` 摘要改打两个实测计数;`:173` `--list` 表头改成「列出的是表本身」 |
| `check_patch_offsets.py` | `checked` 可以是 0,没有任何东西被比过 | `:277` `if checked == 0:` → `no GamePatch::At(...) call site was found under Code/client, so nothing was compared against st_patch_offsets_1597.tsv; an empty scan means the sources moved, not that the offsets agree` |
| `check_crash_diagnostics.py` | `if cleanup:` —— 正则未命中就静默跳过整条规则 | `:98` `if not cleanup: failures.append(...)` + `else:` 保留原体 |
| `check_capstone_report.py` | `--list` 分支**直接 `print` 三句判定字面量**,而 `--list` **从不调用 `check()`** | `:191` 改成 `this gate compares (run without --list for the verdict):` + 三条**规则名**;删掉硬编码结论句。**这条缺陷不在退出码里** —— 任何基于退出码的变异都测不到它,只能读 `--list` 的文本 |
| `check_ostim_support_message.py` | 选择器侧与消息侧各自可能为空 | `:95` `if not accepted_exact and not accepted_prefix:`、`:99` `if not claimed_exact and not claimed_prefix:` |
| `check_ae_to_se_overrides.py` | `SHIPPED_MAP` 缺失或为空时只 `warnings.append` | `:278-283` 改成 `failures.append(...)`;`:257` 加注释说明「空 map 只在下面记一次失败,这里 `continue` 免得每行重复」 |
| `check_library_scan.py` | `checked == 0` 时只 `warnings.append('no address library found on disk...')` | `:175-179` 改成 `failures.append`,理由与同文件 `:177` 的既有先例一致:「these libraries under version control, so an empty scan means they moved rather than that the filter is correct」 |
| `check_package_metadata.py` | `newest_own_version()` 返回 `None` 时只 `warnings.append('no tag ... a shallow clone has none')` | `:98-104` 改成 `failures.append`:`no tag of this repository's own series is visible, so the committed version was never compared against a release; a checkout without tags is not evidence that the metadata is in step. The workflow fetches them (fetch-depth: 0, fetch-tags: true)...` |

**共同修法**:门禁的语料**本来就在版本控制里**。它的缺席意味着检出损坏或扫描条件写错了,**不是**「这台机器上不需要检查」。所以 `warnings.append` 是错的容器 —— `warnings` 只被打印,永不进入退出码。

### 156.3 三个方法论错误:变异没落地、镜像的行尾、pre-fix 也红

这一轮的验证 harness(`.verify_vacuity.py`,非交付物)自己错了三次,每一次都长得像「门禁漏了」:

1. **「pre-fix 版本」最初是用行号区间从当前文件里切出来的**。行号一漂移就切错,于是报了三条假 `UNEXPECTED`。改成从 `.mut-mirror/orig/` 取独立备份后才对。
2. **一处常量把文件名拼成 `SkieInterfaces.h`,而真名是 `Skee`Interfaces.h(双 e)**。拼错的路径让变异**根本没应用** —— 而「门禁没抓到」与「什么都没改」在输出上完全一样。
3. **还原步骤只按行号恢复,漏掉了一处**,于是下一次运行的「干净基线」本身是脏的。

⇒ **教训:变异测试必须先断言变异真的落地(字节真的变了、`after != before`)再跑门禁**,否则整轮测量的意义是零。

镜像树还有两个陷阱:

- 镜像建立时经过的一层把门禁文件写成了 **CRLF**,于是 sha256 与真树不同,harness 报了 8 条假 `STALE`。用 `\r\n` 归一化后重比:`mirror == real` 全为 True,`real vs mirror diff: 0 lines`。⇒ harness 现在开头调 `sync_gates()`,把真树门禁**按字节**覆盖进镜像,不再靠假设。
- **`Path.read_text()` 的 universal newlines 会把 `\r\n` 折成 `\n`**,所以探针里数 `\r` 恒为 0 —— 早期探针就是这样自欺的。只有 `read_bytes()` 看得出。

还有三条**变异本身太弱**的教训(修法是对的,是测量打偏了):

- `check_exports.py`:改**常量名**不是有效变异 —— `contract.json` 的 `exportNames` 存的是**字符串值**,`named` 仍拿得到 3 条。要打中必须改字符串值或改 manifest。
- `check_ostimng_build.py`:把 manifest **清空**什么也证明不了 —— 文件仍被 `rglob` 找到,随后解析失败,而修复前的门禁本来就会抓。只有**删除**才真正清空扫描。
- `check_skee_abi.py` / `check_allocation_guards.py`:只改一份拷贝 / 只改一个成员都不够 —— 另一份拷贝仍声明该接口,另外几个函数仍解引用自己的成员,下限不触发。`check_allocation_guards.py` 的基线 `compared` 是 **4 而不是 6**(`GetByType` 走 `auto pEntry = data;`、`GetCount` 走 `BSExtraData* pNext = data;`,都是局部别名)。
- `check_library_scan.py` 的空语料**无法用镜像清空**:它的 `LIB_DIRS` 第二个元素是绝对路径 `D:/game/SkyrimSE/Data/SKSE/Plugins`,删掉仓库那份仍有库可选。这正是它那条下限在本机永不触发的原因。

最后一条判读纪律:**pre-fix 也红 ≠ 缺陷被确认**。那说明该变异打中的是**另一条既有规则**,不能用来证明这条缺陷存在过。`check_ostim_support_message.py` 的两条 selector 提取同时清空就是这种情况,如实记为 `pre-fix also red`。

### 156.4 验证:四阶段判据 + 控制组

每个门禁走四阶段,全部落在镜像树上(真树的 `Code/`、`Tools/`、`docs/` 在本会话对沙箱不可写,只有根目录与镜像可写):

1. **clean** 必须退出 0;
2. **变异** 必须非 0(带新消息);
3. **pre-fix**(用 `.mut-mirror/orig/` 的修复前副本跑同一变异语料)必须 **0** —— 这才是「洞是真的」的证明;
4. **还原** 必须回到 0。

结果:`problem(s): 0`。11 个门禁全部 `pre-fix exit=0 → fixed exit=1 → reverted exit=0`;`check_ostim_support_message.py` 记为 `pre-fix also red`(见上)。

**控制组必须保持绿**(证明修法没有把合法形态变成失败):

- `check_skse_bootstrap.py`:只把终止符**缩进**成 `    };` —— 纯重排,必须仍然绿(证明放宽正则没有把重新格式化变成失败);
- `check_skee_abi.py`:`OStimTogether` 合法地不声明 `IBodyMorphInterface`,仍 exit 0 并报 `skee interfaces: 5 declaration(s) checked against RaceMenu`;
- `check_ostimng_build.py`:把**整个 `plugins/OStimNG` 目录删掉** —— 合法缺席,必须仍然绿(证明 scoped 下限没有把「未 checkout」变成失败);
- `check_exports.py`:把 `    virtual ` 改成 `  virtual ` 的纯空白重排 —— 下限不被空白触发。

全量复扫:`ran 44 gate(s); 44 passed, 0 failed`;`check_gate_integrity.py` 退出 0;改动文件 `ast.parse` 干净。

### 156.5 教训

- **门禁的语料在版本控制里,缺席就是失败。**「没找到可比的东西」与「比过了,没问题」必须产生不同的输出 —— 前者要一条指名文件的失败行,而不是 `note:`。
- **`warnings` 是给人看的,不是给 CI 看的。** 本仓库里 `warnings` 只被打印、永不进入退出码,所以任何「规则 X 的语料不存在时 append 一条 warning」的写法,等于「规则 X 可以被静默退役」。
- **`if X and not failures:` 是这一批缺陷的共同语法。** 只要 `X` 来自正则或查找,它就有第三种取值:没找到。没找到要单独判。
- **`--list` 也不能携带判定。** 它是文档;让它断言结论,等于让文档在没人运行检查时替检查发言。
- **变异测试的第一条断言是「变异落地了」。** 没有这一条,`MISSED` 与 `NOT-APPLIED` 无法区分,而两者的结论完全相反。

## 157. 2026-10-13 场次:五个「覆盖洞」—— 判据依赖的那份数据被削弱后,门禁仍然报 OK

**锚点**:
- `Code/plugins/STRPM/contract.json`(`exportNames` 4 条、`headerSha256`、`consumers` 7 条)
- `Code/plugins/STRPM/include/STRPluginMessagingAPI/STRPluginMessagingAPI.h:22-29`(4 个 `kQuery*ExportName[]`)
- `Code/client/Services/PluginMessagingExport.cpp`(4 个 `STRPM_EXPORT` 定义)
- `Code/plugins/tools/check_exports.py:15-18,46-79`
- `Code/plugins/tools/check_skee_abi.py:99-102,217-236,241-254`
- `Code/plugins/tools/check_contract_copies.py:316-324`
- `Tools/Scripts/check_library_scan.py:131-135`
- `Tools/Scripts/check_target_meta.py:234-250`
- `plugins/MorphSyncTogether/src/SkeeInterfaces.h:35,97,160`
- `plugins/OStimTogether/src/RaceMenuOverlayBridge.cpp:46,124`

### 157.1 与 §149/§156 的区别

§149 是「**无法检查时报告成功**」,§156 是「**什么都没比就报成功**」。
这一批是第三种:门禁**确实在比**,但**它比的那份数据本身被削弱了**,而削弱这件事没有任何东西在守。

语料不是「扫描范围」,而是「**判据所依赖的那份文件**」:
- 头文件里的 4 个契约常量 ↔ `contract.json` 的 `exportNames`;
- 每个插件声明了哪些 SKEE 接口 ↔ 一份手写的期望表;
- `contract.json` 里的 `headerSha256` ↔ 权威头的实际哈希;
- `Code/skse_client/xmake.lua` 里的 legacy 构建目标;
- 随包出货的 14 个 AE 地址库。

**共同形状**:把这份数据改弱(改名一个常量、删一个键、少一个文件、把库截断),门禁不是失败,而是「比原来更少的东西、然后打印 OK」。

### 157.2 五个洞

**洞 1 —— `check_exports.py` 从不读 `contract.json`。**
它的 docstring 写着「Verify the framework exports the exact plugin entry points **the contract names**」,而代码只读头文件的常量声明。`named` 是 **dict**,把 4 个 `kQuery*ExportName[]` 改名一个就只剩 3 条,两条既有下限都不触发,循环照跑。
**修法**:读 `Code/plugins/STRPM/contract.json` 的 `exportNames`(`:46-79`),与头文件扫出的值做**双向**集合相等 —— 前向 `required - declared` 与反向 `declared - required` 都记失败。manifest 缺失 / JSON 非法 / 没有 `exportNames` 列表,三者都记失败。
**为什么反向也要失败**:`exportNames` 是 manifest 公布的契约,`strpm_contract.py` 与所有下游消费者都这样读它;一个自身被收窄的 manifest 会让门禁保持绿。

**洞 2 —— `check_skee_abi.py` 没有 per-copy 下限。**
`IOverlayInterface` 被**两份**源文件声明(`plugins/MorphSyncTogether/src/SkeeInterfaces.h:97`、`plugins/OStimTogether/src/RaceMenuOverlayBridge.cpp:46`)。只改一份时 `declaring['IOverlayInterface']` 仍非空,上一轮补的 per-interface 下限不触发,**那一份的方法顺序静默离开比较**。
**为什么 per-interface 下限不够**:它的判据是「这个接口还有没有任何人声明」,而缺陷的形状是「**这一份**不再声明了」—— 同一接口被两个组件各 vendor 一份时,前者恒为真。要抓它,判据必须是**逐份**的。
**修法**:`EXPECTED` 表(`:99-102`)记录「每个插件被记为声明哪些接口」——
```python
EXPECTED: dict[str, set[str]] = {
    'MorphSyncTogether': {'IBodyMorphInterface', 'IOverlayInterface', 'IOverrideInterface'},
    'OStimTogether': {'IOverlayInterface', 'IOverrideInterface'},
}
```
逐插件双向比对(`:225` 缺声明 / `:231` 多声明),并沿用仓库既有的**反向 stale 检查**(`:241-254`):`EXPECTED` 点名了 `SOURCES`/`REFERENCE` 里没有的东西也失败。
**实测期望矩阵**:MorphSyncTogether 声明全部 3 个(`:35`/`:97`/`:160`);OStimTogether 只声明 2 个(`:46`/`:124`),**不含 `IBodyMorphInterface`** —— 这个合法缺席是控制组,必须保持绿。

**洞 3 —— `check_contract_copies.py:317` 的 `headerSha256` 缺失只 warn。**
`recorded = manifest.get('headerSha256')` 为空时只 `warnings.append`,而 `warnings` 只打印、永不进退出码 ⇒ 删掉这个键就**静默退役了整条 header-hash 规则**。
**修法**:`:316-324` 改 `failures.append`,附注释说明「A warning here would be a silent skip… The manifest is the corpus of this rule, and a missing corpus is a failure rather than a satisfied check.」

**洞 4 —— `Tools/Scripts/check_library_scan.py:131-135` 的 `xmake.lua` 缺失只 warn。**
**自相矛盾**:同文件 `:89-99` 已经因为 `SOURCE`(`Code/skse_client/main.cpp`)缺失而 `failures.append`。同一个门禁里「源文件缺失 = 失败」而「xmake.lua 缺失 = 警告」不一致,而 `Code/skse_client/xmake.lua` 是 **git 受控文件**,缺失意味着被删/被移动。
**修法**:`:131-135` 改成同一形状。

**洞 5 —— `Tools/Scripts/check_target_meta.py:234-250` 的地址库缺失只 warn。**
`read_library()` 对**存在但读不动**的库返回 `{}`,旧代码 `if not library: continue` 静默跳过,而摘要行仍把它算进 `all 14 AE runtime(s)`。
**修法**:`:245-249` 在 `continue` 之前记失败:`{path.name} is present but is not a readable AE address library, so no recorded id was checked against it. These libraries are tracked under version control, so an unreadable one means the coverage claim is unfounded rather than that this runtime needs no check`。
**语义要点**:「缺席不失败、存在但不可读才失败」—— 某个运行时本来就没出货是合法的,控制组实测把 `versionlib-1-7-99-0.bin` 整个移走后仍 exit 0 并报 `all 13 AE runtime(s)`。

### 157.3 判为「诚实汇报、不改」的 warn 站点

同一轮里逐条读过全部 11 个 `warnings.append` 站点,只有上面 5 处(含 §156 里的几处)是缺陷,其余是:

- `Tools/Scripts/check_branch_layouts.py:236,238` —— 纯信息(逐文件 conditional 计数 + `guards logic, not a layout`),RULE 1/RULE 2 自带 `pairs != 0` / `layouts != 0` 下限;
- `Tools/Scripts/check_map_coverage.py:286` —— **明确声明不覆盖**通用 map,是诚实的范围声明;
- `Tools/Scripts/check_patch_offsets.py:236,240` —— 门禁自己的 docstring(`:21-26`)写明这个方向「不是缺陷」(1.5.x 上故意跳过某些补丁),只是值得报告;
- `Tools/Scripts/check_rtti_coverage.py:297` —— `Cast<T>` 在 `template <class T> GetByServerId` 里,id 由调用点决定,这里查不了;且 `:302 if compared == 0:` 已有全局下限兜底;
- `Tools/Scripts/check_library_scan.py:188` —— `UNREACHABLE` 表的过期条目卫生提醒,不是覆盖收窄;
- `Code/plugins/tools/check_contract_copies.py:293` —— 某副本声明了运行时没有的常量(反向漂移),`inline constexpr` 不是导出符号,改它会让插件侧合法新增变红。

**判据**:warn 是缺陷,当且仅当「它不存在时某条规则**静默地什么都不比**」。纯信息、能力边界、已文档化的非缺陷方向、范围声明,都不是。

### 157.4 验证

| 门禁 | 变异 | pre-fix | fixed | reverted |
|---|---|---|---|---|
| `check_exports.py` | 头文件里 `kQueryInterfaceExportName` 改名 | 0 | 1 | 0 |
| `check_skee_abi.py` | **两份**拷贝里同时改 `class IOverlayInterface` | 0 | 1 | 0 |
| `check_contract_copies.py` | 删 `contract.json` 的 `headerSha256` | 0 | 1 | 0 |
| `check_library_scan.py` | 让 `Code/skse_client/xmake.lua` 在镜像里消失 | 0 | 1 | 0 |
| `check_target_meta.py` | 把 `versionlib-1-7-99-0.bin` 截成 16 个零字节 | 0 | 1 | 0 |

残余洞(如实记下,未修):`check_exports.py` 仍不报告「导出了但没有任何常量点名」的符号;`check_skee_abi.py` 的 `EXPECTED` 是手工维护表,插件合法新增接口声明会先红;`check_contract_copies.py:161-164` 在 manifest 缺失时 `failures.append` 后 **`return 0`** 提前返回,跳过了后续所有检查(不是空过,但同形状)。

## 158. 2026-10-13 场次:工作树 `CHANGELOG.md` 被截断损坏 —— 44 个门禁里只有 1 个会读它,而它是硬崩

**锚点**:
- `Tools/Scripts/check_docs.py:61,99,231,338`(读 `CHANGELOG.md` 的唯一门禁)
- `Tools/Scripts/check_docs.py:131-136,139-170,364-372,388-395`(新增的文本完整性检查)
- `Tools/Scripts/check_crash_diagnostics.py:122`(读 `docs/PITFALLS.md`,同样是严格读取)
- `docs/FOLLOW-UP.md`(文档分工的定义:CHANGELOG = 修了什么 / PITFALLS = 怎么定位、证据在哪 / FOLLOW-UP = 还有什么没验)

### 158.1 现象与定量

工作树 `CHANGELOG.md`:**621282 字节 / 6574 行 / 3620 行非法 UTF-8**。
- 首个坏字节在偏移 **40**(第 3 行):`\xe4\xbb\xa5`(`以`)被截成 `\xe4\xbb?` —— 多字节序列被逐处截断,`?` 是 `0x3F`。
- 3620 个坏行的错误全是同一个 reason:`invalid continuation byte`;`3620/3620` 都含一个字面 `?`;文件里 `?` 字节总数 **12369**(正常副本 40)。
- 15 处「引导字节后紧跟 `?`」;正常副本 **0** 处。
- 比正常副本**少 2435 行**,含**整个 `## v1.1.6(2026-09-27)` 节** —— 一级标题从 11 个掉到 10 个。
- 行级 `difflib`:`ratio=0.3792`,最大的相等块只有 35 行。

**已证伪的假设**:不是 GBK/cp936/mbcs 往返(三种编码的往返重编码都 `equals disk = False` 且 `same length = False`);不是「只损坏新加的行」(坏行从 `:3` 起,包括 HEAD 已有的旧内容);不是 CRLF/NUL 问题;盘上没有第二份 `CHANGELOG`(无 `.bak`/`.orig`),`git stash list` 为空。

**范围是全仓库唯一的**:扫描 **2332 个受控文本文件**,只有 `CHANGELOG.md` 一个非法;`docs/PITFALLS.md`(1235910 字节 / 20549 行)、`docs/FOLLOW-UP.md`、`README.md` 全部 0 坏行。

### 158.2 恢复:镜像就是损坏前那一版

`.mut-mirror-full/CHANGELOG.md`(**633619 字节 / 9009 行 / UTF-8 合法 / sha256 `9EBB7A59CC478984`)是损坏前的副本,四条独立证据:

1. **对 HEAD 是纯超集**:镜像 vs HEAD = `+3479 -0`;损坏副本 vs HEAD = `+4707 -3662`。
2. **会话中记录的 10 个行号锚点逐一吻合**:`:1` `# 更新日志`、`:10` `## v1.1.9(2026-10-01)`、`:8015` `## v1.1.8(2026-09-28)`、`:8175` `## v1.1.7`、`:8271` `## v1.1.6`、`:8499` `## v1.1.5`、`:8743` `## v1.1.4`、`:8844` `## v1.1.3`、`:8872` `## v1.1.2`、`:8963` `## v1.1.1`。
3. **它带着损坏副本丢掉的那一整节**,且镜像独有的 136 个标题全部是**完好形态**(损坏副本对应的是同一标题被截断的形态)。
4. **损坏副本没有独有内容**:工作树里「可抢救的非空行」共 2047 行,其中不在镜像里的 **0 行**;ASCII 标识符 run 比对里 disk-only 只有 14 个 token,全部是截断产物。

**恢复动作**:先把损坏版另存为 `.changelog-damaged.bak`(作为证据保留),再 `shutil.copyfile` 从镜像逐字节拷回。恢复后实测 633619 字节、sha256 与源相同、UTF-8 合法、CRLF=0;`check_docs.py` 回到退出 0;全量门禁从 `43 passed, 1 failed` 回到 **44/44**。

**mtime 铁证**:损坏时刻 `2026-10-12 23:53:37`,镜像建立时刻 `21:08:10`,子代理首次动门禁 `2026-10-13 00:01:13` —— **损坏比任何一次代码改动早 8 分钟**。

### 158.3 为什么没有任何门禁报出它

**两半,都不在退出码里:**

- `Tools/Scripts/check_docs.py` 是 44 个门禁里**唯一**读 `CHANGELOG.md` 的。它通过 `read_text(encoding='utf-8')` 读,损坏时抛的是**未捕获的 `UnicodeDecodeError` 回溯**:
  ```
  File "Tools/Scripts/check_docs.py", line 99, in changelog_versions
  File "Tools/Scripts/check_docs.py", line 61, in read
  UnicodeDecodeError: 'utf-8' codec can't decode bytes in position 40-41: invalid continuation byte
  ```
  它既不**指名文件**,也不说明**后果** —— 退出码 1 是真的,但读它的人只能自己从 traceback 里推断。
- 其余门禁读文档时普遍用 `errors='ignore'` 或 `errors='replace'`(约 40 处,如 `check_layout_asserts.py:78`、`check_map_coverage.py:131`、`Code/plugins/tools/check_skee_abi.py:157`、`Tools/Scripts/gen_ae_to_se_map.py:251`),所以一份损坏的文档会以「短了一截、安静了一截」的形态抵达规则,**规则照常打印 OK**。注意 `check_docs.py` 自己打印的 coverage 数字就是经由其中一个算出来的。

**这一条是本轮最值得记住的**:门禁的读取方式决定了它能不能看见损坏。`errors='ignore'` 让门禁对损坏**免疫也失明**。

### 158.4 修法:`check_docs.py` 新增文本完整性检查(355 → 412 行)

- `TEXT_SUFFIXES`(`:131-136`)34 个后缀:`.as .bat .cfg .cmake .cpp .css .csv .def .h .hpp .html .ini .inl .js .json .lua .map .md .patch .ps1 .psc .py .rc .scss .sh .svg .toml .ts .tsv .txt .v .xml .yaml .yml`
- `check_text_integrity(failures)`(`:139-170`):语料是 `git ls-files`;逐条后缀匹配后 `read_bytes().decode('utf-8')`,`UnicodeDecodeError` ⇒
  ```
  {rel}: byte {e.start} is not valid UTF-8 ({e.reason}); every gate that reads it with errors='ignore'
  or errors='replace' is comparing a degraded copy and can report OK while doing so
  ```
  `git ls-files` 本身失败(退出码非 0)也记失败:`refusing to pass vacuously`。**受控但已从工作树删除的路径跳过**(`if not path.is_file(): continue`)—— 脏检出不是文本问题。
- 注册为 `CHECKS` 字典的**第一项**(`:364-372`),`main()` 在它失败后 **`break`**(`:388-395`)。这是对「只加这条规则」的**唯一刻意偏离**,理由:后续规则都通过 `read()` 读同一批字节,继续跑只会用 `UnicodeDecodeError` 回溯**换掉**那行指名失败。失败行在循环之后才打印,没有 `break` 就永远到不了 stdout。
- `--list` 新增一行 `text-integrity  Every tracked text file must be readable UTF-8, not decodable with holes.`

### 158.5 验证

**真树语料**:`git ls-files` → 2939 条目 → 2739 匹配 34 个后缀 → **2738 存在且解码通过**。唯一跳过的是 `Code/client/Games/Skyrim/AnimationExperiments.cpp`(受控但已从工作树删除,`git status` = ` D`);另 12 个无后缀条目是 submodule gitlink。

**四组测量**(镜像路线,`GIT_DIR` 指向真树 `.git` + `GIT_WORK_TREE` 指向镜像):

| 组 | 变异 | 门禁 | 结果 |
|---|---|---|---|
| 1 | 无 | 改后 | 退出 0;输出与改动前**逐字节相同**(真树也验过 `stdout == the pre-edit 4 lines exactly: True`) |
| 2 | `docs/PITFALLS.md` 偏移 465 `e4 bb a5 → e4 bb 3f` | 改后 | 退出 1 + 指名失败行 |
| 3a | 同上 | **pre-fix** | **退出 0(盲)** —— PITFALLS 是 `check_docs.py` 一条规则都不读的文件 |
| 3b | `CHANGELOG.md` 偏移 40 同一替换 | **pre-fix** | 退出 1,**但只是未捕获的 `UnicodeDecodeError` 回溯** |
| 3c | 同上 | 改后 | 退出 1 + 可读的指名失败行 |
| 4 | 还原 | 改后 | 退出 0 |
| extra | 无 git 仓库 | 改后 | 退出 1(`git ls-files failed (exit 128)`) |

**3a 与 3b 是两件事**:PITFALLS 那条旧门禁是**盲**(退出 0),CHANGELOG 那条旧门禁是**崩**(退出 1 但无信息)。交付的是 ①可读诊断(点名文件 + 字节偏移 + 后果)②覆盖到此前**没有任何门禁严格读取**的文本文件 —— 而不是「关掉了一个退出码洞」。

**未覆盖(如实记下)**:43 个**无后缀**的受控文本文件(`.gitignore`、`.gitattributes`、`.editorconfig`、`.clang-format`、`.gitmodules`、`.dockerignore`、`.releaserc`、`LICENSE`、`VERSION`、`Dockerfile`、`Doxyfile`、`snapshots/plugins/*/LICENSE` …)的 `Path(rel).suffix` 是 `''`,后缀规则够不到;要覆盖需要按文件名的白名单。另有约 40 个 `errors='ignore'`/`errors='replace'` 读取器按范围未动 —— 新规则是第一个会注意到那些字节坏掉的东西。

### 158.6 教训

- **控制台乱码既可能是终端假象也可能是真损坏。** 判据不是肉眼看 `pwsh` 的输出(它按 GBK 解码,中文全是乱码),而是 `grep` 的 `(line is not valid UTF-8)` 与 `read_bytes()` 逐行 `decode`。
- **`errors='ignore'` 让门禁对损坏免疫也失明。** 一份被截断的文档会以「短了一截」的形态通过所有规则。
- **损坏的检测与损坏的诊断是两件事。** `UnicodeDecodeError` 的 traceback 有退出码,但没有信息;要把「哪个文件、哪个字节、后果是什么」写进失败行,必须显式 `read_bytes().decode()` 并捕获。
- **`git ls-files` 是这份检查的语料,而它本身也会失败。** 语料枚举失败必须记失败,否则「枚举不到任何文件」与「所有文件都合法」在输出上一样。
- **镜像树在关键时刻救了这一轮。** 它是唯一完好的副本,而它**未被 git 跟踪** —— 也就是说它不会在 `git clean` 或全新 checkout 后存活。恢复必须显式从它拷回,而不是指望 `git checkout --`。

---

## 159. 2026-10-14 场次:「权威信号」的扫描停在了前缀上,而前缀认不出运行时

**锚点**:
- `Code/skse_client/main.cpp:87-110`(`GetSKSEModuleVersion`,本轮修)
- `Code/skse_bootstrap/main.cpp:147-167`(`IsLegacyGame`,**没有**这个缺陷,是判据的另一半)
- `Tools/Scripts/check_skse_module_scan.py`(新门禁)
- 本机证据:`D:\game\SkyrimSE\skse64_steam_loader.log`、`D:\game\SkyrimSE\st_boot.log`

### 159.1 判据:同一件事写了两遍,把两遍抽出来对照

§115 定下「同一个东西定义在两个地方,抽出来对照」;§116/§119 补上「**扫哪里要按规则推出,
不能按发现地点写**」;§120 把它用在「谁复制了契约」上。这一轮问的是同一族里更基础的一问:

> **「同一个模块列表,谁还在扫?」**

答案是两个翻译单元,而且它们回答的是**同一个问题**:

| 文件 | 函数 | 它决定什么 |
| --- | --- | --- |
| `Code/skse_client/main.cpp` | `GetSKSEModuleVersion()` | 用哪个地址库 / 哪个运行时 |
| `Code/skse_bootstrap/main.cpp` | `IsLegacyGame()` | 加载哪个 per-version 运行时 DLL |

两个都扫 `skse64_` 前缀,都用 `swscanf_s(name.c_str() + 7, L"%d_%d_%d", …)` 取版本。
**写法却不一样**,而差别正好落在「什么时候停」:

```cpp
// Code/skse_client/main.cpp(改前)
for (…)
{
    const std::wstring name = me.szModule;
    if (name.rfind(L"skse64_", 0) != 0)
        continue;
    int a = 0, b = 0, c = 0;
    if (swscanf_s(name.c_str() + 7, L"%d_%d_%d", &a, &b, &c) >= 3)
    {
        char buf[64];
        sprintf_s(buf, "%d.%d.%d.0", a, b, c);
        result = buf;
    }
    break;          // <-- 无条件,在解析之外
}
```

```cpp
// Code/skse_bootstrap/main.cpp(一直如此)
for (…)
{
    const std::wstring name = me.szModule;
    if (name.rfind(L"skse64_", 0) != 0)
        continue;
    int major = 0, minor = 0;
    if (swscanf_s(name.c_str() + 7, L"%d_%d", &major, &minor) >= 2)
    {
        CloseHandle(snap);
        return major == 1 && minor == 5;   // 停在解析成功之后
    }
}
```

**「停在解析成功处」和「停在前缀处」不是同一种停止**,而这正是判据的落点。

### 159.2 为什么前缀不足以认出运行时

SKSE 安装目录里,**`skse64_` 前缀的模块有两个**:

| 模块 | 在游戏进程里? | 名字里有版本? |
| --- | --- | --- |
| `skse64_1_5_97.dll`(运行时) | 是 | **是** |
| `skse64_steam_loader.dll` | **是** | **否** |

`skse64_steam_loader.dll` 先于运行时被映射:它给游戏的 IAT 打补丁、挂 `WinMain`,
再加载运行时。它自己的日志就是证据(本机,1.5.97):

```text
skse64_steam_loader.log
    loader base addr = 00007FFEF9F30000
    exe base addr    = 00007FF6DDFD0000
    found iat at 00007FF6DF4D98A8
    patched iat
    HookMain: thread = 35920 retaddr = 00007FF6DF31B159
    dll = D:\game\SkyrimSE\\skse64_1_5_97.dll
```

**模块枚举顺序不是我们能选的**(`Module32FirstW`/`Module32NextW` 的顺序由加载器决定)。
所以「运行时恰好排在前面」不是可以依赖的东西 —— 而 `break` 就依赖了它。

### 159.3 后果:权威信号静默降级成它本要取代的两级 fallback

`result` 保持空串时,`Code/skse_client/main.cpp` 的调用点会依次退到:

```text
① SKSE 模块名     <- 本轮修的这一步(权威:SKSE 与游戏版本锁定,降级版换 exe 不换它)
② exe 版本资源    <- 降级版/整合包上可能被剥离或伪造
③ 地址库文件名    <- check_library_scan.py 已在守(族过滤 + 取最高版本)
```

而 ② 恰恰是这段代码存在的理由:`Code/skse_bootstrap/main.cpp:168-170` 自己写着
**「cracked builds 上结构化的 `VS_FIXEDFILEINFO` 不可信(CODEX 把它盖成 `1.0.0.0`,
而 StringFileInfo 里仍是 `1.5.97.0`)」**。所以这不是「少了一级优化」,而是
**在降级版上把最可靠的那一级换成了最不可靠的两级**。

### 159.4 本机实测

把两个实现各自的循环跑在同一组模块名上:

```text
skse64_steam_loader.dll      解析 = None
skse64_1_5_97.dll            解析 = ('1','5','97')

改前循环        → resolved = ''            (运行时根本没被读到)
停在解析成功处  → resolved = '1.5.97.0'
改前循环,若运行时恰好枚举在前 → resolved = '1.5.97.0'   (所以它是顺序相关的,不是恒坏)
```

第三行是这条缺陷难被发现的原因:**本机顺序恰好让运行时在前**,所以
`st_boot.log` 里一直写着正确的一行:

```text
[bootstrap] game version is 1.5.x, loading D:\game\SkyrimSE\SkyrimTogetherRuntime_1_5.dll
boot: entered, exe version='1.5.97.0', resolved='1.5.97.0'
```

`resolved='1.5.97.0'` 与 `exe version='1.5.97.0'` **相同**,所以从日志上分不出
它来自哪一级 —— 只有把扫描单独跑一遍才看得出来。

### 159.5 修法

把 `break` 移进解析成功的分支,使两个调用方写法一致:

```cpp
if (swscanf_s(name.c_str() + 7, L"%d_%d_%d", &a, &b, &c) >= 3)
{
    char buf[64];
    sprintf_s(buf, "%d.%d.%d.0", a, b, c);
    result = buf;
    break;
}
```

**范围(如实记录)**:两个模块名都以 `skse64_` 开头这一点没有变,所以扫描仍然会先看到 loader;
变的是**它不再在 loader 上停下来**。若将来出现第三个 `skse64_` 前缀且名字可解析的模块,
`break` 会停在第一个**可解析**的模块上 —— 这与 `IsLegacyGame()` 的语义相同,
也正是「停在解析成功处」这条规则的边界。

### 159.6 门禁与变异

`Tools/Scripts/check_skse_module_scan.py`,四条静态断言,对**两份实现**各跑一遍:

| # | 断言 | 它防的是 |
| --- | --- | --- |
| 1 | 仍匹配 `skse64_` 前缀 | 扫描换了识别方式 |
| 2 | 仍把版本从模块名解析出来 | 权威信号被删掉 |
| 3 | **停在解析成功处**;前缀之后、解析之前的裸 `break` 与解析之后的裸 `break` **都算失败** | 本轮的缺陷本身,以及它的两种重新引入方式 |
| 4 | 循环真的在遍历 | 规则失去对象 |

另有一条**警告**:`skse64_steam_loader.dll` 不再被 `main.cpp` 点名时提示 ——
这条规则的理由已经不在了,应该**显式删规则**而不是让它继续空转。

**变异实测(5/5 全部被抓,`exit 1` 且指名失败)**:

| # | 变异 | 结果 |
| --- | --- | --- |
| 1 | 恢复无条件 `break`(原缺陷) | 抓 ✓ |
| 2 | 把 `break` 提到解析之前 | 抓 ✓(第 3 条的 3a 分支) |
| 3 | 删掉版本解析 | 抓 ✓ |
| 4 | 删掉前缀匹配 | 抓 ✓ |
| 5 | 重命名函数,让门禁找不到 | 抓 ✓ |

**第 2 条是第一版门禁漏掉的**:第一版只查「解析之后有没有裸 `break`」,
把 `break` 提到解析之前就能通过。变异测试把它暴露出来,补上 3a 分支后 5/5 全抓 ——
**门禁自己也要过变异测试**,否则它只是把「我以为我查了」写成了代码。

---

## 160. 2026-10-14 场次:四个插件的加载路径是 `noexcept` 且会分配 —— 而只有一份被修过

**锚点**:
- `plugins/STRPluginMessagingAPI/src/STRPluginMessagingAPI.cpp:17-34`(已修的那一份)
- `plugins/AnimSyncTogether/src/STRPMApi.cpp:14-30`、`plugins/DAVSyncTogether/src/STRPMApi.cpp:14-30`、
  `plugins/TradeTogether/src/STRPluginMessagingAPI.cpp:10-25`(**未被任何一轮覆盖**)
- `plugins/OStimNG/skse/src/ModAPI/OstimNG-API-Thread.cpp`(8 个 `noexcept` 入口,函数体会分配)
- `Code/plugins/patches/STRPluginMessagingAPI/0005-…-the-load-path.patch`(只修了它自己那一份)
- `Code/plugins/patches/OStimNG/0003-keep-an-allocation-failure-out-of-the-scene-api-entries.patch`(兄弟文件已修)
- `Code/plugins/tools/check_noexcept_allocations.py`(新门禁)

### 160.1 判据:同一类缺陷的**兄弟副本**

仓库对「`noexcept` 帧里分配」已有四轮普查,判据本身在 §「分配失败逃出 `noexcept` ⇒
`std::terminate` ⇒ 无崩溃转储、无日志」上,产物是
`Code/plugins/patches/STRPluginMessagingAPI/0004..0007`。这一轮的判据是**范围**:

> **上一轮修的是一份副本,还是这个缺陷的**全部**副本?**

`LoadPluginModule` 在插件树里有**四份**,分布在**四个独立仓库**:

```text
plugins/STRPluginMessagingAPI/src/STRPluginMessagingAPI.cpp   HMODULE LoadPluginModule(const wchar_t* moduleName) noexcept
plugins/AnimSyncTogether/src/STRPMApi.cpp                     HMODULE LoadPluginModule(const wchar_t* moduleName) noexcept
plugins/DAVSyncTogether/src/STRPMApi.cpp                      HMODULE LoadPluginModule(const wchar_t* moduleName) noexcept
plugins/TradeTogether/src/STRPluginMessagingAPI.cpp           HMODULE LoadPluginModule(const wchar_t* a_moduleName) noexcept
```

`STRPluginMessagingAPI/0005` 的标题里就写着 **"…and the load path"** ——
它修的是**它自己那一份**。另外三份在各自独立的仓库里,而**没有任何东西同时看这四份**
(§113/§116/§119 是同一错误的第二、三、四次;这是第五次)。

### 160.2 两个形状,同一个后果

**(a) 插件加载路径**。四份都在 `noexcept` 里构造 `std::wstring` 并 `+=` 模块名:

```cpp
HMODULE LoadPluginModule(const wchar_t* moduleName) noexcept
{
    if (!moduleName) return nullptr;

    auto module = GetModuleHandleW(moduleName);
    if (!module) module = LoadLibraryW(moduleName);
    if (!module) {
        std::wstring pluginPath = L"Data\\SKSE\\Plugins\\";   // 分配
        pluginPath += moduleName;                             // 分配
        module = LoadLibraryW(pluginPath.c_str());
    }
    return module;
}
```

**(b) OStimNG 的 Thread API**。`OstimNG-API-Thread.cpp` 里 8 个入口在
`OstimNG-API-Thread.h` 中声明为 `noexcept`,而 **OStimTogether 逐字复制了那份 vtable**
(`ModThreadControl.h:108-193`),所以承诺**不能单方面撤回**:

| 入口 | 分配的容器 |
| --- | --- |
| `GetNavigationCount` | `std::vector<Trait::ActorCondition>` |
| `GetNavigationOptions` | `std::vector` + `m_navDescCache.reserve/push_back` |
| `NavigateToScene` | `std::string(sceneID)` 进 lambda 捕获 |
| `RegisterEventCallback` | `eventCallbacks.push_back` |
| `RegisterControlCallback` | `controlCallbacks.push_back` |
| `SearchScenes` | `std::string` + `std::vector<Graph::Node*>` |
| `HasCompatibleNode` | 三个 `std::vector`(含 `ActorUtil::sort` 内部的 `dominants`) |
| `MigrateThread` | `std::vector` + `std::make_shared<std::promise>` |

**同一个仓库的兄弟文件已经因为同一个原因被修过**:`OstimNG-API-Main.cpp` 就是
`Code/plugins/patches/OStimNG/0003` 的产物,它的注释里写着「an exception escaping a noexcept
frame terminates the process with no crash dump」。**这一份没有跟着写** —— §109 的判据
(「同一个抽象层的兄弟函数已经判过的空,新加的入口要判同样的空」)在**同一个仓库的同一个目录**里
第二次不成立。

### 160.3 后果与可达性

分配失败逃出 `noexcept` 帧 ⇒ `std::terminate` ⇒ 游戏进程直接结束,**没有崩溃转储**,
所以玩家报的会是「游戏没了」而不是一条可诊断的记录 —— 这正是本仓库反复修这一类的原因。

可达性:四份 `LoadPluginModule` 都由 `LoadFromModule()` 调用,而它由每个插件自己的
初始化路径调用(`STRPMClient.cpp:78`、`DAVNetworkService.cpp:50`、`StrTransport.cpp:91` …);
OStimNG 那 8 个入口由 OStimTogether 通过 `RequestAPI` 拿到的 vtable 调用
(`CoopSessionManager.cpp:151`、`OStimBridge.cpp:1164`、`ParticipantAlignmentSync.cpp:79`、
`PPAIntegration.cpp:305`)。**没有一条需要管理员或特殊配置**。

### 160.4 修法(必须落成 patch)

这些文件在**钉住的 submodule** 里,而 CI 每次都跑
`git submodule update --init --force --recursive --depth=1`,**工作树里的改动会被丢掉**
(`Code/plugins/tools/plugin_patches.py` 的模块文档写明这一点)。所以四处修复都写成
`Code/plugins/patches/` 下的普通内容:

| 文件 | 覆盖 |
| --- | --- |
| `STRPluginMessagingAPI/0009-keep-an-allocation-failure-out-of-the-plugin-load-path.patch` | facade 那一份 |
| `AnimSyncTogether/0003-…` | AnimSyncTogether 那一份 |
| `DAVSyncTogether/0004-…` | DAVSyncTogether 那一份 |
| `TradeTogether/0003-…` | TradeTogether 那一份 |
| `OStimNG/0007-…`(**扩充原有文件**) | `OstimNG-API-Thread.cpp` 的 8 处 catch |
| `OStimNG/0008-…`(**0007 拆出的另一半**) | `Main.cpp` / `PapyrusThread.h` / `MainThread.h` |

`OStimNG/0007` 被 `docs/PITFALLS.md` §109 的锚点**按文件名引用**,所以没有新建编号去取代它 ——
那会让锚点指向一个不再包含它所描述内容的文件。做法是把新内容追加进同一份,
并保持它对钉住提交的 `git apply --check --cached` 仍然通过。

**为什么拆成两份**:补丁的**上下文行逐字节匹配**,而钉住的 blob **逐文件行尾不同** ——
`OstimNG-API-Thread.cpp` 是 **CRLF**,`Main.cpp` / `PapyrusThread.h` / `MainThread.h` 是 **LF**。
一份补丁文件**不可能同时**满足两边,所以按目标文件拆开:0007 只含线程文件(整份 CRLF),
0008 含其余三个(整份 LF)。两半各自通过 `git apply --check --cached`,合起来是原先那一组改动。
这条坑在 §160.7 有完整记录。

修法的语义都落在**代码已有的失败分支**上,不新增行为:

- `LoadPluginModule` 失败时**落到「没加载」** —— 这正是模块不在 `Data\SKSE\Plugins` 时已有的行为,
  每个调用方都把空模块读作「消息 API 不可用」;
- OStimNG 那 8 个入口返回各自的空答案(`0`/`false`/`APIResult::Invalid`),
  `MigrateThread` 通过 **`onComplete(-1, context)`** 上报 —— 否则调用方会等一个永远不会到的回调。

### 160.5 门禁:按性质查,不按名单查

`Code/plugins/tools/check_noexcept_allocations.py` 四条规则:

| # | 规则 | 它防的是 |
| --- | --- | --- |
| 1 | `plugins/` 下**每一个**「`noexcept` 且函数体真的会分配」的函数都必须含 `catch` | 本轮的 12 处,**以及语料之外的第三、第四处**(见 160.5.1) |
| 2 | **每一份**会分配的 `LoadPluginModule` 都必须含 `catch`(**按名字更严**) | 本轮的四份,以及**将来新增的第五份** |
| 3 | 提到该助手却在其中找不到定义的插件**报失败** | 重命名/搬家让门禁**悄悄漏掉一个插件** |
| 4 | 本文件**为之而写**的 12 个站点必须**真的被看见**(语料下限) | 「没发现不安全的」与「什么也没看见」输出相同(§156) |

第 3 条是变异测试逼出来的:第一版只查「找到的定义有没有 `catch`」,
于是**把助手改名**就能让那个插件从对照里消失而门禁仍然 `exit 0`。

### 160.5.1 门禁的第二版:语料必须按规则推出,不能按发现地点写

第一版门禁只有两个语料 —— 「每一份 `LoadPluginModule`」与「`OstimNG-API-Thread.cpp`
的每一个 `noexcept override`」。**两个语料都是「我在哪里找到缺陷」写出来的**,
于是它**看不见这两处以外的任何同类站点**。实测数了一下:

```text
noexcept 且函数体会分配:  14 处在语料内,18 处在语料外(对第一版完全不可见)
```

这正是 §113/§116/§119 记过的**同一错误第五次**(「门禁的扫哪里要按规则推出,
不能按发现地点写」)。修法是把规则 1 的语料换成**整棵插件树**,并加规则 4 把
「为之而写的站点必须被看见」变成断言 —— 于是第二版查到 **29 处**并全部通过。

**规则 4 立刻抓到了两个站点**(`GetNavigationCount`、`NavigateToScene`):
它们**在**语料里,但**分配标记匹配不到**,因为第一版标记只认
`std::vector<T> name =` 之外的写法,漏了带模板实参的声明与 `std::string(x)` 构造。
换句话说:**没有规则 4,第二版会一边少看两个站点一边报 OK**。
补上两种写法后 12/12 全部可见。

**JUDGED 表**:第一版普查把 `STRPMChatUiSuppressV3.h:FindNearestCallAfterXref` 报成缺陷,
逐条读下来是**假阳性** —— `std::vector<std::uint8_t> snapshot;` 是**默认构造**(不分配),
唯一会分配的一步委托给 `Snapshot()`,而它**自己含 `resize` 的 catch**,调用方
(`STRPMChatUiSuppressBootstrap.h:71-101`)又整个包在 `try/catch` 里。
第二版把标记收紧到「真的会分配」之后它**不再被匹配**,于是那张豁免表**留空** ——
留空而不是删表,是因为「用更窄的正则把它排除掉」正是让真实站点漏看的做法;
**豁免是看得见的,窄正则是看不见的**。

### 160.6 验证

```text
python Code/plugins/tools/plugin_patches.py verify
    PATCH VERIFY OK
    44 patch(es) apply to their pinned commits        (改前 39)

python Code/plugins/tools/check_noexcept_allocations.py
    NOEXCEPT ALLOCATION CHECK OK
    29 allocating noexcept frame(s) across the plugin tree contain their own failure
    (4 of them the load-path helper), and all 12 site(s) this gate exists for are visible to it

全量门禁:ran 46 gate(s); 46 passed, 0 failed          (改前 44)
```

**变异实测(第一版 5/5 全抓,`exit 1` 且指名失败)**:去掉 AnimSyncTogether 的守卫 ✓;
去掉 facade 的守卫 ✓;去掉 `GetNavigationCount` 的 catch ✓;
去掉 `RegisterEventCallback` 的 catch ✓;重命名助手 ✓。

**变异实测(第二版,按新规则分组)**:

| 规则 | 变异 | 结果 |
| --- | --- | --- |
| 2 | 去掉 AnimSyncTogether 的守卫 | 抓 ✓ |
| 2 | 去掉 facade 的守卫 | 抓 ✓ |
| 3 | 重命名助手,使其定义找不到 | 抓 ✓ |
| 1 | 去掉 `GetNavigationCount` 的 catch | 抓 ✓ |
| 1 | **去掉第三语料外一个站点的 catch**(`STRPMChatUiSuppress.h:Snapshot`) | 抓 ✓ |
| 4 | 把 `std::vector<...> conditions = ...` 改成 `auto`,使已知站点不可见 | 抓 ✓ |

**第 6 条(规则 4)与第 5 条(规则 1 在第三语料上)是这一轮补测的**:
它们验的是「门禁真的比第一版看得更多」,而不只是「门禁还认识原来那 12 个名字」。

### 160.7 工具链上的一个坑(本轮踩到,记下来)

把新内容并入 `OStimNG/0007` 时,`git apply --check --cached` **反复失败**,而
「新写的补丁与 `git diff` 生成的补丁**逐行相同**」。根因是**行尾**:

- 本仓库 `.gitattributes` 是 `* text=auto` 且 `core.autocrlf=true`,所以**签出的补丁是 CRLF**;
- 而 submodule 里钉住的 blob **逐文件不同**:`OstimNG-API-Thread.cpp` 是 **CRLF**,
  `Main.cpp` / `PapyrusThread.h` / `MainThread.h` 是 **LF**;
- `git apply` 的上下文行是**逐字节**匹配的 ⇒ **一份补丁文件不可能同时满足两边**。

第一处误导来自 `git show HEAD:<patch>`:它会**套用 smudge 过滤器**,把 CRLF 变成 LF,
于是「与原始补丁逐字节相同」的比对结论是**假的**(`git cat-file blob` 才给真字节)。

**最终做法是按目标文件拆成两份**,每份内部行尾统一:

```text
OStimNG/0007-…   只含 OstimNG-API-Thread.cpp   整份 CRLF   581 个 CRLF / 0 个裸 LF
OStimNG/0008-…   含 Main.cpp / PapyrusThread.h / MainThread.h   整份 LF   0 个 CRLF / 70 个裸 LF
```

两半各自 `git apply --check --cached` 通过(实测 `thread.patch YES` / `rest.patch YES`),
`plugin_patches.py verify` 报 **44 份**全部通过 —— 拆开的是**文件**,不是改动。

**教训**:在 `core.autocrlf=true` 的树上,**用 `git show` 比对文本补丁会得到错误结论**;
比对补丁内容要用 `git cat-file blob`。而且当目标文件的行尾**不一致**时,
「一份补丁覆盖多个文件」这件事本身就不成立 —— 应当**按行尾分组拆开**。

---

## 161. 2026-10-18 场次：框架本体四模块审计（线程 / Hook / 协议 / Papyrus）

**范围**：`Code/` 框架本体（1058 个源文件）。**不含** `Code/plugins/patches/` 下已有 53 个 patch 的插件层。
**方法**：四路并行只读审计，边界互不重叠；每条结论都由协调者对照源码**独立复核**后才计入。
**本地零编译**，全部静态证据；`Tools/Scripts/check_*.py` 35 个门禁全 PASS。

### 161.1 线程安全：同一个 UAF 的两半

**缺陷**：`World.h` 的成员注释与声明顺序**互相矛盾**。
注释写「Transport before runner … makes the runner (and the queue it owns) outlive the transport」，
但 `m_transport`(:58) 先声明、`m_runner`(:59) 后声明——C++ **逆序析构** ⇒ `m_runner` **先死**，
注释描述的正是它声称已避免的那个 bug。

**另一半**：`TransportService.cpp:730` 的循环内投递
`m_world.GetRunner().Queue([this]() { FlushPendingMessages(); });`
**没有** `!m_handshakeThreadStop` 前置条件，而循环**之后**的同款投递(:748)**有**。
`:745-747` 的注释恰好把这个 hazard 写得很清楚：「a thread that was told to stop ... must not put
another callback holding a pointer to this into a queue that can outlive the join」。

**触发**：`StopHandshakePump()` 先置 `m_handshakeThreadStop` 再 `join()`。泵线程在 `:738` 的
`sleep_for` 之后可能**已经越过** `:688` 的 while 条件检查，仍执行到 `:730` 投递一个捕获 `this` 的 lambda；
对象随后析构 ⇒ 悬空回调。叠加上面的析构顺序问题，队列本身也可能已经不在了。

**修复**：`World.h` 交换为 `m_runner` 在前、`m_transport` 在后（`World.cpp:35-36` 初始化列表同步，
否则触发 `-Wreorder`，而 `xmake.lua` 开了 `set_warnings("all")`）；`:730` 补上 `!m_handshakeThreadStop.load()`。

**教训**：这两处必须**一起**修。只改析构顺序，泵仍会在 join 期间往队列里塞东西；只改守卫，
队列本身仍会先于 join 被拆掉。**注释写了「为什么」不等于代码做到了**——这条注释就是反例。

### 161.2 Hook 安全：noexcept 标注比实际保证强

**缺陷一（P1）**：`Hook_WndProc`（`BSGraphicsRenderer.cpp:71`）无 `try/catch`，直接转调
`InputService::WndProc`（`InputService.cpp:506-749`，**零 catch**）。这是引擎的窗口过程回调，
栈上没有 C++ 调用者能接住异常 ⇒ 异常到达 Win32 ⇒ `std::terminate`。
修复：整体包 `try/catch(...)`，异常路径返回 **`DefWindowProc`**——**不是 `0`**，
返回 0 会吞掉窗口消息、让窗口假死，那比崩溃更难诊断。

**缺陷二（P2）**：`Hook_Renderer_Init`(:79-92) 同样无保护，且 `:92` 直接
`ctx().at<RenderSystemD3D11>()`。`World::Create` 在构建失败时**吞掉**异常（`World.cpp:140-143`），
于是 registry 在、渲染系统不在，这个 lookup 会抛。
修复：**只包这一处 lookup**，失败置 `g_sRs=nullptr` + `Diagnostics::ReportFailure`（noexcept、不分配），
其后加 `if (!g_sRs) return;`。**`Renderer_Init` 引擎调用不要包**——那是正常路径，包了会掩盖真实崩溃。

**缺陷三（P1）**：`GamePatch::Jump`(:408)/`PutCall`(:445) 硬编码写 **5 字节**，
而 `ValidatePatchSite`(:366) 只 `memcpy` 了 **2 字节** `head[2]`、只判断首字节是否 `E9`/`FF 25`，
**从不检查这 5 字节是否落在指令边界上**。首条指令短于 5 字节时写入会把它拦腰截断，
跳回点落进指令中间。带 `acExpected` 的严格重载(:415)**无任何调用者**。

修复采取**诚实的最小步**：新增 `InstructionLengthFn` / `InstructionLengthDecoder()` /
`SetInstructionLengthDecoder()` / `InstructionCovers()`。**仓库内没有可用的解码器**
（hde64/Zydis/Capstone 无源码；MinHook 只是 `add_packages("minhook")` 的二进制包；
`Libraries/TiltedReverse` 是空目录），因此**没有硬编码任何指令长度表**——手写表会在
legacy/REX 前缀、ModRM 扩展、VEX 这些**最要命**的形式上**静默**出错，比不检查更危险。
无解码器时只 `warn`、行为与修复前**完全一致**；注册后长度 < 5 则拒绝写入。
回调读取用 `__try/__except` 守卫（与 `ValidatePatchSite` 读 `head[2]` 同形，handler 内不含 C++ 对象）。

**已知取舍（勿改）**：`:384-395`「目标已有分支只 warn 仍照写」**不是缺陷**。注释写明理由：
走这里的补丁是窗口样式、菜单解冻、线程命名，在用户 load order 上一直能工作；
改硬失败是把**可见冲突**换成**静默缺功能**。

**审计覆盖面**：`HookAudit` 只记录 `TP_HOOK`，而 `TP_HOOK_IMMEDIATE`×5、`TP_HOOK_IAT`×7、
`GamePatch::*`×13 全在 `Recorded()` 之外。`main.cpp:149` 的「63 recorded, 0 did not land」
**不是全量健康信号**，别当绿灯读。

### 161.3 网络协议：读后夹取 vs 读前校验

**根因**：`WireLimits.h:23-27` **自己写明了**局限——
「the clamp runs after the string has been read, so it bounds what the value is used for,
not the transient allocation inside the reader」。
全仓**零处**「剩余可读字节」检查，而多个 `Deserialize` 在读到第一个元素**之前**就按对端给的计数
`resize`。最干净的一例：`StringCacheUpdate.cpp:30-31`，`count` 掩码到 16 位后直接
`Values.resize(count)`，**2 字节的包就能让客户端分配 65535 个 String**。

**修复**：新增 `kMaxElementCount = 1 << 16`，统一改为「读 count → 比较 → 超限 clear+return → resize」。
**选 reject 而非 clamp**：clamp 会让 reader 停在「它保留的那些条目」的末尾，而流里还剩着其余的，
于是**后面每一个字段都从错误的偏移读取**。`Mods.cpp`/`Factions.cpp` 原本就是 clamp，一并改 reject；
`Factions` 的两个 list 任一超限**两个都清**（第二个 list 在流里位于第一个之后）。
`Code/tests/encoding.cpp:1160-1177` 的断言由「被 clamp 到 `kMaxModCount`」改为「空列表」——
这是**行为契约变更**，不是改测试迁就代码。

**`AuthenticationRequest`（未认证可达）**：首个消息就能到达，解析 3 次 `ReadString` 加
`UserMods.Deserialize` 全是分配，函数标 `noexcept` 却无 `try/catch` ⇒ 分配失败即 terminate。
修复时**清空 `Version` 是关键**：`GameServer.cpp:886` 先比 `Version` 再看 `Token`(:919-922)，
所以解析失败会导向「版本不符 → kick」，而**不是**「无 mod + 正确密码 → 直接进服」。
这个失败方向是刻意选的。

**`StringCache`（服务端全局，无上限）**：一个已认证客户端可持续送入唯一字符串，
服务端 `Clear()` **零调用点**，直到 OOM。注意 `Add` 原有的 `try/catch` 回滚**是对的、保留不动**，
缺的是**容量上限**。新增 `kMaxCachedStrings = 1 << 18`，超限返回 `kInvalidId` 复用现有失败路径。
**不做 LRU**——`id` 必须保持发送方前缀，这是「按 id 指名字符串」的前提（`StringCache.cpp:28-32` 明说）。

**`NotifyFactionsChanges` 的 `>=`**：写侧 `std::min(Changes.size(), kMaxChangeCount)` 可**正好**写出 `0x400`，
读侧却用 `count >= kMaxChangeCount` 早退 ⇒ 每一个**满批**更新都被整包丢掉。改为 `>`。

**`Differential.h` 未修，理由记在这里**：`Serialize` 对 `Prepare()` 产出的空 `m_nextValue` 调
`GenerateDifferential` 是 UB，**且 `Deserialize` 里 `m_nextValue.ApplyDifferential(...)` 用的是 `.` 而非 `->`，
该模板一旦实例化就会编译失败**。它今天**零实例化点**，所以模板体从未被编译过。修一个没人用的模板会**触发**
那个既有的编译错误，故本轮**整体回退**，保持原状。真要启用它，`Serialize`/`Deserialize` 两处必须一起改。

**消息工厂不兜异常（已修）**：`ServerMessageFactory::Extract` / `ClientMessageFactory::Extract` 都是
`noexcept` 却**直接调用** `DeserializeRaw`。全仓 **16 个消息文件**的 `DeserializeRaw` 有 `ReadString`/`resize`/
`push_back` 却没有自己的 `try/catch`——因为帧本身是 `noexcept`，上层也没地方接。三个接收入口
（`TransportService::OnConsume`、`GameServer::OnConsume`、`AdminApp::OnConsume`）都只校验包大小，
**都不 catch**。包大小校验约束了单次分配量，但没有消除分配本身。
修复：在 `Extract` 里统一兜底，分配失败降级为「解析失败的包」而不是终止进程。
这比给 16 个文件各加一个 `try/catch` 更小，也不会漏掉将来新增的消息。

**分发表空槽（已修）**：`GameServer.cpp:617` 与 `admin/Transport.cpp:40` 直接调用 handler，
**无范围检查、无空槽检查、无 try/catch**；而客户端 `TransportService.cpp:288-301` 三项俱全——
**同一判据在不同模块应用不一致**。`Visit` 未命名的 opcode 在槽里留下空的 `std::function`，
调用即抛 `std::bad_function_call`。对端跑不同 build 是走到空槽的常规途径。已补齐并显式包含 `Opcodes.h`。

**仍开放**：逐字段越界读未防（`Buffer::Reader` 行为未知）。见 §10。

### 161.4 Papyrus：失败路径被设计成静默

**缺陷一（P2，永久锁死）**：`PAPYRUS_FUNCTION` 展开出的 `s_p##name` 是**函数内 static**，
`m_pFunction` 在构造时由 `Get()` 的返回值**锁死**，`operator()` 此后只读它。
若首次调用早于 `BindEverythingToScript`，该 native 在本进程内**永久失效**，
静默返回默认值（`IsDead→false`、`GetOpenState→kNone`、`GetCombatTarget→nullptr`），
`Capture` 之后注册进来**也没有回填路径**。
`PapyrusService.cpp:35-41` 的 `captured==0` 自检说明作者**预期过**「先查后注册」这个时序，
但结论无法被后来的注册修正。

修复：`m_pFunction` 改 `mutable` + 新增 `Resolve()` 重解析，命中即缓存；
失败按**调用次数**退避（每 64 次查一次表），既不让未注册的 native 把每次调用变成查表，
也仍能接住迟到注册。新增 `PapyrusService::Find`（`Get` 的安静孪生，首次未命中已由 `Get` 报过，
重复报会埋掉那行点名函数名的日志）。

**缺陷二（P2，VM 每帧放大）**：`ActorValueService.cpp:303` 的 `pActor->IsDead()` 走
`PAPYRUS_FUNCTION`（`Actor.cpp:1049-1054`），而 `IsDead` 本是**纯引擎状态读取**，
不必过 VM。循环体无 per-actor 闸门 ⇒ VM 调用 = `4×N_local` 次/秒，随本地 actor 数线性增长。
修复：轮转窗口，每次最多 64 个，游标是 **view 偏移**而非迭代器（actor 增删安全），扫到末尾回绕。
单次 VM 调用上界 64 与 actor 总数无关；延迟上界 `ceil(M/64)×250ms` 已写进注释。

**为什么不用「直接读内存」修 `IsDead`**：本仓库没有可静态验证的 `IsDead` 偏移，
硬编码偏移的风险**高于**收益（对比 `SkyrimVM::virtualMachine` 那个 §12.3 的真实教训）。
限制工作量是不需要偏移的唯一正确解。

**`GetStackFrameVariable` 未判空（已修）**：`BSScript.cpp:285`/`:321` 拿到 `pArg1` 后直接解引用，
判空判的是**结果** `pActor` 而不是 `pArg1`。改为 `pArg1 ? ... : nullptr`，失败语义与既有 null-actor 路径
完全一致（`Set<bool>(false)` + `return true`），**不改变对外行为**。全仓仅这两处调用点。

**`NativeFunction` 的 `parameters`（已修）**：基类 ctor 在引擎 ctor `104653` 未映射时直接 `return`，
`parameters` 保持未初始化，派生类随后 `parameters.data[0].pType = ...` **沿垃圾指针写入**。
对照同一结构体析构函数的注释「degrade to a leak」——那一处是 leak，这一处是写野指针。

这条本轮**拿到了证据**：`104653` 在 1.5.97 **有**真实映射（`st_overrides_all.tsv` → `0x1250c80`，
capstone `sim=1.00`），且该 ctor 会调 `ae105361(a1+48, a5, 0)` 初始化 `parameters`——所以引擎 ctor
不跑时它确实是空的。修复：ctor 内先显式置空，派生类写入前检查 `parameters.data && parameters.size > 0`。
顺带修 `LookUpActorParameterType` 在 `catch(...)` 之后**没有 return**（非 void 函数走到末尾是 UB），
以及 `GetVarNameForStackIndex` 用 `capacity` 而非 `size` 当边界。

**仍未修（故意）**：`Variable::GetObject()` 的 `type>kMax` 穿透——`Type` 是 `uint64_t` 且数组编码方式
没有静态依据，改动等于猜编码。见 §10。

**否定结论（证据链完整，可直接引用）**：**未发现跨线程 VM 调用**。
`TransportService.cpp:239` 只有 `thread id == m_gameThreadId` 才本地 `DispatchMessage`，否则 `:248-249` 只入队；
泵线程只把 `FlushPendingMessages` 投进 `Queue`（`:730-733`/`:748-751`）；
`RunnerService.cpp:58-62` 只在 `OnUpdate` 执行；`World.cpp:105` 是唯一驱动点，在游戏线程。
全树 3 个非游戏线程（`TransportService.cpp:642` pump、`DiscordService.cpp:206`、`BehaviorVar.cpp:769`）均不可达 VM。

### 161.5 本轮**无法**审计的部分（是「不知道」，不是「已修」）

- **TCP 粘包/半包**：长度前缀 + 循环拆包的代码**不在本仓库**（`Libraries/TiltedConnect` 空目录，
  `tiltedcore` 由 xmake 拉取）。三个 `OnConsume` 收到的 `aSize` 已是**一条完整消息**的长度，
  本仓库内无累积缓冲区、无拆包循环。**既不能证实也不能证伪。**
- **`Buffer::Reader` 越界行为**：抛异常 / 返回 0 / UB，三种可能的修法完全不同，未知。
- **MinHook 的 XMM/EFLAGS 保存**、**`MH_EnableHook` 是否冻结线程**、**`FunctionHookManager::Add` 的 commit 实现**：
  都在空 submodule 里。
- **`~World` 是否在运行期执行**：`World` 是 `entt::locator` 进程级单例（`World.cpp:135-137`），
  只在静态析构期销毁。§161.1 的两条 UAF **机制已闭合**，但**触发频率需运行期证据**才能定级。
  这不影响修复的正确性——两条都是无条件应当修好的。

**记法**：以上每一条都缺**具体的一份证据**（vendor 源码 / 真机日志 / native 表）。
拿到那份证据才能定论，在此之前不要把它们当已解决，也不要用猜测填空。





















