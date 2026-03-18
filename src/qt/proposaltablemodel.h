// Copyright (c) 2018 The Phore developers
// Copyright (c) 2017-2019 The Bulwark developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef AGOUTI_QT_PROPOSALTABLEMODEL_H
#define AGOUTI_QT_PROPOSALTABLEMODEL_H

#include "amount.h"

#include <QAbstractTableModel>
#include <QStringList>

class ProposalRecord;

class ProposalTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit ProposalTableModel(QObject* parent = 0);
    ~ProposalTableModel();

    enum ColumnIndex {
        Proposal             = 0,
        Amount               = 1,
        StartBlock           = 2,
        EndBlock             = 3,
        TotalPaymentCount    = 4,
        RemainingPaymentCount = 5,
        YesVotes             = 6,
        NoVotes              = 7,
        AbstainVotes         = 8,
        Percentage           = 9
    };

    enum RoleIndex {
        ProposalRole              = Qt::UserRole,
        AmountRole,
        StartBlockRole,
        EndBlockRole,
        TotalPaymentCountRole,
        RemainingPaymentCountRole,
        YesVotesRole,
        NoVotesRole,
        AbstainVotesRole,
        PercentageRole,
        ProposalUrlRole,
        ProposalHashRole
    };

    int      rowCount(const QModelIndex& parent) const;
    int      columnCount(const QModelIndex& parent) const;
    QVariant data(const QModelIndex& index, int role) const;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const;
    QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const;

    void refreshProposals();
    void setProposalType(int type);

private:
    QList<ProposalRecord*> proposalRecords;
    QStringList            columns;
    int                    proposalType; // 0 = all, 1 = budget projection
};

#endif // AGOUTI_QT_PROPOSALTABLEMODEL_H
