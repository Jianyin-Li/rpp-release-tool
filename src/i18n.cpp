#include "i18n.hpp"

#include <cstdlib>
#include <cstring>

namespace rpp {
namespace {

struct Ent {
  const char* key;
  const char* zh;
  const char* en;
};

// Keep keys stable: they are also used by --no-tui output and tests.
constexpr Ent kTable[] = {
    // ---- app / navigation ----
    {"app.title", "rpp 发布闸门", "rpp release gate"},
    {"app.subtitle", "reprepro 发布前置检查与执行", "preflight checks and publishing for reprepro"},
    {"nav.quit", "[q] 退出", "[q] quit"},
    {"nav.back", "[esc] 返回", "[esc] back"},
    {"nav.next", "[enter] 继续", "[enter] next"},
    {"nav.rescan", "[r] 重新扫描", "[r] rescan"},
    {"nav.settings", "[s] 设置", "[s] settings"},
    {"nav.repo", "[t] 换仓库", "[t] change repo"},
    {"nav.copy", "[p] 复制命令", "[p] copy commands"},
    {"nav.run", "[y] 执行", "[y] execute"},
    {"nav.help", "[?] 帮助", "[?] help"},
    {"nav.publish", "[y] 发布", "[y] publish"},
    {"nav.lang", "[l] 切换语言", "[l] switch language"},
    {"nav.expand", "[空格] 折叠/展开", "[space] collapse/expand"},
    {"nav.unchanged", "[u] 显示/隐藏未变更", "[u] show/hide unchanged"},
    {"nav.updown", "[↑/↓] 移动", "[up/down] move"},
    {"nav.leftright", "[←/→] 切换", "[left/right] switch"},
    {"nav.add", "[a] 新增", "[a] add"},
    {"nav.del", "[d] 删除", "[d] delete"},
    {"nav.save", "[ctrl+s] 保存", "[ctrl+s] save"},
    {"nav.discard", "[esc] 放弃", "[esc] discard"},
    {"hint.esc", "Esc 返回", "Esc back"},
    {"hint.enter_open", "Enter 进入", "Enter open"},
    {"hint.enter_select", "Enter 选择", "Enter select"},
    {"hint.enter_next", "Enter 继续", "Enter next"},
    {"hint.enter_confirm", "Enter 确认", "Enter confirm"},
    {"hint.enter_run", "Enter 执行", "Enter run"},
    {"hint.enter_done", "Enter 完成", "Enter done"},
    {"hint.save", "Ctrl+S 保存", "Ctrl+S save"},
    {"hint.tab", "Tab 切页", "Tab switch"},
    {"hint.mark", "+/- 标记", "+/- mark"},
    {"hint.g_preview", "g 预览", "g preview"},
    {"hint.g_run", "g 执行", "g run"},
    {"hint.help", "? 帮助", "? help"},
    {"hint.quit", "q 退出", "q quit"},
    {"hint.details", "v 详情", "v details"},
    {"nav.tabs", "[tab] 切换标签页", "[tab] switch tab"},
    {"nav.choice", "[空格] 切换选项", "[space] cycle option"},
    {"nav.help", "[?] 快捷键", "[?] shortcuts"},
    {"nav.settings.repo", "[ctrl+a] 新增仓库  [ctrl+d] 删除仓库", "[ctrl+a] add repo  [ctrl+d] delete repo"},
    {"nav.settings.prevnext", "[/] 切换仓库", "[/] switch repo"},

    // ---- common ----
    {"common.yes", "是", "yes"},
    {"common.no", "否", "no"},
    {"common.ok", "通过", "ok"},
    {"common.warn", "警告", "warning"},
    {"common.error", "错误", "error"},
    {"common.none", "无", "none"},
    {"common.auto", "自动", "auto"},
    {"common.unknown", "未知", "unknown"},
    {"common.enabled", "启用", "enabled"},
    {"common.disabled", "禁用", "disabled"},
    {"common.na", "—", "—"},
    {"common.line", "第 {} 行", "line {}"},
    {"common.bytes", "{} B", "{} B"},
    {"common.mb", "{} MB", "{} MB"},

    // ---- fields ----
    {"field.name", "名称", "name"},
    {"field.base", "仓库根目录 (reprepro -b)", "repository base (reprepro -b)"},
    {"field.conf", "reprepro 配置 (conf/distributions)", "reprepro config (conf/distributions)"},
    {"field.codename", "codename", "codename"},
    {"field.component", "component", "component"},
    {"field.architectures", "目标架构 (空格分隔)", "target architectures (space separated)"},
    {"field.incoming", "incoming 目录 (可选)", "incoming directory (optional)"},
    {"field.artifacts", "产物目录", "artifacts directory"},
    {"field.detail", "详细程度", "detail level"},
    {"field.unchanged_fold", "未变更折叠阈值 (-1 不折叠, 0 不显示)", "unchanged fold threshold (-1 never, 0 hide)"},
    {"field.show_unchanged", "默认显示未变更项", "show unchanged entries by default"},
    {"field.language", "语言", "language"},
    {"field.require_both", "缺 .deb/.dsc 时弹窗确认", "confirm when .deb/.dsc is missing"},
    {"field.missing_arch", "缺架构时", "when an architecture is missing"},
    {"field.buildinfo", "缺 .buildinfo 时", "when .buildinfo is missing"},
    {"field.sha256", "sha256 校验失败时", "when sha256 mismatches"},
    {"field.allow_replace", "允许覆盖已有版本 (remove + include)", "allow replacing existing versions"},

    // ---- config screen ----
    {"config.title", "配置 reprepro 仓库", "Configure reprepro repository"},
    {"config.missing", "还没有配置文件，先把仓库信息填好", "No config file yet. Fill in the repository first."},
    {"config.invalid", "配置文件无法读取，请在这里修正", "The config file cannot be read. Fix it here."},
    {"config.first_hint", "保存后写入 {}", "Saved to {}"},
    {"config.save_hint", "[ctrl+s] 保存并继续", "[ctrl+s] save and continue"},
    {"config.saved", "配置已保存到 {}", "Config saved to {}"},
    {"config.save_failed", "保存失败: {}", "Save failed: {}"},
    {"config.need_repo", "至少需要配置一个仓库", "At least one repository is required"},
    {"config.need_field", "{} 不能为空", "{} must not be empty"},
    {"config.repo_conf_error", "无法读取仓库配置 {}: {}", "cannot read repository config {}: {}"},
    {"config.repo_conf_empty", "仓库配置 {} 里没有 Codename", "no Codename in repository config {}"},
    {"config.auto_hint", "codename / component / 架构每次从 conf/distributions 读取，不写入配置；name 留空时用仓库目录名", "codename / component / architectures are read from conf/distributions on every run and never stored; name defaults to the repository directory name"},
    {"config.policy_hint", "warn = 提示（缺架构在确认页确认）；error = 阻塞；ignore = 忽略", "warn = warn (missing arch is confirmed); error = block; ignore = skip"},
    {"config.base_missing", "仓库目录不存在: {}", "Repository base does not exist: {}"},
    {"config.conf_missing", "找不到 reprepro 配置: {}", "reprepro config not found: {}"},
    {"config.conf_ok", "配置可解析：{} 个 codename", "Config parsed: {} codename(s)"},
    {"config.conf_bad", "配置解析失败: {}", "Config parse failed: {}"},
    {"config.arch_hint", "从 distributions 读取实际值", "actual values come from distributions"},

    // ---- home menu ----
    {"home.title", "主页", "Home"},
    {"home.upload", "上传 / 更新包", "Upload / update packages"},
    {"home.upload.desc", "选择目标仓库和本地包，预览变更后发布", "pick target and local packages, preview, then publish"},
    {"home.manage", "管理已发布包", "Manage published packages"},
    {"home.manage.desc", "删除包、重新导出索引、清理未引用文件", "delete packages, re-export indices, clean unreferenced files"},
    {"home.repo_settings", "编辑仓库设置", "Repository settings"},
    {"home.repo_settings.desc", "仓库、路径、发布策略", "repositories, paths, publish policy"},
    {"home.settings", "设置", "Settings"},
    {"home.settings.desc", "仓库、路径、发布策略、界面", "repositories, paths, publish policy, interface"},
    {"home.config", "配置", "config"},
    {"home.artifacts", "产物目录", "artifacts"},
    {"home.hint", "[↑/↓] 选择   [enter] 进入   [q] 退出", "[up/down] choose   [enter] open   [q] quit"},
    {"home.no_config", "还没有配置文件，先在设置里填写仓库信息", "no config yet; fill in the repository settings first"},
    {"repo.purpose.upload", "上传 / 更新", "upload / update"},
    {"repo.purpose.maint", "管理已发布包", "manage published packages"},

    // ---- repo target screen ----
    {"repo.title", "选择发布目标", "Select publish target"},
    {"repo.hint", "选中 component 后回车继续", "Select a component and press enter"},
    {"repo.empty", "还没有配置任何仓库，按 s 打开设置", "No repository configured. Press s for settings."},
    {"repo.conf_error", "无法读取 {}: {}", "Cannot read {}: {}"},
    {"repo.count", "{} 个仓库", "{} repository(ies)"},
    {"repo.target", "目标", "target"},
    {"repo.archs", "架构", "architectures"},

    // ---- local package set picker ----
    {"sources.title", "选择本地包集", "Select local package sets"},
    {"sources.dir", "产物目录", "artifact directory"},
    {"sources.hint", "[+/-] 标记或取消，[enter] 展开/标记，[g] 预览并继续", "[+/-] mark/unmark, [enter] expand/mark, [g] preview and continue"},
    {"sources.count", "已选 {}/{} 个包集", "{}/{} package set(s) selected"},
    {"sources.files", "{} 个 .dsc / {} 个 .deb", "{} .dsc / {} .deb"},
    {"sources.empty", "该目录没有可发布的 .deb / .dsc", "no publishable .deb / .dsc in this directory"},
    {"sources.scan_failed", "无法扫描 {}: {}", "cannot scan {}: {}"},
    {"sources.changes_set", "整个上传（含 .changes）", "whole upload (.changes)"},
    {"sources.changes_note", "目录含 .changes，只能整体发布", "directory contains .changes; publish as a whole"},
    {"sources.missing_dir", "产物目录不存在", "artifact directory does not exist"},
    {"sources.source_tag", "源码 {}", "source {}"},
    {"sources.binary_tag", "二进制 {}", "binary {}"},
    {"sources.only_source", "仅源码", "source only"},
    {"sources.only_binary", "仅二进制", "binary only"},
    {"sources.browse", "[d] 选择目录", "[d] choose directory"},
    {"sources.staged", "已暂存到 {}", "staged to {}"},
    {"sources.archs", "架构", "architectures"},
    {"sources.select_all", "[a] 全选", "[a] select all"},
    {"sources.invert", "[i] 反选", "[i] invert"},
    {"sources.group.update", "可更新", "upgradable"},
    {"sources.group.new", "新增", "new"},
    {"sources.group.downgrade", "降级", "downgrade"},
    {"sources.group.same", "已存在", "already present"},
    {"sources.status.update", "更新 {} → {}", "update {} → {}"},
    {"sources.status.new", "新增", "new"},
    {"sources.status.downgrade", "仓库版本更高: {}", "newer in repository: {}"},
    {"sources.status.same", "已存在 {}", "present {}"},
    {"sources.pending", "待发布: {} 个包集", "{} package set(s) marked"},
    {"sources.mark_hint", "[+/-] 标记 / 取消，[enter] 展开或标记，[g] 预览并继续", "[+/-] mark/unmark, [enter] expand or mark, [g] preview and continue"},

    // ---- directory browser ----
    {"browse.title", "选择产物目录", "Choose artifact directory"},
    {"browse.dir", "目录", "directory"},
    {"browse.parent", "..（上级目录）", ".. (parent)"},
    {"browse.empty", "没有子目录", "no subdirectories"},
    {"browse.current", "[空格] 使用当前目录", "[space] use this directory"},
    {"browse.artifacts", "{} 个产物", "{} artifact(s)"},
    {"browse.filter", "筛选: {}", "filter: {}"},
    {"browse.filter_hint", "输入字母跳转，退格删除，esc 清除筛选", "type letters to jump, backspace deletes, esc clears"},
    {"browse.no_match", "没有匹配的目录", "no matching directory"},

    // ---- detail overlay ----
    {"detail.repo_title", "仓库详情: {}", "Repository details: {}"},
    {"detail.set_title", "包集详情: {} {}", "Package set: {} {}"},
    {"detail.repo", "仓库", "repository"},
    {"detail.base", "仓库根目录", "base"},
    {"detail.conf", "distributions", "distributions"},
    {"detail.codename", "codename", "codename"},
    {"detail.component", "component", "component"},
    {"detail.archs", "架构", "architectures"},
    {"detail.components", "components", "components"},
    {"detail.suite", "Suite", "Suite"},
    {"detail.sign", "SignWith", "SignWith"},
    {"detail.label", "Label", "Label"},
    {"detail.origin", "Origin", "Origin"},
    {"detail.conf_error", "distributions 解析失败", "cannot parse distributions"},
    {"detail.dists", "dists 目录", "dists directory"},
    {"detail.inrelease", "InRelease", "InRelease"},
    {"detail.dist_dir", "索引目录", "index directory"},
    {"detail.present", "存在", "present"},
    {"detail.absent", "不存在", "missing"},
    {"detail.contents", "当前仓库内容（{} 项）", "current repository contents ({} entries)"},
    {"detail.empty", "仓库里还没有包", "repository is empty"},
    {"detail.state_error", "读取仓库状态失败: {}", "cannot read repository state: {}"},
    {"detail.source", "源码包", "source package"},
    {"detail.version", "版本", "version"},
    {"detail.dir", "产物目录", "artifact directory"},
    {"detail.scene", "类型", "kind"},
    {"detail.set_files", "文件（{} 个）", "files ({})"},

    // ---- repository maintenance ----
    {"maint.title", "仓库维护", "Repository maintenance"},
    {"maint.hint", "[-] 标记删除   [+] 取消   [enter] 展开/标记   [g] 预览并执行", "[-] mark for deletion   [+] unmark   [enter] expand/mark   [g] preview and run"},
    {"maint.target", "目标", "target"},
    {"maint.count", "已选 {}/{} 项", "{}/{} selected"},
    {"maint.empty", "仓库里没有包", "repository is empty"},
    {"maint.source_label", "源码", "source"},
    {"maint.binary_label", "二进制", "binary"},
    {"maint.source_group", "源码包", "source packages"},
    {"maint.binary_group", "二进制包", "binary packages"},
    {"maint.pending", "待删除: {} 项", "{} marked for deletion"},
    {"maint.mark_hint", "[-] 标记删除，[+] 取消，[enter] 展开/标记，[g] 预览并执行", "[-] mark for deletion, [+] unmark, [enter] expand/mark, [g] preview and run"},
    {"maint.no_selection", "没有选中任何包", "no package selected"},
    {"maint.delete", "删除包", "delete packages"},
    {"maint.export", "重新导出", "re-export"},
    {"maint.clean", "清理未引用文件", "clean unreferenced files"},
    {"maint.delete_hint", "输入 codename「{}」后回车确认删除", "type codename \"{}\" and press enter to delete"},
    {"maint.clean_hint", "输入 codename「{}」后回车确认清理", "type codename \"{}\" and press enter to clean"},
    {"maint.export_hint", "按 y 或回车重新生成索引并签名，esc 取消", "press y or enter to regenerate indices and sign, esc cancels"},
    {"maint.result_title", "维护结果: {}", "maintenance result: {}"},
    {"maint.done", "维护完成", "maintenance finished"},
    {"maint.failed", "维护过程中有错误，请检查上面的输出", "maintenance reported errors; see the output above"},
    {"maint.will_run", "将执行以下命令", "the following commands will run"},
    {"maint.wrong_codename", "codename 不匹配，请重新输入", "codename does not match; type it again"},
    {"maint.source_note", "注意：删除源码包会同时删除其二进制包，且 removesrc 不能限定 component", "note: removing a source also removes its binaries, and removesrc cannot be restricted to a component"},
    {"browse.error", "无法读取 {}: {}", "cannot read {}: {}"},
    {"browse.chosen", "已切换目录（按 ctrl+s 可写入配置）", "directory changed (ctrl+s saves it)"},

    // ---- staging snapshot ----
    {"stage.nothing", "没有选中任何文件", "nothing selected"},
    {"stage.copy_failed", "暂存失败 {} -> {}: {}", "staging failed {} -> {}: {}"},
    {"stage.mkdir_failed", "无法创建暂存目录 {}: {}", "cannot create staging directory {}: {}"},
    {"stage.missing", "暂存前源文件已消失: {}", "source file disappeared before staging: {}"},
    {"stage.size_mismatch", "暂存副本大小不一致: {}", "staged copy size mismatch: {}"},

    // ---- scan / checks ----
    {"scan.title", "产物扫描", "Artifact scan"},
    {"scan.files", "{} 个 .deb / {} 个 .dsc", "{} .deb / {} .dsc"},
    {"scan.scene.both", "`.deb` 与 `.dsc` 同时存在", "both .deb and .dsc present"},
    {"scan.scene.binary", "只有 .deb，没有 .dsc", "only .deb, no .dsc"},
    {"scan.scene.source", "只有 .dsc，没有 .deb", "only .dsc, no .deb"},
    {"scan.scene.empty", "目录里没有 .deb / .dsc", "no .deb / .dsc in the directory"},
    {"scan.multi_dsc", "目录里有 {} 份 .dsc，只能发布一个源码版本", "{} .dsc files in one directory; only one source version may be published"},
    {"scan.source_version", "源码版本 {}", "source version {}"},
    {"scan.no_source_version", "源码缺 Version 字段", "source has no Version field"},

    {"check.deb.declared_missing", "{} 在 .dsc 的 Binary 里，但没有对应的 .deb", "{} is listed in the .dsc Binary field but no matching .deb is present"},
    {"check.deb.no_priority", "{} 缺 Priority → reprepro 会 'No priority given, skipping'", "{} has no Priority -> reprepro prints 'No priority given, skipping'"},
    {"check.dsc.no_priority", ".dsc 缺 Priority（原生源码包常见；reprepro 可正常处理）", ".dsc has no Priority (normal for native source packages; reprepro handles it)"},
    {"check.dsc.no_version", ".dsc 缺 Version 字段", ".dsc has no Version field"},
    {"check.dsc.no_source", ".dsc 缺 Source 字段", ".dsc has no Source field"},
    {"check.dsc.no_files", ".dsc 缺 Files 字段", ".dsc has no Files field"},
    {"check.file.missing", "缺文件: {}", "missing file: {}"},
    {"check.file.size", "{} 大小不符：声明 {}，实际 {}", "{} size mismatch: declared {}, actual {}"},
    {"check.file.md5", "{} md5 不符", "{} md5 mismatch"},
    {"check.file.sha256", "{} sha256 不符", "{} sha256 mismatch"},
    {"check.file.ok", "{} 校验通过", "{} checksum ok"},
    {"check.file.unlisted", "{} 在目录里但没进 .dsc，reprepro 收不到", "{} exists but is not listed in .dsc; reprepro cannot see it"},
    {"check.asc.missing", "{} 已进 .dsc，但磁盘上缺少 .asc 签名", "{} is listed in .dsc but its .asc signature is missing"},
    {"check.deb.version", "{} 版本 {} ≠ 源码版本 {}", "{} version {} != source version {}"},
    {"check.deb.source", "{} 的 Source 是 {}，与源码包 {} 不一致", "{} Source is {}, expected {}"},
    {"check.deb.binary", "dsc 的 Binary 列表里没有 {}", "dsc Binary list does not include {}"},
    {"check.deb.arch", "{} 的架构 {} 不在目标集合内", "{} architecture {} is not in the target set"},
    {"check.deb.all", "{} 是 Architecture: all，写入全部架构索引", "{} is Architecture: all, written to every architecture index"},
    {"check.arch.missing", "缺少架构 {} 的 deb", "missing a .deb for architecture {}"},
    {"check.arch.extra", "多出架构 {}（不在目标列表内）", "unexpected architecture {} (not in target list)"},
    {"check.buildinfo.missing", "{} 缺少该架构的 .buildinfo（reprepro 不要求，仅提示）", "{} has no per-architecture .buildinfo (not required by reprepro)"},
    {"check.binary.version_drift", "仅二进制发布，但 {} 与 {} 版本不一致", "binary-only release but {} and {} have different versions"},
    {"check.binary.source_drift", "仅二进制发布，但 {} 的 Source 是 {}", "binary-only release but {} has Source {}"},
    {"check.exists", "仓库里已有 {} {} ({})", "{} {} ({}) already exists in the repository"},
    {"check.orphan", "{} 没有被任何 .dsc/.deb 引用", "{} is not referenced by any .dsc/.deb"},

    // ---- preflight ----
    {"pf.conf_missing", "reprepro 配置不存在: {}", "reprepro config not found: {}"},
    {"pf.conf_bad", "reprepro 配置解析失败: {}", "cannot parse reprepro config: {}"},
    {"pf.codename_ok", "codename {} 存在", "codename {} exists"},
    {"pf.codename_missing", "codename {} 不在 {}", "codename {} is not in {}"},
    {"pf.component_ok", "组件 {} 可用", "component {} is available"},
    {"pf.component_missing", "组件 {} 不在 {} 的 Components 里（可选: {}）", "component {} is not in Components of {} (available: {})"},
    {"pf.source_arch_missing", "{} 的 Architectures 缺 source，.dsc 会被静默跳过", "{} does not list 'source' in Architectures; .dsc will be silently skipped"},
    {"pf.arch_missing", "{} 的 Architectures 不含 {}", "{} does not enable architecture {}"},
    {"pf.arch_ok", "架构 {} 已启用", "architecture {} is enabled"},
    {"pf.sign_missing", "找不到签名密钥 {} 的私钥", "no secret key for {}"},
    {"pf.sign_none", "conf 没有 SignWith，InRelease 将不会签名", "no SignWith in conf; InRelease will not be signed"},
    {"pf.arch_none", "conf/distributions 未声明 Architectures，跳过架构检查", "conf/distributions declares no Architectures; architecture checks skipped"},
    {"pf.sign_ok", "签名密钥 {} 可用", "signing key {} is available"},
    {"pf.not_writable", "{} 当前用户不可写", "{} is not writable by the current user"},
    {"pf.locked", "仓库数据库被占用: {}", "repository database is locked: {}"},
    {"pf.lock_ok", "仓库锁可用", "repository lock is free"},
    {"pf.space_low", "剩余空间 {} MB，待写入约 {} MB", "free space {} MB, about to write {} MB"},
    {"pf.options_missing", "没有 conf/options，按默认布局处理", "no conf/options; assuming the default layout"},
    {"pf.options_mismatch", "conf/options 的 {} = {}，与 base 不一致", "conf/options {} = {}, differs from base"},
    {"pf.dist_mismatch", ".dsc 的 Distribution {} ≠ 目标 {}", ".dsc Distribution {} != target {}"},
    {"pf.warn_only", "仅警告，不阻塞", "warning only"},

    // ---- diff / change tree ----
    {"diff.title", "发布变更预览", "Publish preview"},
    {"diff.added", "新增", "added"},
    {"diff.modified", "修改", "modified"},
    {"diff.deleted", "删除", "deleted"},
    {"diff.unchanged", "未变更", "unchanged"},
    {"diff.fold", "… 未更改 {} 项   [u] 展开", "... {} unchanged   [u] expand"},
    {"diff.no_changes", "该版本已在仓库里，本次发布不会产生任何变更", "Already in the repository; this publish changes nothing"},
    {"diff.summary", "{} 版本 {} · 变更 {} / 共 {} 项", "{} version {} · {} changed / {} total"},
    {"diff.pkg_stats", "变更 {} / 共 {} 项", "{} changed / {} total"},
    {"diff.index.new", "[index] {} {} 新增", "[index] {} {} new"},
    {"diff.index.same", "[index] {} {} 已存在", "[index] {} {} exists"},
    {"diff.legend", "+ 新增   * 修改   · 未变更", "+ added   * modified   · unchanged"},
    {"diff.target", "目标: {}/{}/{}", "target: {}/{}/{}"},
    {"diff.counts", "+{}  *{}  ·{}", "+{}  *{}  ·{}"},
    {"diff.collapsed", "({} 项已折叠)", "({} folded)"},

    // ---- confirm ----
    {"confirm.title", "确认发布", "Confirm publish"},
    {"confirm.binary_only", "只有二进制包：源码不会进仓库，apt source 拿不到该版本。", "Binary-only: source will not be published; apt source cannot fetch this version."},
    {"confirm.source_only", "只有源码包：用户 apt install 仍会拿到旧版本。", "Source-only: apt install will still get the old version."},
    {"confirm.replace", "{} 个文件将被覆盖（内部先 remove 再 include）。", "{} file(s) will be overwritten (remove, then include)."},
    {"confirm.type_codename", "输入 codename「{}」后回车", "type codename \"{}\" and press enter"},
    {"confirm.publish_hint", "按 y 或回车发布，Esc 取消", "press y or enter to publish, esc to cancel"},
    {"confirm.ack_hint", "按 y 或回车确认上述提醒，Esc 取消", "press y or enter to acknowledge, esc to cancel"},
    {"confirm.binary_label", "仅二进制", "binary-only"},
    {"confirm.source_label", "仅源码", "source-only"},
    {"confirm.acked", "已确认：本次为{}发布", "acknowledged: {} release"},
    {"confirm.blocked", "存在阻塞问题，不能发布", "blocking issues, cannot publish"},

    // ---- execute ----
    {"exec.title", "执行 reprepro", "Run reprepro"},
    {"exec.running", "正在执行 {}/{}", "running {}/{}"},
    {"exec.done", "执行完成，退出码 {}", "finished with exit code {}"},
    {"exec.failed", "命令失败: {}", "command failed: {}"},
    {"exec.skipped", "已跳过: {}", "skipped: {}"},
    {"exec.noop", "没有需要执行的命令", "nothing to run"},
    {"exec.confirm", "按 y 开始执行，Esc 返回", "press y to run, esc to go back"},
    {"exec.log", "输出", "output"},
    {"exec.hint", "[enter] 查看核对结果，[r] 重扫", "[enter] verification, [r] rescan"},

    // ---- verify ----
    {"verify.title", "发布后核对", "Post-publish verification"},
    {"verify.present", "{} {} ({}) 已在仓库中", "{} {} ({}) is in the repository"},
    {"verify.missing", "{} {} ({}) 不在仓库中", "{} {} ({}) is missing from the repository"},
    {"verify.inrelease_missing", "没有 dists/{}/InRelease", "dists/{}/InRelease is missing"},
    {"verify.inrelease_ok", "InRelease 验签通过", "InRelease signature is valid"},
    {"verify.inrelease_bad", "InRelease 验签失败: {}", "InRelease signature failed: {}"},
    {"verify.all_ok", "发布成功，核对通过", "published successfully"},
    {"verify.failed", "发布后核对未通过", "post-publish verification failed"},
    {"verify.skip", "未执行发布，跳过核对", "nothing was published; verification skipped"},

    // ---- settings ----
    {"settings.title", "设置", "Settings"},
    {"settings.tab.repo", "仓库", "repositories"},
    {"settings.tab.paths", "路径", "paths"},
    {"settings.tab.policy", "策略", "policy"},
    {"settings.tab.ui", "界面", "interface"},
    {"settings.need_name", "仓库必须有名字", "repository needs a name"},
    {"settings.last_repo", "至少保留一个仓库", "keep at least one repository"},
    {"settings.deleted", "已删除仓库 {}", "removed repository {}"},
    {"settings.added", "已新增仓库", "repository added"},
    {"settings.saved", "已保存", "saved"},
    {"settings.tabs_hint", "[tab] 分组  [ctrl+s] 保存  [a] 新增仓库  [d] 删除仓库  [esc] 返回", "[tab] group  [ctrl+s] save  [a] add repo  [d] delete repo  [esc] back"},
    {"help.title", "快捷键", "Keyboard shortcuts"},
    {"help.current", "当前页面", "This screen"},
    {"help.global", "全局", "Global"},
    {"help.hint", "[↑/↓] 滚动   [esc/?] 关闭", "[up/down] scroll   [esc/?] close"},
    {"help.scroll_pos", "位置 {}/{}", "position {}/{}"},
    {"quit.title", "退出前要做什么？", "Before quitting"},
    {"quit.yes", "是，直接退出", "Yes, quit"},
    {"quit.no", "否，继续使用", "No, stay"},
    {"quit.save", "保存状态缓存并退出", "Save state cache and quit"},
    {"quit.hint", "[←/→] 选择   [enter] 确认   [y/n/s] 直接执行   [esc] 取消", "[left/right] choose   [enter] confirm   [y/n/s] act immediately   [esc] cancel"},
    {"quit.saved", "状态已保存到 {}", "State saved to {}"},
    {"quit.save_failed", "保存状态失败：{}", "Failed to save state: {}"},
    {"err.state_path", "无法确定状态缓存路径", "cannot determine the state cache path"},
    {"help.move", "移动 / 选择", "move / select"},
    {"help.scroll", "滚动", "scroll"},
    {"help.focus", "移动焦点", "move focus"},
    {"help.enter", "继续 / 确认", "next / confirm"},
    {"help.back", "返回 / 取消", "back / cancel"},
    {"help.fold", "折叠 / 展开", "collapse / expand"},
    {"help.unchanged", "显示 / 隐藏未变更项", "show/hide unchanged entries"},
    {"help.copy", "复制 reprepro 命令", "copy reprepro commands"},
    {"help.rescan", "重新扫描", "rescan"},
    {"help.settings", "打开设置", "open settings"},
    {"help.repo", "切换仓库", "change repository"},
    {"help.lang", "中英文切换", "toggle language"},
    {"help.quit", "退出（先确认）", "quit (with confirmation)"},
    {"help.save", "保存配置", "save configuration"},
    {"help.tab", "切换标签页", "switch tab"},
    {"help.addrepo", "新增仓库", "add repository"},
    {"help.delrepo", "删除仓库", "delete repository"},
    {"help.prevrepo", "上一个 / 下一个仓库", "previous/next repository"},
    {"help.option", "切换选项 / 勾选", "cycle option / toggle"},
    {"help.publish", "发布", "publish"},
    {"help.ack", "确认当前提示", "acknowledge the warning"},
    {"help.type", "输入 codename 后回车", "type the codename, then Enter"},
    {"help.help", "打开 / 关闭帮助", "open/close help"},
    {"help.selectall", "全选 / 反选", "select all / invert"},
    {"help.pickdir", "选择产物目录", "choose artifact directory"},
    {"help.usecur", "使用当前目录", "use this directory"},
    {"help.parent", "上级目录", "parent directory"},
    {"help.open", "进入目录", "open directory"},
    {"help.stage", "暂存快照并继续", "stage snapshot and continue"},
    {"help.reselect", "重新选择本地包集", "re-select local package sets"},
    {"help.details", "查看详情", "show details"},
    {"help.typing", "输入名字跳转", "type a name prefix to jump"},
    {"help.delete", "删除选中的包", "delete selected packages"},
    {"help.export", "重新导出索引并签名", "re-export indices and sign"},
    {"help.clean", "清理未引用文件", "clean unreferenced files"},
    {"help.home", "返回主页", "back to home"},
    {"help.open_menu", "打开所选功能", "open the selected entry"},
    {"help.mark", "标记 / 取消标记", "mark / unmark"},
    {"help.expand", "展开 / 折叠", "expand / collapse"},
    {"help.go", "预览并继续", "preview and continue"},
    {"help.run", "预览并执行", "preview and execute"},
    {"help.done", "完成，返回主页", "done, back to home"},

    // ---- CLI / errors ----
    {"cli.usage", "用法", "usage"},
    {"cli.help.1", "  rpp-release-tool [选项] [产物目录]", "  rpp-release-tool [options] [artifacts-dir]"},
    {"cli.help.2", "  --config <path>      使用指定的 TOML 配置", "  --config <path>      use this TOML config"},
    {"cli.help.3", "  --repo <name>        预选仓库", "  --repo <name>        preselect repository"},
    {"cli.help.4", "  --codename <name>    预选 codename", "  --codename <name>    preselect codename"},
    {"cli.help.5", "  --component <name>   预选 component", "  --component <name>   preselect component"},
    {"cli.help.6", "  --no-tui             纯文本输出，适合 CI", "  --no-tui             plain text, CI friendly"},
    {"cli.help.7", "  --check-only         只检查，不执行 reprepro", "  --check-only         check only, do not publish"},
    {"cli.help.8", "  --detail <level>     summary|normal|full", "  --detail <level>     summary|normal|full"},
    {"cli.help.9", "  --lang <code>        zh-CN|en-US", "  --lang <code>        zh-CN|en-US"},
    {"cli.help.10", "  --show-config        打印生效配置后退出", "  --show-config        print effective config and exit"},
    {"cli.help.11", "  --print-cmd          打印 reprepro 命令后退出", "  --print-cmd          print reprepro commands and exit"},
    {"cli.help.12", "  --ack                非交互下确认 warning", "  --ack                acknowledge warnings non-interactively"},
    {"cli.help.13", "  --version            版本号", "  --version            version"},
    {"cli.help.14", "退出码: 0 成功, 1 阻塞, 2 需确认, 3 配置错误, 4 用法, 5 执行失败, 6 核对失败", "exit: 0 ok, 1 blocked, 2 needs ack, 3 config, 4 usage, 5 exec, 6 verify"},
    {"cli.help.15", "  --replace            允许覆盖同版本（remove + include）", "  --replace            allow replacing an existing version (remove + include)"},
    {"cli.replace_hint", "同版本已存在，非交互发布需显式加 --replace（并配合 --ack）。", "The same version already exists; non-interactive publishing requires --replace (with --ack)."},

    {"err.bad_value", "{} 的值不合法: {}", "invalid value for {}: {}"},
    {"err.cannot_read", "读不了 {}: {}", "cannot read {}: {}"},
    {"err.cannot_write", "写不了 {}: {}", "cannot write {}: {}"},
    {"err.no_config", "没有可用的配置，且当前不是交互终端", "no usable config and not an interactive terminal"},
};

constexpr std::size_t kCount = sizeof(kTable) / sizeof(kTable[0]);

Lang g_lang = Lang::ZhCN;

const Ent* find_ent(std::string_view key) {
  for (std::size_t i = 0; i < kCount; ++i) {
    if (key == kTable[i].key) return &kTable[i];
  }
  return nullptr;
}

}  // namespace

Lang detect_lang() {
  for (const char* var : {"LANGUAGE", "LC_ALL", "LC_MESSAGES", "LANG"}) {
    const char* v = std::getenv(var);
    if (!v || !*v) continue;
    if (std::strncmp(v, "zh", 2) == 0) return Lang::ZhCN;
    if (std::strncmp(v, "en", 2) == 0) return Lang::EnUS;
  }
  return Lang::EnUS;
}

Lang parse_lang(const std::string& s, bool* ok) {
  std::string v;
  for (char c : s) v.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  bool good = true;
  Lang l = Lang::ZhCN;
  if (v == "zh" || v == "zh-cn" || v == "zh_cn" || v == "cn" || v == "chinese") {
    l = Lang::ZhCN;
  } else if (v == "en" || v == "en-us" || v == "en_us" || v == "english") {
    l = Lang::EnUS;
  } else if (v == "auto") {
    l = detect_lang();
  } else {
    good = false;
    l = detect_lang();
  }
  if (ok) *ok = good;
  return l;
}

std::string lang_code(Lang l) { return l == Lang::ZhCN ? "zh-CN" : "en-US"; }
std::string lang_label(Lang l) { return l == Lang::ZhCN ? "中文 (zh-CN)" : "English (en-US)"; }

void set_lang(Lang l) { g_lang = l; }
Lang current_lang() { return g_lang; }

const char* trc(std::string_view key) {
  const Ent* e = find_ent(key);
  if (!e) return nullptr;
  return g_lang == Lang::ZhCN ? e->zh : e->en;
}

std::string tr(std::string_view key) {
  const char* v = trc(key);
  return v ? std::string(v) : std::string(key);
}

}  // namespace rpp
