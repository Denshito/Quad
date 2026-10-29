# Quad 团队 Git / Git LFS 协作规范

> 适用仓库：`https://github.com/Denshito/Quad`  
> 适用规模：2～4 人的小型 Unreal Engine 项目  
> 当前引擎版本：Unreal Engine 5.4  
> 最后更新：2026-10-30

## 1. 目标与基本原则

本项目使用：

- Git 管理代码、配置、提交历史和分支。
- Git LFS 管理 `.uasset`、`.umap` 等大型二进制文件。
- GitHub 私有仓库保存远程版本。
- `main` 只保存已经确认可以共享的版本。
- 每项工作使用独立分支，通过 Pull Request 合并到 `main`。

UE 的蓝图、地图、材质、Control Rig 等文件通常无法像文本代码一样自动合并。因此团队最重要的规则不是“发生冲突后解决”，而是**编辑前确认文件归属，避免两个人同时修改同一个二进制资产**。

## 2. 必须遵守的六条规则

1. 开始工作前先同步 `main` 和 Git LFS 文件。
2. 不直接向 `main` 开发，始终创建任务分支。
3. 修改共享地图、蓝图、动画蓝图或 Control Rig 前，先取得文件锁并通知队友。
4. 切换分支、Pull 或合并前关闭 Unreal Editor，并保证工作区干净。
5. 不提交 `Binaries`、`DerivedDataCache`、`Intermediate`、`Saved`、`.vs` 和 `.sln`。
6. 禁止对 `main` 使用强制推送，禁止在没有备份时执行 `git reset --hard`。

## 3. 仓库中包含与排除的内容

需要提交：

```text
Config/
Content/
Source/
Plugins/                 如果项目以后增加自建插件
Quad.uproject
.gitignore
.gitattributes
TEAM_GIT_WORKFLOW.md
```

不提交：

```text
.vs/
Binaries/
DerivedDataCache/
Intermediate/
Saved/
*.sln
```

当前 `.gitattributes` 已将以下类型交给 Git LFS：

```text
*.uasset
*.umap
*.fbx
*.psd
*.tga
*.exr
*.wav
```

不要删除或绕过 `.gitattributes`。如果直接把 `.uasset` 当普通 Git 文件提交，仓库体积会快速增长。

## 4. 新队员首次准备

### 4.1 安装软件

美术队员需要安装：

1. Git。
2. Git LFS。
3. Unreal Engine 5.4。
4. Visual Studio 2022 或 Build Tools，并安装 `Game development with C++`、MSVC 和 Windows SDK。

本项目是 C++ UE 工程，`Binaries` 不进入仓库。新电脑第一次打开工程时，需要从 `Source` 编译项目模块。仅安装 UE、不安装 C++ 编译环境，可能出现“缺少 Quad 模块”或“无法编译模块”的提示。

### 4.2 配置 Git 身份

每台电脑只需执行一次：

```powershell
git config --global user.name "自己的名字"
git config --global user.email "自己的 GitHub 邮箱"
git lfs install
```

### 4.3 克隆仓库

```powershell
git clone https://github.com/Denshito/Quad.git
cd Quad
git lfs pull
git status
```

正确结果应显示当前位于 `main`，并且没有未提交改动。

检查 LFS 是否正常：

```powershell
git lfs version
git lfs ls-files
git lfs fsck
```

如果资源文件打开后只有几行 `version https://git-lfs...` 文本，说明下载到的是 LFS 指针而不是真实资源，执行：

```powershell
git lfs install
git lfs pull
```

### 4.4 第一次打开 UE 工程

1. 关闭 Unreal Editor。
2. 右键 `Quad.uproject`，选择生成 Visual Studio 项目文件；如果菜单不可用，可先打开 UE 5.4 或运行对应引擎的项目文件生成工具。
3. 使用 Visual Studio 编译 `Development Editor | Win64`，或双击 `.uproject` 并允许 UE 编译模块。
4. 工程打开后不要立刻保存全部资产，先确认地图、蓝图和材质能够正常加载。

## 5. 分支命名

每个任务一个分支：

```text
art/<姓名>/<任务>
code/<姓名>/<任务>
design/<姓名>/<任务>
fix/<姓名>/<问题>
```

示例：

```text
art/lin/wall-material
art/lin/quad-blockout
code/zst/pawn-movement
fix/zst/input-mapping
```

名称使用小写英文和短横线，不使用空格和中文，避免命令行与工具兼容问题。

## 6. 每次开始新任务

先关闭 Unreal Editor，然后执行：

```powershell
git status
git switch main
git pull --ff-only origin main
git lfs pull
git switch -c art/<姓名>/<任务>
```

`git status` 必须干净。如果存在未提交文件，不要继续 Pull 或切换分支，应先提交到当前任务分支，或确认这些文件可以安全撤销。

