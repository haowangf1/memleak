# memleak
memleak

复现一个exe加载两个dll,两个dll同时使用spdlog，并且分批次卸载dll时发生的共享资源冲突
复现步骤：

  1. 编译

  cd /root/work/memleak
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
  cmake --build build -j

  2. 拷 so

  cp build/mcnp/libmcnp.so build/exe/
  cp build/nextmesh/libnextmesh.so build/exe/

  3. 跑 valgrind

  cd build/exe
  valgrind --leak-check=full --track-origins=yes ./memleak_exe


  # DLL/so 混用 spdlog 导致崩溃问题总结

  ## 1. 现象

  同一进程中同时加载：

  - `AMCAXTestRunner`
  - `libAMCAXNextMesh.so`
  - `libAMCAXMCNP.so`

  两边都使用了 `spdlog`，运行 `NMMesh` 相关测试时，`valgrind` 报错：

  - `Invalid free() / delete / delete[] / realloc()`
  - 调用栈显示：
    - `set_formatter / set_pattern` 在 `libAMCAXNextMesh.so`
    - `pattern_formatter::~pattern_formatter` / `full_formatter::~full_formatter` 在 `libAMCAXMCNP.so`
  - 被释放地址位于：
    - `libAMCAXNextMesh.so` 的 `r--` 映射段

  ---

  ## 2. 本质结论

  这不是普通内存泄漏问题，也不是两个模块内存“重叠”了。

  本质是：

  - 两个 DLL/so 都带了一份 `spdlog/fmt` 实现
  - 由于 `MCNP` 模块内部相关符号对外可见
  - 运行时动态符号解析时，`NextMesh` 的某些调用路径绑定到了 `MCNP` 那份实现
  - 结果形成了“跨模块生命周期”：
    - 对象/内部状态按 `NextMesh` 的实现创建
    - 但析构/释放逻辑跑到了 `MCNP` 的实现里
  - 最终 `MCNP` 那份析构代码错误释放了 `NextMesh` 模块中的只读地址，触发崩溃

  ---

  ## 3. 进程内存状态是什么样

  进程启动后，每个模块都被映射到自己的虚拟地址空间区域中。

  每个 `.dll/.so` 一般包含：

  - 代码段：函数机器码
  - 只读段：常量、字符串、只读表、部分 RTTI/vtable
  - 数据段：全局/静态变量
  - 堆：运行时 `new/malloc` 得到的对象

  因此：

  - `NextMesh` 的代码和数据在自己的模块区域
  - `MCNP` 的代码和数据在自己的模块区域
  - 二者地址空间本身并不重叠

  所以问题不是“内存撞在一起”，而是：

  - 一个模块的代码
  - 去解释并释放另一个模块构造出的对象内部状态

  ---

  ## 4. 为什么会发生跨模块调用

  关键不在源码文件，而在**符号解析**。

  即使两个模块各自有自己的代码地址，调用最终跳到哪个地址，并不是源码写死的，而是依赖：

  - 编译阶段产生的符号引用
  - 链接阶段保留的动态符号关系
  - 装载阶段的动态符号解析与重定位

  如果两个模块都包含同名符号，例如：

  - `spdlog::logger::set_formatter`
  - `spdlog::pattern_formatter::~pattern_formatter`
  - `spdlog::details::full_formatter::~full_formatter`

  并且这些符号对外可见，那么运行时就可能出现：

  - 一个模块里的未解析引用
  - 被绑定到另一个模块导出的同名实现

  这就是“跨 DLL/so 调到另一份实现”的根本原因。

  ---

  ## 5. 编译、链接、装载三个阶段分别做了什么

  ## 编译阶段
  - 源码编译为 `.obj`
  - 这里只知道“我要调用某个符号”
  - 还不知道最终调用地址

  不是在这个阶段出错。

  ## 链接阶段
  - 生成 `exe`、`dll` 或 `so`
  - 会决定一部分符号关系
  - 但对于动态库之间的很多引用，通常不会写死最终地址
  - 而是保留导入/重定位信息，等待运行时解析

  链接阶段决定了“将来由动态装载器去解这个符号”。

  ## 装载阶段（运行时动态链接）
  - 进程启动后，系统装载 `exe` 和各个 `dll/so`
  - 动态装载器根据导出表、导入表、动态符号表、重定位信息，把符号名绑定到具体地址
  - 这一步才真正决定：
    - 某个 `spdlog::...` 调用
    - 到底跳到 `NextMesh` 那份实现
    - 还是 `MCNP` 那份实现

  **因此，本次问题实际落点主要在装载阶段的动态符号绑定。**

  ---

  ## 6. 从对象内存角度看为什么会崩

  一个 `pattern_formatter/full_formatter` 对象不是简单的一块堆内存，它内部可能包含：

  - 指向堆资源的指针
  - 指向模块只读常量区的指针
  - 指向静态表的指针
  - 其他内部状态

  粗略理解为：

  - 对象本体可能在堆上
  - 但它内部引用的数据未必都在堆上

  如果对象是在 `NextMesh` 那份实现语义下创建的，那么其中某些内部指针完全可能合法地指向：

  - `libAMCAXNextMesh.so` 的只读段
  - `libAMCAXNextMesh.so` 的静态数据

  这本来没问题。

  问题在于析构时如果调用到了 `MCNP` 那份实现，则：

  - `MCNP` 的析构函数会按自己那套实现规则解释这些成员
  - 它可能把某个原本不该释放的地址，当成了“自己应释放的资源”
  - 然后执行 `free/delete`

  由于那个地址实际上属于 `NextMesh` 的只读映射段，不是堆块，于是触发：

  - `Invalid free`

  这正是 `valgrind` 报错的根本原因。

  ---

  ## 7. 为什么命名空间不能解决问题

  `namespace AMCAX::MCNP::Log` 只能保护你自己写的封装符号，例如：

  - `AMCAX::MCNP::Log::Init`
  - `AMCAX::MCNP::Log::Shutdown`

  但这次冲突的并不是这些符号，而是第三方库自己的实现符号，例如：

  - `spdlog::pattern_formatter::~pattern_formatter`
  - `spdlog::details::full_formatter::~full_formatter`

  这些符号的命名空间仍然是：

  - `spdlog::...`
  - `fmt::...`

  不会因为你外层包了一层 `AMCAX::MCNP::Log` 就自动隔离。

  所以本问题是**二进制符号可见性问题**，不是源码命名空间问题。

  ---

  ## 8. 为什么“我只暴露一个业务 API”仍然会中招

  源码层面的“公开 API”与二进制层面的“导出符号”不是一回事。

  即使你业务上只想暴露：

  - `MCNPReader::Read`

  最终编译出的 `.dll/.so` 中仍可能对外可见很多内部符号，例如：

  - 第三方库实现符号
  - 模板实例
  - inline 生成体
  - 析构函数
  - 辅助函数

  因此这次问题不是“手工把 spdlog API 暴露出去了”，而是：

  - `MCNP` 模块的最终二进制产物把内部 `spdlog/fmt` 实现也导出到了动态符号集合中

  ---

  ## 9. 修复方式及原理

  最终生效的修复是在 `MCNP` 模块上启用符号隐藏：

  ```cmake
  set_target_properties(${PROJECT_NAME} PROPERTIES
      CXX_VISIBILITY_PRESET hidden
      VISIBILITY_INLINES_HIDDEN YES
  )

  对应效果：

  - 默认隐藏该模块中大多数 C++ 符号
  - 尤其是 inline/template 产生的内部实现符号
  - 使 libAMCAXMCNP.so 中的 spdlog/fmt 内部实现不再对外可见
  - 这样其他模块就不能再把自己的调用绑定到 MCNP 这份内部实现上

  结果是：

  - NextMesh 只能使用它自己那份 spdlog 实现
  - 构造、使用、析构都落在同一套实现里
  - 生命周期闭合
  - 不再发生跨模块错误释放

  ———

  ## 10. 最终一句话总结

  本次崩溃的根因不是“两个 DLL/so 的内存冲突”，而是：

  - 两个模块都带了一份 spdlog/fmt 实现
  - MCNP 模块内部相关符号对外可见
  - 装载阶段的动态符号解析把 NextMesh 的部分调用绑定到了 MCNP 那份实现
  - 导致对象在 NextMesh 中创建、却在 MCNP 中析构
  - 最终错误释放了 NextMesh 模块只读段中的地址，触发 Invalid free