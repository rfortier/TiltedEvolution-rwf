# WS4 · Papyrus 交互审计（范围收窄，不含 §10 全量复查）

范围：`Code/client/Services/PapyrusService.h`(155行) + `Code/client/Services/Generic/PapyrusService.cpp`(85行) 全读；
`Code/client/Games/Misc/BSScript.cpp`(233行)、`Code/client/Games/Skyrim/Misc/BSScript.cpp`(349行) 全读；
`Code/client/Games/PapyrusFunctions.cpp`(26行) 全读。
为闭合调用链额外只读了：`Code/client/Games/Skyrim/Actor.cpp`(PAPYRUS_FUNCTION 调用点 10 处)、
`TESObjectREFR.cpp`(8 处)、`Forms/TESQuest.cpp`(4 处)、`Games/Skyrim/Misc/GameVM.cpp`、
`Services/Generic/{ActorValueService,InventoryService,CharacterService,ObjectService,QuestService,TransportService,RunnerService,ImguiService,DiscordService}.cpp`、
`Systems/RenderSystemD3D11.cpp`、`ModCompat/BehaviorVar.cpp`、`Games/Skyrim/Misc/BSScript.h`、`Games/Skyrim/Misc/BSFixedString.h`。

未编译，全部为静态阅读。已 grep `docs/PITFALLS.md` 去重：§150（`MarshallAndDispatch` 的 `return false` 与
`IsPlayer` 日志刷屏，均已修）、§148（`Variable::Type` 两套枚举）**不在本报告内**，本报告不重复。

Papyrus 包装器的实际规模：`PAPYRUS_FUNCTION` 共 **22 处生效**（Actor.cpp 10 / TESObjectREFR.cpp 8 / TESQuest.cpp 4），
另有 3 处被注释（Actor.cpp:1074、1507、1524）。`GLOBAL_PAPYRUS_FUNCTION` 与 `LATENT_PAPYRUS_FUNCTION` **零调用点**。

---

## Q1 · 从 Papyrus 拿到的 Form*/Actor* 有没有使用前判空？

### 结论：返回值侧无缺陷；缺陷在**入参侧**与**变量解包侧**

返回值侧逐个核对（22 处包装器的返回类型）：返回值全部是 `bool` / `void` / `uint32_t` / `OpenState`（按值），
唯一返回指针的是 `Actor::GetCombatTarget()`（`Actor.cpp:418-422` → `PAPYRUS_FUNCTION(Actor*, Actor, GetCombatTarget)`），
它的唯一使用点是 `Actor.cpp:428` 的 `if (GetCombatTarget() != apTarget)` —— **只做比较，不解引用**。
`Actor::StartCombatEx`（`Actor.cpp:426-433`）全仓库**零调用者**（grep 只命中 `Actor.h:260` 声明与定义本身），
所以「`apTarget` 可能为 nullptr 却当 `Actor*` 传给 native」这条路径是死代码。
**返回值侧：无可证的未判空解引用。**

### [低] `Code/client/Games/Skyrim/Misc/BSScript.cpp:69-81` — `Variable::GetObject()` 对越界非数组标签**穿透**返回指针

```cpp
if (type > Type::kMax) {
    bool isArray = type & 1;
    if (isArray) return nullptr;      // 数组 → nullptr（正确）
}                                     // ← 非数组越界标签：这里没有 return
else if (type != Type::kObject) return nullptr;
return data.pObj;                     // 越界非数组也走到这里
```

`type > kMax(16)` 且数组位为 0（例如 17）时，函数**落到最后一行**，把一个非 object 的 `data` 当 `BSScript::Object*` 交出。
调用方 `ExtractComplexType<T>()`（同文件 `:83-101`）紧接着在 `:95` 调 `pBaseObject->GetHandle()` —— 对伪对象调引擎函数。
意图显然是「不是 object ⇒ nullptr」，数组分支写了 return，非数组分支漏了。
**可达性未证明**（需 VM 给出 >16 且非数组的类型标签；按 §148 当前两侧枚举一致，正常不会出现），故列低。

### [低] `Code/client/Games/Skyrim/Misc/BSScript.cpp:285-286` / `:321-322` — `GetStackFrameVariable` 的返回值未判空

