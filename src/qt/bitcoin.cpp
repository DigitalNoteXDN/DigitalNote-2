/*
 * W.J. van der Laan 2011-2012
 */

#include "compat.h"

#include <QApplication>
#include <QMessageBox>
#include <QTextCodec>
#include <QLocale>
#include <QTimer>
#include <QTranslator>
#include <QSplashScreen>
#include <QLibraryInfo>

#include <boost/filesystem.hpp>
#include <boost/thread.hpp>

#include "bitcoingui.h"
#include "clientmodel.h"
#include "walletmodel.h"
#include "optionsmodel.h"
#include "messagemodel.h"
#include "guiutil.h"
#include "guiconstants.h"
#include "init.h"
#include "util.h"
#include "paymentserver.h"
#include "wallet.h"
#include "cscript.h"
#include "main_const.h"
#include "main_extern.h"
#include "ui_interface.h"
#include "fork.h"
#include "walletrebuild.h"

#ifdef Q_OS_MAC
#include "macdockiconhandler.h"
#endif


#if defined(BITCOIN_NEED_QT_PLUGINS) && !defined(_BITCOIN_QT_PLUGINS_INCLUDED)
#define _BITCOIN_QT_PLUGINS_INCLUDED
#define __INSURE__
#include <QtPlugin>
Q_IMPORT_PLUGIN(qcncodecs)
Q_IMPORT_PLUGIN(qjpcodecs)
Q_IMPORT_PLUGIN(qtwcodecs)
Q_IMPORT_PLUGIN(qkrcodecs)
Q_IMPORT_PLUGIN(qtaccessiblewidgets)
#endif

// Need a global reference for the notifications to find the GUI
static DigitalNoteGUI *guiref;
static QSplashScreen *splashref;
static QColor splashMessageColor(255, 255, 255); // default white
static int splashMessageAlign = Qt::AlignCenter;  // overridden per-mode in main()

static void ThreadSafeMessageBox(const std::string& message, const std::string& caption, unsigned int style)
{
    // Message from network thread
    if(guiref)
    {
        // Hide the splash screen first if it's still up.  Otherwise our
        // message box renders BEHIND it because the normal-startup splash is
        // created with Qt::WindowStaysOnTopHint -- the user sees nothing
        // happen and the app appears frozen while it's actually waiting on
        // a dialog they can't see. The maintenance-mode splash doesn't have
        // this flag but hide-on-message is the right behaviour either way.
        //
        // The handler may be invoked from a non-GUI thread, so marshal
        // the hide() call rather than calling it directly.  Clearing
        // splashref afterwards stops InitMessage from refreshing it.
        if (splashref) {
            QMetaObject::invokeMethod(splashref, "hide", Qt::QueuedConnection);
            splashref = nullptr;
        }

        bool modal = (style & CClientUIInterface::MODAL);
        // In case of modal message, use blocking connection to wait for user to click a button
        QMetaObject::invokeMethod(guiref, "message",
                                   modal ? GUIUtil::blockingGUIThreadConnection() : Qt::QueuedConnection,
                                   Q_ARG(QString, QString::fromStdString(caption)),
                                   Q_ARG(QString, QString::fromStdString(message)),
                                   Q_ARG(bool, modal),
                                   Q_ARG(unsigned int, style));
    }
    else
    {
        LogPrintf("%s: %s\n", caption, message);
        fprintf(stderr, "%s: %s\n", caption.c_str(), message.c_str());
    }
}

static bool ThreadSafeAskFee(int64_t nFeeRequired, const std::string& strCaption)
{
    if(!guiref)
        return false;
    if(nFeeRequired < MIN_TX_FEE || nFeeRequired <= nTransactionFee)
        return true;
    bool payFee = false;

    QMetaObject::invokeMethod(guiref, "askFee", GUIUtil::blockingGUIThreadConnection(),
                               Q_ARG(qint64, nFeeRequired),
                               Q_ARG(bool*, &payFee));

    return payFee;
}

static void InitMessage(const std::string &message)
{
    if(splashref)
    {
        splashref->showMessage(QString::fromStdString(message), splashMessageAlign, splashMessageColor);
        splashref->raise();
        splashref->activateWindow();
        QApplication::instance()->processEvents();
    }
    LogPrintf("init message: %s\n", message);
}

