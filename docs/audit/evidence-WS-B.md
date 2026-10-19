# WS-B · 客户端引擎交互安全审计（空指针 / 越界 / 偏移）

范围：`Code/client/Games/GamePatch.h`、`PapyrusFunctions.cpp/.h`、`Papyrus.h`、`Memory.cpp`、
`Overrides.cpp/.h`、`Skyrim/Actor.cpp`、`Skyrim/Misc/BSScript.cpp/.h`、`Skyrim/TESObjectREFR.cpp`、
`HookAudit.cpp/.h`。其余 `Games/` 下的文件仅在**被上述文件直接调用**时读取
（`Games/References.h`、`Games/ActorExtension.cpp/.h`、`Games/Skyrim/RTTI.h`、`VersionDb.h`、
`Services/PapyrusService.h`、`CrashHandler.cpp` 的 `FormatModuleOffset`、`Games/Magic/MagicSystem.h`）。

前置去重：`docs/audit/WS2-hooks.md`（GamePatch 5 字节写入、HookAudit 覆盖缺口、ValidatePatchSite 只 warn、
Memory.cpp 无 SuspendThread）、`docs/audit/WS4-papyrus.md`（`GetObject` 越界穿透、`GetStackFrameVariable`
未判空、`NativeFunction` 构造退化、`PapyrusService` 查找失败锁死）、`docs/FOLLOW-UP.md` §1/§2
（`bUseFaceGenPreprocessedHeads` 三偏移）、`docs/PITFALLS.md` §34.4（数据 id 拿桩）、§34.5
（`GetExtension()` 已在 `ForcePosition` 判空）、§152、§161.4。
**下列条目均不重复上述结论。**

### 本轮新测到的工具事实（均实跑，作为下文判据的基础）

1. `python Tools/Scripts/check_null_accessors.py` → `NULL ACCESSOR CHECK OK`：19 个可空访问器、
   445 个源文件、0 条豁免；**`Actor::GetExtension` 不在表内**（`NOT_NULLABLE` 只有 `BehaviorVar::Get`）。
   ⇒「`GetExtension()` 的结果能不能直接用」**今天没有任何门禁在回答**。
2. `GameFiles/Skyrim/SKSE/Plugins/versionlib-ae-to-se-*.map` 十张表逐 id 实测：
   `414391 / 400188 / 68115 / 68117 / 104653 / 104655 / 104296 / 104247 / 104484 / 104483 / 38717`
   **全部在十张表里**。⇒ 下文所有条目**不得**再以「1.5.x 上这个 id 缺映射」为由解释。

### 一处必须写在前面的机制订正

本报告的第一版把「`GetExtension()` 之后不判空」写成「nullptr 解引用」。**核对实现后这个说法不成立**，
记录在此以免后来的人沿用它：

```cpp
// Code/client/Games/Skyrim/Actor.cpp:308-337
ActorExtension* Actor::GetExtension() noexcept
{
    if (AsExActor())           return static_cast<ActorExtension*>(AsExActor());
    if (AsExPlayerCharacter()) return static_cast<ActorExtension*>(AsExPlayerCharacter());
    return nullptr;
}
ExActor* Actor::AsExActor() noexcept
{
    if (formType == Type && this != PlayerCharacter::Get())   // 只比对 formType 和「是不是玩家」
        return static_cast<ExActor*>(this);                    // 不验证这个对象是不是我们分配的
    return nullptr;
}
ExPlayerCharacter* Actor::AsExPlayerCharacter() noexcept
{
    if (this == PlayerCharacter::Get()) return static_cast<ExPlayerCharacter*>(this);
    return nullptr;
}
```

对一个**非空**的 `Actor*`：非玩家走 `AsExActor`（`formType == Character` 即命中），玩家走
`AsExPlayerCharacter` ⇒ **两条分支各命中一条，`GetExtension()` 实际上几乎不可能返回 `nullptr`。**
所以「判空」不是这块的要点。真正的要点见 **WS-B-01**：
`static_cast<ExActor*>(this)` 把一个**没有扩容过**的引擎对象硬当成 `ExActor`/`ExPlayerCharacter`，
读到的 `ActorExtension` 落在对象**之外**。

---

## 【可闭合】

---

### WS-B-01 · `GetExtension()` 假设「每个 actor 都被我们的分配钩子扩容过」—— 越界读写的真正入口

- **类别**：越界读 / 越界写（结构前提可闭合，是否成立见文末判据）
- **严重度**：**高**
- **位置**：`Code/client/Games/Skyrim/Actor.cpp:308-337`（产生越界指针的那两行）
  与 `Code/client/Games/Skyrim/Actor.cpp:495 / 1066 / 1059 / 1552 / 1570 / 1583 / 1223 / 1238 /
  1432 / 1461`、`Code/client/Games/Skyrim/TESObjectREFR.cpp:1025 / 1204 / 1217 / 1230`（消费点）
