# WS-F 审计报告：基础设施层与前端

审计者：WS-F（代码审计子代理）
仓库：D:\sktest\SkyrimTogetherNext
范围：Code/components/es_loader/**、Code/base/simpleini/SimpleIni.h（读取路径）、Code/base/containers/**、
Code/base/allocator/**、Code/base/threading/ThreadUtils.cpp、Code/common/{Cell,Map,DateTime}.cpp、
Code/skse_bootstrap/**、Code/skse_client/**、Code/tp_process/**、Code/skyrim_ui/src/**/*.ts（IPC 部分）。
已跳过：Code/components/imgui（vendored）。
本机不可构建（无 xmake/MSVC，Libraries 四个子模块为空目录），所有结论为静态读数；少量用 g++ 16 独立小程序验证过的语义在下文单独标注。

## 关于 `Buffer::Reader` 的前提（影响下文多条结论的定性）

`Buffer`/`Buffer::Reader` 来自 `tiltedcore`（由 xmake 拉取，**不在本仓库**；`Libraries/` 下无源码，`find` 全盘无 `TiltedCore/Buffer.hpp`）。
`docs/PITFALLS.md:329-332` 已记录该实现未知。因此下文凡是「越界读之后会怎样」的**后果**一律标【运行时依赖】，
而「代码没有做边界校验」这一事实本身是【可闭合】——它不需要知道 Reader 的实现就能确认：
缺少校验意味着正确性完全寄托在一个未 vendor 的组件上。

## 关于 es_loader 的当前可达性（先声明，避免把严重度报高）

`ESLoader::BuildRecordCollection` 在 `ESLoader.cpp:54` 直接 `return MakeUnique<RecordCollection>();`，
其后的 `LoadFiles()` 调用被 `/* */` 注释掉（`:56-61`）。全仓 `LoadFiles` 只有 `ESLoader.cpp:57`（注释内）这一个引用。
所以 **`TESFile::LoadFile` / `IndexRecords` / `ReadGroupOrRecord` / 全部 `Chunks::*` 解析器当前是不可达的死路径**，
`RecordCollection` 恒为空。`docs/PITFALLS.md:14693` 已就同一事实把 8 个站点判为「不改」。
本节仍逐条报告其缺陷，理由与处理方式：
- 这些代码是**唯一**解析不可信二进制文件的实现，注释块只差一次「打开」就重新可达；
- 严重度按**「重新启用后」**的定性给出，并在每条里显式写明「当前不可达」，不把死路径缺陷伪装成线上风险。


## 索引（43 条：可闭合 34 / 运行时依赖 9；致命 1 / 高 7 / 中 20 / 低 15）

| 区 | 条目 | 可闭合 | 运行时依赖 |
| --- | --- | --- | --- |
| 一、es_loader | ESL-01..15、RD-01 | 15 | 1 |
| 二、SimpleIni 读取路径 | SII-01..05 | 5 | 0 |
| 三、容器与分配器 | CTN-01..03、ALLOC-01 | 3 | 1 |
| 四、前端 IPC | UI-01..06、UI-07 | 6 | 1 |
| 五、skse_bootstrap / skse_client | SKSE-01..09 | 4 | 5 |
| 六、tp_process | 无 | 0 | 0 |
| 七、Code/common | CMN-01、CMN-02 | 1 | 1 |

**按严重度**
- 致命 1：ESL-01
- 高 7：ESL-02、ESL-03、ESL-04、ESL-05、ESL-06、CTN-01、UI-01
- 中 20：ESL-07..ESL-12、RD-01、SII-01、CTN-02、CTN-03、ALLOC-01、UI-02、UI-03、UI-04、SKSE-01、SKSE-03、SKSE-04、SKSE-05、SKSE-07、CMN-02
- 低 15：ESL-13..ESL-15、SII-02..05、UI-05、UI-06、UI-07、SKSE-02、SKSE-06、SKSE-08、SKSE-09、CMN-01

**只在活代码上成立的（不是死路径）**：CTN-01、CTN-02、CTN-03、UI-01、UI-02、UI-03、UI-04、UI-05、UI-06、SII-01、SII-02、SII-03、ESL-12、ESL-13、SKSE-01、SKSE-02、SKSE-03、SKSE-04、CMN-01、ALLOC-01。
其余 es_loader 条目（ESL-01..ESL-11、ESL-14、ESL-15）都在 `LoadFiles()` 被注释掉之后的死路径上，严重度按「重新启用后」定级，每条都单独标注了可达性。

---

# 一、es_loader（解析不可信二进制文件）

严重度口径：**按代码重新可达（LoadFiles() 被取消注释）时的风险定级**；每条的「可达性」字段单独说明当前是否为死路径。

## 【可闭合】

### ESL-01 · 致命 · TESFile::ReadGroupOrRecord 用未校验的 size 驱动指针算术，且读头前不校验剩余字节

- 位置：Code/components/es_loader/TESFile.cpp:60-83、:160
- 问题：
  1. :62 只判 aReader.Eof()（是否到末尾），:66-68 直接读 8 字节头；剩余字节 < 8 时越界读。
  2. :83 把 m_buffer.GetWriteData() + GetBytePosition() 硬转成 Record*，:85 立刻解引用 GetType()——此时连记录头 0x18 字节是否仍在缓冲区内都没验证。
  3. :73 endOfGroup = GetBytePosition() + size，size 完全来自文件，可以远超缓冲区；:74 Advance(sizeof(Group))（0x18）同样不校验。
  4. :160 aReader.Advance(sizeof(Record) + size)，size 来自文件，可以把读位置推到缓冲区之外，且 Advance 的越界语义未知（见开头前提）。
