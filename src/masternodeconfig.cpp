// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// clang-format off
#include "net.h"
#include "masternodeconfig.h"
#include "util.h"
#include "ui_interface.h"
#include <base58.h>
// clang-format on

#include <set>
#include <sstream>

CMasternodeConfig masternodeConfig;

void CMasternodeConfig::add(std::string alias, std::string ip, std::string privKey, std::string txHash, std::string outputIndex)
{
    LOCK(cs);
    CMasternodeEntry cme(alias, ip, privKey, txHash, outputIndex);
    entries.push_back(cme);
}

bool CMasternodeConfig::read(std::string& strErr)
{
    LOCK(cs);
    int linenumber = 1;
    boost::filesystem::path pathMasternodeConfigFile = GetMasternodeConfigFile();
    boost::filesystem::ifstream streamConfig(pathMasternodeConfigFile);

    if (!streamConfig.good()) {
        FILE* configFile = fopen(pathMasternodeConfigFile.string().c_str(), "a");
        if (configFile != NULL) {
            std::string strHeader = "# Masternode config file\n"
                                    "# Format: alias IP:port masternodeprivkey collateral_output_txid collateral_output_index\n"
                                    "# Example: mn1 127.0.0.2:5151 93HaYBVUCYjEMeeH1Y4sBGLALQZE1Yc1K64xiqgX37tGBDQL8Xg 2bcd3c84c84f87eaa86e4e56834c92927a07f9e18718810b92e0d0324456a67c 0\n";
            fwrite(strHeader.c_str(), std::strlen(strHeader.c_str()), 1, configFile);
            fclose(configFile);
        }
        return true; // Nothing to read, so just return
    }

    for (std::string line; std::getline(streamConfig, line); linenumber++) {
        if (line.empty()) continue;

        std::istringstream iss(line);
        std::string comment, alias, ip, privKey, txHash, outputIndex;

        if (iss >> comment) {
            if (comment.at(0) == '#') continue;
            iss.str(line);
            iss.clear();
        }

        if (!(iss >> alias >> ip >> privKey >> txHash >> outputIndex)) {
            iss.str(line);
            iss.clear();
            if (!(iss >> alias >> ip >> privKey >> txHash >> outputIndex)) {
                strErr = _("Could not parse masternode.conf") + "\n" +
                         strprintf(_("Line: %d"), linenumber) + "\n\"" + line + "\"";
                streamConfig.close();
                return false;
            }
        }

        if (Params().NetworkID() == CBaseChainParams::MAIN) {
            if (CService(ip).GetPort() != 5151) {
                strErr = _("Invalid port detected in masternode.conf") + "\n" +
                         strprintf(_("Line: %d"), linenumber) + "\n\"" + line + "\"" + "\n" +
                         _("(must be 5151 for mainnet)");
                streamConfig.close();
                return false;
            }
        } else if (CService(ip).GetPort() == 5151) {
            strErr = _("Invalid port detected in masternode.conf") + "\n" +
                     strprintf(_("Line: %d"), linenumber) + "\n\"" + line + "\"" + "\n" +
                     _("(5151 could be used only on mainnet)");
            streamConfig.close();
            return false;
        }


        add(alias, ip, privKey, txHash, outputIndex);
    }

    streamConfig.close();
    return true;
}

void CMasternodeConfig::clear()
{
    LOCK(cs);
    entries.clear();
}

void CMasternodeConfig::deleteAlias(int index)
{
    LOCK(cs);
    if (index < 0 || index >= (int)entries.size()) return;
    entries.erase(entries.begin() + index);
}

bool CMasternodeConfig::updateIp(const std::string& txHash, const std::string& outputIndex, const std::string& ip,
    std::string* aliasOut, std::string* oldIpOut, std::string* privKeyOut)
{
    LOCK(cs);
    BOOST_FOREACH (CMasternodeEntry& mne, entries) {
        if (mne.getTxHash() != txHash || mne.getOutputIndex() != outputIndex)
            continue;

        if (aliasOut)
            *aliasOut = mne.getAlias();
        if (oldIpOut)
            *oldIpOut = mne.getIp();
        if (privKeyOut)
            *privKeyOut = mne.getPrivKey();

        mne.setIp(ip);
        return true;
    }

    return false;
}

bool CMasternodeConfig::updateIpByPrivKey(const std::string& privKey, const std::string& ip)
{
    LOCK(cs);
    BOOST_FOREACH (CMasternodeEntry& mne, entries) {
        if (mne.getPrivKey() != privKey)
            continue;
        if (mne.getIp() == ip)
            return false; // already current, nothing to persist
        mne.setIp(ip);
        return true;
    }
    return false;
}

