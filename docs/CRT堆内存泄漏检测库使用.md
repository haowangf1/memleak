# Windows CRT Debug Heap 内存泄漏排查笔记

本文总结 Windows C++ 项目中使用 MSVC CRT Debug Heap 做内存泄漏排查的基本方法，并结合 OpenCASCADE STEP 导入场景说明注意事项。

## 1. CRT Debug Heap 是什么

CRT 是 Microsoft C Runtime，Debug Heap 是 MSVC 调试版运行库提供的堆内存调试机制。

它适合检查：

- `new` / `delete` 泄漏
- `malloc` / `free` 泄漏
- CRT 堆损坏
- 分配编号
- 某段代码执行前后的 CRT 堆差异

它不擅长检查：

- GDI 对象泄漏
- HANDLE 泄漏
- 显存泄漏
- 第三方库内部自定义内存池
- 全局缓存、单例、延迟释放对象

对于 OpenCASCADE 这类大型库，CRT Debug Heap 可以作为第一层排查工具，但不能代表全部内存问题。

## 2. 最小泄漏检测代码

```cpp
#define _CRTDBG_MAP_ALLOC
#include <cstdlib>
#include <crtdbg.h>

int main()
{
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);

    int* p = new int[10];

    return 0;
}
```

Debug 运行后，在 Visual Studio 的输出窗口中查看：

```text
输出窗口 -> 调试
```

如果存在泄漏，会看到类似：

```text
Detected memory leaks!
Dumping objects ->
...
Object dump complete.
```

其中：

```cpp
_CRTDBG_ALLOC_MEM_DF
```

表示启用 Debug Heap 分配跟踪。

```cpp
_CRTDBG_LEAK_CHECK_DF
```

表示程序正常退出时自动检查泄漏，效果类似退出前调用 `_CrtDumpMemoryLeaks()`。

## 3. 显示文件名和行号

为了让泄漏报告尽量显示分配位置，可以在自己的 `.cpp` 中加入：

```cpp
#define _CRTDBG_MAP_ALLOC
#include <cstdlib>
#include <crtdbg.h>

#ifdef _DEBUG
#define new new(_NORMAL_BLOCK, __FILE__, __LINE__)
#endif
```

示例：

```cpp
#define _CRTDBG_MAP_ALLOC
#include <cstdlib>
#include <crtdbg.h>

#ifdef _DEBUG
#define new new(_NORMAL_BLOCK, __FILE__, __LINE__)
#endif

int main()
{
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);

    int* p = new int[10];

    return 0;
}
```

注意：不要把 `#define new ...` 随意放进公共头文件，也不要放在第三方库头文件之前，否则可能影响第三方库代码。

## 4. 手动触发泄漏检查

不想等进程退出时，可以手动调用：

```cpp
_CrtDumpMemoryLeaks();
```

示例：

```cpp
int main()
{
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF);

    int* p = new int[10];

    _CrtDumpMemoryLeaks();

    delete[] p;

    return 0;
}
```

注意：`_CrtDumpMemoryLeaks()` 调用时仍然存活的对象也会被认为是未释放对象，所以调用位置很重要。

## 5. 使用 Checkpoint 做函数前后对比

实际项目中更推荐用 `_CrtMemCheckpoint()` 比较某个函数调用前后的 CRT 堆变化。

```cpp
_CrtMemState before;
_CrtMemState after;
_CrtMemState diff;

_CrtMemCheckpoint(&before);

readwstep();

_CrtMemCheckpoint(&after);

if (_CrtMemDifference(&diff, &before, &after)) {
    _CrtMemDumpStatistics(&diff);
    _CrtMemDumpAllObjectsSince(&before);
}
```

这适合判断：

```text
某个函数返回后，CRT 堆是否还有净增长。
```

对于 OpenCASCADE 的 `STEPControl_Reader`、`STEPControl_Writer`，建议在函数返回后再做 checkpoint，因为函数返回后局部变量才会析构。

## 6. 使用分配编号断到泄漏现场