- **问题**：`ActorExtension` 是 `ExActor : Actor, ActorExtension` / `ExPlayerCharacter : PlayerCharacter,
  ActorExtension` 的**基类**（`Games/References.h:20-27`），也就是说它只存在于 **我们自己的派生类**里，
  而 `GetExtension()` 不做任何「这个对象到底是不是 `ExActor`」的检查 —— 它只看
  `formType == Type`。对任何一个**由引擎按 `sizeof(Actor)` 原样分配**的 actor，
  `static_cast<ExActor*>(this)` 仍然返回 `this`，再 `static_cast<ActorExtension*>` 就得到
  「对象起始 + `sizeof(Actor)`」——**一个刚好落在该 actor 分配之外**的指针，随后：
  - **读**：`IsRemote()`/`IsPlayer()`/`IsLocalPlayer()` 读 `onlineFlags`（`ActorExtension.h:38`，
    在扩展体的最后），`GetExtension()->GraphDescriptorHash`（`Actor.cpp:1059`）读得**更远**；
  - **写**：`SetRemote`/`SetPlayer`（`ActorExtension.cpp:30-44`）与
    `nakedDeadline`/`LatestAnimation` 的赋值写出去。

  扩容发生在 `HookFormAllocate`（`Games/Memory.cpp:29-58`），它**只在一个 switch 分支里**匹配
  `case sizeof(Actor)` / `case sizeof(PlayerCharacter)`。也就是说 `GetExtension()` 的正确性
  完全依赖「**每一个** actor 的分配都恰好长这两个尺寸之一，且都经过这个钩子」。这个前提代码里
  **没有任何地方声明过，也没有任何东西在检查**。
- **证据**：

```cpp
// Code/client/Games/References.h:20-27 —— 扩展体只在我们自己的派生类里
struct ExActor : Actor, ActorExtension {};
struct ExPlayerCharacter : PlayerCharacter, ActorExtension {};

// Code/client/Games/Skyrim/Actor.cpp:323-329 —— 只看 formType，不看分配来源
ExActor* Actor::AsExActor() noexcept
{
    if (formType == Type && this != PlayerCharacter::Get())
        return static_cast<ExActor*>(this);        // <-- 未扩容的引擎对象也走这条
    return nullptr;
}

// Code/client/Games/Memory.cpp:29-58 —— 扩容只匹配两个精确尺寸
void* TP_MAKE_THISCALL(HookFormAllocate, GameHeap, size_t aSize, size_t aAlignment, bool aAligned)
{
    switch (aSize)
    {
    case sizeof(Actor):           aSize = sizeof(ExActor); break;
    case sizeof(PlayerCharacter): aSize = sizeof(ExPlayerCharacter); break;
    default: break;                        // <-- 其它尺寸一律不扩容，但 GetExtension() 照样返回指针
    }
    ...
    switch (aSize)
    {
    case sizeof(ExActor):           pExtension = static_cast<ActorExtension*>(static_cast<ExActor*>(pPointer)); break;
    case sizeof(ExPlayerCharacter): pExtension = static_cast<ActorExtension*>(static_cast<ExPlayerCharacter*>(pPointer)); break;
    default: break;
    }
    if (pExtension) { new (pExtension) ActorExtension; }   // <-- 没进这两个 case 时，构造函数从未跑过
}

// Code/client/Games/Skyrim/Actor.cpp:495 —— 读侧（钩子体，每次引擎移除法术都跑）
if (apThis->GetExtension()->IsLocalPlayer() && result)
// Code/client/Games/Skyrim/TESObjectREFR.cpp:1204 —— 读侧（每次旋转都跑）
if (pActor->GetExtension()->IsRemote())
```

- **一句话最小改法**：`GetExtension()` 改成先验身份再转换，例如给 `ActorExtension` 一个
  magic 头（`HookFormAllocate` 里构造时写入），`AsExActor` 在返回前校验该 magic；
  或在 `Actor` 上加一个「是否 Ex」的位并由分配钩子设置，`GetExtension()` 只在置位时返回扩展体。
- **【运行时依赖】判据**：**如果** 存在任何一条「分配尺寸不等于 `sizeof(Actor)`/`sizeof(PlayerCharacter)`
  的 actor 分配路径」，或**在本钩子装好之前**就有 actor 被构造出来（`PITFALLS` 已记录：SKSE 路径下
  `HookFormAllocate` 的安装时机晚于引擎的部分初始化），**则** 这些 actor 的 `GetExtension()`
  返回值落在分配之外，读写命中相邻堆块；后果是**静默的内存破坏**，崩溃点不在本文件、也不在调用点。
  **如果** 所有 actor 无一例外都走 `HookFormAllocate` 的两个分支，则本条不成立。
  本轮**无法判定**属于哪一种（见「未覆盖区域」第 2 条：`Libraries` 是空 submodule，读不到分配器全貌）。
- **本轮新增的旁证（同一契约在仓库里被读成两种意思）**：@@Actor.cpp@@ 里 21 处
  @@GetExtension()@@ 引用中，**4 处写了判空**再使用 —— @@:201-202@@ @@ForcePosition@@、
  @@:530@@ @@HookCharacterDestructor@@、@@:1153@@ @@HookSetPosition@@、@@:1271@@ @@HookApplyActorEffect@@；
  其余 **10 处箭头形式直接解引用**（@@:260@@ @@SetRemote@@（**写**）、@@:495@@、@@:889@@、@@:1223@@、
  @@:1432@@ ×2、@@:1453@@、@@:1461@@、@@:1552@@、@@:1570@@、@@:1583@@），另有 **5 处**先存进局部变量却
  不判空就解引用（@@:1058@@、@@:1065@@、@@:1218@@、@@:1237@@、@@:1297@@）。
  `TESObjectREFR.cpp` 另有 4 处（`:1025`、`:1204`、`:1217`、`:1230`）。
  两种写法**并存且都活得下去**，正说明「`GetExtension()` 一定非空、且一定指向已构造的扩展体」
  这个前提**没有任何东西在维持** —— 它既没有断言，也没有门禁（见本轮工具事实 1），
  更没有在 `HookFormAllocate` 之外的任何地方被建立过。

