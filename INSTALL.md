# LoongArch64 交叉编译工具链 · macOS 安装指南

`loongarch-toolchain-X.Y.Z-arm64.pkg`

在 Apple Silicon Mac 上一键安装的 LoongArch64 交叉编译工具链，目标：龙芯 Loongnix 20（DaoXiangHu）。

---

## 系统要求

| 项 | 要求 |
|---|---|
| 操作系统 | macOS 14 (Sonoma) 或更高 |
| 架构 | **Apple Silicon (arm64)** —— 不支持 Intel Mac |
| 硬盘空间 | ≥ 4 GB（pkg ~1 GB，装完 ~3 GB，外加自行提取的 sysroot ~2.5 GB） |
| 工具 | macOS 自带 `installer`、`pkgutil`；提取 sysroot 需要能访问 Loongnix 20 机器或 rootfs tarball |

---

## 安装步骤

### 1. 从 GitHub Releases 下载 pkg

到本仓库的 [Releases](../../releases) 页下载最新版 `loongarch-toolchain-X.Y.Z-arm64.pkg`，并核对 SHA-256：

```bash
shasum -a 256 ~/Downloads/loongarch-toolchain-X.Y.Z-arm64.pkg
# 跟 Release 页公布的哈希对比
```

### 2. 验证签名（可选）

```bash
pkgutil --check-signature ~/Downloads/loongarch-toolchain-X.Y.Z-arm64.pkg
```

期望（视发布者而定）：
```
Status: signed by a developer certificate issued by Apple for distribution
```

### 3. 双击安装

> ⚠️ **如果出现"无法打开 ××.pkg，因为它来自身份不明的开发者"**：当前版本签名了但未做 Apple 公证。两种解决：
>
> **方法 A — 右键打开**：Finder 里 **右键** `loongarch-toolchain-...pkg` → 选 **打开** → 弹窗里再次点 **打开**。
>
> **方法 B — 系统设置允许**：双击被拦下后，去 **系统设置 → 隐私与安全性**，最下方点 **仍要打开**。

按向导走完即可。会提示输入管理员密码（写入 `/opt/loongarch-toolchain/`）。

### 4. CLI 安装（脚本/CI 友好）

```bash
sudo installer -pkg ~/Downloads/loongarch-toolchain-X.Y.Z-arm64.pkg -target /
```

### 5. 配置 PATH

```bash
echo 'export PATH="/opt/loongarch-toolchain/bin:$PATH"' >> ~/.zshrc
source ~/.zshrc
```

### 6. 提取 sysroot（必做！）

pkg **不含 sysroot**（专有组件不能再分发）。装完 pkg 后必须自己提取：

```bash
# 从 Loongnix 20 实机
./scripts/extract-sysroot.sh --from-ssh root@<loongnix-host> --out /opt/loongarch-toolchain/sysroot

# 或从本地 rootfs 目录
sudo ./scripts/extract-sysroot.sh --from-rootfs /mnt/loongnix-rootfs --out /opt/loongarch-toolchain/sysroot
```

脚本会拉必要的 `.h` 和 `.so`、修绝对软链、生成 GNU ld linker script、处理 APFS 大小写碰撞。详见 `scripts/extract-sysroot.sh` 头部注释。

---

## 验证安装

```bash
loongarch64-linux-gnu-gcc --version
# 期望：loongarch64-linux-gnu-gcc (GCC) 14.2.0

loongarch64-linux-gnu-gcc -print-sysroot
# 期望：/opt/loongarch-toolchain/sysroot

loongarch64-linux-gnu-ld --version
# 期望：GNU ld (GNU Binutils) 2.42
```

### Hello World

```bash
cat > hello.c <<'EOF'
#include <stdio.h>
int main() { printf("hello loongarch\n"); return 0; }
EOF

loongarch64-linux-gnu-gcc -O2 hello.c -o hello

file hello
# hello: ELF 64-bit LSB executable, LoongArch, ..., interpreter /lib64/ld.so.1, ...
```

`scp hello target:/tmp/ && ssh target /tmp/hello` 即可看到 `hello loongarch`。

### C++

```bash
cat > hello.cc <<'EOF'
#include <iostream>
int main() { std::cout << "hello loongarch++\n"; return 0; }
EOF

# 静态链 libstdc++ / libgcc：单文件部署，~10MB
loongarch64-linux-gnu-g++ -O2 -static-libstdc++ -static-libgcc hello.cc -o hello++
```

---

## 工具链组成

| 组件 | 版本 | 来源 |
|---|---|---|
| Binutils | 2.42 | 自源码构建 |
| GCC | 14.2.0 | 自源码构建（C / C++） |
| Glibc | 2.28 | 来自 Loongnix 20 sysroot（用户自提取） |
| Linux kernel headers | 4.19.190.8.26-lnd.11 | 来自 Loongnix 20 sysroot（用户自提取） |
| libstdc++ | 6.0.33（GLIBCXX_3.4.32） | GCC 14.2 编出 |
| libgcc_s | 14.2 | GCC 14.2 编出 |

---

## 已知限制

详见 [README.md](README.md) "限制 / 已知问题" 一节。要点：

1. 只编"老世界"二进制，跑不了上游新世界 LoongArch 系统
2. 不含 libsanitizer
3. NSS 相关程序不能 `-static` 全静态链
4. 仅 Apple Silicon (arm64) macOS

---

## 卸载

```bash
sudo rm -rf /opt/loongarch-toolchain
sudo pkgutil --forget cn.made2020.app.loongarch
```

`/opt/loongarch-toolchain/` 是工具链唯一占据的位置。

---

## 故障排查

见 [README.md](README.md) "故障排查" 一节。
