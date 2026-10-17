# WS2 — Hook 安全审计（范围收窄）

仓库 `D:\sktest\SkyrimTogetherNext`，纯静态阅读，未编译、未改任何仓库文件（本文件除外）。
审计范围：`Code/client/HookAudit.{cpp,h}`、`Code/client/SkyrimVM64.cpp`、`Code/client/Games/` 下全部实际 patch 站点，
外加为回答 Q1/Q2 必须读到的 `Games/GamePatch.h` 与各 `Initializer` 安装点。

---

## Q1 — HookAudit 到底做了什么？真正的安装点在哪？

### 结论：它是"记录 + 事后校验"，安装动作本身转发给了 TiltedReverse 的 FunctionHookManager

```
Code/client/HookAudit.h:28-41  HookAudit::Add()
  :37  if (!Record(reinterpret_cast<void**>(appTargetSlot))) return;   // 门禁 + 记录
  :40  TiltedPhoques::FunctionHookManager::GetInstance().Add(appTargetSlot, apHookFunction, true);
                                                                    // ^^^^ delayed = true
```

第三参数 `true` 是 **delayed**：这里只把 (槽, 钩子函数) 排进队列，**不写任何字节**。
真正改写目标函数首字节的是 TiltedReverse 的 `FunctionHookManager`（MinHook 封装），
由 `TP_HOOK_COMMIT` 一次性提交：

| 环节 | 位置 | 做了什么 |
|---|---|---|
| 宏重定义 | `Code/client/TiltedOnlinePCH.h:42-43` | `#undef TP_HOOK` → `#define TP_HOOK(...) HookAudit::Add(...)` |
| 记录 | `HookAudit.cpp:103-171 Record()` | 存目标地址 + 前 8 字节；拒绝 null 槽与 unresolved stub；报重复目标 |
| 安装 | `HookAudit.h:40` → TiltedReverse（本地未检出） | **真正的字节改写发生在这里** |
| 提交 | `Code/client/main.cpp:138 TP_HOOK_COMMIT` | 提交全部 delayed hook |
| 首次校验 | `Code/client/main.cpp:149 HookAudit::Report()` | 读目标首字节，非 `0xE9` 即报 "did not land" |
| 事后复检 | `HookAudit.cpp:246-286 Verify()` | 被 `CrashHandler.cpp:367`、`PapyrusService.cpp:39` 调用 |

`Record()` 的两个门禁是真实的、非装饰性的：

- `HookAudit.cpp:110-116` — 槽为空 → 报错并拒绝（否则地址 0 交给 detour 引擎）
- `HookAudit.cpp:125-130` — 目标是 `VersionDb::GetUnresolvedStub()` → 报错并拒绝
  （否则 53 个未映射 id 共用的那个桩会被我们跳走）
- `HookAudit.cpp:132-156` — 与已记录目标撞车 → 报"one of the two will be installed and one silently dropped"

### 但真正的 patch 位置远不止 TP_HOOK 这一条路径

`HookAudit::Add` 的**唯一**调用者是 `TP_HOOK` 宏。以下三类实际写游戏字节的地方**完全不经过 HookAudit**：

**A. `TP_HOOK_IMMEDIATE` — 5 处（立即改写，无记录、无审计）**

| 文件:行 | 目标 |
|---|---|
| `Code/client/Games/Memory.cpp:100` | `GameHeap::Allocate`（EngineFixes rehook 路径） |
| `Code/client/Games/Skyrim/BSGraphics/BSGraphicsRenderer.cpp:210` | 帧结束函数（1.5.x 路径） |
| `Code/client/Games/Skyrim/BSGraphics/BSGraphicsRenderer.cpp:225` | `Renderer::Init` |
| `Code/client/Games/Skyrim/BSInput/BSInputDeviceManager.cpp:32` | `PollInputDevices` |
| `Code/client/Games/Skyrim/BSSystem/BSThread.cpp:56` | `BSThread::Initialize` |

