// Copyright (c) 2018 The Phore developers
// Copyright (c) 2017-2019 The Bulwark developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "proposalfilterproxy.h"
#include "proposaltablemodel.h"

ProposalFilterProxy::ProposalFilterProxy(QObject* parent)
    : QSortFilterProxyModel(parent),
      proposalName(),
      minAmount(0),
      minPercentage(-100),
      minYesVotes(0),
      minNoVotes(0),
      minAbstainVotes(0),
      startBlock(0),
      endBlock(0),
      totalPaymentCount(0),
      remainingPaymentCount(0)
{}

bool ProposalFilterProxy::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const
{
    QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);

    int    sBlock     = index.data(ProposalTableModel::StartBlockRole).toInt();
    int    eBlock     = index.data(ProposalTableModel::EndBlockRole).toInt();
    int    tPayments  = index.data(ProposalTableModel::TotalPaymentCountRole).toInt();
    int    rPayments  = index.data(ProposalTableModel::RemainingPaymentCountRole).toInt();
    QString propName  = index.data(ProposalTableModel::ProposalRole).toString();
    qint64  amount    = llabs(index.data(ProposalTableModel::AmountRole).toLongLong());
    int    yes        = index.data(ProposalTableModel::YesVotesRole).toInt();
    int    no         = index.data(ProposalTableModel::NoVotesRole).toInt();
    int    abstain    = index.data(ProposalTableModel::AbstainVotesRole).toInt();
    int    pct        = index.data(ProposalTableModel::PercentageRole).toInt();

    if (startBlock         > 0 && sBlock    < startBlock)         return false;
    if (endBlock           > 0 && eBlock    < endBlock)           return false;
    if (tPayments          < totalPaymentCount)                   return false;
    if (rPayments          < remainingPaymentCount)               return false;
    if (!propName.contains(proposalName, Qt::CaseInsensitive))    return false;
    if (amount             < minAmount)                           return false;
    if (yes                < minYesVotes)                         return false;
    if (no                 < minNoVotes)                          return false;
    if (abstain            < minAbstainVotes)                     return false;
    if (pct                < minPercentage)                       return false;

    return true;
}

void ProposalFilterProxy::setProposal(const QString& proposal)
    { proposalName = proposal; invalidateFilter(); }

void ProposalFilterProxy::setMinAmount(qint64 minimum)
    { minAmount = minimum; invalidateFilter(); }

void ProposalFilterProxy::setMinPercentage(int minimum)
    { minPercentage = minimum; invalidateFilter(); }

void ProposalFilterProxy::setMinYesVotes(int minimum)
    { minYesVotes = minimum; invalidateFilter(); }

void ProposalFilterProxy::setMinNoVotes(int minimum)
    { minNoVotes = minimum; invalidateFilter(); }

void ProposalFilterProxy::setMinAbstainVotes(int minimum)
    { minAbstainVotes = minimum; invalidateFilter(); }

void ProposalFilterProxy::setProposalStart(int minimum)
    { startBlock = minimum; invalidateFilter(); }

void ProposalFilterProxy::setProposalEnd(int minimum)
    { endBlock = minimum; invalidateFilter(); }

void ProposalFilterProxy::setTotalPaymentCount(int count)
    { totalPaymentCount = count; invalidateFilter(); }

void ProposalFilterProxy::setRemainingPaymentCount(int count)
    { remainingPaymentCount = count; invalidateFilter(); }

int ProposalFilterProxy::rowCount(const QModelIndex& parent) const
{
    return QSortFilterProxyModel::rowCount(parent);
}