---

### WS-B-02 · `HookActivate` 对 `apThis->baseForm` 不判空（仓库其余同类站点都判了）

- **类别**：空指针（可闭合）
- **严重度**：中
- **位置**：`Code/client/Games/Skyrim/TESObjectREFR.cpp:1129-1147`
- **问题**：`HookActivate` 是玩家对任何物体按 E 的入口。进 `if (pActivator && ...)` 之后
  **连续两处** `apThis->baseForm->formType` 都不判 `baseForm`。
  「`baseForm` 必须判空」在本仓库有明确先例：`Services/Generic/ObjectService.cpp:214/273/407`
  三处写 `if (pObject->baseForm && pObject->baseForm->formType == ...)`；另有一批日志点写
  `pActor->baseForm ? pActor->baseForm->GetName() : "<no base form>"`
  （`Actor.cpp:679/1541`、`CharacterService.cpp` 十处、`InventoryService.cpp` 四处）。
  也就是说：**仓库自己认定 `baseForm` 可空，只有这个每按一次 E 就跑一遍的钩子不这么认为。**
- **证据**：

```cpp
// Code/client/Games/Skyrim/TESObjectREFR.cpp:1129-1147
bool TP_MAKE_THISCALL(HookActivate, TESObjectREFR, TESObjectREFR* apActivator, uint8_t aUnk1,
                      TESBoundObject* apObjectToGet, int32_t aCount, char aDefaultProcessing)
{
    Actor* pActivator = Cast<Actor>(apActivator);
    if (pActivator && apThis->baseForm->formType != FormType::Book)     // <-- baseForm 不判空
    {
        auto openState = TESObjectREFR::kNone;
        if (apThis->baseForm->formType == FormType::Door)               // <-- 第二处
            openState = apThis->GetOpenState();
        World::Get().GetRunner().Trigger(ActivateEvent(apThis, pActivator, apObjectToGet, aCount,
                                                       aDefaultProcessing, aUnk1, openState));
    }
    return TiltedPhoques::ThisCall(RealActivate, apThis, apActivator, aUnk1, apObjectToGet, aCount, aDefaultProcessing);
}
```

- **一句话最小改法**：守卫改成
  `if (pActivator && apThis->baseForm && apThis->baseForm->formType != FormType::Book)`。

---

### WS-B-03 · `HookRemoveSpell` 里 `apSpell` 未判空（同功能的 `Actor::RemoveSpell` 判了）

- **类别**：空指针（可闭合）
- **严重度**：低
- **位置**：`Code/client/Games/Skyrim/Actor.cpp:492-506`
- **问题**：同名功能的两个入口对同一个参数的处理**不一致**：`Actor::RemoveSpell`（`:453-473`）
  第一件事就是 `if (!apSpell) { error; return false; }`，而钩子体 `HookRemoveSpell` 在
  `result` 为真时直接 `apSpell->formID`。钩子包住的是**引擎的**调用者，参数是否可能为空由引擎决定，
  而本仓库对同一个函数明确的立场是「它可能为空」。
- **证据**：

```cpp
// Code/client/Games/Skyrim/Actor.cpp:453-466 —— 同一功能，判了空
bool Actor::RemoveSpell(MagicItem* apSpell) noexcept
{
    if (!apSpell) { spdlog::error(__FUNCTION__ ": apSpell is null"); return false; }
    return TiltedPhoques::ThisCall(RealRemoveSpell, this, apSpell);
}
// Code/client/Games/Skyrim/Actor.cpp:492-506 —— 同一个引擎函数，没判
bool TP_MAKE_THISCALL(HookRemoveSpell, Actor, MagicItem* apSpell)
{
    bool result = TiltedPhoques::ThisCall(RealRemoveSpell, apThis, apSpell);
    if (apThis->GetExtension()->IsLocalPlayer() && result)
    {
       RemoveSpellEvent removalEvent;
        removalEvent.TargetId = apThis->formID;
        removalEvent.SpellId = apSpell->formID;          // <-- 空指针就崩在这里
        World::Get().GetRunner().Trigger(removalEvent);
    }
    return result;
}
```

- **一句话最小改法**：`:501` 改为 `if (apSpell) removalEvent.SpellId = apSpell->formID; else continue;`
  （或直接把条件写成 `... && result && apSpell`）。

---

### WS-B-04 · 通过 `POINTER_SKYRIMSE` 解析的地址在 `ThisCall` / `Get()(...)` 之前**从不判空**

- **类别**：错误被忽略 / 空指针（调用点侧可闭合；触发条件见文末判据）
- **严重度**：**高**（一旦触发是跳 0 执行，不是降级）
- **位置**（本任务范围内）：

