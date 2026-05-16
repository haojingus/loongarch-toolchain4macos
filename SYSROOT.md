# Sysroot 提取攻略（反向从 2K3000 EV 开发板拉）

本工具链**不分发 sysroot**——Loongnix 20 的库树里含龙芯专有 GPU 驱动（`libEGL_loonggpu` / `libGLX_loonggpu`）和闭源加密库（`libloongson_crypto`），公开仓库没有再分发权。

但工具链本身就是工具集（gcc / g++ / ld / as / ...），**完全可以接 `--sysroot=` 参数**指定 sysroot 路径。只要你手头有 Loongnix 20 系统（或它的 rootfs 镜像），就能自己拉一份合法 sysroot 出来用。

本仓库的参考 sysroot 是反向从**龙芯 2K3000 EV 开发板**（Loongnix 20，DaoXiangHu）上拉的。下面是完整流程。

---

## 你需要

- 一台可访问的 Loongnix 20 机器或开发板（推荐 2K3000 EV，但任何跑 Loongnix 20 的机器都行）
- 它和 Mac 在同一网络，能 SSH（或拷贝 rootfs tarball 也行）
- Mac 上装 `rsync`（`brew install rsync` 装 brew 版的，自带的 BSD rsync 也凑合）

---

## 步骤 1：在 Loongnix 上装齐 dev 头

裸 Loongnix 20 镜像里只有运行时库，**`.h` 头文件**（特别是 EGL / GLES / DRM / GBM / curl / freetype / OpenSSL / ALSA / GnuTLS 这些）默认不装。先在目标上 apt install 一波：

```bash
# 在 2K3000 EV / Loongnix 上跑
sudo apt update
sudo apt install -y \
    libc6-dev linux-libc-dev \
    libstdc++-8-dev \
    libegl1-mesa-dev libgles2-mesa-dev libgl1-mesa-dev \
    libdrm-dev libgbm-dev \
    libwayland-dev libx11-dev libxext-dev libxkbcommon-dev \
    libcurl4-openssl-dev libssl-dev \
    libfreetype6-dev libfontconfig1-dev \
    libasound2-dev libpulse-dev \
    libnghttp2-dev libgnutls28-dev \
    zlib1g-dev libbz2-dev libzstd-dev \
    pkg-config
```

按需增删——你的项目用得到啥就装啥。装完后 `/usr/include` 和 `/usr/include/loongarch64-linux-gnu/` 下应该有对应的 `.h`。

> 不想 SSH 进 Loongnix？也可以挂载 Loongnix ISO / rootfs 镜像，直接对镜像的根目录跑提取脚本（参数 `--from-rootfs`）。但镜像里的 dev 头肯定比 apt install 后少，自己权衡。

---

## 步骤 2：从 Mac 跑提取脚本

```bash
cd /Users/haojing/Project/aarch64/loongarch/loongarch-toolchain4macos

# 方式 A：SSH 直拉（推荐，能拉到 apt install 后的 dev 头）
./scripts/extract-sysroot.sh \
    --from-ssh root@<2k3000-ev-ip> \
    --out /opt/loongarch-toolchain/sysroot
# 要 sudo 写 /opt 时：
sudo ./scripts/extract-sysroot.sh \
    --from-ssh root@<2k3000-ev-ip> \
    --out /opt/loongarch-toolchain/sysroot

# 方式 B：本地 rootfs tarball
# 先把 Loongnix rootfs 解到一个目录，然后：
./scripts/extract-sysroot.sh \
    --from-rootfs ~/loongnix-rootfs \
    --out ~/loongarch-sysroot
```

脚本会：

1. `rsync` 这几条路径：
   - `/lib/loongarch64-linux-gnu/`（libc / libm / libpthread / libdl / librt / ...）
   - `/lib64/`（含老世界动态加载器 `ld.so.1`）
   - `/usr/lib/loongarch64-linux-gnu/`（更多库 + `crt*.o`）
   - `/usr/include/`（glibc + 内核头 + 你 apt 装的所有 dev 头）
2. **把绝对符号链接改成相对** —— 否则在 sysroot 里 `libc.so.6 → /lib/loongarch64-linux-gnu/libc-2.28.so` 这种链接会指向 Mac 上不存在的路径
3. **改 GNU ld linker script**——形如 `GROUP ( /lib/loongarch64-linux-gnu/libc.so.6 ... )` 的 `.so` 文件，rewrite 掉绝对路径前缀
4. **清 APFS 大小写碰撞**——macOS HFS+/APFS 默认 case-insensitive，Loongnix 里 `libxt_CONNMARK.so` vs `libxt_connmark.so` 这种会撞。脚本直接 drop 整个 `xtables/` 目录（防火墙模块，cross-build 用不到）

---

## 步骤 3：验证

```bash
. /opt/loongarch-toolchain/bin/...    # 或加 PATH

# 1) 看 sysroot
loongarch64-linux-gnu-gcc -print-sysroot
# 应该输出 /opt/loongarch-toolchain/sysroot

# 2) 编 hello world
loongarch64-linux-gnu-gcc -O2 examples/hello.c -o /tmp/hello
file /tmp/hello
# /tmp/hello: ELF 64-bit LSB executable, LoongArch, ...,
#             interpreter /lib64/ld.so.1, ...

# 3) 编 C++（验证 libstdc++ 链得通）
loongarch64-linux-gnu-g++ -O2 -static-libstdc++ -static-libgcc \
    examples/hello.cc -o /tmp/hello++
file /tmp/hello++

# 4) 上目标跑
scp /tmp/hello /tmp/hello++ root@<target>:/tmp/
ssh root@<target> '/tmp/hello && /tmp/hello++'
# hello loongarch
# hello loongarch++
```