**B. `TP_HOOK_IAT` — 7 处（IAT 表项改写）**

`Code/client/Games/Memory.cpp:158-163`（_msize/free/calloc/malloc/_aligned_malloc/_aligned_free）、`:201`（`_initterm_e`）

**C. `GamePatch::*` 字节补丁 — 13 处实际生效的写**

| 文件:行 | 助手 | 写入长度 |
|---|---|---|
| `BSThread.cpp:60` | `Jump` | 5 |
| `BSTaskletManager.cpp:37` | `SwapCall` | 4 |
| `BSGraphicsRenderer.cpp:188` | `Put<uint32_t>` | 4 |
| `BSGraphicsRenderer.cpp:196` | `Put<uint32_t>` | 4 |
| `BSGraphicsRenderer.cpp:214` | `SwapCall` | 4 |
| `Interface/UI.cpp:120` | `SwapCall` | 4 |
| `Interface/UI.cpp:127` | `Put<uint8_t>` | 1 |
| `Interface/UI.cpp:133` | `Put<uint16_t>` | 2 |
| `Menus/SkillsMenu.cpp:15,18,21,28,30` | `Nop` | 6/4/2/4/2 |
| `Projectiles/Projectile.cpp:204` | `Jump` | 5 |

（`Menus/MapMenu.cpp:16-18` 在 `#if 0` 内，`:10`，不计。）

### 因此 "63 recorded, 0 did not land" 不是全量健康信号

`docs/PITFALLS.md:1013` / `:572` 把 `hooks: 63 recorded, 0 did not land` 记成 ✅。
但 `Hook_Renderer_Init`、`Hook_RenderFrameEnd`、`Hook_BSThread_Initialize`、
`Hook_BSInputDeviceManager_PollInputDevices` 这 4 个**每帧/每启动都真实运行**的钩子
由 `TP_HOOK_IMMEDIATE` 安装，**一行都不在那 63 里**。见 F5。

---

## Q2 — 被 patch 的指令总长度：有没有硬编码 5/14 字节但不校验目标指令实际长度？

### F1 [P1] `GamePatch::Jump` / `PutCall` 无条件写 5 字节，从不校验指令边界

```
Code/client/Games/GamePatch.h:408   uint8_t patch[5]{0xE9};
Code/client/Games/GamePatch.h:412   return WriteBytes(apAddress, patch, sizeof(patch), acpWhat);   // 5 字节
Code/client/Games/GamePatch.h:445   uint8_t patch[5]{0xE8};
Code/client/Games/GamePatch.h:449   return WriteBytes(apAddress, patch, sizeof(patch), acpWhat);   // 5 字节
```

守卫 `ValidatePatchSite`（`:352-396`）只读 **2 字节**：

```
Code/client/Games/GamePatch.h:366   uint8_t head[2]{};
Code/client/Games/GamePatch.h:389   if (head[0] == 0xE9 || (head[0] == 0xFF && head[1] == 0x25))
```

它判断的是"这里是否已经有一条分支"，**不是**"接下来 5 个字节是否落在完整指令边界上"。
5 字节写入可能从一条 `mov rax, [rip+x]`（7 字节）的中间开始。

更关键的是：**唯一带期望字节校验的重载没有调用者**：

```
Code/client/Games/GamePatch.h:415   template <class TFunc> bool Jump(void* apAddress, TFunc aReplacement,
                                                                const uint8_t acExpected, const char* acpWhat)
```
grep 全仓，`GamePatch::Jump` 只有两个调用点（`BSThread.cpp:60`、`Projectile.cpp:204`），
**都走无 `acExpected` 的版本**（`:399`）。`:428` 是 `:415` 内部转发给 `:399`，不是外部调用者。

### 最危险的一处：`Projectile.cpp:204` 打在函数中段

```
Code/client/Games/Skyrim/Projectiles/Projectile.cpp:171   auto* pJumpSite = GamePatch::At(pHookLoc,
                                                              {kModern.jumpOut, kLegacy1597.jumpOut, "1.5.97"}, ...);
Code/client/Games/Skyrim/Projectiles/Projectile.cpp:204   GamePatch::Jump(pJumpSite, gen.getCode(), "projectile null check");
```