| 文件:行 | id | 形式 |
| --- | --- | --- |
| `Skyrim/Misc/BSScript.cpp:6-8` | 104247 `Variable::GetHandle` | `ThisCall(s_getHandle, this)` |
| `Skyrim/Misc/BSScript.cpp:22-26` | 104296 `Variable::Reset` | `ThisCall(s_reset, this)` |
| `Skyrim/Misc/BSScript.cpp:105-107` | 53915 `Statement::SetSize` | `ThisCall` |
| `Skyrim/Misc/BSScript.cpp:198-200` | 104651 `NativeFunctionBase::Call` | `ThisCall` |
| `Skyrim/Misc/BSScript.cpp:112-113` | 414391 `IObjectHandlePolicy::Get` | `return *s_policy.Get();`（**数据 id**） |
| `Skyrim/TESObjectREFR.cpp:130-134` | 17201 `GetByHandle` | `s_getRefrByHandle.Get()(aHandle, pResult)` |
| `Skyrim/TESObjectREFR.cpp:1040-1042` | 52849 `UpdateItemList` | `ThisCall` |
| `Skyrim/TESObjectREFR.cpp:1054-1058` | 19800 `EnableImpl` | `ThisCall` |
| `Skyrim/TESObjectREFR.cpp:143-148` | 19846 `GetHandle` | `ThisCall` |
| `Skyrim/TESObjectREFR.cpp:153-157` | 400312 `GetNullHandle` | `return s_nullHandle.Get();`（**数据 id**） |
| `Skyrim/TESObjectREFR.cpp:167-172` | 20231 `SetLeveledCreature` | `ThisCall` |
| `Skyrim/Actor.cpp:226-229` | 40255 `QueueUpdate` | `ThisCall` |
| `Games/Memory.cpp:60-68` | 400188 `GameHeap::Get()` | `ThisCall(HookFormAllocate, GameHeap::Get(), ...)` |

- **问题**：`VersionDbPtr::Get()` 有**三个**失败出口，语义**互不相同**：

| 出口 | 条件 | 返回值 | 调用方安全吗 |
| --- | --- | --- | --- |
| (a) | id 不在 `_data` 里，`aAllowUnresolvedStub == true`（默认） | 7 字节零返回桩 | **安全**（这就是注释承诺的降级） |
| (b) | `GetPtr()` 的 `catch (...)` | **`nullptr`** | 不安全 |
| (c) | `_base == 0`（`VersionDb::Load` 从未成功）→ `FindAddressById` 直接 `return NULL` | **`nullptr`** | 不安全，**且走不到 (a) 的补桩分支** |

也就是说 (b)(c) 两个出口出来的是裸 `nullptr`，而本表**每一个**调用点都把它当函数指针用。
对照证据：同一个仓库对**函数指针**是有正确写法的 —— `HookAudit.cpp:110-116` 判 `!*appTargetSlot`，
`GamePatch.h:298` 的 `SwapCall` 判 `!apAddress`，`GamePatch.h:113` 的 `At()` 判 `!apAnchor`，
`Games/Skyrim/RTTI.h:23` 的 `Cast<>` 判 RTTI 指针，`Services/PapyrusService.h:87/153/188`
三处都判 `if (!m_pFunction)`。**只有「解析后立刻调用」这一族没有。**

- **证据**：

```cpp
// Code/client/VersionDb.h:556-598 —— 出口 (a) 与 (b)
void* GetPtr() const noexcept
{
    try {
        if (m_pPtr == nullptr) {
            m_pPtr = VersionDb::Get().FindAddressById(m_id);
            if (m_pPtr == nullptr && m_allowUnresolvedStub) { m_pPtr = VersionDb::Get().GetUnresolvedStub(); ... }
        }
        return m_pPtr;
    }
    catch (...) { Diagnostics::ReportFailure(__FUNCTION__, "ran out of memory in GetPtr"); return nullptr; }  // (b)
}
// Code/client/VersionDb.h:81-92 —— 出口 (c)：_base 为 0 时连补桩的机会都没有
void* FindAddressById(unsigned long long id) const {
    unsigned long long b = _base;
    if (b == 0) return NULL;
    ...
}
// Code/client/Games/Skyrim/Misc/BSScript.cpp:20-27 —— 典型调用点
void BSScript::Variable::Reset() noexcept
{
    TP_THIS_FUNCTION(TReset, void, BSScript::Variable);
    POINTER_SKYRIMSE(TReset, s_reset, 104296);
    TiltedPhoques::ThisCall(s_reset, this);      // <-- 没有 if (s_reset.Get())
}
// Code/client/Games/Skyrim/Misc/BSScript.cpp:110-114 —— 数据 id 更糟：直接解引用
BSScript::IObjectHandlePolicy* BSScript::IObjectHandlePolicy::Get() noexcept
{ POINTER_SKYRIMSE(BSScript::IObjectHandlePolicy*, s_policy, 414391); return *s_policy.Get(); }
// Code/client/Games/Memory.cpp:60-63
void* Memory::Allocate(const size_t aSize) noexcept
{ return TiltedPhoques::ThisCall(HookFormAllocate, GameHeap::Get(), aSize, 0, false); }
```

- **一句话最小改法**：把 `VersionDbPtr::Get()` 的出口 (b) 与 (c) 也接到零返回桩（一处改动覆盖全表），
  或给这族调用点加一个 `SafeThisCall` 包装，`if (!f) return R{};`。
- **【运行时依赖】判据**：**如果** 启动时 `VersionDb::Load()` 返回 false（`Data/SKSE/Plugins/versionlib-*.bin`
  缺失、被截断、或版本对不上 `Load` 里的 `format`/`ptrSize` 校验），**则** `_base` 保持 0
  （`VersionDb.h:189` `Clear()` 置 0，`:263-264`/`:442-443` 只在成功时写入），
  出口 (c) 对**所有** id 生效，本表每一次调用都跳到 0 —— 而 `VersionDbPtr` 的注释（`:558-559`）
  明确承诺的却是「返回 null，is the same answer as an offset that has no pointer」，
  **注释与实现对调用方的后果不一致**。本机已实测：上述全部 id 都在十张 1.5.x 映射表里，
  所以「id 缺映射」这条**不成立**，成立的只有「库整体没加载 / GetPtr 抛异常」这两条。

