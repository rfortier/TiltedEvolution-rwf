# 打包发布与 MO2 安装说明

## 一、打包成 GitHub Release(自动构建)

本仓库自带 CI 构建,**不需要本地 Windows 编译环境**:

- 推送到 `main` / `dev` 分支会自动触发
  `Playable Skyrim Together Build` 工作流,产物在 Actions 页面的
  Artifacts 里下载(名为 "Skyrim Together Build (版本号)")。
- **打 tag 自动发布 Release**(由 `release.yml` 完成):

  ```
  git tag v1.0.0
  git push origin v1.0.0
  ```

  Actions 会在 GitHub 的 Windows 云端机器上构建并发布 Release,包含三个包:

  | 文件 | 内容 |
  |---|---|
  | `*-full.zip` | 完整安装包:`SkyrimTogetherReborn/`(全部二进制:客户端 dll、启动器、服务器、UI)+ GameFiles(esp、脚本、全部地址库 `.bin`、AE→SE 映射 `.map`) |
  | `*-mo2-data.zip` | **MO2 专用 Data 包**:只有 esp、脚本、地址库、映射表,可直接用 MO2"从文件安装" |
  | `*-symbols.zip` | 调试符号(pdb),崩溃报告时有用 |

  也可以在 Actions 页面手动 `Run workflow`(workflow_dispatch),此时产物
  作为 Artifact 提供而不发布 Release。注意:仓库里至少要有一个 tag,
  否则版本号生成步骤会失败。

  在自己的 fork 上该工作流同样可用(仅每周定时构建被禁用,手动/推送触发不受影响)。

## 二、完整安装(不用 MO2)

1. 解压 `*-full.zip`。
2. 把 `SkyrimTogetherReborn/` 里的**全部文件**复制到游戏根目录
   (与 `SkyrimSE.exe` 同级)。
3. 把 zip 里其余内容(`SkyrimTogether.esp`、`scripts/`、`meshes/`、
   `SKSE/` 等)复制到游戏的 `Data/` 目录。
4. 装好对应版本的 SKSE(见 [LAN-RADMIN-GUIDE.md](LAN-RADMIN-GUIDE.md) 的版本表)。
5. 通过 `SkyrimTogether.exe` 启动游戏;房主另开 `SkyrimTogetherServer.exe`。

## 三、MO2 安装方式

Skyrim Together 支持两种从 MO2 启动的方式:

### 方式 A:SKSE 启动(推荐,MO2 常规流程,零手动操作)

客户端自带 SKSE 引导插件 `SkyrimTogetherSKSE.dll`(`Data/SKSE/Plugins/`)
和**自部署运行时** `Data/SkyrimTogetherRuntime/`。安装流程:

1. 在 MO2 中"从文件安装" `*-mo2-data.zip`(或 full 包里 `Data/` 的内容),
   启用该 mod;
2. 通过 MO2 正常启动 `skse64_loader.exe`——完成,没有第三步。

首次启动时引导插件会自动把 `SkyrimTogetherRuntime/` 里的运行时文件
(`SkyrimTogetherRuntime.dll`、`UI/`、`bin/` 里的 libcef 等)部署到游戏根目录;
mod 更新后再次启动会自动同步有变化的文件,**无需任何手动复制**。

- 兼容 SKSE 2.0.20(游戏 1.5.97)到 SKSE 2.2.x(游戏 1.6.1170)/新版
  1.7.x SKSE;
- 1.5.97 需要的地址库 `version-1-5-97-0.bin` 和映射表
  `versionlib-ae-to-se-1-5-97-0.map` 已随包附带;
- 若同时安装了其他 SKSE 插件,ST 与它们共存加载;
- 自动部署失败(权限/杀软拦截)时会弹窗列出 payload 与游戏根路径,按
  提示手动复制一次即可;
- 卸载说明:MO2 中卸载本 mod 不会清理已部署到游戏根目录的文件,可手动
  删除 `SkyrimTogetherRuntime.dll`、`UI/`、`bin/` 与 `.str_new`/`.str_old`
  残留(均在游戏根目录)。

### 方式 B:ST 启动器启动(可选)