```cpp
BSScript::Variable* pArg1 = apStackFrame->GetStackFrameVariable(0, page);
Actor* pActor = pArg1->ExtractComplexType<Actor>();   // pArg1 未判空
```

`GetStackFrameVariable` 是引擎函数（`Misc/BSScript.h:126-131` 转调 id 104484），可能返回 nullptr。
注意 `pArg1` 与 `pActor` 是两个不同的判空对象：`:287` / `:323` 判的是 `pActor`，**不是** `pArg1`。
同文件 `:278` / `:314` 已经示范了「引擎句柄可能给不出东西」的写法（`LookUpActorParameterType` 返回 nullptr 被显式记录），
这里却没有。参数个数为 1 时下标 0 大概率有效，故列低。

---

## Q2 · 有没有「每个 tick / 每帧」调 Papyrus 的路径？会不会线性放大？

### 结论：**有，且放大是线性的。最严重的一条无任何 per-actor 闸门。**

帧率来源：`World::Update`（`World.cpp:84-107`）由 `TiltedOnlineApp::Update`（`TiltedOnlineApp.cpp:115-134`）
驱动，且 `:93-94` 限流到 **≥8ms 一次（~125/s）**；`UpdateEvent` 在 `:106` 分发。
各 Service 的 `OnUpdate` 都挂在这个 sink 上（如 `ActorValueService.cpp:34`、`InventoryService.cpp:37`、`CharacterService.cpp:78`）。

### [中] `Code/client/Services/Generic/ActorValueService.cpp:303` — 每 250ms × 每个本地 actor 一次 Papyrus `IsDead`

调用链（全闭合）：

```
World.cpp:106  m_dispatcher.trigger(UpdateEvent)        ← 每 ~8ms
ActorValueService.cpp:34  sink<UpdateEvent> → OnUpdate
ActorValueService.cpp:112 RunDeathStateUpdates()
ActorValueService.cpp:284 cDelayBetweenUpdates = 250ms   ← 唯一的闸门（时间）
ActorValueService.cpp:292 auto localView = m_world.view<FormIdComponent, LocalComponent>();
ActorValueService.cpp:294 for (auto entity : localView)
ActorValueService.cpp:303 bool isDead = pActor->IsDead();
Actor.cpp:1051-1053        PAPYRUS_FUNCTION(bool, Actor, IsDead);  return s_pIsDead(this);
PapyrusService.h:79        m_pFunction(pVirtualMachine, 0, apThis)   ← 一次 VM 原生派发
```

**循环体里没有任何 per-actor 提前退出**：`:298` 只判 `pActor` 为空。于是
**VM 调用次数 = 4 × N_local 次/秒**，N_local 是本客户端拥有所有权的 actor 数（`LocalComponent` 由
`CharacterService.cpp:515` 与 `:939` 安装，即「我负责同步的 actor」，一个活跃 cell 里可达数十个）。
N_local=40 → 160 次 VM 调用/秒，**持续整个会话**。

而且 `IsDead` 是纯引擎状态读取，本身不需要过 VM —— 这是「用 VM 调用换一个布尔值」的纯开销。

### [低] `Code/client/Services/Generic/InventoryService.cpp:485` — 每 1s × 每个 naked-deadline actor

`InventoryService.cpp:45-50` 的 `OnUpdate` 每 tick 调 `RunNakedNPCBugChecks()`，后者在 `:455-463` 用 1000ms 限流，
然后在 `:465` 遍历 `m_world.view<FormIdComponent>()`（**全集，含远端 actor**），
`:482` 先按 `nakedDeadline == cNoDeadline` 跳过，`:485` 才 `pActor->IsDead()`。
因为 `:482` 的闸门只放行「刚分配、3D 就绪」（`CharacterService.cpp:2459` 设 deadline，`InventoryService.cpp:487` 清），
所以实际是「每个新 actor 一个小窗口」，**不是稳定的线性放大**。列低。

### [低] `Code/client/Services/Generic/CharacterService.cpp:2450` — 每 tick × 每个 WaitingFor3D actor