`pJumpSite` 是**函数内部**的 5 字节槽（modern `+0x374`，1.5.97 `+0x397`），
不是函数入口。`Tools/ida/st_patch_offsets_1597.tsv:15` 对这一行的标注是：

> `HAND ALIGNED: same instruction run, slot 0x50->0x58, field 0x12c->0x124, frame 0x138->0x158`

"hand aligned" = 人工对齐，不是 `patch_offsets_1597.py` 的机械逐指令对齐产物
（对比同表 `:8-13` 那些 "aligned on jne (+0 into it)"）。也就是说这个偏移的证据强度
明显低于表里其它行，而它恰恰是唯一一处打在函数中段的 5 字节写入。

### 截断一条指令后运行时会怎样（静态可推）

不是"跳进垃圾执行"。`jmp rel32` **不压栈**，所以被覆盖区之后的剩余字节变成**不可达**：
- 如果 5 字节正好吃掉一条完整指令 → 行为符合设计（`Projectile.cpp:187` 的
  `jmp_S(apLoc + acSite.jumpBack)` 跳回"第一条没被覆盖的指令"）。
- 如果 5 字节跨越两条指令的边界 → 跳回点 `apLoc + jumpBack` 落进**第二条指令的中间**，
  CPU 从字节中间解码出完全不同的指令流；同时 `Projectile.cpp:192` 的
  `add(rsp, acSite.frame)` + 8 次 `pop`（`:193-200`）所假设的栈帧已经与真实栈帧不符。
- 结果：AV（`CrashHandler` 能抓，`ExceptionInformation[0]==8` 的 execute 故障）
  或静默的寄存器/栈破坏。`CrashHandler.cpp:332-340` 的注释描述的正是这个形态。
- 对 `Jump` 的入口写入（`BSThread.cpp:60`）而言，截断的后果是**被覆盖指令少执行一半**，
  下一次进入该函数时读到的寄存器值错误。

### F2 [P3] `NearStub` 写 14 字节但写在自己分配的内存上，不是缺陷

```
Code/client/Games/GamePatch.h:240   auto* pStub = static_cast<uint8_t*>(RipAllocateN(16));
Code/client/Games/GamePatch.h:246-249  0xFF 0x25 + rel32(0) + 8 字节绝对地址   // 共 14 字节
```

分配 16 字节、写 14 字节，自洽。这一处**没有**指令边界问题。

### F3 [P3] `Nop` 的长度由调用方给，无边界校验（当前全部站点安全）

```
Code/client/Games/GamePatch.h:192   inline bool Nop(void* apAddress, const size_t acLength, const char* acpWhat) noexcept
Code/client/Games/GamePatch.h:213   if (!WriteBytes(pDestination + written, filler, chunkSize, acpWhat))
```

5 个 `Nop` 调用点全部在 `SkillsMenu.cpp:15,18,21,28,30`，长度 6/4/2/4/2，
与 `st_patch_offsets_1597.tsv:8-12` 的 "aligned on jne/je/or (+N into it)" 一致。
当前无缺陷，但同样没有机制阻止将来传入一个跨越边界的长度。

### 门禁没有覆盖这一层

`Tools/Scripts/check_patch_offsets.py` 只比对 "代码里的偏移数字" 与 "tsv 里的偏移数字"
（`:1-30` 的模块 docstring 写明）。它**不知道** `Jump` 会写 5 字节，
也不检查目标处指令有多长。grep `length` 在该文件里的 9 处命中全部与指令长度无关。

---

## Q3 — Hook 函数体有没有 noexcept / try-catch？

### 逐点核对（题目点名的 std::stoi/stoll、容器 `.at`、字符串解析）

