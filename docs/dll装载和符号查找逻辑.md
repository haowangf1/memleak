# 多个 DLL/so 内嵌同名第三方符号时的命名、装载与错误机制

  ## 1. 结论

  当多个 DLL/so 都各自编进同一个第三方库实现（如 `spdlog/fmt`）时：

  - 只要它们定义的是**同一个 C++ 实体**
  - 并且符号对外可见
  - 运行时装载器就可能把某个模块的调用绑定到另一个模块的实现上

  这会导致：

  - 对象在模块 A 的实现语义下创建
  - 却在模块 B 的实现语义下析构或释放
  - 最终出现 `Invalid free`、崩溃或其他未定义行为

  ---

  ## 2. 为什么两个模块里的符号名会完全一致

  C++ 编译后的符号名（mangled name）由以下信息决定：

  - 命名空间
  - 类名
  - 函数名
  - 参数类型
  - ABI 相关限定信息

  例如：

  ```cpp
  spdlog::pattern_formatter::compile_pattern(std::string const&)

  无论它被编进：

  - libAMCAXNextMesh.so
  - libAMCAXMCNP.so
  - libAMCAXMeshing.so

  只要函数签名一致、ABI 一致，生成的符号名就会一致。

  例如会出现类似：

  _ZN6spdlog17pattern_formatter16compile_patternERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEE

  注意：

  - C++ 的符号修饰不会编码“这个函数来自哪个 DLL/so”
  - 不会因为模块不同而自动变成不同名字
  - 装载器看到的只是“多个模块都提供了同名符号”

  ———

  ## 3. 为什么命名空间不能避免这个问题

  例如用户自己写了：

  namespace AMCAX::MCNP::Log
  {
      ...
  }

  这只能隔离自己定义的符号，例如：

  - AMCAX::MCNP::Log::Init
  - AMCAX::MCNP::Log::Shutdown

  但对第三方库内部符号无效，例如：

  - spdlog::pattern_formatter::~pattern_formatter
  - spdlog::details::full_formatter::~full_formatter

  这些符号仍然属于：

  - spdlog::...
  - fmt::...

  不会因为外面包了一层自定义命名空间而改变。

  因此这是二进制符号可见性问题，不是源码命名空间问题。

  ———

  ## 4. 编译、链接、装载三个阶段分别做了什么

  ## 编译阶段

  源码被编译为 .obj：

  - 目标文件里记录“我要调用某个符号”
  - 此时通常还不知道最终绝对地址
  - 所以这一步只是产生“符号引用”

  例如：

  call [symbol: spdlog::pattern_formatter::~pattern_formatter]

  而不是直接写死某个实际地址。

  ## 链接阶段

  链接器生成 exe / dll / so：

  - 会决定一部分本地符号关系
  - 但对于动态库之间的很多引用，通常不会把最终地址完全写死
  - 而是保留成导入信息、重定位信息、待运行时解析项

  因此：

  - 链接阶段建立了“将来由动态装载器解析”的条件
  - 但通常还不是最终决定调用落到哪个模块地址的阶段

  ## 装载阶段（运行时动态链接）

  程序启动时，动态装载器加载各个模块，并进行：

  - 库查找
  - 模块装载
  - 动态符号解析
  - 重定位修正(用该符号在当前进程地址空间中的实际地址去修正对应引用)

  这一步才真正决定：

  - 某个未解析符号引用
  - 最终绑定到哪个模块的哪个地址

  因此，本类问题的真正落点主要在装载阶段的动态符号绑定。

  ———

  ## 5. 装载器如何查找符号

  在 Linux/ELF 下，可以通过：

  LD_DEBUG=libs,symbols ./your_program > lddebug.log 2>&1

  观察运行时过程。

  日志中类似：

  symbol=...
  lookup in file=./AMCAXTestRunner
  lookup in file=.../libAMCAXGCS.so
  lookup in file=.../libAMCAXGeomE.so
  lookup in file=.../libAMCAXNextMesh.so
  ...

  含义是：

  - 装载器正在为某个符号做查找
  - 它会按运行时的搜索顺序遍历已加载模块
  - 查找哪个模块提供了这个可见符号

  一般规则可理解为：

  - 对一个待解析引用
  - 装载器按 link map 顺序检查各已加载对象
  - 找到第一个满足条件的可见定义后
  - 就把该引用绑定到那个地址

  因此：

  - 它不是按“源码归属”来选
  - 而是按“运行时搜索顺序里，谁先提供这个可见符号”来选

  ———

  ## 6. 为什么会出现跨模块绑定

  如果多个模块都导出同名 spdlog 符号，例如：

  - libAMCAXNextMesh.so
  - libAMCAXMCNP.so
  - libAMCAXMeshing.so

  都提供：

  spdlog::pattern_formatter::compile_pattern(...)

  那么某个待解析引用在装载阶段就可能被绑定到“错误模块”的实现。

  例如：

  - 引用来源于 NextMesh
  - 但最终绑定到了 MCNP 中那份同名实现

  这就形成了“跨模块实现串绑”。

  ———

  ## 7. 为什么这会导致内存错误

  问题不在于模块内存映射重叠，而在于：

  - 对象由一套实现创建
  - 却由另一套实现析构

  一个 pattern_formatter/full_formatter 对象内部不仅有堆资源，还可能引用：

  - 模块内部只读常量
  - 静态表
  - 模块私有数据结构

  如果：

  - 对象按 NextMesh 那份实现语义构造
  - 但析构时调用到 MCNP 那份析构代码

  那么 MCNP 那份代码会按自己的规则解释对象成员。

  它可能会把某个本来属于：

  - NextMesh 模块只读段
  - 静态区
  - 非堆内存

  的地址，误判成“应释放资源”，从而执行 free/delete。

  于是出现：

  - Invalid free
  - 崩溃
  - 未定义行为

  这不是“内存撞在一起”，而是：

  - 对象生命周期跨实现
  - 释放权和解释权错位

  ———

  ## 8. 为什么隐藏符号可以解决问题

  通过设置：

  set_target_properties(${PROJECT_NAME} PROPERTIES
      CXX_VISIBILITY_PRESET hidden
      VISIBILITY_INLINES_HIDDEN YES
  )

  效果是：

  - 默认隐藏该模块中大多数 C++ 符号
  - 尤其是模板/inline 产生的内部实现符号
  - 使这些内部 spdlog/fmt 符号不再进入外部可见的动态符号集合

  这样一来：

  - 装载器在为别的模块解析符号时
  - 就不会再把 MCNP 的内部 spdlog 符号纳入候选
  - NextMesh 只能使用它自己那份实现

  结果：

  - 构造、使用、析构都在同一实现体系内完成
  - 生命周期闭合
  - 不再发生跨模块错误释放

  ———

  ## 9. 一句话总结

  多个 DLL/so 各自内嵌同名第三方实现时，只要内部符号对外可见，装载阶段的动态符号解析就可能把某个模块的调用绑定到另一个模块的同名实现
  上。

  最终造成：

  - 同一对象由不同模块的实现共同处理
  - 生命周期跨模块
  - 析构释放错位
  - 出现 Invalid free 和崩溃