泄漏输出中可能出现：

```text
{12345} normal block at 0x000001A2... 40 bytes long.
```

其中 `{12345}` 是分配编号。

可以在程序开头加入：

```cpp
_CrtSetBreakAlloc(12345);
```

示例：

```cpp
int main()
{
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);

    _CrtSetBreakAlloc(12345);

    readwstep();

    return 0;
}
```

再次运行时，程序会在第 `12345` 次内存分配处中断。此时查看调用栈，就可以定位是谁分配了这块内存。

## 7. 检查堆损坏

有时候问题不是泄漏，而是越界写、重复释放、释放后继续使用。

可以使用：

```cpp
_CrtCheckMemory();
```

示例：

```cpp
int* p = new int[10];

p[10] = 123;

_CrtCheckMemory();

delete[] p;
```

也可以开启更严格的检查：

```cpp
_CrtSetDbgFlag(
    _CRTDBG_ALLOC_MEM_DF |
    _CRTDBG_CHECK_ALWAYS_DF |
    _CRTDBG_LEAK_CHECK_DF
);
```

注意：`_CRTDBG_CHECK_ALWAYS_DF` 会在每次分配和释放时检查堆完整性，性能开销很大，大项目中会明显变慢。

## 8. 常用标志说明

```cpp
_CRTDBG_ALLOC_MEM_DF
```

启用 Debug Heap 分配跟踪。

```cpp
_CRTDBG_LEAK_CHECK_DF
```

程序退出时自动检查泄漏。

```cpp
_CRTDBG_CHECK_ALWAYS_DF
```

每次分配和释放都检查堆完整性，适合排查堆损坏，但很慢。

```cpp
_CRTDBG_CHECK_CRT_DF
```

把 CRT 自己内部的内存块也纳入检查，通常噪音较多，不建议一开始开启。

```cpp
_CRTDBG_DELAY_FREE_MEM_DF
```

释放内存后不立即归还，并用特殊字节填充，适合排查 use-after-free，但会让进程内存看起来更高。

## 9. 推荐初始化模板

```cpp
#ifdef _MSC_VER
#define _CRTDBG_MAP_ALLOC
#include <cstdlib>
#include <crtdbg.h>
#endif

void InitCrtDebug()
{
#ifdef _DEBUG
    int flags = _CrtSetDbgFlag(_CRTDBG_REPORT_FLAG);
    flags |= _CRTDBG_ALLOC_MEM_DF;
    flags |= _CRTDBG_LEAK_CHECK_DF;
    _CrtSetDbgFlag(flags);

    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_DEBUG);
#endif
}
```

使用：

```cpp
int main()
{
    InitCrtDebug();

    readwstep();

    return 0;
}
```

## 10. OpenCASCADE STEP 导入排查建议

STEP 导入一般分为几个阶段：

```cpp
reader.ReadFile(...);
reader.TransferRoots();
TopoDS_Shape result = reader.OneShape();
writer.Transfer(result, STEPControl_ManifoldSolidBrep);
writer.Write(...);
```

通常真正吃内存的是：

```cpp
reader.TransferRoots();
writer.Transfer(...);
```

`ReadFile()` 主要读取和解析 STEP 文件实体，不一定是内存峰值最高的地方。

建议同时做两类检测：

1. 用 CRT Debug Heap 判断函数返回后是否有 CRT 堆净增长。
2. 用 `GetProcessMemoryInfo()` 实时打印进程内存，判断哪一步内存暴涨。

## 11. 实时打印进程内存

