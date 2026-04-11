// Copyright (c) 2013-2014 The Bitcoin Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "rpcserver.h"
#include "rpcclient.h"

#include "base58.h"
#include "script/standard.h"
#include "txmempool.h"
#include "wallet.h"

#include <boost/algorithm/string.hpp>
#include <boost/test/unit_test.hpp>

using namespace std;
using namespace json_spirit;

extern Array createArgs(int nRequired, const char* address1 = NULL, const char* address2 = NULL);
extern Value CallRPC(string args);

extern CWallet* pwalletMain;

namespace {

static CScript NewOwnedScript()
{
    LOCK(pwalletMain->cs_wallet);
    const CPubKey pubKey = pwalletMain->GenerateNewKey();
    return GetScriptForDestination(pubKey.GetID());
}

static CScript NewExternalScript()
{
    CKey key;
    key.MakeNewKey(true);
    return GetScriptForDestination(key.GetPubKey().GetID());
}

class WalletTxScope
{
public:
    WalletTxScope(const CTransaction& tx, const CAmount debit, const CAmount credit) : tx(tx), hash(tx.GetHash())
    {
        {
            LOCK(pwalletMain->cs_wallet);
            pwalletMain->mapWallet[hash] = CWalletTx(pwalletMain, tx);
            CWalletTx& stored = pwalletMain->mapWallet[hash];
            stored.nTimeReceived = 1;
            stored.nTimeSmart = 1;
            stored.nOrderPos = 0;
            stored.fDebitCached = true;
            stored.nDebitCached = debit;
            stored.fCreditCached = true;
            stored.nCreditCached = credit;
            stored.fWatchDebitCached = true;
            stored.nWatchDebitCached = 0;
            stored.fWatchCreditCached = true;
            stored.nWatchCreditCached = 0;
        }

        mempool.addUnchecked(hash, CTxMemPoolEntry(tx, 0, GetTime(), 0.0, chainActive.Height() + 1));
    }

    ~WalletTxScope()
    {
        std::list<CTransaction> removed;
        mempool.remove(tx, removed, true);

        LOCK(pwalletMain->cs_wallet);
        pwalletMain->mapWallet.erase(hash);
    }

    std::string Txid() const
    {
        return hash.GetHex();
    }

private:
    const CTransaction tx;
    const uint256 hash;
};

static bool HasSendDetailForVout(const Array& details, const int vout)
{
    BOOST_FOREACH (const Value& value, details) {
        const Object& detail = value.get_obj();
        if (find_value(detail, "category").get_str() == "send" &&
            find_value(detail, "vout").get_int() == vout) {
            return true;
        }
    }
    return false;
}

} // namespace

BOOST_AUTO_TEST_SUITE(rpc_wallet_tests)

BOOST_AUTO_TEST_CASE(rpc_addmultisig)
{
    LOCK(pwalletMain->cs_wallet);

    rpcfn_type addmultisig = tableRPC["addmultisigaddress"]->actor;

    // old, 65-byte-long:
    const char address1Hex[] = "041431A18C7039660CD9E3612A2A47DC53B69CB38EA4AD743B7DF8245FD0438F8E7270415F1085B9DC4D7DA367C69F1245E27EE5552A481D6854184C80F0BB8456";
    // new, compressed:
    const char address2Hex[] = "029BBEFF390CE736BD396AF43B52A1C14ED52C086B1E5585C15931F68725772BAC";

    Value v;
    CBitcoinAddress address;
    BOOST_CHECK_NO_THROW(v = addmultisig(createArgs(1, address1Hex), false));
    address.SetString(v.get_str());
    BOOST_CHECK(address.IsValid() && address.IsScript());

    BOOST_CHECK_NO_THROW(v = addmultisig(createArgs(1, address1Hex, address2Hex), false));
    address.SetString(v.get_str());
    BOOST_CHECK(address.IsValid() && address.IsScript());

    BOOST_CHECK_NO_THROW(v = addmultisig(createArgs(2, address1Hex, address2Hex), false));
    address.SetString(v.get_str());
    BOOST_CHECK(address.IsValid() && address.IsScript());

    BOOST_CHECK_THROW(addmultisig(createArgs(0), false), runtime_error);
    BOOST_CHECK_THROW(addmultisig(createArgs(1), false), runtime_error);
    BOOST_CHECK_THROW(addmultisig(createArgs(2, address1Hex), false), runtime_error);

    BOOST_CHECK_THROW(addmultisig(createArgs(1, ""), false), runtime_error);
    BOOST_CHECK_THROW(addmultisig(createArgs(1, "NotAValidPubkey"), false), runtime_error);

    string short1(address1Hex, address1Hex + sizeof(address1Hex) - 2); // last byte missing
    BOOST_CHECK_THROW(addmultisig(createArgs(2, short1.c_str()), false), runtime_error);

    string short2(address1Hex + 1, address1Hex + sizeof(address1Hex)); // first byte missing
    BOOST_CHECK_THROW(addmultisig(createArgs(2, short2.c_str()), false), runtime_error);
}

