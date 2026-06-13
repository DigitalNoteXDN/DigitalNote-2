#include "compat.h"

#include <QComboBox>
#include <QDateTimeEdit>
#include <QDesktopServices>
#include <QDoubleValidator>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPoint>
#include <QScrollBar>
#include <QSettings>
#include <QSignalMapper>
#include <QTableView>
#include <QTabBar>
#include <QUrl>
#include <QVBoxLayout>
#include <QMessageBox>

#include "addresstablemodel.h"
#include "bitcoinunits.h"
#include "csvmodelwriter.h"
#include "editaddressdialog.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "transactiondescdialog.h"
#include "transactionfilterproxy.h"
#include "transactionrecord.h"
#include "transactiontablemodel.h"
#include "walletmodel.h"
#include "smsg.h"
#include "cclientuiinterface.h"

#include "transactionview.h"

TransactionView::TransactionView(QWidget *parent) :
    QWidget(parent), model(0), transactionProxyModel(0),
    transactionView(0)
{
    QSettings settings;
    // Build filter row
    setContentsMargins(0,0,0,0);

    // Tab bar at the very top: "My Transactions" / "Watch-Only".
    // Drives the watch-only filter dimension on the proxy model.
    // The Watch-Only tab is hidden when no watch-only addresses are
    // present in the wallet (managed by updateWatchOnlyColumn).
    watchOnlyTabBar = new QTabBar(this);
    watchOnlyTabBar->addTab(tr("My Transactions"));
    watchOnlyTabBar->addTab(tr("Watch-Only"));
    watchOnlyTabBar->setTabData(0, (int)TransactionFilterProxy::WatchOnlyFilter_No);
    watchOnlyTabBar->setTabData(1, (int)TransactionFilterProxy::WatchOnlyFilter_Yes);
    watchOnlyTabBar->setCurrentIndex(0);
    watchOnlyTabBar->setExpanding(false);
    watchOnlyTabBar->setDocumentMode(true);
    // Visible styling: bold selected tab, larger padding so it's easy
    // to see which view is currently active.  Padding is generous
    // because the bold weight on the selected tab takes more horizontal
    // space than the regular weight, and we don't want the text to
    // overflow or jiggle when switching tabs.
    watchOnlyTabBar->setStyleSheet(
        "QTabBar::tab {"
        "  padding: 8px 28px;"
        "  min-width: 140px;"
        "  font-size: 13px;"
        "}"
        "QTabBar::tab:selected {"
        "  font-weight: bold;"
        "  border-bottom: 2px solid palette(highlight);"
        "}"
        "QTabBar::tab:!selected {"
        "  color: palette(mid);"
        "}");

    QHBoxLayout *hlayout = new QHBoxLayout();
    hlayout->setContentsMargins(0,0,0,0);
#ifdef Q_OS_MAC
    hlayout->setSpacing(5);
    hlayout->addSpacing(26);
#else
    hlayout->setSpacing(0);
    hlayout->addSpacing(23);
#endif

    dateWidget = new QComboBox(this);
#ifdef Q_OS_MAC
    dateWidget->setFixedWidth(121);
#else
    dateWidget->setFixedWidth(120);
#endif
    dateWidget->addItem(tr("All"), All);
    dateWidget->addItem(tr("Today"), Today);
    dateWidget->addItem(tr("This week"), ThisWeek);
    dateWidget->addItem(tr("This month"), ThisMonth);
    dateWidget->addItem(tr("Last month"), LastMonth);
    dateWidget->addItem(tr("This year"), ThisYear);
    dateWidget->addItem(tr("Range..."), Range);
    dateWidget->setCurrentIndex(settings.value("transactionDate").toInt());
    hlayout->addWidget(dateWidget);

    typeWidget = new QComboBox(this);
#ifdef Q_OS_MAC
    typeWidget->setFixedWidth(TYPE_COLUMN_WIDTH+1);
#else
    typeWidget->setFixedWidth(TYPE_COLUMN_WIDTH);
#endif

    typeWidget->addItem(tr("All"), TransactionFilterProxy::ALL_TYPES);
    typeWidget->addItem(tr("Most Common"), TransactionFilterProxy::COMMON_TYPES);
    typeWidget->addItem(tr("Received with"), TransactionFilterProxy::TYPE(TransactionRecord::RecvWithAddress) |
                                        TransactionFilterProxy::TYPE(TransactionRecord::RecvFromOther));
    typeWidget->addItem(tr("Sent to"), TransactionFilterProxy::TYPE(TransactionRecord::SendToAddress) |
                                  TransactionFilterProxy::TYPE(TransactionRecord::SendToOther));
    typeWidget->addItem(tr("Darksent"), TransactionFilterProxy::TYPE(TransactionRecord::Darksent));
    typeWidget->addItem(tr("MNengine Make Collateral Inputs"), TransactionFilterProxy::TYPE(TransactionRecord::MNengineMakeCollaterals));
    typeWidget->addItem(tr("MNengine Collateral Payment"), TransactionFilterProxy::TYPE(TransactionRecord::MNengineCollateralPayment));
    typeWidget->addItem(tr("To yourself"), TransactionFilterProxy::TYPE(TransactionRecord::SendToSelf));
    typeWidget->addItem(tr("Mined"), TransactionFilterProxy::TYPE(TransactionRecord::Generated));
    typeWidget->addItem(tr("Other"), TransactionFilterProxy::TYPE(TransactionRecord::Other));
    typeWidget->setCurrentIndex(settings.value("transactionType").toInt());

    hlayout->addWidget(typeWidget);

    addressWidget = new QLineEdit(this);
#if QT_VERSION >= 0x040700
    addressWidget->setPlaceholderText(tr("Enter address or label to search"));
#endif
    hlayout->addWidget(addressWidget);

    amountWidget = new QLineEdit(this);
#if QT_VERSION >= 0x040700
    amountWidget->setPlaceholderText(tr("Min amount"));
#endif
#ifdef Q_OS_MAC
    amountWidget->setFixedWidth(97);
#else
    amountWidget->setFixedWidth(100);
#endif
    amountWidget->setValidator(new QDoubleValidator(0, 1e20, 8, this));
    hlayout->addWidget(amountWidget);

    QVBoxLayout *vlayout = new QVBoxLayout(this);
    vlayout->setContentsMargins(0,0,0,0);
    vlayout->setSpacing(0);

    QTableView *view = new QTableView(this);
    vlayout->addWidget(watchOnlyTabBar);
    vlayout->addLayout(hlayout);
    vlayout->addWidget(createDateRangeWidget());
    vlayout->addWidget(view);
    vlayout->setSpacing(0);
    int width = view->verticalScrollBar()->sizeHint().width();
    // Cover scroll bar width with spacing
#ifdef Q_OS_MAC
    hlayout->addSpacing(width+2);
#else
    hlayout->addSpacing(width);
#endif
    // Always show scroll bar
    view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    view->setTabKeyNavigation(false);
    view->setContextMenuPolicy(Qt::CustomContextMenu);

    view->installEventFilter(this);

    transactionView = view;

    // Actions
    QAction *copyAddressAction = new QAction(tr("Copy address"), this);
    QAction *copyLabelAction = new QAction(tr("Copy label"), this);
    QAction *copyAmountAction = new QAction(tr("Copy amount"), this);
    QAction *copyTxIDAction = new QAction(tr("Copy transaction ID"), this);
    QAction *copySmsgInfoAction = new QAction(tr("Copy Secure Message Info"), this);
    QAction *editLabelAction = new QAction(tr("Edit label"), this);
    QAction *showDetailsAction = new QAction(tr("Show transaction details"), this);

    contextMenu = new QMenu();
    contextMenu->addAction(copyAddressAction);
    contextMenu->addAction(copyLabelAction);
    contextMenu->addAction(copyAmountAction);
    contextMenu->addAction(copyTxIDAction);
    contextMenu->addAction(copySmsgInfoAction);
    contextMenu->addAction(editLabelAction);
    contextMenu->addAction(showDetailsAction);

    // Connect actions
    connect(dateWidget, SIGNAL(activated(int)), this, SLOT(chooseDate(int)));
    connect(typeWidget, SIGNAL(activated(int)), this, SLOT(chooseType(int)));
    connect(watchOnlyTabBar, SIGNAL(currentChanged(int)), this, SLOT(onWatchOnlyTabChanged(int)));
    connect(addressWidget, SIGNAL(textChanged(QString)), this, SLOT(changedPrefix(QString)));
    connect(amountWidget, SIGNAL(textChanged(QString)), this, SLOT(changedAmount(QString)));

    // doubleClicked is a relay signal on TransactionView (declared in the
    // signals: section of transactionview.h).  This connect forwards the
    // inner QTableView's doubleClicked event up to listeners on
    // TransactionView itself (bitcoingui.cpp connects it to showDetails).
    // Was previously SLOT(doubleClicked(QModelIndex)) which made Qt look
    // up doubleClicked on the slot list, fail, and log a runtime warning.
    connect(view, SIGNAL(doubleClicked(QModelIndex)), this, SIGNAL(doubleClicked(QModelIndex)));
    connect(view, SIGNAL(clicked(QModelIndex)), this, SLOT(computeSum()));
    connect(view, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(contextualMenu(QPoint)));

    connect(copyAddressAction, SIGNAL(triggered()), this, SLOT(copyAddress()));
    connect(copyLabelAction, SIGNAL(triggered()), this, SLOT(copyLabel()));
    connect(copyAmountAction, SIGNAL(triggered()), this, SLOT(copyAmount()));
    connect(copyTxIDAction, SIGNAL(triggered()), this, SLOT(copyTxID()));
    connect(copySmsgInfoAction, SIGNAL(triggered()), this, SLOT(copySmsgInfo()));
    connect(editLabelAction, SIGNAL(triggered()), this, SLOT(editLabel()));
    connect(showDetailsAction, SIGNAL(triggered()), this, SLOT(showDetails()));
}

