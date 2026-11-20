# rpp-release-tool

版本：**0.0.3**

reprepro 的**发布前置闸门**：扫描产物目录 → 校验 `.deb` / `.dsc` → 预检目标仓库 → 预览变更树 → 确认 → 真正执行 reprepro → 发布后核对。

TUI 基于 FTXUI，配置用 TOML（默认 `~/.config/rpp-release-tool/config.toml`），界面支持 **zh-CN / en-US**。

---

## 1. 为什么需要它

踩过的坑固化成规则：

| 规则 | 级别 |
|---|---|
| `.deb` 和 `.dsc` 必须同时在（缺一边只弹窗确认，不硬拦） | 警告 + 确认 |
| `.dsc` 的 `Files:` / `Checksums-Sha256:` 中每个文件存在，size / md5 / sha256 对得上 | 阻塞 |
| `.dsc` 必须有 `Priority:`（reprepro 会 `No priority, skipping`） | 阻塞 |
| `.deb` 的 control 也必须有 `Priority:`（实测 reprepro 会 `No priority given, skipping`） | 阻塞 |
| `.dsc` 的 `Version` 与所有 `.deb` 的 `Version` 一致；`Source` 名一致；`Binary:` 列表覆盖所有 deb | 阻塞 |
| `.deb` 覆盖的架构 = 目标架构集合（`all` 单独处理） | 按 policy |
| `.orig.tar.*` 有对应 `.asc`（并写进 `.dsc`） | 警告 |
| 每个 `.deb` 有 `.buildinfo` | 按 policy |
| 目录里没被 `.dsc` 引用的源码文件 | 警告 |

预检还会检查（就是"发布前就知道会不会炸"）：

- codename / component 是否存在于 `conf/distributions`
- 目标 codename 的 `Architectures:` 是否包含 `source`（否则 `.dsc` 会被静默跳过）
- 每个 deb 的架构是否被 `Architectures:` 允许
- `SignWith` 的私钥在当前用户 `GNUPGHOME` 是否可用
- `<base>/{db,pool,dists}` 可写性、`db/lock` 占用、剩余空间
- `conf/options` 的 `basedir` 是否与配置一致
- `.dsc` 的 `Distribution:` 是否与目标 codename 一致

---

## 2. 构建

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

依赖：

- CMake ≥ 3.16、GCC 13+（用到 `std::format`）
- FTXUI：优先系统包（Debian trixie 的 `libftxui-dev`），找不到则 FetchContent `v6.1.9`
- toml++：优先 `find_package(tomlplusplus)`，否则 FetchContent `v3.4.0`
- 打 deb/源码包还需要：`dpkg-dev`、`debhelper`、`fakeroot`、`file`、`xz-utils`

运行时需要：`dpkg-deb`、`md5sum`、`sha256sum`、`gpg`、`reprepro`。

### 打包与发布

发布包用标准的 `dpkg-buildpackage`（debhelper）构建，因此会同时产生 `.buildinfo` 和源码包：

```bash
# 当前架构：.deb + .buildinfo + 原始二进制；--source 同时生成 .dsc 和源码 tar
bash scripts/make-release.sh --source build-out

# 把 deb + dsc + 源码 tar + buildinfo 汇总成一个未压缩 tar（供 reprepro 导入）
# 注意输入和输出用不同目录，脚本会重建输出目录
bash scripts/make-repo-tar.sh build-out dist
```

输出：

- `rpp-release-tool_0.0.3_<arch>.deb`：安装到 `/usr/bin/rpp-release-tool`，文档在 `/usr/share/doc/rpp-release-tool/`。
- `rpp-release-tool-0.0.3-linux-<arch>`：原始二进制（dh_strip 已 strip）。
- `rpp-release-tool-0.0.3-repo.tar`：**不压缩**，包含：
  - 全部架构的 `.deb`
  - `rpp-release-tool_0.0.3.dsc` + 源码 `rpp-release-tool_0.0.3.tar.xz`
  - 每个架构的 `.buildinfo`
  
  解包后直接指向该目录，本工具会看到完整源码 + 三个二进制 + buildinfo，不再有 “only .deb / missing .buildinfo” 警告。