/*
   Translate string to current locale using Qt.
 */
static std::string Translate(const char* psz)
{
    return QCoreApplication::translate("bitcoin-core", psz).toStdString();
}

/* Handle runaway exceptions. Shows a message box with the problem and quits the program.
 */
static void handleRunawayException(std::exception *e)
{
    PrintExceptionContinue(e, "Runaway exception");
    QMessageBox::critical(0, "Runaway exception", DigitalNoteGUI::tr("A fatal error occurred. DigitalNote can no longer continue safely and will quit.") + QString("\n\n") + QString::fromStdString(strMiscWarning));
    exit(1);
}

/* qDebug() message handler --> debug.log */
#if QT_VERSION < 0x050000
void DebugMessageHandler(QtMsgType type, const char * msg)
{
    const char *category = (type == QtDebugMsg) ? "qt" : NULL;
    LogPrint(category, "GUI: %s\n", msg);
}
#else
void DebugMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString &msg)
{
    const char *category = (type == QtDebugMsg) ? "qt" : NULL;
    LogPrint(category, "GUI: %s\n", msg.toStdString());
}
#endif

#ifndef BITCOIN_QT_TEST
int main(int argc, char *argv[])
{	
	fHaveGUI = true;
    // Command-line options take precedence:
    ParseParameters(argc, argv);

#if QT_VERSION < 0x050000
    // Internal string conversion is all UTF-8
    QTextCodec::setCodecForTr(QTextCodec::codecForName("UTF-8"));
    QTextCodec::setCodecForCStrings(QTextCodec::codecForTr());
#endif

    Q_INIT_RESOURCE(bitcoin);
    #if QT_VERSION >= QT_VERSION_CHECK(5, 6, 0)
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
#endif
    QApplication app(argc, argv);
    GUIUtil::applyDefaultFont(&app);

    // Do this early as we don't want to bother initializing if we are just calling IPC
    // ... but do it after creating app, so QCoreApplication::arguments is initialized:
    if (PaymentServer::ipcSendCommandLine())
        exit(0);
    PaymentServer* paymentServer = new PaymentServer(&app);

    // Install global event filter that makes sure that long tooltips can be word-wrapped
    app.installEventFilter(new GUIUtil::ToolTipToRichTextFilter(TOOLTIP_WRAP_THRESHOLD, &app));
    // Install qDebug() message handler to route to debug.log
#if QT_VERSION < 0x050000
    qInstallMsgHandler(DebugMessageHandler);
#else
    qInstallMessageHandler(DebugMessageHandler);
#endif

    // Command-line options take precedence:
    ParseParameters(argc, argv);

    // ... then bitcoin.conf:
    if (!boost::filesystem::is_directory(GetDataDir(false)))
    {
        // This message can not be translated, as translation is not initialized yet
        // (which not yet possible because lang=XX can be overridden in bitcoin.conf in the data directory)
        QMessageBox::critical(0, "DigitalNote",
                              QString("Error: Specified data directory \"%1\" does not exist.").arg(QString::fromStdString(mapArgs["-datadir"])));
        return 1;
    }
    ReadConfigFile(mapArgs, mapMultiArgs);

    // Application identification (must be set before OptionsModel is initialized,
    // as it is used to locate QSettings)
    app.setOrganizationName("DigitalNote");
    //XXX app.setOrganizationDomain("");
    if(GetBoolArg("-testnet", false)) // Separate UI settings for testnet
        app.setApplicationName("DigitalNote-Qt-testnet");
    else
        app.setApplicationName("DigitalNote-Qt");

    // ... then GUI settings:
    OptionsModel optionsModel;

    // Apply dark theme stylesheet if enabled
    if (fUseDarkTheme) {
        qApp->setStyleSheet(
            "QMainWindow, QDialog, QWidget { background-color: #1e1e1e; color: #d4d4d4; }"
            "QMenuBar { background-color: #2b2b2b; color: #d4d4d4; }"
            "QMenuBar::item:selected { background-color: #3d3d3d; }"
            "QMenu { background-color: #2b2b2b; color: #d4d4d4; border: 1px solid #444; }"
            "QMenu::item:selected { background-color: #3d6099; }"
            "QToolBar { background-color: #2b2b2b; border: none; }"
            "QTabWidget::pane { background-color: #1e1e1e; border: 1px solid #444; }"
            "QTabBar::tab { background-color: #2b2b2b; color: #d4d4d4; padding: 6px 12px; border: 1px solid #444; }"
            "QTabBar::tab:selected { background-color: #3d6099; color: #ffffff; }"
            "QTabBar::tab:hover { background-color: #3d3d3d; }"
            "QTableWidget, QTreeWidget, QListWidget { background-color: #252526; color: #d4d4d4; "
            "  gridline-color: #3d3d3d; border: 1px solid #444; alternate-background-color: #2d2d2d; }"
            "QTableWidget::item:selected, QTreeWidget::item:selected { background-color: #3d6099; color: #ffffff; }"
            "QHeaderView::section { background-color: #2b2b2b; color: #d4d4d4; border: 1px solid #444; padding: 4px; }"
            "QLineEdit, QTextEdit, QPlainTextEdit { background-color: #252526; color: #d4d4d4; "
            "  border: 1px solid #555; border-radius: 3px; padding: 2px; }"
            "QLineEdit:focus, QTextEdit:focus { border: 1px solid #3d6099; }"
            "QPushButton { background-color: #3d3d3d; color: #d4d4d4; border: 1px solid #555; "
            "  border-radius: 4px; padding: 4px 12px; }"
            "QPushButton:hover { background-color: #4a4a4a; }"
            "QPushButton:pressed { background-color: #3d6099; }"
            "QPushButton:disabled { background-color: #2b2b2b; color: #666; }"
            "QComboBox { background-color: #252526; color: #d4d4d4; border: 1px solid #555; "
            "  border-radius: 3px; padding: 2px 6px; }"
            "QComboBox::drop-down { border: none; }"
            "QComboBox QAbstractItemView { background-color: #2b2b2b; color: #d4d4d4; "
            "  selection-background-color: #3d6099; }"
            "QScrollBar:vertical { background-color: #2b2b2b; width: 12px; }"
            "QScrollBar::handle:vertical { background-color: #555; border-radius: 6px; min-height: 20px; }"
            "QScrollBar::handle:vertical:hover { background-color: #777; }"
            "QScrollBar:horizontal { background-color: #2b2b2b; height: 12px; }"
            "QScrollBar::handle:horizontal { background-color: #555; border-radius: 6px; min-width: 20px; }"
            "QCheckBox { color: #d4d4d4; }"
            "QCheckBox::indicator { width: 13px; height: 13px; border: 1px solid #666; background-color: #2d2d2d; border-radius: 2px; }"
            "QCheckBox::indicator:unchecked:hover { border: 1px solid #3d6099; }"
            "QCheckBox::indicator:checked { background-color: #3d6099; border: 1px solid #3d6099; image: url(:/images/checkbox_checked); }"
            "QLabel { color: #d4d4d4; }"
            "QGroupBox { color: #d4d4d4; border: 1px solid #555; border-radius: 4px; margin-top: 8px; }"
            "QGroupBox::title { color: #a0c4ff; }"
            "QSplitter::handle { background-color: #3d3d3d; }"
            "QToolTip { background-color: #2b2b2b; color: #d4d4d4; border: 1px solid #555; }"
            "QStatusBar { background-color: #1d1f22; color: #3098c6; }"
            "QProgressBar { background-color: #2b2b2b; border: 1px solid #555; border-radius: 4px; }"
            "QProgressBar::chunk { background-color: #3d6099; border-radius: 4px; }"
            "QFrame { color: #d4d4d4; }"
            "QSpinBox { background-color: #252526; color: #e0e0e0; border: 1px solid #555; }"
            "QDoubleSpinBox { background-color: #252526; color: #e0e0e0; border: 1px solid #555; }"
            "QSlider::groove { background-color: #3d3d3d; }"
            "QSlider::handle { background-color: #3d6099; border-radius: 6px; }"
        );
    }

    // Get desired locale (e.g. "de_DE") from command line or use system locale
    QString lang_territory = QString::fromStdString(GetArg("-lang", QLocale::system().name().toStdString()));
    QString lang = lang_territory;
    // Convert to "de" only by truncating "_DE"
    lang.truncate(lang_territory.lastIndexOf('_'));

    QTranslator qtTranslatorBase, qtTranslator, translatorBase, translator;
    // Load language files for configured locale:
    // - First load the translator for the base language, without territory
    // - Then load the more specific locale translator

    // Load e.g. qt_de.qm
    if (qtTranslatorBase.load("qt_" + lang, QLibraryInfo::location(QLibraryInfo::TranslationsPath)))
        app.installTranslator(&qtTranslatorBase);

    // Load e.g. qt_de_DE.qm
    if (qtTranslator.load("qt_" + lang_territory, QLibraryInfo::location(QLibraryInfo::TranslationsPath)))
        app.installTranslator(&qtTranslator);

    // Load e.g. bitcoin_de.qm (shortcut "de" needs to be defined in bitcoin.qrc)
    if (translatorBase.load(lang, ":/translations/"))
        app.installTranslator(&translatorBase);

    // Load e.g. bitcoin_de_DE.qm (shortcut "de_DE" needs to be defined in bitcoin.qrc)
    if (translator.load(lang_territory, ":/translations/"))
        app.installTranslator(&translator);

    // Subscribe to global signals from core
    uiInterface.ThreadSafeMessageBox.connect(ThreadSafeMessageBox);
    uiInterface.ThreadSafeAskFee.connect(ThreadSafeAskFee);
    uiInterface.InitMessage.connect(InitMessage);
    uiInterface.Translate.connect(Translate);

    // Show help message immediately after parsing command-line options (for "-lang") and setting locale,
    // but before showing splash screen.
    if (mapArgs.count("-?") || mapArgs.count("--help"))
    {
        GUIUtil::HelpMessageBox help;
        help.showOrPrint();
        return 1;
    }

#ifdef Q_OS_MAC
    // on mac, also change the icon now because it would look strange to have a testnet splash (green) and a std app icon (orange)
    if(GetBoolArg("-testnet", false))
    {
        MacDockIconHandler::instance()->setIcon(QIcon(":icons/bitcoin_testnet"));
    }
#endif

    // Both themes use same splash image
    // Light: white text, Dark: black text
    splashMessageColor = fUseDarkTheme ? QColor(0, 0, 0) : QColor(255, 255, 255);

    // Maintenance mode: triggered by any startup flag that puts the wallet
    // into a long, opt-in operation where the GUI cannot open until the
    // operation completes. Splash uses a chromed window (taskbar entry,
    // minimise/close buttons, no always-on-top) so users can put the splash
    // in the background while it runs. Normal startup keeps the original
    // frameless always-on-top splash for the brief load.
    //
    // The umbrella check covers every flag that triggers a multi-minute
    // startup phase. -maintenancemode is a no-op flag for testing the
    // chromed splash without invoking a real maintenance operation.
    // -iknowsalvagewalletisdangerous is the deprecated salvagewallet's
    //  escape hatch; it implies maintenance mode if used.
    // The .rebuildwallet-pending flag is written by the GUI Compact Wallet
    //  flow before requesting shutdown -- on next launch the rebuild
    //  handler in init.cpp consumes it and runs RebuildWallet().
    bool fMaintenanceMode =
        GetBoolArg("-rebuildwallet", false) ||
        GetBoolArg("-rescan", false) ||
        GetBoolArg("-reindex", false) ||
        GetBoolArg("-iknowsalvagewalletisdangerous", false) ||
        GetBoolArg("-maintenancemode", false) ||
        RebuildPendingFlagExists();

    // Splash image: swap to the maintenance variant for maintenance mode.
    // The maintenance image is fully opaque and has "MAINTENANCE MODE" baked
    // in below the circle, eliminating runtime painting entirely (which
    // caused progressive-bolding artefacts when chunked InitMessage repaints
    // happened at high frequency during block index load).
    QPixmap splashPixmap(fMaintenanceMode
        ? ":/images/splash_maintenance"
        : ":/images/splash");
    Qt::WindowFlags splashFlags =
        Qt::WindowStaysOnTopHint | Qt::FramelessWindowHint | Qt::SplashScreen;

    QSplashScreen splash(splashPixmap, splashFlags);
    if (fMaintenanceMode)
    {
        // Replace the auto-added Qt::SplashScreen flag with a chromed set:
        // title bar, taskbar entry, minimise + close buttons, plus
        // always-on-top so it doesn't get buried under MSYS2/IDE/etc.
        // windows during long-running operations.  The user can still
        // minimise to tray and restore via the tray icon (always-on-top
        // governs Z-order while visible; hide() works regardless).
        // (QSplashScreen constructor unconditionally ORs Qt::SplashScreen
        //  into whatever we pass; setWindowFlags() AFTER construction
        //  replaces rather than ORs.)
        splash.setWindowFlags(Qt::Window | Qt::WindowMinimizeButtonHint
                            | Qt::WindowCloseButtonHint | Qt::WindowTitleHint
                            | Qt::WindowStaysOnTopHint);
        splash.setWindowTitle(QObject::tr("DigitalNote -- Maintenance Mode"));
        splash.setAttribute(Qt::WA_ShowWithoutActivating);
        // Maintenance pixmap is fully opaque, no need for translucent
        // background or autoFillBackground gymnastics.
        splash.setAttribute(Qt::WA_TranslucentBackground, false);
    }
    else
    {
        splash.setAttribute(Qt::WA_TranslucentBackground);
    }

    if (GetBoolArg("-splash", true) && !GetBoolArg("-min", false))
    {
        splash.show();
        splashref = &splash;
    }

    app.processEvents();

    app.setQuitOnLastWindowClosed(false);

    try
    {
        // Regenerate startup link, to fix links to old versions
        if (GUIUtil::GetStartOnSystemStartup())
            GUIUtil::SetStartOnSystemStartup(true);

        boost::thread_group threadGroup;

        DigitalNoteGUI window;
        guiref = &window;

        QTimer* pollShutdownTimer = new QTimer(guiref);
        QObject::connect(pollShutdownTimer, SIGNAL(timeout()), guiref, SLOT(detectShutdown()));
        pollShutdownTimer->start(200);

        if(AppInit2(threadGroup))
        {
            {
                // Put this in a block, so that the Model objects are cleaned up before
                // calling Shutdown().

                paymentServer->setOptionsModel(&optionsModel);

                if (splashref)
                {
                    // Clear splashref BEFORE finish() so any late InitMessage
                    // calls (e.g. keypool top-up firing after "Done loading"
                    // but before the GUI opens) no-op instead of painting
                    // onto a splash that's about to close.
                    splashref = nullptr;
                    splash.finish(&window);
                }

                ClientModel clientModel(&optionsModel);
                WalletModel walletModel(pwalletMain, &optionsModel);
                MessageModel messageModel(pwalletMain, &walletModel);

                window.setClientModel(&clientModel);
                window.setWalletModel(&walletModel);
                window.setMessageModel(&messageModel);

                // If -min option passed, start window minimized.
                if(GetBoolArg("-min", false))
                {
                    window.showMinimized();
                }
                else
                {
                    window.show();
                }

                // Now that initialization/startup is done, process any command-line
                // bitcoin: URIs
                QObject::connect(paymentServer, SIGNAL(receivedURI(QString)), &window, SLOT(handleURI(QString)));
                QTimer::singleShot(100, paymentServer, SLOT(uiReady()));

                app.exec();

                window.hide();
                window.setClientModel(0);
                window.setWalletModel(0);
                guiref = 0;
            }
            // Shutdown the core and its threads, but don't exit DigitalNote-Qt here
            threadGroup.interrupt_all();
            threadGroup.join_all();
            Shutdown();
        }
        else
        {
            threadGroup.interrupt_all();
            threadGroup.join_all();
            Shutdown();
            return 1;
        }
    } catch (std::exception& e) {
        handleRunawayException(&e);
    } catch (...) {
        handleRunawayException(NULL);
    }
    return 0;
}
#endif // BITCOIN_QT_TEST