- `std::sto*` 全家：**全仓 0 处**（grep `std::sto(i|l|ul|ull|f|d)|stoi\(|stoll\(` over `Code/client` = 0 命中）。不是缺陷面。
- 容器 `.at(`：`Code/client/Games` **0 处**。不是缺陷面。
- **但** `entt` registry 的 `ctx().at<T>()` 是另一回事，见 F4。

### F4 [P1] `Hook_WndProc` → `InputService::WndProc`：749 行、0 个 catch，跑在引擎 WndProc 回调上

```
Code/client/Games/Skyrim/BSGraphics/BSGraphicsRenderer.cpp:71   LRESULT CALLBACK Hook_WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
Code/client/Games/Skyrim/BSGraphics/BSGraphicsRenderer.cpp:73       if (InputService::WndProc(hwnd, uMsg, wParam, lParam) != 0)
Code/client/Games/Skyrim/BSGraphics/BSGraphicsRenderer.cpp:76       return RealWndProc(hwnd, uMsg, wParam, lParam);
```

- `Hook_WndProc` 无 `noexcept`、无 `try`。
- `InputService::WndProc`（`Code/client/Services/Generic/InputService.cpp:506-749`）：
  grep `catch` 在该文件 = **0 命中**。
- 函数体里全是可能抛的东西：
  - `:514-517` `static const bool s_rawInputRegistered = [] { RegisterRawInput(); return true; }();`
    — 函数内 static 的初始化如果抛，异常直接穿出（并且标准规定该 static 之后保持"未初始化"，后续重试）
  - `:529-535` `std::chrono` 比较
  - `:575` `discord.WndProcHandler(...)`、`:581` `imgui.WndProcHandler(...)`
  - `:574`/`:580`/`:610` `World::Get().ctx().at<DiscordService>()` / `at<ImguiService>()`

`Hook_WndProc` 是通过 `aOSData->pWndProc = Hook_WndProc`（`BSGraphicsRenderer.cpp:88`）装上的，
**既不经 HookAudit 也不经 GamePatch**，所以它连"是否落地"都没有记录。

### F4b [P2] 同一个异常缺口：`Hook_Renderer_Init` 无 catch，且 `World::Create` 的失败被吞掉之后仍裸解引用

```
Code/client/Games/Skyrim/BSGraphics/BSGraphicsRenderer.cpp:79   void Hook_Renderer_Init(Renderer* self, ...)      // 无 noexcept、无 try
Code/client/Games/Skyrim/BSGraphics/BSGraphicsRenderer.cpp:92       g_sRs = &World::Get().ctx().at<RenderSystemD3D11>();
```

grep `catch` 在 `BSGraphicsRenderer.cpp` = **0 命中**。
而 `World::Create()` 明确**吞掉**构造失败并让游戏继续跑：

```
Code/client/World.cpp:129-143   void World::Create() noexcept
                                { try { if (!entt::locator<World>::has_value()) entt::locator<World>::emplace(); }
                                  catch (...) { Diagnostics::ReportFailure(...); } }
Code/client/World.cpp:146-149   World& World::Get() noexcept { return entt::locator<World>::value(); }
```

`Create()` 失败 → locator 为空 → `Get()` 的 `value()` 未定义/抛 → 异常从
`Hook_Renderer_Init`（引擎渲染器初始化回调）穿出 → `/EHsc` 下 `std::terminate`。
`CrashHandler` 是 SEH 处理器，接不住 C++ 异常。

同一模式还有：`Hook_Renderer_Init` 里 `:122` `spdlog::info`（分配）、
`:115`/`:119` D3D COM 调用、`:131` `g_sRs->OnDeviceCreation(...)`。

### 其余 hook 体的守卫状态（对照表）