---

### WS-B-05 · `IObjectHandlePolicy::Get()` 的两级解引用：外层判了空，内层重新取一次且不判

- **类别**：空指针（可闭合）
- **严重度**：低
- **位置**：`Code/client/Games/Skyrim/Misc/BSScript.h:157`（模板实现）、`BSScript.cpp:110-114`
- **问题**：`GetObjectForHandle<T>(handle)` 的实现里 `IObjectHandlePolicy::Get()` 被**重新调用一次**
  且不判空；唯一的调用者 `ExtractComplexType`（`BSScript.cpp:83-101`）**在上一行判过**
  `if (!pBaseObject || !pPolicy) return nullptr;`，但判的是**另一次** `Get()`（经 `pVirtualMachine->GetObjectHandlePolicy()`）。
  判空与解引用之间隔了一层模板，前一次的非空结论没有传递过来。
  `IObjectHandlePolicy::Get()` 的实现是 `return *s_policy.Get();`，`s_policy` 走 WS-B-04 的出口 (b)/(c)
  时是 `nullptr`，于是这一句既可能是 `nullptr->`（解引用空对象指针），也可能是读 `nullptr` 所指。
- **证据**：

```cpp
// Code/client/Games/Skyrim/Misc/BSScript.h:155-157
virtual void* GetObjectForHandle(uint32_t aType, uint64_t aHandle);
template <class T> T* GetObjectForHandle(uint64_t aHandle)
{ return (T*)(BSScript::IObjectHandlePolicy::Get()->GetObjectForHandle((uint32_t)T::Type, aHandle)); }
// Code/client/Games/Skyrim/Misc/BSScript.cpp:83-101 —— 调用者判过空，但判的是另一次 Get()
auto* pPolicy = pVirtualMachine->GetObjectHandlePolicy();
...
if (!pBaseObject || !pPolicy) return nullptr;
...
return pPolicy->GetObjectForHandle<T>(handle);   // <-- 模板内部重新 Get() 一次，没有再判
// Code/client/Games/Skyrim/Misc/BSScript.cpp:110-114
BSScript::IObjectHandlePolicy* BSScript::IObjectHandlePolicy::Get() noexcept
{ POINTER_SKYRIMSE(BSScript::IObjectHandlePolicy*, s_policy, 414391); return *s_policy.Get(); }
```

- **一句话最小改法**：模板改成
  `auto* p = BSScript::IObjectHandlePolicy::Get(); return p ? (T*)p->GetObjectForHandle(...) : nullptr;`。

---

### WS-B-06 · HookAudit 用**一个字节** `0xE9` 判定「钩子已落地」，安装失败会被报成「与别的 mod 共用」

- **类别**：错误被忽略 / 诊断给出相反结论（可闭合）
- **严重度**：低（不改变行为，但它让**唯一**一条能回答「钩子到底装上没有」的日志说反话）
- **位置**：`Code/client/HookAudit.cpp:200-235`（`Report`）、`:255-278`（`Verify`）
- **问题**：判定链是「现在首字节 == `0xE9` ⇒ 我们的跳转在；否则 `missing++`」，而 `before[8]`
  （安装**之前**的字节）只在**之后**才用于区分「原本就有分支」。于是当目标**原本就**是别人写的
  `E9` / `FF 25` 而安装**失败**（trampoline 分配失败、目标页不可写、并发写入竞争）时：
  `now[0] == 0xE9` 成立 ⇒ 不计入 `missing`；`before[0]` 也是分支 ⇒ 计入 `shared` ⇒
  日志说「另一个 mod 也钩了这里，我们的盖在上面」。**真实情况是我们的钩子根本没装，那个 `E9` 是别人的。**
  这个盲区正落在该文件自述要解决的场景上（`HookAudit.h:5-11`：「A hook that never lands is silent」）。
  同一判据也用在 `Verify`：别人后来把我们的跳转覆盖成另一条 `E9` 时，`Verify` 认为「还在」。
- **证据**：

```cpp
// Code/client/HookAudit.cpp:200-221
uint8_t now[8]{};
SafeReadCode(now, recorded.pTarget, sizeof(now));
// MinHook redirects a function by writing a 5 byte relative jump over its first bytes,
// so finding that jump is the proof the hook landed
if (now[0] != 0xE9)                       // <-- 只看首字节
{ missing++; spdlog::error(...); continue; }
const bool wasBranch = recorded.before[0] == 0xE9 || recorded.before[0] == 0xE8 ||
                       recorded.before[0] == 0xEB ||
                       (recorded.before[0] == 0xFF && recorded.before[1] == 0x25);
if (!wasBranch) continue;
shared++;                                 // <-- 原本就有分支 ⇒ 一律算「共用」，不排除「我们没装上」
// :262-263 Verify 同一判据
if (now[0] == 0xE9) continue;
```

- **一句话最小改法**：`Report`/`Verify` 里把 `now` 的 rel32 解出来（`BranchTarget(now, target)`
  已经现成，见 `:74-101`），与钩子函数地址比对，只有落到我们的钩子才算「已落地」。

---

## 【运行时依赖】

---

### WS-B-07 · `ExtractComplexType` 拿到越界类型标签后，会把不受控的 handle 送进三次虚调用

