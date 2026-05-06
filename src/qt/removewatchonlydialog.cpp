// Copyright (c) 2024-2026 DigitalNote developers
// Distributed under the MIT software license.
// SPDX-License-Identifier: MIT

#include "removewatchonlydialog.h"
#include "watchonlyworker.h"

#include <QApplication>
#include <QBrush>
#include <QCheckBox>
#include <QColor>
#include <QEventLoop>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QThread>
#include <QVBoxLayout>

extern bool fUseDarkTheme;

RemoveWatchOnlyDialog::RemoveWatchOnlyDialog(WalletModel *model, QWidget *parent)
    : QDialog(parent)
    , m_model(model)
    , m_table(nullptr)
    , m_selectionCountLabel(nullptr)
    , m_removeButton(nullptr)
    , m_cancelButton(nullptr)
    , m_selectAllButton(nullptr)
    , m_deselectAllButton(nullptr)
{
    setWindowTitle(tr("Manage Watch-Only Addresses"));
    setModal(true);
    setMinimumWidth(560);
    setMinimumHeight(380);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    buildUi();
    populateTable();
    updateSelectionLabel();
}

RemoveWatchOnlyDialog::~RemoveWatchOnlyDialog()
{
}

void RemoveWatchOnlyDialog::buildUi()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 14, 16, 14);
    mainLayout->setSpacing(10);

    // Title
    QLabel *titleLabel = new QLabel(tr("Watch-Only Addresses"), this);
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(titleFont.pointSize() + 2);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    mainLayout->addWidget(titleLabel);

    // Description
    QLabel *descLabel = new QLabel(
        tr("Select watch-only addresses to stop tracking. Removing an address only "
           "stops the wallet from monitoring it — it does not affect any funds."),
        this);
    descLabel->setWordWrap(true);
    if (fUseDarkTheme)
        descLabel->setStyleSheet("color: #b0b0b0;");
    else
        descLabel->setStyleSheet("color: #555555;");
    mainLayout->addWidget(descLabel);

    // Toolbar row: Select all / Deselect all / spacer / count
    QHBoxLayout *toolbarLayout = new QHBoxLayout();
    toolbarLayout->setSpacing(8);

    m_selectAllButton = new QPushButton(tr("Select All"), this);
    m_selectAllButton->setFlat(true);
    m_selectAllButton->setCursor(Qt::PointingHandCursor);
    connect(m_selectAllButton, &QPushButton::clicked,
            this, &RemoveWatchOnlyDialog::onSelectAllClicked);

    m_deselectAllButton = new QPushButton(tr("Deselect All"), this);
    m_deselectAllButton->setFlat(true);
    m_deselectAllButton->setCursor(Qt::PointingHandCursor);
    connect(m_deselectAllButton, &QPushButton::clicked,
            this, &RemoveWatchOnlyDialog::onDeselectAllClicked);

    m_selectionCountLabel = new QLabel(this);
    m_selectionCountLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    toolbarLayout->addWidget(m_selectAllButton);
    toolbarLayout->addWidget(m_deselectAllButton);
    toolbarLayout->addStretch(1);
    toolbarLayout->addWidget(m_selectionCountLabel);
    mainLayout->addLayout(toolbarLayout);

    // Table
    m_table = new QTableWidget(this);
    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels(QStringList()
                                       << QString()      // checkbox column header
                                       << tr("Label")
                                       << tr("Address"));
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setShowGrid(false);
    m_table->setAlternatingRowColors(true);
    m_table->setFocusPolicy(Qt::NoFocus);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_table->setColumnWidth(0, 32);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    mainLayout->addWidget(m_table, 1);

    // Buttons
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(8);

    m_cancelButton = new QPushButton(tr("Cancel"), this);
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);

    m_removeButton = new QPushButton(tr("Remove Selected"), this);
    m_removeButton->setDefault(true);
    connect(m_removeButton, &QPushButton::clicked,
            this, &RemoveWatchOnlyDialog::onRemoveClicked);

    buttonLayout->addStretch(1);
    buttonLayout->addWidget(m_cancelButton);
    buttonLayout->addWidget(m_removeButton);
    mainLayout->addLayout(buttonLayout);
}

