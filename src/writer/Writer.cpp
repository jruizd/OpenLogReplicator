/* Base class for thread to write output
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
<http:////www.gnu.org/licenses/>.  */

#include <fstream>
#include <thread>
#include <unistd.h>

#include "../builder/Builder.h"
#include "../common/Ctx.h"
#include "../common/DataException.h"
#include "../common/NetworkException.h"
#include "../common/RuntimeException.h"
#include "../metadata/Metadata.h"
#include "Writer.h"

namespace OpenLogReplicator {
    Writer::Writer(Ctx* newCtx, const std::string& newAlias, const std::string& newDatabase, Builder* newBuilder, Metadata* newMetadata) :
            Thread(newCtx, newAlias),
            database(newDatabase),
            builder(newBuilder),
            metadata(newMetadata),
            checkpointScn(ZERO_SCN),
            checkpointTime(time(nullptr)),
            confirmedScn(ZERO_SCN),
            confirmedMessages(0),
            sentMessages(0),
            currentQueueSize(0),
            maxQueueSize(0),
            queue(nullptr),
            streaming(false) {
    }

    Writer::~Writer() {
        if (queue != nullptr) {
            delete[] queue;
            queue = nullptr;
        }
    }

    void Writer::initialize() {
        if (queue != nullptr)
            return;
        queue = new BuilderMsg*[ctx->queueSize];
    }

    void Writer::createMessage(BuilderMsg* msg) {
        ++sentMessages;

        queue[currentQueueSize++] = msg;
        if (currentQueueSize > maxQueueSize)
            maxQueueSize = currentQueueSize;
    }

    void Writer::sortQueue() {
        if (currentQueueSize == 0)
            return;

        BuilderMsg** oldQueue = queue;
        queue = new BuilderMsg*[ctx->queueSize];
        uint64_t oldQueueSize = currentQueueSize;

        for (uint64_t newId = 0 ; newId < currentQueueSize; ++newId) {
            queue[newId] = oldQueue[0];
            uint64_t i = 0;
            --oldQueueSize;
            while (i < oldQueueSize) {
                if (i * 2 + 2 < oldQueueSize && oldQueue[i * 2 + 2]->id < oldQueue[oldQueueSize]->id) {
                    if (oldQueue[i * 2 + 1]->id < oldQueue[i * 2 + 2]->id) {
                        oldQueue[i] = oldQueue[i * 2 + 1];
                        i = i * 2 + 1;
                    } else {
                        oldQueue[i] = oldQueue[i * 2 + 2];
                        i = i * 2 + 2;
                    }
                } else if (i * 2 + 1 < oldQueueSize && oldQueue[i * 2 + 1]->id < oldQueue[oldQueueSize]->id) {
                    oldQueue[i] = oldQueue[i * 2 + 1];
                    i = i * 2 + 1;
                } else
                    break;
            }
            oldQueue[i] = oldQueue[oldQueueSize];
        }

        if (oldQueue != nullptr)
            delete[] oldQueue;
    }

    void Writer::confirmMessage(BuilderMsg* msg) {
        if (msg == nullptr) {
            if (currentQueueSize == 0) {
                ctx->warning(70007, "trying to confirm empty message");
                return;
            }
            msg = queue[0];
        }

        msg->flags |= OUTPUT_BUFFER_CONFIRMED;
        if (msg->flags & OUTPUT_BUFFER_ALLOCATED) {
            delete[] msg->data;
            msg->flags &= ~OUTPUT_BUFFER_ALLOCATED;
        }
        ++confirmedMessages;

        uint64_t maxId = 0;
        {
            while (currentQueueSize > 0 && (queue[0]->flags & OUTPUT_BUFFER_CONFIRMED) != 0) {
                maxId = queue[0]->queueId;
                confirmedScn = queue[0]->scn;

                if (--currentQueueSize == 0)
                    break;

                uint64_t i = 0;
                while (i < currentQueueSize) {
                    if (i * 2 + 2 < currentQueueSize && queue[i * 2 + 2]->id < queue[currentQueueSize]->id) {
                        if (queue[i * 2 + 1]->id < queue[i * 2 + 2]->id) {
                            queue[i] = queue[i * 2 + 1];
                            i = i * 2 + 1;
                        } else {
                            queue[i] = queue[i * 2 + 2];
                            i = i * 2 + 2;
                        }
                    } else if (i * 2 + 1 < currentQueueSize && queue[i * 2 + 1]->id < queue[currentQueueSize]->id) {
                        queue[i] = queue[i * 2 + 1];
                        i = i * 2 + 1;
                    } else
                        break;
                }
                queue[i] = queue[currentQueueSize];
            }
        }

        builder->releaseBuffers(maxId);
    }

