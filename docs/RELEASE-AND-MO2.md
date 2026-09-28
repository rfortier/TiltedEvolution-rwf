# 打包发布与 MO2 安装说明

## 一、发版(全部在 GitHub 云端完成)

本仓库自带 CI 构建,**不需要本地 Windows 编译环境**。两条路径:

| 触发方式 | 工作流 | 产物 |
|---|---|---|
| 推送到 `main` / `dev`,或手动 Run workflow | `Playable Skyrim Together Build` | Actions 页面 **Artifacts** 里的 `Skyrim Together Build (<版本>)`,外加 `Debug Symbols (<版本>)` |
| **推送 tag** | `Release build` | **GitHub Release**,含下面两个 zip |

```bash
# 1. 文档先到位。CODE_GUIDELINES.md 把"文档更新"算作改动的一部分,
#    这一步不是可选的:覆盖率数字、两个 README 的版本表、推荐版本,
#    以及本 tag 在 CHANGELOG 里的小节,缺一个都会在这里红。
python Tools/Scripts/check_docs.py

# 2. 打 tag 并推送
git tag v1.0.20
git push origin v1.0.20
```

> **为什么发版前必须跑这一条**:这个检查不是形式主义,它抓到过的都是真的——
> `v1.1.3` 当时打了 tag 却**没有在 CHANGELOG 里留下任何小节**,该版本的内容
> 只存在于两个 README 的版本表里;两个 README 的版本表也曾经**互不一致**;
> 覆盖率数字在三个文档里各写各的,其中两个还是旧口径。
> 这些都属于"读者会相信、但其实是错的"的内容,所以由 `check_docs.py` 挡住。

发布的两个包:

| 文件 | 内容 |
|---|---|
| `SkyrimTogetherNextMod-<版本>.zip` | 客户端 mod。zip 根目录镜像游戏 `Data/`,**MO2 可直接"从文件安装"**,自带 FOMOD 引导安装器 |
| `SkyrimTogetherNextServer-<版本>.zip` | 专用服务器。Windows 下带图形控制面板,`--nogui` 回到纯控制台 |

> 版本号由 `git describe --tags` 生成,FOMOD 向导里显示的版本也从同一个 tag 盖章,
> 两者不会脱节(见 `release.yml` 的 stamp 步骤)。
> 手动 `Run workflow` 时产物只作为 Artifact 提供,不发布 Release;仓库里至少要有一个 tag,
> 否则版本号步骤会失败。

## 二、MO2 安装(推荐,零手动操作)

1. MO2 里"**从文件安装**" `SkyrimTogetherNextMod-<版本>.zip`;
2. 向导第一步选**中文 / English**,后面两步的选项文案跟着切换(选完只显示对应语言的那一步);
3. 通过 MO2 正常启动 `skse64_loader.exe`——完成,没有更多步骤。

> 向导只有**三步**:选语言 → 勾可选组件(中文)或 Optional components(英文)。
> 联机必需的文件全在 `requiredInstallFiles` 里**自动安装**,可选步骤只是附加项——
> 跳过向导、或直接解压 zip 手动安装,结果同样是完整的。
> 向导里显示的版本号来自发布 tag,`release.yml` 盖章时**显式指定 UTF-8**读写:
> 本文件含中文,GitHub 的 Windows runner 默认用 `pwsh` 7(默认即 UTF-8)所以本来也没事,
> 但换成 `powershell` 5.1 执行同一段脚本就会按 ANSI 往返、把中文描述写成乱码。
> 显式编码让这一步与 shell 版本无关。

客户端自带 SKSE 引导插件 `SkyrimTogetherSKSE.dll`(`Data/SKSE/Plugins/`)和
**自部署运行时** `Data/SkyrimTogetherRuntime/`。首次启动时引导插件会把运行时
(`SkyrimTogetherRuntime.dll`、`SkyrimTogetherRuntime_1_5.dll`、`UI/`、CEF 的
`libcef.dll`/`icudtl.dat`/`resources.pak`/`locales/`、`TPProcess.exe` 等)部署到
游戏根目录;mod 更新后再次启动会自动同步有变化的文件,**无需任何手动复制**。

- 兼容 SKSE 2.0.20(游戏 1.5.97)到 SKSE 2.2.x(游戏 1.6.1170)/新版 1.7.x SKSE;
- 两个运行时 DLL 都在包里:引导插件按游戏版本挑一个。1.5.x 会加载
  `SkyrimTogetherRuntime_1_5.dll`,它用 1.5.x 的结构体布局编译;
- 1.5.97 需要的地址库 `version-1-5-97-0.bin` 和映射表
  `versionlib-ae-to-se-1-5-97-0.map` 已随包附带(全部 10 个 1.5.x 版本都在);
- 若同时安装了其他 SKSE 插件,ST 与它们共存加载;
- 自动部署失败(权限/杀软拦截)时会弹窗列出 payload 与游戏根路径,按提示手动复制一次即可;
- **卸载**:MO2 中卸载本 mod 不会清理已部署到游戏根目录的文件,可手动删除
  `SkyrimTogetherRuntime.dll`、`SkyrimTogetherRuntime_1_5.dll`、`UI/`、`bin/`
  与 `.str_new` 残留(均在游戏根目录)。
  `.str_new` 是被占用文件的**待替换副本**:下次启动时会自动顶上,
  正常情况下无需手动处理。

## 三、不用 MO2 时的手动安装

mod 包的 zip 根目录就是游戏 `Data/` 的镜像,所以手动安装等同于解压到 `Data/`:

1. 解压 `SkyrimTogetherNextMod-<版本>.zip`;
2. 把 zip 里的**全部内容**(`SKSE/`、`scripts/`、`meshes/`、
   `SkyrimTogetherRebornBehaviors/`、`SkyrimTogetherRuntime/`、
   `SkyrimTogether.esp`、`SkyrimTogetherQuestPatches.esp`)复制到游戏 `Data/`;
3. 装好对应版本的 SKSE(版本表见
   [LAN-RADMIN-GUIDE.md](LAN-RADMIN-GUIDE.md)),通过 `skse64_loader.exe` 启动;
4. 首次启动同样会自动把运行时部署到游戏根目录,不需要手动复制 DLL。

> `Data/SKSE/Plugins/` 下若同时存在一份手动复制过去的
> `SkyrimTogetherSKSE.dll` 和 MO2 mod 里的一份,**两份都会加载**——排查问题时
> 会互相干扰。只保留一套(推荐 MO2 那一套)。

服务器包与 `Data/` 无关:解压后运行 `SkyrimTogetherServer.exe` 即可,任意位置。

## 四、可选:独立启动器(FOMOD 里勾选)

FOMOD 向导的"可选组件"步骤可勾选"独立启动器",安装后位于
`Data\SkyrimTogetherLauncher\`:

1. 把 `SkyrimTogetherLauncher\` 里的**全部文件**复制到游戏根目录;
2. 把 `Data\SkyrimTogetherRuntime\` 里的**全部文件**也复制到游戏根目录
   (启动器与 SKSE 路径共用同一套运行时);
3. 在 MO2 的"执行程序"里新增一个条目指向游戏根目录的 `SkyrimTogether.exe`
   (启动器已内置 usvfs 检测,在 MO2 下正常)。

> 注意:如果**不经过 MO2** 直接双击 `SkyrimTogether.exe`,MO2 虚拟的文件不可见,
> 此时地址库和 esp 必须真实存在于游戏 `Data/` 中(即第三章的完整复制)。
> 绝大多数玩家应使用第二章,无需本章。

## 五、按 F2 没有反应时怎么排查

游戏能进、但联机界面不出来,几乎总是"客户端根本没跑起来"。按顺序看这几个文件
(前几个在**游戏根目录**,即 `SkyrimSE.exe` 同级):

| 文件 | 说明 |
|---|---|
| `st_boot.log` | 每次启动都会追加。看到 `[bootstrap] game version is ...` 说明 SKSE 已加载引导插件并选好了运行时 DLL;完全没有这个文件,就是 SKSE 没加载 `SkyrimTogetherSKSE.dll`(检查 mod 是否启用、是否走 MO2 启动 `skse64_loader.exe`)。 |
| `st_boot.log` 的第一行 | `loaded from <路径>, mod organizer vfs active`——路径是 `...\mods\<某个 mod>\SKSE\Plugins\...` 说明插件来自 MO2 的 mod(正确);若是游戏目录下的 `Data\SKSE\Plugins\...` 说明有人手动复制过一份,它**即使 mod 没启用也会运行**,删掉那份残留。`vfs NOT active` 表示这次不是从 MO2 启动的。 |
| `st_client_error.log` | 运行时 DLL 加载失败时才有。`error=126` 表示文件缺失或它的依赖缺失;1.5.x 玩家若看到 `selected=SkyrimTogetherRuntime_1_5.dll size=0`,说明这个包里没带 1.5.x 运行时。 |
| `st_deploy_error.log` | 自部署失败时才有。记录插件的实际加载路径、MO2 虚拟文件系统是否生效、以及游戏进程看到的 `Data\` 子目录——`SkyrimTogetherRuntime` 不在其中就是 mod 没装好或没启用,不是权限问题也不是和 SKSE 冲突。 |
| `logs\tp_client.log` | 客户端自己的日志。`address library loaded: game 1.5.97.0, ... ids` 确认地址库选对了;`renderer init: swapchain ...` 确认渲染钩子挂上了;`overlay render pump is live` 确认每帧回调在跑;`overlay in-game state: true` 之后 F2 才会生效(主菜单里按 F2 本来就不响应,要先进游戏)。 |

`tp_client.log` 里的两种 `patch` 行都是**正常**输出,不是错误:

- `patch '...' skipped: no site inside this function is verified for game 1.5.x`
  ——该补丁点在 1.5.x 上没有经过逐个字节核对的偏移,于是跳过。只影响体验
  (菜单不冻结游戏、收藏栏编号、统计菜单),不影响联机本身。
- `patch '...' skipped: address library id ... is not mapped on game 1.5.x`
  ——该锚点在 1.5.x 的地址库里没有对应项,同样跳过。

崩溃时 `tp_client.log` 会记录 `VectoredExceptionHandler: crash occurred!` 及其后的
寄存器、故障地址与字节现场。如果 `access type` 是 `execute` 且目标地址 `unmapped`,
说明线程跳到了没有代码的地方,常见两类:

1. **补丁/跳板改错了调用点**(`DescribeCall` 会给出肇事指令的前 16 字节,
   里面能看到 `e8`(call)或 `e9`(jmp));
2. **某个 mod 的钩子被拆掉后仍被调用**。

日志还会打印栈顶能落到模块里的返回地址和完整模块地址表,**第一条落在模块里的
返回地址就是肇事的调用方**,发日志时请把这一段一起带上。

## 六、给联机伙伴的最低要求

- 所有人使用**同一个 Release 构建的客户端**(服务器会校验构建号);
- 游戏版本可以不同(服务器不校验),但建议一致;
- **本次发布(1.0.20 起)采纳了上游的协议模型,与 1.0.19 及更早的客户端/服务端
  不互通**,所有人需要一起升级;
- 地址库文件已随包附带;若单独分发,需从
  [Nexus 32444](https://www.nexusmods.com/skyrimspecialedition/mods/32444) 获取。