- `sha256sums.txt`：release 目录内文件的校验和。

CI（`.github/workflows/ci.yml`）用 matrix 在 Debian trixie 里构建三种架构：

| 目标 | 平台 |
|---|---|
| amd64 | `linux/amd64` |
| arm64 | `linux/arm64`（QEMU） |
| armhf | `linux/arm/v7`（QEMU） |

每个架构用 `dpkg-buildpackage -b` 产出 `.deb` + `.buildinfo` + 原始二进制；amd64 额外用 `dpkg-buildpackage -S` 产出源码包。最后的 `assemble` job 汇总成一个 `rpp-release-tool-0.0.3-repo.tar` 和校验和。推送 `v*` 标签时自动创建 GitHub Release，附件为：三个 `.deb`、三个原始二进制、一个 repo tar、校验和文件。

---

## 3. 配置

路径：`${XDG_CONFIG_HOME:-~/.config}/rpp-release-tool/config.toml`
（可用 `--config` 或 `RPP_RELEASE_CONFIG` 覆盖读取路径；首次运行会弹表单创建）

最小配置只需要仓库根目录，其余结构直接从 reprepro 的 `conf/distributions` 读取：

```toml
version = 1

[paths]
artifacts = "/srv/incoming"          # 待发布产物目录

[policy]
require_both   = true                # 缺 deb/dsc 时弹窗确认
missing_arch   = "error"             # error | warn | ignore
buildinfo      = "warn"
sha256         = "error"
allow_replace  = false               # 允许同版本覆盖（remove + include）

[ui]
detail         = "normal"            # summary | normal | full
unchanged_fold = 15                  # 未变更折叠阈值；-1 不折叠；0 不显示
language       = "auto"              # auto | zh-CN | en-US

[[repo]]
base = "/srv/reprepro"
# 下面都可以省略：
# name          = "spark"                              # 默认用 base 的目录名
# conf          = "/srv/reprepro/conf/distributions"   # 默认 base/conf/distributions
# codename      = "trixie"                             # 默认用第一个 Codename
# component     = "main"                               # 默认用第一个 Components
# architectures = ["amd64", "arm64", "source"]         # 默认读该 Codename 的 Architectures
# incoming      = "/srv/reprepro/incoming"
```

例如 `conf/distributions` 里有：

```
Codename: bookworm
Architectures: amd64 arm64 armhf source
Components: main
SignWith: 3238B26E255818B9

Codename: trixie
Architectures: amd64 arm64 armhf source
Components: main test
SignWith: 3238B26E255818B9
```

TUI 会自动展示并让你选择：

```
spark  /srv/reprepro
  bookworm
    main
  trixie
    main
    test
```

- 一个 reprepro base 只写一个 `[[repo]]`；不要为每个 codename/component 重复写。
- `name`、`conf`、`codename`、`component`、`architectures` 都是可选的；`conf` 读不到时保存配置会直接报错。
- 每个 codename 的架构、组件、SignWith 都以 `conf/distributions` 为准。
- `[ui].detail`：`summary` 只到包节点、`normal` 显示文件、`full` 额外显示 `[index]` 行。

---

## 4. 使用

### TUI

```bash
rpp-release-tool /tmp/pub/ccid
```

启动后是**主页菜单**：

```
主页
配置: /real/config.toml
产物目录: /real/pub
▸ 上传 / 更新包
    选择目标仓库和本地包，预览变更后发布
  管理已发布包
    删除包、重新导出索引、清理未引用文件
  编辑仓库设置
    仓库、路径、发布策略
  应用程序设置
    界面、语言、详细程度、折叠阈值
```

