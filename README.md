# Skyrim Together Next

[![Build windows](https://github.com/komAAmok/SkyrimTogetherNext/actions/workflows/windows.yml/badge.svg)](https://github.com/komAAmok/SkyrimTogetherNext/actions/workflows/windows.yml)
[![Discord](https://img.shields.io/discord/247835175860305931.svg?label=&logo=discord&logoColor=ffffff&color=7389D8&labelColor=6A7EC2)](https://discord.gg/skyrimtogether)

为 Bethesda 游戏提供联机能力的开源框架,当前支持 **上古卷轴 5:天际特别版(Skyrim Special Edition)**,也就是广为人知的 **Skyrim Together**。

本仓库在上游 [TiltedEvolution](https://github.com/tiltedphoques/TiltedEvolution) 基础上扩展了多项实用能力:

## ✨ 本仓库特性

- **多游戏版本支持**:1.6.1170 / 1.6.640 等 1.6.x 全系、1.7.x 新版本(1.7.99/1.7.104)、以及**老版 1.5.x(含 1.5.97)**,地址库与 ID 映射表全部随包附带;
- **MO2 + SKSE 无缝启动**:自带 SKSE 插件,通过 Mod Organizer 2 正常启动 `skse64_loader.exe` 即可加载本 mod,并带**自部署运行时**——首次启动自动把所需文件部署到游戏根目录,无需任何手动复制;
- **图形化安装引导**:mod 包内置 FOMOD 安装向导(MO2 原生支持),分步引导选择游戏版本与联机方式;
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
> 随包附带 10 个版本,地址映射覆盖 **99.7%**(代码引用的 3075 个地址中已解析
> 3066),且全部 3699 条映射都能在官方地址库 `version-1-5-97-0.bin` 中找到对应
> 符号;剩余 9 个(近孪生兄弟函数、无调用者的模块、调试视图)运行时安全降级
> (空实现桩 / RTTI 空指针保护 / 跳过补丁),不会崩溃,清单与各自影响见
> [Tools/missing_1_5_97_ids.txt](Tools/missing_1_5_97_ids.txt)。
> 注意:结构体成员偏移按 1.6.x 编译,1.5.x 上个别字段存在少量差异(已确认
> Actor 的两个 flags 字段相差 8 字节),在 1.5.97 上正式使用前请先小规模
> 联机验证。

## 🐛 反馈问题

请在仓库的 "Issues" 页面提交,附上可复现步骤、游戏版本、SKSE 版本与服务器日志,详细的报告对开发非常重要。

## 📄 许可证

[![GNU GPLv3](https://www.gnu.org/graphics/gplv3-127x51.png)](http://www.gnu.org/licenses/gpl-3.0.en.html)

本项目基于 GPLv3 许可(继承自 Tilted Online / TiltedEvolution),可自由使用、修改与再分发,衍生作品须保持同一许可证。
