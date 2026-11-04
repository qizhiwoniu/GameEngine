#include "EditorWindow.h"
#include <QApplication>
#include <QFont>
#include <QIcon>
#include <QString>
#include <memory>
#include <windows.h>

namespace
{
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

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("3DGameEngine"));
    app.setOrganizationName(QStringLiteral("GameEngine"));
    app.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 9));
    // 窗口 / 任务栏图标（来自 Assets/app.qrc；exe 图标由 logo.rc 提供）
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/logo.ico")));

    auto win = std::make_unique<GameEditor::EditorWindow>();
    win->show();

    return app.exec();
}