| Hook | 位置 | noexcept | try/catch | 体内容量 |
|---|---|---|---|---|
| `HookVMUpdate` | `SkyrimVM64.cpp:84` | 否 | 否 | 中（调 `World::Update() noexcept`、`DescribeOffset` 自带 try） |
| `HookMainLoop` | `SkyrimVM64.cpp:159` | 否 | 否 | 小（`spdlog::info` 分配） |
| `HookVMDestructor` | `SkyrimVM64.cpp:176` | 否 | 否 | 空壳 |
| `Hook_RenderFrameEnd` | `BSGraphicsRenderer.cpp:169` | 否 | 否 | 小（`PumpOverlay` → imgui/D3D） |
| `Hook_StopTimer` | `BSGraphicsRenderer.cpp:153` | 否 | 否 | 小（同上） |
| `Hook_SetThreadName` | `BSThread.cpp:31` | 否 | 否 | 小（`spdlog::warn`） |
| `Hook_BSThread_Initialize` | `BSThread.cpp:11` | 否 | 否 | 小（`spdlog::warn`） |
| `Hook_BSInputDeviceManager_PollInputDevices` | `BSInputDeviceManager.cpp:10` | 否 | 否 | 小 |
| `HookFormAllocate` | `Memory.cpp:29` | 否 | 否 | placement new（不抛） |
| `HookAudit::Record/Report/Verify` | `HookAudit.cpp:103/173/246` | **是** | **是** | — |

`HookVMUpdate` 与 `HookMainLoop` 是唯一两处体内容量够大的，它们没炸是因为
被调方（`World::Update() noexcept`、`DescribeOffset`）自己收了口 —— 这是**巧合成立**，不是设计。

---

## Q4 — 寄存器保护：pushfq/popfq、XMM、x64 栈 16 字节对齐

### 结论：整个 `Code/` 里 pushfq/popfq 出现 0 次；ABI 保护完全依赖 TiltedReverse/MinHook，而它本地不可读

```
grep "pushfq|popfq" over Code/client  →  0 命中
grep "xmm|XMM|_mm_|SSE" over BSGraphicsRenderer.cpp  →  0 命中
glob "**/FunctionHook.hpp"  →  0
glob "**/MinHook.h"         →  0
Libraries/ 目录实际只有 3 个文件: cxxopts.hpp, magic_enum.hpp, xmake.lua
```

`xmake.lua:43` 声明 `"entt v3.10.0"`，`:45` `"tiltedcore 0.2.9"`，`:119` 提到
`TiltedHooks`/`TiltedReverse`/`TiltedUi` 用 `/GL` 编译 —— 但**源码不在这份工作树里**。
`docs/PITFALLS.md:20499` 也记了 `Libraries/TiltedConnect`（以及 `TiltedHooks`/`TiltedReverse`/`TiltedUI`）
在本机是**未初始化的空 submodule**。

### F6 [P3] 唯一手写的 trampoline：不保存 XMM、不 pushfq，且把 `cmp` 的 EFLAGS 带回了被截断的指令流

```
Code/client/Games/Skyrim/Projectiles/Projectile.cpp:181   mov(rbx, ptr[rsp + acSite.slot]);
Code/client/Games/Skyrim/Projectiles/Projectile.cpp:184   cmp(rbx, 0);          // ← 改 EFLAGS
Code/client/Games/Skyrim/Projectiles/Projectile.cpp:185   jz("exit");
Code/client/Games/Skyrim/Projectiles/Projectile.cpp:187   jmp_S(apLoc + acSite.jumpBack);   // ← 带着 cmp 的 flags 跳回
Code/client/Games/Skyrim/Projectiles/Projectile.cpp:191-201  mov(al,0); add(rsp, frame); pop ×8; ret();
```

- 非 exit 路径上，`cmp rbx, 0` 的 EFLAGS 直接带到 `jumpBack`。
  注释 `:153-157` 声称 `jumpBack` 处原来是 `test r12b,r12b`，`test` 会重设 ZF/SF/PF 并清零 OF/CF，
  所以**在注释属实的前提下**侥幸无害 —— 但这个前提没有任何校验，也没有 `static_assert`/运行时断言。
- 手写 trampoline 不保存 XMM，也不 `pushfq`。它只执行 `mov`/`cmp`/`jz`/`jmp`，
  不碰 XMM，所以当前形态**没有** XMM 问题；但没有机制阻止将来往里加一条浮点指令。