- **类别**：越界读（运行时依赖）/ 对 WS4 「`GetObject` 越界穿透」结论的**新证据补充**
- **严重度**：低
- **位置**：`Code/client/Games/Skyrim/Misc/BSScript.cpp:69-81`（`GetObject`，WS4 已报过本身）
  → `:83-101`（`ExtractComplexType`，**本轮新增的是它下游去哪**）
- **问题**：WS4 记录的是「`GetObject()` 对越界非数组标签穿透返回指针」。本轮补的是**落点**：
  `ExtractComplexType` 拿到这个非空指针后立刻 `uint64_t handle = pBaseObject->GetHandle();`，
  再 `pPolicy->HandleIsType(...)` → `IsHandleObjectAvailable(...)` → `GetObjectForHandle<T>(handle)`，
  **三次虚调用全部吃这个 handle**。所以「越界标签」不只是读到一个错指针，而是把一个不受控的 64 位值
  交给引擎的 handle 策略。`IsRemotePlayerFunc`/`IsPlayerFunc` 的 `MarshallAndDispatch`
  （`:302`/`:346`）就站在这条链的入口，每个出货 `.psc` 激活点都会进来。
- **证据**：

```cpp
// Code/client/Games/Skyrim/Misc/BSScript.cpp:69-81（WS4 已报）
BSScript::Object* BSScript::Variable::GetObject() const noexcept
{
    if (type > Type::kMax) { bool isArray = type & 1; if (isArray) return nullptr; }
    else if (type != Type::kObject) return nullptr;
    return data.pObj;            // <-- 越界且非数组 ⇒ 直接吐指针
}
// Code/client/Games/Skyrim/Misc/BSScript.cpp:83-101（本轮新增：下游）
auto* pPolicy = pVirtualMachine->GetObjectHandlePolicy();
BSScript::Object* pBaseObject = GetObject();
if (!pBaseObject || !pPolicy) return nullptr;
uint64_t handle = pBaseObject->GetHandle();
if (!pPolicy->HandleIsType((uint32_t)T::Type, handle) || !pPolicy->IsHandleObjectAvailable(handle))
    return nullptr;
return pPolicy->GetObjectForHandle<T>(handle);   // <-- 三次虚调用都吃这个 handle
```

- **一句话最小改法**：`GetObject()` 的 `if (type > Type::kMax)` 分支里，非数组一律 `return nullptr;`。
- **【运行时依赖】判据**：**如果** VM 确实能把 `Variable::type` 写成 `> kMax(16)` 的偶数（`kMax` 之外的
  数组标签在 `components/es_loader` 侧**确实存在**，见 PITFALLS §148 的两套枚举），**则**
  `IsRemotePlayer(x)` 会把垃圾 handle 交给 handle 策略，崩溃点在引擎虚函数里、不在本仓库；
  **如果** 引擎保证 `type` 只在 0..16 之间，则本条不成立。

---

### WS-B-08 · `Memory::Allocate` / `Memory::Free` 依赖 `GameHeap::Get()` 非空

- **类别**：空指针（运行时依赖）
- **严重度**：低
- **位置**：`Code/client/Games/Memory.cpp:60-68`（`GameHeap::Get()` 在 `:13-18`，id 400188）
- **问题**：`GameHeap::Get()` 返回 `s_gameHeap.Get()`，即 `VersionDbPtr` 的结果，可能是 `nullptr`
  （WS-B-04 出口 (b)/(c)）；`Memory::Allocate` 把这个值当 `__fastcall` 的 `this` 用，`Memory::Free` 同。
  id 400188 在十张 1.5.x 表里都在，「id 缺映射」这条**已排除**，只剩出口 (b)/(c)。
- **证据**：

```cpp
// Code/client/Games/Memory.cpp:13-19
struct GameHeap { static GameHeap* Get() { POINTER_SKYRIMSE(GameHeap, s_gameHeap, 400188); return s_gameHeap.Get(); } };
// Code/client/Games/Memory.cpp:60-68
void* Memory::Allocate(const size_t aSize) noexcept
{ return TiltedPhoques::ThisCall(HookFormAllocate, GameHeap::Get(), aSize, 0, false); }
void Memory::Free(void* apData) noexcept
{ TiltedPhoques::ThisCall(RealFormFree, GameHeap::Get(), apData, false); }
```

- **一句话最小改法**：`auto* pH = GameHeap::Get(); if (!pH) return nullptr;`
- **【运行时依赖】判据**：**如果** 地址库未加载或 `GetPtr()` 走了 catch，**则**
  `Actor::New()`（`Actor.cpp:544` `Memory::Allocate<Actor>()`）在分配第一步就跳 0。

---

### WS-B-09 · `Memory::Free` 与 free 侧钩子的不对称（记录，**非缺陷**）

- **类别**：观察项
- **位置**：`Code/client/Games/Memory.cpp:22,24-25,65-68,141`
- **内容**：`RealFormFree` 被赋值（`:141`）但**从未被 `TP_HOOK`/`TP_HOOK_IMMEDIATE` 安装**；
  `TFormFree`/`HookFormFree` 只有声明（`:22`）没有定义也没有安装点。free 侧**没有**与 allocate 侧对称的
  尺寸镜像逻辑。**不判为缺陷**：尺寸替换发生在 allocate 侧，free 只需把原指针交回同一个堆。
  列在这里是因为「allocate 有钩子、free 没有」是下一个读这段代码的人第一个会问的问题。

---

