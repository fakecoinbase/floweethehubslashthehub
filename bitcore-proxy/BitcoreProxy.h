/*
 * This file is part of the Flowee project
 * Copyright (C) 2019 Tom Zander <tomz@freedommail.ch>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef BITCOREPROXY_H
#define BITCOREPROXY_H

#include <Blockchain.h>
#include <QJsonObject>
#include <boost/unordered/unordered_map.hpp>
#include <httpengine/server.h>

class BitCoreWebRequest;

struct RequestString
{
    RequestString(const QString &path);

    QString anonPath() const;

    QString wholePath;
    QString chain;
    QString network;
    QString request;
    QString post;
};

class BitcoreWebRequest : public HttpEngine::WebRequest, public Blockchain::Search
{
    Q_OBJECT
public:
    BitcoreWebRequest(qintptr socketDescriptor, std::function<void(HttpEngine::WebRequest*)> &handler);

    enum {
        Unset,
        TxForHeight,
        TxForBlockHash,
        TxForTxId,
        TxForTxIdAuthHead,
        TxForTxIdCoins,

        AddressTxs,
        AddressUnspentOutputs,
        AddressBalance,
    } answerType = Unset;

    QJsonObject &map();

    // Blockchain::Search interface
    void finished(int unfinishedJobs) override;
    void transactionAdded(int jobId, const Blockchain::Transaction &transaction) override;
    void txIdResolved(int jobId, int blockHeight, int offsetInBlock) override;
    void spentOutputResolved(int jobId, int blockHeight, int offsetInBlock) override;

    QJsonObject m_map;

private slots:
    void threadSafeFinished();

private:
    // add things like 'network', 'chain' and '_id'
    void addDefaults(QJsonObject &node);

    boost::unordered_map<uint256, int, HashShortener> blockHeights;
    std::map<int, std::pair<int, int> > spentMap;
};

class BitcoreProxy : public Blockchain::SearchEngine
{
    Q_OBJECT
public:
    BitcoreProxy();

    // http engine callback
    void onIncomingConnection(HttpEngine::WebRequest *request);

    void parseConfig(const QString &confFile) override;


    void initializeHubConnection(NetworkConnection &connection) override;

private:
    void returnEnabledChains(HttpEngine::WebRequest *request) const;
    void returnTemplatePath(HttpEngine::Socket *socket, const QString &templateName, const QString &error = QString()) const;

    void requestTransactionInfo(const RequestString &rs, BitcoreWebRequest *request);
    void requestAddressInfo(const RequestString &rs, BitcoreWebRequest *request);
    void requestBlockInfo(const RequestString &rs, BitcoreWebRequest *request);
    void returnFeeSuggestion(const RequestString &rs, BitcoreWebRequest *request);
    void returnDailyTransactions(const RequestString &rs, BitcoreWebRequest *request);
};

#endif