- 栈 16 字节对齐：exit 路径 `add(rsp, frame)` + 8×`pop` + `ret` 完整复原；
  非 exit 路径栈完全没动（只有 `mov`/`cmp`/`jz`/`jmp`）。这一条**成立**。

### 待验证（缺 MinHook 源码，无法静态证明）

- MinHook 的 `CreateHook/EnableHook` 是否在 trampoline 里保存 XMM0-15 与 EFLAGS；
- `MH_EnableHook` 是否真的 `SuspendThread` 冻结其它线程；
- TiltedReverse 的 `FunctionHookManager::Add(slot, fn, /*delayed=*/true)` 在 commit 时的具体实现。

需要：`Libraries/TiltedReverse/include/FunctionHook.hpp`、`Libraries/TiltedHooks/**`（MinHook）的**实际版本**。

---

## Q5 — 安装时机：是否在主线程安装？目标函数正在执行时被改写会怎样？

### 两条启动路径的时序（均为"游戏还没跑起来时安装"）

**launcher 路径**
```
Code/immersive_launcher/Launcher.cpp:117   InstallStartHook();
Code/immersive_launcher/Launcher.cpp:120   RunTiltedInit(LC->gamePath, LC->Version);   // ← 装 hook + HookAudit::Report
Code/immersive_launcher/Launcher.cpp:123   LC->gameMain();                             // ← 游戏才开始跑
Code/immersive_launcher/stubs/CrtStartupHooks.cpp:13/19   std::call_once(s_initGuard, [] { launcher::InitClient(); });
```

**SKSE 路径**
```
Code/skse_client/main.cpp:217   STClient_Bootstrap(...)
Code/skse_client/main.cpp:247   RunTiltedInit(gamePath, version.c_str());
Code/skse_client/main.cpp:249   RunTiltedApp();
```

装 hook 的入口链：
```
Code/client/main.cpp:137        TiltedOnlineApp::InstallHooks2();
Code/client/main.cpp:138        TP_HOOK_COMMIT;                       // delayed 队列在这里提交
Code/client/TiltedOnlineApp.cpp:152   TiltedPhoques::Initializer::RunAll();   // 37 个 static Initializer
Code/client/TiltedOnlineApp.cpp:154   TiltedPhoques::DInputHook::Install();
Code/client/main.cpp:149        HookAudit::Report();
```

### F5 [P2] 全仓没有任何线程挂起：改写"正在被执行"的函数首字节是裸竞态

```
grep "SuspendThread|GetThreadContext|Wow64Suspend" over Code/  →  0 命中
```

最具体的一处：

```
Code/client/Games/Memory.cpp:165   TP_HOOK(&RealFormAllocate, HookFormAllocate);
```
目标是 `GameHeap::Allocate`（id 68115）—— **分配器，任何线程在任何时刻都在调用它**。
在改写其前 5 字节的过程中，如果另一个线程正处在这 5 字节内执行，CPU 会解码出
半新半旧的指令流。launcher 路径下安装发生在 `gameMain()` 之前（相对安全），
但 SKSE 路径下 `STClient_Bootstrap` 由 SKSE 插件加载器调用，此时游戏主线程与
其它 SKSE 插件的线程**已经存在**（`docs/PITFALLS.md:2057` 记录了 SKSE 已把 EngineFixes
加载完这一事实，即至少 EF 的线程可能已起）。

例外：`Memory.cpp:173-184 Hook_initterm_e` 里的 `RehookFormAllocate`（`:181`）跑在
`_initterm_e` 内部，那是 CRT 单线程静态初始化期，**这一条是安全的**。

`docs/PITFALLS.md:2056-2057` 记录的是"EF 会不会**事后覆盖**我们的 hook"（时序问题），
**没有**记录"改写瞬间的线程竞态"。判为新。

### 关于"是否在主线程"（待验证）