`CharacterService.cpp:344` 的 `RunSpawnUpdates()` **完全没有时间闸门**（`:2535-2538` 直接进循环），
循环里 `:2450` 调 `pActor->IsDead()`。但 `:2435` 的 `!pActor->GetNiNode()` 把未就绪的挡掉了，
而 `:2482` 在同一个 tick 内把就绪的从 `WaitingFor3D` 移除 —— 所以每个 actor 实际只过一次，**不构成每帧 × N**。

### 补充（用户触发，非自动路径）

`Code/client/Services/Debug/DebugService.cpp:353-357`：「Stop all combat」按钮对 `highActorHandleArray` 逐个
`pRefr->StopCombat()`（→ `Actor.cpp:449-450` → Papyrus `Actor::StopCombat`），**一次点击 = N 次 VM 调用**。
只在调试菜单里，且 `:356` 有 `GetNiNode()` 判空。列备注。

### 与「N 个远端玩家 = N 次 VM 调用」的关系

没有一处是「按远端玩家列表循环调脚本函数」的写法。远端玩家只通过上面这些**通用 per-actor 循环**被计入 N，
即 Q2 的放大确实存在，但机制是「遍历注册表」而不是「遍历玩家名单」。

---

## Q3 · 有没有从网络线程或延迟任务里直接调 VM 的路径？

### 结论：**未发现。** 证据链如下（这是本任务唯一的否定结论，附完整链路）

本客户端只有 **3 个非游戏线程**（全树 grep `std::thread|CreateThread|async|detach`）：

| 线程 | 位置 | 是否可达 Papyrus |
| --- | --- | --- |
| handshake pump | `TransportService.cpp:642` | 否（见下） |
| Discord callbacks | `DiscordService.cpp:206-221` | 否（只跑 `run_callbacks`） |
| BehaviorVar 初始化 | `BehaviorVar.cpp:769-779` | 否（只读 behaviors 目录） |

pump 线程为什么到不了 VM：

1. `TransportService.cpp:239` —— 线程 id **等于** `m_gameThreadId`（`:118` 构造时记下，即 World 构造线程 = 游戏线程）
   才在本地 `DispatchMessage`；否则走 `:248-249` 只入队，**不解析后处理**。
2. `:730-733` / `:748-751` —— pump 只把 `FlushPendingMessages()` 投进 `RunnerService::Queue`。
3. `RunnerService.cpp:58-62` —— 任务只在 `OnUpdate` 里 `Pop` + 执行。
4. `World.cpp:105` —— `RunnerService::OnUpdate` 的唯一调用者，在游戏线程的帧循环里。

所以 `FlushPendingMessages` → `DispatchMessage` → 消息 handler（`ObjectService::OnActivateNotify` 的 `GetOpenState`、
`OnNotifyScriptAnimation` 的 `PlayAnimation`/`PlayAnimationAndWait`、`QuestService::OnQuestUpdate` 的 `ScriptSetStage`/
`ScriptResetAndUpdate`、`CharacterService` 的 `IsDead` 等）**全部落在游戏线程**。

延迟任务（`RunnerService::Queue`）同理：唯一执行点是 `World.cpp:105`，也在游戏线程。
第二条驱动源是窗口定时器 `WM_TIMER → World::Get().Update()`（`PITFALLS.md:191` 记录，`InputService::WndProc`），
`WM_TIMER` 投递给创建窗口的线程，即游戏主线程 —— 仍是同一线程。

调试 UI 里的 Papyrus 调用（`QuestDebugView.cpp:66/80` 的 `ScriptSetStage`、`DebugService.cpp:357` 的 `StopCombat`）
走 `ImguiService.cpp:40 m_drawSignal.publish()` ← `RenderSystemD3D11.cpp:50` ← `PumpOverlay()`
← `BSGraphicsRenderer.cpp:147` 的 `Hook_StopTimer`，即游戏帧末，**主线程**。

> 注：`PapyrusService::Capture`（`PapyrusService.cpp:52-59`）确实在「script extender 的 papyrus 线程」上跑
> （`PapyrusService.h:23-25` 的注释与 `Misc/BSScript.cpp:64-68` 的说明一致），但它只写 `m_functions`，
> 与 `Get` 共用 `m_lock`，**不调 VM**。这一条不是缺陷。

