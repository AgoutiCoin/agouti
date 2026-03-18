// Copyright (c) 2018 The Phore developers
// Copyright (c) 2017-2019 The Bulwark developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef AGOUTI_QT_PROPOSALRECORD_H
#define AGOUTI_QT_PROPOSALRECORD_H

#include "amount.h"

#include <QString>

/** In-memory record of a single budget proposal, used by ProposalTableModel. */
class ProposalRecord
{
public:
    ProposalRecord() :
        hash(""),
        start_epoch(0),
        end_epoch(0),
        totalPaymentCount(0),
        remainingPaymentCount(0),
        url(""),
        name(""),
        yesVotes(0),
        noVotes(0),
        abstainVotes(0),
        amount(0),
        percentage(0)
    {}

    ProposalRecord(QString hash,
                   int start_epoch, int end_epoch,
                   int totalPaymentCount, int remainingPaymentCount,
                   QString url, QString name,
                   int yesVotes, int noVotes, int abstainVotes,
                   CAmount amount, double percentage) :
        hash(hash),
        start_epoch(start_epoch),
        end_epoch(end_epoch),
        totalPaymentCount(totalPaymentCount),
        remainingPaymentCount(remainingPaymentCount),
        url(url),
        name(name),
        yesVotes(yesVotes),
        noVotes(noVotes),
        abstainVotes(abstainVotes),
        amount(amount),
        percentage(percentage)
    {}

    QString hash;
    int     start_epoch;
    int     end_epoch;
    int     totalPaymentCount;
    int     remainingPaymentCount;
    QString url;
    QString name;
    int     yesVotes;
    int     noVotes;
    int     abstainVotes;
    CAmount amount;
    double  percentage;
};

#endif // AGOUTI_QT_PROPOSALRECORD_H
