#include "RuntimeWindow.h"

#include <QGuiApplication>
#include <QJsonObject>
#include <QKeyEvent>
#include <QScreen>
#include <QVBoxLayout>

namespace GameEditor
{
	RuntimeWindow::RuntimeWindow(Viewport3D* source, Viewport3D::RuntimeMode mode,
		const QString& title, QWidget* parent)
		: QWidget(parent, Qt::Window)
		, m_source(source)
		, m_mode(mode)
	{
		setAttribute(Qt::WA_DeleteOnClose);
		setWindowTitle(title);
		setStyleSheet(QStringLiteral("background:#1e1e21;"));

		auto* lay = new QVBoxLayout(this);
		lay->setContentsMargins(0, 0, 0, 0);
		lay->setSpacing(0);

		m_view = new Viewport3D(this);
		lay->addWidget(m_view);

		// 复制当前场景（对象 + 相机姿态），运行时的改动不会回写编辑器
		if (source)
		{
			QJsonObject state;
			source->SaveState(state);
			m_view->LoadState(state);
		}
		m_view->EnterRuntime(mode);

		resize(1280, 720);
		if (QScreen* sc = QGuiApplication::primaryScreen())
		{
			const QRect avail = sc->availableGeometry();
			move(avail.center() - QPoint(width() / 2, height() / 2));
		}

		connect(m_view, &Viewport3D::RuntimeExitRequested, this, &QWidget::close);
	}

	void RuntimeWindow::showEvent(QShowEvent* e)
	{
		QWidget::showEvent(e);
		m_view->setFocus();
	}

	void RuntimeWindow::Refresh()
	{
		if (!m_source)
			return;
		// 重新取一份编辑器场景：对象改动会同步过来，模拟时间/速度归零
		QJsonObject state;
		m_source->SaveState(state);
		m_view->LoadState(state);
		m_view->EnterRuntime(m_mode);
		m_view->setFocus();
	}

	void RuntimeWindow::keyPressEvent(QKeyEvent* e)
	{
		// 视口没拿到焦点时（比如刚点过标题栏）也能 ESC 退出
		if (e->key() == Qt::Key_Escape)
		{
			close();
			return;
		}
		QWidget::keyPressEvent(e);
	}
}
