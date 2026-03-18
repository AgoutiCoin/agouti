// Copyright (c) 2017-2019 The Bulwark developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "proposaldialog.h"
#include "ui_proposaldialog.h"

#include "guiutil.h"
#include "main.h"
#include "masternode-budget.h"
#include "masternode-sync.h"
#include "utilstrencodings.h"
#include "wallet.h"

#include <QIntValidator>
#include <QMessageBox>
#include <QTimer>

ProposalDialog::ProposalDialog(Mode mode, QWidget* parent)
    : QDialog(parent, Qt::WindowSystemMenuHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint),
      ui(new Ui::ProposalDialog),
      mode(mode),
      timer(new QTimer(this)),
      counter(0)
{
    ui->setupUi(this);

    ui->nameEdit->setPlaceholderText(tr("Short name, max 20 characters"));
    ui->urlEdit->setPlaceholderText(tr("https://example.com/proposal"));
    ui->paymentsEdit->setPlaceholderText(tr("Number of monthly payments (1–100)"));
    ui->paymentsEdit->setValidator(new QIntValidator(1, 100, this));
    ui->blockEdit->setPlaceholderText(tr("Starting superblock height"));
    ui->blockEdit->setValidator(new QIntValidator(1, INT_MAX, this));
    ui->addressEdit->setPlaceholderText(tr("Payment address"));
    ui->amountEdit->setPlaceholderText(tr("Monthly amount (AGU)"));
    ui->amountEdit->setValidator(new QIntValidator(1, INT_MAX, this));
    ui->hashEdit->setPlaceholderText(tr("Collateral transaction ID"));

    // Pre-fill next superblock
    CBlockIndex* pindexPrev = chainActive.Tip();
    if (pindexPrev) {
        int nNext = pindexPrev->nHeight
                    - pindexPrev->nHeight % GetBudgetPaymentCycleBlocks()
                    + GetBudgetPaymentCycleBlocks();
        ui->blockEdit->setText(QString::number(nNext));
    }

    switch (mode) {
    case PrepareProposal:
        setWindowTitle(tr("Prepare Proposal"));
        ui->confirmLabel->setVisible(false);
        ui->hashEdit->setVisible(false);
        ui->hashLabel->setVisible(false);
        break;
    case SubmitProposal:
        setWindowTitle(tr("Submit Proposal"));
        ui->confirmLabel->setVisible(true);
        ui->hashEdit->setVisible(true);
        ui->hashLabel->setVisible(true);
        break;
    }

    connect(timer, SIGNAL(timeout()), this, SLOT(checkProposalTX()));
}

ProposalDialog::~ProposalDialog()
{
    timer->stop();
    delete ui;
}

bool ProposalDialog::validateProposal()
{
    std::string strError;

    if (!masternodeSync.IsBlockchainSynced())
        strError = "Wallet must be fully synchronised before submitting a proposal.";

    std::string strName = SanitizeString(ui->nameEdit->text().toStdString());
    if (strName.empty())
        strError = "Proposal name must not be empty.";
    else if (strName.size() > 20)
        strError = "Proposal name must not exceed 20 characters.";

    std::string strURL = SanitizeString(ui->urlEdit->text().toStdString());
    if (strURL.size() > 64)
        strError = "URL must not exceed 64 characters.";

    int nPaymentCount = ui->paymentsEdit->text().toInt();
    if (nPaymentCount < 1)
        strError = "Payment count must be at least 1.";

    CBlockIndex* pindexPrev = chainActive.Tip();
    if (pindexPrev) {
        int nBlockMin = pindexPrev->nHeight
                        - pindexPrev->nHeight % GetBudgetPaymentCycleBlocks()
                        + GetBudgetPaymentCycleBlocks();
        int nBlockStart = ui->blockEdit->text().toInt();
        if (nBlockStart < nBlockMin)
            strError = strprintf("Start block must be at least %d (next superblock).", nBlockMin);
        else if (nBlockStart % GetBudgetPaymentCycleBlocks() != 0)
            strError = strprintf("Start block must be a superblock. Next valid: %d", nBlockMin);
    }

    CBitcoinAddress address(ui->addressEdit->text().toStdString());
    if (!address.IsValid())
        strError = "Payment address is not a valid Agouti address.";

    if (!strError.empty()) {
        QMessageBox::critical(this, tr("Validation Error"), QString::fromStdString(strError));
        return false;
    }
    return true;
}