void TransactionView::setModel(WalletModel *model)
{
    QSettings settings;
    this->model = model;
    if(model)
    {
        transactionProxyModel = new TransactionFilterProxy(this);
        transactionProxyModel->setSourceModel(model->getTransactionTableModel());
        transactionProxyModel->setDynamicSortFilter(true);
        transactionProxyModel->setSortCaseSensitivity(Qt::CaseInsensitive);
        transactionProxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);

        transactionProxyModel->setSortRole(Qt::EditRole);

        transactionView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        transactionView->setModel(transactionProxyModel);
        transactionView->setAlternatingRowColors(true);
        transactionView->setSelectionBehavior(QAbstractItemView::SelectRows);
        transactionView->setSelectionMode(QAbstractItemView::ExtendedSelection);
        transactionView->setSortingEnabled(true);
        transactionView->sortByColumn(TransactionTableModel::Date, Qt::DescendingOrder);
        transactionView->verticalHeader()->hide();

        transactionView->setColumnWidth(TransactionTableModel::Status, STATUS_COLUMN_WIDTH);
        transactionView->setColumnWidth(TransactionTableModel::Watchonly, WATCHONLY_COLUMN_WIDTH);
        transactionView->setColumnWidth(TransactionTableModel::Date, DATE_COLUMN_WIDTH);
        transactionView->setColumnWidth(TransactionTableModel::Type, TYPE_COLUMN_WIDTH);
        transactionView->setColumnWidth(TransactionTableModel::Amount, AMOUNT_MINIMUM_COLUMN_WIDTH);

        // Note: it's a good idea to connect this signal AFTER the model is set
        connect(transactionView->selectionModel(), SIGNAL(selectionChanged(QItemSelection, QItemSelection)), this, SLOT(computeSum()));

        columnResizingFixer = new GUIUtil::TableViewLastColumnResizingFixer(transactionView, AMOUNT_MINIMUM_COLUMN_WIDTH, MINIMUM_COLUMN_WIDTH);

        /*if (model->getOptionsModel())
        {
            // Add third party transaction URLs to context menu
            QStringList listUrls = model->getOptionsModel()->getThirdPartyTxUrls().split("|", QString::SkipEmptyParts);
            for (int i = 0; i < listUrls.size(); ++i)
            {
                QString host = QUrl(listUrls[i].trimmed(), QUrl::StrictMode).host();
                if (!host.isEmpty())
                {
                    QAction *thirdPartyTxUrlAction = new QAction(host, this); // use host as menu item label
                    if (i == 0)
                        contextMenu->addSeparator();
                    contextMenu->addAction(thirdPartyTxUrlAction);
                    connect(thirdPartyTxUrlAction, SIGNAL(triggered()), mapperThirdPartyTxUrls, SLOT(map()));
                    mapperThirdPartyTxUrls->setMapping(thirdPartyTxUrlAction, listUrls[i].trimmed());
                }
            }
        }*/

        // Initial watch-only filter: match the default tab
        // (My Transactions tab = filter to non-watch-only entries).
        transactionProxyModel->setWatchOnlyFilter(
            (TransactionFilterProxy::WatchOnlyFilter)watchOnlyTabBar->tabData(watchOnlyTabBar->currentIndex()).toInt());

        // show/hide Watch-Only tab and column
        updateWatchOnlyColumn(model->haveWatchOnly());

        // Watch-only signal
        connect(model, SIGNAL(notifyWatchonlyChanged(bool)), this, SLOT(updateWatchOnlyColumn(bool)));
        
        // Update transaction list with persisted settings
        chooseType(settings.value("transactionType").toInt());
        chooseDate(settings.value("transactionDate").toInt());
    }
}