- 证据（TESFile.cpp:60-85）：

    bool TESFile::ReadGroupOrRecord(Buffer::Reader& aReader, RecordCollection& aRecordCollection) noexcept
    {
        if (aReader.Eof())
            return false;

        uint32_t type = 0;
        aReader.ReadBytes(reinterpret_cast<uint8_t*>(&type), 4);
        uint32_t size = 0;
        aReader.ReadBytes(reinterpret_cast<uint8_t*>(&size), 4);
        aReader.Reverse(8);

        if (type == static_cast<uint32_t>(FormEnum::GRUP))
        {
            const size_t endOfGroup = aReader.GetBytePosition() + size;
            aReader.Advance(sizeof(Group));
            ...
            Record* pRecord = reinterpret_cast<Record*>(m_buffer.GetWriteData() + aReader.GetBytePosition());
            switch (pRecord->GetType())

- 后果：一个损坏/恶意的 .esm/.esp 即可让解析器按文件内声明的长度做指针运算；越界读到的内容随后会被当作记录类型、chunk id、字符串长度继续放大（配合 ESL-04/ESL-05）。这类文件正是「用户从网上下的 mod」，属于典型的解析不可信输入。
- 最小改法：在 ReadGroupOrRecord 入口算出 remaining = bufferSize - position，先要求 remaining >= 8（读头）/ >= 0x18 + size（记录体）再解析；size 用 std::min(size, remaining) 夹取，超出即 return false 终止该文件。
- 可达性：**当前死路径**（ESLoader.cpp:56-61 的 LoadFiles() 被注释）。

### ESL-02 · 高 · WRLD 分支缺 break，每个 WRLD 记录都会被当作 NAVM 再解析一遍

- 位置：Code/components/es_loader/TESFile.cpp:138-147
- 证据：

        case FormEnum::WRLD:
        {
            WRLD parsedRecord = CopyAndParseRecord<WRLD>(pRecord);
            aRecordCollection.m_worlds[parsedRecord.GetFormId()] = parsedRecord;
        }                                    // <-- 缺 break
        case FormEnum::NAVM:
        {
            NAVM parsedRecord = CopyAndParseRecord<NAVM>(pRecord);
            aRecordCollection.m_navMeshes[parsedRecord.GetFormId()] = parsedRecord;
        }

- 问题：教科书式 fallthrough。每条 WRLD 记录都会继续走 NAVM 分支：CopyAndParseRecord<NAVM> 把同一个 WRLD 记录当成 NAVM 解析其 chunk，然后以同一个 formId 塞进 m_navMeshes。产出物是「一个不是 navmesh 的 navmesh」：其 NVNM 成员（见 ESL-03）不会被任何 chunk 命中而保持未初始化。
- 后果：navmesh 索引被污染 + 未初始化读；RecordCollection::BuildReferences（RecordCollection.cpp:9）据此判断 m_worldSpaceId 并挂到 world 上。
- 最小改法：case FormEnum::WRLD 块末尾补 break;。
- 可达性：当前死路径。

### ESL-03 · 高 · WRLD / NVNM 的标量成员没有默认初始化，缺 chunk 时被当值使用

- 位置：Code/components/es_loader/Records/WRLD.h:19-20、Records/Chunks.h:366-380、使用点 RecordCollection.cpp:9
- 证据：

        std::optional<uint32_t> m_parentId;
        uint32_t m_musicId;          // 无初始化器
        float m_lodMultiplier;       // 无初始化器

        uint32_t m_unknown;          // NVNM，以下均无初始化器
        uint32_t m_locactionMarker;
        uint32_t m_worldSpaceId;
        ...
        uint32_t m_divisor;
        glm::vec2 m_maxDistance;
        glm::vec3 m_min;
        glm::vec3 m_max;

- 问题：TESFile::CopyAndParseRecord 里 T parsedRecord;（TESFile.cpp:173）是默认初始化，WRLD/NAVM 都没有用户声明的构造函数，因此这些标量成员是不确定值；ParseChunks 只在对应 chunk 出现时才写它们（WRLD.cpp:24-25 的 ZNAM/NAMA、NAVM.cpp:12 的 NVNM）。
- 后果：RecordCollection.cpp:9 的 if (navmesh.m_navMesh.m_worldSpaceId) 在不确定值上分支——可能把一条没有 NVNM 的记录挂到某个随机 world 上，也可能反过来丢掉真实 navmesh。MSVC /RTCu 下直接报运行时错误；发布版是静默脏数据。
- 最小改法：给这些成员补 = 0 / {}；WRLD 的两个标量改成 std::optional 更贴合「可能不存在」的语义。
- 可达性：当前死路径。

### ESL-04 · 高 · ReadZString 在无 NUL 终止的 chunk 上无界扫描，并可能触发巨量分配

- 位置：Code/components/es_loader/ESLoader.cpp:13-18
- 证据：

    String ReadZString(Buffer::Reader& aReader) noexcept
    {
        String zstring = String(reinterpret_cast<const char*>(aReader.GetDataAtPosition()));
        aReader.Advance(zstring.size() + 1);
        return zstring;
    }

- 问题：std::string(const char*) 会一直往后扫到第一个 NUL 为止，没有任何上界——既没有 chunk 的 dataSize，也没有整个 m_buffer 的末尾。而调用点拿到的 reader 正是「chunk 起点、无终点」的 reader（见 ESL-05）。
- 后果：一条 EDID/MAST/FNAM/GNAM 数据段不含 NUL 的记录，会让读取一直跑到缓冲区之后（越界读），长度取决于堆上第一个 0 字节的位置；若一直没有 NUL，就是数十 MB 的分配。ReadZString 自身是 noexcept，超长分配的异常会穿透到调用方（GMST/CONT/CLMT/NPC/TES4 的 ParseChunks 也都是 noexcept）导致 std::terminate。
- 最小改法：改成有界版本——让 IterateChunks 交付的 chunk reader 带上 dataSize 上界，用 strnlen 夹取后再构造 String；末尾无 NUL 时截断返回。
- 可达性：当前死路径。同类：ReadWString（ESLoader.cpp:20-27）读 uint16_t 长度后 String(ptr, len)，同样不校验剩余字节是否够 len。

### ESL-05 · 高 · Record::IterateChunks 交付给回调的 chunk reader 不按 dataSize 截断

- 位置：Code/components/es_loader/Records/Record.cpp:40-67
- 证据：

        while (!reader.Eof())
        {
            Record::Chunk* pChunk = reinterpret_cast<Chunk*>(reader.GetDataAtPosition());
            reader.Advance(sizeof(Record::Chunk));

            uint32_t dataSize = pChunk->m_dataSize;
            if (pChunk->m_dataSize == 0)
                dataSize = largeDataSize;
            ...
            Buffer::Reader chunk(reader);      // 只是复制 reader，没有任何「到 dataSize 为止」的边界
            reader.Advance(dataSize);
            aCallback(pChunk->m_chunkId, chunk);
        }

- 问题：两条静态事实——(a) chunk 是 reader 的拷贝，构造过程中没有任何地方传入或设置 chunk 的末尾；(b) 循环头 reinterpret_cast<Chunk*>(reader.GetDataAtPosition()) 在剩余字节 < 6 时越界读（while (!reader.Eof()) 只保证还有 >= 1 字节）。
- 后果：所有 Chunks::* 解析器（CNTO/ACBS/TNAM/NVNM/VMAD…）都会跨过本 chunk 的边界继续读下一个 chunk 的头和体。一个短小的 DATA chunk 后面紧跟另一个 chunk 时，GMST::ParseChunks 会把下一个 chunk 的 id 当成 int 值读走（GMST.cpp:25）。这也是 ESL-04 越界读的直接上游。
- 最小改法：Buffer::Reader chunk(reader, dataSize)，或解析前记录 chunkStart、回调后校验 position - chunkStart <= dataSize；循环头前要求剩余 >= 6 字节。
- 可达性：当前死路径。

### ESL-06 · 高 · Record::IterateChunks 里 m_dataSize - 4 无符号下溢 + 解压尺寸不设上限

- 位置：Code/components/es_loader/Records/Record.cpp:28-38、:70-88
- 证据：

        Buffer pDecompressed;
        if (Compressed())
        {
            uint32_t size = 0;
            reader.ReadBytes(reinterpret_cast<uint8_t*>(&size), 4);
            pDecompressed.Resize(size);
            const uint32_t fieldSize = m_dataSize - 4;      // m_dataSize < 4 时下溢
            DecompressChunkData(reader.GetDataAtPosition(), fieldSize,
                                pDecompressed.GetWriteData(), pDecompressed.GetSize());

- 问题：m_dataSize 是记录头里由文件给出的 uint32_t，压缩标志 kCompressed (0x40000) 也由文件给出。当「压缩标志置位 + m_dataSize 属于 {0,1,2,3}」时：(1) ReadBytes(&size,4) 本身就在读 4 字节（数据不足）；(2) m_dataSize - 4 下溢成 0xFFFFFFFC..0xFFFFFFFF，即 fieldSize 约 4 GiB，被直接塞进 z_stream.avail_in，zlib 会照这个长度读输入——等于让攻击者指定越界读的长度；(3) pDecompressed.Resize(size) 的 size 同样由文件给出（最大 0xFFFFFFFF），一次 Resize 就是 4 GiB。
- 后果：越界读（长度由文件控制）+ 分配失败。IterateChunks 不是 noexcept，但它的所有调用方（TES4/GMST/CONT/WRLD/NAVM/CLMT/REFR/NPC::ParseChunks）都是 noexcept（如 GMST.cpp:5），Resize 抛出的 bad_alloc/length_error 会穿透 noexcept 触发 std::terminate。
- 最小改法：if (m_dataSize < 4) return;，并把 size 与 fieldSize 一起夹到剩余可读字节和合理上限内再 Resize。
- 可达性：当前死路径。

### ESL-07 · 中 · DecompressChunkData 只调一次 inflate，失败仅打日志，解析继续在未解压的缓冲上跑

- 位置：Code/components/es_loader/Records/Record.cpp:70-88
- 证据：

        inflateInit(&compressionStream);
        int res = inflate(&compressionStream, Z_NO_FLUSH);
        if (res < Z_OK)
            spdlog::error("Failed to decompress chunk of data (inflate): {}.", res);
        res = inflateEnd(&compressionStream);

- 问题：(a) 压缩流需要多次 inflate 才能耗尽，单次调用在输出缓冲不够（Z_BUF_ERROR）或输入分片时只解出一部分；(b) inflateInit 的返回值未检查；(c) 错误只写日志，函数返回 void，调用方 IterateChunks 无条件把 reader 换成 pDecompressed 并继续解析。
- 后果：压缩记录被静默地按「半解压 + 缓冲尾部残留」解析，产出物错乱但不报错（尾部到底是零值还是未初始化，取决于 Vector::Resize 的实现，见「运行时依赖」RD-01）。
- 最小改法：改 bool DecompressChunkData(...)，循环 inflate 至 Z_STREAM_END（或输出满/输入空），失败即 return false，让 IterateChunks 放弃该记录。
- 可达性：当前死路径。

### ESL-08 · 中 · NVNM 的五个 count 都是有符号 int32_t 且直接 resize，负值即巨型分配

- 位置：Code/components/es_loader/Records/Chunks.cpp:197-236
- 证据：

        int32_t vertexCount = 0;
        aReader.ReadBytes(reinterpret_cast<uint8_t*>(&vertexCount), 4);
        m_vertices.resize(vertexCount);          // int32_t -> size_t，负数变 2^64-x
        ...
        int32_t coverTriangleCount = 0;
        aReader.ReadBytes(reinterpret_cast<uint8_t*>(&coverTriangleCount), 4);
        m_coverTris.resize(coverTriangleCount);
        aReader.ReadBytes(reinterpret_cast<uint8_t*>(m_coverTris.data()), sizeof(int16_t) * m_coverTris.size());

- 问题：vertexCount/triangleCount/connectionCount/doorCount/coverTriangleCount 全部由文件给出、全部是有符号 32 位、全部直接交给 resize。负值转成 size_t 后约 1.8e19，vector 抛 length_error/bad_alloc；正的大值（如 0x7FFFFFFF）请求 2G 个元素（glm::vec3 是 12 字节 → 约 25 GB）。
- 后果：NAVM::ParseChunks 是 noexcept（NAVM.cpp:5），抛出即 std::terminate。一个损坏的 NVNM chunk 就足以终止进程。
- 最小改法：if (count < 0 || count > remainingBytes / sizeof(elem)) return;，五个 count 一起处理。
- 可达性：当前死路径。

### ESL-09 · 中 · ScriptProperty::ParseValue 的数组长度无上限，且 WSTRING 长度不校验剩余字节

- 位置：Code/components/es_loader/Records/Chunks.cpp:77-103
- 证据：

        case Type::WSTRING:
        {
            uint32_t stringLength = 0;
            aReader.ReadBytes(reinterpret_cast<uint8_t*>(&stringLength), 2);
            m_dataSingleValue.m_string = {reinterpret_cast<const char*>(aReader.GetDataAtPosition()), stringLength};
            aReader.Advance(stringLength);
            break;
        }
        ...
        case Type::STRING_ARRAY:
        {
            uint32_t sizeOfArray = 0;
            aReader.ReadBytes(reinterpret_cast<uint8_t*>(&sizeOfArray), 4);
            for (uint32_t i = 0; i < sizeOfArray; i++)   // 循环上界来自文件

- 问题：(a) sizeOfArray 是文件给的 uint32_t，循环里每轮 push_back，最多约 42 亿轮——只需把 4 个字节写成 0xFFFFFFFF 就是长时间循环 + OOM；(b) m_string 只存指针，Advance(stringLength) 不校验剩余字节；(c) 循环内没有按「至少还剩 1 字节」提前退出。
- 后果：NPC::ParseChunks（noexcept）→ VMAD ctor → ParseValue，push_back 抛 bad_alloc 即 std::terminate。
- 最小改法：if (sizeOfArray > kMaxArrayElements) return;，并在每轮循环前检查 reader 是否还有剩余字节。
- 可达性：当前死路径。

### ESL-10 · 中 · ScriptProperty 把指向 TESFile::m_buffer 的裸指针存进 RecordCollection，缓冲随 TESFile 析构

- 位置：Code/components/es_loader/Records/Chunks.h:38-43、Chunks.cpp:81、生命周期 ESLoader.cpp:145-157
- 证据：

        struct Str
        {
            const char* data;      // 非拥有
            size_t length;
        } m_string{nullptr, 0};

        TESFile pluginFile(m_masterFiles);            // m_buffer 在栈上
        ...
        pluginFile.IndexRecords(*recordCollection);   // 存入指向 pluginFile.m_buffer 的指针
    }   // <- pluginFile 析构，m_buffer 释放
    return recordCollection;

- 问题：m_string.data 指向 TESFile::m_buffer 内部，而 RecordCollection 的寿命远超 pluginFile（LoadFiles 返回后仍被使用）。整条路径上没有任何地方复制这份字节。
- 后果：任何读取 m_dataSingleValue.m_string 的代码都是 use-after-free。当前仓库内没有读者（这正是不报成致命的原因），但这是已经埋好的哑弹：只要有人加一个「输出 NPC 脚本属性」的功能就会触发。
- 最小改法：把 Str 改成拥有型 TiltedPhoques::String，或在 CopyAndParseRecord 里把字符串拷进拥有型成员。
- 可达性：当前死路径。

### ESL-11 · 中 · LoadFile 是 noexcept 却调用会抛异常的 file_size，且不检查 read 的实际字节数

- 位置：Code/components/es_loader/TESFile.cpp:25-42
- 证据：

    bool TESFile::LoadFile(const std::filesystem::path& acPath) noexcept
    {
        m_filename = acPath.filename().string();
        const uintmax_t fileSize = std::filesystem::file_size(acPath);   // 失败即抛 filesystem_error
        m_buffer.Resize(fileSize);                                       // 失败即抛 bad_alloc
        std::ifstream file(acPath, std::ios::binary);
        if (file.fail())
        { ... return false; }
        file.read(reinterpret_cast<char*>(m_buffer.GetWriteData()), fileSize);  // 返回值忽略
        return true;
    }

- 问题：三个独立问题叠在一处——(a) file_size 的抛出版本在 noexcept 函数里，文件不存在/无权限/路径是目录时直接 std::terminate（:33 的 file.fail() 检查在它之后，救不了这种情形）；(b) 打开失败前就已分配 fileSize 字节；(c) file.read 的返回值未检查——短读（文件在 file_size 与 read 之间被截断、磁盘错误）时缓冲区尾部是未初始化内容，随后被当作记录数据解析。
- 后果：前者 terminate；后者是未初始化堆读，随机内容被当作记录解析，可能通过日志/网络把堆内容带出去。
- 最小改法：auto ec = std::error_code{}; const auto fileSize = std::filesystem::file_size(acPath, ec); if (ec) return false;，先开文件再 Resize，并校验 file.gcount() == fileSize。
- 可达性：当前死路径。

### ESL-12 · 中 · loadorder.txt 解析：在擦除 CR 之前就保存文件名，且擦除后对可能为空的串取 line.back()

- 位置：Code/components/es_loader/ESLoader.cpp:95-127（关键 :99、:103、:106、:108）、:162-172（GetPath）
- 证据：

        while (!loadOrderFile.eof())
        {
            String line;
            std::getline(loadOrderFile, line);
            if (line[0] == '#' || line.empty())
                continue;

            PluginData plugin;
            plugin.m_filename = line;                 // :103 在擦除 CR 之前赋值

            // On Linux, the carriage return won't be taken into account
            line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());   // :106

            char extensionType = line.back();         // :108 此时 line 可能已被擦成空串

- 问题（已用 g++ 16 独立小程序实测，见文末「验证记录」PROBE-1，不是推断）：
  1. **文件名带 CR 的赋值顺序问题只影响 Linux**。`std::ifstream` 默认文本模式，**Windows 上 CRLF 会被翻译成 LF**，getline 拿到的串里没有 CR，所以 :103 与 :106 在 Windows 上等价。但在 Linux（专用服务器是真实部署形态）上 text 与 binary 没有区别，CRLF 的 CR 原样留在串里：:103 把带 CR 的文件名存进 plugin.m_filename，:106 擦掉 CR 只影响局部变量 line，**m_filename 永远带着尾随 CR**；GetPath（:162-172）拿它和目录里真实文件名比较，永远不相等，于是 LoadFiles（:138-143）对每个插件都打 "Path to plugin file not found" 然后 continue。
  2. **line.back() 于空串是 UB，两个平台都能触发**，只是触发形态不同：
     - Linux：一个**普通的 CRLF 空行**（即 "\r\n"）经 getline 得到只含 CR 的 1 字符串，能通过 :99 的检查（line[0] 是 CR，非 '#'；line 非空），:106 把 CR 擦掉后长度变 0，:108 的 line.back() 就是越界的 *(data() - 1)。实测输出 `afterErase_len=0 <== line.back() on EMPTY STRING = UB`。
     - Windows：需要一行只有 CR（例如 \r\r\n，某些编辑器/混合换行工具会产出），同样的路径命中。
  - 附注：代码注释 "On Linux, the carriage return won't be taken into account" 本身是**对的**（它解释为什么要手工擦 CR），只是没意识到擦除发生在赋值之后。
- 后果：Linux 专用服务器上，一个 CRLF 的 loadorder.txt 会让所有插件"找不到"，同时命中一次未定义行为（std::string::back() 于空串，libstdc++ release 下通常读到 ' ' 而无害，debug 下有 _GLIBCXX_ASSERTIONS 会 abort）。
- 最小改法：把擦除 CR 挪到 getline 之后、任何 line[0]/back()/m_filename 赋值之前，擦完再 if (line.empty()) continue;。
- 可达性：**当前可达**。BuildRecordCollection 的活路径就是 fs::is_directory → LoadLoadOrder() → MakeUnique<RecordCollection>()（ESLoader.cpp:43-54），LoadLoadOrder 在线路径上。这是本次审计里在活代码上确证的唯一一处未定义行为。

### ESL-13 · 低 · 插件 id 计数器溢出（uint8_t 标准位 / uint16_t lite 位）

- 位置：Code/components/es_loader/ESLoader.cpp:115、:121
- 证据：

            case 'p':
                plugin.m_standardId = standardId;
                standardId += 0x01;          // uint8_t，255 之后回绕到 0
                ...
            case 'l':
                plugin.m_liteId = liteId;
                liteId += 0x0001;            // uint16_t，65535 之后回绕

- 问题：没有任何上界检查。超过 255 条标准插件（loadorder.txt 里还可以列出「已存在但未启用」的插件）时标准位回绕，两个插件拿到同一个 formId 前缀（TESFile::Setup(uint8_t) 里 m_formIdPrefix = id * 0x1000000），RecordCollection 里两个插件的记录互相覆盖。
- 后果：静默数据损坏，只在特定 load order 下出现，极难排查。
- 最小改法：if (standardId == 0xFF || liteId == 0xFFFF) { spdlog::error(...); return false; }。
- 可达性：当前可达（同 ESL-12 的路径）。

### ESL-14 · 低 · GetFormIdPrefix 查不到前缀时返回 0，不同插件的记录被折叠到同一 formId

- 位置：Code/components/es_loader/TESFile.cpp:192-206、调用点 Records/Record.cpp:16-20
- 证据：

        if (masterId == std::end(aParentToFormIdPrefix))
        {
            spdlog::warn("Form id prefix not found: {:X}", baseId);
            return 0;
        }

    void Record::SetBaseId(uint32_t aBaseId)
    {
        m_formId &= 0x00FFFFFF;
        m_formId += aBaseId;
    }

- 问题：前缀缺失时返回 0，SetBaseId 于是产出 0x00xxxxxx。所有「前缀不在表里」的记录共享同一段 id 空间。注释里承认的现实（Skyrim.esm 的 iDaysToRespawnVendor 前缀是 0x01）说明这张表确实会缺项。
- 后果：m_allRecords/m_objectReferences 等 Map 里键碰撞，后者覆盖前者（TESFile.cpp:110 是普通 operator[] 赋值）。丢失的记录没有任何提示，只有一条 warn。
- 最小改法：返回一个不可能冲突的哨兵（如 0xFF000000），或在返回 0 时让调用方丢弃该记录并计数。
- 可达性：当前死路径（但 GetFormIdPrefix 是 Chunks::ReadFormId 的公共入口）。

### ESL-15 · 低 · XXXX 大 chunk 路径的 Advance 长度与自身体长不符

- 位置：Code/components/es_loader/Records/Record.cpp:53-64
- 证据：

            if (pChunk->m_chunkId == ChunkId::XXXX_ID)
            {
                reader.ReadBytes(reinterpret_cast<uint8_t*>(&largeDataSize), 4);
                reader.Reverse(4);
            }
            Buffer::Reader chunk(reader);
            reader.Advance(dataSize);     // dataSize == largeDataSize（因为 XXXX 的 m_dataSize 是 0）

- 问题：XXXX 的 chunk 体就是那 4 字节长度，之后才是下一个 chunk 的头。代码 Reverse(4) 把位置退回长度字段起点，然后只 Advance(largeDataSize)——没有跳过这 4 字节长度字段。XXXX 自身的体长恒为 4，前进量应当是 4。
- 后果：largeDataSize != 4 时读位置落到上一个 chunk 数据内部，后续所有 chunk 错位，且错位是级联的。代码注释自称 "Doesn't ever seem to trigger"，故定为低。
- 最小改法：const uint32_t advance = (pChunk->m_chunkId == ChunkId::XXXX_ID) ? 4u : dataSize; reader.Advance(advance);
- 可达性：当前死路径。


## 【运行时依赖】

### RD-01 · 中 · 越界读/越界 Advance 之后 Buffer::Reader 的实际行为未知

- 位置：Code/components/es_loader/**（全部）、依赖 TiltedCore/Buffer.hpp
- 问题：ESL-01/04/05 的**后果**取决于 Buffer::Reader 的实现——越界 ReadBytes 是抛异常、返回 0、还是直接读野内存，越界 Advance 之后 GetBytePosition/Eof 是否还能自洽，仓库内无法确定。Libraries/ 下无 TiltedCore 源码，全盘 find 也没有 Buffer.hpp。
- 证据：docs/PITFALLS.md:329-332 已记录同一结论（「Buffer::Reader 读越界后的行为未知」）。
- 为什么仍然报：ESL-01/04/05 的**事实**（没有边界校验）是可闭合的；只是「后果有多严重」需要 vendor 源码才能定级。如果 Reader 抛异常，那么那些已标 noexcept 的 ParseChunks 会把异常变成 std::terminate（更严重）；如果 Reader 静默越界，那就是越界读（也严重）。两种结局都是缺陷。
- 最小改法：vendor TiltedCore 的 Buffer.hpp 进仓库，或在本仓库内包一层带边界检查的 reader。

---

# 二、SimpleIni 读取路径

## 【可闭合】

### SII-01 · 中 · 配置加载失败被 BASE_ASSERT 吞掉，玩家配置静默回落到默认值

- 位置：Code/components/console/IniSettingsProvider.cpp:104-110、Code/base/Check.h:9-24
- 证据：

    void LoadSettingsFromIni(ConsoleRegistry& aReg, const std::filesystem::path& aPath)
    {
        CSimpleIni ini;
        {
            auto buf = TiltedPhoques::LoadFile(aPath);
            BASE_ASSERT(ini.LoadData(buf.c_str()) == SI_Error::SI_OK, "Failed to load ini data");
        }

    namespace base {
    static void Assert(const char* apExpression, const char* apMessage)
    {
        spdlog::critical("Assertion failed ({}): {}", apExpression, apMessage);
    #if defined(_WIN32)
        if (IsDebuggerPresent())
            __debugbreak();
    #endif
    }
    } // namespace base

- 问题：BASE_ASSERT 在发布版里**只打一条 critical 日志**，不 abort、不返回。于是 ini 解析失败（文件被截断、非法 UTF-8、权限不足导致 LoadFile 返回空串）时，函数继续往下走，ForAllSettings 里每个 GetValue 都返回默认值，然后**写回 setting->data**。整个过程只留下一条 critical 日志。
- 后果：玩家的全部设置（含端口、昵称、密码等）静默变成默认值，且下一次 SaveSettingsToIni 会用默认值覆盖原文件——配置**永久丢失**。
- 最小改法：把 BASE_ASSERT 换成真正的 if (rc != SI_OK) { spdlog::error(...); return; }，加载失败时不要进入写回循环。

### SII-02 · 低 · LoadData(buf.c_str()) 走 const char* 到 std::string 的隐式转换，长度信息丢失

- 位置：Code/components/console/IniSettingsProvider.cpp:109、Code/base/simpleini/SimpleIni.h:616、:625
- 证据：

    SI_Error LoadData(const std::string& a_strData) { return LoadData(a_strData.c_str(), a_strData.size()); }
    SI_Error LoadData(const char* a_pData, size_t a_uDataLen);

    BASE_ASSERT(ini.LoadData(buf.c_str()) == SI_Error::SI_OK, "Failed to load ini data");

- 问题：实参只有一个 const char*，重载决议选中的是 LoadData(const std::string&)（从 c_str 构造一个临时 std::string），而不是 LoadData(const char*, size_t)。这意味着 (a) 多一次完整拷贝；(b) 若 ini 内容含内嵌 NUL（字符串设置值里被写入 NUL，或二进制垃圾），长度在构造 std::string 时就被截断了。
- 后果：截断之后的 ini 被当作完整内容解析，后半段设置静默丢失（与 SII-01 同类后果，但触发面窄）。
- 最小改法：改成 ini.LoadData(buf.c_str(), buf.size())，并检查返回值。

### SII-03 · 低 · SetIniValue 未检查 to_chars 与 ConvertFromStore 的返回值，失败时把未初始化的栈缓冲交给 AddEntry

- 位置：Code/components/console/IniSettingsProvider.cpp:21-32
- 证据：

    template <typename T, typename TVal> static SI_Error SetIniValue(CSimpleIni& ini, const T* a_pSection, const T* a_pKey, const TVal a_nValue, const T* a_pComment = nullptr)
    {
        char szValue[64]{};
        std::to_chars(szValue, szValue + sizeof(szValue), a_nValue);

        // convert to output text
        T szOutput[256];                     // 未初始化
        CSimpleIni::Converter c(ini.IsUnicode());
        c.ConvertFromStore(szValue, std::strlen(szValue) + 1, szOutput, sizeof(szOutput) / sizeof(T));   // 返回值未检查

        return ini.AddEntry(a_pSection, a_pKey, szOutput, a_pComment, false, true);
    }

- 问题：两处返回值都没看。(a) std::to_chars 失败（value_too_large）时不写 szValue，但 szValue 有 {} 初始化，所以落成空串——写入一个空值而不是报错；(b) ConvertFromStore 返回 false 时 szOutput 的**内容不确定**（没有 {} 初始化），却仍被当字符串传给 AddEntry。
- 后果：写出的 ini 里出现空值或垃圾值；下次加载时这些值被解析成默认值。
- 最小改法：if (auto res = std::to_chars(...); res.ec != std::errc()) return SI_FAIL; 以及 if (!c.ConvertFromStore(...)) return SI_FAIL;，并把 T szOutput[256] 改成 T szOutput[256]{}。

### SII-04 · 低 · 多行标签检测 IsMultiLineTag 无长度保护（仅在 m_bAllowMultiLine 打开时可达）

- 位置：Code/base/simpleini/SimpleIni.h:1550-1573
- 证据：

        if (m_bAllowMultiLine && IsMultiLineTag(a_pVal))
        ...
    bool CSimpleIniTempl<...>::IsMultiLineTag(const SI_CHAR* a_pVal) const
    {
        if (*a_pVal++ != '<') return false;
        if (*a_pVal++ != '<') return false;
        if (*a_pVal++ != '<') return false;
        return true;
    }

- 问题：值恰好以两个小于号结尾（长度 1 或 2）时，第三/第二个 *a_pVal++ 读到的是缓冲区自身的 NUL 终止符之后——即越过 m_uDataLen。缓冲区由 new SI_CHAR[uLen+1] + memset 分配，只有一个 NUL，所以长度为 2 的值会读到 [uLen+1] 这个**已越界**的位置。
- 后果：越界读 1-2 字节（最多读到 NUL）。是否造成可观察后果取决于堆布局，故定为低。
- 可达性：**当前不可达**——全仓 CSimpleIni 构造点（IniSettingsProvider.cpp:65/106、ResourceCollection.cpp:110、CellRespawnOverrides.cpp:71/111、server_runner/main.cpp:206）没有一个把 a_bAllowMultiLine 置 true（构造参数是 CSimpleIni(true) = 只开 UTF-8）。这是潜伏缺陷，不是活缺陷。
- 最小改法：IsMultiLineTag 改为接收剩余长度，或在 FindEntry 里先判 strlen(a_pVal) >= 3。

### SII-05 · 低 · LoadData(std::istream&) 的取块循环忽略首字符，含 NUL 时死循环

- 位置：Code/base/simpleini/SimpleIni.h:1380-1390
- 证据：

    SI_Error CSimpleIniTempl<...>::LoadData(std::istream& a_istream)
    {
        std::string strData;
        char szBuf[512];
        do
        {
            a_istream.get(szBuf, sizeof(szBuf), '\0');
            strData.append(szBuf);
        } while (a_istream.good());
        return LoadData(strData);
    }

- 问题：(a) istream::get(char*, streamsize, char delim) 在**第一个字符就是定界符**（这里定界符是 NUL）时不提取任何字符、只置 failbit，而 do-while 只在 good() 为真时继续，所以第一次失败就退出并把**未初始化的 szBuf** 交给 strData.append(szBuf)——未初始化栈读；(b) 若输入中段含 NUL，get 在不提取定界符的情况下停止，下一次调用看到的仍是同一个 NUL，good() 仍为真，于是**无限循环**（每次 append 空串），进程挂死。
- 可达性：当前不可达——SI_SUPPORT_IOSTREAMS 未被任何 TU 定义（全仓 grep 只在 SimpleIni.h 自身内出现），这个重载根本没有被实例化，也没有调用者。
- 最小改法：改成 while (a_istream.getline(...)) 或 std::istreambuf_iterator 的读法，并对 szBuf 做 {} 初始化。


---

# 三、容器与分配器（Code/base/containers、Code/base/allocator）

## 【可闭合】

### CTN-01 · 高 · fast_queue 的 write_acquire 无背压：队列满时阻塞写线程，而唯一的读者就是同一个线程

- 位置：Code/base/containers/fast_queue.hpp:98-109、唯一使用者 Code/components/console/CommandQueue.h:24-33/48、消费端 Code/components/console/ConsoleRegistry.cpp:331-343、驱动端 Code/server_runner/DediRunner.cpp:145-150
- 证据：

    typename fast_queue<T, Exponent>::write_lock fast_queue<T, Exponent>::write_acquire(T*& data)
    {
        auto loc = m_last.fetch_add(1);
        auto& entry = m_buffer[index(loc)];
        const auto rnd = round(loc) << 1;
        while (rnd != entry.state.load(std::memory_order_acquire))
            std::this_thread::yield();          // 队列满时在这里无限自旋
        data = &entry.data;
        return write_lock(loc, *this);
    }

    fast_queue<Item, 2> m_queue;                // Exponent = 2 → kSize = 4，只有 4 个槽

    bool ConsoleRegistry::Update()              // 每 tick 只取 1 条
    {
        if (m_queue.HasWork())
        {
            CommandQueue::Item item;
            m_queue.Fetch(item);
            ...

    bool DediRunner::TickOnce()
    {
        m_pServerInstance->Update();
        if (bConsole)
        {
            uv_run(&m_loop, UV_RUN_NOWAIT);     // 这里可能连续 Upload 多条
            if (m_console.Update())             // 这里每 tick 只 Fetch 一条
                ...

- 问题：fast_queue 是「固定 4 槽 + 满则自旋等待读者推进 state」的设计，**没有任何满队列返回/丢弃路径**（fast_queue.hpp 全文没有 try 版本，write_acquire 的返回类型也不携带「是否成功」）。而唯一的读者 m_console.Update() 由**与 uv_run 相同的线程**调用，并且**每 tick 只消费 1 条**。于是「单次 uv_run 内入队 >= 5 条」就会让第 5 次 write_acquire 自旋等待一个**必须由同一线程**才会推进的 state —— 自旋永不解锁，服务器主循环永久挂死。
- 触发条件：把 5 条以上合法命令一次性喂进 stdin。uv_read_start 的回调（DediRunner.cpp:96-117）逐字节扫换行，**每个换行就调用一次 HandleConsole → TryExecuteCommand → Upload**，全都在同一个 uv_run 里完成。终端粘贴一段命令脚本、或 tmux/expect 类工具批量发送，都能命中。
- 后果：专用服务器无响应、无日志、不退出（Ctrl-C 也进不来，因为信号处理在同一线程）。属于「正常操作即可触发的挂死」，不是理论构造。
- 最小改法：把 write_acquire 改成返回 bool 的非阻塞版本（state 不匹配即返回 false），Upload 在满时记一条 warn 并丢弃；或把 kSize 提高到远超单次 uv_run 可能产生的条目数，并把 Update 改成一次 drain 到底。
- 可达性：**当前可达，且是默认配置**。Code/server_runner/DediRunner.cpp:23 的 `Console::Setting bConsole{"bConsole", "Enable the console", true}` 默认值是 true，main.cpp:464-469 因此在默认启动下就会 StartTerminalIO + RunGSThread。

### CTN-02 · 中 · fast_queue 的 read_acquire 也无限等待，empty() 与 read_acquire 的判据不是同一个量

- 位置：Code/base/containers/fast_queue.hpp:120-132、:143-147、调用点 Code/components/console/ConsoleRegistry.cpp:334-337
- 证据：

    typename fast_queue<T, Exponent>::read_lock fast_queue<T, Exponent>::read_acquire(T*& data)
    {
        auto loc = m_first.fetch_add(1);
        auto& entry = m_buffer[index(loc)];
        const auto rnd = round(loc) << 1;
        while (rnd + 1 != entry.state.load(std::memory_order_acquire))
            std::this_thread::sleep_for(std::chrono::milliseconds(1));   // 无超时、无放弃
        ...

    inline bool fast_queue<T, Exponent>::empty() const
    {
        return m_first.load(std::memory_order_relaxed) == m_last.load(std::memory_order_relaxed);
    }

- 问题：两个独立缺陷。
  1. **empty() 的判据与 read_acquire 的等待条件不是同一个量**。empty() 比较的是 first/last 两个**计数器**，而 read_acquire 等待的是**目标槽的 state**。写者 write_acquire 已经 fetch_add 了 m_last（计数器前进），但 write_release 之前 state 仍是旧值；此时 empty() 返回 false，读者进入 read_acquire 并 fetch_add 了 m_first，然后 sleep 等待 state；一旦写者那条路径抛异常（例如 Item 的 operator= 分配失败），read_release 不执行，state 永远停在 rnd，读者永久 sleep。
  2. empty() 用 memory_order_relaxed 读两个不同原子量，跨线程观察到的 first/last 不保证是同一个瞬间的值。
- 后果：1 的后果是消费端永久挂起（读者已经占掉了 m_first 却拿不到元素）；2 的后果是 empty() 偶尔返回错值，多进一次 read_acquire 就落到 1 的等待里。
- 最小改法：让 empty() 也基于 state 判断（或至少用 acquire），并给 read_acquire 一个可放弃的上限；根本修法是统一由「槽 state」做唯一真相，而不是 first/last 计数器。
- 可达性：当前可达（同上）。

### CTN-03 · 中 · fast_queue 的 entry.data 全程只构造一次，read_release 后不析构也不复位

- 位置：Code/base/containers/fast_queue.hpp:86-94、:106、:129
- 证据：

        struct alignas(hardware_destructive_interference_size) entry
        {
            std::atomic<size_t> state{ 0 };
            T data;                     // 无初始化器
        };
        ...
        entry m_buffer[kSize];          // 固定数组，T 必须可默认构造

- 问题：m_buffer 是值成员数组，所以 T 在 fast_queue 构造时被默认构造一次，之后**再也不构造/析构**——队列在语义上是「搬运对象」，实现上是对同一个 T 对象反复赋值。这对 CommandQueue::Item（含 UniquePtr 与持有 std::any 的 ArgStack）是可行的（赋值即释放旧值），但它把「槽里始终存在一个已构造的 T」变成了隐式约定：任何依赖「元素构造/析构次数」的 T（用 RAII 计数、或持有必须在消费时释放的资源）都会出错。read_release 之后队列仍持有上一轮的 T 值（对 Item 来说是一份已 move 走的 ArgStack 加一个残留的 CommandBase*）。
- 后果：不是当前崩溃，而是**契约陷阱**：一旦有人换用 non-trivial 的 T 就会泄漏或双释放。
- 最小改法：在注释里写明「T 全程只构造一次，release 不析构」；或改用 aligned_storage 加 placement new/destroy。
- 可达性：当前可达，但仅在未来换 T 时爆。

## 【运行时依赖】

### ALLOC-01 · 中 · 替换的 operator new 允许返回 nullptr，违反可替换分配函数的契约

- 位置：Code/base/allocator/AllocatorSymbolOverride.cpp:6-19
- 证据：

    void* TiltedAlloc(const size_t acSize)
    {
        return TiltedPhoques::Allocator::GetDefault()->Allocate(acSize);
    }
    ...
    void* operator new(size_t size)
    {
        return TiltedAlloc(size);      // 没有判空、没有抛 bad_alloc
    }

- 问题：C++ 要求**非 nothrow 形式**的 operator new 在失败时抛 std::bad_alloc，不得返回 nullptr。这里把 new / new[] 直接转发给 TiltedAlloc，函数体里没有判空也没有抛出。若 Allocate 返回 nullptr，new 表达式会带着空指针继续，调用方随后写入即空指针解引用。
- 后果：堆耗尽时不是标准的 bad_alloc（可被上层 try/catch 接住），而是空指针写。这与仓库里大量以 try/catch 兜 bad_alloc 的设计（docs/PITFALLS.md 第 104-107 节整套修复）直接冲突：那些 catch 永远接不到这种情况。
- 为什么是运行时依赖：Allocate 失败时到底返回 nullptr 还是自己 terminate，取决于 TiltedCore 的 Allocator 实现（不在本仓库）。
- 最小改法：if (auto* p = TiltedAlloc(size)) return p; throw std::bad_alloc{};，nothrow 版本保持返回 nullptr。
- 附带说明（同一文件，不算缺陷）：没有重写 C++17 的 aligned 版 operator new/delete（align_val_t）。这里**不构成**分配器错配——因为一旦不重写，new 与 delete 两侧都会落到运行库的默认 aligned 版本，配对自洽。真正要小心的是跨 DLL 边界（一边链接了本重写、一边没有），那属于构建配置问题，不适合静态判定。


---

# 四、前端（Code/skyrim_ui/src 的 TS）与游戏进程通信

范围说明：只看 JS 与 C++ 之间的桥接、序列化、生命周期。三层名字必须逐字一致：
1. 注册层 Code/tp_process/ProcessHandler.cpp:13-38
2. 派发层 Code/client/Services/Generic/OverlayClient.cpp:45-89
3. 调用层 Code/skyrim_ui/src/app/services/*.ts

（docs/PITFALLS.md 第 6 节与第 18294 行起已系统记录过「注册/派发/调用三层不一致」这一类缺陷，本节不重复已修好的 openPlayGuide 与 deactivate；下面只报当前仍成立的。）

## 【可闭合】

### UI-01 · 高 · OverlayClient 在派发前无条件读 eventArgs 的固定下标，短参数列表即越界或空指针解引用

- 位置：Code/client/Services/Generic/OverlayClient.cpp:27-43（关键 :31-39），对照注册层 Code/tp_process/ProcessHandler.cpp:13-38
- 证据：

    if (message->GetName() == "ui-event")
    {
        auto pArguments = message->GetArgumentList();

        auto eventName = pArguments->GetString(0).ToString();
        auto eventArgs = pArguments->GetList(1);

        spdlog::info(eventName);
        spdlog::info(eventArgs->GetString(0).ToString());     // 无条件读下标 0
        spdlog::info(std::to_string(eventArgs->GetInt(1)));   // 无条件读下标 1
        spdlog::info(eventArgs->GetString(2).ToString());     // 无条件读下标 2

        ...
        if (eventName == "connect")
            ProcessConnectMessage(eventArgs);

- 问题：这 4 行是**派发之前的日志**，对**每一个** ui-event 执行，不区分事件类型。可是注册层里大多数函数根本不带 3 个参数：
  - abandonAttempt、disconnect、revealPlayers、launchParty、leaveParty、toggleDebugUI、openPlayGuide、deactivate —— 0 个参数
  - createPartyInvite、acceptPartyInvite、kickPartyMember、changePartyLeader、teleportToPlayer —— 1 个参数
  - sendMessage（type, content）、setTime（hours, minutes）—— 2 个参数
  也就是说，点「离开队伍」「打开攻略」「取消连接」这些正常按钮，都会走到 eventArgs->GetInt(1) 去读一个不存在的下标。另外，若 JS 侧调用时不传第二个实参，CefListValue 只有 1 个元素，GetList(1) 返回 nullptr，:37 立刻空指针解引用；GetString(0) 在下标不存在时返回空 CefString，GetInt(1) 的返回值取决于 CEF 的越界读行为。
- 后果：CEF 的参数越界读在 release 下是未定义行为，在 debug 下 CEF 内部有 DCHECK/CHECK 会直接终止渲染进程或游戏进程。而这段代码唯一的用途是**调试日志**，与功能无关。
- 最小改法：把这三行移到对应分支内部，或加一句 if (eventArgs && eventArgs->GetSize() >= 3)；更好的是按 eventName 决定是否需要这些字段，因为它们本来就只属于 sendMessage。
- 可达性：**当前可达**。这是本次审计里最直接可复现的一条——每次点击无参按钮都会经过它。

### UI-02 · 中 · ChatMessageType 由 JS 侧原样传入并 static_cast，无范围校验

- 位置：Code/client/Services/Generic/OverlayClient.cpp:151-163（关键 :157）、枚举定义 Code/encoding/ChatMessageTypes.h:3-10、JS 侧 Code/skyrim_ui/src/app/services/chat.service.ts:6-12 与 :47
- 证据：

    enum ChatMessageType : unsigned char
    {
        kSystemMessage = 0,
        kGlobalChat,
        kPlayerDialogue,
        kPartyChat,
        kLocalChat          // = 4，合法上界
    };

    void OverlayClient::ProcessChatMessage(CefRefPtr<CefListValue> aEventArgs)
    {
        std::string contents = aEventArgs->GetString(1).ToString();
        if (!contents.empty())
        {
            SendChatMessageRequest messageRequest;
            messageRequest.MessageType = static_cast<ChatMessageType>(aEventArgs->GetInt(0));   // 无范围校验
            messageRequest.ChatMessage = contents;

- 问题：JS 侧的 MessageTypes（chat.service.ts:6-12）与 C++ 的 ChatMessageType 顺序一致（0 到 4），但这是两个文件各自手写的映射，编译期不交叉校验。任何能在该 CEF 上下文执行 JS 的东西都可以传任意 int。static_cast 到底层类型 unsigned char 的枚举不做范围检查：传 300 静默截断成 44，再经序列化发到服务端。
- 后果：服务端 sendPlayerMessage 的 switch 有 default 分支（OverlayService.cpp:61 打 error 后丢弃），所以不会越权；但非法值会先经过序列化再被拒绝，属于「带到服务端才拦」。
- 最小改法：在 :157 之前加 if (aEventArgs->GetInt(0) > kLocalChat) return;
- 可达性：当前可达，但需要能执行 JS，故定为中。

### UI-03 · 中 · client.service.ts 的 ngOnDestroy 只按事件名 off，而注册用的是 bind(this) 产生的新函数对象

- 位置：Code/skyrim_ui/src/app/services/client.service.ts:125-161（注册）、:166-194（注销）；typings Code/skyrim_ui/src/typings.d.ts:227 起（off 的 callback 是可选参数）
- 证据：

    // 构造函数
    skyrimtogether.on('init', this.onInit.bind(this));
    ...
    // ngOnDestroy
    skyrimtogether.off('init');
    skyrimtogether.off('activate');
    ...

- 问题：on 的实参是 this.onInit.bind(this)，每次求值都是新的函数对象，而 off 调用**没有传 callback**。typings.d.ts 把 callback 标成可选，所以「不传就移除该名字下全部监听」是合法语义；但 bind 产生的包装函数只能靠「移除该事件名下全部监听」清掉，而 client.service 是 providedIn root 的单例，ngOnDestroy 实际上不会被 Angular 调用。
- 后果：这不是崩溃，而是**「销毁即解绑」的保护是个空操作**：一旦将来该 service 的生命周期变成随组件创建，旧监听会叠加，同一事件触发 N 次。
- 最小改法：把 bind 结果存成字段（this.boundInit = this.onInit.bind(this)），注册与注销用同一个引用；或删掉 ngOnDestroy 并注明 service 是单例、无需注销。
- 可达性：当前不构成可观察故障（单例），故定为中。

### UI-04 · 中 · ChatService 构造函数注册的 message 监听从不注销，且注册点分散

- 位置：Code/skyrim_ui/src/app/services/chat.service.ts:101-107（关键 :102）
- 证据：

    constructor() {
      skyrimtogether.on('message', this.onMessageRecieved.bind(this));

      this.CommandHandler = new CommandHandler(this);
      ...
    }

- 问题：与 UI-03 同源，但这一处**连注销代码都没有**（ChatService 没有 ngOnDestroy，也没有 off('message')）。全仓 grep 显示 off() 只集中在 client.service.ts 的 ngOnDestroy 里。
- 后果：同 UI-03（单例因此当前无害），但它是「注册点分散、注销点集中在一个文件」的结构性后果：任何新增服务自己 on() 时，没有任何机制保证它被 off()；叠加 UI-03 里 off 不带 callback 的语义，即使服务被销毁也解不掉 bind 出来的包装函数。
- 最小改法：让 ChatService 实现 OnDestroy 并 off('message')；或把整个桥接收进一个集中注册/注销的 ClientService。
- 可达性：同 UI-03。

### UI-05 · 低 · notifications 组件用 setTimeout 管理条目，组件销毁后定时器仍持有数组引用

- 位置：Code/skyrim_ui/src/app/components/notifications/notifications.component.ts:16-42（关键 :28-31），对照同目录里写对的 notification-popup.component.ts:35-37
- 证据：

      public constructor(
        private readonly destroy$: DestroyService,
        private readonly chatService: ChatService,
      ) {
        this.chatService.messageList
          .pipe(takeUntil(this.destroy$))
          .subscribe(message => {
            this.notifications.push({
              ...message,
              timer: setTimeout(() => {
                this.notifications.shift();
              }, 5000),
            });
          });

- 问题：订阅本身用 takeUntil 正确解绑了，但**已经排上队的 setTimeout 没有被清理**：组件销毁时不 clearTimeout，回调仍然持有 this.notifications 并调用 shift()。同目录的 notification-popup.component.ts:35-37 就在 ngOnDestroy 里调了 stopTimer()，这一处漏了。
- 后果：每个待显示通知泄漏一个 5 秒定时器与一个数组；组件反复创建销毁时定时器会短暂堆积。功能上无害（shift 空数组是 no-op），故定为低。
- 最小改法：实现 ngOnDestroy，遍历 this.notifications 逐个 clearTimeout 并清空数组。

### UI-06 · 低 · settime 命令的 C++ 侧用 uint8_t 接收，截断后可绕过 UI 侧校验

- 位置：Code/skyrim_ui/src/app/services/chat/commands.ts:25-49（关键 :36-38）、C++ 侧 Code/client/Services/Generic/OverlayClient.cpp:166-177
- 证据：

      const hours = parseInt(args[0]);
      const minutes = parseInt(args[1]);
      if (hours < 0 || hours > 23 || minutes < 0 || minutes > 59 || Number.isNaN(hours) || Number.isNaN(minutes)) {

    void OverlayClient::ProcessSetTimeCommand(CefRefPtr<CefListValue> aEventArgs)
    {
        const uint8_t hours = static_cast<uint8_t>(aEventArgs->GetInt(0));
        const uint8_t minutes = static_cast<uint8_t>(aEventArgs->GetInt(1));

- 问题：JS 侧范围判断**是正确的**（0 到 23、0 到 59，NaN 也拦了），所以这条不是「UI 不校验」。问题在 C++ 侧：300 会被 static_cast 截断成 44，从而**绕过** UI 的校验变成一个合法值。C++ 侧没有任何自己的范围检查，而 CalendarService::SetTime（CalendarService.cpp:80）拿到的是已经截断过的值。
- 后果：一个本应被拒绝的输入变成合法 hour/minutes。SetTime 自身还有一次检查，所以不会污染时间；缺陷是「校验放在唯一能绕过的位置上」。
- 最小改法：在 ProcessSetTimeCommand 里用 int 接收再判范围，通过后才转 uint8_t。
- 可达性：当前可达，但需用户手动输入超范围的值。

## 【运行时依赖】

### UI-07 · 低 · CefListValue 越界读写的确切后果取决于 CEF 版本

- 位置：同 UI-01、UI-02
- 问题：CEF 的 CefListValue::GetInt/GetString 在下标越界时的行为没有跨版本保证：某些版本返回默认值（0 / 空串），某些版本在 debug 构建里走 DCHECK 直接崩。本仓库不 vendor CEF，工作区里没有 cef 头文件（docs/RELEASE-AND-MO2.md:58 只提到 libcef.dll 是部署产物）。
- 为什么仍然报：UI-01 的**事实**（在派发前无条件读 3 个下标，而大多数事件只带 0 到 2 个参数）是可闭合的；只是「越界读会不会崩」需要 CEF 版本才能定级。
- 最小改法：同 UI-01。

---

# 五、skse_bootstrap / skse_client / tp_process

## 【可闭合】

### SKSE-01 · 中 · 声明了 kVersionIndependent_Signatures，但 skse_client 仍在扫 address library

- 位置：Code/skse_bootstrap/main.cpp:70-108（结构体与 static_assert）、:810-820（导出）、Code/skse_client/main.cpp:163-214（ScanLibraryVersion）
- 证据：

    __declspec(dllexport) SksePluginVersionData SKSEPlugin_Version = {
        SksePluginVersionData::kVersion,      // dataVersion
        1,                                    // pluginVersion
        "SkyrimTogether",
        "",
        "",
        SksePluginVersionData::kVersionIndependentEx_NoStructUse,
        SksePluginVersionData::kVersionIndependent_Signatures,
        {},                                   // compatibleVersions 全零
        0,                                    // seVersionRequired
    };

- 问题：这份 hand-written 镜像的**布局**已经被三条 static_assert（sizeof == 848、name 偏移 8、versionIndependenceEx 偏移 772、seVersionRequired 偏移 844）钉死，这一点做得对。缺陷在**语义**：kVersionIndependent_Signatures 声明「不依赖 address library」，而 skse_client 的 ScanLibraryVersion 正是在扫 address library 文件（version-*.bin / versionlib-*.bin）。两者不矛盾（扫描只是版本探测的 fallback，不是 id 解析依赖），但 flags 处 :62-69 的注释只解释了「为什么要声明它」，没有解释「声明之后仍然去读 address library 文件是否仍算合规」。
- 后果：这是 ABI/约定层面的可维护性风险而非当前 bug——SKSE 只看 flags，不看 ScanLibraryVersion。但它意味着任何基于 flags 认为「本插件不需要 address library」的推理都是错的。
- 最小改法：在 flags 的注释里补一句「Signatures 只表示不依赖 Address Library 的 id 解析；skse_client 仍把它当版本探测的 fallback」。
- 可达性：**当前可达**（每次 SKSE 加载这个 dll 都读）。

### SKSE-02 · 低 · SKSEPlugin_Query 忽略传入的 SKSE 侧信息，不做版本协商

- 位置：Code/skse_bootstrap/main.cpp:836-844
- 证据：

    __declspec(dllexport) bool SKSEPlugin_Query(const void* apInfo, SksePluginInfo* apInfo)
    {
        if (!apInfo)
            return false;
        apInfo->infoVersion = kSksePluginInfoVersion;
        apInfo->name = "SkyrimTogether";
        apInfo->version = 1;
        return true;
    }

- 问题：第一个形参（SKSE 传进来的查询信息）被命名为 apQuery 之类才有意义，实际写法里两个形参同名之外的细节是：第一个参数是 const void* 且函数体里**完全没用**。真正的 SKSE 版本协商（「本插件要求 SKSE >= X」）因此没有发生，插件对任何 SKSE 版本都返回 true。
- 后果：在一个过旧、不认识 kVersionIndependentEx 标志的 SKSE 上，插件会**加载成功**，然后在 SKSEPlugin_Load 里做完全部文件部署，最后 LoadLibrary 失败——比在 Query 阶段直接拒绝更难诊断。
- 最小改法：把第一形参换成 SKSE 的实际查询结构体类型并读版本；或至少在注释里写明「故意不做版本协商，理由是什么」。当前类型是 const void*，所以不先改签名就做不到——这也是定低的原因。
- 可达性：当前可达。

### SKSE-03 · 中 · skse_bootstrap 的 IsLegacyGame 判定「命中即返回」，与 skse_client 的「继续扫」规则不一致，而后者注释自称两者必须一致

- 位置：Code/skse_bootstrap/main.cpp:147-166（IsLegacyGame）、Code/skse_client/main.cpp:102-125（GetSKSEModuleVersion）
- 证据：

    // skse_bootstrap IsLegacyGame :147-166
    for (BOOL ok = Module32FirstW(snap, &me); ok; ok = Module32NextW(snap, &me))
    {
        const std::wstring name = me.szModule;
        if (name.rfind(L"skse64_", 0) != 0)
            continue;
        int major = 0, minor = 0;
        if (swscanf_s(name.c_str() + 7, L"%d_%d", &major, &minor) >= 2)
        {
            CloseHandle(snap);
            return major == 1 && minor == 5;      // 命中即返回，且不匹配也返回 false
        }
    }

    // skse_client GetSKSEModuleVersion :109-121
    for (BOOL ok = Module32FirstW(snap, &me); ok; ok = Module32NextW(snap, &me))
    {
        const std::wstring name = me.szModule;
        if (name.rfind(L"skse64_", 0) != 0)
            continue;
        int a = 0, b = 0, c = 0;
        if (swscanf_s(name.c_str() + 7, L"%d_%d_%d", &a, &b, &c) >= 3)
        {
            ...
            break;                                 // 解析成功才停
        }
    }

- 问题：skse_client/main.cpp:99-101 的注释明确写着「Code/skse_bootstrap/main.cpp 对同一份列表问同一个问题……两者不能不一致，保持一致」。但实际规则不同：
  1. 格式不同：bootstrap 用两段（skse64_1_5_97 解析成 1, 5），client 用三段（解析成 1, 5, 97）。在 1.5.x 上恰好都得出「1.5」，所以看不出差别。
  2. **控制流不同，且这才是真正的缺陷**：bootstrap 一旦遇到任何能解析出两段的 skse64_ 名字就**无条件 return**，包括 major/minor 不是 1/5 的情形——那种情形返回 false 并且**放弃继续扫描**。client 侧只在解析成功时才 break，注释 :88-94 说明了原因（skse64_steam_loader.dll 带同样前缀但不含版本，枚举顺序不由本进程决定）。
- 后果：若模块列表里排在前面的某个 skse64_ 模块解析出非 1.5 的两段（例如一个残留的 skse64_1_6_xxx.dll 与真正的 1.5.97 同时存在，而前者先在枚举里出现），bootstrap 判成「非 legacy」并停止扫描，于是去加载 1.6/1.7 的 runtime DLL，在 1.5.x 游戏上 LoadLibrary 失败，报「Windows refused」。client 侧不会犯这个错，两边的答案会分歧——正是注释声称要避免的。
- 最小改法：bootstrap 改成与 client 一致：解析成功且 major==1 && minor==5 才 return true，解析成功但不匹配则 continue，扫完才 return false。
- 可达性：**当前可达**（每次启动都跑，bootstrap 在 SKSEPlugin_Load 里、client 在 STClient_Bootstrap 里）。

### SKSE-04 · 中 · 部署「已是最新」快路径只 stat 两个 dll 的存在性，不校验文件清单与内容

- 位置：Code/skse_bootstrap/main.cpp:585-607（快路径）、:658-665（marker 写出）、同文件 :759-769（kDeps 列表）
- 证据：

    const auto clientDllsPresent = [&acGameRoot]() {
        std::error_code dllEc;
        return std::filesystem::exists(acGameRoot / kClientDllName, dllEc) ||
               std::filesystem::exists(acGameRoot / kClientDllLegacyName, dllEc);
    };

    if (std::filesystem::exists(deployedMarker, markerEc) && SameBuild(marker, deployedMarker) && clientDllsPresent())
    {
        ...
        return true;      // 完全跳过 per-file 扫描
    }

- 问题：marker 只描述「上一次部署来自哪个 commit」，**不描述那次部署了哪些文件、每个文件的字节或大小**。代码注释 :580-584 自己承认了一半（「marker 无法为部署之后被删除或被隔离的文件作证」），但实际缺口比注释更大：
  1. clientDllsPresent 只 stat **两个特定名字**。libcef.dll、icudtl.dat、resources.pak、locales/ 下任一文件被 AV 隔离或用户删掉，快路径都不会发现；
  2. 内容被换成另一个版本的同名文件也不会发现（不比较字节）；
  3. 于是失败从「文件缺失」变成 LoadLibraryW 抛错，用户看到的是「Windows refused with error N」（:774-775）。
- 后果：部署完整性依赖一个只记录 commit 的单个文件；诊断信息指向错误的方向。这与 SKSE-06 叠加时尤其难查。
- 最小改法：marker 里带文件清单（名字 + 大小），快路径逐个 stat 大小；或至少把 kDeps 那四个（libcef.dll、chrome_elf.dll、d3dcompiler_47.dll、TPProcess.exe）都纳入 clientDllsPresent。
- 可达性：**当前可达**（每次启动的快路径判断）。

## 【运行时依赖】

### SKSE-05 · 中 · SksePluginVersionData 的 848 字节布局按某个 SKSE64 版本写死，跨 SKSE 版本不保证

- 位置：Code/skse_bootstrap/main.cpp:70-108、:810-820
- 问题：三条 static_assert 保证的是「这份结构体自身的大小与关键偏移是 848 / 8 / 772 / 844」，它们**不能**保证「SKSE 运行时读的也是这个布局」。SKSE 是外部二进制，它的 PluginVersionData 定义由 SKSE 侧决定。仓库内的 static_assert 只能防止**自己**改错，防不了对面改了。
- 后果：若某个 SKSE 版本的布局不同，SKSE 会把 name 读成 author、把 flags 从版本列表里读出来——而导出仍然存在、名字仍然叫 SKSEPlugin_Version，失败是静默的（代码注释 :91-100 自己也这么说）。表现就是「插件加载了但什么都没发生」。
- 最小改法：纯静态层面修不好。唯一可用的协商点是 dataVersion：SKSE 会检查它，未来改了布局通常会改这个字段；当前代码硬写成 kVersion = 1 且没有读取或适配逻辑。
- 为什么定运行时依赖：需要 SKSE 各版本的实际头文件才能确认布局是否真的变过，本机没有。

### SKSE-06 · 低 · IsLegacyGame 的 exe 版本 fallback 硬编码语言代码页 040904B0

- 位置：Code/skse_bootstrap/main.cpp:186-208
- 证据：

    const wchar_t* kVersionKeys[] = {
        L"\\StringFileInfo\\040904B0\\ProductVersion",
        L"\\StringFileInfo\\040904B0\\FileVersion",
    };
    ...
    if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&ffi), &len) || !ffi)
        return false;
    return HIWORD(ffi->dwFileVersionMS) == 1 && LOWORD(ffi->dwFileVersionMS) == 5;

- 问题：040904B0 是英语(美国)加 Unicode 的固定语言与代码页。非英语版 Skyrim 的版本资源里这个键不存在，VerQueryValueW 返回 false，于是 fallback 直接落到结构化的 VS_FIXEDFILEINFO——而注释 :168-170 自己指出结构化字段在破解版上不可靠（CODEX 把它 stamp 成 1.0.0.0）。
- 后果：非英语版加破解版的组合下，IsLegacyGame 会把 1.5.x 判成非 legacy，于是去加载 1.6/1.7 的 runtime DLL，失败信息是 Windows refused，而不是版本判断错了。
- 最小改法：先用 VerQueryValueW 取 VarFileInfo 下 Translation 的实际语言代码页，再拼 StringFileInfo 路径，而不是硬编码 040904B0。
- 为什么定运行时依赖：取决于玩家实际安装的语言版本与版本资源形态。

### SKSE-07 · 中 · RipAllocateN 的无符号倒序扫描在到达下界后下溢，会一直扫到地址空间顶端

- 位置：Code/skse_client/main.cpp:31-46
- 证据：

    void* RipAllocateN(size_t blockLength)
    {
        const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const uintptr_t step = 0x10000;
        const uintptr_t minAddr = base > 0x40000000ull ? base - 0x40000000ull : step;
        const uintptr_t maxAddr = base + 0x40000000ull;

        for (uintptr_t addr = maxAddr; addr >= minAddr; addr -= step)
        {
            if (void* p = VirtualAlloc(reinterpret_cast<void*>(addr), blockLength, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE))
                return p;
        }

        return VirtualAlloc(nullptr, blockLength, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    }

- 问题：循环变量是无符号的，addr >= minAddr 在 addr 等于 minAddr 时仍然成立，于是执行 addr -= step 后下溢成接近 UINTPTR_MAX 的巨值，条件再次成立——循环继续从地址空间顶端向下扫。也就是说「扫完 ±1GB 就掉到最后的 VirtualAlloc(nullptr, ...)」这个设计意图**永远不会发生**：它实际上要扫完整 2 的 64 次方范围才会退出。
- 后果：一旦 ±1GB 内没有可用页（地址空间碎片化、base 很高、或 blockLength 大于该范围内的空闲块），游戏启动就卡死在这个循环里，且没有任何日志。
- 最小改法：循环条件改成严格大于（addr > minAddr），并在循环之后再单独试一次 minAddr 本身；或直接改成从 minAddr 向上递增扫描。
- 为什么定运行时依赖：是否真的扫不到取决于进程地址空间布局，静态只能证明「扫不到时不会优雅退出」。

### SKSE-08 · 低 · 扫描循环把可写又可执行的内存作为首选

- 位置：Code/skse_client/main.cpp:38-45
- 证据：

            if (void* p = VirtualAlloc(reinterpret_cast<void*>(addr), blockLength, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE))
                return p;
        }

        // last resort: any executable page
        return VirtualAlloc(nullptr, blockLength, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);

- 问题：W^X 违规——同一页同时可写与可执行。另有一个小问题：注释说「last resort: any executable page」，但代码并没有请求任何可执行属性之外的东西，两次调用参数完全一致（只差地址），所以这不是「另一种手段」，只是「扫不到就随便给一块」。
- 后果：稳定的静态特征，杀软与 CFG/ACG 会拦；在开了 Arbitrary Code Guard 的进程里 VirtualAlloc 加 PAGE_EXECUTE_READWRITE 直接失败，函数返回 nullptr 而调用方（见下）没有判空。
- 最小改法：MEM_RESERVE 加 MEM_COMMIT 后先给 PAGE_READWRITE，写完再 VirtualProtect 成 PAGE_EXECUTE_READ。
- 说明：RipAllocateN 在本仓库内**没有调用点**（全仓 grep 只有定义），它由 client 库通过外部符号引用——属于运行时依赖部分。

### SKSE-09 · 低 · RipAllocateN 的返回值为 nullptr 时无兜底

- 位置：Code/skse_client/main.cpp:44-45
- 问题：函数返回 void* 且允许返回 nullptr（VirtualAlloc 失败时），本文件内没有调用点可以检查；但注释 :29-30 说这是「jit stub allocations」的来源。若调用方不判空就写，是空指针写。
- 最小改法：无法在本文件内修，需在调用方检查；至少在函数的注释里写明「可能返回 nullptr」。
- 为什么定运行时依赖：调用点在 client 库内，不在本仓库。

---

# 六、tp_process

## 未发现缺陷（说明理由）

- Code/tp_process/main.cpp:88-94：WinMain 先调 KeepGameRootProxiesOutOfThisProcess 再进 UIMain，四个 PreloadSystemDll 的顺序与理由写在 :59-78（dxgi 必须先于 d3d11，因为 d3d11 静态导入 dxgi）。GetSystemDirectoryW 的返回长度做了 length == 0 或 length >= MAX_PATH 的检查（:48-50），路径拼接正确。
- Code/tp_process/ProcessHandler.cpp:9-39：只在 OnContextCreated 里往 m_pCoreObject 挂 V8 函数，每个 SetValue 的返回值（bool）都没检查——但 m_pCoreObject 若为 nullptr，上一步 OverlayRenderProcessHandler::OnContextCreated 已经失败，这里会立刻空指针崩；这属于 TiltedUI 的契约（不在本仓库），因此不作为缺陷上报。
- 该目录唯一的静态可证问题是：**注册列表与派发列表的一致性**依赖外部脚本 Tools/Scripts/check_overlay_bridge.py 把关（ProcessHandler.cpp:35-36 注释自述），而它已经在 CI 里（48 个门禁中的一个）。本轮用脚本逐条比对（从 SetValue 提取 16 个名字、从 eventName == 提取 16 个分支）：**两个方向都无差集**，注册层与派发层完全对齐，逐字不一致这一类缺陷当前不存在。剩下的只有 UI-01 的参数数量问题（注册层的参数个数与派发前那 3 次固定下标读不匹配）。


---

# 七、Code/common（Cell.cpp、Map.cpp、DateTime.cpp）

（Cell.cpp 5 行、Map.cpp 6 行，都是空实现：Cell::Cell() 空构造、Map::At 恒返回 nullptr。两者在仓库内都**没有调用点**（grep 确认），因此不构成缺陷，见 INV-05。）

## 【可闭合】

### CMN-01 · 低 · GetNumberOfDaysByMonthIndex 对负下标数组越界

- 位置：Code/common/DateTime.cpp:9-14
- 证据：

    const uint32_t cDayLengthArray[12] = {31u, 28u, 31u, 30u, 31u, 30u, 31u, 31u, 30u, 31u, 30u, 31u};

    uint32_t DateTime::GetNumberOfDaysByMonthIndex(int aIndex)
    {
        return cDayLengthArray[aIndex % 12];
    }

- 问题：C++ 的 % 对负数返回负值。aIndex = -1 时 -1 % 12 == -1，于是 cDayLengthArray[-1] —— **越界读**（读数组前一个 uint32）。aIndex = -12 时得 0，返回 1 月（错误但安全）。
- 后果：越界读 4 字节；是否可观察取决于链接布局。
- 可达性：**当前不可达**。全仓三个调用点都是非负：DateTime.cpp:23 与 :25（m_timeModel.Month 是 uint32_t，且 TimeModel::Deserialize 把 Month 夹到 kMaxMonth=11）、DateTime.cpp:42（循环变量 i 从 0 到 Month）、CalendarService.cpp:100（aMonth 已在 :98 校验 0 到 11）。这是「参数当前已经受控、但函数本身没有自我保护」的防御性缺口，故定低。
- 最小改法：return cDayLengthArray[((aIndex % 12) + 12) % 12];

## 【运行时依赖】

### CMN-02 · 中 · 客户端把引擎浮点时间原样写进 TimeModel 的整数字段，靠隐式 float 到 uint32_t 转换

- 位置：Code/client/Services/Generic/TransportService.cpp:474-478（发送侧）、Code/encoding/Structs/TimeModel.h:14-16（字段类型）、Code/common/DateTime.cpp:38-46（消费侧）
- 证据：

        request.PlayerTime.TimeScale = pGameTime->TimeScale->f;
        request.PlayerTime.Time = pGameTime->GameHour->f;
        request.PlayerTime.Year = pGameTime->GameYear->f;      // float 到 uint32_t 的隐式转换
        request.PlayerTime.Month = pGameTime->GameMonth->f;
        request.PlayerTime.Day = pGameTime->GameDay->f;

        // TimeModel.h
        uint32_t Year = 1;
        uint32_t Month = 1;
        uint32_t Day = 1;

- 问题：TimeModel 的 Year/Month/Day 是 uint32_t，而引擎侧这几个值（pGameTime->GameYear->f 等）是 float。float 到无符号整数的转换在值为负或超出可表示范围时是**未定义行为**（[conv.fpint]）。引擎在加载画面、控制台改时间、或某些 mod 直接写内存时可能给出负值或 NaN。
- 后果：未定义行为发生在**客户端**，而夹取是在**服务端**（TimeModel::Deserialize，TimeModel.cpp:44-51）才做的——防线在错误的一侧：客户端先把 UB 走完，才把值发出去。
- 重要限定：服务端侧的处理本身是**正确的、不需要改**。TimeModel.cpp:44-51 的注释明确写了 "Never trust the wire"，五个字段全部夹取，且 ClampFloat（:13-19）把 NaN 与 Inf 都降成 0。这一条不是质疑服务端 clamp，是指出**发送侧**缺同样的保护。
- 最小改法：发送前先做 static_cast<uint32_t>(std::clamp(std::isfinite(v) ? v : 0.f, 0.f, 999.f))，与 TimeModel 的 kMaxYear/kMaxMonth/kMaxDay 常量对齐。
- 为什么定运行时依赖：引擎在这几个字段里到底会不会给出负值或 NaN，取决于游戏运行时行为，静态无法确定。

---

# 八、验证记录

本机的 g++ 16 可用，因此对三条**可以脱离仓库编译的语义**做了实测（源代码与二进制都放在 .audit/_probe/，不属于仓库源码）。

## PROBE-1 · loadorder.txt 的 CRLF 行为（对应 ESL-12）

方法：把 ESLoader.cpp:95-127 的循环体逐字复制成一个独立程序，喂入 CRLF 文件，分别在文本模式与二进制模式下跑。

    文本模式（Windows 默认）:  raw_len=10 raw_tail=109 ; afterErase_len=10  ext='m'
    二进制模式（Linux 等价）:  raw_len=11 raw_tail=13  ; afterErase_len=10  ext='m'   <== filename keeps trailing CR
    含单独 CR 的空行（文本模式）:  raw_len=1 raw_tail=13 ; afterErase_len=0  <== line.back() on EMPTY STRING = UB

结论：**(a)** 「文件名带 CR 导致所有插件找不到」只在 Linux 上成立（Windows 的文本模式会翻译 CRLF），我最初写成「Windows 主线路径上」是错的，已改正为 Linux-only。**(b)** line.back() 于空串是真实的 UB，Linux 上一个普通的 CRLF 空行就能命中，Windows 上需要一行只有 CR。

## PROBE-2 · fast_queue 满队列阻塞（对应 CTN-01）

方法：把 fast_queue.hpp 的 write_acquire/read_acquire/write_release/read_release/empty 逐字复制，容量按 CommandQueue.h:48 的 Exponent=2（即 4 槽），模拟「一次 uv_run 里连续上传 6 条命令、中途没有 Update()」。

     queue capacity = 4
     uploading command 1 ... 2 ... 3 ... 4 ...
     uploading command 5 ...
       write_acquire STUCK after 2000001 spins (queue full)

结论：**第 5 次上传即永久自旋**。这不是推断——把自旋计数换成 abort 才能让程序退出。DediRunner.cpp:107-116 的 stdin 回调对每个换行调用一次 HandleConsole，全在一次 uv_run 内，而 ConsoleRegistry::Update 每 tick 只消费 1 条且在同一线程，因此写者等待的 state 永远不会有别的线程推进。

## PROBE-3 · 未做的验证

以下项目**没有**实测，只有静态读数：
- es_loader 的全部关于 Buffer::Reader 越界行为的推断（无法编译，缺 TiltedCore）；
- CEF 的 CefListValue 越界读行为（缺 CEF 头文件）；
- SKSE 的 PluginVersionData 布局（缺 SKSE 头文件）。



## 附：两条探测程序的源码（已从仓库移除，此处留档以便复现）

PROBE-1（probe12b.cpp）：把 ESLoader.cpp:95-127 的循环体逐字复制，分别用文本模式与二进制模式打开同一份 CRLF 文件。

    while (!f.eof()) {
        String line;
        std::getline(f, line);
        if (line.empty()) { /* safe */ continue; }
        if (line[0] == '#') continue;
        String before = line;
        line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());
        // 对比 before（即 m_filename 实际拿到的）与 line.back()
    }