## 已确认无问题的关键不变量

逐条查过、**当前代码成立**，供后续改动作参照：

1. **`HookAudit::Add` 对「未解析目标」确实 fail-closed**：`HookAudit.h:37-38` `if (!Record(...)) return;` ——
   目标槽为 `nullptr` 或等于零返回桩时**不安装**，且已在 `HookAudit.cpp:110-130` 打了两条不同的 `error`
   （空目标 / 打到桩上）。这是 `HookAudit` 唯一一处「失败不静默」的路径，与 WS-B-04 形成对照。
2. **`HookAudit::Record` 分配失败时 fail-open 是设计**：`HookAudit.cpp:163-170` 返回 `true` 并
   `Diagnostics::ReportFailure`，注释写明「宁可漏审计也不要丢能用的钩子」。不是缺陷。
3. **`SafeReadCode` 的 SEH 用法与缓冲区尺寸都正确**：`HookAudit.cpp:27-39` 的
   `memcpy(apDst, acpSrc, aLen)` 在 `__try` 内、`__except(EXCEPTION_EXECUTE_HANDLER)` 复位；
   所有调用点传的都是 `sizeof(recorded.before)` / `sizeof(now)`（8），没有越界。
   `GamePatch.h:366-382` 与 `GamePatch.h:439-446` 的同形状读取也正确。
4. **`HookAudit` 里所有定长缓冲的写入都带显式长度**：`FormatId` 的 `char[32]`、
   `FormatModuleOffset` 的 `char[MAX_PATH+48]` 全部走 `sprintf_s(aBuf, sizeof(aBuf), ...)`
   （`CrashHandler.cpp:42/47`）或 `strcpy_s` 模板重载（`HookAudit.cpp:226/271`），
   没有裸 `strcpy`/`sprintf`。**检查点 2（定长数组写入是否查长度）在本范围内无发现。**
5. **`Actor::casters[4]` / `magicItems[4]` 的下标全部有界**：
   `MagicSystem::CastingSource` 取值 `LEFT_HAND=0..INSTANT=3` + `CASTING_SOURCE_COUNT=4`
   （`Games/Magic/MagicSystem.h:6-13`），数组长度正好 4。全仓访问点 6 处：
   `Actor.cpp:1044-1045`（`for (i = 0; i < 4)`）、`Actor.cpp:847-851`（常量 `[0]`/`[1]`）、
   `MagicService.cpp:178/185`（常量 `CS::LEFT_HAND`）、`MagicService.cpp:195`（**先判**
   `acMessage.CastingSource < 0 || >= 4`，见 `:187-195`）、`PlayerService.cpp:245-246` 与
   `PlayerDebugView.cpp:30-31`（常量 0/1）。**没有一处用未校验的外部整数当下标。**
   （`MagicService.cpp:185` 那条「未映射的远程法术把占位 caster 变成空引用」属于另一族 id 缺失的后果，
   不在本任务的文件范围，本轮不记。）
6. **`GamePatch::Jump`/`PutCall` 的写前校验已经补齐**（对照 WS2 F1）：`GamePatch.h:462-468`/
   `:504-508` 都先 `ValidatePatchSite` + `InstructionCovers`，`WriteBytes` `:141` 判
   `!apAddress || acSize == 0`。**WS2 那条「无校验」在今天代码上已不成立**；剩下的只是
   `InstructionCovers` 在**没装解码器**时返回 `true`（`GamePatch.h:427-433`，注释已自述）。
7. **`BSScript::NativeFunctionBase::GetVarNameForStackIndex`（`BSScript.cpp:215-230`）同时判了
   `parameters.data` 与 `aIndex < parameters.size`**，与同文件 `GetParam`（`:141-154`）一致，
   没有按 `capacity` 越界读。
8. **三个 Papyrus native 的参数个数 / 类型 / 下标三者一致**（**检查点 4**）：
   注册参数个数 1/1/0（`BSScript.cpp:83-85` 的构造实参）与 `FunctionType`
   （`bool(Actor*)` / `bool(Actor*)` / `bool()`，`BSScript.h:309/318/327`）以及
   `MarshallAndDispatch` 里取的变量下标（0/0/无，`BSScript.cpp:297/341/369`）**一一对应**；
   `PAPYRUS_FUNCTION` 的 ABI 形状 `(vm, stackID, self, args...)` 与 `PapyrusService.h:75/143/178`
   三个模板一致。`MarshallAndDispatch` 在 `pArg1` 判空之后才解引用（`:302-313`/`:346-353`），
   PITFALLS §150.2 的修法仍在。唯一的签名来源缺口（`Reset`/`GetAnimationVariableInt`）已由 §161.4 记录。
9. **`Overrides.h`/`Overrides.cpp` 的 `thread_local` 引用计数是平衡的**：构造 `s_refCount++`、
   析构 `s_refCount--`（`Overrides.h:5-7`），十个实例各有独立 `thread_local` 定义
   （`Overrides.cpp:5-14`，与 `Overrides.h:48-57` 的 `using` **10/10 一一对应**）；
   `ScopedOverride` 禁拷贝禁移动（`:9`），不存在「复制后减两次」。唯一可记的是
   `uint32_t` 在 `IsOverriden()` 里按 `> 0` 判：计数若下溢到 `UINT32_MAX` 会**永久为真**，
   而这只在「析构多于构造」时发生，当前没有任何路径能做到。