FOMOD 向导的"可选组件"步骤可勾选"独立启动器",安装后位于
`Data\SkyrimTogetherLauncher\`:

1. 把 `SkyrimTogetherLauncher\` 里的**全部文件**复制到游戏根目录;
2. 把 `Data\SkyrimTogetherRuntime\` 里的**全部文件**也复制到游戏根目录
   (启动器与 SKSE 路径共用同一套运行时);
3. 在 MO2 的"执行程序"里新增一个条目,指向游戏根目录的
   `SkyrimTogether.exe` 启动(启动器已内置 usvfs 检测,在 MO2 下正常)。

> 注意:如果**不经过 MO2** 直接双击 `SkyrimTogether.exe`,MO2 虚拟的文件
> 不可见,此时地址库和 esp 必须真实存在于游戏 `Data/` 中(即完整复制)。
> 绝大多数玩家应使用方式 A,无需方式 B。

服务器 `SkyrimTogetherServer.exe` 与 MO2 无关,房主在任意位置运行即可。方式 A 则完全依赖 MO2 的虚拟文件系统,无需手动
> 复制任何数据文件。

## 四、按 F2 没有反应时怎么排查

游戏能进、但联机界面不出来,几乎总是"客户端根本没跑起来"。按顺序看这几个文件
(前三个在**游戏根目录**,即 `SkyrimSE.exe` 同级):

| 文件 | 说明 |
|---|---|
| `st_boot.log` | 每次启动都会追加。看得到 `[bootstrap] game version is ...` 说明 SKSE 已加载引导插件并选好了运行时 DLL;完全没有这个文件,就是 SKSE 没加载 `SkyrimTogetherSKSE.dll`(检查 mod 是否启用、是否走 MO2 启动 `skse64_loader.exe`)。 |
| `st_boot.log` 的第一行 | `loaded from <路径>, mod organizer vfs active` —— 路径若是 `...\mods\<某个 mod>\SKSE\Plugins\...` 说明插件来自 MO2 的 mod(正确);若是游戏目录下的 `Data\SKSE\Plugins\...` 说明有人手动复制过一份,它**即使 mod 没启用也会运行**,删掉那份残留。`vfs NOT active` 表示这次不是从 MO2 启动的。 |
| `st_client_error.log` | 运行时 DLL 加载失败时才有。`error=126` 表示文件缺失或它的依赖缺失;1.5.x 玩家若看到 `selected=SkyrimTogetherRuntime_1_5.dll size=0`,说明这个包里没带 1.5.x 运行时。 |
| `st_deploy_error.log` | 自部署失败时才有。里面会记录插件的实际加载路径、MO2 虚拟文件系统是否生效、以及游戏进程看到的 `Data\` 下有哪些子目录——`SkyrimTogetherRuntime` 不在其中就是 mod 没装好或没启用,不是权限问题也不是和 SKSE 冲突。 |
| `logs\tp_client.log` | 客户端自己的日志。`address library loaded: game 1.5.97.0, ... ids` 确认地址库选对了;`renderer init: swapchain ...` 确认渲染钩子挂上了;`overlay render pump is live` 确认每帧回调在跑;`overlay in-game state: true` 之后 F2 才会生效(主菜单里按 F2 本来就不响应,要先进游戏)。 |

`tp_client.log` 里 `patch '...' skipped` 是 1.5.x 的正常输出——那些字节补丁的偏移
是在 1.6.x 上量的,1.5.x 需要各自的偏移且必须逐个核对字节才敢用,目前全部跳过。
它们只影响体验(菜单不冻结游戏、收藏栏编号、统计菜单),不影响联机本身。

崩溃时 `tp_client.log` 会记录 `VectoredExceptionHandler: crash occurred!` 及其后的
寄存器、故障地址与字节现场。如果 `access type` 是 `execute` 且目标地址 `unmapped`,
说明线程跳到了没有代码的地方(通常是补丁改错了调用点,或钩子的跳板失效);此时日志还会
打印栈顶能落到模块里的返回地址和完整模块地址表,**第一条落在模块里的返回地址就是肇事的
调用方**,发日志时请把这一段一起带上。

## 五、给联机伙伴的最低要求

- 所有人使用**同一个 Release 构建的客户端**(服务器会校验构建号)。
- 游戏版本可以不同(服务器不校验),但建议一致。
- 地址库文件已随包附带;若单独分发,需从
  [Nexus 32444](https://www.nexusmods.com/skyrimspecialedition/mods/32444) 获取。
