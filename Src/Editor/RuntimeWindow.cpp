#include "RuntimeWindow.h"

#include "Document.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QScreen>
#include <QVBoxLayout>

namespace GameEditor
{
	RuntimeWindow::RuntimeWindow(Viewport3D* source, Viewport3D::RuntimeMode mode,
		const QString& title, const QJsonObject& graph, QWidget* parent)
		: QWidget(parent, Qt::Window)
		, m_source(source)
		, m_mode(mode)
		, m_graph(graph)
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
		m_view->SetRuntimeGraph(m_graph);            // 2D 节点图（空图 = 纯重力）
		m_view->EnterRuntime(mode);

		resize(1280, 720);
		if (QScreen* sc = QGuiApplication::primaryScreen())
		{
			const QRect avail = sc->availableGeometry();
			move(avail.center() - QPoint(width() / 2, height() / 2));
		}

		connect(m_view, &Viewport3D::RuntimeExitRequested, this, &QWidget::close);

		// 方块碰到跳转精灵 -> 在运行窗口内部换关卡（编辑器里的当前文档不受影响）
		connect(m_view, &Viewport3D::LevelSwitchRequested, this, &RuntimeWindow::SwitchLevel);
	}

	// 目标关卡文件：写了名字就用它（可写 Level02 / Level02.yu / 绝对路径），
	// 没写 = 自动下一关（Levels 目录里按文件名排在当前关之后的那一个，到末尾回到第一个）
	QString RuntimeWindow::ResolveLevel(const QString& target) const
	{
		const QDir d(Document::LevelsDir());
		QString name = target.trimmed();
		if (!name.isEmpty())
		{
			if (!name.endsWith(QStringLiteral(".yu"), Qt::CaseInsensitive))
				name += QStringLiteral(".yu");
			const QString path = QFileInfo(name).isAbsolute() ? name : d.filePath(name);
			return QFile::exists(path) ? path : QString();
		}

		const QStringList files = d.exists()
			? d.entryList(QStringList() << QStringLiteral("*.yu"), QDir::Files, QDir::Name)
			: QStringList();
		if (files.isEmpty())
			return {};
		const int idx = files.indexOf(QFileInfo(m_levelPath).fileName());
		if (idx < 0)
			return d.filePath(files.first());
		return d.filePath(files.at((idx + 1) % files.size()));
	}

	void RuntimeWindow::SwitchLevel(const QString& target)
	{
		const QString path = ResolveLevel(target);
		if (path.isEmpty())
			return;

		QJsonObject vp, graph;
		QString err;
		if (!Document::LoadRuntime(path, vp, graph, &err))
		{
			qWarning("切关卡失败：%s", qPrintable(err));
			return;
		}

		// 换成新关卡的对象 + 节点图，重新进入运行（模拟时间 / 速度 / 按键状态归零）
		m_view->LoadState(vp);
		m_view->SetRuntimeGraph(graph);
		m_view->EnterRuntime(m_mode);
		m_view->setFocus();

		m_levelPath = path;
		const QString mode = (m_mode == Viewport3D::RuntimeMode::Project) ? tr("Run Project") : tr("Run Scene");
		setWindowTitle(QStringLiteral("%1 - %2").arg(mode, QFileInfo(path).completeBaseName()));
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
		m_view->SetRuntimeGraph(m_graph);
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
