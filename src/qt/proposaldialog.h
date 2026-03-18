// Copyright (c) 2017-2019 The Bulwark developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef AGOUTI_QT_PROPOSALDIALOG_H
#define AGOUTI_QT_PROPOSALDIALOG_H

#include "wallet.h"

#include <QDialog>
#include <QTimer>

namespace Ui {
class ProposalDialog;
}

/** Two-stage dialog: first prepares the collateral TX, then submits the proposal
 *  once the TX is sufficiently confirmed. */
class ProposalDialog : public QDialog
{
    Q_OBJECT

public:
    enum Mode {
        PrepareProposal,
        SubmitProposal
    };

    explicit ProposalDialog(Mode mode, QWidget* parent);
    ~ProposalDialog();

public slots:
    void checkProposalTX();
    void on_acceptButton_clicked();
    void on_cancelButton_clicked();

private:
    void prepareProposal();
    void submitProposal();
    bool validateProposal();

    Ui::ProposalDialog* ui;
    Mode       mode;
    QTimer*    timer;
    CWalletTx  wtx;
    int        counter; // block height at which collateral TX was sent
};

#endif // AGOUTI_QT_PROPOSALDIALOG_H
