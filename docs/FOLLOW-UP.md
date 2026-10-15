# 后续计划 / 未决验证项

这份文件记录**已知但尚未关闭**的验证缺口。它和 `CHANGELOG.md`、`docs/PITFALLS.md`
的分工是:

| 文件 | 回答的问题 | 时态 |
| --- | --- | --- |
| `CHANGELOG.md` | 这个版本修了什么 | 过去 |
| `docs/PITFALLS.md` | 当时怎么定位的、证据在哪 | 过去 |
| `docs/FOLLOW-UP.md`(本文件) | **还有什么没验、下一步怎么做** | 未来 |

**当前开放的是第 1、2、5、6、7 条**(第 3、4 条是已完成的留档)。

一条缺口在这里的存续条件是:**它是具体的、可执行的、且当前状态无法由本机
证据关闭**。含糊的「以后再优化」不写进来;能当场验完的也不写进来,直接验完。

---

## 1. 待验:`bUseFaceGenPreprocessedHeads` 的两个结构体偏移在 AE 运行时上未验证

**状态**:开放,但**判据已就位**。影响 1.6.317 ~ 1.7.104 全部 AE 运行时。
**引入**:`ca24d5ce`(v1.1.6),`f0877c1b` 之后的后续修复同样依赖它。

> **2026-10-12 更新**:三个偏移**仍然未验证** —— 本机只有 1.5.97,验不了。
> 变的是「**谁能回答它**」:代码里补了三条 `static_assert`(把「这是假设」从三个裸魔数
> 变成下一个读代码的人能看见的东西),`SetRemoteFaceGenForce` 的首次写入提升为
> `info`、查找失败路径补了 `warn`,`Tools/Scripts/check_facegen_scope.py` 第 7 条
> 判据锁住这两件事。**所以现在一名 1.6.x/1.7.x 玩家的一行日志就能定论**
> (判读方法见 §1.4(a))。**不要把这个缺口标成「已关闭」。**

### 1.1 缺口是什么

客户端为了「只对『外观来自网络』的角色清掉烘焙头开关」,自己走了一遍引擎的
INI 设置链表:

```cpp
// Code/client/Games/TES.h
struct Setting
{
    void* unk0;
    uint64_t data;        // <-- 引擎读这个字节决定烘焙头 / 运行时生成
    const char* name;
};

struct INISettingCollection
{
    uint8_t unk0[0x118];  // <-- 链表头在这里
    Entry head;
};
```

这三个数字(`data` 在 `+0x8`、`name` 在 `+0x10`、链表头在 `+0x118`)**只在
1.5.97 的 `SkyrimSE.exe` 上验证过**。当时的读数:

| 查证 | 地址 | 读数 |
| --- | --- | --- |
| 引擎自己的 `GetSetting` | `0x1403104d0` | `lea 0x118(%rcx),%rax` → 链表头 `+0x118`;`mov 0x10(%rdi),%rcx` → `name` 在 `+0x10` |
| 引擎读那个开关 | `0x140363e38` | `movzbl 0x141dea030` → `data` 在 `+0x8` |
| 引擎自己拨这个开关 | `0x14031c153` | `movzbl 0x8(%rax),%esi` 存 / `movb $0x0,0x8(%rax)` 写 |

**问题**:AE(1.6.x/1.7.x)上这三个偏移是否相同,**没有任何本机证据**。

### 1.2 为什么现在不能就地关掉

