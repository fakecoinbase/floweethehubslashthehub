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
#ifndef ADDRESSINDEXER_H
#define ADDRESSINDEXER_H

#include <qsqldatabase.h>
#include <qsqlquery.h>
#include <streaming/ConstBuffer.h>

#include "HashStorage.h"

class AddressIndexer
{
public:
    AddressIndexer(const boost::filesystem::path &basedir);

    int blockheight();
    void blockFinished(int blockheight, const uint256 &blockId);
    void insert(const Streaming::ConstBuffer &addressId, int outputIndex, int blockHeight, int offsetInBlock);

    struct TxData {
        int offsetInBlock = 0;
        short blockHeight = -1;
        short outputIndex = -1;
    };

    std::vector<TxData> find(const uint160 &address) const;

private:
    void createTables();

    // Streaming::BufferPool m_pool;
    HashStorage m_addresses;

    QSqlDatabase m_db;
    QSqlQuery m_insertQuery;
    QSqlQuery m_lastBlockHeightQuery;

    int m_lastKnownHeight = -1;
};

#endif
