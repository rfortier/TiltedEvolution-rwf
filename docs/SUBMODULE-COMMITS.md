# 子模块本地提交存档

本文件记录 **2026-10 本地对 8 个插件子模块所做提交**的索引，用于追溯与复现。

它**不是**修复的权威来源。修复的权威来源是
[`Code/plugins/patches/`](../Code/plugins/patches)，那里有 53 个受版本控制的 patch，
由 [`plugin_patches.py`](../Code/plugins/tools/plugin_patches.py) 在构建前应用到子模块。
本文件只回答一个问题：**这台机器上那 8 个 commit 分别是什么、对应哪些 patch、为什么还推不上去。**

---

## 1. 为什么这些提交推不上去

8 个子模块指向的仓库**不属于本项目**（见 [PLUGIN-SOURCES.md](PLUGIN-SOURCES.md)）。
实测 `git push --dry-run` 对每个仓库都返回 403：

```
remote: Permission to Caelvanost/AnimSyncTogether.git denied to komAAmok.
fatal: unable to access '...': The requested URL returned error: 403
```

当前账号不是这些仓库的协作者。这不是配置问题，是权限问题。

**这也是本仓库用 patch 交付插件修复的原因**：CI 会执行
`git submodule update --init --force --recursive --depth=1`，丢弃子模块工作区的
一切本地改动，所以只存在于工作区（或只存在于无法推送的本地提交里）的修复
永远进不了发布包。patch 是本项目的子模块改动唯一能抵达用户的路径。

---

## 2. 提交清单

每个子模块都在 `main` 分支、各领先 `origin/main` 1 个提交。提交基座（`HEAD~1`）
等于该子模块当前的 pin。

| 子模块 | 本地 commit | 基座（= pin） | 主题 |
| --- | --- | --- | --- |
| AnimSyncTogether | `7d80391` | `a432ec3` | 停止逐行刷盘；OAR 发现移出主线程 |
| DAVSyncTogether | `66919a6` | `cd4ec0e` | 停止逐行刷盘 |
| IEDSyncTogether | `9a5978d` | `ad4710a` | 停止逐行刷盘 |
| MorphSyncTogether | `84be265` | `017c7b0` | 停止逐行刷盘；关闭外观明细日志 |
| OStimNG | `06c2a32` | `3954683` | 注解数字不抛异常；不在主线程等自己 |
| OStimTogether | `49da4e8` | `541d426` | 对端可控缓存设上限；合并且重放 |
| STRPluginMessagingAPI | `ce049a8` | `62143fd` | 版本元数据对齐 SKSE 2.0.x/2.2.x；缓存设上限 |
| TradeTogether | `4c9ff6f` | `17199e4` | 对端名与请求 id 表设上限 |

完整 hash 可用 `git -C plugins/<name> rev-parse HEAD` 查看。

---

## 3. 每个提交对应哪些 patch

这些提交的内容**就是**既有 patch 应用后的结果（`plugin_patches.py check` 报
`PATCH CHECK OK` 即可印证）。因此本文件不复制 patch 内容，只列出对应关系。

| 子模块 | 该子模块的 patch 数量 | 目录 |
| --- | --- | --- |
| AnimSyncTogether | 5 | `Code/plugins/patches/AnimSyncTogether/` |
| DAVSyncTogether | 5 | `Code/plugins/patches/DAVSyncTogether/` |
| IEDSyncTogether | 5 | `Code/plugins/patches/IEDSyncTogether/` |
| MorphSyncTogether | 7 | `Code/plugins/patches/MorphSyncTogether/` |
| OStimNG | 8 | `Code/plugins/patches/OStimNG/` |
| OStimTogether | 10 | `Code/plugins/patches/OStimTogether/` |
| STRPluginMessagingAPI | 9 | `Code/plugins/patches/STRPluginMessagingAPI/` |
| TradeTogether | 4 | `Code/plugins/patches/TradeTogether/` |

主题对应关系举例：

- 刷盘修复 → 各目录的 `*-stop-flushing-every-log-line*.patch`
- OAR 发现移出主线程 → `AnimSyncTogether/0005-keep-the-oar-discovery-walk-off-the-main-thread.patch`
- 缓存设上限 → `OStimTogether/0006`/`0007`/`0008`、`TradeTogether/0002`、
  `MorphSyncTogether/0004`/`0005`、`IEDSyncTogether/0002`
- 注解不抛异常 → `OStimNG/0002-guard-the-animation-event-path.patch`
- SKSE 版本元数据 → `STRPluginMessagingAPI/0002-load-on-every-supported-runtime.patch`

---

## 4. 复现方式

拿到写权限（或 fork 这些仓库）后，逐个推送：

```sh
for d in AnimSyncTogether DAVSyncTogether IEDSyncTogether MorphSyncTogether \
         OStimNG OStimTogether STRPluginMessagingAPI TradeTogether; do
    git -C "plugins/$d" push origin main
done
```

**顺序很重要：先推子模块，再更新父仓库指针。** 父仓库记录的是子模块的 commit
hash；若父仓库先指向一个远程不存在的 commit，任何人 clone 都会失败
（`fatal: reference is not a tree`）。所以目前父仓库的指针**故意保持旧值**。

推送完成后，回父仓库：

```sh
git add plugins/*
git commit -m "chore: move the plugin submodules to the commits that carry the audit fixes"
```

然后把 `Code/plugins/patches/` 里对应的 patch 删除——它们的改动已经进入子模块
历史，留着会在 `plugin_patches.py apply` 时被判为「已应用」而跳过，属于无害但
冗余的记录。**删除前请先确认对应的 commit 确实已推送并可在远程解析。**

---

## 5. 一个操作陷阱（本次踩到并已修复）

在本仓库里给子模块做本地提交，会**破坏 `plugin_patches.py verify`**。

原因：`verify` 用 `git apply --check --cached` 对**索引**校验，而索引在正常状态下
是未打补丁的 pin 内容——这样的设计让「patch 已应用到工作区」无法掩盖「patch 已
不再匹配 pin」。一旦在子模块里提交，索引就变成已打补丁的内容，53 个 patch 全部
被判「does not apply to the pinned commit」。

修复方式（保留提交与工作区，只把索引恢复到 pin）：

```sh
git -C plugins/<name> read-tree HEAD~1
```

8 个子模块执行后 `plugin_patches.py verify` 重新报
`PATCH VERIFY OK — 53 patch(es) apply to their pinned commits`。

副作用：父仓库会重新把这 8 个子模块显示为 modified（子模块内部 HEAD 与索引不一致），
这是预期状态，不是错误。

---

## 6. 未做的事

- **没有**把这些提交导出成第二套 `.patch`。`Code/plugins/patches/` 已经是权威记录，
  再复制一份会造成两套同义记录日后静默失真。
- **没有**把父仓库的子模块指针改成新 commit（见 §4 的顺序要求）。
- **没有**删除 `Code/plugins/patches/` 里的任何 patch（子模块尚未推送，patch 仍是
  当前唯一能进发布包的路径）。