void TransactionView::chooseDate(int idx)
{
    if(!transactionProxyModel)
        return;
    QDate current = QDate::currentDate();
    dateRangeWidget->setVisible(false);
    switch(dateWidget->itemData(idx).toInt())
    {
    case All:
        transactionProxyModel->setDateRange(
                TransactionFilterProxy::MIN_DATE,
                TransactionFilterProxy::MAX_DATE);
        break;
    case Today:
        transactionProxyModel->setDateRange(
                current.startOfDay(),
                TransactionFilterProxy::MAX_DATE);
        break;
    case ThisWeek: {
        // Find last Monday
        QDate startOfWeek = current.addDays(-(current.dayOfWeek()-1));
        transactionProxyModel->setDateRange(
                startOfWeek.startOfDay(),
                TransactionFilterProxy::MAX_DATE);

        } break;
    case ThisMonth:
        transactionProxyModel->setDateRange(
                QDate(current.year(), current.month(), 1).startOfDay(),
                TransactionFilterProxy::MAX_DATE);
        break;
    case LastMonth:
        transactionProxyModel->setDateRange(
                QDate(current.year(), current.month()-1, 1).startOfDay(),
                QDate(current.year(), current.month(), 1).startOfDay());
        break;
    case ThisYear:
        transactionProxyModel->setDateRange(
                QDate(current.year(), 1, 1).startOfDay(),
                TransactionFilterProxy::MAX_DATE);
        break;
    case Range:
        dateRangeWidget->setVisible(true);
        dateRangeChanged();
        break;
    }
    // Persist settings
    if (dateWidget->itemData(idx).toInt() != Range){
        QSettings settings;
        settings.setValue("transactionDate", idx);
    }
}

