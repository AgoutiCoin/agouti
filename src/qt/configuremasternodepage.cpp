// Copyright (c) 2018 The Phore developers
// Copyright (c) 2018 The Curium developers
// Copyright (c) 2026 The Agouti developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "configuremasternodepage.h"
#include "ui_configuremasternodepage.h"

#include "activemasternode.h"
#include "base58.h"
#include "chainparams.h"
#include "key.h"
#include "masternodeconfig.h"
#include "masternodeman.h"
#include "net.h"
#include "netbase.h"
#include "wallet.h"

#include <QMessageBox>
#include <QString>
#include <boost/foreach.hpp>
#include <cstring>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Error-mapping table for user-friendly masternode configuration diagnostics
// Adapted from the Omega error-explanation pattern for the basic MN system.
// ---------------------------------------------------------------------------
struct MnErrorMapping {
    const char* code;
    const char* message;
    const char* solution;
};

static const MnErrorMapping kMnErrors[] = {
    // Alias
    {"MN-101",
     "The alias is empty.",
     "Enter a short name for the masternode (e.g. mn1)."},

    {"MN-102",
     "An alias with this name already exists in masternode.conf.",
     "Choose a unique alias, or edit the existing entry instead."},

    // IP / port
    {"MN-201",
     "The VPS IP:Port field is empty.",
     "Enter the public IP address and port of your masternode VPS (e.g. 1.2.3.4:5151)."},

    {"MN-202",
     "The port is not valid for this network.",
     "Use port 5151 for mainnet."},

    {"MN-203",
     "The IP address does not appear to be a valid public IPv4 address.",
     "Enter a publicly reachable IP, not a LAN or localhost address."},

    // Private key
    {"MN-301",
     "The masternode private key is empty.",
     "Click 'Generate Key' or paste an existing masternode private key."},

    {"MN-302",
     "The masternode private key is not a valid Base58 secret.",
     "Generate a new key with 'Generate Key', or check that you copied the key correctly."},

    // Collateral output
    {"MN-401",
     "The collateral output TxID is empty.",
     "Click 'Auto-fill Output' or paste the transaction hash of a confirmed collateral output."},

    {"MN-402",
     "The collateral TxID is not a valid 64-character hexadecimal hash.",
     "Copy the full transaction hash from the transaction list or from the debug console."},

    {"MN-403",
     "The output index is empty.",
     "Click 'Auto-fill Output' or enter the output index (usually 0 or 1)."},

    {"MN-404",
     "The output index is not a valid non-negative integer.",
     "Enter the numeric index of the collateral output (e.g. 0)."},

    {"MN-405",
     "No unused collateral output was found in your wallet.",
     "Send exactly the required collateral amount to yourself and wait for it to confirm."},

    // File I/O
    {"MN-501",
     "Failed to write masternode.conf to disk.",
     "Check that the data directory is writable and has enough space."},
};

static QString formatError(const char* code, const char* message, const char* solution)
{
    return QObject::tr("[%1] %2\n\nSuggestion: %3")
        .arg(QLatin1String(code), QObject::tr(message), QObject::tr(solution));
}

static const MnErrorMapping* findError(const char* code)
{
    for (size_t i = 0; i < sizeof(kMnErrors) / sizeof(kMnErrors[0]); ++i)
        if (std::strcmp(kMnErrors[i].code, code) == 0)
            return &kMnErrors[i];
    return NULL;
}

static void showMnError(QWidget* parent, const char* code)
{
    const MnErrorMapping* e = findError(code);
    if (!e) return;
    QMessageBox::warning(parent, QObject::tr("Masternode Configuration"),
                         formatError(e->code, e->message, e->solution));
}

// ---------------------------------------------------------------------------

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

