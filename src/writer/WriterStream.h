/* Header for WriterStream class
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

#include "Writer.h"
#include "../common/OraProtoBuf.pb.h"

#ifndef WRITER_STREAM_H_
#define WRITER_STREAM_H_

namespace OpenLogReplicator {
    class Stream;

    class WriterStream : public Writer {
    protected:
        Stream* stream;
        pb::RedoRequest request;
        pb::RedoResponse response;

        std::string getName() const override;
        void readCheckpoint() override;
        void processInfo();
        void processStart();
        void processRedo();
        void processConfirm();
        void pollQueue() override;
        void sendMessage(BuilderMsg* msg) override;

    public:
        WriterStream(Ctx* newCtx, const std::string& newAlias, const std::string& newDatabase, Builder* newBuilder, Metadata* newMetadata, Stream* newStream);
        ~WriterStream() override;

        void initialize() override;
    };
}

#endif
