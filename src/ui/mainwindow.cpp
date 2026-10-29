#include "mainwindow.h"
#include <QApplication>
#include <QActionGroup>
#include <QMenuBar>
#include <QStatusBar>
#include <QTabBar>
#include <QToolButton>
#include <QScrollArea>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QPlainTextEdit>
#include <QMessageBox>
#include <QInputDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QDesktopServices>
#include <QClipboard>
#include <QCloseEvent>
#include <QPainter>

MainWindow::MainWindow(aten::Profiles *p,aten::DebugLog *l,bool test):profiles(p),log(l),smoke(test) {
    setWindowTitle("QATENtion");resize(1100,800);setMinimumSize(640,400);
    tabs=new QTabWidget(this);tabs->setTabsClosable(true);tabs->setMovable(true);setCentralWidget(tabs);
    auto *plus=new QToolButton(tabs);plus->setText("+");plus->setToolTip("New session tab");tabs->setCornerWidget(plus);
    connect(plus,&QToolButton::clicked,this,[this]{addTab();connectionDialog();});
    connect(tabs,&QTabWidget::tabCloseRequested,this,&MainWindow::closeTab);
    connect(tabs,&QTabWidget::currentChanged,this,[this]{
        if(auto *t=current()) log->write(QString("[tab %1 | %2] Selected").arg(t->id).arg(t->name));
        for(auto *t:sessions) t->console->cancelInput();
        applyPreferences();updateTabs();
    });
    logDock=new QDockWidget("Log",this);logDock->setObjectName("logPanel");
    auto *text=new QPlainTextEdit(logDock);text->setReadOnly(true);text->setMaximumBlockCount(3000);logDock->setWidget(text);
    addDockWidget(Qt::BottomDockWidgetArea,logDock);logDock->hide();
    // Batch display updates: packet logging must not repaint the whole log on each frame.
    auto *flush=new QTimer(this);flush->setInterval(250);
    connect(flush,&QTimer::timeout,this,[this,text]{
        if(logDock->isVisible() && text->toPlainText()!=log->text()) {text->setPlainText(log->text());text->moveCursor(QTextCursor::End);}
    });flush->start();
    fps=settings.value("fps",0).toInt();fullSeconds=settings.value("fullSeconds",5).toInt();
    quality=settings.value("quality",11).toInt();mode=settings.value("imageMode",444).toInt();
    makeMenus();
    if(!smoke) {restoreGeometry(settings.value("geometry").toByteArray());restoreState(settings.value("windowState").toByteArray());}
    if(!smoke && reopenAction->isChecked()) {
        const auto entries=profiles->entries();
        for(const auto &name:settings.value("openTabs").toStringList()) {
            for(const auto &entry:entries) if(entry.toMap().value("name").toString()==name) {
                auto *t=addTab(name);t->options=entry.toMap();t->options["autoReconnect"]=reconnectAction->isChecked();
                t->session->open(t->options);break;
            }
        }
        tabs->setCurrentIndex(qBound(0,settings.value("activeTab",0).toInt(),tabs->count()-1));
    }
    if(tabs->count()==0) addTab();
    applyPreferences();updateTabs();
}
MainWindow::~MainWindow() {
    closing=true;tabs->disconnect(this);
    for(auto *t:sessions) {
        t->session->disconnect(this);t->media->disconnect(this);t->console->disconnect(this);
        t->media->unmount();t->session->close();delete t->scroll;delete t;
    }
    sessions.clear();
}
MainWindow::Tab *MainWindow::current() const {
    for(auto *t:sessions) if(t->scroll==tabs->currentWidget()) return t;return nullptr;
}
MainWindow::Tab *MainWindow::addTab(const QString &name) {
    auto *scroll=new QScrollArea(tabs);auto *console=new Console(scroll);scroll->setWidget(console);scroll->setWidgetResizable(true);
    auto *t=new Tab{console,scroll,new aten::Session(console),new aten::Media(console),{},name,nextTabId++};
    console->setSession(t->session);sessions.append(t);
    console->setDiagnostics(log->enabled());
    connect(log,&aten::DebugLog::enabledChanged,console,[this,console]{console->setDiagnostics(log->enabled());});
    connect(t->session,&aten::Session::stateChanged,this,[this,t]{t->console->update();applyPreferences();updateTabs();});
    auto diagnostic=[this,t](const QString &s){log->write(QString("[tab %1 | %2] %3").arg(t->id).arg(t->name,s));};
    connect(t->session,&aten::Session::diagnostic,this,diagnostic);
    connect(console,&Console::diagnostic,this,diagnostic);
    connect(t->session,&aten::Session::unsupportedFrame,log,&aten::DebugLog::capture);
    connect(t->media,&aten::Media::diagnostic,this,diagnostic);
    connect(t->media,&aten::Media::changed,this,[this,t]{t->session->setKeepMedia(t->media->busy());updateTabs();});
    connect(console,&Console::isoDropped,this,&MainWindow::mountIso);
    tabs->setCurrentIndex(tabs->addTab(scroll,name.isEmpty()?"New session":name));
    t->session->setDisplay(quality,mode);applyPreferences();return t;
}
void MainWindow::closeTab(int index) {
    Tab *t=nullptr;for(auto *candidate:sessions) if(candidate->scroll==tabs->widget(index)) t=candidate;
    if(!t) return;
    if(t->media->busy() && QMessageBox::question(this,"Close session","Unmount the ISO and close this session?")!=QMessageBox::Yes) return;
    t->media->unmount();t->session->close();sessions.removeOne(t);tabs->removeTab(index);
    t->session->disconnect(this);t->media->disconnect(this);t->console->disconnect(this);
    QTimer::singleShot(1100,t->scroll,&QObject::deleteLater);delete t;
    if(tabs->count()==0 && !closing) addTab();
    updateTabs();
}
void MainWindow::updateTabs() {
    if(closing) return;
    auto *active=current();
    for(auto *t:sessions) {
        const int i=tabs->indexOf(t->scroll);if(i<0) continue;
        tabs->setTabText(i,QString("%1 [%2]").arg(t->name.isEmpty()?"New session":t->name).arg(t->id));
        tabs->setTabToolTip(i,t->session->status()+"\n"+t->media->status());
        QPixmap icon(12,12);icon.fill(Qt::transparent);QPainter painter(&icon);painter.setPen(Qt::NoPen);
        painter.setBrush(t->session->connected()?(t->session->hostPower()==0?QColor(Qt::gray):QColor(40,160,70)):QColor(Qt::gray));
        painter.drawEllipse(2,2,8,8);tabs->setTabIcon(i,QIcon(icon));
    }
    const bool online=active && active->session->connected();
    for(auto *action:onlineActions) action->setEnabled(online);
    mountAction->setEnabled(online && !active->media->busy());
    unmountAction->setEnabled(active && active->media->busy());
    if(auto *status=findChild<QAction*>("mediaStatus")) status->setText(active?active->media->status():"No session");
    if(active) {statusBar()->showMessage(active->session->status()+"  |  "+active->media->status());setWindowTitle((active->name.isEmpty()?QString():active->name+" — ")+"QATENtion");}
}
void MainWindow::applyPreferences() {
    if(!actualAction) return;
    for(auto *t:sessions) {
        const auto endpoint=t->session->mediaEndpoint();
        bool duplicate=false;
        if(t->session->connected()) for(auto *other:sessions) {
            const auto candidate=other->session->mediaEndpoint();
            if(other!=t && other->session->connected() && endpoint.port==candidate.port &&
               endpoint.host.compare(candidate.host,Qt::CaseInsensitive)==0) {duplicate=true;break;}
        }
        t->session->setDuplicateEndpoint(duplicate);
        t->console->setView(actualAction->isChecked(),smoothAction->isChecked());
        t->session->setStreaming(t==current() || backgroundAction->isChecked(),fps>0?1000/fps:0,fullSeconds);
    }
}
void MainWindow::makeMenus() {
    auto action=[this](QMenu *menu,const QString &name,std::function<void()> callback,bool online=false){
        auto *a=menu->addAction(name);connect(a,&QAction::triggered,this,callback);if(online) onlineActions.append(a);return a;
    };
    auto toggle=[this](QMenu *menu,const QString &name,const QString &key,bool initial){
        auto *a=menu->addAction(name);a->setCheckable(true);a->setChecked(settings.value(key,initial).toBool());
        connect(a,&QAction::toggled,this,[this,key](bool checked){if(!smoke) settings.setValue(key,checked);applyPreferences();});return a;
    };
    auto *power=menuBar()->addMenu("&Power");
    const QStringList powerNames={"Power off","Power on","Reset","Soft off (ACPI)"};
    for(int code:{1,0,2,3}) action(power,powerNames[code],[this,code,powerNames]{
        auto *t=current();if(!t) return;
        if(QMessageBox::question(this,powerNames[code],powerNames[code]+" "+t->name+"?") == QMessageBox::Yes) t->session->power(code);
    },true);
    auto *keys=menuBar()->addMenu("&Keys");
    action(keys,"Ctrl+Alt+Del",[this]{if(current()) current()->session->ctrlAltDelete();},true);
    action(keys,"Paste clipboard to host…",[this]{paste();},true);
    auto *send=keys->addMenu("Send keys");
    const QList<QPair<QString,QList<quint32>>> combos={
        {"Alt+Tab",{0xe2,0x2b}},{"Alt+F4",{0xe2,0x3d}},{"Alt+Space",{0xe2,0x2c}},
        {"Ctrl+Esc",{0xe0,0x29}},{"Windows key",{0xe3}},{"Print Screen",{0x46}}};
    for(const auto &c:combos) action(send,c.first,[this,c]{if(current()) current()->session->combo(c.second);},true);
    action(send,"Custom combination…",[this]{
        bool ok=false;const auto input=QInputDialog::getText(this,"Send key combination","USB HID usages in hexadecimal, separated by spaces\nExample: e0 e2 4c = Ctrl+Alt+Del",QLineEdit::Normal,{},&ok);
        if(!ok || !current()) return;QList<quint32> codes;
        for(const auto &part:input.split(' ',Qt::SkipEmptyParts)) {bool valid=false;auto code=part.toUInt(&valid,16);if(!valid || code==0 || code>0xe7 || codes.size()>=16) {QMessageBox::warning(this,"Invalid combination","Enter up to 16 valid USB HID usages.");return;}codes.append(code);}
        current()->session->combo(codes);
    },true);
    keys->addSeparator();action(keys,"Screenshot…",[this]{screenshot();},true);
    action(keys,"Keyboard / Mouse HotPlug",[this]{if(current()) current()->session->hotplug();},true);
    auto *mouse=menuBar()->addMenu("&Mouse");auto *absolute=mouse->addAction("Absolute");absolute->setCheckable(true);absolute->setChecked(true);
    for(const auto &name:{"Relative","Single"}) {auto *a=mouse->addAction(name);a->setEnabled(false);a->setToolTip("Not implemented in ATENtion");}
    auto *view=menuBar()->addMenu("&View");action(view,"Refresh",[this]{if(current()) current()->session->refresh();},true);
    auto *fullscreen=action(view,"Full screen",[this]{isFullScreen()?showNormal():showFullScreen();});fullscreen->setShortcut(Qt::Key_F11);
    actualAction=toggle(view,"Actual size (1:1)","actualSize",false);
    action(view,"Size window to host",[this]{if(current() && !current()->console->frameSize().isEmpty()) resize(current()->console->frameSize()+QSize(24,menuBar()->height()+statusBar()->height()+tabs->tabBar()->height()+24));});
    smoothAction=toggle(view,"Smooth scaling","smoothScaling",false);
    auto radio=[this](QMenu *menu,const QList<QPair<QString,int>> &items,int currentValue,std::function<void(int)> apply){
        auto *group=new QActionGroup(menu);
        for(const auto &item:items) {auto *a=menu->addAction(item.first);a->setCheckable(true);a->setChecked(item.second==currentValue);group->addAction(a);connect(a,&QAction::triggered,this,[apply,item]{apply(item.second);});}
    };
    radio(view->addMenu("Full-frame requests"),{{"Continuous",-1},{"Every second",1},{"Every 2 seconds",2},{"Every 3 seconds",3},{"Every 5 seconds",5},{"Every 10 seconds",10},{"Off (incremental only)",0}},fullSeconds,[this](int value){fullSeconds=value;applyPreferences();});
    backgroundAction=toggle(view,"Keep background tabs streaming","backgroundStreaming",false);
    radio(view->addMenu("Frame rate limit"),{{"Unlimited",0},{"60 FPS",60},{"30 FPS",30},{"10 FPS",10}},fps,[this](int value){fps=value;applyPreferences();});
    auto *display=view->addMenu("Display preference");
    radio(display,{{"Enhanced text (4:4:4)",444},{"Normal (4:2:2)",422}},mode,[this](int value){mode=value;for(auto *t:sessions) t->session->setDisplay(quality,mode);});
    radio(display->addMenu("Quality"),{{"11 (best)",11},{"9",9},{"6",6},{"3",3},{"0 (lowest)",0}},quality,[this](int value){quality=value;for(auto *t:sessions) t->session->setDisplay(quality,mode);});
    auto *connection=menuBar()->addMenu("&Connection");
    action(connection,"New session tab…",[this]{addTab();connectionDialog();});
    action(connection,"Connect / Change server…",[this]{connectionDialog();});
    action(connection,"Reconnect current",[this]{if(auto *t=current()) {t->console->cancelInput();t->options["autoReconnect"]=reconnectAction->isChecked();t->session->open(t->options);}});
    action(connection,"Disconnect",[this]{if(auto *t=current()) {t->media->unmount();t->session->close();}});
    reconnectAction=toggle(connection,"Auto reconnect","autoReconnect",true);
    connect(reconnectAction,&QAction::toggled,this,[this](bool enabled){for(auto *t:sessions) {t->options["autoReconnect"]=enabled;t->session->setAutoReconnect(enabled,t->options);}});
    reopenAction=toggle(connection,"Reopen tabs on startup","reopenTabs",true);
    connection->addSeparator();
    action(connection,"User list…",[this]{if(current()) QMessageBox::information(this,"Console users",current()->session->users().isEmpty()?"No session list received from BMC.":current()->session->users().join('\n'));},true);
    action(connection,"Session information…",[this]{
        if(!current()) return;
        auto *dialog=new QDialog(this);dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowTitle("Session information");
        auto *layout=new QVBoxLayout(dialog);auto *label=new QLabel(dialog);label->setTextInteractionFlags(Qt::TextSelectableByMouse);layout->addWidget(label);
        auto *timer=new QTimer(dialog);QPointer<aten::Session> session=current()->session;
        auto update=[label,session]{label->setText(session?session->information():"Session closed");};update();connect(timer,&QTimer::timeout,dialog,update);timer->start(1000);dialog->show();
    });
    auto *logging=menuBar()->addMenu("&Logging");
    auto *enabled=logging->addAction("Enable debug logging");enabled->setCheckable(true);enabled->setChecked(log->enabled());
    connect(enabled,&QAction::toggled,log,&aten::DebugLog::setEnabled);
    logging->addAction(logDock->toggleViewAction());
    action(logging,"Open log file",[this]{QDesktopServices::openUrl(QUrl::fromLocalFile(log->path()));});
    action(logging,"Clear log",[this]{log->clear();});
    auto *storage=menuBar()->addMenu("&Storage");
    auto *status=storage->addAction("No media mounted");status->setObjectName("mediaStatus");status->setEnabled(false);
    connect(storage,&QMenu::aboutToShow,this,[this,status]{status->setText(current()?current()->media->status():"No session");
        if(auto *t=current();t && t->session->connected() && !t->media->busy()) t->media->probe(t->session->mediaEndpoint(),t->session->untrustedAllowed());});
    connect(storage,&QMenu::aboutToHide,this,[this,status]{if(current()) status->setText(current()->media->status());});
    storage->addSeparator();
    mountAction=action(storage,"Mount ISO…",[this]{const auto path=QFileDialog::getOpenFileName(this,"Mount ISO",settings.value("isoDirectory").toString(),"ISO images (*.iso);;All files (*)");if(!path.isEmpty()) mountIso(path);});
    unmountAction=action(storage,"Unmount ISO",[this]{if(current()) current()->media->unmount();});
    auto *help=menuBar()->addMenu("&Help");
    action(help,"About QATENtion",[this]{QMessageBox::about(this,"QATENtion","QATENtion 0.2 — Qt/C++ ATEN console viewer\nBased on ATENtion.\n\nNative desktop menus, console sessions and read-only ISO redirection.\nhttps://github.com/librarian/qatention");});
    action(help,"About Qt",[this]{QMessageBox::aboutQt(this);});
}
void MainWindow::connectionDialog() {
    auto *t=current();if(!t) t=addTab();
    QDialog dialog(this);dialog.setObjectName("connectionDialog");dialog.setWindowTitle("Connect to a BMC");dialog.resize(520,570);
    auto *outer=new QVBoxLayout(&dialog);auto *scroll=new QScrollArea(&dialog);scroll->setWidgetResizable(true);outer->addWidget(scroll);
    auto *fields=new QWidget(scroll);scroll->setWidget(fields);auto *form=new QFormLayout(fields);
    auto *picker=new QComboBox(fields);auto *name=new QLineEdit(fields);auto *host=new QLineEdit(fields);
    auto *user=new QLineEdit(fields);auto *password=new QLineEdit(fields);password->setEchoMode(QLineEdit::Password);
    auto *arm=new QCheckBox("Log in through the BMC web interface",fields);auto *tls=new QCheckBox("Use TLS for console and media",fields);
    auto *untrusted=new QCheckBox("Allow unverified BMC certificates (ATENtion compatibility)",fields);
    auto *java=new QCheckBox("Switch BMC console interface to Java",fields);
    auto *webPort=new QSpinBox(fields);auto *port=new QSpinBox(fields);for(auto *spin:{webPort,port}) spin->setRange(1,65535);
    auto *profileRow=new QWidget(fields);auto *row=new QHBoxLayout(profileRow);row->setContentsMargins(0,0,0,0);row->addWidget(picker,1);
    auto *newProfile=new QPushButton("New",fields);auto *remove=new QPushButton("Delete",fields);row->addWidget(newProfile);row->addWidget(remove);
    form->addRow("Saved profile",profileRow);form->addRow("Name",name);form->addRow("Host",host);form->addRow(arm);
    form->addRow("Username / token",user);form->addRow("Password / token",password);form->addRow("HTTPS port",webPort);form->addRow("Console port",port);form->addRow(tls);form->addRow(untrusted);form->addRow(java);
    auto *note=new QLabel("Connect saves this profile, including credentials, on this computer. ATEN BMCs may use separate self-signed certificates for the console and media.",fields);note->setWordWrap(true);form->addRow(note);
    auto reload=[&]{picker->clear();picker->addItem("New profile");for(const auto &v:profiles->entries()) picker->addItem(v.toMap().value("name").toString(),v);};reload();
    auto load=[&](const QVariantMap &o){name->setText(o.value("name").toString());host->setText(o.value("host").toString());user->setText(o.value("user","ADMIN").toString());password->setText(o.value("password").toString());arm->setChecked(o.value("autoArm",true).toBool());tls->setChecked(o.value("tls",true).toBool());untrusted->setChecked(o.value("allowUntrusted",true).toBool());java->setChecked(o.value("switchJava",false).toBool());port->setValue(o.value("port",5900).toInt());webPort->setValue(o.value("webPort",443).toInt());};
    connect(picker,&QComboBox::activated,&dialog,[&](int){load(picker->currentData().toMap());});
    connect(newProfile,&QPushButton::clicked,&dialog,[&]{picker->setCurrentIndex(0);load({});});
    connect(remove,&QPushButton::clicked,&dialog,[&]{
        if(picker->currentIndex()>0 && QMessageBox::question(&dialog,"Delete profile","Delete "+picker->currentText()+"?")==QMessageBox::Yes) {profiles->remove(picker->currentText());reload();load({});}
    });
    connect(arm,&QCheckBox::toggled,&dialog,[&](bool on){webPort->setEnabled(on);java->setEnabled(on);port->setEnabled(!on);tls->setEnabled(!on);});
    load(t->options);webPort->setEnabled(arm->isChecked());java->setEnabled(arm->isChecked());port->setEnabled(!arm->isChecked());tls->setEnabled(!arm->isChecked());
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);buttons->button(QDialogButtonBox::Ok)->setText("Connect");outer->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{
        if(host->text().trimmed().isEmpty() || user->text().isEmpty() || password->text().isEmpty()) {QMessageBox::warning(&dialog,"Missing credentials","Enter a host, username and password (or tokens).");return;}
        if(t->media->busy() && QMessageBox::question(&dialog,"Change connection","Unmount the ISO and change this connection?")!=QMessageBox::Yes) return;
        const QString label=name->text().trimmed().isEmpty()?host->text().trimmed():name->text().trimmed();
        QVariantMap o={{"host",host->text().trimmed()},{"user",user->text()},{"password",password->text()},{"autoArm",arm->isChecked()},{"tls",tls->isChecked()},{"allowUntrusted",untrusted->isChecked()},{"switchJava",java->isChecked()},{"port",port->value()},{"webPort",webPort->value()},{"autoReconnect",reconnectAction->isChecked()},{"name",label}};
        if(!profiles->save(label,o)) {QMessageBox::warning(&dialog,"Cannot save profile",profiles->error());return;}
        t->media->unmount();t->options=o;t->name=label;t->session->setDisplay(quality,mode);t->session->open(o);applyPreferences();dialog.accept();
    });
    if(smoke) QTimer::singleShot(250,&dialog,[&dialog]{
        const auto path=qEnvironmentVariable("QATENTION_SMOKE_SCREENSHOT");if(!path.isEmpty()) dialog.grab().save(path);dialog.reject();
    });
    dialog.exec();if(t==current()) t->console->setFocus();
}
void MainWindow::paste() {
    auto *t=current();if(!t) return;const auto text=QApplication::clipboard()->text();if(text.isEmpty()) return;
    int skipped=0;for(auto c:text) {bool shift=false;if(c!='\r' && !Console::characterUsage(c,shift)) ++skipped;}
    if((skipped || text.size()>200) && QMessageBox::question(this,"Paste to host",QString("Type clipboard text using the host's US keyboard layout?\n%1 unsupported characters will be skipped.").arg(skipped))!=QMessageBox::Yes) return;
    t->console->setFocus();t->console->paste(text);
}
void MainWindow::screenshot() {
    if(!current()) return;const auto file=QFileDialog::getSaveFileName(this,"Save screenshot","console.png","PNG image (*.png)");
    if(!file.isEmpty() && !current()->console->saveScreenshot(QUrl::fromLocalFile(file))) QMessageBox::warning(this,"Screenshot","Could not save the screenshot.");
}
void MainWindow::mountIso(const QString &path) {
    auto *t=current();if(!t || !t->session->connected()) return;
    if(!smoke) settings.setValue("isoDirectory",QFileInfo(path).absolutePath());
    t->media->mount(t->session->mediaEndpoint(),t->session->untrustedAllowed(),path);
}
void MainWindow::saveSettings() {
    if(smoke) return;
    QStringList names;for(int i=0;i<tabs->count();++i) for(auto *t:sessions) if(t->scroll==tabs->widget(i) && !t->name.isEmpty()) names.append(t->name);
    settings.setValue("openTabs",names);settings.setValue("activeTab",tabs->currentIndex());settings.setValue("geometry",saveGeometry());settings.setValue("windowState",saveState());
    settings.setValue("fps",fps);settings.setValue("fullSeconds",fullSeconds);settings.setValue("quality",quality);settings.setValue("imageMode",mode);
}
void MainWindow::closeEvent(QCloseEvent *event) {
    if(closing) {event->accept();return;}
    bool media=false;for(auto *t:sessions) media|=t->media->busy();
    if(media && QMessageBox::question(this,"Quit","Unmount all ISOs and quit?")!=QMessageBox::Yes) {event->ignore();return;}
    saveSettings();closing=true;for(auto *t:sessions) {t->media->unmount();t->session->close();}if(media) {event->ignore();QTimer::singleShot(1100,this,&QWidget::close);}else event->accept();
}
