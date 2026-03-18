// Copyright (c) 2018 The Phore developers
// Copyright (c) 2017-2019 The Bulwark developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "governancelist.h"

#include "guiutil.h"
#include "masternode-budget.h"
#include "masternodeconfig.h"
#include "masternodeman.h"
#include "obfuscation.h"
#include "proposaldialog.h"
#include "proposalfilterproxy.h"
#include "proposalrecord.h"
#include "proposaltablemodel.h"
#include "sync.h"
#include "util.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QDoubleValidator>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QTableView>
#include <QUrl>
#include <QVBoxLayout>

GovernanceList::GovernanceList(QWidget* parent)
    : QWidget(parent),
      proposalTableModel(0),
      proposalProxyModel(0),
      proposalList(0),
      nLastUpdate(0),
      columnResizingFixer(0)
{
    proposalTableModel = new ProposalTableModel(this);

    // ---- Header row: Create button + type combo ----
    QPushButton* createButton = new QPushButton(tr("Create Proposal"), this);
    createButton->setToolTip(tr("Create a new budget proposal (requires 50 AGU collateral)"));

    proposalTypeCombo = new QComboBox(this);
    proposalTypeCombo->setFixedWidth(200);
    proposalTypeCombo->addItem(tr("All Proposals"),    0);
    proposalTypeCombo->addItem(tr("Budget Projection"), 1);

    QHBoxLayout* headLayout = new QHBoxLayout();
    headLayout->setContentsMargins(0, 0, 0, 8);
    headLayout->addWidget(createButton);
    headLayout->addStretch(1);
    headLayout->addWidget(proposalTypeCombo);

    // ---- Filter row ----
    proposalWidget = new QLineEdit(this);
    proposalWidget->setPlaceholderText(tr("Proposal name"));
    proposalWidget->setMaximumWidth(150);

    amountWidget = new QLineEdit(this);
    amountWidget->setPlaceholderText(tr("Min amount"));
    amountWidget->setValidator(new QDoubleValidator(0, 1e15, 0, this));
    amountWidget->setMaximumWidth(90);

    startBlockWidget = new QLineEdit(this);
    startBlockWidget->setPlaceholderText(tr("Start block"));
    startBlockWidget->setValidator(new QIntValidator(0, INT_MAX, this));
    startBlockWidget->setMaximumWidth(90);

    endBlockWidget = new QLineEdit(this);
    endBlockWidget->setPlaceholderText(tr("End block"));
    endBlockWidget->setValidator(new QIntValidator(0, INT_MAX, this));
    endBlockWidget->setMaximumWidth(90);

    yesVotesWidget = new QLineEdit(this);
    yesVotesWidget->setPlaceholderText(tr("Min yes votes"));
    yesVotesWidget->setValidator(new QIntValidator(0, INT_MAX, this));
    yesVotesWidget->setMaximumWidth(90);

    noVotesWidget = new QLineEdit(this);
    noVotesWidget->setPlaceholderText(tr("Min no votes"));
    noVotesWidget->setValidator(new QIntValidator(0, INT_MAX, this));
    noVotesWidget->setMaximumWidth(90);

    abstainVotesWidget = new QLineEdit(this);
    abstainVotesWidget->setPlaceholderText(tr("Min abstain"));
    abstainVotesWidget->setValidator(new QIntValidator(0, INT_MAX, this));
    abstainVotesWidget->setMaximumWidth(90);

    percentageWidget = new QLineEdit(this);
    percentageWidget->setPlaceholderText(tr("Min %"));
    percentageWidget->setValidator(new QIntValidator(-100, 100, this));
    percentageWidget->setMaximumWidth(70);

    QHBoxLayout* filterLayout = new QHBoxLayout();
    filterLayout->setContentsMargins(0, 0, 0, 4);
    filterLayout->setSpacing(6);
    filterLayout->addWidget(new QLabel(tr("Filter:"), this));
    filterLayout->addWidget(proposalWidget);
    filterLayout->addWidget(amountWidget);
    filterLayout->addWidget(startBlockWidget);
    filterLayout->addWidget(endBlockWidget);
    filterLayout->addWidget(yesVotesWidget);
    filterLayout->addWidget(noVotesWidget);
    filterLayout->addWidget(abstainVotesWidget);
    filterLayout->addWidget(percentageWidget);
    filterLayout->addStretch(1);

    // ---- Table view ----
    QTableView* view = new QTableView(this);
    view->setAlternatingRowColors(true);
    view->setSelectionBehavior(QAbstractItemView::SelectRows);
    view->setSelectionMode(QAbstractItemView::SingleSelection);
    view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view->setTabKeyNavigation(false);
    view->setContextMenuPolicy(Qt::CustomContextMenu);
    view->verticalHeader()->hide();
    proposalList = view;

    // ---- Action bar ----
    QPushButton* voteYesButton     = new QPushButton(tr("Vote Yes"),     this);
    QPushButton* voteAbstainButton = new QPushButton(tr("Vote Abstain"), this);
    QPushButton* voteNoButton      = new QPushButton(tr("Vote No"),      this);
    secondsLabel = new QLabel(this);

    QHBoxLayout* actionBar = new QHBoxLayout();
    actionBar->setContentsMargins(0, 8, 0, 8);
    actionBar->setSpacing(8);
    actionBar->addWidget(voteYesButton);
    actionBar->addWidget(voteAbstainButton);
    actionBar->addWidget(voteNoButton);
    actionBar->addWidget(secondsLabel);
    actionBar->addStretch(1);

    // ---- Assemble ----
    QVBoxLayout* vlayout = new QVBoxLayout(this);
    vlayout->setSpacing(0);
    vlayout->addLayout(headLayout);
    vlayout->addLayout(filterLayout);
    vlayout->addWidget(view);
    vlayout->addLayout(actionBar);

    // ---- Context menu ----
    QAction* voteYesAction     = new QAction(tr("Vote Yes"),          this);
    QAction* voteAbstainAction = new QAction(tr("Vote Abstain"),      this);
    QAction* voteNoAction      = new QAction(tr("Vote No"),           this);
    QAction* copyUrlAction     = new QAction(tr("Copy proposal URL"), this);

    contextMenu = new QMenu(this);
    contextMenu->addAction(voteYesAction);
    contextMenu->addAction(voteAbstainAction);
    contextMenu->addAction(voteNoAction);
    contextMenu->addSeparator();
    contextMenu->addAction(copyUrlAction);

    // ---- Proxy model ----
    proposalProxyModel = new ProposalFilterProxy(this);
    proposalProxyModel->setDynamicSortFilter(true);
    proposalProxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
    proposalProxyModel->setSortCaseSensitivity(Qt::CaseInsensitive);
    proposalProxyModel->setSortRole(Qt::EditRole);
    proposalProxyModel->setSourceModel(proposalTableModel);

    view->setModel(proposalProxyModel);
    view->setSortingEnabled(true);
    view->sortByColumn(ProposalTableModel::YesVotes, Qt::DescendingOrder);

    view->setColumnWidth(ProposalTableModel::Proposal,              PROPOSAL_COLUMN_WIDTH);
    view->setColumnWidth(ProposalTableModel::Amount,                AMOUNT_COLUMN_WIDTH);
    view->setColumnWidth(ProposalTableModel::StartBlock,            BLOCK_COLUMN_WIDTH);
    view->setColumnWidth(ProposalTableModel::EndBlock,              BLOCK_COLUMN_WIDTH);
    view->setColumnWidth(ProposalTableModel::TotalPaymentCount,     TOTAL_PAYMENT_COLUMN_WIDTH);
    view->setColumnWidth(ProposalTableModel::RemainingPaymentCount, REMAINING_PAYMENT_COLUMN_WIDTH);
    view->setColumnWidth(ProposalTableModel::YesVotes,              VOTES_COLUMN_WIDTH);
    view->setColumnWidth(ProposalTableModel::NoVotes,               VOTES_COLUMN_WIDTH);
    view->setColumnWidth(ProposalTableModel::AbstainVotes,          VOTES_COLUMN_WIDTH);
    view->setColumnWidth(ProposalTableModel::Percentage,            PERCENTAGE_COLUMN_WIDTH);

    columnResizingFixer = new GUIUtil::TableViewLastColumnResizingFixer(view, PROPOSAL_COLUMN_WIDTH, MINIMUM_COLUMN_WIDTH);

    // ---- Timer ----
    nLastUpdate = GetTime();
    timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(refreshProposals()));
    timer->start(1000);

    // ---- Connections ----
    connect(view,               SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(contextualMenu(QPoint)));
    connect(createButton,       SIGNAL(clicked()),                          this, SLOT(createProposal()));
    connect(proposalTypeCombo,  SIGNAL(currentIndexChanged(int)),           this, SLOT(proposalType(int)));
    connect(voteYesButton,      SIGNAL(clicked()),                          this, SLOT(voteYes()));
    connect(voteAbstainButton,  SIGNAL(clicked()),                          this, SLOT(voteAbstain()));
    connect(voteNoButton,       SIGNAL(clicked()),                          this, SLOT(voteNo()));
    connect(voteYesAction,      SIGNAL(triggered()),                        this, SLOT(voteYes()));
    connect(voteAbstainAction,  SIGNAL(triggered()),                        this, SLOT(voteAbstain()));
    connect(voteNoAction,       SIGNAL(triggered()),                        this, SLOT(voteNo()));
    connect(copyUrlAction,      SIGNAL(triggered()),                        this, SLOT(copyProposalUrl()));

    connect(proposalWidget,      SIGNAL(textChanged(QString)), this, SLOT(changedProposal(QString)));
    connect(amountWidget,        SIGNAL(textChanged(QString)), this, SLOT(changedAmount(QString)));
    connect(startBlockWidget,    SIGNAL(textChanged(QString)), this, SLOT(changedStartBlock(QString)));
    connect(endBlockWidget,      SIGNAL(textChanged(QString)), this, SLOT(changedEndBlock(QString)));
    connect(yesVotesWidget,      SIGNAL(textChanged(QString)), this, SLOT(changedYesVotes(QString)));
    connect(noVotesWidget,       SIGNAL(textChanged(QString)), this, SLOT(changedNoVotes(QString)));
    connect(abstainVotesWidget,  SIGNAL(textChanged(QString)), this, SLOT(changedAbstainVotes(QString)));
    connect(percentageWidget,    SIGNAL(textChanged(QString)), this, SLOT(changedPercentage(QString)));
}