bool CMasternodeConfig::writeToMasternodeConf()
{
    const boost::filesystem::path pathConfig = GetMasternodeConfigFile();

    static const std::string strHeader =
        "# Masternode config file\n"
        "# Format: alias IP:port masternodeprivkey collateral_output_txid collateral_output_index\n"
        "# Example: mn1 127.0.0.2:5151 93HaYBVUCYjEMeeH1Y4sBGLALQZE1Yc1K64xiqgX37tGBDQL8Xg 2bcd3c84c84f87eaa86e4e56834c92927a07f9e18718810b92e0d0324456a67c 0\n";

    // Snapshot the in-memory entries under the lock so the file we write is
    // self-consistent with any concurrent updateIp()/updateIpByPrivKey().  The
    // (slow) file I/O below runs without the lock held.
    std::vector<CMasternodeEntry> snapshot;
    {
        LOCK(cs);
        snapshot = entries;
    }

    // Build the new file contents.  Preserve the original file's comments, blank
    // lines and any line we cannot parse, substituting only the IP token of
    // recognised collateral lines from the snapshot.  This means a dynamic-IP
    // rewrite never discards operator comments or formatting.
    std::string out;
    std::set<std::string> writtenKeys; // "txHash:outputIndex" of entries already emitted
    std::vector<std::string> vChanges; // human-readable "alias: oldIp -> newIp" for logging

    boost::filesystem::ifstream streamIn(pathConfig);
    bool fHaveOriginal = false;
    if (streamIn.good()) {
        for (std::string line; std::getline(streamIn, line); ) {
            fHaveOriginal = true;

            // Preserve blank lines and comments verbatim.
            size_t firstNonSpace = line.find_first_not_of(" \t\r");
            if (firstNonSpace == std::string::npos || line[firstNonSpace] == '#') {
                out += line;
                out += "\n";
                continue;
            }

            std::istringstream iss(line);
            std::string alias, ip, privKey, txHash, outputIndex;
            if (!(iss >> alias >> ip >> privKey >> txHash >> outputIndex)) {
                // Unparseable line: keep verbatim rather than lose data.
                out += line;
                out += "\n";
                continue;
            }

            // Substitute the IP from the snapshot if we know this collateral.
            std::string ipToWrite = ip;
            BOOST_FOREACH (const CMasternodeEntry& mne, snapshot) {
                if (mne.getTxHash() == txHash && mne.getOutputIndex() == outputIndex) {
                    ipToWrite = mne.getIp();
                    break;
                }
            }
            if (ipToWrite != ip)
                vChanges.push_back(alias + ": " + ip + " -> " + ipToWrite);
            out += alias + " " + ipToWrite + " " + privKey + " " + txHash + " " + outputIndex + "\n";
            writtenKeys.insert(txHash + ":" + outputIndex);
        }
        streamIn.close();
    }

    // No readable original (first run / deleted file): synthesise from the header.
    if (!fHaveOriginal)
        out = strHeader;

    // Append any snapshot entries that were not present in the original file.
    BOOST_FOREACH (const CMasternodeEntry& mne, snapshot) {
        if (mne.getAlias().empty())
            continue;
        const std::string key = mne.getTxHash() + ":" + mne.getOutputIndex();
        if (writtenKeys.count(key))
            continue;
        out += mne.getAlias() + " " + mne.getIp() + " " + mne.getPrivKey() + " " +
               mne.getTxHash() + " " + mne.getOutputIndex() + "\n";
        writtenKeys.insert(key);
    }

    // Atomic write: write to a temp file in the same directory, fsync it, keep a
    // .bak of the previous config, then atomically rename over the original.  A
    // crash or power loss can therefore never leave a truncated masternode.conf
    // (the file holds the only copy of the masternode private keys).
    const boost::filesystem::path pathTmp(pathConfig.string() + ".tmp");

    FILE* file = fopen(pathTmp.string().c_str(), "wb");
    if (!file) {
        LogPrintf("CMasternodeConfig::writeToMasternodeConf -- cannot open temp file %s for writing\n", pathTmp.string());
        return false;
    }
    if (!out.empty() && fwrite(out.c_str(), 1, out.size(), file) != out.size()) {
        fclose(file);
        LogPrintf("CMasternodeConfig::writeToMasternodeConf -- write error to %s\n", pathTmp.string());
        boost::filesystem::remove(pathTmp);
        return false;
    }
    FileCommit(file); // flush + fsync temp contents to disk before the rename
    fclose(file);

    // Back up the previous config (kept until the atomic rename below succeeds).
    if (boost::filesystem::exists(pathConfig)) {
        const boost::filesystem::path pathBak(pathConfig.string() + ".bak");
        try {
            boost::filesystem::copy_file(pathConfig, pathBak, boost::filesystem::copy_option::overwrite_if_exists);
        } catch (const std::exception& e) {
            LogPrintf("CMasternodeConfig::writeToMasternodeConf -- backup to .bak failed: %s\n", e.what());
        }
    }

    try {
        boost::filesystem::rename(pathTmp, pathConfig);
    } catch (const std::exception& e) {
        LogPrintf("CMasternodeConfig::writeToMasternodeConf -- atomic rename failed: %s\n", e.what());
        boost::filesystem::remove(pathTmp);
        return false;
    }

    if (vChanges.empty()) {
        LogPrintf("CMasternodeConfig::writeToMasternodeConf -- persisted %s (%u entries, no IP change)\n",
                  pathConfig.string(), (unsigned)writtenKeys.size());
    } else {
        BOOST_FOREACH (const std::string& strChange, vChanges)
            LogPrintf("CMasternodeConfig::writeToMasternodeConf -- updated masternode.conf %s\n", strChange);
    }

    return true;
}

bool CMasternodeConfig::CMasternodeEntry::castOutputIndex(int &n)
{
    try {
        n = std::stoi(outputIndex);
    } catch (const std::exception& e) {
        LogPrintf("%s: %s on getOutputIndex\n", __func__, e.what());
        return false;
    }

    return true;
}
