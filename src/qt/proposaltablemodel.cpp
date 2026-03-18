// Copyright (c) 2018 The Phore developers
// Copyright (c) 2017-2019 The Bulwark developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "proposaltablemodel.h"

#include "bitcoinunits.h"
#include "guiconstants.h"
#include "proposalrecord.h"
#include "masternode-budget.h"
#include "masternodeman.h"
#include "sync.h"
#include "util.h"

#include <QColor>
#include <QList>

static const int column_alignments[] = {
    Qt::AlignLeft  | Qt::AlignVCenter, // Proposal
    Qt::AlignRight | Qt::AlignVCenter, // Amount
    Qt::AlignRight | Qt::AlignVCenter, // StartBlock
    Qt::AlignRight | Qt::AlignVCenter, // EndBlock
    Qt::AlignRight | Qt::AlignVCenter, // Total
    Qt::AlignRight | Qt::AlignVCenter, // Remaining
    Qt::AlignRight | Qt::AlignVCenter, // Yes
    Qt::AlignRight | Qt::AlignVCenter, // No
    Qt::AlignRight | Qt::AlignVCenter, // Abstain
    Qt::AlignRight | Qt::AlignVCenter  // Percentage
};

ProposalTableModel::ProposalTableModel(QObject* parent)
    : QAbstractTableModel(parent),
      proposalType(0)
{
    columns << tr("Proposal") << tr("Amount") << tr("Start Block") << tr("End Block")
            << tr("Total") << tr("Remaining") << tr("Yes") << tr("No")
            << tr("Abstain") << tr("Percentage");

    refreshProposals();
}

ProposalTableModel::~ProposalTableModel()
{
    qDeleteAll(proposalRecords);
}

void ProposalTableModel::refreshProposals()
{
    beginResetModel();
    qDeleteAll(proposalRecords);
    proposalRecords.clear();

    int mnCount = mnodeman.CountEnabled();

    std::vector<CBudgetProposal*> proposals;
    if (proposalType == 0)
        proposals = budget.GetAllProposals();
    else
        proposals = budget.GetBudget();

    for (CBudgetProposal* p : proposals) {
        int percentage = 0;
        if (mnCount > 0)
            percentage = static_cast<int>(round(p->GetYeas() * 100.0 / mnCount));

        proposalRecords.append(new ProposalRecord(
            QString::fromStdString(p->GetHash().ToString()),
            p->GetBlockStart(),
            p->GetBlockEnd(),
            p->GetTotalPaymentCount(),
            p->GetRemainingPaymentCount(),
            QString::fromStdString(p->GetURL()),
            QString::fromStdString(p->GetName()),
            p->GetYeas(),
            p->GetNays(),
            p->GetAbstains(),
            p->GetAmount(),
            percentage));
    }

    endResetModel();
}

void ProposalTableModel::setProposalType(int type)
{
    proposalType = type;
    refreshProposals();
}

int ProposalTableModel::rowCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent);
    return proposalRecords.size();
}

int ProposalTableModel::columnCount(const QModelIndex& parent) const
{
    Q_UNUSED(parent);
    return columns.length();
}

QVariant ProposalTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid())
        return QVariant();

    ProposalRecord* rec = static_cast<ProposalRecord*>(index.internalPointer());

    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case Proposal:             return rec->name;
        case Amount:               return BitcoinUnits::format(BitcoinUnits::AGU, rec->amount);
        case StartBlock:           return rec->start_epoch;
        case EndBlock:             return rec->end_epoch;
        case TotalPaymentCount:    return rec->totalPaymentCount;
        case RemainingPaymentCount: return rec->remainingPaymentCount;
        case YesVotes:             return rec->yesVotes;
        case NoVotes:              return rec->noVotes;
        case AbstainVotes:         return rec->abstainVotes;
        case Percentage:           return QString("%1%").arg(rec->percentage);
        }
        break;

    case Qt::EditRole:
        switch (index.column()) {
        case Proposal:             return rec->name;
        case Amount:               return qint64(rec->amount);
        case StartBlock:           return rec->start_epoch;
        case EndBlock:             return rec->end_epoch;
        case TotalPaymentCount:    return rec->totalPaymentCount;
        case RemainingPaymentCount: return rec->remainingPaymentCount;
        case YesVotes:             return rec->yesVotes;
        case NoVotes:              return rec->noVotes;
        case AbstainVotes:         return rec->abstainVotes;
        case Percentage:           return rec->percentage;
        }
        break;

    case Qt::TextAlignmentRole:
        return column_alignments[index.column()];

    case Qt::ForegroundRole:
        if (index.column() == Percentage) {
            return (rec->percentage < 10) ? COLOR_NEGATIVE : QColor(23, 168, 26);
        }
        return COLOR_BAREADDRESS;

    case ProposalRole:              return rec->name;
    case AmountRole:                return qint64(rec->amount);
    case StartBlockRole:            return rec->start_epoch;
    case EndBlockRole:              return rec->end_epoch;
    case TotalPaymentCountRole:     return rec->totalPaymentCount;
    case RemainingPaymentCountRole: return rec->remainingPaymentCount;
    case YesVotesRole:              return rec->yesVotes;
    case NoVotesRole:               return rec->noVotes;
    case AbstainVotesRole:          return rec->abstainVotes;
    case PercentageRole:            return rec->percentage;
    case ProposalUrlRole:           return rec->url;
    case ProposalHashRole:          return rec->hash;
    }
    return QVariant();
}

QVariant ProposalTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal)
        return QVariant();

    if (role == Qt::DisplayRole)
        return columns[section];

    if (role == Qt::TextAlignmentRole)
        return Qt::AlignCenter;

    if (role == Qt::ToolTipRole) {
        switch (section) {
        case Proposal:              return tr("Proposal name.");
        case Amount:                return tr("Monthly payment amount.");
        case StartBlock:            return tr("Block at which payments begin.");
        case EndBlock:              return tr("Block at which payments end.");
        case TotalPaymentCount:     return tr("Total number of payments.");
        case RemainingPaymentCount: return tr("Remaining number of payments.");
        case YesVotes:              return tr("Yes votes received.");
        case NoVotes:               return tr("No votes received.");
        case AbstainVotes:          return tr("Abstain votes received.");
        case Percentage:            return tr("Net yes vote percentage of enabled masternodes.");
        }
    }
    return QVariant();
}

QModelIndex ProposalTableModel::index(int row, int column, const QModelIndex& parent) const
{
    Q_UNUSED(parent);
    if (row >= 0 && row < proposalRecords.size())
        return createIndex(row, column, proposalRecords[row]);
    return QModelIndex();
}