void GovernanceList::createProposal()
{
    ProposalDialog dlg(ProposalDialog::PrepareProposal, this);
    if (QDialog::Accepted == dlg.exec())
        refreshProposals(true);
}

void GovernanceList::proposalType(int type)
{
    proposalTableModel->setProposalType(type);
    refreshProposals(true);
}

void GovernanceList::refreshProposals(bool force)
{
    int64_t secondsRemaining = nLastUpdate - GetTime() + GOVERNANCELIST_UPDATE_SECONDS;

    secondsLabel->setText(secondsRemaining > 60
        ? tr("Updates in %1 minute(s)").arg(secondsRemaining / 60)
        : tr("Updates in %1 second(s)").arg(secondsRemaining));

    if (secondsRemaining > 0 && !force) return;
    nLastUpdate = GetTime();

    proposalTableModel->refreshProposals();
    secondsLabel->setText(tr("Updates in 0 second(s)"));
}

void GovernanceList::contextualMenu(const QPoint& point)
{
    QModelIndex index = proposalList->indexAt(point);
    if (!index.isValid()) return;

    QModelIndexList selection = proposalList->selectionModel()->selectedRows(0);
    if (selection.empty()) return;

    contextMenu->exec(QCursor::pos());
}

void GovernanceList::voteYes()     { vote_click_handler("yes");     }
void GovernanceList::voteNo()      { vote_click_handler("no");      }
void GovernanceList::voteAbstain() { vote_click_handler("abstain"); }

