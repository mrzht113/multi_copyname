# multi_copyname

Windows 原生多文件名复制工具。选中一个或多个文件 → 右键 → **multi_copyname · 复制名称** → 复制 → 在 Excel 中按 **Ctrl+V**，每个名称占一行。

## 下载与安装

从 [Releases](https://github.com/mrzht113/multi_copyname/releases) 下载 `multi_copyname-v1.0.0-windows-x64.zip`，**先完整解压**。

1. 保持 `multi_copyname.exe` 与 `multi_copyname_shell.dll` 在同一目录。
2. 双击 `multi_copyname.exe`，点击 **安装右键菜单**。无需管理员权限。
3. 在资源管理器中多选文件后右键。Windows 11 先选择 **显示更多选项**（Shift+F10）。
4. 选择 **复制文件名（每行一个，含扩展名）**，在 Excel 中选中起始单元格并 Ctrl+V。不要双击单元格进入编辑状态。

安装将程序复制至 `%LOCALAPPDATA%\Programs\multi_copyname\1.0.0`，只写当前用户注册表。安装完成后，原解压文件夹可以移动或删除。

## 功能

| 菜单 | 示例结果 |
| --- | --- |
| 复制文件名 | `PCB V1.2.xlsx` |
| 复制文件名，不含扩展名 | `PCB V1.2` |
| 复制完整路径 | `D:\项目\PCB V1.2.xlsx` |

- 多选数据一次读取并写入剪贴板，不会被逐个文件的进程互相覆盖。
- Unicode 文本、Windows CRLF 换行，无多余空行、引号或表头。
- 同时提供 Excel 可读取的 HTML 单列表格，单元格按文本格式输出；普通文本程序收到原始名称。
- 不含扩展名只移除最后一个后缀；`.gitignore` 保留，文件夹名称中的点保留。
- 文件夹也支持；只复制选中的文件夹名称，不递归其内容。
- 按资源管理器提供的顺序输出，不保证等于鼠标点击选择顺序，不额外排序或去重。
- 直接打开程序可多选添加文件或拖入文件 / 文件夹，再点击复制按钮。界面列表按添加顺序去重。
- 无常驻进程，无网络连接，无 Qt / .NET 或额外运行库要求。

## 卸载

打开安装目录内的 `multi_copyname.exe`，点击 **卸载右键菜单**。菜单立即取消注册；资源管理器缓存可能需要重新打开窗口或注销登录刷新。

安装文件保留，注销后可删除 `%LOCALAPPDATA%\Programs\multi_copyname`。程序不会强制结束资源管理器或删除使用中的 DLL。

静默操作（退出代码 0 表示成功，1 表示失败）：

```bat
multi_copyname.exe /install
multi_copyname.exe /uninstall
```

## 兼容性与限制

- 面向 Windows 10 / 11 **x64**；不提供 x86 或 ARM64 资源管理器扩展。
- Windows 11 经典右键菜单扩展位于“显示更多选项”，不修改系统菜单样式。
- Excel 正常粘贴应分为一列多行；“仅粘贴文本”由 Excel 自己判断单元格格式。若名称包含前导零或 `= + - @` 等开头，请正常粘贴，或先将目标列设为文本并确认结果。
- DLL 与应用没有数字签名。企业策略禁用第三方 Shell 扩展时，仍可使用独立界面复制。
- 从同一个版本目录覆盖已被资源管理器加载的不同 DLL 可能失败；先卸载菜单并注销，再安装。

## 构建

安装 Visual Studio 2022 / Build Tools 的“使用 C++ 的桌面开发”及 Windows SDK：

```powershell
cmake -S . -B build -A x64 -DBUILD_TESTING=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

输出：`build/Release/multi_copyname.exe` 和 `multi_copyname_shell.dll`。MSVC 使用静态 CRT。

测试覆盖名称提取、中文、多后缀、文件夹、10,000 项换行、HTML 字节偏移，以及 Windows DLL 的 COM 生命周期、整组选中项与真实剪贴板交互。CI 另验证当前用户安装与卸载。

GitHub Actions 在 push 时自动构建测试；推送 `v*` 标签，或手动运行工作流并勾选 `publish`，生成 ZIP 与 SHA256 并发布 Release。

## 实现

原生 C++17 / Win32；`IShellExtInit` + `IContextMenu` 从 `CF_HDROP` 接收全部选中项，向剪贴板写入 `CF_UNICODETEXT` 与 `HTML Format`。

注册位置：

```text
HKCU\Software\Classes\CLSID\{E8C82EC2-9808-42C9-9929-A8FF52BD76C1}
HKCU\Software\Classes\AllFilesystemObjects\shellex\ContextMenuHandlers\multi_copyname
```