void TransactionView::chooseType(int idx)
{
    if(!transactionProxyModel)
        return;
    transactionProxyModel->setTypeFilter(
        typeWidget->itemData(idx).toInt());
    // Persist settings
    QSettings settings;
    settings.setValue("transactionType", idx);
}

void TransactionView::onWatchOnlyTabChanged(int idx)
{
    if(!transactionProxyModel)
        return;
    transactionProxyModel->setWatchOnlyFilter(
        (TransactionFilterProxy::WatchOnlyFilter)watchOnlyTabBar->tabData(idx).toInt());

    // Visually differentiate the Watch-Only tab from My Transactions so
    // the user can tell at a glance which class of transactions they're
    // looking at.  Greyer base + greyer alternate keep alternating-row
    // readability while signalling "this is external/observed data, not
    // your own".  My Transactions tab uses default palette.
    if (idx == 1) // Watch-Only tab
    {
        if (fUseDarkTheme)
        {
            transactionView->setStyleSheet(
                "QTableView {"
                "  background-color: #2a2a2a;"
                "  alternate-background-color: #353535;"
                "  color: #aaaaaa;"
                "}"
                "QTableView::item:selected {"
                "  background-color: #4a4a5a;"
                "  color: #ffffff;"
                "}");
        }
        else
        {
            transactionView->setStyleSheet(
                "QTableView {"
                "  background-color: #ececec;"
                "  alternate-background-color: #dddddd;"
                "  color: #555555;"
                "}"
                "QTableView::item:selected {"
                "  background-color: #c0c0d0;"
                "  color: #000000;"
                "}");
        }
    }
    else // My Transactions tab -- restore default appearance
    {
        transactionView->setStyleSheet("");
    }
}

