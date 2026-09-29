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

## 10. 未修复 / 遗留问题（open，按优先级）

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
每帧关窗；取 INI 前判空。

**反向验证过十种改坏方式**，每一种都被它抓到（含 `FormId` 比较被反过来这一种 ——
它最初**逃过**了第一版门禁，因为那版只查标记、不查比较，见 §38.7）。

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

客户端 AE 地址库覆盖度按运行时逐一统计(3082 个代码 id):

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
| `Code/encoding/StringCache.cpp:74-76`(接收侧) | `assert(aMessage.StartId == m_idToString.size());` —— 断言本身就要求**连续性** | 这就是判据 |
| `Code/encoding/Structs/Mods.cpp:24`(v1.1.9 已修) | 写侧 `for (size_t i = 0; i < modCount; ++i)` | ✅ 同一条规则的写侧版本 |

**可机械执行的判据**:凡是「按游标/偏移切片发送」的地方,
**推进量必须等于写出去的条数** ——
写成 `cursor = container.size();` 的那一刻,就已经把「截断」悄悄变成了「永久丢失」。
仓库的接收侧 `assert` 正是这条不变量的断言:`StartId` 必须**恰好等于**本地缓存长度,
一旦发送侧跳过一段,这个断言在 debug 构建下立刻炸,release 下则表现为静默错位。

### 49.3 修法:切一片出去,游标只推进这一片

```cpp
// Code/encoding/StringCache.cpp(修复后)
    if (aStartId < m_idToString.size())
    {
        // One message carries at most kMaxValueCount entries, and the receiver
        // asserts that the ids are contiguous. Send a slice and advance the cursor
        // by exactly what was written, so the rest goes out on the next call
        // instead of being skipped and leaving the two caches out of step.
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