- `↑/↓` 选择，`enter` 进入，也可以直接按 `1`~`4`。
- “编辑仓库设置”打开设置页的仓库标签；“应用程序设置”直接打开界面标签。两者仍是同一个设置页，可以继续用 `tab` 切换。
- 选“上传/更新包”后进入目标仓库树，选 component → 选择本地包集 → 预览 → 确认。
- 选“管理已发布包”后进入同一个仓库树，选 component 后进入**仓库维护**页。

**统一操作约定**（底栏常驻显示当前页最常用的 3~4 个键，不用再反复按 `?`）：

- `Esc`：返回上一级 / 取消，**永远不退出程序**（根页面无操作）。
- `Enter`：进入或激活当前项；结果页（发布后核对）按 `Enter` 表示“完成”，回到主页。
- `g`：执行当前流程（包集页生成快照并预览，维护页预览并执行删除）。
- `q`：退出程序（仍会弹确认）；`?`：完整快捷键帮助。
- `+` / `-`：标记 / 取消标记（包集页、维护页）；
- 输入状态（设置页文本框、覆盖确认输入 codename、目录筛选）下字母优先作为输入，不触发全局快捷键。

| 键 | 作用 |
|---|---|
| `↑/↓` | 移动 |
| `enter` | 继续 / 展开分组 / 切换标记 |
| `+` / `-` | 标记 / 取消标记（包集页、维护页） |
| `g` | 预览并继续（包集页）/ 预览并执行（维护页） |
| `x` / `y` | 在确认页真正执行 reprepro（`x` 与 `y` 等价） |
| `空格` | 折叠/展开树节点；设置页里切换选项 |
| `u` | 显示/隐藏未变更项 |
| `p` | 复制 reprepro 命令 |
| `r` | 重新扫描 |
| `t` | 换仓库 |
| `s` | 设置页（仓库 / 路径 / 策略 / 界面） |
| `l` | 中英切换 |
| `?` | 快捷键浮层（只列当前页 + 全局） |
| `o` | 重新选择本地包集（报告 / 变更页） |
| `d` | 在包集页切换产物目录 |
| `a` / `i` | 包集页全选 / 反选 |
| `v` | 查看仓库详情（仓库页）/ 包集详情（包集页） |
| `esc` | 返回 / 取消 |
| `q` | 退出（弹窗：是 / 否 / 保存状态缓存并退出） |
| `ctrl+s` | 保存配置（设置页内同样生效，不会退出） |

#### 选择本地包（集）

选定目标 component 后进入**选择本地包集**页。列表按“相对仓库的状态”分组，类似 aptitude 的包分组：

```
产物目录: /real/fixtures/arts/complete-2.0
▾ 可更新 (1)
+   ccid 2.0-1  [更新 1.8.4-1 → 2.0-1]  1 个 .dsc / 2 个 .deb  amd64 arm64
▸ 已存在 (1)
待发布: 1 个包集
```

- 状态由本地包与当前 codename/component 的仓库内容比较得出：`可更新`、`新增`、`降级`、`已存在`。版本比较用 dpkg 规则（epoch、`~`、revision）。
- `↑/↓` 移动；`enter` 在分组上展开/折叠，在包集上切换标记；`+` 标记，`-` 取消（在分组上操作会作用于整组）。
- 默认只预标记 `可更新` 和 `新增`；`已存在` 分组默认折叠，需要时可以展开后手动标记（用于强制重发）。
- `g` = 生成快照并进入预览（即原来的“回车继续”）；`a` 全选，`i` 反选，`v` 看包集详情，`d` 换目录，`r` 重新扫描并重新比较，`esc` 返回。
- 底栏会显示 `待发布: N 个包集`。
- `d` 打开目录浏览器：列表第一行是“✓ 使用当前目录”，`enter` 直接选中它（或按 `空格`）；其余行 `enter` 进入目录，`←` 返回上级。目录行会显示其中的 `.deb`/`.dsc`/`.changes` 数量。
- 目录浏览器里直接输入字母即可**按前缀跳转**（大小写不敏感），继续输入会不断收窄；`退格` 删除字符，`esc` 清除筛选（再按一次返回）。筛选状态会显示在顶部，`ctrl+r` 重新读取当前目录。
- 回车后立即把选中的文件**快照**到 `~/.cache/rpp-release-tool/stage/run-<pid>-<stamp>/`：优先用 reflink（写时复制），不支持时普通复制。之后校验、diff、确认页和 reprepro 全部使用快照路径，源目录删改都不影响本次发布；正常退出时自动清理，崩溃/被杀留下的快照会在下次暂存时清理（仅删除 7 天前的）。可用 `RPP_RELEASE_STAGE` 指定别的暂存根目录。
- 目录里有 `.changes` 时只能整体发布（按 `include` 语义），列表会显示为“整个上传（含 .changes）”。
- 报告页/变更页按 `o` 可重新进入本页换目录或换包集。

