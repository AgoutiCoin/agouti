// Copyright (c) 2018 The Phore developers
// Copyright (c) 2018 The Curium developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "configuremasternodepage.h"
#include "ui_configuremasternodepage.h"

#include "activemasternode.h"
#include "base58.h"
#include "key.h"
#include "masternodeconfig.h"
#include "masternodeman.h"
#include "wallet.h"

#include <QMessageBox>
#include <QString>
#include <boost/foreach.hpp>
#include <string>
#include <vector>

ConfigureMasternodePage::ConfigureMasternodePage(Mode mode, QWidget* parent)
    : QDialog(parent, Qt::WindowSystemMenuHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint),
      ui(new Ui::ConfigureMasternodePage),
      mapper(0),
      mode(mode),
      counters(0)
{
    ui->setupUi(this);

    switch (mode) {
    case NewConfigureMasternode:
        setWindowTitle(tr("New Masternode"));
        break;
    case EditConfigureMasternode:
        setWindowTitle(tr("Edit Masternode"));
        break;
    }
}

ConfigureMasternodePage::~ConfigureMasternodePage()
{
    delete ui;
}

void ConfigureMasternodePage::loadAlias(QString strAlias)    { ui->aliasEdit->setText(strAlias); }
void ConfigureMasternodePage::loadIP(QString strIP)          { ui->vpsIpEdit->setText(strIP); }
void ConfigureMasternodePage::loadPrivKey(QString strPrivKey) { ui->privKeyEdit->setText(strPrivKey); }
void ConfigureMasternodePage::loadTxHash(QString strTxHash)  { ui->outputEdit->setText(strTxHash); }
void ConfigureMasternodePage::loadOutputIndex(QString strOutputIndex) { ui->outputIdEdit->setText(strOutputIndex); }

void ConfigureMasternodePage::counter(int counter)         { setCounters(counter); }
void ConfigureMasternodePage::MNAliasCache(QString MnAliasCache) { setMnAliasCache(MnAliasCache); }

void ConfigureMasternodePage::saveCurrentRow()
{
    if (ui->aliasEdit->text().isEmpty() || ui->vpsIpEdit->text().isEmpty() ||
        ui->privKeyEdit->text().isEmpty() || ui->outputEdit->text().isEmpty() ||
        ui->outputIdEdit->text().isEmpty()) {
        QMessageBox::warning(this, tr("Missing Fields"), tr("All fields must be filled in."));
        return;
    }

    switch (mode) {
    case NewConfigureMasternode:
        masternodeConfig.add(
            ui->aliasEdit->text().toStdString(),
            ui->vpsIpEdit->text().toStdString(),
            ui->privKeyEdit->text().toStdString(),
            ui->outputEdit->text().toStdString(),
            ui->outputIdEdit->text().toStdString());
        masternodeConfig.writeToMasternodeConf();
        break;
    case EditConfigureMasternode:
        updateAlias(
            ui->aliasEdit->text().toStdString(),
            ui->vpsIpEdit->text().toStdString(),
            ui->privKeyEdit->text().toStdString(),
            ui->outputEdit->text().toStdString(),
            ui->outputIdEdit->text().toStdString(),
            getMnAliasCache().toStdString());
        break;
    }
}

void ConfigureMasternodePage::updateAlias(std::string Alias, std::string IP, std::string PrivKey, std::string TxHash, std::string OutputIndex, std::string mnAlias)
{
    int count = 0;
    BOOST_FOREACH (CMasternodeConfig::CMasternodeEntry mne, masternodeConfig.getEntries()) {
        count++;
        if (mnAlias == mne.getAlias()) {
            uint256 mnTxHash;
            mnTxHash.SetHex(mne.getTxHash());
            int nIndex;
            if (!mne.castOutputIndex(nIndex))
                continue;
            COutPoint outpoint = COutPoint(mnTxHash, nIndex);
            pwalletMain->UnlockCoin(outpoint);

            masternodeConfig.deleteAlias(count);
            masternodeConfig.add(Alias, IP, PrivKey, TxHash, OutputIndex);
            masternodeConfig.writeToMasternodeConf();
            break;
        }
    }
}

void ConfigureMasternodePage::on_acceptButton_clicked()
{
    saveCurrentRow();
    emit accepted();
    QDialog::accept();
}

void ConfigureMasternodePage::on_cancelButton_clicked()
{
    this->close();
}

void ConfigureMasternodePage::on_AutoFillPrivKey_clicked()
{
    CKey secret;
    secret.MakeNewKey(false);
    ui->privKeyEdit->setText(QString::fromStdString(CBitcoinSecret(secret).ToString()));
}

void ConfigureMasternodePage::on_AutoFillOutputs_clicked()
{
    std::vector<COutput> possibleCoins = activeMasternode.SelectCoinsMasternode();

    BOOST_FOREACH (COutput& out, possibleCoins) {
        std::string txHash  = out.tx->GetHash().ToString();
        std::string outputId = std::to_string(out.i);

        // Skip outputs already in masternode.conf
        bool alreadyUsed = false;
        BOOST_FOREACH (CMasternodeConfig::CMasternodeEntry mne, masternodeConfig.getEntries()) {
            if (outputId == mne.getOutputIndex() && txHash == mne.getTxHash()) {
                alreadyUsed = true;
                break;
            }
        }
        if (alreadyUsed)
            continue;

        ui->outputEdit->setText(QString::fromStdString(txHash));
        ui->outputIdEdit->setText(QString::fromStdString(outputId));
        break;
    }
}