`RunTiltedInit` 由 `STClient_Bootstrap`（SKSE 插件加载线程）或 `Launcher.cpp:120`（主线程）调用，
两处都发生在游戏主循环启动之前。但 `docs/PITFALLS.md:946` 记录
`Hook_WndProc` 跑在"游戏主线程"，而 `InputService.cpp:512-513` 的注释自己写着
`RegisterRawInput()` 被刻意放在"窗口自己的线程而不是引导客户端的那条线程"——
说明**装 hook 的线程与游戏主线程确实不是同一条**。需要 MinHook 源码才能确认
`MH_EnableHook` 的线程冻结行为。

---

## F7 [P2] `ValidatePatchSite` 发现"已被别人占用"只 warn，随后照样写

```
Code/client/Games/GamePatch.h:389   if (head[0] == 0xE9 || (head[0] == 0xFF && head[1] == 0x25))
Code/client/Games/GamePatch.h:391       spdlog::warn("patch '{}': {} already begins with a branch, so another mod owns it; writing on top of it", ...);
Code/client/Games/GamePatch.h:395   return true;      // ← 放行
```

`:384-388` 的注释承认这是有意的（"would trade a visible conflict for a silently missing feature"）。
但 `PutCall`（`:434`）的注释 `:436-437` 自己写着
"an existing branch there means the sequence is not what the caller thinks it is" ——
**却说一套做一套，仍然继续写**。

后果与 `HookAudit.cpp:213-235` 检测到的"shared with another mod"是同一类冲突，
但那里只是**报告**，这里是真的**覆盖**。PITFALLS §17（`:1002-1063`）追查的是
"谁先占了这个函数"，没有覆盖到 GamePatch 的写入策略。

---

## F8 [P2] HookAudit 的覆盖缺口：63 recorded ≠ 全部 hook

见 Q1 的表 A/B/C。`HookAudit::Add` 只被 `TP_HOOK` 宏调用（`TiltedOnlinePCH.h:43`），
所以以下全部不进 `Recorded()`：

- `TP_HOOK_IMMEDIATE` × 5（含 `Hook_Renderer_Init` —— 唯一在引擎帧里做 D3D 初始化的那个）
- `TP_HOOK_IAT` × 7
- `GamePatch::Jump/SwapCall/Put/Nop` × 13

`main.cpp:149 HookAudit::Report()` 打印的 "63 recorded, 0 did not land"
**不覆盖** `Hook_Renderer_Init` 这种自己就有失败 return 路径（`BSGraphicsRenderer.cpp:103-108`、
`:125-129`）的关键钩子。`docs/PITFALLS.md:572`、`:1013` 把这个数字当成健康信号，是误读。

---

## 待验证（缺证据，未计入缺陷）

| # | 事项 | 缺什么 |
|---|---|---|
| V1 | MinHook 是否保存 XMM0-15 / EFLAGS | `Libraries/TiltedHooks/**` 源码（本地空 submodule，`PITFALLS.md:20499`） |
| V2 | `MH_EnableHook` 是否 `SuspendThread` 冻结其它线程 | 同上 |
| V3 | `FunctionHookManager::Add(slot, fn, true)` 的 commit 实现 | `Libraries/TiltedReverse/include/FunctionHook.hpp` |
| V4 | entt 3.10 的 `ctx().at<T>()` 在未注册时是 `throw` 还是 `assert` | entt 源码（`xmake.lua:43` 只声明版本，源码不在树内）。若是 assert，F4b 的形态从 "terminate" 变成 "读垃圾内存" |
| V5 | 1.5.97 在 `34452+0x397` 之后第一条指令是否真是 `test r12b,r12b` | 真机 `.text` 字节或 `Tools/ida` 的 1.5.97 反汇编（当前只有 tsv 的 "HAND ALIGNED" 结论，无原始字节） |
| V6 | `STClient_Bootstrap` 被 SKSE 调用时游戏已起了哪些线程 | 运行时线程列表；静态无法证明 |

---

## 摘要（按严重度）

