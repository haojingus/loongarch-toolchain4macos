# LoongArch64 Cross-Toolchain for macOS (Apple Silicon → Loongnix 20)

在 macOS Apple Silicon (arm64) 上运行的 **LoongArch64 交叉编译工具链**。从源码自构建，目标是龙芯 Loongnix 20 (DaoXiangHu) **老世界 ABI**。

> 同类工具链龙芯官方至今没出 macOS 版。

## 规格

| 项 | 值 |
|---|---|
| Host | macOS arm64 (Apple Silicon) |
| Target triplet | `loongarch64-linux-gnu` |
| ABI | `lp64d`（双精度浮点）、**老世界 OBJ-v1 e_flags + ABI Version 0** |
| Dynamic linker（默认烧死） | `/lib64/ld.so.1`（老世界，非上游） |
| Binutils | 2.42 |
| GCC | 14.2.0（C / C++ 全套，含 libstdc++ / libgcc / libgomp / libatomic / libitm，**不含 libsanitizer**） |
| Glibc | 2.28（来自目标 sysroot，未自编） |
| Linux kernel headers | 4.19.190.8.26-lnd.11 |
| Sysroot 来源 | Loongnix 20（参考机：**龙芯 2K3000 EV 开发板**），**用户自行提取**（见 [SYSROOT.md](SYSROOT.md)） |

兼容验证（实机 Loongnix 20 上跑过）：C / C++17 / 异常 / `thread_local` / `dlopen` + 跨 .so 抛异常 / `std::thread` / `std::atomic` / chrono / 浮点 全 PASS。

## 安装方式

### 方式 1：装预编译 pkg（推荐）

从 [Releases](../../releases) 下下载 `loongarch-toolchain-X.Y.Z-arm64.pkg` 双击安装，详见 [INSTALL.md](INSTALL.md)。

> ⚠️ **pkg 不含 sysroot**——sysroot 因含龙芯专有组件（GPU/crypto blobs）无法重新分发，必须本地从 Loongnix 系统提取。装完 pkg 后跑 `scripts/extract-sysroot.sh` 把 sysroot 拉到 `/opt/loongarch-toolchain/sysroot/`。

### 方式 2：从源码自构建

见 [BUILD.md](BUILD.md)（如果存在）或直接照本 README 末尾"构建参数"一节配 + `make`。

## Sysroot 自助提取

工具链本身就是工具集（gcc / ld / ...），**接 `--sysroot=` 参数指定 sysroot 路径**。本仓库的参考 sysroot 是反向从**龙芯 2K3000 EV 开发板**（Loongnix 20，DaoXiangHu）上拉的——你手头有任何 Loongnix 20 机器都可以这么提：

```bash
./scripts/extract-sysroot.sh --from-ssh root@<your-loongnix-host> --out /opt/loongarch-toolchain/sysroot
```

完整攻略见 **[SYSROOT.md](SYSROOT.md)**——含步骤 1（在目标上 apt install 哪些 dev 包）/ 步骤 2（跑脚本）/ 步骤 3（验证）/ 用 `--sysroot=` 同时维护多目标 / 老世界 vs 新世界注意点。

## 快速开始

```bash
export PATH="/opt/loongarch-toolchain/bin:$PATH"

# 验证
loongarch64-linux-gnu-gcc --version
# loongarch64-linux-gnu-gcc (GCC) 14.2.0

# C
cat > hello.c <<'EOF'
#include <stdio.h>
int main(){ printf("hello loongarch\n"); return 0; }
EOF
loongarch64-linux-gnu-gcc -O2 hello.c -o hello

file hello
# hello: ELF 64-bit LSB executable, LoongArch, ..., interpreter /lib64/ld.so.1, ...

# C++ (静态链 libstdc++ / libgcc，单文件可部署)
cat > hello.cc <<'EOF'
#include <iostream>
int main(){ std::cout << "hello loongarch++\n"; return 0; }
EOF
loongarch64-linux-gnu-g++ -O2 -static-libstdc++ -static-libgcc hello.cc -o hello++
```

更多示例见 `examples/`。

## 部署模式

### 选 1：静态链 libstdc++（最简单，单文件）

```bash
loongarch64-linux-gnu-g++ -O2 -static-libstdc++ -static-libgcc app.cc -o app
loongarch64-linux-gnu-strip app
scp app target:/path/
```

二进制约 8-10 MB（含完整 C++ 运行时）。

### 选 2：rpath bundle（多文件，多程序共享 runtime）

```bash
mkdir -p dist/lib
cp /opt/loongarch-toolchain/loongarch64-linux-gnu/lib/libstdc++.so.6.0.33 \
   /opt/loongarch-toolchain/loongarch64-linux-gnu/lib/libstdc++.so.6 \
   /opt/loongarch-toolchain/loongarch64-linux-gnu/lib/libgcc_s.so.1 \
   dist/lib/
loongarch64-linux-gnu-strip dist/lib/libstdc++.so.6.0.33 dist/lib/libgcc_s.so.1

loongarch64-linux-gnu-g++ -O2 -Wl,-rpath,'$ORIGIN/lib' app.cc -o dist/app
loongarch64-linux-gnu-strip dist/app
```

