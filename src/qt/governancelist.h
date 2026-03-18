// Copyright (c) 2018 The Phore developers
// Copyright (c) 2017-2019 The Bulwark developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef AGOUTI_QT_GOVERNANCELIST_H
#define AGOUTI_QT_GOVERNANCELIST_H

#include "guiutil.h"
#include "proposaltablemodel.h"

#include <QLabel>
#include <QMenu>
#include <QTimer>
#include <QWidget>

class ProposalFilterProxy;

QT_BEGIN_NAMESPACE
class QComboBox;
class QLineEdit;
class QTableView;
QT_END_NAMESPACE

#define GOVERNANCELIST_UPDATE_SECONDS 30

class GovernanceList : public QWidget
{
    Q_OBJECT

public:
    explicit GovernanceList(QWidget* parent = 0);

    enum ColumnWidths {
        PROPOSAL_COLUMN_WIDTH          = 150,
        AMOUNT_COLUMN_WIDTH            = 100,
        BLOCK_COLUMN_WIDTH             = 90,
        TOTAL_PAYMENT_COLUMN_WIDTH     = 70,
        REMAINING_PAYMENT_COLUMN_WIDTH = 80,
        VOTES_COLUMN_WIDTH             = 60,
        PERCENTAGE_COLUMN_WIDTH        = 80,
        MINIMUM_COLUMN_WIDTH           = 23
    };

private:
    ProposalTableModel*  proposalTableModel;
    ProposalFilterProxy* proposalProxyModel;
    QTableView*          proposalList;
    int64_t              nLastUpdate;

    QLineEdit* proposalWidget;
    QLineEdit* amountWidget;
    QLineEdit* startBlockWidget;
    QLineEdit* endBlockWidget;
    QLineEdit* yesVotesWidget;
    QLineEdit* noVotesWidget;
    QLineEdit* abstainVotesWidget;
    QLineEdit* percentageWidget;
    QLabel*    secondsLabel;
    QComboBox* proposalTypeCombo;
    QMenu*     contextMenu;
    QTimer*    timer;

    GUIUtil::TableViewLastColumnResizingFixer* columnResizingFixer;

    void vote_click_handler(const std::string& voteString);

    virtual void resizeEvent(QResizeEvent* event);

private Q_SLOTS:
    void createProposal();
    void proposalType(int type);
    void contextualMenu(const QPoint&);
    void voteYes();
    void voteNo();
    void voteAbstain();
    void copyProposalUrl();

public Q_SLOTS:
    void refreshProposals(bool force = false);
    void changedProposal(const QString& proposal);
    void changedAmount(const QString& minAmount);
    void changedStartBlock(const QString& startBlock);
    void changedEndBlock(const QString& endBlock);
    void changedYesVotes(const QString& minYesVotes);
    void changedNoVotes(const QString& minNoVotes);
    void changedAbstainVotes(const QString& minAbstainVotes);
    void changedPercentage(const QString& minPercentage);
};

#endif // AGOUTI_QT_GOVERNANCELIST_H
