// Copyright (C) 2017 Michael Kazakov. Subject to GNU General Public License version 3.
#pragma once

#include "NetworkConnectionsManager.h"
#include "VFSInstanceManager.h"

class NetworkConnectionIconProvider
{
public:
    NSImage *Icon16px(const NetworkConnectionsManager::Connection &_connection) const;
    
    /**
     * May return nil if _promise describes not a network vfs known to NetworkConnectionIconProvider
     */
    NSImage *Icon16px(const VFSInstanceManager::Promise &_promise) const;

private:

};