- **[P1]** `Code/client/Games/GamePatch.h:408,445` — `Jump`/`PutCall` 硬编码写 5 字节，`ValidatePatchSite`(`:352-396`) 只读 2 字节、不校验指令边界；唯一带期望字节的 `:415` 重载**无调用者**，两个实际调用点都走无校验版本。
- **[P1]** `Code/client/Games/Skyrim/Projectiles/Projectile.cpp:204` — 唯一一处打在**函数中段**的 5 字节 `jmp`（1.5.97 `+0x397` 仅由 tsv 的 "HAND ALIGNED" 支撑），截断则 `:187` 的跳回点落进指令中间。
- **[P1]** `Code/client/Games/Skyrim/BSGraphics/BSGraphicsRenderer.cpp:71` — `Hook_WndProc` → `InputService::WndProc`（`InputService.cpp:506-749`，grep `catch` = 0）跑在引擎 WndProc 回调上，异常穿出即 `std::terminate`。
- **[P2]** `Code/client/Games/Skyrim/BSGraphics/BSGraphicsRenderer.cpp:79,92` — `Hook_Renderer_Init` 无 catch，且 `World::Create`（`World.cpp:140-143`）吞掉失败后仍裸调 `World::Get().ctx().at<RenderSystemD3D11>()`。
- **[P2]** `Code/client/Games/GamePatch.h:389-395` — 检测到目标已被别的 mod 占用只 `warn` 然后**照样写**；`PutCall` 的注释(`:436-437`)与行为自相矛盾。
- **[P2]** `Code/client/HookAudit.h:28-41` — 审计只覆盖 `TP_HOOK`；`TP_HOOK_IMMEDIATE`×5、`TP_HOOK_IAT`×7、`GamePatch::*`×13 全部在 `Recorded()` 之外，`main.cpp:149` 的 "63 recorded, 0 did not land" 不是全量信号。
- **[P2]** `Code/client/Games/Memory.cpp:165` — 改写 `GameHeap::Allocate` 首 5 字节时无 `SuspendThread`（全仓 grep = 0），SKSE 路径下安装时其它线程已存在。
- **[P3]** `Code/client/Games/Skyrim/Projectiles/Projectile.cpp:184,187` — 手写 trampoline 把 `cmp` 的 EFLAGS 带回 `jumpBack`，靠 `test` 重设 flag 侥幸成立；无 XMM 保存、无 `pushfq`（当前体内容量下不触发）。
- **[P3]** `Code/client/Games/GamePatch.h:192-220` — `Nop` 长度由调用方给，无边界校验（当前 5 个站点全部安全）。
- **[P3]** `Code/client/Games/GamePatch.h:240-249` — `NearStub` 写 14 字节于自分配的 16 字节块，**不是缺陷**，仅记录已核。

---

## 与 PITFALLS 的关系

已 grep `docs/PITFALLS.md` 确认以下**不是**本报告的新发现（已在案）：
- `HookAudit` 只认 `e9`、跟不下 `ff 25` → `PITFALLS.md:1041-1045`（v1.0.40 已修，代码 `HookAudit.cpp:74-101` 已是双形态）
- EngineFixes 占用 `GameHeap::Allocate` → `PITFALLS.md:1002-1063`、`:2056-2057`
- 未映射 id 挂到共享桩 → `PITFALLS.md:238`、`:312`
- 1.5.97 两个帧钩子不在执行路径 → `PITFALLS.md:188-189`
- `DescribeOffset` 的 `std::to_string` 收口 → `PITFALLS.md:14631`、`:14675`
- `MapMenu.cpp` 的 `Nop` 在 `#if 0` 内 → `PITFALLS.md:1985`

以下为本轮**新**结论（PITFALLS 中 grep 无对应记录）：
`ValidatePatchSite` 写入策略、`Jump`/`PutCall` 的 5 字节无长度校验、
`Hook_WndProc`/`Hook_Renderer_Init` 的异常缺口、HookAudit 覆盖缺口、
无 `SuspendThread` 的改写竞态。