bool ConfigureMasternodePage::validateFields()
{
    // --- Alias ---
    std::string alias = ui->aliasEdit->text().trimmed().toStdString();
    if (alias.empty()) {
        showMnError(this, "MN-101");
        return false;
    }
    // Check for duplicates (only in New mode)
    if (mode == NewConfigureMasternode) {
        BOOST_FOREACH (const CMasternodeConfig::CMasternodeEntry& mne, masternodeConfig.getEntries()) {
            if (alias == mne.getAlias()) {
                showMnError(this, "MN-102");
                return false;
            }
        }
    }

    // --- IP:Port ---
    std::string ip = ui->vpsIpEdit->text().trimmed().toStdString();
    if (ip.empty()) {
        showMnError(this, "MN-201");
        return false;
    }
    CService addr(ip);
    if (Params().NetworkID() == CBaseChainParams::MAIN && addr.GetPort() != 5151) {
        showMnError(this, "MN-202");
        return false;
    }
    if (!addr.IsRoutable()) {
        showMnError(this, "MN-203");
        return false;
    }

    // --- Private key ---
    std::string privKey = ui->privKeyEdit->text().trimmed().toStdString();
    if (privKey.empty()) {
        showMnError(this, "MN-301");
        return false;
    }
    CBitcoinSecret secret;
    if (!secret.SetString(privKey)) {
        showMnError(this, "MN-302");
        return false;
    }

    // --- TxHash ---
    std::string txHash = ui->outputEdit->text().trimmed().toStdString();
    if (txHash.empty()) {
        showMnError(this, "MN-401");
        return false;
    }
    if (txHash.size() != 64 || txHash.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) {
        showMnError(this, "MN-402");
        return false;
    }

    // --- Output index ---
    std::string outputIdx = ui->outputIdEdit->text().trimmed().toStdString();
    if (outputIdx.empty()) {
        showMnError(this, "MN-403");
        return false;
    }
    try {
        int idx = std::stoi(outputIdx);
        if (idx < 0) throw std::out_of_range("negative");
    } catch (...) {
        showMnError(this, "MN-404");
        return false;
    }

    return true;
}

void ConfigureMasternodePage::saveCurrentRow()
{
    if (!validateFields())
        return;

    switch (mode) {
    case NewConfigureMasternode:
        masternodeConfig.add(
            ui->aliasEdit->text().trimmed().toStdString(),
            ui->vpsIpEdit->text().trimmed().toStdString(),
            ui->privKeyEdit->text().trimmed().toStdString(),
            ui->outputEdit->text().trimmed().toStdString(),
            ui->outputIdEdit->text().trimmed().toStdString());
        if (!masternodeConfig.writeToMasternodeConf()) {
            showMnError(this, "MN-501");
            return;
        }
        break;
    case EditConfigureMasternode:
        updateAlias(
            ui->aliasEdit->text().trimmed().toStdString(),
            ui->vpsIpEdit->text().trimmed().toStdString(),
            ui->privKeyEdit->text().trimmed().toStdString(),
            ui->outputEdit->text().trimmed().toStdString(),
            ui->outputIdEdit->text().trimmed().toStdString(),
            getMnAliasCache().toStdString());
        break;
    }
}

void ConfigureMasternodePage::updateAlias(std::string Alias, std::string IP, std::string PrivKey, std::string TxHash, std::string OutputIndex, std::string mnAlias)
{
    int index = 0;
    BOOST_FOREACH (CMasternodeConfig::CMasternodeEntry mne, masternodeConfig.getEntries()) {
        if (mnAlias == mne.getAlias()) {
            uint256 mnTxHash;
            mnTxHash.SetHex(mne.getTxHash());
            int nIndex;
            if (!mne.castOutputIndex(nIndex)) {
                index++;
                continue;
            }
            COutPoint outpoint = COutPoint(mnTxHash, nIndex);
            pwalletMain->UnlockCoin(outpoint);

            masternodeConfig.deleteAlias(index);
            masternodeConfig.add(Alias, IP, PrivKey, TxHash, OutputIndex);
            if (!masternodeConfig.writeToMasternodeConf())
                showMnError(this, "MN-501");
            break;
        }
        index++;
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
        BOOST_FOREACH (const CMasternodeConfig::CMasternodeEntry& mne, masternodeConfig.getEntries()) {
            if (outputId == mne.getOutputIndex() && txHash == mne.getTxHash()) {
                alreadyUsed = true;
                break;
            }
        }
        if (alreadyUsed)
            continue;

        ui->outputEdit->setText(QString::fromStdString(txHash));
        ui->outputIdEdit->setText(QString::fromStdString(outputId));
        return;
    }

    showMnError(this, "MN-405");
}