void RemoveWatchOnlyDialog::populateTable()
{
    m_entries = m_model->getWatchOnlyEntries();

    m_table->setRowCount(static_cast<int>(m_entries.size()));

    for (int row = 0; row < static_cast<int>(m_entries.size()); ++row)
    {
        const WalletModel::WatchOnlyEntry &entry = m_entries[row];

        // Column 0: checkbox (centered in cell via container widget)
        QWidget *checkContainer = new QWidget(m_table);
        QHBoxLayout *checkLayout = new QHBoxLayout(checkContainer);
        QCheckBox *checkBox = new QCheckBox(checkContainer);
        checkBox->setChecked(false);
        connect(checkBox, &QCheckBox::stateChanged,
                this, &RemoveWatchOnlyDialog::onSelectionChanged);
        checkLayout->addWidget(checkBox);
        checkLayout->setAlignment(Qt::AlignCenter);
        checkLayout->setContentsMargins(0, 0, 0, 0);
        m_table->setCellWidget(row, 0, checkContainer);

        // Column 1: label (or "(no label)" if empty)
        QString labelText = entry.label.isEmpty() ? tr("(no label)") : entry.label;
        QTableWidgetItem *labelItem = new QTableWidgetItem(labelText);
        if (entry.label.isEmpty())
        {
            QFont f = labelItem->font();
            f.setItalic(true);
            labelItem->setFont(f);
            labelItem->setForeground(QBrush(fUseDarkTheme ? QColor(150, 150, 150)
                                                          : QColor(120, 120, 120)));
        }
        m_table->setItem(row, 1, labelItem);

        // Column 2: address (monospace would be nice, but consistent with rest of app)
        QTableWidgetItem *addrItem = new QTableWidgetItem(entry.displayAddress);
        addrItem->setToolTip(entry.displayAddress);
        m_table->setItem(row, 2, addrItem);
    }

    m_table->resizeRowsToContents();
}

void RemoveWatchOnlyDialog::updateSelectionLabel()
{
    int total = static_cast<int>(m_entries.size());
    int selected = 0;

    for (int row = 0; row < total; ++row)
    {
        QWidget *container = m_table->cellWidget(row, 0);
        if (!container)
            continue;
        QCheckBox *box = container->findChild<QCheckBox*>();
        if (box && box->isChecked())
            ++selected;
    }

    m_selectionCountLabel->setText(tr("%1 of %2 selected").arg(selected).arg(total));
    m_removeButton->setEnabled(selected > 0);

    if (selected > 0)
        m_removeButton->setText(tr("Remove Selected (%1)").arg(selected));
    else
        m_removeButton->setText(tr("Remove Selected"));
}

void RemoveWatchOnlyDialog::onSelectAllClicked()
{
    for (int row = 0; row < m_table->rowCount(); ++row)
    {
        QWidget *container = m_table->cellWidget(row, 0);
        if (!container)
            continue;
        QCheckBox *box = container->findChild<QCheckBox*>();
        if (box)
            box->setChecked(true);
    }
    // updateSelectionLabel is triggered via stateChanged signals
}

void RemoveWatchOnlyDialog::onDeselectAllClicked()
{
    for (int row = 0; row < m_table->rowCount(); ++row)
    {
        QWidget *container = m_table->cellWidget(row, 0);
        if (!container)
            continue;
        QCheckBox *box = container->findChild<QCheckBox*>();
        if (box)
            box->setChecked(false);
    }
}

void RemoveWatchOnlyDialog::onSelectionChanged()
{
    updateSelectionLabel();
}

std::vector<CScript> RemoveWatchOnlyDialog::collectCheckedScripts() const
{
    std::vector<CScript> result;

    for (int row = 0; row < static_cast<int>(m_entries.size()); ++row)
    {
        QWidget *container = m_table->cellWidget(row, 0);
        if (!container)
            continue;
        QCheckBox *box = container->findChild<QCheckBox*>();
        if (box && box->isChecked())
            result.push_back(m_entries[row].script);
    }

    return result;
}

