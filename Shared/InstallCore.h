#pragma once
// InstallCore — 安装核心（对应 ProgramReg 的 InstallHelper.cs）：
// 解压负载 → 生成 uninstall.ini → 注册表卸载项 → 快捷方式 → 任务栏固定

#include <QString>
#include <QStringList>
#include <functional>

namespace esp {

struct InstallOptions
{
    QString appName;
    QString version = QStringLiteral("1.0.0");
    QString publisher;
    QString installDir;
    QString mainExeName;     // 主程序 exe 文件名（打包器封包时写入元数据）
    QString appId;           // 注册表卸载子键名
    bool startMenuShortcut = true;
    bool desktopShortcut = false;
    bool taskbarShortcut = false;
};

// 执行安装。zipData 为负载 ZIP。log 回调逐条报告；progress 回调报百分比（0-100）。返回失败数。
int runInstall(const QByteArray &zipData, const InstallOptions &o,
               const std::function<void(const QString &)> &log,
               const std::function<void(int percent)> &progress = nullptr,
               QStringList *warningsOut = nullptr);

// 从安装目录读取 uninstall.ini 获取主程序 exe 名（供完成页"立即运行"使用）
QString installedMainExe(const QString &installDir);

} // namespace esp
