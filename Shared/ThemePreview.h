#pragma once
// ThemePreview — 主题效果预览：把窗口外观按虚拟坐标 960x560 画出来
//
// 打包器界面里的实时预览与命令行 /export-preview 导出的效果图共用这一份实现，
// 绘制参数（背景图、侧栏半透明、按钮渐变）与运行时窗口保持一致，做到 WYSIWYG。

#include "ThemeSpec.h"

#include <QPixmap>
#include <QString>
#include <QStringList>

class QPainter;
class QRect;

namespace esp {

struct MockSpec
{
    QString appName;
    QString title;             // 标题栏文字
    QString subtitle;          // 侧栏副标题（安装向导 / 卸载向导）
    QStringList steps;         // 侧栏步骤（空则按安装/卸载默认步数占位）
    int activeStep = 0;
    bool uninstaller = false;  // 主按钮用危险色（卸载）
    bool showText = true;      // 缩略图过小时可关闭文字
    QString primaryButton;     // 主按钮文字（安装 / 卸载）
    QString backButton;        // 次按钮文字（上一步）
    QString cancelButton;      // 左按钮文字（取消）
};

// 在 rect 内绘制窗口外观模型（始终按内部虚拟坐标绘制并等比缩放居中）
void paintMock(QPainter *p, const QRect &rect, const ThemeStyle &style,
               const MockSpec &spec, const QImage &bgImage);

// 渲染为 QPixmap（内部按 size 有界解码背景图）
QPixmap renderMock(const QSize &size, const ThemeStyle &style, const MockSpec &spec);

} // namespace esp