- 本机只有 `SkyrimSE.exe` **1.5.97.0**(`D:\game\SkyrimSE\`),没有 AE 可执行文件,
  无法反汇编对照。
- 仓库里**有** 1.6.1170 的逆向产物(`Tools/ida/st_target_meta_1170.json`,91 条
  带反编译的函数;`apply_st_map_idc.idc`,3605 条标号),但**采集口径是「代码里
  真正调用的目标」**,`INISettingCollection` / `GameSettingCollection` /
  `GetSetting` **一条都不在其中** —— 已实测确认(`docs/PITFALLS.md` §43.8 记录了
  同一口径导致的另一处缺口)。所以这批产物**帮不上这个缺口**。
- 全仓**没有** `static_assert` 覆盖这两个结构体的任何偏移:

  ```powershell
  git grep -n 'static_assert.*Setting' -- Code/client
  # 只有 EffectSetting.h:35,与本缺口无关
  ```

  也就是说这三个数字目前**只以魔数形式存在于代码里**,没有任何一处声明过它们
  是「按 1.5.97 量的」。

### 1.3 风险有多大

- **写入失败**不会崩溃:`INISettingCollection::Get()` 经 `POINTER_SKYRIMSE(411155)`
  解析,该 id 在**全部**随包运行时上都有映射(实测 1.6.317/640/1170/1.7.99/1.7.104
  全部 `Y`),`GetSetting` 的返回值也在调用点判了空。
- **真正的风险是「静默失效」**:如果 AE 的 `+0x118` 或 `+0x8` 不同,`GetSetting`
  会返回空(→ 开关不拨,头继续隐形,**表现为 bug 没修好**)或返回错的对象
  (→ 写坏别的 INI 设置)。两者都不会崩溃,所以**只会以「功能没生效」的形式出现**,
  这正是最难从用户日志里看出来的一类。
- 也就是说:**这个缺口的存在,会让「AE 上没修好」和「AE 上偏移不对」两种解释
  无法区分**。

### 1.4 关闭它的两条路,按成本排序

**(a) 最便宜 —— 让用户日志自己回答(建议先做这个)**

`SetRemoteFaceGenForce` 里已经有一行能直接判定:

```cpp
// Code/client/Services/Generic/CharacterService.cpp:2296 —— 首次写入(info)
spdlog::info("{}: bUseFaceGenPreprocessedHeads at {} = {} (was {})", __FUNCTION__, fmt::ptr(pSetting), pSetting->data, m_faceGenPreprocessedBackup);
// 同文件 :2299 —— 之后每次(debug)
spdlog::debug("{}: bUseFaceGenPreprocessedHeads = {} (was {})", __FUNCTION__, pSetting->data, m_faceGenPreprocessedBackup);
// 同文件 :2275 —— 查找失败(新增)
spdlog::warn("{}: bUseFaceGenPreprocessedHeads is not in the engine's ini collection, so the ...");
```

`Code/client/TiltedOnlineApp.cpp:71` 的 `set_level(spdlog::level::debug)`
是**注释掉的**(该行原文就是 `// rotatingLogger->set_level(spdlog::level::debug);`),
所以 `debug` 那行从来不打印 —— 这就是把首次写入提到 `info` 的全部理由。
**已做**(原先计划的两步):

1. ✅ **首次**写入提升为 `info`(只打一次,用函数内 `static bool s_reportedFirstForce` 守住 ——
   `CharacterService.h` 不该为一个日志改动新增成员,且该函数是 `const noexcept`),
   带解析到的 `Setting*` 与读到的原值,之后仍走 `debug`;
2. ⬜ 让一名 1.6.x/1.7.x 玩家提供一行日志 —— **这一条只能由用户完成。**

判读方法:

- `Setting*` 非空且 `was 0` 或 `was 1` → **偏移正确**,缺口关闭;
- 出现 `... is not in the engine's ini collection ...` 这条 `warn` → **偏移不对**,按 (b) 处理。
  (空指针是引擎对「这个运行时没定义这个设置」和「我们的偏移不对」给出的**同一个答案**,
  补 `warn` 就是为了不再把后者看成前者。)

这一步不需要任何反汇编,只需要一个 AE 用户的日志。**代价最低,信息量最大。**

**(b) 彻底的 —— 反汇编 AE 可执行文件**

把任一 1.6.640 / 1.6.1170 `SkyrimSE.exe` 放进工作区,重跑 1.5.97 上那三条读数
(引擎 `GetSetting` 的 `lea 0x118`、`name` 的 `+0x10`、读 `data` 的 `+0x8`)。
工具链已经就绪:`Tools/ida/patch_offsets_1597.py` 就是为「同一函数在两个
build 间逐指令对齐」写的,`Tools/ida/match_capstone.py` 消费的正是
`--ae-exe` / `--se-exe` 这一对参数。

### 1.5 顺带该做的一件小事

给这两个结构体补 `static_assert`,把「这三个数字是按 1.5.97 量的」写进代码本身:

```cpp
static_assert(offsetof(Setting, data) == 0x8);
static_assert(offsetof(Setting, name) == 0x10);
static_assert(offsetof(INISettingCollection, head) == 0x118);
```

这不会**关闭**缺口(断言只在编译期检查本仓库自己的布局,不会去问引擎),
但它把「这是假设,不是事实」变成**可被下一个读代码的人看见的东西** ——
现在这三个数字散在 `TES.h` 的结构体定义里,没有任何标记说明它们只验过一个版本。

**已做**(2026-10-12):三条断言落在 `Code/client/Games/TES.h:176/177/178`,注释明说
「断言不关闭缺口、只在编译期检查本仓库自己的布局、错偏移是静默失效而不是崩溃」。
`Tools/Scripts/check_facegen_scope.py` 第 7 条判据保证它们不会被悄悄删掉
(删任一条 → `FACEGEN SCOPE CHECK FAILED (1)`;`check_layout_asserts.py` 断言总数 192 → 195)。

> **本文件不受 `Tools/Scripts/check_docs.py` 管辖**:那个门禁只读
> `COVERAGE_DOCS` 里列出的四个文件(`README.md` / `README_EN.md` /
> `Tools/ida/README.md` / `docs/LAN-RADMIN-GUIDE.md`),不会扫到本文件。
> 所以这里引用的覆盖率之类的数字**不会被自动校验** —— 写的时候要自己
> 回源头核一遍,别把这里的数字当成可信来源。

---

## 2. 待验:1.6.x/1.7.x 上「脸看不见」的实机复现

**状态**:开放(与第 1 条同源,但独立)。

`docs/PITFALLS.md` §38 的结论是「这个 bug 跟随上游 #764 这个**提交**,而不是跟随
游戏版本」。这个结论由三条证据支撑:

1. `git merge-base --is-ancestor 7e09f8b4` 对 v1.6.1~v1.7.1 全为**假**、对
   v1.8.0+ 全为**真**;
2. 上游 v1.7.1 的 `VersionDb.h` 只有 `versionlib-%d-%d-%d-%d.bin`,**没有**
   `version-%d` 回退、也没有 ae-to-se 映射表 ⇒ 它**根本不能在 1.5.97 上启动**;
   所以报告者说「1.7.1.42 好」时,**必然是 AE 运行时**;
3. 那个每帧清零写的 id `378620` 在 1.5.97 上**没有映射**(退化成零返回桩),
   而在 1.6.640/1170/1.7.99/1.7.104 上**都有真实可写地址**。

第 3 条是关键:它说明**在 AE 上,历史上那个每帧清零是真的生效的**,所以
「1.7.1 好」在 AE 上是**机制成立**的,不是巧合。反过来说,当前版本丢掉它之后,
**AE 上应当复现,且比 1.5.97 上更干净**(1.5.97 上那个清零本来就是个空操作)。

**仍未做的**:在真实 1.6.x/1.7.x 客户端上复现一次。上面三条都是静态读数,
没有一条是「在 AE 上亲眼看到头隐形」。计划:CI 出 1.6.1170 包之后,
用自定义种族角色进服复现一次,并对比 `check_facegen_scope.py` 覆盖的两个写入点
是否都触发。

> **2026-10-12 更新**:这次复现要看的日志**已经不用再临时加了** —— §1 那次改动
> 把 `SetRemoteFaceGenForce` 的首次写入提到了 `info`,并给查找失败补了 `warn`。
> 所以一次 AE 场次现在能同时回答 §1 和本条:有 `info` 且 `Setting*` 非空 → 偏移正确;
> 有 `warn` → 偏移不对。**在这两行出现之前,本条无法关闭。**

---

## 3. 已完成:上游 v1.8.3 的同步(2026-10-11)

上游 v1.8.3 = `v1.8.2..4917189f`,共 **12 个提交**,已按本仓库的写法逐个判定并落地
(见 `CHANGELOG.md` 的 v1.1.9 一节与 `docs/PITFALLS.md` §110)。本仓库**不能**直接
merge,每个提交都要按「同时支持 1.5.x 与 AE」的前提重新判定。

**仍开放的是它的实机验证**,不是同步本身:

- **§110.2 的 NPC 复制修复**(上游 #906):判据是「WE23 的『拾荒者』不再每轮
  往返复制一份」,只能实机观察。
- **§110.6 的 `--generate-config` 入口重构**:它改了跨 DLL 的导出签名
  (`CreateGameServer` 去掉回调参数、新增 `RegisterServerSettings`),
  所以 runner 与 server **两个二进制必须同一次构建产出**。版本握手
  (`CheckBuildTag`)会在启动时拦下混用,但这条只有 CI 出包后才能真正验证。

本机没有 `xmake`/`cl`/`vcpkg` 工具链,以上都只能做到静态判据。

---

## 4. 已完成:全项目审计(2026-10-11 ~ 2026-10-13,§111–§158)

**每轮换一条可判定判据,不靠读代码找味道。** §111–§114 是审计的开始;§115 起每轮
把判据本身也做成门禁,于是这一节同时是**缺陷台账**与**门禁台账**(§149 那一行里的
`check_gate_integrity.py` 就是用来守住这张表本身的:门禁会跑吗、会失败吗、看全了吗)。

四轮审计各自换了一条**可判定**的判据,不靠读代码找味道:

| 轮次 | 判据 | 找到的缺陷 |
| --- | --- | --- |
| §111 | 「重写了一遍引擎某个函数」的地方,**引擎那份就是契约** | `GetSetting` 抄漏引擎的空表保护;15 处单例解引用 |
| §112 | **宏的名字不是它的语义**,按展开结果判 | 发布版不写崩溃转储(文档却让玩家交);清理函数会删无关文件 |
| §113 | 「谁在用地址库」与「门禁扫了谁」**分别数一遍再对照** | 插件版本门禁只扫 1 个插件,另一个的 8 个站点从未被查 |
| §114 | **同一成员的读侧与写侧必须同意「空」是什么意思** | `ExtraDataList` 读侧判空、写侧不判;`New()` 不判分配;`Add()` 存空节点 |
| §115 | **同一个东西定义在两个地方**,把两处抽出来对照 | 110 条消息的写/读契约此前无门禁(审计后**全部一致**,加的是守护) |
| §116 | **门禁的「扫哪里」要按规则推出,不能按发现地点写** | §115 的门禁自己只扫了 `Messages`,漏掉 `Structs` 的 23 对(总量的三分之一) |
| §117 | **签入的证据文件若不进门禁,地位等同注释** | 1.5.x 补丁偏移的实测 tsv 无门禁读;13 处调用点逐条核对后**一致**(加的是守护) |
| §118 | 同上判据问遍 `Tools/ida/` | **活缺陷**:覆盖表里 id 35086 两行两值,读取方 `table[id]=value` **静默取最后一个**;旧值落在 1.5.97 函数体内 9 字节,而 `Actor.cpp` 用该 id 解析**活的**函数指针 |
| §119 | 同上判据用在**刚写的门禁**上 | 门禁只读了 1 个文件,而出货 map 由 **3 个** `--overrides` 文件生成;**同一错误第三次**(§113/§116/§119) |
| §120 | 同上判据用在**其余门禁**上,并顺线索查「谁复制了契约」 | 契约头被复制进 **6 处**,ABI(成员顺序)无任何门禁比较;**6 份 ABI 全部一致**(加的是守护);另证明 `check_skee_abi.py`/`check_exports.py` 范围**正确** |
| §121 | 同上判据继续问 `Tools/ida/` | 91 条引擎地址证据无门禁读;三条主张(地址/绑定/跨 14 个运行时的解析)**91/91 全过**(加的是守护);教训:**行号会腐烂 46/91,三元组才是主张** |
| §122 | 同上判据继续问 `Tools/ida/` | 匹配报告与出货 map **不一致 2 条**;真机反汇编判定**map 两次都对**;发现可固化规律(**≥0.90 档 31/31 一致**,不一致全在最低两档);门禁把「已否决」与**该规律本身**都固定下来 |
| §123 | 转到**加载路径**(哪个运行时/按哪个版本被加载) | **改代码**:地址库回退「取第一个」,而 1.5.x 之间 **98.7% 的 id 指向不同地址**;改为按族过滤 + 取族内最高;**我第一版「全局最高」在手头机器上恰好正确、在 1.6.1170 上会选到 1.7 库,提交前纠正**;另查出 `versionlib-1-6-1170-0-1.bin` 无任何路径可打开(有意加入故不删,登记为不可达) |
| §124 | 同一条路径上继续问「同一件事写了几遍」 | **改代码**:版本查询写了三遍,启动器那份的 `ProductVersion` 分支**少测「串非空」**(同一文件里另一分支却有),空值被当成版本号 → **存在的回退永不执行** → 启动失败且**信息指错原因**;另核对三项形似缺陷实则不是(A/W 混用合法、`void**` 与 `LPVOID*` 同类型、`FileVersion.inl` 只有一个) |
| §125 | 把加载路径**走完** | **无修复**:三处形似缺陷逐条查清后都不是(手动映射下的 `GetModuleHandleA(NULL)` 正确,因为启动器**就地覆盖自己的镜像头**;format 5 固定 64 字节模块名正确,用 `96+4*count==filesize` 恒等式验证;`FileVersion.inl` 只有一个);另两条门禁范围正确,其中 `check_skse_bootstrap.py` 正是「AE 上失效、1.5.97 上看不见」的兼容性形状 |
| §126 | 查 `Code/plugins/STRPM/` 时发现 `contract.json` | 契约门禁**手写清单漏 2 个消费者**(`MorphSyncTogether`/`OStimTogether`),而**规则早写在 `contract.json` 里**;**同一错误第四次**;另一处:成员正则只认 `STRPM_CALL`,而 `OStimTogether` 写 `__cdecl` → 成员**数成 0** → **假通过**;现从契约读清单、两种拼写都认、新增 `headerSha256` 校验;覆盖 6/20 → **8/25** |
| §127 | 把 §126 的问法系统化:列出所有 manifest 逐个问「谁读它」 | **活缺陷**:`fomod/info.xml` 的版本号**落后四个版本**(1.1.5 vs v1.1.9),而**它自己的注释就写着「要与最新 tag 保持一致」** —— **约定写下来了但没被执行**;新增 `check_package_metadata.py`(版本号取**本仓库自己那条 tag 序列**的最新值,**不能取全局最新** —— tag 命名空间与上游共享;描述必须列出运行时接受的版本且不得声称拒绝的版本);另核对 `plugins.json` 交叉引用全解析、`merge_fomod.py check` 通过 |
| §128 | 先走 §127 建议的注释线(276 候选逐条读过,无产出),改查**服务端主循环** | **改代码**:**每帧 delta 没有上界** —— 测的是真实时间,而挂起/调试器/hitch 只会让它变大;服务端循环 `while` 里**无 sleep**;消费方含 `m_timer += Delta` 与**原样交给 Lua**;**先问仓库已有判据**(delta 的 clamp **0 处**,而线上浮点有整套约定)→ delta 是唯一漏掉且**更不可控**的;在 `WireLimits.h` 加 `kMaxFrameDeltaSeconds=1.0` + `ClampFrameDelta()`,两处测量点应用;新增单元测试 **22 → 23 个用例** |
| §129 | 按 §128 的判据找**累加器**(全仓仅 6 处) | **改代码**:**经验值只挡了非有限、没挡量级** —— 本地测量无界 → 累加无界 → 服务端**原样转发** → **接收方直接喂进引擎**;`ClampFinite` 让 `1e30` 原样通过,而引擎拿它与**每级阈值**比较,没法比较的值**后续任何奖励都修不回来**;出错来源**对端拦不住**(溢出 / 读档切换),故界加在**发送侧**;加 `kMaxExperienceDelta=100000.f` + `ClampExperienceDelta()`,**四处**应用;新增测试 **23 → 24 个用例**(含**线上往返**) |
| §130 | **「同一个东西出货了几份,每一份是不是都被声明过」** | **改代码 + 改数据**:十张 `versionlib-ae-to-se-*.map` 只有 1.5.97 那张被记账,而 id `16113`(`ExtraContainerChanges::Data::GetArmor`)**在 1.5.3 / 1.5.16 / 1.5.23 三张上根本不存在** ⇒ `GetArmor` 恒为空 ⇒ `Actor::IsWearingBodyPiece()` 恒假 ⇒ `InventoryService::RunNakedNPCBugChecks` 给**已经穿着护甲**的 actor 每趟重穿一次;RVA 无法从本仓库恢复(证据全是为 1.5.97 采集的,而**猜一个比缺一个更危险**),故缺口改为**声明**在 `Tools/missing_ids_by_runtime.tsv`,后果就地修(`CanQueryArmor()` 要求确定答案),新增门禁 `check_map_coverage.py`(四条判据,含**守卫的 id 必须等于同文件 `POINTER_SKYRIMSE` 解析的 id**),五向变异测试逐条被指名 |
| §131 | **「同一份源码、两个目标」时,清单上的守护到底在不在跑** | **改代码 + 新增门禁**:客户端同一批源码编两次(`SKYRIM_TARGET_LEGACY=1`),而 §2 的防线「两分支各一条 `static_assert`」只是 §0 的**勾选项**;实测 16 个条件块/7 处改变布局,其中 **`ExtraDataList` 无任何断言点名**(正是 FOLLOW-UP 记着的那一项:尺寸只被 `TESObjectREFR` 的 `sizeof` 间接覆盖 ⇒ 改动会在**离编辑点一个结构之外、且只在恰好被消费时**才被发现),另 `inactive`(读错即 `World::Update()` 永不再执行)的偏移被写**五遍**而只有 assert 那一遍被检查(含 `SkyrimVM64.cpp` 里**为诊断它而生**的探针与日志);现补 `ExtraDataList` 两分支断言、把两个候选值命名并让 pad/常量/断言/探针/日志全部由它们推导,新增 `check_branch_layouts.py`(三条判据 + 防退化);**变异测试还抓出门禁自己第一版不认虚函数的盲点并已修掉** |
| §132 | **「这个字段反正没人用」—— 但它的值域里有 0** | **改代码 + 新增门禁**:mods-mismatch 回包用 `Id == 0` 表示「你必须安装」,而 **0 是合法 id** —— 标准 mod 的 load-order 索引 0 是第一个插件,**ESL 的 `liteId` 更是从 0 开始发号**(`ESLoader::LoadLoadOrder` 逐字 `uint16_t liteId = 0x0;`, `TESFile::Setup` 逐字 `m_formIdPrefix = 0xFE000000 + (m_liteId * 0x1000);`),客户端 `Mod::GetId()` 返回的正是它 ⇒ **第一号 ESL 不在服务器上的客户端会被要求「安装」一个它必须「删除」的 mod**,且永远不会被告知删除(与 v1.1.9 的 MO2 那条同类,方向相反);现用**值域之外的两个标记**(`0xFFFF`/`0xFFFE`,由 `static_assert(kRemoveMarker > kMaxLiteId)` 兜住)、两次比较拆成两个列表、客户端把标记翻成 `install`/`remove` 词再进 JSON,UI 两个词都显式比较;新增 `check_ui_contract.py` —— **本仓库第一条同时读 C++ / JSON 语言文件 / TypeScript 的门禁**(错误码 × 14 种语言 × 联合类型 + 方向词两侧一致 + 防退化),五向变异测试逐条被指名(含「只删 `de.json` 的一项」),**首跑就抓出自己正则的驼峰盲点** |
| §133 | **「本仓库答不了的问题」怎么办 —— 先问「它能不能变成一行日志」** | **加证据 + 新增门禁**:`Mod::IsLoaded()` 的 `standardId != 0xFF` 对 **lite(ESL)文件**是真还是假,本仓库**没有任何证据**(全仓唯一的 `TESFile.h` 是本仓库**自己**给服务端写的模型、**没有哨兵**,不能回答引擎行为;联网已实测不可用;本机只有 1.5.97 且禁止本地编译),而两个答案后果相反 —— 通过则 lite 路径正常,**被丢则该 ESL 服务端从来不知道 ⇒ 它加的每个 form 静默失去服务端 id,且开了 mod policy 的服务器会要求玩家「安装」一个他已经装了的 mod(无法解决的拒绝)**;而 1.6.1130+ 随游戏出货的 `_ResourcePack.esl` **本身就是 ESL 且 `liteId == 0`**,所以这不是小众问题(与 §132 同源:都对**第一个 ESL** 敏感);**故意不改行为**(改成恒真会把被禁用插件也上报,反向误报;换成 `liteId != 哨兵` 又需要一个同样没证据的哨兵),改为**把它变成一行可判定的日志**(报出被丢文件 + `isLite/standardId/liteId`,外加两侧计数汇总,**必须是 `info` 级** —— 因为 `set_level(debug)` 在 `TiltedOnlineApp.cpp` 里是注释掉的),新增 `check_lite_mod_path.py` 守住「仪器 + 它问的那条路径 + 待验项」三者不被单独删掉;四向变异测试逐条被指名,**其中「改名主干标题」当场抓出门禁自己的锚太松(`'## 6.'` 同时匹配子标题 `## 6.1`)** |
| §134 | **一个按钮要走三层,而三层是三份独立手写清单 —— 没有任何东西同时看三层** | **改代码 + 新增门禁**:**活缺陷** `openPlayGuide`:第 3 层(派发)`else if (eventName == "openPlayGuide") ProcessOpenPlayGuide()` 与实现(`ShellExecuteW` 打开 wiki)早就写好、按钮接着(`party-hint.service.ts` 挂成组队提示的动作)、14 种语言也都译好,**只差 `ProcessHandler` 里那一行注册** ⇒ 「打开指南」按钮点了什么都不发生;**潜伏缺陷** `reconnect`:typings 声明、UI 调用、mock 实现,而 C++ **两层都没有**(按钮本身在 HTML 注释里,但那一行还会把重连预算清零 ⇒ 一旦放开就「什么都不做 + 自动重连被关掉」);**第三条由门禁自己抓出** `deactivate`:注册了、声明了、UI 包了一层,而 `OverlayClient` 无分支 ⇒ 请求跨边界后被**静默丢掉**;修法:补一行注册、`reconnect` 改用已注册的 `abandonAttempt()`+`connect()`(自动重连那条生产路径的两步)并从 typings 删掉对象上不存在的 `reconnect`、`deactivate` 补分支调用 `SetUIVisible(false)`(同文件 `ProcessRevealPlayersMessage` 的既有写法);新增 `check_overlay_bridge.py` 四条判据(声明⇒注册、调用⇒注册、调用的名字⇒派发、派发⇒注册)+两条报告;四向变异测试共 7 条诊断逐条被指名,**并顺带证明「注释里出现名字不算注册」** |
| §135 | **「搬过去就丢」的字段 —— 而通用版门禁说假话,所以扔掉** | **改代码(协议)+ 新增窄门禁**:`RequestQuestSceneUpdate` 带一个**没人读**的 `Status`,而它唯一的写入者拿**任务阶段号**和枚举常量 **5** 比较(`stageId == RequestQuestUpdate::StageUpdateForced`)—— 即「这个任务正好停在阶段 5」,与它读起来的意思无关;服务端 `OnQuestSceneChanges` 从头到尾没读它(用的是 `SceneType`/`SceneId`/`QuestId`/`Stage`/`ClientQuestType`,以及把 `StatusCode::SceneUpdate` 当**去重标签**);修法端到端删字段(结构体+`operator==`+序列化两侧+客户端写入者);**先否掉两个不存在的缺陷**(两个 `StatusCode` 值集不同是**对的**:`StageUpdateForced` 走 `[[fallthrough]]` 进 `StageUpdate` 并置 aggressive send,强制状态由服务端自己产生);判据:`check_wire_symmetry` 133 对仍对称(字段删除的机械判据)+ 新增 `check_wire_field_reads.py`(作用域**显式**为那两条场景消息与两个已读完的 handler);三向变异测试逐条被指名;**通用版(全部消息)写过、跑过、扔掉** —— 按字段名全树搜会**漏报**(`Status` 属于十几条消息)、按 handler 体内搜会**假报**(handler 转发给 helper 是常见写法,实测把 `CharacterSpawnRequest.FactionsContent`、`StringCacheUpdate.Values` 等在用字段报成没人读) |
| §136 | **自注册注册表里,**一个名字两个对象** —— 而「哪个赢」由静态初始化顺序决定** | **改代码 + 新增门禁**:`Console::Setting` 构造即挂进全局链表(`SettingBase(ROOT(),…)`),`RegisterServerSettings` 整条交给注册表且**不去重**,而 `FindSetting` 用 `find_if` **取第一个同名** ⇒ 重复声明 = **两个对象共用一个地址**,ini 与控制台只写进第一个,而第一个是谁由**跨 TU 静态初始化顺序**(未指定)**决定**,输的那个终生保持默认值;实测 21 条声明里恰好一处重名 `LiveServices:bAnnounceServer`(`GameServer.cpp` 与 `CommandService.cpp`),**两处读者都是真的**(`IsPublicServer()` → 公开列表判定 + `AllowsAutoPartyJoin()`;`/settime` 的队长权限)⇒ 两个方向的后果都错(公开列表设了不生效 / 公开服上队长仍能改时间);修法删副本改读 `IsPublicServer()`;新增 `check_console_settings.py`(**只读声明字符串,不依赖调用图** ⇒ 能担保)+ 防退化下限;三向变异测试(还原重复、**同文件内重名**、下限)逐条被指名;**门禁第一版没剥注释,把我自己引用该声明的注释当成声明 —— 与 §3.1 旧口径同一个坑,已修** |
| §137 | **玩家的四条消息是英文散文,而聊天把系统消息当翻译键渲染** | **改代码 + 扩展门禁**:链路逐段可查 —— `SendSystemMessage` 送 `kSystemMessage = 0` + 字符串,TS 侧 `MessageTypes.SYSTEM_MESSAGE = 0`(**两侧同值**)映成 `'system'`,而 `chat.component.html` 对 `typeClass === 'system'` 用 **`content \| transloco`** 渲染,Transloco 又是 `fallbackLang: 'en'` + `useFallbackTranslation` ⇒ **查不到就把键本身显示出来**;于是四条英文句子(其中两条的**同一意思在 14 份语言文件里早就译好**:`SERVICE.COMMANDS.NOT_ADMIN`、`COMPONENT.CHAT.SET_TIME_INVALID_ARGUMENTS`)**只有英文是对的**,其余 13 种语言看到英文;修法:两条改用**已存在已翻译**的键、传送失败两条改用新键 `SERVICE.COMMANDS.TELEPORT_FAILED`,新键**只加 en.json**(不放假译文,门禁**报告**缺语言);`check_ui_contract.py` 新增第 4 条判据「送进聊天的字面量必须在 en.json 里是键」+ 防退化;**两个变异逐条被指名**(改回散文、删掉新键 → **两处站点被一次点名**);**并记下:该门禁从 §132 起就把 `NOT_ADMIN` 报成不可达,我两次读成「死键」——报告项要当线索读** |
| §138 | **证据表里的一个偏移,让「往运行中的游戏写字节」静默通过** | **改工具 + 改证据 + 收紧门禁**(**不是代码 bug**:1.5.x 上 frame end 走 `TP_HOOK_IMMEDIATE` 钩锚点本身,只有 1.6.x 才 `SwapCall` 里面那条 `call`,所以 `{9}` 是故意的):生成器输入里逐字写着 `"frame end (call site, 1.6 only)"`,但**判断只是描述字符串**,工具照样对齐出 `0x9` 并写进表 ⇒ 表与代码互相矛盾;而门禁的 `UNUSED_LEGACY_OK` 把「表里有、代码不用」豁免掉,连带让**把 `0x9` 加回代码**这个改动**静默通过**(`SwapCall` 只检查那个字节是不是 `0xE8`,1.5.97 上 +0x9 那条 `call` 若不是同一条就会被**静默重定向**);修法:生成器加结构化字段 `MODERN_ONLY`(键为 `(id, offset)` + 原因)→ 输出 `-`;表行改为 `-` + 原因(生成器本机跑不了,逐字手写并说明);frame end 从 `UNUSED_LEGACY_OK` 移到 `NO_LEGACY_EXPECTED`;两个变异:加回偏移**现在失败**并引用表里的理由、表改回 `0x9` 从「被豁免」变成「被报告」 |
| §139 | **「卡住了」的日志说不出是谁;并为 648 条日志加占位符/实参一致性检查** | **改代码 + 新增门禁**:判据是二值的「**格式串占位符数 == 实参数**」,两个方向的坏法不同(占位符多于实参 ⇒ fmt **在写这行的那一刻抛 `format_error`**,而只在罕见路径触发的日志**从没被执行过**;实参多于占位符 ⇒ fmt 只格式化它要的,**多出来的值一个都不进日志**);实测全仓 **648 条**字面量格式调用里**恰好一处**:`InventoryService.cpp` 同族四行(440/446/452/455)中唯一没有占位符的那条,正是「截止时间到了而这只 actor **仍**带 `WaitingForAssignmentComponent`」——**那一段里唯一在说「卡住了」的日志,恰好说不出是谁**(`pActorName` 取好、传进去、被丢掉);修法补两个占位符与同族对齐;新增 `check_log_format_args.py`(**跨行调用**、`__FUNCTION__` 拼接、`{{`/`}}` 转义不算,格式串是变量则跳过但**跳过比例 >35% 即失败**、调用数下限 300、**只剥注释保留字面量**);三向变异(还原缺陷、跨行改成 4 占位符/3 实参、字面量花括号**不误报**) |
| §140 | **「还剩几次重连」的占位符原样打给玩家** | **改代码 + 新增门禁**:判据是「**文本要的占位符必须在渲染它的地方被交进去**」(Transloco 只替换拿到的参数,拿不到的原样留在文本里 ⇒ 「键被引用了」≠「这句话能读」);实测 `SERVICE.CLIENT.CONNECTION_LOST` 的文本在**14 份语言文件里都带 `{{remainingReconnectionAttempt}}`**,而它**唯一的调用点**(`client.service.ts` 的自动重连分支)一个参数都没交,值就在上一行(`this._remainingReconnectionAttempt--`)⇒ **每次重连玩家都读到 `{{remainingReconnectionAttempt}}`**;它活下来的原因:键确实被引用、第二个参数本来可选、英文玩家看到的**其余部分完全正常**;修法传一个参数;新增 `check_i18n_params.py` 三条判据(R1 跨语言占位符集合一致;R2 字面量键的调用点必须交出所需参数,覆盖 `translate(...)`/`\| transloco:`/`params:` 三种写法;R3 **运行时拼出来的键**(`'SERVICE.ERROR.ERRORS.' + code.toUpperCase()`)所需的参数必须由 `error.service.ts` 里该 code 的接口提供——此前没有任何东西把「键由 code 决定」与「参数由接口决定」对起来)+ 下限;**三向变异各命中一条判据**;**并记下门禁第一版报 4 条里 3 条是它自己的解析器洞(TS 简写属性、接口用 `;` 分隔、`selectTranslate` 没有词边界)——修解析器而不是改代码**;同轮另记三处「已验证干净」(初始化器管线 36 个/`RunAll` 唯一调用点、模板硬编码 12 处全是调试/标题、跨语言占位符 0 处不一致) |
| §141 | **五个 UI 事件监听没有产出者,其中一个包住整个 overlay** | **审计 + 扩展门禁(§134 那条桥的反向)**:判据「`skyrimtogether.on('X')` 必须有 C++ 侧 `ExecuteAsync("X")`/`EmitEvent("X")`」;实测 **29 个监听 × 24 个事件名 → 5 个死监听、0 个无监听的事件**,逐条查明:`setName`(源码写着「不要」)/`debug`(写着「不再需要」)/`dummyData`(只喂 F2 调试面板,数据循环已注释)/`protocolMismatch`(已被 `triggerError`+`wrong_version` 取代)/**`openingMenu`**——它的消费端是**整个 overlay**(`root.component.html` 的 `*ngIf="!(menuOpen$ \| async)"`),而全仓(任何扩展名)搜 `openingMenu` **只有 UI 侧三处**,24 个 C++ 事件名里没有它,**连 mock 都不发**;真正隐藏 overlay 的只有 `SetUIVisible(false)` 的两个调用点(都不是菜单钩子),`UI::GetMenuOpen` 只被 `DebugService` 用 ⇒ **菜单打开时 overlay 不会被隐藏**;**不改行为**(两个方向都改产品行为且相反:实现产出者会让开任何菜单整个 overlay 消失;删死通道则零变化但丢掉意图),改为:事实写进 **FOLLOW-UP 第 7 节**(含决定后的同步清单)、`root.component.html` 留注释、门禁 `NO_EMITTER` 里理由写 **OPEN QUESTION** 锁住状态;门禁从 34 条检查升到 **58 条**,三向变异(改 C++ 事件名 / 加无产出者的监听 / 加背后无监听的豁免)逐条命中;**并记下本轮一次自伤:两次锚点追加编辑把 `## v1.1.8` 标题整行删掉,靠编辑后回看结构才发现并修复** |
| §142 | **索引表查了范围,没查有没有填 —— 一个字节终止进程** | **改代码(四个工厂)+ 新增门禁 + 回归测试**:四个消息工厂都是「索引表 + 手写清单」,`Extract()` 只查 `data >= kXOpcodeMax`(**范围**),然后直接调 `s_xExtractor[opcode](aReader)`;而清单没点到的 opcode 其槽位是**空 `std::function`** ⇒ 调用抛 `bad_function_call` ⇒ `Extract()` 是 **`noexcept`** ⇒ **`std::terminate`,线上一个字节终止进程**;实测四个工厂今天**槽位全满**(53/53、56/56、1/1、2/2),但**服务端清单里有三个重复条目**(`NotifyDialogue`/`NotifySubtitle`/`NotifyPlayerDialogue` 各两遍)—— 正是「手写清单被反复编辑」的指纹,而**下一次编辑可能就是删而不是重**;修法:①四个抽取器调用前查空槽并返回 `nullptr`(与越界同一种行为)②新增 `check_message_factories.py`(R1 每个 opcode 有抽取器、R2 每个类型有 opcode、R3 **不得重复**、R4 声明了 opcode 的消息头必须被点到)③清单 59 → **56**,与 56 个 opcode 一一对应;判据**不猜名字**(`RemoveSpellRequest`→`kRequestRemoveSpell`、`AdminShutdownRequest`→`kAdminShutdown` 不同构,且两个方向共用目录)而是读消息头自己声明的**枚举名**;**第一版猜名字导致 116 条假失败,先怀疑判据**;两个变异(删掉一个名字 = 制造 terminate、写两遍 = 还原重复条目)逐条命中;新增回归测试钉住两种越界并断言真实消息仍解得开 |
| §143 | **消息「四样俱全」,唯独没人发送 —— 玩家拔/收武器之后别人看不见** | **改代码 + 新增门禁**:判据「线上的每条消息,要么有人发,要么有人收」;实测 **109 条**消息里查出 **`NotifyDrawWeapon` 从未被任何代码构造**:客户端检测到本地 actor 拔刀状态变化 → 发 `DrawWeaponRequest` → 服务端 `OnWeaponDrawnRequest` 只更新自己的 `CharacterComponent` 就结束,而 `NotifyDrawWeapon`(字段恰好 `Id`+`IsWeaponDrawn`)**有 opcode、在访问清单里、被客户端 include 着,却全仓无人构造** ⇒ 拔刀状态**只在角色生成时同步一次**,之后的变化**永不广播**:玩家 A 当着 B 的面拔刀,只要 B 早就加载了 A 就**永远看不到**(没有报错,功能只是不存在);修法:服务端按**同文件装备广播的既有写法**`SendToPlayersInRange(notify, cOrigin, acMessage.GetSender())` 广播 + 客户端补 `sink<NotifyDrawWeapon>`→`OnNotifyDrawWeapon`(用与 `OnNotifyEquipmentChanges` 相同的 `view<RemoteComponent, FormIdComponent>` 解析服务端 id,状态确实不同才调**生成路径用的同一个** `SetWeaponDrawnEx`,它置 `g_forceAnimation` 强制重放动画);新增 `check_message_use.py`(两方向各一条判据 + **「有人收、没人发」必须失败** + 豁免腐烂检查);**并记下门禁第一版会放过本缺陷本身**(只有「既没人发也没人收」才算失败 ⇒ 「有人收、没人发」直接通过;第一次做的是**假变异**(把变量改名 ≠ 不发送),真变异(注释掉整段广播)才暴露);另记录两条死通道不动代码的理由:`NotifyPlayerDialogue`(功能活着,走 `NotifyChatMessageBroadcast` + `kPlayerDialogue`)与 `RequestObjectInventoryChanges`/`NotifyObjectInventoryChanges`(保留未接线,容器背包走 `ObjectService`),**都不删 opcode** —— `Opcodes.h` 规定只追加,删表中段会让其后全部重编号 |
| §144 | **进程内事件总线(结论:干净),与一个只发不读的字段** | **审计 + 扩展门禁**(**本轮无缺陷**):①「进程内事件是否被派发」**不可由文本判定** —— 第一版按 `.trigger(` 扫报出 **30 个假失败**,三层原因:参数有 4 种形状(类型字面量/限定名/成员访问/变量,实测 3+4+3+61)、**还有第二个派发入口** `RunnerService::Trigger`(把 `m_dispatcher.trigger` 包进 lambda 入队,事件类型只在调用点由实参推导、文本里不出现)、以及 `Queue`;需要类型感知扫描,与 §135.5/§140.4 同一堵墙,**判据做不到就不发布**;②`ServerSettings` 通路**干净**:服务器 7 个字段全写(`GameServer.cpp:141-147`),客户端两处 `trigger(acMessage.Settings)`(`TransportService.cpp:791/903`)⇒ `OnServerSettingsReceived` 确实会跑,6 个字段有读者(`Difficulty`/`GreetingsEnabled`/`DeathSystemEnabled`/`PvpEnabled`×2/`SyncPlayerHomes`/`SyncPlayerCalendar`);③唯一记录项 **`AutoPartyJoin` 只发不读**(自动入队完全由服务端决定并把结果用普通队伍消息发出,客户端不需要它)——**不删**(固定字段表,删中间会让其后位移)**也不硬造读者**,改为扩展 §135 门禁的第三个主体并写明理由;**并修掉该门禁自己的三处洞**(结构体正则只认 `final`;`fields_of` 从错误的 `{` 开始拿到内联方法体;「在到达处被读」对共享结构体不成立 ⇒ 支持目录作用域,修前报 3 个假失败);变异:两个读者**都**改 `if(false)` 被抓、只改一个**正确通过** |
| §145 | **手写的 SKSE ABI 镜像:断言了语义,没断言布局** | **加固代码 + 新增门禁**:`SksePluginVersionData` 是**手写**的(dll 不链接 SKSE 头文件),而 **SKSE 按偏移直接从导出里读它、永远看不到字段名**;逐字段核对 **9 字段/类型/数组长度/`sizeof = 848` 与 SKSE64 `PluginAPI.h` 完全一致**(干净),**但文件里 3 个 `static_assert` 只断言了标志位与 `kVersion`,没有一个断言布局** —— 而加字段/改数组长度/交换相邻字段会让其后**每个**字段偏移移动 ⇒ SKSE 把 name 读成 author、把标志位从版本列表里读出来,且**静默**(导出仍在、名字仍叫 `SKSEPlugin_Version`);修法两层:①新增 4 条 `static_assert`(`sizeof == 848`、`offsetof(name) == 8`、`offsetof(versionIndependenceEx) == 772`、`offsetof(seVersionRequired) == 844`,与 §131 给 `ExtraDataList`/`GameVM` 的同一形状)**编译期**挡住 + 补 `<cstddef>`(`offsetof` 需要它,而这是**没有 PCH** 的独立 dll,客户端那些文件靠 PCH 传递、不能照抄);②新增 `check_skse_abi.py`,把 SKSE64 参考布局**写成数据**逐字段比对 —— 判据是「这个声明是否仍描述 SKSE 实现的接口」而非「是否与它自己一致」(后者永远为真);三向变异(数组改长 / **交换同类型相邻字段** / 标志位改错)逐条命中,交换那个**编译期断言也会挡**;**门禁自己两个坑**:比常量文本而非值(`1 << 1` vs `2`,2 条假失败)、`const char*` 空格未归一化 |
| §146 | **插件 C ABI 的结构体布局没有被钉住** | **扩展门禁(契约新增 C5)**:`STRPluginMessagingAPI.h` 是**独立插件仓库各自 vendor 一份**的 C ABI;契约此前钉住 **sha256**、**常量值**、**24 个类型名字**,而 `interfaceTypes` **一个字段都没描述** ⇒ **「改字段顺序/加字段/改数组长度 + 重新记录 hash」全绿通过**(而重记 hash 正是设计好的**正常工作流**,不是漏洞利用);危险是真的:消费者按**自己那份拷贝**编译,`Target` **按值**传进我们的 `send()`,`Message*`/`RuntimeStatus*` 我们交回去它**逐字段读** ⇒ 布局不一致时它把我们的 `sender.displayName` 从错误偏移读出来**当指针解引用**,崩溃在**它的**插件里且**构建期无错**;修法:`struct_layouts()` 抽字段表(类型+名字+数组长度,按声明顺序)→ `dump` 写入 `interfaceLayouts`(**10 结构体/48 字段**)→ `check` 的 C5 **两层**(主体:当前布局与 pinned 逐字段相同,故「重记 dump」不再顺手带走改动;每消费者:vendored 拷贝里双方都声明的结构体布局必须一致 —— 这才是跨 DLL 危险);变异:**重排某消费者 vendored 拷贝里的 `Sender` 字段**被抓,而**只保留主体层**或**保留 `struct in used` 过滤**都不会发现(两者都必要);**门禁自己三个坑**:①正则灾难性回溯(嵌套量词重叠,读一个头文件 **5 分钟不结束**)⇒ 改**线性扫描**后 **0.001 秒**;②字段静默漏掉(只认 `类型 名字;` ⇒ `const char* channel` 与**全部函数指针**没进布局,`Interface` 1 个字段应 7 个);③**过滤器把该查的排除了**(`struct in used` 让「消费者不点名但会读到的结构体」全部豁免,而跨边界读的结构体**恰恰大多不被点名**) |
| §147 | **游戏结构体的布局断言(结论:自洽),与三次失败的工具** | **审计 + 新增窄门禁**(**本轮无缺陷**):想查的「断言的字段是否覆盖代码用到的字段」**在文本层面不可判定** —— 第一版报 `61 个有断言的结构体、0 个解析出成员`,逐层查下去**全是工具自己的错**:①`STRUCT` 正则**漏 `re.M`**(`^\s*struct` 只在文件开头匹配 ⇒ 一个结构体都没找到);②补上后**嵌套类型被当成员**(这些头文件有 **153 个嵌套 struct/class/enum 声明**、**22 个预处理分支**,`Actor` 因此报出 **114 个「成员」**,「未钉住」里是 `SpellItemEntry`/`ActorValueModifiers` 等类型名与 `SKYRIM_TARGET_LEGACY`/`TP_PLATFORM_32` 等宏);③断言检查**无视分支**(把 `#ifdef SKYRIM_TARGET_LEGACY/#else` 的两套偏移当成「同字段两值」,`Actor` 一个文件报 **14 条假冲突**,而那是**设计**);⇒ 需要真正的 C++ 解析,与 §135.5/§140.4/§144.1 同一堵墙;能判定的那部分:**同一预处理分支内一个偏移只对应一个字段、一个字段只有一个偏移**(跨分支允许不同)= 实测 **192 条断言/47 文件/55 分支 ⇒ 0 处不一致**,做成 `check_layout_asserts.py`(跟踪 `#if/#else/#endif` 深度 + 下限 100),**文档字符串写明它不查字段覆盖率及原因**;两向变异(同字段两偏移 / 两字段一偏移)逐条命中 |
| §148 | **Papyrus 变量类型标签定义了两遍,而两者零个共同名字** | **审计 + 新增门禁**:同一个标签空间由**两个永远看不到对方代码的组件**各自定义 —— `components/es_loader/Records/Chunks.h` 的 `Type`(解析 `.pex` 脚本块,`Chunks.cpp` 里 `case Type::OBJECT_ARRAY`)与 `client/Games/Skyrim/Misc/BSScript.h` 的 `BSScript::Variable::Type`(向 VM 注册 native、VM 回传返回类型);**零个共同名字**(`INVALID/OBJECT/WSTRING/INT/FLOAT/BOOL` vs `kEmpty/kObject/kString/kInteger/kFloat/kBoolean`),**实测 11 处对应关系全部一致**(六个标量同值、五个数组标签都在 `kMax=16` 之内)⇒ **今天对,但没有任何东西维持它**;两边各自改一处**两个枚举都照样编译且各自自洽**,失败形式是**静默误解码一个 Papyrus 变量**(插件/脚本一侧),与 §145/§146 同形状;新增 `check_papyrus_types.py`:R1 六个标量两侧同值(**配对表写死在门禁里并附理由,因为按名字找不到对应**)、R2 loader 的每个数组标签低于 client 的 `kMax`(那份声明才代表「VM 表能到多远」)、下限防退化;两向变异:删 client 侧一个隐式编号枚举值(`kFloat`)→ **同时报出 `FLOAT` 与 `BOOL` 两条**(隐式编号删一项,后面全部重编号,**第二条诊断才是真正后果**)、`kMax` 16→12 → 四条数组标签逐个点名 |
| §149 | **提交清单(§0)逐条查账,查出一个「无法检查时报告成功」的门禁** | **改代码(门禁)+ 改 CI + 新增两个门禁 + 新增「守门禁的门禁」**:①把 §0 的 **13 个 `- [ ]` 条目**整个读一遍逐条问「谁在看它」,分三类(已有门禁 / 能变成门禁 / 只能靠人读);②**FOMOD 安装向导的两条语言分支**必须提供同样的东西(`ModuleConfig.xml` 整套步骤写了**两遍**,一份 `lang=zh` 一份 `lang=en`,而**插件名字是翻译过的**所以只能比结构与载荷)⇒ 新增 `check_fomod_languages.py`(R1 步骤数 / R2 逐步插件数 / R3 逐步**安装的文件夹与文件完全相同**;R3 才是要害:**加在中文分支而英文分支没有的插件,英文玩家根本装不上且永远不知道自己少了**,因为向导照自己文件执行、一切正常);实测 4 步/6,2,9,2/12,2,9,2 两侧一致 ✓;③**xmake 下限与 CI pin** 是**两处不同类声明**(`xmake.lua` 的 `set_xmakever("3.0.0")` 是 xmake 自己会拒绝运行的**下限**,`windows.yml` 的 `xmake-version: 3.1.0` 是**装哪个**)⇒「让它们相等」既不是规则也做不到,真不变式是**装的 ≥ 要求的**,新增 `check_xmake_version.py` 按这个方向查并逐个打印读到的数;两向变异(抬下限到 3.2.0、把 pin 降到 2.9.9)都被抓;④**活缺陷**:`Code/plugins/tools/check_workflow_shell.py` 用 `try/except ImportError` 包住 `import yaml`,失败时打印 `POWERSHELL PARSE CHECK SKIPPED` 并 **`return 0`**,而它的注释声称「CI 会先装好 pyyaml」——**跑它的 `contract` 作业什么都不装**,整个 `plugins.yml` 里唯一一句 `pip install pyyaml` 在 **`wizard` 作业**里(而 wizard 不跑这个门禁)⇒ 它**在 CI 里从来没解析过任何东西却一直是绿的**,比「门禁不存在」更糟(**不存在的看得见**,而「跳过」与「通过」输出完全相同);修法两向:门禁改 `raise SystemExit`(写明 pyyaml 是**需求**不是优化、不要降级成跳过)+ **空语料保护**(一个 PowerShell 块都没找到也失败),并在**跑它的那个作业**里补 `pip install --quiet --disable-pip-version-check pyyaml`;两向验证:正常 `WORKFLOW SHELL CHECK OK` exit=0,用**只让 `yaml` 不可导入**的包装器跑同一脚本 ⇒ `WORKFLOW SHELL CHECK CANNOT RUN` + `VERDICT: FAIL (correct)`;⑤新增 `check_gate_integrity.py` 把 FOLLOW-UP 里早写成文字的三条(它会跑吗/它会失败吗/它看全了吗)变成会跑的**七条判据**(R1 无吞掉的 ImportError / R2 无未登记 SKIPPED 横幅 / R3 磁盘上每个门禁都被工作流调用 / R4 每个门禁被**跑它的那个作业**的路径触发覆盖 / R5 同一工作流 `push` 与 `pull_request` 路径列表一致 / R6 **工作流点名的门禁必须在磁盘上存在** / R7 跑门禁的作业必须装了它依赖的包);**R6 是单向规则自己暴露的**:最初的 R3 只遍历**磁盘上找到的**脚本,于是把 40 个门禁删到只剩 1 个仍打印 `1 gate script(s), 0 never run by CI` + `OK`(**而工作流里仍点着 40 个**)⇒ 补反向并要求**两个方向各报一个数且必须都是 40**;顺带查出并修掉四条真实问题(`contract` 无任何 `pip install`;三个新门禁**三条路径触发里都没有**;`.github/workflows/**` 此前不是任何触发路径,而 R5/R7 读的正是它;两条列表各 6 个重复条目——**报告**而不失败,因为它是手改列表的指纹);**七个变异全部被抓,而其中四个第一次是 MISSED、三个是变异自己写歪**(改的是**已存在且仍被工作流点名**的脚本 ⇒ 改成**新增**一个;只改一条列表 ⇒ 先触发 R5 而 R5 命中时会跳过 R4 ⇒ 改成两条一起改;变异加 `import requests` 触发的是「包名没登记」而非「作业没装它」⇒ 改成删掉那句 pip;第四个 MISSED **确实是门禁的错**:变异写成 4 空格缩进让 YAML 移到别处 ⇒ 门禁抛 traceback,已包上 `except yaml.YAMLError` 报成失败);最终 40/40 门禁通过、两条触发列表各 81 条(74 唯一)集合相等、`contract` 46 步骤/40 门禁步骤 |
| §150 | **Papyrus `native` 的两份手写清单(隔着语言边界,没有编译器同时看到它们),与出货 `.pex` 的同类配对** | **改代码(客户端两处)+ 新增两个门禁**:①判据沿用 §134/§143 —— **同一个名字写在两个地方而没有任何东西比较它们**,这次的两处隔着语言边界:`.psc` 的 `Function Foo() global native` 是**承诺**,C++ 的 `RegisterFunction`/`BindNativeMethod` 是**兑现**;扫 4 个第一方脚本目录 + 全树 232 个 C++ 源文件 ⇒ **9 声明 / 9 注册,类名与函数名全部一致**(加的是守护);②**顺着调用链往下读一层,找到真缺陷**:`Code/client/Games/Skyrim/Misc/BSScript.cpp` 的 `IsRemotePlayerFunc`/`IsPlayerFunc` 的 `MarshallAndDispatch` 都是 `if (!pActor) return false;` —— **`return false` 不是「返回 false」而是「这次调用失败」**,VM 于是用返回类型的默认值代替结果,脚本拿到一个合法的 `false`;而 `!pActor` **不是异常路径**:所有出货调用点都做**不检查的** `as Actor`(`TempleBlessingScript.psc:7`、`ResourceFurnitureScript.psc:44`、`HallofStoriesDiskScript.psc:95,107,119`),非 Actor 激活物 ⇒ `None` ⇒ **每次普通激活都走进该分支**,而这几处脚本的用法恰好都是守卫 `if IsRemotePlayer(x) → return`,于是**守卫静默失效**;改为 `apResult->Set<bool>(false); return true;`(None 是答案,不是失败);③同轮第二个缺陷:`Code/client/Games/PapyrusFunctions.cpp:19` 的 `IsPlayer` 第一行是无条件 `spdlog::info("Calling IsPlayer");`,每次脚本调用写一行,已删;④新增 `check_papyrus_natives.py` 五条判据(R1 声明⇒注册 / R2 注册⇒声明 / R3 C++ 写明的参数个数 == `.psc` 的参数个数(VM 拒绝个数不符) / R4 类名是真实 `Scriptname` / R5 **任何 `MarshallAndDispatch` 不许 `return false;`**),刻意不查 SKSE 侧参数**类型**(由 C++ 签名编译期推导,源码里没有数字,边界写在 docstring);⑤**门禁自己的三个坑**:R3 **一次都没跑过**(参数个数表**按文件**建,而这三个函数**注册**在 `Code/client/Games/Misc/BSScript.cpp`、**构造**在 `Code/client/Games/Skyrim/Misc/BSScript.cpp`,两条路径 ⇒ 三条全报「参数个数未写明」,改成**全树扫描**才真正执行 —— **一条规则静静地从未执行而输出里看不出来**)、`BIND` 抓到 `…Func` 而构造表以无后缀名为键、R4 里一句**恒真式**;⑥**而 R5 第一版恰好漏掉它自己为之而写的缺陷**:判据 `returns_false and not sets_result` 中**成功路径也有** `apResult->Set<` ⇒ `sets_result` 恒真,把 `return false;` 改回磁盘门禁照样打印 OK,收紧为 `re.search(r"\breturn\s+false\s*;", body)`(花括号配对取体、剥掉 `//` 注释 —— §143 的教训)才抓住;四个变异(参数 `1→2` / `"IsPlayer"→"…MUTATION"` 双向同时命中 / 类名加 `MUTATION` / 恢复 `return false;`)全部被抓、全部还原;⑦**第二处同类配对**:`.psc` 是源码而 **`.pex` 才是出货的东西**,`GameFiles/Skyrim/scripts/` 下 10 对一一对应、`Tools/Packaging/New-STNModPackage.ps1:105` 打包 `.pex`,而**全树没有任何东西读它** —— `windows.yml` 的编译步骤**故意写到临时目录** ⇒ **改了 `.psc` 不重编就出货旧脚本,而源码是读者信任的那份**;新增 `check_shipped_pex.py`(每个 `.psc` 有 `.pex` 且反向也有、大小写不敏感配对、声明的每个名字与**每个 ASCII 字面量**(多行**逐行**)必须在 `.pex` 可打印串表里、下限 8/30),刻意不查控制流/算术与**既不碰名字也不碰字面量**的编辑(已提交的 `.pex` 出自原始编译器,`SkyrimTogetherVerifyLaunchScript.psc` 当初是**照着它 `.pex` 里已有的字节**重写的);两个变异(改字面量、改事件名)都被抓;⑧最终 **42/42 门禁通过**、两条触发列表各 **84 条**集合相等、`contract` **48 步骤 / 42 门禁步骤** |

| §151 | **一个包住两个组件的守卫,被记成了产品决定** | **改代码(删死通道)+ 扩展门禁**:①§141 把 `openingMenu` 这个**没有产出者**的监听记录下来了,但**代价记错了一个量级** —— 当时写的是「实现产出者会让开任何菜单整个 overlay 消失」,实测 `root.component.html` 的 `*ngIf="!(menuOpen$ \| async)"` **只包住两个组件**(`app-debug`、`app-group`),聊天窗/通知/队伍提示都在守卫之外 ⇒ 这句话是错的,而它当时没人核对(三份文档都跟着抄了一遍);②**订正后决定删死通道而不是实现产出者**:两个方向都会改产品行为,而删除是**零变化**的;留着的代价是**一个被记录的空行为比一个不存在的功能更容易被误认为已实现**;③同轮第二处:overlay 自己也在把**散文当翻译键**渲染(`chat.component.html` 的 `{{ message.content \| transloco }}`),10 个调用点里 9 个是点号大写键、1 个是散文,走的是**同一条渲染路径**而 §137 的门禁只扫了 C++ 那一侧;④门禁 `check_overlay_bridge.py` 从 34 条检查升到 **58 条**,三向变异(改 C++ 事件名 / 加无产出者的监听 / 加背后无监听的豁免)逐条命中;⑤**记下一次自伤**:两次锚点追加编辑把 `## v1.1.8` 标题整行删掉,靠编辑后回看结构才发现并修复;⑥**一条已知边界(如实记下)**:同一个变异如果把调用点**注释掉**而不是改名,门禁**不会**报警(正则的 `\s*` 允许跨行),这是设计边界不是漏洞 —— 要连注释一起抓得先剥注释,那是另一条规则 |
| §152 | **门禁只认「同一条语句里的判空」,于是 15 处「赋值-解引用」型缺陷一次也没被报出来** | **改代码(客户端 13 文件)+ 修门禁**:①判据是「访问器返回值在**被解引用之前**必须判过空」,而上一轮那份门禁(207 行)只做一件事:`call_sites()` 逐条**语句**找 `accessor\(\)\s*->`,**没有「赋值 → 使用点」的追踪**;②两条可判定的测量:**上一轮那份门禁指向改动前的 13 个文件 ⇒ 0 个站点**(装着全部 15 处缺陷的树它报 OK),**本轮这份指向同一份改动前的树 ⇒ 恰好 15 个站点**;③15 处落点:`Actor.cpp:650→673`、`Actor.cpp:902→909`、`MagicItem.cpp:40→41`、`CalendarDebugView.cpp:12→14`、`CellView.cpp:16→18`、`ProcessView.cpp:16→18`、`WeatherView.cpp:13→15`、`CalendarService.cpp:74→75`、`CalendarService.cpp:89→104`、`CharacterService.cpp:2510→2511`、`DiscoveryService.cpp:216→217`、`InventoryService.cpp:299→304`、`PlayerService.cpp:276→279`、`TransportService.cpp:410→411`、`ModSystem.cpp:91→107`;④**修法一律走「已存在的退化路径」**,不发明新行为:能早退的早退(四个 Debug 视图的 `return` 必须在 `ImGui::Begin` **之前**,否则 `Begin`/`End` 栈错配)、不能早退的包住(`Actor::SetMagicEquipment` 用同层 `const bool hasEquipManager` —— 写成三处 `if (pX != nullptr)` 会被门禁报「赋值行比守卫深」,因为 `unguarded_use()` 只把 `depth[j] <= depth[call]` 的测试算作支配)、`TransportService` 空指针时**保持 `TimeModel` 的默认值**(`TimeScale = 20.f` / `Time = 12.f` / `Year = 1` / `Month = 1` / `Day = 1`),`ModSystem` 空指针时让每个条目落进既有 `else`(`++unmapped` + `error`);⑤**订正一处因果**:15 处从未被报出**不是**形状漏认造成的,是判据缺了「赋值→使用点」半边;形状漏认造成的是**另外 6 条误报**(`if (pX)` 裸真值判断与 `&& pX` 右侧),把形状表换回上一轮、指向当前树恰好报那 6 条;⑥门禁 `test_of()` 扩到 **7 组 alternation**、`SELFTEST_CASES` **25 条** + `--selftest`;6 处豁免站点**故意未改**(代码里判空真实存在,只是旧正则不认) |
| §153 | **5 个 RTTI id 在随仓库发布的地址库里整条不存在,而 `Cast<NiObjectNET>` 会静默返回 nullptr** | **新增门禁(记录并按住)**:①§43.8 早就记下了缺失,结论是「不修」,留了一句「评审时盯住」—— **没有任何东西在维护它**;②**订正两处口径**:不是 **4 个**是 **5 个**(多出 `400180 BSWin32GameDataSystemUtility`,14 张 bin 里全在、10 张 map 里全不在),而任务书说的「38 个实参 / 200 站点」是**不清注释**的裸扫,清注释后是 **37 / 196**(差掉的 4 个都在注释里,`ExtraTextDisplayData` 在活代码里一次都不出现);③**为什么不修**:`versionlib-*.bin` 是 SKSE 官方发布产物,改一个字节哈希就对不上、`CheckAddressLibrary` 可能直接拒收;缺口是 **id 空洞**(`392519` 整行不存在,邻居 `392518 = 0x203B928` / `392520 = 0x203B970`),不是偏移 0 —— 所以语义是**记录并按住**,只要求缺口不被使用;④新门禁 `Tools/Scripts/check_rtti_coverage.py`(355 行)比 **2806 条注册 × 24 张表 = 67344 对** + **196 个 `Cast<>` 站点 / 37 个实参**,四条判据:实参全解析 / `GAPS` 双向一致(含 stale 反向查)/ `GAPS` 里的 id **不得被任何 `Cast<>` 触达** / 四类语料任一为空即失败;⑤`GAPS` 登记 5 个 id:`392519 NiObjectNET`、`394235 CreationClubMenu`、`398437 NiParticleSystem`(各缺 6 张 bin)、`400159 CCallbackBase`(缺 3 张)、`400180`(缺全部 10 张 map);⑥**门禁做不到的四件事**写进 docstring:补不上缺口只能记录、看不见 `Cast<T, U>` 的源类型、说不清调用点拿到 null 后会怎样、只读 RTTI 表(`POINTER_SKYRIMSE` 那套 id 属于 `check_map_coverage.py` 的语料);⑦四个变异全部被抓(新增 `Cast<NiObjectNET>` 站点 / 删 `GAPS` 条目 / 反向加 stale 条目 / 清空扫描语料) |
| §154 | **三个魔数只验过一个版本 —— 断言钉得住自己,问得动引擎的只有一行日志** | **改代码 + 扩门禁(缺口仍未关闭)**:①`bUseFaceGenPreprocessedHeads` 的三个结构体偏移(`Setting::data` +0x8、`Setting::name` +0x10、`INISettingCollection` 链表头 +0x118)**只在 1.5.97 上量过**,影响 1.6.317 ~ 1.7.104 全部 AE 运行时;②**这个缺口最坏的地方不是「可能错」而是「错了看不出来」** —— 偏移错了不会崩,`GetSetting` 返回空,标志就永远不被清,于是「AE 上没修」与「AE 上偏移错了」看起来**完全一样**;③拆法一:`Code/client/Games/TES.h:176/177/178` 补三条 `static_assert`(**它们钉得住本仓库的布局,钉不住引擎的** —— 引擎按自己的偏移读,看不到我们的字段名);④拆法二:`SetRemoteFaceGenForce` 的**首次写入**从 `debug` 提到 `info`(`fmt::ptr(pSetting)` + 原值),查找失败路径补 `warn` 且**仍然 `return`** —— 这才是真正问得动引擎的东西,而**出货构建的日志等级是 info**(`TiltedOnlineApp.cpp:71` 的 `set_level(debug)` 是注释掉的),写成 debug 等于写进没人看得到的日志;用函数内 `static bool s_reportedFirstForce` 守住只打一次(`CharacterService.h` 不该为一个日志改动新增成员,且该函数是 `const noexcept`);⑤拆法三:`check_facegen_scope.py` 第 7 条锁住上面两件事(三个字段声明与三条断言仍在 / info 行被 `if` 包住且 debug 行未删 / 失败路径既 `warn` 又仍然 `return`);⑥5 条变异全部被咬(info 改回 debug / 删 `warn` / 删 `head` 断言 / info 改成无条件 / 删 `Setting::data` 字段),`check_layout_asserts.py` 断言总数 192 → **195**;⑦**状态必须保持「待验」**:判据已就位,能回答它的只有一名 1.6.x/1.7.x 玩家的一行日志,**不要把这个缺口标成「已关闭」** |
| §155 | **同一个 `STRPM::Message`,框架路径交给插件的是另一个结构** | **改代码 + 新增门禁**:①现象:插件的诊断日志里 `sequence` 恒为 0;②定位:`plugins/MorphSyncTogether/src/UdpTransport.cpp:270` 与 `plugins/OStimTogether/src/STRPMTransport.cpp:449` 的 `message.sequence` **都只是格式化日志实参**,不进任何分支,payload 走 SKSE task interface、packet 里不含 sequence ⇒ **这是诊断信息缺失,不是崩溃类缺陷**;③根因**不是漏赋值,是信封里没有这两个字段**:`PluginMessagingService::Send` 的 `aFlags` 形参**出现 0 次**,而接收侧把两个字段**硬写成常量**(`message.flags = STRPM::kMessageNone; message.sequence = 0;`);④修法:两个线上消息各加两字段(`PluginMessagingRequest.h:59-60`、`NotifyPluginMessaging.h:46-47`),**追加在 payload 之后不移动既有偏移**,服务端只做 router 不解释,发送侧 `m_nextSequence.fetch_add(1)` 供号;⑤顺带补上**从未实现**的 `kMessageAllowLoopback`(全 `Code/client/**` 原零命中),本地投递**锁内只收集、锁外调用回调**(插件可在 receive handler 里 send,非递归 mutex 持锁调用会死锁);⑥**为什么可以加字段**:`Code/server/GameServer.cpp:886` `if (acRequest->Version != BUILD_COMMIT)` ⇒ 同版本配对是唯一受支持配置,跨版本混用不受支持;⑦**未验证(原样保留)**:`Libraries/TiltedConnect` 在本机是**未初始化的空 submodule**,全仓 grep `SendMessageToConnection\|nSteamNetworkingSend\|k_nSteamNetworkingSend_Reliable` **零命中** ⇒ 底层是否可靠有序**无法从本仓库确证**,因此只透传 `kMessageReliable\|kMessageOrdered`,不改协议语义;⑧新门禁 `Tools/Scripts/check_message_flags_parity.py` 五条静态断言(`Send` 必须真的用 flags 形参 / sequence 与接收侧赋值右侧不得是常量 / 两个线上消息写读同序 / 服务端必须转发),两个变异实测均退出 1 |
| §156 | **14 个门禁在「什么都没比」时报 OK —— 空语料被当成了通过** | **改门禁**:①形状与 §149(无法检查时返回 0)、§152(门禁缺半边)都不同 —— 门禁**没有跳过任何东西**,它正常走进循环、正常打印 OK,只是**循环体一次都没执行**;判据代码的共同语法是 `if X and not failures:`,而 `X` 来自正则或查找时就有**第三种取值:没找到**;②「**数成 0」比「数错」更危险**:数错会打印一条错的行号人会去看,数成 0 会打印 OK,而 OK 与「真的比过了」在输出上完全一样;③14 个门禁的机制逐条:正则要求终止符在第 0 列(`check_skse_bootstrap.py` 缩进一格就让三条标志位规则整段跳过)/两处 `re.findall` 可返回空(`check_exports.py`)/`if other is None: continue` 后**仍 `checked += 1`** 且无下限(`check_contract_copies.py`,清空一份 vendored 头文件掉到 22 仍通过)/两条顺序规则被 `if ... and 'kSetTimeCommandRequest' in client:` 门控(`check_transport_compat.py`,改成 `elif` 是刻意的 —— 只有「opcode 声明着而锚点标识符没了」才是缺陷)/`if not match: return None` 被读成「这个插件不用那个接口」却仍 `checked += 1`(`check_skee_abi.py`)/未 checkout 的子模块上 `rglob` 静默返回空(`check_ostimng_build.py`,与 `check_ostimng_versions.py:126/158` 的既有先例对齐)/`deref_pattern()` 只认两种拼写 + 末尾反向循环是**空操作** + 摘要行算的是**常量表的长度却被当成实测值打印**(`check_allocation_guards.py`,基线 `compared` 实为 **4 不是 6**)/`checked` 可为 0(`check_patch_offsets.py`)/`if cleanup:` 未命中就跳过整条规则(`check_crash_diagnostics.py`)/`--list` **直接 print 三句判定字面量而从不调用 `check()`**(`check_capstone_report.py`,**这条缺陷不在退出码里**,任何基于退出码的变异都测不到它)/选择器侧与消息侧各自可能为空(`check_ostim_support_message.py`)/`SHIPPED_MAP` 缺失只 warn(`check_ae_to_se_overrides.py`)/`checked == 0` 只 warn(`check_library_scan.py`)/`newest_own_version()` 返回 None 只 warn(`check_package_metadata.py`);④**共同修法**:门禁的语料**本来就在版本控制里**,它的缺席意味着检出损坏或扫描条件写错,不是「这台机器上不需要检查」,所以 `warnings.append` 是错的容器(`warnings` 只被打印、永不进入退出码);⑤**变异测试的第一条断言是「变异落地了」** —— harness 自己错了三次(用行号区间切「pre-fix 版本」切错、文件名拼成 `SkieInterfaces.h` 而真名是 `Skee`Interfaces.h(双 e)导致变异**根本没应用**、还原只按行号恢复漏了一处),而「门禁没抓到」与「什么都没改」在输出上完全一样;镜像树还有两个陷阱(建立时经过的一层把门禁写成 CRLF ⇒ 8 条假 STALE,`Path.read_text()` 的 universal newlines 把 `\r\n` 折成 `\n` 所以数 `\r` 恒为 0,只有 `read_bytes()` 看得出);⑥三条**变异本身太弱**的教训(改常量名 ≠ 有效变异,因为 `contract.json` 的 `exportNames` 存的是字符串值;清空 manifest ≠ 清空扫描,只有删除才行;只改一份拷贝/一个成员都不够);⑦**pre-fix 也红 ≠ 缺陷被确认** —— 那说明该变异打中的是**另一条既有规则**,`check_ostim_support_message.py` 如实记为 `pre-fix also red`;⑧**控制组必须保持绿**:只缩进终止符(纯重排)/`OStimTogether` 合法不声明 `IBodyMorphInterface`/整个 `plugins/OStimNG` 目录删掉(合法缺席)/纯空白重排;⑨结果 `problem(s): 0`,11 个门禁全部 `pre-fix exit=0 → fixed exit=1 → reverted exit=0`,全量复扫 **44/44** |
| §157 | **五个「覆盖洞」—— 判据依赖的那份数据被削弱后,门禁仍然报 OK** | **改门禁**:①与 §149/§156 都不同的第三种形状 —— 门禁**确实在比**,但**它比的那份数据本身被削弱了**,而削弱这件事没有任何东西在守;语料不是「扫描范围」而是「**判据所依赖的那份文件**」(头文件 4 个契约常量 ↔ `contract.json` 的 `exportNames`、每个插件声明了哪些 SKEE 接口 ↔ 一份手写期望表、`contract.json` 的 `headerSha256` ↔ 权威头实际哈希、`Code/skse_client/xmake.lua` 的 legacy 构建目标、随包出货的 14 个 AE 地址库);②**洞 1** `check_exports.py` 的 docstring 写着「the contract names」而代码只读头文件常量,`named` 是 **dict**,改 4 个里 1 个就只剩 3 条 ⇒ 修法是读 `contract.json` 的 `exportNames` 做**双向**集合相等(**前向与反向都记失败**,否则一个自身被收窄的 manifest 会让门禁保持绿);③**洞 2** `check_skee_abi.py` 没有 per-copy 下限 —— `IOverlayInterface` 被**两份**源文件声明(`plugins/MorphSyncTogether/src/SkeeInterfaces.h:97`、`plugins/OStimTogether/src/RaceMenuOverlayBridge.cpp:46`),只改一份时 per-interface 下限不触发、**那一份的方法顺序静默离开比较**;**为什么 per-interface 下限不够**:它的判据是「这个接口还有没有任何人声明」,而缺陷的形状是「**这一份**不再声明了」,同一接口被两个组件各 vendor 一份时前者恒为真 ⇒ 判据必须**逐份**;修法是 `EXPECTED` 表(实测矩阵 MorphSyncTogether 声明全部 3 个、OStimTogether 只 2 个**不含 `IBodyMorphInterface`**,这个合法缺席是控制组)+ 双向比对 + 沿用仓库既有的**反向 stale 检查**;④**洞 3/4/5** 都是 `warnings.append` 改成 `failures.append`(`check_contract_copies.py` 的 `headerSha256` 缺失、`check_library_scan.py` 的 `xmake.lua` 缺失 —— 同文件 `:89-99` 已因 `main.cpp` 缺失而失败,**自相矛盾**、`check_target_meta.py` 的地址库存在但不可读 —— `read_library()` 返回 `{}` 被 `if not library: continue` 静默跳过,而摘要行仍把它算进 `all 14 AE runtime(s)`);⑤**洞 5 的语义要点**:「缺席不失败、存在但不可读才失败」(某个运行时本来就没出货是合法的,控制组实测移走 `versionlib-1-7-99-0.bin` 后仍退出 0 并报 `all 13 AE runtime(s)`);⑥**判据**:warn 是缺陷当且仅当「它不存在时某条规则**静默地什么都不比**」—— 纯信息(逐文件计数)、能力边界(`Cast<T>` 的模板参数)、已文档化的非缺陷方向(`check_patch_offsets.py` 的 1.5.x 故意跳过)、范围声明(`check_map_coverage.py` 明说不覆盖通用 map)、卫生提醒(`UNREACHABLE` 过期条目)、反向漂移(`inline constexpr` 不是导出符号),这六类都**不是**缺陷,不改;⑦五个变异全部 `pre-fix 0 → fixed 1 → reverted 0`;⑧**残余洞如实记下**:`check_exports.py` 仍不报告「导出了但没有任何常量点名」的符号、`EXPECTED` 是手工维护表(插件合法新增接口声明会先红)、`check_contract_copies.py:161-164` 在 manifest 缺失时 `failures.append` 后 **`return 0`** 提前返回跳过后续所有检查(不是空过但同形状) |
| §158 | **工作树 `CHANGELOG.md` 被截断损坏 —— 44 个门禁里只有 1 个会读它,而它是硬崩** | **恢复 + 改门禁**:①现象:**621282 字节 / 6574 行 / 3620 行非法 UTF-8**,首个坏字节在偏移 **40**(`\xe4\xbb\xa5`(`以`)被截成 `\xe4\xbb?`),`?` 字节总数 **12369**(正常副本 40),比正常副本**少 2435 行**含**整个 `## v1.1.6(2026-09-27)` 节**(一级标题从 11 掉到 10);②**已证伪**不是 GBK/cp936/mbcs 往返(三种编码往返重编码都 `equals disk = False` 且 `same length = False`)、不是「只损坏新加的行」(坏行从 `:3` 起含 HEAD 旧内容)、盘上无第二份副本、`git stash list` 为空;范围是全仓库唯一的(**2332 个受控文本文件只有它一个非法**,PITFALLS 1235910 字节 / 20549 行全合法);③**恢复源**:会话镜像 `.mut-mirror-full/CHANGELOG.md`(633619 字节 / 9009 行)是损坏前那一版,四条独立证据 —— 对 HEAD 是**纯超集**(镜像 `+3479 -0` vs 损坏副本 `+4707 -3662`)、会话记录的 **10 个行号锚点逐一吻合**、带着损坏副本丢掉的那一整节、损坏副本**没有独有内容**(可抢救的 2047 行里不在镜像里的 **0 行**);逐字节拷回后 `check_docs.py` 回到退出 0、全量门禁从 `43 passed, 1 failed` 回到 **44/44**;损坏版另存 `.changelog-damaged.bak`;④**mtime 铁证**:损坏 `2026-10-12 23:53:37`、镜像建立 `21:08:10`、子代理首次动门禁 `2026-10-13 00:01:13` —— 损坏比任何一次代码改动早 8 分钟;⑤**为什么没有门禁报出它(两半都不在退出码里)**:`check_docs.py` 是 44 个里**唯一**读 CHANGELOG 的,它 `read_text(encoding='utf-8')` 抛的是**未捕获的 `UnicodeDecodeError` 回溯**(既不指名文件也不说后果);其余约 40 处读文档用 `errors='ignore'`/`errors='replace'`,损坏的文档以「短了一截、安静了一截」的形态抵达规则,**规则照常打印 OK**;⑥**修法**:`check_docs.py`(355→412 行)新增 `check_text_integrity()` + `TEXT_SUFFIXES`(34 个后缀),语料 `git ls-files`,逐条 `read_bytes().decode('utf-8')`,坏就指名**文件 + 字节偏移 + 后果**;注册为 `CHECKS` **第一项**并在它失败后 **`break`**(唯一刻意偏离,否则后续规则用 traceback 换掉那行指名失败);`git ls-files` 本身失败也记失败(`refusing to pass vacuously`);**受控但已从工作树删除的路径跳过**(脏检出不是文本问题);⑦验证:clean 退出 0 且输出与改动前**逐字节相同**;变异 PITFALLS(`check_docs` 一条规则都不读它)⇒ 旧版 **0(盲)** 新版 **1**;变异 CHANGELOG ⇒ 旧版 1 但只是回溯、新版 1 且带指名失败行;无 git 仓库 ⇒ 1;真树语料 **2939 条目 → 2739 匹配后缀 → 2738 存在且解码通过**(唯一跳过 `Code/client/Games/Skyrim/AnimationExperiments.cpp`,受控但已删除);⑧**未覆盖如实记下**:43 个**无后缀**受控文本文件(`.gitignore`/`.editorconfig`/`LICENSE`/`Dockerfile` …)后缀规则够不到,要覆盖需按文件名的白名单;⑨教训:**控制台乱码既可能是终端假象也可能是真损坏**(判据是 grep 的 `(line is not valid UTF-8)` 与 `read_bytes()` 逐行 decode,不是肉眼看 pwsh);**`errors='ignore'` 让门禁对损坏免疫也失明**;**镜像未被 git 跟踪**,不会在 `git clean` 或全新 checkout 后存活,恢复必须显式拷回而不是指望 `git checkout --` |

| §159 | **「权威信号」的扫描停在了前缀上,而前缀认不出运行时** | **改代码 + 新增门禁**:①判据沿用 §115/§116/§120 的「同一个东西定义在两个地方,抽出来对照」,这一轮问的是「**同一个模块列表,谁还在扫**」—— 答案是两个翻译单元回答同一个问题(`Code/skse_client/main.cpp` 的 `GetSKSEModuleVersion()` 决定用哪个地址库,`Code/skse_bootstrap/main.cpp` 的 `IsLegacyGame()` 决定加载哪个 per-version 运行时 DLL),两边都扫 `skse64_` 前缀、都用 `swscanf_s(name + 7, …)` 取版本,**但只有前者把 `break` 写在了解析之外**;②**为什么前缀不足以认出运行时**:SKSE 目录里 `skse64_` 前缀的模块有**两个** —— 运行时 `skse64_1_5_97.dll`(名字带版本)与 `skse64_steam_loader.dll`(先于运行时被映射、给游戏 IAT 打补丁并挂 `WinMain`,**名字不带版本**),而模块枚举顺序不是我们能选的 ⇒「运行时恰好排在前面」不可依赖;③**后果不是少一级优化,而是把最可靠的一级换成最不可靠的两级**:`result` 为空时调用点退到 exe 版本资源(该文件自己的注释写着 cracked builds 上 `VS_FIXEDFILEINFO` 被盖成 `1.0.0.0`)与地址库文件名;④**本机实测**(`D:\game\SkyrimSE\`,1.5.97):loader 解析 `None`、运行时解析 `('1','5','97')`,改前循环 `resolved=''`,停在解析成功处 `resolved='1.5.97.0'`,而**若运行时恰好枚举在前改前循环也对** ⇒ 缺陷是**顺序相关**的,这正是它没被发现的原因(`st_boot.log` 里 `resolved='1.5.97.0'` 与 `exe version='1.5.97.0'` 相同,日志上分不出它来自哪一级);⑤修法:把 `break` 移进解析成功的分支,与 `IsLegacyGame()` 的写法一致(它用 `return` 停在解析成功之后,所以从来没这个缺陷);⑥新门禁 `Tools/Scripts/check_skse_module_scan.py` 四条静态断言(**两份实现各跑一遍**):仍匹配前缀 / 仍解析版本 / **停在解析成功处**(前缀之后解析之前的裸 `break` 与解析之后的裸 `break` **都算失败**)/ 循环真的在遍历;另有一条警告:`skse64_steam_loader.dll` 不再被点名时提示 —— 规则的理由已不在,应**显式删规则**而不是让它空转;⑦**变异实测 5/5 全抓**(恢复无条件 `break` / 把 `break` 提到解析之前 / 删版本解析 / 删前缀匹配 / 重命名函数);**第 2 条是第一版门禁漏掉的** —— 第一版只查「解析之后有没有裸 `break`」,把 `break` 提到解析之前就能通过,变异测试暴露后补上「解析之前」分支才全抓 |
| §160 | **四个插件的加载路径是 `noexcept` 且会分配 —— 而只有一份被修过** | **改代码(4 份副本 + 8 个入口)+ 新增门禁**:①判据是 §「同一类缺陷的**兄弟副本**」:仓库对「`noexcept` 帧里分配 ⇒ `std::terminate` ⇒ 无崩溃转储、无日志」已有**四轮**普查(产物 `Code/plugins/patches/STRPluginMessagingAPI/0004..0007`),这一轮问的是**范围** —— 上一轮修的是**一份**副本还是**全部**副本;②`LoadPluginModule` 在插件树里有**四份、分布在四个独立仓库**(`STRPluginMessagingAPI` / `AnimSyncTogether` / `DAVSyncTogether` / `TradeTogether`),四份都在 `noexcept` 里构造 `std::wstring` 并 `+=` 模块名,而 `STRPluginMessagingAPI/0005` 的标题里就写着 "…**and the load path**" —— 它修的是**它自己那一份**(§113/§116/§119 是同一错误的第二、三、四次,**这是第五次**);③第二个形状:`OstimNG-API-Thread.cpp` 里 **8 个入口**在头文件中声明为 `noexcept`,而 **OStimTogether 逐字复制了那份 vtable**(`ModThreadControl.h:108-193`)⇒ 承诺**不能单方面撤回**,8 个函数体在增长容器(`GetNavigationCount`/`GetNavigationOptions`/`NavigateToScene`/`RegisterEventCallback`/`RegisterControlCallback`/`SearchScenes`/`HasCompatibleNode`/`MigrateThread`)**一个 `catch` 都没有**;同一个仓库的**兄弟文件** `OstimNG-API-Main.cpp` 已因同一原因被修过(`patches/OStimNG/0003`,注释里写着 "an exception escaping a noexcept frame terminates the process with no crash dump")⇒ §109 的判据在**同一个仓库的同一个目录**里第二次不成立;④**修法必须落成 patch**:这些文件在钉住的 submodule 里,而 CI 每次跑 `git submodule update --init --force` ⇒ 工作树改动会被丢掉;四处修复写成 `patches/` 下普通内容(`STRPluginMessagingAPI/0009`、`AnimSyncTogether/0003`、`DAVSyncTogether/0004`、`TradeTogether/0003`,以及**扩充** `OStimNG/0007` + 拆出的 `OStimNG/0008`);**为什么扩充而不新建编号**:`OStimNG/0007` 被 `docs/PITFALLS.md` §109 的锚点**按文件名引用**,新建会让那条锚点指向一个不再包含它所描述内容的文件;**为什么 OStimNG 那部分拆成两份**:钉住的 blob 逐文件行尾不同(`OstimNG-API-Thread.cpp` 是 CRLF,其余三个是 LF),而 `git apply` 的上下文行逐字节匹配 ⇒ 一份补丁文件不可能同时满足两边,故按目标文件拆开(0007 只含线程文件、0008 含其余三个),两半各自 `check --cached` 通过;⑤修法语义都落在**代码已有的失败分支**上:`LoadPluginModule` 失败落到「没加载」(模块不在 `Data\SKSE\Plugins` 时已有的行为,每个调用方都读作「消息 API 不可用」),OStimNG 那 8 个入口返回各自空答案、`MigrateThread` 通过 **`onComplete(-1, context)`** 上报(否则调用方等一个永远不会到的回调);⑥新门禁 `Code/plugins/tools/check_noexcept_allocations.py` **按性质查不按名单查**,四条规则:①`plugins/` 下**每一个**「`noexcept` 且函数体真的会分配」的函数都必须含 `catch`;②**每一份**会分配的 `LoadPluginModule` 都必须含 `catch`(按名字更严);③**提到该助手却在其中找不到定义的插件报失败**;④本文件**为之而写**的 12 个站点必须**真的被看见**(语料下限);**第一版门禁的语料是按「发现地点」写的 —— 这是同一错误的第五次**:它只有两个语料(每份 `LoadPluginModule` + `OstimNG-API-Thread.cpp` 的 `noexcept override`),实测 `noexcept` 且会分配的 **14 处在语料内、18 处在语料外**,对第一版完全不可见;第二版把规则 1 的语料换成**整棵插件树**并加规则 4,查到 **29 处**全部通过;**规则 4 立刻抓到两个站点**(`GetNavigationCount`/`NavigateToScene`)——它们在语料里但分配标记匹配不到(标记漏了 `std::vector<T> name =` 与 `std::string(x)` 两种写法),**没有规则 4,第二版会一边少看两个站点一边报 OK**;另有一个**假阳性**(`STRPMChatUiSuppressV3.h:FindNearestCallAfterXref`:`std::vector<std::uint8_t> snapshot;` 是默认构造不分配,唯一会分配的一步委托给 `Snapshot()` 而它自己含 `resize` 的 catch,调用方又整个包在 `try/catch` 里),第二版收紧标记后不再匹配,豁免表**留空而不删表**(用更窄的正则排除正是让真实站点漏看的做法:豁免看得见,窄正则看不见);⑦**变异实测**:第一版 5/5 全抓(去掉 AnimSyncTogether 守卫 / 去掉 facade 守卫 / 去掉 `GetNavigationCount` 的 catch / 去掉 `RegisterEventCallback` 的 catch / 重命名助手),第二版按新规则补测两条 —— **去掉第三语料外一个站点的 catch**(`STRPMChatUiSuppress.h:Snapshot`)、**把 `std::vector<...> conditions = ...` 改成 `auto` 使已知站点不可见**,两条均被抓,验的是「门禁真的比第一版看得更多」而不只是「它还认识原来那 12 个名字」;⑧验证:`plugin_patches.py verify` → `PATCH VERIFY OK`、**44 份 patch**(改前 39),全量门禁 **46/46**(改前 44) |

**门禁可靠性有三个各自独立的问题,三个现在都有对应做法**:

0. **三条现在都由 `Tools/Scripts/check_gate_integrity.py` 机械检查**(§149)—— 下面 1/2/3
   写的是做法,那个门禁写的是判据:1 ⇒ R3/R4/R5,2 ⇒ R1/R2(**「无法检查时返回 0」**),
   3 ⇒ R6(以及「两个方向的计数必须同时报出来」)。**这三条此前只是文字,而文字不会失败。**
1. **它会跑吗** —— 新门禁必须接进 CI 的 `push` **和** `pull_request` 两条
   `paths` 触发列表,并各自有运行步骤。§112.5 的教训:上一轮的门禁加好了、
   本地跑绿了,但**根本没被 CI 调用**。
2. **它会失败吗** —— 每个新门禁都做**变异测试**,并且**先断言变异真的生效**
   (§113.4 的教训:第一次变异因沙箱拒写而没生效,门禁跑的是没改过的文件,
   打印 OK,差点被误判成「门禁不行」)。
3. **它看全了吗** —— 扫描范围写成**显式清单**,并让「清单里的东西贡献了 0 项」
   本身失败。否则子模块重组之后门禁会**静默退化成空转,却照样打印 OK**。

**仍未关闭的实机验证项**(都需要 CI 出包):

- 崩溃转储:§112 证明的是**它不再被构建宏关掉**、且文档与代码一致;
  「转储确实能写出来」要制造一次崩溃才知道。
- NPC 复制修复(§110.2):判据是「WE23 的『拾荒者』不再每轮往返复制一份」。
- `--generate-config` 的跨 DLL 导出签名(§110.6):runner 与 server 必须
  **同一次构建**产出,版本握手会拦下混用,但这条只有出包后才能真正验证。
- 1.6.x/1.7.x 上「脸看不见」的实机复现(第 2 节)。

**已知但未做**:(已关闭,见 §131)`ExtraDataList.h` 在 `SKYRIM_TARGET_LEGACY` 下分支却没有自己的
`static_assert`,其尺寸只被 `TESObjectREFR` 的 `sizeof` 断言**间接**覆盖(§111.7)。
§131 已补上两分支的直接断言(`data`/`bitfield` 的 `offsetof` + `sizeof`),
并加了门禁 `Tools/Scripts/check_branch_layouts.py`,使「两分支布局必须被断言」从 §0 的
**勾选项**变成会跑的检查。

---

## 5. 待验:1.5.3 / 1.5.16 / 1.5.23 上 id `16113`(`TGetArmor`)的 RVA 未恢复

**状态**:开放。影响且**只**影响这三个 pre-AE 运行时(1.5.39 及以上都带这一行)。
**引入**:自这三张 map 生成起就有,`docs/PITFALLS.md` §130 首次发现。

### 5.1 缺口是什么

`ExtraContainerChanges::Data::GetArmor` 经 AE 地址库 id `16113` 解析,而
`GameFiles/Skyrim/SKSE/Plugins/versionlib-ae-to-se-1-5-{3,16,23}-0.map` **没有这一行**,
另外七张有且值随构建而变:

| 运行时 | `16113` 的 RVA |
| --- | --- |
| 1.5.3 / 1.5.16 / 1.5.23 | **缺**(本缺口) |
| 1.5.39 | `0x1e7750` |
| 1.5.50 / 1.5.53 / 1.5.62 | `0x1e7760` |
| 1.5.73 / 1.5.80 / 1.5.97 | `0x1e7570` |

未映射的 id 在 legacy 运行时走 `VersionDbPtr::GetPtr` 的桩(`GetUnresolvedStub()`,
七字节 `xor rax,rax; xorps xmm0,xmm0; ret`)⇒ **每次调用返回 0**,所以
`GetArmor(32)` 恒为 `nullptr`。

**已经就地修的**是后果,不是这个缺口:`ExtraContainerChanges::Data::CanQueryArmor()`
用 `FindAddressById(16113)` 直查,`InventoryService::RunNakedNPCBugChecks` 因此在那三个运行时上
**不**再执行 `EquipOutfit()`。也就是说当前它们在行为上落在「**不修**」而不是「**修错**」这一侧。

### 5.2 为什么本机关不掉

**根因已经实测确定,不是推测**:map 是**穿过 pre-AE id 命名空间的翻译**——
一个 AE id(`16113`)命名一个 pre-AE id(`15873`),而每个 pre-AE 库把那个 id 拼成**自己构建**的
RVA。七张**有**这一行的表逐一验证了这个规则:

| 运行时 | `version-<构建>.bin[15873]` | 该表的 `16113` |
| --- | --- | --- |
| 1.5.39 | `0x1e7750` | `0x1e7750` |
| 1.5.50 / 1.5.53 / 1.5.62 | `0x1e7760` | `0x1e7760` |
| 1.5.73 / 1.5.80 / 1.5.97 | `0x1e7570` | `0x1e7570` |

**七比七全等**,而 `version-1-5-{3,16,23}-0.bin` 里**没有 `15873` 这个 id**:

```text
  1-5-3-0      ids= 791088  shared_with_97= 775712  only_in_97=  2962  only_here= 15376
  1-5-16-0     ids= 794077  shared_with_97= 777719  only_in_97=   955  only_here= 16358
  1-5-23-0     ids= 794170  shared_with_97= 777781  only_in_97=   893  only_here= 16389
```

即 pre-AE 的 id 空间是**演进**而不是只增:1.5.3 的表**比** 1.5.97 的**更大**(791,088 > 778,674,
多出 15,376 个 id),同时**少了** 1.5.97 有的 **2,962** 个 —— `15873` 在其中。
所以:

- 没有可翻译的值,**没有任何 override 能提供它**(值必须逐构建量,上表三档已经证明);
- **重新生成也补不上** —— 生成器 `gen_se_map_from_history.py --se-bins-dir` 只能译出
  **那个库命名过的 id**;
- `0x1e7570`(1.5.97 的 RVA)**不是** `version-1-5-3-0.bin` 列出的任何偏移;
- **不许猜**:缺了,调用落到零返回桩 —— 行为错但进程是活的;猜错,就是**调用一个无关函数**。

所以这三行**故意留空**,由 `Tools/missing_ids_by_runtime.tsv` 声明,
由 `Tools/Scripts/check_map_coverage.py` 保证「声明」和「实际的缺口」始终一致,
并保证另外七张**命名的是同一个符号**(§130.4 第 5 条)。

### 5.3 关闭它的两条路

**(a) 有这三个构建的话,量三个 RVA**(最彻底)

把任一 1.5.3 / 1.5.16 / 1.5.23 的 `SkyrimSE.exe` 放进工作区,查它在 1.5.97 上对应的那个函数
(`0x1e7570`,即 `ExtraContainerChanges::Data::GetArmor`),取该构建里同一函数的 RVA,
然后:

1. 在三张 map 里补一行 `16113 <rva>`;
2. 从 `Tools/missing_ids_by_runtime.tsv` **删掉对应的三行**;
3. `python Tools/Scripts/check_map_coverage.py` → 仍须 `MAP COVERAGE CHECK OK`
   (删了声明而 map 已带上,两个方向都要求一致,所以这一步**既是校验也是确认**);
4. `Code/client/Games/Skyrim/ExtraData/ExtraContainerChanges.cpp` 的 `CanQueryArmor()`
   **不要删** —— 它在别的运行时上仍是「没映射」与「映射到了但为空」之间的唯一判据。

**(b) 不投入这三个构建的话,接受这个降级**(当前状态)

判据是「三个运行时上不再有错误的 `EquipOutfit()`」,而不是「自动修复仍然生效」。
若走这条路,本节可以改成记录「有意不修」并从待验清单里去掉 —— 但要**留在这里**,
因为「`IsWearingBodyPiece()` 在这三个运行时上恒假」这件事本身没有被消除,
下游若新增读者仍会踩到(`check_map_coverage.py` 的 `JUDGED` 表会要求新读者先被读一遍)。

### 5.4 顺带该做的一件小事(本轮已做)

`Tools/ida/README.md` 的「Regenerating the shipped maps」一节只写了**怎么跑**生成器,
没有写它的**输出上限**:一张 map 能带的 id 只能是**那个构建自己的库命名过的** id。
本条缺口正是这个上限的结果,而 README 读起来像是「重跑一遍就能重建全部十张」。
现已在那一节补上这个前提(以及 §130.3 的七比七验证),下一个补行的人不必再自己推一遍。

---

## 6. 待验:lite(ESL)文件是否被 `Mod::IsLoaded()` 丢掉

**状态**:开放。**影响每一个装了 ESL 的玩家**;而在 1.6.1130+ 上,`_ResourcePack.esl`
(随游戏本体出货、且 `DiscoveryService::IsDefaultModlist` 的期望列表里就有它)**本身就是 ESL**,
所以这条在 AE 上不是「某些人」的问题。
**引入**:自认证包构建那一天起就有,`docs/PITFALLS.md` §133 首次把它变成可判定项。

### 6.1 缺口是什么

客户端认证包里那份 mod 列表,是**唯一**让服务端认识客户端 mod 的东西 —— 服务端的
`ModsComponent` 用它发号,客户端回来再用 `ModSystem` 把服务端 id 映射回本机 form id。
而它的成员资格由这一个判断决定:

```cpp
// Code/client/Games/TES.h
[[nodiscard]] bool IsLoaded() const noexcept { return standardId != 0xFF; }
```

`standardId == 0xFF` 是引擎的「**不在标准空间里**」。而 **lite(ESL)文件按定义就不在标准空间里** ——
它的 form id 前缀是 `0xFE000000 + liteId * 0x1000`,由 `IsLite()`(flags 第 9 位)决定,
`GetId()` 走的是另一条分支:

```cpp
[[nodiscard]] uint16_t GetId() const noexcept { return IsLite() ? liteId : standardId; }
```

**所以 `standardId != 0xFF` 对 lite 文件到底是真还是假,本仓库里没有任何地方能回答。**
引擎头文件不在这个树里(`Code/components/es_loader/TESFile.h` 是**本仓库自己**给服务端写的
加载器模型,它把 `m_standardId` 与 `m_liteId` 分开赋值、压根没有哨兵,所以它不能回答**引擎**的行为),
本机也不能联网查。

### 6.2 两个答案的后果相反,所以不能猜

| 答案 | 后果 |
| --- | --- |
| lite 文件**通过**(`standardId` 是 `0xFE` 之类) | 下面的 lite 路径正常工作:`ModSystem::HandleMods` 填 `m_liteToServer`,`GetServerModId` 用 `0xFE` 分支解析 ESL form id |
| lite 文件**被丢掉**(`standardId` 就是 `0xFF`) | 这个 ESL 服务端**从来不知道**:它加的每个 form 都拿不到服务端 id(静默不同步);而开了 mod policy 的服务器还会反过来要求玩家「安装」一个他已经装了的 mod —— **一个他无法解决的拒绝** |

第二种后果与 §132 修掉的那条是同一个形状(玩家被要求处理一个不存在的问题),
但根因不同:那次是**方向编码撞了合法 id**,这次是**过滤器可能吃掉了整个 ESL 空间**。
两条的共同点也值得记下来:**它们都只对「第一个 ESL」敏感** ——
`liteId` 从 0 发号,而 1.6.1130+ 上第一个 ESL 就是随游戏出货的 `_ResourcePack.esl`。

### 6.3 不猜,改成一行的证据

**做法是让用户日志自己回答**(与第 1 条同一套路,理由也一样:本机没有证据,而日志是最便宜的
证据来源)。认证包构建循环现在把**它丢掉的每个文件**报出来,并带上决定这个问题的三个字段:

```cpp
spdlog::info("mod '{}' is not loaded by the standard id rule: isLite={} standardId={:#04x} liteId={:#06x}", ...);
```

并在循环之后给出**一行汇总**,这一行就足以在**两个方向**上判定:

```text
mod list: N reported (L lite), K dropped by the standard id rule
server mod list: M entries, S lite; lite mappings established: T, not found locally: U
```

- **`L > 0`** ⇒ lite 文件通过 ⇒ **缺口关闭**(第 2 条那种后果不成立);
- **`L == 0` 而丢掉的那几行里有 `isLite=1`** ⇒ lite 文件被丢掉 ⇒ **确认缺陷**,
  按 §6.4 修;
- `S > 0` 而 `T == 0` ⇒ 服务端那边有 lite、客户端一条也没建立映射 ⇒ 同一个缺陷的另一面。

**这两条必须在 `info` 级**:`Code/client/TiltedOnlineApp.cpp` 里
`rotatingLogger->set_level(spdlog::level::debug);` 是**注释掉的**,所以出货构建的等级是 `info`,
写成 `debug` 就等于写进了没人看得到的日志。
`Tools/Scripts/check_lite_mod_path.py` 把这条**当成判据**:

| 判据 | 为什么 |
| --- | --- |
| 丢掉文件的那条日志存在、点名 `isLite/standardId/liteId`、且**不是** `debug`/`trace` | 少了它,问题重新变成不可判定;等级写低了,等于没写 |
| 两侧的计数还在报 | 没有计数就只能看到「有文件被丢」,看不到「丢的里面有没有 lite」 |
| lite 路径本身还接着(`m_liteToServer.emplace` / `hiByte == 0xFE` / `m_liteToServer.find`) | 就算答案是「通过」,这条路径断了也一样坏 —— 这条判据与答案无关 |
| `Mod` 的 `IsLite()`/`GetId()`/`standardId != 0xFF` 与两条 `static_assert(offsetof(Mod, ...))` 还在 | 日志读的就是这些;改名会让它要么编不过,要么打印别的字段 |
| 本节仍存在且仍点名该门禁 | **缺口与它的仪器一起退休**,不许只删一个 |

### 6.4 如果答案是「被丢掉」,修法是哪一条

不是「把 `IsLoaded()` 改成恒真」—— 那会把**被禁用**的插件也报给服务端,服务端于是认为你有
你没装的东西(mod policy 反向误报)。要按「这个文件到底有没有被加载」来问,而 lite 文件的
`liteId` 在未加载时无法与「第一个 ESL 的 `liteId == 0`」区分,所以**先要有证据**:
拿到日志确认之后,按 `ModManager` 里那个数组的真实语义(它是否只装已加载文件)决定是
(i) 对 lite 文件改用「是否出现在 loadorder 中」,还是
(ii) 换一个能表达「已加载」的引擎字段(那时需要新的实测偏移)。
**在此之前不改行为** —— 改成一个同样没有证据的写法,只是把不确定性换个地方放。

---

## 7. 已决:游戏菜单打开时,overlay 不隐藏 —— 死通道已删

**状态**:**已完成**(2026-10-12)。选了 §7.3 表里的**「删掉守卫」**那一条。
**引入**:上游的 overlay 就有这个守卫;§141 把它从「没人知道」变成「可判定 + 已记录」;
本轮由产品决定收尾。

### 7.1 事实(逐条可查)

```html
<!-- Code/skyrim_ui/src/app/components/root/root.component.html, 已删除 -->
<ng-container *ngIf="!(menuOpen$ | async)">      <!-- 只包住 app-debug 与 app-group -->
```

`menuOpen$` ← `ClientService.openingMenuChange`(`BehaviorSubject(false)`) ← `onOpeningMenu(boolean)`
← `skyrimtogether.on('openingMenu', …)`。而 **`openingMenu` 这个事件没有任何产出者**:

| 检查 | 结果 |
| --- | --- |
| 全仓(`Code/`,任何扩展名)搜 `openingMenu` | 只出现在 `root.component.ts`、`client.service.ts`、`typings.d.ts` —— **UI 侧三处** |
| `Code/client/**` 与 `Code/tp_process/**` 的 `ExecuteAsync("…")`/`EmitEvent("…")` | 24 个事件名,其中**没有** `openingMenu` |
| mock(`app/mock/skyrimtogether.mock.ts`) | 也不发它 —— 所以**连浏览器里 mock 也看不到这个行为消失** |

⇒ `openingMenuChange` 永远是 `false`,守卫永远通过:**overlay 在游戏菜单打开时不会被隐藏。**

**那现在是什么在隐藏 overlay?** 只有 `OverlayClient::SetUIVisible(false)`,而它只有两个调用点:
`revealPlayers` 的处理(§134 的既有写法)与 `deactivate` 的处理(§134 补的)。
**没有任何菜单钩子**:`UI::GetMenuOpen`(id 82074)存在,但只有 `DebugService` 用它;
`MapMenu`/`SkillsMenu` 虽然声明了 `MenuOpenCloseEvent` sink,那是为了它们各自的补丁。

**一处措辞订正**(§141 与 §141 那版的本节都写错过):这个守卫**不是**「包住整个 overlay」。
它只包住 `root.component.html` 里的 `<app-debug>` 与 `<app-group>` 两个组件 ——
聊天窗(`app-window.app-root-chat`)、通知(`app-notifications`、`app-notification-popup-container`)、
菜单(`app-root-menu`、`app-root-status`)与弹窗层(`app-root-popups`)**都在守卫之外**。
所以就算产出者存在,菜单打开时消失的也只是 F2 调试面板与队伍列表,不是整个 overlay。
这个错误让「实现产出者」的代价被高估了(原表写「只要开任何菜单整个 overlay 都会消失」),
从而把天平推向「不动」;订正之后两条路的代价都不大,删掉死通道成为更省的选择。

### 7.2 决定:删掉死通道

两条路的**运行时**代价都不大(见上),所以按「不留一个没人知道是空的行为」取舍:

- **实现产出者**(钩 `MenuOpenCloseEvent` → 发 `openingMenu`):要让 F2 调试面板与队伍列表
  在菜单打开时消失 —— 而队伍列表恰恰是玩家一边翻背包一边要看的东西,方向本身就存疑;
  还要先定义「哪些菜单算 menu」,是净增的未决问题。
- **删掉死通道**(守卫 + 监听 + subject + 类型):运行时**零变化**(`!(false)` 恒真),
  代价是丢掉 UI 侧那句已经写好的意图;真要做时重写这条通道只是十几行,而这十几行的**存在**
  会让下一个人以为「菜单隐藏已经实现了」。

⇒ **选删**。判断依据不是「哪个更省事」,而是**空行为的存在成本**:一个被记录的空行为比一个
不存在的功能更容易被误认为已实现 —— 而它连 mock 都不发,浏览器里也看不出差别。

### 7.3 已同步改的地方(逐条对账)

| 改什么 | 落点 |
| --- | --- |
| 守卫与注释 | `Code/skyrim_ui/src/app/components/root/root.component.html:2-16` —— 守卫删除,换成解释这次决定的长注释 |
| 订阅字段 | `Code/skyrim_ui/src/app/components/root/root.component.ts` 删 `menuOpen$ = this.client.openingMenuChange.asObservable();` |
| 监听 + subject | `Code/skyrim_ui/src/app/services/client.service.ts` 删 `skyrimtogether.on('openingMenu', …)`、`off('openingMenu')`、`onOpeningMenu`、`public openingMenuChange` |
| 类型 | `Code/skyrim_ui/src/typings.d.ts` 删 `OpeningMenuCallback` 与两条 `event: 'openingMenu'` 重载 |
| 门禁豁免 | `Tools/Scripts/check_overlay_bridge.py` 的 `NO_EMITTER` 删掉 `openingMenu` 那一行;说明写进 `:59-78` 的注释块 |
| 记录 | 本节(已完成)、`docs/PITFALLS.md` §141 与 §151、`CHANGELOG.md` v1.1.9 |

**门禁自证**:`check_overlay_bridge.py` 的第 5 条判据是反向的 ——
「`NO_EMITTER` 豁免的名字若不再被任何 UI 订阅 ⇒ 失败」。删掉监听之后,如果只是把豁免留在原地,
门禁会以 `NO_EMITTER exempts "openingMenu", but no UI code subscribes to it; drop the exemption`
失败;如果只删豁免、留着监听,又会以 `client.service.ts subscribes to "openingMenu", which no C++
code emits` 失败。**两边必须一起删** —— 这是这次删改唯一的机械保证。

### 7.4 以后要接回来

要重新实现「菜单打开时隐藏 UI」,照 §134/§141 的顺序做:先定哪些菜单算 menu、
哪些组件豁免,再在游戏侧 `MenuOpenCloseEvent` sink 里发事件、UI 侧加回监听。
**不要**只把 `root.component.html` 的守卫加回来 —— 没有产出者的守卫正是本轮删掉的东西,
而 `check_overlay_bridge.py` 会拦住这个半成品。


