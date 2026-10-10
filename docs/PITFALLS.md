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

