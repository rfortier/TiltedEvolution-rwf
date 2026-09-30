[English](README_EN.md) | 中文

# Skyrim Together Next

[![Build windows](https://github.com/komAAmok/SkyrimTogetherNext/actions/workflows/windows.yml/badge.svg)](https://github.com/komAAmok/SkyrimTogetherNext/actions/workflows/windows.yml)
[![Discord](https://img.shields.io/discord/247835175860305931.svg?label=&logo=discord&logoColor=ffffff&color=7389D8&labelColor=6A7EC2)](https://discord.gg/skyrimtogether)

> ## ⚠️ 推荐游戏版本: **1.5.97**
> ## 📦 推荐 Mod 版本: **1.1.1** ⭐ **稳定版**
>
> 1.5.97 是本框架验证最充分的版本,1.0.41 起修复了部署与联机崩溃问题。
> **1.1.1 是指定为稳定版的 Mod 版本**:它经过三轮审计,是目前实机验证最充分的
> 构建。更新的版本(1.1.2 ~ 1.1.4)照常提供、也照常可以联机,只是尚未被指定为
> 稳定版;想要跟进新功能可以用新版本,想要稳定就留在 1.1.1。
> 使用其它游戏版本或更旧的 Mod 版本出现的问题,请先升级到上述版本再反馈。

本仓库旨在 Skyrim Together 开源框架的基础上,提供更多游戏版本和Mod(Mod整合包)的兼容,更丰富的联机插件生态,以达到更沉浸的联机体验。

本仓库在上游 [TiltedEvolution](https://github.com/tiltedphoques/TiltedEvolution) 基础上扩展了多项实用能力:

## ✨ 本仓库特性

- **多游戏版本支持**:1.6.1170 / 1.6.640 等 1.6.x 全系、1.7.x 新版本(1.7.99/1.7.104)、以及**老版 1.5.x(含 1.5.97)**,地址库与 ID 映射表全部随包附带;
- **MO2 + SKSE 无缝启动**:自带 SKSE 插件,通过 Mod Organizer 2 正常启动 `skse64_loader.exe` 即可加载本 mod,并带**自部署运行时**——首次启动自动把所需文件部署到游戏根目录,无需任何手动复制;
- **图形化安装引导(中英双语)**:mod 包内置 FOMOD 安装向导(MO2 原生支持),第一步先选中文/English,后续步骤与文案随之切换;联机所需文件自动安装,只把真正需要你选择的东西交给你勾选——两个框架选项(1.6.x 的启动校验脚本、独立启动器),外加**六个可选插件**,每个都带一句说明,一个都不装也不影响联机;
- **六个可选联机插件内置**:OStimTogether(动作场景同步)、MorphSyncTogether(体型与覆盖层同步)、IEDSyncTogether(装备展示同步)、AnimSyncTogether(动画图谱变量与事件同步)、DAVSyncTogether(Dynamic Armor Variants 外观同步)、TradeTogether(玩家间物品与金币交易)随同一个包分发,在安装向导里按需勾选,不需要单独下载、也不需要自己开放或转发端口;每个插件的附属项(OCum、PPA、体毛覆盖包等)同样在向导里展开;
- **服务器图形控制面板**:Windows 专用服务器带精简 GUI(状态/在线人数/日志/启停按钮),`--nogui` 可回到纯控制台模式;
- **局域网联机开箱即用**:网络层为纯 UDP/GameNetworkingSockets,不依赖 Steam,支持局域网、Radmin LAN、Hamachi、公网 VPS/Docker 等任意组网方式;
- **Release 自动构建**:推送 tag 即在 GitHub 云端完成 Windows 构建并发布两个包——`SkyrimTogetherNextMod-<版本>`(客户端 mod)与 `SkyrimTogetherNextServer-<版本>`(专用服务器)。

## 🚀 快速开始(玩家)

1. 从本仓库 [Releases](../../releases) 下载两个 zip(版本号相同的 Mod 包与 Server 包);
2. **房主**:解压 Server 包,运行 `SkyrimTogetherServer.exe`(图形界面,默认监听 UDP 10578,记得防火墙放行);
3. **所有玩家(含房主)**:把 Mod 包通过 MO2"从文件安装"(会弹出安装向导),或手动解压到游戏 `Data/`;通过 MO2 启动 SKSE 即可;
4. 进游戏按 **F2** 呼出联机界面,填写 `<房主IP>:10578` 连接。

局域网/Radmin 联机图文说明见 [docs/LAN-RADMIN-GUIDE.md](docs/LAN-RADMIN-GUIDE.md)。

## 📦 支持的游戏版本

| 游戏版本 | SKSE | 说明 |
|---|---|---|
| 1.5.3 ~ 1.5.97(老 SE) | SKSE 2.0.x | 完整支持:见下方说明 |
| 1.6.317 ~ 1.6.1179 | SKSE 2.1.x / 2.2.x | 开箱即用(推荐) |
| 1.7.99 / 1.7.104 | 新版 SKSE | 开箱即用(format 5 地址库) |

> **1.5.x 支持说明**:1.5.x(含 1.5.97)已完整支持。AE ID → 1.5.x 偏移翻译表
> 随包附带 10 个版本,地址映射覆盖 **99.6%**(代码引用的 3082 个地址中已解析
> 3069),且全部 3698 条映射都能在官方地址库 `version-1-5-97-0.bin` 中找到对应
> 符号;剩余 13 个(近孪生兄弟函数、无调用者的模块、调试视图)运行时安全降级
> (空实现桩 / RTTI 空指针保护 / 跳过补丁),不会崩溃,清单与各自影响见
> [Tools/missing_1_5_97_ids.txt](Tools/missing_1_5_97_ids.txt)。
>
> 这 13 个里只有 `32883` 会被玩家察觉(远程玩家动画重播时不重置动画图);
> 其余是调试视图、调试器线程名与没有调用者的模块。**函数 id 与数据 id 的降级
> 方式不同**:未映射的**函数** id 会拿到"调用即返回 0"的桩,可以安全调用;
> 未映射的**数据** id 若照常解引用,读到的是那段桩自己的机器码,所以这类 id
> 一律改用 `FindAddressById` 显式解析、失败即降级。
>
> **1.5.x 结构体**:包内附带**第二个运行时 DLL**(`SkyrimTogetherRuntime_1_5.dll`),
> 它与 1.6.x/1.7.x 用的那个从同一份源码编出,靠 `SKYRIM_TARGET_LEGACY` 条件编译
> 1.5.x 的结构体布局(相较 1.6.x 普遍相差 8 字节——1.5.x 的 `ExtraDataList` 没有
> 虚表;`TESObjectCELL` 另有三个尾部成员缺失,差 32 字节)。SKSE 引导插件按游戏
> 版本自动挑一个。布局由 `static_assert` 在**编译期**逐个校验,不是"按 1.6.x 编
> 译再赌运行时对不对"。

## ⚠️ 已知冲突的 Mod

以下 Mod 与本框架存在冲突,安装后会引发闪退、界面异常或功能失效,**建议不要同时启用**:

| Mod 名 | 链接 | 中文别名 |
| --- | --- | --- |
| Floating Subtitles | https://www.nexusmods.com/skyrimspecialedition/mods/154424 | 浮动字幕 |

> **Floating Subtitles(po3_FloatingSubtitles)**:该 Mod 的 trampoline 桩跳转到未分配内存页,
> 会在进入房间 / 准备联机时直接导致游戏崩溃。如遇进房闪退,请先禁用此 Mod 再试。
> 若你确实需要字幕类功能,可改用其它实现,或等待上游修复。

## 🧩 支持的整合包

以下整合包已做过联机适配验证,可直接在其基础上启用本框架:

| 整合包名 | 链接 | 稳定性 |
| --- | --- | --- |
| 神话整合 | https://magicskyrim.net/ | 测试中 |
| Nolvus v6 | https://www.nolvus.net/ | 测试中 |

> 遇到闪退请先按"反馈问题"附上日志。
> 若与本框架的行为变量替换冲突,会打印 `BehaviorVar::Patch` 相关日志。

## 🐛 反馈问题

请在仓库的 "Issues" 页面提交,附上可复现步骤、游戏版本、SKSE 版本与服务器日志,详细的报告对开发非常重要。

**日志位置(游戏根目录下):**

| 日志 | 路径 | 说明 |
| --- | --- | --- |
| 客户端日志 | `游戏根目录\logs\tp_client.log` | 联机核心日志:连接、同步、崩溃堆栈都在这里 |
| 界面日志 | `游戏根目录\logs\cef_debug.log` | F2 界面(CEF)相关报错 |

> 多开时客户端日志会依次命名为 `tp_client_instance_2.log`、`tp_client_instance_3.log`;
> 服务器日志为 `logs/STServerOut.log`。反馈闪退问题时请附上崩溃点附近的完整片段。

## 📜 版本更新日志

| 版本 | 日期 | 主要内容 |
| --- | --- | --- |
| 1.1.9 | 2026-10-01 | **修掉一整类「线上计数与实体数不一致」的缺陷,其中最严重的一条是未认证连接即可打崩专用服务器**:`Factions::Deserialize` 在阵营数超 511 时 `throw std::runtime_error`,而整条调用链(`AssignCharacterRequest::DeserializeRaw` / `CharacterSpawnRequest::DeserializeRaw` 都是 `noexcept` 虚函数,`CODE_GUIDELINES.md:47` 也禁止异常)异常逃出 `noexcept` 帧即 `std::terminate` —— 进程直接死、连崩溃转储都没有;判据是**解析发生在认证之前**(`GameServer::OnConsume` 先 `factory.Extract` 再查 handler,`GetByConnectionId` 判空在 handler 内部),所以任何未认证连接都能触发。同一族另两副面孔**合法流量**即可触发:写侧只写计数低位、循环却写全部条目(`Mods.cpp` 的 13 位计数配 `for (auto& entry : ModList)`;`Tints.cpp` 的 8 位计数**没有 clamp**),读侧于是只消费 `count` 条、剩余字节被当下一条消息解析,**整条流从此失步**;以及 `insert()`/`push_back()` 不清空导致复用对象上累积。现按仓库既有范式统一修法——读侧读完计数立刻 `std::min` 夹紧或早退、**绝不抛异常**,写侧写几条就只循环几条,共 **38 个 `Code/encoding/**` 文件**、新增 21 个上界常量(全部有出处:`kMaxActorValueCount=164` 即游戏的 `kActorValueCount`,`0xFF`/`0x1FF`/`0xFFFF` 即线上计数本身的位宽,`0x1000`/`0x40` 给真实数据留足余量);顺带修掉 `AnimationVariables::String_to_VectorBool` 位表外层循环不检查 `chars` 边界导致的**读越界**、两处 `push_back` 不清空、以及 `MagicEquipment::Deserialize` 在「这只手没有法术」时保留上一包法术(等于把收起来的法术又装回去)。另修掉**字符串缓存超过 65535 条后新玩家永久收不全**:发送侧原本把整份缓存切片发出去,而一条消息的计数只有 16 位,游标却**无条件推到了缓存末尾**,于是接收侧停在 65535、服务端却认为「这个人收全了」,**一条都不再发且永不自愈**(只有重启服务器才会好一阵子);现在按 16 位切一片、游标只推进真正写出去的那几条,并把同源的 `StringCacheUpdate::StartId` 从 16 位放宽到 32 位(该字段是 `uint32_t` 游标的隐式窄化点,VarInt 对 <65536 的值字节完全相同,故线上格式向后兼容)。**另修掉远端角色动画回放队列没有上界**:`RemoteAnimationComponent::TimePoints` 有两处入口(`CharacterService.cpp:689` 逐条 `push_back`、`AnimationSystem.cpp:104` 整批 `insert`)、却只有「队首到期就 pop」这一条出口,而这条出口被两道**正常状态**挡着——actor 解析不出来时调用点直接 `continue`、动画图未就绪时 `Update` 提前返回,于是远端角色刚 spawn、3D 或动画图还没就绪的那段时间里,队列以服务端 50 Hz 的速度只进不出、没有任何一处裁剪它;判据是同族的插值窗口早就有 `kMaxSamples = 8` 硬上界(注释「Keep the window bounded. Newest samples win」),现按同一策略加 `kMaxQueuedActions = 256`,并把裁剪放在**所有提前返回之前**,调用点也改成无条件调用;**另修掉服务端队伍相关的三处空指针**:`PartyService::GetPlayerParty` 是所有人查队伍的唯一入口,却用了 `m_parties[*JoinedPartyId]` 而不是 `find` —— `operator[]` 在 id 已失效时会**默认构造一个「成员为空、`LeaderPlayerId` 为 0」的假队伍并插进 `m_parties`**,于是所有按契约写 `if (!pParty)` 的调用方都拿到一个非空指针、把假队伍当真队伍用,同时留下一条永远清不掉的记录(同文件的 `GetById` 用的就是 `find`,同文件两种写法不一致即判据);`QuestService::OnQuestChanges` 拿到这个返回值后**全程没有判空**就取 `pLeader->...` 与 `GetQuestStageDedupHistory()`,而**已经在途**的任务更新遇上玩家退队/被踢就会以「没有队伍」到达(`GameServer::Kick` 只标记断开,真正的 `OnDisconnection` 要等下一轮事件循环),客户端那三处发送点虽然都带 `IsInParty()` 门禁也拦不住这种在途包 —— 同文件的 `OnQuestSceneChanges` 就写了早退保护;修法只把**依赖队伍的那部分**圈进 `if (inParty && pParty != nullptr)`,`pLeader`/`SceneMaster` 改用三目表达式,**任务日志记账留在块外**(单人/未组队玩家的事件同样要记录);`GameServer::SendToLeader` 则从未判过 `apLeader`,而它全仓唯一调用点传的就是上面那个可能为 `nullptr` 的 `pLeader`(队伍会先于玩家行存活到 `PlayerLeaveEvent` 跑完),现加早退并打 `"Party leader no longer exists, canceling broadcast."` **另修掉两处「空指针」与「状态被当成已发送」的缺陷**:客户端给远程角色生成脸部时,蒙版着色器分支里 `BSLightingShaderProperty::material`(裸 `void*`,蒙版着色器**可以没有材质**)与 `CreateTexture` 的返回值(**以返回空表示失败**)都不判空就直接写穿(`pMaterial->renderedTexture` / `pTexture->buffer`),而 `FaceGenSystem::Update` 在 `noexcept` 帧里 —— 同文件的 `GetHeadTriBasedGeom` 把这条链上**每一个**引擎返回值都判了空,只有蒙版分支这两处没有;现两处各自判空、先 `DecRef` 再跳过本帧。服务端 `CharacterService::ProcessMovementChanges` 则在实体循环**结束之后**对**全部**实体无条件 `Actions.clear()`,而「走过循环」不等于「送出去了」:实体因 `Sent == true` 提前 `continue`(**没有新移动、但队列里有新动作**)、或内层玩家循环里没有任何玩家在范围内(全部 `continue`)时,动作从未进过任何包却被清掉 —— 该队列全仓只有这里的读出与清空两个读者,清空即**永久丢失**,而同文件同族的 factions 早就改成了「送出才清」(`if (queued) SetDirtyFactions(false)`);现改用同一个 `queued` 模式,没人靠近时把积压裁剪到 32(与回放缓存的 `kReplayCacheMaxSize = 32` 同宽,超出丢最旧的); **另修掉一处「缓存跨存档活着」**:OStim 的 `AddonBridge` 用 `RE::FormID` 做一级键缓存远程玩家的 addon 状态(`_remoteOverlays`/`_remoteObjects`),而 FormID 在新存档里会被**重新分配** —— 这两个容器全仓只有写入(`:531`/`:617`)与读取(`:686-691`),**没有任何清理路径**,于是读档后旧条目会命中的是另一个角色。判据是 `plugins/OStimTogether/src/main.cpp:212-223` 的 `kPreLoadGame` 分支逐条清掉了 9 个单例(其中 `OStimBridge::ResetRemoteState()` 一次清 15 个 map、`MorphSyncService::Reset()` 清 3 个 map),**唯独它缺席**;现补 `ClearRemoteState()`(持 `_stateMutex` 清两个 map)并挂进同一串清理,修复以 `Code/plugins/patches/OStimTogether/0003-clear-the-remote-addon-cache-on-load.patch` 交付 —— 子模块不归本项目所有,而 CI 的 `git submodule update --init --force --recursive --depth=1`  ; **再补上上一轮空指针清扫漏掉的一整种形状**:上一轮的验收条件是 `'PlayerCharacter::Get()->' across Code/client: no matches, was 18` —— 那是一条**形状**判据(只覆盖「同一表达式内解引用」),而 `grep '= PlayerCharacter::Get();'` 在 `Code/**/*.cpp` 有 31 处全在它之外。逐个读完,只有三处真漏网:`MagicService.cpp:456` 的慢速法术 hack 用 `pActor = PlayerCharacter::Get();` **覆盖掉上面刚判过空的 `pActor`** 后 `:458` 直接解引用(同函数 `:433` 的 `pCaster = PlayerCharacter::Get();` 同样把已解析并判过空的施法者写成空,再经 `data.pCaster` 交给引擎 `RealAddTarget`),以及 `PlayerService.cpp:295` 的倒地补偿 —— 它在 `m_knockdownStart` 记下 **1.5 秒之后**才解引用那个指针,而这段时间里玩家可能已经回主菜单或去读档。两处路径都**不需要玩家存在**就能走到(`OnNotifyAddTarget` 由收包触发、队列每帧重放;`RunPostDeathUpdates` 每帧从 `OnUpdate` 调用),而 `PlayerCharacter::Get()` 在主菜单与读档期间为空。现三处都改成守卫式「拿到非空才覆盖」,并**不引入新的早退** —— 玩家不在时慢速法术留在已经判过空的目标身上,倒地补偿则把 `m_knockdownStart = false;` 留在 `if` 之外收尾,不至于每帧重试。 ; **另修掉一处「启动期的解析错误没有第二个机会」**:服务端命令行设置解析(`Code/components/console/CommandSettingsProvider.cpp`)用 `value.substr(value.find_first_not_of('-'))`,而 `substr(npos)` 会抛 `std::out_of_range`(参数是空串、或全由横线组成时 `find_first_not_of` 返回 `npos`);五个数值类型又用 `std::stol/stoul/stoll/stoull/stof`,对解析不了的输入抛 `std::invalid_argument`/`std::out_of_range`。而整条路径从 `main()` → `DediRunner` 构造函数 → `LoadSettingsFromCommand` **一处 `catch` 都没有**(全仓 `catch` 只有 8 处,全在别处),于是 `--uPort=abc` 或一个全是横线的参数会让服务器在开始监听之前就终止;判据是**写同样这五个字段的另外两条路径早就不抛**——`set` 控制台命令(`ConsoleRegistry.cpp:141-145`)与 `.ini` 载入(`IniSettingsProvider.cpp:122-126`)都走 `ConvertStringValue`(注释原文 "on error, you get the default"),只有命令行这条会抛,而它也是唯一一条**出错就没有第二次机会**的。现改为 `find_first_not_of` 返回 `npos` 时跳过该参数、五个数值改走同族早就在用的 `ConvertStringValue`(失败回落默认值),该文件 `std::sto*` 清零 ; **再修掉「查询队伍的接口自己会造出一个空队伍」这一族**:`Code/server/Services/PartyService.cpp` 里 `GetById()` 一直用 `find` 并判 `end`,而 `GetPlayerParty()`、`OnPartyChangeLeader`、`OnPartyKick`、`OnPlayerJoin`、`OnPartyInvite`、`OnPartyAcceptInvite`、`RemovePlayerFromParty` 七处用的是 `m_parties[...]` —— `operator[]` 在 id 不再解析时**静默插入**一个全新空队伍,而 `struct Party` 的 `uint32_t LeaderPlayerId;` 没有默认初始化,紧接着的 `if (party.LeaderPlayerId == player->GetId())` 就是读未初始化内存。同一族的第二处:`members.erase(std::find(...))` 没有配对 `end()`,跨队踢人时 `apPlayer` 本就不在 `members` 里,必然走到 `erase(end())`(未定义行为);而 `OnPartyKick` 原本只有「目标存在」和「我是队长」两道门,**没有一道门问过「目标在不在我的队里」**,`PartyMemberPlayerId` 又是从线里直接读的(`PartyKickRequest.cpp:11`)。现七处 `operator[]` 全部改为 `find`/`GetById` + 判空(只有 `OnPartyCreate` 里刚由 `m_nextId++` 生成的 key 保留下标),`RemovePlayerFromParty` 在 `erase` 前先判 `find` 结果,`OnPartyKick` 增加成员归属校验,并补上 `Code/server/Services/QuestService.cpp:223` 唯一没跟上 `GetPlayerParty()` 新契约的 `pParty` 判空(顺带删掉该 handler 里从未被使用的 `pLeader` 赋值行) ; **再修掉 OStimNG 里「注解字段数不是 2 就整段落进越界分支」这一族**:`plugins/OStimNG/skse/src/Core/Thread.cpp:849-856` 的 `OStimEvent` 分支只判了 `payloadVec.size() == 2`,其余情况全落进 `else` 去读 `payloadVec[1..3]`,而 `stl::string_split`(`skse/src/Util.cpp:47-62`)会丢掉尾部空 token(`""` 得到 0 个、`"a,"` 得到 1 个、`"a,b,c"` 得到 3 个),这条 `else` 就是唯一出口;注解的两种合法形状写在 `data/SKSE/Plugins/OStim/list of annotations.txt:14-19`(`OStimEvent.[event],[target]` 与 `OStimEvent.[event],[actor],[target],[performer]`),同一段里 9 处 `std::stoi` 没有任何保护,数字写错就抛异常,异常从 `RE::BSTEventSink` 的事件槽逃出去即 `std::terminate`;这段 payload 也不只来自本地动画图——`plugins/AnimSyncTogether/src/STRPMClient.cpp:436-437` 把网络上收到的字符串直接交给 `actor->NotifyAnimationGraph()`,唯一过滤是 `:394` 的 `SyncRules::ShouldSyncEvent(tag)`,远端客户端能决定它。判据就在同一个文件里:`Papyrus/PapyrusScenes.h:18-28` 的 `GetActor` 先判 thread 再判 actor,紧邻的 `:10`/`:15` 一个都不判;`Papyrus/PapyrusThread.h:279` 对 actor/target/performer 三个下标做了范围校验,`ProcessEvent` 这条同构入口不做。同族一并修掉的还有 `GetActor(actor)->` 十处、`getCurrentNodeInternal()` 两处、`NodeHandler::threadEnd()` 里 `delete this` 之后没人把 `Thread::nodeHandler` 置空、`LoopPeakHandler::speedChanged()` 的三重解引用加不判范围下标、`PeakSoundPlayer` 构造函数的三层解引用、`RemoveActor()` 在空容器上算 `size() - 1`。现新增 `parseAnnotationInt()`(`std::from_chars`,不抛),`OStimEvent` 只接受 2 与 4 两种字段数、其余丢弃,十处解引用改为入口处取一次的 `threadActor` 并判空,`NodeHandler` 的两处节点取用先落局部再判空、`threadEnd()` 在 `delete this` 前清掉宿主指针;以 `Code/plugins/patches/OStimNG/0002-guard-the-animation-event-path.patch` 交付(8 个文件、16 个 hunk);以及认证包里的日历字段:`TimeModel::Deserialize` 对 `Day`/`Month`/`Year` 只做 `& 0xFFFFFFFF`(对 `uint32_t` 是恒等操作)、对 `TimeScale`/`Time` 连 NaN/Inf 都不判,而 `Month` 直接成了 `DateTime::GetTimeInDays()` 的循环上界、`Day` 成了 `DateTime::Update()` 进位 `while` 的减法次数 ⇒ `Month = 0xFFFFFFFF` 让服务端在**接受进服的瞬间**空转约 42.9 亿次、`Day = 0xFFFFFFFF` 约 1.4 亿次,整个 tick 线程停住;同一段代码还会把客户端的时间**无条件**抄进服务端 `m_dateTime` 再广播给所有人,一个客户端即可改写全服日历。现按 `Factions.h` 的既有范式在结构体上声明边界(`kMaxTimeScale`/`kMaxTime`/`kMaxDay`/`kMaxMonth`/`kMaxYear`,取自服务端 `SetTimeScale` 的 `1000.f`、`SetDate` 的 `< 12`/`<= 999` 与 `std::fmod(Time, 24.f)`)并在读侧夹住,非有限值归零(`std::isfinite` 判不出就夹不进任何区间);同类的另一条是**血量增量**:接收端把它加到血量上、再用加完的结果判决死亡,所以 `-1e38` 就是一个**远程处决开关**,`NaN` 还会顺着 `ForceActorValue` 污染引擎侧的 actor value。现按同一范式在两个方向的结构体(`RequestHealthChangeBroadcast`/`NotifyHealthChangeBroadcast`)上声明 `kMaxHealthDelta = 100000.f` 并在读侧夹住,非有限值归零;同时把服务端账本的减号改成加号 —— 它与客户端 `+ acMessage.DeltaHealth` 的约定原本相反,会让每一次受伤都被记成加血并分发给新进服的玩家;actor value 的**键**同样是引擎表的下标,此前只夹了条目数没夹下标范围,越界键会直达 `SetActorValue` —— 现在读侧用 `ActorValues::IsValidActorValue` 同时判「下标 ≤ 163」与「值有限」,被丢弃的条目仍消费自己的字节以免整包错位; 客户端的 `TP_ASSERT` 既不 `abort` 也不返回失败,它只写一行日志(`Code/client/Utils.h:11-15`),所以四处「判过了」其实是活的空指针解引用 —— 抛射物句柄与 `Cast<Projectile>` 的结果(`Code/client/Games/Skyrim/Projectiles/Projectile.cpp:99-120`)、引擎额外数据里的 `EnchantmentItem*` 与 `EffectItem::pEffectSetting`(`Code/client/Games/Skyrim/TESObjectREFR.cpp:519-562`)、以及写回额外数据的 `SetEnchantmentData`(`:634-641`)现在都是真分支;后两条线上可达(`Inventory::Entry.ExtraEnchantId` 随 `NotifyInventoryChanges` 过线,`EnchantmentItem::Create` 与 `Cast<EnchantmentItem>(TESForm::GetById(...))` 两条分支都能给出空指针),而 `RTTI.h:17-27` 的 `Cast<>` 在 RTTI 未映射的旧版游戏上**恒**返回 `nullptr`;拿到空的抛射物指针时事件照发、只把 `Event.Power` 留在 `arData.fPower` 的初值上 —— 少发一发比发错数值更糟; 线上字符串此前**完全没有长度上限** —— `Code/encoding` 里 30 处 `ReadString` 只有 4 处带夹取,`Username`/`ChatMessage`/`Text`/`SoundFilename`/`AppearanceBuffer`/`ModList[i].Filename` 的长度全由对端决定,并且会继续流进 CEF、`TESNPC::Deserialize` 与 `SubtitleManager::ShowSubtitle`(一次 `Mods::Deserialize` 就能产出 65535 个任意长的文件名),现在统一走 `Code/encoding/WireLimits.h` 的 `Clamp`,上限取的是没有任何合法发送方能碰到的值;同时把服务端聊天转发里**每包重编译一次**的 `std::regex`(3 处构造点,`Code/server/Services/OverlayService.cpp:34`、`Code/server/Scripting/GameServer_Bindings.cpp:20`/`:29`)换成 `Code/encoding/ChatSanitize.h` 的单遍扫描 —— 原实现在 `noexcept` 的 `sendPlayerMessage()` 里构造可抛的 STL 类型,分配失败是 `std::terminate` 而不是丢一条聊天,而穷举 55998 个字符串的对拍证明新旧实现逐字等价;另外把两个面板在构造函数里对 localStorage 的 `JSON.parse` 收进 `StoreService.getJsonArray`(`Code/skyrim_ui/src/app/services/store.service.ts:34-48`)—— 那份列表的寿命比写它的构建长,解析失败会让面板整块不渲染而不是显示空列表; 另修掉**认证失败的 JSON 载荷是手工拼接的**:`Code/client/Services/Generic/TransportService.cpp` 把**服务端送来的**版本字符串与 mod 文件名直接写进引号之间,一个 `"` 就提前结束字符串、整个文档不再是 JSON,于是原因明明送到了却只剩「服务器没有理由地拒绝了连接」(`client.service.ts` 回落到 `no_reason`);现在统一走新增的 `Code/encoding/JsonEscape.h`,按 RFC 8259 section 7 转义引号、反斜杠与全部 < 0x20 的字节 —— 这些值已经过了 `WireLimits::Clamp` 的长度上限,文件名允许包含文件系统允许的任何字符,而一个需要转义的名字仍然是必须展示给玩家的名字,所以是转义而不是拒包。同一段里还有第二处:`case AR::kClientModsDisallowed:` 的 `if (acMessage.MO2Active)` 只包住了逗号那一行,`ErrorInfo += "\"MO2\"";` 在 `if` 之外**无条件**执行,只装了 SKSE 的玩家被告知要移除一个自己没装的 mod 管理器(服务端早在 `GameServer.cpp:896-897` 就把两个真实 flag 回传了);OStim Together 的会话表 `_ownerSessions` 只有三处写入、零处删除,唯一清空点在读档(`plugins/OStimTogether/src/main.cpp:216`),于是每个「加演员」菜单、每个直连会话、每次预检捕获都留下一条永不回收的状态,而 `plugins/OStimTogether/src/Input.cpp:163` 与 `plugins/OStimTogether/src/CoopSessionManager.cpp:249` 正好每次按键都要遍历整张表 —— 现在终态分支真正 `erase`,清理循环也从「只收直连会话」放宽为「收所有已结束的会话」;另外三处「读侧越过写侧已知上界」的缺陷也一并收口:认证包把 mod 计数放在 13 位里(可写 8191),而写侧最多只发 4096 条,读侧原先照对端给的数直接 `resize`(`Code/encoding/Structs/Mods.cpp:38-39`);服务器转发字幕时漏抄 `TopicFormId`,对端收到的话题恒为 0(`Code/server/Services/CharacterService.cpp:580`);`Status` 不在枚举内的 `RequestQuestUpdate` 会让服务器把一个未初始化的 `notify.Status` 转发给全队(`Code/server/Services/QuestService.cpp:109-116`),现在它只记一条日志就停下; 另外插件的最高频日志点一直在**同步刷盘**:`AnimSyncTogether` 的 `set_level(trace)` + `flush_on(trace)` 让**每一条** `info` 都立刻落盘(实测 10 分钟 43271 行、约 72 次刷盘/秒,其中 39834 行来自 `AnimationInputProbe.cpp:76`、3407 行来自 `AnimationProbe.cpp:228`,而这两条日志的 `tracked=` 全为 `false`、`remoteConnection=` 全为 `0`),现改为主程序同款 `spdlog::flush_every(1s)` + `flush_on(warn)`、两条每事件日志降为 `debug`,以 `Code/plugins/patches/AnimSyncTogether/0001-stop-flushing-every-log-line-and-demote-the-two-per-event-markers.patch` 交付 |
| 1.1.8 | 2026-09-28 | **修掉任意玩家一条聊天消息即可打崩专用服务器**:`Player::GetCharacter()` 是 `std::optional<entt::entity>`,在角色创建前为空,而 `OverlayService::HandleChatMessage` 把它**直接解引用**后交给脚本回调(旁边 `WeatherService`/`PlayerService` 两处同调用都判了空,只有这一处没判,与 v1.1.4 修的 `/settime` 同类);现只跳过脚本回调、不吞消息,该玩家的全局/队伍聊天照常送达。**并修掉 1.6.x/1.7.x 上「装好了但 F2 打不开、插件同步全死」的连锁缺陷**:`SkyrimTogetherSKSE.dll` 从未导出 `SKSEPlugin_Version`,而 SKSE 2.2.x 只认这个导出名、**没有 `SKSEPlugin_Query` 回退**,于是 `handle 0` 被跳过 → 运行时从未部署 → F2 永不出现;`STRPluginMessagingBridge.dll` 反过来缺 `SKSEPlugin_Query`,在 SKSE 2.0.x(1.5.97)上被判「does not appear to be an SKSE plugin」;三个门面 DLL 的 `compatibleVersions` 只写 `1.6.1170` 且非版本无关,导致 1.6.317/640/1130/1179 与 1.7.99/104 —— **每一个本包都带了地址库的运行时** —— 全被判「incompatible with current version of the game」。三者**在开发机(1.5.97)上全都看不见**,现改为版本无关声明(`kVersionIndependent_Signatures` + `kVersionIndependentEx_NoStructUse`)并补齐导出,新增 `check_skse_bootstrap.py` 与 `check_skse_entry_points.py` 把 SKSE 两族的准入规则**回放**在每个手写插件 × 每个随包运行时上(反向验证:还原补丁 → 5 条失败,改名导出 → 6 条失败)。**另修掉 MO2 引导页里 `OStim Together` 永远灰掉**:MO2 的 `fileDependency` 只比对 esp/esm/esl,上一版 `detectFile` 写成 `SKSE/Plugins/OStim.dll` 于是 Active/Inactive 两个分支都不可能命中、`NotUsable` 永远生效(**与前置装没装无关**),现改指 `OStim.esp` 并加门禁拒绝非插件后缀;同时修掉 OStim 拒绝文案比选择器旧一个版本(已接受 `7.5.1.x` 却仍称最新只到 `7.5.0.2`,被拒玩家会被建议去装更旧的版本),新增 `check_ostim_support_message.py` 把文案与选择器双向绑死 |
| 1.1.7 | 2026-09-27 | **修掉 v1.1.6 的远程编译全红(本地 13 个门禁全绿也照样红)**:三条流水线都死在同一步 `Build the OStimNG prerequisite` —— 那段 `run:` 里 `Write-Host "COMMONLIB_SSE_FOLDER=..."` **少了一个收尾引号**,PowerShell 对整块**先解析再执行**,于是整步在跑任何命令之前就退出 1、日志里一行 CMake 都没有(三条流水线复用同一段,所以一处笔误红三条);同一处还依赖 `plugins/OStimNG/skse/extern/CommonLibSSE-NG`,而 **OStimNG `f20db82` 已经把那个 gitlink 删掉**(它转向 vcpkg),断言路径在钉住的树里根本不存在 —— 现把 CommonLibSSE-NG 修订钉在 `plugins.json` 的 `commonLibSseNg`(选 `v8.1.0`,即 OStimNG `126fc52` 明确适配的那版),并改用 OStimNG **自己的 `--preset release`**(手写 `-A x64` 用不了 Ninja 生成器、也拿不到 `clib-util` 所需的 overlay ports,而该 port 不在上游 vcpkg),构建目录随之改为预设的 `skse/build/release`;`Plugin gates` 的 `Verify the plugin source snapshots` 则是**快照少了一个文件** —— OStimNG 自带的 `.gitignore` 第 553 行 `gfxfontlib.swf` 优先于根目录的 `!/snapshots/**`,文件**在本地存在却从未进过 git**,本地 verify 看工作区所以绿、CI 看干净检出所以红;现 `snapshot` 用 `git add --force` 暂存(唯一能无视 ignore 的机制)并记录被覆盖的规则,`verify` 改问「git 是否记录」而非「磁盘上有没有」;新增 `check_workflow_shell.py`(用 PowerShell 自己的解析器校验全部 35 个 pwsh 块)与 `check_ostimng_build.py`(清单与 CI 步骤必须都钉住),各反向验证过;顺带修掉 `plugin_snapshot.py update` 仍按「根目录有无 CMakeLists.txt」判断检出(导致 OStimNG 每次被静默跳过)、`UPSTREAM.json` 的 `upstream` 从 `.git/config` 读到空字符串(改读受版本控制的 `.gitmodules`) |
| 1.1.6 | 2026-09-27 | 修掉两处「刚进游戏就闪退」:法术/龙吼装备钩子在引擎用空形式清空槽位时读空指针(我们的运行时),以及插件按 RaceMenu 的**接口版本**门禁取用 SKEE 接口(此前版本错配会把 `GetOverlayCount` 调到 `RevertHeadOverlays`,读 `0x15` 崩溃);新增门禁校验 SKEE 声明顺序;并修掉**自定义种族的头不显示** —— 上游 #764 删掉了「每帧强制运行时生成脸」那一行,于是**头只以种族自带 `.nif`/`.tri` 存在、磁盘上没有烘焙 FaceGenData** 的角色头是空的;改成**只对「外观来自网络」的角色**清零该开关,存原值、关窗时还原,并新增 `Tools/Scripts/check_facegen_scope.py`(反向验证过十种改坏方式);**并把 OStimNG 作为 OStim Together 的前置纳入本仓库结构**(子模块 + `prerequisites` 登记,快照只备份代码),"1.5.x–1.7.x"由 `check_ostimng_versions.py` 逐条核对 33 个 `RELOCATION_ID` 在 10 个 pre-AE 与 14 个 AE 地址库里的覆盖;**修掉 OStimNG 在 1.5.80/1.5.97 上改坏一条 6 字节指令**;**修掉「OStim Together 没激活」**(根因是前置 OStim 没装:日志现在 `critical` 指名,向导改为依赖型检测);**修掉七个插件同步「装上就是死的」**(门面优先用**已加载**的框架运行时,只用 `GetModuleHandleW`、绝不 `LoadLibraryW`) |
| 1.1.5 | 2026-09-27 | 修掉插件补丁里写错的 fmt 占位符(CI 编译报 `error C7595`),并让联机界面辅助进程不再误加载 ENB 代理 DLL(此前会弹 KiLoader 初始化失败);推荐版本定为 1.1.1,并在版本表中标为稳定版 |
| 1.1.4 | 2026-09-26 | **同步上游 v1.8.2 并按本仓库多版本逻辑改写**:并入 #899(按“原始 NPC + 房主选中的模板”重建等级化 NPC,保住守卫姓名/默认装备等非继承数据)与 #893(排除各派系监狱储物容器,否则一个玩家的物品会进另一个玩家的箱子);两处 1.5.x 改写都不是猜的 —— `GarbageCollector` 单例(id `400329`)与 `SetLeveledCreature`(id `20231`)在 1.5.97 上**没有映射**,照抄上游会分别变成**解引用零返回桩**和**每帧 Disable/Enable 死循环**,已改为直查地址库 + `CanRecordPick()` 门控;修掉 `CommandService::OnSetTimeCommand` 里唯一一处未判空的 `GetByConnectionId` 解引用(**任意玩家一条 `/settime` 即可打崩专用服务器**);修正 `docs/COMPANION-PLUGINS.md` 中与实际出货值**完全相反**的传输描述并加门禁绑死;**覆盖率口径修正**:`collect_codebase_ids()` 原先漏扫全部 301 个 `.h`、又把注释掉的调用算作活的,现口径 **3069/3082(99.6%)** |
| 1.1.3 | 2026-09-26 | **插件生态落地**:把剩余三个插件按同一配方并入 —— `AnimSyncTogether`(动画图谱变量/事件同步)、`DAVSyncTogether`(Dynamic Armor Variants 外观同步)、`TradeTogether`(玩家间物品与金币交易),可选插件由 3 个增至 **6 个**,子模块、契约校验、打包清单、CI 构建、耐久快照、安装向导六处同步更新;删除 FOMOD 里那段**两条指引都是死路**的 OStim 提示(上游既无 release 也无 tag,该脚本又从不进包),中英文改为只描述功能;修复**自己引入的发布阻断** —— 两个插件 pin 的 vcpkg baseline 在 CI 的 `--depth 1` 克隆里取不到,而 vcpkg 的 builtin registry 路径**没有 fetch 回退**,已改为按 manifest 反推 baseline 再逐个 fetch;**OStim 的两个 Papyrus 脚本终于能编了**:改用开源编译器 `russo-2025/papyrus-compiler`(pin tag + SHA-256),补 12 个基础类型 stub(`plugins/` 是 gitlink,只能放本仓库),`OSKSE.pex` / `OStimTogetherNative.pex` 随包出货,**Add Actor 同意门控真正可用**;顺带修掉 `OStimTogether_OCum.esp` **出货却不带脚本**的缺口(补 4 个 `Form` + 1 个 `Game` stub,`OStimTogetherOCum.pex` 接入同一编译通道),并让打包脚本支持**子选项的 artifacts**(原先静默忽略);`GameFiles/Skyrim/scripts/source/` 的 10 个出货脚本也补上了可复现的编译入口,其中 `SkyrimTogetherVerifyLaunchScript.psc` 修掉编译器不支持的 `\n` 转义(它会**静默产出字面反斜杠**);归档 papyrus-compiler 源码到 `snapshots/tools/` 并加门禁 |
| 1.1.2 | 2026-09-26 | 四轮审计 + 插件层专项:修复服务端可被远程打崩(重复认证请求空指针)、`PlayerService`/`DiscoveryService`/`WeatherService`/`OverlayService`/`CalculateHealthPercentage` 等 20 余处空指针;插件层 **ProxyResolver 映射监听器从来没被触发过**(`OStimTogether`/`IEDSyncTogether` 都注册了),现已由 `PlayerComponent` 构造/销毁触发;`setLogCallback` 存而不用、服务端限流桶泄漏也已修;**快捷键只保留 F2**(右 Ctrl/F3/F4/F6/F7/F8 全部注释或禁用);还原被误删的 34 个地址库文件(1.5.x 与 1.6.x/1.7.x 全部在位) |
| **1.1.1** ⭐ **稳定版** | 2026-09-25 | 三轮审计收尾:修复 `VisitInteriorCell` 在整个 load 期间对空 cell 的链式解引用、`Actor::Create` 对玩家的连续三次解引用、`DebugService` 未判空就用的 actor;并修正 `Actor::Create` 的判空顺序(原会把已分配的 actor 泄漏) |
| 1.1.0 | 2026-09-25 | **性能与同步**:插值改 Catmull-Rom 三次曲线 + 有界外推(消除远端玩家的折线感与丢包时的冻结—跳变);帧循环间隔 16→8 ms,更新率约 32→64/s;移动更新由 O(更新数×实体数) 降为线性;修复重复生成同一远程玩家引发的引擎空指针崩溃;全仓库 `GetById` 解引用审计,修复 15 处无守卫解引用;**自部署「假失败」修复**:改为内容比对,mtime 仅作前置过滤,删除从未存在的 `.str_old` 清理(它把错误码污染成 `error 2`);定时器量化定量(请求 16 ms 实为 31.25 ms) |
| 1.0.41 | 2026-09-23 | FOMOD 安装引导中英双语 + 精简;修掉发布包内中文变乱码(盖章版本号时未指定编码) |
| 1.0.40 | 2026-09-23 | 跟随 `ff 25` 跳转桩,hook 冲突日志直接点名占用方 mod |
| 1.0.39 | 2026-09-23 | 削减每帧开销,解决加载慢与卡顿 |
| 1.0.38 | 2026-09-23 | 全项目审计并移除可证死代码 |
| 1.0.37 | 2026-09-23 | 收尾 F3 / i18n 遗留项,让 F3 复测可复现 |
| 1.0.36 | 2026-09-23 | 阻止 CEF 把 overlay 窗口降级(elevation 丢失) |
| 1.0.35 | 2026-09-23 | 1.5.97 读 `SkyrimVM::virtualMachine` 用 0x200,不是上游的 0x210 |
| 1.0.34 | 2026-09-22 | 修正 4 个错误的 1.5.x id 映射,并让 HookAudit 点名冲突 |
| 1.0.33 | 2026-09-22 | 修复 v1.0.32 的启动闪退:给定时器驱动的帧循环补上门控 |
| 1.0.32 | 2026-09-22 | 帧循环改由窗口定时器驱动,1.5.97 真正能同步 |
| 1.0.31 | 2026-09-21 | 连接结果弹窗、菜单常显、中文输入法、MainLoop 心跳 |
| 1.0.30 | 2026-09-21 | 让网络泵只做解析,分发交还游戏线程 |
| 1.0.29 | 2026-09-21 | 连接尝试自带泵,并把失败原因打出来 |
| 1.0.28 | 2026-09-20 | 从发起连接的那个线程开始连接,而不是 runner 队列 |
| 1.0.27 | 2026-09-20 | 让每次失败的连接尝试都能结束,并通知 UI |
| 1.0.26 | 2026-09-20 | 按 1.5.97 真正的偏移读 `SkyrimVM::inactive` |
| 1.0.25 | 2026-09-20 | 出错时让 overlay 退出「正在连接」状态 |
| 1.0.24 | 2026-09-20 | 连接稳定性:握手超时上限与可用的取消 |
| 1.0.23 | 2026-09-20 | 维护性发布 |
| 1.0.22 | 2026-09-19 | 冲突 mod 清单(Floating Subtitles)与英文 README |
| 1.0.21 | 2026-09-19 | 修复 1.5.97 点击「新游戏」闪退;留档 v1.0.18 兼容性审计 |
| 1.0.20 | 2026-09-19 | 同步上游 TiltedEvolution dev 后的首个构建 |
| 1.0.19 | 2026-09-19 | 合并上游 dev(等级化 NPC 同步 + 版本化归属);修复全部合并引入的编译错误;恢复 1.5.x 兼容与裸体 NPC 自愈;FOMOD 显示作者与项目 |
| 1.0.18 | 2026-09-16 | 修复 F2 overlay 状态;启用 1.5.97 字节补丁 |
| 1.0.17 | 2026-09-06 | 不再在游戏建好 Papyrus VM 之前读它 |
| 1.0.16 | 2026-09-06 | 用 VM 自己的虚表确认 Papyrus hook 打对了函数 |
| 1.0.15 | 2026-09-06 | 日志明确说明 Papyrus 注册 hook 是否执行过 |
| 1.0.14 | 2026-09-06 | 未注册的 Papyrus native 不再拖垮游戏 |
| 1.0.13 | 2026-09-06 | 失效跳转与共享 hook 目标会在日志里自我说明 |
| 1.0.12 | 2026-09-06 | 打通 1.5.97 启动链路;关闭未经验证的字节补丁 |
| 1.0.11 | 2026-09-06 | 缺 payload 时说明原因,而不是归咎权限 |
| 1.0.10 | 2026-09-06 | 修复 1.5.97 崩溃与缺失的 overlay 泵 |
| 1.0.9 | 2026-09-05 | SKSE 启动路径可用;1.5.97 覆盖率到 99.7% |
| 1.0.8.1 | 2026-09-04 | 崩溃处理器补上寄存器、AV 访问地址、代码 hexdump 与栈回溯 |
| 1.0.8 | 2026-09-04 | 去掉 comctl32 ordinal-345 导入(Win11 24H2 报 error 182) |
| 1.0.7 | 2026-09-03 | 用调用图消歧再恢复 5 个 class-A 函数 |
| 1.0.6 | 2026-09-03 | 客户端加载失败时打印各依赖项大小 |
| 1.0.5 | 2026-09-03 | zip 根目录对齐 `Data/`,直接解压安装也可用 |
| 1.0.4 | 2026-09-03 | 从 SKSE 探测真实游戏版本;所有地址库随 Core 附带 |
| 1.0.3 | 2026-09-03 | 用版本字符串而非结构化版本号判定 legacy |
| 1.0.2 | 2026-09-02 | 修正 1.5.x `PlayerCharacter` 内边距锚点(0x580 / 0x588) |
| 1.0.1 | 2026-09-02 | 用 capstone 匹配器把 1.5.x 地址映射补到 99.2% |
| 1.0.0 | 2026-08-30 | 首个版本 |

> 每个 tag 对应一个 [Release](../../releases),附上版本号相同的两个包——
> 客户端 mod 与专用服务器。逐条提交见 `git log`;踩坑记录与定量结论见
> [docs/PITFALLS.md](docs/PITFALLS.md)。

## 🙏 致谢

本项目的存在离不开以下上游与协作项目。它们的成果直接构成本仓库的基础,
在此一并致谢:

| 项目 | 作者 | 本项目如何使用 |
| --- | --- | --- |
| [TiltedEvolution](https://github.com/tiltedphoques/TiltedEvolution) | Tilted Phoques | **本仓库的上游**。客户端/服务端框架、网络层与引擎逆向成果均来自该项目;GPLv3 许可亦继承于此。 |
| [TiltedEvolution-rwf](https://github.com/rfortier/TiltedEvolution-rwf) | rfortier | **行为/动画 Mod 支持**。`Code/client/ModCompat/BehaviorVar.*` 与随包的 `SkyrimTogetherRebornBehaviors/` 继承自这条 fork 链,详见 [README-ANIMATION-MODS.md](README-ANIMATION-MODS.md)。 |
| [STRPluginMessagingAPI](https://github.com/Caelvanost/STRPluginMessagingAPI) | Caelvanost | **插件消息接口**。对外消费者契约、facade DLL、chat-tunnel 桥与服务器中继资源;本仓库以固定提交的子模块引入,并在 `Code/plugins/STRPM/` 保存权威头文件与契约校验。 |
| [OStimTogether](https://github.com/Caelvanost/OStimTogether) | Caelvanost | **可选联机插件**:动作场景状态、参与者对齐与装备锁同步。 |
| [MorphSyncTogether](https://github.com/Caelvanost/MorphSyncTogether) | Caelvanost | **可选联机插件**:体型滑块与 RaceMenu 覆盖层同步。 |
| [IEDSyncTogether](https://github.com/Caelvanost/IEDSyncTogether) | Caelvanost | **可选联机插件**:装备展示同步。 |
| [AnimSyncTogether](https://github.com/Caelvanost/AnimSyncTogether) | Caelvanost | **可选联机插件**:动画图谱变量与动画事件同步(配合 OAR 等动画 Mod)。实验性。 |
| [DAVSyncTogether](https://github.com/Caelvanost/DAVSyncTogether) | Caelvanost | **可选联机插件**:Dynamic Armor Variants(DAV)外观结果同步。 |
| [TradeTogether](https://github.com/Caelvanost/TradeTogether) | Caelvanost | **可选联机插件**:玩家间物品与金币交易。 |

> 六个可选插件与 STRPluginMessagingAPI 均以**固定提交的子模块**引入(`plugins/`),各自保留独立仓库、独立构建与独立发布节奏。本仓库只负责按需打包、接口一致性校验,以及为它们提供联机会话传输;它们的代码与版权归各自作者所有。

## 📄 许可证

[![GNU GPLv3](https://www.gnu.org/graphics/gplv3-127x51.png)](http://www.gnu.org/licenses/gpl-3.0.en.html)

本项目基于 GPLv3 许可(继承自 Tilted Online / TiltedEvolution),可自由使用、修改与再分发,衍生作品须保持同一许可证。