void GovernanceList::vote_click_handler(const std::string& voteString)
{
    if (!proposalList->selectionModel()) return;

    QModelIndexList selection = proposalList->selectionModel()->selectedRows();
    if (selection.empty()) return;

    QString proposalName = selection.at(0).data(ProposalTableModel::ProposalRole).toString();

    QMessageBox::StandardButton retval = QMessageBox::question(this,
        tr("Confirm vote"),
        tr("Are you sure you want to vote <strong>%1</strong> on the proposal <strong>%2</strong>?")
            .arg(QString::fromStdString(voteString), proposalName),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);

    if (retval != QMessageBox::Yes) return;

    uint256 hash;
    hash.SetHex(selection.at(0).data(ProposalTableModel::ProposalHashRole).toString().toStdString());

    int nVote = VOTE_ABSTAIN;
    if (voteString == "yes") nVote = VOTE_YES;
    if (voteString == "no")  nVote = VOTE_NO;

    int success = 0;
    int failed  = 0;

    for (const CMasternodeConfig::CMasternodeEntry& mne : masternodeConfig.getEntries()) {
        std::string errorMessage;
        CPubKey pubKeyMasternode;
        CKey    keyMasternode;

        if (!obfuScationSigner.SetKey(mne.getPrivKey(), errorMessage, keyMasternode, pubKeyMasternode)) {
            failed++;
            continue;
        }

        CMasternode* pmn = mnodeman.Find(pubKeyMasternode);
        if (!pmn) {
            failed++;
            continue;
        }

        CBudgetVote vote(pmn->vin, hash, nVote);
        if (!vote.Sign(keyMasternode, pubKeyMasternode)) {
            failed++;
            continue;
        }

        std::string strError;
        if (budget.UpdateProposal(vote, NULL, strError)) {
            budget.mapSeenMasternodeBudgetVotes.insert(std::make_pair(vote.GetHash(), vote));
            vote.Relay();
            success++;
        } else {
            failed++;
        }
    }

    QMessageBox::information(this, tr("Voting"),
        tr("Voted %1 successfully %2 time(s), failed %3 time(s) on \"%4\".")
            .arg(QString::fromStdString(voteString))
            .arg(success)
            .arg(failed)
            .arg(proposalName));

    refreshProposals(true);
}