void ProposalDialog::prepareProposal()
{
    std::string strError;

    if (pwalletMain->IsLocked()) {
        QMessageBox::critical(this, tr("Wallet Locked"),
            tr("Please unlock the wallet before creating a proposal."));
        return;
    }

    std::string strName         = SanitizeString(ui->nameEdit->text().toStdString());
    std::string strURL          = SanitizeString(ui->urlEdit->text().toStdString());
    int         nPaymentCount   = ui->paymentsEdit->text().toInt();
    int         nBlockStart     = ui->blockEdit->text().toInt();
    CBitcoinAddress address(ui->addressEdit->text().toStdString());
    CScript     scriptPubKey    = GetScriptForDestination(address.Get());
    CAmount     nAmount         = ui->amountEdit->text().toLongLong() * COIN;

    CBudgetProposalBroadcast proposal(strName, strURL, nPaymentCount, scriptPubKey, nAmount, nBlockStart, 0);

    std::string err;
    if (!proposal.IsValid(err, false))
        strError = "Proposal is invalid: " + err;

    bool useIX = false;
    if (strError.empty() && !pwalletMain->GetBudgetSystemCollateralTX(wtx, proposal.GetHash(), useIX))
        strError = "Could not create collateral transaction. Check your balance (50 AGU required).";

    CReserveKey reservekey(pwalletMain);
    if (strError.empty() && !pwalletMain->CommitTransaction(wtx, reservekey, useIX ? "ix" : "tx"))
        strError = "Failed to commit collateral transaction.";

    if (!strError.empty()) {
        QMessageBox::critical(this, tr("Prepare Proposal Error"), QString::fromStdString(strError));
        return;
    }

    // Transition to submit mode
    ui->nameEdit->setDisabled(true);
    ui->urlEdit->setDisabled(true);
    ui->paymentsEdit->setDisabled(true);
    ui->blockEdit->setDisabled(true);
    ui->addressEdit->setDisabled(true);
    ui->amountEdit->setDisabled(true);
    ui->cancelButton->setDisabled(true);
    ui->acceptButton->setDisabled(true);
    ui->acceptButton->setText(tr("Waiting for confirmations…"));

    ui->hashEdit->setText(QString::fromStdString(wtx.GetHash().ToString()));
    ui->hashEdit->setVisible(true);
    ui->hashLabel->setVisible(true);
    ui->confirmLabel->setVisible(true);
    ui->confirmLabel->setText(tr("Waiting for confirmations…"));

    mode = SubmitProposal;
    setWindowTitle(tr("Submit Proposal"));
    counter = chainActive.Tip()->nHeight + 1;
    timer->start(1000);
}

void ProposalDialog::submitProposal()
{
    std::string strError;

    std::string strName       = SanitizeString(ui->nameEdit->text().toStdString());
    std::string strURL        = SanitizeString(ui->urlEdit->text().toStdString());
    int         nPaymentCount = ui->paymentsEdit->text().toInt();
    int         nBlockStart   = ui->blockEdit->text().toInt();
    CBitcoinAddress address(ui->addressEdit->text().toStdString());
    CScript     scriptPubKey  = GetScriptForDestination(address.Get());
    CAmount     nAmount       = ui->amountEdit->text().toLongLong() * COIN;
    uint256     hash          = uint256S(ui->hashEdit->text().toStdString());

    CBudgetProposalBroadcast proposal(strName, strURL, nPaymentCount, scriptPubKey, nAmount, nBlockStart, hash);

    int         nConf = 0;
    std::string err;
    if (!IsBudgetCollateralValid(hash, proposal.GetHash(), err, proposal.nTime, nConf))
        strError = "Collateral TX is not yet valid: " + err;

    if (strError.empty() && !budget.AddProposal(proposal))
        strError = "Failed to add proposal – see debug.log for details.";

    if (!strError.empty()) {
        QMessageBox::critical(this, tr("Submit Proposal Error"), QString::fromStdString(strError));
        return;
    }

    budget.mapSeenMasternodeBudgetProposals.insert(std::make_pair(proposal.GetHash(), proposal));
    proposal.Relay();

    QMessageBox::information(this, tr("Proposal Submitted"),
        tr("Proposal \"%1\" has been submitted to the network.")
            .arg(QString::fromStdString(strName)));

    this->accept();
}

void ProposalDialog::checkProposalTX()
{
    if (mode != SubmitProposal) return;

    int nConf  = static_cast<int>(Params().Budget_Fee_Confirmations());
    int nDepth = (chainActive.Tip()->nHeight + 1) - counter;

    if (nDepth > nConf) {
        ui->acceptButton->setDisabled(false);
        ui->acceptButton->setText(tr("Submit Proposal"));
        ui->confirmLabel->setText(tr("Collateral confirmed. Click Submit Proposal to proceed."));
        timer->stop();
    } else if (nDepth > 0) {
        ui->confirmLabel->setText(tr("%1 of %2 confirmations…").arg(nDepth).arg(nConf + 1));
    }
}

void ProposalDialog::on_acceptButton_clicked()
{
    if (!validateProposal()) return;

    if (mode == PrepareProposal)
        prepareProposal();
    else if (mode == SubmitProposal)
        submitProposal();
}

void ProposalDialog::on_cancelButton_clicked()
{
    this->reject();
}
