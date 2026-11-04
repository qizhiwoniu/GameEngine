#pragma once
#include <windows.h>
#include "Theme.h"

namespace GameEngine
{
	// Qt6 与 Win32 外壳之间的桥。
	//
	// 现在主窗口还是 Win32 自绘（标题栏 / 菜单栏 / 弹出菜单都是自己画的），
	// 这里把 Qt 的工具条作为一个原生子窗口挂进主窗口内容区顶部。
	//
	// 迁移建议：以后整窗换成 QMainWindow 时，这个类整体删掉，
	// 工具条代码（BuildToolBar）可以直接搬过去，只是父窗口从 m_host 换成 QMainWindow。
	//
	// 用 pimpl 是为了不让 Qt 头文件扩散到头文件里（MainWindow.h 保持干净、编译快）。
	class QtShell
	{
	public:
		static QtShell& Instance();

		// 必须晚于 QApplication 构造、且主窗口已创建
		bool Initialize(HWND owner, HACCEL accel, ThemeMode theme);
		void Shutdown();

		// 内容区（客户区坐标、物理像素）变化时调用：工具条贴在内容区顶部
		void SetContentRect(const RECT& rcContent);

		// 主题切换：工具条跟着换成暗色 / 亮色
		void ApplyTheme(ThemeMode theme);

		// 进入 Qt 事件循环（替代原来的 GetMessage 循环）
		int  Exec();

		bool IsReady() const;

	private:
		QtShell() = default;
		~QtShell();
		QtShell(const QtShell&) = delete;
		QtShell& operator=(const QtShell&) = delete;

		struct Impl;
		Impl* m_impl = nullptr;
	};
}