    void Writer::run() {
        if (ctx->trace & TRACE_THREADS) {
            std::ostringstream ss;
            ss << std::this_thread::get_id();
            ctx->logTrace(TRACE_THREADS, "writer (" + ss.str() + ") start");
        }

        ctx->info(0, "writer is starting with " + getName());

        try {
            // External loop for client disconnection
            while (!ctx->hardShutdown) {
                try {
                    mainLoop();

                // client got disconnected
                } catch (NetworkException& ex) {
                    ctx->warning(ex.code, ex.msg);
                    streaming = false;
                }

                if (ctx->softShutdown && ctx->replicatorFinished)
                    break;
            }
        } catch (DataException& ex) {
            ctx->error(ex.code, ex.msg);
            ctx->stopHard();
        } catch (RuntimeException& ex) {
            ctx->error(ex.code, ex.msg);
            ctx->stopHard();
        }

        ctx->info(0, "writer is stopping: " + getName() + ", max queue size: " + std::to_string(maxQueueSize));
        if (ctx->trace & TRACE_THREADS) {
            std::ostringstream ss;
            ss << std::this_thread::get_id();
            ctx->logTrace(TRACE_THREADS, "writer (" + ss.str() + ") stop");
        }
    }

    void Writer::mainLoop() {
        // The client is connected
        readCheckpoint();

        BuilderMsg* msg;
        BuilderQueue* builderQueue = builder->firstBuilderQueue;
        uint64_t oldLength = 0;
        uint64_t newLength = 0;
        currentQueueSize = 0;

        // Start streaming
        while (!ctx->hardShutdown) {

            // Get a message to send
            while (!ctx->hardShutdown) {
                // Check for client checkpoint
                pollQueue();
                writeCheckpoint(false);

                // Next buffer
                if (builderQueue->next != nullptr)
                    if (builderQueue->length == oldLength) {
                        builderQueue = builderQueue->next;
                        oldLength = 0;
                    }

                // Found something
                msg = (BuilderMsg*) (builderQueue->data + oldLength);
                if (builderQueue->length > oldLength + sizeof(struct BuilderMsg) && msg->length > 0) {
                    newLength = builderQueue->length;
                    break;
                }

                ctx->wakeAllOutOfMemory();
                if (ctx->softShutdown && ctx->replicatorFinished)
                    break;
                builder->sleepForWriterWork(currentQueueSize, ctx->pollIntervalUs);
            }

            if (ctx->hardShutdown)
                break;

            // Send message
            while (oldLength + sizeof(struct BuilderMsg) < newLength && !ctx->hardShutdown) {
                msg = (BuilderMsg*) (builderQueue->data + oldLength);
                if (msg->length == 0)
                    break;

                // The queue is full
                pollQueue();
                while (currentQueueSize >= ctx->queueSize && !ctx->hardShutdown) {
                    if (ctx->trace & TRACE_WRITER)
                        ctx->logTrace(TRACE_WRITER, "output queue is full (" + std::to_string(currentQueueSize) +
                                      " schemaElements), sleeping " + std::to_string(ctx->pollIntervalUs) + "us");
                    usleep(ctx->pollIntervalUs);
                    pollQueue();
                }

                writeCheckpoint(false);
                if (ctx->hardShutdown)
                    break;

                // builder->firstBufferPos += OUTPUT_BUFFER_RECORD_HEADER_SIZE;
                uint64_t length8 = (msg->length + 7) & 0xFFFFFFFFFFFFFFF8;
                oldLength += sizeof(struct BuilderMsg);

                // Message in one part - send directly from buffer
                if (oldLength + length8 <= OUTPUT_BUFFER_DATA_SIZE) {
                    createMessage(msg);
                    sendMessage(msg);
                    oldLength += length8;

                // Message in many parts - merge & copy
                } else {
                    msg->data = new uint8_t[msg->length];
                    if (msg->data == nullptr)
                        throw RuntimeException(10016, "couldn't allocate " + std::to_string(msg->length) +
                                               " bytes memory for: temporary buffer for JSON message");
                    msg->flags |= OUTPUT_BUFFER_ALLOCATED;

                    uint64_t copied = 0;
                    while (msg->length - copied > 0) {
                        uint64_t toCopy = msg->length - copied;
                        if (toCopy > newLength - oldLength) {
                            toCopy = newLength - oldLength;
                            memcpy(reinterpret_cast<void*>(msg->data + copied),
                                   reinterpret_cast<const void*>(builderQueue->data + oldLength), toCopy);
                            builderQueue = builderQueue->next;
                            newLength = OUTPUT_BUFFER_DATA_SIZE;
                            oldLength = 0;
                        } else {
                            memcpy(reinterpret_cast<void*>(msg->data + copied),
                                   reinterpret_cast<const void*>(builderQueue->data + oldLength), toCopy);
                            oldLength += (toCopy + 7) & 0xFFFFFFFFFFFFFFF8;
                        }
                        copied += toCopy;
                    }

                    createMessage(msg);
                    sendMessage(msg);
                    pollQueue();
                    writeCheckpoint(false);
                    break;
                }
            }

            // All work done?
            if (ctx->softShutdown && ctx->replicatorFinished) {
                // Some data to send?
                if (builderQueue->length != oldLength || builderQueue->next != nullptr)
                    continue;
                break;
            }
        }

        writeCheckpoint(true);
    }

