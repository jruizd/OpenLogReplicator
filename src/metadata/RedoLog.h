/* Header for RedoLog class
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

#include <string>

#include "../common/types.h"

#ifndef REDOLOG_H_
#define REDOLOG_H_

namespace OpenLogReplicator {
    class RedoLog {
    public:
        int64_t group;
        std::string path;

        RedoLog(int64_t newGroup, const char* newPath);

        bool operator<(const RedoLog& other) const;
    };
}

#endif
