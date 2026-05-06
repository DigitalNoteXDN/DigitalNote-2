#ifndef OPTIONSMODEL_H
#define OPTIONSMODEL_H

#include <QAbstractListModel>

#include "types/camount.h"

extern bool fUseDarkTheme;

/** Interface from Qt to configuration data structure for DigitalNote client.
   To Qt, the options are presented as a list with the different options
   laid out vertically.
   This can be changed to a tree once the settings become sufficiently
   complex.
 */
class OptionsModel : public QAbstractListModel
{
    Q_OBJECT

public:
    explicit OptionsModel(QObject *parent = 0);

    enum OptionID {
        StartAtStartup,         // bool
        MinimizeToTray,         // bool
        MapPortUPnP,            // bool
        MinimizeOnClose,        // bool
        Fee,                    // qint64
        ReserveBalance,         // qint64
        DisplayUnit,            // DigitalNoteUnits::Unit
        Language,               // QString
        CoinControlFeatures,    // bool
        UseDarkTheme,     // bool
        MNengineRounds,    // int
        AnonymizeDigitalNoteAmount, //int
        OptionIDRowCount,
    };

    void Init();
    void Reset();

    int rowCount(const QModelIndex & parent = QModelIndex()) const;
    QVariant data(const QModelIndex & index, int role = Qt::DisplayRole) const;
    bool setData(const QModelIndex & index, const QVariant & value, int role = Qt::EditRole);

    /* Explicit getters */
    qint64 getReserveBalance();
    bool getMinimizeToTray() { return fMinimizeToTray; }
    bool getMinimizeOnClose() { return fMinimizeOnClose; }
    int getDisplayUnit() { return nDisplayUnit; }
    bool getCoinControlFeatures() { return fCoinControlFeatures; }
    const QString& getOverriddenByCommandLine() { return strOverriddenByCommandLine; }

    /* Restart flag helper */
    void setRestartRequired(bool fRequired);
    bool isRestartRequired();
private:
    /* Qt-only settings */
    bool fMinimizeToTray;
    bool fMinimizeOnClose;
    QString language;
    int nDisplayUnit;
    bool fCoinControlFeatures;
    /* settings that were overriden by command-line */
    QString strOverriddenByCommandLine;

    /// Add option to list of GUI options overridden through command line/config file
    void addOverriddenOption(const std::string &option);

signals:
    void displayUnitChanged(int unit);
    // NOTE: declared as CAmount (not qint64) to match the connect at
    // sendcoinsdialog.cpp.  Qt's string-based old-style connect compares
    // the literal type names; "CAmount" and "qint64" don't match even
    // though both are int64_t typedefs, and the failed connect was
    // logged at startup as a "no such signal" warning.
    void transactionFeeChanged(CAmount);
    void reserveBalanceChanged(qint64);
    void coinControlFeaturesChanged(bool);
    void mnengineRoundsChanged(int);
    void AnonymizeDigitalNoteAmountChanged(int);
};

#endif // OPTIONSMODEL_H
