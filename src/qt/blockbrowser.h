#ifndef BLOCKBROWSER_H
#define BLOCKBROWSER_H

#include <cstdint>
#include <QWidget>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTime>
#include <QTimer>
#include <QStringList>
#include <QMap>
#include <QSettings>
#include <QSlider>

#include "clientmodel.h"
#include "walletmodel.h"

double getBlockHardness(int);
double getTxTotalValue(const std::string&);
double convertCoins(int64_t);
double getTxFees(const std::string&);
int getBlockTime(int);
int getBlocknBits(int);
int getBlockNonce(int);
int blocksInPastHours(int);
int getBlockHashrate(int);
std::string getInputs(const std::string &);
std::string getOutputs(const std::string &);
std::string getBlockHash(int);
std::string getBlockMerkle(int);
bool addnode(std::string);
const CBlockIndex* getBlockIndex(int);
int64_t getInputValue(CTransaction, CScript);


namespace Ui {
class BlockBrowser;
}
class WalletModel;

class BlockBrowser : public QWidget
{
    Q_OBJECT

public:
    explicit BlockBrowser(QWidget *parent = 0);
    ~BlockBrowser();
    
    void setModel(WalletModel *model);
    
public slots:
    
    void blockClicked();
    void txClicked();
    void updateExplorer(bool);

private slots:

private:
    Ui::BlockBrowser *ui;
    WalletModel *model;
    
};

#endif // BLOCKBROWSER_H