> 没有采用“另开 shell + alias 标记 + 硬链接”的方案：alias 只在交互 shell 生效、状态文件要自己维护；硬链接跨文件系统会失败，也挡不住原地改写/截断；而且真正的危险窗口是“预览后到 reprepro 读取前”，在 shell 退出时才做链接并不能消除它。快照在选中时立刻完成，之后所有步骤都只读快照，窗口为零。

#### 管理已发布包（仓库维护）

主页选“管理已发布包” → 选 component 后进入：

```
仓库维护
目标: real/trixie/main
▾ 源码包 (1)
  ccid 2.1.0-1
▾ 二进制包 (1)
  libccid 2.1.0-1  amd64 arm64
待删除: 0 项
```

| 键 | 作用 |
|---|---|
| `-` | 标记删除（在分组上操作整组） |
| `+` | 取消标记 |
| `enter` | 分组展开/折叠；包条目切换标记 |
| `g` | 预览并执行删除（输入 codename 确认；`x`/`d` 同义） |
| `e` | 重新导出索引并签名（`reprepro export`，确认后执行） |
| `c` | 清理 pool 中未引用的文件（`deleteunreferenced`，输入 codename 确认） |
| `r` | 重新读取仓库状态 |
| `esc` | 返回仓库树 |

- 底栏显示 `待删除: N 项`。
- 删除二进制：`reprepro -C <component> -T deb remove <codename> <pkg>`（不带 `-A`，删除该包的所有架构）。
- 删除源码：`reprepro removesrc <codename> <source> <version>`。注意 **`removesrc` 不能限定 component**，并且会连同该源码构建的二进制一起删掉；界面会显式提示。
- 删除后会自动再执行一次 `reprepro export <codename>`，保证 `dists/`、签名和 db 一致。
- 所有维护动作都会先展示将执行的完整命令；删除/清理必须手动输入 codename 才会执行。执行结果（每条命令的 stdout/stderr）会在只读弹窗里列出。

#### 查看详情

- **仓库页按 `v`**：显示仓库名、根目录、`conf/distributions` 路径、codename、component、架构、Suite、components、SignWith、`dists/<codename>` 与 `InRelease` 是否存在，以及该 component 当前已发布的包（按 `包 版本 (架构)` 列出，最多 80 条）。
- **包集页按 `v`**：显示源码包名、版本、产物目录、类型（完整/仅源码/仅二进制）、架构，以及该包集包含的每个 `.dsc` / `.deb` 文件名、架构和大小。
- 详情弹窗只读，`↑/↓` 滚动，`esc` / `?` / `q` 关闭。

#### 仓库写入边界

本工具**只通过 reprepro 写仓库**：发布用 `includedsc` / `includedeb`（覆盖时先按范围 `remove` 再 include），`.changes` 用 `include`；维护用 `remove` / `removesrc` / `export` / `deleteunreferenced`。它不会直接编辑 `conf/distributions`、`dists/` 或 `pool/`，也不会绕过 reprepro 的索引与签名流程。仓库配置的修改仍在设置页里改本工具的 TOML；reprepro 自身的 `conf/distributions` 请用编辑器维护。