整个 `dist/` 上传，`./dist/app` 自动从 `dist/lib` 找运行时。

### 选 3：系统级安装

把 `libstdc++.so.6` / `libgcc_s.so.1` 装到目标 `/usr/local/lib` + `ldconfig`。**仅在目标是定制最小镜像、确认没有其他 C++ 程序时推荐**——Loongnix 自己的 GCC 8.3 libstdc++ 6.0.25 在 `/usr/lib`，强行盖会撞。

## 限制 / 已知问题

1. **只能编出"老世界"二进制**——dyn linker 烧死 `/lib64/ld.so.1`，跑不了上游新世界 LoongArch 系统（Debian sid `loong64` 端口、AOSC OS 新版、openKylin）。新世界 dyn linker 是 `/lib64/ld-linux-loongarch-lp64d.so.1`。
2. **不含 libsanitizer**（asan/tsan/ubsan/lsan）——cross-build 在 GCC 14 这版上撞 Linux 移植 bug 没过；非核心，常规开发用不到。
3. **glibc 2.28 + GCC 14**：跨代工作，但有边界。动态链 C++ 时，目标自带 libstdc++ 6.0.25 顶不住我们的 GLIBCXX_3.4.32 → 必须按"选 1/2"自带 runtime。
4. **仅 arm64 macOS**：x86_64 Mac 跑不了。
5. **sysroot 不分发**：见上方"Sysroot 自助提取"。

## glibc 静态链接注意事项

工具链使用 glibc 2.28。glibc 上游设计：**NSS（Name Service Switch）模块（`libnss_files.so.2`、`libnss_dns.so.2` 等）始终在运行时通过 `dlopen` 加载**。因此：

- 任何用了 `getaddrinfo` / `getifaddrs` / `getpwnam` / `getservbyname` 等主机/用户/服务名解析 API 的程序，**用 `-static` 全静态链时会段错误**。
- 不沾 NSS 的程序用 `-static` 无问题。

| 场景 | 链接方式 |
|---|---|
| 用了 NSS 相关 API 的 C / C++ 程序 | **动态链 glibc** + 静态链 libstdc++/libgcc（或 rpath bundle 自带），即默认行为 |
| 纯算法 / 嵌入式风格 freestanding | `-static` 全静态链 |
| 想完全摆脱该限制 | 切 musl libc 重构工具链（`musl-cross-make`） |

**该限制并非本工具链引入，所有标准 glibc 工具链都一样**。实测案例：`examples/sysinfo.c` 用 `getifaddrs` 列网卡，`-static` 链 → 启动即段错误；改动态链 → 一切正常。

## 故障排查

**`command not found: loongarch64-linux-gnu-gcc`**：PATH 没配，加 `export PATH="/opt/loongarch-toolchain/bin:$PATH"`。

**`fatal error: bits/libc-header-start.h: No such file`**：sysroot 没装好或 multiarch 失效。检查 `/opt/loongarch-toolchain/sysroot/usr/include/loongarch64-linux-gnu/` 存在。

**目标上跑报 `version 'GLIBCXX_3.4.32' not found`**：动态链 C++ 但目标用了它自己的旧 libstdc++。改静态链或 rpath bundle 自带 runtime。

**目标上跑报 `No such file or directory`（明明文件在）**：99% 是动态加载器路径不对。`readelf -l <bin> | grep INTERP` 应该是 `/lib64/ld.so.1`。如果是 `/lib64/ld-linux-loongarch-lp64d.so.1`，说明目标其实是新世界系统，本工具链不适配。

## 工具链构建参数（自构建参考）

**Binutils 2.42**:
```
--target=loongarch64-linux-gnu
--prefix=/opt/loongarch-toolchain
--with-sysroot=/opt/loongarch-toolchain/sysroot
--disable-nls --disable-werror --disable-gdb
--disable-gprofng --disable-sim --disable-multilib
--with-system-zlib
```

**GCC 14.2**:
```
--target=loongarch64-linux-gnu
--prefix=/opt/loongarch-toolchain
--with-sysroot=/opt/loongarch-toolchain/sysroot
--enable-languages=c,c++
--enable-shared --enable-threads=posix --enable-multiarch
--disable-multilib --disable-nls --disable-werror
--disable-libssp --disable-libstdcxx-pch
--with-system-zlib
```

构建前打 `patches/gcc-14.2-loongarch-oldworld-dynlinker.patch`（把上游默认动态加载器路径改成老世界 `/lib64/ld.so.1`）。

依赖（host 端）：`brew install gmp mpfr libmpc isl zlib`。

## License

- GCC：GPL-3.0+ with GCC Runtime Library Exception
- Binutils：GPL-3.0+
- 本仓库的 GCC patch、构建脚本、文档：**GPL-3.0+**（见 [LICENSE](LICENSE)）
- Sysroot：来自 Loongnix 20，**本仓库不分发**；各组件遵循其原 License（glibc=LGPL-2.1+，linux headers=GPL-2 with syscall exception，等）

## 贡献 / Issues / 联系

欢迎在 GitHub 提 issue / PR。

遇到问题也可以加微信沟通：**`haojing0036`**（加好友请备注「loongarch-toolchain」方便我通过）。
