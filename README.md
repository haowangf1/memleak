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