void TransactionView::changedPrefix(const QString &prefix)
{
    if(!transactionProxyModel)
        return;
    transactionProxyModel->setAddressPrefix(prefix);
}

void TransactionView::changedAmount(const QString &amount)
{
    if(!transactionProxyModel)
        return;
    CAmount amount_parsed = 0;

    // Replace "," by "." so DigitalNoteUnits::parse will not fail for users entering "," as decimal separator
    QString newAmount = amount;
    newAmount.replace(QString(","), QString("."));

    if(DigitalNoteUnits::parse(model->getOptionsModel()->getDisplayUnit(), newAmount, &amount_parsed))
    {
        transactionProxyModel->setMinAmount(amount_parsed);
    }
    else
    {
        transactionProxyModel->setMinAmount(0);
    }
}

void TransactionView::exportClicked()
{
    // CSV is currently the only supported format
    QString filename = GUIUtil::getSaveFileName(
            this,
            tr("Export Transaction Data"), QString(),
            tr("Comma separated file (*.csv)"));

    if (filename.isNull()) return;

    CSVModelWriter writer(filename);

    // name, column, role
    writer.setModel(transactionProxyModel);
    writer.addColumn(tr("Confirmed"), 0, TransactionTableModel::ConfirmedRole);
    if (model && model->haveWatchOnly())
        writer.addColumn(tr("Watch-only"), TransactionTableModel::Watchonly);
    writer.addColumn(tr("Date"), 0, TransactionTableModel::DateRole);
    writer.addColumn(tr("Type"), TransactionTableModel::Type, Qt::EditRole);
    writer.addColumn(tr("Label"), 0, TransactionTableModel::LabelRole);
    writer.addColumn(tr("Address"), 0, TransactionTableModel::AddressRole);
    writer.addColumn(DigitalNoteUnits::getAmountColumnTitle(model->getOptionsModel()->getDisplayUnit()), 0, TransactionTableModel::FormattedAmountRole);
    writer.addColumn(tr("ID"), 0, TransactionTableModel::TxIDRole);

    if(!writer.write()) {
        emit message(tr("Exporting Failed"), tr("There was an error trying to save the transaction history to %1.").arg(filename),
            CClientUIInterface::MSG_ERROR);
    } 
    else {
        emit message(tr("Exporting Successful"), tr("The transaction history was successfully saved to %1.").arg(filename),
            CClientUIInterface::MSG_INFORMATION);
    }
}