设置页用 `tab`（或 `shift+tab`）切换标签页；第一页全是文本框，所以左右键也可切页，选项字段用空格切换。方向键只移动焦点。`ctrl+a` / `ctrl+d` 增删仓库，`[/]` 切换正在编辑的仓库，`esc` 放弃未保存修改并返回。

退出弹窗支持 `←/→` + `enter`，也可直接按 `y` / `n` / `s` 立即执行（不保存退出 / 继续使用 / 保存状态缓存后退出）。默认停在“否”，避免误按回车退出。

UI 选择状态（仓库、codename、component、标签页、是否显示未变更）单独缓存于 `~/.cache/rpp-release-tool/state.toml`（可用 `RPP_RELEASE_STATE` 覆盖），与配置文件分离，损坏或删除都不影响启动。按 `?` 打开的帮助浮层只显示当前页可用快捷键和全局快捷键；底栏只保留 `[?] 帮助`。

发布前会先展示**变更树**（相对当前仓库的 diff）：

```
trixie/
  main/
    amd64/
      libccid  1.8.5-1  变更 1 / 共 1 项
      + [index] libccid 1.8.5-1 新增
      + libccid_1.8.5-1_amd64.deb  832 B
      … 未更改 23 项   [u] 展开
    source/
      ccid  1.8.5-1  变更 3 / 共 3 项
      + ccid_1.8.5-1.dsc
      + ccid_1.8.5-1.debian.tar.xz
      + ccid_1.8.5.orig.tar.xz
```

- `+` 绿：新增；`*` 黄：覆盖（同名文件校验和不同）
- `-` 预留：当前流程不产生（reprepro 发新版会自动清掉旧版本）
- 首列符号，目录/包节点不带符号
- 每个架构单独成组（`amd64` / `arm64` / `all` / `source`），component 分开
- **未变更的目录自动折叠成一行**；展开的目录里未变更项超过 `unchanged_fold` 就折叠成 `… 未更改 N 项`

含 `*`（覆盖）时必须手输 codename 才能发布；缺 `.deb`/`.dsc` 时先弹一次确认，两次确认分开。

### 非交互（CI）

```bash
rpp-release-tool --no-tui --check-only --config cfg.toml /tmp/pub/ccid
rpp-release-tool --no-tui --print-cmd --config cfg.toml /tmp/pub/ccid
rpp-release-tool --no-tui --ack --config cfg.toml /tmp/pub/ccid      # 真发布
rpp-release-tool --no-tui --ack --replace --config cfg.toml /tmp/pub/ccid  # 覆盖同版本
```

- 缺 `.deb`/`.dsc`：`--ack` 确认即可
- 同版本覆盖：`--ack` **不够**，必须显式加 `--replace`（未加时退出码 2，且不会执行任何命令）

退出码：

| 码 | 含义 |
|---|---|
| 0 | 成功 / 无需变更 |
| 1 | 存在阻塞问题 |
| 2 | 需要确认（缺一边 / 覆盖 / 非交互） |
| 3 | 配置错误 |
| 4 | 用法错误 |
| 5 | reprepro 执行失败 |
| 6 | 发布后核对失败 |

### 发布后核对

自动做四件事：

1. 新版本是否出现在 `Packages` / `Sources` 索引
2. `reprepro ls` 视角的包版本
3. `dists/<codename>/InRelease` 是否存在
4. `gpg --verify InRelease`

---

## 5. 变更树的语义

- 数据来源全部本地：`dists/<cod>/<comp>/{binary-<arch>/Packages,source/Sources}` + `pool/` 文件校验和
- 同名文件校验和一致 → 未变更；不同 → `*`；仓库里没有 → `+`
- 同一版本已存在且内容一致 → 提示"无变更"，**不执行 reprepro**（避免 `already exists`）
- 覆盖流程：按变更范围精确 `remove`，再 `includedsc` / `includedeb`
  - 源码：`reprepro -C <comp> -T dsc remove <codename> <source>`
  - 二进制：`reprepro -C <comp> -A <arch> -T deb remove <codename> <pkg>`
  - `Architecture: all` 的包会注册到每个具体架构，且 `-A all` 无效，因此退化为按类型整体 remove
  - 只移除真正发生 `*` 变更的包；其他架构（如仓库里的 arm64）不会被误删
