// Copyright (c) 2024-2026 DigitalNote developers
// Distributed under the MIT software license.
// SPDX-License-Identifier: MIT
//
// removewatchonlydialog.h -- modal for managing watch-only addresses
// Lists all watch-only entries with checkboxes; bulk-removes the
// selected ones via a background worker.

#pragma once

#include <vector>

#include <QDialog>

#include "walletmodel.h"

class QTableWidget;
class QPushButton;
class QLabel;

class RemoveWatchOnlyDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RemoveWatchOnlyDialog(WalletModel *model, QWidget *parent = nullptr);
    ~RemoveWatchOnlyDialog();

private slots:
    void onRemoveClicked();
    void onSelectAllClicked();
    void onDeselectAllClicked();
    void onSelectionChanged();

private:
    void buildUi();
    void populateTable();
    void updateSelectionLabel();
    std::vector<CScript> collectCheckedScripts() const;

    WalletModel                      *m_model;
    std::vector<WalletModel::WatchOnlyEntry> m_entries;

    QTableWidget   *m_table;
    QLabel         *m_selectionCountLabel;
    QPushButton    *m_removeButton;
    QPushButton    *m_cancelButton;
    QPushButton    *m_selectAllButton;
    QPushButton    *m_deselectAllButton;
};