---

## Q4 · 参数数量/类型/栈深度是否与脚本签名一致？失败时清理了吗？

### [中] `Code/client/Games/Skyrim/Misc/BSScript.cpp:280-288` / `:307-315` — 基类构造退化时 `parameters` 未初始化，派生类仍往里写

```cpp
// Misc/BSScript.h:280-288
NativeFunction(const char* apFunctionName, const char* apClassName, bool aIsStatic, uint32_t aParameterCount)
    : pFunction(nullptr)
{
    POINTER_SKYRIMSE(TNativeFunctionCtor, ctor, 104653);
    if (!ctor.Get())          // ← 未映射就返回，parameters{data,size,capacity} 全是未初始化内存
        return;
    TiltedPhoques::ThisCall(ctor, this, apFunctionName, apClassName, aIsStatic, aParameterCount);
}
```

```cpp
// Skyrim/Misc/BSScript.cpp:271-279（IsRemotePlayerFunc，IsPlayerFunc :307-315 同构）
    : NativeFunction(apFunctionName, apClassName, true, 1)
{
    pFunction = ...;
    returnType = aType;
    parameters.data[0].pType = LookUpActorParameterType(...);   // ← 通过未初始化的 parameters.data 写
}
```

`parameters` 是 `{Entry* data; uint16_t size; uint16_t capacity;}`（`Misc/BSScript.h:237-252`），
`data` 只在引擎 ctor（id 104653）里分配。ctor 未映射时 `:285-286` 直接 return，
派生类 ctor 随后**沿垃圾指针写入** —— 这是 VM 注册路径上的写崩溃，而不是「降级成空回调」。
`Misc/BSScript.h:285` 的注释说 ctor 缺失时「degrade to a no-op callback」，但派生类 ctor 的写入使这个降级不成立。
（对照：dtor `Misc/BSScript.h:270-278` 在 `!dtor.Get()` 时 return，是安全的 —— 它不写 `this` 的字段。）
**可达性未证明**：取决于 id 104653 在 1.5.x 上是否映射，见「待验证」。

### [中] `Code/client/Services/PapyrusService.h:153` + `:64-80` — 查找失败**永久锁死**，无重试、无清理

```cpp
#define PAPYRUS_FUNCTION(returnType, scope, name, ...) \
    static PapyrusFunction<returnType, scope, __VA_ARGS__> s_p##name( \
        #scope "::" #name, World::Get().ctx().at<PapyrusService>().Get(#scope, #name));
```

`s_p##name` 是**函数内 static**：第一次执行该行时构造，构造参数就是那一刻 `Get` 的返回值。
`PapyrusFunction` 把它存进 `m_pFunction`（`:66`），`operator()`（`:70-80`）此后**只读 `m_pFunction`**：

```cpp
if (!m_pFunction)
    return PapyrusDetail::Unavailable<Return>(m_pName, "the game never registered it", m_reported);
```

`m_reported`（`:85`）让这条日志只出现一次，而**没有任何代码重新调用 `PapyrusService::Get`**。
于是「第一次查不到」= 「本进程内这个 native 永久不可用」，即使 `Capture` 之后把它注册进来了。
`PapyrusService::Get`（`PapyrusService.cpp:6-50`）里那段 `captured == 0` 的 `HookAudit::Verify` 自检
（`:35-41`）说明作者预期过「先查后注册」，但返回值没有回填路径 —— **自检跑了，结论却无法被后来的注册修正**。

这直接回答「失败时清理了吗」：**没有**。失败被固化在 static 里，`Return{}` 静默返回
（`Actor::IsDead` → `false`、`GetOpenState` → `kNone`、`GetCombatTarget` → `nullptr`）。
对照 §150.2 的教训（「返回默认值 = 静默失效」），这里是同一形状的第二层：**默认值被永久缓存**。

### 参数个数/类型核对结果（22 处）

按 `Code/plugins/papyrus/stubs/*.psc`（仓库自带的签名参照）与 `Tools/Scripts/dump_addresses.py` 逐条比对
（22 处全查，下表列出有独立签名来源的 16 处），**个数与类型全部一致**，无一处错位：