PROBE-2（probe_queue.cpp）：把 fast_queue.hpp:98-141 的四个函数逐字复制（仅把无限自旋改成计数到 2000000 后 abort，以便程序能退出），容量取 CommandQueue.h:48 的 Exponent=2。

    int main() {
        fast_queue<int, 2> q;                    // kSize = 4
        for (int i = 1; i <= 6; ++i) {
            int* p;
            auto lock = q.write_acquire(p);      // 第 5 次在此永久自旋
            *p = i;
        }
    }

两个探测程序在跑完后已删除（.audit/_probe/ 目录一并移除），仓库里只留这份报告。构建与运行都没有触碰 Code/ 下的任何源码。

---

# 九、已确认无问题的关键不变量

逐条列出**我实际检查过、并且确认成立**的东西，避免下一位审计者重复劳动。

- **INV-01 · es_loader 的记录头布局**：Record.h:8-14 的 Chunk 用 #pragma pack(1) 且 static_assert(sizeof(Chunk) == 6)；Record.h:55 的 static_assert(sizeof(Record) == 0x18)。两个断言让「用 reinterpret_cast 把文件字节当成结构体读」这件事在**布局层面**是合法的（成员顺序与 TES4 格式一致：type/dataSize/flags/formId/vc/formVersion/vcVersion）。Group.h:17 也有 sizeof(Group) == 0x18。缺陷在**边界校验**不在布局。
- **INV-02 · TES4::ParseChunks 的 MAST 处理**：TES4.cpp:12-15 只处理 MAST_ID，其余 chunk 走 switch 的默认（无 default 分支，不做事）。没有越界、没有未初始化。
- **INV-03 · 服务端的 TimeModel 夹取**：TimeModel.cpp:36-52 对来自网络的全部五个字段做了显式夹取，ClampFloat 把 NaN 与 Inf 降成 0，ClampUint 用 std::min。字段上界（kMaxTimeScale=1000、kMaxTime=24、kMaxDay=31、kMaxMonth=11、kMaxYear=999）与 TimeModel.h:22-26 一致。**这一处不要改。**
- **INV-04 · DateTime::Update 的进位**：DateTime.cpp:23-29 的 while 加 Month++ 加 Year += Month/12 加 Month %= 12 组合，在 Month 为 0 基的前提下语义正确：while 保证 Day 落在当月天数内，Month 越过 12 时 (a) GetNumberOfDaysByMonthIndex 用 aIndex % 12 兜住下标，(b) Month / 12 正确给出跨年数，(c) Month %= 12 正确折回。**我最初怀疑这里有两条缺陷（月份长度表越界、跨年进位错误），逐条复核后都不成立，在此记录以免下一轮重复怀疑。**
- **INV-05 · common/Cell.cpp 与 common/Map.cpp 是空实现且无调用点**：Cell::Cell() 是空构造（Cell.cpp:3-5），Map::At 恒 return nullptr（Map.cpp:3-6）。全仓 grep 没有任何 Map::At 或 Cell 的调用点（server_ui 的 .At() 命中全是它自己的 TimedSeries/RingBuffer，与 common/Map 无关）。死代码，不是缺陷。
- **INV-06 · ThreadUtils.cpp 的两条平台实现都做了截断与判空**：Windows 侧 ToWString（:21-37）先判 !src 与 !*src，再限制 strlen <= 256，再按 MultiByteToWideChar 返回的长度分配，两次调用参数一致；SetThreadName/SetCurrentThreadName（:58-68）都判了 acHandle 与 apThreadName。Linux 侧 SetThreadNameLinux（:72-82）用 char buf[16] 加 strncpy(…, 15) 加显式 buf[15]='\0'，截断正确。**本文件无缺陷。**
- **INV-07 · AllocatorSymbolOverride.cpp 的 aligned 版不重写是自洽的**：C++17 的 operator new(size_t, align_val_t) 没被重写，因此对齐分配走运行库默认实现，对应的 delete 也走默认实现——配对正确。不构成分配器错配。真正需要警惕的是跨 DLL 边界（本重写只在一个模块里），那属于构建配置，不由静态审计判定。
- **INV-08 · SimpleIni 的核心解析循环不越界**：LoadData（:1292-1377）先把整份数据转成带单个 NUL 终止的 pData（:1325-1330 用 new SI_CHAR[uLen+1] 加 memset 清零），FindEntry（:1419-1562）的每个扫描循环都以 *a_pData 为终止条件，并且每次对 a_pData 的写入（*pTrail = 0）都发生在 [a_pKey, a_pData) 区间内。**逐字符扫描型解析是安全的**；唯一的越界点在 IsMultiLineTag（见 SII-04），且该分支当前不可达。
- **INV-09 · 前端桥接的三层名字当前完全一致**：用脚本从 ProcessHandler.cpp:13-38 提取 SetValue 名字 16 个、从 OverlayClient.cpp:45-89 提取 eventName 分支 16 个，**两个方向的差集都为空**。docs/PITFALLS.md 第 18294 行起记录的那批（openPlayGuide、deactivate）已修好且未回退。
- **INV-10 · skse_bootstrap 的路径拼接没有目录穿越**：DeployRuntime（:609-653）用 std::filesystem::relative(it->path(), acPayload) 得到相对路径再拼到 acGameRoot 上，relative 对不在 acPayload 下的路径会返回空或报错，且代码显式检查了 relEc 与 rel.empty()（:629）。payload 目录来自固定的 kRuntimeDirName 常量（:865），不是用户输入。
- **INV-11 · tp_process 的 DLL 预加载顺序有实测依据且实现正确**：main.cpp:79-85 的顺序（dxgi → d3d11 → d3d12 → dcomp）与 :72-78 的理由一致（d3d11 静态导入 dxgi）。PreloadSystemDll（:45-57）检查了 GetSystemDirectoryW 的返回值上界与 0 值。
- **INV-12 · IniSettingsProvider 的数值读写路径不会越界**：SetIniValue 的 szValue[64] 与 GetIniValue 的 szValue[64] 都够 long long 与 double 的十进制表示（最长约 24 字符），且 GetIniValue 显式检查了 ConvertToStore 的返回值才继续解析（:43-46）。缺陷只在**写侧不检查返回值**（SII-03）。

