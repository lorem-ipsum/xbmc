/*
 *      Copyright (C) 2005-2013 Team XBMC
 *      http://xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "SFTPSessionManager.h"
#ifdef HAS_FILESYSTEM_SFTP
#include "SFTPSession.h"
#include "threads/CriticalSection.h"
#include "threads/SingleLock.h"
#include "utils/log.h"
#include "utils/Variant.h"
#include "Util.h"
#include "URL.h"
#include <fcntl.h>
#include <map>
#include <sstream>
#include <libssh/callbacks.h>

#ifdef TARGET_WINDOWS
#include <windows.h>
#pragma comment(lib, "ssh.lib")
#endif

using namespace std;

namespace
{

#ifdef TARGET_WINDOWS

template <class T>
struct wrap_mutex_fun
{
  int operator()(void** mutex)
  {
    T(reinterpret_cast<CRITICAL_SECTION*>(*mutex));
    return 0;
  }
}

struct ssh_threads_callbacks_struct
get_windows_callbacks()
{
  struct ssh_threads_callbacks_struct callbacks;

  callbacks.mutex_init = wrap_mutex_fun<InitializeCriticalSection>;
  callbacks.mutex_destroy = wrap_mutex_fun<DeleteCriticalSection>;
  callbacks.mutex_lock = wrap_mutex_fun<EnterCriticalSection>;
  callbacks.mutex_unlock = wrap_mutex_fun<LeaveCriticalSection>;
  callbacks.thread_id = GetCurrentProcessId;
}

#endif // TARGET_WINDOWS

class SFTPInitHelper
{
public:
  static int Initialize();

private:
  SFTPInitHelper();
  ~SFTPInitHelper();
  SFTPInitHelper(const SFTPInitHelper&);
  SFTPInitHelper& operator=(const SFTPInitHelper&);

  int rc;
};

SFTPInitHelper::SFTPInitHelper()
{
#ifdef TARGET_POSIX
  ssh_threads_set_callbacks(ssh_threads_get_pthread());
#elif (defined TARGET_WINDOW)
  ssh_threads_set_callbacks(get_windows_callbacks());
#else
# error "SFTPInitHelper: unsupported threading model"
#endif
  rc = ssh_init();
  CLog::Log(LOGINFO, "SFTP: Initialization done with return value %i", rc);
}

SFTPInitHelper::~SFTPInitHelper()
{
  ssh_finalize();
}

int
SFTPInitHelper::Initialize()
{
  static SFTPInitHelper helper;
  return helper.rc;
}

} // anonymous namespace

class CSFTPSessionManagerImpl
{
public:
  CSFTPSessionPtr CreateSession(const CStdString &host,
                                unsigned int port,
                                const CStdString &username,
                                const CStdString &password);
  CSFTPSessionPtr CreateUniqueSession(const CStdString &host,
                                      unsigned int port,
                                      const CStdString &username,
                                      const CStdString &password);

  void ReturnUniqueSession(CSFTPSessionPtr);
  void ClearOutIdleSessions();
  void DisconnectAllSessions();

private:
  typedef std::map<CStdString, CSFTPSessionPtr>      shared_map;
  typedef std::multimap<CStdString, CSFTPSessionPtr> unique_map;

  CCriticalSection m_critSect;
  shared_map shared_sessions;
  unique_map unique_sessions;
};

CSFTPSessionManager&
CSFTPSessionManager::GetInstance()
{
  static CSFTPSessionManager manager;
  return manager;
}

CSFTPSessionManager::CSFTPSessionManager()
  : pimpl(new CSFTPSessionManagerImpl())
{
}

CSFTPSessionManager::~CSFTPSessionManager()
{
  if (pimpl)
    delete pimpl;
}

CSFTPSessionPtr CSFTPSessionManager::CreateSession(const CURL &url)
{
  CStdString username = url.GetUserName();
  CStdString password = url.GetPassWord();
  CStdString hostname = url.GetHostName();
  unsigned int port = url.HasPort() ? url.GetPort() : 22;

  return CreateSession(hostname, port, username, password);
}

CSFTPSessionPtr CSFTPSessionManager::CreateUniqueSession(const CURL &url)
{
  CStdString username = url.GetUserName();
  CStdString password = url.GetPassWord();
  CStdString hostname = url.GetHostName();
  unsigned int port = url.HasPort() ? url.GetPort() : 22;

  return CreateUniqueSession(hostname, port, username, password);
}


CSFTPSessionPtr
CSFTPSessionManager::CreateSession(const CStdString &host, unsigned int port,
                                   const CStdString &username,
                                   const CStdString &password)
{
  return pimpl->CreateSession(host, port, username, password);
}

CSFTPSessionPtr
CSFTPSessionManagerImpl::CreateSession(const CStdString &host,
                                       unsigned int port,
                                       const CStdString &username,
                                       const CStdString &password)
{
  // Convert port number to string
  stringstream itoa;
  itoa << port;
  CStdString portstr = itoa.str();

  CSingleLock lock(m_critSect);
  SFTPInitHelper::Initialize();
  CStdString key = CSFTPSession::MakeHostString(host, port, username);
  CSFTPSessionPtr ptr = shared_sessions[key];
  if (ptr == NULL)
  {
    ptr = CSFTPSessionPtr(new CSFTPSession(host, port, username, password));
    shared_sessions[key] = ptr;
  }

  return ptr;
}

CSFTPSessionPtr
CSFTPSessionManager::CreateUniqueSession(const CStdString &host,
                                         unsigned int port,
                                         const CStdString &username,
                                         const CStdString &password)
{
  return pimpl->CreateUniqueSession(host, port, username, password);
}

CSFTPSessionPtr
CSFTPSessionManagerImpl::CreateUniqueSession(const CStdString &host,
                                             unsigned int port,
                                             const CStdString &username,
                                             const CStdString &password)
{
  // Convert port number to string
  stringstream itoa;
  itoa << port;
  CStdString portstr = itoa.str();

  CSingleLock lock(m_critSect);
  SFTPInitHelper::Initialize();

  unique_map::iterator it =
    unique_sessions.find(CSFTPSession::MakeHostString(host, port, username));

  if (it == unique_sessions.end())
    return CSFTPSessionPtr(new CSFTPSession(host, port, username, password));

  CSFTPSessionPtr ret = it->second;
  unique_sessions.erase(it);
  return ret;
}

void CSFTPSessionManager::ReturnUniqueSession(CSFTPSessionPtr ptr)
{
  pimpl->ReturnUniqueSession(ptr);
}

void
CSFTPSessionManagerImpl::ReturnUniqueSession(CSFTPSessionPtr ptr)
{
  CSingleLock lock(m_critSect);

  unique_map::value_type session(ptr->GetHostString(), ptr);
  unique_sessions.insert(session);
}

void CSFTPSessionManager::ClearOutIdleSessions()
{
  pimpl->ClearOutIdleSessions();
}

void
CSFTPSessionManagerImpl::ClearOutIdleSessions()
{
  CSingleLock lock(m_critSect);
  for (shared_map::iterator it = shared_sessions.begin();
       it != shared_sessions.end(); ++it)
    if (it->second->IsIdle())
      shared_sessions.erase(it);

  for (unique_map::iterator it = unique_sessions.begin();
       it != unique_sessions.end(); ++it)
    if (it->second->IsIdle())
      unique_sessions.erase(it);
}

void CSFTPSessionManager::DisconnectAllSessions()
{
  return pimpl->DisconnectAllSessions();
}

void
CSFTPSessionManagerImpl::DisconnectAllSessions()
{
  CSingleLock lock(m_critSect);
  unique_sessions.clear();
  shared_sessions.clear();
}

#endif