不要把未保存的 UE 修改寄希望于 `git stash`。二进制资产暂存与恢复不便核对，优先使用小型 WIP 提交：

```powershell
git add <本次文件>
git commit -m "WIP: 保存当前美术进度"
```

## 7. 二进制资产锁定规范

### 7.1 必须先锁定的资产

修改下列已有共享资产前必须锁定：

- `.umap` 地图。
- 角色、Controller、GameMode 等公共蓝图。
- Animation Blueprint、Control Rig、Skeleton、Physics Asset。
- 公共材质母版和被多人引用的数据资产。
- 其他已经明确由某位成员占用的 `.uasset`。

锁定示例：

```powershell
git lfs lock "Content/Testing_Hall.umap"
git lfs lock "Content/Blueprints/Quadguy/BP_QuadGuy.uasset"
```

查看当前锁：

```powershell
git lfs locks
```

锁定后在群里说明：

```text
我开始修改 Content/Testing_Hall.umap，预计今天完成，完成合并后解锁。
```

### 7.2 新建资产

新文件在远程不存在时不需要提前锁定，但应放入自己的任务目录或提前说明命名，例如：

```text
Content/ArtRes/Environment/Lin/
```

新资产提交后如果会成为多人共同编辑的公共资产，后续修改按锁定流程执行。

### 7.3 解锁时机

不要在刚 Push 分支后立刻解锁。等待 Pull Request 合并到 `main`，确认远程版本正确后再解锁：

```powershell
git lfs unlock "Content/Testing_Hall.umap"
```

只有仓库管理员在确认原锁持有人不再修改该文件后，才可强制解锁：

```powershell
git lfs unlock --force "Content/Testing_Hall.umap"
```

## 8. 保存、提交与 Push 标准流程

### 8.1 在 Unreal Editor 中

1. 只保存本次真正修改的资产。
2. 运行必要的白盒检查或打开目标地图检查画面。
3. 修复重定向器时确认不会影响队友正在使用的资产。
4. 移动或重命名 UE 资产必须在 Content Browser 内完成，不要在 Windows 文件资源管理器中移动 `.uasset`。
5. 保存完成后关闭 Unreal Editor，尤其是修改了地图或准备切换分支时。

### 8.2 检查改动

```powershell
git status
git lfs status
```

确认没有这些目录：

```text
Binaries/
DerivedDataCache/
Intermediate/
Saved/
```

### 8.3 精确暂存文件

优先指定路径，不要习惯性使用 `git add .`：

```powershell
git add "Content/ArtRes/Materials/Walls_Debug/M_Wall.uasset"
git add "Content/ArtRes/Materials/Walls_Debug/Instances/MI_Wall.uasset"
```

检查待提交内容：

```powershell
git diff --cached --stat
git status
```

### 8.4 提交信息

格式：

```text
类型: 简短描述
```

推荐类型：

```text
art: 美术资产
feat: 新功能
fix: 修复问题
level: 关卡修改
refactor: 不改变行为的整理
docs: 文档
```

示例：

```powershell
git commit -m "art: 添加墙面白盒材质"
git commit -m "level: 调整测试关卡攀爬路线"
```

### 8.5 Push 当前分支

第一次 Push：

```powershell
git push -u origin art/<姓名>/<任务>
```

后续 Push：

```powershell
git push
```

Push 会先上传 Git LFS 对象，再上传 Git 提交。必须等命令完整结束并显示成功，不能看到普通 Git 提交上传完成就关闭窗口。

## 9. 创建 Pull Request

在 GitHub 仓库页面点击 `Compare & pull request`，设置：

```text
base: main
compare: 自己的任务分支
```

PR 描述至少包含：

```markdown
## 修改内容
- 修改了哪些资产
- 解决了什么任务

## 验证方式
- 打开了哪个地图
- 在 UE 5.4 中检查了什么

## 二进制资产
- 锁定了哪些文件
- 是否修改地图、蓝图、骨骼或 Control Rig

## 截图
- 美术和关卡修改附修改后截图
```

不要只写“更新了”“改好了”。二进制文件无法在 GitHub 网页中完整审查，文件清单、说明和截图就是主要审查证据。

## 10. 合并前同步 main

如果 PR 创建后 `main` 又发生变化，先在本地更新：

```powershell
git fetch origin
git switch main
git pull --ff-only origin main
git switch art/<姓名>/<任务>
git merge main
git lfs pull
```

无冲突时重新打开 UE，检查目标地图和资产，再提交并 Push 合并结果：

```powershell
git add <确认后的文件>
git commit -m "merge: 同步 main"
git push
```

共享分支不要使用强制 rebase/force push。小团队优先保留清楚、安全的合并过程。

## 11. PR 合并标准

满足以下条件才合并：

