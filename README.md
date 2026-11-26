# EFNoLauncher — DW-Proton 直接启动终末地

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

国服 1.6.x 起，`OneChannelLC.dll` 会检查 **直接父进程**：

1. 文件名必须是 `Games.exe`
2. 映像需带鹰角 / Gryphline 的有效 Authenticode 签名

直接双击或让 Steam/Lutris 拉起 `Endfield.exe` 会得到「请通过官方启动器打开游戏」（errorCode `10100` / `10101`）。  
本仓库用与 [issue #21](https://github.com/misaka10843/Hi3Helper.Plugin.Hypergryph/issues/21) / `official_launcher.cpp` 相同的做法：挂起一份改名为 `Games.exe` 的已签名副本，再通过 `PROC_THREAD_ATTRIBUTE_PARENT_PROCESS` 启动游戏。

## 构建

需要 `mingw-w64-gcc`：

```bash
cd /path/to/EFNoLauncher
make
```

得到 `endfield_bridge.exe`（不入库，需本地 `make`；可在 Windows 或 DW-Proton / Wine 下运行）。

## DW-Proton + Steam

1. 用官方安装包装好鹰角启动器与游戏（prefix 里要有 `Endfield.exe`）。
2. 非 Steam 游戏目标可指向 `Launcher.exe` 或 `Endfield.exe`。
3. **兼容性工具**选 DW-Proton。
4. **启动选项**：

```text
/绝对路径/EFNoLauncher/launch-endfield.sh %command%
```

若自动找不到游戏，可显式指定：

```text
ENDFIELD_GAME="/home/你/Games/endfield/drive_c/Program Files/Hypergryph Launcher/games/…/Endfield.exe" /绝对路径/EFNoLauncher/launch-endfield.sh %command%
```

干跑（只打印改写后的命令）：

```bash
ENDFIELD_DRY_RUN=1 ./launch-endfield.sh /path/to/proton waitforexitandrun 'Z:\...\Endfield.exe'
```

日志：`~/.local/state/endfield-launch/launch.log`

## Lutris（推荐）

不需要 `launch-endfield.sh`。把桥接 exe 放进游戏目录，让 Lutris 直接跑它即可。

1. 复制桥接到游戏目录（与 `Endfield.exe` 同层）：

```bash
cp endfield_bridge.exe "/home/你/games/Endfield Game/"
```

2. 右键游戏 → **配置** → **游戏选项**：
   - **可执行文件**：`…/Endfield Game/endfield_bridge.exe`
   - **参数**（可选）：`--hide-console --no-popup`
   - **Wine 前缀**：保持你原来的 prefix（例如 `~/Documents/PFX/ENDFIELD`）
3. **运行环境选项** → Wine 版本：`DW-Proton Latest`（或你的 dwproton）
4. 保存后点 Play。

同目录时桥接会自动找：

`Games.exe` → `PlatformProcess.exe` → `QtWebEngineProcess.exe` → `UnityCrashHandler64.exe` → `Endfield.exe`

并复制到 `%TEMP%\EndfieldLaunchBridge\<pid>\Games.exe` 后挂起。

更新游戏时把可执行文件临时改回官方 `Launcher.exe` / `Endfield.exe`，更新完再改回 bridge。

## `endfield_bridge.exe` 参数

| 参数 | 说明 |
| --- | --- |
| `--game <Endfield.exe>` | 游戏路径（不在同目录时必填） |
| `--parent <signed.exe>` | 强制指定父进程源文件 |
| `--hide-console` | 脱离控制台（Steam/脚本用） |
| `--no-popup` | 出错不弹窗 |
| `--no-default-args` | 不附加官方默认 `-launcher_lang=zh-cn -launcher_sub_channel=1` |
| `--` | 之后参数原样传给游戏 |

## 说明

- **不修改**游戏安装目录里的 exe/dll；临时 `Games.exe` 在退出后删除。
- 更新仍走官方启动器；本工具只负责「开始游戏」那一步。
- 绕过官方启动器 UI 不是官方支持行为，请自行评估风险。
- DW-Proton 负责 ACE 等 Wine 侧兼容；本脚本只解决父进程启动校验。