- `Architecture: all` 的包放在 `all/` 节点，不重复出现在每个架构下

---

## 6. 测试

```bash
ctest --test-dir build --output-on-failure
```

- `tests/unit_tests.cpp`：dsc 解析（clearsign）、配置往返、diff、折叠阈值、检查规则、i18n
- `tests/integration.sh`：用真实 `dpkg-source` / `dpkg-deb` 造包，跑 `--check-only` 的六种场景
- `tests/make_pkg.sh`：可复用的小包生成器
- `tests/make_fixtures.sh`：生成完整测试矩阵（见下）

### 测试夹具（包 + 仓库变体）

`tests/make_fixtures.sh` 一次生成完整的测试矩阵：

```bash
RPP_TEST_KEY=<gpg-key-id> bash tests/make_fixtures.sh /tmp/rpp-real/fixtures
```

- `arts/`：完整包、`Architecture: all`、仅源码、仅二进制、缺架构，以及校验和 / Priority / 版本 / Source / 缺文件 / 截断 deb 等破坏样本
- `repos/`：`main-src`、`main-nosrc`、`main-contrib-src`、`src-only`
- `configs/`：容器路径（`/real/fixtures/...`）
- `configs-host/`：宿主路径（`/tmp/rpp-real/fixtures/...`）

仓库只是骨架，首次使用前在容器里 `reprepro -b <repo> export` 即可（详见生成的 `fixtures/README.md`）。

### 用容器跑真实 reprepro

基础镜像只读使用，所有安装都在容器层；用 `--name`、不加 `--rm`，容器可复用，基础镜像不受影响（不做派生镜像）：

```bash
docker run -d --name rpp-trixie --network host \
  -v "$PWD":/work -v /tmp/rpp-real:/real debian:trixie sleep infinity

docker exec rpp-trixie bash -c 'cat > /etc/apt/sources.list.d/debian.sources <<EOF
Types: deb
URIs: http://mirrors6.tuna.tsinghua.edu.cn/debian
Suites: trixie
Components: main
Signed-By: /usr/share/keyrings/debian-archive-keyring.gpg
EOF'
docker exec rpp-trixie apt-get -o Acquire::ForceIPv6=true update
docker exec rpp-trixie apt-get install -y reprepro gnupg \
  libftxui-component5.0.0 libftxui-dom5.0.0 libftxui-screen5.0.0

# 生成签名密钥、初始化仓库、造包、发布
docker exec rpp-trixie /work/build/rpp-release-tool --no-tui --ack --config /real/config.toml
```

> 容器内所有命令默认以 **root**（uid 0）执行；GPG 密钥在容器的 `/root/.gnupg` 中，与宿主完全隔离，不会读取宿主密钥。宿主的 `/tmp/rpp-real` 挂载为 `/real`，容器写入的文件在宿主上属于 root。

已验证：首次发布、重复发布（无变更跳过）、升级发布、`InRelease` 验签、`reprepro ls` 核对。

---

## 7. 已知限制

- 只支持本机路径（不做远程/ssh 传输）
- 覆盖（`*`）默认关闭，需要 `[policy].allow_replace = true`；非交互下还要 `--replace`
- `.asc` / `.buildinfo` 规则目前是 warning，可按 policy 调整
- 不解析 `conf/overrides` 的 Priority/Section 覆盖（依赖 control 字段）
- 未实现 `incoming/` + `processincoming` 路径（配置里保留了 `incoming` 字段）

---

## 8. 许可证

MIT，见 [LICENSE](LICENSE)。
