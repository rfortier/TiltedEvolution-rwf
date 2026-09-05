# 1.5.97 剩余 4 个函数目标分析留档

覆盖 99.5%（3044/3058）。剩余未映射：4 个函数 + 10 个数据静态变量。
本文件记录已完成的排查与待续线索，供后续用 IDA Pro 反编译继续。

## 待分析函数（4 个）

### 1. 21600 isFirstPerson（PlayerCamera::IsFirstPerson）

**1.6.1170 结构**（0x332360, size 0xf0）：
- `xor edx,edx; mov rbx,r9; mov [r9],rdx`（输出指针清零）
- 读全局1（PlayerCamera 单例）→ `cmp dword [rax+0x20], edx; je`
- 读全局2 → `mov rax,[r10+0xc8]; cmp [r10+0x28], rax; jne`
- `mov edi,[r10+0xb0]; mov ebp,0xffffffff; mov rsi,[r10+0xb8]` 循环（哨兵 -1）
- 循环内 `cmp ecx,ebp; jne; cmp dword [r10+0x40],0; ...`

**PlayerCamera 单例**：id 400802 → 1.5.97 `0x2ec59b8`（已映射）。

**已排除候选**：0x358fc0（sim 0.71，参数结构不同）、0x15b180、0x587960（多全局菜单检查）、0x2dc490（朝向函数，输出 double）、0x541ef0 等。

**待续方向**：PlayerCamera 单例 0x2ec59b8 的 xref 函数群中，找「输出置零 + 0x20 检查 + 0xc8/0x28 对比较 + 0xffffffff 哨兵」的函数。1.5.97 的偏移可能为 0x18/0xc0/0x20/0xa8/0xb0（-8）。候选区间可重点看引用 0x2ec59b8 的函数（当前找出的 10 个均不匹配，需扩大 xref 范围或检查间接引用）。

### 2. 32883 InternalRevertAnimGraph（Actor::InternalRevertAnimGraph?）

**1.6.1170 结构**（0x54b4f0, size 0x170）：
- 5 个 push + `sub rsp,0x30` + sentinel
- `mov rax,[rcx]; lea rdx,[rsp+0x68]; call qword [rax+0x10]`（虚表+0x10 调用）
- `test al,al; je`
- `mov rcx,[rsp+0x68]; call 0x140ba4ab0`（直接调用）
- TLS 0x768 读写 + `mov dil,1`

**已排除候选**：0x6e3af0（虚表偏移 0x140/0x138 不符）、0x4f1d40（TLS 写 58 完全不同）。

**待续方向**：找含「虚表+0x10 调用 + 调用 0xba4ab0 的 1.5.97 对应」的函数。0xba4ab0 全扫匹配 0x10fc7d0（sim 0.72 低置信，需复核）。或搜索 TLS+0x768 与 `mov dil,1` 组合。

### 3. 37717 setPlayerTeammate（Actor::SetPlayerTeammate）

**1.6.1170 结构**（0x68d690, size 0xa0）：
- `mov rbx,rcx; movzx esi,r8b; mov rcx,[rip+全局]; movzx edi,dl; cmp rbx,rcx; je`
- `mov eax,[rbx+0xe8]; shr eax,0x1a; and al,1`（flags1 bit 26 = IS_PLAYER_TEAMMATE）
- `test dl,dl; je; test al,al; jne; call 0x140745270 / call 0x140745290`（设置/清除）
- `btr eax,0x1a; bts ecx,0x1a; test dil,dil; cmove ecx,eax; mov [rbx+0xe8],ecx`
- `mov eax,[rbx+0x204]; test sil,sil; je; bts eax,7 / btr eax,7; mov [rbx+0x204],eax`

**关键特征**：同函数内 btr+bts 操作同一 bit（0x1a）+ 两个 flags 字段（1.5.97 应为 0xe0 与 0x1fc）。

**已排除候选**：0xb5ee70（a1+136 赋值，非 flags）、0xe04840（引用计数）、0x194fc0/0x194a10/0x194c70/0x194ec0/0x194f50（0x194 段一族兄弟，均为 `*((_DWORD*)a1+4)` 单字段 + 虚表+0x50 调用，非双 flags 模式）。

**待续方向**：0x194 段函数族是「单 bit 设置 + 虚表+0x50 通知」模式（类似 setXxx 系统），不是目标。需找双字段（0xe0/0xe8 + 0x1fc/0x204）操作函数。IDB 扫描 `0xe0/0xe8 + 0x1fc/0x204` 组合候选 12 个均需逐个反编译核验（0x213410, 0x3c2c20, 0x3c2ef0, 0x587960, 0x5d1a00, 0x5ec080, 0x5ecbf0, 0x5fa750, 0x5fb640, 0x609a90, 0x60e820, 0x60f3a0）。

### 4. 82088 s_CloseAll（UI 相关，菜单关闭）

**1.6.1170 结构**（0xfa5630, size 0x240）：
- `lea rax,[rip+0x22ec256]; mov [rsp+0x60],rax`
- `call qword [rip+0x7a9e53]`（间接调用）
- `lfence; xor r12d,r12d; lea r14d,[r12+1]`
- `cmp dword [rip+0x22ec238],eax; jne`
- `lock inc dword [rip+0x22ec233]`
- 自旋：`lock cmpxchg dword [rip+0x22ec223],r14d; sete cl; test; jne; pause; lock cmpxchg ...`

**关键特征**：lfence + pause + 多个 lock cmpxchg（原子自旋）。

**已排除候选**：0x337660（检查 a1+18 字节，不同）、0x6d54b0（引用计数）。

**待续方向**：`lock cmpxchg + pause` 组合在 1.5.97 IDB 中扫描（`F0 0F B1` 字节命中 1369 函数太多，需 +pause 约束；IDA 指令前缀 0xF0 检测）。

## 数据静态变量（10 个，ROI 低）

370892 s_greetDistance、381472 s_difficulty、382393 s_value、382400 s_value、
400180 rtti、405282 s_value、406126 s_matrix、406160 s_port、410506 NiCameraRTTI、414391 s_policy

建议：保持 stub 降级，除非用「已映射函数的 rip-relative xref 反推」逐一定位。

## 已解决记录（本会话）

| id | 名称 | 1.5.97 地址 | 方法 |
|---|---|---|---|
| 11619 | setChargeData | 0x10e420 | IDA 反编译（常量 37 vs 孪生 47） |
| 11620 | setSoulData | 0x114550 | IDA 反编译（lock+0x9c 模式） |
| 14953 | s_constructor | 0x938b40 | IDA 反编译（OnTrackedStatsEvent 字符串） |
| 19784 | refrGetWorldLocation | 0x2964a0 | IDA 反编译（逻辑同构，纠正此前误拒） |
| 33285 | arrayQuickSort | 0x3236e0 | 反编译（回调比较 vs 孪生 _stricmp） |
| 38533 | s_setNoBleedoutRecovery | 0x623ba0 | 反编译（0x1fc bit5 + vtable+0x718） |
| 42345 | setEnabled | 0x705250 | 形状扫描（sete 等价 xor dl,1） |
| 34370 | s_finish | 0x54b2f0 | callee 交集 3/3 |
| 34529 | getTargetAsActor | 0x553e90 | 交叉验证（被 s_finish 调用） |
| 34989 | s_start | 0x548b50 | TLS 0x768 + 常量 |
| 37527 | s_getGoldAmount | 0x2a7240 | 包装形状 + 双 callee |
| 52847 | fadeOutGame | 0x8d5530 | callee 唯一调用者 |
