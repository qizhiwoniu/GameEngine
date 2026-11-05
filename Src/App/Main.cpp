#include "EditorWindow.h"
#include <QApplication>
#include <QTimer>
#include <QFont>
#include <QIcon>
#include <QString>
#include <QtGlobal>
#include <cstdio>
#include <memory>
#include <windows.h>

namespace
{
    // 排障/验收辅助：GE_SHOW_SETTINGS=1 时启动后自动打开"设置"窗口
    void MaybeAutoShowSettings(GameEditor::EditorWindow* win)
    {
        if (qEnvironmentVariableIsSet("GE_SHOW_SETTINGS"))
            QTimer::singleShot(2500, win, [win] { win->OpenSettings(); });
    }

    // Windows GUI 程序的 qWarning/qDebug 默认只发到 OutputDebugString（调试器），
    // 命令行重定向 stderr 收不到。装一个处理器把日志转发到 stderr，
    // 这样"D:\...\GameEngine.exe >log.txt 2>&1"就能收集诊断信息。
    void StderrMessageHandler(QtMsgType type, const QMessageLogContext&, const QString& msg)
    {
        const char* level = "?";
        switch (type)
        {
            case QtDebugMsg:    level = "D"; break;
            case QtInfoMsg:     level = "I"; break;
            case QtWarningMsg:  level = "W"; break;
            case QtCriticalMsg: level = "C"; break;
            case QtFatalMsg:    level = "F"; break;
        }
        std::fprintf(stderr, "[%s] %s\n", level, msg.toLocal8Bit().constData());
        std::fflush(stderr);
    }

    // 按显示器 DPI 感知：高分屏下界面不再被系统拉伸模糊
    void EnableDpiAwareness()
    {
        if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
            SetProcessDPIAware();   // 老系统回退
    }
}

// 注意：本工程由 Qt6::EntryPoint 提供 WinMain（qtentrypoint_win.cpp 里会调用 main），
// 所以这里只能定义 main，不能再自己写 WinMain，否则会与 Qt 的入口点重复/冲突。
int main(int argc, char* argv[])
{
    EnableDpiAwareness();

    qInstallMessageHandler(StderrMessageHandler);

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("3DGameEngine"));
    app.setOrganizationName(QStringLiteral("GameEngine"));
    app.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 9));
    // 窗口 / 任务栏图标（来自 Assets/app.qrc；exe 图标由 logo.rc 提供）
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/logo.ico")));

    auto win = std::make_unique<GameEditor::EditorWindow>();
    win->show();
    MaybeAutoShowSettings(win.get());

    // 排障/验收辅助：GE_TEST_LOCK=1 时启动后自测"锁定布局→解锁布局"后的面板显隐
    if (qEnvironmentVariableIsSet("GE_TEST_LOCK"))
    {
        QTimer::singleShot(3000, win.get(), [w = win.get()]
            {
                w->SelfTestLock();
                QCoreApplication::quit();
            });
    }

    return app.exec();
}