    void Writer::writeCheckpoint(bool force) {
        // Nothing changed
        if (checkpointScn == confirmedScn || confirmedScn == ZERO_SCN)
            return;

        // Not yet
        time_t now = time(nullptr);
        uint64_t timeSinceCheckpoint = (now - checkpointTime);
        if (timeSinceCheckpoint < ctx->checkpointIntervalS && !force)
            return;

        if (ctx->trace & TRACE_CHECKPOINT) {
            if (checkpointScn == ZERO_SCN)
                ctx->logTrace(TRACE_CHECKPOINT, "writer confirmed scn: " + std::to_string(confirmedScn));
            else
                ctx->logTrace(TRACE_CHECKPOINT, "writer confirmed scn: " + std::to_string(confirmedScn) + "checkpoint scn: " +
                              std::to_string(checkpointScn));
        }
        std::string name(database + "-chkpt");
        std::ostringstream ss;
        ss << R"({"database":")" << database
                << R"(","scn":)" << std::dec << confirmedScn
                << R"(,"resetlogs":)" << std::dec << metadata->resetlogs
                << R"(,"activation":)" << std::dec << metadata->activation << "}";

        if (metadata->stateWrite(name, ss)) {
            checkpointScn = confirmedScn;
            checkpointTime = now;
        }
    }

    void Writer::readCheckpoint() {
        std::ifstream infile;
        std::string name(database + "-chkpt");

        // Checkpoint is present - read it
        std::string checkpoint;
        rapidjson::Document document;
        if (!metadata->stateRead(name, CHECKPOINT_FILE_MAX_SIZE, checkpoint)) {
            metadata->setStatusBoot();
            return;
        }

        if (checkpoint.length() == 0 || document.Parse(checkpoint.c_str()).HasParseError())
            throw DataException(20001, "file: " + name + " offset: " + std::to_string(document.GetErrorOffset()) +
                                " - parse error: " + GetParseError_En(document.GetParseError()));

        const char* databaseJson = Ctx::getJsonFieldS(name, JSON_PARAMETER_LENGTH, document, "database");
        if (database != databaseJson)
            throw DataException(20001, "file: " + name + " - invalid database name: " + databaseJson);

        metadata->setResetlogs(Ctx::getJsonFieldU32(name, document, "resetlogs"));
        metadata->setActivation(Ctx::getJsonFieldU32(name, document, "activation"));

        // Started earlier - continue work & ignore default startup parameters
        metadata->startScn = Ctx::getJsonFieldU64(name, document, "scn");
        metadata->startSequence = ZERO_SEQ;
        metadata->startTime.clear();
        metadata->startTimeRel = 0;
        ctx->info(0, "checkpoint - reading scn: " + std::to_string(metadata->startScn));

        metadata->setStatusReplicate();
    }

    void Writer::wakeUp() {
        builder->wakeUp();
    }
}