10. **`FormatModuleOffset`（`CrashHandler.cpp:31-48`）不依赖模块列表**：`GetModuleHandleExW`
    直接从地址反查模块，失败才写 `unmapped 0x...`；`HookAudit::Record` 传入的
    `char[MAX_PATH+48]` 与形参 `char (&)[MAX_PATH+48]` 精确匹配。
11. **`VersionDb::FindIdByAddress`（`VersionDb.h:105-113`）对 `_base == 0` 有保护**，且
    `addr - _base` 只可能是模块内偏移 —— 本范围内唯一调用点是 `HookAudit::FormatId`，
    实参是**钩子目标**（一定在 `SkyrimSE.exe` 内），不会下溢；`BranchTarget` 的结果只拿去
    `FormatModuleOffset`，不经过 `FindIdByAddress`。（`CrashHandler` 内的崩溃地址不满足这个前提，
    但那份代码不在本范围。）
12. **`HookAudit::BranchTarget`（`:74-101`）的两条读路径都判了长度**：`E9` 读 4 字节位移、
    `FF 25` 经 `SafeReadCode` 读 8 字节目标，都对不上就返回 0；调用方（`:227`、`:272`）
    用 `if (const auto previous = BranchTarget(...))` 把 0 当「跟不下去」处理，没有解引用 0。

---

## 未覆盖区域

1. **`Code/client/Games/Skyrim/` 下除 `Actor.cpp`、`TESObjectREFR.cpp`、`Misc/BSScript.cpp` 之外的
   全部文件**（`Magic/`、`Animation/`、`Skyrim/` 其余、`Skyrim/Misc/` 其余）。它们里面的钩子体
   是否同样在「已判 `formType` / 已 `Cast<>` 之后」就直接用 `GetExtension()`，本轮**未统计**。
   按本报告 WS-B-01 的机制，这一族是同一形状，下一轮应按形状扫而不是按文件读。
2. **`TiltedPhoques::FunctionHookManager::Add` 的返回值与语义**：`Libraries/TiltedCore` 在本机是空
   submodule，读不到 `Add` 是否返回 bool、是否抛异常、trampoline 分配失败怎么表现。这直接决定
   WS-B-06 的严重度上限（若 `Add` 内部已 log，本条只是「多一条说反话的日志」），
   **也决定 WS-B-01 里「扩容是否 100% 覆盖」这一条能否判定**。
3. **`HookFormAllocate` 是否覆盖全部 actor 分配路径**（WS-B-01 的判据）：本机无法构建、无游戏可跑，
   只能从代码读出「它按精确尺寸 `switch`」。引擎是否存在其它尺寸的 actor 分配、以及钩子安装时机
   与引擎早期 actor 构造的先后，**本仓库无证据**。这是 WS-B-01 无法升级为纯静态结论的唯一原因。
4. **MinHook 的部分安装语义**：`GamePatch.h:398-408` 自述「没有指令长度解码器」，
   `SetInstructionLengthDecoder` 在全仓**没有调用者** ⇒ `InstructionCovers` 永远走
   `if (!decoder) { warn; return true; }` 分支，这一段今天等于不设防。「截断指令的具体后果」
   属于 WS2 F1 已记录条目，本轮不重复展开。
5. **`VersionDb::Load` 全路径**：只读了 `_base` 的两处赋值（`:263-264`、`:442-443`）与失败返回点，
   没有逐行审 format 1/2/5 三个解析分支的边界（PITFALLS §123 已记录其中的族选择问题）。
6. **`Services/PapyrusService.h` 的 `LatentPapyrusFunction`**：WS4 已判「零调用点」，本轮只确认了它与
   `PapyrusFunction`/`GlobalPapyrusFunction` 三者的 ABI 形状差异（多一个 `RefrOrInventoryObj` 按值），
   没有再去数调用点。
7. **`Actor::Save_Reversed`（`Actor.cpp:88-128`）与 `TESObjectREFR` 的 `SAVE_STUFF` 段**：
   整段在 `#ifdef SAVE_STUFF` 内，本机无构建无法确认该宏是否开启（按当前命令行推断**未开启**），
   故只做了「写入都是固定 4 字节、无长度字段」的形状核对，未深究。
8. **建议下一轮的判据**：把 `Actor::GetExtension` 加进 `Tools/Scripts/check_null_accessors.py`
   的 `NULLABLE_ACCESSORS` 并不能解决 WS-B-01（它不是「可能为空」而是「可能越界」）。
   真正该加的是**形状判据**：「`Cast<Actor>` 或 `formType == Actor::Type` 之后直接用
   `GetExtension()` 的站点，必须与 `HookFormAllocate` 的尺寸清单对账」。按本轮手工统计，
   `Actor.cpp` 内 `GetExtension()->` 直接解引用 **10 处**、先存局部变量再不判空解引用 **5 处**，
   `TESObjectREFR.cpp` 内 **4 处** —— 合计 19 处，每一处都需要一条「这个对象一定经过
   `HookFormAllocate` 的两个尺寸分支」的理由。

---

## 计数

- **可闭合**：6 条（WS-B-01~06）；最严重 **WS-B-01（高）**、**WS-B-04（高）**。
- **运行时依赖**：3 条（WS-B-07~09）；最严重 WS-B-07（低，且是对 WS4 结论的补充）。
- **12 条已确认为不是缺陷的不变量**，其中包含检查点 2（定长数组写入）与检查点 4（Papyrus 参数个数）
  的否定结论 —— 这两项在指定范围内**没有发现缺陷**，理由见上。