void RemoveWatchOnlyDialog::onRemoveClicked()
{
    std::vector<CScript> toRemove = collectCheckedScripts();
    if (toRemove.empty())
        return; // shouldn't happen -- button should be disabled

    int count = static_cast<int>(toRemove.size());

    // Confirm
    QMessageBox::StandardButton confirm = QMessageBox::question(
        this,
        tr("Confirm removal"),
        tr("Stop tracking %1 watch-only address(es)?\n\n"
           "This will remove the selected addresses from the wallet's "
           "tracking set. The action does not affect any funds.").arg(count),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);

    if (confirm != QMessageBox::Yes)
        return;

    // Disable controls during the worker run
    m_removeButton->setEnabled(false);
    m_cancelButton->setEnabled(false);
    m_selectAllButton->setEnabled(false);
    m_deselectAllButton->setEnabled(false);
    m_table->setEnabled(false);

    // Progress dialog -- modal to this dialog only
    QProgressDialog progress(tr("Removing watch-only addresses..."), QString(),
                             0, count, this);
    progress.setWindowTitle(tr("Please wait"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumWidth(420);
    progress.setWindowFlags(progress.windowFlags() & ~Qt::WindowContextHelpButtonHint);
    progress.setMinimumDuration(0);
    progress.setCancelButton(nullptr); // can't cancel mid-batch
    progress.setValue(0);
    progress.show();
    QApplication::processEvents();

    // Spawn worker
    QThread *thread = new QThread(this);
    WatchOnlyWorker *worker = new WatchOnlyWorker(m_model, toRemove);
    worker->moveToThread(thread);

    int finalSucceeded = 0;
    int finalFailed = 0;
    QString finalError;

    connect(thread, &QThread::started, worker, &WatchOnlyWorker::run);
    // CRITICAL: the lambda connects MUST specify `this` (the dialog) as
    // their context QObject.  Without it, Qt connects with
    // Qt::DirectConnection and the lambdas run on the emitter's thread
    // (the worker thread).  The progress lambda calls
    // QApplication::processEvents() which must only run on the GUI
    // thread -- calling it from a worker thread is UB and was the
    // intermittent crash source during watch-only removal.
    connect(worker, &WatchOnlyWorker::progress, this,
            [&](int cur, int total, QString label) {
                progress.setRange(0, total);
                progress.setValue(cur);
                progress.setLabelText(label);
                // No processEvents() call needed -- we're already on the
                // GUI thread (signal arrived via Qt::QueuedConnection
                // from the worker's emit) and the running QEventLoop
                // below will repaint between events.
            });
    connect(worker, &WatchOnlyWorker::finished, this,
            [&](int succeeded, int failed, QString errorSummary) {
                finalSucceeded = succeeded;
                finalFailed = failed;
                finalError = errorSummary;
                thread->quit();
            });
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);

    // Use a local QEventLoop tied to the worker thread's finished signal
    // instead of a busy-wait `while (thread->isRunning()) processEvents()`.
    // The event loop lets Qt drain timers, repaints, and progress signals
    // naturally -- so the dialog stays interactive (mouse hover, drag,
    // close-button hover) during the long wait.  exec() returns when
    // quit() is called from the &QThread::finished connection below.
    QEventLoop waitLoop;
    connect(thread, &QThread::finished, &waitLoop, &QEventLoop::quit);

    thread->start();
    waitLoop.exec();
    progress.close();

    // Result dialog
    if (finalFailed == 0)
    {
        QMessageBox::information(
            this,
            tr("Done"),
            tr("Removed %1 watch-only address(es).").arg(finalSucceeded));
        accept();
    }
    else
    {
        QMessageBox::warning(
            this,
            tr("Partial removal"),
            tr("Removed %1 of %2 watch-only address(es).\n%3")
                .arg(finalSucceeded)
                .arg(finalSucceeded + finalFailed)
                .arg(finalError));
        // Refresh the dialog so user sees what's left
        populateTable();
        m_removeButton->setEnabled(false); // no selection until user picks again
        m_cancelButton->setEnabled(true);
        m_selectAllButton->setEnabled(true);
        m_deselectAllButton->setEnabled(true);
        m_table->setEnabled(true);
        updateSelectionLabel();
    }
}
