#pragma once
#include "console.h"
#include "core/debuglog.h"
#include "core/media.h"
#include "core/profiles.h"

#include <QDockWidget>
#include <QMainWindow>
#include <QSettings>
#include <QTabWidget>

class QScrollArea;

class MainWindow: public QMainWindow
{
    Q_OBJECT
public:
    MainWindow(
        aten::Profiles* profiles,
        aten::DebugLog* log,
        bool smoke = false);
    ~MainWindow() override;
    void connectionDialog();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    struct Tab
    {
        Console* console;
        QScrollArea* scroll;
        aten::Session* session;
        aten::Media* media;
        QVariantMap options;
        QString name;
        int id;
    };

    aten::Profiles* profiles;
    aten::DebugLog* log;
    QSettings settings;
    QTabWidget* tabs;
    QDockWidget* logDock;
    QList<Tab*> sessions;
    QList<QAction*> onlineActions;
    QAction *actualAction, *smoothAction, *backgroundAction, *reopenAction,
        *reconnectAction, *mountAction, *unmountAction;
    int fps = 0, fullSeconds = 5, quality = 11, mode = 444;
    int nextTabId = 1;
    QString traceFolder;
    bool smoke = false, closing = false;
    Tab* current() const;
    Tab* addTab(const QString& name = {});
    void closeTab(int index);
    void updateTabs();
    void applyPreferences();
    void makeMenus();
    void mountIso(const QString& path);
    void paste();
    void screenshot();
    void saveSettings();
};
