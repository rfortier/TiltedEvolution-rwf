# 更新日志

本项目的更新记录以 **GitHub Releases** 为准:

> https://github.com/komAAmok/SkyrimTogetherNext/releases

每个 tag(形如 `v1.0.20`)对应一个 Release,附上版本号相同的两个包——
客户端 mod 与专用服务器。逐条提交的历史见 `git log` 与各次 PR。

## v1.1.9(2026-10-01)

### 修复:经验值只挡了「非有限」,没挡**量级** —— 而它会被另一个玩家的引擎直接吃下

**怎么找到的**:按 §128 的判据 —— **「这个值的来源可不可控」**,
而不是「它是不是别人发来的」。于是找**累加器**(只增不减、且值会喂给比较或索引的成员):

```text
m_cachedExperience += acEvent.Experience;     <- CharacterService.cpp:1281
data.m_timer       += acUpdateEvent.Delta;    <- 上一轮已修
m_fadeTimer        += updateDelta;            <- CalendarService.cpp:114
```

**`m_cachedExperience` 是这一族里最特别的一个**:它是**本地测量出来的数**,
却会被**另一个玩家**的引擎直接吃下。

链路逐段查过:

```text
PlayerCharacter.cpp:245   float deltaExperience = newExperience - oldExperience;   <- 本地测量,无界
        ↓ AddExperienceEvent
CharacterService.cpp:1281 m_cachedExperience += acEvent.Experience;               <- 累加,无界
        ↓ SyncExperienceRequest(每 4s 发一次)
服务端 CharacterService.cpp:643  notify.Experience = acMessage.Packet.Experience;  <- 原样转发
        ↓ NotifySyncExperience
接收方 CharacterService.cpp:1300 pPlayer->AddSkillExperience(..., acMessage.Experience);  <- 进引擎
```

**而线上只挡了「非有限」,没挡量级**:

```cpp
Experience = WireLimits::ClampFinite(Serialization::ReadFloat(aReader));
```

`ClampFinite` 把 NaN/Inf 换成 0,但**一个 `1e30` 会原样通过**。

**为什么量级才是问题**:引擎把技能已存 xp 当成一个**浮点**,
再拿它与**每级的阈值**比较。一个它**没法比较**的值不会「让技能升很多级」——
它会让那个技能**进入一个后续任何奖励都修不回来的状态**。

**而这个测量本身可以出错,且对端拦不住**:

- 一次奖励把技能的浮点 xp 推到溢出;
- 两次读取之间发生**读档/存档切换**,让那个减法**失去意义**。

两者都不是对端能做对什么就能避免的 —— 所以**界必须加在发送侧**,
而不是只加在接收侧。

**修法**:在 `WireLimits.h`(该仓库放这类常量的地方)新增
`kMaxExperienceDelta = 100000.f` 与 `ClampExperienceDelta()`,并在**四处**应用:

| 位置 | 为什么 |
| --- | --- |
| `CharacterService::OnAddExperienceEvent` | **本地测量变成别人游戏里的数**的地方 —— 加在入口 |
| `SyncExperienceRequest::DeserializeRaw` | 线上读入 |
| `NotifySyncExperience::DeserializeRaw` | 线上读入 |
| `CharacterService::OnNotifySyncExperience` | **进引擎前的最后一点** —— 纵深防御 |

**为什么是 100000**:技能整条进度线是**每级几百点**,单次奖励是**不到一级**,
所以这个数**远在任何真实奖励之上**;它与 `kMaxHealthDelta` **取同一个数**,
因为两者都是「引擎自己的尺度,宽裕地加上界」而不是游戏常量。

**是区间截断而不是下限**,因为**负的经验是有意义的**(技能可以掉进度)。

**新增单元测试** `[encoding.experience_delta]`(本轮三个新用例之一:测试套件 **22 → 25**):

| 断言 | 为什么 |
| --- | --- |
| `0` / `1` / `±12.5` **原样通过** | 否则这个界会改变**正常游玩** |
| 边界两侧、**正负都测** | 这是区间截断,不是下限 |
| `±kMax * 2` 截到 `±kMax` | 越界被限住 |
| `float::max()` → `kMax` | 注释里点名的溢出情形 |
| NaN / ±Inf → **0** | 与 `ClampFinite` 同理:NaN 会在下游每次比较里存活 |
| `kMax > 1000` | 界要**宽裕**,否则会削掉真实奖励 |
| 线上往返:写入 `float::max()` 后读回 == `kMax`;再写 NaN 读回 == `0` | **线上那一层也真的截断了** |

最后一条是**线上往返测试**而不只是常量测试 —— 它证明的是
「接收方拿到的值已经被截断」,而不是「那个函数本身没问题」。

### 修复:每帧的 delta **没有上界** —— 一次挂起就把一个巨大的时间差发给每一个订阅者

**怎么找到的**:查服务端的主循环(`GameServer::OnUpdate`)。它测的是**真实经过时间**:

```cpp
const auto cDelta = cNow - m_lastFrameTime;
const auto cDeltaSeconds = std::chrono::duration_cast<...>(cDelta).count();
dispatcher.trigger(UpdateEvent{cDeltaSeconds});
```

而**客户端的 `World.cpp` 是同一个形状**(只挡下界,不挡上界):

```cpp
// 一次被跳过的调用不会推进 m_lastFrameTime,所以下一次被接受的调用
// 测到的是「距上一次真正更新的整段时间」。
if (m_hasTicked && cDelta < std::chrono::milliseconds(8))
    return;
```

**注释自己说出了机制**:被跳过的调用**不推进** `m_lastFrameTime`,
所以下一次测到的 delta **跨越整个间隔**。

**而真实时间的测量不会因为进程内卡住而变小,只会变大**:
挂起机器、在调试器里断下、一次长页错误、一次 hitch —— 都会在下一 tick
**送来一个巨大的值**。服务端的循环是 **`while` 里没有任何 sleep**
(`DediRunner::RunGSThread`),所以它完全取决于这两次迭代之间真实过了多久。

**先核对「仓库里有没有已有的判据」**(§111/§114 的做法):

| 查法 | 结果 |
| --- | --- |
| 全仓搜 `std::min`/`clamp` 作用在 delta 上 | **0 处** |
| 全仓搜 `kMaxDelta`/`kMaxFrame` 类常量 | **0 处** |
| 而同一仓库对**线上来的浮点**有整套约定 | `WireLimits::ClampFinite`、`kMaxHealthDelta`、`TimeModel::kMaxTimeScale` |

也就是说:**这个仓库已经建立了「外部/不可控的数值要有界」的约定,
而 delta 是唯一漏掉的那个** —— 而它比线上来的值更不可控(它来自真实时间)。

**消费方**(逐个查过):

| 消费方 | 怎么用 delta |
| --- | --- |
| `CharacterService::ApplyCachedWeaponDraws` | `data.m_timer += acUpdateEvent.Delta;` 再与 **0.5s / 2.0s** 比较 |
| `CalendarService::HandleUpdate` | 读 delta,但实际推进用的是 `GetTick()` 差值(**安全**) |
| `RunnerService::OnUpdate` | 忽略 delta |
| 服务端 `ScriptService::OnUpdate` | `CallEvent("onUpdate", acEvent.Delta)` —— **原样交给 Lua** |

**修法**:在 `WireLimits.h`(该仓库放这类常量的地方)新增
`kMaxFrameDeltaSeconds = 1.0` 与 `ClampFrameDelta()`,
并在**两处测量点**(客户端 `World.cpp`、服务端 `GameServer.cpp`)应用。

**上界取 1 秒的理由**:

- **在正常服务端上不可能触发** —— 客户端最快 8ms 一 tick,服务端循环无 sleep,诚实的一 tick 是毫秒级;
- **1 秒正是消费方思考的单位** —— 武器拔出计时器等 0.5s / 2.0s,日历按游戏分钟推进;
  所以被截断后的值仍然意味着「大约过了一秒」,而不是「什么都没发生」。

**截断而不是丢弃**是有意的:每个订阅者**本来就已经容忍漏 tick**
(客户端的 `m_hasTicked` 路径、服务端的事件循环都会丢工作),
而**丢掉整个 update 会跳过 runner 的 drain**,那正是排队工作所依赖的。

**顺带处理**:非有限值与负数一律归零 —— 与 `ClampFinite` 存在的理由相同
(NaN 会在下游每一次比较里存活),而「时钟倒退」不是负的经过时间。

**新增单元测试** `[encoding.frame_delta]`(本轮三个新用例之一:测试套件 **22 → 25**),
按仓库既有风格逐条断言:

| 断言 | 为什么 |
| --- | --- |
| 8ms / 100ms / 0 **原样通过** | 否则这个界会改变**每个**正常服务端的行为 |
| 边界两侧 | 1.0 通过,1.0001 截到 1.0 |
| 30s / 7200s 截到 1.0 | 挂起被限住 |
| `kMaxFrameDeltaSeconds < 2.0` | 否则武器拔出计时器的 2.0s 窗口就失去意义 |
| NaN / ±Inf / -5.0 → **0.0** | 坏时钟读作 0,而不是送进下游 |
| `kMaxFrameDeltaSeconds > 0` | 界本身是正的时长 |

### 修复:安装器元数据里的版本号**落后四个版本** —— 而它自己的注释就写着「要与最新 tag 保持一致」

**怎么找到的**:按 §126 的新判据问 ——**「这个仓库里还有哪些机器可读的规则文件,
而没有任何门禁读它?」** 逐个查 manifest,其中一个引出了这条线:

```text
Code/plugins/plugins.json          <- 被 5 处读,其中 check_ostimng_build.py 是门禁
Code/plugins/tools/merge_fomod.py  <- 由它生成向导 XML,有 check 模式且已接进 CI
GameFiles/Skyrim/fomod/info.xml    <- MO2 向导顶部显示的元数据
```

`merge_fomod.py check` 跑通了(`FOMOD OK … 55 folder sources`),
`plugins.json` 的内部交叉引用也逐条查过(7 个插件 + 1 个前置,
`requires` 指向存在,8 个子模块目录都在)。**这些都正确。**

**但 `info.xml` 里有一处**:

```xml
<!-- The Version element is stamped from the release tag while the release zip
     is assembled (release.yml); the number below is only what a hand-made
     package shows. Keep it in step with the newest tag. -->
<Version>1.1.5</Version>
```

**它自己的注释写着「要与最新 tag 保持一致」** —— 而本仓库最新的 tag 是 **v1.1.9**。
**落后四个版本。**

**为什么这条算缺陷,而不是「反正发布时会被 stamp」**:

- 注释自己说了 stamp 只在**发布 zip 组装时**发生,所以**手工打包**用的是这个数;
- 而**手工打包是这个仓库支持的路径**(`Tools/Packaging/New-STNModPackage.ps1`
  就接受 `-Version` 参数,不传就用文件里的值);
- 更直接的是:**任何读这个文件来判断「这个包是哪个版本」的人,会得到四个版本前的答案**。
- 而「保持一致」这句话**只是注释 —— 注释不会运行**。

**修法**:版本号改成 `1.1.9`,并把「它曾经落后四个版本」这件事写进注释 ——
因为注释的下一句现在指向了门禁(`check_package_metadata.py`),
**让下一个人知道这句约定是被检查的,而不是被期望的**。

**新增门禁** `Tools/Scripts/check_package_metadata.py`,三条:

1. **版本号等于本仓库自己那条 tag 序列的最新值**。
   注意**不能取「全局最新 tag」**:tag 命名空间与上游共享
   (上游发 v1.0.x…v1.8.x,而本仓库 merge 了那些提交),
   所以本地 clone 里**两家的 tag 都在**。这条与 `check_docs.py` 的
   `OWN_TAG_SERIES` 是**同一条判断**,沿用同一个序列(`1.1.`)与同一个理由。
2. **描述里必须列出运行时真正接受的游戏版本**(`1.5.x / 1.6.x / 1.7.x`,
   即 `Code/client/main.cpp` 那个前缀门禁的三项),**且不得声称运行时拒绝的版本** ——
   这是**玩家在安装前读到的那句兼容性声明**,而它是散文,没有别的东西查它。
3. **向导要显示的字段非空**(`Name` / `Author`),因为空字段正是这个文件当初被加进来的原因。

**变异测试**,四种形状各自被精确指名:

| 变异 | 门禁输出 |
| --- | --- |
| **还原原始缺陷**(版本改回 `1.1.5`) | `says version 1.1.5 but the newest tag of this repository's series is v1.1.9. A hand-made package, or anyone reading the file, then gets a version from an older release` |
| 从描述里去掉 `1.6.x` | `does not name the supported game version prefix(es) ['1.6.']; the runtime accepts ['1.5.', '1.6.', '1.7.'], so a player on one of them is told the package is not for them` |
| 声称支持 `1.9.x` | `claims support for ['1.9'], which the runtime's version gate does not accept (['1.5', '1.6', '1.7'])` |
| 把 `Author` 置空 | `has no <Author>; the wizard shows that field blank` |
| 未变异的对照组 | **通过** |

第二条与第三条是**同一个兼容性声明的两个方向**:
少了会漏掉一个版本,多了会承诺一个跑不起来的版本。

### 修复:契约门禁只覆盖 **6 个消费者,而契约自己列了 7 个** —— 并且只认一种调用约定拼写

**怎么找到的**:查 `Code/plugins/STRPM/` 时发现一个此前没注意的文件:**`contract.json`**。
它是一份**机器可读的契约清单**,包含:

```json
{
  "header": "Code/plugins/STRPM/include/.../STRPluginMessagingAPI.h",
  "headerSha256": "8fdcf481…",
  "interfaceVersion": 2,
  "exportNames": [...],
  "interfaceConstants": { ... },
  "interfaceTypes": [...],
  "consumers": { "AnimSyncTogether": {...}, ..., "TradeTogether": {...} }
}
```

**它列了 7 个消费者**,而 §120 加的门禁**硬编码了 6 个副本**。
逐条对照后,**两个消费者从未被任何门禁读过**:

| 消费者 | 它 vendor 的文件 |
| --- | --- |
| `MorphSyncTogether` | `plugins/MorphSyncTogether/src/STRPMCompat.h` |
| `OStimTogether` | `plugins/OStimTogether/src/STRPMApi.h` |

**这正是 §113/§116/§119 那个错误的第四次**:清单写在发现地点,而不是按规则搜出来。
**而这次连「规则」都早就写在仓库里了** —— `contract.json` 就是那份规则,
我上一轮却**手写了一份清单**。

**第二处缺陷**:门禁的成员提取正则只认 `STRPM_CALL` 这一种拼写:

```python
re.finditer(r'STRPM_CALL\s*\*\s*(\w+)\s*\)', body)
```

而 `OStimTogether` 直接写 **`__cdecl`**。在 x64 上两者**完全等价**
(`STRPluginMessagingAPI.h:7` 就是 `#define STRPM_CALL __cdecl`),但正则匹配不到 ——
于是那个文件的**函数指针成员会被数成 0**。
**「数成 0」比「数错」更危险**:0 个成员意味着**没有东西可比对**,
门禁会**通过**。这是**假通过**,不是假失败。

**修法**:三条,都是「让规则来自契约而不是手写」:

1. **消费者清单改为从 `contract.json` 读**(`copies()` 函数),
   正本用 `header` 字段,其余用每个消费者的 `root` + `vendored`;
2. **调用约定宏与直接拼写都认**(`STRPM_CALL|__cdecl|__fastcall|__stdcall`);
3. **新增 `headerSha256` 校验** —— 契约公布了正本的哈希(供消费者判断自己
   vendor 的是不是同一修订版),而**没有任何东西查它**。

**覆盖从 6 个副本 / 20 个布局升到 8 个副本 / 25 个布局。**

**顺带查清的两件事(都不是缺陷)**:

- **`headerSha256` 一开始报不匹配,查清后是行尾问题**:记录的是 **LF 形式**
  (7,424 字节,即 git 存的形式),而工作区在 Windows 上是 **CRLF**(7,686 字节)。
  两者内容相同。所以门禁**按 LF 归一化后再算哈希**,并在 `--list` 里
  **同时打印三种哈希**(记录值 / LF 形式 / 磁盘原样),让归一化**看得见**而不是隐含。
- **`OStimTogether` 用 `__cdecl` 而正本用 `STRPM_CALL` 不是 ABI 差异** ——
  x64 上 `STRPM_CALL` 就是 `__cdecl`。

**还有一处必须记的自我纠正**:加宽覆盖后门禁**报了 4 条错,全是假阳性** ——
它要求每个消费者声明**全部**常量与类型。而消费者**只声明它用到的**是合法的:
契约说明哪些结构体**存在**,不是每个插件都用全部。
核对确认那两个消费者**从未提到**那些名字(`TransportInterface`、`RuntimeStatus`、
`kTransportInterfaceVersion` 等)。所以规则改成:
**用到却没声明**才报错,**没用到就不要求** ——
否则「把能工作的代码报成坏的」,而假阳性会让判据被放弃(§115.3)。

**变异测试**:三个变异**都打在上一版从不读的那两个副本上**:

| 变异 | 门禁输出 |
| --- | --- |
| 在 `OStimTogether` 里交换两个成员 | `OStimTogether/Interface declares 8 member(s) but the runtime copy declares 7: … Every call after the difference dispatches through the wrong slot` |
| 在 `MorphSyncTogether` 里改常量 | `MorphSyncTogether sets kInterfaceVersion = 9 but the runtime copy sets 2` |
| 把 `headerSha256` 改成全 0 | `contract.json records headerSha256 000… but the canonical header hashes to 8fdcf481… (LF form). A consumer cannot tell which revision it vendored` |
| 未变异的对照组 | **通过** |

### 核对:加载路径的其余部分**经查无缺陷** —— 三项形似缺陷实则不是,以及两条门禁范围正确

本轮把 §123/§124 那条加载路径**走完**,并把「形似缺陷」的三处**逐条查清**。
**没有发现新缺陷** —— 这一节记录的是**核对结果**,而不是修复。
之所以值得记:这三处的形状都像 bug,**只有查过才能说不是**。

**① `_base = GetModuleHandleA(NULL)` 在启动器模式下对不对?**

`VersionDb.h` 在三处把镜像基址设成 `GetModuleHandleA(NULL)`。
而启动器是**手动映射** exe 的(`ExeLoader::Load`),于是问题很具体:
手动映射的镜像**不是**进程的 exe 模块,那 `GetModuleHandleA(NULL)` 会不会返回**启动器**的基址,
从而让每个地址都算错?

**查法**:读 `ExeLoader::Load` 的实现。

```cpp
bool ExeLoader::Load(const uint8_t* apProgramBuffer)
{
    m_pBinary = apProgramBuffer;
    m_moduleHandle = GetModuleHandleW(nullptr);      // <- 就是启动器自己
    ...
    auto* sourceNtHeader = GetTargetRVA<IMAGE_NT_HEADERS>(...);   // 启动器自己的头
```

**结论:不是缺陷。** 启动器是**就地覆盖自己的镜像头**
(`GetTargetRVA` = 启动器自己的 RVA),所以**启动器模块就是游戏镜像**,
两种启动方式下 `GetModuleHandleA(NULL)` 都返回正确的基址。
这个设计正是为了让「基址从哪来」这件事**只有一个答案**。

**② `LoadFormat5` 按固定 64 字节读模块名,而 format 1/2 按长度前缀读 —— 是不是抄错了?**

两种格式读模块名的方式**不同**,看起来像其中一处抄错。

**查法**:直接读文件头。`versionlib-1-7-104-0.bin`(2,263,132 字节):

```text
format=5  version=1.7.104.0
bytes 20..84 = "SkyrimSE.exe" + 52 个 \0      <- 固定 64 字节,零填充
ptrSize@84=8   reserved@88=0   count@92=565759
96 + 4*count = 96 + 2263036 = 2263132 == 文件大小   <- 精确吻合
```

**结论:不是缺陷。** format 5 的模块名**确实是固定 64 字节零填充**,
而 format 1/2 确实是长度前缀 —— 两种格式在这件事上本来就不同。
**用文件本身的算术验证**(`96 + 4*count == filesize`)比读代码更硬。

**③ `FileVersion.inl` 是不是有两个文件?**(上一轮遗留的疑问)

`Launcher.cpp` 写 `#include "Utils/FileVersion.inl"`,而 git 跟踪的是小写
`utils/FileVersion.inl`。**查法**:`git ls-files` 只有一个,
`Get-ChildItem -Recurse` 也只有一个 —— Windows 大小写不敏感,两边指同一个文件。

**④ 两条门禁的范围按 §119 规程核对过,都正确**

| 门禁 | 规则 | 核对结果 |
| --- | --- | --- |
| `check_ostim_support_message.py` | 「拒绝信息与选择器必须一致」 | 选择器接受 **3** 种形式(`7.4.0.3` / `7.5.0.2` / `7.5.1.x`),信息**恰好**列这 3 种 —— 一致;两处都在同一个文件里,范围正确 |
| `check_skse_bootstrap.py` | 「bootstrap 必须能被本包支持的 SKSE 版本加载」 | 它同时查 `SKSEPlugin_Version`(2.2.x 用)与 `SKSEPlugin_Query`/`Load`(2.0.x/1.5.97 用),**正是 1.5.97 与 AE 的兼容性本身** |

第 ④ 条里 `check_skse_bootstrap.py` 值得单独说:它的 docstring 记着一个**只在 AE 上出现、
在 1.5.97 上完全看不见**的缺陷(SKSE 2.2.x 按导出名 `SKSEPlugin_Version` 查找,
而那个 dll 只导出了旧式的 `SKSEPlugin_Query`),并说明**为什么它查源码而不是二进制**
——「出货的构建跑在 CI 上,而这个失败在本机的 1.5.x 上是看不见的」。
**这正是本目标(1.5.x/1.6.x/1.7.x 兼容性)的核心形状**。

### 修复:同一个版本查询写了**三遍**,而启动器那一遍**少测一项** —— 空值会被当成版本号

**怎么找到的**:顺着 §123 的加载路径往上走。`Code/skse_client` 那条链有三步回退,
而**启动器那条链一步都没有** —— `Launcher.cpp`:

```cpp
LC.Version = QueryFileVersion(LC.exePath.c_str());
if (LC.Version.empty())
    DIE_NOW(L"Failed to query game version");
```

于是去看 `QueryFileVersion`,并把**同一段查询的三份实现**摆在一起对照
(§114/§120 的判据:**一份定义变成多份,一致性就不再是编译器的事**):

| 实现 | `ProductVersion` 的守卫 | `FileVersion` 的守卫 |
| --- | --- | --- |
| `immersive_launcher/utils/FileVersion.inl` | `len && ptr` | `len && ptr && *ptr` |
| `client/VersionDb.h` | `len && ptr && *ptr` | `len && ptr && *ptr` |
| `skse_client/main.cpp` | `ptr && *ptr` | `ptr && *ptr` |

**只有启动器那一份的 `ProductVersion` 少了 `*ptr`** —— 而**同一个文件里紧接着的
`FileVersion` 分支却带着它**。一个文件对同一个查询的两半给出不同的判据,
这就是它藏不住的地方。

**后果**:一个**带 `ProductVersion` 这个键、但值为空**的版本块,
会让第一个分支返回空串,而**第二个分支(那个存在的回退)永远不会被执行**。
调用方接着就是 `DIE_NOW(L"Failed to query game version")` ——
**一次启动失败,而错误信息指错了原因**:`FileVersion` 就在那里可以读。

**修法**:把两个键写成一张表、用同一个守卫,四项测试
(查询成功 / 有长度 / 有指针 / **串非空**)在两条分支上完全一致。
`*ptr` 这一项是必须的,因为**空串是一个「成功」的查询结果**,
而它不是一个可用的版本号。

**顺带核对(不是缺陷)**:

- `VerQueryValue` 的 `A`/`W` 混用**是合法的**:全仓没有定义 `UNICODE`,
  所以裸名解析成 `A`;而 `GetFileVersionInfoW` 只决定**资源块怎么读**,
  `VerQueryValueA` 返回的始终是 `char*`。两处都是 `A`,一致。
- 出参类型 `(void**)` 与 `(LPVOID*)` 在 Windows 上是**同一个类型**,不是缺陷。
- `FileVersion.inl` 只有**一个**文件:`Launcher.cpp` 写的是 `"Utils/FileVersion.inl"`,
  而 git 跟踪的是小写 `utils/` —— Windows 大小写不敏感,所以两边指的是同一个文件。

**新增门禁** `Tools/Scripts/check_version_queries.py`:

1. **每一处被守卫的 `VerQueryValue` 调用都必须测「串非空」** ——
   按括号配平取守卫(不是正则抓一行),因为条件跨行且调用内部还有括号;
2. **每个被读到的键都必须被某个守卫覆盖** —— 免得新增一个键却不加守卫;
3. **三份实现读的键集合必须一致,且这是失败而不是提示** ——
   少读一个键就是**少了回退**:一个把首个键留空的版本块会让那条路径直接中止,
   而不是去试第二个键。

**刻意不要求测长度**:三份里有两份没有测它,而**它们是对的** ——
`VerQueryValueA` 会设置长度,且非空串本身就意味着长度非零。
要求它会把**能工作的代码报成坏的**,而**假阳性会让判据被放弃**(§115.3)。

**变异测试**:

| 变异 | 门禁输出 |
| --- | --- |
| **还原原始缺陷**(`ProductVersion` 去掉 `*ptr`) | `the ProductVersion guard is '&& productVersionLen && productVersion', which does not test non-empty. An empty ProductVersion then counts as a version, and the fallback key is never tried` |
| 删掉某个实现里的 `FileVersion` 回退 | `reads ['ProductVersion'] while ... reads ['ProductVersion', 'FileVersion']; a copy with fewer keys has no fallback` |
| 未变异的对照组 | **通过** |

第二条在**第一版门禁里只报「提示」而不是失败** —— 我在做变异测试时发现并改成失败:
**少了回退是缺陷,不是提醒**(§113.3 的「贡献 0 项即失败」是同一条)。

### 修复:地址库回退「取目录里第一个」—— 而一个整合包有 **10 个以上**地址库,1.5.x 之间 **98.7% 的 id 指向不同地址**

**怎么找到的**:转到**加载路径**——「哪个运行时被加载、按哪个版本加载」这条链。
`Code/skse_client/main.cpp` 用三步解析游戏版本:SKSE 模块名(权威,SKSE 与游戏版本锁定)、
exe 版本资源、最后**扫 `Data/SKSE/Plugins` 里的地址库文件名**。
第三步存在的理由写在注释里:**重打包与降级补丁会剥掉或伪造版本资源**。

而这一步的实现是「**取第一个匹配的**」:

```cpp
for (auto it = std::filesystem::directory_iterator(plugins, ec); ...)
{
    if (sscanf_s(name.c_str(), "versionlib-%d-%d-%d-%d.bin", ...) == 4 || ...)
        return buf;          // 第一个就返回
}
```

**目录枚举顺序是未定义的**,而注释里那句「一个整合包只带一套 bin」**是错的**:
本仓库自带 **23 个**,装好的游戏里有 **10 个**。

**这不是「差一点」,是灾难性的**:把 1.5.16 与 1.5.97 逐 id 对比 ——
两者共享 **777,719** 个 id,其中 **767,644 个(98.7%)解析到不同的 RVA**。
这正是 `GamePatch.h` 早就记下的那条:「十个不同的 1.5.x 地址库,它们的代码不可互换」。

**实测确认了这条路径真的会被走到**:装好的游戏里 `version-*.bin` 有 10 个
(`1-5-3` 到 `1-5-97`),按名字排序第一个是 **`version-1-5-16-0.bin`**。
而随包的 `st_boot.log` 显示 `resolved='1.5.97.0'` —— **只是因为 exe 版本资源这次能读**。
一旦它读不出来(注释里点名的那种情况),回退就会返回 `1.5.16.0`。

**修法**:两件事要分开,而且需要**不同的信息**:

| 问题 | 依据 |
| --- | --- |
| **哪个族**(pre-AE / AE) | 文件名本身:树里每个 `version-*.bin` 都是 1.5.x,每个 `versionlib-*.bin` 都是 1.6.x/1.7.x。**这个翻译单元被编译两次**,legacy 构建带 `SKYRIM_TARGET_LEGACY`,所以族**不是猜的** |
| **族内哪个版本** | 取**最高**。比运行版本更新的地址库是**惰性的**(客户端只解析它认识的 id),更旧的则**解析到错地址** |

**过程中我自己先写错了一版并当场纠正**:第一版取的是「**全局最高**」——
那在 1.6.1170 的游戏上会选到 **1.7.104.0**,加载一个 1.7 的库。
两个族在 `major.minor` 上**不相交**,所以「全局最高」两个问题都没回答。
改成按 `SKYRIM_TARGET_LEGACY` 过滤后:legacy 构建只可能解析 1.5.x,modern 只可能 1.6.x/1.7.x。

**顺带查出一个死文件**:`versionlib-1-6-1170-0-1.bin`(795,210 字节)
与 `versionlib-1-6-1170-0.bin`(795,129 字节)**同名版本、内容不同**
(共享 id 里 98.7% 解析到不同 RVA),自 `28eca3fb`「适配不同版本」起被签入,
**但没有任何代码路径能打开它** —— `VersionDb::Load` 只格式化
`versionlib-%d-%d-%d-%d.bin`,四个数;而 `sscanf_s` 对五段名**仍然返回 4**
(四个转换在字面量 `.bin` 失配**之前**就成功了),所以两个文件都解析成 `1.6.1170.0`,
`-1` 那个永远选不中。**它是有意加进来的,所以不删** —— 门禁把它登记为「不可达」并写明理由。

**新增门禁** `Tools/Scripts/check_library_scan.py`,四条:

1. `ScanLibraryVersion` **必须保留最高候选**,不能返回第一个匹配;
2. **必须按族过滤**,且族来自构建宏而非猜测;
3. **构建必须真的产出 `SKYRIM_TARGET_LEGACY=1` 的运行时** ——
   否则过滤条件在**每个**构建里都是假,legacy 运行时就会去读 AE 的库名;
4. **前提检查**:磁盘上每个 `version-*.bin` 真的都是 1.5.x、每个 `versionlib-*.bin` 都不是
   —— 因为族过滤正是建立在这个命名约定上。

**变异测试**:去掉族过滤 → 报 `computes a family but no longer filters on it`;
去掉 `SKYRIM_TARGET_LEGACY=1` 构建 → 报「过滤条件在每个构建里都是假」;
对照组通过。

### 新增门禁:1.5.97 匹配报告里有 **2 条候选与出货 map 不一致** —— 逐条用真机反汇编判定谁对

**怎么找到的**:继续按 §117/§118/§121 的判据问 `Tools/ida/`
「谁在读这个文件」。`st_capstone_report.json` 的答案:**没有门禁读它**。

它是**相似度匹配的中间日志**:对 91 个 AE id,记录匹配器给出的候选 `se_rva`
与它的相似度评分(或没找到的原因,如 `no bracket`、`ambiguous 0.84 vs 0.80`、
`low similarity 0.62`)。它是**证据**,而出货物是
`versionlib-ae-to-se-1-5-97-0.map`。

**没有任何东西把两者对照过**,所以读者无法分辨:哪些候选被采纳、哪些被否决、
否决是不是**有意的**。对照之后:

| | 数量 |
| --- | --- |
| 有候选的条目 | 49 |
| 与出货 map **一致** | **47** |
| 与出货 map **不一致** | **2** |

不一致的两条:**id 37511**(`s_payGoldToContainer`,相似度 **0.82**)与
**id 47303**(`actorHasEquippedRangedWeapon_m`,相似度 **0.74**)。

**两条都用真机反汇编判定了,而出货 map 两次都对、报告的候选两次都错:**

**37511** —— 出货值 `0x5e5dd0` 是一个**薄转发函数**,读 `rcx` / `rdx` / `r8d`
然后尾调用,正是客户端传的三个参数(`this`、容器、数量):

```asm
1405e5dd0:  48 8b f9        mov %rcx,%rdi        <-- this
1405e5de2:  41 8b f0        mov %r8d,%esi        <-- amount
1405e5de9:  48 8b da        mov %rdx,%rbx        <-- container
1405e5e0c:  e9 ff 1b c0 c0  jmp ...              <-- tail call
```

而报告的候选 `0x5e3a50` **完全不读 `rdx`** —— 是另一个函数。

**47303** —— 出货值 `0x7bcb40` 测的是**形态类型 0x29**(`cmpb $0x29,0x1a(%rax)`)
加一个 `<= 7` 的范围检查,正是客户端按 `bool Actor::()` 调用的那个「是否装备远程武器」:

```asm
1407bcb40:  48 8b 89 f0 00 00 00  mov 0xf0(%rcx),%rcx
1407bcb66:  80 78 1a 29           cmpb $0x29,0x1a(%rax)    <-- form type 0x29
1407bcb75:  83 f8 07              cmp $0x7,%eax
```

而候选 `0x7bcaf0` 测的是**另一个**形态类型。

**这不是运气,而是一条可以固化的规律**:

| 相似度区间 | 与出货 map 一致 |
| --- | --- |
| **≥ 0.90** | **31 / 31 全部一致** |
| 0.80 ~ 0.90 | 6 / 7 |
| < 0.80 | 10 / 11 |

**每一条不一致都落在两个最低的相似度区间里**,而 ≥0.90 那一档**全票一致**。
也就是说:**这两条否决是匹配器的低置信度猜测被人工判定推翻的结果**,
出货 map 的处置是对的。

**新增门禁** `Tools/Scripts/check_capstone_report.py`,把这件事**固定下来**:

1. 每条候选**要么与出货 map 一致,要么登记在 `REJECTED` 表里并写明理由** ——
   于是第三条不一致不可能无声出现;
2. **反向查**:登记为「已否决」的条目若 map 现在**同意了**候选,报错 ——
   免得豁免活得比理由长;
3. 固化上面那条相关性:**≥0.90 那一档若有任何一条不一致就报错** ——
   因为本节的两条否决**正是建立在「高相似度从不被推翻」之上的**;
   一旦它不成立,这个推理就要重新推导。

**变异测试**,三种形状各自被精确指名:

| 变异 | 门禁输出 |
| --- | --- |
| 在高相似度档(1.0)造一条不一致 | `16005 (s_isQuestObject): the report resolved 0xf423f (similarity 1.0) but the shipped map has 0x1d6ba0; every candidate at or above 0.9 has been accepted, so this one disagreeing is new` **+** `that band was previously unanimous, so the rejections recorded here need re-deriving` |
| 在低相似度档造**第三条**未登记的不一致 | `68545 (setEnabled): the report resolved 0xd9038 (similarity 0.74) but the shipped map has 0xc11c60. Either the map is wrong or the candidate was rejected - if rejected, record it in REJECTED with the reason` |
| 让 map 与某条已否决的候选**一致**(豁免过期) | `37511: this gate records it as rejected (...), but the map now agrees with the candidate; drop the entry rather than leaving it stale` |
| 未变异的对照组 | **通过** |

### 新增门禁:91 条「引擎地址证据」**没有任何门禁读它** —— 而它有三条各自可查的主张

**怎么找到的**:按 §117/§118 的判据把 `Tools/ida/` 逐个问「谁在读这个文件」。
`st_target_meta_1170.json`(132 KB)的答案是:**只有写它的 IDA 脚本和一个 README**。

它是**客户端所解析的地址库 id 背后的证据**:91 条记录,每条给出

```json
"11612": {
  "var": "setWornData",
  "callsite": "Games/Skyrim/ExtraData/ExtraDataList.cpp:112",
  "ea_1170": "0x14015ab40",
  "size": 481,
  "decompile": "…反汇编全文…",
  "callees": ["ae68234", …],
  "bytes16": "40555657415641574883ec3048c74424"
}
```

**它有三条可以各自独立验证的主张** —— 而三条都可能无声地失效:

| 主张 | 失效后果 |
| --- | --- |
| ① `ea_1170` 是那个 id 的地址 | **被审阅的反汇编不是客户端调用的那个函数** |
| ② 客户端**仍然**把那个 id 绑到那个变量 | 记录的反汇编不再描述实际运行的代码 |
| ③ 那个 id 在**每个** AE 运行时上都存在 | 只在一个版本上存在 = 其他版本解析为空(旧版运行时则是那个共享桩) |

**先核对现状**(三条各自查一遍):

| 检查 | 结果 |
| --- | --- |
| `ea_1170` − 镜像基址 == `versionlib-1-6-1170-0.bin` 记的 RVA | **91 / 91 一致** |
| (文件, 变量, id) 三元组仍存在于客户端 | **91 / 91 存在** |
| 91 个 id 在全部 **14 个** AE 运行时上的解析 | **91/91 × 14 全部解析** |

**也就是说当前没有活缺陷** —— 这份证据是准确的(§113.2 的区分)。

**核对过程中值得记的两件事**:

1. **`callsite` 里的行号已经漂移了** —— 91 条里有 **46 条**的行号不再指向那个绑定
   (`ExtraDataList.cpp` 那 6 条都偏了 31 行)。行号是**拍摄时那个修订版**的,
   而**三元组 (文件, 变量, id) 才是主张本身**。所以门禁按**三元组**查,
   不按行号 —— 这是本节最该留下的一条。
2. **我的第一版匹配器报了 12 条「变量绑到了别的 id」,全是假阳性**:
   `POINTER_SKYRIMSE` 是**函数局部**的,同一个名字在不同函数里**合法地**绑不同 id
   (`s_pickUpObject` 在 `Actor.cpp` 是 37521、在 `PlayerCharacter.cpp` 是 40533)。
   `callsite` 的作用正是**指明是哪一处**。而 `400180` 那条用的是
   `RttiLocator<...>(400180)` 而不是 `POINTER_SKYRIMSE` —— 元数据把它的 `var`
   标成 `"rtti"`,**标对了**。门禁现在两种绑定形式都认。

**新增门禁** `Tools/Scripts/check_target_meta.py`,三条主张各查一条,
并**先断言证据文件与库都在**(否则报「无从校验」而不是静默通过)。

**变异测试**,三条主张各自被独立命中:

| 变异 | 门禁输出 |
| --- | --- |
| `ea_1170` 改 1 字节 | `ea_1170 0x14015ab41 is RVA 0x15ab41, but versionlib-1-6-1170-0.bin records 0x15ab40 for this id; the disassembly that was reviewed is not the function this id resolves to` |
| 把 `var` 改成同文件里另一个名字 | `11612 (setHealth) is recorded against ...ExtraDataList.cpp, but that file binds setHealth to ['11616']; the metadata describes a binding the client no longer has` |
| 加一条**只在 1170 上存在**的 id | `versionlib-1-6-317-0.bin: 1 recorded id(s) do not resolve there, e.g. [415831] …`(同时命中 317 与 318) |
| 未变异的对照组 | **通过** |

第二条的诊断**直接说出正确的 id 是 `11616`** —— 是能拿去改的信息,不只是「错了」。

### 新增门禁:契约头被**复制进 6 个地方**,而它的 ABI 是**成员顺序** —— 没有任何东西比较这些副本

**怎么找到的**:按 §119 的规程(「扫描范围要按规则搜,不能按发现地点写」)
把 `Code/plugins/tools/` 下 9 条门禁逐个做范围审计。`check_skee_abi.py` 的
`SOURCES` 只有两个文件,所以先问:**规则是什么?**
规则是「**凡声明了这三个 SKEE 接口类的文件**」—— 用规则搜一遍全仓:

| 文件 | 声明了 | 是否在 `SOURCES` |
| --- | --- | --- |
| `plugins/MorphSyncTogether/src/SkeeInterfaces.h` | 3 个 | **是** |
| `plugins/OStimTogether/src/RaceMenuOverlayBridge.cpp` | 2 个 | **是** |

**0 个漏掉** —— 这条门禁的范围是对的。`check_exports.py` 同理:契约里 4 个名字、
运行时导出 4 个符号,**双向对称、无多余**。

**但顺着「谁复制了契约」这条线问下去,发现一个真空白**:
`STRPluginMessagingAPI/STRPluginMessagingAPI.h` 被**复制进 6 个地方**:

```text
Code/plugins/STRPM/...            运行时的正本
plugins/STRPluginMessagingAPI/... 独立桥
plugins/AnimSyncTogether/...
plugins/DAVSyncTogether/...
plugins/IEDSyncTogether/...
plugins/TradeTogether/...
```

而这些副本的字节大小**差到 1.4 KB**(6,590 ~ 8,010),于是问题变成:
**它们的 ABI 一致吗?谁在检查?**

答案:**没有人在检查**。而这些接口结构体是**函数指针表**:

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

**ABI 就是成员的顺序。** 交换 API 检查不了它:运行时交回一个指针,
插件按**第 N 个槽位**调用,某个副本里插入/删除/重排一个成员,
后面**每一次调用都派发到错的函数** —— 与 `check_skee_abi.py` 存在的理由
(RaceMenu 版本错配)**是同一类失败**。

**先核对现状**(把 6 个副本逐项对照):

| 检查项 | 结果 |
| --- | --- |
| 4 个接口结构体的**成员顺序** | **6 个副本全部一致**(20 个布局) |
| 共享常量(`kInterfaceVersion` = 2 等 6 个) | 全部一致 |
| 声明的类型集合(15 个) | 全部一致 |

**也就是说当前没有活缺陷** —— 字节差异来自空白与注释,不是 ABI
(§113.2 的区分:补的是**守护**)。

**新增门禁** `Code/plugins/tools/check_contract_copies.py`,三条检查:

1. **每个接口结构体的成员顺序**必须与正本一致 —— 这是 ABI 本身;
2. **每个共享常量的值**必须一致 —— 两边会拿它比较后才派发;
3. **声明的类型集合**必须一致 —— 免得某个副本悄悄多/少一个类型。

**刻意不要求**逐字节相同:副本在空白、以及「带了哪些尾部声明」上**合法地不同**,
而**要求字节相同是错的**(§117.2 的方向性教训:门禁要问「ABI 一致吗」,
而不是「文件一样吗」)。

**变异测试**,三种 ABI 失败形状各自被精确指名:

| 变异 | 门禁输出 |
| --- | --- |
| 在 `AnimSyncTogether` 副本里**交换两个成员** | `declares the same members in a DIFFERENT ORDER: [...] vs [...]. The order is the ABI` |
| 在 `DAVSyncTogether` 副本里**改常量**(2→3) | `sets kInterfaceVersion = 3 but the runtime copy sets 2` |
| 在 `TradeTogether` 副本里**删掉一个成员** | `declares 6 member(s) but the runtime copy declares 7: [...] vs [...]. Every call after the difference dispatches through the wrong slot` |
| 未变异的对照组 | **通过** |
| **删掉整个副本文件** | `is missing; IEDSyncTogether cannot be compared against the other copies` |

第三条变异**同时命中了两个结构体**(`Interface` 与 `TransportInterface`)——
说明它按结构体逐个比对,不是只比一个。

### 修复:上一轮新加的覆盖表门禁**又只读了一个文件** —— 而出货 map 是三个文件生成的

**怎么找到的**:上一轮(§118)刚写完 `check_ae_to_se_overrides.py` 并抓到活缺陷,
本轮先问同一类问题(§113/§116 的判据):**这个门禁扫得够全吗?**
它的扫描根写死成 `st_overrides_all.tsv` —— 因为**缺陷是在那里找到的**。

而 `Tools/ida/README.md` 里给出的再生命令是:

```text
--overrides Tools/ida/st_overrides.txt \
--overrides Tools/ida/st_overrides_graph.txt \
--overrides Tools/ida/st_overrides_all.tsv \
--out       GameFiles/Skyrim/SKSE/Plugins/versionlib-ae-to-se-1-5-97-0.map
```

**三个文件都是出货 map 的输入**,而门禁只读了第三个。

**这是第三次同一个错误**:§113(`check_ostimng_versions.py` 只扫 1 个插件)、
§116(`check_wire_symmetry.py` 只扫 `Messages`)、现在是 §119。
**三次都是「扫描根写成发现地点而不是规则」**。所以这次修法不只是补两个文件名。

**先核对现状**(把三个文件都按三条不变量跑一遍):

| 文件 | 行数 | 重复 id | RVA 不是函数起点 | 在 `.text` 内却不是起点 | 与出货 map 不一致 |
| --- | --- | --- | --- | --- | --- |
| `st_overrides.txt` | 63 | 0 | 0 | 0 | 0 |
| `st_overrides_graph.txt` | 21 | 0 | 0 | 0 | 0 |
| `st_overrides_all.tsv` | 3,678 | 0 | 0 | 0 | 0 |

**三个文件全部干净** —— 也就是说补的是**守护**,不是修 bug(§113.2 的区分)。
唯一一处「id 在覆盖文件里但不在 map 里」是 `st_overrides_graph.txt` 的 **14774**
(`BSRandom::GetGenerator`),而 README **明确**写了它「not yet folded into the map」、
且「**不要**为了对齐而把它从覆盖文件里删掉 —— 文件是证据日志,map 才是出货物」。
所以这一条被登记为**有意的例外**并写明理由,而不是被当成错误。

**修法**:

1. `OVERRIDE_FILES` 写成**显式清单**,三个文件都扫;
2. 加 `ALLOWED_ABSENT` 表,把「有意的例外」连同理由登记下来 ——
   并**反向查**:豁免的 id 若不在文件里了就报错,免得豁免活得比理由长;
3. 加**防范围腐化**的反向检查:清单里的文件若**贡献 0 行**即为失败 ——
   因为「文件被清空」与「文件格式变了」在扫描结果上**长得一样**,
   而后者会让门禁**静默退化成空转却照样打印 OK**(§113.3/§116.3 的同一招)。

覆盖从 **3,678 行 / 1 个文件** 升到 **3,762 行 / 3 个文件**。

**变异测试**——**专门在上一版从不读的那两个文件里造缺陷**:

| 变异 | 门禁输出 |
| --- | --- |
| `st_overrides.txt` 里一个 RVA 改成 `0x1234` | `Tools/ida/st_overrides.txt:4: id 11612 maps to 0x1234, which is inside .text but is not a function start` **+** `says 0x1234 but the shipped map has 0x10de90` |
| `st_overrides_graph.txt` 里制造重复行 | `id 14774 appears on line(s) 18, 19; the reader keeps the last one, so the value that ships depends on file order` |
| 三个真实文件的对照组 | **通过** |
| **把 `st_overrides_graph.txt` 清空** | `contributed no override row` **+** 豁免过期提示 |

**上一版门禁对前两条变异完全不报** —— 这正是补范围的意义。

### 修复:1.5.x 的 id→RVA 覆盖表里 **id 35086 有两个互相矛盾的值**,而读取方**静默取最后一个**

**怎么找到的**:上一轮(§117)的判据是「找出只有文档意义、编译器管不着的证据文件」。
本轮把它用遍 `Tools/ida/` —— 逐个问「这个文件谁在读?」。`st_overrides_all.tsv`
(3,692 行)的答案是:**除了一个 README,没有任何代码或门禁读它**。

而它其实是**出货物的输入**:

```text
GameFiles/Skyrim/SKSE/Plugins/versionlib-ae-to-se-1-5-97-0.map
  = gen_ae_to_se_map.py --overrides Tools/ida/st_overrides_all.tsv 的输出
```

也就是说:**1.5.x 运行时实际加载的那张 AE id → 1.5.97 RVA 翻译表,
就是从这张表生成的。** 表里 3,678 行是「手工核对过的 id → RVA」。

**缺陷**:表里 **id 35086 出现了两次,值不同**:

```text
L288   35086 0x567a89
L3680  35086 0x567a80
```

而读取它的代码是:

```python
table[ae_id] = se_offset     # 后出现的覆盖先出现的 —— 静默
```

**出货的 map 取到的是「最后一个」** —— 这次是**对的**那个,
但那是**行序的运气**,不是任何检查的结果。把文件排一次序,或者把某一行挪个位置,
出货的就是另一个值,而**没有任何东西会报错**。

**两个值谁对?** 这不是靠读代码能回答的,要问 1.5.97 的地址库本身:

| RVA | 在 `version-1-5-97-0.bin` 里是函数起点? |
| --- | --- |
| `0x567a80` | **是**(SE id 34286) |
| `0x567a89` | **不是** |

再把真实 `SkyrimSE.exe`(1.5.97,34,769,792 字节)反汇编出来看:

```asm
140567a80:  48 85 d2        test %rdx,%rdx          <-- 函数真正的开头
140567a83:  0f 84 3d 03 00 00  je 0x140567dc6
140567a89:  48 8b c4        mov %rsp,%rax           <-- 旧值指向这里,函数体内 9 字节
```

`0x567a89` **落在函数体内部** —— 越过 `test rdx,rdx / je` 这个序言,
指向「已经假定 rdx 非空」之后的代码。

**这不是死数据**:`Actor.cpp:1581` 用这个 id 解析一个**活的**函数指针:

```cpp
POINTER_SKYRIMSE(TApplyActorEffect, s_applyActorEffect, 35086);
```

在 1.5.x 上,它经 `VersionDbPtr` 拿到的是**覆盖表 → 出货 map → 1.5.97 RVA**。
一个指向函数内部的地址,**就是一次跳到错误位置的调用**。

**修法**:把 L288 的旧值改成正确的 `0x567a80`,并**删掉 L3680 那条重复行**
(两行同值没有意义,留着只会让「一个 id 一行」这个不变量继续被破坏)。
现在全表 **3,678 行、0 个重复 id、0 处与出货 map 不一致**。

**先核对现状,再动手**(§111 的方法):把**全部 3,678 行**逐条对 1.5.97 地址库查一遍 ——

| 检查 | 结果 |
| --- | --- |
| RVA 是函数起点 | 3,677 |
| RVA 不是函数起点 | **1**(就是 35086 的旧值) |
| 落在 `.text` 内却不是函数起点(= 可疑) | **1** |
| 落在 `.text` 之后(数据 / RTTI 定位符,正常) | 0 |

也就是说:**全表只有这一个坏值**,不是普遍问题。

**新增门禁** `Tools/Scripts/check_ae_to_se_overrides.py`,三条检查各自独立:

1. **每个 id 只出现一次** —— 重复意味着读取方在**静默选择**,而选哪个取决于行序;
2. **每个 RVA 是 `version-1-5-97-0.bin` 里的函数起点**(该客户端在那个版本上加载的库),
   例外是 `.text` 之后的定位符(结构上就是数据);
3. **每个值与出货 map 一致** —— map 是从这张表生成的,不一致意味着**只重生成了一边**。

**变异测试**(每个都先断言真的生效),三种形状各自被精确指名:

| 变异 | 门禁输出 |
| --- | --- |
| **还原原始缺陷**(35086 → `0x567a89`) | `id 35086 maps to 0x567a89, which is inside .text but is not a function start in version-1-5-97-0.bin - a call through it lands mid-function` **+** `id 35086 says 0x567a89 but the shipped map has 0x567a80` |
| 还原重复行 | `id 35086 appears on line(s) 288, 3680; the reader keeps the last one, so the value that ships depends on file order` |
| 值改成 `0xf9e91`(差 1 字节) | 同样两条,指名 L14 |
| 未变异的对照组 | **通过** |

第一条正是**本轮的原始缺陷** —— 门禁确实会拦住它。

### 新增门禁:1.5.x 的补丁偏移**有实测证据文件,但没有任何东西把代码和它绑在一起**

**怎么找到的**:上一轮把 15 条门禁的**扫描范围**逐个核对(§113/§116 的判据),
其中 `Code/plugins/tools/plugin_patches.py verify` 跑通了 39 个插件补丁。
接着问同一类问题:**仓库里还有哪些「证据文件」是只有文档意义、编译器管不着的?**

`Tools/ida/st_patch_offsets_1597.tsv` 就是这样一个 —— 它是
`Tools/ida/patch_offsets_1597.py` 把 1.6.x 与 1.5.97 两份反汇编**逐指令对齐**的产物,
每一行给出「这个补丁站在 1.5.97 上应该写哪里」,并对没有对齐成功的行**写明原因**。
它被签入仓库,`GamePatch.h` 的注释也指向它。

但**没有任何门禁读它**:

```text
git grep -ln 'st_patch_offsets_1597' -- Tools/Scripts Code/plugins/tools
  (空)
```

偏移量是以**字面量**形式散在 8 个文件、13 处
`GamePatch::At(pAnchor, {modern, legacy, "1.5.97"}, "site")` 调用里的。
于是两边可以各说各话:

- **tsv 是证据** —— 它说「哪一行的 1.5.x 偏移有实测支持」;
- **代码是主张** —— 它决定这个补丁**到底写不写**。

**危险的方向只有一个**:调用点带着一个**没有任何行支持的 1.5.x 偏移** ——
那是把**没人量过的数字写进正在运行的游戏**。反方向(tsv 有、代码不用)不是缺陷
(1.5.x 上主动放弃一个纯外观补丁是有意的,`frame end` 就是这样),
但值得**报告**,因为那通常意味着「行被更新了、调用点没跟上」。

**先核对现状**:把 13 处调用点的偏移与 tsv **逐条对照**:

| 站点 | 代码 modern | tsv ae | 代码 legacy | tsv se | 结论 |
| --- | --- | --- | --- | --- | --- |
| window style | `0x174 + 1` = 0x175 | 0x175 | – | – | 一致(该行注明 1.5.x 未对齐) |
| dinput cooperative level | 0x57 | 0x57 | 0x57 | 0x57 | 一致 |
| stats menu appear / background / update | 0x84E / 0xA10 / 0x1040 | 同 | 0x929 / 0xAEC / 0xFBC | 同 | 一致 |
| stats menu controls a / b | 0x46 / 0x4A | 同 | 0x46 / 0x4A | 同 | 一致 |
| menu unfreeze | 0x682 | 0x682 | 0x6A4 | 0x6A4 | 一致 |
| favorites menu numbering | 0x15 | 0x15 | 0x15 | 0x15 | 一致 |
| projectile null check | 0x374 | 0x374 | 0x397 | 0x397 | 一致 |
| skip startup movie | 0xFE | 0xfe | – | – | 一致(1.5.x 未对齐) |
| tasklet thread names | 0x63 | 0x63 | – | – | 一致(该 id 在 1.5.97 未映射) |
| **frame end** | 0x9 | 0x9 | **–** | **0x9** | **tsv 有实测偏移,代码不用** |

**也就是说当前没有活缺陷**,缺的是守护 —— 与 §113.2、§115.4 同一种区分。

**新增门禁** `Tools/Scripts/check_patch_offsets.py`:

- 机械枚举**每一处** `GamePatch::At(...)` 调用(不是认行号),解析出锚点、
  modern 偏移、legacy 偏移、实测版本字符串与站点名;
- **modern 必须等于 tsv 记录的 `ae_offset`**;
- **legacy 偏移只在 tsv 那一行确实记录了 `se_offset` 时才允许出现** ——
  否则报错并引述该行的原因(如 `'instruction not in an aligned run'`);
- 有 legacy 偏移时,**实测版本字符串必须是 `1.5.97`**,与 tsv 的前提一致;
- 两处**命名空间不同**(tsv 的行名带限定词如 `menu unfreeze (call site)`,
  代码用短名),所以按「行名 = 站点名 或 站点名+限定词」匹配,
  多行匹配时**用偏移量定行**(`stats menu controls a/b` 就是这样区分的);
- **反向查**:`NO_LEGACY_EXPECTED` / `UNUSED_LEGACY_OK` 两张豁免表里的站点
  若在 tsv 里不再存在,报错 —— 免得豁免活得比理由长。

**变异测试**(每个都先断言变异真的生效),三种缺陷形状各自被精确指名:

| 变异 | 门禁输出 |
| --- | --- |
| 1.5.x 偏移改成 tsv 没记录的 `0x999` | `uses 1.5.x offset 0x999 but ... records 0x929` |
| 给「tsv 明确没有 1.5.x 偏移」的站点**加上**一个 | `applies a 1.5.x offset (0x100) that ... does not record - the row says 'instruction not in an aligned run'. A number nobody measured must not be written into a running game` |
| modern 偏移改成 tsv 没有的 `0x375` | `uses modern offset 0x375, which no ... row records for it (recorded: 0x374, 0x379)` |
| 未变异的对照组 | **通过** |

**顺带核对(无缺陷)**:39 个插件补丁**全部**通过 `plugin_patches.py verify`
(即都能干净地应用到固定的子模块提交上 —— 一个应用不上的补丁意味着
**一个静默消失的修复**)。

### 修复:上一轮新加的线上格式门禁**自己犯了它要防的那个错** —— 只扫了 `Messages`,漏掉 `Structs` 的 23 对

**怎么找到的**:上一轮(§115)加 `check_wire_symmetry.py` 时,扫描根写死成
`Code/encoding/Messages` 一个目录 —— 因为那是**第一个想到的**地方。
本轮把「还有哪些目录定义同样的成对函数」数一遍:

| 目录 | `.cpp` | 同时定义 `Serialize`+`Deserialize` |
| --- | --- | --- |
| `Code/encoding/Messages` | 112 | 110(名字是 `SerializeRaw`/`DeserializeRaw`) |
| **`Code/encoding/Structs`** | **26** | **23** |

`Structs` 是消息**内部**用的结构体(`LockData`、`Inventory`、`ActorValues`、
`QuestLog`…),它们同样有一对 `Serialize`/`Deserialize`、同样决定字节数、
同样**没有任何东西把两侧绑在一起**。**23 对 = 总量的三分之一**从未被扫。

**这正是上一节(§113)记的同一个错误**:门禁的扫描根按「第一个想到的地方」写,
而不是按**规则**写。而这一次是**我自己在同一轮之后犯的** —— 说明这不是一次疏忽,
是**写门禁时的默认思维**。所以修法不能只补目录,必须让这类错误**自己暴露**。

**修法**:

1. `SCANNED` 改成显式清单,同时列出 `Messages`(函数名 `SerializeRaw`/`DeserializeRaw`)
   与 `Structs`(函数名 `Serialize`/`Deserialize`)—— 同一规则、两种拼写;
2. 加**防范围腐化**的反向检查:清单里的目录若**贡献 0 对**,本身就是失败 ——
   因为「目录改名/移动」与「目录里真的没有」在扫描结果上**长得一样**,
   而后者会让门禁**静默退化成空转却照样打印 OK**。

覆盖从 **110 对升到 133 对(+21%)**;审计结果 **133 对全部一致**
(即仍然没有活缺陷,补的是守护)。

**变异测试扩到两个目录**(每个都先断言变异真的生效):

| 变异 | 门禁输出 |
| --- | --- |
| `Messages` 里写了不读 | `the write side performs 1 more operation(s)` |
| **`Structs` 里只改一侧的顺序** | `same operations, DIFFERENT ORDER` |
| **`Structs` 里改位宽(写 16 / 读 8)** | `only written: ['Bits16']` / `only read: ['Bits8']` |
| 未变异的对照组 | 通过 |
| **把 `Structs` 清空** | `Code/encoding/Structs contributed no Serialize/Deserialize pair` |

最后那条是本轮最值得留的:**它正是会在上一轮抓到我的那条检查**。

**顺带核对(均无缺陷)**:

- **61 处 pad 声明的算术**逐条算过,没有一处是非正大小;4 处用
  `sizeof(X)` 的(`TESObjectARMO`、`PlayerCharacter` 两个分支、`Projectile`)都写在
  明确注释了「锚点式」的地方。
- **两个随包运行时的 DLL** 大小相同(6,409,728 字节),版本特定标记
  (`bUseFaceGenPreprocessedHeads`、`SetRemoteFaceGenForce`、
  `legacy game version detected`)两边都在 —— 与「同一份源码、两个
  `SKYRIM_TARGET_LEGACY` 配置」一致,没有出现只更新一个构建的情况。
- **手动 `Lock()`/`Unlock()`** 全仓只有两处(`Primitives.h` 的 `BSScopedLock` 本身、
  `ExtraDataList::GetByType`),后者的两条返回路径**都**配了 `Unlock`,
  且三次 `IsOverriden()` 是同一个 thread_local 计数、同一线程内不会变。

### 新增门禁:每条消息的「写」与「读」必须走同一串线上操作 —— 这类缺陷不会报错,只会把值读错

**怎么找到的**:线上格式的每一条消息都**被定义两次** —— 一次在 `SerializeRaw`,
一次在 `DeserializeRaw` —— 而**没有任何东西把两者绑在一起**:不是同一个函数、
不是同一个宏、编译器也不检查。于是把 110 条消息的**两侧操作序列**逐条抽出来对照。

**为什么这算稳定性缺陷而不是洁癖**:这类不一致**不会抛异常、不会报解码失败**。
写了一个字段却没读,后面每个字段就整体错位一个宽度;而**收包方通常还有字节**
(一个包不止那几个字段),所以症状是**值读错了**(一个坐标、一个 form id、一个计数),
而不是「解不出来」。流还保持着同步 —— 这正是它难找的原因,与
`docs/PITFALLS.md` 第 45 节记的「写几条就只循环几条」是**同一族的另一端**:
那边是写侧写了比计数更多的条目,这边是两侧对**字段顺序本身**不一致。

**判据是「线上操作序列」,不是字段名**:读侧**合法地**用不同的名字 ——
循环下标或 map 键会替代写侧对条目的称呼:

```text
写: WriteVarInt(Tick) WriteVarInt(count) WriteVarInt(kvp.first) kvp.second.Serialize
读: ReadVarInt(Tick)  ReadVarInt(count)  ReadVarInt(cServerId)  Updates[cServerId].Deserialize
```

两者都是 `VarInt VarInt VarInt Nested` —— **这才是决定字节数的东西**。
循环体同样塌缩成一次迭代:两侧迭代同一个计数,重复次数不携带序列之外的信息。

**审计结果:110 条消息全部一致** —— 也就是说**当前没有活缺陷**。
这一条要分开说:本节加的是**守护**,不是修 bug。审计过程中我的抽取脚本报过
17 条「不一致」,逐条读下来**全是脚本的假阳性**(循环变量名不同、
`WireLimits::ClampFinite(...)` 包了一层、`Updates[id].Deserialize` 里带下标),
把抽取改成按**操作**而非按**字段名**之后归零。

**新增门禁** `Tools/Scripts/check_wire_symmetry.py`,并做了**四向变异测试**
(每个变异都先断言真的生效,§113.4 的教训):

| 变异 | 门禁输出 |
| --- | --- |
| 写了不读(写侧多一个操作) | `the write side performs 1 more operation(s) than the read side` |
| 只改一侧的顺序 | `same operations, DIFFERENT ORDER` |
| 读了不写(读侧多一个操作) | `the read side performs 1 more operation(s) than the write side` |
| 只定义了半对 | `defines only one half of the pair - DeserializeRaw is missing` |
| 未变异的对照组 | **通过** |

**变异测试本身又踩了一个坑,值得记下**:第一次把「顺序」变异做成
**两侧同时交换** —— 那样两侧**依然一致**,门禁正确地放行了,而我差点把它
读成「门禁抓不到顺序问题」。**真实的缺陷形状是只改一侧**。
改完再跑,四种形状全部被抓到,且诊断各不相同。

**顺带核对**:107 个 opcode(客户端 52 + 服务端 55)**取值全部唯一、全部 ≤ 255**;
`Opcodes.h` 里每个 opcode 都有对应的消息头,且每个都**既被 include 也被注册**
进对应方向的 `MessageFactory` —— 三处(枚举、include、visitor 注册)互相核对,
**零问题**。注册漏一个不会崩,只会让那条消息**静默不被处理**,
所以这一条同样值得核对而不是假设。

### 修复:`ExtraDataList` 的读侧判空、写侧不判 —— 同一份文件里两个函数对同一个成员的处理相反

**怎么找到的**:找「本仓库自己重写了一遍引擎结构」的地方,然后**逐对**比较同一成员
的读侧与写侧。`ExtraDataList` 是本仓库手写的引擎结构(不是转发引擎地址),
三个成员(`data` / `bitfield` / `lock`)各自被多个函数解引用。

**缺陷一:读侧判空、写侧不判**

```cpp
bool ExtraDataList::Contains(ExtraDataType aType) const
{
    if (bitfield)              // 读侧:判空,返回「没有任何位被设置」
    { ... }
    return false;
}

void ExtraDataList::SetType(ExtraDataType aType, bool aClear)
{
    uint8_t& flag = bitfield->data[index];   // 写侧:直接解引用  <- 崩
```

两者操作**同一个成员**,而 `SetType` 是 `Add` 调用的(`Add` → `SetType`),
`Add` 在**物品栏同步路径**上。于是:一个 `bitfield` 为空的列表,
**读它会得到「空集」,写它会崩** —— 读侧已经把「空」的语义定下来了,
写侧却不同意。

**缺陷二:`New()` 两次分配都不判空,而且立刻写穿**

```cpp
ExtraDataList* pExtraDataList = Memory::Allocate<ExtraDataList>();
pExtraDataList->data = nullptr;                    // 第一次分配失败 -> 崩
pExtraDataList->bitfield = Memory::Allocate<...>();
memset(pExtraDataList->bitfield, 0, 0x18);         // 第二次失败 -> 崩
```

`Memory::Allocate` 在失败时返回空(这一点由同文件的
`TESObjectREFR.cpp:705` 已经按「可能为空」处理过:那里会 `Memory::Delete`
两个指针并把结果置空)。**唯一的调用者本来就处理了空返回值** ——
所以这里崩掉纯粹是没判,不是契约不允许。

**缺陷三:`Add()` 不判节点**

```cpp
apNewData->next = pNext;   // apNewData 可能为空
```

链表里存进一个空节点,之后 `GetByType` 的遍历与 `GetCount` 的计数
**都会解引用它**。而 `Add` 的返回值本来就是 `bool`(成功/失败),
判空后返回假是现成的语义。

**修法**:四处各自补判空 —— `New` 两次分配(第二次失败时回收第一次)、
`Add` 的节点参数、`SetType` 的 `bitfield`;并给 `New()` 的调用点
(`TESObjectREFR::GetExtraDataFromItem`)补一条判空早退,因为它接下来
每一行都解引用那个结果。

**顺带核对**:`ExtraDataType` 全部 18 个枚举值的位图下标
(`value >> 3`)最大是 19,而位图是 `0x18 = 24` 字节 —— **全部在界内**,
所以这里没有越界写。

**新增门禁** `Tools/Scripts/check_allocation_guards.py`:它按函数读**函数体**
(而不是认行号),要求函数体里解引用的指针要么在同体内判过、要么由构造函数保证。
**已做变异测试**:把三个函数改回修前的写法 → 门禁报 3 条并逐条指名
(`New()` 的 `pExtraDataList`、`Add()` 的 `apNewData`、`SetType()` 的 `bitfield`);
改回修好的写法 → 通过。

### 修复:插件版本门禁「绿是因为它没在看」—— 只扫了 OStimNG,而 OStimTogether 另有 4 组 id 一个都没查

**怎么找到的**:把「每个插件用了多少个地址库 id」数一遍,再和门禁实际扫的范围对照。
结果:

| 插件 | `RELOCATION_ID` 站点数 | 门禁是否扫 |
| --- | --- | --- |
| OStimNG(前置) | 33 | **是** |
| **OStimTogether** | **8(4 组不同的 id 对)** | **否** |

`Code/plugins/tools/check_ostimng_versions.py` 的扫描根写死成
`plugins/OStimNG/skse` 一个目录 —— 名字也说明了这一点。而 `OStimTogether`
自己带 8 个站点(5 处 `RELOCATION_ID`,其中 `55712/56243` 出现 4 次),
**一个都没被检查过**。

**为什么这算缺陷而不是「覆盖不全」**:`REL::ID()` 在 id 缺失时不是软失败 ——
`REL/IDDB.cpp` 的 `report_id_lookup_failure` 会**直接中止游戏**。所以
「本仓库支持 1.5.x–1.7.x」这句话,对 OStimTogether 而言**从来没有被校验过**,
而门禁的绿灯会让人以为它已经被守住了。

**先核对现状**(不是猜):把那 4 组 id 对全部 24 个地址库跑一遍 ——
`55693/56224`、`53950/54774`、`55712/56243`、`55706/56237`
**全部完整**。也就是说当前**没有活缺陷**,缺的是**守护**。

**修法**:门禁改成扫**多个插件**,并把清单显式列出来
(`SCANNED = {'OStimNG/skse', 'OStimTogether/src'}`)。同时加一条**反向检查**:
清单里的插件若被检出却**贡献了 0 个站点**,本身就是失败 —— 因为
「子模块没检出」和「子模块里没有站点」在扫描结果上长得一模一样,
而后者会让这个检查**静默地退化成什么都不查**。

**变异测试**:在隔离副本里把 `OStimTogether/src/OStimBridge.cpp` 的
`RELOCATION_ID(55706, 56237)` 改成不存在的 `999999`,门禁报错并**指名文件与行号**:

```text
PLUGIN VERSION CHECK FAILED (1)
  - version-1-5-97-0.bin: 1 SE id(s) absent, e.g. [999999] at ['OStimTogether:OStimBridge.cpp:421']
```

改回真 id 则通过。站点数从 **33 升到 41**,检查的插件从 **1 个升到 2 个**。

### 修复:发布版**根本不写崩溃转储** —— 文档让玩家交 `crash_*.dmp`,而那个文件在发布版里是死代码

**怎么找到的**:查「哪些宏在发布版里把代码整段剥掉」。`IS_MASTER` 由 xmake 的
`before_build` 从 **git 分支名**推导,而本仓库的发布分支**就是 `main`**
(`xmake.lua`:`branch == "master" or branch == "main"`)—— 所以**每个出货构建里
`IS_MASTER` 都是 1**。这不是推断,是**实测**:随包 DLL 里嵌的构建标记就是
`Build: v1.1.8 main EVO`,而 `#if (!IS_MASTER)` 里的字符串
(`Components`、`Show selected entity in world`、`Crash client`)在同一个二进制里
**全部不存在**。

于是 `CrashHandler.cpp` 这一段的语义就反了:

```cpp
#if (IS_MASTER)
    volatile static bool bMiniDump = false;   // 发布版 -> 不写转储
#else
    volatile static bool bMiniDump = true;    // 开发版 -> 才写
#endif
```

**上游的语义 vs 本仓库的语义**:上游只有一条 `master` 发布分支,所以
「`IS_MASTER` = 这是发布版」成立。本仓库把 `main` 也并进来,而 `main` 是**唯一的
发布分支** —— 于是 `IS_MASTER` 恒为 1,**玩家跑的那个构建永远不写转储**,
而 `docs/PITFALLS.md` 第 7 节明确告诉玩家「正确诊断文件是游戏根目录
`logs/tp_client.log`、`crash_*.dmp`、…」。两边**各自都自洽**,所以谁也没发现:
文档说的是一件代码做不到的事,而代码做的事文档没写。

**修法**:转储不再受任何构建宏控制,每个构建都写。代价是崩溃时游戏根目录多一个文件,
而 `RemovePreviousDump` 本来就负责只留最新那个。

**顺带修掉同一段里的第二个缺陷**:那个清理函数的判据是

```cpp
if (entry.path().string().find("crash") != std::string::npos)   // 改前
    DeleteFileA(entry.path().string().c_str());
```

它在**整条路径**上做子串匹配,所以 `CrashLogger.dll`、名为 `Crash Fixes` 的目录、
以及任何路径里带 "crash" 的文件**都会被删掉**。改成只匹配自己写的那种名字
(`crash_<UTC 时间戳>.dmp`),并且删除失败不再影响流程。

**新增门禁** `Tools/Scripts/check_crash_diagnostics.py`:它同时读三处 ——
处理器、`docs/PITFALLS.md`、`xmake.lua` 的分支定义 —— 并拒绝:
转储被任何构建宏包住、`bMiniDump` 初值为假、清理判据退化成子串匹配、
以及**文档提到的产物代码里不存在**。**已做变异测试**:把原版 `#if` 与子串清理
还原回去,门禁报 5 条(含「文档让读者交 `crash_*.dmp`,而处理器从不写」);
改回修好的版本则通过。

**同时补上门禁的接线问题**:上一轮新增的 `check_null_accessors.py`
**没有被 CI 调用** —— 一个不跑的门禁等于没有门禁。现在两个新门禁都接进了
`.github/workflows/plugins.yml` 的 `push` 与 `pull_request` 两条 `paths` 触发列表,
并各自加了运行步骤。

### 修复:全项目审计 —— 15 处「引擎单例还没建好就解引用」,其中一处还抄错了引擎自己的遍历

**怎么找到的**:不靠读代码猜,靠三条**可判定**的线:

1. **把每个地址库 id 对着每个随包运行时算一遍** —— 3063 个 id × 24 个 `.bin`
   (1.5.3 ~ 1.5.97 走 ae-to-se 映射表,1.6.x 走 format 2,1.7.x 走 format 5)。
   结论:1.5.x 缺 13 个,AE 侧缺 4 个,全部已在 `Tools/missing_1_5_97_ids.txt` 记录。
2. **把 1.5.97 的每个 patch 偏移对着真机 `SkyrimSE.exe` 反汇编核一遍** ——
   10 个带 1.5.97 偏移的站点逐个读字节,确认 `Put`/`Nop`/`SwapCall` 各自的
   前提都成立(例如 `SwapCall` 要求站点首字节是 `0xE8`)。全部通过。
3. **反汇编引擎自己的 `INISettingCollection::GetSetting`(`SkyrimSE.exe+0x3104d0`)**,
   拿它当**契约**来对照本仓库手写的那一份。这一条抓到了真缺陷。

**缺陷一:`GetSetting` 抄漏了引擎的空表保护**

引擎那份的入口序列是:

```asm
1403104f4  cmpq $0x0,0x8(%rax)   ; head.next == 0 ?
1403104ff  cmpq $0x0,(%rax)      ; head.setting == 0 ?
140310503  je   140310521        ; 两个都是 0 -> 直接 return NULL
```

而本仓库的版本**无条件**读 `pCurrent->setting->name`:

```cpp
while (pCurrent)
{
    if (_stricmp(acpName, pCurrent->setting->name) == 0)   // setting 为空 -> 崩
```

空集合(或链表里出现一个 `setting` 为空的项)时,引擎返回「没找到」,
本仓库**解引用空指针**。同一趟反汇编也确认了结构体三个偏移与 `TES.h` 的声明一致
(`lea 0x118(%rcx)` 取链表头、`0x8(%rax)` 取 `next`、`(%rax)` 取 `setting`、
`0x10(%rdi)` 取 `setting->name`),所以**偏移没问题,缺的是保护**。

**缺陷二:`INISettingCollection::Get()` 自己会返回空,而三处调用点有两处直接解引用**

```cpp
return *settingCollection.Get();   // 改前:直接解引用,不看返回值
```

两个独立的空来源:该 id 在旧版运行时上没映射时返回的是**共享桩**
(`48 31 C0 0F 57 C0 C3` = `xor rax,rax; xorps xmm0,xmm0; ret`),
调用它得到 `rax = 0`,于是 `*stub` 就是**读地址 0**;以及引擎要等 ini 设置建好
才填那个单例指针,所以**启动早期它本来就是空**。现在返回空,三个调用点各自判空。

**缺陷三:同一类「单例还没建好就解引用」共 15 处**

判据是**树内不一致**,不是风格偏好:`PlayerCharacter::Get()` 在**全部 56 处**
调用点都判了空,`ModManager::Get()` 13 处里判了 12 处 —— 也就是说这个仓库
**自己已经**把这些访问器当可空用,漏判的那几处就是缺陷。逐处补齐:

| 访问器 | 漏判处 | 后果 |
| --- | --- | --- |
| `INISettingCollection::Get` | 2(`CharacterService`、`DiscoveryService`) | 启动早期崩 |
| `ModManager::Get` | 5(`Actor`、`PlayerCharacter`、`DiscoveryService`×3、`MagicService`、`QuestService`×2) | 刷怪/切格/法术计时/任务查询崩 |
| `EquipManager::Get` | 5(`Actor`×3、`TESObjectREFR`×2、调试视图×2) | 装备同步崩 |
| `EventDispatcherManager::Get` | 2(`DiscoveryService`、`ObjectService` 构造函数) | **启动期**崩 |
| `SubtitleManager::Get` | 1(`CharacterService` 字幕) | 收到字幕包崩 |
| `TES::Get` / `UI::Get` | 2(调试菜单) | 调试菜单崩 |
| `ActorMediator::Get` | 1(`AnimationSystem` 回放) | 远端动画回放崩 |

**为什么这些在 1.5.x/1.6.x/1.7.x 上都要紧**:三个版本走**三条不同的地址库路径**
(format 1 + 映射表 / format 2 / format 5),但**共用同一份调用点代码**。
单例是否已建好取决于**引擎启动顺序**,与游戏版本无关 —— 也就是说这三类
缺陷在三个版本上都成立,而且**旧版运行时更严重**:那里未映射的 id 返回的是可执行桩,
`*stub` 直接读地址 0,而不是返回空。

**新增门禁** `Tools/Scripts/check_null_accessors.py`:它读「已知可为空的访问器」
名单,再拒绝任何**在同一语句里解引用且没有判空**的调用点;反向也查 ——
豁免条目一旦不再需要就报错,免得名单烂成一张万能通行证。
**已做变异测试**:把守卫改回未判空形式,门禁报错并指出行号;改回守卫则通过。

**本机不编译**(无 xmake/MSVC),以上判据是静态的 + 对真机 1.5.97 反汇编的核对。

### 同步:上游 v1.8.3(12 个提交)—— 修掉「NPC 疯狂复制直到崩游戏」,并给专用服务器加上 `--generate-config`

**为什么不能直接 merge**:本仓库在上游基础上魔改(多游戏版本、1.5.x 翻译表、
插件层与 UI 中文化),两边对同一批文件的写法已经不同。所以本节按**逐个提交判定**
再按本仓库的写法落地,而不是拉一个合并。

**上游 v1.8.3 的 12 个提交,处置如下**(`v1.8.2..4917189f`):

| 上游提交 | 内容 | 处置 |
| --- | --- | --- |
| `8f08d405` #906 | **NPC 疯狂复制**:owner 把服务器回显的**过期**状态套回自己刚分配到的 actor | **已同步**(见下) |
| `af7f37e6` #907 | `SpellCastRequest::DesiredTarget` 未初始化 | 已同步(一行) |
| `3e68a605` #902 | `DLC1DimHollowCrypt02` 死亡后掉进锁住的门里 | 已同步(一行) |
| `3941ddd0` #905 | 删除死代码 `AnimationExperiments.cpp`(429 行) | 已同步(见下) |
| `6847eaa0` | 一批 `Console::Setting` 描述改成能读懂的写法 | 已同步(6 条) |
| `92d28f07` #910 | 新增瑞典语 | 已同步,并补齐本仓库多出的 5 个键 |
| `23e47d2c` | `--generate-config` 生成默认 `STServer.ini` | 已同步,含入口重构(见下) |
| `a89d8185` | 「你的安装不是纯原版」警告只在 master 构建显示 | **改写**(见下) |
| `fbf72883` #901 | 远端 actor 的 Havok 时间步 | 本仓库早已按 1.5.x 安全写法修过,无需再动 |
| `a3f62ba7` #909 | 单人做任务时提示组队 | 已同步(C++ + UI + 14 种语言) |
| `589b5f5b` | CI 非发布构建的双层 zip | 不适用:本仓库 CI 不上传 `.zip` 路径 |
| `4917189f` | 合并提交 | 不适用 |

**#906 的根因(上游说明 + 本仓库核对)**:自 `1f488f6f` 起,owner 拿到
`AssignCharacterResponse` 时会用服务器回传的数据**重新对齐**新分配到的 actor。
但那份数据**只是自己请求快照的回显**,在往返途中可能已经过期。典型受害者是
WE23 的「拾荒者」士兵 —— 它们在读盘时就被 `WEAliasScript` 杀掉,于是:
回显说「活着」→ `Respawn()`(Resurrect + Reset)→ 引用被重置、3D 被重载、
等级化临时 actor 被重掷 → owner 交还实体 → 该 actor 被当成**新**实体重新发现 →
每轮往返重复一次,而每个交还的实体又被交给另一个玩家留一份新副本,
**累积到几百个 NPC 后崩游戏**。

**修法**(两处,都在本仓库落地):
1. `ReconcileActorData` 里 `Respawn()` 只在**不是本地 owner** 时执行;本地 owner
   保持自己的死亡状态,交给 `LocalComponent` 的常规差分去上报(新增 `warn` 说明
   为什么保留)。`Kill()` 方向不受影响。
2. `OnAssignCharacter` 的 owner 分支**不再**用回显覆盖本地状态:死亡状态不一致时
   只打一条 `info`,并补上 `m_weaponDrawUpdates.erase()`(上游同款:所有权授予
   到达时可能还有一条远端拔刀修正排队)。
   本仓库原先在 owner 分支里还有一对 `SetRemote(true/false)` 夹着 `ReconcileActorData`,
   随该调用一并去掉 —— 它存在的唯一理由就是那次对齐。

**#905 的核对**:`AnimationExperiments.cpp` 是一个 429 行的逆向试验文件
(`RePerformAction` / `ReHandleEvent` / `ComputeResult` / `g_animErrorCode`),
全仓**零引用**。它同时是 `hkEventType::hkEventType` 与 `hkEventContext::hkEventContext`
两个构造函数的**唯一**定义处,所以删文件必须同时把这两处内联回去
(上游把 `hkEventType` 的构造改成成员初始化列表,本仓库照做)。
删除前逐个 grep 了 8 个符号,确认除该文件外没有任何引用。

**#23e47d2c 的入口重构**:上游把 `CreateGameServer(conReg, callback)` 拆成
「先 `RegisterServerSettings(conReg)`、再 `CreateGameServer(conReg)`」,并让
`LoadSettings` 返回 `bool`、新增 `IsValid()`。这不是为了 `--generate-config` 好看,
而是消掉上游代码注释里自己写下的隐患:**设置注册晚于读取**,所以首次生成
`STServer.ini` 时服务器设置可能整批缺失。本仓库按同一形状落地,并额外处理了
**本仓库特有的 GUI 模式**:`RequestGuiMode` 在 Windows 上默认返回真,
不加判断的话 `--generate-config` 会被 GUI 吞掉、只弹出窗口而不生成文件,
所以给它加了一条 `--generate-config` 直接返回假。CI 里加了一步冒烟测试
(生成后必须存在且能打印)。

**`a89d8185` 为什么不能照抄**:上游把这条警告限制为 `#if (IS_MASTER)`,
而上游的 `IS_MASTER` 只在 `branch == "master"` 时为真。本仓库的 `xmake.lua`
把 `branch == "main"` 也算作 master,而本仓库的分支**就是** `main` ——
照抄等于**空操作**,警告照样每次都弹。更关键的是这条警告的文案是
「我们**不**建议使用 mod」,而本仓库的定位恰恰是**支持整合包**(README 第 18 行)。
所以本仓库的处置是:**保留判定、去掉弹窗**,改成一条 `info` 日志
(`Mod list is not the default one; this is expected when playing with a modpack`)。
判定本身仍有价值 —— 有人报不稳定时,日志里能看到他跑的不是默认列表 ——
但不再把整合包玩家拦下来。判定还依赖 `_ResourcePack.esl`(AE 免费 CC 内容,
1.5.97 上根本不存在),这也印证了它不适合当阻断条件。

**#909 组队提示**:单人、已连上服务器、附近有别的玩家、且不在队伍里时,
任务**开始或推进**会弹一条 15 秒提示,提供「打开指南 / 忽略 / 不再显示」,
冷却 15 分钟;`None` 与 `Miscellaneous` 类型不触发(隐藏的后台任务一直在推进,
太吵)。本仓库按自己的写法落地:
- 判「任务是不是刚开始」用的是本仓库既有的 `pQuest->IsStopped()`,
  **没有**采用上游给 `TESQuestStartStopEvent` 新加 `started`/`failed` 两个字段的做法 ——
  那两个字段来自引擎结构体布局,本仓库无法在 1.5.97/AE 两边都验证,
  而 `IsStopped()` 是已在用的判据。
- 提示文案与三个按钮键(`OPEN_GUIDE` / `DISMISS` / `DONT_SHOW`)加进了**全部 14 个语言文件**;
  上游只改了 de/en/ru 三个,其余语言会退回英文。

**验证**:`python Tools/Scripts/check_facegen_scope.py`(脸部开关门禁)与
`python Tools/Scripts/check_docs.py`(文档门禁)均通过;14 个语言文件逐个解析,
新增 4 个键**一个不缺、占位符与英文逐键一致**;所有改动的 `.cpp/.h` 花括号收支为 0。
**本机不编译**(无 xmake/MSVC 工具链),以上都是静态判据。

### 修复:自定义种族联机时队友的脸仍然看不见 —— 上一版只把开关拨对了,却从没要求引擎跑那趟生成

**现象**:`v1.1.6` 的「自定义种族的头不显示」修复(`ca24d5ce`)发布之后,报告者回报**没有修好**,
联机时依旧看不到自定义种族队友的脸。这是同一个 bug 的第二次修复。

**上一版为什么不够**:那一版把 `bUseFaceGenPreprocessedHeads` 在「外观来自网络」的角色身上置 0,
并且论证了写入目标、写入时机、还原方式都对 —— 这些**全部是对的**,但**少了一半**:
这个开关只决定引擎那趟**脸部生成过程内部**走哪条分支,它本身**不会让那趟过程发生**。
客户端从头到尾**没有任何一处要求引擎为这个 actor 跑一趟生成**,于是引擎按自己的节奏去建头,
而 `UpdateRemoteFaceGenWindow()` 又在同一个 tick 里、`WaitingFor3D` 被摘掉的那一刻就把开关**放回去了**。
结果就是:开关确实被按下过,但那趟会读它的生成过程要么跑在开关已经还原之后,要么根本没人叫它。

**取证(全部来自本机 `SkyrimSE.exe` 1.5.97 的反汇编,不是推断)**:

| 查证 | 地址/命令 | 结论 |
| --- | --- | --- |
| 引擎自己怎么写这个开关 | `0x14031c153` | `GetSetting` → 存 `+0x8` → 写 `0` → **调 `Actor::QueueUpdate`** → 还原 |
| `Actor::QueueUpdate` 是谁 | `0x140693110`(AE **40255**) | 正是 `Actor.cpp:211` 里那个 `QueueUpdate`,参数 `(this, bool)` 也对得上 |
| 生成过程在哪读开关 | `0x140363e38` / `0x140363e61` | 在 `0x140363de0` 内,用 `movzbl 0x141dea030` 直接读那个字节 |
| 生成过程从哪进 | `0x140363de0` ← `0x140650df0`(AE 39395) | 而 `0x140650df0` **正是 `QueueUpdate` 的直接被调方**(深度 1) |
| 写入目标对不对 | `FindAddressById(411155)` = `base+0x301d758` | 与引擎 `mov 0x2d015c7(%rip),%rcx # 0x14301d758` 是同一个单例 ⇒ **上一版的写入目标没错** |

也就是说:引擎**自己**在想要一张运行时脸时的做法,就是「把开关按下 → **调 `Actor::QueueUpdate`** → 还原」。
仓库里 `Actor::QueueUpdate()` 写的就是这个括号,却**从来没有被任何地方调用过**
(v1.0.40/1.0.41/1.1.x/HEAD/upstream master/dev/v1.7.1/v1.8.0 全 grep 过,只有定义)。
上一版甚至**专门写了一段理由**说明为什么不接线它 —— 那段理由把「引擎多帧建身体」当成了「不能只调一次」,
但事实是 `QueueUpdate` **本身就是**那趟生成过程的入口,它跟多帧建身体不是互斥的两件事。

**修法**:在 `RunRemoteUpdates` 里、`WaitingFor3D` **还没被摘掉**的那一刻(也就是窗口还开着),
对「外观来自网络」的 actor 补上这一趟请求:

```cpp
if (waitingFor3D.SpawnRequest.FormId == GameId{})
    pActor->QueueUpdate();
```

三处配套:

1. **定界与窗口同源** —— 同样用 `SpawnRequest.FormId == GameId{}`,本地加载序里解析出来的 base 保留它的烘焙头;
2. **顺序** —— 必须在 `m_world.remove<WaitingFor3D>` **之前**,否则开关已经被还原,这趟生成又会走烘焙分支;
3. **`Actor::QueueUpdate` 补判空** —— 它现在在活路径上,`INISettingCollection::Get()` 或 `GetSetting` 返回空时
   原来会直接解引用(那个函数从没被调用过,所以从没暴露)。

**验证**:`Tools/Scripts/check_facegen_scope.py` 增加第 5、6 条判据(要求那趟请求存在、按空 `FormId` 定界、
在摘掉 `WaitingFor3D` 之前、且 `OnUpdate` 里窗口求值在请求之前),**反向验证三种改坏方式全部被抓到**
(整段删掉 / 去掉定界 / 把 `FormId` 比较反过来),基线未改动时通过。
门禁在真树上跑:**`FACEGEN SCOPE CHECK OK`**。

> 这一节最该记住的是:**「把开关拨对」和「让读开关的那段代码跑起来」是两件事**。
> 上一版把全部注意力放在「写哪个地址、什么时候写、怎么还原」上,每一条都验得很干净,
> 却从没问过「谁会让引擎去读它」—— 而死代码 `Actor::QueueUpdate()` 就在仓库里等着被问这一句。

### 修复:同一个审计脚本漏掉的 9 个 `noexcept` 分配点(客户端 8 个 + `es_loader` 1 个)—— 判据是「先问脚本漏了什么 token,再问剩下的算不算缺陷」

**现象**:没有任何现象。前面三批(`941bd606` 服务端、`f0877c1b` 客户端、`84649881` 编码层)用的分配标记是 `push_back|emplace|insert\(|resize|reserve|make_shared|make_unique|new|String|Vector|Map|spdlog`。这一批把标记换成**逐行、只看函数体、屏蔽注释与 `#if 0`** 之后,又找出 9 个:

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

前六个是 `insert` 的两种写法之一被漏掉:旧标记里写的是 `\binsert\b`,但 `ActorValues::Deserialize` 那批用的是 `insert({key, value})`,而第一轮脚本用 `insert\s*\(` 时又把 `map.insert(...)` 的普通调用漏在外面 —— 两轮各漏一半。这一轮改成**逐行扫描 + 逐 token 匹配**,并把 `MakeUnique`/`MakeShared`/`std::to_string`/`std::ifstream`/`fs::is_directory` 也加进标记表。

**判据(同族对照,载重)**:

| 对照 | 事实 | 结论 |
| --- | --- | --- |
| 前一批的修法可以直接复用 | `Code/client/Diagnostics.h`(`f0877c1b`)经 `TiltedOnlinePCH.h:52` 进入客户端的每个 TU | 8 个客户端站点用 `Diagnostics::ReportFailure`,与那一批同形 |
| `es_loader` **不在**那条 include 链上 | 它有自己的 xmake target(`Code/components/es_loader/xmake.lua`)和自己的强制包含 `stdafx.h`,后者只 `#include <spdlog/spdlog.h>` | 这一处用**本地 `try`/`catch` + `spdlog::error`**,与 §106 的编码层同理:先问这一层能不能用那个 helper |
| 编码层留下的那条规则仍然成立 | `Code/encoding` 依旧零 `spdlog` 引用 | 这一批没有碰它 |
| `es_loader` 里其余 8 个站点**不是**缺陷 | `Code/components/es_loader/ESLoader.cpp:49-54` 里 `LoadFiles()` 的调用被 `/* */` 注释掉,`BuildRecordCollection` 在 `:47` 直接 `return MakeUnique<RecordCollection>();`;`pluginFile.LoadFile`(`:127`)与 `IndexRecords` 的唯一调用点都在 `LoadFiles` 内 | `TESFile::LoadFile`/`IndexRecords`、`RecordCollection::GetFormType`、`Chunks::ParseValue`、`CONT`/`GMST`/`TES4::ParseChunks`、`TESFile::GetFormIdPrefix` 全在注释块之后的死路径上,与 §104 的 `#if 0` 三站点同类,判为**不改** |
| 「同名不同层」要分开看 | `Actor::GetEssentialActorValues` 与 `ActorValueService::CreateActorValuesComponent` 都写 `ActorValuesList`,前者返回快照、后者写组件 | 回退语义不同,注释分别写清楚 |

**可达性**:8 个客户端站点全在帧循环或收包路径上,没有一条需要管理员:

- **tick**:`ActorValueService::OnUpdate` → `BroadcastActorValues`(每 tick 遍历全部本地 actor);`DiscoveryService::OnUpdate` → `VisitForms`(每 tick 遍历 `ProcessLists::highActorHandleArray`);`AnimationSystem` 的 replay 入队;`RunnerService::OnUpdate` → `m_runner.Drain()`。
- **hook 回调**:`SkyrimVM64.cpp:68 DescribeOffset` 由 `HookVMUpdate` 调用(`:136`),每 `kTickHeartbeatEvery` 个 tick 一次。
- **启动**:`ESLoader::BuildRecordCollection` 由 `Code/server/World.cpp:47` 在 `World` 构造时调用一次。

触发条件是堆耗尽。**本机不编译**(`docs/PITFALLS.md` §0/§1),所以每一条都是静态读数。

**修法**:9 个函数体加 `try`/`catch`,**一个 `noexcept` 都没有删**。回退值:

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

`ESLoader` 那一处的报告是本地写的,而且**自己再套一层 `try`/`catch`** —— 与 `Code/server/Diagnostics.h` 和 `Code/client/Diagnostics.h` 里那句注释同一个理由:失败的报告本身也会失败。

**验证**:`git diff --numstat -- Code` = **9 files changed, 260 insertions(+), 135 deletions(-)**,52 个 hunk。逐文件:`client/Games/Skyrim/Actor.cpp` +18/-6(1596→1608)、`client/Games/Skyrim/CellRespawnOverrides.cpp` +4/-0(168→172)、`client/Services/Generic/ActorValueService.cpp` +75/-49(457→483)、`client/Services/Generic/DiscoveryService.cpp` +59/-46(380→393)、`client/Services/Generic/ObjectService.cpp` +25/-14(578→589)、`client/Services/Generic/RunnerService.cpp` +13/-1(21→33)、`client/SkyrimVM64.cpp` +12/-1(182→193)、`client/Systems/AnimationSystem.cpp` +16/-4(196→208)、`components/es_loader/ESLoader.cpp` +38/-14(151→175)。

这 9 个文件的 `try`/`catch` 计数从 **29 / 29 到 39 / 39**,新增 10 对(9 个函数体 + `ESLoader` 里那层嵌套的报告守卫)。`Diagnostics::ReportFailure` 新增调用点 9 个。

全仓普查(15 个 target、`noexcept` 定义 1514 个)在**换成逐行 + 完整 token 表**之后,剩下的未守站点只有 `Code/encoding/WireLimits.h:84 Clamp`(向下 `resize`,§106 已判)与 `es_loader` 的 8 个死路径站点(本节已判)。
每个改过的文件都做过花括号/圆括号配平、`try`/`catch` 缩进配对与行尾检查;`Code/client/Games/Skyrim/CellRespawnOverrides.cpp` 与 `Code/components/es_loader/ESLoader.cpp` 都补上了报告,不留空 `catch`。
**本机不编译**(§0/§1),所以判据是静态的。

### 修复:编码层 `Code/encoding` 的 503 个 `noexcept` 一个 `catch` 都没有,25 个在按对端声称的条数分配 —— 判据是「这一层不记日志,所以 `catch` 只能就地恢复」

**现象**:没有任何现象。`Code/encoding/**` 共 352 个 `.cpp/.h`,里面 **503 个 `noexcept` 函数定义**,体内含 `catch` 的 **0 个**,全文 `try`/`catch` 计数 **0 / 0**。同一个脚本数出 **25 个**定义在按线上的长度分配 —— 20 个 `resize`,4 个 `insert`,1 个 `push_back`。

这一层是前两批(`941bd606` 服务端、`f0877c1b` 客户端)的共同底座:`Code/client/xmake.lua:41`、`Code/server/xmake.lua:44`、`Code/admin/xmake.lua:18`、`Code/tests/xmake.lua:9` 四个 target 都 `add_deps("SkyrimEncoding")`。客户端的 `TransportService::OnConsume` 与服务端的 `GameServer::OnConsume` 解析出的每一个包,都要先经过这里的 `DeserializeRaw`。

**根因**:`noexcept` 写在函数身上,分配发生在函数体里,没有 catch —— `std::bad_alloc` 穿出去就是 `std::terminate`。

**判据(同族对照,载重)**:

| 对照 | 事实 | 结论 |
| --- | --- | --- |
| **这一层没有日志,而且是有意的** | `git grep -rn "spdlog\|fmt::" -- Code/encoding` **零命中**;`Code/encoding/xmake.lua:18 add_packages("hopscotch-map", "glm", "tiltedcore")` 没有 `spdlog`;`Code/tests/xmake.lua:10-15` 也不加 | 前两批用的 `Diagnostics::ReportFailure` 在这里**用不了** —— 加它会改这一层的依赖图,还会把 spdlog 拖进只依赖 `SkyrimEncoding` 的 `TPPTests` |
| 同层已有的恢复形状 | `Code/encoding/Structs/CachedString.cpp:37-47`(commit 早期)在 `noexcept` 里用 `*this = TiltedPhoques::String();` 恢复,注释逐字「It used to assert(false), which aborts the process: this frame is noexcept, so there is no way to fail softly」 | 修法不是新发明:同一层早就用「就地恢复」而不是「上报」 |
| `WireLimits.h` 的自我说明 | `:81-83`「Resizing down cannot throw」 | 唯一留在原样的站点正是向下 `resize`,不是遗漏 |
| 上界的值由我们自己的发送侧决定 | 20 个 `resize` 里的条数分别来自 8 位、13 位、16 位、VarInt,而 `kMaxObjectEntities`/`kMaxEntryCount`/`kMaxActorValueCount` 等上界已经先夹过一遍 | 所以这里要防的不是「超长」,是「夹过之后仍然会分配的那一次失败」 |
| 「下游有守卫」不等于「值被挡住了」 | `Messages/ClientMessageFactory.cpp:32-42` 与 `ServerMessageFactory.cpp` 的 `Extract` 都 `noexcept`,调用 `s_*MessageExtractor[opcode](aReader)`,而每个 extractor 直接 `ptr->DeserializeRaw(aReader)` | 抛出点在里面一层,工厂的 `noexcept` 挡不住它 |

**可达性**:25 个站点全部落在两条路径上,没有一条需要管理员:

- **收包解析**:`ClientMessageFactory::Extract`(`Code/encoding/Messages/ClientMessageFactory.cpp:32`,服务端 `GameServer.cpp:609` 调用)与 `ServerMessageFactory::Extract`(客户端 `TransportService.cpp:200` 调用)都 `noexcept`,都调 `DeserializeRaw`。任何一个已连接的对端都能触发 8 个 `Messages/*` 站点。
- **状态与结构体**:`Structs/{ActorValues,Inventory,Factions,Mods,QuestLog,ReferenceUpdate,Tints,ActionReplayChain}.cpp` 与 `StringCache.cpp`。`StringCache::Add` 还有两个非收包调用点:`Code/server/GameServer.cpp:1043`(`StringCache::Get().Serialize(startId)`)与 `Code/encoding/Structs/CachedString.cpp:27/:62`(`AddWanted`)。

触发条件是堆耗尽。**本机不编译**(`docs/PITFALLS.md` §0/§1),所以每一条都是静态读数。

**修法**:21 个函数体加 `try { ... } catch (...) { <就地恢复> }`,**一个 `noexcept` 都没有删**(503 → 503)。回退值都选「与空消息同一个答案」:

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
| `StringCache::Add` | `m_stringToId.erase(acValue); return kInvalidId;` | 见下 |

`StringCache::Add` 是这一批里唯一需要**新增一个常量**的地方。`Deserialize` 靠「`Add` 返回的 id 是否等于自己数出来的游标」判断这条消息能不能继续应用,所以失败时必须返回一个**任何游标都不可能等于**的值:

```cpp
// Code/encoding/StringCache.h:13-18(新增)
static constexpr uint32_t kInvalidId = 0xFFFFFFFFu;

// Code/encoding/StringCache.cpp:44-48
catch (...)
{
    m_stringToId.erase(acValue);
    return kInvalidId;
}
```

两个容器都分配,失败时把先写的那张表撤掉,缓存仍然是发送方的前缀;返回 `kInvalidId` 让 `Deserialize` 走它本来就有的「id 对不上就停下」分支。

`WireLimits::Clamp`(`:84-88`)是**唯一留在原样**的站点,理由写在它自己的注释里:「Resizing down cannot throw」—— 它只在 `aValue.size() > aMaxBytes` 时调用 `resize(aMaxBytes)`,是向下收缩。

**验证**:`git diff --numstat -- Code/encoding` = **22 files changed, 560 insertions(+), 305 deletions(-)**,115 个 hunk。逐文件行数:`Messages/AssignObjectsRequest.cpp` 29→40、`AssignObjectsResponse.cpp` 29→38、`NotifyActorMaxValueChanges.cpp` 50→61、`NotifyActorValueChanges.cpp` 50→61、`NotifyPartyInfo.cpp` 39→48、`NotifyPartyJoined.cpp` 39→48、`NotifyPluginMessaging.cpp` 44→54、`PluginMessagingRequest.cpp` 49→58、`RequestActorMaxValueChanges.cpp` 50→61、`RequestActorValueChanges.cpp` 50→61、`ShiftGridCellRequest.cpp` 42→51、`StringCacheUpdate.cpp` 36→46、`StringCache.cpp` 143→175、`StringCache.h` 31→37、`Structs/ActionReplayChain.cpp` 45→55、`Structs/ActorValues.cpp` 88→99、`Structs/Factions.cpp` 56→66、`Structs/Inventory.cpp` 215→243、`Structs/Mods.cpp` 51→61、`Structs/QuestLog.cpp` 56→65、`Structs/ReferenceUpdate.cpp` 46→56、`Structs/Tints.cpp` 77→86。

| 读数 | `HEAD` | 现在 |
| --- | --- | --- |
| `noexcept` 定义数 | 503 | 503 |
| 体内含 `catch` | 0 | 25 |
| 体内有分配标记且无 `catch` | 25 | **1**(`WireLimits::Clamp`,向下 `resize`) |
| `Code/encoding` 全文 `try`/`catch` | 0 / 0 | 25 / 26 |

**这一层仍然不依赖 `spdlog`**:`git grep -rn "spdlog\|fmt::" -- Code/encoding` 改前改后都是零命中,`Code/encoding/xmake.lua:18` 与 `Code/tests/xmake.lua:10-15` 的包列表一个字没动。`Code/tests/encoding.cpp` 的 8 个 `StringCache` 测试全部依赖 `Add` 返回真实 id(其中 `:1595-1596` 直接 `REQUIRE(cId == 0)`),新常量只在 `catch` 路径上返回,成功路径的返回值不变。
每个改过的文件都做过花括号/圆括号配平、`try`/`catch` 缩进配对与行尾检查。`Code/encoding/xmake.lua:9 set_pcxxheader("EncodingPch.h")` 与 `:6-7 add_headerfiles("**.h")`/`add_files("**.cpp")` 覆盖全部 22 个文件。
**本机不编译**(§0/§1),所以判据是静态的。

### 修复:客户端 `Code/client` 里 698 个 `noexcept` 只有 9 个写了 `catch`,142 个在自己分配或格式化日志 —— 判据是「承诺写在哪个函数身上,`catch` 就得写在哪个函数体里」

**现象**:没有任何现象。`Code/client/**` 共 446 个 `.cpp/.h/.inl`,里面 **698 个 `noexcept` 函数定义**,体内含 `catch` 的只有 **9 个**;同一个脚本数出 **142 个**定义在自己分配内存或调用 `spdlog`/`fmt::format`。`Code/client` 全文的 `try`/`catch` 计数是 **18 / 14**,`Code/encoding` 与 `Code/admin` 是 **0 / 0**。

这与刚提交的服务端那一批(`941bd606`,`Code/server` 211 → 212 个 `noexcept`、6 → 70 个 `catch`)是同一族缺陷的另一半:那一批只覆盖了 `Code/server`,客户端从来没有过边界。

**根因**:`noexcept` 写在函数身上,分配发生在函数体里,而 catch 一个都没有 —— `std::bad_alloc` 从体内穿出去就是 `std::terminate`。客户端比服务端更糟的一点是:**`Code/client` 里没有任何 `set_terminate`、`std::terminate`、`AddVectoredExceptionHandler` 或 `SetUnhandledExceptionFilter`**(`git grep -rn -E "set_terminate|std::terminate|SetUnhandledExceptionFilter|AddVectoredExceptionHandler" -- Code/client` 零命中)。`CrashHandler` 装的是 `LPTOP_LEVEL_EXCEPTION_FILTER`,而 terminate 是 C++ 运行时的 `abort()`,不经过它 —— 所以这一族在客户端连 dump 都没有。

**判据(同族对照,载重)**:

| 对照 | 事实 | 结论 |
| --- | --- | --- |
| 本仓已有的正确形状 | `Code/server/Diagnostics.h`(commit `941bd606` 新增,27 行)与更早的 `ScriptService::ReportFailure`(`e7253f3b`) | 修法不是新发明,是让客户端用同一个 helper |
| 「同族其它实现长什么样」 | 客户端已有的 9 个 catch 里,8 个是 `PluginMessagingService.cpp` 自己的 `Log()`(`cd74a67e`) | 全客户端只有那一处边界 |
| 日志本身会分配 | `Code/client/xmake.lua` 没有定义 `SPDLOG_NO_EXCEPTIONS`;`TiltedOnlineApp.cpp:75 spdlog::flush_every(std::chrono::seconds(1))`、`:80 logger->flush_on(spdlog::level::warn)` | 与 `docs/PITFALLS.md` §103 第三条教训同一格:失败的报告本身也会失败 |
| 「下游有守卫」不等于「值被挡住了」 | `Code/client/Services/TransportService.h` 的 `OnConsume`/`OnConnected`/`OnDisconnected`/`OnUpdate` 四个覆写**都不带 `noexcept`** | 但 handler 自己是 ⇒ 抛出仍在 handler 里 terminate,上游非 `noexcept` 救不了它 |
| `#if 0` 里的分配不算 | 3 个站点(`DebugService::OnUpdate`、`CombatService::OnHitEvent`、`CombatService::RunTargetUpdates`)的全部命中都在 `#if 0` 块内 | 逐行核对后不改:`OnUpdate` 的 10 行活代码不分配,另外两个函数体整个是 `#if 0` |

**可达性**:142 个站点落在四条路径上,没有一条需要管理员:

- **收包**:`Code/client/Services/TransportService.cpp:181-220 OnConsume`(服务端可发的每一种包)→ `:222 DispatchMessage` → 每个服务的 `OnNotify*`。这条路径上还有 `CharacterService.cpp` 的 30 个定义,是客户端最密的一处。
- **tick**:`TransportService::HandleUpdate` → 各服务的 `Run*`(`RunRemoteUpdates`、`RunSmallHealthUpdates`、`RunNakedNPCBugChecks`、`ApplyQueuedEffects`、`RunExperienceUpdates`、`ApplyCachedWeaponDraws`、`RunWeatherUpdates`、`ProcessLeveledConforms`)。
- **握手泵**:`TransportService::HandshakeThreadMain` / `PumpConnectionLocked` / `PumpHeartbeat` —— 按 `TransportService.h` 的注释,这条路径上的线程**不是游戏线程**。
- **hook 回调**:`HookAudit::Record/Report/Verify`、`GamePatch.h` 的 6 个字节补丁 helper、`Actor`/`TESObjectREFR`/`PlayerCharacter` 等引擎侧 helper。

**修法**:新增 `Code/client/Diagnostics.h`(27 行),与 `Code/server/Diagnostics.h` 逐字同形,经 `Code/client/TiltedOnlinePCH.h:50-52` 进入每个编译单元。选 PCH 而不是逐文件 include:`Code/client/xmake.lua:17 set_pcxxheader("TiltedOnlinePCH.h")` 已把它设成强制包含,而 `TiltedOnlinePCH.cpp` 只有一行 `#include <TiltedOnlinePCH.h>` —— 不这么写就会有一个不包含它的 TU。

42 个文件、142 个函数体加了 `try { ... } catch (...) { Diagnostics::ReportFailure(__FUNCTION__, "..."); }`,**一个 `noexcept` 都没有删**(698 → 699,新增的是 `Diagnostics::ReportFailure` 自己)。非 void 的补了显式回退:

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

**验证**:`git diff --numstat -- Code/client` = **42 files changed, 5561 insertions(+), 4126 deletions(-)**,1318 个 hunk;`Code/client/Diagnostics.h` 是唯一的新文件。同一个普查脚本在 `HEAD` 与工作树上各跑一次:

| 读数 | `HEAD` | 现在 |
| --- | --- | --- |
| `noexcept` 定义数 | 698 | 699 |
| 体内含 `catch` | 9 | 149 |
| 体内有分配/日志标记且无 `catch` | 142 | **3** |
| 其中命中全在 `#if 0` 内 | 0 | 3(逐行核对) |
| `Code/client` 全文 `try`/`catch` | 18 / 14 | 158 / 164 |

`Diagnostics::ReportFailure` 调用点 **139 个,分布在 41 个文件**:`CharacterService.cpp` 30、`Actor.cpp` 9、`MagicService.cpp` 9、`ObjectService.cpp` 8、`TransportService.cpp` 8、`TESObjectREFR.cpp` 7、`OverlayService.cpp` 7、`GamePatch.h` 6、`ActorValueService.cpp` 5、`InventoryService.cpp` 5、`PartyService.cpp` 4、`WeatherService.cpp` 4,其余各 1–3。

留在原样的三处不是遗漏,是**逐行核对过命中全在 `#if 0` 里**:`DebugService::OnUpdate` 的活代码只有窗口查找与一次挂起的 `MoveTo`,不分配;`CombatService::OnHitEvent` 与 `RunTargetUpdates` 的函数体整个是 `#if 0`。

`Code/client/xmake.lua:13-14 add_headerfiles("**.h")` / `add_files("**.cpp")` 覆盖新头与全部 42 个文件;`:17 set_pcxxheader("TiltedOnlinePCH.h")` 保证 `Diagnostics.h` 进入每个 TU。每个改过的文件都做过花括号/圆括号配平、`try`/`catch` 缩进配对与行尾检查(工作树里 6 个文件本来就是 LF,与 `HEAD` 一致,未动;`Code/client/Components/LocalAnimationComponent.h` 是其中之一,现在仍是 LF)。
**本机不编译**(`docs/PITFALLS.md` §0/§1),所以判据是静态的:分配点是逐字读出来的,`noexcept`、`try`/`catch` 与 `#if 0` 的判定都是脚本数出来的。

### 修复:服务端 `Code/server` 里 211 个 `noexcept` 只有 6 个写了 `catch`,而其中 27 个在自己分配、44 个在格式化日志 —— 判据是「`noexcept` 是一条承诺,失败的报告本身也会失败」

**现象**:没有任何现象。`Code/server/**` 共 100 个 `.cpp/.h/.inl`,里面 **211 个 `noexcept` 函数定义**,体内含 `catch` 的只有 **6 个**。用等长清空注释与字面量、再按花括号配平取函数体的脚本逐个数,得到两族分配点,它们有重叠:

```
家族一:体内自己分配内存(27 个定义)
  Code/server/Components/ModsComponent.cpp:5  AddStandard          m_standardMods.emplace(...)
  Code/server/Components/ModsComponent.cpp:20 AddLite              m_liteMods.emplace(...)
  Code/server/Game/Animation/ActionReplayCache.cpp:5  FormRefinedReplayChain  Vector<ActionEvent>{begin+1, end}
  Code/server/Game/Animation/ActionReplayCache.cpp:18 AppendAll            m_actions.push_back(action)
  Code/server/Game/Animation/ActionReplayCache.cpp:31 RefineReplayCache    action.EventName = String{*instantAnimName}
  Code/server/Game/Animation/ActionReplayCache.cpp:64 IsExitAction         String lowercaseName{acAction.EventName}
  Code/server/Game/PlayerManager.cpp:27       Create               m_players.emplace(id, MakeUnique<Player>(...))
  Code/server/GameServer.cpp:1149             SanitizeUsername     String username = acUsername;
  Code/server/GameServer.h:92                 AddAdminSession      m_adminSessions.insert(acSession)
  Code/server/Services/ActorValueService.cpp:23/:50/:77   ...ActorValuesList[id] = value / notify.Values = message.Values
  Code/server/Services/CharacterService.cpp:251/:374/:587/:904/:960      push_back / 8 次 emplace / Map<...>
  Code/server/Services/ObjectService.cpp:31/:60           Vector<entt::entity> toDestroy / response.Objects.push_back
  Code/server/Services/PartyService.cpp:113/:226/:273/:320/:439/:463/:487  m_parties[partyId] / push_back / Players[id] = ...
  Code/server/Services/QuestService.cpp:27   OnQuestChanges       dedupHistory.emplace_back(...)

家族二:体内只有日志格式化,自己不含分配标记(44 个定义,共 106 条 spdlog 调用)
  Code/server/Game/PlayerManager.cpp:16                    PlayerManager::Get
  Code/server/Services/CharacterService.cpp  x11           :190 :332 :362 :445 :495 :508 :560 :573 :681 :745 :794
  Code/server/Services/InventoryService.cpp  x3            :28 :99 :148
  Code/server/Services/MagicService.cpp      x4            :24 :40 :53 :72
  Code/server/Services/ActorValueService.cpp x1            :102
  Code/server/Services/OverlayService.cpp    x1            :29  sendPlayerMessage
  Code/server/Services/PartyService.cpp      x1            :190 OnPartyKick
  Code/server/Services/PlayerService.cpp     x1            :153 OnPlayerRespawnRequest
  Code/server/Services/PluginMessagingService.cpp x1        :54  OnPluginMessage
  Code/server/Services/QuestService.cpp      x1            :309 QuestStageDedupHistory::Expire
  Code/server/Services/CombatService.cpp     x1            :15
  Code/server/Services/AdminService.cpp      x1            :14  HandleShutdown
```

**根因**:`noexcept` 写在函数身上,分配发生在函数体里,而 catch 一个都没有 —— 一个 `std::bad_alloc` 从体内穿出去就是 `std::terminate`,没有 dump,也没有日志。

家族二更隐蔽:**日志本身会分配**。`Code/server/xmake.lua:47` 固定 `spdlog`(v1.13.0),全仓没有任何地方定义 `SPDLOG_NO_EXCEPTIONS`(`git grep -rn -E "SPDLOG_ACTIVE_LEVEL|SPDLOG_LEVEL|SPDLOG_NO_EXCEPTIONS" -- Code xmake.lua` 零命中),所以 `spdlog::error`/`warn` 会把参数格式化进一个新建的 buffer —— 在一个 `noexcept` 的 catch 里直接调它,等于把 terminate 从原来的位置挪到处理它的位置上。这正是 `docs/PITFALLS.md:14097-14098`(§103 第三条教训)已经写下的那句「失败的报告本身也会失败」,只是当时只落在了 `ScriptService` 一个文件里。

**判据(同族对照,载重)**:

| 对照 | 事实 | 结论 |
| --- | --- | --- |
| 本仓已有的正确形状 | `Code/server/Services/ScriptService.cpp:252-263`(commit `e7253f3b`)的 `ReportFailure` 是 `noexcept` + 内层 `try/catch(...)` | 修法不是新发明,是把这个形状搬给别的服务用 |
| 同族的下一次提交 | 同一批的 51 个站点里,`ScriptService.*` 已经修完(14 个 `noexcept` 里 13 个在分配) | 剩下的 211 个定义是同一族,只是没轮到 |
| 「同族其它实现长什么样」 | `git grep -n -E "\bcatch \(" -- Code/server` 在本次修改前是 **14 处**,其中 2 处在 `ScriptService` | 全仓的服务层没有第二处边界 |
| 日志级别不是编译期属性 | `Code/server_runner/main.cpp:43 Console::StringSetting sLogLevel{"sLogLevel", "Log level to print", "info"}`、`:74 globalOut->set_level(level::from_str(sLogLevel.value()))` | 「`debug` 默认不打」是运行期设置,不是代码性质;44 个定义里的 `debug` 行同样要守 |
| 「下游有守卫」不等于「值被挡住了」 | `GameServer.h:103/104/106` 的 `OnUpdate`/`OnConsume`/`OnDisconnection` 三个覆写**都不带 `noexcept`** | 但 handler 自己是 `noexcept` ⇒ 抛出仍然在 handler 里 terminate,上游非 `noexcept` 救不了它 |

**可达性**:这 71 个站点全部落在三条路径上,没有一条需要管理员:

- **tick 线程**:`GameServer.cpp:557-571 OnUpdate` → `:567 dispatcher.trigger(UpdateEvent{cDeltaSeconds})`,五个 sink(`CalendarService.cpp:18`、`CharacterService.cpp:53`、`PartyService.cpp:24`、`ScriptService.cpp:57`、`StringCacheService.cpp:10`)。`CharacterService::OnUpdate` 又转调 `ProcessFactionsChanges`(2 秒一次)与 `ProcessMovementChanges`(20 ms 一次)—— 后者每个 tick 为每个在线玩家建一张 `Map<uint32_t, ServerReferencesMoveRequest>`。
- **收包**:`GameServer.cpp:573-618 OnConsume` → `:616 m_messageHandlers[pMessage->GetOpcode()](pMessage, aConnectionId)`。任何一个已连接玩家都能发:聊天、任务更新、任务场景、队伍创建/邀请/接受/踢人/换队长、背包/装备、移动、法术/投射物、物件分配。
- **断连**:`GameServer.cpp:626-686 OnDisconnection` → `:645 trigger(PlayerLeaveEvent)` → `PartyService::OnPlayerLeave` → `RemovePlayerFromParty` + `BroadcastPlayerList`,后者是 O(n²) 条消息、每条一个 `Map<uint32_t, String>`。

触发条件本身是堆耗尽。本机不编译(`docs/PITFALLS.md` §0/§1),所以每一条都是静态读数,不是复现。

**修法**:新增 `Code/server/Diagnostics.h`(27 行),把 `ScriptService` 那个私有静态提升成一个每个服务都能用的自由函数:

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

它经 `Code/server/Pch.h:29 #include <Diagnostics.h>` 进入所有编译单元。选 `Pch.h` 而不是逐文件 include,是因为 `Code/server/xmake.lua:9 set_pcxxheader("Pch.h")` 已经把它设成强制包含:`Code/server/Pch.cpp` 是**空文件**,所以不这么写就会有一个不包含它的 TU。

18 个文件、71 个函数体加了 `try { ... } catch (...) { Diagnostics::ReportFailure(__FUNCTION__, "..."); }`,**一个 `noexcept` 都没有删**(211 → 212,新增的那一个是 `Diagnostics::ReportFailure` 自己)。三个非 void 的函数补了显式回退值:

| 函数 | catch 里的回退 | 语义 |
| --- | --- | --- |
| `PlayerManager::Get` | `return nullptr;` | 与「没有实例」同一个答案,调用方本来就处理 |
| `PlayerManager::Create` | `return nullptr;` | `GameServer.cpp:1006-1013` 已经把这个答案当成「该连接已有玩家」 |
| `ModsComponent::AddStandard`/`AddLite` | 无(先取 `const auto id = m_seed++;` 再 `try`) | id 必须唯一:`Code/client/Systems/ModSystem.cpp:88-101` 用它建 `m_serverToGame`/`m_standardToServer` 反查表,两个 mod 共用一个 id 会让客户端派生的每个 form id 撞车,比「这一条没去重」更坏。`emplace` 失败只丢去重行 —— 服务端除这个 `find` 之外没有任何读者 |
| `GameServer::SanitizeUsername` | `return String{};` | 退化成空名,查找失败并打印,唯一调用点是 `GameServer.cpp:436` 的控制台命令 |
| `ActionReplayCache::RefineReplayCache`/`IsExitAction` | `return false;` | 「不是退出动作」是安全答案:动作留在缓存里,客户端不重置动画图 |
| `CharacterService::CanClaimOwnership`/`TransferOwnership` | `return false;` | 「不能认领」/「没转移成功」,调用方不会广播一次只做了一半的交接 |

`ScriptService` 那份私有静态**保持不动**。它与新的自由函数行为完全一致,把 8 个调用点改成 `Diagnostics::` 是纯改名、零行为差,只会让这次 diff 变大;两边并存不产生第二种语义。

**验证**:`git diff --numstat -- Code/server` = **18 files changed, 2196 insertions(+), 1564 deletions(-)**;`git status` 里 `Code/server/Diagnostics.h` 是唯一的新文件。

同一个普查脚本在 `HEAD` 与工作树上各跑一次:

| 读数 | `HEAD` | 现在 |
| --- | --- | --- |
| `noexcept` 定义数 | 211 | 212 |
| 体内含 `catch` | 6 | 70 |
| 体内有分配标记且无 `catch` | 27 | **0** |
| 体内有 `spdlog::` 且无 `catch` | 44(106 条日志) | **0**(0 条) |
| `Code/server` 全文 `try`/`catch` 计数 | 11 / 14 | 75 / 80 |

`Diagnostics::ReportFailure` 调用点 **63 个,分布在 17 个文件**:`CharacterService.cpp` 18、`PartyService.cpp` 10、`OverlayService.cpp` 5、`ActorValueService.cpp` 4、`ActionReplayCache.cpp` 4、`MagicService.cpp` 4、`InventoryService.cpp` 3、`QuestService.cpp` 3、`ModsComponent.cpp` 2、`PlayerManager.cpp` 2、`ObjectService.cpp` 2、`GameServer.cpp`/`GameServer.h`/`AdminService.cpp`/`CombatService.cpp`/`PlayerService.cpp`/`PluginMessagingService.cpp` 各 1。

`Code/server/xmake.lua:12-13 add_headerfiles("**.h")` / `add_files("**.cpp")` 覆盖新头与全部 18 个文件;`:9 set_pcxxheader("Pch.h")` 保证 `Diagnostics.h` 进入每个 TU。每个改过的文件都做过花括号/圆括号/方括号配平与行尾检查(`Code/server/Services/PluginMessagingService.{cpp,h}` 本来就没有文件尾换行,与 `HEAD` 一致,未动)。
**本机不编译**(`docs/PITFALLS.md` §0/§1),所以判据是静态的:分配点是逐字读出来的,`noexcept` 数量是脚本数出来的。

### 修复:服务端脚本层的 14 个 noexcept 里 13 个在分配,而全仓唯一有 catch 的文件把边界放在了调用者身上 —— 判据是「noexcept 的检查要顺着调用链走」

**现象**:没有任何现象。`Code/server/Services/ScriptService.h`(pin 76 行)、`ScriptService.cpp`(pin 235 行)、`ScriptService.inl`(pin 42 行)三个文件里 **14 个函数声明**都写着 `noexcept`,而其中 **13 个的函数体在分配**:

```cpp
// Code/server/Services/ScriptService.inl:3-8(pin,修复前)
template <typename... Args>
std::tuple<bool, String> ScriptService::CallCancelableEvent(const String& acName, Args&&... args) noexcept
{
    m_eventCanceled = false;

    auto& callbacks = m_callbacks[acName];   // :8 缺失即插入 ⇒ 分配
```

唯一不分配的那一个是 `:42 void OnPlayerEnterWorld(const PlayerEnterWorldEvent& acEvent) noexcept` —— 它的整个函数体是注释掉的。

**根因**:这 14 个 `noexcept` 覆盖了从脚本加载到每一帧 tick 的全部路径,而 `try`/`catch` 在 `.inl` 里是 **0/0**,在 `.cpp` 里只有 **2 处**:

| 位置(pin) | 分配点 | 谁调它 |
| --- | --- | --- |
| `ScriptService.inl:3-26 CallCancelableEvent` | `:8 m_callbacks[acName]`、`:12 sol::protected_function pf{callback}`、`:17 sol::error err = result;`、`:22`/`:25 std::make_tuple` | 六个入口 |
| `ScriptService.inl:28-42 CallEvent` | `:30 m_callbacks[acName]`、`:34 sol::protected_function pf{callback}`、`:38 sol::error err = result;` | `OnUpdate`、`HandlePlayerQuit` |
| `ScriptService.cpp:63-72 ~ScriptService`(隐式 `noexcept`) | `:68 m.clear()`、`:71 collect_garbage()` | `World` 析构 |
| `ScriptService.cpp:74-102 Initialize` | `:80 m_globals = luaVm.globals()`、`:82-83 open_libraries`、`:87 luaVm["package"]["path"] = "./?.lua"`、`:91-101` 每个 manifest 建一条 path | `Code/server/GameServer.cpp:227` |
| `ScriptService.cpp:169-197 HandlePlayerQuit` | `:171 std::string reason;` + 6 次赋值 | `Code/server/GameServer.cpp:634` |
| `ScriptService.cpp:204-217 OnUpdate` | `:211 CallEvent("onUpdate", acEvent.Delta)` | `UpdateEvent` sink |
| `ScriptService.cpp:226-229 AddEventHandler` | `:228 m_callbacks[String(acName)].push_back(acFunction);` | Lua 的 `addEventHandler` |
| `ScriptService.cpp:231-235 CancelEvent` | `:234 m_cancelReason = aReason;` | Lua 的 `cancelEvent` |
| 六个入口 `:144`/`:149`/`:154`/`:159`/`:164`/`:199` | 每个都在 `noexcept` 函数体里把字面量建成临时 `String` | 各自的 handler |

`:121 catch (std::exception& exception)` 在 `LoadScript` 里(那个函数**不** `noexcept`,而且它的 catch 是对的);`:213 catch (sol::error& exception)` 在 `OnUpdate` 里,只接 `sol::error`。

**判据(同族对照,载重)**:判据不是「这里会不会耗尽内存」,而是「**全仓唯一有 `try` 的那个文件,它的边界只覆盖了两条路径**」。

`git grep --cached -n -E "\bcatch \(" -- Code/server` 在整个 `Code/server` 只命中 **2 处**,两处都在本文件(`:121`、`:213`),其余 **0 处**。`docs/PITFALLS.md:6179-6180`(§55)记的是同一件事:「全仓 `catch` 只有 8 处」,其中就有这两处。

同族的先例是同一栋楼里刚修完的两处:

| 场景 | `noexcept` 边界在哪 | catch 写在哪 |
| --- | --- | --- |
| §101 客户端 STRPM 桥 | `extern "C"` 导出,外面没有调用者 | 函数体里 |
| §92/§93 插件补丁 | ABI 虚函数,消费方持逐字副本 | 函数体里 |
| 本节 服务端 `ScriptService` | `Code/server/GameServer.h:103 void OnUpdate() override;`、`:104 OnConsume(...)`、`:106 OnDisconnection(...)` 三者**都不带 `noexcept`** | **仍然写在函数体里** |

第三格是本节最需要解释的一格:它明明可以靠「删掉 `noexcept`」把责任推给上游,但没有。理由是承诺写在本文件身上,而调用链上确实有一个只接 `sol::error` 的 catch —— 那正是「边界放错了位置」的证据:`OnUpdate` 的 `:209-216` catch 在**调用者**身上,而分配在被调者体内(`docs/PITFALLS.md:12389-12394` §92.2 的判据逐字:「这条缺陷的判据不是「`Worker` 有没有分配」,而是**「`Worker` 的调用链上有没有人分配」**」)。

**可达性**:七个入口各有至少一条不需要管理员的路径:

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

**修法**:三个文件,13 个函数体。`noexcept` **一处不删**。

第一步不是加 `try`,是**改签名**:`CallCancelableEvent`/`CallEvent` 的第一个参数从 `const String& acName` 改成 `const char* acName`。

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

六个入口全都传字面量,而 `const char*` → `String` 的临时对象是在**调用者的 `noexcept` 函数体里、被调者 `try` 之前**构造的 —— 在被调者体内新加的 catch 结构性地够不着它。改成指针后,唯一的分配(`m_callbacks[acName]` 那一次隐式转换)回到 `try` 里面。`HandlePlayerQuit` 的 `std::string reason` 同理:

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

第二步是把 catch 放到**分配所在的那一层**:

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

第三步是**报告本身不能再失败**。`xmake.lua:47` 钉的是 `"spdlog v1.13.0"`,全仓没有定义 `SPDLOG_NO_EXCEPTIONS`,所以 `spdlog::error` 的格式化会分配:

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

**验证**:`git diff --stat` = `Code/server/Services/ScriptService.cpp | 138 ++++++++++++++++++++++++---------` / `Code/server/Services/ScriptService.h | 17 +++-` / `Code/server/Services/ScriptService.inl | 77 +++++++++++++------` / **3 files changed, 168 insertions(+), 64 deletions(-)**,7 个 hunk(`.inl` 1 个 `@@ -1,42 +1,71 @@`、`.h` 1 个 `@@ -46,14 +46,27 @@ struct ScriptService`、`.cpp` 5 个);`ScriptService.inl` 42 行 → 71 行,`ScriptService.h` 76 行 → 89 行,`ScriptService.cpp` 235 行 → 297 行。
`noexcept` 的**声明一处未动**(14 → 14,新增的第 15 个是 `ReportFailure`);`catch` 从 **2 处 → 8 处**,`try` 从 **2 处 → 8 处**,新增的 6 对全在分配点所在的那一层。
`Code/server/xmake.lua:12 add_headerfiles("**.h")`、`:13 add_files("**.cpp")` 覆盖三个文件;`.inl` 经 `Code/server/Services/ScriptService.h:89 #include "ScriptService.inl"` 进入同一个编译单元。
`Code/tests/xmake.lua` 的 target 是 `TPTests`、依赖 `SkyrimEncoding`,**不含 `SkyrimTogetherServer`**,所以服务端的改动在本仓测试里覆盖不到。
**本机不编译**(`docs/PITFALLS.md` §0/§1),所以判据是静态的:13 个函数体的分配点是逐字读出来的,`Code/server/GameServer.h:103/104/106` 三个覆写都不带 `noexcept` 也是逐字读出来的。
### 修复:服务端的任务去重历史只有时间上界、没有条数上界,而键由客户端任选 —— 判据是「同一层楼的任务日志已经写了 4096,而它们是同一类表」

**现象**:没有任何现象。`Code/server/Services/PartyService.h`(pin 141 行)里的 `struct QuestStageDedupHistory` 只有一条退役路径:

```cpp
// Code/server/Services/PartyService.h:79-80(pin)
void inline Expire() noexcept;
Container m_cache{};       // Short, time-ordered, duplicates valid (do they ever happen?)
```

**根因**:`Expire()` 只看时钟。`Code/server/Services/QuestService.cpp:309-321`(pin):

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

窗口是 `Code/server/Services/QuestService.cpp:17` 的 `Console::Setting uQuestHistoryExpiration("GameServer:uQuestHistoryExpiration", "Time in milliseconds to retain quest progression changes for deduplication", 30000U);` —— 30 秒。而 `Add` 在三条路径上都是无条件调用的(`:170` leader、`:203` member、`:258` scene begin),键由发送方逐包任选:`Code/encoding/Messages/RequestQuestUpdate.h` 的 `GameId Id; uint16_t Stage; uint8_t Status; uint8_t ClientQuestType;`,而 `GameId` 是 `{uint32_t BaseId; uint32_t ModId;}` 的 128 位值。⇒ 一个不断编新 id 的客户端,在这 30 秒窗口里能塞进多少行只取决于它的发送速率。

**判据(同族对照,载重)**:同一层楼的任务日志已经为这件事写过答案 —— `Code/server/Services/QuestService.h:27-32`:

| 事实 | 实测 |
| --- | --- |
| 同族上限 | `Code/server/Services/QuestService.h:32 static constexpr std::uint32_t kMaxQuestLogEntries = 4096;` |
| 它的注释 | 「A quest log row is keyed by a GameId the client picks, and the only thing that ever retires one is a Stopped update for that same id: a client that keeps inventing ids grows this table for as long as it stays connected.」 |
| 它的满表行为 | `Code/server/Services/QuestService.cpp:71 else if (entries.size() >= kMaxQuestLogEntries)` → `:78 spdlog::warn(...)` → **仍然转发**,只拒绝新键 |
| 全树速率限制 | `git grep -n -iE "ratelimit\|rate_limit\|tokenbucket\|kBurst" -- Code/server` 只命中 `Code/server/Services/PluginMessagingService.{h,cpp}` 的令牌桶 ⇒ 任务包没有任何速率限制 |
| 全树 `kMax*` | `git grep -n -E "kMax[A-Za-z]+\s*=" -- Code/server` 共 7 条:6 条既有 + 本条新增的 1 条 |

两张表的键都是对端任选的 `GameId`,退役路径都只有一条。所以这里取同一个数:**4096**。

**可达性**:包从 `Code/server/GameServer.cpp:261-275` 的 handler lambda(不带 `noexcept`)进,`:274 m_pWorld->GetDispatcher().trigger(PacketEvent<T>(pRealMessage.get(), pPlayer));`,由 `Code/server/Services/QuestService.cpp:23` 的 sink 接到 `:27 void QuestService::OnQuestChanges(const PacketEvent<RequestQuestUpdate>& acMessage) noexcept`。服务端 handler 链上没有任何一层限流,唯一的门是 `Code/server/GameServer.cpp:584` 的单包大小检查。

**修法**:`Code/server/Services/PartyService.h:58` 加 `static constexpr uint32_t kMaxEntries = 4096;`(与隔壁同名同值),`Add` 先查重、满表拒新:

```cpp
// Code/server/Services/QuestService.cpp:348-377(修复后)
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
```

查重那一半不是为了省内存,是为了**不改变语义**:`Found`/`FoundWPlayerId` 的判定全部发生在 `Add` 之前(`:160`、`:178`、`:202`、`:256`),它们只问「有没有这一行」,第二行对它们没有任何区别。第一版写的是 `return;`(重复即丢弃),被 `:256` 否掉了 —— 那里的注释逐字「Checking for a recent scene change in history keeps us unstuck in case scene end event is missed」,直接丢弃会让「玩家重进同一场景」这一正是它要覆盖的用例看到一条陈旧行;`erase` 加落到末尾的 `emplace_back` 是**刷新那一行的窗口**,不是丢掉它。

**验证**:`git diff --stat` = `Code/server/Services/PartyService.h | 7 +++++++` / `Code/server/Services/QuestService.cpp | 23 +++++++++++++++++++++++` / **2 files changed, 30 insertions(+)**,两个 hunk(`@@ -50,6 +50,13 @@ struct QuestStageDedupHistory`、`@@ -350,5 +350,28 @@ void QuestStageDedupHistory::Add(const QuestOrSceneId& acQuestOrSceneId, const Q`),`PartyService.h` 141 行 → 148 行,`QuestService.cpp` 354 行 → 377 行。
`Code/tests/xmake.lua` 的 target 是 `TPTests`、依赖 `SkyrimEncoding`,**不含 `SkyrimTogetherServer`**,所以服务端的改动在本仓测试里覆盖不到。
**本机不编译**(`docs/PITFALLS.md` §0/§1),所以判据是静态的:`Add` 的三个调用点、`Found`/`FoundWPlayerId` 的四个判定点、以及全树唯一的速率限制都是逐条读出来的。
### 修复:客户端 STRPM 桥的 19 个入口全写着 noexcept,其中 8 个函数体在分配,而它们唯一的出口是同一个 DLL 里的 C 导出 —— 判据是「导出外面没有能接住异常的调用者,所以接住要写在函数体里」

**现象**:没有任何现象。`Code/client/Services/PluginMessagingService.cpp`(pin 405 行)里 **19 个函数签名**都带 `noexcept`,其中 **8 个函数体在分配**:

```cpp
// Code/client/Services/PluginMessagingService.cpp:123(pin,修复前)
STRPM::Result PluginMessagingService::RegisterChannel(const char* acpChannel, STRPM::ReceiveCallback aCallback, void* apUserData, STRPM::ListenerHandle* apOutHandle) noexcept
```

`:141` 逐字 `m_listeners.push_back(Listener{ acpChannel, aCallback, apUserData });`,`:142` 逐字 `m_channels[handle] = acpChannel;`。

**根因**:这 19 个入口没有一个是内部调用 —— 它们全部被 `Code/client/Services/PluginMessagingExport.cpp` 里的函数局部 static 结构体取地址后交给插件:

| 位置 | 写法 |
| --- | --- |
| `Code/client/Services/PluginMessagingExport.cpp:75` | `STRPM_EXPORT STRPM::Result STRPM_CALL STR_QueryPluginMessagingInterface(...) noexcept` |
| `:80` | `STR_QueryPluginMessagingProxyResolver(...) noexcept` |
| `:146` | `STR_QueryPluginMessagingDiagnostics(...) noexcept` |
| `:151` | `STRPM_QueryTransportInterface(...) noexcept` |
| `:25-37` / `:54-65` / `:125-140` | 三个 `s_interface` 的 14 个 lambda,**逐个都写 `noexcept`** |

导出是 C ABI,插件通过 `GetProcAddress` 直接调 —— **外面没有能接住异常的调用者**,而抛出点(本文件)与 `noexcept` 边界(同 DLL 的导出)之间没有任何一层写 `try`。这与 §99 的跨 DLL 情形不同:那里是两个模块各写各的 `noexcept`,这里是同一个模块里从函数体一路裸奔到 `extern "C"`。

**判据(同族对照,载重)**:判据不是「这里会不会耗尽内存」,而是「**这 8 个帧里哪一个是最后一道 `noexcept`**」。逐个查调用链:

| 函数 | pin 行 | 体内分配点 | 它被谁调 |
| --- | --- | --- | --- |
| `FireProxyMapping` | `:63-84` | `:74 Vector<MappingListener> listeners;` + `:79 push_back` | `OnPlayerComponentAdded`/`OnPlayerComponentRemoved`(entt observer) |
| `RegisterChannel` | `:123-148` | `:141 push_back` + `:142 m_channels[handle] =` | `PluginMessagingExport.cpp:30` |
| `UnregisterChannel` | `:150-170` | `:158 const auto channel = it->second;` | `PluginMessagingExport.cpp:31` |
| `Send` | `:172-226` | `:192 request.Channel =` + `:217 PluginData.assign` | `PluginMessagingExport.cpp:32` |
| `SetLocalDisplayName` | `:247-260` | `:258 m_localDisplayName =` | `PluginMessagingExport.cpp:36` |
| `ResolveProxy` | `:290-324` | `:306 m_pWorld->view<FormIdComponent, PlayerComponent>()` | `PluginMessagingExport.cpp:56` |
| `RegisterProxyMappingListener` | `:326-341` | `:339 m_mappingListeners.push_back` | `PluginMessagingExport.cpp:57` |
| `OnPluginMessage` | `:359-405` | `:386 Vector<Delivery> deliveries;` + `:396`/`:400 push_back` | `Code/client/World.cpp:65` 的 dispatcher sink |

同族先例(载重):插件侧那 7 个子模块**没有任何一处 `try`/`catch`**(逐个文件实测 `try=0 catch=0`),而它们全部经这四个导出进来 —— 也就是说这 8 个帧是这条链上唯一能放 `catch` 的地方。

**可达性**:触发频率由调用方决定,不由这条路径决定:
- `FireProxyMapping` 每次玩家实体出现/消失各一次(`Code/client/Services/Generic/CharacterService.cpp:358 OnAssignCharacter`、`:481 OnCharacterSpawn`,`Code/client/Services/Generic/OverlayService.cpp:434 OnPlayerComponentRemoved`,`Code/client/Services/Generic/WeatherService.cpp:110 OnPlayerComponentRemoved` —— **这四个调用帧自己也都写着 `noexcept`**);
- `ResolveProxy` 每个插件每收到一个载荷调一次(七个插件的接收路径全部经它);
- `OnPluginMessage` 每个入站载荷一次。

**修法**:给这 8 个函数体各包一层 `try { … } catch (...) { Log("…"); return <既有失败值>; }`,catch 里只记一行、**不重试**。`noexcept` 一处未改 —— 删掉它只会把终止点挪到导出那一帧。

```cpp
// Code/client/Services/PluginMessagingService.cpp:142-174(修复后)
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

`RegisterChannel` 是唯一需要回滚的一处:两条表都分配,而 `m_channels` **没有任何枚举者**,半写下的行会永远持有一个 channel 名。`handle` 提到 `try` 外初始化,catch 里重新取锁擦掉那一行(`scoped_lock` 已析构,不重入)。

失败值一律沿用契约里**既有**的码(`kNotAvailable`/`kTransportError`/`kTargetNotFound`):七个插件各持一份 `ResultToString`,并且没有一处区分「没找到」与「分配失败」(`plugins/DAVSyncTogether/src/DAVNetworkService.cpp:181`、`plugins/TradeTogether/src/StrMessagingTransport.cpp:440`、`plugins/AnimSyncTogether/src/STRPMClient.cpp:375` 都只看 `kOk`/`kTargetNotFound`/`kNotAvailable`)⇒ 新增一个码的收益为零。

**验证**:`git diff --numstat` = `161	67	Code/client/Services/PluginMessagingService.cpp` / **30 个 hunk** / 405 行 → **499 行**;`noexcept` 出现次数 **19 → 19**(签名一处未动),`try`/`catch` **0/0 → 9/9**。
`Code/client/xmake.lua:21` 的 `add_files("**.cpp|Games/Skyrim/**")` 覆盖本文件与 `PluginMessagingExport.cpp`,两者都在 `SkyrimTogetherClient` 与 `SkyrimTogetherClientLegacy` 的编译闭包内。
`Code/tests/xmake.lua` 的 target 是 `TPTests`、依赖 `SkyrimEncoding`,**不含 `SkyrimTogetherClient`**,所以客户端的改动在本仓测试里覆盖不到。
**本机不编译**(§0/§1),所以判据是静态的:8 个函数体各自的分配点是逐字读出来的,而它们的调用者全是 `noexcept` 或 C 导出。

### 修复:服务端的邀请过期清理遍历的是一个没有任何实体携带的组件,所以一次都没有执行过 —— 判据是「全树只有这一个 view 指向没有挂载点的组件」

**现象**:没有任何现象。`Code/server/Services/PartyService.cpp:88`(修复前)逐字

```cpp
    auto view = m_world.view<PartyComponent>();
    for (auto entity : view)
    {
        auto& partyComponent = view.get<PartyComponent>(entity);
```

这段代码的唯一作用是每 10 秒(`:86 m_nextInvitationExpire = cCurrentTick + 10000;`)把 `PartyComponent::Invitations` 里过期的邀请擦掉。

**根因**:`PartyComponent` 在这棵树里**不是实体组件,而是 `Player` 的一个成员**。`Code/server/Game/Player.h:54` 逐字 `PartyComponent m_party;`。
全树唯一曾经的挂载点是 `OnPlayerJoin` 里的 `m_world.emplace<PartyComponent>(acEvent.Entity);`,它在 `663a187e Refactor player` 里被删除 —— 同一个提交把 `PartyService.h` 的 `Vector<entt::entity> Members;` 改成 `Vector<Player*> Members;`、把 `BroadcastPlayerList(std::optional<entt::entity>)` 改成 `BroadcastPlayerList(Player*)`,把 `emplace<PartyComponent>` 换成 `Player` 行上的 `m_party` 成员,而 `:88` 这个 view 被漏掉了。
`git log --oneline -S "view<PartyComponent>"` 只给出 `9e0c078f`,说明它从被写下的那天起就没改过。

一个 view 遍历的是「携带该组件的实体」;没有实体携带它,循环体一次都不执行 ⇒ **邀请从来没有被擦除过一次**。

**判据(同族对照,载重)**:判据不是「这段循环写错了」,而是「**全树只有这一个 view 指向没有挂载点的组件**」—— 对着 `Code/` 全树取差集,结果只有一条:

| 事实 | 实测 |
| --- | --- |
| 全树 `emplace/emplace_or_replace/try_emplace/replace/insert_or_assign/emplace_back<PartyComponent>` | **0 处** |
| 全树 `view<PartyComponent>` | **1 处**(`Code/server/Services/PartyService.cpp:88`) |
| 被 view 的组件总数 | 22 个,只有 `PartyComponent` 落在差集里 |
| 唯一读者 | 就是这个 view(`Invitations` 在客户端 `Code/client/Services/PartyService.h:30` 有一对同名的,是另一个成员) |
| 同族正确写法 | `GetParty()` 在 `Code/server` 有 **29 处**读者:`GameServer.cpp:788/810/845`、`PartyService.cpp:47/52/64/116/138/163/204/212/249/260/284/285/332/333/391`、`QuestService.cpp:39/175/215/231`、`MapService.cpp:26/37`、`OverlayService.cpp:45/47/118`、`CharacterService.cpp:556`、`WeatherService.cpp:34` —— **全部走 `Player*`** |

**可达性**:写入点是 `Code/server/Services/PartyService.cpp:303`(`OnPartyInvite` 里的 `inviteePartyComponent.Invitations[pInviter] = cExpiryTick;`),读取点是 `:330`(`OnPartyAcceptInvite` 里的 `if (selfPartyComponent.Invitations.count(pInviter) == 0) return;`)。
上界由写者决定,不由传输决定:键是 `Player*`,一个队伍的玩家数 ≤ `uMaxPlayerCount`(默认 8),玩家断线时整个 `Player`(含 `m_party`)随 `PlayerManager::Remove` 销毁 ⇒ **这不是无界增长,是过期语义失效** —— 邀请在 `cExpiryTick = GetTick() + 60000` 之后仍然被服务端接受。
客户端 `Code/client/Services/Generic/PartyService.cpp:66-74` 早因本地 `m_invitations` 过期而拒发,但服务端没有对应门禁。

**修法**:改成遍历玩家管理器,与其余 29 处读者一致 —— `Code/server/Services/PartyService.cpp:88-110`(修复后)逐字

```cpp
    // The invitations are a member of Player, not a component of an entity. PartyComponent used
    // to be attached with emplace<PartyComponent> in OnPlayerJoin; that call went away when the
    // party moved onto the player row (Code/server/Game/Player.h:54) and the view was left
    // behind. A view over a component no entity carries iterates nothing, so no invitation was
    // ever expired: the entry a leader wrote in OnPartyInvite stayed for as long as the invitee
    // stayed connected, and OnPartyAcceptInvite kept accepting an invitation the client had
    // already dropped. Walking the player manager is what the other 29 readers of GetParty() do.
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

擦除循环体逐字照抄 pin,只把取容器的两行换掉。`GetPlayerManager()` 的 range-for 在本仓另有 **25 处**先例(`Code/server/GameServer.cpp:328/722/731/762/805/837/1049` 等),`PlayerManager::Iterator`(`Code/server/Game/PlayerManager.h:9-26`)**只前进不删除**,所以遍历中改 `Player` 的成员是安全的。

**验证**:`git diff --stat` = `Code/server/Services/PartyService.cpp | 12 +++++++++---` / `1 file changed, 9 insertions(+), 3 deletions(-)`,唯一 hunk `@@ -85,10 +85,16 @@ void PartyService::OnUpdate(const UpdateEvent& acEvent) noexcept`。
`Code/tests/xmake.lua` 的 target 是 `TPTests`、依赖 `SkyrimEncoding`,**不含 `SkyrimTogetherServer`**,所以服务端的改动在本仓测试里覆盖不到。
**本机不编译**(`docs/PITFALLS.md` §0/§1),所以判据是静态的:全树差集只剩这一条,而 `view` 的语义就是「携带该组件的实体集合」。

### 修复:STRPM 桥的映射函数写着 noexcept,而其中三个跨 DLL 调用会分配的导出、五个自己建容器,每个都只被 VEH 与工作线程调用 —— 判据是「noexcept 的边界画在哪一层,就要把 try 写在哪一层」

**现象**:没有任何现象。`plugins/STRPluginMessagingAPI/src/STRPMProxyResolverBridge.h`(pin 1165 行)里八个函数连着写 `noexcept`,其中五个体内会分配:

```cpp
        void ObserveSender(
            STRPM::ConnectionID connectionID,
            std::uint32_t playerId) noexcept
```

`:773` 逐字 `g_connectionToPlayer[connectionID] = playerId;`;`:792` 逐字 `std::vector<STRPM::ConnectionID> connections;`(`ObservePlayerProxy`,另有 `:795` 的 `g_playerToForm[playerId] = formID;` 与 `:799` 的 `connections.push_back(connectionID);`);`:815`/`:827` 同形(`RemovePlayerProxy`);`:850` 逐字 `std::vector<std::pair<STRPM::ConnectionID, STRPM::ProxyFormID>> mappings;` 加 `:857` 的 `mappings.emplace_back(connectionID, form->second);`(`FlushMappings`)。

**根因**:这三层各自都能抛,而 `noexcept` 写在最里面那一层:

1. **跨 DLL 的一层**。`:732` 的 `ReportResolvedMapping`、`:748` 的 `RemoveResolvedMapping`、`:754` 的 `ClearRuntimeMappings` 自己不做容器,但它们用 `:711-730` 的三个取址函数(`GetModuleHandleW(L"STRPluginMessagingAPI.dll")` + `GetProcAddress`)取到 `src/STRPMProxyResolverRuntime.h` 的三个导出 —— `:254 STRPM_ReportProxyMapping`、`:261 STRPM_RemoveProxyMapping`、`:267 STRPM_ClearProxyMappings`。
   那三个导出**都不写 `noexcept`**,体内却各有一份 `std::vector<Listener> listeners;`(`:107`/`:153`/`:177`)与一次 `listeners = _listeners;`(`:136`/`:167`/`:184`)。
   ⇒ **抛出点在 API DLL,而 `noexcept` 边界在桥 DLL**;异常穿过 `extern "C"` 边界之后落进的第一个 `noexcept` 帧,就是这三个薄壳。
2. **容器的一层**。`ObserveSender`/`ObservePlayerProxy`/`RemovePlayerProxy`/`FlushMappings` 体内各建一个容器(上引四行)。
3. **调用者的一层**。这八个函数的调用者里既有 `noexcept` 也有非 `noexcept`:
   `src/STRPMProxyResolverTrace.h:185`/`:194` 调 `ObservePlayerProxy`/`RemovePlayerProxy`,而它们在 `:158 bool CaptureLifecycleCall(CONTEXT* context) noexcept` 体内,唯一调用者是 `:265`(在非 `noexcept` 的 `ExceptionHandler` 里)⇒ 终止落在 `CaptureLifecycleCall`;
   `src/STRPMProxyResolverBridge.h:886` 调 `ObserveSender`,它在 `:864 void ObserveIncomingEnvelope(CONTEXT* context) noexcept` 体内,唯一调用者是 `:949`(桥自己的 `ExceptionHandler` 的 `EXCEPTION_BREAKPOINT` 分支);
   `:990`/`:996` 调 `ObservePlayerProxy`/`RemovePlayerProxy`、`:1029` 调 `ClearMappings`(同一个 `ExceptionHandler` 的 `kDisconnected` 分支)、`:1132` 调 `FlushMappings`(在 `:1088 void Worker(std::stop_token token)` 的每秒循环里)、`src/STRPMProxyResolverBootstrapV2.h:312` 同形 —— **两个 Worker 都不是 `noexcept`**。

**判据(同族对照,载重)**:判据不是「这些函数会不会分配」,而是「**`noexcept` 画在哪一层,异常就会在哪一层的帧里变成 terminate**」。
桥 DLL 这一侧把 `noexcept` 写在了最内层五个函数上,而最外层能接住异常的两个 VEH 处理器与两个 Worker 都没写;API DLL 那一侧的三个导出干脆没写。
所以「删掉桥侧的 `noexcept`」不是修法:它只是把终止点从被调者挪到 `CaptureLifecycleCall`(那条链上的上一层恰好写了 `noexcept`),而另外三条链上的终止点则外移 —— **两种结果都不是「有人处理了」**。
同族先例是同一个文件里的四处(`:203 SnapshotProcessMemory`、`:627 ReadString`、`:652 PeekRawEnvelope`、`:1139 Start()`),它们的形状统一是「体内 `try` + `catch (...)` + 记一行日志 + 返回失败值」,不重试。`0004`/`0005`/`0006` 三个既有补丁给的都是同一个形状。

**修法**:在 `:760` 前插 8 行理由注释,然后**给这八个函数各包一层 `try { … } catch (...) { Log(…); }`** —— 注意是八个而不是五个:
三个薄壳(`ReportResolvedMapping`/`RemoveResolvedMapping`/`ClearRuntimeMappings`)虽然自己不建容器,但它们才是跨 DLL 那一跳的执行者,漏掉它们,异常就会在它们自己的帧里终止。
函数体其余部分逐字照抄 pin,只整体缩进 4 格;失败时只记一行日志,不重试 —— 丢一条映射退化成一次漏映射,而这个解析器本来就容忍漏映射。

**验证**:`0007` 的 `git apply --check --cached` 退 **0**、前向 `git apply --check` 退 **0**;
应用后 `git apply --check --reverse` 退 **0**,再次前向 `--check` 退 **1** 并报
`error: patch failed: src/STRPMProxyResolverBridge.h:729` 与 `error: src/STRPMProxyResolverBridge.h: patch does not apply`;
补丁里的 pin blob 哈希与 `git rev-parse :src/STRPMProxyResolverBridge.h` 相等(`55eabd3`);
累积校验 `0001`…`0007` 依次退 **0**,`git diff --stat` 空、`git diff --cached --stat` = **13 files changed, 526 insertions(+), 213 deletions(-)**。
`python Code\plugins\tools\plugin_patches.py verify` → `PATCH VERIFY OK` / **37** patch(es);`… check` → `PATCH CHECK OK`。
**本机不编译**(`docs/PITFALLS.md` §0/§1),所以「这条路径到底会不会耗尽内存」不可判定;
判据不是「会不会发生」,而是「发生之后这八个帧里哪一个是最后一道 `noexcept`」。

**交付**:`Code/plugins/patches/STRPluginMessagingAPI/0007-keep-an-allocation-failure-out-of-the-proxy-resolver-mapping-table.patch`
(12270 B / 282 行,单文件单 hunk `@@ -729,136 +729,204 @@`,`src/STRPMProxyResolverBridge.h` `55eabd3`→`34dcb17`)。
The plugin is a submodule this project does not own and cannot push, and CI runs
`git submodule update --init --force --recursive`, which discards a working-tree only change.
It is therefore shipped as the patch above, which `plugin_patches.py` applies before configuring and building.

### 修复:STRPM 的两份日志重定向冒充 `fopen_s` 却继续写着 noexcept,而它们改用的 std::filesystem 每写一行日志都要分配 —— 判据是「替换一个 C 函数时,noexcept 要按替换后的实现重判,不能按被替换的签名继承」

**现象**:没有任何现象。`plugins/STRPluginMessagingAPI/src/STRPMLogRedirect.h:19-22` 逐字

```cpp
inline errno_t STRPMRedirectFopen(
    FILE** file,
    const char* filename,
    const char* mode) noexcept
```

该文件在 `:76` 用 `#define fopen_s STRPMRedirectFopen` 把自己顶替掉 `fopen_s`,而
`plugins/STRPluginMessagingAPI/CMakeLists.txt:423` 逐字
`/FI${CMAKE_CURRENT_SOURCE_DIR}/src/STRPMLogRedirect.h` 把它强制包含进 `STRPluginMessagingBridge` 的每一个 TU;
`:415` 用同样的 `/FI` 把 `src/STRPMDiagnosticLogRedirect.h` 注入 `STRPluginMessagingDiagnostic`。
两份头文件没有任何源文件 `#include`(`git grep -n "LogRedirect" -- *.cpp *.h CMakeLists.txt` 只命中 `CMakeLists.txt` 的那三行),
也就是说 **改这两份头文件就等于改两个目标的全部日志出口**,而它们此前从未被任何补丁触及。

**根因**:`src/STRPMLogRedirect.h:39` 逐字 `std::filesystem::path logDirectory(documents);`,
后面是 `:42-44` 的三次 `logDirectory /= …`、`:47` `std::filesystem::create_directories(logDirectory, error);`、
`:50` `const auto logPath = logDirectory / L"STRPluginMessagingBridge.log";` —— **四次分配**,全部发生在 `fopen_s` 被调用的那一刻。
被替换的 `fopen_s` 是 C 运行时函数,它不抛;替换它的这个函数会抛,却把原签名的 `noexcept` 一起继承了下来。

**判据(同族对照,载重)**:不是「要不要加 try/catch」,而是「**谁在 `noexcept` 后面调用它**」。
经这一个宏走到那四次分配的调用者有九个,九个全是 `noexcept`:
`src/STRPluginMessagingBridgeReceive.cpp:124` 的 `void Log(const char* text) noexcept`(`:129` 就是那次 `fopen_s`)与
`:134` 的 `void LogAddress(const char* label, std::uintptr_t value) noexcept`;
`src/STRPluginMessagingBridgeSKSE.cpp:43` 的 `void LogIdentity(const char* format, ...) noexcept`;
`src/STRPMProxyResolverBridge.h:126` 的 `void Log(const char* format, ...) noexcept`;
`src/STRPMChatUiSuppress.h:73` 的 `inline void Log(const char* text) noexcept`、`:83` 的 `inline void LogAddress(…) noexcept`、
`:508` 的 `inline DWORD WINAPI Worker(void*) noexcept`;
`src/STRPMChatUiSuppressV3.h:282` 的 `inline void MarkPendingEnvelope() noexcept` 与 `:361` 的 `inline void LogExecuteAsyncResolution(…) noexcept`。
所以「把 `noexcept` 删掉」不是修法:异常会立刻撞进上一层 `noexcept` 的边界,终止只是从被调者挪到调用者,
`std::terminate` 与「没有崩溃转储」两件事一件不少。同族先例就在隔壁 —— `src/STRPMRuntimeLoadRedirect.h` 用同样的宏替换,
既有补丁 `0005` 给它的选择也是 `try { … } catch (...) { }` 加落到本来就有的兜底,而不是删 `noexcept`。

**修法**:两份头文件各把 `SHGetKnownFolderPath` 守卫块里的那一段包进 `try { … } catch (...) { }`,
失败时落到函数本来就有的 `:71 return ::fopen_s(file, filename, mode);` —— **不改去向,只让失败不再终止**。两处细节:

1. `CoTaskMemFree(documents)` 原来紧跟在 `std::filesystem::path logDirectory(documents);` 之后;那行一旦抛出,`documents` 就泄漏。
   所以释放被移进 `try` 并紧跟一句 `documents = nullptr;`,`try` 之后再补 `if (documents != nullptr) CoTaskMemFree(documents);` —— 既不泄漏也不双重释放。
2. 两份文件的缩进风格不同(`src/STRPMLogRedirect.h:53`/`:59` 是同行花括号,`src/STRPMDiagnosticLogRedirect.h:53-54`/`:60-61` 是另起一行),
   补丁按原文各自保留,没有顺手统一。

诊断侧更窄:它的唯一 TU `src/STRPluginMessagingDiagnosticSKSE.cpp` 全文件 24 个函数**一个 `noexcept` 都没有**,
所以那里的危险只来自 `STRPMDiagnosticRedirectFopen` 自己;但这一份同样必须包,否则终止点就落在函数体内。

**验证**:`0006` 的 `git apply --check --cached` 退 **0**、前向 `git apply --check` 退 **0**;
应用后 `git apply --check --reverse` 退 **0**,再次前向 `--check` 退 **1** 并报
`error: patch failed: src/STRPMLogRedirect.h:36` 与 `error: patch failed: src/STRPMDiagnosticLogRedirect.h:36`;
补丁里两个 pin blob 哈希与 `git rev-parse :<path>` 相等(`src/STRPMLogRedirect.h` `3e63576`、`src/STRPMDiagnosticLogRedirect.h` `48d002c`);
累积校验 `0001`…`0006` 依次退 **0**,`git diff --stat` 空、`git diff --cached --stat` = **13 files changed, 386 insertions(+), 141 deletions(-)**。
`python Code\plugins\tools\plugin_patches.py verify` → `PATCH VERIFY OK` / **36** patch(es);`… check` → `PATCH CHECK OK`。
**本机不编译**(`docs/PITFALLS.md` §0/§1),所以「这条路径到底会不会耗尽内存」不可判定;
判据不是「会不会发生」,而是「发生之后这行日志还剩不剩得下一条记录」。

**交付**:`Code/plugins/patches/STRPluginMessagingAPI/0006-keep-an-allocation-failure-out-of-the-log-redirect.patch`
(7300 B / 169 行,两个文件:`src/STRPMLogRedirect.h` 的 `@@ -36,35 +36,54 @@`、`src/STRPMDiagnosticLogRedirect.h` 的 `@@ -36,37 +36,54 @@`)。
The plugin is a submodule this project does not own and cannot push, and CI runs
`git submodule update --init --force --recursive`, which discards a working-tree only change.
It is therefore shipped as the patch above, which `plugin_patches.py` applies before configuring and building.
### 修复:DAV 解码一件盔甲的激活 addon 列表时长度由对端决定,而每个元素都要过一次引擎查找 —— 判据是「上限要绑到自己的发送侧能产出什么,而不是绑到传输层允许什么」

**现象**:`plugins/DAVSyncTogether/src/NetworkProtocol.cpp:142-186 DecodeArmorState` 解 `DAVSTATE|armo=…|state=…|variant=…|active=…` 时,`:168-183` 按逗号逐个切分 `active=` 字段并把每个 token 追加进结果:

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

循环次数只由对端给的 `active=` 有多长决定,**没有任何计数上限**。而每个 token 都要走一次 `DecodeIdentity`(`:84-104`),那里 `:100-102` 逐字
`if (auto* resolved = identity.Resolve()) { identity.runtimeFormID = resolved->GetFormID(); }` ——
`FormIdentity::Resolve()`(`plugins/DAVSyncTogether/src/FormIdentity.cpp:18-29`)在 `plugin` 非空时走
`RE::TESDataHandler::GetSingleton()->LookupForm(localFormID, plugin)` ⇒ **一个 token 一次按「插件名 + 局部 ID」的引擎查找**,而插件名也是对端给的。

**根因**:这个字段的长度是对端填的,而解码器把它当成真的。全 DAV 只有两个 `kMax*` 常量,都来自 STRPM 头
(`plugins/DAVSyncTogether/include/STRPluginMessagingAPI/STRPluginMessagingAPI.h:21 kMaxPayloadBytes = 24 * 1024`),**插件自己一处上限都没有**。

**判据(同族对照,载重)**:这不是「要不要防」的问题,是「防到哪个数」的问题 —— 上限必须绑到**我们自己的发送侧能产出什么**。
发送侧就在同一个文件:`EncodeArmorState`(`NetworkProtocol.cpp:116-140`)`:126-131` 逐字
`for (std::size_t i = 0; i < armor.activeArmorAddons.size(); ++i) {`,而这个 vector 由
`plugins/DAVSyncTogether/src/DAVProbe.cpp:175-182` 填 —— 从 `VisitArmorNodes`(`:391-412`,递归走 3D 节点树、按节点名里的 `(ARMA)/(ARMO)` 归到该件盔甲名下)取那一份,
`:182 SortAndUniqueIdentities(state.activeArmorAddons);` 去重。
⇒ 一件盔甲的 addon 数 = 这件盔甲当前挂了多少个 ARMA 节点,原版与常见 mod 都在个位到十几个的数量级;**64 已是它的数倍**。
传输层那个 24 KiB 换算下来约一万两千个 token,只能用来判「包大不大」,不能用来判「一件盔甲该有几个 addon」。
同族的既定修法是 `Code/plugins/patches/IEDSyncTogether/0002-bound-the-live-peer-table-and-the-decoded-object-list.patch` 的 `kMaxRemoteStates = 64` / `kMaxObjects = 512`、
`Code/plugins/patches/MorphSyncTogether/0006-bound-a-single-morph-chunk-and-the-values-a-snapshot-may-carry.patch` 的 `kMaxMorphValues`、
`Code/plugins/patches/TradeTogether/0002-bound-the-peer-name-and-request-id-tables.patch` —— 四处都是同一句话:**列表长度由对端给,解码器必须自己封顶**。

**修法**:整包拒收而不是截断。`plugins/DAVSyncTogether/src/NetworkProtocol.h:15` 的 `RemoteArmorState` 里加

```cpp
        static constexpr std::size_t kMaxActiveArmorAddons = 64;
```

`src/NetworkProtocol.cpp:172` 的 `if (!token.empty()) {` 之后加

```cpp
                if (result.activeArmorAddons.size() >= RemoteArmorState::kMaxActiveArmorAddons) {
                    return std::nullopt;
                }
```

**为什么是拒收而不是截断**:截断后的列表会被下游当成一份完整状态。`plugins/DAVSyncTogether/src/DAVNetworkService.cpp:164-174` 逐字
`bool activeValid = true; for (auto& identity : message->activeArmorAddons) { … if (!addon) { activeValid = false; … } }` ——
`activeValid` 只问「列表里每个元素解没解开」,**不问「列表全不全」**;列表短一截照样 `:185 const bool valid = resolvedArmor != nullptr && activeValid;` 为真。
而拒收路径本来就有:`DAVNetworkService.cpp:154-158` 逐字
`auto message = DecodeArmorState(received.payload); if (!message) { SKSE::log::warn("DAVST STRPM RX malformed payload connection={} sequence={}", received.connectionID, received.sequence); return; }`
⇒ 返回 `std::nullopt` 不需要新增任何分支、日志或调用点改动。

**交付**:`Code/plugins/patches/DAVSyncTogether/0003-bound-the-active-armor-addon-list-a-single-payload-may-carry.patch`
(1405 B / 33 行,2 个文件各 1 个 hunk;`src/NetworkProtocol.cpp` `6d0b2bf`→`a9bc4fe`、`src/NetworkProtocol.h` `ff1e445`→`c6ce500`)。
The plugin is a submodule this project does not own and cannot push, and CI runs
`git submodule update --init --force --recursive`, which discards a working-tree only change.
It is therefore shipped as the patch above, which `plugin_patches.py` applies before configuring and building.


### 修复:OStimNG 的输入回调先查一次「玩家线程在跑」、再去取指针,两次查询之间线程会被删掉 —— 判据是「判空和用指针之间不能再隔一次查询」

**现象**:按场景快捷键时崩溃,前提是那一帧恰好有线程被停。`plugins/OStimNG/skse/src/Events/EventListener.cpp:81` 逐字
`if (Threading::ThreadManager::GetSingleton()->playerThreadRunning()) {`,`:83` 再逐字 `Threading::Thread* playerThread = Threading::ThreadManager::GetSingleton()->getPlayerThread();` ——
这是对同一把 `m_threadMapMtx` 的**两次独立查询**,`playerThreadRunning()` 返回 `true` 不保证紧接着的 `getPlayerThread()` 还拿到同一个指针。

**根因**:两次查询之间线程可以被删掉。`ThreadManager::stopThreadNoLock`(`plugins/OStimNG/skse/src/Core/ThreadManager.cpp:204-219`)在后台线程里执行
`:209 UI::UIState::GetSingleton()->HandleThreadRemoved(thread);` → `:210 m_threadMap.erase(threadID);` → `:211 thread->close();` → `:212 delete thread;`,
而输入回调在游戏线程(`plugins/OStimNG/skse/src/Main.cpp:66 RE::BSInputDeviceManager::GetSingleton()->AddEventSink(Events::EventListener::GetSingleton());`)⇒ **写指针的线程不是读指针的线程**。
`:83` 拿到的指针在 `:84` 立刻被 `playerThread->getActorCount()`/`getCenter()` 解引用;而 `ThreadManager::getPlayerThread()` 本身**从不判空**(它在 `m_threadMap` 里找不到 `playerThread` 时返回 `nullptr`)。
同族里另外四处(`:149`/`:151`/`:155`/`:160`)连 `playerThreadRunning()` 这道门都没有,直接 `getPlayerThread()->…`。
其中 `:84` 那条更重:`Furniture::selectFurniture(..., true, [playerThread](GameAPI::GameObject furniture) { playerThread->changeFurniture(furniture, nullptr); })` ——
`plugins/OStimNG/skse/src/Furniture/Furniture.cpp:146 if (alwaysAsk || MCM::MCMTable::selectFurniture())` 恒真,而 `:155` 走 `GameAPI::Game::showMessageBox(..., callback)`;
`plugins/OStimNG/skse/src/GameAPI/Game.cpp:31-58` 的两条出口(UIExtensions 的 `DispatchStaticCall` 与 `MessageBoxMenu::QueueMessage`)**都是异步** ⇒
按值捕获的 `playerThread` 在**后续帧**才被解引用,那时 `delete thread` 早已执行。

**判据(同族对照,载重)**:只隔 16 行就是正确写法 ——
`EventListener.cpp:165` 逐字 `Threading::Thread* thread = Threading::ThreadManager::GetSingleton()->getPlayerThread();`、`:166 if (thread) {`;
`plugins/OStimNG/skse/src/Core/Thread.cpp:624-640 Thread::stopFaded()`(行号是 **pin 的**;工作区里该函数在 `:687-703`,因为 0001–0004 先给这个文件加了 63 行)里那条 detached 线程逐字 `Thread* thread = ThreadManager::GetSingleton()->getPlayerThread(); if (thread) { thread->stop(); }`;
`plugins/OStimNG/skse/src/Core/Thread/ThreadFurniture.cpp:15-26` 同形。⇒「取一次局部、就地判空」是这个文件与这个族的既定写法,`:83`/`:149`/`:151`/`:155`/`:160` 是 5 处例外。

**修法**:不改任何公开签名,五处一律「取局部 → 判空 → 用局部」;`:84` 那条额外**去掉按值捕获** ——
lambda 改为在触发时重新 `getPlayerThread()` 并判空,而不是把一个可能已经析构的指针搬进后续帧。

**交付**:`Code/plugins/patches/OStimNG/0005-keep-a-deleted-player-thread-out-of-the-input-handler.patch`
(3465 B / 54 行,单文件 2 个 hunk;`skse/src/Events/EventListener.cpp` `796ffd3`→`1952eec`,212 行)。
The plugin is a submodule this project does not own and cannot push, and CI runs
`git submodule update --init --force --recursive`, which discards a working-tree only change.
It is therefore shipped as the patch above, which `plugin_patches.py` applies before configuring and building.

### 修复:OStimNG 的家具搜索把选项上限算出来却不用,把半径内的家具全部塞进消息框 —— 判据是「算出来的上限必须出现在唯一的消费者身上」

**现象**:家具搜索半径内超过 8 件家具时,消息框收到的按钮数超过游戏上限。`plugins/OStimNG/skse/src/GameAPI/Game.h:17` 逐字 `inline static int getMessageBoxOptionLimit() { return 9; }`(含 `$ostim_message_none` 那一项)。
上限是**游戏本体**的:`plugins/OStimNG/skse/src/GameAPI/Game.cpp:56 messagebox->buttonText.push_back(text.c_str());` 往 `RE::MessageBoxData` 里逐条塞按钮,而这里传进去的 `options` 长度就是按钮数。

**根因**:`plugins/OStimNG/skse/src/Furniture/Furniture.cpp:149` 逐字
`int max = std::min(static_cast<int>(furniture.size()), GameAPI::Game::getMessageBoxOptionLimit() - 1);`,
而 `:150` 逐字 `for (int i = 0; i < furniture.size(); i++) {` —— `max` 算出来之后**一次都没被读过**;`:151`/`:152` 把 `furniture[i].first->name` 与 `furniture[i].second` 成对塞进 `options`/`objects`,于是按钮数与 `objects` 长度都等于 `furniture.size()`。
全树 `std::min(static_cast<int>` 只有这一处命中 ⇒ 不是「抄错了参数顺序」,是「压根没用」。
两个兄弟用了:`Core/ThreadStarter/DebugNPCThreadStarter.cpp:33` 与 `Core/ThreadStarter/PlayerThreadStarter.cpp:108` 都逐字
`int max = std::min<int>(GameAPI::Game::getMessageBoxOptionLimit() - 1, actors.size());`,并在 `while (i < max)` 里读它。

**修法**:`:150` 改为 `for (int i = 0; i < max; i++) {` —— `options` 与 `objects` 同步截断,`:156 if (result == 0 || result > objects.size())` 的边界判断因此保持成立(两者长度始终一致)。

**交付**:`Code/plugins/patches/OStimNG/0006-honour-the-message-box-option-limit-when-listing-furniture.patch`
(712 B / 14 行,单文件 1 个 hunk;`skse/src/Furniture/Furniture.cpp` `223b6bf`→`ba08674`,198 行)。
The plugin is a submodule this project does not own and cannot push, and CI runs
`git submodule update --init --force --recursive`, which discards a working-tree only change.
It is therefore shipped as the patch above, which `plugin_patches.py` applies before configuring and building.

- 三重校验实跑(cwd = `plugins/OStimNG`):两个补丁 `git apply --check --cached` 各退 **0**(能打到 pin)、前向 `git apply --check` 各退 **0**(未应用时干净);
  合并应用后 `git apply --check --reverse` 退 **0**(与工作区一致);再次前向 `--check` 退 **1** 并报 `error: patch failed: skse/src/Events/EventListener.cpp:81` 与 `error: patch failed: skse/src/Furniture/Furniture.cpp:147`(已应用,符合预期)。
- 累积校验:`0001`…`0006` 依次 `git apply --cached` 全部退 **0**,`git diff --cached --stat` = **18 files changed, 408 insertions(+), 151 deletions(-)**,
  `git diff --stat` 在补丁全部应用到索引后无输出,随后 `git reset -q` 复原。
- `python Code\plugins\tools\plugin_patches.py verify` → `PATCH VERIFY OK` / **34** patch(es);`… check` → `PATCH CHECK OK`。
- **本机不编译**(`docs/PITFALLS.md` §0/§1,`plugins/OStimNG/skse/AGENTS.md` 逐字「do not attempt to build c++ or cmake」),所以「两次查询之间线程会不会真的被删掉」不可判定;
  判据不是「会不会发生」,而是「发生之后 `:84` 读的是空指针还是已析构对象」—— 修复前两条都有。

### 修复:OStimNG 的菜单在后台线程删掉线程对象之后仍然解引用它 —— 判据是「判空要判在解引用那一刻,不是判在函数入口」

**现象**:没有任何现象,直到停线程与菜单读取撞在同一帧。UI 层持有两个 `public` 裸指针 ——
`plugins/OStimNG/skse/src/UI/UIState.h:49-50` 逐字 `Threading::Thread* currentThread;` 与 `Graph::Node* currentNode;`,两个都没有初始化器;
`UIState::HandleThreadRemoved`(`plugins/OStimNG/skse/src/UI/UIState.cpp:106-110`)只清 `currentThread`,**`currentNode` 一次都不清**。
清空写在 `ThreadManager::stopThreadNoLock`(`plugins/OStimNG/skse/src/Core/ThreadManager.cpp:204-219`)里,顺序逐字是
`:209 UI::UIState::GetSingleton()->HandleThreadRemoved(thread);` → `:210 m_threadMap.erase(threadID);` → `:211 thread->close();` → `:212 delete thread;`,
而它唯一的调用点 `:51` 在 `:38` 起的后台线程 `m_excitementThread` 里(`:44 if (!GameAPI::Game::isPaused())` 之后)—— **写指针的线程不是读指针的线程**。

**根因**:全树 15 处解引用 `currentThread`/`currentNode` 时不判空,其中 5 处落在延迟执行的 lambda 里 ——
`UI/OStimMenu.h:13-19` 的 `Update()` 逐字 `while (!_taskQueue.empty()) { auto& task = _taskQueue.front(); task(); _taskQueue.pop(); }`,
`:63-67 QueueUITask` 只入队,`UI/OStimMenu.cpp:69-74 AdvanceMovie` 才调 `Update()` ⇒ **队列里的 lambda 在下一帧执行,与入队时刻不同步**;`SKSE::GetTaskInterface()->AddTask(...)` 同理。
于是「帧 N 入队、后台线程在 N 与 N+1 之间停线程、帧 N+1 执行」这三步就构成一次空指针解引用:
`UI/Scene/SceneMenu.cpp:193 ChangeAnimation`、`:206`/`:209 ChangeSpeed`、`UI/Search/SearchMenu.cpp:165 SelectOption`,以及经 `UI/Scene/SceneMenu.cpp:106-124 UpdateMenuData` 的 `QueueUITask` 落到 `:130`/`:131`/`:134`/`:138 BuildMenuData`。
同步路径 10 处:`UI/Align/AlignMenu.cpp:66 LoadCurrentAlignment`、`:210 ToggleActor`、`:252 Increment`、`UI/Scene/SceneOptions.cpp:78 BuildSubPages`、`:105 BuildOptions`、
`UI/Search/SearchMenu.cpp:153`/`:154 Search`。
第 15 处 `ModAPI/OstimNG-API-Thread.cpp:439 SearchScenes` **没有修**:该文件的 pin blob 是 CRLF(26928 B / CR=685),
而 `.gitattributes:38` 对 `Code/plugins/patches/**/*.patch` 强制 `text eol=lf` —— 实测按同样手法生成的补丁对索引 `git apply --check --cached` 退 **1**
(`error: patch failed: skse/src/ModAPI/OstimNG-API-Thread.cpp:437` / `error: skse/src/ModAPI/OstimNG-API-Thread.cpp: patch does not apply`),
只有对工作树的前向 `--check` 才退 0 ⇒ 这个文件**不可打补丁**;而且 `SearchScenes` 在本仓**零调用点**(7 处命中里 5 处是声明/快照,另 2 处是同一份 `OStimAPI/ModThreadControl.h` 的逐字副本),按既有判据(候选 3 不可达未修)记为**不修**。
`Core/ThreadManager.cpp:144-146 AnySceneRunning()` 是 `m_threadMap.size() > 0`,**不是**「`currentThread` 非空」,所以 `UIState::HandleControl` 首行那道门拦不住这一族。
同一条族里还有一条比 nullptr 更糟的路径:`ThreadManager::UntrackAllThreads`(`plugins/OStimNG/skse/src/Core/ThreadManager.cpp:133-142`)逐字 `for (auto& entry : m_threadMap) { delete entry.second; } m_threadMap.clear();`,
**完全不通知 UI**;它的唯一调用者 `plugins/OStimNG/skse/src/Serial/Manager.cpp:290-299 Revert(...)` 是读档/回滚 —— 那条路径留下的是非空悬垂指针。

**判据(同族对照,载重)**:同一个 `plugins/OStimNG/skse/src/ModAPI/OstimNG-API-Thread.cpp` 里 24 处 `GetThread(threadID)` **每一处都紧跟判空**,
`UI/Scene/SceneMenu.cpp:152-159 UpdateSpeed` 逐字先 `auto thread = UI::UIState::GetSingleton()->currentThread; if (!thread) { return; }` 再取 `currentNode` 并同样判空,
`UI/Align/AlignMenu.cpp:83-85`、`UI/UIState.cpp:15-18`、`UI/UIState.cpp:85-97 NodeChanged` 也都是先判后用 ⇒ UI 层这 15 处是 24:1 的惯例差,不是风格问题。
`getFurnitureTypeInternal()` 也在这批里:`plugins/OStimNG/skse/src/Core/Thread.h:226` 逐字 `Furniture::FurnitureType* furnitureType = nullptr;`,
同族 `Core/ThreadActor.cpp:538` 已按「可能为空」写(`if (Furniture::FurnitureType* furnitureType = thread->getFurnitureTypeInternal()) {`),而 `SceneMenu.cpp:138`/`SearchMenu.cpp:154` 直接 `->isChildOf(...)`。

**修法**:不改任何公开签名,只把「用之前先取局部、取到之后再判空」补上 ——
(1) `UIState.h:49-50` 两个成员就地补 `= nullptr`;(2) `UIState.cpp:106-110` 在清 `currentThread` 的同一个分支里补 `currentNode = nullptr;`;
(3) `ThreadManager.cpp:137-139` 让 `UntrackAllThreads` 在 `delete` 之前逐个调 `UI::UIState::GetSingleton()->HandleThreadRemoved(entry.second);`,与 `stopThreadNoLock` 同一形状;
(4) `AlignMenu.cpp` 的 `LoadCurrentAlignment`/`ToggleActor`/`Increment` 各取一次局部指针再判空(`ToggleActor` 另加 `currentNode->actors.empty()` —— 原式 `selectedSlot > currentNode->actors.size() - 1` 在空容器上是无符号回绕);
(5) `SceneMenu.cpp` 的 `BuildMenuData` 把 `currentNode`/`currentThread`/`getFurnitureTypeInternal()` 各取一次局部并判空,`ChangeAnimation`/`ChangeSpeed` 的 lambda 内先取局部再判空;
(6) `SceneOptions.cpp` 的 `auto& thread = UI::UIState::GetSingleton()->currentThread;` 改成 `auto thread = …` 并紧跟 `if (thread == nullptr) continue;`(引用绑定的空指针不会因为判空而变得安全);
(7) `SearchMenu.cpp` 的 `Search` 在 `thread == nullptr` 时 `AssignData(data); return;`(保留「无结果」的既有语义,而不是让菜单停在上一次的结果上)。

**交付**:`Code/plugins/patches/OStimNG/0004-keep-a-dead-thread-out-of-the-menus-that-dereference-it.patch`
(9466 B / 210 行,7 个文件 14 个 hunk;`UIState.h` `0d0a5d5`→`806c2e5`、`UIState.cpp` `5149498`→`ac68b9c`、`ThreadManager.cpp` `2be63d4`→`64af7ba`、
`AlignMenu.cpp` `64a3686`→`3d84077`、`SceneMenu.cpp` `68d56ba`→`fd02463`、`SceneOptions.cpp` `67ad326`→`1e7c074`、`SearchMenu.cpp` `6efe1b5`→`35b6df4`)。
The plugin is a submodule this project does not own and cannot push, and CI runs
`git submodule update --init --force --recursive`, which discards a working-tree only change.
It is therefore shipped as the patch above, which `plugin_patches.py` applies before configuring and building.

- 三重校验实跑(cwd = `plugins/OStimNG`):`git apply --check --cached` 退 **0**(能打到 pin)、前向 `git apply --check` 退 **0**(未应用时干净)、
  应用后 `git apply --check --reverse` 退 **0**(与工作区一致);再次前向 `--check` 退 **1** 并报 `error: patch failed: skse/src/Core/ThreadManager.cpp:135`(已应用,符合预期)。
- 累积校验:`0001`/`0002`/`0003`/`0004` 依次 `git apply --cached` 全部退 **0**,`git diff --stat`(工作区 vs 累积索引)**无输出**,
  `git diff --cached --stat` = 16 files changed, 387 insertions(+), 143 deletions(-),随后 `git reset -q` 复原。
- `python Code\plugins\tools\plugin_patches.py verify` → `PATCH VERIFY OK` / **32** patch(es);`… check` → `PATCH CHECK OK`。
- `git apply` 会打两行 `warning: 2 lines add whitespace errors.`(补丁第 165/178 行):那两行是 pin 里本来就带尾空白的行(`if(results[i]->actors.size() == thread->getActorCount() ` 行尾 1 空格、`});    ` 行尾 4 空格),
  只被局部改写,**不是新引入的空白** —— 这类告警要回到 pin 里核对,不能就地删掉尾空白(会多出一个不属于本补丁的改动)。
- **本机不编译**(`docs/PITFALLS.md` §0/§1,`plugins/OStimNG/skse/AGENTS.md` 逐字「do not attempt to build c++ or cmake」),所以「后台线程与菜单读取会不会真的撞上」不可判定;
  判据不是「会不会发生」,而是「发生之后读的是空指针还是已析构对象」—— 修复前两条都有,修复后 `HandleThreadRemoved` 覆盖了全部三条销毁路径(`stopThreadNoLock`/`UntrackAllThreads`/`migrateThread`)。

### 修复:OStimNG 的 Scene API 七个入口都写着 noexcept,而每个函数体都在分配、指针参数又从不判空 —— 判据是「声明在别人手里的 noexcept 撤不掉,只能自己接住」

**现象**:没有任何现象。`plugins/OStimNG/skse/src/ModAPI/OstimNG-API-Main.cpp`(pin 132 行)里七个入口逐个写着 `noexcept`,
而这七个 `noexcept` 是写在 `plugins/OStimNG/skse/src/ModAPI/OstimNG-API-Scene.h:30-46` 的**虚函数声明**上的 ——
消费方 `plugins/OStimTogether/src/OStimAPI/ModSceneControl.h:19` 持有一份逐字副本(`:22` 注释逐字「Exact OStim Scene ModAPI V1 vtable prefix through StopScene.
No virtual destructor.」,`:24-29`/`:31`/`:39`/`:48`/`:58-60` 是同一批 `noexcept = 0;`),所以这一侧的 `noexcept` **撤不掉**:
单边去掉会让定义与声明的异常规格不一致,而保留声明、任由异常逃出去,terminate 就落在调用方 `plugins/OStimTogether/src/CoopSessionManager.cpp:655` 那一帧上。

**根因**:七个入口里六个的整个函数体都在分配,而它们调用的每一层都不写 `noexcept`:
`GameAPI::GameActor::convertVector()`(`plugins/OStimNG/skse/src/GameAPI/GameActor.cpp:10`,形参**按值** `std::vector<RE::Actor*>`,`:11 std::vector<GameActor> ret` + `:13 ret.push_back`)、
`Threading::startThread()`(`plugins/OStimNG/skse/src/Core/ThreadStarter/ThreadStarter.cpp:13`,非 noexcept,`:20 std::vector<GameAPI::GameActor> actorSet` + `:24`/`:29`/`:34` 三处 `"actor " + actor.getName() + …` 字符串拼接 + `:37 push_back` + `:42 Trait::ActorCondition::create`)、
`Thread::stopFaded()`(`plugins/OStimNG/skse/src/Core/Thread.cpp:687`,非 noexcept,`:694 std::thread fadeThread = std::thread([&] { … });` —— `std::thread` 构造失败会抛 `std::system_error`)、
`Thread::startAutoMode()`(`plugins/OStimNG/skse/src/Core/Thread/ThreadAutoControl.cpp:119` → `progressAutoMode()` `:162 std::string action = … ? "malemasturbation" : "femalemasturbation";`、`:163 getRandomNode(...)` 按值吃 `std::vector<Trait::ActorCondition>` + `std::function`)。
`ThreadStartParams` 在 `plugins/OStimNG/skse/src/Core/ThreadManager.h:17`/`:50` 都是**按值**传递,所以一次 `startThread` 还要多拷一整包。
分配失败逃出这些 `noexcept` 边界就是 `std::terminate`,**没有崩溃转储**(`docs/PITFALLS.md` §45);调用链上没有任何 catch ——
`CoopSessionManager.cpp`(1275 行)的四处 try/catch 在 `:85-87`/`:99-101`/`:877-919`/`:990-1009`,与 `:607`/`:693` 两个入口**都不相干**。

**修法**:不动 `noexcept`(声明不在本仓,`noexcept` 也不进 vtable 布局,单边撤销只换来一个「声明与定义不一致」),
给六个会分配的函数体各加一层 `try { … } catch (...) { SKSE::log::error("Scene API: <名字> failed with an exception"); return Result::Failed; }` ——
**不重试**:调用方 `CoopSessionManager.cpp:687-697` 本来就有 `RetryStartApprovedOwnerSession`,这里再退避只是把同一段分配走两遍。
第七个 `TryGetAutoMode` **不加 try**:`ThreadManager::GetThread()`(`plugins/OStimNG/skse/src/Core/ThreadManager.cpp:108`)只取一次 `std::shared_lock`,
`Thread::isInAutoMode()`(`plugins/OStimNG/skse/src/Core/Thread.h:163`)是 inline 返回一个 `bool`,体内零分配。
同一补丁还补上这批入口一直缺的判空:`StartScene` 加 `!pluginName || !actors || !threadID`(原代码 `:15` 直接读 `actors[i]`),
三个 `StartXScene` 各加 `!pluginName || !threadID`(actor 指针原本就判了),`TryGetAutoMode` 的 `if (!thread)` 扩成 `if (!thread || !autoMode)`(原代码 `:110` 直接写 `*autoMode`)。
`startingAnimation` 的修法不是改 `getNodeById` 的签名,而是在四个调用点写成 `startingAnimation ? Graph::GraphTable::getNodeById(startingAnimation) : nullptr;` ——
`getNodeById`(`plugins/OStimNG/skse/src/Graph/GraphTable/GraphTableNodes.cpp:144`)按值吃 `std::string`,传 null 就是从 null 构造 `std::string`(UB),三目之后天然落进本来就有的 `if (node)` 分支。

**同族先例(判据载重)**:同一批 API 的兄弟 `plugins/OStimNG/skse/src/ModAPI/OstimNG-API-Thread.cpp`(685 行,39 个 `noexcept` 函数)**每一个指针参数都先判** ——
`:297 if (!outData) return;`、`:374 if (!outData) return false;`、`:394 if (!data) return APIResult::Invalid;`、`:426 if (!query || !buffer || bufferSize == 0) return 0;`、`:567 if (!actorFormIDs || actorCount == 0) return false;`;
Scene 侧是这唯一一处两者都缺的。异常接住的形状在本仓已出现多次:`Util/VectorUtil.cpp:11`/`:22`(OStimNG 自己的 `try { … } catch (const std::exception& ex) {}`)、
`STRPluginMessagingBridgeReceive.cpp:174-182`、`STRPMChatUiSuppress.h:113-143`。判据不是「要不要用异常」,而是「**不让异常穿过一个 `noexcept` 边界**」。

**交付**:`Code/plugins/patches/OStimNG/0003-keep-an-allocation-failure-out-of-the-scene-api-entries.patch`
(11507 B / 225 行,一个文件 `skse/src/ModAPI/OstimNG-API-Main.cpp`、一个 hunk `@@ -6,106 +6,149 @@`;pin blob `75bc52c` → 新 blob `f7f4067`,132 行 → 175 行)。
The plugin is a submodule this project does not own and cannot push, and CI runs
`git submodule update --init --force --recursive`, which discards a working-tree only change.
It is therefore shipped as the patch above, which `plugin_patches.py` applies before configuring and building.

- 三重校验实跑(cwd = `plugins/OStimNG`):`git apply --check --cached` 退 **0**(能打到 pin)、前向 `git apply --check` 退 **0**(未应用时干净)、
  应用后 `git apply --check --reverse` 退 **0**(与工作区一致);再次前向 `--check` 退 **1** 并报 `error: patch failed: skse/src/ModAPI/OstimNG-API-Main.cpp:6`(已应用,符合预期)。
- 打到 pristine 索引后 `git show :skse/src/ModAPI/OstimNG-API-Main.cpp` 与手写的新文本**逐字节相等**,随后 `git reset -q` 复原。
- 累积校验:`0001`/`0002`/`0003` 依次 `git apply --cached` 全部退 **0**,`git diff --stat`(工作区 vs 累积索引)**无输出**,`git diff --cached --stat` = 10 files changed, 339 insertions(+), 121 deletions(-)。
- `python Code\plugins\tools\plugin_patches.py verify` → `PATCH VERIFY OK` / **31** patch(es) apply to their pinned commits;`… check` → `PATCH CHECK OK`。
- **本机不编译**(`docs/PITFALLS.md` §0/§1,`plugins/OStimNG/skse/AGENTS.md` 亦逐字写着「do not attempt to build c++ or cmake」),所以「这条链会不会真的耗尽内存」不可判定;
  判据不是「会不会发生」,而是「发生之后进程还剩不剩得下一条记录」—— 修复前是 `std::terminate` 且无转储,修复后是一行日志加一个 `Result::Failed`。

### 修复:STRPM 桥的接收解析器写着 noexcept,而它调用的四个查找函数全都在建 vector —— 判据是「noexcept 的检查要顺着调用链走」

**现象**:没有任何现象。`plugins/STRPluginMessagingAPI/src/STRPluginMessagingBridgeReceive.cpp:431`(pin 1221 行)的
`bool ResolveOnConsumeAddress() noexcept` 由 `:1143 bool Start(STRPM::ReceiveCallback, void*) noexcept`(pin;工作树 `:1181`)调用,
而 `Start` 由 `plugins/STRPluginMessagingAPI/src/STRPluginMessagingBridge.cpp:991`(在 `:946 BootstrapLoop` 内)调用 ——
`BootstrapLoop` 每 750 ms 重试一次(`:36 kBootstrapRetryDelay`),**没有上限也没有退避**。

**根因**:`ResolveOnConsumeAddress` 体内一行分配都没有,但它的调用链上每一步都在建容器:
`EnumerateRuntimeMemory()`(`:240`,非 noexcept,`:242 std::vector<MemorySpan> result` + `:264 result.push_back`)、
`FindTransportTypeDescriptor()`(`:311`,非 noexcept,转调 `FindBytes` + `:318 std::sort` + `:319 erase(unique)`)、
`FindCompleteObjectLocator()`(`:325`,非 noexcept,`:331 std::vector<std::uintptr_t> matches` + `:362 push_back`)、
`FindTransportVftable()`(`:374`,非 noexcept,同形);而 `FindBytes()`(`:274`)每扫 1 MiB 还额外建一个
`std::vector<std::uint8_t> snapshot`(`:295`)。五个被调者**没有一个**写着 `noexcept`,也没有一个有 try/catch。
分配失败逃出 `ResolveOnConsumeAddress` 的 `noexcept` 边界就是 `std::terminate`,**没有崩溃转储**(`docs/PITFALLS.md` §45)。

**修法**:不动 `noexcept` 本身(`Start` 的返回值就是「解析成功了没有」,失败是它的正常语义),
把 `ResolveOnConsumeAddress` 的整个函数体包进 `try { … } catch (...) { Log("receive resolver: ran out of memory while resolving TransportService::OnConsume"); return false; }`。
日志走已有的 `Log(...)`(`:126`,只 `fopen_s`/`fprintf`/`fclose`,不分配);**不重试** ——
`BootstrapLoop` 本来就会在 750 ms 后重来一次,这里再退避只是把同一段分配走两遍。
同一补丁的第二处:`plugins/STRPluginMessagingAPI/src/STRPMRuntimeLoadRedirect.h:15` 的
`inline HMODULE LoadLibraryAdjacentToBroker(LPCWSTR) noexcept` 在 `:34`/`:37`/`:38` 建 `std::wstring` 并 `resize`/`+=`;
它经该文件末尾的 `:55 #define LoadLibraryW STRPMRuntimeLoad::LoadLibraryAdjacentToBroker` 冒充 Win32 加载器
(唯一调用方 `LoadStrBridgeModule` 在 `plugins/STRPluginMessagingAPI/src/STRPluginMessagingAPIRuntime.cpp:854`,而该文件**全文件零 noexcept、零 try/catch**),
所以把这三步分配包进 `try { … } catch (...) {}`,失败时落到函数本来就有的兜底 `return ::LoadLibraryW(modulePath);`(`:46`)。

**同族先例(判据载重)**:`SnapshotProcessMemory()`(`:165`)在同一份文件里早就写着这个形状 ——
`:174-182 try { snapshot.resize(size); } catch (...) { snapshot.clear(); return false; }`;
`STRPMChatUiSuppress.h:113-143`、`STRPMChatUiSuppressV3.h:221-228`/`:254-262` 是另外三处。
判据不是「要不要用异常」,而是「**不让异常穿过一个 `noexcept` 边界**」(`CODE_GUIDELINES.md:47` 的「Don't use exceptions」约束的是控制流,不是这个)。

**交付**:`Code/plugins/patches/STRPluginMessagingAPI/0005-keep-an-allocation-failure-out-of-the-receive-resolver-and-the-load-path.patch`
(8360 B / 174 行,两个文件:`src/STRPluginMessagingBridgeReceive.cpp` 的 `@@ -428,69 +428,84 @@`、`src/STRPMRuntimeLoadRedirect.h` 的 `@@ -31,14 +31,23 @@`)。
The plugin is a submodule this project does not own and cannot push, and CI runs
`git submodule update --init --force --recursive`, which discards a working-tree only change.
It is therefore shipped as the patch above, which `plugin_patches.py` applies before configuring and building.

- 三重校验实跑(cwd = `plugins/STRPluginMessagingAPI`):`git apply --check --cached` 退 **0**(能打到 pin)、
  前向 `git apply --check` 退 **0**(未应用时干净)、应用后 `git apply --check --reverse` 退 **0**(与工作区一致);
  再次前向 `--check` 退 **1** 并报 `error: patch failed: src/STRPluginMessagingBridgeReceive.cpp:428`(已应用,符合预期)。
- 打到 pristine 索引后 `git show :<path>` 的结果与手写的新文本**逐字节相等**(两个文件都比对过),随后 `git reset -q` 复原。
- 累积校验:`0001`…`0005` 依次 `git apply --cached` 全部退 **0**,`git diff --stat`(工作区 vs 累积索引)**无输出**,
  `git diff --cached --stat` = 11 files changed, 300 insertions(+), 91 deletions(-)。
- `python Code\plugins\tools\plugin_patches.py verify` → `PATCH VERIFY OK` / **30** patch(es) apply to their pinned commits;
  `… check` → `PATCH CHECK OK`。
- **本机不编译**(`docs/PITFALLS.md` §0/§1),所以「这条链会不会真的耗尽内存」不可判定;判据不是「会不会发生」,
  而是「发生之后进程还剩不剩得下一条记录」—— 修复前是 `std::terminate` 且无转储,修复后是一行日志加一次「尚未解析」的重试。

### 修复:插件层有一个线程入口声明了 noexcept,而它调用的每一步都在分配 —— 判据是「noexcept 是一条承诺,不是装饰」

**现象**:没有任何现象。`plugins/STRPluginMessagingAPI/src/STRPMChatUiSuppressBootstrap.h:55`(pin 120 行)的
`inline DWORD WINAPI Worker(void*) noexcept` 由 `:95 Start()` 经 `CreateThread` 起,而 `Start()` 由
`plugins/STRPluginMessagingAPI/src/STRPluginMessagingBridgeSKSE.cpp:299`(在 `:289 SKSEPlugin_Load` 内)调用 ——
每次加载这个 SKSE 插件都会起这条线程,没有配置能关掉它。

**根因**:`Worker` 的等待循环里每一步都在分配 —— `ResolveExternalStrAllocationBase()`(`:9`,体内 `EnumerateMemory()`
与 `FindBytes()` 各自增长一个 `std::vector`,`std::sort`/`erase(unique)` 之外还有一个 `std::vector<void*>`)、
`STRPMChatUiSuppressV3::ResolveExecuteAsync()`(`plugins/STRPluginMessagingAPI/src/STRPMChatUiSuppressV3.h:461`,
非 noexcept:两处 `EnumerateMemory`/`FindBytes`、`std::sort`、`std::unordered_map<std::uintptr_t, std::size_t> votes`)、
`STRPMChatUiSuppressV3::Arm()`(`:514`,非 noexcept)。分配失败逃出一个 `noexcept` 入口就是 `std::terminate`,
**没有崩溃转储**(`docs/PITFALLS.md` §45)。更隐蔽的是 `ResolveExternalStrAllocationBase()` 自己也写着 `noexcept`,
却去调两个非 noexcept 且都会 `push_back` 的函数 —— terminate 会发生在**被调者内部**,外层即使有 handler 也够不着。

**修法**:两处,都不动 `noexcept` 本身(这个入口是 `CreateThread` 的回调,`noexcept` 是这里唯一诚实的声明)。
① `ResolveExternalStrAllocationBase()` 去掉 `noexcept` —— 它本来就是会分配的那一层;
② `Worker` 的循环体包一层 `try { … } catch (...) { Log("… ran out of memory"); return 0; }`,
日志走已有的 `STRPMChatUiSuppress::detail::Log`(`:75`,只 `fopen_s`/`fprintf`/`fclose`,不分配)。
失败**不重试**:这个循环等的是运行时解包(最多 120 次 × 1 秒),内存耗尽时重试只是再走一遍同样的分配。

**同族先例(判据载重)**:`plugins/STRPluginMessagingAPI/src/STRPMProxyResolverBootstrapV2.h:248` 与
`plugins/STRPluginMessagingAPI/src/STRPMProxyResolverTrace.h:301` 的 `Worker` 都**没有** `noexcept`,且各自的 `Start()`
都用 try/catch 兜住线程创建(`:325-334` / `:329`)。`STRPMChatUiSuppressBootstrap` 是全项目唯一偏离这个形状的线程入口。

**不可达的同形副本不修**:`plugins/STRPluginMessagingAPI/src/STRPMChatUiSuppress.h:508` 的 `Worker` 有同一个形状
(`:516` 的 `ResolveStrAllocationBase()` 同样写着 `noexcept` 却调非 noexcept 的 `EnumerateMemory`/`FindBytes`),
但 `STRPMChatUiSuppress::Start()`(`:542`)**没有任何调用点** —— 全仓 1758 个源文件扫描,`&detail::Worker` 只出现在
`:550`,即 `Start()` 自身内;`STRPMChatUiSuppressV2.h` 也无人 include。与 Morph 那份「同一段源码的两个副本」不同,
这里是两份独立实现,其中一份死 ⇒ 只修活的那一份。

**交付**:`Code/plugins/patches/STRPluginMessagingAPI/0004-keep-an-allocation-failure-out-of-the-chat-ui-observers-thread-entry.patch`
(3999 B / 81 行,改一个文件:`src/STRPMChatUiSuppressBootstrap.h` 的 `@@ -6,7 +6,7 @@`、`@@ -52,6 +52,14 @@`、`@@ -60,28 +68,37 @@`)。
The plugin is a submodule this project does not own and cannot push, and CI runs
`git submodule update --init --force --recursive`, which discards a working-tree only change.
It is therefore shipped as the patch above, which `plugin_patches.py` applies before configuring and building.

- 三重校验实跑(cwd = `plugins/STRPluginMessagingAPI`):`git apply --check --cached` 退 **0**(能打到 pin)、
  前向 `git apply --check` 退 **0**(未应用时干净)、应用后 `git apply --check --reverse` 退 **0**(与工作区一致)。
- `python Code\plugins\tools\plugin_patches.py verify` → `PATCH VERIFY OK` / **29** patch(es) apply to their pinned commits;
  `… check` → `PATCH CHECK OK`。
- **本机不编译**(`docs/PITFALLS.md` §0/§1),所以「这条线程会不会真的耗尽内存」不可判定;判据不是「会不会发生」,
  而是「发生之后进程还剩不剩得下一条记录」—— 修复前是 `std::terminate` 且无转储,修复后是一行日志加一个未武装的观察者。
### 修复:Morph 的快照只夹了块数,没夹单块大小也没夹总条数 —— 判据是「上界要绑到自己的发送侧能产出什么,而不是绑到传输层允许什么」

**现象**:没有任何现象。这是 §90 那条判据(「拒绝一个状态」vs「夹住一个字段」)在同一个插件里的第二处落地,
也是同族里第一处**上界本身选错了参照物**的:`plugins/MorphSyncTogether/src/MorphSyncServiceBodyHair.cpp:60`
(与它那份被 `#include` 进来的副本 `plugins/MorphSyncTogether/src/MorphSyncService.cpp:585`)只写了 `*count > 128`,
夹的是**块数**;而一块能有多大、整份快照能有多少条 morph,一个门也没有。

**根因**:发送侧(`plugins/MorphSyncTogether/src/MorphSyncService.cpp:441` 的 `constexpr std::size_t kChunkLimit = 2200;`)
把一次快照切成 2200 字符的块,所以**合法**的 `props` 永远不超过 2200 字符、一份快照的值也就那么多;
而接收侧的门只数了块数。一个忽略分块器的对端可以把 128 块每块都塞到传输上限(`STRPM::kMaxPayloadBytes` = 24 KiB),
于是 `values` 可达数十万条,每条都要 `Split` + `HexDecode` + `std::stof`(每个被接受的 value 两次 `std::string` 分配),
全部跑在游戏线程的 `AddTask` 任务里,随后还要逐条 `_bodyMorph->SetMorph` —— 这正是需求 (4)「不得拖慢交互」的那一类。

**修法**:两个上界,都绑到本插件自己的发送侧能产出什么,而不是绑到传输层允许什么 ——
`static constexpr std::size_t kMaxMorphChunkBytes = 4096;`(发送侧切块常量的两倍)与
`static constexpr std::size_t kMaxMorphValues = 4096;`(一份快照就是一个 actor 自己的 morph 列表)。
单块超限走**已有的**入口拒绝分支(与 `*count > 128` 同形,`MST MORPH RX invalid packet`);
总条数超限在解析**之前**用一个预扫描判定(`std::count(chunk.begin(), chunk.end(), ';') + 1` 累加),
这样「拒绝」不需要先付出解析代价,并把这一份 assembly 从 `_assemblies` 里删掉。

**双份实现**:`MorphSyncServiceBodyHair.cpp` 与 `MorphSyncService.cpp` 是同一段源码的两个副本
(`MorphSyncServiceBodyHair.cpp:11` 用 `#define` 改名后 `#include "MorphSyncService.cpp"`,把那份的 `HandleMorphPacket`
变成无调用点的 `HandleMorphPacketLegacy`),两处必须同形改,否则补丁只覆盖一半。

**交付**:`Code/plugins/patches/MorphSyncTogether/0006-bound-a-single-morph-chunk-and-the-values-a-snapshot-may-carry.patch`
(4486 B / 98 行,改三个文件:`src/MorphSyncService.h` 的 `@@ -110,6 +110,18 @@`、`src/MorphSyncServiceBodyHair.cpp` 的
`@@ -57,7 +57,8 @@` 与 `@@ -106,6 +107,22 @@`、`src/MorphSyncService.cpp` 的 `@@ -572,7 +572,8 @@` 与 `@@ -620,6 +621,22 @@`)。
The plugin is a submodule this project does not own and cannot push, and CI runs
`git submodule update --init --force --recursive`, which discards a working-tree only change.
It is therefore shipped as the patch above, which `plugin_patches.py` applies before configuring and building.
pre-image 一律是 **pin 内容**(`plugin_patches.py:439-442`:每个补丁都要能独立打到 pristine 索引上),
所以 `MorphSyncService.h` 的常量块刻意放在 `ReadField` 声明之后(`:110`),与 0004 加表上界的那处(`:126`)分开 —— 两个 hunk 不重叠。

- 三重校验实跑:`git apply --check --cached` 退 **0**(能打到 pin)、`git apply --check --reverse` 退 **0**(与工作区一致)、
  前向 `git apply --check` 退 **1** 报 `error: patch failed: src/MorphSyncService.h:110`(已应用时的预期)。
- **累积验证**:`0001`…`0006` 依次 `git apply --cached` 打到 pristine 索引,六个都退 0;随后
  `git diff --stat`(工作树 vs 累积索引)输出**空** ⇒「pin + 六个补丁」逐字节等于工作树;
  `git diff --cached --stat`(累积索引 vs pin)= 13 files changed, 242 insertions(+), 12 deletions(-)。验证完 `git reset -q`。
- `python Code\plugins\tools\plugin_patches.py verify` → `PATCH VERIFY OK` / **28** patch(es) apply to their pinned commits;
  `… check` → `PATCH CHECK OK`;`python Code\plugins\tools\plugin_snapshot.py verify` → `SNAPSHOT OK`;
  `python Tools\Scripts\check_docs.py` → `DOC CHECK OK`;`python Code\plugins\tools\check_transport_compat.py` → `TRANSPORT COMPATIBILITY OK`;
  `python Code\plugins\tools\strpm_contract.py check` → `CONTRACT OK`。
- **本机不编译**(`docs/PITFALLS.md` §0/§1),所以「一个对端到底会不会这么发」不可判定;判据不是「有没有人这么发」,
  而是「发了以后解析代价由谁决定」—— 修复前由对端决定,修复后由本插件自己的发送侧常量决定。
### 修复:IED 的远端变换把对端的 `nan` 原样写进引擎节点,而它唯一的调用点恰恰在 NaN 时不会被调用 —— 判据是「拒绝一个状态」和「夹住一个字段」在这里不是同一个修复

**现象**:没有任何现象。这是 §89 那条判据(「这个字段的下游有没有第二道门」)在插件层的第一处落地,
也是**同一族里唯一一条「下游的守卫反而让缺陷更难触发」的**:`plugins/IEDSyncTogether/src/RemoteIEDRenderer.cpp:392`
的 `WatchdogTick()` 用 `needsCorrection = rotationDelta > kRotationEpsilon || positionDelta > kPositionEpsilon || scaleDelta > kScaleEpsilon`
决定要不要调 `:444 ApplyRawTransform(remoteObject, tracked.object);`,而三个 delta 由 `:207-219 MaxMatrixDelta` /
`:221-230 MaxPositionDelta` / `:417 std::abs(...)` 算出 —— 全是 `std::max`/`std::abs` 链。**NaN 参与比较恒为假,
所以整条 delta 链在 NaN 上得 0,`needsCorrection` 得 `false`,修正根本不会发生。** 这是降低可达性,不是消除:
同一个对象里只要**还有一项是有限的超阈值值**,`ApplyRawTransform` 就会被调用,于是 NaN 那一项跟着一起写进节点。

**根因**:`nan` 与 `inf` 在这条链上是**合法输入**,而且**合法发送侧自己就会产生它们**。

| 环节 | 位置 | 逐字 |
| --- | --- | --- |
| 捕获 | `plugins/IEDSyncTogether/src/LocalCaptureProbe.cpp:15-18` | `float Q(float value) { return std::round(value * 1000.0f) / 1000.0f; }` —— 量化不剥非有限值(`round(NaN)` 仍是 `NaN`) |
| 编码 | `plugins/IEDSyncTogether/src/LocalIEDState.cpp:56-67` | `fmt::format("…\|{:.6f}\|{:.6f}\|{:.6f}\|{:.6f}", …, object.position[0], object.position[1], object.position[2], object.scale)` —— **fmt 对 NaN 输出文本 `nan`、对 inf 输出 `inf`** |
| 解码 | `plugins/IEDSyncTogether/src/LocalIEDState.cpp:33`(修复前) | `out = static_cast<T>(std::stof(std::string(text)));` —— `std::stof` 把 `"nan"`/`"inf"`/`"infinity"` 当**合法输入接受,不抛** |
| 落点 | `plugins/IEDSyncTogether/src/RemoteIEDRenderer.cpp:238-246` | `object->local.translate.x = captured.position[0];` … `object->local.rotate.entry[row][col] = captured.rotationMatrix[index++];` —— 无守卫;紧邻的 `:241 object->local.scale = std::clamp(captured.scale, 0.01f, 100.0f);` 是**唯一**有守卫的一行 |

`:32-37` 那个 `try { … } catch (...) { return false; }` 只拦 `std::invalid_argument`/`std::out_of_range`,
对非有限值一次也不拦。而 `std::clamp` 同样挡不住 NaN —— NaN 与任何值比较都为假,`clamp` 原样返回。

**判据**:这里**不是**「夹住一个字段」的场合,是「拒绝一个状态」的场合,三条理由:

1. **没有替代值。** 位置、九个旋转项、缩放各是一个独立语义的浮点;`0.f` 对一个位置来说是「原点」、
   对一个缩放来说是「零尺寸」,都不是「这个字段没有值」。§70/§89 用 `0.f` 替代是因为那些字段的下游
   拿到 `0.f` 只是「不生效」;这里写进的是节点的局部变换,`0.f` 是一个**合法但错误**的变换。
2. **失败形态已经存在,而且已经打过日志。** `plugins/IEDSyncTogether/src/STRPMAdapter.cpp:527-535` 本来就有
   `auto state = DecodeLocalIEDState(payload); if (!state) { SKSE::log::warn("STRPM IED state RX decode failed: sender={} name=\"{}\" bytes={}", …); return; }`
   —— 在解码层拒掉,上层走的就是这条既有路径:整份 state 作废、一条 warn、不新增日志点、不改函数签名、不改线格式。
3. **一个 state 是一份快照,不是一串独立字段。** 21 个字段描述同一个对象集合;其中一项是 `nan` 时,
   「其余项照写」并不比「整份丢掉」更接近对端真实的外观 —— 发送侧的 NaN 来自引擎自己给出的坏变换,
   这种情况下对端看到的本来就不是一个可用的姿态。

**修法**:只改 `plugins/IEDSyncTogether/src/LocalIEDState.cpp` 一个文件、两个 hunk:

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

把 `return true;` 换成 `return std::isfinite(out);` 之后,`ParseNumber` 的**整型分支一字未动**
(`std::from_chars` + `result.ptr == last` 本来就拒掉一切非数字),浮点分支对非有限值返回 `false`,
于是 `:134-144` 的 `if (!ParseNumber(fields[8], object.position[0]) || …) { return std::nullopt; }`
与九个旋转项那一段照原样生效 —— **不需要新增任何一个判断点**。

**交付**:`Code/plugins/patches/IEDSyncTogether/0003-reject-a-transform-value-the-sender-would-never-produce.patch`
(943 B / 25 行 / `index 26d1461..0e0879d`)。子模块不归本项目所有,而 CI 的
`git submodule update --init --force --recursive --depth=1` 会丢弃只改工作树的改动,所以以补丁交付。
补丁的两个 hunk 都在 0002 的 hunk(`@@ -93,6 +93,9 @@`)**之前**,所以按 `patch_files()` 的排序累积应用时行号不漂移。

- 补丁的 pre-image 是 **pin 内容**(`git rev-parse :src/LocalIEDState.cpp` = `26d146145ce40693fa2634e605008b385a3ff384`,
  与 0002 补丁头的 `index 26d1461..` 一致),因为 `plugin_patches.py:439-442` 的 `verify` 是**对索引**跑的:
  「The index holds the pinned content even when the patch is already applied to the working tree, so "already applied"
  cannot hide a patch that has stopped matching the pin.」
- 三重校验实跑:`git apply --check --cached` 退 **0**(能打到 pin)、`git apply --check --reverse` 退 **0**(与工作区一致)、
  前向 `git apply --check` 退 **1** 报 `error: patch failed: src/LocalIEDState.cpp:2`(已应用时的预期)。
- **累积验证(这条是本轮新增的复核手法)**:把 `0001`+`0002`+`0003` 依次 `git apply --cached` 打到 pristine 索引上,
  三个都退 0;随后 `git diff --stat`(**工作树 vs 累积索引**)输出**空** —— 即「pin + 三个补丁」逐字节等于工作树,
  补丁集没有漏改也没有多改;`git diff --cached --stat`(**累积索引 vs pin**)= 5 files changed, 31 insertions(+), 2 deletions(-)。
  验证完 `git reset -q` 还原索引(否则 `verify` 会因 `--cached` 读到已打补丁的索引而报 4 条
  `does not apply to the pinned commit - the submodule was re-pinned without rewriting the patch`)。
- `python Code\plugins\tools\plugin_patches.py verify` → `PATCH VERIFY OK` / **27** patch(es) apply to their pinned commits;
  `… check` → `PATCH CHECK OK`;`python Code\plugins\tools\plugin_snapshot.py verify` → `SNAPSHOT OK`。
- **本机不编译**(`docs/PITFALLS.md` §0/§1),所以「谁在真正发 NaN」不可判定;判据不是「有没有人这么发」,
  而是「发了以后有没有人挡」—— 修复前一个都没有,修复后解码层整份拒绝。
### 修复:线上浮点还剩下两处裸读,一处直接写进引擎的转向、一处进 TintMask 的 alpha —— 判据是「这个字段的下游有没有第二道门」

**现象**:没有任何现象。这是 §70 那条判据(「引擎 setter 之前是最后一道门」)的收尾:全仓
`reinterpret_cast<float*>` 只剩 6 处,其中 4 处读的是引擎自己的内存,剩下两处读的是线上字节 ——
`Code/encoding/Structs/Movement.cpp:38` 的 `Direction` 与 `Code/encoding/Structs/Tints.cpp:67` 的 `Alpha`。

**根因**:两个字段都用 `aReader.ReadBits(tmp, 32)` 取位再 `reinterpret_cast<float*>` 重解释,
这条路绕开了 `Serialization::ReadFloat` —— 于是 §70 给 `Code/encoding` 里二十多处读点补的
`WireLimits::ClampFinite` 一处也没覆盖到它们。两个字段的下游都没有第二道门:

| 字段 | 读点(修复前) | 下游 |
| --- | --- | --- |
| `Movement::Direction` | `Code/encoding/Structs/Movement.cpp:38` | `Code/client/Services/Generic/CharacterService.cpp:683 point.Direction = movement.Direction;` → `Code/client/Systems/InterpolationSystem.cpp:112 apActor->currentProcess->middleProcess->direction = second.Direction;`(**直写引擎 middle process**,`:110` 只判了空) |
| `Tints::Entry::Alpha` | `Code/encoding/Structs/Tints.cpp:67` | `Code/client/Systems/FaceGenSystem.cpp:119 tints[i]->alpha = tintsEntries[i].Alpha;`(**写进引擎 `TintMask`**,该函数唯一的门是 `:153 if (acTints.Entries.empty()) return;`) |

两个字段都属于「`+inf` 过得去」那一类:`InterpolationSystem` 拿 `Direction` 算的是朝向,`FaceGenSystem` 拿
`Alpha` 交给引擎混合;`NaN` 参与算术后还是 `NaN`,而这两个下游都不做比较,所以没有一道门能替它挡住。

**修法**:两处统一走既有的 `WireLimits::ClampFinite`(`Code/encoding/WireLimits.h:99`),两个文件各补
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

**为什么是「替换为 0」而不是「夹进一个区间」**:与 §70 同一条 —— 没有任何区间能让 `NaN` 变得有意义,
而这两个字段都不带符号相关的判断(不像血量增量那样需要一个下界),所以不需要给它们单独定范围。
**为什么不动那 4 处引擎内存的 `reinterpret_cast<float*>`**:它们读的不是线上字节
(`Code/client/Games/Skyrim/TESObjectREFR.cpp:243`/`:314` 是引擎的图变量表、
`Code/client/Services/Debug/Views/AnimDebugView.cpp:164` 是调试视图、
`Code/client/Games/Skyrim/Interface/Menus/HUDMenuUtils.cpp:12` 是引擎调用参数)。

**验证**:本机不编译(`docs/PITFALLS.md` §0/§1),证据是逐字命中 + 门禁 + 往返测试。

`Code/tests/encoding.cpp` 的 `[encoding.finite_floats]` 新增四个块(`:1431-1503`),新增两条
`#include`(`:47 <Structs/Movement.h>`、`:49 <Structs/Tints.h>` —— 这两个结构体此前**没有任何往返测试**):

- `Movement` 的 `Direction = NaN` 读回 `0.f`;`Direction = -2.75f` 读回 `-2.75f` 且 `received == sent`
  —— 后一条证明**有限的合法值原样通过**,也就是「读的时候替换了」是前一条断言成立的唯一原因。
- `Tints` 的 `Alpha = +inf` 读回 `0.f`;`Alpha = 0.375f` / `Color = 0x11223344` / `Type = 7` 整包 `==`
  —— 后一条同时钉住字段顺序没被改动。
- 全仓 `git grep -rn "ClampFinite" -- Code/encoding` 现在 **30 处**命中(含定义 `WireLimits.h:99`)。
- `python Tools\Scripts\check_docs.py` → `DOC CHECK OK`。
### 修复:服务端的日志 sink 把 admin 协议的帧发给游戏连接 —— 判据是「收件人集合里的成员,是不是这个协议的端点」

**现象**:没有任何现象,而且是**纵深修复** —— 这一条和上一条同源,今天连触发条件都凑不齐:
管理端根本连不上服务端。`Code/admin/Transport.cpp:43-51` 的 `AdminApp::OnConnected` 建的
`AuthenticationRequest` **只填了 `Token`**,`Version` 留空;而 `Code/server/GameServer.cpp:885`
的版本判断发生在 `:918` 看令牌**之前**,于是管理端永远拿不到 `m_adminSessions` 的席位。

**根因**:`Code/server/Services/AdminService.cpp:22-31`(修复前)把**每一条服务端日志**
格式化成一个 admin 协议的 `ServerLogs`,交给 `Code/server/GameServer.h:73-78` 的 `ForEachAdmin`
广播出去 —— 而那个集合的语义是「**管理员凭据**」,不是「管理端连接」:

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

`m_adminSessions` 的**两个**写入点都在游戏握手路径上,成员必然是普通游戏连接:

| 写入点 | 语义 |
| --- | --- |
| `Code/server/GameServer.cpp:925` | 游戏客户端用管理员密码登录(`:918` 的 `adminPasswordUsed`),在 `HandleAuthenticationRequest` 里插入 |
| `Code/server/GameServer.cpp:430`/`:441` | `addadmin` 控制台命令按用户名解析出 `Player*` 再取其 connection id |

而两套协议的**编号是重叠的**:`Code/admin_protocol/Opcodes.h:13` 的 `kServerLogs = 1`,
`Code/encoding/Opcodes.h:11` 的 `kAssignCharacterResponse = 1`。于是这一帧落在游戏客户端上时,
`Code/encoding/Messages/ServerMessageFactory.cpp:32-42` 会**合法地**解出 `AssignCharacterResponse`,
接着 `Code/client/Services/Generic/CharacterService.cpp:358-430` 拿这串字节去当
`Cookie`、`AllActorValues`、`CurrentInventory`、动作列表——即「一条日志行被当成一次角色指派」。

**修法**:这一帧**没有收件人**,所以不构造它。`sink_it_` 只留一行 `(void)msg;`,sink 本身
继续挂在服务端 logger 上(`Code/server/World.cpp:24-25`),把这条流留给下一个读者。

- **为什么不是「删掉这个 sink」**:它被 `World` 持有(`Code/server/World.h:55`),删掉要动
  `World.cpp:24-25` 与头文件;`docs/PITFALLS.md:873-874` 已经就同一处下过结论:
  「删掉会改日志行为。保留。」
- **为什么不是「给 `ForEachAdmin` 加过滤」**:过滤器要回答的是「这个连接是不是管理端」,
  而**这个问题今天没有答案** —— 服务端根本没有「管理端连接」这个概念,只有「知道管理员密码的
  游戏连接」。等 `Code/server/GameServer.cpp:593-607` 的 admin 分派从注释里出来,
  判据才有对象。
- **顺带**:`sink_it_` 是**每一条 spdlog 调用**都会走的路径,原实现在这里把同一条日志
  **第二次格式化**(`formatter_->format`)并做一次 `fmt::to_string`。日志量越大,这笔
  开销越稳定地落在服务端线程上(满足「不拖慢」那条要求)。

**验证**:本机不编译(`docs/PITFALLS.md` §0/§1),证据是逐字命中 + 门禁 + 双向反向验证。

- `python Code\plugins\tools\check_transport_compat.py` → `TRANSPORT COMPATIBILITY OK`。
- 门禁新增两半(`Code/plugins/tools/check_transport_compat.py:249-315`):
  **①** 解析 `ServerAdminOpcode.kServerLogs` 与 `ServerOpcode.kAssignCharacterResponse` 的**数值**,
  两者必须仍然相等(注释里的那句话必须成立);
  **②** 读 `Code/server/Services/AdminService.cpp` 的**代码行**(先剥掉 `//` 之后的内容),
  不允许出现 `ServerLogs` 或 `ForEachAdmin`。
- **反向验证过两次**:把 `ServerLogs logs;` 放回 `sink_it_` 的函数体 → 门禁
  `TRANSPORT COMPATIBILITY FAILED (1)` + 上面那条失败说明,exit 1;把
  `kServerLogs` 改成 `= 9` → 同样 `FAILED (1)`,exit 1。两次还原后都回到 `OK`。
  这条证据同时说明第二半查的是**定义**而不是**提及** —— 新写的注释里就有 `ServerLogs` 与
  `ForEachAdmin` 两个词,注释被剥掉后门禁不误报。

### 修复:整包尺寸这条上限有两个接收入口在用、第三个没用,而注释里写的就是「只有两个」 —— 判据是「这条上限的入口数,和入口的总数对得上吗」

**现象**:没有任何现象,而且这一条比 §84 更远:管理端(`TiltedAdmin`)当前**根本收不到**服务端的消息 ——
`Code/server/GameServer.cpp:593-607` 的 admin 分派整段被注释掉(`// TODO: ClientAdminMessageFactory`)。
所以这不是活缺陷,是**纵深修复**:§84 把判据限定在「游戏本体/插件层」,明确写下了「不改管理端」;
这一条推翻的正是那个限定 —— 不是因为管理端现在可打,而是因为**同一条上限的作用域应该等于接收入口的个数**。

**根因**:`Code/encoding/WireLimits.h:23-27` 的注释自己给出了那个错误的范围:

```cpp
// Code/encoding/WireLimits.h(修复前 :23-27)
// Note the limit of the fix: the clamp runs after the string has been read, so it bounds
// what the value is used for, not the transient allocation inside the reader. Bounding
// the packet itself is what kMaxPacketBytes below does, and the only place for it is the
// receive entry point (GameServer::OnConsume and Client::OnConsume), which see the
// packet size.
```

全仓的接收入口是**三个**,不是两个:

| 入口 | §84 之前 | §84 | 本条之后 |
| --- | --- | --- | --- |
| `Code/server/GameServer.cpp:573 GameServer::OnConsume` | 无判断 | `WireLimits::WithinPacketLimit` | 不变 |
| `Code/client/Services/Generic/TransportService.cpp:181 TransportService::OnConsume` | 无判断 | `WireLimits::WithinPacketLimit` | 不变 |
| `Code/admin/Transport.cpp:16 AdminApp::OnConsume` | 无判断 | **漏了** | `WireLimits::WithinPacketLimit` |

漏掉的那一处把 `aSize` 直接交给 `ViewBuffer`,再交给 `ServerAdminMessageFactory::Extract`:

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

- **为什么 `kMaxPacketBytes` 对管理端也是对的**:管理端自己的出包上界是 `1 << 16`(`Code/admin/Transport.cpp:75`、`:103`
  两个 `Send` 重载都建 `TiltedPhoques::Buffer buffer(1 << 16)`),服务端给它发消息走的是 `Code/server/GameServer.cpp:708`
  那个 `1 << 20` 的缓冲。取大者 ⇒ `1 << 20` 对两端都成立,不需要为管理端单独定义一个常量。
  这也是 §84 已经立过的判据:「这个值必须**大于等于**发送侧能产出的一切」。
- **为什么不按「反正现在接不到消息」放过**:门禁查的是**入口数**,不是「今天有没有流量」。
  管理端的分派被注释掉是一行 `TODO`,而不是一个架构约束;消费端
  `Code/admin/Transport.cpp:128-131` 的 `HandleMessage(const ServerLogs&)` 也还在。把 `//` 去掉的那一刻,
  这个入口就是活的,而那时没人会想起来补一个尺寸判断。
  (生产侧那半 —— `AdminService::sink_it_` 把每条服务端日志打包成 `ServerLogs` 广播出去 ——
  已在**上一条**里关掉:它的收件人集合是游戏连接,而 `kServerLogs` 与 `kAssignCharacterResponse`
  同为 `1`。这里只留接收入口这一半。)
- **为什么顺带给管理端加 `#include <spdlog/spdlog.h>`**:管理端此前没有任何日志设施 ——
  `Code/admin` 与 `Code/admin_protocol` 两个目录里 `spdlog` 出现 0 次(唯一命中是 `Code/admin/xmake.lua:26` 的包名)。
  §84 的「不改管理端」正是拿这一点当理由之一(「没有日志设施、失败分支是裸 `return;`」)。
  这次反过来:一个丢弃分支如果什么都不说,门禁就成了唯一的证据,而门禁只证明调用存在、不证明它被走到。
  `Code/admin/xmake.lua:24-31` 的 `add_packages(... "spdlog" ...)` 已经在依赖里,构建脚本不需要改。

**验证**:本机不编译(`docs/PITFALLS.md` §0/§1),证据是逐字命中 + 门禁 + 反向验证。

- `python Code\plugins\tools\check_transport_compat.py` → `TRANSPORT COMPATIBILITY OK`。
- **反向验证过**:把 `AdminApp::OnConsume` 里的 `if (!WireLimits::WithinPacketLimit(aSize))` 临时换成
  `if (aSize > (1u << 20))`,门禁输出
  `TRANSPORT COMPATIBILITY FAILED (1) - Code/admin/Transport.cpp AdminApp::OnConsume does not call WireLimits::WithinPacketLimit;
  the size of the packet is known only there, so without it the bound does nothing`,exit 1;还原后重新 `OK`。
- 门禁的 `PACKET_GATE_SITES` 从 2 项加到 3 项(`Code/plugins/tools/check_transport_compat.py:211-215`),
  那条 rationale 注释里原本写的「the receive entry points」也改成了「三个」。

> **教训**:第一条:**「只有两个入口」这句话本身没有被门禁检查过。**
> §84 的注释、门禁的 `PACKET_GATE_SITES`、和真实入口数是三份互相独立的记录,当时只有前两份互相对齐。
> 判据是「这条上限的入口数,和入口的总数对得上吗」—— 一份手工维护的名单,它的价值等于它被核对过的次数。
> 第二条:**「现在不可达」和「不该有这道门」是两回事。**
> 这一处离活缺陷的距离是一行 `//`,而它旁边就写着 `// TODO`。
> 把范围排除写进文档(§84 的「不改管理端」)比写进代码更危险:文档不会被编译器或门禁提醒。
> 第三条:**同一条上限在两个进程里的正确值可能相同,但理由不同。**
> 这里 `1 << 20` 同时覆盖管理端的 `1 << 16` 出包与服务端的 `1 << 20` 出包 ——
> 是「取大者」这条既有判据让它一次成立,而不是因为管理端「碰巧」和客户端一样大。
### 修复:插件层的浮点图变量只有发送侧判了有限,接收侧把线上的数直接喂给动画图 —— 判据是「这个值两端都判过吗」

**现象**:没有任何现象。这是 §70 那二十处「对端说了算的浮点」在插件层的同一条:正常流量下永远不会分岔,
只有在对手存在时才成立,而对手存在时它是**未认证的对端**就能触发的那一类。

**根因**:同一条通道的两端,一侧判、一侧不判。发送侧 (`plugins/AnimSyncTogether/src/GraphVariableSync.cpp:68`) 是:

```cpp
// plugins/AnimSyncTogether/src/GraphVariableSync.cpp:68
if (!holder->GetGraphVariableFloat(variableName, value) || !std::isfinite(value) || !HasChanged(rule, value)) {
    break;
}
```

接收侧 (`plugins/AnimSyncTogether/src/STRPMClient.cpp:459-546`) 从头到尾没有任何有限性判断:

```cpp
// plugins/AnimSyncTogether/src/STRPMClient.cpp(修复前 :459-546)
void STRPMClient::ApplyGraphVariableMessage(..., float floatValue)   // :459 线上来的 float
{
    const auto type = static_cast<GraphVariableType>(variableType);  // :471
    if (!SyncRules::GetSingleton()->ShouldSyncGraphVariable(name, type)) { ... }

    switch (type) {
    case GraphVariableType::kFloat:
        applied = actor->SetGraphVariableFloat(variableName, floatValue);   // :531 直接进引擎
```

`floatValue` 来自 `packet->graphVariableFloat`(`:291`),而那个结构体是**逐字节从 STRPM 载荷强转**出来的
(`:260` 只判了 `message->size == sizeof(SyncPacket)`,即长度,不判内容)⇒ `NaN` / `±inf` 一路畅通。

- **为什么不是「反正动画图会夹住」**:动画图不会。这个变量喂的是 `SetGraphVariableFloat`,它写进行为图之后被
  选剪辑的逻辑与 OAR 的替换条件读走(`:543-544` 之后紧跟 `OARClient::ClearConditionStateData`);
  同族已经修过 6 处(`WireLimits::ClampFinite`,见 §70),判据是同一条:引擎 setter 之前是最后一道门。
- **为什么判在 `ShouldSyncGraphVariable` 之后而不是之前**:`type` 只有在规则里存在时才可信 ——
  规则表要求 `it->second == type`(`plugins/AnimSyncTogether/src/SyncRules.cpp:126-132`),
  所以 `type == GraphVariableType::kFloat` 成立时,这条规则真的是 `float`,而不是对端自称的。
  把判断放在前面就等于相信对端填的 `variableType`。
- **为什么整包丢弃而不是夹成 0**:一个不是有限数的图变量没有「接近的值」,它是发送侧自己都会拒绝发送的东西
  (`GraphVariableSync.cpp:68`)⇒ 收下它就等于承认这一包是数据。与 §70 里那些有量纲的量不同,
  这里没有可以夹取的目标值。
- **交付形式是补丁**:`plugins/AnimSyncTogether` 是本仓不拥有、也不能 push 的子模块,
  而 CI 会 `git submodule update --init --force --recursive` 丢掉工作树里的改动。

**验证**:本机不编译(`docs/PITFALLS.md` §0/§1),证据是逐字命中 + 补丁可打在 pin 上 + 门禁全绿。

- `python Code\plugins\tools\plugin_patches.py verify` → `PATCH VERIFY OK` / `26 patch(es) apply to their pinned commits`。
- `git apply --check --cached` 对该补丁 exit 0(打在 pin `a432ec34` 上),`--reverse` 对当前工作树 exit 0。
- 同族对照:整个插件层的 `SetGraphVariableFloat` 只有 4 处,另外 3 处(`plugins/OStimNG/skse/src/GameAPI/GameActor.cpp:74`、
  `:102`、`plugins/OStimTogether/src/FreeScenePhaseSync.cpp:953`)写的是本地算出来的常量或本地状态,不是线上的字节。
- 插件层 `std::isfinite` 的既有用法共 26 处(MorphSyncTogether / OStimTogether 为主),本条补上的是唯一漏掉的一处接收侧。

> **教训**:第一条:**「发送侧判过」不是「这个值安全」。**
> 发送侧的判断保护的是发送侧自己;一个值从线上回来时,它只经过了长度检查。
> 判据是「这个值两端都判过吗」,而不是「有没有人判过」。
> 第二条:**判定的位置比判定本身更重要。**
> 同一个 `isfinite`,放在 `ShouldSyncGraphVariable` 之前是错的 —— 那时 `type` 还是对端自称的;
> 放在之后,`type` 才由本地规则表担保。**先证明「这个字段是什么」,再判「这个值合不合法」。**
> 第三条:**「引擎会兜住」在这个仓库里已经被推翻过很多次,这次也一样。**
> `SetGraphVariableFloat` 之后没有第二道门,它写进去的东西会被选剪辑与 OAR 条件读走 ——
> 而这一处比 §70 那些更难发现,因为它长在一个**只由规则文件开关**的路径上:
> 默认的 `HelmetToggle2.rules` 只声明了两个 `int`,所以即使实机跑起来也看不到它。

### 修复:任务日志的条数上限写侧在用、读侧被忽略,而读侧拿它 resize —— 判据是「同一份常量有没有被两边都读到」

**现象**:没有任何现象。这一条同样属于「修掉它不会让任何一局游戏变好」的那一类:它只在对手存在时才有意义。

**根因**:`QuestLog` 的条数常量一直在,而且一直在写侧被使用,读侧却绕过了它:

```cpp
// Code/encoding/Structs/QuestLog.cpp(修复前 :13 / :25-28 / :39-54)
static constexpr size_t kMaxEntryCount = 0xFFFF;   // 文件作用域,只有写侧读它

void QuestLog::Serialize(...) const noexcept
{
    const auto count = std::min<size_t>(Entries.size(), kMaxEntryCount);   // :27 写侧夹了
    aWriter.WriteBits(count, 16);

void QuestLog::Deserialize(TiltedPhoques::Buffer::Reader& aReader) noexcept
{
    aReader.ReadBits(temp, 16);
    const size_t count = temp & 0xFFFF;
    Entries.resize(count);                          // :45 读侧照抄线上的数
```

`count` 最大 65535,一条 `Entry` 是一个 `GameId` 加一个 `uint16_t`,而 `GameId` 的两个字段各自是一个
VarInt —— 也就是**只要两个字节**就能让接收侧按 65535 条去 `resize`,在任何一条条目字节被读到之前。
`WireLimits::kMaxPacketBytes`(1 MiB)挡不住这一步:它在 `GameServer::OnConsume` 里判,而这里要分配的是
「条数 × 单条大小」,与包有多大无关;`WireLimits` 里那些逐字段的上限也全都跑在读取之后。

- **为什么上界取 `0x1000` 而不是继续用 `0xFFFF`**:`0xFFFF` 是线上计数的位宽,不是数据的量级。
  任务日志的行数由客户端 `PlayerCharacter::objectives` 决定(`Code/client/Services/Generic/CharacterService.cpp:1439`
  遍历的就是那个数组),真实存档在几百以内;`Inventory` 的同类上限(也是 VarInt 计数)取的就是 `0x1000`。
  写侧仍然按位宽夹到 `0xFFFF`,因为那是「线上能不能表示」的问题,不是「该不该分配」的问题。
- **为什么读侧是 `clear()` + 逐条 `push_back`,而不是「夹取后 `resize`」**:夹取只解决了这一包要分配多少,
  没解决「包短于声明时那些没被读到的条目」—— `resize` 出来的条目是默认值,会被当成真的日志内容。
  逐条 `push_back` 让分配跟着真正读到的条目走。这也是 `Inventory`(`Code/encoding/Structs/Inventory.cpp:124-136`)
  与 `Tints`(`Code/encoding/Structs/Tints.cpp:52-70`)已经用的形状。
- **`clear()` 是必须的,不是顺手加的**:`Deserialize` 不保证被调进一个全新的对象,
  追加语义会让复用的对象每收一包就长一截 —— `Tints.cpp:50-51` 上已经写过同一条注释。
- **为什么常量从文件作用域挪进结构体**:门禁要能在 `Code/tests` 里断言它,而测试只能看到公开成员。
  服务端同类上限的既有位置是类内 `static constexpr`(`Code/server/Services/ObjectService.h:31-37`)。
- **为什么同时给服务端的任务日志表加上限**:`Code/server/Services/QuestService.cpp:73` 的 `entries.emplace_back`
  是这张表唯一的增长点,而唯一的删除点是 `:91-101` 那条 `Stopped` 分支 —— 键是客户端挑的 `GameId`,
  客户端一直造新 id 就一直涨,和 `ObjectService` 那张表是同一类。上限取 4096,已存在的 id 照常更新,
  满了只拒绝新增的那一条(`ObjectService` 的四条规则逐条照搬)。

**验证**:本机不编译(`docs/PITFALLS.md` §0/§1),证据是逐字命中 + 新用例。
`Code/tests/encoding.cpp:1642-1704` 新增
`TEST_CASE("A quest log cannot make the reader allocate what it never sent", "[encoding.quest_log]")`:`

- 写侧喂 `kMaxEntryCount + 7` 条,读回来正好是 `kMaxEntryCount` 条,且首尾两条的内容对得上。
- 手写一个声明 `0xFFFF` 条、实际也只写了 `kMaxEntryCount` 条的包,读侧停在 `kMaxEntryCount` —— 夹取生效且流被消费干净。
- 一个已经装了两条旧条目的对象收到只声明一条的包之后只剩一条 ⇒ 替换语义,不是追加。

> **教训**:第一条:**「常量存在」不等于「常量在生效」。**
> 这一处比 §84 那处更隐蔽 —— §84 是上限根本不存在,这里是上限存在、名字对、值对、注释也对,
> 只是**读侧那一行没引用它**。检查的办法不是「有没有这个常量」,而是「用到它的地方是不是两边都有」。
> 第二条:**`resize(线上的数)` 和 `push_back(读到的条目)` 在正常流量下完全一样。**
> 两者只在包短于声明时分岔,而那正是对手能造出来的唯一形状 —— 用正常流量做回归测试永远测不出区别。
> 第三条:**上限的「值」和上限的「位置」是两个决定。**
> 写侧夹的是「线上能不能表示」,所以用位宽;读侧夹的是「该不该按这个数分配」,所以用量级。
> 两者可以不同,但必须是**两个都写出来**的决定,不能靠一个常量同时承担。

### 修复:整包尺寸没有任何上限,而包内每一个长度字段都被当成真的 —— 判据是「谁在分配之前知道这个包有多大」

**现象**:没有任何现象。这是本仓库里唯一一类「修掉它不会让任何一局游戏变好」的缺陷 —— 它只在对手存在时才有意义,
而对手存在的假设下,它是**未认证连接**就能触发的那一类。

**根因**:两个接收入口都先把字节包成 `ViewBuffer`、再交给工厂解析,**中间没有任何尺寸判断**:

```cpp
// Code/server/GameServer.cpp(修复前 :572-602)
void GameServer::OnConsume(const void* apData, const uint32_t aSize, const ConnectionId_t aConnectionId)
{
    ViewBuffer buf((uint8_t*)apData, aSize);          // :574 aSize 唯一一次被使用
    Buffer::Reader reader(&buf);

    const ClientMessageFactory factory;
    auto pMessage = factory.Extract(reader);          // :593 从这里开始相信包内的一切
}
```

客户端 `Code/client/Services/Generic/TransportService.cpp:180-206` 是同一个形状。

包内每一个长度字段都是「这个包里有这么多字节」的**声明**,而解析器按声明分配(`Mods::Deserialize` 的 8191 计数、
`Inventory` 的 4096 条、每条字符串的 `ReadString`)。`Code/encoding/WireLimits.h` 里的那些字符串上限全都
**在读完之后**才夹取 —— 它们约束的是「值怎么用」,不是 reader 内部的瞬时分配。这个缺口在 `WireLimits.h` 的
头注释 `:23-26` 里被明确写下来了,写的是「Bounding the packet itself is a separate change, and the only place for it
is the receive entry point」—— 文档把判据写死了,实现一直不存在。

**为什么没有任何东西报错**:因为报错的前提是有人发大包,而合法客户端永远发不出大包 —— 两个发送侧都在固定缓冲区里
序列化(服务端 `Code/server/GameServer.cpp:676` 的 `Buffer buffer(1 << 20)`,客户端
`Code/client/Services/Generic/TransportService.cpp:165` 的 `Buffer buffer(1 << 16)`)。于是这条路径在正常对局里
**不可达**,在 CI 里也不可达,只有在对手存在的假设下才可达 —— 而那时它是未认证连接可达。

**为什么 CI 挡不住、本地也看不见**:没有任何测试构造过「一个比发送侧缓冲区更大的包」,因为没有哪一处代码在问这个问题。
`Code/**` 全仓搜 `aSize >` / `packetSize` / `kMaxPacket` **零命中** —— 这不是「门禁太松」,是「门禁不存在」。

**修法**:上限放进 `Code/encoding/WireLimits.h`,与其它上限放在一起;判断在两个接收入口的最前面:

```cpp
// Code/encoding/WireLimits.h
inline constexpr uint32_t kMaxPacketBytes = 1 << 20;

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
```

- **为什么上限是 `1 << 20` 而不是更小**:这个值必须**大于等于**发送侧能产出的一切,否则它会开始丢弃合法流量,
  而丢包的后果看起来像失同步、不像丢包。服务端序列化进 `1 << 20`,客户端进 `1 << 16`,取两者的大者 —— 这就是
  「我们自己的对端能发出的最大包」这个量的定义。写得再小需要先证明「没有任何一条合法报文能超过 X」,
  而 `AssignObjectsRequest` 一条就能带 255 个 `ObjectData`、每个含一个完整 `Inventory`(4096 条 × 每条 ≤64 个
  `EffectItem`,每项含最长 260 字节的附魔串),这个证明本地做不出来。
- **为什么放在 `Code/encoding` 而不是放在两个调用者旁边**:`Code/tests/xmake.lua` 只 `add_deps("SkyrimEncoding")`,
  那是测试目标唯一能看到的库 —— 门禁放服务端就永远没有用例(`docs/PITFALLS.md:7446-7465` 的 §62.4 已立过同一条判据)。
- **为什么拆出一个 `WithinPacketLimit` 而不是直接写 `aSize > kMaxPacketBytes`**:边界本身要可测。
  把判断留成常量比较,测试只能测常量;留成函数,测试能测「等于上限通过、上限加一不通过」。
- **为什么是「丢弃 + 一行 warn」而不是「夹取」**:夹取在这里没有意义 —— 包被截断之后剩下的字节不再是一个包。
  这一条与字符串那些上限的取舍**相反**,理由是两者处理的对象不同:字符串截短之后仍然是那个字符串的近似,
  包截短之后不是。
- **不改管理端**(`Code/admin/Transport.cpp:12-25`):它是独立桌面二进制、没有日志设施、失败分支是裸 `return;`,
  不在「游戏本体/插件层」的判据范围内。
- **同时加一条门禁**:`Code/plugins/tools/check_transport_compat.py` 现在会读 `WireLimits.h` 里的 `kMaxPacketBytes`,
  断言它不低于服务端的 `1 << 20`,并断言两个接收入口真的调用了 `WithinPacketLimit`。
  理由是这条上限有一个**静默失败方向**:把值改小不会让任何测试变红,只会让大包在对手不存在的对局里消失。

**验证**:本机不编译(`docs/PITFALLS.md` §0/§1),证据是逐字命中 + 新用例 + 门禁。
`Code/tests/encoding.cpp:1612-1638` 新增
`TEST_CASE("A packet larger than any sender can produce is rejected before it is read", "[encoding.packet_limit]")`:

- `WithinPacketLimit(0)` / `(1)` / `(kMaxPacketBytes)` 通过,`(kMaxPacketBytes + 1)` 不通过 —— 边界钉在用例里。
- `WithinPacketLimit(std::numeric_limits<uint64_t>::max())` 不通过 ⇒ 64 位尺寸不会回绕进接受区间。
- `kMaxPacketBytes >= (1u << 20)` ⇒ 这条上限永远不会低于服务端自己的出包能力。
- `kMaxPacketBytes` 大于三个字符串上限 ⇒ 两层门禁独立,改一层不会把另一层关掉。

`python Code\plugins\tools\check_transport_compat.py` 通过,且**反向验证过**:把客户端那处调用临时换成
`aSize > (1u << 20)` 之后,门禁输出
`TRANSPORT COMPATIBILITY FAILED (1) - Code/client/Services/Generic/TransportService.cpp TransportService::OnConsume does not call WireLimits::WithinPacketLimit; the size of the packet is known only there, so without it the bound does nothing`。

> **教训**:第一条:**「上限写在文档里」和「上限存在」是两件事。**
> `WireLimits.h` 的头注释逐字写了「整包上限只能在接收入口加」,写了很久,而两个入口一次都没判过尺寸 ——
> 一段把缺口描述得非常准确的注释,读起来和一段修好的代码一样让人放心。
> 第二条:**安全上限的失败方向是「静默丢包」,不是「崩溃」。**
> 把 `1 << 20` 改成 `1 << 10` 不会让任何测试变红,只会让某些报文在正常对局里凭空消失,
> 而消失的后果看起来像失同步 —— 所以这条常量需要一个门禁去盯它的**下界**,光有用例盯上界不够。
> 第三条:**同一个「太大」,在字符串层和包层的正确取舍是相反的。**
> 字符串截短之后仍然是那个字符串的近似(所以夹取);包截短之后不是包(所以丢弃)。
> 判据不是「夹取更温和」,而是「截断之后剩下的东西还有没有意义」。

### 修复:字符串缓存的起点是对端填的字段,而它被拿去喂 assert —— 判据是「这个断言的实参是谁选的」

**现象**:客户端在游戏线程上收到一条 `StringCacheUpdate` 之后进程直接退出,没有崩溃栈、没有异常、
没有日志 —— 因为杀掉它的不是崩溃,是 `assert`。触发条件不是「对端发坏包」,而是「对端的字符串缓存
与本地不同步」,后者在版本混连、存档重载、脚本改过游标之后都可能自然发生。

**根因**:`Code/encoding/StringCache.cpp` 的 `Deserialize` 用两条断言校验连续性,而两条的实参**都不是本地算的**:

```cpp
// Code/encoding/StringCache.cpp(修复前 :75-87)
    // We should only receive contiguous updates
    assert(aMessage.StartId == m_idToString.size());          // :78 实参来自线上

    for (auto& value : aMessage.Values)
    {
        const auto expectedId = m_idToString.size();
        auto id = Add(value);

        assert(id == expectedId);                             // :85 实参受对端内容影响
    }
```

`StartId` 是包里的字段(`Code/encoding/Messages/StringCacheUpdate.cpp:23` 的
`StartId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;`,没有任何本地校验);
而 `Add` 在值**已经在缓存里**时早退返回旧 id(`Code/encoding/StringCache.cpp:26-36`),
所以 `:85` 那条在「同一条消息里出现重复值」时也会失败 —— 而重复值是一条完全合法的消息。

**为什么没有任何东西报错**:`assert` 失败不产生日志,它调用 `abort()`。
更糟的是它**看起来像文档**:`Code/encoding/StringCache.cpp` 写侧的注释当时写着
「the receiver asserts that the ids are contiguous」,`docs/PITFALLS.md` 的 §49.2 判据表
与 §73 的排除语也都把它当成「正确的不变量断言」引用过。三处引用一处代码,没有一处问过
「这个实参是谁选的」。

**为什么 CI 挡不住、本地也看不见**:全仓 `NDEBUG` **零命中**,而 CI 的默认 `build-mode` 是 `releasedbg`
(`.github/workflows/windows.yml:11-14`)—— 也就是说断言在**发布构建里也是活的**,这条路径不需要 debug 构建。
本地同样看不见:`StartId` 不匹配要求两端缓存长度不同,而正常流程下服务端从 0 开始推、客户端从 0 开始收,
只有失步(混版本、重载存档、脚本改游标)才会碰到。

**修法**:两条断言换成「不连续就丢弃这条消息,缓存原样保留」(`Code/encoding/StringCache.cpp:76-104`):

```cpp
    if (aMessage.StartId != m_idToString.size())
        return;

    for (auto& value : aMessage.Values)
    {
        const auto expectedId = m_idToString.size();
        const auto id = Add(value);

        if (id != expectedId)
            return;
    }
```

- **为什么是「丢弃」而不是「清空缓存」**:第一版写的是 `Clear()`,测试也按那个方向写好了。改判的理由是代价 ——
  服务端每 2000 ms 才推一次增量(`Code/server/Services/StringCacheService.cpp:17`),
  清空会把一次失步放大成两秒的同步空洞,而丢弃只损失这一条。
- **为什么丢弃是安全的**:`m_idToString` 在任何 `return` 点上都仍是发送侧缓存的**连续前缀**。
  依据只有两条 —— `StartId` 的判断在任何变更之前;`Add` 在值已存在时早退且不触碰任何容器。
  所以「下一次发送的切片仍然对得上」是可推导的,不是希望。
- **重复值那条分支不是回滚**:`{"gamma","gamma"}` 的第一条 `Add` 成功之后才轮到第二条返回旧 id,
  丢弃之后缓存里有 `gamma` —— 这一点写进了测试的注释。
- **写侧注释同步改掉**(`Code/encoding/StringCache.cpp:55-59`):那句「the receiver asserts that the ids are contiguous」
  描述的是一个会把进程杀掉的契约,改实现不改它,下一轮审计还会把它当先例引用。
- **不打日志**:`Code/encoding` 层没有任何日志设施(全目录 3 个 `#include <iostream>` 一处使用都没有,也没有 spdlog),
  而唯一的调用点 `Code/client/Services/Generic/StringCacheService.cpp:28-31` 是 `noexcept` 且没有返回值,
  拿不到失败信息 ⇒ 这次是静默自愈。

**验证**:本机不编译(`docs/PITFALLS.md` §0/§1),证据是逐字命中 + 新用例。
`Code/tests/encoding.cpp:575-647` 新增
`TEST_CASE("A string cache update that does not line up is dropped", "[encoding.string_cache_selfheal]")`:

- `StartId = 5` 而本地长度是 2 ⇒ 断言 `Size() == 2`、`alpha`/`beta` 都还在、`gamma` 没进去
  (把「丢弃而不是清空」钉在用例里:改成 `Clear()` 这条断言就红)。
- 紧接着造一个「id 9 不在缓存里」的 `CachedString` 包,断言名字读成空串 —— 「丢弃之后消费者仍然安全」的证据。
- 重复值 `{"gamma","gamma"}` ⇒ 断言 `Size() == 3` 且 `gamma` 在,即第一条落了地。
- 对齐的消息(`StartId = 1`,`{"beta","gamma"}`)照旧生效 ⇒ 断言三条全在,证明正常路径没有被一起关掉。

> **教训**:第一条:**`assert` 不是「文档化的前置条件」,是「谁都能触发的进程退出」。**
> 判断一条断言该不该留,只问一句:它的实参是**谁**选的?本地算的,留;对端填的,换掉。
> 第二条:**注释会替错误作证。** 写侧那句「the receiver asserts that the ids are contiguous」
> 把「对端一跳就终止进程」描述成了一条设计约定,而它读起来完全像文档 ——
> 改掉实现时必须回头把引用它的注释、判据表、代码块引文一起改掉。
> 第三条:**「丢弃」和「清空」都叫「失败处理」,但代价差两个数量级。**
> 失败处理的粒度要跟「重来一次的代价」对齐:这里重来要等 2000 ms,所以粒度是这一条消息,不是整个缓存。

### 修复:一个 cell 里第 256 个之后的容器与门永远不同步 —— 判据是「写侧夹住了计数,那被夹掉的那些谁负责」

**现象**:一个放着 255 个以上容器与门的 cell,进去之后只有前 255 个会同步。第 256 个开始,门锁不上、
容器里的东西两边不一样,而且**每一次**重进这个 cell 都是同样一批被丢掉 —— 不是偶发,不是顺序问题。

**根因**:`AssignObjectsRequest` 的计数只有 8 位,写侧的处理是自洽的:

```cpp
// Code/encoding/Messages/AssignObjectsRequest.cpp(修复前 :11-14)
const auto count = std::min<size_t>(Objects.size(), kMaxObjectCount);   // :11 夹住
aWriter.WriteBits(count, 8);

for (size_t i = 0; i < count; ++i)                                      // :14 只写 count 条
    Objects[i].Serialize(aWriter);
```

包因此永远是合法的:计数与实际写出的条目数一致,读侧消费 `count` 条之后游标正好落在包尾。
但发送侧并不知道有这道夹取 —— `Code/client/Services/Generic/ObjectService.cpp` 把
`pCell->GetRefsByFormTypes({FormType::Container, FormType::Door})` 的**全部**结果推进 `request.Objects`,
然后 `:197 m_transport.Send(request);` **只发一次、不问多少条**。超过 255 条的那些既没进这个包,
也没有第二个包。

**为什么没有任何东西报错**:这是这一族里最容易被放过的一种形状。§49 修的是**写侧与读侧不一致**
(写侧只写计数低位、循环却写全部条目)⇒ 读侧少消费、剩余字节被当下一条消息解析 ⇒ **整条流失步**,
那种缺陷会以「连不上」「同步乱掉」的形式炸出来。这里写侧与读侧完全一致,流也没有失步:
丢的是**服务端从没听说过的那几个对象**,而客户端对「回包里没有这一项」的处理本来就是「不建本地映射」
(`Code/client/Services/Generic/ObjectService.cpp:200-243` 逐条 `GetGameId` + `Cast<TESObjectREFR>`,
命中才 `CreateObjectEntity`)—— 于是「服务端拒绝了它」与「这个对象根本不存在」在客户端看来是同一件事。
两边都没有一行日志。

**为什么 CI 挡不住、本地也看不见**:门禁能查的是「解析器信不信对端给的数」,而这里的数字是**写侧自己算的**,
永远自洽 —— 没有任何断言能从一条合法包上看出一部分内容根本没进包。本地也难碰上:
`docs/PITFALLS.md:9823` 自己写着任何真实 cell 的容器与门都在**几十到几千**量级,
所以 255 这个数看着绰绰有余,而「几千」那一半正是这个上限够不着的地方。

**修法**:
- 常量上移到两个消息头(`Code/encoding/Messages/AssignObjectsRequest.h:24-27`、
  `AssignObjectsResponse.h:23-26`,先例是 `Code/encoding/Structs/Tints.h:30-33`)。
  它原来是两个 `.cpp` 里的 file-scope `static`,而**发送侧现在也要按同一个数切片** ——
  一个约定只能有一个出处。
- `Code/client/Services/Generic/ObjectService.cpp:197-201`:在 `push_back` **之前**判满,
  满则先 `Send` 再 `clear`;收尾 `:206-207` 从无条件 `Send` 改成 `if (!request.Objects.empty())`,
  否则空 cell(或全被 `ShouldSyncObject` 过滤掉的 cell)会多发一个空包。
- **服务端不需要跟着改**:`response.Objects` 只在该 handler 的两处 push(命中 `Code/server/Services/ObjectService.cpp:98`、
  新建 `:123`),拒绝分支 `:100-103` 不 push ⇒ `response.Objects.size() <= acMessage.Packet.Objects.size()`。
  请求侧切片之后,响应侧永远到不了 255,`AssignObjectsResponse` 的夹取再也触发不到 ——
  一次改动同时修好两个方向,但**只改生产者的那一侧**。

**验证**:本机不编译,证据是逐字命中 + 新用例。`Code/tests/encoding.cpp:1462-1537` 新增
`TEST_CASE("An object list longer than the 8 bit count is sent in slices", "[encoding.assign_objects]")`:
先断言 `AssignObjectsRequest::kMaxObjectCount == 0xFF`,再造 258 条 `ObjectData`,
第一片 255 条往返后 `Objects.size() == kMaxObjectCount` 且 `back().Id` 正是第 255 条,
第二片 3 条往返后 `back().Id` 正是最后一条 —— 也就是「原来会被丢掉的那三条现在真的到了」。
最后把 258 条整批交给一个请求,断言写侧仍然只发 255 条且尾条是第 255 条,
把「发送侧必须切片」这个前提钉在用例里(否则以后有人删掉切片,这条断言会红)。

> **教训**:第一条:**「写侧夹住了计数」不等于「被夹掉的东西有人管」。**
> 夹取让包合法,而**合法正是它不报错的原因** —— 判据要问的是「这个上限是谁定的,超出它的那部分由谁负责」,
> 而这里定上限的是编码层、超出的部分是发送方,两边互不知情。
> 第二条:**同一个约定出现在两个文件里时,它迟早会变成两个各自为政的字面量。**
> 常量留在 `.cpp` 的 file-scope `static` 里,是因为当时只有写侧一个读者;发送侧要切片时它就必须上移到消息头。
> 第三条:**修一侧就够的时候,把「为什么另一侧不用改」的关系式写出来。**
> `response.size() <= request.size()` 这句话就是「不用对称地改两遍」的全部理由;
> 写不出这句话,就该两边都改。

### 修复:Morph 的三张表,键由对端选、键长也由对端选,而唯一的清理点是读档 —— 判据是「这个键是谁选的,而它什么时候被删掉」

**现象**:同一个对端每包换一个显示名,`plugins/MorphSyncTogether` 里三张表就每包多一行,
再也没有一行被删掉。三张表分别是 `UdpTransport::_senderConnections`(显示名 → 连接号)、
`AppearanceProbe::_snapshots`(显示名 + proxy FormID → 采样)、
`AppearanceProbe::_appearanceCaches`(显示名 → 面部材质基线)。

**根因**:三张表的键里都有对端自报的 `displayName`,而三处都没做长度判定、也都没做行数判定。
`UdpTransport.cpp` 的 `SanitizeField` 只把 `|`/CR/LF 换成 `_`、空则填 `"Player"` ——
它看起来像一道净化,净化的却是**分隔符**,不是长度。写入点则是每次收包无条件
`_senderConnections[sender] = message.sender.connectionID;`,连 `contains` 判定都没有。
`AppearanceProbe` 侧更隐蔽:两张表全文件只有两个 `operator[]` 插入点
(`AppearanceProbe.cpp` 的 `_snapshots[key]` 与 `_appearanceCaches[key]`),
其余 19 个读者全是 `find`,从不插入 —— 所以只要拦住这两个 `operator[]`,表就不会被撑大。

**为什么没有任何东西报错**:三张表都是「上一包是什么」的缓存,键配不上任何东西时本来也只返回
`nullptr`。表涨大不会让任何一次查询失败,只会让 `MorphSyncService.cpp` 每 tick 的那轮
`senderSet` 收集与 `ResolveRemoteProxyByName` 扫描变长 —— 那不是「内存慢慢涨」,
是每秒一次的 O(表大小 × 演员数)扫描,而它表现为卡顿,不表现为报错。

**为什么 CI 挡不住、本地也看不见**:清理代码是**存在**的 —— `UdpTransport::Stop()` 里有
`_senderConnections.clear()`,`AppearanceProbe::Reset()` 里有两个 `clear()`。
但 `Stop()` 的唯一调用者是 `~UdpTransport()`,`src/main.cpp` 只调 `Start()`;
`Reset()` 的唯一调用者是 `kPreLoadGame` 事件。也就是说:**进程起来之后,第一张表永远不清;
另外两张表只在读档时清。** 判据不是「有没有清理代码」,是「这行清理跑得到吗」——
而这一点,只有把调用者一路查到底才看得见。

**修法**:
- `plugins/MorphSyncTogether/src/UdpTransport.cpp`:加 `kMaxSenderNameBytes = 256` 与
  `kMaxSenderConnections = 64`,写入前先判键长,再在锁内判 `contains` / 行数;满了丢这一包并 `warn`。
- `plugins/MorphSyncTogether/src/AppearanceProbe.h`:加 `kMaxProbeKeyBytes = 256`、
  `kMaxProbeSnapshots = 256`、`kMaxAppearanceCaches = 128`。
- `plugins/MorphSyncTogether/src/AppearanceProbe.cpp`:键长门禁放在 `Probe()` 最前,
  两张表的行数上限各自贴在自己的 `operator[]` 前面。三处都是「满了丢这一次采样」,不是「清表」。
- 交付为 `Code/plugins/patches/MorphSyncTogether/0005-bound-the-peer-supplied-probe-keys-and-the-sender-name-table.patch`
  (插件是子模块,本仓不拥有也不能 push,CI 的 `git submodule update --init --force --recursive` 会丢弃只改工作树的改动)。

**验证**:本机不编译,证据是逐字命中 + 门禁 + 顺序应用。
`python Code\plugins\tools\plugin_patches.py verify` 从 24 个补丁变成 25 个并全绿;
`plugin_patches.py check` 报 `PATCH CHECK OK`;
`python Code\plugins\tools\check_transport_compat.py` 报 `TRANSPORT COMPATIBILITY OK`;
`python Tools\Scripts\check_docs.py` 报 `DOC CHECK OK`。
补丁的三个目标文件都能从 `src/AppearanceProbeBodyHair.cpp:9 #include "AppearanceProbe.cpp"`
这条链到达(`AppearanceProbe.cpp` 与 `MorphSyncService.cpp` 都不在 `CMakeLists.txt` 的 `SOURCES` 里,
它们是被 `#include` 编进去的),所以 §78 那条编译闭包门禁没有报「文件不在构建闭包里」。
按 0001 → 0002 → 0003 → 0004 → 0005 的顺序对 pin `017c7b0c` 全部应用成功。

> **教训**:第一条:**「键由对端选」要连着问「键有多长」,因为这两件事通常由同一段代码负责 —— 而它只做了其中一件。**
> 第二条:**一个只增不减的表,要看它唯一的清理点是谁在什么时候调。**
> 「有 `clear()`」不等于「`clear()` 会跑」;两张表的答案是「读档」,第三张表的答案是「析构」,而那个析构函数在进程起来之后再也不会被调用。
> 第三条:**节流不等于上限。** `Probe()` 的节流门禁查的是「这个键在不在表里」,
> 所以它对**新键**完全无效 —— 越是攻击者想要的路径,越是节流管不着的那条。

### 修复:中继的四个身份字段可以被对端抢注 —— 判据是「同一个名字,中继写在尾、对端写在头,而读取器取的是第一个」

**现象**:插件层的传输通道上,中继脚本每转发一封信封,就把服务端认证过的身份追加在**尾部**:

```lua
-- plugins/STRPluginMessagingAPI/extras/str-server-resources/strpm-chat-relay/main.lua(修复前 :180-185)
local relayEnvelope =
    message ..
    "|sender=" .. tostring(senderConnectionId) ..
    "|senderPlayerId=" .. tostring(senderPlayerId) ..
    "|senderName=" .. safeField(player:GetUsername()) ..
    "|serverTick=" .. tostring(gameServer:GetTick())
```

而 `message` 整段是**发送方自己拼的**。信封的头由对端写、尾由中继写,
同一个名字因此可以在一封信封里出现两次 —— 谁赢,取决于读的人取的是哪一个。

**根因**:两侧取的是相反的。中继脚本的字段表是**后者胜**:

```lua
-- main.lua(修复前 :30-41)
local function splitFields(envelope)
  local fields = {}
  ...
      fields[key] = value                                   -- :37,后出现的覆盖先出现的
```

而接收侧桥的读取器是**前者胜**:

```cpp
// plugins/STRPluginMessagingAPI/src/STRPluginMessagingBridgeReceive.cpp(修复前 :758-774)
std::optional<std::string_view> ReadField(std::string_view packet, std::string_view key)
{
    ...
        if (token.size() > key.size() && token.starts_with(key) && token[key.size()] == '=')
            return token.substr(key.size() + 1);              // :769,第一个匹配就返回
```

中继自己不会被骗 —— 它不读 `fields["sender"]`,它用的是 `player:GetConnectionId()` 与
`player:GetId()`(修复前 `:168-169`),取的是会话真值。被欺骗的是**接收方客户端**:
`AddFragment`(修复前 `:825-924`)的九次 `ReadField` 里有两次读的是保留字段,
`:869 pending.senderName = senderName ? std::string(*senderName) : std::string(fallbackPlayerName);`
之后一路写到

```cpp
// plugins/STRPluginMessagingAPI/src/STRPluginMessagingBridgeReceive.cpp(修复前 :936-937)
message.sender.connectionID = completed->senderId;
message.sender.displayName = completed->senderName.c_str();
```

第二处在 `src/STRPMProxyResolverBridge.h`:`ObserveIncomingEnvelope` 读 `sender`/`senderPlayerId`
后直接 `ObserveSender(*connectionID, *playerId)`(修复前 `:886`),写进 `g_connectionToPlayer`。

**为什么没有任何东西报错**:抢注出来的是**合法值**。`senderName` 是一个合法字符串,
`sender` 是一个合法连接号,信封本身仍然过 `validateEnvelope`(修复前 `:82-111`)——
它校验 `msg`/`seq`/`channel`/`payload`/`flags`/`part`/`parts`/`target`,
**从不过问字段集合**,所以多一个 `|sender=` 不是畸形。受害面是所有以 displayName 为键的表:
`plugins/MorphSyncTogether/src/UdpTransport.h:48`、`src/TngSync.h:58`、
`MorphSyncService.h:130-134`、`plugins/IEDSyncTogether/src/STRPMAdapter.h:63`、
`plugins/TradeTogether/src/StrMessagingTransport.cpp:517`;
而 `plugins/MorphSyncTogether/src/MorphSyncService.cpp:1012` 的
`if (!rawName || !EqualsInsensitive(rawName, name)) return;` 会把伪造的名字解析成
**一个真实存在的代理角色** —— 于是外观 / TNG / 交易的身份可以按名字冒充。

**为什么 CI 挡不住、本地也看不见**:`check_transport_compat.py` 当时只把中继的
`MAX_ENVELOPE_LENGTH` 与 `WireLimits.h` 的 `kMaxChatMessageBytes` 放在一起看
(上一节刚加的那条),以及两个 opcode 下标。**没有任何一条检查把中继追加的字段名
与读取它们的字段名放在一起看** —— 而这正是缺陷所在:名字是同一个,位置是两头。
每一层单独读都是对的:中继确实追加了真值,桥确实读了一个 `senderName`。

**修法**:保留字段单独按「尾部」读,其余字段的读法一个都不动。

- `src/STRPluginMessagingBridgeReceive.cpp:776-797` 与 `src/STRPMProxyResolverBridge.h:681-702`:
  新增 `ReadRelayField`,与前者的唯一区别是**记下最后一个匹配再返回**(`found = token.substr(...)`,
  循环结束才 `return found;`)。桥里只有 `sender`/`senderName` 两处换成它,另外七个字段
  (`msg`/`channel`/`seq`/`flags`/`part`/`parts`/`payload`)都是客户端写、中继不改的,仍走 `ReadField`。
- `src/STRPMProxyResolverBridge.h`:该文件的两个读取点**都**是保留字段,没有客户端字段的读者,
  所以原来的 `ReadField` 整体被 `ReadRelayField` 取代。
- `extras/str-server-resources/strpm-chat-relay/main.lua:13-17`、`:88-95`、`:183-187`:
  中继自己判一次 —— `RESERVED_FIELDS = { "sender", "senderPlayerId", "senderName", "serverTick" }`,
  信封里已经带了其中任何一个就丢弃并打印
  `[STRPM] Dropped envelope carrying reserved field <name>`。这一条挡的是**转发**,
  上面两条挡的是**读取**,两层各自独立成立。
- `docs/OFFICIAL_STR_CHAT_TUNNEL.md:86-95`(子模块内):追加字段那行补上漏写的
  `senderPlayerId`,并写明这四个名字是保留名、中继会拒收已带保留名的信封、桥从尾部读。

**没有把 `ReadField` 整体翻成后者胜**,这是这次修复里最容易做错的一步:`payload` 必须保持**前者胜**。
identity 通道(`main.lua:176-178`,修复前)用 `replacePayload(message, hexEncodeAscii(tostring(senderPlayerId)))`
覆写的是**第一个** `|payload=`(修复前 `:70-80` 的 `string.gsub(..., 1)`),中继写进去的 PlayerId
就落在第一个 `payload=` 上。整体翻转的话,客户端在尾部补一个 `|payload=` 就能顶掉它 ——
「认证过的身份」会从另一个口子漏掉。

**验证**:本机不编译(`docs/PITFALLS.md` §0/§1),所以证据是逐字命中 + 门禁 + 反向验证,不是编译产物。
`Code/plugins/tools/check_transport_compat.py` 新增一段(三部分:三个文件必须都在、
`main.lua` 必须声明 `RESERVED_FIELDS` 且**真的调用** `reservedFieldPresent(fields)` 且在追加字段之前、
两个 C++ 文件各自的 `ReadRelayField` 函数体必须含 `found = token.substr` 且不得含 `return token.substr`),
并把 `ReadRelayField(envelope, "sender")`/`"senderName"`/`"senderPlayerId"` 三个调用点写死。
反向验证过两轮:把桥的 `found = token.substr(...)` 改回 `return token.substr(...)`,
门禁立刻 exit 1 并给出「ReadRelayField returns the first match again」;
把 `local reserved = reservedFieldPresent(fields)` 改成 `local reserved = nil`,
门禁立刻 exit 1 并给出「declares RESERVED_FIELDS but never calls reservedFieldPresent」;
两次都恢复后回到 `TRANSPORT COMPATIBILITY OK`。
交付是 `Code/plugins/patches/STRPluginMessagingAPI/0003-read-the-relays-own-sender-fields-from-the-tail.patch`
(7076 字节 / 156 行 LF / 9 个 hunk / 4 个文件),按 0001 → 0002 → 0003 的顺序对 pin `62143fd` 全部应用成功。

> **教训**:第一条:**同一个名字可以在一份报文里出现两次,而「谁赢」是两个相反的实现细节。**
> 中继的 Lua 表是后者胜,接收侧桥的读取器是前者胜 —— 两个都对,合起来就是可抢注。
> 定这类字段时先问「这个字段是**谁写的**,写在哪一段」,再问「读的人取第一个还是最后一个」。
> 第二条:**「服务端认证过了」不等于「客户端读到的就是认证值」。**
> 中继确实把真值追加进去了,认证没有失效;失效的是**读取**,而读取发生在别人机器上。
> 第三条:**修一个「取第一个」的读取器时,要看同一个函数里有没有别的字段依赖「取第一个」。**
> 这里 `payload` 就依赖 —— 翻整体的方向会把 identity 通道的认证从另一个口子放走。
### 修复:一个按「一行聊天」定的上限,把插件层的传输通道静默截断了 —— 截断后的 hex 仍然是合法 hex

**现象**:`ChatMessage` 这一个线上字段同时承担两件事。对玩家它是聊天行;
对 7 个伴生插件它是**唯一的传输通道** —— 桥把 `STRPM|v2|...` 信封塞进聊天行,
服务端的中继脚本把信封读出来再转发给目标玩家。两条契约的尺寸相差 8 倍:

```lua
-- plugins/STRPluginMessagingAPI/extras/str-server-resources/strpm-chat-relay/main.lua
local MAX_ENVELOPE_LENGTH = 4096                                        -- :8
...
if string.len(message) > MAX_ENVELOPE_LENGTH then
  print("[STRPM] Dropped oversized envelope")                           -- :150-153
  return
end
```

而上一轮给这个字段加的上限是按聊天行定的:

```cpp
// Code/encoding/Messages/SendChatMessageRequest.cpp(修复前)
ChatMessage = Serialization::ReadString(aReader);
WireLimits::Clamp(ChatMessage, WireLimits::kMaxChatMessageBytes);   // :16,当时是 512
```

**根因**:夹取发生在**中继脚本之前**。服务端收包链是
`Code/server/GameServer.cpp:572-602 OnConsume` → `ClientMessageFactory::Extract`
(`Code/encoding/Messages/ClientMessageFactory.cpp:17-20` 里 `ptr->DeserializeRaw(aReader)`)→
`Code/server/Services/OverlayService.cpp:60-81 HandleChatMessage` →
`Code/server/Services/ScriptService.cpp:164-167` 的 `CallCancelableEvent("onChatMessage", ...)`,
而中继脚本正是挂在 `onChatMessage` 上的。>512 字符的信封在脚本看到它之前就已经短了一截,
`plugins/STRPluginMessagingAPI/src/STRPluginMessagingBridge.cpp:31` 的
`constexpr std::size_t kMaxEnvelopeChars = 3500;` 与这个 `512` 同时存在于仓库里,却没有任何一处对比过。

**为什么没有任何东西报错**:被切掉的是 `payload=<hex>` 的**尾巴**。信封字段顺序是
`msg|seq|channel|target|flags|part|parts|payload`,中继追加的 `sender`/`senderPlayerId`/
`senderName`/`serverTick` 在最后 ⇒ 截断后 `part`/`parts` 仍在,中继脚本
`plugins/STRPluginMessagingAPI/extras/str-server-resources/strpm-chat-relay/main.lua:89` 的
`string.match(fields["payload"], "^[0-9A-Fa-f]*$")` 对任何 hex 前缀都通过;
接收侧 `plugins/STRPluginMessagingAPI/src/STRPluginMessagingBridgeReceive.cpp:798` 的
`if ((text.size() & 1u) != 0) return std::nullopt;` 只多判一条奇偶 ⇒
偶数长度的截断 hex 被**照常解码**,消费方拿到一个更短、内容错误的载荷,而不是「这条消息被丢了」。

**为什么 CI 挡不住、本地也看不见**:三个数值住在三个互不引用的文件里(C++ 头、Lua 资源、C++ 桥),
没有任何一条检查把它们放在一起看。这不是编译能发现的问题 —— 每一层单独看都是对的。

**修法**:
- `Code/encoding/WireLimits.h:33-44`:`kMaxChatMessageBytes` 从 `512` 改成 `4096 + 512`,
  并把「这个字段同时是插件层传输通道」与「截断 hex 仍是合法 hex」写进注释。
- `Code/plugins/tools/check_transport_compat.py:153-198`:新增一段,读 `main.lua` 的
  `MAX_ENVELOPE_LENGTH` 与 `WireLimits.h` 的 `kMaxChatMessageBytes`,要求后者**严格大于**前者
  —— 中继会在信封后面追加 `|sender=|senderPlayerId=|senderName=|serverTick=`。
- `Code/tests/encoding.cpp:838-886`:新增断言,要求一个「中继上限 + 元数据」大小的信封在
  `SendChatMessageRequest` 与 `NotifyChatMessageBroadcast` 两个方向都原样往返,并显式断言它 `> 512`。
- `docs/PITFALLS.md`:§62.4 里那句 `kMaxChatMessageBytes = 512` 已按同一提交更正,新增 §79。

**验证**:本机不编译(`docs/PITFALLS.md` §0/§1),所以证据是逐字命中 + 门禁 + 反向验证,不是编译产物。
`python Code/plugins/tools/check_transport_compat.py` 全绿
(`client opcodes: 53 in use, plugin request at index 52` / `server opcodes: 56 in use, plugin notify at index 55` /
`chat opcodes  : send 38, broadcast 36`);把 `WireLimits.h` 改回 `512` 后门禁立刻 exit 1 并给出上面那段文案,
改回 `4096 + 512` 后恢复 exit 0。`python Tools/Scripts/check_docs.py` 全绿。

> **教训**:第一条:**给字段定上限时,要问的不是「这个值多大」,而是「这个字段被当成什么在用」。**
> `ChatMessage` 的名字只说了聊天,而同一个仓库里 `kMaxEnvelopeChars = 3500` 已经写着它还是传输层。
> 第二条:**截断一个 hex 字符串不会让它变成非法 —— 它会变成一个更短的合法值。**
> 「下游会校验」在这里是空的:校验器能看出的只有奇偶,看不出内容少了一半。
> 第三条:**跨文件的数值契约要进门禁,不能靠注释。**
> 注释只能提醒读到这里的人;门禁能在任何人改其中一个时立刻说话 —— 而它这次报的是本仓自己上一轮引入的回归。
### 修复:IEDSyncTogether 的一张上限加在一个从来没被编译的文件上 —— 三条门禁全绿,而那个文件不在任何二进制里

**现象**:`plugins/IEDSyncTogether` 在 pin `ad4710a6` 上的 `src/` 有 25 个 `.cpp/.h`,
而 `CMakeLists.txt:15-32` 的 `SOURCES` 只列 16 项。`src/SyncService.{h,cpp}`、`src/ProxyResolver.{h,cpp}`、
`src/Config.{h,cpp}`、`src/StrTransport.{h,cpp}`、`src/UdpTransport.{h,cpp}`、`src/StrServerDiscovery.{h,cpp}`
这 12 个文件既不在 `SOURCES` 里,也没有任何 `#include` 链从 `SOURCES` 到达 —— 它们**不在任何二进制里**。

**根因**:上一轮给这张「键由对端填」的表加上限时,补丁打的是 `src/SyncService.h` 与 `src/SyncService.cpp`。
那是 STRPM-only 运行时之前的旧架构,`ace777c16633ad4d368c5e9b5b58e7b83104dd7a`(2026-08-19,
`Build v0.6.0 STRPM-only runtime`)把它从 `SOURCES` 里删掉了 15 行,文件留在树里没删。
于是那条上限**从未进过任何二进制**,而 `git apply` 只看文本能不能对上,一路绿灯。

真正在编译的是 `src/STRPMAdapter.{h,cpp}` 与 `src/LocalIEDState.{h,cpp}`,两张表的实际位置是:

```cpp
// plugins/IEDSyncTogether/src/STRPMAdapter.h(修复前)
std::unordered_map<STRPM::ConnectionID, RemoteSnapshot> _remoteStates;   // :63,键是对端填的

// plugins/IEDSyncTogether/src/LocalIEDState.cpp(修复前)
for (const auto line : Split(payload, '\n'))                            // :80,行数由对端决定
    state.objects.emplace_back(std::move(object));                       // :140,每行追加一个
```

**为什么 CI 挡不住、本地也看不见**:`plugin_patches.py verify` 当时只判三件事 ——
补丁能不能打上、`+` 行的 fmt 占位符有没有写错、补丁是不是已经应用过。
这三件事**都只关心补丁的文本**,没有一件问过「被改的文件编不编」;
而文档里点名的补丁路径、`// plugins/…(修复后)` 标记也没有任何东西核对过,
所以旧补丁名可以删掉、标记可以继续指着一个没人再改的文件。

**修法**:补丁换靶。删掉 `0002-bound-the-remote-snapshot-table.patch`,
新增 `Code/plugins/patches/IEDSyncTogether/0002-bound-the-live-peer-table-and-the-decoded-object-list.patch`
(4 个 hunk,2735 字节),打真正被编译的四个文件:

- `src/STRPMAdapter.h:61` 加 `static constexpr std::size_t kMaxRemoteStates = 64;`,
  `src/STRPMAdapter.cpp:555` 在 `_remoteStates[senderID] = RemoteSnapshot{…}` 之前守卫,
  满表丢弃这个包并 `SKSE::log::warn("STRPM IED state RX dropped: peer table full size={} sender={}", …)`;
- `src/LocalIEDState.h:36` 加 `static constexpr std::size_t kMaxObjects = 512;`,
  `src/LocalIEDState.cpp:96` 在解 `O|` 行时判上限 —— **整包拒收而不是截断**,
  因为少几个对象的快照会被下游当成完整快照渲染。

**顺带加了两条门禁**(`Code/plugins/tools/plugin_patches.py`),都挂在 CI 已经在跑的那道 `verify` 上:

1. **构建闭包**:从该插件每份 `CMakeLists.txt` 的源 token 出发做 `#include "…"` 闭包,
   补丁改的每个源文件都必须落在这个闭包里,否则报错并直接说明「这个文件永远不会进二进制」。
   含 `GLOB` 的插件(OStimNG)整插件跳过;MorphSyncTogether 那种靠 `#include "X.cpp"` 编译的文件靠闭包到达。
2. **文档与补丁目录一致**:文档(`README.md`、`README_EN.md`、`CHANGELOG.md`、`docs/PITFALLS.md`)里
   点名的 `Code/plugins/patches/<id>/<file>.patch` 必须真实存在,
   点名的 `// plugins/<id>/<path>(修复后)` 标记必须真的是该插件某个补丁的 `--- a/` 目标。
   含 `...` 的刻意的省略写法跳过。

反向验证过:门禁**第一次实跑就报出 7 条、零误报** —— 4 条是文档点名的补丁路径已经不存在
(`README.md`、`README_EN.md`、`CHANGELOG.md`、`docs/PITFALLS.md` 各 1),
3 条是 `plugins/IEDSyncTogether/src/SyncService.{h,cpp}(修复后)` 这种标记还在、
而那个文件已经没有任何补丁会改它。这 7 处引用全部改正,
并且 §78 自己引用旧补丁名/旧标记的那三行也被自己的门禁抓出来、改成不带路径前缀的写法。

**验证**:`python Code\plugins\tools\plugin_patches.py verify` → `PATCH VERIFY OK` / `23 patch(es)`;
补丁总数仍是 23(重写不是新增)。

> **教训**:补丁能应用 ≠ 补丁编得过,也 ≠ **补丁改的文件编得过**。
> `git apply` 的绿灯只证明文本能对上;一个文件从 `SOURCES` 里被删掉之后,
> 树里那一份还在、`grep` 还找得到、补丁还能打上,唯一不再发生的事就是**它被编译**。
>
> **教训**:审计一个「热路径」之前,先问这个文件有没有被编译。
> 这一轮最初怀疑的是 `src/SyncService.cpp:302-379` 的 `RefreshProxyMitigation()`
> 每 750 ms 对每张快照做一次 ProcessLists 全表扫描 —— 结论是这个函数不在任何发布产物里,
> 该候选因此结案,不是「不值得修」,而是**修了也没有任何效果**。
>
> **教训**:一条判据只有在**它能自己跑**的时候才算判据。
> 「这个补丁改的文件在不在构建闭包里」是可以用 CMakeLists 的源列表 + `#include` 闭包机械判定的,
> 所以它进了门禁;而「这条上限到底有没有生效」只能靠编译器,本地没有工具链时,
> 唯一能守住的底线就是**别让补丁打到编译不到的地方**。
### 修复:两张键由对端选、长度也由对端选的表 —— TradeTogether 的对端名字表与请求 id 历史表

**现象**:TradeTogether 的 `StrMessagingTransport` 把「对端名字 → 连接号」写进
`_namedConnections`,而那个名字是**包内 `from` 字段** hex 解码后的字符串:
`Protocol::HexDecode`(`plugins/TradeTogether/src/Protocol.cpp:37-54`)只判长度是偶数、每个字符是十六进制,
**没有任何长度上限**,它拿到的字段又来自上限为 `24 * 1024` 的载荷 ⇒ 单个键最长约 12 KiB。
同一插件里 `TradeState::recentIncoming` 的键是包内 `request` 字段,而**同一次解析里 `from`/`to` 是有 256 上限的**
(`Trade.cpp:1061`),`requestID` 却只判了「非空」。

**根因**:两张表的键都由对端选,而**没有任何一处问过键有多长、表有多少行**:

```cpp
// plugins/TradeTogether/src/StrMessagingTransport.cpp(修复前)
std::optional<std::string> peerName;
if (const auto encodedFrom = Protocol::ReadField(packet, "from"))
{
    peerName = Protocol::HexDecode(*encodedFrom);       // :514-515,长度只受 24 KiB 载荷约束
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

```cpp
// plugins/TradeTogether/src/Trade.cpp(修复前)
if (state.recentIncoming.contains(a_requestID))
{
    return;
}
state.recentIncoming.emplace(a_requestID, std::chrono::steady_clock::now());   // :749-751
```

判据是**回收路径**:`_namedConnections` 只有三处清空(`kCleared` 分支 `:184`、`Start()` `:317`、
`Stop()` `:402`),其余只有一处 `std::erase_if(… e.second == a_event->connectionID)`(`:194`)——
**按连接号删,不按名字删**,所以换一个 `from` 就是一个永远删不掉的键;`recentIncoming` 只有
`Update()` 里的按龄清扫(`:976-983`,`historyLifetime = std::max(requestTimeout * 2, 60000ms)`),
而 `HandleIncomingRequest` 的插入发生在**「本机是否正忙」这道判断之前**(`:754-766` 先插入、`:768` 才判忙)
⇒ 被自动拒绝的请求同样占一行,60 秒窗口内的行数完全由对端决定。

这张表的代价落在**游戏线程**:`ResolveConnectionIDForPlayerName`(`:233`)先持 `_proxyMutex`
线性扫 `_namedConnections` 做大小写不敏感比较(`:241`),而它的唯一调用点是 `SendTo`(`:420`);
`Update()` 的按龄清扫则挂在每帧输入事件(`Input.cpp:52`)与每个网络包(`Trade.cpp:976`/`:998`/`:1047`)上。

**判据可机械执行**:同族的修法已经在仓里 —— `IEDSyncTogether/0002` 的 `kMaxRemoteStates = 64`
与 `kMaxObjects = 512`、
`MorphSyncTogether/0004` 的三张 64、`OStimTogether/0007` 的 `kMaxResolvedEntries`/`kMaxRemoteNames = 512`、
`0008` 的 `kMaxRemotePreps = 32`/`kMaxReadyTokens = 256`,全部是「先判 `contains` 再判上限,
已有的键永远仍可更新,被拒的只是新键」。这一批缺的只有这两张。

**修法**:

- `plugins/TradeTogether/src/StrMessagingTransport.cpp:14-20` 加两个文件内常量
  `constexpr std::size_t kMaxPeerNameBytes = 256;` 与 `constexpr std::size_t kMaxNamedConnections = 64;`。
  256 与 Trade 层自己的 `Trade.cpp:1061` 上限**取同一个值**:更长的名字在那边必然被丢掉,
  在这边也永远匹配不上任何一次 `ResolveConnectionIDForPlayerName`,所以拒绝它不丢信息。
- `:520-538` 收包守卫加上 `peerName->size() <= kMaxPeerNameBytes`,写入改成
  `if (_namedConnections.contains(*peerName) || _namedConnections.size() < kMaxNamedConnections)`
  才赋值,否则打一条 `spdlog::warn("TradeTogether STRPM peer name table is full: size={} nameBytes={}", …)`。
- `:261-265` 代理解析那条写入路径同样夹住(`ResolveConnectionIDForPlayerName` 一次会写两个键),
  否则「表满」只能靠收包路径挡住一半。
- `plugins/TradeTogether/src/Trade.cpp:21-27` 加 `constexpr std::size_t kMaxRequestIDBytes = 64;`
  与 `constexpr std::size_t kMaxRecentIncoming = 256;`。64 是本地 `MakeRequestID()` 产出的 32 字符
  (`:100-108`,`fmt::format("{:08X}{:016X}{:08X}", …)`)的两倍。
- `:757-763` 在 `contains` 之后、插入之前加满表早退(满表丢弃并打一条 warn);
  `:1068-1070` 的校验加上 `requestID->size() > kMaxRequestIDBytes`。
- 交付:`plugins/TradeTogether` 是子模块,而 CI 的
  `git submodule update --init --force --recursive --depth=1` 会丢弃只改工作树的改动,
  所以修复以 `Code/plugins/patches/TradeTogether/0002-bound-the-peer-name-and-request-id-tables.patch`
  交付(`0001` 只改 `src/main.cpp`,所以这一份的基线就是「`0001` 已应用」)。

**验证**:`python Code\plugins\tools\plugin_patches.py verify` → `PATCH VERIFY OK` / `23 patch(es)`;
`check` → `PATCH CHECK OK`;`plugin_snapshot.py verify` → `SNAPSHOT OK` / `8 plugin(s) have a durable
copy matching the pinned commit`;`Tools\Scripts\check_docs.py` → `DOC CHECK OK`;
`check_transport_compat.py` → `TRANSPORT COMPATIBILITY OK`(**这一批没有动线格式**)。
补丁的两个方向都实测过:`git apply --check --cached`(对 index = pin)exit 0,
`git apply --check --reverse`(对工作树)exit 0,而不带这两个开关的 `git apply --check` 对工作树
必然失败(`error: patch failed: src/StrMessagingTransport.cpp:11`)—— 因为改动已经在里面。
两处修改都不新增 include(`plugins/TradeTogether/src/PCH.h:16-34` 已有
`<chrono> <cstdint> <string> <unordered_map>`)。本机不编译(仓库约束),所以证据是逐字命中 + 门禁,
不是编译产物。

**教训**:同一次解析里两个字段一个有上界一个没有,这就是判据 —— `from`/`to` 早就有 256 上限,
而同一个包里的 `request` 只有「非空」。

**教训**:上限要加在**写入它的那一层**,不是消费它的那一层 —— Trade 层的 256 校验发生在
transport 已经把键写进 `_namedConnections` 之后,「下游会挡住」挡住的只是下游的使用,不是上游的增长。

**教训**:「有回收路径」不等于「有上界」—— `recentIncoming` 有按龄清扫,看起来是自愈的,
但 60 秒窗口里的行数完全由对端决定,而清扫本身每帧都要遍历整张表;
回收路径的**周期**决定了窗口内能堆多高。
### 修复:对象实体表只增不减 —— 一张行数由客户端决定、却只由「房间空掉」回收的表

**现象**:服务端 `ObjectService` 的实体数由客户端发来的 `AssignObjectsRequest` 决定:
每个包里最多 255 个对象,而**包没有速率限制**、实体总数**没有上限**。每个新建实体都带一份
对端给的 `Inventory`(≤ 4096 条 × 每条 ≤ 64 个附魔效果),而全仓唯一销毁它的地方是
`OnPlayerLeaveCellEvent` —— 也就是说**只要有一个玩家待在这个 cell 里,它里面的对象就永远活着**。
一个客户端反复发同一个包(哪怕每次只改一个 `Id`)就能让实体表一直涨下去。

**根因**:这条路径上没有任何一处问过「现在有多少」:

```cpp
// Code/server/Services/ObjectService.cpp(修复前)
else
{
    const auto cEntity = m_world.create();          // :96,没有计数、没有上限
    m_world.emplace<FormIdComponent>(cEntity, object.Id);
    auto& objectComponent = m_world.emplace<ObjectComponent>(cEntity, acMessage.pPlayer);
    ...
}
```

判据是**同一个系统里的另一条容量路径早就有上限**:`GameServer.cpp:877` 在
`GetPlayerManager().Count() >= uMaxPlayerCount.value_as<uint32_t>()` 时直接拒绝入服
(默认 8,`GameServer.cpp:29`),而同族的每一张对端可控表(插件层的 `kMaxRemoteSnapshots`、
`kMaxRemotePreps`、`kMaxReadyTokens`)都已经有上限 —— 缺席的只有这张实体表。

**为什么不在服务端校验 `object.CellId`**:看起来「对端声称的对象所在 cell」应该等于发送者
当前的 cell,但**合法流量下这两个值也会不一致**:`Code/client/Services/Generic/DiscoveryService.cpp`
只有 `gameCenterGrid` 变化才发 `ShiftGridCellRequest`,而 `gameCurrentGrid` 单独变化时只发
`CellChangeEvent`;并且 `Code/client/World.cpp` 里 `ObjectService`(`:47`)的 `CellChangeEvent`
sink 注册早于 `PlayerService`(`:55`)⇒ 客户端**总是先发** `AssignObjectsRequest`、后更新自己的
`CellIdComponent`。严格相等会打断正常同步,所以这条不做。

**同一文件里的第二个缺陷**是只写不读的字段:

```cpp
// Code/server/Components/ObjectComponent.h(修复前)
struct ObjectComponent
{
    ObjectComponent(Player* apLastSender) : pLastSender(apLastSender) {}
    Player* pLastSender;        // 全仓只有这三行:构造参数、初始化、字段本身
    LockData CurrentLockData{};
};
```

`git grep -rn pLastSender -- Code` 只有 3 处命中、**没有任何读者**;而 `Player` 由
`GameServer.cpp:610-670 OnDisconnection` 里的 `m_pWorld->GetPlayerManager().Remove(pPlayer)`
释放,对象实体却要活到 cell 空掉为止 —— 于是这个指针在玩家掉线后必然悬垂。判据是
**「没有人读的字段」比「加一处清理」更安全的修法是删掉它**,不是给它补一个失效通知。

**修法**:

- `Code/server/Services/ObjectService.h:37` 加类内常量(与 `PluginMessagingService.h:44-47`
  的 `static constexpr std::uint32_t kMax…` 同形),`:29-34` 写清它为什么存在:
  `static constexpr std::uint32_t kMaxObjectEntities = 64 * 1024;`
  (4 字节/实体 ⇒ 约 256 KiB,远大于任何真实 cell 的容器与门;`Pch.h` 无 `<algorithm>`,
  所以这里补了 `#include <cstdint>`。)
- `Code/server/Services/ObjectService.cpp:64-68` 在 `view` 建立之后数一遍现有实体
  (`uint32_t objectCount` / `for (auto it = std::begin(view); …) ++objectCount;`),
  `:100-107` 未命中分支改成 `else if (objectCount >= kMaxObjectEntities) { ++refusedCount; }`,
  只有能建的那一支才 `++objectCount;` —— **已有的对象永远仍能命中查询分支**,
  被拒的只是新实体。
- `:127-129` 饱和时打一条 `spdlog::warn("Object table is full ({} objects), {} of {} requested
  objects from player {} were refused", …)`。被拒的实体不进 `response`,客户端那一侧
  (`Code/client/Services/Generic/ObjectService.cpp:200-243`)对「回包里没有这一项」的处理
  本来就是「不建本地映射」,所以是**少同步一个对象,而不是记错一个对象**。
- `Code/server/Components/ObjectComponent.h` 删掉带参构造与 `pLastSender`(保留
  `#include <Game/Player.h>`:它是组件头里唯一 include 它的地方,别的 TU 靠它传递拿完整 `Player`);
  `Code/server/Services/ObjectService.cpp:111` 随之改成 `m_world.emplace<ObjectComponent>(cEntity);`。

**顺带补掉同一批浮点读取里最后 6 处**:`Code/encoding/Messages/RequestPlayerHealthUpdate.cpp:14`、
`NotifyPlayerHealthUpdate.cpp:16`、`Code/encoding/Structs/Inventory.cpp:19`(效果强度 `Magnitude`)、
`:22`(`RawCost`)、`:63`(`ExtraCharge`)、`:76`(`ExtraHealth`),以及
`Code/encoding/Structs/AnimationVariables.cpp:149`(动画变量浮点)。判据是**这些值接下来决定了什么**:
`ExtraCharge` 经 `Code/client/Games/Skyrim/TESObjectREFR.cpp:611` 的 `if (arEntry.ExtraCharge > 0.f)`
进 `SetChargeData`、`ExtraHealth` 经 `:657` 同形的门进 `SetHealth` —— **`+inf` 比 `0.f` 大,所以它过得去**;
`Magnitude`/`RawCost` 经 `Code/client/Games/Skyrim/Forms/EnchantmentItem.cpp:24-27` 直接写进引擎的
`EffectItem`;血量百分比在 `Code/client/Services/Generic/OverlayService.cpp:543` 只做
`>= 0.f ? … : 0.f` 的三元,**同样是 `+inf` 过得去**;而 `AnimationVariables` 的浮点由对端填的
位图决定读不读(`:143-149`)。六处统一改成 `WireLimits::ClampFinite(Serialization::ReadFloat(aReader))`
(`Code/encoding/WireLimits.h:74` 已有),四个文件各补 `#include <WireLimits.h>`(写在第 2 行,
与 `Code/encoding/Messages/AddTargetRequest.cpp:1-2` 同形)。

**验证**:`Code/tests/encoding.cpp` 的 `[encoding.finite_floats]` 新增五个块,接收对象一律先写有限哨兵
`1.f`,让「读的时候替换了」成为断言成立的唯一原因:`Inventory::Entry` 的 `ExtraCharge = +inf` /
`ExtraHealth = NaN` 读回 `0.f`(`:1222-1223`)、`Inventory::EffectItem` 的 `Magnitude = +inf` /
`RawCost = -inf` 读回 `0.f`(`:1244-1245`)、`RequestPlayerHealthUpdate` 与 `NotifyPlayerHealthUpdate`
各做一次整包往返(`:1258`/`:1271`,后者同时断言 `PlayerId == 21` 证明字段顺序没被改动)、
`AnimationVariables` 用 `GenerateDiff`/`ApplyDiff` 走位图路径断言 `{0, 0, 0, 2.5}`(`:1295-1300`,
2.5 那一条证明**有限的合法值原样通过**)。`git grep -rn "ClampFinite" -- Code/encoding` 现在 28 处命中、
`ClampFinite` 定义本身在 `WireLimits.h:74`;`git grep -rn "pLastSender" -- Code` **exit 1 / 零命中**;
`python Tools\Scripts\check_docs.py` → `DOC CHECK OK`;
`python Code\plugins\tools\plugin_patches.py verify` → `PATCH VERIFY OK` / `22 patch(es)`;
`check` → `PATCH CHECK OK`;`plugin_snapshot.py verify` → `SNAPSHOT OK`;
`check_transport_compat.py` → `TRANSPORT COMPATIBILITY OK`(没有新 opcode,`Code/encoding` 的线格式不变)。
本机不编译(仓库约束),所以证据是逐字命中与门禁,不是编译产物。

**教训**:「这个字段合法吗」和「这个字段能有多少个」是两个问题 —— `object.Id` 的取值空间是 2^64、
`AssignObjectsRequest` 也确实是合法包,而表的**行数**没人管。

**教训**:回收路径决定了上限的必要性 —— 只在「房间空了」时回收,就等于「只要有人待着就一直涨」;
先问「这张表什么时候变小」,再问「它能变多大」。

**教训**:只写不读的字段是负债而不是接口 —— 它不会因为「反正没人用」而安全,只会等到有人用它时
才发现它已经悬垂了;删掉它比给它补清理便宜。
### 修复:任意客户端一个畸形包即可打崩专用服务器,并修掉一整类「线上计数与实体数不一致」

**现象**:专用服务器上,一个**还没通过认证**的连接发一个畸形的
`AssignCharacterRequest`,服务端进程直接终止,没有崩溃转储、没有日志。

**根因**:`Factions::Deserialize`(`Code/encoding/Structs/Factions.cpp`)在阵营
数量超过 511 时**抛异常**:

```cpp
// Code/encoding/Structs/Factions.cpp(修复前)
auto npcCount = Serialization::ReadVarInt(aReader);
if (npcCount > 0x1FF)
    throw std::runtime_error("Too many NPC factions received !");
```

而这条调用链**整条都是 `noexcept`**:`AssignCharacterRequest::DeserializeRaw`
(`Code/encoding/Messages/Message.h:24`)是 `noexcept` 虚函数,
`CharacterData::Deserialize`、`CharacterSpawnRequest::DeserializeRaw` 同样如此;
`CODE_GUIDELINES.md:47` 也明文写着 "Don't use exceptions"。于是异常从 `noexcept`
帧里逃出 ⇒ `std::terminate` ⇒ **进程直接死**,连崩溃处理都没机会跑。
判据是**解析发生在认证之前**:`GameServer::OnConsume`(`Code/server/GameServer.cpp:592`)
先 `factory.Extract(reader)` 再 `:600 m_messageHandlers[...]`,而
`GetByConnectionId` 与 `Kick` 都在 handler 内部 —— 所以**未认证的连接就能触发**。

同一族还有第二副面孔,而且**合法流量**就能触发:计数写在线上,但写侧只写了
计数的低位、循环却写了全部条目,读侧于是只消费 `count` 条,剩下的字节被当成
下一条消息解析 —— **整条流从此失步**。最典型的是 `Mods`:

```cpp
// Code/encoding/Structs/Mods.cpp(修复前)
const uint16_t modCount = std::min(ModList.size(), size_t(4096)) & 0xFFFF;
aWriter.WriteBits(modCount, 13);
for (auto& entry : ModList)   // ← 写的是全部条目,不是 modCount 条
```

`Tints` 更直接:8 位计数**没有 clamp**,超过 255 个条目即静默截断失步。
第三副面孔是**复用对象上的累积**:`insert()` / `push_back()` 都不清空,
消息对象复用一次就把上一包的内容再应用一遍。

**判据是仓库自己的既有约定**——同族缺陷里早就有两处正确写法:

| 位置 | 写法 | 上界来自 |
| --- | --- | --- |
| `NotifyFactionsChanges.cpp:27-28` | `if (count >= kMaxChangeCount) return;` | 早退,不抛异常 |
| `PluginMessagingRequest.cpp:40-42` | `std::min<uint32_t>(size, kMaxPayloadBytes)` | 注释:"Never trust the wire length: a peer can claim more bytes than it sent." |
| `Factions` / `Mods` / `Tints` / … | 无上界,或计数与循环不一致 | ❌ **本次缺陷** |

判据可机械执行:**「`WriteBits(x.size() & MASK, N)` 后面跟 `for (auto& e : x)`」
= 计数与实体数必然不一致**;以及**「`ReadVarInt` 出来的计数直接进
`resize`/`assign`/循环」= 无上界**。

**修复**:38 个 `Code/encoding/**` 文件,统一成两句话 ——
**读侧读完计数立刻 `std::min` 夹紧(或早退),绝不抛异常;写侧写几条就只循环几条。**

```cpp
// Code/encoding/Structs/Factions.cpp(修复后)
const auto npcCount = std::min<uint64_t>(Serialization::ReadVarInt(aReader), kMaxFactionCount);
NpcFactions.resize(npcCount);
```

```cpp
// Code/encoding/Structs/Mods.cpp(修复后)
for (size_t i = 0; i < modCount; ++i)   // ← 与 13 位计数一致
```

新增的上界常量全部有出处,不是随手取的数:`kMaxActorValueCount = 164` 就是游戏的
`kActorValueCount`;`kMaxFactionCount = 0x1FF`、`kMaxObjectCount = 0xFF`、
`kMaxTintCount = 0xFF`、`kMaxEntryCount = 0xFFFF` 都是**线上计数本身的位宽**;
`kMaxEntryCount = 0x1000` / `kMaxEffectCount = 0x40` / `kMaxVariableCount = 0x1000`
是 VarInt 计数的宽上界,给真实数据留足余量(玩家背包、附魔效果、行为图变量
都在几百以内)。

**同时修掉三处同族缺陷**:`AnimationVariables::String_to_VectorBool`
(`Code/encoding/Structs/AnimationVariables.cpp:33-42`)的位表外层循环**不检查
`chars` 边界**,字符串偏短就**读越界**;`Inventory::Entry::Deserialize` 与
`Inventory::Deserialize` 的 `push_back` 不清空;`MagicEquipment::Deserialize`
在三个「这个手有没有法术」的 flag 全为假时**保留上一包的法术**,等于把已经
收起来的法术又装回去。

**验证**:全 `Code/encoding` 再查一遍 `WriteBits(...size()...)` 与
`WriteVarInt(aWriter, ...size())`,**零命中**;34 个 `ReadVarInt` 读点逐个核对,
计数语义的全部有上界,非计数的(Id / Tick / DiscordId / MessageType)本就不需要;
`git diff --shortstat` = `39 files changed, 396 insertions(+), 120 deletions(-)`;
`python Tools/Scripts/check_docs.py` 全绿。

### 修复:断开连接时,「还没有角色的玩家」会销毁别人的角色

**现象**:玩家在**还没建好角色**的时候断开连接(连上后被踢、角色创建失败、或角色已被
删除后掉线),服务端会把**另一个玩家**的角色实体销毁掉 —— 那个玩家会看到自己的角色
凭空消失,或客户端开始报找不到实体。

**根因**:`GameServer::OnDisconnection` 用 `0` 冒充「没有角色」:

```cpp
// Code/server/GameServer.cpp(修复前)
entt::entity playerCharacter = pPlayer->GetCharacter().value_or(static_cast<entt::entity>(0));
...
if (entity == playerCharacter)      // ← 没有角色时,这里等于 entity == 0
```

`entt::entity` 的 `0` **不是空值**。EnTT 的空值是「entity 位全 1」
(`skypjack/entt v3.10.0` 的 `src/entt/entity/entity.hpp`:`entity_mask = 0xFFFFF`、
`reserved = entity_mask | (version_mask << entity_shift)`),而 `0` 恰恰是
`registry::create()` 交出的**第一个实体** —— 正常情况下就是第一个玩家的角色。
于是「没有角色」的玩家断开时 `entity == 0` 命中该实体,走的是
`CharacterRemoveEvent` 而不是 `OwnershipTransferEvent`,**别人的角色被直接销毁**。

**判据是仓库自己的既有约定**:同一个函数里「有角色 / 没角色」分得很清楚,
只有这一处把 `optional` 拍平成了整数:

| 位置 | 写法 | 判定 |
| --- | --- | --- |
| `Code/server/Services/OverlayService.cpp:75` | `if (const auto character = acMessage.pPlayer->GetCharacter())` | ✅ |
| `Code/server/Services/PlayerService.cpp:157` | `if (!pPlayer->GetCharacter()) return;` | ✅ |
| `Code/server/Services/CommandService.cpp:100` | `auto character = pTargetPlayer->GetCharacter(); if (character)` | ✅ |
| `Code/server/GameServer.cpp:636` | `GetCharacter().value_or(static_cast<entt::entity>(0))` | ❌ 本次缺陷 |

可机械执行的判据:**`optional<entt::entity>` 绝不能 `value_or(0)`** ——
EnTT 里没有「0 号空实体」这个概念。全仓 `value_or(` 只有这一处。

**修复**:保留 `optional`,在比较处判空:

```cpp
// Code/server/GameServer.cpp(修复后)
const auto playerCharacter = pPlayer->GetCharacter();
...
if (playerCharacter && entity == *playerCharacter)
```

**修崩溃,不改行为**:有角色的玩家断开时走的路径一条没变。

**验证**:`Select-String 'value_or\('` 全仓**零命中**;
`static_cast<entt::entity>(0)` / `entt::entity{0}` / `entt::null` 同样**零命中**;
`git diff --shortstat -- Code` = `2 files changed, 6 insertions(+), 4 deletions(-)`;
`python Tools/Scripts/check_docs.py` 全绿。

### 修复:游戏读盘时解引用空 cell —— 一整个「`GetParentCellEx()` 允许返回空」的家族

**现象**:玩家在读盘(或换 cell)的窗口里点「连接」,握手正好在这个窗口完成,
客户端进程直接终止。同一个窗口里只要有 `MoveTo` 走到,也一样崩。

**根因**:`TESObjectREFR::MoveTo`(`Code/client/Games/Skyrim/TESObjectREFR.cpp:385-400`)
把 cell 直接读穿:

```cpp
// Code/client/Games/Skyrim/TESObjectREFR.cpp(修复前)
void TESObjectREFR::MoveTo(TESObjectCELL* apCell, const NiPoint3& acPosition) const noexcept
{
    ...
    TiltedPhoques::ThisCall(s_internalMoveTo, this, GetNullHandle(), apCell, apCell->worldspace, ...);
}
```

`apCell->worldspace` 没有判空,所以传进来的 cell 为空就是空指针解引用。
这是**单点缺陷、九个调用点共享** —— 修 `MoveTo` 一处,九个调用点全部受益。

同一族还有四处直接写 `parentCell->formID`,而 `parentCell` 在同一个窗口里同样是空的:
`TransportService.cpp:307`(`OnConnected`,玩家点连接时)、`ObjectService.cpp:127`(`OnCellChange`)、
`CharacterService.cpp:1359`(`RequestServerAssignment`,同一表达式解引用两次)、
以及两个调试视图的「Teleport to me」按钮。

**判据是仓库自己的既有约定**:`TESObjectREFR.h:160-162` 的注释就写着
"The actual parent cell is the field below at offset 0x60; use `GetParentCellEx()` when the
effective cell is needed.",而 `GetParentCellEx()`(`TESObjectREFR.cpp:500-503`)是

```cpp
return parentCell ? parentCell : GetSaveParentCell();
```

—— **回退的虚函数本身可以返回空**。`PlayerCharacter.cpp:111` 用它之后仍在 `:117` 判空,
`docs/PITFALLS.md` 的 §22(`:1559-1591`)也已经写下这条法条:
"`PlayerCharacter::Get() != nullptr` **不蕴含** `GetParentCellEx() != nullptr`"。

| 位置 | 写法 | 判定 |
| --- | --- | --- |
| `CharacterService.cpp:1285-1291` | 取 cell → 判空 → 打日志 `return` | ✅ |
| `CommandService.cpp:58-77` / `OverlayService.cpp:520-539` | 取 cell → 判空 → 提示 `return` | ✅ |
| `CharacterService.cpp:1739` | `if (!pCell \|\| !pCell->IsAttached())` | ✅ |
| `AnimationSystem.cpp:118` | `if (const auto pCell = pActor->parentCell)` | ✅ |
| `TESObjectREFR::MoveTo` | `apCell->worldspace`,无判空 | ❌ **本次缺陷** |
| `TransportService` / `ObjectService` / `CharacterService` | `parentCell->formID`,无判空 | ❌ 同族 |

可机械执行的判据:**`parentCell->` 直接解引用 = 缺陷**;取 cell 必须走
`GetParentCellEx()` 且结果判空。

**修复**:6 个文件。`MoveTo` 开头加单点守卫:

```cpp
// Code/client/Games/Skyrim/TESObjectREFR.cpp(修复后)
    // The cell is read through below, and GetParentCellEx() is allowed to come
    // back empty while the game is between cells.
    if (!apCell)
        return;
```

四处 `parentCell->formID` 改走 `GetParentCellEx()` 并判空(空时保持默认 `CellId`,
服务端把它当普通 cell 值,由首次 cell-change 自愈);三个 `MoveTo` 调用点改传
`GetParentCellEx()`;两个调试视图用 `if (pCell)` 而不是提前 `return` ——
后者会把当前帧剩下的界面元素整段跳过。

**修崩溃,不改行为**:`parentCell` 非空时 `GetParentCellEx()` 就是 `parentCell` 本身,
正常路径一个字节都没变;为空时原本是崩溃。

**验证**:`Get-ChildItem -Path Code\client -Recurse -Include *.cpp,*.h | Select-String 'parentCell->|parentCell\.'`
剩余 11 处**全部已有守卫**;`git diff --stat -- Code` = `6 files changed, 31 insertions(+), 9 deletions(-)`;
`python Tools/Scripts/check_docs.py` 全绿。

### 修复:主菜单与读盘期间解引用「还没有玩家」 —— 一整个 `PlayerCharacter::Get()` 允许返回空的家族

**现象**:游戏刚启动停在主菜单,或者正在读一个存档,此时点「连接」;又或者只是等一个
断线包、一条经验同步包、一次传送响应到达 —— 客户端进程直接终止。

**根因**:`PlayerCharacter::Get()` 在读盘与主菜单期间返回空,而 18 处调用点直接写
`PlayerCharacter::Get()->...`。判据不是本次新立的,是仓库自己早就写在两处的:

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

`Code/client/Games/Skyrim/PlayerCharacter.cpp:44-49` 的实现同样说明它可空:

```cpp
PlayerCharacter* PlayerCharacter::Get() noexcept
{
    POINTER_SKYRIMSE(PlayerCharacter*, s_character, 401069);
    return *s_character.Get();
}
```

| 位置 | 写法 | 判空 |
| --- | --- | --- |
| `OverlayService.cpp:615` | `CalculateHealthPercentage(PlayerCharacter::Get())` | ✅(函数内判) |
| `PlayerService.cpp:218` | `PlayerCharacter* pPlayer = PlayerCharacter::Get(); if (!pPlayer) return;` | ✅ |
| `PlayerService.cpp:309`/`:326`/`:359` | `if (auto* pPlayer = PlayerCharacter::Get())` | ✅ |
| `PlayerService.cpp:86` | `PlayerCharacter::Get()->SetDifficulty(m_previousDifficulty);` | 否(本次缺陷) |
| `CharacterService.cpp:602` | `pActor->MoveTo(PlayerCharacter::Get()->GetParentCellEx(), ...);` | 否(本次缺陷) |
| `TransportService.cpp:275` | `PlayerCharacter* pPlayer = PlayerCharacter::Get();` 后 `:280` 就解引用 | 否(本次缺陷) |
| `Forms.cpp:55` | `Cast<TESNPC>(PlayerCharacter::Get()->baseForm)` | 否(本次缺陷) |
| `References.cpp:60` | `s_getDifficultyMultiplier(PlayerCharacter::Get()->difficulty, ...)` | 否(本次缺陷) |
| `TESObjectREFR.cpp:919` | `PlayerCharacter* pPlayer = PlayerCharacter::Get();` 后 `pPlayer->IsItemInInventory(...)` | 否(本次缺陷) |

**可机械执行的判据**:`PlayerCharacter::Get()->` 出现一次,就是一处缺陷 ——
表达式里根本没有放判空的位置。判空必须落在一个局部量上,写成
`if (PlayerCharacter* pPlayer = PlayerCharacter::Get())`。

**修复**:9 个文件、18 处,一律「取一次到局部量 + 判空」,不改调用顺序、不改原有语义:

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
    ...
    auto* pNpc = pPlayer ? Cast<TESNPC>(pPlayer->baseForm) : nullptr;
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

其余七处同构:`Forms.cpp`(`TESNPC::Initialize` 的六个默认值改到 `if (pPlayerBaseForm)` 内)、
`TESObjectREFR.cpp`(`pPlayer && !pPlayer->IsItemInInventory(...)`)、
`PlayerService.cpp`(五处)、`CharacterService.cpp`(八处)、`MapService.cpp`(两处)、
`CommandService.cpp`/`OverlayService.cpp`(传送各一处)。

**修崩溃,不改行为**:指针非空时走的还是原来那条路;为空时原本是崩溃。传送的两处
仍然先取 cell 并保留原来的失败提示(`"Teleporting to player failed."`),只是不再对着
空玩家解引用。

**验证**:`Get-ChildItem -Path Code\client -Recurse -Include *.cpp,*.h | Select-String 'PlayerCharacter::Get\(\)->'`
→ **零命中**(修前 18 处);`git diff --stat -- Code` = `9 files changed, 98 insertions(+), 34 deletions(-)`;
`python Tools/Scripts/check_docs.py` 全绿。

### 修复:字符串缓存超过 65535 条时,后加入的玩家会永久卡在「收到一半的缓存」

**现象**:服务器开得久了(或装了大量会往字符串缓存里塞名字的 mod),新加入的玩家
**部分文字显示不出来** —— 字幕、对话、动画事件名是空的,而老玩家一切正常。重连没用,
换一台机器也没用;只有服务器重启才会好一阵子。

**根因**:字符串缓存是**增量同步**的,每个玩家记一个「我已经收到第几条」的游标
(`Player::GetStringCacheId()`)。发送侧按游标把剩下的条目切成一条消息发出去。
一个 `StringCacheUpdate` 消息的计数是 **16 位**(`Code/encoding/Messages/StringCacheUpdate.cpp:26`
读回来是 `& 0xFFFF`),所以**一条消息最多带 65535 条**。而发送侧原本是:

```cpp
// Code/encoding/StringCache.cpp(修复前)
    update.Values.insert(std::begin(update.Values), itor, std::end(m_idToString));

    aStartId = m_idToString.size();   // ← 不管写出去几条,游标都推到末尾
```

写出去的条目被 16 位计数截断,而**游标仍然推到了缓存末尾**。接收侧只收到前 65535 条,
它的本地缓存长度就停在那里;下一次增量同步时,服务端按**已经推到末尾的游标**认为
「这个人已经收全了」,于是**一条都不再发** —— 两边的 id 从此永久错位,而且**再也不会自愈**。

**判据**:仓库自己早就写下了「计数写在线上、写几条就只循环几条」这条规则
(v1.1.9 前面几节刚统一过一整族),`StringCacheUpdate.cpp` 的 16 位掩码正是这个家族
的另一副面孔 —— **只是这次错的是「游标」而不是「循环」**:写出去的条数与游标推进的
条数必须相等,否则接收侧永远追不上。

**修复**:发送侧按 16 位上界切一片出去,并且**只把游标推进真正写出去的那几条**;
剩下的留给下一次增量同步。接收侧的 `assert(aMessage.StartId == m_idToString.size())`
也因此重新成立。

```cpp
// Code/encoding/StringCache.cpp(修复后)
        const auto count = std::min<size_t>(m_idToString.size() - aStartId, StringCacheUpdate::kMaxValueCount);

        auto itor = std::begin(m_idToString);
        std::advance(itor, aStartId);

        auto end = itor;
        std::advance(end, count);

        update.Values.insert(std::begin(update.Values), itor, end);

        aStartId += static_cast<uint32_t>(count);
```

上界常量 `StringCacheUpdate::kMaxValueCount = 0xFFFF` 放在消息头里
(`Code/encoding/Messages/StringCacheUpdate.h`),因为**它本来就是线上计数的位宽**,
不是随手取的数。

**同一处还有第二层截断,修完游标才会露出来**:`StringCacheUpdate::StartId` 字段本身声明成
`uint16_t`,而游标是 `uint32_t`(`StringCache::Serialize(uint32_t& aStartId)`)——
`update.StartId = aStartId;` 是一次**隐式窄化**。缓存超过 65535 条之后,发出去的 `StartId`
已经不是真实游标了,接收侧那条连续性判据**永远不可能成立**:debug 构建下每收一条就炸,
release 构建下断言被编译掉,表现为 id 静默错位。只修游标不修字段,等于把「永久卡住」
换成「每一条都错位」。现把字段放宽成 `uint32_t`,读侧掩码同步改成 `& 0xFFFFFFFF`:

```cpp
// Code/encoding/Messages/StringCacheUpdate.h
-    uint16_t StartId{0};
+    uint32_t StartId{0};

// Code/encoding/Messages/StringCacheUpdate.cpp
-    StartId = Serialization::ReadVarInt(aReader) & 0xFFFF;
+    StartId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
```

VarInt 对小于 65536 的值**编码字节完全相同**,所以线上格式向后兼容:新旧版本混连时,
老版本发来的 `StartId` 语义不变。

**验证**:`Code/encoding/StringCache.cpp` 里不再有 `aStartId = m_idToString.size();`
这种「无条件推到末尾」的写法;`python Tools/Scripts/check_docs.py` 全绿。

### 修复:远端角色的动画回放队列没有上界,动画图没就绪时会一直涨

`RemoteAnimationComponent::TimePoints` 是远端角色的**动作回放队列**:服务端以 50 Hz 把范围内
每个非 owner 角色的动作整批发下来,客户端逐条塞进这个队列,再由 `AnimationSystem::Update`
每帧弹出**到期的那一条**去播放。入口有两处,都不看队列有多长:

```cpp
// Code/client/Services/Generic/CharacterService.cpp:689
animationComponent.TimePoints.push_back(action);
// Code/client/Systems/AnimationSystem.cpp:104
aAnimationComponent.TimePoints.insert(aAnimationComponent.TimePoints.end(), acReplay.Actions.begin(), acReplay.Actions.end());
```

出口却只有「队首到期就 pop 一条」这一条,而且被两道门挡着:actor 解析不出来时调用点
直接跳过(`if (!pActor) continue;`),`Actor` 有了但 `animationGraphHolder.IsReady()` 还是假时
函数提前返回(注释原文「Animation graph not ready, keep the action in queue and try again later」)。
两道门都是**正常状态** —— 远端角色刚 spawn、模型还没加载完,或者玩家刚切完 cell ——
但队列在这期间只进不出:动作越积越多,而**没有任何一处会裁剪它**。

判据在**同族的另一个队列**上:`Code/client/Systems/InterpolationSystem.cpp:19` 的插值窗口有
`constexpr size_t kMaxSamples = 8;`,注释写得很清楚 ——「Keep the window bounded. Newest samples
win; dropping from the front is what makes this safe against a play head that has stopped
advancing.」动画回放队列面对的是同一个问题(play head 停了),却没有这个上界。

修法是给队列加硬上界,并把裁剪放在**所有提前返回之前**:

```cpp
// Code/client/Systems/AnimationSystem.cpp
constexpr size_t kMaxQueuedActions = 256;

    auto& actions = aAnimationComponent.TimePoints;

    while (actions.size() > kMaxQueuedActions)
        actions.pop_front();

    // An unresolvable actor has nothing to replay, but its queue still has to be
    // trimmed above, or it grows with every snapshot that mentions it.
    if (!apActor)
        return;
```

调用点一并改掉:以前 `if (!pActor) continue;` 会让「actor 解析不出来」这种情况连裁剪的机会
都没有,现在无论 actor 能不能解析都调用 `AnimationSystem::Update`,把裁剪做掉。

**验证**:`Code/client/Services/Generic/CharacterService.cpp` 里 `AnimationSystem::Update` 的调用
不再被 `if (!pActor)` 跳过;`kMaxQueuedActions` 只在 `AnimationSystem.cpp` 定义一处;
`python Tools/Scripts/check_docs.py` 全绿。

### 修复:队伍里的三处空指针 —— 「查询队伍的接口自己会造出一个空队伍」

服务端有三个和「玩家队伍」有关的解引用,根因都是**把「组件里存着一个 id」当成「队伍一定存在」**。

**第一处**:`PartyService::GetPlayerParty` 是所有人查队伍的唯一入口,它用的是 `operator[]`:

```cpp
// Code/server/Services/PartyService.cpp(修复前)
    if (inviterPartyComponent.JoinedPartyId)
        return &m_parties[*inviterPartyComponent.JoinedPartyId];
```

`m_parties` 是 `TiltedPhoques::Map<uint32_t, Party>`,`operator[]` 在 key 不存在时会**默认构造一个
`Party` 并插进去**。于是一个「id 已经失效」的玩家调用这个函数,不会拿到 `nullptr`,而是拿到一个
**成员为空、`LeaderPlayerId` 为 0 的假队伍**,同时往 `m_parties` 里塞了一条永远清不掉的垃圾。
同文件的 `GetById`(`:36-43`)**用的是 `find` 并返回 `nullptr`**,调用方 `WeatherService.cpp:23`/`:39`
和 `CharacterService.cpp:782` 也都写了 `if (!pParty) return;` —— 只有这一处不同,判据就是同文件内的
两种写法不一致。

**第二处**:`QuestService::OnQuestChanges` 拿到返回值后**直接解引用,全程没有判空**:

```cpp
// Code/server/Services/QuestService.cpp(修复前,:40-42)
    PartyService::Party* pParty = partyService.GetPlayerParty(pPlayer);
    Player* pLeader = isLeader ? pPlayer : m_world.GetPlayerManager().GetById(pParty->LeaderPlayerId);
    auto& dedupHistory = pParty->GetQuestStageDedupHistory();
```

客户端只在**进了队伍**时才发这个包(`Code/client/Services/Generic/QuestService.cpp` 的三处发送点
`:72`/`:108`/`:158` 都带 `!m_world.Get().GetPartyService().IsInParty()` 门禁),但**已经在途的请求**
遇上玩家退队/被踢就会以「没有队伍」到达,而服务端对此不做校验;`GameServer::Kick` 只是标记断开,
真正的 `OnDisconnection` 要等下一轮事件循环,这个窗口是真实存在的。判据同样在**同一个文件里**:
`OnQuestSceneChanges`(`:210-211`)写了早退保护 `if (!partyComponent.JoinedPartyId.has_value()) return;`。

修法是只把**依赖队伍的那部分**跳过去,`pLeader`/`SceneMaster` 改用三目表达式,`dedupHistory` 的引用
从函数头部**下移到 `if` 块内**:

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

**任务日志的记账(quest log)留在 `if` 之外**:`Player::GetQuestLogComponent()` 全仓只有两个消费者
(`CharacterService.cpp:658` 的赋值和这里),单人/未组队玩家的事件同样要被记录,不能因为「不在队伍」
整体跳过。`SceneMaster` 写 0 而不是跳过,也与 `QuestStageDedupHistory::m_sceneMaster{0}` 的既有注释
「PlayerIDs start at 1, so zero is "not set."」一致。

**第三处**:`GameServer::SendToLeader`(`:755`)从来没判过 `apLeader`:

```cpp
    if (const_cast<Player*>(apLeader)->GetParty().JoinedPartyId != acPartyComponent.JoinedPartyId)
```

它全仓**只有一个调用点** —— 就是上面 `QuestService.cpp:194` 传进来的 `pLeader`,而那个值来自
`GetPlayerManager().GetById(pParty->LeaderPlayerId)`,`GetById` 查不到就返回 `nullptr`(队伍会先于玩家行
存活到 `PlayerLeaveEvent` 跑完)。现按同函数的既有风格加一条早退:

```cpp
    if (!apLeader)
    {
        spdlog::warn("Party leader no longer exists, canceling broadcast.");
        return;
    }
```

**验证**:`Code/server/Services/PartyService.cpp` 的 `GetPlayerParty` 里不再出现 `m_parties[`;
`QuestService.cpp` 的 `dedupHistory` 引用只在 `if (inParty && pParty != nullptr)` 块内使用;
`GameServer.cpp` 的 `SendToLeader` 在使用 `apLeader` 之前先判空;`python Tools/Scripts/check_docs.py` 全绿。

### 修复:客户端给远程角色生成脸部时,「没有材质的蒙版着色器」与「纹理工厂返回空」都会被直接写穿

**现象**:部分角色(尤其是外观从网络同步过来的角色)进场时客户端崩溃,崩溃点落在
`skee64.dll` 之外的**我们自己的运行时**里,读 `BSMaskedShaderMaterial` 时 fault。

**根因**:`FaceGenSystem::Update`(`Code/client/Systems/FaceGenSystem.cpp`)在蒙版着色器分支里
连取两个**引擎给的指针**却都不判空:

```cpp
// Code/client/Systems/FaceGenSystem.cpp(修复前,:86-90)
BSMaskedShaderMaterial* pMaterial = static_cast<BSMaskedShaderMaterial*>(pLightingShader->material);

BSFixedString name("");
auto pTexture = CreateTexture(name);
pTexture->buffer = CreateResourceView(s_textureHolder.Get(), 512, 512);
```

`BSLightingShaderProperty::material` 是裸 `void*`(`Code/client/Games/Skyrim/NetImmerse/BSShaderProperty.h:11`),
蒙版着色器**可以没有材质**;`CreateTexture` 的签名是
`using TCreateTexture = NiRenderedTexture*(__fastcall)(BSFixedString& aName);`(`:56`),
**以返回空表示失败**。两者都在几行之后被写穿 —— `:126` 写 `pMaterial->renderedTexture`、
`:90` 写 `pTexture->buffer` —— 而本函数在 `noexcept` 帧里,空指针就是直接 fault。

**判据在同一个函数里**:`GetHeadTriBasedGeom`(`:22-51`)把这条链上的**每一个**引擎返回值都判了空
(`pFaceNode && pActorBase`、`pFacePart`、`pHeadNode`、`pGeometry`),只有进入蒙版分支之后这两处没有。

**修法**:两处各自判空并**先 `pShaderProperty->DecRef()` 再 return**(着色器在上面刚 `IncRef` 过),
把这一帧跳过去,下一帧自然重试:

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

### 修复:服务端把「还没送出去的动作队列」当成已发送,直接清空

**现象**:某个玩家不在场时打出的动作(挥砍、施法),等他走进范围后**永远看不到** —— 不是延迟,是彻底没有。

**根因**:`Code/server/Services/CharacterService.cpp` 的 `ProcessMovementChanges` 在实体循环**结束之后**
对**全部**实体无条件清空动作队列:

```cpp
// Code/server/Services/CharacterService.cpp(修复前,:1013-1017)
    m_world.view<AnimationComponent>().each([](AnimationComponent& animationComponent)
    {
        // Remove actions we've sent
        animationComponent.Actions.clear();
    });
```

而「真的送出去了」与「走过这个循环」是两件事,两条路径都只走循环不发送:

- 实体因为 `if (movementComponent.Sent == true) continue;`(`:990`)提前跳过 —— **没有新移动、但队列里有新动作**;
- 有移动,但内层玩家循环里**没有任何玩家在范围内**(`:1000` 全部 `continue`)。

`animationComponent.Actions` 全仓只有两个读者(这里的读出与清空),写入只有 `:413` 一处,所以清空即**永久丢失**。

**判据在同一个文件里**:同族的 factions 早就改成了「送出才清」——
`bool queued = false;` → 真正塞进消息时 `queued = true;` → `if (queued) characterComponent.SetDirtyFactions(false);`
(`:924-945`,注释原文 "Only clear the flag once the change actually went out. An actor that nobody is near
right now stays dirty, so the next player to come into range still gets it instead of keeping whatever
factions it spawned with."),只有动作队列这一处还停在旧写法。

**修法**:同一个 `queued` 模式 + 与回放缓存同宽的上界(`kReplayCacheMaxSize = 32`):

```cpp
        bool actionsQueued = false;
        ...
            update.ActionEvents = animationComponent.Actions;
            actionsQueued = true;
        ...
        if (actionsQueued)
        {
            animationComponent.Actions.clear();
        }
        else if (animationComponent.Actions.size() > kMaxPendingActionEvents)
        {
            animationComponent.Actions.erase(animationComponent.Actions.begin(), animationComponent.Actions.end() - kMaxPendingActionEvents);
        }
```

新增 `constexpr uint32_t kMaxPendingActionEvents = 32;`(`:48`)。
**为什么还要加上界**:队列现在**允许跨 tick 保留**,没人靠近的实体就会一直攒;上界与
`Code/server/Game/Animation/ActionReplayCache.h:19` 的回放缓存一致(32),超出丢最旧的。

**验证**:`Code/client/Systems/FaceGenSystem.cpp` 的蒙版分支里 `pMaterial` 与 `pTexture` 都在使用前判空
且两条早退都先 `DecRef`;`Code/server/Services/CharacterService.cpp` 里 `Actions.clear()` 只出现在
`if (actionsQueued)` 块内、`kMaxPendingActionEvents` 在越界时生效;`python Tools/Scripts/check_docs.py` 全绿。

### 修复:OStim 的远程 addon 缓存从不清理,读档后会把上一个存档的状态套到同 id 的角色上

**现象**:读档(或开新档)之后进 OStim 场景时,**远程**角色的 OCum 覆盖贴图与 OStim 物体状态
会短暂按**上一个存档**的样子渲染 —— 不是不同步,是本地缓存里还留着旧存档的条目。

**根因**:`plugins/OStimTogether/src/AddonBridge.cpp` 的两个缓存 `_remoteOverlays` / `_remoteObjects`
以 `RE::FormID` 为一级键,而 FormID 在新存档里会被**重新分配**。这两个容器全仓只有写入
(`:531`、`:617`)与读取(`:686-691`),**没有任何 `erase`/`clear` 路径**。

**判据在同一个文件外**:`plugins/OStimTogether/src/main.cpp:212-223` 的
`case SKSE::MessagingInterface::kPreLoadGame:` 逐条清理了 9 个单例
(`EquipmentLock::ClearAllOStimTargets()`、`ClearManual()`、`DefaultOutfitGuard::RestoreAll()`、
`CoopSessionManager::Reset()`、`OStimBridge::ResetRemoteState()`、`FreeScenePhaseSync::Reset()`、
`ParticipantAlignmentSync::Reset()`、`FreeSceneSelfOriginLock::Reset()`、`OCumStateSync::Reset()`、
`PPAIntegration::Reset()`),**唯独 AddonBridge 缺席**。同构先例:`OStimBridge::ResetRemoteState()`
(`:1623-1649`)一次清掉 15 个成员 map,`MorphSyncService::Reset()` 同样清三个 map。

**修法**:给 `AddonBridge` 补一个 `ClearRemoteState()`(持 `_stateMutex` 清两个 map),
并挂到 `kPreLoadGame` 那一串清理里:

```cpp
// plugins/OStimTogether/src/AddonBridge.cpp
void AddonBridge::ClearRemoteState()
{
    std::scoped_lock lock(_stateMutex);
    _remoteOverlays.clear();
    _remoteObjects.clear();
}
```

**交付方式**:该修复落在**本项目不拥有、也无法推送**的子模块里,所以按既有做法做成
`Code/plugins/patches/OStimTogether/0003-clear-the-remote-addon-cache-on-load.patch`,
由 `plugin_patches.py apply` 在配置与构建前打到子模块工作树上 —— CI 的
`git submodule update --init --force --recursive --depth=1` 会丢弃任何只存在于工作树的改动。

**验证**:`python Code/plugins/tools/plugin_patches.py verify` → `PATCH VERIFY OK`(9 个补丁全部
能打到各自的 pin 上);`check` → `PATCH CHECK OK`;`python Tools/Scripts/check_docs.py` 全绿。

### 修复:上一轮空指针清扫的判据漏掉了「先取值、后解引用」这一整种形状

**现象**:在主菜单、或正在读档时,客户端收到一条远程**慢速法术**(Slow Time 系)的
`NotifyAddTarget`,进程可能直接崩;另一处是玩家死后回到主菜单/读档,1.5 秒后那次「补倒地」
会解引用一个已经不存在的玩家。

**根因**:两处都是**先把 `PlayerCharacter::Get()` 存进局部变量,之后才解引用**,
而它主菜单与读档期间为空:

```cpp
// Code/client/Services/Generic/MagicService.cpp(修前)
if (pEffect->IsSlowEffect())
    pActor = PlayerCharacter::Get();          // :456 覆盖掉上面刚判过空的 pActor

pActor->magicTarget.AddTarget(data, ...);     // :458 直接解引用
```

同一个函数 `:433` 的 `acMessage.CasterId && (pCaster = PlayerCharacter::Get());` 还会把
`pCaster` 写成空,而这个值经 `:440 data.pCaster = pCaster;` 交给引擎
(`MagicTarget::AddTarget` → `RealAddTarget`)。`PlayerService.cpp` 那处同理:

```cpp
// Code/client/Services/Generic/PlayerService.cpp(修前)
PlayerCharacter* pPlayer = PlayerCharacter::Get();          // :295
PlayerCharacter::SetGodMode(true);
m_godmodeStart = true;
pPlayer->currentProcess->KnockExplosion(pPlayer, &pPlayer->position, 0.f);   // :300
```

**判据**:`PlayerCharacter::Get()` 是 `POINTER_SKYRIMSE(PlayerCharacter*, s_character, 401069)`,
主菜单与读档期间为空;而两条路径**都不需要玩家存在就能走到** —— `OnNotifyAddTarget` 由收包触发
(`m_notifyAddTargetConnection`),`RunPostDeathUpdates` 每帧从 `OnUpdate` 调用,而
`m_knockdownStart` 是**1.5 秒前**记下的,这段时间里存档可能已经被离开。

**为什么上一轮没发现**:上一次清扫的验收条件是
「`PlayerCharacter::Get()->` across Code/client: no matches, was 18」—— 那是一条**形状**判据
(在同一个表达式里解引用)。本轮把同一个问题重问成「**取值**之后才解引用的站点」,
`grep '= PlayerCharacter::Get();'` 在 `Code/**/*.cpp` 有 **31 处**,全在这条判据之外。

**修法**:两处都改成「拿到非空才覆盖」,原值保留:

```cpp
if (acMessage.CasterId)
{
    if (auto* pPlayer = PlayerCharacter::Get())
        pCaster = pPlayer;
}
```

```cpp
if (pEffect->IsSlowEffect())
{
    if (auto* pPlayer = PlayerCharacter::Get())
        pActor = pPlayer;
}
```

```cpp
PlayerCharacter* pPlayer = PlayerCharacter::Get();
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

**为什么不直接 `return`**:慢速法术那个 hack 只是「把效果挪到玩家身上」,玩家不在时留在已经解析
并判过空的目标身上即可,没必要丢掉整条远程法术;`m_knockdownStart = false;` 特意留在 `if` 之外,
是为了让「玩家不在」的这一次也能把倒地状态收尾,不至于每帧重试。

**验证**:`grep 'PlayerCharacter::Get()->' Code` 仍为 0 命中;`Code/client/Services/Generic/MagicService.cpp`
`:437`/`:466` 两处都是 `if (auto* pPlayer = PlayerCharacter::Get())` 守卫、`:470` 的
`pActor->magicTarget.AddTarget(...)` 之前 `pActor` 已被 `:413-419` 判过空;
`Code/client/Services/Generic/PlayerService.cpp:300` 是 `if (pPlayer)` 守卫;
`python Tools/Scripts/check_docs.py` 全绿。

### 修复:专用服务器一个手滑的命令行参数就能让它连端口都没监听就退出

**现象**:给专用服务器带上 `--uPort=abc`(或任何写错的数值参数),进程**在打印任何一行日志之前就退出**,
不监听端口、不写崩溃转储;把参数去掉一切正常。

**根因**:启动期设置解析有**两处**会抛异常,而整条路径上没有一处 `catch`:

```cpp
// Code/components/console/CommandSettingsProvider.cpp(修复前)
std::string value = argv[i];
value = value.substr(value.find_first_not_of('-'));   // 空串或全由 '-' 组成时 find_first_not_of 返回 npos
...
case SettingBase::Type::kInt: setting->data.as_int32 = std::stol(args[pName]); break;
case SettingBase::Type::kUInt: setting->data.as_uint32 = std::stoul(args[pName]); break;
case SettingBase::Type::kInt64: setting->data.as_int64 = std::stoll(args[pName]); break;
case SettingBase::Type::kUInt64: setting->data.as_uint64 = std::stoull(args[pName]); break;
case SettingBase::Type::kFloat: setting->data.as_float = std::stof(args[pName]); break;
```

`std::string::substr(npos)` 抛 `std::out_of_range`;`std::sto*` 对解析不了的输入抛
`std::invalid_argument`(`abc`、空串)或 `std::out_of_range`(超出该类型范围)。

**判据 1 —— 这条路径上没有第二个机会**:调用链是
`Code/server_runner/main.cpp:233` → `DediRunner::DediRunner`(`Code/server_runner/DediRunner.cpp:34`,
`CreateGameServer` 的回调)→ `CreateGameServer`(`Code/server/main.cpp:100 aCallback();`)
→ `DediRunner::LoadSettings`(`:53 LoadSettingsFromCommand`)。异常要逃出的是 **`main()` 的调用栈**,
而 `grep "catch" Code` 全仓只有 **8 处**,没有一处在这条链上(`Code/server_runner/**` 与
`Code/server/main.cpp` 各 0 处)。这不是「异常会被上层处理」,是「异常直接结束进程」。

**判据 2 —— 写同样这五个字段的另外两条路径早就不抛**:`set` 控制台命令
(`Code/components/console/ConsoleRegistry.cpp:141-145`)用的是
`pSetting->data.as_int32 = ConvertStringValue(value.c_str(), pSetting->data.as_int32);`;
`.ini` 载入(`Code/components/console/IniSettingsProvider.cpp:122-126`)五个数值类型全走
`GetIniValue` → `ConvertStringValue<TVal>(szValue, a_nDefault)`。
`Code/components/console/ConsoleUtils.h:20-26` 的注释原文就是 "on error, you get the default"。
⇒ 三条路径里,**只有命令行这一条会抛**,而它也是唯一一条**出错就没有第二次机会**的
(另外两条出错只是该值保持默认,服务器照常起来)。

**修法**:跳过没有非横线字符的参数,五个数值改走 `ConvertStringValue`。

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
// Code/components/console/CommandSettingsProvider.cpp(修复后,:55-59)
case SettingBase::Type::kInt: setting->data.as_int32 = ConvertStringValue(args[pName].c_str(), setting->data.as_int32); break;
case SettingBase::Type::kUInt: setting->data.as_uint32 = ConvertStringValue(args[pName].c_str(), setting->data.as_uint32); break;
case SettingBase::Type::kInt64: setting->data.as_int64 = ConvertStringValue(args[pName].c_str(), setting->data.as_int64); break;
case SettingBase::Type::kUInt64: setting->data.as_uint64 = ConvertStringValue(args[pName].c_str(), setting->data.as_uint64); break;
case SettingBase::Type::kFloat: setting->data.as_float = ConvertStringValue(args[pName].c_str(), setting->data.as_float); break;
```

**为什么不加 `try`/`catch`**:`CODE_GUIDELINES.md:47` 明文 "Don't use exceptions, don't use STL code
that can throw, use the nothrow version if available or the unsafe version.";而且这里要的语义是
「这个参数不认识,就跳过它」,不是「这个参数不认识,就整体失败」—— 一条写错的参数不该让同一行里
`--uMaxPlayerCount=32` 这种**正确的参数**也一起失效。`ConvertStringValue` 失败时保留
`setting->data` 里的原值(即该设置项的默认值),与 `.ini` 载入、`set` 命令的行为完全一致。

**验证**:`Select-String -Path Code\components\console\CommandSettingsProvider.cpp -Pattern 'std::sto'`
→ **0 命中**;`python Tools/Scripts/check_docs.py` 全绿。

### 修复:查询队伍的接口自己会造出一个空队伍,而踢人接口不看对方在不在自己队里

**现象**:服务端的队伍系统有**两个**独立的故障,都由同一批客户端可控的 id 触发:

1. **跨队踢人**:任何一个队伍的队长,可以用 `PartyKickRequest` 把**别的队伍**里的玩家踢出去(只要
   猜得到对方的 `PlayerId`)。被踢的玩家莫名其妙地离开队伍。
2. **队伍凭空出现**:`GetPlayerParty()` 这类「查询」接口用的是 `m_parties[id]`。`JoinedPartyId`
   还在、但队伍已经被销毁时,这个下标会**静默插入**一个全新的空队伍 —— 于是每个调用者都会把
   这个空队伍当成真队伍继续用,而它的 `LeaderPlayerId` 是**未初始化的**。

**根因**:`Code/server/Services/PartyService.cpp` 里同时存在两种写法 —— `GetById()`(`:36-43`)
一直用 `find` 并判 `end`,而 `GetPlayerParty()`、`OnPartyChangeLeader`、`OnPartyKick`、
`OnPlayerJoin`、`OnPartyInvite`、`OnPartyAcceptInvite`、`RemovePlayerFromParty` 七处用的是
`m_parties[...]`:

```cpp
// Code/server/Services/PartyService.cpp(修复前)
Party& party = m_parties[id];                  // 队伍不存在时会插入一个空队伍
auto& members = party.Members;
members.erase(std::find(std::begin(members), std::end(members), apPlayer));  // 找不到时 erase(end())
```

```cpp
// Code/server/Services/PartyService.h:89-96(修复前)
struct Party
{
    uint32_t LeaderPlayerId;   // 没有默认初始化
    ...
};
```

两条判据:

- `std::find` 找不到时返回 `end()`,而 `vector::erase(end())` 是**未定义行为**(空 vector 上会把
  size 减成 `SIZE_MAX`)。跨队踢人时 `apPlayer` 本来就不在 `members` 里,所以这条路径**必然**走到
  `erase(end())` —— 而它恰好是 `if (members.empty())` 成立的那条分支。
- `operator[]` 插入的新 `Party` 里 `LeaderPlayerId` 是未定义值,紧接着的
  `if (party.LeaderPlayerId == player->GetId())` 就是**读未初始化内存**。

`OnPartyKick` 原本只有两道门:`if (!pKick)`(目标存在)和 `if (party.LeaderPlayerId ==
player->GetId())`(请求者是队长)。**没有一道门问过「pKick 在不在请求者的队伍里」**,而
`PartyMemberPlayerId` 是从线里直接读出来的:

```cpp
// Code/encoding/Messages/PartyKickRequest.cpp:11
PartyMemberPlayerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
```

**修法**:七处 `m_parties[...]` 全部改成 `find`/`GetById` + 判空(只有 `OnPartyCreate` 里 `:117`
那一处保留下标 —— 它的 key 刚由 `m_nextId++` 生成,必然不存在);`RemovePlayerFromParty` 在 `erase`
前先判 `std::find` 的结果;`OnPartyKick` 增加成员归属校验:

```cpp
// Code/server/Services/PartyService.cpp(修复后,OnPartyKick)
const auto* pParty = GetById(*inviterPartyComponent.JoinedPartyId);
if (pParty != nullptr && pParty->LeaderPlayerId == player->GetId())
{
    const auto& kickPartyComponent = pKick->GetParty();
    if (!kickPartyComponent.JoinedPartyId || *kickPartyComponent.JoinedPartyId != *inviterPartyComponent.JoinedPartyId)
    {
        spdlog::error("[PartyService]: Player {} is not in the party of {}", message.PartyMemberPlayerId, player->GetId());
        return;
    }
    ...
}
```

```cpp
// Code/server/Services/PartyService.cpp(修复后,RemovePlayerFromParty)
const auto partyItor = m_parties.find(id);
if (partyItor != std::end(m_parties))
{
    Party& party = partyItor->second;
    auto& members = party.Members;
    const auto memberItor = std::find(std::begin(members), std::end(members), apPlayer);
    if (memberItor != std::end(members))
    {
        members.erase(memberItor);
        ...
```

顺带修掉 `Code/server/Services/QuestService.cpp:223` 的 `pParty` 未判空即解引用 —— 那是
`GetPlayerParty()` 改成返回 `nullptr` 之后**唯一**没跟上的调用点(同文件 `:46-47` 早就用了
`pParty == nullptr ? ... : ...` 的三目写法,`Code/server/Services/WeatherService.cpp:23`/`:39`
两处也都判了空)。

**验证**:`Select-String -Path Code -Include *.cpp,*.h,*.hpp,*.inl -Pattern 'erase\(std::find'`
→ **0 命中**;`Select-String -Path Code\server\Services\PartyService.cpp -Pattern 'm_parties\['`
→ 只剩 `OnPartyCreate` 的 1 处;`python Tools/Scripts/check_docs.py` 全绿。

### 修复:OStimNG 的动画事件注解只判了「2 个字段」,其余全部落进越界分支

**现象**:OStimNG 场景里,一条字段数不是 2 的 `OStimEvent` 注解会让插件读越界;
任何一段数字写错的注解会让 `std::stoi` 抛异常,而异常从 `RE::BSTEventSink`
的事件槽逃出去 ⇒ `std::terminate`。

**根因**:`plugins/OStimNG/skse/src/Core/Thread.cpp:849-856`(修复前)只判了
`payloadVec.size() == 2`,**其余全部落进 `else`**:

```cpp
// plugins/OStimNG/skse/src/Core/Thread.cpp(修复前)
std::vector<std::string> payloadVec = stl::string_split(a_event->payload.c_str(), ',');
if (payloadVec.size() == 2) {
    int actorIndex = GetActor(actor)->index;
    callEvent(payloadVec[0], actorIndex, std::stoi(payloadVec[1]), actorIndex);
} else {
    callEvent(payloadVec[0], std::stoi(payloadVec[1]), std::stoi(payloadVec[2]), std::stoi(payloadVec[3]));
}
```

注解语法有两个合法形状,写在 `data/SKSE/Plugins/OStim/list of annotations.txt:14-19`:
`OStimEvent.[event],[target]` 与 `OStimEvent.[event],[actor],[target],[performer]`。
代码把第一个判成正例,**把「不是它」当成了「就是第二个」**。而
`stl::string_split`(`skse/src/Util.cpp:47-62`)会丢掉尾部的空 token,于是
`""` → size 0、`"a,"` → size 1、`"a,b,c"` → size 3 —— 三种都不是 2,三种都落进
`else`,前两种读 `payloadVec[1..3]` 越界、第三种读 `[3]` 越界。

**payload 由谁决定**:注解文本写在动画图的 behaviour 数据里
(`data/Nemesis_Engine/mod/ostim/0_master/#0106.txt:1222`),但同一条 tag 也能
**从网络重放** —— `plugins/AnimSyncTogether/src/STRPMClient.cpp:436-437` 把收到的
字符串直接 `actor->NotifyAnimationGraph(inputEvent)`,唯一过滤是
`:394` 的 `SyncRules::...ShouldSyncEvent(tag)`。所以**远端客户端能决定这段 payload**。

**判据在同一个文件里**:`skse/src/Papyrus/PapyrusScenes.h:18-28` 的 `GetActor`
先判 `thread` 再判 `actor`,而紧邻的 `:10` / `:15` 两个入口**一个都不判**;
`skse/src/Papyrus/PapyrusThread.h:279` 对 `actor`/`target`/`performer` 三个下标
做了范围校验,`ProcessEvent` 这条**同构的入口**不做。⇒ 判据是
**「同一个文件里已经有判空的正确写法」**,不是「看起来像不像会空」。

同一族在这次一并修掉的还有:`GetActor(actor)->` 在 `ProcessEvent` 里 **10 处**直接
解引用(而 `skse/src/Core/Thread.cpp:458-463` 明确 `return nullptr;`);
`getCurrentNodeInternal()`(`skse/src/Core/Thread.h:125 Graph::Node* m_currentNode = nullptr;`)
在 `NodeHandler::handlePeakAnnotation` 与 `nodeChanged` 里两处直接解引用;
`NodeHandler::threadEnd()` 的 `delete this` 之后 `Thread::nodeHandler` 仍是**悬垂指针**
(而 `getNodeHandler()` 是 public、`nodeHandler` 是 private,外部无法置空);
`LoopPeakHandler::speedChanged()` 三重解引用加一个不判范围的下标;
`PeakSoundPlayer` 构造函数三层解引用;`RemoveActor()` 在空 `m_actors` 上算
`size() - 1`。

**修法**:新增一个不抛异常的注解数字解析器,并把每个入口改成先判空再取值:

```cpp
// plugins/OStimNG/skse/src/Core/Thread.cpp(修复后)
namespace {
    bool parseAnnotationInt(const std::string& a_text, int& a_out, int a_base = 10) {
        const char* begin = a_text.data();
        const char* end = begin + a_text.size();
        ...
        const auto result = std::from_chars(begin, end, value, a_base);
        if (result.ec != std::errc{}) {
            return false;
        }
        ...
    }
}
```

**交付方式**:该修复落在**本项目不拥有、也无法推送**的子模块里,所以按既有做法做成
`Code/plugins/patches/OStimNG/0002-guard-the-animation-event-path.patch`,
由 `plugin_patches.py apply` 在配置与构建前打到子模块工作树上 —— CI 的
`git submodule update --init --force --recursive --depth=1` 会丢弃任何只存在于工作树的改动。

**验证**:`python Code/plugins/tools/plugin_patches.py verify` → `PATCH VERIFY OK`(10 个补丁
全部能打到各自的 pin 上);`check` → `PATCH CHECK OK`;
`python Code/plugins/tools/plugin_snapshot.py verify` → `SNAPSHOT OK`;
`python Tools/Scripts/check_docs.py` 全绿。

### 修复:任意客户端一个畸形认证包即可让服务端空转数十亿次,并顺手改写全服日历

**现象**:专用服务器上,一个客户端只要在认证包里把日历字段写成
`Month = 0xFFFFFFFF`,服务端在**接受他进服的那一瞬间**就会卡在
`GetTimeInDays()` 的月份循环里空转约 42.9 亿次;写成
`Day = 0xFFFFFFFF` 则会卡在 `Update()` 的进位 `while` 里约 1.4 亿次。
两种都会让整个 tick 线程停住,所有玩家一起掉线。

**根因**:这五个字段是从线上原样读进来的,读侧一个范围校验都没有。

```cpp
// Code/encoding/Structs/TimeModel.cpp(修复前)
Day = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
Month = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
Year = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
```

`& 0xFFFFFFFF` 对 `uint32_t` 是恒等操作,等于什么都没做。而 `Month` 直接
成了 `Code/common/DateTime.cpp:38-46` 的循环上界:

```cpp
// Code/common/DateTime.cpp
float DateTime::GetTimeInDays() const noexcept
{
    float totalDays = static_cast<float>(m_timeModel.Year) * 365.f;
    for (uint32_t i = 0u; i < m_timeModel.Month; i++)
        totalDays += GetNumberOfDaysByMonthIndex(i);
    ...
}
```

`Day` 则成了 `DateTime::Update` 里 `while (m_timeModel.Day > GetNumberOfDaysByMonthIndex(m_timeModel.Month))`
的减法次数。同一族的还有 `Time` 与 `TimeScale`:两者都没有 NaN/Inf 检查,
却被 `m_timeModel.Day += static_cast<uint32_t>(m_timeModel.Time / 24.f);` 拿去
做「浮点转无符号整数」——NaN 与负值在这里都是未定义行为。

**可达性**:认证包在认证**之前**就被解析(`Code/server/GameServer.cpp:572 OnConsume`
→ `:592 factory.Extract(reader)`),`BindMessageHandlers` 里那道「拿不到 player 就踢」
的判空(`:262`)被 `AuthenticationRequest` 的专属分支(`:281-286`)绕开了。
服务端没设密码时(`:33-34` 默认空串、`:183 m_isPasswordProtected`),
`:905` 的 `"" == ""` 判定为真,所以任何一个 `Version` 对得上、
服务端没满、SKSE/MO2 标志合格的客户端都能走到 `:1053` 的
`PlayerJoinEvent(pPlayer, ..., acRequest->PlayerTime)`。

**顺带的数据污染**:`Code/server/Services/CalendarService.cpp:40` 拿客户端数据
算 `playerHasFurthestTime`,一旦为真就把客户端的 `Day/Month/Year` **无条件**
抄进服务端的 `m_dateTime`(`:50-52`),再广播给所有人(`:54-58`)⇒ 一个客户端
可以单方面改写全服日历。夹住范围同时也把这条路上的越界值挡掉了。

**修法**:按 `Code/encoding/Structs/Factions.h:18-20` 的既有做法,
在结构体上声明边界、在读侧夹住,而不是抛异常 —— `Deserialize` 处在 `noexcept` 链上:

```cpp
// Code/encoding/Structs/TimeModel.h(修复后)
static constexpr float kMaxTimeScale = 1000.f;
static constexpr float kMaxTime = 24.f;
static constexpr uint32_t kMaxDay = 31u;
static constexpr uint32_t kMaxMonth = 11u;
static constexpr uint32_t kMaxYear = 999u;
```

边界取自服务端**自己**已有的判断:`CalendarService::SetTimeScale` 的
`aScale <= 1000.f`、`SetDate` 的 `aMonth < 12` 与 `aYear <= 999`、
`std::fmod(Time, 24.f)`。非有限值一律归零,因为 NaN/Inf 夹不进任何区间:

```cpp
// Code/encoding/Structs/TimeModel.cpp(修复后)
float ClampFloat(float aValue, float aMax) noexcept
{
    if (!std::isfinite(aValue))
        return 0.f;

    return std::min(std::max(0.f, aValue), aMax);
}
```

`Serialize` 未改:夹在**读**侧,所以客户端发出去的是原值,而
`ServerTimeSettings` 的广播在两端也各过一次 `Deserialize`,同样被夹住。

**验证**:`Code/tests/encoding.cpp:549 TEST_CASE("TimeModel wire bounds", "[encoding.time_model]")`
用 `Serialization::WriteFloat/WriteVarInt` 手工造畸形包,覆盖 NaN、`1.0e30f`、
三个 `0xFFFFFFFF` 与三个负值,并回环一次正常值;
CI 的 `.github/workflows/windows.yml:594-638` 会跑 `TPTests.exe` 并要求
`All tests passed`。`python Tools/Scripts/check_docs.py` 全绿。

### 修复:血量增量既没有范围校验、服务端账本的符号还和客户端相反

**现象**:一个客户端发一条 `RequestHealthChangeBroadcast`,把 `DeltaHealth` 写成
`-1e38` 或 `-inf`,范围内**所有**客户端都会把那个 actor 的血量算成非正数,
然后 `pActor->Kill()` —— 非玩家 actor 一律被远程处决;写成 `NaN` 则让
`ForceActorValue` 收到 NaN。

**根因**:这个字段从线上原样读进来,读侧没有任何值域检查,而它在接收端是
「加到血量上的增量」,加完还要判死。

```cpp
// Code/encoding/Messages/RequestHealthChangeBroadcast.cpp(修复前)
Id = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
DeltaHealth = Serialization::ReadFloat(aReader);
```

接收端 `Code/client/Services/Generic/ActorValueService.cpp:295-305`:

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

同一处还有第二个问题:**服务端账本的符号是反的**。服务端在
`Code/server/Services/ActorValueService.cpp:90` 用的是
`ActorValuesList[24] = currentHealth - message.DeltaHealth`,而客户端用的是 `+`,
两端的约定本来就该一致 —— 客户端的发送侧
(`Code/client/Games/Skyrim/Actor.cpp:1093`/`:1142`/`:1169`)发的是「受伤为负、
治疗与回血为正」,所以服务端的减号会把每一次受伤都记成加血。这份账本不是死数据:
它会经 `InitialActorValues`/`AllActorValues` 分发给新加入的玩家
(`Code/server/Services/CharacterService.cpp:109`/`:688`/`:883`),再由客户端
`Actor::SetActorValues` 逐键 `ForceActorValue` 写回引擎。

**可达性**:`RequestHealthChangeBroadcast` 在服务端 handler 表里
(`Code/server/Services/ActorValueService.cpp:19`),不需要任何所有权即可触发 ——
同文件另三个 handler(`:31`/`:58`/`:110`)都写了 `IsCurrentOwner`,只有这一个没有。

**修法**:按 `TimeModel` 的同一范式,在**结构体上**声明上界、在**读侧**夹住,
并且两个方向都夹(客户端发来的 `RequestHealthChangeBroadcast` 与服务端广播的
`NotifyHealthChangeBroadcast` 各自过一次 `Deserialize`):

```cpp
// Code/encoding/Messages/NotifyHealthChangeBroadcast.cpp
if (!std::isfinite(DeltaHealth))
    DeltaHealth = 0.f;
else
    DeltaHealth = std::min(std::max(DeltaHealth, -kMaxHealthDelta), kMaxHealthDelta);
```

`kMaxHealthDelta = 100000.f` 声明在两个结构体里(`RequestHealthChangeBroadcast.h`
与 `NotifyHealthChangeBroadcast.h`),注释写明它为什么存在。**不加所有权门**:
客户端对「打远程玩家」本来就不发广播
(`Code/client/Games/Skyrim/Actor.cpp:1096-1099` 直接 `return wouldKill;`),而 NPC 的
所有权归 spawn 方,贸然加门会挡掉合法的 NPC 受击广播 —— 那属于玩法决策,
不在「稳定性/兼容/同步」的审计范围内。服务端账本的减号同时改成加号,与客户端一致。

**验证**:`Code/tests/encoding.cpp:609 TEST_CASE("Health change broadcast wire bounds", "[encoding.health_change]")`
手工造包覆盖 NaN、`-1.0e30f`、`1.0e30f`,并回环一次 `-12.5f`;
CI 的 `.github/workflows/windows.yml:594-638` 会跑 `TPTests.exe` 并要求
### 修复:actor value 的**键**从线上原样读入,而它是引擎表的下标

**现象**:一个客户端发一条 `RequestActorValueChanges`,把 `Values` 的键写成
`0xFFFFFFFF`,范围内的客户端会拿这个键去 `SetActorValue`/`ForceActorValue`,
也就是直接写进引擎的 actor value 表下标;值是 `NaN`/`inf` 时,污染的是那个
actor value 之后**每一次**读取(血量、速度、抗性都由同一张表供给)。

**根因**:这条消息只夹了**条目数**,没夹**键的取值范围**,也没夹值的有限性。
结构体与四个消息的读侧都是同一形状:

```cpp
// Code/encoding/Structs/ActorValues.cpp(修复前)
const uint32_t key = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
auto value = Serialization::ReadFloat(aReader);
ActorValuesList.insert({key, value});
```

接收端 `Code/client/Services/Generic/ActorValueService.cpp:346` 与 `:376` 只按
键**挑分支**,没有任何范围校验:

```cpp
// Code/client/Services/Generic/ActorValueService.cpp:341-346
if (key == ActorValueInfo::kStamina || key == ActorValueInfo::kMagicka || key == ActorValueInfo::kHealth)
{
    pActor->ForceActorValue(ActorValueOwner::ForceMode::DAMAGE, key, value);
    continue;
}
pActor->SetActorValue(key, value);
```

`Actor::SetActorValue`(`Code/client/Games/Skyrim/Actor.cpp:732-735`)把它转发给
`actorValueOwner.SetValue(aId, aValue)`,而那张表的合法下标只有
`Code/client/Games/Skyrim/Forms/ActorValueInfo.h:174` 的 `kActorValueCount = 164`
个。同一个 `ActorValues` 结构体还随角色数据一起走
(`AssignCharacterResponse.AllActorValues`、`CharacterSpawnRequest.InitialActorValues`),
经服务端账本(`Code/server/Services/CharacterService.cpp:109`/`:688`/`:883`)
分发给新加入的玩家,再由 `Code/client/Games/Skyrim/Actor.cpp:836-843`
逐键写回引擎 —— 一份被污染的账本会把污染复制给每一个后进服的玩家。

**可达性**:`RequestActorValueChanges`/`RequestActorMaxValueChanges` 在服务端
handler 表里(`Code/server/Services/ActorValueService.cpp:17-18`),两者都有
`IsCurrentOwner` 门(`:31`/`:58`) —— 也就是说,能触发它的是**那个 actor 的
当前所有者**,不是任意客户端。但「所有者」这个身份本身是可以在游戏里转移的
(读档、换角色、`OwnershipEpoch` 递增),而客户端自己的发送侧
(`Code/client/Services/Generic/ActorValueService.cpp:131`)只发 `0..163` 的键,
所以越界键**只可能来自被改过的客户端或半途失步的流**。

**修法**:与 `TimeModel`/血量增量同一范式 —— 上界声明在结构体上、夹在**读**侧、
四个消息与嵌套结构体共用同一个判据:

```cpp
// Code/encoding/Structs/ActorValues.cpp
bool ActorValues::IsValidActorValue(uint32_t aId, float aValue) noexcept
{
    return aId <= kMaxActorValueId && std::isfinite(aValue);
}
```

```cpp
// Code/encoding/Messages/RequestActorValueChanges.cpp:39-47(修复后)
const uint32_t key = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;
const auto value = Serialization::ReadFloat(aReader);

// Both fields are read above so that an entry which is dropped here still
// consumes its bytes and the rest of the stream stays aligned.
if (!ActorValues::IsValidActorValue(key, value))
    continue;

Values.insert({key, value});
```

`Code/encoding/Structs/ActorValues.h:20` 新增
`static constexpr uint32_t kMaxActorValueId = static_cast<uint32_t>(kMaxActorValueCount - 1);`,
与既有的条目数上限 `kMaxActorValueCount` 并列 —— 前者管「能收几条」,后者管
「下标能到哪」,两者此前被当成了同一个东西。**一处刻意的取舍**:被丢弃的条目
仍然要**消费掉自己的字节**(键与值都先读出来再判),`continue` 而不是 `break`,
否则它后面所有字段都会从错误的偏移读起 —— 那是把「一个坏键」换成「整包错位」。
`Serialize` 侧未改,与 `TimeModel` 的修复同一策略:夹在读侧。

**验证**:`Code/tests/encoding.cpp:670 TEST_CASE("Actor value wire bounds", "[encoding.actor_values]")`
手工造包覆盖越界键 `0xFFFFFFFF`、`0x12345678`、`NaN` 值,并在包尾放一个
`0xDEADBEEF` 哨兵 —— 读完之后它必须原样读得到,以此证明被丢弃的条目确实
消费了自己的字节;另有一段回环一次正常的两键集合。
CI 的 `.github/workflows/windows.yml:594-638` 会跑 `TPTests.exe` 并要求
`All tests passed`。`python Tools/Scripts/check_docs.py` 全绿。

### 修复:四处的「判空」其实是日志,宏从不中止 —— release 里它们就是空指针解引用

**现象**:客户端有三条路径在引擎可能给出空指针的地方继续解引用:抛射物发射后取回
`Projectile*`(`Code/client/Games/Skyrim/Projectiles/Projectile.cpp`)、从附魔额外数据读
`EnchantmentItem*` 及其 `EffectItem` 的 `pEffectSetting`(`Code/client/Games/Skyrim/TESObjectREFR.cpp`
的 `GetItemFromExtraData`)、把附魔写回额外数据列表(`GetExtraDataFromItem` 的 `SetEnchantmentData`)。
四处都写了 `TP_ASSERT`,看上去像判过了。

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
`TP_ASSERT` 既不 `abort` 也不返回失败,它只写一行日志;`__debugbreak()` 也只在挂了调试器时
执行 —— 文件上方作者自己写着 `// TODO: should this be debug only? I removed the check since
debug is broken, can only use releasedbg`。所以 `TP_ASSERT(pProjectile, ...)` 之后的
`pProjectile->fPower` 没有任何保护。

**可达性**:四条指针来源都允许为空 —— `Code/client/Games/Skyrim/Projectiles/Projectile.cpp:99`
的 `ThisCall(RealLaunch, ...)` 把结果写进出参;`Code/client/Games/Skyrim/TESObjectREFR.cpp:126-140`
的 `TESObjectREFR::GetByHandle` 显式允许 `pResult` 保持 `nullptr`;`Code/client/Games/Skyrim/RTTI.h:17-27`
的 `Cast<>` 在 RTTI 未映射的旧版游戏上**恒**返回 `nullptr`(注释逐字:
`// guard against unmapped rtti ids (legacy game versions): the game's dynamic cast dereferences`);
`Code/client/Games/Skyrim/Forms/EnchantmentItem.cpp:39` 的 `EnchantmentItem* pItem = nullptr;` 在创建被
引擎拒绝时原样返回。附魔那两条路径**线上可达**:`Inventory::Entry.ExtraEnchantId` 随
`NotifyInventoryChanges` / `NotifyObjectInventoryChanges` 过线(`Code/encoding/Structs/Inventory.cpp:64`),
`GetExtraDataFromItem` 里 `arEntry.ExtraEnchantId.ModId == 0xFFFFFFFF` 走 `EnchantmentItem::Create`,
否则走 `Cast<EnchantmentItem>(TESForm::GetById(...))` —— 两条分支都能给出空指针,随后直接进引擎的
`Code/client/Games/Skyrim/ExtraData/ExtraDataList.cpp:130-135 SetEnchantmentData`。

**修法**:把三处「日志」换成真分支。`Projectile.cpp` 在拿到空的 `result` 时 `return nullptr`;
拿到空的 `pProjectile` 时保留事件本身、只让 `Event.Power` 留在 `arData.fPower` 的初值上 ——
**少发一个抛射物比漏发一个更糟**,所以 `World::Get().GetRunner().Trigger(Event)` 没有被跳过。
`GetItemFromExtraData` 把 `pEnchantment` 的整段(`GetServerModId`、`formID & 0xFF000000`、
`listOfEffects` 遍历)移进 `if (pEnchantment)`,并把 `if (!pEffectItem) continue;` 收紧成
`if (!pEffectItem || !pEffectItem->pEffectSetting) continue;`(`pEffectSetting` 在
`EnchantmentItem::Create` 里同样只被日志、不被跳过);`GetExtraDataFromItem` 把
`SetEnchantmentData` 包进 `if (pEnchantment)`。`arEntry.ExtraEnchantCharge` /
`ExtraEnchantRemoveUnequip` 仍在 `if` 之外赋值 —— 它们来自 `pExtraEnchantment` 本身,
与 `pEnchantment` 是否为空无关。

**验证**:这四处是引擎指针判空,`Code/tests` 只覆盖 `Code/encoding`,且 `Projectile::Launch` /
`GetItemFromExtraData` / `GetExtraDataFromItem` 都依赖引擎与 `World::Get()`,
**没有可写的单元测试** —— 以静态复核为准:重扫 `Code/client` 全部 9 个 `TP_ASSERT` 站点,
每一个后面现在都紧跟真 `if`(`Projectile.cpp:20`/`:30`/`:106`/`:116`、
`TESObjectREFR.cpp:536`/`:571`,以及 `:521`/`:629` 的注释与其后的 `:528 if (pEnchantment)` /
`:634 if (pEnchantment)`)。`python Tools/Scripts/check_docs.py` 全绿。

### 修复:线上字符串完全没有长度上限 —— 一个字段就能让接收方分配任意大的内存

**现象**:线上的字符串是「长度 + 这么多字节」,长度由发送方随便写,接收方读完也不知道整包
到底多大。于是 `Username`、`ChatMessage`、`Text`、`SoundFilename`、`AppearanceBuffer`、
`ModList[i].Filename`、缓存字符串的长度全部由对端决定,而这些值还会继续往下走:进 CEF 渲染、
进 `TESNPC::Deserialize`、进 `SubtitleManager::ShowSubtitle`、进服务端整个会话都持有的
per-player map。`Code/encoding` 里 30 处 `Serialization::ReadString` 只有 4 处带上限
(全部是 `NotifyPluginMessaging` / `PluginMessagingRequest` 的插件消息通道)。

**根因**(修复前):
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
一次 `Mods::Deserialize` 就能产出 65535 个任意长的文件名。

**可达性**:这些全是普通客户端消息。`AuthenticationRequest` 本身**不需要先过任何门**——
它就是那道门,而 `Username` 读进来的那一刻玩家对象还没被创建;`SendChatMessageRequest` /
`PlayerDialogueRequest` / `SubtitleRequest` 只要连上就能发;`Mods` 随 `AuthenticationRequest`
一起过线。客户端侧同样没有上限:用户名取自游戏内 NPC 的全名
(`Code/client/Services/Generic/TransportService.cpp:290` 的 `request.Username = pNpc->fullName.value.AsAscii();`),
聊天内容直接来自 UI 输入框,而输入框**没有 `maxlength`**。

**修法**:新建 `Code/encoding/WireLimits.h`,把每个字段的上限写成具名常量,并提供
`inline void Clamp(TiltedPhoques::String& aValue, const uint32_t aMaxBytes) noexcept`;
22 个文件在读完字符串之后调用它。上限取的是**宽松到没有任何合法发送方会碰到**的值
(`kMaxDisplayNameBytes = 64`、`kMaxChatMessageBytes = 4096 + 512`(2026-10-10 更正:该字段同时是插件层的传输通道,见本节之后的新条目)、`kMaxDialogueTextBytes = 1024`、
`kMaxSoundPathBytes = 260`、`kMaxFileNameBytes = 260`、`kMaxVersionBytes` / `kMaxTokenBytes = 64`、
`kMaxAppearanceBytes = 1 << 15`、`kMaxCachedStringBytes = 512`)。
`kMaxAppearanceBytes` 不是拍出来的:`Code/client/Games/Forms.cpp:20-36` 的 `TESNPC::Serialize`
写进一个 `char buffer[1 << 15]` 的栈缓冲,本来就产不出更长的东西。

**为什么夹取而不是丢包**:为一个字段丢掉整包,会把「显示得不对」升级成「掉线」,而
`String::resize` 向下不会抛。这也决定了这个修法的**边界**:夹取发生在字符串读完之后,它约束的是
「值怎么用」,不是 reader 内部的瞬时分配 —— 真正的整包上限只能加在接收入口
(`GameServer::OnConsume` / `Client::OnConsume`),只有它们知道包的真实大小。这条边界写在
`Code/encoding/WireLimits.h` 的头注释里。

**验证**:`Code/tests/encoding.cpp:761 TEST_CASE("Wire string limits", "[encoding.wire_limits]")`
覆盖四段:`Clamp` 的单元行为(短串不动、恰好等于上限不动、超长被截);`AuthenticationRequest`
的 `Username` 超长时读回恰好 `kMaxDisplayNameBytes`;`SendChatMessageRequest` 的 `ChatMessage`
超长时被截而 `MessageType` 保持;`NotifyChatMessageBroadcast` 的 `PlayerName` 与 `ChatMessage`
各自被截。另有一段正常长度的往返 `REQUIRE(sentRound == receivedRound)`,证明夹取没有改动合法包。
CI 的 `.github/workflows/windows.yml:594-638` 会跑 `TPTests.exe` 并要求 `All tests passed`。
`python Tools/Scripts/check_docs.py` 全绿。

### 修复:服务端在 `noexcept` 的聊天转发里每包重编译一次 `std::regex`

**现象**:`OverlayService::sendPlayerMessage` 和两条脚本绑定各自构造一个
`std::regex escapeHtml{"<[^>]+>\\s+(?=<)|<[^>]+>"}` 来剥掉聊天文本里的 HTML 标签。
构造发生在 `noexcept` 的函数里,而且每个聊天包、每次脚本调用都要重编译一次 —— 位置就在
把消息转发给所有人的那条路上。

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
`CODE_GUIDELINES.md:47` 写的是「Don't use exceptions, don't use STL code that can throw」,而
`std::regex` 正是一个会抛的 STL 类型:`std::regex` 的构造与 `std::regex_replace` 都要分配,
在 `noexcept` 的函数里分配失败就是 `std::terminate` —— 一条聊天消息换来整个进程结束。
同样的构造还出现在 `Code/server/Scripting/GameServer_Bindings.cpp:20` 与 `:29`。

**可达性**:`Code/server/Services/OverlayService.cpp:80`(`HandleChatMessage` 的末尾)与 `:85`
(`OnPlayerDialogue`)都直接调 `sendPlayerMessage`,两个都是**任何已连接玩家能发的包**;
两条脚本绑定则是服务器自己的 Lua 脚本调用。全仓 `std::regex` 的构造点只有这 3 处。

**修法**:新建 `Code/encoding/ChatSanitize.h`,`StripHtmlTags` 单遍扫描,语义与原正则逐字对齐
(`[^>]+` 要求括号间至少一个字符,所以 `"<>"` 与未闭合的 `<` 都算文本;`<[^>]+>\s+(?=<)` 这一支
会连标签之间的空白一起吃掉)。`\s` 的集合按 ECMAScript 默认文法写死,不走 locale 相关的
`std::isspace`。它放在 `Code/encoding` 而不是两个服务端调用者旁边,是因为那是测试目标链接的
静态库:`Code/tests/xmake.lua` 只 `add_deps("SkyrimEncoding")`。

**验证**:把 `StripHtmlTags` 逐字移植成 JS,与原正则对拍;字母表 `['<','>','a',' ','\t','\n']`,穷举
长度 0–6 的**全部 55998 个字符串**外加 17 个手写探针 ⇒ **0 处不一致**
(`"<b>hello</b>"→"hello"`、`"<b> <i>x</i>"→"x"`、`"<b> keep <i>x</i>"→" keep x"`、
`"a <> b"→"a <> b"`、`"<>"→"<>"`、`"x <b> y"→"x  y"`、`"<b> "→" "` 等全部逐字相同)。
这 11 例连同幂等性一起写进 `Code/tests/encoding.cpp:850 TEST_CASE("Chat sanitize", "[encoding.chat_sanitize]")`。
`git grep -n "regex" -- Code/server` 现在 0 命中。
`python Tools/Scripts/check_docs.py` 全绿。

### 修复:两个面板在构造函数里 `JSON.parse` 一个可能不是 JSON 的 localStorage 值

**现象**:连服务器密码面板与服务器列表面板偶尔整块空白 —— 列表不渲染、收藏不显示,
控制台里是一条 `Unexpected token` / `Unexpected non-whitespace character after JSON`,
而且**清掉 localStorage 就恢复正常**。因为读的是同一个键,两个面板会一起坏。

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
`localStorage` 的寿命比写它的那个构建长。`savedServerList` 与 `favoriteServerList` 是
**追加式**写进去的(`savedServerList` 在每次连接成功时被重写),所以「两个数组被拼在
一起」或者「写到一半被中断」都会留下一个仍然存在、但已经不是合法 JSON 的值。
而这两处解析发生在**组件构造期**:`JSON.parse` 抛出后构造函数没有完成,面板不是显示
空列表,而是根本不渲染。

**可达性**:两个键都由 `Code/skyrim_ui/src/app/services/store.service.ts` 的 `set` 写入,
读取点只有这两个组件(`connect-password.component.ts:149`/`:170`、
`server-list.component.ts:192`)。触发条件不需要用户做任何异常操作:换一个构建、
或在写入过程中关掉游戏就够。

**修法**:`store.service.ts:34-48` 新增
`public getJsonArray<T>(key: string, valueIfNull: T[]): T[]` —— 读出来先判 `null`,再
`try { JSON.parse } catch { return valueIfNull; }`,并且用 `Array.isArray(parsed)` 拒绝
「能解析但不是数组」的值(对象、字符串、数字同样会让 `.find`/`for...of` 出问题)。
两个组件改为调用它,`SavedServer` 接口就近声明在 `connect-password.component.ts:20-24`。
**为什么不就地包一层 `try/catch`**:两处解析的意图完全相同(「这个键要么是一个数组,
要么当它不存在」),把它写进 `StoreService` 才只有一处需要维护;而且下一个读数组的人
不必再想一遍这件事。

**验证**:两处调用点已改为 `getJsonArray<SavedServer>('savedServerList', [])` 与
`getJsonArray<Server>('favoriteServerList', [])`(`Server` 来自
`Code/skyrim_ui/src/app/models/server.ts`);`git grep -n "JSON.parse"` 现在只剩
`client.service.ts:579` 与 `store.service.ts:42` 两处,且都在 `try/catch` 内。
`python Tools/Scripts/check_docs.py` 全绿。

### 修复:认证失败的 JSON 载荷是手拼的,服务端给的一个引号就让整条错误原因消失

**现象**:认证被拒时玩家看到的不是原因,而是「服务器没有理由地拒绝了连接」。服务器其实**把原因送过来了**
(`wrong_version` / `mods_mismatch` / `client_mods_disallowed` / `wrong_password` / `server_full`),
它写在 `ConnectionErrorEvent::ErrorDetail` 里,由 `OverlayService::OnConnectionError` 交给 CEF,
UI 用 `JSON.parse` 读出来。这条载荷是**手工拼接**的字符串:值直接写在引号之间,一个 `"` 就提前结束字符串,
整个文档从此不再是 JSON,`client.service.ts` 只能回落到 `no_reason`。

**根因**(修复前):
```cpp
// Code/client/Services/Generic/TransportService.cpp:675(修复前)
ErrorInfo += fmt::format("\"expectedVersion\": \"{}\", \"version\": \"{}\"", acMessage.Version, BUILD_COMMIT);

// Code/client/Services/Generic/TransportService.cpp:686(修复前)
ErrorInfo += fmt::format("[\"{}\",\"{}\"]", m.Filename.c_str(), m.Id);
```
`acMessage.Version` 是**服务端**的版本字符串,`m.Filename` 是**服务端**回传的 mod 文件名 ——
两个都是本侧无法控制的文本,而且都是刚过线的东西。同一段还有第二处:
`case AR::kClientModsDisallowed:` 里 `if (acMessage.MO2Active)` 只包住了逗号那一行,
`ErrorInfo += "\"MO2\"";` 在 `if` 之外,**无条件**执行。于是只装了 SKSE 的玩家被告知
「请移除 MO2」—— 一个他根本没装的 mod 管理器。服务端早就把两个真实 flag 回传了
(`Code/server/GameServer.cpp:896-897 serverResponse.SKSEActive = acRequest->SKSEActive;` /
`serverResponse.MO2Active = acRequest->MO2Active;`),拼数组时用的却是错的。

**可达性**:这条路径不需要玩家做任何异常操作,也不需要恶意客户端 —— 服务端与客户端的版本号
本来就不同(`BUILD_COMMIT` 取自 `Code/common/BuildInfo.h.in:4`),而版本不一致正是 `wrong_version`
这条分支存在的理由;mod 文件名由对端在 `AuthenticationRequest` 里上报,允许包含文件系统允许的任何字符。
**两端都在本地不可控**:`acMessage.Version` 已被 `Code/encoding/Messages/AuthenticationResponse.cpp:19`
夹到 64 字节,`m.Filename` 已被 `Code/encoding/Structs/Mods.cpp:46` 夹到 260 字节 —— 长度夹住了,
但**内容**没有。

**修法**:新建 `Code/encoding/JsonEscape.h`(`Escape(const TiltedPhoques::String&)`,按 RFC 8259 section 7
转义引号、反斜杠与全部 < 0x20 的字节,其余原样保留),`TransportService.cpp` 的三处拼接改走它。
**为什么是转义而不是拒包**:这些字符串已经过了 `WireLimits::Clamp` 的长度上限,文件名允许包含文件系统
允许的任何字符,而一个需要转义的名字仍然是**必须展示给玩家**的名字 —— 为它丢掉整个包,会把
「显示得不对」升级成「掉线」。

**验证**:`Code/tests/encoding.cpp:882 TEST_CASE("JSON escaping of the authentication error payload", "[encoding.json_escape]")`
覆盖转义表(空串 / 普通文本 / `"` / `\\` / `\b\f\n\r\t` / 其余控制字符走 `\u00xx` / 非 ASCII 原样)、
一个试图提前闭合文档的版本字符串,以及**全部 256 个字节**逐个过一遍:结果里每个 `"` 前面必须有奇数个反斜杠,
且不能残留任何 < 0x20 的字节。`python Tools/Scripts/check_docs.py` 全绿。
### 修复:三种「对端说了算」的读取各自越过了自己的边界

**现象**:三处独立缺陷,共同点都是**读侧按对端给的数走,而写侧早就知道真正的上界**:

1. 一个手搓的认证包把 mod 计数写成 13 位能表达的最大值 `8191`,服务器/客户端就会为 8191 个
   `Mods::Entry`(每项含一个最长 260 字节的文件名)分配内存,然后在包已耗尽的情况下继续读。
2. 队伍里某个玩家说的台词(字幕)到了对端**只剩文本,丢了话题**:客户端把 `TopicFormId` 发出去,
   服务器转发的 `NotifySubtitle` 里这一项恒为 0。
3. 一个 `Status` 不在枚举内的 `RequestQuestUpdate`(线上是裸的 8 位字段,没有范围校验)会让服务器
   把一个**未初始化的 `notify.Status`** 读进日志、再原样转发给全队。

**根因**(修复前):

```cpp
// Code/encoding/Structs/Mods.cpp:38-39(修复前)
const size_t modCount = data & 0xFFFF;
ModList.resize(modCount);   // 13 位读允许 8191,而写侧 :22 封顶 4096

// Code/server/Services/CharacterService.cpp:577-579(修复前)
NotifySubtitle notify{};
notify.ServerId = message.ServerId;
notify.Text = message.Text;      // TopicFormId 从未被写,线上恒为 0

// Code/server/Services/QuestService.cpp:109-113(修复前)
default:
    spdlog::error("... unknown quest status {} ...", __FUNCTION__, notify.Status, ...);
    break;   // 不 return;notify.Status 在这条路径上从未被赋值
```

第三条之所以是不确定值而不是 0:`NotifyQuestUpdate` 有**用户提供的**默认构造函数
(`NotifyQuestUpdate() : ServerMessage(Opcode) {}`),按 C++ 的规则它不算聚合,`NotifyQuestUpdate notify{}`
**不会零初始化** `Stage`/`Status`/`ClientQuestType` 这三个没有默认成员初始化器的字段。

**可达性**:第一条在认证包解析路径上,即**未认证连接可达**(`GameServer::OnConsume` 先解析后认证);
第二条是每个玩家说台词时的正常路径,只是信息被丢掉,不崩;第三条需要一个伪造包 —— 客户端自己的
三个发送点只会写 0..3(其中 `Code/client/Services/Generic/QuestService.cpp:188` 写的是布尔值,
只能得到 0 或 1),所以它只对「对端不守规矩」成立,但那正是这条链要防的。

**修法**:`Mods` 新增 `static constexpr uint64_t kMaxModCount = 4096`(`Code/encoding/Structs/Mods.h`,
放在成员之后,与 `Factions.h`/`ActorValues.h` 同一风格),写侧改用它,读侧 `std::min<uint64_t>(data & 0xFFFF, kMaxModCount)`
—— 两边同宽,读者与写者对流的解释重新一致;`OnSubtitleRequest` 补上 `notify.TopicFormId = message.TopicFormId;`;
`QuestService` 的 `default:` 改为打印**请求里的** `message.Status` 并 `return`,不再把无法映射的状态转发出去。
另外把客户端 `OnQuestUpdate` 的 `default:` 里那句永远不可达的 `spdlog::error`(它在 `break;` 之后)提到 `break;` 之前,
未知状态现在会真的留下一条日志。

**验证**:`Code/tests/encoding.cpp:937 TEST_CASE("Mods count is bounded by the writer bound", "[encoding.mods_count]")`
(4096+7 条 mod 往返后只剩 4096 条;手工写入计数 8191 的包被夹到 4096 且末尾条目的 id 正确)与
`Code/tests/encoding.cpp:980 TEST_CASE("Subtitle topic travels in both directions", "[encoding.subtitle]")`
(两端各自的 `TopicFormId` 往返后保持)。`python Tools/Scripts/check_docs.py` 全绿。
### 修复:插件的会话表只增不减,而它正好压在每次按键的路径上

**现象**:OStim Together 的联机场景开得越多,「场景开始键」越钝 —— 同一次按键要做两遍
`_ownerSessions` 全表遍历;并且每个「加演员」菜单、每个直连会话、每次预检捕获都会往表里
加一条,而整张表只在读档时清空一次。

**根因**(修复前):
```cpp
// plugins/OStimTogether/src/CoopSessionManager.cpp(修复前)
// 三处写入,零处删除
:311        _ownerSessions[sessionID] = std::move(session);   // 直连会话
:423        _ownerSessions[sessionID] = std::move(session);   // 预检捕获
// plugins/OStimTogether/src/AddActorConsentGate.cpp:80
            _ownerSessions[gateID] = std::move(gate);          // 加演员闸门

// 唯一的清空点,只在读档时跑一次
// plugins/OStimTogether/src/main.cpp:216(kPreLoadGame)
            OStimTogether::CoopSessionManager::GetSingleton().Reset();
```
失败路径只翻标志位、不删条目:`CoopSessionManager.cpp:707`(`CancelOwnerSession`)、
`:679`(`StartApprovedOwnerSession` 启动失败)、`AddActorConsentGate.cpp:151`/`:181`
(`PollAddActorConsent` 的两条终态分支)。而
`plugins/OStimTogether/compat/OStimUIConsent/Data/Scripts/Source/OSKSE.psc:87-108` 会一直
`PollAddActorConsent` 到它返回非 0 —— 也就是说**每打开一次加演员菜单就必然命中一条终态
分支**,每条都留下一份永不回收的状态。

**可达性**:两次全表遍历都在按键路径上 —— `plugins/OStimTogether/src/Input.cpp:163` 先调
`CleanupCompletedDirectSessions()`(函数体整个内联在 `CoopSessionManager.h:41-57`),`:165`
再调 `TryGateDirectSceneStart()`,后者在 `CoopSessionManager.cpp:249` 调
`HasPendingDirectIntent()`(`std::any_of` 扫同一张表)。按键路径本来就是每个按键都要过的,
而表长只增不减。

**修法**:把「标 `canceled`」换成「真删」,并让清理循环接受所有已结束的会话 ——
`CleanupCompletedDirectSessions` 改为迭代器循环,条件从
`session.directStartIntent && !active && !canceled && activeThreadID >= 0` 放宽为
`!active && !restarting && !canceled && activeThreadID >= 0`(原来只有直连会话会被收,
预检捕获的那条永远留着),命中即 `_activeOwnerByThread.erase(...)` +
`_ownerSessions.erase(it)`;`CancelOwnerSession` 在锁内先取 `snapshot`、再写
`snapshot.canceled = true`、然后 `_ownerSessions.erase(it)`(所有调用方与所有路由查询对
「找不到」与「已取消」的处理完全一致,所以留着一个 `canceled` 条目只是把它多留一整个存档);
`StartApprovedOwnerSession` 的启动失败分支同样改删;`PollAddActorConsent` 的三条终态分支
各 `_ownerSessions.erase(gateIt)`。另外 `RouteOwnerPayload` 在「线程映射还在、会话已经不在
了」时从 `return false` 改为 `return pending`:线程确实是本侧捕获过的,它的原始流量必须继续
被吞掉,不能因为条目被删就转发给所有 peer。

**验证**:`python Code\plugins\tools\plugin_patches.py verify` → `PATCH VERIFY OK` /
`11 patch(es) apply to their pinned commits`;`check` → `PATCH CHECK OK`;`apply` 幂等
(`patches: 0 applied, 11 already present`)。三个文件的 `{`/`}` 平衡都是 0
(`CoopSessionManager.cpp` 1274 行 323/323、`CoopSessionManager.h` 231 行 37/37、
`AddActorConsentGate.cpp` 200 行 42/42)。`python Tools/Scripts/check_docs.py` 全绿。
### 修复:插件的最高频日志点开着同步刷盘,而它就在主线程的动画路径上
**现象**:`AnimSyncTogether` 在 10 分钟的会话里写了 **43271 行日志(7.3 MB)**,其中 39834 行(92.1%)来自
`src/AnimationInputProbe.cpp:76`,3407 行(7.9%)来自 `src/AnimationProbe.cpp:228`;最忙的一秒写了 290 行。
这两条都是 `SKSE::log::info`,无条件触发,而且**每一条都要同步刷盘**。
**根因**(修复前):
```cpp
// plugins/AnimSyncTogether/src/main.cpp:25-26(修复前)
log->set_level(spdlog::level::trace);
log->flush_on(spdlog::level::trace);   // trace 是最低等级 ⇒ 每条消息都立刻落盘
```
`flush_on(N)` 的参数是**刷盘起点**而不是日志等级,所以 `flush_on(trace)` 等于 `flush_on(info)` 等于
「每条都刷」:同一份日志里零条 trace,43271 行全是 `[info]`,也就是 600 秒里 43271 次阻塞刷盘(约 72 次/秒)。
```cpp
// plugins/AnimSyncTogether/src/AnimationInputProbe.cpp:76-80(修复前)
SKSE::log::info("AnimInput actor=00000014 localPlayer=true event='{}' result={} tracked={}", name, result, tracked);
```
**可达性**:两条日志分别挂在本地玩家的动画图输入 vfunc(`RE::VTABLE_PlayerCharacter[3]`,
`src/AnimationInputProbe.cpp:27` 的 `write_vfunc(0x1, PlayerNotifyAnimationGraph)`)和动画图事件 sink 上,
都在**主线程**同步执行,触发点是每次移动、攻击、受击、换装备。`basic_file_sink_mt` 每次刷盘都要抢锁并进内核,
直接落在用户要求的「不得让加载、过场动画、与 NPC/场景交互变慢」上。本仓主程序早已对同一件事定过调
(`Code/client/TiltedOnlineApp.cpp:75-80`:`flush_every(1s)` + `flush_on(warn)`,注释写明「per-frame logging
不得变成 per-frame file I/O」),插件走的却是相反配置。两条日志的内容本身也不值得留在默认等级:
`tracked=` 的 39834 行**全部是 `false`**,`localPlayer=` 的 3407 行**全部是 `true`**、`remoteConnection=` 全为 `0`
—— 它们从未描述过一个真正被同步的事件。
**修法**:`src/main.cpp:28-33` 改成主程序同款策略(`set_level(info)` + `spdlog::flush_every(std::chrono::seconds(1))`
+ `flush_on(spdlog::level::warn)`),info 行最多晚一秒落盘、warn 及以上仍立刻落盘;`AnimationInputProbe.cpp:76`
与 `AnimationProbe.cpp:228` 降为 `SKSE::log::debug`;同步改 `README.md` 的 `## Logging` 节,把 `AnimInput`/`AnimEvent`
标为 debug 并说明 Helmet Toggle 那几条低频诊断仍在默认等级。
**验证**:交付形式是 `Code/plugins/patches/AnimSyncTogether/0001-stop-flushing-every-log-line-and-demote-the-two-per-event-markers.patch`
(该插件是子模块,CI 的 `git submodule update --init --force --recursive` 会丢弃工作树改动)。`python Code\plugins\tools\plugin_patches.py verify`
→ `PATCH VERIFY OK` / `12 patch(es) apply to their pinned commits`;同目录下 `check` → `PATCH CHECK OK`。
`git -C plugins/AnimSyncTogether grep -n "flush_on\|flush_every\|set_level"` 命中 `src/main.cpp:28`/`:32`/`:33`,
`flush_on(spdlog::level::trace)` 已无命中。`python Tools/Scripts/check_docs.py` 全绿。
### 修复:二十处「对端说了算」的浮点直接进了引擎,而 NaN 之后每一次比较都不再成立

**现象**:一个法术效果强度、一次经验同步、一发弹道的落点与朝向,发送方都可以写成 `NaN` 或 `±Inf`。
这二十个字段读完就交给引擎,中间没有任何一处检查 —— 而 `NaN` 不是「很大的数」,它是「没有数」:
它参与算术之后仍然是 `NaN`,参与比较永远为假,所以它落到哪个引擎字段上,哪个字段就再也不可比较、
不可夹紧、不可存储。`Inf` 是同一件事的另一半。

**根因**(修复前):

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

这三个家族各有各的落点,但都落在引擎里:

1. `Magnitude` 经 `Code/client/Services/Generic/MagicService.cpp:450 data.fMagnitude = acMessage.Magnitude;`
   进 `MagicTarget::AddTargetData`,再交给 `pActor->magicTarget.AddTarget(data, ...)`。
2. `Experience` 经 `Code/client/Services/Generic/CharacterService.cpp:1094 OnNotifySyncExperience` 直接进
   `PlayerCharacter::AddSkillExperience`(`Code/client/Games/Skyrim/PlayerCharacter.cpp:74-87`),
   那个函数把值原样传给 `RealAddSkillExperience`,再回读 `newExperience - oldExperience` —— 两个 `NaN` 相减还是 `NaN`。
3. 那八个浮点填的是 `Projectile::LaunchData`(`Code/client/Games/Skyrim/Projectiles/Projectile.h:18-50`,
   `static_assert(sizeof(LaunchData) == 0xA8)`),`Projectile::Launch` 把 `fPower` 写进弹丸实体,
   其余进 `pProjectile` 的弹道解算。

同族的既有先例只有两处:`Code/encoding/Messages/RequestHealthChangeBroadcast.cpp:20` 的 `std::isfinite` 夹紧
(`kMaxHealthDelta = 100000.f`)与 `Code/encoding/Structs/TimeModel.cpp:11-15` 的 `ClampFloat`。
两者都在 `Code/encoding/` 里,`WireLimits.h` 的头部注释本来就写着这些读侧钳制该怎么放。

**修法**:`Code/encoding/WireLimits.h` 新增 `inline float ClampFinite(const float aValue) noexcept`,
非有限值**替换**为 `0.f` 而不是夹进某个区间 —— 没有任何区间能让 `NaN` 变得有意义,而这批字段里也没有哪一个
像血量增量那样带「正负即判决」的语义。六个 `.cpp` 的第 2 行加 `#include <WireLimits.h>`,二十处读取改成
`X = WireLimits::ClampFinite(Serialization::ReadFloat(aReader));`。
**有限的合法值必须原样通过**:发送方真的能产生 `99999.f` 的落点与 `-1234.5f` 的角度,
把它们夹进某个范围就等于每一发弹道都被悄悄挪位。

**验证**:`Code/tests/encoding.cpp` 新增 `TEST_CASE("A non finite wire float is replaced where it is read", "[encoding.finite_floats]")`
—— 先直接断言 `ClampFinite` 对 `1.5f`/`-1.5f`/`0.f` 恒等、对 `NaN`/`±Inf` 给 `0.f`,
再对六个消息各做一次「整包往返 + 每个接收对象先写哨兵 `1.f`」,最后用一条 `REQUIRE(received == sent)`
证明合法弹道原样通过。`python Tools/Scripts/check_docs.py` 全绿。
### 修复:血量广播的读取被钳制块挤掉了,于是这个包一直在送未初始化的血量增量
**现象**:一个 `RequestHealthChangeBroadcast` / `NotifyHealthChangeBroadcast` 到达时,包里 `DeltaHealth`
的那四个字节**从不被消费**;紧接着的 `if (!std::isfinite(DeltaHealth))` 检查的是**未初始化的成员** ——
这两个结构体的 `float DeltaHealth;` 没有默认成员初始化器(`RequestHealthChangeBroadcast.h:25`、
`NotifyHealthChangeBroadcast.h:25`),基类 `ClientMessage`/`ServerMessage` 只初始化 `m_opcode`,而工厂是
`TiltedPhoques::MakeUnique<T>()` + `DeserializeRaw`。
**根因**(修复前):
```cpp
// Code/encoding/Messages/RequestHealthChangeBroadcast.cpp:11-24(修复前)
Id = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF;

// Never trust the wire: a NaN or an infinity would follow this value into every
// arithmetic operation the receiver performs on it.
if (!std::isfinite(DeltaHealth))
    DeltaHealth = 0.f;
else
    DeltaHealth = std::min(std::max(DeltaHealth, -kMaxHealthDelta), kMaxHealthDelta);
```
写侧一直是 `Serialization::WriteFloat(aWriter, DeltaHealth);`(`:8`),而 `git grep -n ReadFloat` 在这两个
文件上**零命中**:读取那一行是在加入钳制的那次提交里被替换掉的,钳制因此作用于一个与线上无关的值。
**为什么测试没拦住**:`Code/tests/encoding.cpp:620 TEST_CASE("Health change broadcast wire bounds", "[encoding.health_change]")`
只断言钳制的结果(`NaN → 0`、`-1e30 → -kMaxHealthDelta`、`1e30 → kMaxHealthDelta`)。钳制对一个恰好为 0 的
未初始化内存是恒等,所以那三条断言在缺陷上照样通过;`windows.yml:618` 的门禁只检查退出码与 `All tests passed`,
也区分不出「读进来了」和「碰巧是对的」。
**可达性**:服务端 `Code/server/Services/ActorValueService.cpp:77-100 OnHealthChangeBroadcast` 把这个值原样
写进 `ActorValuesList[24]` 并放进转发包;客户端 `Code/client/Services/Generic/ActorValueService.cpp:286-312`
把它加进目标血量,结果不再为正时对非玩家 actor 调 `pActor->Kill()`。发送侧
`Code/client/Services/Generic/ActorValueService.cpp:189-202` 一直在送真实的伤害/治疗(小伤害攒进
`m_smallHealthChanges`、`RunSmallHealthUpdates` 每 250 ms 批量发),所以这条路径在联机里是热的。
**修法**:两个文件的 `DeserializeRaw` 各补回一行 `DeltaHealth = Serialization::ReadFloat(aReader);`
(`Code/encoding/Messages/RequestHealthChangeBroadcast.cpp:16`、`Code/encoding/Messages/NotifyHealthChangeBroadcast.cpp:16`),
位置在 `Id` 之后、钳制之前 —— 钳制逻辑与既有断言都不用改。
用例同时给四个接收对象先写一个**有限哨兵**(`nan.DeltaHealth = 1.f;`、`clamped.DeltaHealth = 1.f;`、
`notification.DeltaHealth = 1.f;`、`received.DeltaHealth = 1.f;`):钳制对 1.f 是恒等,于是断言成立的**唯一**
原因就是那一行读取存在 —— 删掉它,`REQUIRE(nan.DeltaHealth == 0.f)` 立刻变成 `1.f == 0.f`。
**验证**:`git grep -n ReadFloat Code/encoding/Messages/RequestHealthChangeBroadcast.cpp Code/encoding/Messages/NotifyHealthChangeBroadcast.cpp`
现在各命中 `:16`;`git grep -c "Seeded with a finite sentinel" Code/tests/encoding.cpp` 为 4。
另外对 `Code/encoding/Messages/**` 与 `Code/encoding/Structs/**` 的全部 `.cpp` 做了「写侧标识符 − 读侧标识符」
差集扫描,过滤掉临时变量赋值(`uint64_t tmp; aReader.ReadBits(tmp, N); Field = tmp & MASK;`)造成的误报后,
**只剩这两个文件**;反向(读了但从不写)为 0 命中。`python Tools/Scripts/check_docs.py` 全绿。
### 修复:一个插件的最高频日志在默认配置下同步刷盘,而它一次采样最多打 45 条
**现象**:`MorphSyncTogether` 在 10 分钟的会话里写了 **1864 行日志(348 KB)**,其中 1433 行(76.9%)来自
`src/AppearanceProbe.cpp:814`、187 行(10.0%)来自 `src/UdpTransport.cpp:195`、94 行来自
`src/MorphSyncService.cpp:469`、93 行来自 `src/TngSync.cpp:177`;这些行**每一条都要同步刷盘**。
**根因**(修复前):
```cpp
// plugins/MorphSyncTogether/src/main.cpp:21-22(修复前)
spdlog::set_level(spdlog::level::trace);
spdlog::flush_on(spdlog::level::trace);   // trace 是最低等级 ⇒ 每条消息都立刻落盘
```
`flush_on(N)` 的参数是**刷盘起点**而不是日志等级,所以 `flush_on(trace)` 等于 `flush_on(info)` 等于
「每条都刷」:同一份日志里零条 trace、零条 debug,1864 行 = `info` 1675 + `warning` 187 + `error` 2,
也就是 586 秒里 1864 次阻塞刷盘。那 1433 行更不是「一秒一条」,而是「一次采样一簇」——
`_verbose` 闸门的三处默认值全是开的,而一次探测的明细行数实测 min 1 / avg 24.71 / **max 45**:
```cpp
// plugins/MorphSyncTogether/src/AppearanceProbe.cpp:812-819(闸门默认值改了,循环本身没动)
if (_verbose) {
    for (const auto& detail : data.details) {
        SKSE::log::info("MST APPEARANCE DETAIL scope={} label=\"{}\" actor={:08X} {}", scope, label, actor->GetFormID(), detail);
    }
}
```
同一函数在它之前 18 行已经打过一条 `MST APPEARANCE CHANGE`(`src/AppearanceProbe.cpp:771-787`,
一行 14 个字段,含 `details=` 的**个数**),只在哈希变化时触发,实测 36 行。也就是说同一份状态被
描述了两遍:一遍是总结,一遍是逐元素展开,而后者 1433 行里 `scope=` 与 `actor=` 各只有一个取值
(全是 `LOCAL` / `00000014`)、distinct payload 只有 131 个。
**可达性**:`MorphSyncService::SyncLoop` 按 `SyncIntervalMs`(默认 1000 ms)切片轮询,每轮经 `QueueTick()`
把 `TickOnGameThread()` 投到**游戏主线程**,而它第一件事之一就是
`src/MorphSyncService.cpp:290 AppearanceProbe::GetSingleton().ProbeLocalPlayer(player);`。
所以那次「一次 45 行」的循环连同 45 次 `basic_file_sink_mt` 阻塞刷盘全在主线程的一个 tick 里;
同一 tick 内的两条广播日志与那 187 条 `result=not-connected` 的 warn 也各自同步落盘。
这直接落在用户要求的「不得让加载、过场动画、与 NPC/场景交互变慢」上。本仓主程序早已对同一件事定过调
(`Code/client/TiltedOnlineApp.cpp:75-80`:`flush_every(1s)` + `flush_on(warn)`,注释写明「per-frame logging
不得变成 per-frame file I/O」),插件走的却是相反配置。
**修法**:`src/main.cpp:22` 的 `flush_on(spdlog::level::trace)` 改为主程序同款策略
(`spdlog::flush_every(std::chrono::seconds(1))` + `flush_on(spdlog::level::warn)`),info 行最多晚一秒落盘、
warn 及以上仍立刻落盘;`set_level(trace)` **保留不动**(该插件全部 3 个 trace 站点都在「已经决定不发」的
分支上,抬等级是行为变化)。`_verbose` 的三处默认值一起翻成 false —— `src/AppearanceProbe.h:164`、
`src/Config.h:39`、`package/Data/SKSE/Plugins/MorphSyncTogether.ini` 的 `Verbose=0`(ini 覆盖前两者,
必须同改),并在 `README.md` 的 `## Expected log` 节说明排查时把它设回 1。
**验证**:交付形式是 `Code/plugins/patches/MorphSyncTogether/0003-stop-flushing-every-log-line-and-disable-the-appearance-detail-log.patch`
(该插件是子模块,CI 的 `git submodule update --init --force --recursive` 会丢弃工作树改动)。`python Code\plugins\tools\plugin_patches.py verify`
→ `PATCH VERIFY OK` / `13 patch(es) apply to their pinned commits`;同目录下 `check` → `PATCH CHECK OK`。
`git -C plugins/MorphSyncTogether grep -n "flush_on\|flush_every\|set_level"` 命中 `src/main.cpp:25`/`:26`,
`flush_on(spdlog::level::trace)` 已无命中。`python Tools/Scripts/check_docs.py` 全绿。
### 修复:客户端可控的 id 与索引在五个使用点上都没被判定过「它真的有效」
**现象**:一次远程施法、一次远程打断施法、一发远程弹道到达时,发送方在包里选中的是哪个 actor、
用哪只手施法、弹丸基类存不存在,接收方都不判定就往下用;覆层的「传送到队员」按钮则会在
第 65536 个玩家之后把人送到**另一个人**身边。这五处都不是崩溃,而是「对端说什么就是什么」:
`Cast<Actor>` 在运行版本的 RTTI 没有映射时**返回空指针**(`Code/client/Games/Skyrim/RTTI.h:17-27`),
`GenerateMagicCasters` 只在引擎肯给的时候才填槽(`Code/client/Games/Skyrim/Actor.cpp:915-924`),
`CastingSource` 是 `int32_t`(`Code/encoding/Messages/NotifySpellCast.h:22`、`NotifyInterruptCast.h:20`、
`NotifyProjectileLaunch.h:42`、`ProjectileLaunchRequest.h:45`),而读侧只判了上界、没判下界。

**根因**(修复前):

```cpp
// Code/client/Services/Generic/MagicService.cpp:149-156(修复前,OnNotifySpellCast)
Actor* pActor = Cast<Actor>(pForm);
pActor->GenerateMagicCasters();                       // Cast 可以是空的

// Only left hand casters need dual casting (?)
pActor->casters[CS::LEFT_HAND]->SetDualCasting(acMessage.IsDualCasting);   // 槽可以是空的

if (acMessage.CastingSource >= 4)                     // int32_t:-1 进不来
    return;

pSpell = pActor->magicItems[acMessage.CastingSource]; // 于是负数索引到了 magicItems[4]
```

```cpp
// Code/client/Services/Generic/MagicService.cpp:284-286(修复前,OnNotifyInterruptCast)
Actor* pActor = Cast<Actor>(pForm);
pActor->GenerateMagicCasters();                       // 同上一处,没有判空
```

```cpp
// Code/client/Services/Generic/CombatService.cpp:125 / :140(修复前,OnNotifyProjectileLaunch)
launchData.pProjectileBase = TESForm::GetById(cProjectileBaseId);   // 可以是空的

launchData.eCastingSource = (MagicSystem::CastingSource)acMessage.CastingSource;   // 没有范围判定
```

```cpp
// Code/encoding/Messages/TeleportRequest.h:20 / TeleportRequest.cpp:12(修复前)
uint16_t PlayerId{};                                        // 字段 16 位
PlayerId = Serialization::ReadVarInt(aReader) & 0xFFFFFFFF; // 读侧按 32 位掩码
```

前四处各自都能落到一次无效解引用或一次越界索引上,而它们要的只是「这个值有效吗」这一句判断;
第五处的字段宽度则与它要装的东西不是一回事:玩家 id 由 `Code/server/Game/Player.cpp:1-30` 的
`static constinit std::atomic<uint32_t> s_counter{1};` 单调发号、**从 1 开始、不回收、不回绕**,所以
第 65536 个连接是一个普通连接;而 `Code/encoding/Messages/NotifyPartyInfo.h:25 Vector<uint32_t> PlayerIds{};`
送到覆层的 id 本来就是 32 位(`Code/client/Services/Generic/PartyService.cpp:126`/`:168` 取,`:137-141` 转发给 CEF),
`Code/client/Services/Generic/OverlayClient.cpp:170 request.PlayerId = aEventArgs->GetInt(0);` 又是一次 `int → uint16_t`,
两次窄化之后服务端 `Code/server/Services/OverlayService.cpp:90` 用它 `GetById` —— 指向的是**另一个**玩家。

**判据**:`Cast<T>` 会返回空、`GenerateMagicCasters` 会留下空槽、`CastingSource` 是带符号的、
玩家 id 是 32 位且不回收 —— 这四条都是**被调用方写明的**事实,而五个使用点都假设了反面。
同族的对照组就在同一个文件里:`Code/client/Games/Skyrim/Projectiles/Projectile.cpp:56-68` 对
`Cast<Actor>(arData.pShooter)` 判了空,`Code/client/Services/Generic/CombatService.cpp:112-116` 对
`Cast<TESObjectCELL>` 也判了空 —— 也就是说这一族在本仓是**多数守住了、少数没守住**,而不是没有范式。

**修法**:

1. `Code/client/Services/Generic/MagicService.cpp:151-157`(`OnNotifySpellCast`)与 `:302-306`(`OnNotifyInterruptCast`)
   在 `Cast<Actor>` 之后加 `if (!pActor) { spdlog::warn(...); return; }`。
2. 同文件 `:161-167` 再加一道 `if (!pActor->casters[CS::LEFT_HAND])` —— `GenerateMagicCasters` 的循环体是
   `if (casters[i] == nullptr) casters[i] = Cast<ActorMagicCaster>(GetMagicCaster(static_cast<CS>(i)));`,
   引擎不给就仍然是空。
3. `:172` 与 `:274` 的范围判定补上下界:`if (acMessage.CastingSource < 0 || acMessage.CastingSource >= 4)`;
   `Code/client/Services/Generic/CombatService.cpp:146-152` 把弹道路径上那句 C 风格强转换成同一段判定 + `static_cast`。
4. `Code/client/Services/Generic/CombatService.cpp:127-131` 在 `pProjectileBase` 赋值之后判空(弹丸基类不存在时
   `Projectile::Launch` 拿不到任何东西可发)。
5. `Code/encoding/Messages/TeleportRequest.h:23` 把 `uint16_t PlayerId{};` 加宽成 `uint32_t PlayerId{};`,
   `Code/client/Services/Generic/OverlayClient.cpp:170` 改成 `static_cast<uint32_t>(aEventArgs->GetInt(0))`;
   读侧 `& 0xFFFFFFFF` 与写侧 `WriteVarInt` 都不用动,线格式对已存在的小 id 完全不变。

**验证**:`Code/tests/encoding.cpp` 新增 `TEST_CASE("A teleport request carries the whole player id", "[encoding.teleport_request]")`,
对 `0`/`1`/`0xFFFF`/`0x10000`/`0x10001`/`0xFFFFFFFF` 各做一次整包往返并断言 `received.PlayerId == aPlayerId` ——
`0x10000` 与 `0x10001` 正是 16 位字段会塌成同一个值的两个 id。其余四处在 `Code/client/Services/Generic/` 里,
C++ 编译由 CI 执行(`windows.yml`)。`python Tools/Scripts/check_docs.py` 全绿。
### 修复:另外四个插件的最高频日志点也都在同步刷盘
**现象**:`DAVSyncTogether`、`IEDSyncTogether`、`TradeTogether`、`OStimTogether` 四个插件里,每一条日志都会在
产生它的那个线程上**同步写进内核**,而它们的等级设置默认开着 `trace` —— 也就是说这两层里所有 trace/debug 调用点
都是活的,并且每一条都要等磁盘。这与 §67/§68 已经修好的 `AnimSyncTogether`、`MorphSyncTogether` 是同一行配置。

**根因**:`spdlog::flush_on(N)` 的参数是**刷盘起点**而不是日志等级 —— 它表示「level >= N 的消息打完立刻 flush」,
所以 `flush_on(trace)` 覆盖全部消息。四个插件的初始化里各写着这一行,而写法只有一处不同:
`plugins/DAVSyncTogether/src/main.cpp:28`、`plugins/IEDSyncTogether/src/main.cpp:23`、
`plugins/OStimTogether/src/main.cpp:51` 是 `spdlog::flush_on(spdlog::level::trace);`,
`plugins/TradeTogether/src/main.cpp:20` 是 `logger->flush_on(spdlog::level::info);` ——
`info` 恰好是这四个插件里唯一一直有输出的等级,所以它比另外三处更重。
四段初始化里都没有 `spdlog::flush_every(...)`,而本仓既有的策略(§67 起)是每秒 flush 一次、warn 及以上立即落盘。

**判据**:刷盘起点是一个数,而等级是另一个数,两个都要读;而 `flush_every` 是**默认 logger** 的全局设置,
`flush_on` 是**某一个 logger** 的设置,所以 `TradeTogether` 那一份不能照抄另外三份 ——
它的 `flush_on` 必须留在 `logger->` 上,`flush_every` 必须写在 `spdlog::set_default_logger(...)` **之后**。

**修法**:四个补丁各只改一段初始化,`set_level(trace)` 全部保留(抬等级是行为变化,与本次缺陷无关):

```cpp
// plugins/DAVSyncTogether/src/main.cpp:26-32(修复后,IEDSyncTogether 与 OStimTogether 逐字同款)
spdlog::set_default_logger(std::move(log));
spdlog::set_level(spdlog::level::trace);
// flush_on(trace) wrote every line to disk before the emitting thread
// could continue, and the level above enables trace by default.
// Warnings stay immediate; the rest is flushed once a second.
spdlog::flush_every(std::chrono::seconds(1));
spdlog::flush_on(spdlog::level::warn);
```

```cpp
// plugins/TradeTogether/src/main.cpp:19-26(修复后,唯一需要区别对待的一份)
logger->set_level(spdlog::level::trace);
logger->flush_on(spdlog::level::warn);
logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [TradeTogether] [%l] [%s:%#] %v");
spdlog::set_default_logger(std::move(logger));
spdlog::flush_every(std::chrono::seconds(1));
```

没有降任何一条日志的等级:这四个插件里「每次收发都会打」的 warn(`IEDSyncTogether/src/STRPMAdapter.cpp:224`/`:514`/`:539`、
`OStimTogether/src/AddonBridge.cpp:268`/`:277`/`:283`/`:484`/`:494`/`:510`/`:587`/`:598`、
`TradeTogether/src/Trade.cpp:1055`/`:1063`/`:1080`/`:1110`/`:1165`、`DAVSyncTogether/src/DAVNetworkService.cpp:144`/`:156`)
都是「对端可控输入触发」的路径,降级只会让现场剩下一片沉默,而新策略下它们本来就是立即落盘的。

**验证**:交付形式是四个补丁(`Code/plugins/patches/DAVSyncTogether/0002-stop-flushing-every-log-line-to-disk.patch`、
`Code/plugins/patches/IEDSyncTogether/0001-stop-flushing-every-log-line-to-disk.patch`、
`Code/plugins/patches/TradeTogether/0001-stop-flushing-every-log-line-to-disk.patch`、
`Code/plugins/patches/OStimTogether/0005-stop-flushing-every-log-line-to-disk.patch`)—— 这四个插件都是子模块,
CI 的 `git submodule update --init --force --recursive` 会丢弃工作树改动。
`python Code\plugins\tools\plugin_patches.py verify` → `PATCH VERIFY OK` / `17 patch(es) apply to their pinned commits`(13 → 17);
同目录下 `check` → `PATCH CHECK OK`;`plugin_snapshot.py verify` → `SNAPSHOT OK`;
四个 `src/main.cpp` 上 `flush_on(spdlog::level::trace)` 已无命中,`flush_every(std::chrono::seconds(1))` 各命中一处
(`DAVSyncTogether:31`、`IEDSyncTogether:26`、`OStimTogether:54`、`TradeTogether:26`)。`check_docs.py` 全绿。
### 修复:三个插件的对端可控缓存只增不减,以及一个「门禁全绿却装不上」的补丁

**现象**:两件事。其一,`IEDSyncTogether`、`MorphSyncTogether`(含 `TngSync`)、`OStimTogether` 的四张远端缓存表全部以「对端在载荷里自选的字符串」为键,唯一清空点是读档,会话内没有上限 —— 而它们每 750ms/1s 会被遍历一次做演员名字扫描,所以表越大,游戏线程上的那次扫描越贵;其二,上一版提交的 `OStimTogether/0006` 补丁从 pin 出发装不上,`plugin_patches.py apply` 直接中止,而 `verify` 与 `check` 两条门禁当时都是绿的。

**根因**:

一、四张表的键由对端写,而全仓没有 `erase`:

```cpp
// plugins/IEDSyncTogether/src/SyncService.cpp(修复前)
const auto instanceID = ReadField(packet, "id");   // :232,长度零检查
auto& snapshot = _remoteSnapshots[*instanceID];    // :262,insert-or-default
// plugins/MorphSyncTogether/src/MorphSyncService.cpp(修复前)
auto& snapshot = _remoteSnapshots[sender];         // :667
auto& snapshot = _remotePubicSnapshots[sender];    // :742
// plugins/MorphSyncTogether/src/TngSync.cpp(修复前)
auto& state = _remoteStates[sender];               // :221
```

IED 的 `id=` 是它自己的包装字段,由对端整包替换(`plugins/IEDSyncTogether/src/StrTransport.cpp:302-309` 拼的是 `IEDST|v1|id={}|from={}|relay=0|{}`,而 `UdpTransport.cpp:680` 只查前缀);Morph/TNG 的 `from=` 来自 `plugins/MorphSyncTogether/src/UdpTransport.cpp:219-222` 的对端显示名,过一道 `SanitizeField`(`:26-37`,只换 `|`/CR/LF、空则 `"Player"`,**没有长度上限**)。同族的 `_assemblies` 是唯一有 prune 的表(`MorphSyncService.cpp:356-366`,15 秒过期),旁边两张快照表没有。

二、补丁的基线选错了:

```powershell
git apply --check --cached  # verify:对 index,即 pin
git apply --check --reverse # check:对工作树,只问「是否已应用」
git apply                   # apply:CI 的那条路,从 pin 出发按文件名顺序落
```

`0006` 是拿 pin 当基线生成的,于是把 `0003` 的 `AddonBridge.cpp`/`AddonBridge.h` 两段 hunk 又抄了一遍;对 pin 自洽、对「`0003` 已应用」的工作树则找不到锚点。

**判据可机械执行**:「这个键是谁选的,而它什么时候被删掉」—— 键由对端选、又没有任何删除路径 = 缺陷;补丁门禁则要问「这条门禁跑在哪种工作树上」—— 只有从 pin 出发的 `apply` 能发现基线选错。

**修法**:

一、五张表各自设上限,满了丢包而不是丢表:

```cpp
// plugins/IEDSyncTogether/src/STRPMAdapter.h(修复后)
static constexpr std::size_t kMaxRemoteStates = 64;
// plugins/MorphSyncTogether/src/MorphSyncService.h(修复后)
static constexpr std::size_t kMaxRemoteSnapshots = 64;
static constexpr std::size_t kMaxRemotePubicSnapshots = 64;
// plugins/MorphSyncTogether/src/TngSync.h(修复后)
static constexpr std::size_t kMaxRemoteStates = 64;
```

先判 `contains` 再判上限:已有的键永远还能更新,被拒的只是**新**键。

二、`FreeScenePhaseSync` 的两张表按各自语义设界:

```cpp
// plugins/OStimTogether/src/FreeScenePhaseSync.h(修复后)
static constexpr std::size_t kMaxRemotePreps = 32;
static constexpr std::size_t kMaxReadyTokens = 256;
```

`_remotePreps` 是有序队列,满了 `erase(_remotePreps.begin())` 丢掉的正是最不可能还在飞的那条;`_lastReadyToken` 没有顺序语义,满了整体清空只值一包。

三、重写 `0006`,把基线从 pin 改成「`0003` 已应用」:先把已应用的工作树 `git add -A` 进 index,再 `git diff`,落盘后 `git reset` 把 index 还原成 pin,让 `verify` 仍然有意义。补丁从 6567 B 变成 5780 B,少掉的正是 `0003` 的 `ClearRemoteState` 两段 hunk。

**验证**:

四处修改都在插件层,交付方式是补丁(插件是子模块,本仓不拥有、也不能 push):

- `Code/plugins/patches/IEDSyncTogether/0002-bound-the-live-peer-table-and-the-decoded-object-list.patch`(2026-10-10 重写:原版打在 `src/SyncService.{h,cpp}` 上,而这两个文件在本插件的 `CMakeLists.txt` 里从来没进过 `SOURCES`,补丁打上去等于没打;现在改打真正被编译的 `src/STRPMAdapter.{h,cpp}` 与 `src/LocalIEDState.{h,cpp}`)
- `Code/plugins/patches/MorphSyncTogether/0004-bound-the-remote-snapshot-tables.patch`
- `Code/plugins/patches/OStimTogether/0008-bound-the-phase-sync-tables.patch`
- `Code/plugins/patches/OStimTogether/0006-coalesce-the-addon-state-reapply-and-bound-the-remote-caches.patch`(重写)

`python Code\plugins\tools\plugin_patches.py verify` → `PATCH VERIFY OK` / `22 patch(es) apply to their pinned commits`(19 → 22);`apply` → **`patches: 22 applied, 0 already present`**(从干净工作树,这是 CI 的那条路径);`check` → `PATCH CHECK OK`;`plugin_snapshot.py verify` → `SNAPSHOT OK`;`check_transport_compat.py` → `TRANSPORT COMPATIBILITY OK`;`check_docs.py` 全绿。另有一项更强的复核:22 个补丁从 pin 依次应用后,九个被改文件与手工编辑的版本**逐字节相同**。

**教训**:「这个键合法吗」和「这个键能有几个」是两个问题 —— `SanitizeField`/`IsSafeAddonToken` 把形状管得很好,条数没人管。

**教训**:同族的表要用同一把尺子量 —— `_assemblies` 有 15 秒 prune 而旁边两张快照表没有,缺陷的形状是「同一处清理里,谁缺席」。

**教训**:门禁要跑在它实际会跑的那种工作树上 —— `verify` 对 index、`check` 对工作树,两者都看不出「补丁的基线选错了」。
### 修复:插件层一次节点变化就起两个线程,以及三张只增不减的对端可控缓存

**现象**:两件事,都发生在 OStim 多人场景里。其一,场景每换一个动画节点,`OStimTogether` 就为每个非玩家 actor 起 2 个 detached 线程(250ms 与 1100ms 各一个),节点切得越密线程起得越多;其二,插件的四张缓存键全部由对端在载荷里自选,而它们只在读档时清空一次,会话内没有上限。

**根因**:

一、重放调度没有合并,而两个调用点都在游戏线程上:

```cpp
// plugins/OStimTogether/src/AddonBridge.cpp(修复前)
schedule(std::chrono::milliseconds(250), "T250");
schedule(std::chrono::milliseconds(1100), "T1100");
```

调用点一是 `plugins/OStimTogether/src/AddonStateRepair.cpp:86`(在 `OStim::ThreadInterface::registerNodeChangedListener` 的回调里对每个非玩家 actor 调一次),节点变化来自 `plugins/OStimNG/skse/src/Core/Thread.cpp:337` 的 `EventUtil::invokeListeners(nodeChangedListeners);`;调用点二是 `plugins/OStimTogether/src/OStimBridge.cpp:3941`(停止场景时对同一批 actor 再调一轮)。

二、缓存键由对端选,而没有任何删除路径:

```cpp
// plugins/OStimTogether/src/AddonBridge.cpp(修复前)
auto& cached = _remoteOverlays[actor->GetFormID()][cacheKey];      // :531
_remoteObjects[actor->GetFormID()][key] = CachedObjectState{...};  // :617
// plugins/OStimTogether/src/ActorResolverSTRPM.cpp(修复前)
_strpmRemoteByName[name] = formID;   // :662
_resolved[key] = resolved.chosen;    // :928
```

`IsSafeAddonToken` 只约束每个键的长度与字符集,不约束**数量**;唯一的清理点是 `ClearRemoteState()`,而它只在 `plugins/OStimTogether/src/main.cpp:227` 的 `kPreLoadGame` 被调一次。

**判据可机械执行**:「这个键是谁选的,而它什么时候被删掉」—— 键由对端选、又没有任何删除路径 = 缺陷。

**修法**:

一、重放合并成一个可自愈的守卫:

```cpp
// plugins/OStimTogether/src/AddonBridge.cpp(修复后)
constexpr auto kReapplyGuardWindow = std::chrono::seconds(5);
...
const auto now = std::chrono::steady_clock::now();
std::scoped_lock lock(_stateMutex);
const auto it = _reapplyPending.find(actorFormID);
if (it != _reapplyPending.end() && it->second > now)
{
    return;
}
_reapplyPending[actorFormID] = now + kReapplyGuardWindow;
```

末阶段(`"T1100"`)在 `sleep_for` 之后 `_reapplyPending.erase(actorFormID)`。用过期时间而不是只用一个 set:如果这中间游戏读了档,`kPreLoadGame` 会重置十个子系统,那个线程可能再也跑不到 erase,守卫会把 actor 永久锁死。

二、两张远程缓存按 actor 设上限:

```cpp
// plugins/OStimTogether/src/AddonBridge.cpp(修复后)
constexpr std::size_t kMaxRemoteChannelsPerActor = 16;
constexpr std::size_t kMaxRemoteObjectsPerActor = 16;
```

先判 `contains` 再判上限:已有的 channel 永远还能更新,被拒的只是**新**键。

三、两个解析缓存设界(`ActorResolver.h` 的 `kMaxResolvedEntries = 512` / `kMaxRemoteNames = 512`):名字缓存满了跳过这一个名字(它是「省一次扫描」的缓存,满了只是多扫一次),解析缓存满了直接清空(它没有任何读者)。

**验证**:

两处修改都在插件层,交付方式是补丁(插件是子模块,本仓不拥有、也不能 push):

- `Code/plugins/patches/OStimTogether/0006-coalesce-the-addon-state-reapply-and-bound-the-remote-caches.patch`
- `Code/plugins/patches/OStimTogether/0007-bound-the-resolver-caches.patch`

`python Code\plugins\tools\plugin_patches.py verify` → `PATCH VERIFY OK` / `19 patch(es) apply to their pinned commits`(17 → 19);同目录下 `check` → `PATCH CHECK OK`;`apply` → `patches: 0 applied, 19 already present`;`python Tools/Scripts/check_docs.py` 全绿。插件是 CMake 工程、本机不编译(`plugins/OStimNG/skse/AGENTS.md` 明写不要构建 C++/CMake),所以这一批的证据是补丁门禁 + 逐字命中。

**教训**:后台线程的数量不能由对端或事件频率决定。

**教训**:「有校验」不等于「有上限」—— 判据要问「这个键是谁选的,而它什么时候被删掉」,不是「这个键合法吗」。

**教训**:合并用的守卫必须能自己过期,否则一次中途读档就能把它变成死锁。
### 修复:管理员判定由对端自称的 id 决定,拒绝的调用仍然改状态,以及一个 `assert` 就长在收包路径上

**现象**:三件事,都可以由一个普通玩家触发。其一,任何玩家在聊天框打 `/settime 12 00` 都能改全服时间,
不需要是管理员;其二,管理员打 `/settime 99 99` 会看到「成功」,而服务器的时间倍率被这一条非法参数**改掉**了;
其三,服务端在 `releasedbg` 构建下收到一个「指向本地缓存里没有的字符串 id」的包会直接 `abort`。

**根因**:

一、`CommandService::OnSetTimeCommand` 的管理员分支拿 `acMessage.Packet.PlayerId` 去比对管理员列表:

```cpp
// Code/server/Services/CommandService.cpp(修复前)
const auto cPlayerId = static_cast<uint32_t>(acMessage.Packet.PlayerId);
for (const auto session : GameServer::Get()->GetAdminSessions())
{
    const auto* pAdmin = PlayerManager::Get()->GetByConnectionId(session);
    if (!pAdmin)
        continue;
    if (pAdmin->GetId() == cPlayerId)   // ← 「某个管理员的 id 等于我自称的 id」
```

`Packet.PlayerId` 是**发送者自己填的**:客户端在 `Code/client/Services/Generic/OverlayClient.cpp:163`
把 `m_transport.GetLocalPlayerId()` 写进 `SetTimeCommandEvent`,再由
`Code/client/Services/Generic/CommandService.cpp:35 request.PlayerId = acEvent.PlayerId;` 原样上线。
而每个玩家的 id 都是公开的 —— `NotifyPlayerJoined` 会把它广播给所有人。于是判定退化成「我知道某个管理员的
id,我就能当管理员」。对照同函数下面的队长分支:`pPartyService->IsPlayerLeader(acMessage.pPlayer)` 用的是
**真指针**,`PartyService.cpp:50` 从队伍表里读 `LeaderPlayerId` —— 同一个 handler 里两种判定,一种用真身份、
一种用对端字段,这个不一致本身就是判据。

同一段循环还有第二个缺陷:`GetByConnectionId` 对「还没有 player row 的会话」返回 `nullptr`,而管理员的
connection id 在 `GameServer.cpp:909`(`m_adminSessions.insert(aConnectionId)`)就进了集合,player row 要到
`:990`(`GetPlayerManager().Create`)才建,中间有三条提前返回(模组策略不符 `:960`、重复认证 `:996`、脚本拒绝
入服 `:1013`)都不会回头清理这个集合 —— 集合只在断线时 `:612 m_adminSessions.erase(aConnectionId);`。
`pAdmin` 判空写成了 `continue`,所以这一段不会崩;真正的问题是**判空之后仍然拿 `GetId()` 去比**,而
「某个管理员的 id 等于我自称的 id」这个条件与「我是那个管理员」无关。

二、`CalendarService::SetTime` 把赋值放在了校验之前:

```cpp
// Code/server/Services/CalendarService.cpp(修复前)
m_dateTime.m_timeModel.TimeScale = aScale;          // ← 先写
if (aHours >= 0 && aHours <= 23 && aMinutes >= 0 && aMinutes <= 59)
{
    ...
    return true;
}
return false;                                        // ← 拒绝了,但上面已经写了
```

`TimeScale` 是**整个世界**的时间倍率(`DateTime::GetDeltaTime` 拿它乘每帧的毫秒数,`Code/common/DateTime.cpp:35`)。
于是「时/分不合法」这条拒绝路径反而把倍率改了,而且**绕过了这个字段在别处的两道界**:
`SetTimeScale`(`Code/server/Services/CalendarService.cpp:148`)要求 `0..1000`,线上读取
(`Code/encoding/Structs/TimeModel.cpp:47 ClampFloat(TimeScale, kMaxTimeScale)`)也要求 `0..1000`,
只有这一处裸写。更糟的是 `aScale` 可以是非有限值:`NaN` 会顺着 `GetDeltaTime` 进 `DateTime::Update`,
最后落在 `static_cast<uint32_t>(m_timeModel.Time / 24.f)`(`Code/common/DateTime.cpp`)—— 把 `NaN` 转成无符号整数
是未定义行为。两个调用点(`Code/server/Services/CommandService.cpp:58`、`:75`)**都没有看返回值**,
一律回 `kSuccess`,于是玩家看到「成功」而什么都没发生。

判据是同族的三处写法:`SetDate`(`:96-113`)先判后写、`SetTimeScale`(`:148`)先判后写、
`GameServer_Bindings.cpp:35-37` 的 Lua `SetTime` 绑定**先夹再调**
(`aHours = std::max(0, std::min(aHours, 23));` … `aScale = std::max(1.0f, std::min(aScale, 100.0f));`)。
同一个函数在脚本绑定里先夹、在聊天命令里不夹 —— 「多数地方守住了」不是「这一族守住了」。

三、`CachedString::Deserialize` 对「收到一个本地缓存里没有的 id」直接 `assert(false)`:

```cpp
// Code/encoding/Structs/CachedString.cpp(修复前)
const auto cValue = StringCache::Get()[cId];
if (cValue)
    *this = *cValue;
else
    assert(false);   // ← 对端可控的输入,断言 = 进程终止
```

`CachedString` 出现在 `ActionEvent::EventName` / `TargetEventName`,而 `ActionEvent` 经
`ReferenceUpdate.cpp:43 Actions[i].ApplyDifferential(aReader);` 从 `ClientReferencesMoveRequest` 的
`Map<uint32_t, ReferenceUpdate> Updates` 读进来 —— 这是**每帧都在跑的活流量**,不是死代码。
`assert` 的活性:`xmake.lua:27` 只有 `mode.debug`/`mode.releasedbg`/`mode.release`,全仓没有 `NDEBUG`,
而 `.github/workflows/windows.yml:11-14` 的 `build-mode` 默认值是 `releasedbg` —— **发布构建里这个断言是活的**。
这条路径整条是 `noexcept`(`DeserializeRaw` 是 `noexcept` 虚函数),所以没有「抛出去让人接」这个选项。

**判据可机械执行**:「授权判定读的是对端给的字段,而不是这次连接本身的身份」= 缺陷;
「函数先写成员再校验参数,失败分支不撤销」= 缺陷;「`assert` 的输入来自线上」= 缺陷。

**修法**:

1. 管理员判定改成问这次连接本身:`if (GameServer::Get()->GetAdminSessions().contains(acMessage.pPlayer->GetConnectionId()))`。
   `m_adminSessions` 存的就是 connection id(`GameServer.h:124`),而 `acMessage.pPlayer` 由
   `GameServer.cpp:254-279` 的 handler 生成器保证非空(它先 `GetByConnectionId` 再 `trigger`)。
   这一改同时消掉了那个循环 —— 不再需要「遍历 + 判空 + continue」。`Packet.PlayerId` 仍是线上字段,
   但服务端不再读它。
2. `CalendarService::SetTime` 改成先判后写,并把倍率的界写全:

```cpp
// Code/server/Services/CalendarService.cpp(修复后)
if (!std::isfinite(aScale) || aScale < 0.f || aScale > TimeModel::kMaxTimeScale)
    return false;

if (aHours < 0 || aHours > 23 || aMinutes < 0 || aMinutes > 59)
    return false;

m_dateTime.m_timeModel.TimeScale = aScale;
```

   界用的是 `TimeModel::kMaxTimeScale` 而不是再写一个字面量 `1000.f` —— 这个常量本来就是「服务端手工设置倍率时
   拒绝的上界」(注释原文)。`std::isfinite` 判的是「这是不是一个数」:`NaN` 夹进任何区间都还是 `NaN`,所以是非有限
   值一律拒绝,与 `WireLimits::ClampFinite` 同一策略。顺带把该文件里裸写的 `floor(` / `ceil(` 补上 `std::`
   并显式 `#include <cmath>` —— 这个头此前是靠传递包含进来的。
3. 两个调用点尊重返回值,并把「参数不合法」与「没有权限」分成两个结果:

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

   追加在末尾而不是插在中间:枚举按字节上线(`NotifySetTimeResult.cpp:5-6` 写 8 位、`:14` 读 8 位),
   而 `Code/encoding/Opcodes.h:57-59` 的注释要求「只许追加」;`Code/plugins/tools/check_transport_compat.py`
   也只约束 opcode 的相对顺序,不看枚举值。客户端 `OnSetTimeResult` 据此分两条提示:
   `kNoPermission` → 「只有管理员/私服队长能改时间」,其余 → 「那不是时间,用 /settime HH MM,小时 0-23、分钟 0-59」。
4. `assert(false)` 换成把名字留空:

```cpp
// Code/encoding/Structs/CachedString.cpp(修复后)
*this = TiltedPhoques::String();
```

   空名字不是「静默吞掉」:消费这个字段的 `Code/server/Game/Animation/ActionReplayCache.cpp:66` 与 `:81`
   本来就是拿名字去查固定表(`AnimationEventLists::kExitSpecial.contains(...)` / `kIgnore.contains(...)`),
   查不到即返回 `false`,空串走的是同一条路。修掉的是**进程终止**,不是这个字段的语义。

**验证**:

`Code/tests/encoding.cpp` 新增两个用例:

- `TEST_CASE("A refused set time names which refusal it was", "[encoding.set_time_result]")`(`:1232-1262`)
  先断言三个枚举值的字节依次是 `0`/`1`/`2`(证明 `kInvalidTime` 是追加的),再对三者各做一次整包往返并
  `REQUIRE(received == sent)`。
- `TEST_CASE("A cached string with an unknown id reads as empty", "[encoding.cached_string]")`(`:1264-1304`)
  先造一个「有 id、但 id 不在缓存里」的包,断言名字变成空串而不是进程死;再往缓存里放一条真实字符串,
  断言同一个字段仍然解析得出内容 —— 证明兜底没有把命中路径一起吃掉。

`Code/server/Services/CommandService.cpp` 与 `Code/server/Services/CalendarService.cpp` 是服务端 C++,
本机不编译(`docs/PITFALLS.md` §0/§1:Windows/MSVC 只由 GitHub CI 验证);
`python Tools/Scripts/check_docs.py` 全绿。

**教训**:一个字段只要发送者能自己填,它就不能用来回答「你是谁」。

**教训**:「拒绝」必须发生在任何写入之前 —— 否则「失败」这个词只是返回值的样子。

**教训**:`assert` 是给「代码自己错了」用的;对端可控的输入落在它上面,就是把进程的存活交给对端。

### 修复:1.5.3 / 1.5.16 / 1.5.23 上「裸体 NPC 修复」会把已经穿好护甲的 NPC 每趟重穿一次

**判据是一条内部不一致**:同一个函数经同一个 id 解析,而**出货的十张地址映射表里只有七张带这一行**。

- `ExtraContainerChanges::Data::GetArmor` 不直接调引擎,而是经 AE 地址库 id **16113** 解析
  (`Code/client/Games/Skyrim/ExtraData/ExtraContainerChanges.cpp:36`)。
- 1.5.x 运行时加载的是 `version-<版本>.bin` 加一张翻译表
  `versionlib-ae-to-se-<版本>.map`(两者 id 命名空间不同,`VersionDb::Load`)。**十张表各对应一个
  1.5.x 运行时,其中三张没有 16113**:`1-5-3-0`、`1-5-16-0`、`1-5-23-0`;另外七张有,且**值随
  构建而不同**(1.5.39 = `0x1e7750`、1.5.50–1.5.62 = `0x1e7760`、1.5.73 起 = `0x1e7570`)——
  这正说明它不可能用一条 override 覆盖。
- 未映射的 id **不报错**:`VersionDbPtr::GetPtr` 在 legacy 运行时退回 `GetUnresolvedStub()`
  (七个字节 `48 31 C0 / 0F 57 C0 / C3`,即 `xor rax,rax; xorps xmm0,xmm0; ret`),也就是
  **每次调用返回 0**,并打一条 warn。于是 `GetArmor(32)` 在那三个运行时上**恒为 `nullptr`**。
- 而它唯一的读者是 `Actor::IsWearingBodyPiece()`,再往上唯一的读者是
  `InventoryService::RunNakedNPCBugChecks`:
  `if (!pActor->IsWearingBodyPiece() && pBase) { warn("naked check fires"); pActor->EquipOutfit(); }`
  —— 于是一个**本来就穿着护甲**的远程 actor 会在每一趟检查里被判成「没穿」并把装备重穿一遍。
  这不是崩溃,是稳定的行为错误(每次重穿都会改变装备状态,进而影响同步)。

**根因:这套记账只覆盖一个运行时。** `Tools/missing_1_5_97_ids.txt` 声明了 1.5.97 的 13 个缺口,
`Tools/Scripts/check_docs.py` 拿这份声明和**那一张** map 对照 —— 另外九张 map,也就是另外九个
**本仓库随包提供地址库、因而声称支持**的 1.5.x 运行时,从来没有被任何门禁读过。
同一个错误在这个仓库里已经是**第五次**——§113(一个插件)、§116(一个目录)、§119(一个文件)、
§126(一份手写清单),本轮是**一张 map**;机制每次都一样:**扫描范围写成发现地点,而不是规则**。

**为什么缺这一行:不是漏了同步,是那三个库根本没有这条 id。** 这条要查清才能知道该不该补。
map 是一张**翻译表**:一个 AE id 命名一个 pre-AE id,而每个 pre-AE 库把那个 id 拼成自己构建里的 RVA。
七张有这一行的 map 正好证明了这个规则 —— **七张的值逐一等于 `version-<构建>.bin[15873]`**:

| 运行时 | `version-<构建>.bin[15873]` | map 里的 `16113` | 一致 |
| --- | --- | --- | --- |
| 1.5.39 | `0x1e7750` | `0x1e7750` | ✅ |
| 1.5.50 / 1.5.53 / 1.5.62 | `0x1e7760` | `0x1e7760` | ✅ |
| 1.5.73 / 1.5.80 / 1.5.97 | `0x1e7570` | `0x1e7570` | ✅ |
| **1.5.3 / 1.5.16 / 1.5.23** | **`15873` 不存在** | **不存在** | —— |

而 pre-AE 的 id 空间是**演进**而不是只增:`version-1-5-3-0.bin` 命名了 **791,088** 个 id,其中
**15,376** 个 1.5.97 没有;同时它**没有** 1.5.97 有的 **2,962** 个,`15873` 正在其中。
⇒ **没有可翻译的值,也没有任何 override 能提供它**(值必须逐构建量:上表三档值已经证明),
**重新生成也补不上**;`0x1e7570` 甚至不是 1.5.3 那个库列出的任何偏移。
所以这三行**故意留空**,而不是填一个邻居的值。

**修复一:缺口被声明,而不是被猜。** 新增 `Tools/missing_ids_by_runtime.tsv`:13 条 `*` 行
(每个随包 pre-AE 运行时的公共缺口)+ 3 条按运行时的行,每条写明原因(上面那条结论)。
**猜一个 RVA 比缺一个更危险**:缺了,调用落到零返回桩上 —— 行为错,但**进程是活的**;
猜错,就是**调用一个无关函数**。

**修复二:后果就地修。** 新增 `ExtraContainerChanges::Data::CanQueryArmor()`,用
`VersionDb::Get().FindAddressById(16113)` **直查**(与 `GarbageCollector::Get` 同一范式,理由也一样:
「没映射」和「映射到了但为空」在 `GetArmor` 的返回值上无法区分);调用点改成

```cpp
if (ExtraContainerChanges::Data::CanQueryArmor() && !pActor->IsWearingBodyPiece() && pBase)
```

重穿是一次**修复**动作,只允许建立在**确定**的答案上;问不出答案时不动 actor,比每趟给穿好衣服的
人重穿一套好。代价是**那三个运行时上「裸体 NPC 自动修复」不再生效** —— 这是有意的降级,恢复它需要
那三个 RVA,已登记为 `docs/FOLLOW-UP.md` 第 5 条。

**修复三:新增门禁 `Tools/Scripts/check_map_coverage.py`。** 六条:

| 判据 | 为什么 |
| --- | --- |
| `version-*.bin` 与 `versionlib-ae-to-se-*.map` 必须成对 | 只有库 = 一个 id 都翻译不了;只有表 = 没有运行时会加载它 |
| 每张 map 的缺口(`collect_codebase_ids()` 里它不带的 id)**恰好等于**该运行时的声明 | 少声明 = 16113 那个形状;**多声明 = 声明已过期**,读者仍被告知这个运行时解析不了某个其实已修的 id |
| 每条按运行时的行必须在 `JUDGED` 表里有已判读的绑定 | 「stub 的 0 落到调用点会怎样」不可判定,所以逐个判读并登记;新加一条没有判读的行**失败**而不是静默通过 |
| 该守卫函数体里写的 id 必须等于**同一个文件经 `POINTER_SKYRIMSE` 解析的 id** | 同一件事写了两遍就必须一致,否则守卫问的是一个 id、调用点用的是另一个 |
| **十张 map 必须对同一个 AE id 命名同一个 pre-AE 符号**(按 pre-AE id 反查比对,**不比值**) | 值跨构建本来就不同(上表三档),所以比值什么都查不出来;而「这条 AE id 名的是哪个符号」是**跨构建不变量**,一行译错就露出来 |
| **每张 map 必须是单射**(两个 AE id 不能指向同一个 RVA) | 两个库都是单射,生成器也**拒绝写出**这样的表;而两个 AE id 共用一个 RVA 正是「照抄了邻居的值」的样子 |

第 5 条是这一轮技术上最值钱的一条:它**不需要任何声明**就能验十张表,并且能抓到「值合法、
但指向另一个函数」这类缺陷 —— §122 已经证明过「地址合法远不足以说明地址正确」。

**验证**:

- `python Tools/Scripts/check_map_coverage.py` → `MAP COVERAGE CHECK OK` /
  `10 shipped pre-AE map(s) match their declared id gap, 1 judged binding(s) still guard the ids
  declared per runtime`。第 5 条在出货数据上**逐对全过**:9 张兄弟表 × ~3,697 个共享 id,
  **0 处符号不一致、0 处「该 RVA 不在对应库里」**;第 6 条十张全为单射。
- **变异测试(五个变异,逐条被指名;每个变异都先确认已落到磁盘)**:
  1. 删掉 `1-5-23-0` 那一行 → `versionlib-ae-to-se-1-5-23-0.map does not carry 1 id(s) ... [16113]`
     —— 这就是**原始缺陷本身**;单独复现过一次(还缺 `1-5-3-0` 那行时门禁只报那一条)。
  2. 把 `1-5-16-0` 那行的 id 改成 map 里**有**的 `16040` → 同时报
     `id 16040 is declared missing on 1-5-16-0 but no binding is judged for it` 与
     `declares [16040] missing from 1-5-16-0, but ... carries them`(两个方向都被抓到)。
  3. 把 `1-5-3-0` 改成不存在的运行时名 `1-5-999-0` → `row for runtime 1-5-999-0, which no shipped
     map matches (...)`。
  4. 把守卫里的探测 id 改成 `16114` → `no CanQueryArmor body naming id 16113` +
     `no judged binding contributed anything`;把 `GetArmor` 的 `POINTER_SKYRIMSE` id 改成 `16114`
     (守卫不动)→ `CanQueryArmor guards id 16113, but the file resolves [16005, 16114, 16142, 16143]
     through POINTER_SKYRIMSE`。
  5. **把 `1-5-39-0` 表里 `16118` 的值换成同表 `16123` 的值**(一个**合法**偏移,所以「地址对不对」
     这类检查看不见)→ `id 16118 names pre-AE id 15883 (0x1e9310), but
     versionlib-ae-to-se-1-5-97-0.map names pre-AE id 15878 (0x1e7ba0) for the same AE id`;
     同一个变异还让该表不再单射,第 6 条同时指名两个 id 共用一个 RVA。
  变异 4/5 还顺带证明了另一件事:把一个 id 改成任何一张表都没有的值之后,门禁在**十张表上全部**报
  「the client resolves [16114] and nothing declares」——**新引入一个不可解析的 id 也会被挡下**。
- 门禁已接进 CI 的两条触发列表(`push` 与 `pull_request` 各 47 条路径,`contract` 作业 30 个步骤),
  并复核 `python Code/plugins/tools/check_workflow_shell.py` → `WORKFLOW SHELL CHECK OK`(51 个块)、
  `yaml.safe_load` 解析通过。
- C++ 由 GitHub CI 编译验证(§0/§1:本机不编译);改动的三个文件是
  `Code/client/Games/Skyrim/ExtraData/ExtraContainerChanges.h`/`.cpp` 与
  `Code/client/Services/Generic/InventoryService.cpp`(后者多一个 `#include <ExtraData/ExtraContainerChanges.h>`)。

**教训**:**「随包提供了 N 份同名产物」本身就是 N 条主张,而不是一条。** 一张表的覆盖度被声明、
被门禁比对,不能推出另外九张也一样 —— 恰恰相反:它们的**生成路径不同**(这里是从 1.5.97 逐构建
翻译),所以差异是常态。判据要按「出货了几份」数,而不是按「当初是在哪一份上发现的」写。

**教训(同轮第二条)**:**跨版本不可比的量做不了跨版本判据,但它背后的不变量可以。**
十个 RVA 互不相同,所以「表与表的值一致」永远为假、也永远查不出错;而「这条 AE id 命名的是
哪个符号」在所有构建上都必须相同 —— 换一个问法,同一份数据就从「无从比较」变成「33,000 次比对
零分歧」。**先找不变量,再写判据。**

### 修复:两分支结构体布局的守护只写在 checklist 里 —— 而 checklist 自己点出的那一处始终没断言

**判据是一条「同一批源码、两个目标」的内部不一致**:客户端是**同一份源码编译两次**
(`Code/client/xmake.lua` 的 `build_client(..., {"SKYRIM_TARGET_LEGACY=1"})`),而两个目标
**对引擎字段的位置并不一致**。`docs/PITFALLS.md` §2 称这是全仓最危险的地方,已经咬过**三次**,
三次形状完全相同:**一个分支采用了另一个目标的值,于是静默读邻居**。它给出的防线是
「改完更新两分支的 `static_assert`」—— 而这条防线此前只是 §0 的一个**勾选项**,
**没有任何东西在跑它**。§117/§127 已经各付过一次账:**写下来的约定不被执行,地位等同注释**。

- 全仓 `SKYRIM_TARGET_LEGACY` 条件块 **16 处**,其中**改变成员布局**的 **7 处**。
- **`docs/FOLLOW-UP.md` 自己记着的那个缺口仍然在**:`ExtraDataList` 在 1.5.x 下**没有虚表**
  (1.6.x+ 有),于是每个**内嵌**它的结构(`Actor`、`TESObjectREFR`、`TESObjectCELL` …)整体后移
  8 字节 —— 而它**没有任何一条断言点它的名字**:尺寸只被 `TESObjectREFR` 的 `sizeof`
  **间接**覆盖。也就是说,改动会在**离编辑点一个结构之外**才被发现,而且**只在那个断言恰好
  消费了这次变化时**才被发现。
- 同一批事实的**第二处**更险:`inactive` 的偏移被写了**五遍** —— 两个 pad 锚点
  (`0x680 - 0x200 - sizeof(void*)` 与 `0x690 - 0x210 - sizeof(void*)`)、两个 `static_assert`、
  一个 `kInactiveOffset`,以及 `SkyrimVM64.cpp` 里**为诊断这个偏移而生**的探针
  (`ReadInt32At(apThis, 0x680/0x690)`)与日志各一遍。而 §2 记着这个字段的代价:
  **读错一位就落到邻居字段上、看到非零、从此不再执行 `World::Update()`**,
  连带连接一起静默死掉(连接 lambda 被排队却永远不被 drain)。五份拷贝里
  **只有 `static_assert` 那一份是被检查的**。

**修复**:

1. **补上 `ExtraDataList` 的两分支断言**(`offsetof` 数据成员 + `sizeof`):legacy
   `data@0x0 / bitfield@0x8 / sizeof 0x18`,AE `data@0x8 / bitfield@0x10 / sizeof 0x20` ——
   差值就是那个虚表指针。这些数是**声明本身的算术**(`BSExtraData*` + `Bitfield*` +
   `BSRecursiveLock`(两个 `LONG`/`DWORD` = 8 字节),AE 再加 vptr),不是量出来的。
2. **把 `GameVM.h` 的两个候选值命名**(`kVirtualMachineOffsetLegacy/Modern`、
   `kInactiveOffsetLegacy/Modern`),pad 锚点、`kInactiveOffset` 与断言全部**由它们推导**,
   再加一条 `static_assert(offsetof(SkyrimVM, inactive) == kInactiveOffset<本分支候选>)`
   把命名值钉在结构体上 —— 改一个候选而字段没跟着动,构建就失败。
3. **`SkyrimVM64.cpp` 的两处探针与日志改用同一批名字**。理由是一句话:诊断「偏移对不对」的
   仪器**必须读的正是结构体被编译时用的那两个数**,否则它会报告一次**它并没有做过的探测** ——
   这与 v1.1.9 里 `--generate-config` 那处「错误信息指错原因」是同一类。
4. **新增门禁 `Tools/Scripts/check_branch_layouts.py`**,两条规则:

| 规则 | 为什么 |
| --- | --- |
| 目标相关的条件块若改变某结构的成员,**该文件里必须有一条 `static_assert` 点到那个类型的名字**(直接 `T` 或限定 `Outer::T`) | 断言写在 `#endif` 之后也能钉住「这次编译的那个分支」,够用;不够用的是**什么都不点这个类型** —— 这正是 `ExtraDataList` 的形状 |
| 每个 `k*Offset*` 常量必须**由 `offsetof()` 算出**或被某条 `static_assert` **点到名字** | 它是结构体已经算过的那个数的第二份拷贝;`SkyrimVM` 就是为什么(五份拷贝、只有一份被检查) |
| 防退化:扫描到的条件块为 0、或没有任何一处声明成员,都**失败** | 否则改一次宏名就能让门禁静默空转却照样打印 OK |

**验证**:

- `python Tools/Scripts/check_branch_layouts.py` → `BRANCH LAYOUT CHECK OK` /
  `7 target-dependent layout(s) pinned, 8 file(s) scanned`(8 = 提到该宏的客户端文件数)。
- **变异测试(三个变异,逐个确认已落到磁盘,逐个被精确指名)**:
  1. **删掉刚补的 `ExtraDataList` 断言块**(= 还原 FOLLOW-UP 记着的原缺口)→
     `ExtraDataList.h:39: the #ifndef SKYRIM_TARGET_LEGACY branch changes ExtraDataList's layout
     (members ['~ExtraDataList']) and no static_assert in this file names ExtraDataList`;
  2. 把 `TESObjectCELL::ReferenceData` 的**两条** `sizeof` 断言改名(限定名路径)→
     `TESObjectCELL.h:32: ... changes ReferenceData's layout (members ['unk0', 'capacity', ...])
     and no static_assert in this file names ReferenceData`;
  3. 把 `GameVM.h` 两条「常量 ↔ 候选值」的断言换成不点名字的写法 →
     `GameVM.h: kInactiveOffset ... is a second copy of an offset the struct already computes and
     nothing ties it back`。
- **变异测试同时暴露了门禁自己的一个盲点,并已修掉**:第一版把 `ExtraDataList` 那一分支读成
  **「没有成员」**(因为它是 `virtual ~ExtraDataList();` —— 虚函数不在我当时的「成员」模式里),
  于是变异 1 **什么都没报**。这正是 §113.4 的规矩(先确认变异真的生效)救回来的一次:
  **一个分支靠有没有虚表来改布局,而我的规则只认数据成员**。现已把 `virtual` 声明计入布局
  (它恰恰是移动后面每个成员 8 字节的那件事),门禁的布局计数随之 6 → 7。
- 门禁已接进 CI 的两条触发列表(`push` 与 `pull_request` 各 49 条路径,`contract` 作业 31 个步骤),
  并复核 `python Code/plugins/tools/check_workflow_shell.py` → `WORKFLOW SHELL CHECK OK`、
  `yaml.safe_load` 解析通过。
- C++ 由 GitHub CI 编译验证(§0/§1:本机不编译)。改动的文件是
  `Code/client/Games/Skyrim/ExtraData/ExtraDataList.h`、
  `Code/client/Games/Skyrim/Misc/GameVM.h`、`Code/client/SkyrimVM64.cpp`;
  两个目标都会编译到这些断言(`check_library_scan.py` 保证 legacy 目标仍在构建里)。

**教训**:**「记在清单上」和「有人在跑」是两件事,而清单越长越像前者已经成立。**
§0 的勾选项、`docs/FOLLOW-UP.md` 的「已知但未做」,两处都**准确地把缺陷写下来了** ——
写下来并没有让缺陷消失,也没有让它的下一次出现被挡住。**凡是「下次改这里时记得…」的句子,
都要问一句:不记得会怎样?** 本轮的答案是:一个后续改动会在离编辑点一个结构之外、且只在恰好
被消费时,才被发现。

**教训(同轮第二条)**:**诊断代码是代码,不是注释 —— 它重复的任何数字都必须与原处绑死。**
`SkyrimVM64.cpp` 那段日志存在的唯一目的就是区分「偏移对」和「偏移错」,而它把两个偏移
**又抄了一遍**;一旦它们漂开,这段日志会带着一个**像是证据**的数字去指错方向。
判据不是「日志写得对不对」,而是「它读的数和结构体编译时用的是不是同一批」。

### 修复:服务器要求玩家**安装**一个他必须**删除**的 mod —— 方向编码用了 0,而 0 是合法 id

**判据是一条「用一个真实数据字段当哨兵值」的内部矛盾。** mods-mismatch 回包是
**一个列表由两次比较拼成**(客户端多出来的 + 服务器多出来的),所以每个条目必须说明自己来自哪一次。
它此前用 `Id == 0` 表示「你必须安装」。而 `0` 是一个**真实存在的 id**:

- 标准 mod:load-order 索引 `0` 就是第一个插件(`standardId`);
- **lite(ESL)mod:`liteId` 从 0 开始发号** ——
  `Code/components/es_loader/ESLoader.cpp` 逐字 `uint16_t liteId = 0x0;` … `plugin.m_liteId = liteId; liteId += 0x0001;`,
  而 `TESFile::Setup(uint16_t aLiteId)` 逐字 `m_formIdPrefix = 0xFE000000 + (m_liteId * 0x1000);`
  —— 也就是说**第一个 ESL 的 liteId 就是 0**(form id 前缀 `0xFE000000`)。
  客户端那一侧正是用它:`Mod::GetId()` 逐字 `return IsLite() ? liteId : standardId;`,
  认证包里逐字 `entry.Id = pMod->GetId();`(`Code/client/Games/TES.h` / `TransportService.cpp`)。

链条于是闭合:服务器 `IsMoPoActive()` 时,对「客户端有、服务器没有 ⇒ **必须删除**」那一组
`modsToRemove.ModList.push_back(mod);` —— **原样回传客户端自己的条目**,于是第一个 ESL 的
`Id` 就是 `0`;而 UI 逐字 `mismatched.filter(mod => mod[1] === '0')` 把它归进**安装**组。
结果:**一个第一号 ESL 不在服务器上的客户端,会被要求去「安装」一个它明明有、而且必须删掉的 mod,
并且永远不会被告知删掉它。** 这与 v1.1.9 修掉的 MO2 那条同类(玩家被要求处理一个并不存在的问题),
但方向相反 —— 那次是「多说了」,这次是「说反了」。

**修复:**

1. **方向改用不可能与真实 id 相撞的标记。** `Code/encoding/Structs/Mods.h` 新增
   `kInstallMarker = 0xFFFF` / `kRemoveMarker = 0xFFFE`,并**静态断言**它们大于
   `kMaxLiteId = 0x0FFF`(liteId 是 form id 的中间十二位,标准 id 是引擎填的 `uint8_t`)——
   也就是「哨兵必须落在 id 值域之外」这件事从此由编译器执行,而不是由注释说明。
2. **两次比较分成两个列表。** `GameServer.cpp` 里 `modsToRemove` / `modsToInstall` 各建一份,
   标记**按列表写一次**(不再逐条推断含义);顺带修掉那条**本身就不准**的日志 ——
   它把整份列表说成 "installed on the client",而其中一半恰恰是「服务器有、客户端没有」,
   现在分别列出两组。
3. **标记不进 UI。** `TransportService.cpp` 把它**翻译成词**(`"install"` / `"remove"`)再写进 JSON,
   因为 UI 是看不到 `Mods.h` 的那一侧;而 `error.service.ts` 与 mock 改用
   `[filename, action]`(`action: 'install' | 'remove'`),并且**两个词都显式比较** ——
   `'remove'` 不再写成 `!== 'install'`,否则一个没预料到的词会被静默当成「删除」。

**新增门禁** `Tools/Scripts/check_ui_contract.py`。overlay 是 CEF 浏览器,客户端与它之间
**没有类型、没有 schema、没有编译器**,只有拼出来的 JSON 文本;两条词汇表跨过这条边界,而
**此前没有任何东西同时看这三处**(C++ / 语言文件 / TS)。三条判据:

| 判据 | 为什么 |
| --- | --- |
| C++ 能发的每个错误码,必须在**每一个**语言文件里有 `SERVICE.ERROR.ERRORS.<CODE>`,并且出现在 UI 的 `ErrorEvent` 联合类型里 | UI 逐字用 `'SERVICE.ERROR.ERRORS.' + error.error.toUpperCase()` **拼键**,所以缺一条就是**把原始键名显示给玩家**;而少一个语言就只在那一种语言里坏掉 |
| mods-mismatch 的方向词在**两侧都必须是恰好** `{install, remove}` | 一侧多一个词 = 那个词永远匹配不上;一侧少一个词 = 那一类被静默丢掉(而 `!==` 写法会把它静默归错类) |
| 防退化:语言文件 < 2 个、或一个错误码都没扫到、或联合类型读不出来 ⇒ **失败** | 否则改一次目录名或载荷形状,门禁就会静默空转却照样打印 OK |

**验证**:

- `python Tools/Scripts/check_ui_contract.py` → `UI CONTRACT CHECK OK` /
  `7 error code(s) reachable from C++, each with a catalog entry in every language and a member in
  the UI union; direction vocabulary bound on both sides`。
  顺带查清一件**已死的**东西:上游那条「你的安装不是纯原版」警告在本仓库早已**改为只记日志**
  (`DiscoveryService.cpp` 的 `IsDefaultModlist` 结果只进 `spdlog::info`,注释写明理由),
  于是 `non_default_install` 这个码**已经不可达**,而联合类型成员与 14 份语言文件里的
  `SERVICE.ERROR.ERRORS.NON_DEFAULT_INSTALL` 都还在 —— 门禁把这件事作为**提示**打印出来
  (不判失败:去掉那条警告是有意的,字符串可能回来)。
- **变异测试(五个变异,逐个先确认已落到磁盘,逐个被精确指名)**:
  1. **把客户端的方向词改回数字 id**(= 还原本轮修掉的原始缺陷)→
     `the mods-mismatch payload carries the direction words [], expected ['install', 'remove']`;
  2. 在 C++ 里加一个**没有翻译**的新错误码 `kicked_by_admin` →
     `SERVICE.ERROR.ERRORS.KICKED_BY_ADMIN: ... 14 language(s) have no entry: cs.json, de.json, ...`
     + `is not in the ErrorEvent union`;
  3. 从联合类型里去掉 `wrong_version` → `error "wrong_version" is sent by ... but is not in the
     ErrorEvent union`;
  4. UI 改成比较 `'add'` → `the UI switches on ['add', 'remove'], expected ['install', 'remove']`;
  5. **只从 `de.json` 删掉 `WRONG_PASSWORD` 一项** →
     `SERVICE.ERROR.ERRORS.WRONG_PASSWORD: ... 1 language(s) have no entry: de.json`
     —— **只指名那一个文件**(这是「每一个语言」这条判据的关键方向)。
- 门禁已接进 CI 的两条触发列表(`push` 与 `pull_request` 各 53 条路径,`contract` 作业 32 个步骤),
  并复核 `python Code/plugins/tools/check_workflow_shell.py` → `WORKFLOW SHELL CHECK OK`、
  `yaml.safe_load` 解析通过。
- C++/TS 由 GitHub CI 编译验证(§0/§1:本机不编译;UI 由 `windows.yml` 的 pnpm 步骤构建)。
  改动的文件:`Code/encoding/Structs/Mods.h`、`Code/server/GameServer.cpp`、
  `Code/client/Services/Generic/TransportService.cpp`、
  `Code/skyrim_ui/src/app/services/error.service.ts`、`Code/skyrim_ui/src/app/mock/skyrimtogether.mock.ts`。
  wire 格式**未改**(标记只出现在 UI 的 JSON 文本里,不进二进制协议)。

**教训**:**「这个字段反正用不到」是错的 —— 只要它是数据字段,它的合法值域里就不许有哨兵。**
`Id` 在错误路径里确实没有别的读者,所以「借它当方向」看起来无害;但它的值域**包含 0**,
而 0 恰好是最常见的一个合法值(第一个插件、第一个 ESL)。**判据是值域,不是读者数。**
现在这件事由 `static_assert(kRemoveMarker > kMaxLiteId, ...)` 兜住:哨兵想和真实 id 撞车,
构建就失败。

**教训(同轮第二条)**:**跨语言边界的契约要按「三处一起看」来建门禁,而不是按「其中一处改了什么」。**
C++ 产生码、语言文件提供串、TS 决定怎么拼键 —— 三处此前各自都有测试/门禁,
**但没有一条同时看它们**,而缺陷恰好只在**交界**上(缺一条翻译、方向词不一致)。
这与 §115「一条消息定义两次」是同一形状,只是这里的两侧是两种语言。

### 变更:lite(ESL)文件是否被上报,本仓库**无从判定** —— 于是改成一行日志可以判定,并加门禁守住它

**判据是一条本仓库自己回答不了、而两个答案后果相反的问题。** 客户端认证包里的 mod 列表是
**唯一**让服务端认识客户端 mod 的东西(服务端 `ModsComponent` 据此发号,客户端 `ModSystem` 据此把
服务端 id 映射回本机 form id),而它的成员资格由一个判断决定:

```cpp
// Code/client/Games/TES.h
[[nodiscard]] bool IsLoaded() const noexcept { return standardId != 0xFF; }
```

`0xFF` 是引擎的「**不在标准空间里**」,而 **lite(ESL)文件按定义就不在标准空间里** ——
它的 form id 前缀是 `0xFE000000 + liteId * 0x1000`,由 `IsLite()`(flags 第 9 位)决定,
`GetId()` 走另一条分支。**所以 `standardId != 0xFF` 对 lite 文件是真还是假,本仓库没有任何证据**:

- 引擎头文件不在这个树里(全仓唯一的 `TESFile.h` 是 **`Code/components/es_loader/`** ——
  本仓库**自己**给服务端写的加载器模型,它把 `m_standardId` 与 `m_liteId` 分开赋值、**没有哨兵**,
  因此它不能回答**引擎**的行为);
- 本机也没有可问的联网通道(已实测:搜索端点不可用)、没有 1.5.x/AE 可执行文件。

**而两个答案的后果相反,所以不能猜:**

| 答案 | 后果 |
| --- | --- |
| lite 文件**通过** | 下面的 lite 路径正常工作:`ModSystem::HandleMods` 填 `m_liteToServer`,`GetServerModId` 的 `0xFE` 分支解析 ESL form id |
| lite 文件**被丢掉** | 这个 ESL 服务端**从来不知道** ⇒ 它加的每个 form **静默失去服务端 id**;开了 mod policy 的服务器还会要求玩家「安装」一个他已经装了的 mod —— **一个他无法解决的拒绝** |

**这不是小众问题**:1.6.1130+ 随游戏出货的 **`_ResourcePack.esl` 本身就是 ESL**,
而且它就是 `liteId == 0` 那一个(`DiscoveryService::IsDefaultModlist` 的期望列表里就写着它),
所以第二个答案在 AE 上是「每个人」的问题。它与 v1.1.9 修掉的 mods-mismatch 方向缺陷**同源**:
两条都只对**第一个 ESL** 敏感(`liteId` 从 0 发号),只是那次撞的是**方向哨兵**,这次怀疑的是**过滤器**。

**做法:不猜,把它变成一行可判定的证据**(与 `docs/FOLLOW-UP.md` 第 1 条同一套路,理由也一样:
本机没有证据,而玩家日志是最便宜的来源)。认证包构建循环现在报出**它丢掉的每个文件**
(上限 8 条,避免 400 个插件的整合包把一次连接刷成一面墙),并带上决定这个问题的三个字段;
循环之后给出**一行汇总**:

```text
mod list: N reported (L lite), K dropped by the standard id rule
server mod list: M entries, S lite; lite mappings established: T, not found locally: U
```

- **`L > 0`** ⇒ lite 文件通过 ⇒ 问题关闭;
- **`L == 0`,而丢掉的那几行里有 `isLite=1`** ⇒ lite 被丢掉 ⇒ 确认缺陷;
- `S > 0` 而 `T == 0` ⇒ 同一个缺陷的另一面。

**这两条必须在 `info` 级,而这本身是一条可查的判据**:`Code/client/TiltedOnlineApp.cpp` 里
`rotatingLogger->set_level(spdlog::level::debug);` 是**注释掉的**,出货构建的等级就是 `info` ——
写成 `debug` 等于写进没人看得到的日志。

**新增门禁** `Tools/Scripts/check_lite_mod_path.py`。它**不判引擎行为**(它判不了),
只守住「让这件事继续可判定」与「它问的那条路径还接着」:

| 判据 | 为什么 |
| --- | --- |
| 丢掉文件的那条日志存在、**按参数**点名 `->IsLite()` / `->standardId` / `->liteId`、且不是 `debug`/`trace` | 少了它问题重新不可判定;等级写低了等于没写。**按参数而不是按格式串**判:格式串可以继续写着 `standardId` 而参数打印的是别的字段,而参数才是承载答案的那一半 |
| 两侧的计数还在报(`reported` / `dropped by the standard id rule` / `lite mappings established`) | 没有计数只能看到「有文件被丢」,看不到「丢的里面有没有 lite」 |
| lite 路径本身还接着(`m_liteToServer.emplace(` / `hiByte == 0xFE` / `m_liteToServer.find(`) | **与答案无关**:就算答案是「通过」,这条路径断了也一样坏 |
| `Mod` 的 `IsLite()` / `GetId()` / `standardId != 0xFF` 与两条 `static_assert(offsetof(Mod, ...))` 还在 | 日志读的就是这些;改名会让它要么编不过,要么打印别的字段 |
| `docs/FOLLOW-UP.md` 第 6 节仍存在且仍点名该门禁 | **缺口与它的仪器一起退休**,不许只删一个 |

**验证**:

- `python Tools/Scripts/check_lite_mod_path.py` → `LITE MOD PATH CHECK OK` /
  `13 anchor(s) present: the lite question is still answerable from a user log, and the lite path
  under it is still wired`。
- **变异测试(四个变异,逐个先确认已落到磁盘,逐个被精确指名)**:
  1. 把那条日志降成 `spdlog::debug` → **两条**诊断:`is not at a level the shipped build prints`
     + `uses ['spdlog::debug'], which the shipped log level filters out`;
  2. 把参数 `pMod->standardId` 换成 `pMod->flags` → `no longer names ['->standardId']`
     (证明「按参数判」这条规则真的在判参数);
  3. 删掉 `ModSystem::HandleMods` 里的 `m_liteToServer.emplace(...)` →
     `"m_liteToServer.emplace(" is gone - the receive side stops recording which server id a lite
     mod got`;
  4. 把 `docs/FOLLOW-UP.md` 的 `## 6.` 改名成 `## 6-X.` → `the open item about lite files
     (section 6) is gone while the instrument remains`。
- **变异 4 当场暴露了门禁自己的一个漏洞,已修**:第一版的锚是 `'## 6.' in doc`,
  而它**同时匹配子标题 `## 6.1`** —— 于是「改名主干标题、正文留着」被读成「记录还在」。
  现改为匹配顶层标题的正则 `^## 6\. `(编号后必须有空格)。**与 §131.3、§132.4 同形:
  锚定得松一点,门禁就会开始说假话。**
- 门禁已接进 CI 的两条触发列表(`push` 与 `pull_request` 各 55 条路径,`contract` 作业 33 个步骤),
  并复核 `python Code/plugins/tools/check_workflow_shell.py` → `WORKFLOW SHELL CHECK OK`、
  `yaml.safe_load` 解析通过。
- C++ 由 GitHub CI 编译验证(§0/§1:本机不编译)。改动的文件:
  `Code/client/Games/TES.h`(只加注释,不改行为)、
  `Code/client/Services/Generic/TransportService.cpp`(日志 + 上限常量)、
  `Code/client/Systems/ModSystem.cpp`(计数 + 汇总日志)。
  **行为未改**:在拿到证据之前,把判断换成一个同样没有证据的写法只是把不确定性换个地方放
  (把 `IsLoaded()` 改成恒真会连**被禁用**的插件一起上报,让服务端认为你有你没装的东西)。
  `docs/FOLLOW-UP.md` 第 6 节写明了两条候选修法与「先要有证据」的理由。

**教训**:**「我不知道」有第三种处理方式,而且它比「猜」和「不写」都好:把问题变成下一次
日志就能回答的形式。** 猜测会把不确定性藏进代码(而且这次两个答案的后果一个是功能正常、
一个是玩家被永久拒绝);不写则会让下一个人重新踩一遍。**判据是「这一行日志能不能把问题
收敛成是/否」**,而这一轮的答案是:一行汇总 + 三条被丢文件的明细,足够。

**教训(同轮第二条)**:**门禁的锚要锚在「主张」上,不能锚在「出现过的字符串」上。**
`'## 6.' in doc` 让子标题替主干标题担保,于是最重要的那个动作(把待验项删掉)
反而不会失败。**锚的宽度就是门禁的可信度。**

### 修复:UI 的「打开指南」按钮和「重连」按钮——一个从没注册过,一个三层都没实现

**判据是一条三层各写一遍、而没有任何东西同时看它们的桥。** 一个 UI 按钮要走三层才能到游戏:

1. **typings**(`Code/skyrim_ui/src/typings.d.ts`)**声明** overlay 对象有什么,
   `client.service.ts` 按名字调用;
2. **`ProcessHandler::OnContextCreated`**(`Code/tp_process/ProcessHandler.cpp`)**注册** JS 函数名 ——
   **没注册的名字根本不出渲染进程**,调用在浏览器里抛异常,没有日志、没有半截动作;
3. **`OverlayClient`**(`Code/client/Services/Generic/OverlayClient.cpp`)**按名字派发**到实现 ——
   注册了却没有分支的名字会跨过进程边界、然后被 if 链**静默丢掉**。

`docs/PITFALLS.md` §0 的清单里就有这条规则(「新 JS→C++ 调用必须在 `ProcessHandler.cpp::OnContextCreated`
注册」),而它是**一个勾选项**。于是:

- **`openPlayGuide`(活缺陷)**:派发分支写着(`else if (eventName == "openPlayGuide")`)、
  实现写着(`ProcessOpenPlayGuide()` → `ShellExecuteW(… wiki … playguide)`)、
  按钮接着(`party-hint.service.ts` 把它挂成组队提示的「打开指南」动作)、
  字符串也译好了(14 种语言的 `COMPONENT.NOTIFICATIONS.OPEN_GUIDE`)—— **只差注册那一行**。
  后果:玩家单机做任务时弹出的组队提示上那个按钮**点了什么都不发生**(overlay 控制台里一条 TypeError)。
  而 mock 实现了它,所以浏览器里 mock 跑得好好的,看不出来。
- **`reconnect`(潜伏缺陷)**:typings 声明了、`client.service.ts` 调了、mock 实现了 ——
  **两层 C++ 都没有**:`OverlayClient` 的派发链里从来没有 `reconnect`。
  按钮本身在 `root.component.html` 里被 **HTML 注释**包着(所以今天点不到),
  但那一行代码同时还会把重连预算清零 ⇒ 一旦有人把按钮放开,它会**既什么都不做、
  又让自动重连不再发生**。这正是 §43.8 那类「记为潜伏项」的形状。
- 顺带由**门禁自己**抓出第三条:**`deactivate`** 注册了、typings 声明了、UI 包了一层,
  而 `OverlayClient` **没有分支** ⇒ 请求跨过边界后被静默丢掉(不报错、无效果)。

**修复:**

1. **`ProcessHandler.cpp` 补 `openPlayGuide` 的注册**(一行)—— 那一整条链本来就是通的。
2. **`client.service.ts` 的 `reconnect()` 改用已注册的两个原语**
   (`skyrimtogether.abandonAttempt()` + `this.connect(host, port, password)`),
   也就是**自动重连那条路一直在用的两步**(`onDisconnect` 里的 `:384-387`)。
   `typings.d.ts` 里那条 `reconnect()` 随之删掉 —— overlay 对象**没有**这个方法,
   而 typings 描述的是 overlay 对象。
3. **`OverlayClient` 补 `deactivate` 的分支与实现**:`SetUIVisible(false)`,即
   `InputService::SetCaptureEnabled(false)` + `OverlayService::SetActive(false)` + 隐藏光标 ——
   正是 typings 上那句「Deactivate UI and release control」,也**正是游戏侧自己隐藏 overlay 时
   用的同一对调用**(同文件 `ProcessRevealPlayersMessage` 里就是直接调 `SetUIVisible(false)`,
   所以线程模型与既有写法一致),两条关闭路径从此不会在「捕获状态」上分叉。

**新增门禁** `Tools/Scripts/check_overlay_bridge.py`,四条判据 + 两条报告:

| 判据 | 为什么 |
| --- | --- |
| typings 声明的每个方法都**必须注册** | 声明就是「这个对象有这个方法」;没有 ⇒ 调用即抛 |
| UI 里每个 `skyrimtogether.<名>(` 调用都**必须注册** | 没注册的名字不出渲染进程:调用抛异常、功能什么都不做 |
| UI **调用**的每个名字都必须**有派发分支**(`on`/`off` 除外:它们是框架在渲染进程内自己处理的) | 否则请求跨过边界被静默丢掉 —— 不报错、无效果,是最难查的一类 |
| 反向:派发了却没注册的名字**失败** | 那是 UI 永远到不了的分支 |
| 报告:注册了但 typings 没声明、UI 也没调 | `toggleDebugUI` 就是(它由键盘驱动),报告而不判失败 |
| 报告:声明了、注册了、但 UI 没人调 | 通道未使用;`deactivate` 修好前属于这一类 |

判据 3 特意写成「UI **调用**的」,而不是「所有注册的」:没人调的名字不可能被静默丢掉,
所以「未使用的通道」是**报告**项而不是失败项 —— 但它**仍然**要求 `deactivate` 这种
「UI 真的调了、却没有分支」的情况失败(`client.service.ts` 那层包装就是调用点)。

**验证**:

- `python Tools/Scripts/check_overlay_bridge.py` → `OVERLAY BRIDGE CHECK OK` /
  `34 call(s) verified through all three layers: declared, registered, dispatched`。
  对照表:`registered` 18 个、`dispatched` 16 个、`declared` 17 个;
  `on`/`off` 是渲染进程内自处理的例外(判据里写明理由)。
- **变异测试(四个变异,逐个先确认已落到磁盘,逐个被精确指名;共 7 条诊断)**:
  1. 删掉刚补的 `openPlayGuide` 注册(= **还原原始缺陷**)→
     `typings.d.ts declares openPlayGuide() but ProcessHandler.cpp never registers it`
     + `client.service.ts:278 calls skyrimtogether.openPlayGuide(), which ... never registers`;
  2. 给 typings 加一个 `phantomTyped()` → 第一条判据指名它;
  3. 在 UI 里加一个 `skyrimtogether.phantomCall()` → 第二条判据指名它;
  4. 把 `deactivate` 的分支换成 `phantomAction`(= **还原第三条缺陷**)→
     第三条判据指名 `deactivate` 缺分支,第四条判据同时指名 `phantomAction` 无注册、`
     openPlayGuide` 派发了却没注册。
  顺带核对:注册判据锚在 `SetValue("名", CefV8Value::CreateFunction` 这个**形式**上,
  所以注释里出现名字不算注册(变异 1 里注释仍在,门禁照样报错)。
- 门禁已接进 CI 的两条触发列表(`push` 与 `pull_request` 各 57 条路径,`contract` 作业 34 个步骤),
  并复核 `python Code/plugins/tools/check_workflow_shell.py` → `WORKFLOW SHELL CHECK OK`、
  `yaml.safe_load` 解析通过。
- C++ 与 TS 由 GitHub CI 编译验证(§0/§1:本机不编译;UI 由 `windows.yml` 的 pnpm 步骤构建)。
  改动的文件:`Code/tp_process/ProcessHandler.cpp`(一行注册)、
  `Code/client/Services/Generic/OverlayClient.h`/`.cpp`(声明 + 分支 + 实现)、
  `Code/skyrim_ui/src/app/services/client.service.ts`(reconnect 改用已注册原语)、
  `Code/skyrim_ui/src/typings.d.ts`(删掉对象上并不存在的 `reconnect`)。

**教训**:**「三层各写一遍」的桥,必须在**一次**检查里同时看三层 —— 只看一层永远发现不了缺口。**
这次两条缺陷正好落在**不同的两层**:`openPlayGuide` 缺第 2 层(而第 3 层早就写好了),
`reconnect` 第 2、3 层都没有而第 1 层声明了。§115 的「一条消息定义两次」在这里变成三次,
而**缺的那一层决定了症状**:缺注册 ⇒ **抛异常**;缺派发 ⇒ **静默丢掉**。
后者更危险,因为它连错误都不给你。

**教训(同轮第二条)**:**mock 会把「桥没接上」这类缺陷完整遮住。**
`openPlayGuide` 与 `reconnect` 都在 mock 里实现了,所以**在浏览器里跑 mock 一切正常** ——
它们只会在真实客户端里坏。**mock 实现了一个真实实现不存在的方法时,那不是 mock 的完整,
而是契约已经分叉了**;门禁把两者分开比对(UI 调用排除了 `app/mock/`),就是为了让 mock 不能替真身担保。

### 修复:场景更新消息带了一个**没人读**的字段,而它唯一的写入者拿「任务阶段号」和常量 5 相比

**判据是一条「线路上有、语义上没有」的字段。** `check_wire_symmetry` 证明的是**两个方向
搬运同一批字段、顺序一致**;它**不问**接收端到底有没有用到那个值。而「搬过去就丢」是协议里
最安静的一类缺陷:**字节被消费了、所有检查都通过、字段背后的功能却从来没发生过**。

`RequestQuestSceneUpdate`(客户端→服务端的场景开始/结束)就有这样一个字段:

```cpp
// Code/client/Services/Generic/QuestService.cpp(修复前)
RequestQuestSceneUpdate update;
update.SceneId = sceneGameId;
update.QuestId = questGameId;
update.Stage  = stageId;
update.Status = stageId == RequestQuestUpdate::StageUpdateForced;   // <-- 这里
```

`stageId` 是**任务当前的阶段号**(`pQuest->currentStage`),而
`RequestQuestUpdate::StageUpdateForced` 的值是 **5** —— 于是这一行是「这个任务现在正好停在
阶段 5」,与它读起来的意思(「这是一次强制更新」)毫无关系。而**服务端从头到尾没有读过它**:
`OnQuestSceneChanges` 用的是 `SceneType`(BEGIN/END)、`SceneId`、`QuestId`、`Stage`、
`ClientQuestType`,以及 `RequestQuestUpdate::StatusCode::SceneUpdate` 作为**去重行标签**
(那是枚举值,不是这个字段)。

**发现过程里先否掉了两个不存在的缺陷**(与 §125 同形,值得记下来):看到
`RequestQuestUpdate::StatusCode` 有 6 个值而 `NotifyQuestUpdate::StatusCode` 只有 4 个,
先怀疑是「同一枚举定义两次、值集不一致」;读完服务端那个 `switch` 后否掉 ——
它把 4 个值映射过去,`StageUpdateForced` 走
`case RequestQuestUpdate::StageUpdateForced: useAggressiveSend = true; [[fallthrough]];`
进 `StageUpdate`,其余落 `default:` 安全丢弃并举一条 error,而通知方向只会被赋那 4 个值之一。
**枚举值集不同在这里是对的**:强制状态是**服务端自己**生成的(它给落后的队员补一次当前阶段),
不来自客户端。

**修复**:把 `Status` 从 `RequestQuestSceneUpdate` 端到端删掉 ——
结构体字段、`operator==`、`SerializeRaw`、`DeserializeRaw`,以及客户端那一行写入者。
**这是协议改动**:同一个 commit 的客户端与服务端才互通(§0 的版本握手),所以删一个 8 位字段
是安全的;而且删掉的这一位**在两种情况下都没有接收者**。

**新增门禁** `Tools/Scripts/check_wire_field_reads.py`:**这两条场景消息的每个字段都必须在
它到达的那个 handler 里被读**(不是被赋值)。作用域写成**显式清单**(两条消息 + 两个 handler),
清单不再匹配就失败,而不是静默检查零个东西。

**验证**:

- `python Tools/Scripts/check_wire_symmetry.py` → `WIRE SYMMETRY CHECK OK` /
  `133 pair(s) write and read the same wire operations in the same order` ——
  **这就是这次字段删除的判据**:结构体与两个序列化器必须仍然一致(少写一个字段、或多读一个字段,
  这条会红)。
- `python Tools/Scripts/check_wire_field_reads.py` → `WIRE FIELD READ CHECK OK` /
  `8 field(s) of the two scene-update messages are read where they arrive`;
  `--list` 打出两条消息的字段清单与各自被读的 handler。
- **变异测试(三个变异,逐个先确认已落到磁盘,逐个被精确指名)**:
  1. **把 `Status` 字段加回结构体**(= 还原本轮修掉的缺陷)→
     `OnQuestSceneChanges never reads RequestQuestSceneUpdate.Status; RequestQuestSceneUpdate
     serialises it and the receiver drops it, so whatever fills it has no effect`;
  2. 把 handler 改名 `OnQuestSceneChanges` → `OnQuestSceneChangesRenamed` →
     `OnQuestSceneChanges was not found; the handler this gate reads RequestQuestSceneUpdate in has
     been renamed or moved`(防退化);
  3. 给 `EXEMPT` 加一条不存在的 `NotifyQuestSceneUpdate.SceneType_phantom` →
     `is exempt but no longer a field of NotifyQuestSceneUpdate; drop the exemption`(豁免不会过期腐烂)。
- 门禁已接进 CI 的两条触发列表(`push` 与 `pull_request` 各 60 条路径,`contract` 作业 35 个步骤),
  并复核 `python Code/plugins/tools/check_workflow_shell.py` → `WORKFLOW SHELL CHECK OK`、
  `yaml.safe_load` 解析通过。
- C++ 由 GitHub CI 编译验证(§0/§1:本机不编译)。

**一个被扔掉的门禁(负结果,已记 §135.5)**:通用版(全部 133 条消息的每个字段)写出来、
跑过、**扔掉了**。按字段名全树搜**区分不了同名跨消息的字段**——`Status`、`Id`、`Stage`
同时属于十几条消息,任何一条读自己的字段就会替别的消息担保;改成「只在注册给该消息的 handler
体内搜」之后,又低估了**转发给 helper** 的常见写法:第一版把
`CharacterSpawnRequest.FactionsContent`、`StringCacheUpdate.Values` 这类**功能明明在用**的字段
报成没人读(实测 7~10 条假阳性)。**一个会说假话的门禁会被关掉,所以它不发布**;
发布的是能做成精确的那一半。判据不是「门禁覆盖得多不多」,而是**「它说的话能不能担保」**。

**教训**:**只查「搬运是否对称」的检查,管不到「搬过去有没有人用」。**
`check_wire_symmetry` 覆盖 133 对读写,一条都没漏 —— 而缺陷正好长在它的**判据边界之外**:
两侧都忠实地搬运了一个没人读的字节。**每加一条对称性检查,都要问一句「它证明了什么、
没证明什么」**,然后把没证明的那部分单独做成一条能担保的判据(哪怕作用域很小)。

**教训(同轮第二条)**:**写入者拿一个数据字段当常量比较,是「读起来像功能、实际是巧合」的典型。**
`stageId == StageUpdateForced` 里左边是阶段号、右边是枚举常量 5:它**永远为假,除了阶段正好是 5 的任务**。
如果这个字段有读者,这就是一个只在特定阶段才触发的怪 bug;因为它没有读者,它只是一颗哑弹。
**两种情况都该删掉这一行** —— 区别只在于「删掉之后要不要补功能」。

### 修复:一条服务器设置被声明了**两次** —— 而 ini 只会写进先注册的那一个,「先」由静态初始化顺序决定

**判据是这个仓库自己的设置机制决定的,不是风格问题。** 每一个 `Console::Setting` 在**构造时**
把自己挂进一条全局链表(`SettingBase(ROOT(), ...)` → `parent = this`),
`RegisterServerSettings()` 再把整条链表交给控制台注册表(`AddSetting` → `m_settings.push_back`,
**没有任何按名字去重**);而按名字取用时:

```cpp
// Code/components/console/ConsoleRegistry.cpp
SettingBase* ConsoleRegistry::FindSetting(const char* acName)
{
    auto it = std::find_if(m_settings.begin(), m_settings.end(), …strcmp(settingName, findName) == 0);
    if (it == m_settings.end()) return nullptr;
    return *it;                       // <-- 第一个同名的赢
}
```

于是**同名的两条 `Console::Setting` 是两个对象共用一个地址**:ini 加载器与控制台命令都只写进
**第一个**;而构造顺序 = **跨翻译单元的静态初始化顺序,标准里是未指定的** —— 所以「哪一个赢」
**连构建之间都不稳定**;输的那一个**终生保持构造时的默认值**。

实测全仓 **21 条声明里恰好一处重名**:

| 设置名 | 声明处 | 谁在读 |
| --- | --- | --- |
| `LiveServices:bAnnounceServer` | `Code/server/GameServer.cpp:36` | `GameServer::IsPublicServer()`(公开服务器列表判定,并被 `AllowsAutoPartyJoin()` 读:`bAutoPartyJoin && !IsPublicServer()`) |
| `LiveServices:bAnnounceServer` | `Code/server/Services/CommandService.cpp:16` | `/settime` 权限:`if (IsPlayerLeader(...) && !bAnnounceServer)`(注释写着「Party leader allowed on private servers only」) |

**两个方向的后果都是错的**,而且取决于链接顺序:

- 若 ini 值落进 **CommandService** 的那份:`IsPublicServer()` 永远是 `false` ⇒ 服务器**不会被
  列进公开列表**(管理员设了 `true` 也没用),并且 `AllowsAutoPartyJoin()` 在公开服务器上**仍然是真**;
- 若落进 **GameServer** 的那份:公开服务器上**队长仍然能改时间**(权限判定看到的是默认 `false`)。

**修复**:删掉 `CommandService.cpp` 里的副本,改走**唯一的那份设置**——
`GameServer::IsPublicServer()`(`CommandService.cpp` 本来就 include 了 `<GameServer.h>`,
而这个访问器也是 `AllowsAutoPartyJoin()` 已经在用的),两处读者从此看同一个值。

**新增门禁** `Tools/Scripts/check_console_settings.py`:**任何 `Console::Setting` 名字都只能声明一次**。
这是**能担保**的判据:只需要声明处的字符串,不需要调用图或类型系统。另加防退化下限
(少于 15 条声明就失败,否则改个写法就能让门禁空转)。

**验证**:

- `python Tools/Scripts/check_console_settings.py` → `CONSOLE SETTING CHECK OK` /
  `20 setting declaration(s), every name unique`(21 条声明去掉副本 = 20 ✓)。
- **变异测试(三个变异,逐个先确认已落到磁盘,逐个被精确指名)**:
  1. **把重复声明加回 `CommandService.cpp`**(= 还原本缺陷)→
     `"LiveServices:bAnnounceServer" is declared 2 times (Code/server/GameServer.cpp:36,
     Code/server/Services/CommandService.cpp:24); a name resolves to the first registration, so the
     others never receive the ini value and keep their default - and which one wins depends on static
     initialisation order`;
  2. **同一个文件里制造重名**(把 `Gameplay:bEnablePvp` 的键改成 `Gameplay:bEnableGreetings`)→
     `"Gameplay:bEnableGreetings" is declared 2 times (GameServer.cpp:41, GameServer.cpp:42)`
     —— 同一文件内的重名同样被抓到;
  3. 把防退化下限抬到 999 → `only 20 Console::Setting declaration(s) were found, below the floor of
     999; the declaration shape changed…`。
- **门禁自己犯过一次错,当场修掉,而且这个坑本仓库已经踩过**:第一版**没有剥注释**,
  而我在 `CommandService.cpp` 里写的说明注释**逐字引用了那条被删掉的声明** ——
  于是门禁把注释当成声明,报告「重复仍在」。这与 §3.1 记的
  「`gen_ae_to_se_map.py` 的旧口径**把注释掉的调用当成活的**」是**同一个坑**。
  现已先剥 `//` 与 `/* */` 再匹配。
- 门禁已接进 CI 的两条触发列表(`push` 与 `pull_request` 各 61 条路径,`contract` 作业 36 个步骤),
  并复核 `check_workflow_shell.py` → `WORKFLOW SHELL CHECK OK`、`yaml.safe_load` 通过。
- C++ 由 GitHub CI 编译验证(§0/§1:本机不编译)。改动文件:
  `Code/server/Services/CommandService.cpp`(删副本 + 改读访问器)。

**教训**:**在「自注册 + 按名字查」的注册表里,一个名字出现两次不是风格问题,是语义分裂。**
两个对象、一个地址、`find_if` 选第一个、而第一个是谁由**静态初始化顺序**决定 ——
它同时具备三个最坏的属性:**静默**(没有日志)、**不确定**(换个构建可能换一个赢家)、
**看起来正常**(两边都有值,只是其中一边永远是默认值)。判据不是「名字起得一致不一致」,
而是**「这个名字被解析到几个对象」**。

**教训(同轮第二条)**:**门禁的判据要锚在「代码」上,不能锚在「文本」上 —— 注释也是文本。**
剥注释这件事在本仓库的采集器里早就是标准动作(§3.1 的旧口径正因如此虚增了 3 个 id),
而我写这条门禁时又漏了一次。**「我在注释里引用了它」与「它还在」在正则眼里没有区别** ——
所以任何扫源码的门禁,第一步都该是剥注释。

### 修复:1.5.x 证据表记录了代码**故意不用**的偏移 —— 而豁免让「把它加回去」这件事**静默通过**

**先说清楚这一轮不是代码 bug。** 代码是对的:`BSGraphicsRenderer.cpp` 的 frame end 在 1.5.x 上
**直接钩住锚点函数本身**(`TP_HOOK_IMMEDIATE`),只有 1.6.x 才需要换掉里面那个 `call`:
所以 `At(pFrameEnd, {9}, "frame end")` 故意不传 1.5.x 偏移。问题在**证据文件**与**门禁**两侧:

1. **判断在转录中丢了**:生成器 `Tools/ida/patch_offsets_1597.py` 的输入里逐字写着
   `(77246, 9, 5, "frame end (call site, 1.6 only)")` —— 人已经判断了「这个 call site 只有 1.6 有」,
   但工具照样去对齐,并在 1.5.97 的同一函数里找到一条 `call`,于是**输出**写下了 `0x9`。
   输入里的判断没有到达输出。
2. **于是危险的方向被豁免掉了**:`Tools/Scripts/check_patch_offsets.py` 把 frame end 列进
   `UNUSED_LEGACY_OK`(「表里有、代码不用的偏移不算缺陷」)—— 这个判断本身合理,
   但它的后果是:**把 `0x9` 加回代码的那个改动,当时是通过的**
   (表里记着 `0x9`,代码用 `0x9`,两边一致)。
   而 `GamePatch::SwapCall` **只检查那个字节是不是 `0xE8`** —— 1.5.97 上 +0x9 处的那条 `call`
   如果不是同一条,就会被**静默重定向**:功能坏掉,没有日志。

**修复(三处,让两个产物说到一起)**:

1. **生成器**:新增 `MODERN_ONLY`,按 `(ae_id, offset)` 精确指定「1.5.x 上没有对应站点」并写明原因;
   命中时输出 `-` 而不是对齐结果。键用 id+偏移,不会漂到别的站点。
2. **证据表**:`77246 0x9 - no 1.5.x patch site: the pre-AE path hooks the anchor itself`
   —— 表里不再提供代码不用的数字。**生成器本机跑不了**(需要两份 exe + id 映射),
   所以这一行是按修好的生成器**逐字**手写的;门禁验的是**表与代码是否一致**,不是生成器能否运行。
3. **门禁**:把 frame end 从 `UNUSED_LEGACY_OK` 移到 `NO_LEGACY_EXPECTED`(后者的理由与它相符),
   `UNUSED_LEGACY_OK` 因此清空(机制保留)。方向没变(表多给一个偏移仍然**不是**缺陷),
   但它现在会**被报出来**,而不是被豁免掉。

**验证**:

- `python Tools/Scripts/check_patch_offsets.py` → `PATCH OFFSET CHECK OK` /
  `13 patch site(s) agree with st_patch_offsets_1597.tsv`,**零 note**(改之前也是零 note,
  因为那条 note 被豁免了 —— 这正是要修的东西)。
- **变异测试(两个变异,逐个先确认已落到磁盘)**:
  1. **把 1.5.x 偏移加回代码**(`{9}` → `{9, 0x9, "1.5.97"}`,即当年会静默通过的那个改动)→
     **现在失败**:
     `patch 'frame end' applies a 1.5.x offset (0x9) that st_patch_offsets_1597.tsv does not record -
     the row says 'no 1.5.x patch site: the pre-AE path hooks the anchor itself'. A number nobody
     measured must not be written into a running game`
     —— 门禁**引用表里的理由**,而不只是说「不一致」;
  2. **把表改回 `0x9`**(产物退化)→ 输出 1 条 note
     `'frame end': st_patch_offsets_1597.tsv records a 1.5.x offset (0x9) that the call site does not
     pass; the patch is skipped on 1.5.x`,门禁仍通过 —— 这个方向**不是缺陷**(弃用一个装饰性补丁是
     故意的),但它从「被豁免」变成「被报告」。
- 生成器本机不能运行,已用**内存内 `compile()`** 确认语法(不用 `py_compile`,它会试图写
  `__pycache__` 而被沙箱拒绝),并确认 `MODERN_ONLY` 在定义处与使用处都到位。
- 门禁已在 CI 的两条触发列表与 `contract` 作业里(§130 那批路径已含 `Tools/ida/**`),本轮不需要改工作流。

**教训**:**「不是缺陷」的豁免,会把它旁边那个**真**缺陷一起豁免掉。**
`UNUSED_LEGACY_OK` 的理由是对的(表多给一个偏移不算错),但同一个豁免让
「把这个偏移加回代码」也通过 —— 而后者是**往正在运行的游戏里写字节**。
豁免要看**它挡住了什么**,不只**它放过了什么**:如果一条判据的两个方向一个失败一个豁免,
那么豁免的那个方向就是**绕过路径**。

**教训(同轮第二条)**:**工具的输入里有判断,不代表输出里有。**
`"frame end (call site, 1.6 only)"` 是人的判断,而工具照样输出对齐结果 ——
**判断写在描述里,就一定会丢**;要写成**结构化字段**(这里是 `MODERN_ONLY` 的键),
否则下游只看到「有偏移」。

### 修复:一条「卡住了」的日志**说不出卡住的是谁**;并给 648 条日志加上占位符/实参一致性检查

**判据是一条二值、可机检的性质:格式串里的占位符数量 == 实参数量。** 两个方向都坏,而且坏法不同:

- **占位符多于实参** ⇒ spdlog 走 fmt,`fmt::format_error` **在写这行日志的那一刻抛出** ——
  异常落在当时有效的处理器上。**只在罕见路径上触发的日志,恰恰是「出事之前从没被执行过」的那条**
  (「地址库 id 没映射」「消息构建失败」……):**一条会崩的日志比没有日志更糟**;
- **实参多于占位符** ⇒ 反过来**静默**:fmt 只格式化它要的参数,多出来的值**一个都不会出现在日志里**。

**实测全仓 648 条带字面量格式的调用,恰好一处**:

```cpp
// Code/client/Services/Generic/InventoryService.cpp(修复前,同一段里的四行)
440: spdlog::debug(__FUNCTION__ ": actorId {:X} naked check is irrelevant {}",       pActor->formID, pActorName);
446: spdlog::debug(__FUNCTION__ ": actorId {:X} with naked check deadline {}",       pActor->formID, pActorName);
452: spdlog::debug(__FUNCTION__ ": actorId {:X} naked check deadline expires {}",    pActor->formID, pActorName);
455: spdlog::debug(__FUNCTION__ ": but still WaitingForAssignment",                  pActor->formID, pActorName);   // <-- 只有它没有占位符
```

第 455 行是「裸体检查的截止时间到了,而这只 actor **仍然**带着 `WaitingForAssignmentComponent`」——
**正是那条要说「卡住了」的日志**,而它**既不打 actorId 也不打名字**:那一段里唯一需要用来定位的
那条恰好说不出是谁(`pActorName` 在 432 行取好、传进去、然后被丢掉)。
**这是 §133 同一条道理的又一处落点:诊断行要能回答「是谁」。**

**修复**:补上两个占位符,与同族三行对齐:
`__FUNCTION__ ": actorId {:X} but still WaitingForAssignment {}"`。

**新增门禁** `Tools/Scripts/check_log_format_args.py`:

- 对全仓 `Code/**` 的 `spdlog::<level>(...)`(含 `logger->` 形式与**跨行调用**)解析第一个参数,
  若是**字符串字面量**(允许与 `__FUNCTION__` 拼接,这是本仓库的写法)则数占位符:
  `{}`、`{:X}`、`{:#04x}` 都算,`{{`/`}}` 是**转义的字面量花括号,不算**;
- 格式串是**变量**的调用**跳过**(不猜),但**跳过比例超过 35% 就失败** ——
  否则「跳过」会把门禁悄悄掏空;
- 另有下限:解析到的调用少于 300 条即失败(调用写法变了要报错,而不是空转);
- **剥注释时保留字符串字面量**(格式串就在字面量里)—— §136 的教训在这里不能照搬。

**验证**:

- `python Tools/Scripts/check_log_format_args.py` → `LOG FORMAT CHECK OK` /
  `648 log call(s) pass as many arguments as their format string asks for`;
  `--list` 报 `648 checked, 17 skipped (variable format)`。
- **变异测试(三个变异,逐个先确认已落到磁盘)**:
  1. **还原第 455 行**(去掉两个占位符)→ `debug has 0 placeholder(s) and 2 argument(s)` ✓;
  2. 把 `main.cpp` 那条**跨行**日志改成 4 个占位符 3 个实参 →
     `info has 4 placeholder(s) and 3 argument(s)` ✓(同时证明跨行调用被正确解析);
  3. 在同一条调用里写入 `{{literal braces must not count}}` → **仍然通过** ✓ ——
     证明花括号转义不会误报(门禁不会因为「字符串里有花括号」而喊狼来了)。
- 门禁已接进 CI 两条触发列表(`push` 与 `pull_request` 各 63 条路径;新增 `Code/**`,
  因为这个门禁读的是**全仓**的日志调用),`contract` 作业 37 个步骤;
  `check_workflow_shell.py` 与 `yaml.safe_load` 复核通过。
- C++ 由 GitHub CI 编译验证(§0/§1:本机不编译)。

**教训**:**向「罕见路径」写日志的那行代码,是最不可能被测到的一行 —— 所以它必须是机械可验证的。**
648 条里错 1 条,靠人眼读是读不出来的;而按「占位符数 == 实参数」这**一条二值判据**扫一遍,
它自己就浮出来了。**能被机检的性质,不要交给 review。**

### 修复:客户端的四条「玩家消息」是**英文散文**,而聊天把系统消息**当翻译键**渲染

**判据是一条已经写好、却没被任何东西执行的契约:系统消息的文本是「键」,不是「句子」。**

链路逐段可查:

1. 客户端发:`OverlayService::SendSystemMessage(const std::string& acMessage)` 逐字
   `pArguments->SetInt(0, kSystemMessage); pArguments->SetString(1, acMessage);`
   `m_pOverlay->ExecuteAsync("message", pArguments);` ——
   而 `Code/encoding/ChatMessageTypes.h` 里 `kSystemMessage = 0`;
2. TS 收:`MessageTypes.SYSTEM_MESSAGE = 0`(**两侧同一个值**),`messageTypeToClassName()` 把它映成 `'system'`;
3. **渲染**:`chat.component.html` 逐字

```html
<span class="app-chat-message" *ngIf="message.typeClass === 'system'">{{
  message.content | transloco: message.translationParams
}}</span>
<span class="app-chat-message" *ngIf="message.typeClass !== 'system'">{{
  message.content
}}</span>
```

⇒ **系统消息的内容是当作翻译键查的**;而 Transloco 配置是 `fallbackLang: 'en'` +
`missingHandler: { useFallbackTranslation: true }`,键在两种语言里都查不到时**把键本身显示出来**。

于是四条 `SendSystemMessage` 字面量(全是英文句子)的实际行为是:**英文玩家看到正确句子**
(键就是句子),**其余 13 种语言看到的是英文** —— 而其中两条的**同一个意思在 14 份语言文件里早就译好了**:

| 站点 | 修复前(散文) | 已有的键 |
| --- | --- | --- |
| `CommandService.cpp`(无权限) | `"Only an admin, or the party leader on a private server, can change the time."` | **`SERVICE.COMMANDS.NOT_ADMIN`**(14 种语言全有译文) |
| `CommandService.cpp`(时间非法) | `"That is not a time. Use /settime HH MM, with hours 0-23 and minutes 0-59."` | **`COMPONENT.CHAT.SET_TIME_INVALID_ARGUMENTS`**(客户端**本地**校验同一条件时用的就是它,14 种语言全有) |
| `CommandService.cpp`(传送失败) | `"Teleporting to player failed."` | 无 —— 本轮新增 |
| `OverlayService.cpp`(同上) | 同上(**同一个句子写了两遍**) | 同一个新键 |

这一条**本仓库的门禁从 §132 起就一直在指着它**:`check_ui_contract.py` 每次都把
`SERVICE.COMMANDS.NOT_ADMIN` 报成「没有任何错误码能到达、也没有 UI 源码按名字要它」——
我当时把它记成「不可达的键」,而没有追问**「那这条消息现在是从哪来的」**。
**一条没有调用者的翻译键,往往意味着它的调用者把句子硬编码了。**

**修复**:

1. 前两条改用**已存在、已翻译**的键 —— 零翻译成本,立刻让 14 种语言看到母语;
2. 传送失败那两条改用新增的 `SERVICE.COMMANDS.TELEPORT_FAILED`;
3. 新键**只加进 `en.json`**(回退语言):另外 13 份文件**不放假译文** ——
   `fallbackLang: 'en'` 本来就是配置好的行为(它们今天看到的也正是这句英文),
   而门禁每次运行都会**报告**它「缺在至少一种语言里」,译者照着补即可。
   **判据是「玩家不会看到键名」,不是「每种语言都有一行」** —— 后者可以造假,前者不能。

**扩展门禁** `Tools/Scripts/check_ui_contract.py`(第 4 条判据):
**客户端送进聊天窗的每一个字面量,都必须在回退语言(`en.json`)里是一个键**;否则失败并说明
「聊天用 `content | transloco` 渲染系统消息,这是键」。另外**报告**「en 里有、别的语言里缺」的键
(降级为英文是配置行为,不是缺陷)。

**验证**:

- `python Tools/Scripts/check_ui_contract.py` → `UI CONTRACT CHECK OK`,两条 note:
  既有的 `NON_DEFAULT_INSTALL` 死键,以及新键
  `['SERVICE.COMMANDS.TELEPORT_FAILED'] are missing from at least one language and fall back to English`。
- 四条字面量现在全是键:`SERVICE.COMMANDS.NOT_ADMIN`、`COMPONENT.CHAT.SET_TIME_INVALID_ARGUMENTS`、
  `SERVICE.COMMANDS.TELEPORT_FAILED`(两处)。
- **变异测试(两个变异,逐个先确认已落到磁盘,逐个被精确指名)**:
  1. **把无权限那条改回散文**(= 还原本缺陷)→
     `CommandService.cpp:56 sends "You must be an admin to use that command." to the chat, which
     renders system messages with content | transloco; that is a translation key, and en.json has no
     such key, so what the player sees is the string itself`;
  2. **从 `en.json` 删掉 `TELEPORT_FAILED`** →
     `CommandService.cpp:85, OverlayService.cpp:606 sends "SERVICE.COMMANDS.TELEPORT_FAILED" …`
     —— **两处站点被一次点名**,证明这条判据覆盖所有站点,而不是只看第一个。
- 门禁已在 CI 的两条触发列表里(`Code/client/**` 与 `Code/skyrim_ui/src/**` 都是 §132 就加过的路径),
  本轮不需要改工作流;`check_workflow_shell.py` 与 `yaml.safe_load` 复核通过。
- C++/TS 由 GitHub CI 编译验证(§0/§1:本机不编译)。

**教训**:**「玩家看到的那句话」如果是硬编码的,那么产品承诺的 N 种语言里只有 1 种是真的。**
这条缺陷之所以能活下来,是因为**它在英文环境下完全正确** —— 键就是句子、查不到就原样显示,
于是开发者(和英文玩家)看到的一切都正常。**判据必须是「渲染路径怎么处理这个字符串」**,
而不是「这句话读起来对不对」:同一个字符串,在 `| transloco` 后面是**键**,在别处是**文本**。

**教训(同轮第二条)**:**一个没有调用者的翻译键,先别急着删 —— 它可能正指着一条硬编码的句子。**
`SERVICE.COMMANDS.NOT_ADMIN` 从 §132 起就被门禁报成不可达,两轮里我都把它读成「死键」,
直到顺着「那这条消息从哪来」才找到四条散文。**报告项要当线索读,不能当噪音读。**

### 修复:「连接断开,正在重连」这条消息把 `{{remainingReconnectionAttempt}}` **原样打给玩家看**

**判据是一条跨三个产物的二值性质:文本里要的占位符,必须在**渲染它的地方**被交进去。**
Transloco 的行为是**只替换它拿到的参数,拿不到的占位符原样留在文本里** ——
所以「键被引用了」不等于「这句话能读」。

实测这条:

```jsonc
// en.json / de.json / ja.json …(14 份语言文件全都一样)
"SERVICE.CLIENT.CONNECTION_LOST": "Connection lost, trying to reconnect. {{remainingReconnectionAttempt}} attempts left.",
"                                 Verbindung verloren, versuche erneut zu verbinden. {{remainingReconnectionAttempt}} Versuche verbleibend.",
```

```ts
// Code/skyrim_ui/src/app/services/client.service.ts(全部站点只有这一处)
this._remainingReconnectionAttempt--;                                    // ← 值就在上一行
this.chatService.pushSystemMessage('SERVICE.CLIENT.CONNECTION_LOST');    // ← 一个参数都没交
```

⇒ **每一次自动重连,玩家读到的是「Connection lost, trying to reconnect.
`{{remainingReconnectionAttempt}}` attempts left.」** —— 而这条消息存在的**唯一意义**就是
告诉他「还剩几次」。**值就在上一行,少传了一个参数。**

**它为什么能活下来**:语句本身没问题(键确实被引用了,`pushSystemMessage` 的第二个参数本来就是可选的),
而英文玩家看到的句子**其余部分完全正常** —— 只有中间那一段是 `{{...}}`。
所以**只有「把目录里的占位符和调用点的参数对起来」这一条判据能抓到它**。

**修复**:把上一行的计数器传进去。

**新增门禁** `Tools/Scripts/check_i18n_params.py`,三条判据:

| # | 判据 | 覆盖的坏法 |
| --- | --- | --- |
| R1 | 一个键的**占位符集合**在 14 份语言文件里必须相同 | 某语言的译文漏掉/改了占位符 ⇒ 那种语言的玩家看到残缺文本 |
| R2 | **字面量键**的调用点必须交出该键需要的参数 | 传错名字或不传 ⇒ 玩家读到 `{{...}}` |
| R3 | **运行时拼出来的键**(`'SERVICE.ERROR.ERRORS.' + code.toUpperCase()`)所需的参数,必须由 `error.service.ts` 里该 code 的接口声明提供 | 面板的键由 code 决定、参数由接口决定 —— **此前没有任何东西把这两边对起来** |

R2 覆盖本仓库的**三种写法**:`translate('KEY', {a})`、`'KEY' | transloco: {a}`、
`{ message: 'KEY', params: {a} }`(以及 `messageParams`);另有下限防退化
(键 < 10、调用点 < 10 即失败)。`[text]="'KEY'"` 这类绑定**故意不在 R2 范围内**:
那个组件自己注入了 `{ length: option.value }`(`dropdown.component.html`),裸键在**那里**才是正确写法 ——
**门禁的作用域必须写清楚**,否则它会把正确写法当缺陷报。

**验证**:

- `python Tools/Scripts/check_i18n_params.py` → `I18N PARAM CHECK OK` /
  `16 parameterised key(s) agree across languages, call sites and runtime codes`
  (note:16 个键、**12 个调用点**、3 个运行时 code)。
- **变异测试(三个变异,每个针对一条判据,逐个先确认已落到磁盘)**:
  1. **还原本轮的修复**(去掉那个参数)→ R2 指名
     `client.service.ts:401: SERVICE.CLIENT.CONNECTION_LOST is rendered without
     ['remainingReconnectionAttempt'] …`;
  2. 把 `de.json` 的占位符改名 `{{remainingReconnectionAttempt}}` → `{{attempts}}` → R1 指名
     `de.json: SERVICE.CLIENT.CONNECTION_LOST needs ['remainingReconnectionAttempt'] but this
     translation has ['attempts']`;
  3. 把 `error.service.ts` 里 `wrong_version` 接口的 `expectedVersion` 改名 → R3 指名
     `the 'wrong_version' interface carries ['serverVersion', 'version'], but
     SERVICE.ERROR.ERRORS.WRONG_VERSION asks for ['expectedVersion']`。
- 门禁已接进 CI 两条触发列表(`push`/`pull_request` 各 65 条路径,新增
  `Tools/Scripts/check_i18n_params.py` 与 `Code/skyrim_ui/src/**`),`contract` 作业 38 个步骤;
  `check_workflow_shell.py` 与 `yaml.safe_load` 复核通过。
- TS 由 GitHub CI(`windows.yml` 的 pnpm 构建)验证(§0/§1:本机不编译)。

**门禁第一版报了 4 条,只有 1 条是真的 —— 另外 3 条是它自己的解析器有问题。**

| 第一版报的 | 真相 |
| --- | --- |
| `commands.ts: SERVICE.COMMANDS.AVAILABLE_COMMANDS is rendered without ['cmds']` | 代码是对的:`{ cmds }` 是**简写属性**,而我的 `param_names` 只认「名字: 值」 |
| `error.service.ts: the 'wrong_version' interface carries ['version'], but … asks for ['expectedVersion']` | 接口是对的:TS 接口成员用 **`;`** 分隔,而我只按 `,` 切 |
| 只见 5 个调用点(下限 10) | `selectTranslate(` **根本没被匹配**:`\btranslate` 在 `selectTranslate` 里**没有词边界**,大小写也不同;另外**管道写法**与 **`params:` 在键之后**的写法完全没覆盖 |
| `client.service.ts:401 … without ['remainingReconnectionAttempt']` | **这一条是真的** ✓ |

修的是**解析器**,不是代码。**与「第一版就通过的门禁值得怀疑」对称:第一版就失败的门禁同样值得怀疑 ——
先去看代码,再决定改哪一边。**

**本轮的另外三处「已验证干净」(记下来是为了以后不用重新探)**:
① **初始化器管线**:36 个 `TiltedPhoques::Initializer` 全在 `Code/client/`,`RunAll()` 只有一处调用
(`TiltedOnlineApp.cpp`),launcher 里那行被注释掉的 `RunAll()` 是因为 launcher **没有任何初始化器** ——
两条启动路径都靠客户端 DLL 自己跑那一遍,顺序也对(`VersionDb::Load` 在前、hook 在后);
② **模板里的硬编码文本**:全仓 26 个模板只有 12 处候选,全部在 `app/mock/`(调试用)、
F2 调试面板与 `index.html` 的窗口标题里,**没有一处是玩家可见 UI**;
③ **占位符的跨语言一致性**:14 份目录 × 全部键,**0 处不一致**。

**教训**:**「键被引用了」和「这句话玩家能读」是两件事。**
键存在、调用点存在、code review 也过得去 —— 但**参数没交**时,文案中间的 `{{...}}` 会原样出现在
聊天窗里,而英文玩家看到的其余部分完全正常。**渲染路径在缺参数时的行为才是判据**,
不是「有没有引用这个键」。

### 审计:五个 UI 事件监听**没有任何产出者** —— 其中一个守卫包住整个 overlay(措辞已订正,见本节末)

**判据是 §134 那条桥的**反向**:`skyrimtogether.on('X')` 必须有一个 C++ 侧
`ExecuteAsync("X")` / `EmitEvent("X")`,否则**它驱动的状态永远不会变**。

实测:**29 个 UI 监听 × 24 个 C++ 事件名 → 5 个监听没有产出者,0 个事件没有监听**:

| 监听 | 真相(逐条查过) |
| --- | --- |
| `setName` | **源码里写着「不要」**:`//not wanted, we dont sync name changes` |
| `debug` | **源码里写着「不再需要」**:`//not needed anymore`,`debugData` 取代了它 |
| `dummyData` | 只喂 F2 调试面板的刷新(`onDummyData` 里打印数据的循环**已被注释掉**) |
| `protocolMismatch` | **已被取代**:协议不匹配现在走 `triggerError` + `wrong_version`(`TransportService.cpp` 组那个 payload),由错误面板显示 |
| **`openingMenu`** | **整条链都在、产出者从来不存在** —— 见下 |

`openingMenu` 这条值得单独说,因为它的消费端是**整个 overlay**:

```html
<!-- Code/skyrim_ui/src/app/components/root/root.component.html, 已删除 -->
<ng-container *ngIf="!(menuOpen$ | async)">
  <app-debug></app-debug>
  <app-group></app-group>
</ng-container>
```

**这行代码块是本节的错误来源**:守卫里只有 `app-debug`(F2 调试面板)与 `app-group`
(队伍成员列表)两个组件,聊天窗、通知、主菜单与弹窗层都在守卫**之外**。当时按「整个 overlay」
写了结论,把「实现产出者」的代价说大了一个量级 —— 订正与后果见 v1.1.9 末尾的
「修复:一个包住两个组件的守卫被记成了产品决定」一节。

即「游戏菜单打开时把 overlay 藏起来」。而它**永远为假**:全仓(任何扩展名)搜 `openingMenu`
只有 UI 侧三处(`root.component.ts`、`client.service.ts`、`typings.d.ts`);
`Code/client/**` 与 `Code/tp_process/**` 的 24 个事件名里没有它;**连 mock 都不发它** ——
所以浏览器里跑 mock 也看不出这个行为是空的。

⇒ **游戏菜单打开时 overlay 不会被隐藏**,守卫是一句空话。真正在隐藏 overlay 的只有
`OverlayClient::SetUIVisible(false)`,而它只有两个调用点(`revealPlayers` 与 §134 补的
`deactivate`),**都不是菜单钩子**:`UI::GetMenuOpen`(id 82074)存在但只有 `DebugService` 用它。

**本轮不做行为改动,理由写下来**:两个方向都改产品行为且方向相反 ——
实现产出者意味着**开任何菜单整个 overlay(含聊天窗)消失**(玩家一边翻背包一边看队伍聊天是常见用法),
删掉死通道则运行时零变化但要丢掉 UI 侧已写好的意图。**隐藏与否是产品决定**,不是审计能替它选的。
所以:事实写进 `docs/FOLLOW-UP.md` **第 7 节**(含「决定之后要同步改哪里」的清单),
在 `root.component.html` 里留下注释(下一个读到它的人不必重新推导),并用门禁**锁住**它。

**后续(2026-10-12)**:按 `docs/FOLLOW-UP.md` §7.3 的「删掉守卫」那一行执行了删除 ——
见 v1.1.9 末尾的「修复:一个包住两个组件的守卫被记成了产品决定」一节。
上面这条「用门禁锁住」的做法正是那次删除的机械保证:豁免与监听必须一起删,少一边门禁就失败。

**扩展门禁** `Tools/Scripts/check_overlay_bridge.py`(第 5 条判据,反向):
**UI 订阅的每个事件都必须有 C++ 产出者**,否则失败;没有产出者的名字必须在
`NO_EMITTER` 里**逐条写明理由**(上面五条的理由就是表里那五行),且**豁免会腐烂**:
某个名字不再被任何 UI 订阅时,门禁要求删掉那条豁免。
这一条真正防的是**改名**:C++ 把 `ExecuteAsync("setVersion")` 改成别的名字,UI 的 handler
从此**再也不运行** —— 没有错误、没有日志,而 mock 里一切照旧。

**验证**:

- `python Tools/Scripts/check_overlay_bridge.py` → `OVERLAY BRIDGE CHECK OK` /
  `58 call(s) verified through all three layers: declared, registered, dispatched`
  (从 §134 的 34 条升到 58 条:新增 24 条事件方向),另有 6 条 note ——
  其中 5 条**逐条点名**这五个死监听并给出理由(删除 `openingMenu` 之后 note 减为 5 条,
  `openingMenu` 那条随之消失,计数仍是 58)。
- **变异测试(三个变异,逐个先确认已落到磁盘,逐个被精确指名)**:
  1. **把 C++ 的 `ExecuteAsync("setVersion")` 改名**(→ `setVersionRenamedForMutation`)→
     `client.service.ts:137 subscribes to "setVersion", which no C++ code emits; the handler never
     runs and the state it drives can never change` —— **这正是改名这个危险情形**;
  2. 在 UI 里加一个没有产出者的监听(`skyrimtogether.on('phantomEvent', …)`)→ 被指名;
  3. 在 `NO_EMITTER` 里加一条背后没有监听的豁免(`phantomExempt`)→
     `NO_EMITTER exempts "phantomExempt", but no UI code subscribes to it; drop the exemption`.
- 门禁已在 CI 的两条触发列表里(§134 就加的 `Code/tp_process/**`、`Code/client/**` 等路径),
  本轮不需要改工作流;`check_workflow_shell.py` 与 `yaml.safe_load` 复核通过。
- C++/TS 由 GitHub CI 编译验证(§0/§1:本机不编译)。

**教训**:**桥的两个方向要分别检查,而「反向」那半通常没人做。**
§134 查的是「UI 调用的名字有没有实现」(call ∈ registered ∈ dispatched);
本轮查的是「游戏推的事件有没有人发」。**两者都是「同一个名字写在两处、没有东西同时看它们」**,
但坏法不同:调用缺实现会**抛异常**(看得见),事件缺产出者会**静默**(handler 永不运行,
状态永远停在初值)。`openingMenu` 就是后者:守卫、类型、subject、消费端**四样俱全**,
唯独没有产出者。

**本节的一条自伤,记下来**:「包住的是整个 overlay」当时**没人去核对** ——
守卫里只有两个组件。这个说法把「实现产出者」的代价说大了一个量级,
于是一个只需删十几行的决定被记成了产品决定。
**影响取舍的事实,要先看代码再写。**

### 修复:消息工厂的抽取表**只查了范围,没查有没有填** —— 一个字节就能让进程终止

**判据是一条二值性质:枚举里的每个 opcode 都必须有一个抽取器槽位。** 四个消息工厂的结构都是:

```cpp
static std::function<UniquePtr<ServerMessage>(TiltedPhoques::Buffer::Reader&)> s_serverMessageExtractor[kServerOpcodeMax];

// 槽位由一个手写的类型清单填:
auto s_visitor = CreateMessageVisitor<AuthenticationResponse, …, NotifyPluginMessaging>;
ServerMessageFactory::Visit(extractor);          // extractor 写 [T::Opcode]
```

而 `Extract()` 只查了**范围**:

```cpp
if (data >= kServerOpcodeMax) [[unlikely]] return {nullptr};   // 越界 → 安全返回
const auto opcode = static_cast<ServerOpcode>(data);
return s_serverMessageExtractor[opcode](aReader);              // ← 槽位可能是空的
```

**范围检查不是完整性检查。** 清单里没点到的 opcode,它的槽位是一个**空的 `std::function`**;
调用空 `std::function` 抛 `std::bad_function_call` —— 而 `Extract()` 是 `noexcept`,
于是 `std::terminate`:**线上一个字节的 opcode 就能终止进程**。这不是「将来可能」的隐患:
本轮就在服务端清单里发现了**三个重复条目**(`NotifyDialogue`/`NotifySubtitle`/`NotifyPlayerDialogue`
各写了两遍)—— **手写清单被人反复编辑,正是漏掉一个名字的操作。**

**实测**:四个工厂的**每一个槽位今天都是填满的**(客户端 53/53、服务端 56/56、admin 1/1 与 2/2),
所以这**不是活崩溃,是活的危险**:唯一挡住它的是「还没人删掉一个名字」。

**修复(两层)**:

1. **抽取器兜底**(四个工厂各一处):调用前检查槽位是否为空,空则返回 `nullptr` ——
   与越界 opcode 的行为一致(丢一个消息,而不是死一个进程);
2. **把「漏名字」变成构建期失败**:新增门禁 `Tools/Scripts/check_message_factories.py`。
3. **顺手修掉那三个重复条目**(服务端清单 59 项 → **56 项,与 56 个 opcode 一一对应**)。

**新增门禁的四条判据**(覆盖四个工厂、112 个 opcode):

| # | 判据 | 为什么 |
| --- | --- | --- |
| R1 | 枚举里每个 opcode 都要有消息类型填进抽取表 | 空的槽位 = `terminate` |
| R2 | 清单里每个类型都要在枚举里有 opcode | 名字陈旧 = 同一个洞的另一面 |
| R3 | **同一个类型不能列两次** | 重复正是「手写清单开始漂移」的指纹,而**下一次编辑可能删而不是重** |
| R4 | 每个声明了 opcode 的消息头都必须被工厂点到 | 能构造、能发送、**永远解不开**的消息 |

**判据怎么把两边的名字对上**:不猜名字。`RemoveSpellRequest` 声明的是 `kRequestRemoveSpell`、
`AdminShutdownRequest` 声明的是 `kAdminShutdown` —— 名字不同构;
而客户端与服务端的消息**共用一个目录**。所以门禁读每个消息头里
`static constexpr <哪个枚举>Opcode Opcode = <哪个名字>;` **这一行本身**,
由**声明的枚举**决定它属于哪个工厂。
(第一版正是猜了名字:`kFooBar` ↔ `FooBar`,于是报了 **116 条假失败** ——
其中 112 条是把服务端的消息算进了客户端。**判据的第一版报错,先怀疑判据**,§140 同一条。)

**验证**:

- `python Tools/Scripts/check_message_factories.py` → `MESSAGE FACTORY CHECK OK` /
  `112 opcode(s) matched to a message type across 4 factories`;
  `--list` 报四个工厂分别是 `53/53`、`56/56`、`1/1`、`2/2`(distinct = 总数,即**零重复**)。
- **变异测试(两个变异,逐个先确认已落到磁盘,逐个被精确指名)**:
  1. **从服务端清单里删掉 `NotifyRemoveSpell`**(= 制造一个空槽位,也就是那个 terminate) →
     `NotifyRemoveSpell.h declares kNotifyRemoveSpell, but the visitor list does not name
     NotifyRemoveSpell; its extractor slot stays an empty std::function and Extract() would terminate
     the process on that opcode` + R4 同时点名「能发不能解」;
  2. **在客户端清单里把 `PluginMessagingRequest` 写两遍**(= 还原那三个重复条目的形状) →
     `PluginMessagingRequest is listed 2 times in the visitor; a repeat means the list is being
     edited by hand and the next edit can drop a name instead of repeating one`。
- **新增回归测试** `Code/tests/encoding.cpp` `[encoding.factory]`:
  `An opcode the factory has no message for is dropped, not fatal` ——
  钉住**两种**越界(恰好 `kClientOpcodeMax`、以及远超它的值)、服务端的同类情形,
  并**同时断言一个真实消息仍然解得开**(否则「一律拒绝」也能让测试通过)。
  测试由 CI 的 `windows.yml`(§ round 1 确认过它会跑 encoding 测试)执行。
- 门禁已接进 CI 两条触发列表(`push`/`pull_request` 各 68 条路径,新增 `Code/encoding/**` 与
  `Code/admin_protocol/**`),`contract` 作业 39 个步骤;`check_workflow_shell.py` 与
  `yaml.safe_load` 复核通过。
- C++ 由 GitHub CI 编译验证(§0/§1:本机不编译)。

**教训**:**「查了范围」和「查了完整性」是两件事,而前者看起来像后者。**
`if (data >= kServerOpcodeMax) return nullptr;` 读起来像一道完整的防线,
但它只挡住了**枚举之外**的值;**枚举之内**却没人填的槽位,同样是空 `std::function`,
同样从 `noexcept` 里抛出去。**索引表的两种坏法:越界,和没填** ——
后者更隐蔽,因为它需要一个**手写清单**与**枚举**保持一致,而这两样东西从来不在一起。

### 修复:玩家**拔/收武器**之后,别人看不见 —— `NotifyDrawWeapon` 从来没被发送过

**判据是一条二值性质:线上的每条消息,要么有人发,要么有人收。**
§142 已经保证「每个 opcode 都有抽取器」;本轮查的是**另一半** ——
消息在枚举里、在工厂里、在访问清单里、`#include` 也在,**就是没有任何代码构造它**。

实测 109 条消息,查出**一条真的死了**:

```
客户端  InventoryService::RunWeaponStateUpdates()
        → 检测到本地 actor 的拔刀状态变了 → 发 DrawWeaponRequest
服务端  InventoryService::OnWeaponDrawnRequest()
        → characterComponent.SetWeaponDrawn(message.IsWeaponDrawn)   ← 只更新了自己的副本
        → 然后……没有然后了
```

而 `NotifyDrawWeapon`(字段正好是 `Id` + `IsWeaponDrawn`)存在、有 opcode、在访问清单里、
被客户端 `InventoryService.cpp` `#include` 着 —— **全仓没有任何一处构造它**
(服务端 `InventoryService.h` 里那个 `struct RequestObjectInventoryChanges;` 前向声明式的痕迹同理)。

**后果**:拔刀状态**只在角色生成时同步一次**(`CharacterSpawnRequest.IsWeaponDrawn` /
`AssignCharacterResponse.IsWeaponDrawn`),**之后的变化永远不广播** ——
所以「玩家 A 当着 B 的面拔刀,只要 B 早就加载了 A,就永远看不到」,
直到 B 重新进格子、A 的角色重新生成一次。**没有任何报错,功能只是不存在。**

**修复**:服务端在更新自己的组件之后,按**同一文件里装备广播的既有写法**广播出去:

```cpp
NotifyDrawWeapon notify;
notify.Id = message.Id;
notify.IsWeaponDrawn = message.IsWeaponDrawn;

const entt::entity cOrigin = static_cast<entt::entity>(message.Id);
if (!GameServer::Get()->SendToPlayersInRange(notify, cOrigin, acMessage.GetSender()))
    spdlog::error("{}: SendToPlayersInRange failed", __FUNCTION__);
```

**客户端**补上接收端:`sink<NotifyDrawWeapon>` → `OnNotifyDrawWeapon`,
把服务端 id 解析回本机 actor(与 `OnNotifyEquipmentChanges` 同样的
`view<RemoteComponent, FormIdComponent>` 查找),状态确实不同才调 `SetWeaponDrawnEx` ——
即**生成路径用的同一个函数**(它会置 `g_forceAnimation` 强制重放动画,否则本地看不到这次拔刀)。

**新增门禁** `Tools/Scripts/check_message_use.py`(两条判据 + 两个方向):

| 判据 | 为什么 |
| --- | --- |
| `ClientMessage` 必须**要么被客户端发、要么被服务端收** | 二者皆无 = 死在线上的一条消息 |
| `ServerMessage` 必须**要么被服务端发、要么被客户端收** | 同上 |
| **「有人收、没人发」必须失败**(而不是通过) | 这正是 `NotifyDrawWeapon` 的形状;见下面那段自伤 |
| 豁免条目必须仍对应真实消息 | 豁免会腐烂(§138/§140 同一条) |

**验证**:

- `python Tools/Scripts/check_message_use.py` → `MESSAGE USE CHECK OK` /
  `109 message(s) checked across 2 direction(s)`,外加 3 条 note
  (`NotifyPlayerDialogue`、`RequestObjectInventoryChanges`、`NotifyObjectInventoryChanges`
  各有理由,见下)。
- **变异测试**:把服务端那段广播**注释掉**(= 还原本缺陷)→
  `NotifyDrawWeapon is handled by the client but nothing on the server constructs it; the handler
  can never run and whatever it updates can never change`。
- 门禁已接进 CI 两条触发列表(各 69 条路径),`contract` 作业 40 个步骤;
  `check_workflow_shell.py` 与 `yaml.safe_load` 复核通过。
- C++ 由 GitHub CI 编译验证(§0/§1:本机不编译)。

**同轮查出的另外两条死通道**(记录理由,**不动代码**):

| 消息 | 真相 |
| --- | --- |
| `NotifyPlayerDialogue` | **功能是活的,只是走的不是它**:玩家对话经 `sendPlayerMessage(kPlayerDialogue, …)` 走 **`NotifyChatMessageBroadcast`**(`MessageType = kPlayerDialogue`)广播,所以客户端 `OverlayService::OnPlayerDialogue` 永不运行 |
| `RequestObjectInventoryChanges` / `NotifyObjectInventoryChanges` | **保留但从未接线**:容器背包走 `ObjectService`;两者除了 include、前向声明、工厂槽位与 opcode 之外没有任何引用 |

**这两组都不删 opcode**:`Opcodes.h` 自己写着「**只追加,从不插入** —— 配套插件按索引挂钩这张表,
所以每个既有 opcode 必须保持它的编号」;删掉**表中段**的一个枚举值会让它之后**全部重编号**,
那正好违反这条规则。所以门禁里给它们逐条写明理由,把状态**锁住**。

**门禁第一版有一条会放过真缺陷的洞(自伤,必须记)**:
最初「既没人发也没人收」才算失败,于是**「有人收、没人发」直接通过** ——
我第一次做变异时把服务端的 `notify` 改名成 `notifyMUTATION`,门禁报 OK,
我一度以为门禁坏了;实际是**我的变异是假的**(改名不等于不发送,消息照样被构造和发送)。
于是做了**真变异**(把整段广播注释掉)才发现:**当时那条规则下,`NotifyDrawWeapon` 这种
「客户端收、服务端从不发」的形状会一路通过** —— 而那正是我要修的缺陷本身。
补上「有人收、没人发 = 失败」之后,真变异才被抓住。
**教训**:**变异要改「行为」,不是改「名字」**;而**门禁漏掉的恰好是它要抓的那一类**时,
只有真变异能暴露。

**教训**:**「消息完整」有四种完整,而检查通常只做前三种。**
有 opcode ✓、有抽取器 ✓、有序列化 ✓、**对面有处理器 ✓** —— `NotifyDrawWeapon` 四样俱全,
依然是一个**从未被发送过的消息**:功能不是坏了,是**根本没接上**。
判据必须问到**「谁构造它」**:一条没人构造的消息,与一条没人监听的消息,
是同一种缺陷的两面。

### 审计:进程内事件总线(**结论:干净**),以及一个只发不读的字段

**先说结论**:本轮**没有找到缺陷**。查了进程内 `entt::dispatcher` 事件总线(153 个 sink、
其中 48 个进程内事件)与 `ServerSettings` 这条「服务器下发玩法策略」的通路,
**两者都是干净的**;唯一记录下来的是一条**只发不读**的字段。下面是判据与**为什么第一版判据错了**。

#### 1. 进程内事件:「订阅了但没人 trigger」——**不可由文本判定**(负结果)

第一版按 `.trigger(` 扫,报出 **30 个事件订阅后从未被触发**,包括
`InventoryChangeEvent`、`CellChangeEvent`、`ActionEvent` 这些**显然在跑**的东西。
逐条查完,原因有三层:

| 我漏掉的 | 实际写法 |
| --- | --- |
| 参数不是裸标识符 | `m_dispatcher.trigger(acMessage.Settings)`、`trigger(ConnectedEvent(id))`、`trigger(Type{…})` —— 实测 4 种形状(类型字面量 3、限定名 4、成员访问 3、其余 61) |
| **还有第二个派发入口** | `World::Get().GetRunner().Trigger(std::move(aEvent))`(`RunnerService.h`)—— 它把 `m_dispatcher.trigger(...)` 包进 lambda 入队,**事件类型只在调用点由实参推导**,文本里根本不出现 |
| 队列 | 同一个 `RunnerService` 还有 `Queue(std::function<void()>)` |

⇒ **「这个进程内事件到底有没有被派发」需要调用点的实参类型,即一次类型感知的扫描** ——
与 §135.5、§140.4 撞的是同一堵墙。**判据做不到,就不发布**(§135 的既定做法);
这里记下来,免得下一轮再撞一次。

#### 2. `ServerSettings` 通路:**干净**

服务器把 7 个玩法策略字段一次性下发(`GameServer.cpp:141-147`),
客户端 `TransportService` 在两处收到后 `m_dispatcher.trigger(acMessage.Settings)`
(`:791`、`:903`)⇒ `PlayerService::OnServerSettingsReceived` **确实会跑**:

| 字段 | 客户端读者 |
| --- | --- |
| `Difficulty` | `PlayerService.cpp:111,113` → `SetDifficulty` |
| `GreetingsEnabled` | `PlayerService.cpp:115` → 关掉问候距离 |
| `DeathSystemEnabled` | `PlayerService.cpp:121` → `ToggleDeathSystem` |
| `PvpEnabled` | `Actor.cpp:1200`、`MagicTarget.cpp:137` |
| `SyncPlayerHomes` | `ObjectService.cpp:158` |
| `SyncPlayerCalendar` | `CalendarService.cpp:36` |
| `AutoPartyJoin` | **没有任何客户端读者** |

**7 个字段里 6 个有人读,服务器端 7 个全部有人写** —— 没有「写了没人读」之外的缺口。

#### 3. 唯一记录项:`AutoPartyJoin` 只发不读

它**不是坏掉的功能**:自动入队完全在服务端决定
(`GameServer::AllowsAutoPartyJoin()` → `PartyService.cpp:136/277` 把玩家加进队伍并逐个发
`SendPartyJoinedEvent`),客户端通过**普通的队伍消息**得知结果,**不需要这个标志**。
所以它是**信息性字段**:发过去、没人读。

**没有删它**:`ServerSettings` 是按固定字段表序列化的,删中间一个字段会让其后**全部位移**;
而**也没有给它硬造一个读者** —— 那会凭空发明一条产品行为。
正确做法是**把它记录下来并锁住**,于是扩展 §135 的门禁:

**`check_wire_field_reads.py` 新增第三个主体 `ServerSettings`**,并顺带修了它自己的两个洞:

1. **结构体解析器只认 `struct X final :`** —— `struct ServerSettings {`(无 final、无基类)解析不出字段;
   更糟的是 `fields_of()` 从匹配点往后找**第一个** `{`,于是拿到的是**内联方法体**
   (`operator==` 的),所以「声明了方法的结构体」的字段全部看起来不存在。已改为从**结构体自己的**
   左花括号开始。
2. **「在到达处被读」对共享结构体不成立** —— 消息的处理器是它唯一的消费者,
   而 `ServerSettings` 是**按需读取**的(`Actor.cpp`、`MagicTarget.cpp`、`ObjectService.cpp`、
   `CalendarService.cpp`)。第一版把它套在 `OnServerSettingsReceived` 一个函数体上,
   于是 `PvpEnabled`/`SyncPlayerHomes`/`SyncPlayerCalendar` 被报成「没人读」——
   **三个假失败**。已让主体支持**目录作用域**(handler 名为空 = 整个 `Code/client`)。

**验证**:

- `python Tools/Scripts/check_wire_field_reads.py` → `WIRE FIELD READ CHECK OK` /
  `14 field(s) of 3 wire type(s) are read where they arrive`,外加一条 note
  (`ServerSettings.AutoPartyJoin is exempt: informational: …`)。
- **变异测试**:把 `PvpEnabled` 的**两个**读者都改成 `if (false)` →
  `Code/client: nothing in that directory reads ServerSettings.PvpEnabled; ServerSettings
  serialises it and the receiver drops it, so whatever fills it has no effect` ✓。
  (先只改一个时**没有**报错 —— 两个读者,去掉一个仍然有人读,**这正是该有的行为**。)
- 门禁已接进 CI 两条触发列表(各 70 条路径,新增 `Code/encoding/Structs/ServerSettings.*`),
  `contract` 作业 40 个步骤;`check_workflow_shell.py` 与 `yaml.safe_load` 复核通过。
- C++ 由 GitHub CI 编译验证(§0/§1:本机不编译)。

**教训**:**判据的「作用域」和「解析器」是两处最容易出错的地方,而它们出错时看起来都像发现了缺陷。**
本轮第一版报了 30 个 + 3 个,全部是判据自己的问题(派发入口漏了一个、作用域套错了一层、
结构体解析器从错误的 `{` 开始)。**先证明判据能看见已知为真的东西**,再去相信它报出来的东西。

### 加固:SKSE 插件版本结构体是**手写的 ABI 镜像**,而它此前只断言了标志位

**判据是一条外部 ABI 的一致性:这个结构体的布局必须与 SKSE64 的 `PluginVersionData` 逐字段相同。**

`Code/skse_bootstrap/main.cpp` 里的 `SksePluginVersionData` 是**手写**的 —— 因为那个 dll 不链接
SKSE 的头文件 —— 而 **SKSE 是按偏移直接从本 dll 的导出里读它的:它永远看不到字段名**。
本轮逐字段核对(9 个字段、类型、数组长度、`sizeof = 848`),**与 SKSE64 `PluginAPI.h` 完全一致** ✓
—— 这一条本身是**干净的**。

**但守卫不完整**:文件里已有 3 个 `static_assert`(标志位 `1 << 1` / `1 << 0`、`kVersion == 1`),
**没有任何一个断言布局**。而布局正是会被人手改坏的东西:

| 改动 | 后果 |
| --- | --- |
| 加一个字段 / 改数组长度 | 其后**每个字段**的偏移全部移动 ⇒ SKSE 把 name 读成 author、把标志位从版本列表里读出来 |
| 交换两个相邻字段 | 同上,而且**类型完全相同**,肉眼与编译都看不出来 |
| 改一个标志位的值 | SKSE 拒绝加载,报的却是「游戏版本」而不是「插件」 |

**而且它是静默的**:导出仍在、名字仍是 `SKSEPlugin_Version`,插件照常被扫描到 ——
这正是 §-round 里那条注释描述的现场(「no version data」那条日志就是这么来的)。

**修法(两层)**:

1. **`static_assert` 钉住布局**(本文件,4 条新增):`sizeof == 848`、
   `offsetof(name) == 8`、`offsetof(versionIndependenceEx) == 772`、`offsetof(seVersionRequired) == 844`
   —— 与 §131 给 `ExtraDataList`/`GameVM` 加的那批同一形状;**编译期**就挡住。
   顺带补上 `<cstddef>`:`offsetof` 需要它,而这是个**没有 PCH** 的独立 dll
   (客户端那几个用 `offsetof` 的文件是靠 PCH 传递的,不能照抄)。
2. **新增门禁** `Tools/Scripts/check_skse_abi.py`:把 SKSE64 的参考布局**写成数据**,
   逐字段比对声明 —— 判据是「**这个声明是否仍描述 SKSE 实现的接口**」,
   而不是「它是否与它自己一致」(后者永远为真)。

**验证**:

- `python Tools/Scripts/check_skse_abi.py` → `SKSE ABI CHECK OK` /
  `15 field/constant(s) matched against the SKSE64 layout`
  (`--list` 打出两个结构体的逐字段布局与三个常量的值)。
- **变异测试(三个变异,逐个先确认已落到磁盘,逐个被精确指名)**:
  1. `char name[256]` → `[260]`(数组改长)→
     `SksePluginVersionData field 2 is char name[260], but SKSE reads char name[256] there` ✓;
  2. **交换 `versionIndependenceEx` 与 `versionIndependence`**(类型相同,肉眼看不出来)→
     两条诊断同时点名这两个字段 ✓(并且**新增的 `offsetof` 断言也会在编译期挡住它**);
  3. `kVersionIndependent_Signatures = 1 << 1` → `1 << 2`(标志位改错)→
     `kVersionIndependent_Signatures is 1 << 2, but SKSE checks 2 (1 << 1); a wrong bit is refused
     with a message about the game version instead of about the plugin` ✓。
- **常量比较比的是「值」不是「文本」**:`1 << 1` 与 `2` 是同一个值,
  门禁求值后再比 —— 否则它会把正确的写法报成错(第一版正是如此,两条假失败)。
- 门禁已接进 CI 两条触发列表(各 72 条路径,新增 `Code/skse_bootstrap/**`),
  `contract` 作业 41 个步骤;`check_workflow_shell.sh`… 即 `check_workflow_shell.py` →
  `WORKFLOW SHELL CHECK OK`、`yaml.safe_load` 通过。
- C++ 由 GitHub CI 编译验证(§0/§1:本机不编译)。

**教训**:**「手写的外部 ABI 镜像」必须同时钉住**语义**和**布局**,而人们通常只钉前者。**
这个结构体已经断言了「标志位该是什么值」(语义),却没有断言「字段在哪」(布局)——
而布局恰恰是**同一类改动**(加字段、改长度、调顺序)最先破坏的东西,
且它的失败方式是**静默的**:导出还在、名字还对,SKSE 只是从错误的偏移读到了别的东西。
**凡是「别人按偏移读我们的内存」的地方,`sizeof` 与 `offsetof` 断言就是那条判据。**

### 加固:插件 C ABI 的**结构体布局**此前完全没有被钉住

**判据是同一形状的第二个落点**:`STRPluginMessagingAPI.h` 是**独立插件仓库各自 vendor 一份**
的 C ABI 头文件,而契约工具 `strpm_contract.py` 此前钉住的是:

| 已钉住 | 没钉住 |
| --- | --- |
| 头文件的 **sha256**(C1) | —— |
| **常量值**(C4:`kInterfaceVersion` 等 6 个) | —— |
| **类型名字**(`interfaceTypes`,24 个名字) | **结构体的字段** |

**后果**:`interfaceTypes` 只是一串**名字**,没有任何字段信息 ——
所以**「改字段顺序 / 加字段 / 改数组长度,然后重新记录 hash」**这条路是**全绿通过**的。
而 C1 的 hash 本来就会被**有意重新记录**(那正是它设计的方式:编辑 → 重记 → 继续),
所以「重新记录」不是漏洞利用,是**正常工作流**。

**这条 ABI 是跨 DLL 边界读的,而且消费者是别人编译的二进制**:

```cpp
// 消费者(独立仓库)按自己 vendor 的那份头文件编译:
STRPM::Target target{ ... };          // 按值传进我们的 send()
const STRPM::Message* message;        // 我们交回去,它逐字段读
STRPM::RuntimeStatus status;          // 同上
```

⇒ 布局不一致时,消费者会**把我们的 `sender.displayName` 从错误的偏移读出来并当成指针解引用** ——
崩溃发生在**它的**插件里,而**构建期没有任何错误**(它编的是自己那份拷贝)。

**修法:契约新增 C5「接口布局」**:

1. `strpm_contract.py` 新增 `struct_layouts()`:抽出每个结构体的**字段表**(类型 + 名字 + 数组长度,**按声明顺序**);
2. `dump` 把结果写进 `contract.json` 的 `interfaceLayouts`(10 个结构体、**48 个字段**);
3. `check` 新增 C5,两层:
   - **主体**:当前头文件的布局必须与 pinned 的布局**逐字段相同** ——
     所以「改完重新 dump」不再能把布局改动**顺手带走**;
   - **每个消费者**:其 vendored 拷贝里**双方都声明**的结构体,布局必须与权威头文件相同 ——
     这才是**真正的跨 DLL 危险**(消费者按旧拷贝编译)。

**验证**:

- `python Code/plugins/tools/strpm_contract.py check` → `CONTRACT OK` /
  `consumers 7`;头文件 pin 未变(`8fdcf481…`,即本轮**没有**夹带任何 ABI 改动)。
- **变异测试(逐个先确认已落到磁盘)**:
  1. **把某个消费者 vendored 拷贝里的 `Sender` 字段重排** ——
     这是**最真实的危险**(消费者按旧布局编译)→
     `C5 [OStimTogether] struct Sender disagrees with the authoritative layout` ✓;
  2. 若只保留「主体」那一层,变异 1 **不会**被发现(见下)—— 两层都必要。
- 邻近的插件门禁同时复核通过:`check_transport_compat.py`(「new opcodes appended, chat indices
  unchanged, docs remain accurate」)、`check_exports.py`(「4 contract entry point(s) exported」)。

**本轮门禁自己踩的三个坑(全部记下来)**:

| 坑 | 症状 | 修法 |
| --- | --- | --- |
| **正则灾难性回溯** | 抽字段的嵌套量词(`identifier\|const\|星号` 三者互相重叠)在**不匹配的行**上有指数级回溯,**读一个头文件 5 分钟没结束** | 改成**线性扫描**(按顶层 `;` 切分声明,再逐条解析)⇒ **0.001 秒** |
| **字段被静默漏掉** | 第一版只认 `类型 名字;`,于是 `const char* channel`(星号贴类型)与**全部函数指针成员**都没进布局 —— `Interface` 只有 1 个字段(应 7 个)、`Message` 4(应 6) | 两条分支:函数指针 + 数据成员;后者把星号在类型/名字两侧的写法都归一化 |
| **过滤器把该查的排除了** | 每个消费者的比较里加了 `struct in used`(只比消费者源码点过名的结构体),理由是「其余是噪音」—— 而 `Sender` **恰恰从不被消费者点名**(它是跟着我们交回的 `Message*` 来的)⇒ **变异 1 静默通过** | **删掉该过滤**:vendored 拷贝声明了的结构体就是 ABI,与消费者是否点名无关 |

**教训**:**「契约」钉住的东西,要和「危险」发生的地方对齐 —— 而人们会钉住容易钉的(哈希、常量、名字),漏掉难钉的(布局)。**
哈希只说明「文件变了」,不说明「变了什么」;而它**本来就会被重新记录**,所以它挡不住「改布局 + 重记哈希」这条**正常流程**。
**类型名字列表**更糟:它读起来像在描述接口,实际一个字段都没描述。
**判据必须落在**语义**上(字段是什么、在哪),而不是**出处**上(文件是否被改过)。**

**教训(同轮第二条)**:**过滤「噪音」的守卫,常常正是漏掉真缺陷的那一处。**
`struct in used` 看起来是精确化(只查用到的),实际是把「消费者不点名但会读到的结构体」全部豁免 ——
而跨 DLL 边界读的结构体**恰恰大多不被点名**(它们是指针后面跟着来的)。
**删掉一个过滤器之后变异才被抓到,这本身就是它一直在放行的证据。**

### 审计:游戏结构体的布局断言(**结论:自洽**),与三次失败的工具

**本轮没有缺陷。** 探针是「`Code/client/Games` 里那些 `offsetof` 断言是否覆盖了代码真正用到的字段」,
**而这个问题在文本层面不可判定** —— 下面是证据,以及**为什么第一版工具报了假结果**。

#### 1. 想要的性质:被断言的字段 vs 被用到的字段 —— **做不到**

第一次尝试抽「每个结构体声明的成员」,结果 `61 个有断言的结构体,0 个解析出成员`。
逐层查下去,**是工具自己的 bug**:

| 工具的错 | 症状 |
| --- | --- |
| `STRUCT` 正则**漏了 `re.M`** | `^\s*struct` 只在文件开头匹配 ⇒ **一个结构体都没找到**,于是 61 行「有断言但没成员」全是这一个标志位的产物 |
| 补上 `re.M` 后,成员解析器把**嵌套类型**当成员 | 这些头文件里有 **153 个嵌套 `struct/class/enum` 声明**、**22 个预处理分支**;`Actor` 因此报出 **114 个「成员」**,而真实字段数远低于此(报出的「未钉住成员」里是 `SpellItemEntry`、`ActorValueModifiers`、`ContactNormal` 这类**嵌套类型名**,以及 `SKYRIM_TARGET_LEGACY`、`TP_PLATFORM_32` 这类**宏**) |
| 断言一致性检查**无视分支结构** | 把 `#ifdef SKYRIM_TARGET_LEGACY / #else` 的两套偏移当成「同一字段被断言成两个值」⇒ **`Actor` 一个文件报出 14 条假冲突**,而那是**设计本身** |

⇒ **「这个结构体的字段是否都被钉住」需要真正的 C++ 解析**(宏展开 + 嵌套作用域),
与 §135.5、§140.4、§144.1 撞的是同一类墙:**文本扫描代替不了类型系统**。

#### 2. 能判定的那一部分:**分支内自洽** —— 干净,且已做成门禁

把问题**缩小到文本能回答的范围**:同一个预处理分支内,一个偏移只能对应一个字段、
一个字段只能有一个偏移(跨分支**允许不同** —— 那正是 legacy/AE 两套布局的意义)。

**实测:192 条断言、47 个文件、55 个分支,`0` 处分支内不一致** ✓ —— 这是**已验证干净**的结论。

**新增门禁** `Tools/Scripts/check_layout_asserts.py`:跟踪 `#if/#else/#endif` 深度,
按分支分组后检查这两条;另有下限(少于 100 条断言即失败,防止形状变化后退化成空转)。
门禁的文档字符串**明确写出它不做什么**(不检查字段覆盖率)以及**为什么**
(153 个嵌套声明 + 22 个分支 ⇒ 文本抽取必然把类型名和宏当字段)——
**把「做不到」写在工具里,比让它安静地报假结果好。**

**验证**:

- `python Tools/Scripts/check_layout_asserts.py` → `LAYOUT ASSERT CHECK OK` /
  `192 assertion(s) in 47 file(s) across 55 branch(es)`。
- **变异测试(两个变异,逐个先确认已落到磁盘,逐个被精确指名)**:
  1. **同一字段在同一分支里断言成两个偏移**(`bitfield == 0x8` 之后再写 `bitfield == 0x10`,即复制粘贴忘了改字段名)→
     `ExtraDataList.bitfield is asserted at both 0x8 and 0x10 in the same branch (#ifdef:SKYRIM_TARGET_LEGACY); one of the two is stale` ✓;
  2. **两个不同字段断言在同一偏移**(`lock == 0x8`)→
     `ExtraDataList: bitfield and lock are both asserted at 0x8 in the same branch …; the struct cannot have both there` ✓。
- 门禁已接进 CI 两条触发列表(各 74 条路径,新增 `Code/client/Games/**`),
  `contract` 作业 42 个步骤;`check_workflow_shell.py` 与 `yaml.safe_load` 复核通过。
- C++ 由 GitHub CI 编译验证(§0/§1:本机不编译)。

**教训**:**工具的每一个 bug 都会伪装成「发现」,而这一轮的工具错了三次。**
`re.M` 漏了 ⇒ 61 行「无成员」;嵌套类型当成员 ⇒ `Actor` 报 114 个成员;
无视分支 ⇒ 14 条「冲突」。**三次都长得像结论,三次都是工具自己的问题。**
判据是:**在相信它报出来的东西之前,先让它看见一件你已知为真的事** ——
这一轮如果先问「它能不能正确解析我手写检查过的那两个字段」,三次错误一次都不会发生。

**教训(同轮第二条)**:**一个性质如果文本层面不可判定,就把它缩小到可判定的那部分,并写清边界。**
「字段是否都被钉住」不可判定;**「分支内偏移是否唯一」可判定** —— 后者虽然窄,
却真的能抓住「复制粘贴断言忘了改数字」这类错误(两个变异都被它抓住了)。
**把「我不查什么、为什么」写进门禁的文档字符串,是让下一个人不重复撞墙的唯一办法。**

### 审计:Papyrus 变量类型标签**定义了两遍**,而没有任何东西把它们连起来

**判据是两个组件必须给同一个标签空间编同样的号。** 这个标签在仓库里出现两次,
**由两个永远看不到对方代码的组件各自定义**:

| 位置 | 枚举 | 用途 |
| --- | --- | --- |
| `Code/components/es_loader/Records/Chunks.h` | `Type` | 解析 `.pex` 脚本块(`Chunks.cpp` 里 `case Type::OBJECT_ARRAY`) |
| `Code/client/Games/Skyrim/Misc/BSScript.h` | `BSScript::Variable::Type` | 向 VM 注册 native(`BSScript.cpp` 传 `kBoolean`),VM 也用它回传返回类型 |

**同一个标签空间,两套拼写,零个共同名字**:

```
loader:  INVALID=0  OBJECT=1  WSTRING=2  INT=3  FLOAT=4  BOOL=5   … ARRAY=11..15
client:  kEmpty=0   kObject=1 kString=2  kInteger=3 kFloat=4 kBoolean=5   kMax=16
```

**实测 11 处对应关系全部一致** ✓(六个标量标签逐一相等;五个数组标签都落在
`kMax = 16` 之内)—— 也就是说**今天是对的,但没有任何东西在维持它**。

**为什么这算真缺口**:两边各自改一处,**两个枚举都照样编译**,各自也照样自洽 ——
失败形式是**静默地误解码一个 Papyrus 变量**,而那条路径正是插件/脚本一侧。
这与 §145/§146 是同一形状:**同一个事实写在两处,而没有任何东西同时看它们**。

**新增门禁** `Tools/Scripts/check_papyrus_types.py`,两条判据:

| 判据 | 为什么 |
| --- | --- |
| R1 六个标量标签在两侧**同值** | 对应关系**不能按名字找**(两侧零个共同名字),所以配对表**写死在门禁里**并附理由 |
| R2 loader 的每个**数组标签**都低于 client 的 `kMax` | client 的 `kMax` 才是「VM 表能到多远」的那份声明 |
| 下限:标量 < 6 或数组 < 4 即失败 | 枚举形状变了要报错,不能退化成空转 |

**验证**:

- `python Tools/Scripts/check_papyrus_types.py` → `PAPYRUS TYPE CHECK OK` /
  `11 tag(s) compared between Chunks.h and BSScript.h`。
- **变异测试(两个变异,逐个先确认已落到磁盘,逐个被精确指名)**:
  1. **删掉 client 侧一个隐式编号的枚举值**(`kFloat`)→ 报
     `FLOAT/kFloat is missing …` **与** `BOOL/kBoolean is missing …` **两条** ——
     这正是要的效果:**隐式编号的枚举里删一个,它后面的全部重编号**,所以第二个名字也一起错位;
  2. **把 client 的 `kMax` 从 16 降到 12** →
     `STRING_ARRAY/INT_ARRAY/FLOAT_ARRAY/BOOL_ARRAY … at or above the client's kMax (12); the VM
     table it is read against does not reach that far`(四条,逐个点名)。
- 门禁已接进 CI 两条触发列表(各 77 条路径,新增 `Chunks.*` 与 `BSScript.*`),
  `contract` 作业 43 个步骤;`check_workflow_shell.py` 与 `yaml.safe_load` 复核通过。
- C++ 由 GitHub CI 编译验证(§0/§1:本机不编译)。

**教训**:**「零个共同名字」本身就是一种危险信号。**
两个枚举**没有任何一个标识符相同**,所以**任何按名字做的检查都看不见它们的关系** ——
而它们编的是同一个号。**当两份定义共用 0 个名字却必须一致时,那份对应关系只能由人写下来**
(门禁里的配对表就是它),**而写下来的那一刻,它才第一次变成可检查的**。

**教训(同轮第二条)**:**隐式编号的枚举里,「删一项」不是「少一项」,是「后面全变」。**
变异 1 之所以同时报出两个名字,正是因为 `kFloat` 之后的所有标签都往前挪了一位 ——
**报出来的第二条诊断才是这个改动真正的后果**。**给必须稳定的标签显式编号,或者用门禁守住位置。**

### 审计:仓库**自己的提交清单**(§0)里,哪些条目只是文字、哪些真的在跑

前两轮(§131、§134)各自撞到过同一件事:§0 的清单上写着一条规则,
**却没有任何东西在执行它**。于是这一轮把 §0 整个读一遍,逐条问**「谁在看它」**,
分成三类:**已经有门禁的**、**能变成门禁的**、**只能靠人读的**。

清单里能判定的部分里,有两条此前完全没有守护,而它们各自连着一次真实的返工:

**一、安装向导的两条语言分支必须提供同样的东西。**
`GameFiles/Skyrim/fomod/ModuleConfig.xml` 把整套安装步骤**写了两遍** ——
一份 `lang = zh`、一份 `lang = en`。插件的**名字是翻译过的**,所以两边对不上名字;
能对的是**结构与载荷**。新增门禁 `Tools/Scripts/check_fomod_languages.py`,三条判据:

| 判据 | 为什么 |
| --- | --- |
| R1 两条分支的安装步骤数相同 | 分支少了整段,只有一边的玩家看不到 |
| R2 逐步,两条分支的插件选项数相同 | 步骤对齐了但选项数不同,同样是漏 |
| R3 逐步,两条分支安装的**文件夹与文件完全相同** | 这条才是要害:**加在中文分支里而英文分支没有的插件,英文玩家根本装不上** |

R3 之所以最关键,是因为**它没有报错**:向导照着自己文件里的内容执行,一切正常,
**玩家只是少了一个组件,而且永远不知道自己少了**。
下限 `MIN_STEPS = 2` / `MIN_PLUGINS = 4`:文件被换掉或解析失败时要失败,不能退化成空转。

**三个变异逐条命中**(每个都先读回磁盘确认,再跑门禁;全部回退到**字节相同**):
在 zh 分支多加一个文件夹 → R3 报 `only in zh: ['OptionalPlugins/MUTATION_ZH_ONLY']`;
从 zh 分支删掉整个插件选项 → R2 报 `6 plugin option(s) in en and 5 in zh`;
把某一步的 `flagDependency` 从 `zh` 改成 `en` → R1 报 `5 install step(s) and zh declares 3`。
第三个最贴近真实返工:**复制一整段步骤去改语言标记时漏改一个**,那一步就**只出现在一种语言里**。

**回退本身也踩了坑**:**第一次用 `git checkout --` 回退被沙箱拒绝**
(`Unable to create '.git/index.lock': Permission denied`),而**失败时文件仍是变异的版本** ——
若不复核哈希,就会把「变异还在」当成已回退,**后面所有结论都建立在脏文件上**。
改用编辑工具反向替换后**逐字节比对哈希确认**(`36375D4F…`),才算回退成功。

**二、构建要求的 xmake 不能高于 CI 安装的 xmake。**
这是**两处声明**,而且**不是同一类声明**,所以「让它们相等」既不是规则也做不到:

```text
xmake.lua:8                       set_xmakever("3.0.0")     <- 下限
.github/workflows/windows.yml     xmake-version: 3.1.0      <- 实际装的
```

真正的不变式是**装的 ≥ 要求的**。**把下限抬到 pin 之上,每次构建都会失败**,
而 xmake 报的是「这个项目」而不是「这个工具链」,所以症状指向项目、根因在版本。
新增门禁 `Tools/Scripts/check_xmake_version.py` 按这个方向检查,并逐个打印读到的数:

```text
note: xmake.lua requires >= 3.0.0; workflows install windows.yml 3.1.0
XMAKE VERSION CHECK OK
every workflow installs an xmake at or above the version the build requires
```

### 修复:**一个「无法检查时报告成功」的门禁** —— 而且它就在 CI 里,一直显示绿色

这一轮的**活缺陷**,是上一轮自己写下的门禁里找到的。

`Code/plugins/tools/check_workflow_shell.py` 用 `try/except ImportError` 包住 `import yaml`,
失败时打印 `POWERSHELL PARSE CHECK SKIPPED` 并 **`return 0`**。它的注释写着
「CI 会先装好 pyyaml」——**而跑它的那个作业 `contract` 什么都不装**:
整个 `plugins.yml` 里唯一一句 `pip install pyyaml` 在 **`wizard` 作业**里。

```text
contract 作业           pyyaml 从未安装  ->  ImportError  ->  SKIPPED + exit 0  ->  CI 绿色
wizard 作业             装了 pyyaml        (但 wizard 不跑这个门禁)
```

所以这个门禁**在 CI 里从来没有解析过任何东西,却一直是绿的**。
这正是它本该防住的那类缺陷 —— **而「跳过」与「通过」对调用方是完全相同的输出**,
这也是它比「门禁根本不存在」更糟的原因:不存在的门禁**看得见**。

**两处修改,方向相反但都要做**:

1. **门禁改为失败**:捕获 `ImportError` 后 `raise SystemExit`,并把原因写清楚
   —— pyyaml 是**需求**而不是优化,并明确写出「不要把它降级成跳过」;
   另外补了一条**空语料保护**:一个 PowerShell 块都没找到时也失败,
   因为「没找到要检查的东西」与「检查完都没问题」同样无法区分。
2. **CI 满足这个需求**:在 `contract` 作业里那一步加上
   `python -m pip install --quiet --disable-pip-version-check pyyaml`,
   并说明为什么装在这里(在**跑这个门禁的作业里**装,而不是别处)。

**验证**(两向都验,这是本轮的关键):

```text
python Code/plugins/tools/check_workflow_shell.py        -> WORKFLOW SHELL CHECK OK  (exit 0)
python .scratch_missing_dep.py <该脚本> yaml              -> WORKFLOW SHELL CHECK CANNOT RUN
                                                             VERDICT: FAIL (correct)
```

后者用一个**只把 `yaml` 变成不可导入**的包装器运行同一个脚本,
证明它在缺依赖时的行为**从「绿」变成了「红」**,而不是只在改动前后看起来不同。

### 新增门禁 `Tools/Scripts/check_gate_integrity.py`:守住「门禁本身」

上面那个缺陷不是孤例,而是**一类**:**门禁的可信度有三个各自独立的问题**,
FOLLOW-UP 里早就把它们写成了三条(它会跑吗 / 它会失败吗 / 它看全了吗),
但**这三条此前也只是文字**。于是把它们变成会跑的检查,七条判据:

| 判据 | 防的是什么 |
| --- | --- |
| R1 没有 `except ImportError` 吞掉错误 | **本轮修的活缺陷**:无法检查时返回 0 |
| R2 没有未经登记的 `SKIPPED` 横幅 | 「跳过」与「通过」输出相同,每个跳过都要写明理由 |
| R3 磁盘上每个门禁都被某个工作流调用 | 门禁没人跑 = 带步骤的注释 |
| R4 每个门禁都被「跑它的那个作业」的路径触发覆盖 | 步骤在,但改动它不会启动作业 |
| R5 同一个工作流的 `push` 与 `pull_request` 路径列表一致 | 只加一边 = 一个方向有守护、另一个方向没有 |
| R6 **工作流点名的门禁必须在磁盘上存在** | **本轮在门禁自己身上找到的缺陷,见下** |
| R7 跑门禁的作业必须安装了该门禁依赖的包 | **本轮修的活缺陷的根因**(`IMPORT_TO_PACKAGE` 表把 `yaml` 映射到 `pyyaml`) |

**R6 是写这个门禁时,在它自己身上发现的。** 最初的 R3 是「磁盘上的每个门禁都被调用」——
**单向**的。于是删掉 39 个门禁脚本后,它照样打印:

```text
note: 1 gate script(s), 0 never run by CI
GATE INTEGRITY CHECK OK
```

**而工作流里仍然点着 40 个。** 一个**语料可以凭空消失而结论不变**的检查,
正是这个文件整篇在讲的那种缺陷 —— **所以把它补成双向**,并要求**两个方向各报一个数**:

```text
note: 40 gate script(s), 0 never run by CI
note: 40 gate script(s) named by workflows, 0 missing
```

两个数**必须同时是 40**:第一个掉了说明有门禁没接线,第二个掉了说明有步骤指向不存在的文件。

**顺带查出的四条真实问题**(全部当场修掉):

- `contract` 作业里**没有**任何 `pip install`,而它跑的两个门禁都要 pyyaml
  (就是上面那条活缺陷);
- `Tools/Scripts/check_fomod_languages.py`、`check_xmake_version.py`、`check_gate_integrity.py`
  **三条路径触发里都没有** —— 步骤加了,但**改动这三个门禁自己不会启动作业**;
- `.github/workflows/**` 此前不是任何触发路径 —— 而 R5/R7 读的正是这些文件,
  **改工作流不会让检查工作流的门禁跑起来**;
- `plugins.yml` 的 `push`/`pull_request` 两条路径列表里各有 **6 个重复条目**
  (`Code/client/**` 出现 3 次)。对 GitHub 无害,但它是**手改列表**的指纹,
  所以门禁**报告**它而不失败。

**变异测试(七个变异,逐个先确认已落到磁盘,逐个被精确指名)**:

| 变异 | 结果 |
| --- | --- |
| 给一个门禁加上吞掉 `ImportError` 的 `try/except` | CAUGHT(`ImportError`) |
| 给一个门禁加上 `print("… SKIPPED")` | CAUGHT(`SKIPPED`) |
| 新增一个**没有任何工作流点名**的门禁脚本 | CAUGHT(`never run`) |
| 两条路径触发列表里**同时**改掉一个门禁的名字 | CAUGHT(`path trigger`) |
| **只改 `push`** 那条列表,加一条 | CAUGHT(`differ`) |
| 从 `contract` 作业里**删掉那句 `pip install pyyaml`** | CAUGHT(`never installs`) |
| 把 40 个门禁删到只剩 1 个 | CAUGHT(39 条 `does not exist on disk`) |

**其中四个变异第一次跑是 MISSED,而四个里三个是变异自己写得不精确** ——
这一条比结果本身更值得记下来:

- 变异 3 原本改的是一个**已存在**的门禁脚本,**而工作流仍然按路径点着它**,
  所以 R3 看不见 —— 改成**新增**一个脚本才测到;
- 变异 4 原本**只改一条**列表,于是先触发的是 **R5**(两条列表不一致),
  而 R5 命中时会跳过 R4 —— 改成**两条一起改**才测到 R4;
- 变异 5 原本把新条目写成 **4 个空格缩进**(YAML 里跑到了别的层级),
  门禁**直接抛 traceback 而不是报告失败** —— 这里**修的是门禁**:`yaml.safe_load`
  包上 `except yaml.YAMLError`,把「这个工作流根本解析不了、所以里面每个步骤都是死的」
  报成一条失败;
- 变异 6 原本给门禁加一句 `import requests`,触发的是「这个包名没登记」而不是
  「作业没装它」—— 改成删掉那句 `pip install pyyaml` 才测到 R7。

**教训**:**变异测试也会撒谎,而它撒谎的方式和门禁一样 —— 变异没生效、或者生效在别的地方。**
所以**每个变异都要先读回磁盘确认**;更要紧的是,**当变异 MISSED 时,先怀疑变异,再怀疑门禁**,
但**两个方向都要查**:上面第三条 MISSED 的结果是**门禁真的有问题**(抛 traceback),
而另外三条是**变异写歪了**。**分不清这两者,就会把「门禁不行」和「测试不行」混为一谈。**

**教训(同轮第二条)**:**一个检查「X 必须被登记」的规则,必须同时检查「登记的东西必须存在」。**
单向的版本对**语料缩小**完全无感:它遍历的是**它找到的东西**,
所以**删掉语料 = 少找到几个 = 仍然全部合规**。
**凡是「遍历发现集合并要求每个成员满足某性质」的检查,都要配一条反向规则,
并且两个方向都要打印计数** —— 两个数一起看,才看得出语料有没有消失。

**验证**:40 个门禁全部跑过一遍,**40/40 通过**;
`plugins.yml` 经 `yaml.safe_load` 复核:两条触发列表各 **81 条(74 条唯一)**、
集合相等,`contract` 作业 **46 个步骤 / 40 个门禁步骤**;
`check_workflow_shell.py` 复核通过(每个 PowerShell 块都能解析)。
C++ 由 GitHub CI 编译验证(§0/§1:本机不编译)。

### 修复:Papyrus `native` 的**两份手写清单** —— 一个「报了失败,却什么也没回答」的原生函数

**怎么找到的**:按 §134/§143 的判据 —— **同一个名字写在两个地方、而没有任何东西比较它们**。
这里的两处隔着**语言边界**:`.psc` 里的 `Function Foo() global native` 是一句**承诺**,
C++ 里的 `RegisterFunction("Foo", "ClassName", …)` / `BindNativeMethod(new …Func("Foo", …))`
才是**兑现**。查了 4 个第一方脚本目录(`GameFiles/Skyrim/scripts/source`、
`plugins/TradeTogether/Source/Scripts`、`plugins/OStimTogether/compat/OStimUIConsent/…`、
`plugins/OStimTogether/optional/OCumIntegration/…`)与全树 232 个 C++ 源文件:
**9 个声明 / 9 个注册**,名字与类名**全部一致**(加的是守护)。

**但顺着调用链往下读一层,发现的是真缺陷。**
`Code/client/Games/Skyrim/Misc/BSScript.cpp` 里 `IsRemotePlayerFunc` 与 `IsPlayerFunc`
的 `MarshallAndDispatch` 都长这样:

```cpp
Actor* pActor = pArg1->ExtractComplexType<Actor>();
if (!pActor)
    return false;          // <- 报告「这次调用失败了」,而 apResult 从头到尾没被写过
```

**`return false` 不是「返回 false」,而是「这次调用失败」** —— 于是 VM 用**返回类型的默认值**
代替结果,脚本拿到的是 `false`。而这个分支**不是异常路径**:
所有出货调用点都先做一次**不检查结果的** `as Actor` 转换
(`TempleBlessingScript.psc:7`、`ResourceFurnitureScript.psc`、`HallofStoriesDiskScript.psc` 三处),
非 Actor 的激活物就是 `None`,**每一次普通激活都会走进这个分支**。
两处都改成:

```cpp
if (!pActor)
{
    apResult->Set<bool>(false);   // None 是一个「答案」,不是一次「失败」
    return true;
}
```

**同一轮第二个缺陷**:`Code/client/Games/PapyrusFunctions.cpp:19` 的 `IsPlayer` 第一行是
`spdlog::info("Calling IsPlayer");` —— 一次**无条件**的日志,脚本每次调用都写一行。
删掉。

### 新增门禁 `Tools/Scripts/check_papyrus_natives.py`

五条判据:**R1** 每个第一方 `native` 声明都有同名同类的注册;**R2** 每个注册都对应一个存在的声明
(单向规则对**语料缩小**无感 —— §149 的教训);**R3** C++ 侧写明参数个数时(`NativeFunction(...)`
基类构造的最后一个参数)**必须等于** `.psc` 的参数个数(VM 会拒绝参数个数不符的调用);
**R4** 注册里的类名必须是一个真实存在的 `Scriptname`;**R5** 任何 `MarshallAndDispatch`
**都不许** `return false;`(上面那个缺陷的判据本身)。刻意**不查** SKSE 侧参数**类型**
(它由 C++ 签名在编译期推导,源码里没有数字),这条边界写在门禁的 docstring 里。

```text
note: 9 native declaration(s) found in 4 first-party script dir(s)
note: 9 registration(s) found across 232 C++ source(s)
note: 3 native function class(es) state a parameter count: DidLaunchSkyrimTogether, IsPlayer, IsRemotePlayer
note: 3 MarshallAndDispatch body(ies) inspected for a result that is never written
PAPYRUS NATIVE CHECK OK / every declared native is registered under its own name
```

**门禁自己的三个坑,都是「跑起来」才暴露的**:

1. **R3 一次都没跑过**:参数个数表是**按文件**建的,而这三个原生函数**注册**在
   `Code/client/Games/Misc/BSScript.cpp`、**构造**在 `Code/client/Games/Skyrim/Misc/BSScript.cpp` ——
   两个文件,于是三条全部报「没有基类构造,参数个数未写明」。改成**全树扫描**才真正执行;
2. 键名不匹配:`BIND` 抓到的是 `IsRemotePlayerFunc`,而构造表以 `IsRemotePlayer` 为键;
3. R4 里有一句**恒真式**(`cls not in known_classes and cls not in {…declared}` 是同一个集合写了两遍)。

**而 R5 的第一版,恰好漏掉了它自己为之而写的那个缺陷**:判据写的是
`returns_false and not sets_result`,但**成功路径里也有** `apResult->Set<`,
所以 `sets_result` 恒为真 —— 把 `if (!pActor) return false;` **改回磁盘**、门禁照样打印 OK。
收紧成 `re.search(r"\breturn\s+false\s*;", body)`(花括号配对取出函数体、剥掉 `//` 注释
—— §143 的教训:**被注释掉的代码不是代码**)之后才抓得住。

**四个变异**(每个都先读回磁盘确认,每个都被抓,每个都已还原):
参数个数 `1 → 2`(R3);`"IsPlayer" → "IsPlayerMUTATION"`(R1+R2 双向同时命中);
类名 `"SkyrimTogetherUtils" → "…UtilMUTATION"`(R4);把 `if (!pActor) return false;` 改回去(R5)。

### 新增门禁 `Tools/Scripts/check_shipped_pex.py`:出货的 `.pex` 与它的 `.psc` 此前**没有任何关系**

同一轮的第二处「两份手写清单」:**`.psc` 是源码,`.pex` 才是真正出货的东西**。
`GameFiles/Skyrim/scripts/` 下 10 个 `.psc` 与 10 个 `.pex` 一一对应,
`Tools/Packaging/New-STNModPackage.ps1:105` 打包的是 `.pex`,
而**全树没有任何东西读它、比较它**(`windows.yml` 的编译步骤故意写到临时目录、
不覆盖 `GameFiles`)—— 也就是说**改了 `.psc` 而没重新编译,出货的是旧脚本,而源码是读者信任的那份**。

判据:每个 `.psc` 必须有 `.pex`(大小写不敏感配对 —— `PetFramework_PetQuest.psc` 出货名是
`petframework_petquest.pex`)、每个 `.pex` 必须有 `.psc`、**声明的每个名字**
(Scriptname / `Function` / `Event`)**必须出现在 `.pex` 的可打印串表里**、
**每个 ASCII 字符串字面量也必须出现**(多行字面量**逐行**查 —— `.pex` 里存的就是逐行的一项);
下限 `MIN_SCRIPTS = 8`、`MIN_LITERALS = 30`(空语料必须失败)。
刻意**不查**控制流、算术,以及**既不碰名字也不碰字面量**的编辑 —— 理由写在文件头:
已提交的 `.pex` 出自原始编译器,`SkyrimTogetherVerifyLaunchScript.psc` 当初是**照着它 `.pex`
里已有的字节**重写的,所以「重新编译得到逐字节相同的产物」本来就做不到。

```text
note: 10 shipped script(s) paired with their source
note: 63 name(s) and 64 literal(s) looked up in the string table
SHIPPED PEX CHECK OK / every .pex shipped names what its .psc declares
```

两个变异:改一个字面量(`ResourceFurnitureScript.psc:121` 的 `"SoundPlay . NPCHumanWoodChop"`)
与改一个事件名(`TempleBlessingScript.psc:5` 的 `OnActivate`),都被抓并逐条指名,都已还原。

**验证**:42 个门禁全部跑过一遍,**42/42 通过**;
`plugins.yml` 经 `yaml.safe_load` 复核:两条触发列表各 **84 条**、集合相等,
`contract` 作业 **48 个步骤 / 42 个门禁步骤**;
`check_workflow_shell.py`(69 个 PowerShell 块)与 `check_gate_integrity.py` 复核通过。
C++ 由 GitHub CI 编译验证(§0/§1:本机不编译)。

### 修复:一个包住两个组件的守卫被记成了产品决定 —— `openingMenu` 死通道已删

**这是 §141 的收尾,也是它的一处订正。** §141 记录:UI 订阅的 `openingMenu` 事件没有任何
C++ 产出者,而 `root.component.html` 用它包住了 overlay。当时写成「包住整个 overlay」,
据此判定「实现产出者会让整个 overlay 在菜单打开时消失」,于是记成产品决定、不动行为。

**核对之后发现那是错的。** 守卫里只有两个组件:

```html
<!-- 删除前 -->
<ng-container *ngIf="!(menuOpen$ | async)">
  <app-debug></app-debug>
  <app-group></app-group>
</ng-container>
```

`app-debug` 是 F2 调试面板,`app-group` 是队伍成员列表。**在守卫之外**的是:
聊天窗(`app-window.app-root-chat`)、通知(`app-notifications`、`app-notification-popup-container`)、
主菜单(`app-root-menu`/`app-root-status`)与整个弹窗层(`app-root-popups`)。
⇒ 「实现产出者」的代价不是「整个 overlay 消失」,而是「F2 面板与队伍列表消失」。

**于是决定改为删除**(记录在 `docs/FOLLOW-UP.md` 第 7 节,状态已改为「已完成」)。
理由不是「哪个更省事」,而是**空行为的存在成本**:一个被记录的空行为比一个不存在的功能
更容易被误认为已实现 —— 而它连 mock 都不发,浏览器里也看不出差别。

**删掉的东西(逐条对账)**:

| 层 | 落点 |
| --- | --- |
| 守卫 + 说明 | `Code/skyrim_ui/src/app/components/root/root.component.html:2-16`(`app-debug`/`app-group` 提到原父节点,位置不变) |
| 订阅字段 | `Code/skyrim_ui/src/app/components/root/root.component.ts` 的 `menuOpenC++ 由 GitHub CI 编译验证(§0/§1:本机不编译)。 |
| 监听 + subject + handler | `Code/skyrim_ui/src/app/services/client.service.ts`(`on('openingMenu')`/`off('openingMenu')`/`onOpeningMenu`/`openingMenuChange`,704→689 行) |
| 类型 | `Code/skyrim_ui/src/typings.d.ts`(`OpeningMenuCallback` + 两条 `event: 'openingMenu'` 重载) |
| 门禁豁免 | `Tools/Scripts/check_overlay_bridge.py` 的 `NO_EMITTER['openingMenu']` |

**门禁自证**:`check_overlay_bridge.py` 第 5 条判据是**双向**的 ——
`NO_EMITTER` 豁免的名字若不再被任何 UI 订阅 ⇒ 失败;UI 订阅的名字若没有 C++ 产出者 ⇒ 失败。
所以**豁免与监听必须一起删**,少一边门禁就失败。这正是 §141 那句「用门禁锁住状态」的兑现。

**验证**:

- `python Tools/Scripts/check_overlay_bridge.py` 改前 / 改后均 `OVERLAY BRIDGE CHECK OK` /
  `58 call(s) verified through all three layers: declared, registered, dispatched`;
  note 从 6 条降到 5 条,消失的正是 `openingMenu` 那条。
- **变异**:把 `skyrimtogether.on('openingMenu', …)` 加回 `client.service.ts` 而豁免不恢复 ⇒
  `OVERLAY BRIDGE CHECK FAILED (1)` /
  `Code/skyrim_ui/src/app/services/client.service.ts:130 subscribes to "openingMenu", which no C++
  code emits; the handler never runs and the state it drives can never change. Either emit it or
  record why it is dead in NO_EMITTER`;还原后回到 OK。
- `git grep -n "openingMenu"` 改后只剩 5 条历史文档行与 `root.component.html` 一条「已被删除」注释。

**订正同步到三处**:`docs/PITFALLS.md` §141(表格行、代码块、「141.3」节与教训)就地标注
「整个 overlay」是错的并指向 §151;`CHANGELOG.md` 的 §141 那条审计小节同样就地标注;
`docs/FOLLOW-UP.md` 第 7 节整节改写为「已决 + 已执行」。

**教训**:**影响取舍的事实,要先看代码再写。**
「包住整个 overlay」这句当时没人核对 —— 它把「实现产出者」的代价说大了一个量级,
于是一个只需删十几行的决定被记成了产品决定,还写进了三份文档。

### 修复:overlay 自己的系统消息也是**散文当翻译键** —— 门禁只查了 C++ 那一侧

§137 修的是 C++ 侧:`SendSystemMessage("英文散文")` 送进聊天,而聊天把系统消息
**当翻译键**渲染(`Code/skyrim_ui/src/app/components/chat/chat.component.html:12-14` 的
`{{ message.content | transloco }}`),于是玩家看到的是句子本身。当时补了
`check_ui_contract.py` 的第 4 条判据。

**但它只扫 `Code/client` 与 `Code/server` 的 `.cpp`。** overlay 自己也会往同一个聊天里
推系统消息 —— `pushSystemMessage('...')` —— 走的还是同一条渲染路径。实测 10 个调用点里
**9 个是点号大写键,1 个是散文**:`Code/skyrim_ui/src/app/services/party-hint.service.ts:43`
的 `'Party hint timer has been reset.'`。

修法两行:`Code/skyrim_ui/src/assets/i18n/en.json:195` 在 `SERVICE.PARTY_HINT` 段新增
`RESET`,调用点改引用该键。

**只补 en.json 的依据**(实测,不是推断):`check_i18n_params.py:146` 是
`if key not in keys: continue  # a missing key falls back to en, and section 137 reported that separately`,
即「某语言缺某键」被显式跳过 —— 与 §137.4 的 `fallbackLang: 'en'` 裁定一致。
新键文案不含 `{{占位符}}`,所以不进入 `check_i18n_params.py:134` 的 `reference` 集合,
R1/R2 的计数逐字不变(实测 `16 parameterised key(s), 12 call site(s), 3 runtime code(s)`,改前改后相同)。

**门禁扩到 overlay 侧**:`check_ui_contract.py` 新增 `UI_SRC`、正则
`SYSTEM_MESSAGE_TS = re.compile(r"pushSystemMessage\(\s*'([^']+)'")`,以及一个**独立的**扫描块 ——
不与 C++ 的命中合并后再判空,因为「某一侧扫到 0 个」正是 helper 被改名/搬走的样子,
先合并会让另一侧的命中把它盖住。两侧合并后跑同一条
`if text not in languages[FALLBACK_LANGUAGE]` 检查。

**验证**:

- `python Tools/Scripts/check_ui_contract.py` 改前 / 改后均 exit 0;
  改后 note 如实列出 `['SERVICE.COMMANDS.TELEPORT_FAILED', 'SERVICE.PARTY_HINT.RESET'] are missing
  from at least one language and fall back to English`(新键只在 en,符合裁定)。
- **变异(a)**:把 `party-hint.service.ts:43` 换回散文 ⇒ `UI CONTRACT CHECK FAILED (1)` /
  `Code/skyrim_ui/src/app/services/party-hint.service.ts:43 sends "Party hint timer has been reset."
  to the chat, which renders system messages with \`content | transloco\`; that is a translation key,
  and en.json has no such key, so what the player sees is the string itself`;还原后 OK。
- **变异(b,空扫描防护)**:把 5 个文件的 `pushSystemMessage(` 改成
  `// pushSystemMessageRenamed(` ⇒ `UI CONTRACT CHECK FAILED (1)` /
  `no pushSystemMessage string was found in the overlay sources, so the "a key, not a sentence" rule
  covered nothing there; the overlay send helper moved or changed shape`;还原后 OK。

**一条已知的边界(如实记下)**:同一个变异如果把调用点**注释掉**而不是改名
(`// pushSystemMessage(` 后接换行与键),门禁**不会**报警 —— 正则的 `\s*` 允许跨行,
注释形式依然命中。**这是设计边界,不是漏洞**:该判据要抓的是「整侧扫空」,
即 helper 被改名或搬走;注释掉一个调用点并不使键消失,也不改变玩家看到的东西。
要连注释形式一起抓,得先剥注释或收紧正则 —— 那会让判据从「这侧还有没有这种调用」
变成「这种调用有没有被注释」,是另一条规则,本轮不做。

**教训**:§137 的判据写的是「聊天把系统消息当翻译键」,**但落点只写了一半**。
同一个渲染路径有两个上游(C++ 与 overlay),判据只覆盖了一个 ——
**当一条规则讲的是「渲染路径」时,要把喂它的每一侧都列出来,再逐侧扫描。**

### 修复:空访问器门禁补认 `if (pX)` 与 `&& pX` 两种形状,并补上 15 处「赋值-解引用」型漏判

**这一条与 `### 修复:全项目审计 —— 15 处「引擎单例还没建好就解引用」...` 修的不是同一批站点。**
上一轮那 15 处是**同一条语句里** `Accessor::Get()->...` 的形态,而当时的门禁扫描窗口就是「一条语句」,
所以那一批它能看见。这一轮报出来的 15 处是**赋值与解引用分成两行**的形态
(`auto* pX = Accessor::Get();` 然后 `pX->...`),旧门禁连检查的机会都没有 ——
把它指向改动前的 13 个文件,它报的是 **0 个站点**;同一份树换成本轮门禁报 **15** 个,逐行等于人工核对清单。

**6 条误报是另一回事**。把形状表换回上一轮的(不认 `&&`/`||` 右操作数、不认裸 `if (pX)`),
当前树报 **6** 条,全部是**已经正确判空**的站点:`PlayerCharacter.cpp:115` 的 `if (pTes && pModManager)`、
`DebugService.cpp:350` 与 `DiscoveryService.cpp:139`/`:269` 的 `if (pX)`、`DiscordService.cpp:53` 的
`if (!m_bCustomPresence && pPlayer)`、`PlayerService.cpp:311` 的 `if (pPlayer)`。
`if (pProcessLists)` 与 `if (pProcessLists != nullptr)` 是同一个条件,门禁此前只认后者 ——
于是它把写了守卫的代码报成缺陷,同时把没写守卫的代码放过去。本轮把两种形状补进 `test_of()`,
`--selftest` 扩到 **25** 条用例(14 条必须认、11 条近失必须继续拒绝)。

**15 处的修法**:空指针时一律走**已存在**的退化路径,不发明新行为 ——
`EquipManager::Get` 的四处在管理器为空时整段跳过引擎装备调用
(其中 `Actor::SetMagicEquipment` 把 `const bool hasEquipManager = pEquipManager != nullptr;`
提到赋值同层,因为门禁只把不深于赋值行的测试算作支配);
`MagicItem::IsInvisibilitySpell` 的空管理器**不消耗** `static bool runonce`,
否则 `bosFormID` 会永久停在 0 且不再重试;四个 Debug 视图把判空放在 `ImGui::Begin` **之前**
(无匹配 `End` 会造成 ImGui 栈失配,该窗口在主菜单也能画出来);
`CalendarService::ToggleGameClock` 的 `s_gameClockLocked = !aEnable;` 留在守卫之外,两条路径都执行;
`HandleUpdate` 的 `m_lastTick` tick 记账照旧;`PlayerService` 的 `m_knockdownDeadline`/`m_knockdownStart` 照设;
`TransportService` 让 `request.PlayerTime` 保持 `TimeModel{}` 默认值
(TimeScale 20 / Time 12 / Year 1 / Month 1 / Day 1),即该函数自己写下的
「everything below degrades to a default」;`ModSystem` 用三元让每个条目落进既有的 `else`
(`++unmapped;` + `spdlog::error`)。

**验证**:`check_null_accessors.py` ⇒ `NULL ACCESSOR CHECK OK`(19 个可空访问器 / 446 个源文件 /
1 个豁免仍有效),exit 0;`--selftest` ⇒ `NULL ACCESSOR SELFTEST OK (25 shape(s))`,exit 0。
改动前基线对新门禁 15 条、当前树 0 条;13 个文件花括号净差为 0。
**本机不编译**(无 xmake/MSVC),以上判据是静态的。

### 新增:一个把「官方地址库缺的 id」按住的门禁

**怎么找到的**:从 §43.8 那句「记为潜伏项,不修」开始。它把 4 个 RTTI 定位器在 1.6.1130+
上整条不存在的实测证据写下来了,但只留了一句「一旦有人写 `Cast<NiObjectNET>`,它会静默返回
`nullptr` 而不是崩溃 —— 这正是需要在评审时盯住的那类改动」。**「评审时盯住」不是一种保证**,
而这件事的两份语料(客户端的 `registerRtti_*` 注册表、随仓库发布的 24 张地址库表)都已经在
仓库里,门禁读得到,不需要靠谁记得。

**这个门禁比什么**:

| 语料 | 来源 | 规模 |
| --- | --- | --- |
| RTTI 注册表 | `Code/client/Games/Skyrim/RTTI.cpp` | 2806 个 id / 2806 个类型名 |
| 地址库表 | `GameFiles/Skyrim/SKSE/Plugins/versionlib-*.bin`(14)+ `versionlib-ae-to-se-*.map`(10) | 24 张 |
| `Cast<>` 站点 | `Code/**/*.{cpp,h,hpp}`,清空注释后 | 196 个站点 / 37 个不同实参 |

四条判据:① 每个 `Cast<>` 实参在每张表上都解析;② 注册表与表的差集和 `GAPS` 声明**双向**
一致(未声明=失败,声明了却已不是缺口=失败);③ `GAPS` 里的 id 不得被任何 `Cast<>` 触达 ——
这条把「评审时盯住」变成会红的判据;④ 四类语料任一为空即失败(反空跑)。

**登记了 5 个缺口**(唯一手工维护的数据,每条带 reason):

| id | 类型 | 缺在哪 |
| --- | --- | --- |
| 392519 | `NiObjectNET` | 6 张 bin(1.6.1130 / 1.6.1170 / 1.6.1170-0-1 / 1.6.1179 / 1.7.99 / 1.7.104) |
| 394235 | `CreationClubMenu` | 同上 6 张 |
| 398437 | `NiParticleSystem` | 同上 6 张 |
| 400159 | `CCallbackBase` | 3 张(1.6.1170-0-1 / 1.6.1179 / 1.6.659) |
| 400180 | `BSWin32GameDataSystemUtility` | 10 张 map(全部 ae-to-se) |

**为什么不是「修掉」**:`versionlib-*.bin` 是 SKSE 官方发布产物,改一个字节哈希就对不上,
`CheckAddressLibrary` 可能直接拒收;而且缺口是一个 **id 空洞**(`392519` 整行不存在,不是
偏移 0)。所以门禁的语义是**记录并按住**:不要求缺口消失,要求缺口不被使用。
`400180` 在另一条链路上早就声明过(`Tools/missing_ids_by_runtime.tsv` /
`check_map_coverage.py` 管的是 map 那一侧),门禁里的 reason 写明了对方是谁、两者不矛盾。

**顺带订正的两个口径**:① §43.8 的「4 个」按 RTTI 注册表口径是 **5 个**,多出来的
`400180` 缺的是 10 张 ae-to-se map(14 张 bin 里全都在);② 任务最初说「38 个实参 / 200 个
站点」是**不清注释**的裸扫,按仓库惯例清空注释后是 **37 / 196** —— 差掉的 4 个站点都在注释里,
其中 `ExtraTextDisplayData` 这个类型在活代码里一次都不出现。不清注释的话,一段注释掉的
`Cast<NiObjectNET>` 也能让判据 ③ 报红,而那条线根本不会运行。

**验证**:`RTTI COVERAGE CHECK OK` /
`2806 registration(s) x 24 shipped library(ies) = 67344 id/runtime pair(s); 37 distinct
Cast<> argument(s) over 196 site(s) resolve everywhere; 5 declared gap(s) are real and
reachable from no cast`。变异 (a) 加一个 `Cast<NiObjectNET>` 站点 ⇒ 报出该 `文件:行` 并说明
「不是崩溃,是静默 no-op」;变异 (b) 删一条 `GAPS` ⇒ 报未声明缺口;反向变异加一条已在所有表
里的 id ⇒ 报 stale;变异 (c) 把扫描语料指向不存在的目录 ⇒ 报空覆盖而不是 OK。四条 sibling
门禁(`check_map_coverage` / `check_target_meta` / `check_library_scan` / `check_gate_integrity`)
改后全绿。

### 可诊断性:三个只验过 1.5.97 的结构体偏移 —— 补上断言,并把能判定它的那行日志提到 info

**背景**:客户端为了只对「从网络重建的 actor」拨掉 `bUseFaceGenPreprocessedHeads`,
自己手走引擎的 INI 设置链表,依赖三个偏移:`Setting::data +0x8`、`Setting::name +0x10`、
`INISettingCollection` 链表头 `+0x118`。这三个数字是在 `SkyrimSE.exe` **1.5.97** 上量的,
**1.6.x/1.7.x 全部未验证**,而仓库里 1.6.1170 的逆向产物是按「代码里真正调用的目标」采集的,
`INISettingCollection` / `GameSettingCollection` / `GetSetting` 一条都不在其中。

**为什么这值得单独记一笔**:偏移错了**不会崩溃**。`INISettingCollection::Get()` 经
`POINTER_SKYRIMSE(411155)` 解析,该 id 在 1.6.317/640/1170/1.7.99/1.7.104 上全部有映射,
`GetSetting` 的返回值也在调用点判了空。于是错误只表现为「开关没拨,隐形头照旧」或
「写坏了别的 INI 设置」—— **「AE 上没修好」和「AE 上偏移不对」两种解释从用户日志里无法区分。**

**改动三处:**

1. `Code/client/Games/TES.h` —— 两个结构体后补三条 `static_assert`,并写明**它们不关闭缺口**
   (只在编译期检查本仓库自己的布局,不会去问引擎),以及这三个数字只按 1.5.97 量过、
   错偏移是静默失效而非崩溃。
2. `Code/client/Services/Generic/CharacterService.cpp` —— `SetRemoteFaceGenForce` 的**首次写入**
   提升为 `spdlog::info`(带解析到的 `Setting*` 与读到的值,只打一次),之后仍走 `debug`;
   查找失败路径从静默 `return` 改为**先 `warn` 再 `return`**(空指针是「设置不存在」与
   「偏移不对」共用的答案,不区分就永远看不出是哪种)。
   `TiltedOnlineApp.cpp:71` 的 `set_level(spdlog::level::debug)` 是注释掉的,所以原来那行
   **在发布版永不打印** —— 这是提升级别的全部理由。
3. `Tools/Scripts/check_facegen_scope.py` —— 新增第 7 条判据,锁住上面两件事:字段声明与
   三条断言仍在、`info` 行仍在且格式串点名该设置、且**被 `if` 包住**(首次写入唯一性)、
   `debug` 行未被删、失败路径**既 `warn` 又仍然 `return`**。

**验证**:`check_facegen_scope.py` exit 0,摘要行新增 `3 asserted offset(s), 1 info first-write log`;
`check_layout_asserts.py` exit 0(`192 → 195` 条断言,唯一性无冲突);
`check_log_format_args.py` exit 0(新日志占位符与参数数一致)。
变异实测:改回 `debug` → `FACEGEN SCOPE CHECK FAILED (1)` 并指名首次写入日志;删 `warn` → `FAILED (1)`;
删一条 `static_assert` → `FAILED (1)`;三者还原后均 exit 0。

**没有关闭缺口**:三个偏移在 AE 上依然未验证。现在有了判据 —— 一名 1.6.x/1.7.x 玩家的一行
日志(`Setting*` 非空且值为 0/1 = 偏移正确;出现 `warn` = 偏移不对)即可定论,
见 `docs/FOLLOW-UP.md` 第 1 节。

### 修复:框架传输路径丢掉了 `STRPM::Message` 的 `flags` 与 `sequence`,`kMessageAllowLoopback` 从未实现

**同一个结构体,两条后端给出不同的值**:契约 `STRPluginMessagingAPI.h:102-110` 的 `Message` 有六项,
独立 bridge 全部兑现(`STRPluginMessagingAPIRuntime.cpp:703` 每实例自增 sequence、`:713-714` 把
`seq=`/`flags=` 编进报文、`:725` 按 `kMessageAllowLoopback` 本地回环投递),
而框架路径三条一条都没兑现。

**这不是三处疏漏,是一处根因**:`Code/encoding/Messages/PluginMessagingRequest.h` 与
`NotifyPluginMessaging.h` 两个线上消息**根本没有这两个字段**(只有
`Channel`/`TargetKind`/`TargetPlayerId`/`PluginData` 与
`Channel`/`SenderDisplayName`/`SenderPlayerId`/`SenderIsHost`/`PluginData`)。
接收方除了写死常量别无选择,所以 `PluginMessagingService.cpp:469-470` 只能写
`message.flags = STRPM::kMessageNone;` 与 `message.sequence = 0;`,
而 `Send` 的第五个形参 `aFlags` 在函数体内**出现 0 次**。

**玩家现象是诊断信息缺失,不是崩溃**:`MorphSyncTogether/src/UdpTransport.cpp:265-270` 与
`OStimTogether/src/STRPMTransport.cpp:444-451` 都把收到的 `sequence` 打进日志
(`sequence={}` / `seq={}`),实测**只是格式化实参,不进任何分支**。恒 0 的日志会让人以为协议没走
sequence,排查时把注意力引到错误的方向。

**修法**:两个线上消息各加 `Flags`/`Sequence`,用仓库既有的 `WriteVarInt`/`ReadVarInt`
(追加在 payload 之后,`SerializeRaw` 与 `DeserializeRaw` 同序);客户端 `Send` 用
`std::atomic<std::uint64_t> m_nextSequence{ 1 }` 打戳并把 `aFlags` 原样写入;服务端只逐字段转发、
仍然不解释;接收侧改用 `acMessage.Flags`/`acMessage.Sequence`;
`kMessageAllowLoopback` 在 `Send` 内本地投递一次,**沿用该文件既有的「锁内收集、锁外调用」纪律**
(插件可以在自己的 receive handler 里 send,持锁调用会死锁)。

**为什么可以加字段**:`Code/server/GameServer.cpp:886` 要求客户端与服务端的 `BUILD_COMMIT`
完全相等,不等即 `RT::kWrongVersion` 踢出;`CHANGELOG.md` 也写明每个 tag 附版本号相同的两个包。
跨版本混用不是受支持配置。

**未验证**:`Libraries/TiltedConnect` 在本机是未初始化的空 submodule,底层是否可靠有序
**无法从本仓库确证**,因此本次只透传 `kMessageReliable|kMessageOrdered`,不改协议语义。

**门禁**:新增 `Tools/Scripts/check_message_flags_parity.py`(五条静态断言,纯标准库):
`Send` 必须真的使用 flags 形参;sequence 与接收侧的两个赋值右侧都不得是常量
(正是本次缺陷的形状);两个线上消息都必须声明两字段且写读同序;服务端必须转发。
变异体实测:`Flags = aFlags;` 改成 `Flags = 0;` 与 `message.flags` 改回 `STRPM::kMessageNone`,
门禁均退出 1。

### 修复:14 个门禁在「什么都没比」时报 OK —— 空语料被当成了通过

**现象**:这一轮的活缺陷,是上一轮自己写的门禁。`Tools/Scripts/check_gate_integrity.py` 把 44 个门禁的
**注册与依赖**管住了,却管不住它们**比了多少**:把一个门禁的语料削弱(改名锚点、清空被读的文件、
缩进一个终止符),它照旧打印 `... CHECK OK` 并退出 0。逐个变异实测,14 个门禁有这一面:

```text
check_skee_abi.py       重命名 class IOverlayInterface  → 0 declaration(s) checked + SKEE ABI CHECK OK
check_exports.py        两处 re.findall 可为空          → 报告循环执行 0 次
check_contract_copies   清空一份 vendored 头            → 22 个布局(基线 25)仍然 OK
check_transport_compat  重命名 kSetTimeCommandRequest   → 两条顺序规则一起跳过
check_skse_bootstrap    把 }; 缩进                      → initializer 为 None,三条标志位规则跳过
check_ostimng_build     子模块没 checkout               → rglob('vcpkg.json') 静默返回空
check_allocation_guards 反向循环是空操作                → 摘要打印的是表的长度,不是实测计数
check_patch_offsets     GamePatch::At 锚点改名          → checked == 0
check_ostim_support_message 选择器/消息两侧都可为空     → 两侧都空仍 OK
check_crash_diagnostics RemovePreviousDump 改名         → 整条规则静默跳过
check_capstone_report   --list 从不调用 check()         → 三句判决直接 print
check_ae_to_se_overrides 出货 map 为空                  → 只 warn
check_library_scan      checked == 0                    → 只 warn
check_package_metadata  看不到自己的 tag                → 只 warn
```

**根因**:三种形状,都不是「规则写错了」,而是**规则没跑**——
① **计数无下限**:`checked`/`compared`/`named` 一路自增,空语料时循环体一次都不执行,尾部照样打 OK;
② **锚点缺失被当成合法缺席**:`if body is None: continue  # this plugin does not use that interface`
与 `if not match: return None` —— 「这个插件不用那个接口」和「这份拷贝被改名」被写成了同一行;
③ **warn-only**:`warnings` 只被打印(`check_contract_copies.py:360-362`),永不进入退出码(`:364`),
于是从语料里删掉一个键就退役了整条规则。

**修法**:按每个门禁已有的语料逐条补下限,并把「合法缺席」与「语料被削」分开——
`check_ostimng_build` 保留「子模块未 checkout 就 continue」(`.github/workflows/plugins.yml:346`
给跑该门禁的 job 设了 `submodules: true`),但插件存在而不贡献站点即失败(与
`check_ostimng_versions.py:158` 的既有先例一致);`check_skse_bootstrap` 的正则从 `\n\};` 放宽到
`\n\s*\};`,同时把「读不到初始化器」从跳过改为失败;`check_transport_compat` 刻意用 `elif` ——
`kSetTimeCommandRequest` 声明着但锚点标识符没了才报错,opcode 合法缺席仍是 skip;
`check_allocation_guards` 的 `deref_pattern()` 补上 `(*p).field` 与裸 `*p` 两种拼写,并把
`examined`/`compared` 分开统计,摘要不再打印表的长度;`check_capstone_report` 的 `--list` 改成打印
「这个门禁比什么」加它算出的计数,删掉三句硬编码结论 —— 缺陷在打印的文本里,没有任何退出码能显示它。

**验证**:`Tools/Scripts/.verify_vacuity.py` 四阶段 —— clean 必须 0 / 变异必须非 0 /
**pre-fix 必须也是 0**(证明缺陷真实存在,而不是变异打中了另一条规则) / 还原必须回 0;
语料走 `.mut-mirror-full/` 的字节镜像,门禁的 `parents[N]` 在镜像内解析。11 条 CAUGHT,
外加一条绿色控制组(只把终止符缩进必须仍绿,证明放宽正则没把合法重排变成失败)。
**两个残留洞被固化下来留给下一轮**:`check_exports` 只改 4 个常量里的 1 个仍 MISSED,
`check_skee_abi` 只改两份拷贝中的一份仍 MISSED —— 它们就是下面那一条。

**门禁**:`check_gate_integrity.py` 的 7 条规则(R1 不得「跑不了却报成功」/R2 不得「什么都没比却通过」/
R3-R6 workflow 注册与 path trigger 双向/R7 依赖安装)是这一批能被看见的原因。14 个门禁此前都已在
`.github/workflows/plugins.yml` 注册,未改任何 workflow。

### 修复:五个「覆盖洞」—— 判据依赖的那份数据被削弱后,门禁仍然报 OK

**现象**:上一批补了「计数下限」之后,还剩一类更细的洞:**下限本身可以被绕过**。判据不是
「有没有比较」,而是「比较覆盖了全部应该比较的东西」。五个,都是变异实测:

```text
check_exports.py        头文件里 4 个 kQuery*ExportName[] 只改名 1 个 → named 仍有 3 条,循环照跑
check_skee_abi.py       只改两份拷贝中的一份的 class IOverlayInterface → declaring 仍非空,那份静默离开比较
check_contract_copies   从 contract.json 删掉 headerSha256          → else 分支只 warn
check_library_scan      Code/skse_client/xmake.lua 消失             → 只 warn,规则 3 静默退役
check_target_meta.py    GameFiles/Skyrim/SKSE/Plugins 下没有 versionlib-*.bin → 只 warn
```

**根因**:前两个是**多来源**的洞 —— 判据的语料有两份以上,一份被削弱时另一份把下限撑住;
后三个是「语料缺席 → warn → exit 0」,而这几个文件(`Code/skse_client/xmake.lua`、
`GameFiles/Skyrim/SKSE/Plugins/versionlib-*.bin` 十四张、`Code/plugins/STRPM/contract.json`、
`Code/skse_client/main.cpp`)**全部受版本控制**,所以缺席意味着检出损坏或被删,不是合法的跳过。
同一个门禁里早就有相反的先例:`check_library_scan.py:89-99` 已经因为
`Code/skse_client/main.cpp` 缺失而 `failures.append`。

**修法**:`check_exports.py` 改为读 `Code/plugins/STRPM/contract.json` 的 `exportNames`(4 个字符串值),
与头文件扫出的常量值做**双向**集合比较 —— 少一条报「常量被改名或删除,比较覆盖的入口点比契约
公布的少」,多一条报「manifest 不再描述头文件公布的入口点」;manifest 缺失 / JSON 非法 /
没有 `exportNames` 列表都记失败。`check_skee_abi.py` 新增 `EXPECTED` 表(`:93-102`,
风格同既有的 `MINIMUM_VERSION`):`MorphSyncTogether` = 三个接口、`OStimTogether` = 两个
(它确实不声明 `IBodyMorphInterface`);某插件不再声明 `EXPECTED` 记的接口即失败,并配**反向 stale 检查**
(`EXPECTED` 点名了不在 `SOURCES`/`REFERENCE` 里的东西也是失败)。其余三条
`warnings.append` → `failures.append`,措辞点明「这些语料受版本控制,所以缺席意味着声明没有根据,
而不是这个运行时不需要检查」。

**验证**:三条走 `.verify_vacuity.py`(pre-fix 全 0、fixed 全 1、还原全 0);
`check_target_meta.py` 另跑五组 —— `versionlib-1-7-99-0.bin` 截成 16 个零字节时 pre-fix 仍打印 OK,
fixed 报「存在但读不出,所以没有 id 被比过」,而**整个删掉该文件仍然 exit 0**
(缺席不失败、存在但不可读才失败,正是要的语义);`check_library_scan` 与 `check_contract_copies`
的变异分别让 `xmake.lua` 从镜像消失、从 `contract.json` 删 `headerSha256`。
全量复扫 `ran 44 gate(s); 44 passed, 0 failed`。

**未修(如实记录)**:`check_contract_copies.py:293` 的反向常量漂移仍是 warn-only ——
某份副本多带一个 `inline constexpr` 常量能编译、不破坏契约,转成失败会让插件侧合法新增变红;
`check_exports.py` 仍不报告「被导出但没有任何常量点名」的符号。

### 修复:工作树 `CHANGELOG.md` 被截断损坏 —— 44 个门禁里只有 1 个会读它,而它是硬崩

**现象**:为落盘找插入点时,`grep` 报 `Line 5751: (line is not valid UTF-8)`,而 `pwsh` 读出的中文
全是乱码(`# 鏇存柊鏃ュ織`)。先按「终端按 GBK 解码的假象」放过了一次,`grep` 的告警推翻了它。

**测量**:

```text
工作树   621282 B / 6575 行 / 3620 行不是合法 UTF-8 / 缺整个 ## v1.1.6(2026-09-27) 节
镜像     633619 B / 9010 行 / 0 行非法 / sha256 9ebb7a59cc47898486a6f81bc3310adc12ab86c667c0ff635a2b26e7ad476284
HEAD     401528 B / 5531 行 / 0 行非法
```

损坏形态是**多字节序列被截断**:第 3 行 `以`(U+4EE5 = `e4 bb a5`)的第三字节变成 `?`(`e4 bb 3f`);
3620 个坏行**全部**含一个字面 `?`,文件里 `?` 共 12369 个;1057 段连续坏行,最长 33 行。
不是任何单一编码往返(`gbk`/`cp936`/`latin-1`/`utf-16-le` 逐一试过都不等),也不是「只坏了新加的行」——
`# 更新日志` 之后的第一段正文就坏了。全树扫描 2332 个受控文本文件,**只有这一个文件受损**。

**恢复**:会话早期建的镜像 `.mut-mirror-full/CHANGELOG.md` 正是损坏前的版本 ——
会话里记录过的 10 个行号锚点(`:1 # 更新日志`、`:10 ## v1.1.9`、`:8015 ## v1.1.8`、`:8175 v1.1.7`、
`:8271 v1.1.6`、`:8499 v1.1.5`、`:8743 v1.1.4`、`:8844 v1.1.3`、`:8872 v1.1.2`、`:8963 v1.1.1`)
**全部命中**;与 HEAD 的行级 diff 是 `+3479 -0`(零删除,纯超集);工作树里「不含 `?` 也不含 U+FFFD」
的可抢救行共 2047 行,**不在镜像里的 0 行**。于是逐字节拷回,恢复后 sha256 与源相同、UTF-8 合法、
`check_docs.py` 重新 `DOC CHECK OK`。

**根因(为什么没有任何门禁抓住它)**:44 个门禁里只有 `check_docs.py` 会读 `CHANGELOG.md`,
而它是**硬崩**而不是报一条失败 —— `UnicodeDecodeError: 'utf-8' codec can't decode bytes in
position 40-41`,栈顶在 `check_version_tables()` 里,既不点名文件也不说后果。其余约 40 处读取器
用 `errors='ignore'` / `errors='replace'`,损坏被静默吞掉(`check_layout_asserts.py:78`、
`check_map_coverage.py:131`、`Code/plugins/tools/check_skee_abi.py:157`、
`Tools/Scripts/gen_ae_to_se_map.py:251`……)—— 连 `check_docs.py` 自己打印的覆盖率数字
都是经由宽松读取算出来的。

**修法**:`check_docs.py` 新增第一条规则 `text-integrity`(`:139-170`):`git ls-files` 列出受控文件,
按 34 个文本后缀过滤,逐份 `decode('utf-8')`,失败时报
`{rel}: byte {offset} is not valid UTF-8 ({reason}); every gate that reads it with errors='ignore'
or errors='replace' is comparing a degraded copy and can report OK while doing so`。
它注册在 `CHECKS` 字典第一位,并在 `main()` 的循环里于它失败时 `break` —— 否则损坏的 `CHANGELOG.md`
会先在 `check_version_tables()` 里死掉,这条可读消息永远到不了 stdout。
`git ls-files` 失败(不在仓库里)也记失败而不是静默跳过。

**验证**:干净树 `DOC CHECK OK` 加原有 4 行,**逐字节与改前相同**(零新增噪音);
把 `docs/PITFALLS.md` 字节 465 改坏 ⇒ `DOC CHECK FAILED (1)` 加可读消息;
**pre-fix 门禁配同一个 PITFALLS 变异 ⇒ exit 0(盲)**;pre-fix 门禁配 `CHANGELOG.md` 字节 40 变异
⇒ exit 1 但只有未捕获的 `UnicodeDecodeError` traceback;还原后回 exit 0。
全量复扫 `ran 44 gate(s); 44 passed, 0 failed`。

**范围(如实记录)**:规则按后缀过滤,**43 个无后缀受控文本文件**(`.gitignore`、`.gitattributes`、
`LICENSE`、`VERSION`、`Dockerfile`……)够不到;107 个合法非 UTF-8 的受控文件全是真二进制
(`.bin` 24、`.ico` 19、`.pex` 12……),没有一个在后缀表里 —— 这正是它在健康树上沉默的原因。

### 修复:游戏版本的那条「权威信号」被 `skse64_steam_loader.dll` 提前掐断

**怎么找到的**:按「同一件事写了两遍,把两遍抽出来对照」这条判据(§115/§116/§120),问
**「同一个模块列表,谁还在扫」** —— 答案是两个翻译单元,而且它们的写法不一样:

```text
Code/skse_client/main.cpp     GetSKSEModuleVersion()   决定用哪个地址库
Code/skse_bootstrap/main.cpp  IsLegacyGame()           决定加载哪个版本的运行时 DLL
```

两个都扫 `skse64_` 前缀、都用 `swscanf_s(name + 7, L"%d_%d_%d", …)` 取版本,
但**只有前者把 `break` 写在了解析之外**:

```cpp
if (name.rfind(L"skse64_", 0) != 0)
    continue;
if (swscanf_s(name.c_str() + 7, L"%d_%d_%d", &a, &b, &c) >= 3)   // 解析成功才赋值
{
    ...
    result = buf;
}
break;                                                            // <-- 无条件,在解析之外
```

**为什么前缀不足以认出运行时**:SKSE 的安装目录里,**`skse64_` 前缀的模块有两个** ——
运行时 `skse64_1_5_97.dll`,以及 `skse64_steam_loader.dll`(它先于运行时被映射进游戏进程,
负责给游戏的 IAT 打补丁并挂 `WinMain`,**它名字里没有版本号**)。模块枚举顺序不是我们能选的,
所以「运行时恰好排在前面」不是可以依赖的东西。

**后果**:loader 排在前面时,这一轮扫描**一个版本都取不到**(`result` 仍是空串),
于是 `RunTiltedInit` 静默退回到它本来要取代的那两级 fallback —— exe 版本资源、地址库文件名。
而这两级恰好是**降级版/整合包**上最不可靠的:那正是这段代码存在的理由。

**本机实测**(`D:\game\SkyrimSE\`,1.5.97,SKSE 2.0.x):

```text
skse64_steam_loader.dll      解析 = None
skse64_1_5_97.dll            解析 = ('1','5','97')
当前循环 → resolved=''                    (运行时根本没被读到)
改为解析成功才停 → resolved='1.5.97.0'
```

同目录的 `skse64_steam_loader.log` 自己记着它就在游戏进程里:
`HookMain: thread = 35920 retaddr = …`、`patched iat`。

**修法**:把 `break` 移进解析成功的分支 —— 与 `Code/skse_bootstrap/main.cpp` 的
`IsLegacyGame()` **写法一致**(它用 `return` 停在解析成功之后,所以从来没这个缺陷)。
两个调用方现在对同一个模块列表给出同一个答案。

**判据**:新门禁 `Tools/Scripts/check_skse_module_scan.py`,四条静态断言 ——
①两份实现都还匹配 `skse64_` 前缀;②都还把版本从模块名里解析出来;
③**停在解析成功处,而不是停在前缀上**(前缀之后、解析之前的裸 `break` 与解析之后的裸 `break`
都算失败);④循环真的在遍历。另外它要求 `skse64_steam_loader.dll` 仍被点名,
否则这条规则的理由已经不在了。

**变异实测(5/5 全部被抓)**:恢复无条件 `break` ✓;把 `break` 提到解析之前 ✓;
删掉版本解析 ✓;删掉前缀匹配 ✓;重命名函数让扫描找不到 ✓。

### 修复:四个插件的加载路径是 `noexcept` 且会分配 —— 而只有一份被修过

**怎么找到的**:按 §「同一类缺陷的兄弟函数/兄弟副本」判据。仓库对「`noexcept` 帧里分配」
已经有**四轮**普查(`Code/client`、`Code/server`、`Code/encoding`、
`plugins/STRPluginMessagingAPI`),产物是 `Code/plugins/patches/STRPluginMessagingAPI/0004..0007`。
这一轮把**同一把尺子**量到插件树的其余部分 —— 之前没有一轮覆盖过。

**现象(两个形状,同一个后果)**:

**(a) 插件加载路径**。每个伴生插件都带一份同名小助手,用来找消息 API 模块:

```text
plugins/STRPluginMessagingAPI/src/STRPluginMessagingAPI.cpp   HMODULE LoadPluginModule(...) noexcept
plugins/AnimSyncTogether/src/STRPMApi.cpp                     HMODULE LoadPluginModule(...) noexcept
plugins/DAVSyncTogether/src/STRPMApi.cpp                      HMODULE LoadPluginModule(...) noexcept
plugins/TradeTogether/src/STRPluginMessagingAPI.cpp           HMODULE LoadPluginModule(...) noexcept
```

四份都在 `noexcept` 里构造 `std::wstring pluginPath` 并 `+= moduleName`。
**分配失败逃出 `noexcept` 帧会调 `std::terminate`** —— 游戏直接结束,没有崩溃转储,
任何日志里也没有痕迹。修 `STRPluginMessagingAPI` 的那一轮(0005 的标题里就写着
"and the load path")只覆盖了它自己那一份;另外三份在**各自独立的仓库**里,所以没人对照过。

**(b) OStimNG 的 Thread API**。`OstimNG-API-Thread.cpp` 里 8 个入口在头文件中声明为 `noexcept`,
而 **OStimTogether 逐字复制了那份 vtable**(`ModThreadControl.h`),所以承诺**不能单方面撤回**;
其中 8 个函数体在增长容器(`GetNavigationCount`/`GetNavigationOptions`/`NavigateToScene`/
`RegisterEventCallback`/`RegisterControlCallback`/`SearchScenes`/`HasCompatibleNode`/`MigrateThread`),
一个 `catch` 都没有。同一个仓库的**兄弟文件** `OstimNG-API-Main.cpp` 已经因为同一个原因被修过
(`Code/plugins/patches/OStimNG/0003`),而这一份没有跟着写。

**修法**:
- 四份 `LoadPluginModule` 把两条字符串操作包进 `try/catch(...)`,失败时**落到「没加载」** ——
  这正是模块不在 `Data\SKSE\Plugins` 时已有的行为,每个调用方都把空模块读作「消息 API 不可用」。
- OStimNG 那 8 个入口各自捕获分配失败并返回空答案/失败码;`MigrateThread` 通过 `onComplete(-1, context)`
  上报,避免调用方等一个永远不会到的回调。

**为什么必须写成 patch**:这些文件在**钉住的 submodule** 里,CI 每次都跑
`git submodule update --init --force`,工作树里的改动会被丢掉。所以四处修复都落成
`Code/plugins/patches/` 下的普通内容:

```text
STRPluginMessagingAPI/0009-keep-an-allocation-failure-out-of-the-plugin-load-path.patch
AnimSyncTogether/0003-…            DAVSyncTogether/0004-…            TradeTogether/0003-…
OStimNG/0007-…  (被扩充:在原有内容之后追加 8 处 catch)
OStimNG/0008-…  (0007 的另一半,见下)
```

`OStimNG/0007` 是被 `docs/PITFALLS.md` §109 点名引用的,所以没有新建编号去取代它,
而是把新内容追加进同一份 —— 否则那条锚点会指向一个不再包含它所描述内容的文件。

**为什么 OStimNG 那部分被拆成 0007 + 0008**(工具链的坑,如实记下):补丁的**上下文行是逐字节
匹配**的,而 submodule 里钉住的 blob **逐文件行尾不同** —— `OstimNG-API-Thread.cpp` 是 **CRLF**,
`Main.cpp` / `PapyrusThread.h` / `MainThread.h` 是 **LF**。一份文件**不可能同时**满足两边,
所以按目标文件拆开:0007 只含线程文件(整份 CRLF),0008 含其余三个(整份 LF)。
两半各自 `git apply --check --cached` 通过,合起来仍是原先那一组改动。

第一处误导来自 `git show HEAD:<patch>`:它会**套用 smudge 过滤器**,把 CRLF 变成 LF,
于是「与原始补丁逐字节相同」的比对结论是**假的**;`git cat-file blob` 才给真字节。
**教训**:在 `core.autocrlf=true` 的树上比对文本补丁内容,要用 `git cat-file blob`。


**判据**:新门禁 `Code/plugins/tools/check_noexcept_allocations.py`。它**按性质检查,不按名单**,
四条规则:①`plugins/` 下**每一个**「`noexcept` 且函数体真的会分配」的函数都必须含 `catch`;
②**每一份**会分配的 `LoadPluginModule` 都必须含 `catch`(按名字更严);③提到该助手却在其中
找不到定义的插件**报失败**;④本文件**为之而写**的 12 个站点必须**真的被看见**(语料下限)。

**第一版门禁的语料是按「发现地点」写的,而这是同一错误的第五次**:它只有两个语料 ——
「每一份 `LoadPluginModule`」与「`OstimNG-API-Thread.cpp` 的每一个 `noexcept override`」,
两者都是「我在哪里找到缺陷」。实测数了一下:`noexcept` 且会分配的函数 **14 处在语料内、
18 处在语料外**,对第一版完全不可见。这正是 §113/§116/§119 记过的同一错误。
第二版把规则 1 的语料换成**整棵插件树**,并加规则 4 —— 于是查到 **29 处**并全部通过。

**规则 4 立刻抓到两个站点**(`GetNavigationCount`、`NavigateToScene`):它们在语料里,
但第一版的分配标记匹配不到,因为标记漏了带模板实参的声明(`std::vector<T> name =`)
与构造式(`std::string(x)`)。**没有规则 4,第二版会一边少看两个站点一边报 OK。**
补上两种写法后 12/12 全部可见。

**一个假阳性,以及为什么不把它排除掉**:普查把
`STRPMChatUiSuppressV3.h:FindNearestCallAfterXref` 报成缺陷,逐条读下来是假的 ——
`std::vector<std::uint8_t> snapshot;` 是**默认构造**(不分配),唯一会分配的一步委托给
`Snapshot()`,而它**自己含 `resize` 的 catch**,调用方又整个包在 `try/catch` 里。
第二版把标记收紧到「真的会分配」之后它不再被匹配,于是那张豁免表**留空** ——
留空而不删表,是因为「用更窄的正则把它排除」正是让真实站点漏看的做法:
**豁免是看得见的,窄正则是看不见的。**

**变异实测(第一版 5/5;第二版按新规则补测)**:去掉 AnimSyncTogether 的守卫 ✓;
去掉 facade 的守卫 ✓;重命名助手 ✓;去掉 `GetNavigationCount` 的 catch ✓;
**去掉第三语料外一个站点的 catch**(`STRPMChatUiSuppress.h:Snapshot`)✓;
**把 `std::vector<...> conditions = ...` 改成 `auto`,使已知站点不可见** ✓。
后两条验的是「门禁真的比第一版看得更多」,而不只是「它还认识原来那 12 个名字」。

**验证**:`python Code/plugins/tools/plugin_patches.py verify` → `PATCH VERIFY OK`,
**44 份 patch**(改前 39 份)全部对钉住的提交 `git apply --check --cached` 通过;
全量门禁 `ran 46 gate(s); 46 passed, 0 failed`(改前 44 个)。


## v1.1.8(2026-09-28)

### 修复:任意玩家一条聊天消息即可打崩专用服务器

**现象**:专用服务器上,一个**还没建好角色**的玩家(刚连上、角色创建失败,或
角色被删除后重连)发一条聊天消息,服务端进程直接终止。

**根因**:`Player::GetCharacter()` 返回的是
`std::optional<entt::entity>`,它在 `CharacterService` 创建角色**之前一直是空**。
`OverlayService::HandleChatMessage`(`Code/server/Services/OverlayService.cpp`)
把它**直接解引用**后交给脚本回调:

```cpp
auto [canceled, reason] = m_world.GetScriptService().HandleChatMessage(
    *acMessage.pPlayer->GetCharacter(), acMessage.Packet.ChatMessage);
```

解引用空的 `optional` 是未定义行为:MSVC release 下取到的是一个**构造出来的
垃圾实体**,脚本层再拿它去 `ctx().at<T>()` 就崩。这不是"玩家操作不当":
`SendChatMessageRequest` 任何已连接客户端都能发,与是否建好角色无关。

**判据是仓库自己的既有约定**——同一个调用在三处都有判空,只有这一处漏了:

| 位置 | 是否判空 |
| --- | --- |
| `WeatherService::OnWeatherChange` | ✅ `if (!...GetCharacter()) return;` |
| `PlayerService::OnPlayerRespawnRequest` | ✅ `if (!character) return;` |
| `OverlayService::HandleChatMessage` | ❌ **本次缺陷** |

这与 v1.1.4 修掉的 `CommandService::OnSetTimeCommand` 是**同一类**:
一条远程报文触发服务端空指针,判据也是"旁边三处都判了、这一处没判"。

**修复**:把脚本回调放进判空分支。**只跳过脚本回调,不吞掉消息** ——
`onChatMessage` 拿到的是实体,传一个默认构造的实体等于把垃圾指针交给脚本;
而下面的广播(`sendPlayerMessage`)自己已经判空,所以该玩家的全局/队伍聊天
照常送达。**修崩溃,不改行为。**

**验证**:改后全仓库再查一遍 `*...GetCharacter()` 的解引用点,只剩
`WeatherService` 与 `PlayerService` 的两处,且三处**都在自己的判空分支里**;
客户端侧同口径(`.value()` 与 `*optional`)逐个核对,全部有前置 `has_value()`
/判空。

### 修复:1.6.x 上 esp 与主菜单 logo 都在,但 F2 菜单打不开

**现象**:游戏 1.6.1170 + SKSE 2.2.6 下,SkyrimTogetherNext 看起来装好了
(Creations 菜单里能看到 esp,主菜单有 logo),**但游戏内按 F2 没有任何反应**。

**根因**:`SkyrimTogetherSKSE.dll` 其实**一次都没被 SKSE 加载过**。
`skse64.log` 里那一行就是判据:

```text
plugin SkyrimTogetherSKSE.dll (00000000  00000000) no version data 0 (handle 0)
```

`handle 0` 表示 SKSE 根本没把它加进加载列表。SKSE 2.2.x 的
`PluginManager::ScanPlugins` 按名字查找 **`SKSEPlugin_Version`** 这个导出;
**找不到就直接记 `no version data` 并跳过,没有回退到旧的 `SKSEPlugin_Query`**。
而本 DLL 只导出了 `Query`/`Load`。

后果是链条式的:`SKSEPlugin_Load` 从未执行 → 运行时
(`SkyrimTogetherRuntime.dll`)**从未被部署** → F2 菜单永不出现。
esp 与 logo 是普通 mod 文件,不受影响,所以"看起来装好了"。

**为什么本地测不出来**:SKSE 2.0.x(游戏 1.5.97)**从不读这个导出**,仍然走
`Query`,所以同一个 DLL 在 1.5.97 上一直是好的。

**修复**:`Code/skse_bootstrap/main.cpp` 增加 `SKSEPlugin_Version` 导出,
声明**版本无关**(`kVersionIndependent_Signatures` +
`kVersionIndependentEx_NoStructUse`)。这两个标志缺一不可:少了后者,
SKSE 1.6.629+ 会答 `only compatible with versions earlier than 1.6.629`;
少了前者,1.7.99+ 会要求 V5 地址库。本 DLL 不解析任何引擎地址,所以版本无关
是如实描述,也避免了每出一版游戏就要改列表、重编译。

新增门禁 `Code/plugins/tools/check_skse_bootstrap.py`,**8 种失败模式各自反向验证**
(含本次真实缺陷、导出被命名空间修饰、结构体字段换序、两个标志缺失、
把标志只写在注释里)。完整取证见 `docs/PITFALLS.md` §42。

### 修复:引导页里 OStim Together 是灰的、根本点不动(装好前置也没用)

**现象**:MO2 引导页里 `OStim Together` 灰掉、无法勾选,而且**先把 OStim
Standalone 装好也照样是灰的**。

**根因不是"前置没装",而是向导里一条永远不可能成立的判断。** MO2 只把
FOMOD 的 `fileDependency` 拿去比对**插件列表**(esp/esm/esl),指向别的东西时
它一律判为缺失,并在日志里明说:

```text
A dependency on non esp/esm/esl SKSE/Plugins/OStim.dll will always find it as missing.
```

上一版把 `detectFile` 写成了 `SKSE/Plugins/OStim.dll`,于是 `Active` 与
`Inactive` 两个分支都不可能命中,`NotUsable` 永远生效——**这条判断跟装没装无关**。

**修复**:

1. `detectFile` 改指前置真实携带的插件文件 `OStim.esp`;
2. `merge_fomod.py` 增加门禁:拒绝非 esp/esm/esl 的 `detectFile`
   (反向验证:改回 `OStim.dll` → exit 1 并指名 `plugins.json`);
3. 顺带修掉拒绝信息比代码旧一个版本的问题:`SelectGraphLayout` 已接受
   OStimNG `7.5.1.x`,而拒绝文案仍称最新只到 `7.5.0.2`——真被拒绝的玩家
   会被告知去装一个比他手上更旧的版本。文案现改为
   `7.4.0.3 (7.4c), 7.5.0.2 (7.5b) and 7.5.1.x`,并新增
   `check_ostim_support_message.py` 把**文案与选择器双向绑死**
   (四种漂移各自反向验证 → 均 exit 1)。

完整取证与命令见 `docs/PITFALLS.md` §41。

### 修复:消息门面只在 1.6.1170 上加载,其余版本静默失效

**现象**:上一条(bootstrap 缺 `SKSEPlugin_Version`)修好之后,1.6.1170 上
`SkyrimTogetherSKSE.dll` 能加载了,但**插件层的同步依然是死的**——桥接日志里
反复刷同一行,永不成功:

```text
send resolver pass failed in 4453 ms; waiting for mapped STR 1.8.0 runtime
ProxyResolver waiting: spans=20 transport=no loaded=no unloaded=no
```

**根因是同一类缺陷的另两副面孔**,而且**都在开发机(1.5.97)上看不见**:

1. **`STRPluginMessagingBridge.dll` 没有导出 `SKSEPlugin_Query`**。
   SKSE 2.0.x(1.5.97)要求 `Query` 与 `Load` **同时存在**,少一个就判
   `does not appear to be an SKSE plugin`——本仓库 1.5.97 的 `skse64.log`
   里就有这一行:

   ```text
   plugin D:\game\SkyrimSE\Data\SKSE\Plugins\\STRPluginMessagingBridge.dll does not appear to be an SKSE plugin
   ```

2. **三个门面 DLL 的 `compatibleVersions` 只写了 `1.6.1170`**,而
   `versionIndependence` 为 `0`。SKSE 2.2.x 对非版本无关的插件,会把这个列表
   拿去和**精确的运行版本**比对,于是 `1.6.317/640/1130/1179` 与
   `1.7.99/104`——**每一个本包都提供了地址库的版本**——全部被判
   `disabled, incompatible with current version of the game`。

两条都在同一处放大:**门面是所有伴侣插件取传输层的入口**(伴侣插件用
`LoadLibraryW` 按名加载它)。门面在某个版本上被 SKSE 跳过,那一条游戏线上
所有插件的同步一起失效。

**为什么 1.6.1170 上是好的**:列表恰好命中,所以这个缺陷只有换一个游戏版本
才暴露——这是本次会话第三次撞上"本地测不出"。

**修复**(`Code/plugins/patches/STRPluginMessagingAPI/0002-*.patch`,子模块只走补丁):

1. 三个 DLL 改为声明**版本无关**(`kVersionIndependent_Signatures` +
   `kVersionIndependentEx_NoStructUse`)。门面**只做签名/锚点扫描**:不含任何
   游戏头文件、不出现任何 `RE::` 类型、没有硬编码地址,所以版本无关是如实描述,
   而不是为了让门禁通过;
2. `STRPluginMessagingBridge.dll` 与 `STRPluginMessagingDiagnostic.dll` 补上
   `SKSEPlugin_Query`(返回 `true`,与 API 门面一致);
3. 三处 `kRuntime_1_6_1170` 列表与不再需要的前置声明一并清掉。

**新增门禁** `Code/plugins/tools/check_skse_entry_points.py`:把 SKSE 两族的
准入规则**回放**在每个手写插件 × **每一个随包发布的运行时**上。
插件按"SKSE 看到的名字"分组而不是按文件,因为 API 门面的版本数据与
`Query`/`Load` 分处两个文件。**反向验证**:还原补丁 → 5 条失败(桥接缺 Query、
三个列表不覆盖);把 bootstrap 的导出改名 → 6 条失败(缺 `Version`)。

完整取证见 `docs/PITFALLS.md` §43。

## v1.1.7(2026-09-27)

### 修复:v1.1.6 的远程仓库编译全红 —— 三处「只有 CI 才会说的话」

**现象**:`v1.1.6` 打 tag 后,四条流水线全挂 —— `Build windows`、`Release build`、
`Playable Skyrim Together Build` 三个都死在同一步 `Build the OStimNG prerequisite`,
`Plugin gates` 死在 `Verify the plugin source snapshots`。而**本地 13 个门禁全绿**,
因为这两处失败都发生在本地看不⻅的地方。

#### A. 那一步的 PowerShell 根本没被解析

```text
Write-Host "COMMONLIB_SSE_FOLDER=$env:COMMONLIB_SSE_FOLDER      ← 少一个收尾引号
Write-Host "COMMONLIB_SSE_FOLDER=$env:COMMONLIB_SSE_FOLDER"     ← 下一行是完整的重复
```

PowerShell 对**整段 `run:` 块**先解析再执行,一个未闭合的字符串会让整块**语法失败**,
于是这一步在任何命令执行前就退出 1,日志里连一行 CMake 输出都没有。三条流水线
共用同一段 `run:`(windows.yml 是被 `release.yml` 与 `windows-playable-build.yml`
复用的 reusable workflow),所以**一处笔误红三条**。

#### B. 那一步依赖的路径,在钉住的那个提交里已经被删掉了

```text
$commonlib = 'plugins/OStimNG/skse/extern/CommonLibSSE-NG'
if (-not (Test-Path (Join-Path $commonlib 'CMakeLists.txt'))) { throw ... }
```

OStimNG 的 `skse/CMakeLists.txt` 会断言 `COMMONLIB_SSE_FOLDER` 并 `add_subdirectory()`
它,所以构建 OStimNG **需要 CommonLibSSE-NG 的源码检出**,而不是插件链的那份 vcpkg 包。
这条路径过去由 OStimNG 自己的 `skse/extern/CommonLibSSE-NG` gitlink 提供 —— 而
`f20db82`("updated compiler",OStimNG 转向 vcpkg 的那一次)**删掉了这个 gitlink**。
我们钉的 `3954683b` 在它之后,于是路径在钉住的树里**不存在**,断言必然抛。

**修法**:把修订钉在 `Code/plugins/plugins.json` 的 `commonLibSseNg` 里,紧挨着它必须匹配的
OStimNG pin,由 CI 读取并 `fetch`。选 `v8.1.0`(`3c0f5a87`)不是随手挑的:
OStimNG 自己的 `126fc52` 就是"fixed build configs, skseapi definition and trampoline for
**commonlib 8.1.0**",而钉住的源码用到 `SKSE_EXPORT`、`GetTrampoline`、
`SkyrimVM::GetVMRuntimeData()`、`MessageBoxMenu::QueueMessage` —— 逐个在 `v8.1.0` 里核对过。

同一处还修掉三个**本来也会让这一步失败**的配置错误:

| 原来 | 问题 | 现在 |
| --- | --- | --- |
| `cmake -S ... -B plugins/OStimNG/build -A x64` | 手写的 `-A` 覆盖不了预设:OStimNG 用 **Ninja** 生成器 | `cmake -S plugins/OStimNG/skse --preset release` |
| 同上 | 没有 `VCPKG_OVERLAY_PORTS`,而 OStimNG 的 `clib-util` **不在上游 vcpkg**(baseline 与 master 都 404),manifest 解析不了 | 预设自带 `${sourceDir}/cmake/ports/` |
| 构建 `plugins/OStimNG/build` | 预设的 `binaryDir` 是 `${sourceDir}/build/${presetName}`,实际产物在 `skse/build/release` | 构建前断言 `CMakeCache.txt` 在正确的树里 |
| `Get-ChildItem 'plugins/*/vcpkg.json'` | **只找一层**,而 OStimNG 的 manifest 在 `plugins/OStimNG/skse/`,于是它 pin 的 `e3ed4186` 从不被 fetch,而 vcpkg 的 `builtin-baseline` 只跑 `git show`、没有回退 ⇒ 在 `--depth 1` 的 vcpkg 里必然 `failed to \`git show\` versions/baseline.json` | 改为 `-Recurse -Filter 'vcpkg.json'`(overlay ports 没有 `builtin-baseline`,会被守卫跳过) |

另外补上 `extern/openvr`:CommonLibSSE-NG 在 `ENABLE_SKYRIM_VR` 打开时会把
`extern/openvr/headers` 加进包含路径并链接 `lib/win64/openvr_api.lib`,而 OStimNG
强制打开三个运行时。它是 CommonLibSSE-NG 自己的子模块,用**它钉住的 gitlink sha** 直接
`fetch`(`submodule update` 在这个 `git init` + 单提交 `fetch` 出来的检出里解析不出 URL,
会报 `no url found for submodule path`)。

#### C. 备份里少了一个文件,因为插件自己的 .gitignore 说了算

```text
snapshots/plugins/OStimNG/.gitignore:553:gfxfontlib.swf
```

快照**原样保留插件的 `.gitignore`**(它本身是被备份的源码,且门禁要求它与钉住提交逐字节一致),
而嵌套的 `.gitignore` **优先于**根目录 `.gitignore` 里的 `!/snapshots/**` 否定规则。
于是 `gfxfontlib.swf`(111 KB,在 `flash/` 下)**在本地存在、却从未进过 git**。
本地的 `verify` 看的是工作区,于是绿;CI 是干净检出,文件不在,于是红 ——
**同一个门禁在两台机器上量的是两件事**。

**修法**:`snapshot` 用 `git add --force` 暂存(这是唯一能无视 ignore 规则的机制),
并把被覆盖的规则打进 `UPSTREAM.json` 的 `ignoredRulesOverridden`;`verify` 改问
**"git 是否记录了这个文件"**而不是"磁盘上有没有"。

### 新增两个门禁(各反向验证过)

| 门禁 | 拦什么 | 反向验证 |
| --- | --- | --- |
| `check_workflow_shell.py` | `.github/workflows/` 里任何 pwsh 块**语法不通过** | 把那个收尾引号删掉 → 精确指出文件、步骤、行号,exit 1 |
| `check_ostimng_build.py` | 清单没钉 CommonLibSSE-NG,或 CI 步骤不再读它 | 删 pin / 换成 tag / 改掉字段名 → 三种都 exit 1 |

`check_workflow_shell.py` 用 PowerShell 自己的解析器,覆盖全部 35 个块;
`${{ ... }}` 先替换成字面量再解析,否则 GitHub 表达式会被当成语法错误误报。

### 顺带修掉三个同类问题

- `plugin_snapshot.py update` 仍在用"根目录有没有 `CMakeLists.txt`"判断子模块是否检出,
  于是 OStimNG 每次都被静默跳过(`not checked out, skipped`)—— 而这正是重钉时必须覆盖的
  那一个前置。§39.5 说"统一为 `is_checked_out()`",但当时只改了 `snapshot` 与 `verify`。
- `UPSTREAM.json` 的 `upstream` 读的是 `.git/config`(只在子模块初始化后才有条目),
  所以 OStimNG 那份记成了**空字符串**。改读 `.gitmodules`(受版本控制的普通内容)。
- `snapshot` 每次都会重写 8 份 `UPSTREAM.json`,把 `ignoredRulesOverridden` 写成空数组,
  产生无意义的 diff。现在只在非空时写入。

### 门禁全绿(13 项)

`strpm_contract`、`plugin_snapshot verify`、`plugin_patches verify`、`papyrus_archive`、
`check_exports`、`check_transport_compat`、`check_skee_abi`、`check_ostimng_versions`、
`check_ostimng_build`、`check_workflow_shell`、`check_facegen_scope`、`check_docs`、`merge_fomod check`。
## v1.1.6(2026-09-27)

### 修复:两处「刚进游戏就闪退」—— 一处在我们的运行时,一处在 RaceMenu 接口版本

**现象**:MO2 + SKSE 打开游戏「一小会儿就直接闪退」。日志里三种崩溃,其中两种反复出现,
都是 `EXCEPTION_ACCESS_VIOLATION`,而且两次读到的都是 `0x14`
(即 `TESForm::formID` 的偏移)附近那个**不可能是地址**的数:

```
# A（三次）skee64.dll+0x81be8                  mov eax, [r14+0x14]   target 0x15
# B（一次）  SkyrimTogetherRuntime_1_5.dll+0x16CFA8  mov eax, [r15+0x14]   target 0x14
```

**A:RaceMenu 的接口按名字取,布局没人核对。** 插件发 `kMessageExchangeInterface` 拿到
`IInterfaceMap`,再 `QueryInterface("Overlay")` —— 这一步**只按名字查表,不校验布局**。
实测本机的 `skee64.dll` 里 `.?AVOverlayInterface@@` 的 vtable 是**版本 1**(23 槽),
而两个插件声明的是**版本 2**:插件以为槽位 `[11]` 是 `GetOverlayCount`,
DLL 那里其实是 `RevertHeadOverlays`。于是 `GetOverlayCount(Spell, ...)` 把
`rdx = 1`(枚举值 `OverlayType::Spell`)当成 `TESObjectREFR*` 传进去,
读 `[1+0x14]` = `0x15` → 闪退。

这也解释了**为什么能进游戏**:四次 `Normal` 调用的 `rdx = 0`,
而 v1 的 `RevertHeadOverlays` 开头恰好有 `testq %rdx,%rdx; je`,
所以它们**全部安全返回**;只有 `Spell` 才踩中 —— 正是「玩一小会儿才崩」。

**修法**:两个插件(补丁形式,见 `Code/plugins/patches/`)在采用接口前**先查 `GetVersion()`**,
版本不足就拒绝该接口并打印明确错误,而不是继续调用。`GetVersion()` 在每种布局里都是槽位 `[1]`,
是唯一能在调用其它方法之前安全调用的方法。`IBodyMorphInterface` 只追加,所以版本 4 的声明对版本 5 仍有效,
门禁写成 `>= 4` 而非 `== 5`。

**B:引擎用空指针调用装备钩子来清空槽位。** `Code/client/Games/Skyrim/EquipManager.cpp` 的六个钩子里,
只有 `apActor` 判了空。法术/龙吼四个直接读 `apSpell->formID` / `apShout->formID`,
而**引擎清空一只手时就是用空形式调用它们**的 → 读 `[0+0x14]` → 闪退。
(物品那两个钩子的 `pSlot` 早就写了 `?: 0` 三元写法,说明这个坑当时知道,只是没推广过去。)

**修法**:六个钩子统一判空,且为空时**原样转发给原函数**而不是返回 `nullptr`
(返回 `nullptr` 会让引擎以为「这次装备被取消」,与清空槽位的行为不符);
`pEquipSlot` 同样补上 `?: 0`。

### 新增门禁:SKEE 接口声明的顺序与版本

新增 `Code/plugins/tools/check_skee_abi.py`(已接入 `plugins.yml`):
把两个插件声明的 SKEE 方法**顺序**逐槽与 RaceMenu 的真实顺序比对,并要求每份声明都带
版本门禁。**反向验证过三种改坏方式**,每一种都会被它抓到:删掉 `RevertOverlay`、
对调 `GetOverlayCount`/`GetOverlayFormat`、移除版本门禁。

**顺带修掉门禁自己开的一个洞**:门禁**拒绝**接口时只是把指针置空,并**没有**让所有人停止使用它。
`MorphSyncService::Start()` 在 BodyMorph 接口不可用时确实直接返回,所以同步线程不会启动 ——
但 `main.cpp` 里**传输层是独立启动的**,`Start()` 失败不会回滚它,于是远端包照收,
`HandleMorphPacket()` → `TryApplyRemote()` 的漂移分支仍然会执行 `_bodyMorph->ClearMorphs()`
等四个调用,**全是没判空的解引用**。这条链上 `CaptureMorphs()` 和 `TickOnGameThread()` 都判了空,
只有「网络上来的那一条」没判 —— 于是它看起来像已经修好了。
MorphSyncTogether 侧补上这一处判空;
**OStimTogether 侧查过了,不需要改** —— `RaceMenuOverlayBridge` 里每个取用缓存接口指针的函数
(如 `ApplyRemoteOverlayChunk` / `RefreshLocalOverlayGeometry`)开头都有
`if (!overlay || !overrides) { ... return; }`,被拒绝的接口根本走不到调用点。
判空按「四个调用是一笔事务」处理:进门就返回,
而不是只做其中一部分把代理留在改了一半的形态上。

**被拒绝之后会掉什么功能** —— 实测本机那份 `skee64.dll`(FileVersion 4.0.0.0,2020-10-04)的
`GetVersion()`:`BodyMorph` 报 **4**(门禁要求 `>= 4`,**通过**),`Overlay`/`Override` 报 **1**(要求 `>= 2`,拒绝)。
所以**身体/面部 morph 同步照常工作**,只是覆盖层那一半(阴毛/体毛/面部覆盖层)不再同步;
两个被拒绝的接口各打一条 `error` 日志说明原因。要不要写 v1 兼容层、以及为什么不该写,见 `docs/PITFALLS.md` §37.6。

> 详细取证(反汇编、`target address` 的读法、两个布局的逐槽对照、拒绝接口之后的残留解引用)见 `docs/PITFALLS.md` §37。

### 修复:自定义种族的头不显示(任何「头不是本机生成的」角色)

**现象**:用**自带头部 `.nif`/`.tri` 的自定义种族**时,头是**隐形**的 ——
没有用 RaceMenu 雕刻、没有套预设、没有动任何滑块,只是把种族自带的头部网格直接用上。
报告者补了一条关键线索:**同一套自定义头部网格配原版种族是正常显示的**
(Nolvus v6 给原版种族换的就是 HPH High Poly Head + Expressive Facegen),
而这一切在 **STR 1.7.1.42 上都是好的**。所以既不是 `.nif`/`.tri` 本身的问题,也不只是「自定义种族」四个字,
而是**「这张脸不是在本机生成的」**。

**根因:上游 #764 删掉了「强制运行时生成脸」那一行。**

引擎用 `bUseFaceGenPreprocessedHeads`(默认 **1**)决定角色的头从哪来:
从磁盘上预先烘焙好的 `Data\meshes\actors\character\FaceGenData\FaceGeom\<插件>\<formid>.nif` 加载,
还是**当场生成**。远端玩家的外观是**序列化后传过来、在本机重建**的,
它描述的那张脸**从没在本机生成过**,所以磁盘上**不存在**匹配的烘焙网格 ——
引擎照默认路径去找、找不到,头就是空的。

自定义种族踩得最狠,因为**它的头只以种族自带的 `.nif`/`.tri` 形式存在,从来没有 FaceGenData**;
而原版种族即使换了自定义头部网格,那些烘焙资源仍然对得上,所以「原版种族 + 自定义头」正常 ——
这正是报告里「vanilla races with custom head meshes work fine」的由来。

1.7.1 之所以是好的,是因为它**每帧**都在执行:

```cpp
// Every frame make sure we won't use preprocessed facegen
POINTER_SKYRIMSE(uint32_t, bUseFaceGenPreprocessedHeads, 378620);
*bUseFaceGenPreprocessedHeads = 0;
```

上游 PR #764(`7e09f8b4`,`Now respects bUseFaceGenPreprocessedHeads flag.`)把这行删了,
为的是修**原版角色**的脖子接缝、黑脸与头部错配(v1.8.0 发布说明:
`Re-enabled bUseFaceGenPreprocessedHeads; this fixes occasional mismatched heads,
face discoloration and facegen mods`)。
**本仓库每个 1.1.x 都含这个删除,而报告者说好的 1.7.1 不含** —— 版本边界与报告完全吻合
(`git merge-base --is-ancestor 7e09f8b4` 对 v1.1.0/1.1.1/1.1.5/HEAD 为真,对 v1.7.1 为假)。

仓库里其实**一直躺着一个专为此写的函数**:`Code/client/Games/Skyrim/Actor.cpp` 的
`Actor::QueueUpdate()` —— 它把该 INI 置 0、调用引擎的 Actor 更新、再把值放回去,
正是「按需强制一次运行时生成」。但它**从未被任何调用点引用**
(1.0.x 到 1.8.x、以及上游 master/dev 全部 ref 都 grep 过,只有它自己的定义),
因为当年走的是「每帧全局清零」那条路。

**修法:把「清零」收窄到真正需要它的角色上。**

`CharacterService` 新增一个**窗口**:

- `SetRemoteFaceGenForce(true)` 在**两个**「外观来自网络」的生成路径上、
  **在把 actor 交给引擎之前**调用(引擎要到**下一帧**才建身体,晚一步就晚了);
- 它先把**当前值存下来**,关窗时**存回原值**,不写死成 1 ——
  这个设置不是我们拥有的,玩家自己关过就得保持关着;
- `UpdateRemoteFaceGenWindow()` **每帧**从 `WaitingFor3D` 推导窗口该不该开:
  只看 `FormId == GameId{}`(即 base 是**在本机从缓冲区重建**的)那些角色。
  用「推导」而不是「配对开关」是故意的 —— 断线、生成失败、角色被删都会自动掉出这个视图,
  不需要谁记得去关,与它下面那个早就在等同一个组件的 `RunSpawnUpdates` 一致。

于是**两边都满足**:本机生成的角色(玩家的,以及所有磁盘上就有烘焙头的 NPC)
继续走 #764 想要的预处理路径,脖子接缝那个修复不被回退;
只有**头从网络来**的角色改用运行时生成 —— 也就是那些隐形的头。

### 新增门禁:预处理头部开关的作用域

新增 `Tools/Scripts/check_facegen_scope.py`(读源码前**先剥注释**),查五件事:

1. **全局清零不准回来** —— 它正是 #764 回退掉的东西,
   在 `TiltedOnlineApp.cpp` 里重新加回会被抓(那里长的注释里提到该设置不算);
2. **每个写这个开关的地方都必须「先存后还原」** —— 包括那个没被调用的 `Actor::QueueUpdate()`,
   它是个正确的存/还原,门禁要求它保持正确;
3. **窗口必须按「来自网络」定界**(等 3D + 空的 FormId),否则不是恒开就是恒关;
4. **窗口必须在 `Actor::Create` 之前打开** —— 身体是后面那一帧才建的;
5. **每帧必须关窗**,以及**取 INI 前必须判空**。

**反向验证过十种改坏方式**,每一种都被抓到:重新加回全局清零;
两处生成路径各自把开窗挪到 `Actor::Create` 之后;去掉每帧关窗;
把窗口放大成「所有等待中的角色」;把 `FormId` 比较反过来;
从视图里去掉 `RemoteComponent`;去掉 INI 判空;两个「先存后还原」各丢掉一次还原。

> 其中「把 `FormId` 比较反过来」**最初逃过了第一版门禁** —— 那一版只查标记是否存在、
> 不查它是不是那个比较,已改成用正则校验比较本身。改坏方式能被自己写的门禁漏掉,
> 只有真去改一遍才会发现。

> 详细取证(上游 #764 的完整正文、`merge-base --is-ancestor` 逐个 tag 的版本边界、
> 以及为什么 tint 纹理那条路不是原因)见 `docs/PITFALLS.md` §38。
### 新增:OStimNG 作为 OStim Together 的前置 mod 纳入本仓库结构

OStim Standalone 的源码以 `plugins/OStimNG`(子模块,`3954683`)进入本仓库,并在
`Code/plugins/plugins.json` 的 `prerequisites` 中登记——它不是随包插件,而是
OStim Together 依赖的运行时。

**为什么是"前置"而不是"打包"**:OStimNG 自己的 `data/` 是 256 MB 的动画、贴图与音效
(974 个 `.hkx` 153 MB、338 个 `.dds` 71 MB),本仓库既不构建、也不打补丁、更不出货。
把源码纳入结构是为了**钉住版本、可打补丁、可校验**,不是为了替玩家分发资源。

**快照只备份代码**:`snapshotScope` 在清单里显式声明,只备份 `skse/`、`flash/` 等
700 个文件 2.4 MB。是否备份由"本项目是否消费它"决定,而不是由一个体积阈值决定。

### 新增门禁:`check_ostimng_versions.py`——"1.5.x 到 1.7.x"从声称变成校验

OStimNG 通过 SKSE 地址库解析引擎地址,而 `REL::ID()` 在 id 缺失时**不是软失败,是
加载即中止**(CommonLibSSE-NG `REL/IDDB.cpp` 的 `report_id_lookup_failure`)。
所以"支持 1.5.x 到 1.7.x"是关于**随包地址库里 id 覆盖率**的断言,本门禁逐条核对:

- `RELOCATION_ID(se, ae)` 的 SE id 必须在**每一个** pre-AE 地址库(10 个,含 1.5.97)里;
- 其 AE id 必须在**每一个** AE 地址库(14 个,含 1.6.1170 / 1.7.104)里。

当前 33 个 `RELOCATION_ID` 站点全部通过。**反向验证过**:把任一 id 改成不存在的值,
门禁 exit 1 并指名文件与行号。

### 修复:OStimNG 在 1.5.80/1.5.97 上会改坏一条 6 字节指令

`GameHooks.h` 的三处 hook 用 `write_call<5>` / `write_branch<5>`,它们**只覆盖 5 字节**,
因此只有当站点本身是 5 字节 rel32 分支时才安全。实测 1.5.97 的 `SkyrimSE.exe`:

| 站点 | SE 偏移 | 首字节 | 真实指令 | 结果 |
| --- | --- | --- | --- | --- |
| IsThirdPerson | `+0x132` | `E8` | `call rel32`(5B) | 正常安装 |
| **GetHeading** | `+0x152` | `FF` | **`call [rax+0x520]`(6B)** | **改坏** |
| PackageStart | `+0x47` | `E9` | `jmp rel32`(5B) | 正常安装 |

`write_call<5>` 在 GetHeading 处会把 6 字节指令劈开:末尾 1 字节 `00` 变成孤立尾巴,
而且它用 `disp32` 中间四个字节反推"原函数地址",得到 `0x63a8e7`——一个纯属巧合落在
`.text` 里的地址。

**修法**:打补丁后每处先核对首字节是否等于期望的 opcode,不符则**跳过该 hook 并 warn**,
而不是写进去。补丁落在 `Code/plugins/patches/OStimNG/`(子模块不可推送,CI 又用
`--force` 检出,工作区改动会被丢弃)。已用真实 1.5.97 二进制验证:GetHeading 跳过,
另两处照常安装。

### 修复:OStim Together「没激活」——日志里现在会指名缺 OStim

**根因不是配置,是前置根本没装**。实测该安装的 `skse64.log` 里**只有
`OStimTogether.dll`,没有 `OStim.dll`**;MO2 的 21 个 mod 里没有任何 OStim;
`OStimTogether.log` 逐条记录 `OStim.dll module not found` 与十余条
`unavailable`/`missing`,而插件本身照常 READY。

也就是说:"没激活"的观感,来自**插件加载成功、但每个子系统都自行关闭**,且没有任何
面向玩家的说明。三处修复:

1. 缺 OStim 时改为 `critical`,并写明要装 OStim Standalone、要确认 `OStim.dll` 与
   `OStim.esp` 已启用;
2. FOMOD 向导里 OStim Together 的类型改为**依赖型**:检测 `SKSE/Plugins/OStim.dll`
   ——`Active` → Recommended,`Inactive` → CouldBeUsable(装了没勾),缺失 → NotUsable;
3. 描述文字在两种语言下都点名"需要先安装 OStim Standalone"。

### 修复:七个插件的联机同步"装上就是死的"——门面不再只看 ini

出货 ini 把 `STRBridgeModule` 指向桥接,而桥接在本框架内**结构性地收不到任何包**:
它的接收端在 `SkyrimSE.exe` 自己的分配区里找 `TransportService` 的 RTTI,而本框架的
`TransportService` 住在独立模块里,于是解析器永不就绪、每次 `send()` 返回
`kNotConnected`。本次实测再次确认:该安装的 `STRPluginMessagingBridge.log` 反复打印
`receive resolver: TransportService RTTI type descriptor not uniquely resolved`,
而 `OStimTogether.log` 记录 `backend=str-bridge knownPeers=0`。

**修法**:给门面打补丁——在读取 ini 指定模块**之前**,先探测**已加载**的框架运行时
(两个版本名都试),探测到就用它。关键约束:**只用 `GetModuleHandleW`,绝不用
`LoadLibraryW`**。两个运行时是不同 ABI,且任何安装后两者都躺在游戏根目录,盲目
`LoadLibrary` 会把跨 ABI 映像拉进进程(这正是 §32.6 记录过的风险)。探测不到时行为
与原先完全一致,独立桥接场景不受影响。

该行为已由 `check_transport_compat.py` 的新断言绑定:文档必须描述"已加载运行时探测",
补丁必须同时提到两个运行时名,且**新增代码里不得出现 `LoadLibraryW`**。三种回归都
反向验证过。

## v1.1.5(2026-09-27)

### 文档约定:已修的缺陷不再写进 README

两个 README 里那节「曾经误报为冲突、现已兼容:KiLoader / KreatE」**已删除**,
同时把这条约定写进 `CODE_GUIDELINES.md`(新增「Where a fix is explained, and
where it is not」一节),以后照此办理。

**为什么删**:README 回答的是「装哪个版本、什么能用、出问题怎么办」。
一个**已经修好**的缺陷不属于这三者 —— 修复出货之后读者没有任何可做的动作,
于是那一段只会被读一次,然后永久占着位置,把玩家真正要看的内容越推越远;
更糟的是它会**过时成一段关于没人再跑的构建的描述**。

**以后放哪里**:

| 内容 | 去处 |
| --- | --- |
| 缺陷已修(包括被广泛误报的、用户点名问过的) | `CHANGELOG.md`;细节值得留就进 `docs/PITFALLS.md` |
| 玩家必须**动手**的变化 | README:要装的版本、支持的游戏版本、**仍然存在**的冲突、安装步骤、该看的日志 |
| 已修缺陷在 README 的痕迹 | 至多版本表里一行 |
| 「不要启用这个 Mod」清单 | 只列**仍然冲突**的;修好就删掉,不留作历史 |

**判据**(写之前先问):*这个修复出货之后,读者会因此做什么不一样的事?*
答案是「没有」,就写进 CHANGELOG,不写进 README。

> 顺带把 1.1.5 那一行从「修复叙事」压回一行摘要:版本表是索引,不是事故报告。
### 修复:插件补丁里的 fmt 占位符写错,导致 CI 编译失败

**现象**:`Build windows` 与 `Playable Skyrim Together Build` 两个工作流都红,
标注为 `companion plugin build failed: DAVSyncTogether (build)`,编译错误是
`DAVConfigIndex.cpp(109,17): error C7595: 'fmt::v12::fstring<...>::fstring': call to
immediate function is not a constant expression`。

**根因**:错在**本仓库自己的补丁文件**,不在插件源码里。
`Code/plugins/patches/DAVSyncTogether/0001-config-index-enumeration.patch` 里那两条
日志的占位符写成了 `path=\"{\"} \" error=\"{\"}` —— **转义引号写进了花括号里面**。
C++ 编译器看到的是 `path="{"} " error="{"}`;`fmt` 在**编译期**解析格式串时,
把花括号里的 `"` 当成**格式说明符**,而 `std::string` 没有对应它的 formatter,
于是直接编译失败。写成 `path=\"{}\" error=\"{}\"` 即两个正常占位符,问题消失。

**为什么 CI 挡不住、本地也看不见**:

- `git apply` 只看补丁**能不能打上**。这条补丁语法完全正确、上下文完全匹配,
  `PATCH VERIFY OK` 一路绿灯 —— 它根本不知道打进去的 C++ 编不过;
- 本地没有 Windows SDK 与 vcpkg,`fmt` 的编译期检查跑不起来,
  所以这个错误**只在 Windows 构建里、十几分钟之后**才现身。

**修法**:补丁里两行占位符改回 `\"{}\"`。**补丁语义一字未动**,
只是把转义引号移到花括号外面;`git diff` 恰好 2 行。

**顺带加了一条门禁**(`Code/plugins/tools/plugin_patches.py`):`verify` 与 `check`
现在会**逐行扫补丁的新增行**,凡是花括号里出现引号的占位符一律报错,并直接指出
正确写法。规则刻意收得很窄 —— 真正的格式说明符是宽度/精度/类型字母,
引号出现在里面**必然是转义写错了**,所以不会误伤合法写法。反向验证过:
把补丁改回原来的写法,门禁立刻报出两处(第 25、34 行)。

> **教训**:补丁能应用 ≠ 补丁编得过。`git apply` 的绿灯只证明文本能对上,
而**唯一的真值来源是编译器**。这类「本地绿、远端红」的坑,
要么在本地补齐工具链,要么把判据写成门禁 —— 这次选了后者,
因为它是**可判定**的:花括号里不该有引号。

### 修复:KiLoader / KreatE 的「不兼容」弹窗(它们本来并不冲突)

**现象**:装了 KiLoader(以及依赖它的 KreatE / AELAS)之后,启动游戏弹
`KiLoader initialization failed`,细节是两条 `Couldn't open logging file`,
第二条 `std::system_error: The process cannot access the file because it is being
used by another process`。看起来像本框架与 KiLoader 不兼容,上游也有同样的
报告([TiltedEvolution#766](https://github.com/tiltedphoques/TiltedEvolution/issues/766),
至今 open,回复是「无法承诺支持 KiLoader」)。

**根因**(证据来自报错文本本身,不是推测):

1. 报错里的进程名是 **`KiLoaderTPProcess`** —— 那是**本框架的 CEF 辅助进程**
   `TPProcess.exe` 的名字,不是游戏进程。也就是说 KiLoader 是在**我们的辅助进程里**
   被拉起来的;
2. 为什么会被拉起来:`TPProcess.exe` 位于**游戏根目录**,Windows 解析它的导入表时
   **先搜 exe 所在目录**,再搜 `System32`;而 `libcef.dll` 按名字延迟加载
   `dxgi.dll`、`d3d11.dll`、`d3d12.dll`、`dcomp.dll`(自写 PE 解析读延迟导入表确认)。
   这四个正是 ENB、ReShade、SpecialK、Community Shaders 用来挂代理的名字,于是
   辅助进程加载的是**游戏根目录的 ENB 代理**,ENB 再带起 `enbseries\` 下的卫星 DLL,
   `KiLoaderSatelliteENB` 随之拉起一份 KiLoader;
3. 为什么那一份必死:KiLoader 的日志路径按**当前进程名**推导,于是它去开
   `Data\KiLoader\KiLoader.log` —— 而游戏进程**整场**都占着这个文件
   (报错第一行的 `AppData\Local\KiLoaderTPProcess\Logs\KiLoader.log` 同样来自
   进程名)。同一个框架的两份实例无法共用该文件,第二份在启动阶段即失败。

所以这**不是**功能冲突,而是辅助进程误加载了不属于它的 DLL,再由 KiLoader 如实报出
了这个碰撞。

**修法**:`Code/tp_process/main.cpp` 在 `CefExecuteProcess` 之前,按**完整路径**加载
`System32` 的 `dxgi.dll`、`d3d11.dll`、`d3d12.dll`、`dcomp.dll` —— 这四个既是
`libcef.dll` **延迟导入表**里的名字(直接读 PE 得到,其余延迟导入都是 USER32/SHELL32
这类不可能被 mod 占用的系统名),也正是图形框架会占用的代理名。加载器在搜索任何目录
之前会先按**基名**匹配已加载模块,因此随后的按名加载(延迟导入正是按名)直接命中它们,
不再落到游戏根目录。

两个细节是**实测**出来的,不是照抄:

- **顺序必须 `dxgi` 在前**:`System32\d3d11.dll` 静态导入 `dxgi.dll`,先加载 `d3d11`
  会让它自己的依赖解析到游戏根目录的代理,加载失败并报 `ERROR_BAD_EXE_FORMAT (193)`。
  本机用一份同名的假代理复现:先 `d3d11` → 193;先 `dxgi` → 两者都正确落到 `System32`。
  (`d3d12.dll` / `dcomp.dll` **不**导入 `dxgi`,这点也实测过,所以只有前两个的相对顺序
  是硬要求);
- **不能改用 `SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32)`**:本框架自己的
  overlay 运行时(`libEGL.dll`、`libGLESv2.dll`、`vk_swiftshader.dll`、
  `d3dcompiler_47.dll`)同样部署在游戏根目录并**按名**加载,收紧搜索路径会把
  「辅助进程误加载 ENB」换成「overlay 起不来」。只钉这四个名字即可,
  因为 `libcef.dll` 的延迟导入表里,只有这四个是第三方会占用名字的图形 DLL。

**未走的路(以及为什么)**:把 `d3d11.dll` / `dxgi.dll` 从游戏根目录删掉、或让 ENB
改用别的代理名,都是在要求玩家改自己的 mod 列表;给辅助进程换一个不在游戏根目录的
路径,则要动 CEF 的 `browser_subprocess_path` 与打包布局,收益不抵风险。

> **教训**:「两个 mod 不兼容」这个结论,在**报错里的进程名不是游戏进程**时就要先怀疑。
> 本例里 `KiLoaderTPProcess` 这一处字样已经指出肇事者是我们自己的辅助进程;
> 顺着「谁在这个进程里、它为什么在这里」查,比顺着「KiLoader 和谁冲突」查快得多。

### 版本策略:把 **1.1.1** 指定为稳定版(推荐版本回到 1.1.1)

两个 README 的横幅推荐版本改为 **1.1.1**,并在版本表里用
`**1.1.1** ⭐ **稳定版**` 单独标出;其余版本行**取消加粗**,保持普通样式,
让「哪一个是稳定版」一眼可辨。中英文两版同步。

> **这一条与 §35 的记录并不矛盾,是同一个判据的两种用法。** §35 里门禁抓到的
> 「推荐版本落后两版」,指的是**无人维护造成的漂移**:横幅停在 `1.1.1`、
> 而最新 tag 已经到 `1.1.4`,谁也不知道该装哪个。现在横幅回到 `1.1.1`,
> 是**有理由的选择**,并且这个理由写在横幅里、由门禁绑住(见下):
> `1.1.2 ~ 1.1.4` 照常出货、照常可联机,只是**未被指定为稳定版**。
> 两者的区别不在数字,在于**说不说得清为什么**。

门禁相应扩了 `recommended-version` 这条(没有新开一条检查):横幅指定的推荐版本,
必须**正是**版本表里标了 ⭐ 稳定版的那一行。这样「横幅说 1.1.1」与「表格标 1.1.1」
不会各自漂移,也顺带保证表里**恰好只有一行**是稳定版 —— 两行都标、或一行都没标,
都会红。四条分支(横幅与标记不一致 / 两行都标 / 一行都没标 / 中英各自不一致)
都做了反向验证。

同时把版本表行的识别放宽到允许数字后面跟稳定版标记(`**1.1.1** ⭐ **稳定版**`),
但**只允许这一个标记**:第一版放宽成「数字后面跟任意内容」时,
`| 1.5.3 ~ 1.5.97(老 SE) |` 这类**游戏版本表**的行也会被当成版本表行,
于是缺行可以用无关的行蒙混过去 —— 正是这条检查存在的意义被它自己绕开了。
已收紧为只匹配 `⭐ **稳定版**` / `⭐ **stable**`,并做了反向验证。

### 文档与约束:让"文档更新"成为可检查的规则

`CODE_GUIDELINES.md` 新增一节 **Documentation is part of the change**:
**每次版本修改必须在同一个提交里更新文档**,并且发 tag 之前必须先过一遍
`Tools/Scripts/check_docs.py`。光写规则没用,所以规则配了一条门禁
(已接入 `plugins.yml`,并在 `docs/RELEASE-AND-MO2.md` 的发版步骤里前置)。

新增 `Tools/Scripts/check_docs.py`,四项检查:

| 检查 | 断言 |
| --- | --- |
| `coverage` | 所有引用 1.5.x 覆盖率的文档都必须引用**当前**数字;历史口径必须显式标注"已被取代/当时" |
| `version-tables` | 两个 README 的版本表必须覆盖 CHANGELOG 的每一个版本,且两者**互不矛盾** |
| `tags-have-sections` | 每个已打的 tag 都必须在 CHANGELOG 里有对应小节 |
| `recommended-version` | 推荐版本必须真实存在(有 CHANGELOG 小节),中英一致 |

**它立刻抓到了四处真实问题**,都已修:

- **`v1.1.3` 打了 tag 却没有 CHANGELOG 小节** —— 该版本的内容只存在于两个
  README 的版本表里,CHANGELOG 里既没有 `## v1.1.3` 也没有任何小节。
  已补写完整小节;
- **`v1.1.4` 在两个 README 的版本表里都没有行** —— 版本表停在 1.1.3;
- **覆盖率数字是旧口径**:`README.md`、`README_EN.md`、`Tools/ida/README.md`、
  `docs/LAN-RADMIN-GUIDE.md` 都还写着 3080/3069/11 个,而生成器给的是
  **3082/3069/13 个**;
- **推荐版本落后两个版本**:横幅写着 `1.1.1`,而最新 tag 是 `1.1.4`。

另外把"未映射 id 是**函数**还是**数据**"这条区分写进了两个 README 与
`docs/LAN-RADMIN-GUIDE.md`:函数 id 的桩"调用即返回 0"可以安全调用,
数据 id 不能走同一条路(桩是可执行内存,当数据读会读到机器码)。
这正是 §34 那处修复背后的判据,值得让读者也看得到。

门禁本身写错过两次,**两次都是自测发现的**,都已修正:

- 第一版只检查"文档里出现过正确数字",于是**同一文档里新旧两个值并存时它会通过**
  ——正是它要防的漂移形态。改为枚举文档中**所有**覆盖率形状的数字逐个比对;
- 第二版又太紧:把 `Tools/ida/README.md` 里**有意保留的历史口径**
  (已标注 `Superseded`)误报,还把 `3698/3698 offsets validated` 这个
  **另一个指标**误判成 id 覆盖率。改为"历史口径必须显式标注为历史",
  并排除两边相等的 pair。

还有一处**通过得太容易**的坑:该检查依赖 `git tag`,而 `actions/checkout@v4`
**默认不取 tag**,那样它会跑零次并报告成功。已在 CI 侧显式加
`fetch-depth: 0` 与 `fetch-tags: true`,并在门禁侧加了"一个本仓库的 tag
都看不见就直接报错"的守卫。

顺带记下一个此前没人写下来的事实:**tag 命名空间是共享的**。上游与本仓库
都用 `v1.1.x`,且指向不同提交(本仓库 `v1.1.0` = `03cf2cbb`,
上游 = `fd8748f0`),所以"这个 tag 是不是我们的"必须显式列出系列
(`OWN_TAG_SERIES`)而不是靠名字判断。

### 同步上游 #901:Havok 控制器时间步长(未发布,来自上游 dev)

上游 `dev` 上有 2 个 v1.8.2 之后的提交,其中 `fbf72883`(#901)修的是
**远端 actor 的 Havok 控制器时间步长为 0** 导致的物理污染:

远端 actor 跳过原生移动处理,新建的 controller 因此停在 `deltaTime = 0`。
插值位置更新仍会重置速度,**除以 0 产生非有限加速度**,再传播进接触质量修正 ——
表现就是"物体和 NPC 碰撞后、踩到散落物品后**异常滑行**"。修法是两条:
在远端移动处理里刷新 controller 时序,以及在强制位置更新前刷新、拿不到有效步长时
推迟 controller 定位。涉及 7 个文件(3 个新增)。

**1.5.x 适配(两处,都不是猜的)**:

- 新增的 3 个文件里,`hkStepInfo` 是**纯数据布局**,与版本无关,原样照搬;
  `bhkCharacterController::stepInfo` 的 **0x80 偏移是实测出来的**,不是照抄上游:
  本机有一份真实的 1.5.97 `SkyrimSE.exe`,在其 `.text` 里搜"加载物理步长全局 +
  求倒数 + 通过同一寄存器写 +0x88 与 +0x8C"这一模式,**全镜像只有一处**
  (RVA `0xDBFDD4`,所属函数 `0xDBF630`)。该 RVA 在
  `version-1-5-97-0.bin` 里是真实符号(id `76436`),它读的全局
  (`0x1E083A8`)也是(id `512261`)—— 两头都有名字,不是插值猜出来的。
  0x88/0x8C 即 `deltaTime`/`invDeltaTime`,所以 `stepInfo` 就在 0x80,
  与上游对 1.6.x/1.7.x 的断言一致,**该成员在两代之间没有移动,故不像 `Actor`
  那样需要版本分支**;
- 两个新 id 在 **1.5.97 上都没有映射**(已核对:`39856`、`389089`;
  同一对 id 在 1.6.317/640/1170/1179 与 1.7.99/104 的库里**全部有映射**,
  所以 1.6.x/1.7.x 走的就是上游原路径)。两者性质不同,处理也不同:
  - `39856`(`AIProcess::GetCharController`,`AIProcess.cpp:14`)是**函数 id**。
    `VersionDb.h:60-62` 明文写着桩的契约是"调用它返回 0 而不是崩",
    所以按本仓库既有写法用 `POINTER_SKYRIMSE` + `ThisCall` 是**正确且一致**的,
    未映射时自然得到 `nullptr`,两个调用点都判空跳过刷新;
  - `389089`(物理步长那个 **float 全局**,id 是**数据 id**)**不能**这样写:
    未映射时桩是一段**可执行内存**(`48 31 C0 0F 57 C0 C3`),`*stub()` 是
    **把它自己的机器码当 float 读**。实测那几个字节解码为 `1.895e-29`,
    恰好被 `UpdateDeltaTime` 的 `<= 0.0001` 规则挡掉 —— 但那是**桩的编码
    碰巧**,不是契约:换一段解码成大有限值的桩,就会被当成合法步长直接写进
    controller。故改用 `FindAddressById` 显式解析、失败返回 0,与
    `GarbageCollector::Get()`、`LeveledNpcSystem::CanRecordPick()` 处理各自
    未映射数据 id 的做法一致;

**顺带修掉一处我自己引入的空指针面**:上游的 `ForcePosition` 没有解引用
`GetExtension()`,而本仓库的 `Actor::GetExtension()` (`Actor.cpp:285-298`)
在既非 `ExActor` 也非 `ExPlayerCharacter` 时**返回 nullptr**。直接照搬会在
"本来就是要让它更安全的这条路径"上新增一个解引用。已按同文件
`HookSetPosition`(`:201`)的写法判空。

验证:`hkStepInfo` 的退化阶梯用 g++ 编成可执行并**分进程**跑过两种映射状态
(静态缓存"只问一次",同一进程内换映射不会重算,这点也是实测发现的):
1.6.x/1.7.x 有映射 → `deltaTime=0.0166667 / inv=60`;1.5.x 无映射 →
退回移动步长 `0.02 / inv=50`,连移动步长也没有时保留 controller 里已有的值。
另用 g++ 把改过的逻辑连同一份最小 shim 编译运行过,确认签名与调用约定无误。

## v1.1.4(2026-09-26)

同步上游 **v1.8.2**(3 个提交,分叉点 `e0002073`),并按本仓库的多版本逻辑改写了两处
会在 1.5.x 上失效的写法。

### 上游同步

- **#899 按"原始 NPC + 房主选中的模板"重建等级化 NPC**:此前远端角色的 base 被**直接**
  换成房主选中的模板,丢掉了只存在于原始 NPC 上的数据(守卫的名字、默认装备等)。
  现在走引擎自己的 `CreateTemplateActorBase(原始, 模板)`,与单机解析结果一致;
  旧的临时 base 交给引擎的 `GarbageCollector` 回收(与 `RecalcLeveledActor` 同一套策略,
  但**不动**旧实现留下的静态 pick);
- **#893 排除各派系的监狱储物容器**:入狱时没收的赃物/随身物品容器按派系各存一份,
  同步它们等于让一个玩家的物品进另一个玩家的储物箱。现在遍历 `ModManager::factions`
  收集这些容器指针并从对象同步里排除(只比较指针,不读它们的内容);

### 1.5.x 兼容(本次同步的主要风险)

- **`GarbageCollector::Get()` 不再解引用零返回桩**:单例 id `400329` 在 1.5.97
  映射表里**不存在**,而 `POINTER_SKYRIMSE` 对未映射 id 返回的是"零返回桩"——
  照抄上游会变成**解引用地址 0**。已改为直查地址库,未映射返回 `nullptr`,
  调用点判空后保留旧 base(泄漏一个临时 form,不崩);
- **修掉一个上游引入的每帧 Disable/Enable 死循环**:上游用 `GetLeveledPick() == pPick`
  判断"重建完成",而这个值读的是 `SetLeveledCreature`(id `20231`,1.5.97 **未映射**)
  写进去的 extra data ⇒ 比较**永远为假**,`WaitingFor3D` 会每帧重新禁用再启用该角色。
  新增 `LeveledNpcSystem::CanRecordPick()`(只查一次并缓存),不可写时退回本仓库原有路径
  并把完成判定切回 `baseForm == pPick`;
- `TESObjectREFR::SetBaseForm` 更名为上游的 `SetObjectReference`(仅改名,布局不变);
- `TESFaction` 补 `CrimeData` / `ModManager::factions`:偏移不是猜的——
  form 数组自 `0x10` 起、每项 `0x18`、按 FormType 索引,Faction(11)→`0x118`
  与既有 Quest(77)→`0x748` 互相印证,三个 `static_assert` 同时钉住;

### 插件层专项审计

- **服务端可被任意玩家一条聊天命令打崩**:`CommandService::OnSetTimeCommand` 对
  `m_adminSessions` 里每个连接调 `PlayerManager::GetByConnectionId(session)->GetId()`,
  **不判空**。认证流程里连接 id 在 `GameServer.cpp:896` 先进集合,玩家行到 `:977`
  才建立,而 `:947`(mod 策略不符)、`:983`(重复认证)都会提前返回且**不回头清理集合**
  ——集合只在断线时(`:612`)擦除。该集合的其余所有读者都判了空,其中
  `GameServer.cpp:496-502` 还专门为此打印 `"Admin session not found"`。已改为取到即判、
  失败即 `continue`;
- **修正一处会误导部署的文档**:`docs/COMPANION-PLUGINS.md` 声称"出货的 ini 指向框架运行时,
  所以按名字加载门面的插件仍然走原生传输"——与 `976f9702` 之后的实际出货值**完全相反**
  (ini 里是 `STRBridgeModule=STRPluginMessagingBridge.dll`)。已改写为事实,并写明
  七个随包插件**无一例外**都按名字找门面 DLL、因而**都落在聊天隧道桥接上**;
- `check_transport_compat.py` 增加两条断言,把该文档与**实际出货的 ini 取值**绑死
  (文档必须出现 ini 里的真实值;不得再声称指向框架运行时)。已做反向验证:注入这两种
  回归后门禁均 `exit 1` 并指名道姓;
- **已查明但未改动**(插件仓库是钉死的子模块,本仓库不推送,改了进不了 CI 与发布):
  `STRBridgeModule` 只有**一个**键,而运行时按游戏版本有两个名字
  (`SkyrimTogetherRuntime.dll` / `SkyrimTogetherRuntime_1_5.dll`),门面的加载器又是
  精确模块名匹配 —— 单份 ini 表达不了两者。两条候选修法都**需要实机验证**,
  本轮不翻转行为,已连同证据与边界写入 `docs/PITFALLS.md` §32;
- **新增本仓库自持的插件补丁机制**(见下),并用它修掉了
  `DAVSyncTogether` 的一处崩溃:`DAVConfigIndex::Load()` 里
  `recursive_directory_iterator` 的构造落在循环内 `try` **之外**,而 `Load()` 由
  `OnSKSEMessage(kDataLoaded)` 直接调用、`main.cpp` 又没有 handler ⇒
  `filesystem_error` 逃出 SKSE 消息回调即 `std::terminate`。触发条件很平常:
  `Data/SKSE/Plugins/DynamicArmorVariants` **存在但不是目录**(残留的同名文件)。
  已改用 `error_code` 重载,枚举失败只记一行日志;
- **删掉 `IEDSyncTogether` 的 `RelayHost` 子选项**:它唯一的作用是打开旧 UDP 传输,
  而该传输**根本没被编译**(CMake SOURCES 里没有 `UdpTransport.cpp`/`StrTransport.cpp`/
  `SyncService.cpp`/`Config.cpp`),勾了也不会有任何效果——与其留一个装样子,
  不如删掉。同时把"随包的 `IEDSyncTogether.ini` 目前是惰性的"写进清单
  `payloadNote`,免得有人报"我的配置被忽略"。

### 新增:本仓库自持的插件补丁机制

七个插件是**钉死的子模块**,指向本仓库**没有权限推送**的仓库(已实测:
`git push --dry-run` 对 `Caelvanost/DAVSyncTogether` 返回 `403`;两个上游的 HEAD
又恰好等于当前 pin,所以也没有"等上游修"这条路)。而 CI 的
`git submodule update --init --force --recursive --depth=1` 会**丢弃工作树里的一切改动**
—— 于是"在子模块里改一行"这种修法**永远进不了包**。

新增 `Code/plugins/patches/<plugin>/*.patch` + `Code/plugins/tools/plugin_patches.py`
把这条件拆掉:补丁是本仓库的**普通内容**,照常 review、照常随包发布,在插件被
configure/build **之前**应用到子模块工作树上。

| 命令 | 用途 | 在哪跑 |
| --- | --- | --- |
| `apply` | 应用全部补丁(**幂等**:已应用则跳过) | `windows.yml`,紧跟 checkout、在 build 之前 |
| `verify` | 每个补丁都必须仍能打到 pin 上 | `plugins.yml`,与其它门禁并列 |
| `check` | 当前工作树里补丁是否已应用 | 本地 |
| `revert` | 反向应用,回到 pin | 本地 |

`verify` 用 `git apply --check --cached`(对**索引**而不是工作树),所以"已经应用了"
**掩盖不了**"这个补丁已经配不上新的 pin"——这正是重新 pin 时最需要拦住的失误。

同时改了 `plugin_snapshot.py`:快照门禁原来比对**工作树**,补丁一应用它就误报
"snapshot differs"。现在它通过 `git show <pin>:<path>` 读**pin 的那个提交**的内容,
量的仍然是"备份是否还等于被构建的那个提交",而不是"工作树是否干净"。
两种状态(已打/未打)都实测通过,篡改快照仍然会被抓住。

### 清单

- `Tools/missing_1_5_97_ids.txt`:新增三个未映射 id(`20231`、`36460`、单例 `400329`),
  覆盖率 **3069/3080(99.6%)**。顺带修掉生成器 `collect_codebase_ids()` 的两个口径错误
  (`*.[ch]pp` 通配漏掉全部 301 个 `.h`;注释掉的调用被当成活的),旧口径 3075 因此
  既漏 8 个又虚增 3 个;
- `docs/PITFALLS.md` §3.1:记录本次同步的两处判断与查证手段。

## v1.1.3(2026-09-26)

**插件生态落地**:把剩余三个插件按同一配方并入,可选插件由 3 个增至 6 个。

> 本节是**补写**的。`v1.1.3` 当时打了 tag,却没有在 CHANGELOG 里留下任何小节——
> 该版本的内容只存在于两个 README 的版本表里。`Tools/Scripts/check_docs.py`
> 的 `tags-have-sections` 检查把这类"打了 tag 却无人可读"的发布挡住了,
> 本节即由该检查发现后补齐。

- 并入 `AnimSyncTogether`(动画图谱变量/事件同步)、`DAVSyncTogether`
  (Dynamic Armor Variants 外观同步)、`TradeTogether`(玩家间物品与金币交易),
  子模块、契约校验、打包清单、CI 构建、耐久快照、安装向导六处同步更新;
- 删除 FOMOD 里那段**两条指引都是死路**的 OStim 提示(上游既无 release 也无 tag,
  该脚本又从不进包),中英文改为只描述功能;
- 修复**自己引入的发布阻断**:两个插件 pin 的 vcpkg baseline 在 CI 的
  `--depth 1` 克隆里取不到,而 vcpkg 的 builtin registry 路径**没有 fetch 回退**,
  已改为按 manifest 反推 baseline 再逐个 fetch;
- **OStim 的两个 Papyrus 脚本终于能编了**:改用开源编译器
  `russo-2025/papyrus-compiler`(pin tag + SHA-256),补 12 个基础类型 stub
  (`plugins/` 是 gitlink,只能放本仓库),`OSKSE.pex` / `OStimTogetherNative.pex`
  随包出货,**Add Actor 同意门控真正可用**;
- 顺带修掉 `OStimTogether_OCum.esp` **出货却不带脚本**的缺口(补 4 个 `Form` +
  1 个 `Game` stub,`OStimTogetherOCum.pex` 接入同一编译通道),并让打包脚本支持
  **子选项的 artifacts**(原先静默忽略);
- `GameFiles/Skyrim/scripts/source/` 的 10 个出货脚本补上可复现的编译入口,
  其中 `SkyrimTogetherVerifyLaunchScript.psc` 修掉编译器不支持的 `\n` 转义
  (它会**静默产出字面反斜杠**);归档 papyrus-compiler 源码到 `snapshots/tools/` 并加门禁。

## v1.1.2(2026-09-26)

第四轮审计:全仓复查 + 插件层专项,以及**一次真实缺陷的回滚**。

### 稳定性修复(全部为可复现的空指针)

- **服务端可被远程打崩**:同一连接上发第二次 `AuthenticationRequest` 时
  `PlayerManager::Create` 返回 `nullptr`,而调用点直接解引用。已改为拒绝重复认证;
- **`PlayerService` 三个每帧路径**在载入/主菜单(此时没有玩家)解引用空玩家指针:
  `RunRespawnUpdates`、`RunBeastFormDetection`、`RunDifficultyUpdates`;同文件另两处
  本来就判空,这三处漏了;
- **`DiscoveryService::DetectGridCellChange`**:`GetParentCellEx()` 与坐标回退
  **都**可能为空,而下一行直接读 `pCell->formID`。同文件另外两处对同一个调用都判了空;
- **`CalculateHealthPercentage`**:每帧传入 `PlayerCharacter::Get()`,函数内部不判空;
- **`WeatherService`** 三处 `Sky::Get()->` 未判空(其中一处由服务端消息驱动),
  同文件另外两处判了空;
- **`OverlayService`/`PartyService`/`InputService` 五处 `GetOverlayApp()->`**:
  该指针在 `Create()` 之前为空,而组队事件正是连接后立刻会到的;
- **`baseForm` 空指针**:仓库自己的调试视图里就写着 `if (!pRefr->baseForm)`,
  但 12 处日志/分支直接解引用它(含每帧的裸体检查与网络驱动的物体/魔法/角色路径);
- **`EntitiesView` 一个死守卫**:先写 `"UNNAMED"` 又在下一行无条件 `sprintf_s` 覆盖它,
  等于没判。

### 插件层(专项)

- **ProxyResolver 的映射监听器从来没被调用过**:`OStimTogether` 与 `IEDSyncTogether`
  都注册了 `registerListener`,而框架只把回调存起来、从不触发;OStim 的反向映射
  (`_connectionByProxy`)因此永远是空的,而它的注释写着这条映射是 "Required"。
  现在由 `on_construct/on_destroy<PlayerComponent>` 触发 `kAdded`/`kRemoved`
  (`FormIdComponent` 每实体只写一次、拆除时移除,所以这两类事件就是全部迁移);
- **`setLogCallback` 存了不用**:插件装上的回调永远不会被调用。现在框架自己的
  插件层诊断(丢弃超长 channel / 超限载荷)会通过它发出,且**在锁外**调用;
- **`setLocalDisplayName` 存了不用**:如实注明这是**有意不上线**的——
  发送者显示名只能来自服务端认证过的登录名,否则任何插件都能冒充别人;
- **服务端限流桶泄漏**:`m_buckets` 以单调递增、永不复用的 `PlayerId` 为键且从不清理,
  长开的服务器会一直涨。现在随玩家行一起移除。

### 快捷键:只保留 F2

按需求把**除 F2 以外的所有游戏内快捷键**注释或禁用:

| 键 | 原用途 | 处理 |
|---|---|---|
| **F2** | 联机菜单 | **保留**(唯一) |
| 右 Ctrl | F2 的别名 | **移除**(第二个绑定=第二个要维护的东西) |
| F3 | 调试菜单栏 | `#if 0`;`toggleDebugUI` 绑定仍在,变成纯 opt-in |
| F4 | 揭示其他玩家 | 移除按键;`reveal players` 按钮照常工作 |
| F6 | Discord 覆盖层解锁 | 置于 `if (false && …)`,并在注释里写明重新启用覆盖层时不得复活 |
| F7/F8 | 开发快捷方式 | 保持 `#if 0` |

### 版本支持(1.5.x / 1.6.x / 1.7.x)

- 复核 `VersionDb` 三条加载路径:format 1(1.5.x,SE id 经 ae-to-se 映射表翻译)、
  format 2(1.6.x AE)、format 5(1.7.99+ 密集偏移数组);
- **34 个地址库文件还原**(见上),1.5.x 的 10 个 `.bin` + 10 张映射表、
  1.6.x/1.7.x 的 13 个 `versionlib` 全部在位;
- `GamePatch::At` 的按版本偏移与 `legacyMeasuredOn` 前缀校验保持原样(1.5.97 之外的
  1.5.x 不会被套用 1.5.97 的偏移)。

### 上一轮遗留

- **修复地址库文件误删**:上一个提交在搬移 `STRPluginMessagingAPI.ini` 时,
  连带删掉了 `GameFiles/Skyrim/SKSE/Plugins/` 下**全部 34 个**地址库文件
  (`version-*.bin` 10 个、`versionlib-*.bin` 13 个、`versionlib-ae-to-se-*.map` 11 个)。
  打包路径直接吃 `GameFiles/Skyrim/`,少了它们玩家一启动就会"地址库失败"并退出。
  **34 个文件已按删除前的 blob 哈希逐一还原**;
- **`#if 0` 逐块判读**:22 处 → **删 15 留 7**。留下的 7 处都是"改 0 为 1"型开关
  (F6/F7/F8、旧战斗瞄准 ×3、地图菜单、imgui 上游 ×2),判据是**块外有没有为它留位置**;
- **debug 天气开关从未生效**:`Sky` 三个 hook 里的 `s_shouldUpdateWeather` 判据
  全在 `#if 0` 里,开关写了个没人读的变量。判据已启用(该变量只由 debug 窗口写,
  而该窗口在 release 版被裁掉,所以对发布版无影响);
- **EF 哨兵结论写进代码**:查清 SKSE 插件路径**本来就不需要** `_initterm_e` 哨兵
  (SKSE 已先加载 EF,我们的 hook 装在其 thunk 之上),只补注释、不改逻辑;
- **发布冻结解除**:`RELEASE-FREEZE.md` 删除,README 中英横幅同步移除;
  `release.yml` 的 guard 保留(冻结文件仍是唯一开关)。

- **修复地址库文件误删**:上一个提交在搬移 `STRPluginMessagingAPI.ini` 时,
  连带删掉了 `GameFiles/Skyrim/SKSE/Plugins/` 下**全部 34 个**地址库文件
  (`version-*.bin` 10 个、`versionlib-*.bin` 13 个、`versionlib-ae-to-se-*.map` 11 个)。
  打包路径直接吃 `GameFiles/Skyrim/`,少了它们玩家一启动就会"地址库失败"并退出。
  **34 个文件已按删除前的 blob 哈希逐一还原**;
- **`#if 0` 逐块判读**:22 处 → **删 15 留 7**。留下的 7 处都是"改 0 为 1"型开关
  (F6/F7/F8、旧战斗瞄准 ×3、地图菜单、imgui 上游 ×2),判据是**块外有没有为它留位置**;
- **debug 天气开关从未生效**:`Sky` 三个 hook 里的 `s_shouldUpdateWeather` 判据
  全在 `#if 0` 里,开关写了个没人读的变量。判据已启用(该变量只由 debug 窗口写,
  而该窗口在 release 版被裁掉,所以对发布版无影响);
- **EF 哨兵结论写进代码**:查清 SKSE 插件路径**本来就不需要** `_initterm_e` 哨兵
  (SKSE 已先加载 EF,我们的 hook 装在其 thunk 之上),只补注释、不改逻辑;
- **发布冻结解除**:`RELEASE-FREEZE.md` 删除,README 中英横幅同步移除;
  `release.yml` 的 guard 保留(冻结文件仍是唯一开关)。**本次不打 tag。**

## v1.1.1(2026-09-25)

第三轮审计。本轮换了两条新判据(容器迭代中改动、链式解引用),并复核了
前两轮修改本身。

- **链式解引用**:`VisitInteriorCell` 里的
  `PlayerCharacter::Get()->GetParentCellEx()->formID` 在 **load 全程**会取到空
  cell(`VisitCell()` 只在入口判过玩家存在,进内层就不再判);`VisitExteriorCell`
  的坐标回退也可能为空。两处都已补；
- **`Actor::Create`** 取玩家后**连续解引用三次**,已改为取到即判、失败即返回；
- **`DebugService`** 对 `Actor::Create` 的返回值从未判空就连续解引用,
  且 `PlayerCharacter::Get()` 与 `baseForm` 同样未判——一并补上；
- **修正上一轮自己引入的缺陷**:`Actor::Create` 的玩家判空曾被插在
  `New()` **之后**,提前返回会泄漏刚分配的 actor。已改为**先判空、再分配**；
- 容器迭代类判据(10 处)逐个核对后确认**全部安全**:代码库统一采用
  "先收集、后改动"或"循环外 clear"。

## v1.1.0(2026-09-25)

性能与同步的算法级改进,以及两轮全仓库审计。

- **插值改 Catmull-Rom**:远端玩家移动由线性改为三次曲线,线段交界处方向连续;
  丢包时不再"冻结—跳变",改为沿最后速度做**有界外推**(上限 1.25),并对
  播放头落后窗口的情形补了下限 0,避免反向外推;
- **帧循环 16 ms → 8 ms**:`SetTimer` 按 15.625 ms 系统 tick 向上取整,
  16 ms 实为 31.25 ms(约 32 次/秒),8 ms 落在单个 tick 内(约 64 次/秒),
  远端玩家采样率翻倍;
- **移动更新复杂度**:`OnReferencesMoveRequest` 由 O(更新数 × 实体数)
  降为 O(实体数 + 更新数)(先建 serverId 索引再查表);
- **修复重复生成同一远程玩家**导致引擎在 `SkyrimSE.exe+0x23d000` 解引用空指针
  的崩溃;重复守卫补 `LocalComponent` 检查;
- **全仓库 `GetById` 解引用审计**(两轮,判据不同):修复 15 处无守卫解引用,
  含 `Actor::Create` 对玩家的连续三次解引用、`PlayerService`/`PartyService`
  对硬编码全局的写入(断线路径也写,退出时崩)、`VisitInteriorCell`
  在 loading 期间对空 cell 的链式解引用等;
- 热路径日志降级:启动首秒曾写 220 行,现降至 debug。

## 历史沿革

本项目 fork 自 [TiltedEvolution](https://github.com/tiltedphoques/TiltedEvolution),
其 GitHub fork 链为 `tiltedphoques/TiltedEvolution` → `rfortier/TiltedEvolution-rwf`
→ 本仓库。

本文件此前保存的是**上游 2022 年 7 月之前**由工具自动生成的 changelog
(最后一条为 v1.38.3,2022-07-01),与本仓库使用的版本号体系(`1.0.x`)无关,
留在根目录容易让打包/报错的人对不上号,因此移除;需要那段历史可查上游仓库,
或本仓库的 git 历史(该文件在移除此内容前已被完整提交过)。