---

# 十、未覆盖区域

明确说明本轮**没有**看或没有看完的部分，供下一轮接手。

1. **Code/components/imgui/** —— 按任务书要求完全跳过（vendored 第三方）。
2. **TiltedCore 的 Buffer.hpp / Stl.hpp / Allocator.hpp 本体** —— 不在仓库内（Libraries 下四个子模块是空目录），全盘 find 也没有。这是 RD-01 无法定级的根本原因。**这是本轮最大的覆盖缺口**：es_loader 的 8 条越界类缺陷，后果全部悬在这个文件上。
3. **SimpleIni.h 的写入路径（Save/AddEntry/CopyString）** —— 按任务书只审读取路径。SII-03 涉及 SetIniValue 但只到「返回值未检查」为止，没有深入 AddEntry 内部的字符串复制逻辑。
4. **SimpleIni.h 的 ConvertUTF 相关（SI_CONVERT_ICU / SI_CONVERT_GENERIC 两条实现）** —— 只确认了 LoadData 调用 SizeFromStore 后有 -1 检查（:1317-1321）与 ConvertFromStore 的返回值检查（:1333-1337），没有逐行审 SizeFromStore/ConvertFromStore/ConvertToStore 三条实现的字符集边界。
5. **Code/skse_bootstrap/main.cpp 的中间段** —— 我读了 1-208、324-475、478-665、700-830、829-903，**跳过了 209-324**（SameContents 之后的 DeployFile 前半段里我已读 324-475）与 **475-478** 少量注释。Skip 的确切范围：:209-323 只看了 :214-240（SameContents）与 :253-319（ReadBuildStamp/SameBuild），中间 DeployFile 的开头注释 :321-333 已读。判断：**功能代码已全部覆盖，跳过的只是注释段**。
6. **Code/skse_client/main.cpp 的 xmake.lua 与构建配置** —— 只读了 main.cpp 全 252 行。SKYRIM_TARGET_LEGACY 宏的两份编译配置（skse_client/xmake.lua 72 行）没读，所以「两份构建是否真的只差这个宏」未验证。
7. **Code/tp_process 的 ProcessHandler.h、process.rc、process.manifest、xmake.lua** —— 只读了 main.cpp（94 行）与 ProcessHandler.cpp（44 行）。manifest 里的 DPI/权限声明、rc 里的版本资源未审。
8. **前端只审了 9 个文件** —— client.service.ts（689 行里读了 100-300 与 125-194）、chat.service.ts、chat/commands.ts、typings.d.ts、notifications/notification-popup/connect/group/root 四个 component、destroy.service.ts。**没有**读的：其他 12 个 service（alert、asset、error、loading、party-hint、player-list、popup-notification、server-list、setting、sound、store）、全部 40 多个 component 的 .html/.scss、mock/ 目录（只看了 skyrimtogether.mock.ts 的前 120 行）、environments/。
9. **前端与 C++ 的 C++→JS 方向** —— 只审了 JS→C++（注册/派发）。C++ 侧用 ExecuteAsync 推事件到 JS 的那条链（OverlayService.h 与它的实现）**没有审**，因此「带参数的事件是否都传了 pArgs」这一条（PITFALLS 第 6 节明确警告过）未验证。
10. **Code/base/containers 只有 fast_queue.hpp 一个文件** —— 已全读（147 行）。Code/base/allocator 只有 AllocatorSymbolOverride.cpp 一个文件，已全读（64 行）。这两个目录**无覆盖缺口**。
11. **48 个门禁脚本** —— 按任务书没有重跑，也没有读它们的源码（除了在注释里看到 check_overlay_bridge.py 与 check_skse_bootstrap.py 的名字与用途）。
