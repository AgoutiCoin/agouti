// Copyright (c) 2018 The Phore developers
// Copyright (c) 2017-2019 The Bulwark developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef AGOUTI_QT_PROPOSALFILTERPROXY_H
#define AGOUTI_QT_PROPOSALFILTERPROXY_H

#include "amount.h"

#include <QSortFilterProxyModel>

class ProposalFilterProxy : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    explicit ProposalFilterProxy(QObject* parent = 0);

    void setProposal(const QString& proposal);
    void setMinAmount(qint64 minimum);
    void setMinPercentage(int minimum);
    void setMinYesVotes(int minimum);
    void setMinNoVotes(int minimum);
    void setMinAbstainVotes(int minimum);
    void setProposalStart(int minimum);
    void setProposalEnd(int minimum);
    void setTotalPaymentCount(int count);
    void setRemainingPaymentCount(int count);

    int rowCount(const QModelIndex& parent = QModelIndex()) const;

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const;

private:
    QString proposalName;
    qint64  minAmount;
    int     minPercentage;
    int     minYesVotes;
    int     minNoVotes;
    int     minAbstainVotes;
    int     startBlock;
    int     endBlock;
    int     totalPaymentCount;
    int     remainingPaymentCount;
};

#endif // AGOUTI_QT_PROPOSALFILTERPROXY_H
