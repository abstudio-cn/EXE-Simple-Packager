#pragma once
// ThemeEditor — 打包器中的「外观主题」编辑器：配色 + 背景图 + 实时预览
//
// 说明：
//  * 配色除了主色/副色，还可单独指定侧栏渐变的起止色；
//  * 背景图在后台线程读取并「有界」重编码（避免大图卡住界面），完成后回填预览；
//  * 预览使用与运行时窗口相同的绘制实现（ThemePreview），所见即所得。

#include "../Shared/ThemePreview.h"
#include "../Shared/ThemeSpec.h"

#include <QByteArray>
#include <QImage>
#include <QSize>
#include <QString>
#include <QThread>
#include <QWidget>

class QComboBox;
class QLabel;
class QPushButton;
class QSlider;

// 后台读取 / 重编码背景图（大图解码放在工作线程，UI 线程只做界面更新）
class BackgroundImageTask : public QThread
{
    Q_OBJECT
public:
    BackgroundImageTask(const QString &path, int maxWidth, QObject *parent = nullptr);

signals:
    void loaded(const QByteArray &encoded, const QSize &pixelSize);
    void failed(const QString &error);

protected:
    void run() override;

private:
    QString path_;
    int maxWidth_;
};

// 主题预览窗格（绘制在 ThemePreview.h 的实现上）
class ThemePreviewPane : public QWidget
{
public:
    explicit ThemePreviewPane(QWidget *parent = nullptr);

    void setTheme(const esp::ThemeStyle &style);
    void setSpec(const esp::MockSpec &spec);

protected:
    void paintEvent(QPaintEvent *e) override;

private:
    esp::ThemeStyle style_;
    esp::MockSpec spec_;
    QByteArray decodedFor_;
    QImage bgImage_;
};

class ThemeEditor : public QWidget
{
    Q_OBJECT
public:
    explicit ThemeEditor(bool uninstaller, QWidget *parent = nullptr);

    esp::ThemeStyle style() const { return style_; }
    void setStyle(const esp::ThemeStyle &style);

    // 预览中显示的产品名（跟随打包器表单的产品名称变化）
    void setPreviewAppName(const QString &appName);

    // 当前预览用的模型数据（CLI 导出效果图时复用）
    esp::MockSpec previewSpec() const;

signals:
    void styleChanged();

private:
    void buildUi();
    void pickColor(int index);
    void chooseBackground();
    void clearBackground();
    void onBackgroundLoaded(const QByteArray &encoded, const QSize &pixelSize);
    void onBackgroundFailed(const QString &error);
    void applyPresetIndex(int index);
    void refreshUi();
    void emitChanged();
    QString mockTitle() const;

    bool uninstaller_ = false;
    esp::ThemeStyle style_;
    QString appName_;

    QComboBox *presetCombo_ = nullptr;
    QPushButton *colorButtons_[4] = {nullptr, nullptr, nullptr, nullptr};
    QLabel *bgNameLabel_ = nullptr;
    QPushButton *bgChooseBtn_ = nullptr;
    QPushButton *bgClearBtn_ = nullptr;
    QComboBox *fitCombo_ = nullptr;
    QSlider *opacitySlider_ = nullptr;
    QLabel *opacityValue_ = nullptr;
    ThemePreviewPane *preview_ = nullptr;
    BackgroundImageTask *imageTask_ = nullptr;
};