void TransactionView::contextualMenu(const QPoint &point)
{
    QModelIndex index = transactionView->indexAt(point);
    if(index.isValid())
    {
        contextMenu->exec(QCursor::pos());
    }
}

void TransactionView::copyAddress()
{
    GUIUtil::copyEntryData(transactionView, 0, TransactionTableModel::AddressRole);
}

void TransactionView::copyLabel()
{
    GUIUtil::copyEntryData(transactionView, 0, TransactionTableModel::LabelRole);
}

void TransactionView::copyAmount()
{
    GUIUtil::copyEntryData(transactionView, 0, TransactionTableModel::FormattedAmountRole);
}

void TransactionView::copyTxID()
{
    GUIUtil::copyEntryData(transactionView, 0, TransactionTableModel::TxIDRole);
}

void TransactionView::copySmsgInfo()
{
    QTableView *table = transactionView;
    QModelIndexList indexes = table->selectionModel()->selectedRows(TransactionTableModel::ToAddress);
    if(indexes.empty()){
        QMessageBox::information(this, tr("Nothing Selected"), tr("You must select an address from the list first."),
                              QMessageBox::Ok, QMessageBox::Ok);
    } else {
        std::string address = indexes[0].data().toString().toStdString();
        std::string publicKey;
        DigitalNote::SMSG::GetLocalPublicKey(address, publicKey);
        GUIUtil::setClipboard(QString::fromStdString(address + ":" + publicKey));
    }
}

void TransactionView::editLabel()
{
    if(!transactionView->selectionModel() ||!model)
        return;
    QModelIndexList selection = transactionView->selectionModel()->selectedRows();
    if(!selection.isEmpty())
    {
        AddressTableModel *addressBook = model->getAddressTableModel();
        if(!addressBook)
            return;
        QString address = selection.at(0).data(TransactionTableModel::AddressRole).toString();
        if(address.isEmpty())
        {
            // If this transaction has no associated address, exit
            return;
        }
        // Is address in address book? Address book can miss address when a transaction is
        // sent from outside the UI.
        int idx = addressBook->lookupAddress(address);
        if(idx != -1)
        {
            // Edit sending / receiving address
            QModelIndex modelIdx = addressBook->index(idx, 0, QModelIndex());
            // Determine type of address, launch appropriate editor dialog type
            QString type = modelIdx.data(AddressTableModel::TypeRole).toString();

            EditAddressDialog dlg(type==AddressTableModel::Receive
                                         ? EditAddressDialog::EditReceivingAddress
                                         : EditAddressDialog::EditSendingAddress,
                                  this);
            dlg.setModel(addressBook);
            dlg.loadRow(idx);
            dlg.exec();
        }
        else
        {
            // Add sending address
            EditAddressDialog dlg(EditAddressDialog::NewSendingAddress,
                                  this);
            dlg.setModel(addressBook);
            dlg.setAddress(address);
            dlg.exec();
        }
    }
}

void TransactionView::showDetails()
{
    if(!transactionView->selectionModel())
        return;
    QModelIndexList selection = transactionView->selectionModel()->selectedRows();
    if(!selection.isEmpty())
    {
        TransactionDescDialog dlg(selection.at(0));
        dlg.exec();
    }
}

/** Compute sum of all selected transactions */
void TransactionView::computeSum()
{
    qint64 amount = 0;
    int nDisplayUnit = model->getOptionsModel()->getDisplayUnit();
    if(!transactionView->selectionModel())
        return;
    QModelIndexList selection = transactionView->selectionModel()->selectedRows();

    foreach (QModelIndex index, selection){
        amount += index.data(TransactionTableModel::AmountRole).toLongLong();
    }
    QString strAmount(DigitalNoteUnits::formatWithUnit(nDisplayUnit, amount, true, DigitalNoteUnits::separatorAlways));
    if (amount < 0) strAmount = "<span style='color:red;'>" + strAmount + "</span>";
    emit trxAmount(strAmount);
}

