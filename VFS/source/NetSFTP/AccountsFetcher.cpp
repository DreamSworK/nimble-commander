// Copyright (C) 2017 Michael Kazakov. Subject to GNU General Public License version 3.
#include "AccountsFetcher.h"
#include <VFS/VFSError.h>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/trim.hpp>

namespace nc::vfs::sftp {

AccountsFetcher::AccountsFetcher
(LIBSSH2_SESSION *_session, OSType _os_type ):
    m_Session(_session),
    m_OSType(_os_type)
{
}

int AccountsFetcher::FetchUsers( vector<VFSUser> &_target )
{
    _target.clear();
    
    int rc = VFSError::Ok;
    if( m_OSType == OSType::Linux || m_OSType == OSType::xBSD )
        rc = GetUsersViaGetent(_target);
    else if( m_OSType == OSType::MacOSX )
        rc = GetUsersViaOpenDirectory(_target);
    else
        rc = VFSError::FromErrno(ENODEV);
    
    if( rc != VFSError::Ok )
        return rc;

    sort(begin(_target),
         end(_target),
         [](const auto &_1, const auto &_2){ return (signed)_1.uid < (signed)_2.uid; });
    _target.erase(unique(begin(_target),
                         end(_target),
                         [](const auto &_1, const auto &_2){ return _1.uid == _2.uid; }),
                  end(_target));

    return VFSError::Ok;
}

int AccountsFetcher::FetchGroups(vector<VFSGroup> &_target)
{
    _target.clear();
    
    int rc = VFSError::Ok;
    if( m_OSType == OSType::Linux || m_OSType == OSType::xBSD )
        rc = GetGroupsViaGetent(_target);
    else if( m_OSType == OSType::MacOSX )
        rc = GetGroupsViaOpenDirectory(_target);
    else
        rc =  VFSError::FromErrno(ENODEV);
    
    if( rc != VFSError::Ok )
        return rc;
    
    sort(begin(_target),
         end(_target),
         [](const auto &_1, const auto &_2){ return (signed)_1.gid < (signed)_2.gid; });
    _target.erase(unique(begin(_target),
                          end(_target),
                          [](const auto &_1, const auto &_2){ return _1.gid == _2.gid; }),
                  end(_target));
    
    return VFSError::Ok;
}

int AccountsFetcher::GetUsersViaGetent( vector<VFSUser> &_target )
{
    const auto getent = Execute("getent passwd");
    if( !getent )
        return VFSError::FromErrno(ENODEV);
    
    vector<string> entries;
    boost::algorithm::split(entries, *getent, [](auto c){ return c == '\n'; });

    for( const auto &e: entries ) {
        vector<string> fields;
        boost::algorithm::split(fields, e, [](auto c){ return c == ':'; });
        const auto passwd_fields = 7;
        if( fields.size() == passwd_fields ) {
            VFSUser user;
            user.name = fields[0];
            user.gecos = fields[4];
            boost::algorithm::trim_right_if(user.gecos, [](auto c){ return c == ','; });
            user.uid = (unsigned)atoi(fields[2].c_str());
            _target.emplace_back( move(user) );
        }
    }

    return VFSError::Ok;
}

int AccountsFetcher::GetGroupsViaGetent( vector<VFSGroup> &_target )
{
    const auto getent = Execute("getent group");
    if( !getent )
        return VFSError::FromErrno(ENODEV);
    
    vector<string> entries;
    boost::algorithm::split(entries, *getent, [](auto c){ return c == '\n'; });

    for( const auto &e: entries ) {
        vector<string> fields;
        boost::algorithm::split(fields, e, [](auto c){ return c == ':'; });
        const auto group_fields_at_least = 3;
        if( fields.size() >= group_fields_at_least ) {
            VFSGroup group;
            group.name = fields[0];
            group.gid = (unsigned)atoi(fields[2].c_str());
            _target.emplace_back( move(group) );
        }
    }

    return VFSError::Ok;
}

int AccountsFetcher::GetUsersViaOpenDirectory( vector<VFSUser> &_target )
{
    const auto ds_ids = Execute("dscl . -list /Users UniqueID");
    if( !ds_ids )
        return VFSError::FromErrno(ENODEV);
    
    const auto ds_gecos = Execute("dscl . -list /Users RealName");
    if( !ds_gecos )
        return VFSError::FromErrno(ENODEV);

    unordered_map<string, pair<uint32_t, string>> users; // user -> uid, gecos
    vector<string> entries;
    
    boost::algorithm::split(entries, *ds_ids, [](auto c){ return c == '\n'; });
    for( const auto &e: entries )
        if( const auto fs = e.find(' '); fs != string::npos ) {
            const auto name = e.substr(0, fs);
            auto uid_str = e.substr(fs);
            boost::algorithm::trim_left(uid_str);
            users[name].first = (uint32_t)atoi(uid_str.c_str());
        }

    boost::algorithm::split(entries, *ds_gecos, [](auto c){ return c == '\n'; });
    for( const auto &e: entries )
        if(const auto fs = e.find(' '); fs != string::npos ) {
            const auto name = e.substr(0, fs);
            auto gecos = e.substr(fs);
            boost::algorithm::trim_left(gecos);
            users[name].second = gecos;
        }
    
    for( const auto &u: users ) {
        VFSUser user;
        user.name = u.first;
        user.uid = u.second.first;
        user.gecos = u.second.second;
        _target.emplace_back( move(user) );
    }

    return VFSError::Ok;
}

int AccountsFetcher::GetGroupsViaOpenDirectory( vector<VFSGroup> &_target )
{
    const auto ds_ids = Execute("dscl . -list /Groups PrimaryGroupID");
    if( !ds_ids )
        return VFSError::FromErrno(ENODEV);
    
    const auto ds_gecos = Execute("dscl . -list /Groups RealName");
    if( !ds_gecos )
        return VFSError::FromErrno(ENODEV);

    unordered_map<string, pair<uint32_t, string>> groups; // group -> gid, gecos
    vector<string> entries;
    
    boost::algorithm::split(entries, *ds_ids, [](auto c){ return c == '\n'; });
    for( const auto &e: entries )
        if( const auto fs = e.find(' '); fs != string::npos ) {
            const auto name = e.substr(0, fs);
            auto gid_str = e.substr(fs);
            boost::algorithm::trim_left(gid_str);
            groups[name].first = (uint32_t)atoi(gid_str.c_str());
        }

    boost::algorithm::split(entries, *ds_gecos, [](auto c){ return c == '\n'; });
    for( const auto &e: entries )
        if(const auto fs = e.find(' '); fs != string::npos ) {
            const auto name = e.substr(0, fs);
            auto gecos = e.substr(fs);
            boost::algorithm::trim_left(gecos);
            groups[name].second = gecos;
        }
    
    for( const auto &g: groups ) {
        VFSGroup group;
        group.name = g.first;
        group.gid = g.second.first;
        group.gecos = g.second.second;
        _target.emplace_back( move(group) );
    }

    return VFSError::Ok;
}

optional<string> AccountsFetcher::Execute( const string &_command )
{
    LIBSSH2_CHANNEL *channel = libssh2_channel_open_session(m_Session);
    if( channel == nullptr )
        return nullopt;
    
    int rc = libssh2_channel_exec(channel, _command.c_str());
    if( rc < 0 ) {
        libssh2_channel_close(channel);
        libssh2_channel_free(channel);
        return nullopt;
    }

    string response;

    char buffer[4096];
    while( (rc = (int)libssh2_channel_read(channel, buffer, sizeof(buffer)-1)) > 0 ) {
        buffer[rc] = 0;
        response += buffer;
    }
    
    libssh2_channel_close(channel);
    libssh2_channel_free(channel);

    if( rc < 0 )
        return nullopt;
    
    return move(response);
}

}