- 分支已同步最新 `main`。
- GitHub 显示无未解决冲突。
- LFS 文件已经完整上传。
- 修改人提供文件清单和截图。
- 至少另一名队员确认没有覆盖自己的资产。
- 重要地图或公共蓝图已在 UE 5.4 中打开检查。

小团队推荐使用 `Squash and merge`，让一个任务在 `main` 中保留为一个清晰提交。

合并完成后，修改人执行：

```powershell
git switch main
git pull --ff-only origin main
git lfs pull
git branch -d art/<姓名>/<任务>
```

然后删除 GitHub 上的远程任务分支，并解锁已完成的二进制资产。

## 12. 二进制文件冲突处理

### 12.1 基本判断

`.cpp`、`.h`、`.ini`、`.md` 等文本文件可以逐行合并。

`.uasset`、`.umap`、`.fbx`、`.psd` 等二进制文件通常不能可靠地自动合并。即使 Git 显示冲突，也不能用文本编辑器拼接两个版本。

### 12.2 推荐解决流程

如果两个人修改了同一个二进制资产：

1. 立即停止继续修改和 Push。
2. 不要盲目点击 `ours` 或 `theirs`。
3. 双方确认哪一个版本应作为最终基础。
4. 如果正在合并且还不确定，先执行：

```powershell
git merge --abort
```

5. 让最终版本的负责人先合并到 `main`。
6. 另一位成员同步新的 `main`，在 UE 中重新应用自己仍然需要的修改。
7. 重新保存、测试并提交。

这比强行合并两个 `.uasset` 更慢，但不会产生一个表面合并成功、实际已经损坏的 UE 资产。

### 12.3 最后手段

只有在双方已经明确选择保留哪一侧，并且已备份另一版本时，才使用：

```powershell
git checkout --ours -- "Content/路径/资产.uasset"
git checkout --theirs -- "Content/路径/资产.uasset"
git add "Content/路径/资产.uasset"
```

`ours` 和 `theirs` 的含义取决于当前合并方向，执行前必须运行 `git status` 并确认当前分支。不要把这组命令当作日常冲突解决方式。

## 13. Pull 与切换分支时的禁止事项

不要在以下状态执行 Pull、Merge 或切换分支：

- Unreal Editor 仍打开并有未保存资产。
- `git status` 显示未提交改动。
- 正在编译 Shader 或保存地图。
- LFS 上传或下载尚未结束。
- 不知道某个二进制冲突应该保留哪一侧。

禁止：

```text
git push --force origin main
git reset --hard              未确认备份时
直接删除 .git 重新开始
通过网盘压缩包覆盖整个 Content
在资源管理器中移动或重命名 UE 资产
两个人同时编辑同一张地图或公共蓝图
```

## 14. 常见 Git LFS 问题

### 14.1 克隆后资产缺失或无法打开

```powershell
git lfs install
git lfs pull
git lfs fsck
```

### 14.2 Push 提示缺少 LFS 对象

先检查：

```powershell
git lfs status
git lfs fsck
```

确认本地确实拥有对象后，再针对当前分支上传：

```powershell
git lfs push origin HEAD
git push
```

不要把 `git lfs push --all` 当作日常命令，它可能上传所有历史分支中的 LFS 对象。

### 14.3 LFS 配额不足

GitHub 的 LFS 存储与下载流量受账户方案限制。出现配额错误时：

1. 停止反复 Push 大型测试资产。
2. 检查是否提交了不需要的原始视频、重复贴图或中间导出文件。
3. 不要仅删除当前文件后认为历史空间会立即释放；旧 LFS 对象仍可能存在于仓库历史中。
4. 由仓库所有者检查 GitHub 的 LFS 使用情况和计费设置。

## 15. 每日最简工作清单

### 开始工作

```text
[ ] 关闭 UE
[ ] git status 干净
[ ] 更新 main
[ ] git lfs pull
[ ] 创建或切换任务分支
[ ] 锁定要修改的共享二进制资产
[ ] 在群里说明占用文件
```

### 结束工作

```text
[ ] UE 中保存并检查目标资产
[ ] 关闭 UE
[ ] 检查 git status 和 git lfs status
[ ] 只暂存本任务文件
[ ] 提交信息明确
[ ] Push 完整结束
[ ] 创建或更新 PR
[ ] 合并后同步 main、删除任务分支并解锁资产
```

## 16. 官方参考

- Git LFS：https://git-lfs.com/
- Git LFS 文件锁：https://github.com/git-lfs/git-lfs/blob/main/docs/man/git-lfs-lock.adoc
- GitHub 邀请协作者：https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/repository-access-and-collaboration/inviting-collaborators-to-a-personal-repository
- GitHub Pull Request 与保护规则：https://docs.github.com/en/pull-requests/reference/managing-and-standardizing-pull-requests