```cpp
#include <windows.h>
#include <psapi.h>
#include <iostream>

#pragma comment(lib, "Psapi.lib")

void PrintMemory(const char* tag)
{
    PROCESS_MEMORY_COUNTERS_EX pmc = {};
    pmc.cb = sizeof(pmc);

    if (GetProcessMemoryInfo(
        GetCurrentProcess(),
        reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
        sizeof(pmc))) {

        std::cout
            << "[PID " << GetCurrentProcessId() << "] "
            << "[MEM] " << tag
            << " | WorkingSet="
            << static_cast<unsigned long long>(pmc.WorkingSetSize) / 1024 / 1024
            << " MB"
            << " | PrivateUsage="
            << static_cast<unsigned long long>(pmc.PrivateUsage) / 1024 / 1024
            << " MB"
            << " | PagefileUsage="
            << static_cast<unsigned long long>(pmc.PagefileUsage) / 1024 / 1024
            << " MB"
            << " | PeakWorkingSet="
            << static_cast<unsigned long long>(pmc.PeakWorkingSetSize) / 1024 / 1024
            << " MB"
            << std::endl;
    }
}
```

各字段含义：

- `WorkingSet`：当前驻留在物理内存中的部分，接近任务管理器默认“内存”列。
- `PrivateUsage`：进程私有提交内存，更接近 VS Diagnostic Tools 中看到的内存压力。
- `PagefileUsage`：页面文件相关提交量。
- `PeakWorkingSet`：历史峰值工作集。

## 12. VS Diagnostic Tools 和任务管理器差异

VS Diagnostic Tools、任务管理器、代码打印的内存值可能不同，原因是统计口径不同。

常见概念：

```text
Virtual Size：进程虚拟地址空间
Commit Size / Private Bytes：进程提交或私有提交内存
Working Set：当前实际驻留在物理 RAM 中的部分
```

一台 16GB RAM 的电脑上，进程显示 20GB 提交内存也是可能的，因为 Windows 可以使用：

```text
物理内存 + 页面文件
```

所以：

- VS 显示 20GB，可能是提交量或专用字节。
- 任务管理器默认“内存”列可能显示 10GB，这是工作集。
- 自己代码里 `GetProcessMemoryInfo(GetCurrentProcess(), ...)` 打印的是当前进程，通常更适合确认调试目标是否正确。

如果代码打印只有几十 MB，而 VS 图显示几 GB，要检查：

- VS Diagnostic Tools 是否绑定到了当前进程。
- 是否有多个 `teststep.exe` 实例。
- 任务管理器详细信息页中的 PID 是否一致。
- VS 图表是否残留了历史调试会话数据。

## 13. 推荐学习顺序

1. 故意写一个 `new` 不释放的例子，确认能看到泄漏。
2. 补上 `delete`，确认泄漏消失。
3. 加 `#define new new(_NORMAL_BLOCK, __FILE__, __LINE__)`，学习看文件名和行号。
4. 使用 `_CrtMemCheckpoint()` 比较函数调用前后差异。
5. 根据泄漏输出中的 `{分配编号}` 使用 `_CrtSetBreakAlloc()` 定位调用栈。
6. 使用 `_CrtCheckMemory()` 排查堆损坏。
7. 对大型第三方库同时结合 `GetProcessMemoryInfo()`、任务管理器、Process Explorer 或 Deleaker。

## 14. 排查时的注意事项

- 必须使用 Debug 配置。
- 必须链接 Debug CRT。
- 第三方库也最好使用 Debug 版本。
- Release 模式下 CRT Debug Heap 效果有限。
- 异常终止或强制结束进程时，可能看不到退出泄漏报告。
- `_CrtDumpMemoryLeaks()` 调用时仍存活的对象会被报出。
- `STEPControl_Reader`、`STEPControl_Writer`、`TopoDS_Shape` 未析构前做 dump 容易误判。
- OpenCASCADE 内部缓存、单例、内存池可能导致“看起来没释放”，不一定是真泄漏。
- 判断是否泄漏时，建议循环导入同一个 STEP 文件，观察每轮函数返回后内存是否持续上涨。

## 15. 最核心的 4 个 API

```cpp
_CrtSetDbgFlag(...)
_CrtDumpMemoryLeaks()
_CrtMemCheckpoint(...)
_CrtSetBreakAlloc(...)
```

先把这 4 个用熟，Windows 下普通 C++ 内存泄漏问题就可以排查一大半。