QWidget *TransactionView::createDateRangeWidget()
{
    dateRangeWidget = new QFrame();
    dateRangeWidget->setFrameStyle(QFrame::Panel | QFrame::Raised);
    dateRangeWidget->setContentsMargins(1,1,1,1);
    QHBoxLayout *layout = new QHBoxLayout(dateRangeWidget);
    layout->setContentsMargins(0,0,0,0);
    layout->addSpacing(23);
    layout->addWidget(new QLabel(tr("Range:")));

    dateFrom = new QDateTimeEdit(this);
    dateFrom->setDisplayFormat("dd/MM/yy");
    dateFrom->setCalendarPopup(true);
    dateFrom->setMinimumWidth(100);
    dateFrom->setDate(QDate::currentDate().addDays(-7));
    layout->addWidget(dateFrom);
    layout->addWidget(new QLabel(tr("to")));

    dateTo = new QDateTimeEdit(this);
    dateTo->setDisplayFormat("dd/MM/yy");
    dateTo->setCalendarPopup(true);
    dateTo->setMinimumWidth(100);
    dateTo->setDate(QDate::currentDate());
    layout->addWidget(dateTo);
    layout->addStretch();

    // Hide by default
    dateRangeWidget->setVisible(false);

    // Notify on change
    connect(dateFrom, SIGNAL(dateChanged(QDate)), this, SLOT(dateRangeChanged()));
    connect(dateTo, SIGNAL(dateChanged(QDate)), this, SLOT(dateRangeChanged()));

    return dateRangeWidget;
}

void TransactionView::dateRangeChanged()
{
    if(!transactionProxyModel)
        return;
    transactionProxyModel->setDateRange(
            dateFrom->date().startOfDay(),
            dateTo->date().startOfDay().addDays(1));
}

void TransactionView::focusTransaction(const QModelIndex &idx)
{
    if(!transactionProxyModel)
        return;
    QModelIndex targetIdx = transactionProxyModel->mapFromSource(idx);
    transactionView->selectRow(targetIdx.row());
    computeSum();
    transactionView->scrollTo(targetIdx);
    transactionView->setCurrentIndex(targetIdx);
    transactionView->setFocus();
}

// We override the virtual resizeEvent of the QWidget to adjust tables column
// sizes as the tables width is proportional to the dialogs width.
void TransactionView::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    columnResizingFixer->stretchColumnWidth(TransactionTableModel::ToAddress);
}

// Need to override default Ctrl+C action for amount as default behaviour is just to copy DisplayRole text
bool TransactionView::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::KeyPress)
    {
        QKeyEvent *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_C && ke->modifiers().testFlag(Qt::ControlModifier))
        {
            QModelIndex i = this->transactionView->currentIndex();
            if (i.isValid() && i.column() == TransactionTableModel::Amount)
            {
                 GUIUtil::setClipboard(i.data(TransactionTableModel::FormattedAmountRole).toString());
                 return true;
            }
        }
    }
    return QWidget::eventFilter(obj, event);
}

// show/hide Watch-Only tab and column based on whether the wallet has
// any watch-only addresses
void TransactionView::updateWatchOnlyColumn(bool fHaveWatchOnly)
{
    // The Watch-Only tab (index 1) only makes sense when there's at
    // least one watch-only address.  When no watch-only present, hide
    // the tab; if it happened to be the active tab, force-switch to
    // My Transactions so the filter resolves to a sensible state.
    if (!fHaveWatchOnly)
    {
        if (watchOnlyTabBar->currentIndex() == 1)
        {
            watchOnlyTabBar->setCurrentIndex(0);
        }
        watchOnlyTabBar->setTabVisible(1, false);
    }
    else
    {
        watchOnlyTabBar->setTabVisible(1, true);
    }

    // Each tab is type-pure (only spendable OR only watch-only), so
    // the per-row eye-icon column is redundant.  Keep it hidden.
    transactionView->setColumnHidden(TransactionTableModel::Watchonly, true);
}