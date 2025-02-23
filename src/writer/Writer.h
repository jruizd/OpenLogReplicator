/* Header for Writer class
   Copyright (C) 2018-2023 Adam Leszczynski (aleszczynski@bersler.com)

This file is part of OpenLogReplicator.

OpenLogReplicator is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License as published
by the Free Software Foundation; either version 3, or (at your option)
any later version.

OpenLogReplicator is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
Public License for more details.

You should have received a copy of the GNU General Public License
along with OpenLogReplicator; see the file LICENSE;  If not see
<http://www.gnu.org/licenses/>.  */

#include "../common/Thread.h"

#ifndef WRITER_H_
#define WRITER_H_

namespace OpenLogReplicator {
    class Builder;
    struct BuilderMsg;
    class Metadata;

    class Writer : public Thread {
    protected:
        std::string database;
        Builder* builder;
        Metadata* metadata;
        typeScn checkpointScn;
        time_t checkpointTime;
        typeScn confirmedScn;
        uint64_t confirmedMessages;
        uint64_t sentMessages;
        uint64_t currentQueueSize;
        uint64_t maxQueueSize;
        BuilderMsg** queue;
        bool streaming;

        void createMessage(BuilderMsg* msg);
        virtual void sendMessage(BuilderMsg* msg) = 0;
        virtual std::string getName() const = 0;
        virtual void pollQueue() = 0;
        void run() override;
        void mainLoop();
        virtual void writeCheckpoint(bool force);
        virtual void readCheckpoint();
        void sortQueue();

    public:
        Writer(Ctx* newCtx, const std::string& newAlias, const std::string& newDatabase, Builder* newBuilder, Metadata* newMetadata);
        ~Writer() override;

        virtual void initialize();
        void confirmMessage(BuilderMsg* msg);
        void wakeUp() override;
    };
}

#endif
