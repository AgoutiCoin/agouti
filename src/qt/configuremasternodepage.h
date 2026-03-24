// Copyright (c) 2018 The Phore developers
// Copyright (c) 2018 The Curium developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_CONFIGUREMASTERNODEPAGE_H
#define BITCOIN_QT_CONFIGUREMASTERNODEPAGE_H

#include <QDialog>
#include <QDataWidgetMapper>
#include <QString>
#include <string>

namespace Ui {
class ConfigureMasternodePage;
}

/** Dialog for creating or editing a masternode.conf entry. */
class ConfigureMasternodePage : public QDialog
{
    Q_OBJECT

public:
    enum Mode {
        NewConfigureMasternode,
        EditConfigureMasternode
    };

    explicit ConfigureMasternodePage(Mode mode, QWidget* parent);
    ~ConfigureMasternodePage();

    void counter(int counter);
    void MNAliasCache(QString MnAliasCache);
    void loadAlias(QString strAlias);
    void loadIP(QString strIP);
    void loadPrivKey(QString strPrivKey);
    void loadTxHash(QString strTxHash);
    void loadOutputIndex(QString strOutputIndex);
    void updateAlias(std::string Alias, std::string IP, std::string PrivKey, std::string TxHash, std::string OutputIndex, std::string mnAlias);

    int getCounters() { return counters; }
    void setCounters(int counter) { counters = counter; }
    QString getMnAliasCache() { return mnAliasCache; }
    void setMnAliasCache(QString mnAliasCaches) { mnAliasCache = mnAliasCaches; }

public slots:
    void on_acceptButton_clicked();
    void on_cancelButton_clicked();
    void on_AutoFillPrivKey_clicked();
    void on_AutoFillOutputs_clicked();

private:
    bool validateFields();
    void saveCurrentRow();

    int counters;
    QString mnAliasCache;
    Ui::ConfigureMasternodePage* ui;
    QDataWidgetMapper* mapper;
    Mode mode;
};

#endif // BITCOIN_QT_CONFIGUREMASTERNODEPAGE_H