| 包装器 | 声明参数 | stub/参照 |
| --- | --- | --- |
| `Actor.cpp:414` IsInCombat | 0 | 无 stub，0 参数 bool |
| `Actor.cpp:420` GetCombatTarget | 0 | `dump_addresses.py:321` |
| `Actor.cpp:443` StartCombat | 1 (`Actor*`) | `dump_addresses.py:395` |
| `Actor.cpp:449` StopCombat | 0 | `dump_addresses.py:396` |
| `Actor.cpp:631` SetOutfit | 2 (`const BGSOutfit*`, `bool`) | 与 Papyrus `SetOutfit(Outfit, bool)` 一致 |
| `Actor.cpp:1025` RemoveFromAllFactions | 0 | — |
| `Actor.cpp:1051` IsDead | 0 | `dump_addresses.py:341` |
| `Actor.cpp:1090` PlayIdle | 1 (`TESIdleForm*`) | `stubs/Actor.psc:10 bool PlayIdle(Idle akIdle)` ✓ |
| `Actor.cpp:1517` IsInDialogueWithPlayer | 0 | `dump_addresses.py:348`；参数类型 `TESObjectREFR*` 与
`plugins/OStimNG/skse/src/GameAPI/GameActor.h:102` 的 `RE::TESObjectREFR*` 一致 ✓ |
| `TESObjectREFR.cpp:362` Delete | 0 | `dump_addresses.py:307` |
| `TESObjectREFR.cpp:371/380` Disable/Enable | 1 (`bool`) | `stubs/ObjectReference.psc:9-10` ✓ |
| `TESObjectREFR.cpp:446` GetOpenState | 0 | `dump_addresses.py:331` |
| `TESObjectREFR.cpp:1065` GetAnimationVariableInt | 1 (`BSFixedString*`) | 无 stub/dump 条目，见「待验证 6」 |
| `TESObjectREFR.cpp:464` RemoveAllItems | 3 | `dump_addresses.py:366` |
| `TESObjectREFR.cpp:1076` PlayAnimationAndWait | 2 (`BSFixedString*`) | `stubs/ObjectReference.psc:15` ✓ |
| `TESObjectREFR.cpp:1104` PlayAnimation | 1 | `stubs/ObjectReference.psc:14` ✓ |
| `TESQuest.cpp:114` SetCurrentStageID | 1 (`int`) | `dump_addresses.py:381` / `stubs/Quest.psc:8 SetStage(int)` ✓ |

栈深度：所有 `PAPYRUS_FUNCTION` 都走 `PapyrusFunction::operator()` 的
`m_pFunction(pVirtualMachine, 0, apThis, args...)`（`PapyrusService.h:79`）—— 只传 `stackID = 0`，不自建栈帧。
这与本仓库既有做法一致（`plugins/OStimNG/skse/src/GameAPI/GameActor.h:86` 同样传 `0`），**不是缺陷**。

### [低] `Code/client/Services/PapyrusService.h:123-151` — `LatentPapyrusFunction` 会伪造 latent 栈帧，且零调用点

```cpp
RefrOrInventoryObj self{apThis, nullptr, 0};        // :142  pInventoryForm=nullptr, itemCount=0
return m_pFunction(pVirtualMachine, 0, self, args...);   // :144
```

`LATENT_PAPYRUS_FUNCTION` 全树**零使用**（grep 只命中定义本身）。它不是「待用」，而是
「一旦有人用就会把 `{pRefr, nullptr, 0}` 这个自造结构当引擎的 `RefrOrInventoryObj&` 传进去」的路径：
latent native 的这个结构由引擎按调用场景填（reference vs inventory object），
硬编码 `nullptr/0` 会让 `RemoveItem` 一类按 `itemCount` 分支的 native 走到 0 分支。
没有调用点 ⇒ 当前不可达，列低；建议删除或加 `static_assert`/注释禁止使用。

### 已核对为「不是缺陷」的两处

- `Code/client/Games/PapyrusFunctions.cpp:10` / `:19` 解引用 `apActor` 前未判空 —— **安全**：
  唯一调用方 `Skyrim/Misc/BSScript.cpp:287` / `:323` 已在 `ExtractComplexType` 之后判空，
  `:296` / `:327` 对 None 参数显式 `apResult->Set<bool>(false); return true;`（§150.2 的修法已落地）。