void GovernanceList::copyProposalUrl()
{
    if (!proposalList || !proposalList->selectionModel()) return;
    GUIUtil::copyEntryData(proposalList, 0, ProposalTableModel::ProposalUrlRole);
}

void GovernanceList::changedProposal(const QString& proposal)
{
    if (proposalProxyModel) proposalProxyModel->setProposal(proposal);
}

void GovernanceList::changedAmount(const QString& minAmount)
{
    if (proposalProxyModel) proposalProxyModel->setMinAmount(minAmount.toLongLong());
}

void GovernanceList::changedStartBlock(const QString& startBlock)
{
    if (proposalProxyModel) proposalProxyModel->setProposalStart(startBlock.toInt());
}

void GovernanceList::changedEndBlock(const QString& endBlock)
{
    if (proposalProxyModel) proposalProxyModel->setProposalEnd(endBlock.toInt());
}

void GovernanceList::changedYesVotes(const QString& minYesVotes)
{
    if (proposalProxyModel) proposalProxyModel->setMinYesVotes(minYesVotes.toInt());
}

void GovernanceList::changedNoVotes(const QString& minNoVotes)
{
    if (proposalProxyModel) proposalProxyModel->setMinNoVotes(minNoVotes.toInt());
}

void GovernanceList::changedAbstainVotes(const QString& minAbstainVotes)
{
    if (proposalProxyModel) proposalProxyModel->setMinAbstainVotes(minAbstainVotes.toInt());
}

void GovernanceList::changedPercentage(const QString& minPercentage)
{
    if (!proposalProxyModel) return;
    int value = minPercentage.isEmpty() ? -100 : minPercentage.toInt();
    proposalProxyModel->setMinPercentage(value);
}

void GovernanceList::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    columnResizingFixer->stretchColumnWidth(ProposalTableModel::Proposal);
}