如果上面 (1) 输出的 sysroot 跟你 `--out` 的不一样，说明工具链 build 时 `--with-sysroot=` 跟你这里的路径不匹配。两种解法：

- **重 build**：用对应 prefix 重编（最干净）
- **临时绕过**：每次编译加 `--sysroot=/path/to/your/sysroot`，比如：

  ```bash
  loongarch64-linux-gnu-gcc --sysroot=$HOME/loongarch-sysroot -O2 hello.c -o hello
  ```

  推荐做法：在 `~/.zshrc` 里 alias：

  ```bash
  alias loongarch-gcc='loongarch64-linux-gnu-gcc --sysroot=$HOME/loongarch-sysroot'
  ```

  或直接 export：
  ```bash
  export LDFLAGS="--sysroot=$HOME/loongarch-sysroot"
  export CFLAGS="--sysroot=$HOME/loongarch-sysroot"
  ```

---

## 用 `--sysroot=` 在不同项目用不同 sysroot

如果你同时维护多个龙芯目标（不同发行版 / 不同 glibc 版本 / 老世界 vs 新世界），一份 toolchain + 多份 sysroot 是最省事的做法：

```
~/sysroots/
  loongnix20/          # 2K3000 EV 拉的，glibc 2.28，老世界
  uos20/               # 统信 UOS 拉的（如果有的话）
  aosc-newworld/       # AOSC 新世界拉的，dyn linker 不一样
```

切换：

```bash
loongarch64-linux-gnu-gcc --sysroot=~/sysroots/loongnix20 -O2 app.c -o app-loongnix
loongarch64-linux-gnu-gcc --sysroot=~/sysroots/uos20      -O2 app.c -o app-uos
```

> ⚠️ **注意老世界 vs 新世界**：本工具链的默认动态加载器烧死为 `/lib64/ld.so.1`（老世界）。换到新世界 sysroot 时，光改 `--sysroot=` 还不够，链出来的 INTERP 还是老世界路径，跑不了新世界目标。要彻底跨世界编，需重编一份 dyn linker 路径不同的 GCC（不打本仓库的 patch，或者改成 `/lib64/ld-linux-loongarch-lp64d.so.1`）。

---

## 验证清单

正确的 sysroot 至少应该有：

```
sysroot/
├── lib64/
│   └── ld.so.1                                      # 老世界动态加载器（关键！）
├── lib/loongarch64-linux-gnu/
│   ├── libc.so.6                                    # → libc-2.28.so
│   ├── libm.so.6
│   ├── libpthread.so.0
│   ├── libdl.so.2
│   └── librt.so.1
└── usr/
    ├── include/
    │   ├── stdio.h
    │   ├── stdlib.h
    │   └── loongarch64-linux-gnu/
    │       └── bits/
    │           └── libc-header-start.h              # multiarch 关键头，缺它编不动
    └── lib/loongarch64-linux-gnu/
        ├── libc.so                                  # linker script，GROUP(libc.so.6 ...)
        ├── libm.so
        ├── libpthread.so
        ├── crt1.o
        ├── crti.o
        ├── crtn.o
        └── libstdc++.so.6                           # 如果装了 libstdc++-8-dev
```

脚本最后会自己跑这串 sanity check。`MISS` 项基本意味着步骤 1 漏装了对应的 dev 包，回去 apt install 然后重跑脚本。

---

## 关于龙芯专有 blob

本仓库的 `.gitignore` 默认忽略 `sysroot/` 目录，**你拉下来的 sysroot 不要 commit 进 git**。即使是你自己的 fork，公开 push 含 `libloongson_crypto.so.0` / `libEGL_loonggpu.so.0` / `loonggpu/` 的 sysroot 也是再分发龙芯专有组件，没拿到龙芯书面许可前别这么干。

自己 Mac 上用没问题——你已经合法拥有 Loongnix 系统里这些文件的本地使用权。

---

## 反向提取的来源（本仓库 reference sysroot）

| 项 | 值 |
|---|---|
| 硬件 | 龙芯 **2K3000 EV** 开发板 |
| 系统 | Loongnix 20（DaoXiangHu） |
| Glibc | 2.28 |
| Linux kernel headers | 4.19.190.8.26-lnd.11 |
| libstdc++ on target | 6.0.25（来自 GCC 8.3 系统包）—— 跟我们工具链 GCC 14.2 编出的 libstdc++ 6.0.33 不兼容，所以 C++ 程序部署时推荐静态链或 rpath bundle |

也就是说，本仓库的 toolchain 跟"任何 Loongnix 20 系统"理论上都兼容；2K3000 EV 只是我们手头那块板子。如果你的是 3A5000 / 3C5000 / 其他 Loongson 处理器 + Loongnix 20 系统，照着上面的步骤一样能拉出能用的 sysroot。