- `MarshallAndDispatch` 的返回值语义 —— 三处（`:281-305`、`:317-336`、`:345-349`）都写 `apResult` 后
  `return true`，无 `return false` 残留，与 `Tools/Scripts/check_papyrus_natives.py` 的 R5 判据一致。

---

## 待验证（缺证据，不计入缺陷）

1. **`ObjectReference::Reset` 是否真的以 `ObjectReference` 类名注册？**
   证据缺口：`Code/client/Games/Skyrim/Actor.cpp:1083-1085`
   `PAPYRUS_FUNCTION(void, ObjectReference, Reset, int, TESObjectREFR*); s_pReset(this, 0, nullptr);`
   —— 这是 2 个参数。仓库自带的 `Code/plugins/papyrus/stubs/ObjectReference.psc` 与 `Actor.psc`
   **都没有 `Reset` 声明**，`Tools/Scripts/dump_addresses.py` 的 104 条 `GenerateDeclarationForPapyrus`
   里也**没有 `Reset`**。缺的证据：该 native 的引擎注册名与真实参数个数（CommonLibSSE 头，或实机
   `papyrus function ObjectReference::Reset is not registered` 是否出现在日志）。
   失败模式本身是安全的（`Get` 返回 nullptr → 记一行日志 → `Actor::Reset()` 变 no-op，
   `Actor::Respawn()` = `Resurrect(false)` + 无 Reset），所以即便缺失也不崩，只是 Respawn 语义不全。

2. **`NativeFunction` 引擎 ctor（id 104653）在 1.5.x 上是否映射？**
   缺的证据：版本映射表里 104653 的 1.5.97 条目（本机禁止编译/运行，`POINTER_SKYRIMSE` 的解析结果只能实机看）。
   若映射存在，上文「[中] parameters 未初始化」不可达；若不存在，是启动期写崩溃。

3. **`PapyrusService::Get` 的第一次调用是否可能早于 `BindEverythingToScript` 完成？**
   缺的证据：`TiltedOnlineApp.cpp:95 World::Create()` 与 VM 注册（`Misc/BSScript.cpp:75-93`）的
   实际先后顺序。代码里 `PapyrusService` 在 `World.cpp:46` 构造，而 bind hook 在游戏 VM 初始化时触发；
   两者的时序在静态阅读里不可判定。这决定上文「[中] 永久锁死」是否已经在生产里发生。

4. **`apStackFrame->GetStackFrameVariable(0, page)` 在参数个数为 1 时是否可能返回 nullptr？**
   缺的证据：id 104484 的行为（引擎内部实现不可读）。若不会返回 nullptr，上文那条低危可撤销。

5. **`Variable::GetObject()` 的越界穿透是否可达？**
   缺的证据：VM 是否会把 `type > 16` 且非数组的标签写进 `Variable`。按 §148 当前两侧枚举一致，
   正常路径不会；需实机在 `IsRemotePlayer`/`IsPlayer` 上打点确认。

6. **`ObjectReference::GetAnimationVariableInt` 是否存在、签名是什么？**
   证据缺口：`TESObjectREFR.cpp:1065` 声明 `PAPYRUS_FUNCTION(uint32_t, ObjectReference, GetAnimationVariableInt, BSFixedString*)`，
   但 `Code/plugins/papyrus/stubs/ObjectReference.psc` 没有这条、`Tools/Scripts/dump_addresses.py` 的 104 条里
   也**没有** `GetAnimationVariableInt`（Papyrus 侧对应的是 `GetAnimationVariableFloat`/`GetAnimationVariableInt` 家族，
   后者存在与否与参数类型需 CommonLibSSE 头确认）。唯一调用点 `Actor.cpp:1129` 在 `FixVampireLordModel` 的 try 块内。
   若该 native 不存在，`Get` 返回 nullptr → 记一行 error → 返回 `0` → `:1133` 的 `if (isLevitating)` 恒假，
   **吸血鬼领主变身不再切换 LevitationToggle 动画**（静默功能缺失，不崩）。这与上文「永久锁死」叠加：
   一行 error 之后，本进程内再无机会。