BOOST_AUTO_TEST_CASE(rpc_wallet)
{
    // Test RPC calls for various wallet statistics
    Value r;

    LOCK2(cs_main, pwalletMain->cs_wallet);

    CPubKey demoPubkey = pwalletMain->GenerateNewKey();
    CBitcoinAddress demoAddress = CBitcoinAddress(CTxDestination(demoPubkey.GetID()));
    Value retValue;
    string strAccount = "walletDemoAccount";
    string strPurpose = "receive";
    BOOST_CHECK_NO_THROW({ /*Initialize Wallet with an account */
        CWalletDB walletdb(pwalletMain->strWalletFile);
        CAccount account;
        account.vchPubKey = demoPubkey;
        pwalletMain->SetAddressBook(account.vchPubKey.GetID(), strAccount, strPurpose);
        walletdb.WriteAccount(strAccount, account);
    });

    CPubKey setaccountDemoPubkey = pwalletMain->GenerateNewKey();
    CBitcoinAddress setaccountDemoAddress = CBitcoinAddress(CTxDestination(setaccountDemoPubkey.GetID()));

    /*********************************
     * 			setaccount
     *********************************/
    BOOST_CHECK_NO_THROW(CallRPC("setaccount " + setaccountDemoAddress.ToString() + " nullaccount"));
    /* D8w12Vu3WVhn543dgrUUf9uYu6HLwnPm5R is not owned by the test wallet. */
    BOOST_CHECK_THROW(CallRPC("setaccount D8w12Vu3WVhn543dgrUUf9uYu6HLwnPm5R nullaccount"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("setaccount"), runtime_error);
    /* D8w12Vu3WVhn543dgrUUf9uYu6HLwnPm5 (33 chars) is an illegal address (should be 34 chars) */
    BOOST_CHECK_THROW(CallRPC("setaccount D8w12Vu3WVhn543dgrUUf9uYu6HLwnPm5 nullaccount"), runtime_error);

    /*********************************
     * 			listunspent
     *********************************/
    BOOST_CHECK_NO_THROW(CallRPC("listunspent"));
    BOOST_CHECK_THROW(CallRPC("listunspent string"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("listunspent 0 string"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("listunspent 0 1 not_array"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("listunspent 0 1 [] extra"), runtime_error);
    BOOST_CHECK_NO_THROW(r = CallRPC("listunspent 0 1 []"));
    BOOST_CHECK(r.get_array().empty());

    /*********************************
     * 		listreceivedbyaddress
     *********************************/
    BOOST_CHECK_NO_THROW(CallRPC("listreceivedbyaddress"));
    BOOST_CHECK_NO_THROW(CallRPC("listreceivedbyaddress 0"));
    BOOST_CHECK_THROW(CallRPC("listreceivedbyaddress not_int"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("listreceivedbyaddress 0 not_bool"), runtime_error);
    BOOST_CHECK_NO_THROW(CallRPC("listreceivedbyaddress 0 true"));
    BOOST_CHECK_THROW(CallRPC("listreceivedbyaddress 0 true extra"), runtime_error);

    /*********************************
     * 		listreceivedbyaccount
     *********************************/
    BOOST_CHECK_NO_THROW(CallRPC("listreceivedbyaccount"));
    BOOST_CHECK_NO_THROW(CallRPC("listreceivedbyaccount 0"));
    BOOST_CHECK_THROW(CallRPC("listreceivedbyaccount not_int"), runtime_error);
    BOOST_CHECK_THROW(CallRPC("listreceivedbyaccount 0 not_bool"), runtime_error);
    BOOST_CHECK_NO_THROW(CallRPC("listreceivedbyaccount 0 true"));
    BOOST_CHECK_THROW(CallRPC("listreceivedbyaccount 0 true extra"), runtime_error);

    /*********************************
     * 		getrawchangeaddress
     *********************************/
    BOOST_CHECK_NO_THROW(CallRPC("getrawchangeaddress"));

    /*********************************
     * 		getnewaddress
     *********************************/
    BOOST_CHECK_NO_THROW(CallRPC("getnewaddress"));
    BOOST_CHECK_NO_THROW(CallRPC("getnewaddress getnewaddress_demoaccount"));

    /*********************************
     * 		getaccountaddress
     *********************************/
    BOOST_CHECK_NO_THROW(CallRPC("getaccountaddress \"\""));
    BOOST_CHECK_NO_THROW(CallRPC("getaccountaddress accountThatDoesntExists")); // Should generate a new account
    BOOST_CHECK_NO_THROW(retValue = CallRPC("getaccountaddress " + strAccount));
    BOOST_CHECK(CBitcoinAddress(retValue.get_str()).Get() == demoAddress.Get());

    /*********************************
     * 			getaccount
     *********************************/
    BOOST_CHECK_THROW(CallRPC("getaccount"), runtime_error);
    BOOST_CHECK_NO_THROW(CallRPC("getaccount " + demoAddress.ToString()));

    /*********************************
     * 	signmessage + verifymessage
     *********************************/
    BOOST_CHECK_NO_THROW(retValue = CallRPC("signmessage " + demoAddress.ToString() + " mymessage"));
    BOOST_CHECK_THROW(CallRPC("signmessage"), runtime_error);
    /* Should throw error because this address is not loaded in the wallet */
    BOOST_CHECK_THROW(CallRPC("signmessage D8w12Vu3WVhn543dgrUUf9uYu6HLwnPm5R mymessage"), runtime_error);

    /* missing arguments */
    BOOST_CHECK_THROW(CallRPC("verifymessage " + demoAddress.ToString()), runtime_error);
    BOOST_CHECK_THROW(CallRPC("verifymessage " + demoAddress.ToString() + " " + retValue.get_str()), runtime_error);
    /* Illegal address */
    BOOST_CHECK_THROW(CallRPC("verifymessage D8w12Vu3WVhn543dgrUUf9uYu6HLwnPm5 " + retValue.get_str() + " mymessage"), runtime_error);
    /* wrong address */
    BOOST_CHECK(CallRPC("verifymessage D8w12Vu3WVhn543dgrUUf9uYu6HLwnPm5R " + retValue.get_str() + " mymessage").get_bool() == false);
    /* Correct address and signature but wrong message */
    BOOST_CHECK(CallRPC("verifymessage " + demoAddress.ToString() + " " + retValue.get_str() + " wrongmessage").get_bool() == false);
    /* Correct address, message and signature*/
    BOOST_CHECK(CallRPC("verifymessage " + demoAddress.ToString() + " " + retValue.get_str() + " mymessage").get_bool() == true);

    /*********************************
     * 		getaddressesbyaccount
     *********************************/
    BOOST_CHECK_THROW(CallRPC("getaddressesbyaccount"), runtime_error);
    BOOST_CHECK_NO_THROW(retValue = CallRPC("getaddressesbyaccount " + strAccount));
    Array arr = retValue.get_array();
    BOOST_CHECK(arr.size() > 0);
    BOOST_CHECK(CBitcoinAddress(arr[0].get_str()).Get() == demoAddress.Get());
}

BOOST_AUTO_TEST_CASE(rpc_gettransaction_legacy_coinstake_reports_reward_and_skips_marker_send)
{
    static const CAmount STAKED_INPUT = 100 * COIN;
    static const CAmount STAKE_REWARD = 87500000;

    CMutableTransaction tx;
    tx.nLockTime = 1001;
    tx.vin.push_back(CTxIn(uint256S("01"), 0));
    tx.vout.push_back(CTxOut());
    tx.vout[0].SetEmpty();
    tx.vout.push_back(CTxOut(STAKED_INPUT + STAKE_REWARD, NewOwnedScript()));

    const CTransaction coinstake(tx);
    WalletTxScope scope(coinstake, STAKED_INPUT, STAKED_INPUT + STAKE_REWARD);

    const Object result = CallRPC("gettransaction " + scope.Txid()).get_obj();

    BOOST_CHECK_EQUAL(AmountFromValue(find_value(result, "amount")), STAKE_REWARD);
    BOOST_CHECK(find_value(result, "fee").type() == null_type);
    BOOST_CHECK(find_value(result, "generated").get_bool());

    const Array details = find_value(result, "details").get_array();
    BOOST_CHECK_EQUAL(details.size(), 1U);
    BOOST_CHECK(!HasSendDetailForVout(details, 0));
}

BOOST_AUTO_TEST_CASE(rpc_gettransaction_v5_coinstake_reports_staker_reward)
{
    static const CAmount STAKER_REWARD = 12500000;
    static const CAmount MASTERNODE_PAYMENT = 87500000;

    CMutableTransaction tx;
    tx.nLockTime = 1002;
    CTxIn marker;
    marker.prevout.SetNull();
    marker.scriptSig = CScript() << OP_PROOFOFSTAKE << 1;
    tx.vin.push_back(marker);
    tx.vout.push_back(CTxOut());
    tx.vout[0].SetEmpty();
    tx.vout.push_back(CTxOut(STAKER_REWARD, NewOwnedScript()));
    tx.vout.push_back(CTxOut(MASTERNODE_PAYMENT, NewExternalScript()));

    const CTransaction coinstake(tx);
    WalletTxScope scope(coinstake, 0, STAKER_REWARD);

    const Object result = CallRPC("gettransaction " + scope.Txid()).get_obj();

    BOOST_CHECK_EQUAL(AmountFromValue(find_value(result, "amount")), STAKER_REWARD);
    BOOST_CHECK(find_value(result, "fee").type() == null_type);
    BOOST_CHECK(find_value(result, "generated").get_bool());

    const Array details = find_value(result, "details").get_array();
    BOOST_CHECK_EQUAL(details.size(), 1U);
    BOOST_CHECK(!HasSendDetailForVout(details, 0));
}

BOOST_AUTO_TEST_CASE(rpc_gettransaction_legacy_coinstake_burn_does_not_report_negative_reward)
{
    static const CAmount STAKED_INPUT = 100 * COIN;
    static const CAmount BURNED_AMOUNT = 87500000;

    CMutableTransaction tx;
    tx.nLockTime = 1003;
    tx.vin.push_back(CTxIn(uint256S("03"), 0));
    tx.vout.push_back(CTxOut());
    tx.vout[0].SetEmpty();
    tx.vout.push_back(CTxOut(STAKED_INPUT - BURNED_AMOUNT, NewOwnedScript()));

    const CTransaction coinstake(tx);
    WalletTxScope scope(coinstake, STAKED_INPUT, STAKED_INPUT - BURNED_AMOUNT);

    const Object result = CallRPC("gettransaction " + scope.Txid()).get_obj();

    BOOST_CHECK_EQUAL(AmountFromValue(find_value(result, "amount")), 0);
    BOOST_CHECK(find_value(result, "fee").type() == null_type);
    BOOST_CHECK(find_value(result, "generated").get_bool());
}


BOOST_AUTO_TEST_SUITE_END()
