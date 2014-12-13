/**
 * The viewer application represented as an object...
 */

#pragma once

#include <QObject>
#include <memory>

class ScriptedCommandListener;
class ViewManager;

///
/// \brief The Viewer class is the main class of the viewer. It sets up all other
/// components.
///
class Viewer : public QObject
{
    Q_OBJECT

public:

    /// constructor
    /// should be called when platform is initialized, but connector isn't
    explicit Viewer();

    /// this should be called when connector is already initialized (i.e. it's
    /// safe to start setting/getting state)
    void start();

signals:

public slots:

protected slots:

    /// internal callback for scripted commands
    void scriptedCommandCB(QString command);

protected:

    /// pointer to scripted command listener
    /// @todo make it unique ptr for auto-delete niceness
    ScriptedCommandListener * m_scl = nullptr;

    std::shared_ptr<ViewManager> m_viewManager;
};
