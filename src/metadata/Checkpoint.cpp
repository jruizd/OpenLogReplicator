/* Base class for process to write checkpoint files
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

#include <thread>

#include "../common/Ctx.h"
#include "../common/RuntimeException.h"
#include "../common/typeTime.h"
#include "../metadata/Metadata.h"
#include "Checkpoint.h"

namespace OpenLogReplicator {

    Checkpoint::Checkpoint(Ctx* newCtx, Metadata* newMetadata, const std::string& newAlias) :
            Thread(newCtx, newAlias),
            metadata(newMetadata) {
    }

    Checkpoint::~Checkpoint() {
    }

    void Checkpoint::wakeUp() {
        std::unique_lock<std::mutex> lck(mtx);
        condLoop.notify_all();
    }

    void Checkpoint::run() {
        if (ctx->trace & TRACE_THREADS) {
            std::ostringstream ss;
            ss << std::this_thread::get_id();
            ctx->logTrace(TRACE_THREADS, "checkpoint (" + ss.str() + ") start");
        }

        try {
            while (!ctx->hardShutdown) {
                metadata->writeCheckpoint(false);
                metadata->deleteOldCheckpoints();

                if (ctx->hardShutdown)
                    break;

                if (ctx->softShutdown && ctx->replicatorFinished)
                    break;

                {
                    std::unique_lock<std::mutex> lck(mtx);
                    condLoop.wait_for(lck, std::chrono::milliseconds (100));
                }
            }

            if (ctx->softShutdown)
                metadata->writeCheckpoint(true);

        } catch (RuntimeException& ex) {
            ctx->error(ex.code, ex.msg);
            ctx->stopHard();
        } catch (std::bad_alloc& ex) {
            ctx->error(10018, "memory allocation failed: " + std::string(ex.what()));
            ctx->stopHard();
        }

        if (ctx->trace & TRACE_THREADS) {
            std::ostringstream ss;
            ss << std::this_thread::get_id();
            ctx->logTrace(TRACE_THREADS, "checkpoint (" + ss.str() + ") stop");
        }
    }
}
