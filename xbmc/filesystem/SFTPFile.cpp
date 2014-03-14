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

#include "threads/SystemClock.h"
#include "SFTPFile.h"
#ifdef HAS_FILESYSTEM_SFTP
#include "SFTPSession.h"
#include "SFTPSessionManager.h"
#include "threads/SingleLock.h"
#include "utils/log.h"
#include "utils/TimeUtils.h"
#include "utils/Variant.h"
#include "Util.h"
#include "URL.h"
#include <fcntl.h>
#include <sstream>

#ifdef TARGET_WINDOWS
#pragma comment(lib, "ssh.lib")
#endif

using namespace XFILE;
using namespace std;

#define QUEUE_COUNT 20

CSFTPFile::CSFTPFile()
  : m_file(),
    m_session(),
    m_read_session(),
    m_sftp_handle(),
    m_queue(QUEUE_COUNT),
    m_buf(),
    m_buf_end(m_buf),
    m_lock(),
    m_eof(false)
{
  m_sftp_handle = NULL;
}

CSFTPFile::~CSFTPFile()
{
  DumpQueue();
  Close();
}

bool CSFTPFile::Open(const CURL& url)
{
  CSingleLock lock(m_lock);
  CSFTPSessionManager& manager = CSFTPSessionManager::GetInstance();
  m_session = manager.CreateSession(url);
  if (!m_session)
  {
    CLog::Log(LOGERROR, "SFTPFile: Failed to allocate read session");
    return false;
  }
  m_read_session = manager.CreateUniqueSession(url);
  if (m_read_session)
  {
    m_file = url.GetFileName().c_str();
    m_sftp_handle = m_read_session->CreateFileHande(m_file);

    return (m_sftp_handle != NULL);
  }
  else
  {
    CLog::Log(LOGERROR, "SFTPFile: Failed to allocate read session");
    return false;
  }
}

void CSFTPFile::Close()
{
  if (m_read_session && m_sftp_handle)
  {
    CSingleLock lock(m_lock);
    m_read_session->CloseFileHandle(m_sftp_handle);
    m_sftp_handle = NULL;
    CSFTPSessionManager::GetInstance().ReturnUniqueSession(m_read_session);
    m_read_session = CSFTPSessionPtr();
  }

  if (m_session)
    m_session = CSFTPSessionPtr();
}

int64_t CSFTPFile::Seek(int64_t iFilePosition, int iWhence)
{
  if (m_read_session && m_sftp_handle)
  {
    CSingleLock lock(m_lock);
    uint64_t position = 0;
    if (iWhence == SEEK_SET)
      position = iFilePosition;
    else if (iWhence == SEEK_CUR)
      position = GetPosition() + iFilePosition;
    else if (iWhence == SEEK_END)
      position = GetLength() + iFilePosition;

    // remove old requests before seeking. If done after seeking it
    // crashes libssh
    DumpQueue();

    if (m_read_session->Seek(m_sftp_handle, position) == 0)
    {
      m_eof = false;
      return GetPosition();
    }
    else
      return -1;
  }
  else
  {
    CLog::Log(LOGERROR, "SFTPFile: Can't seek without a filehandle");
    return -1;
  }
}

unsigned int CSFTPFile::Read(void* lpBuf, int64_t uiBufSize)
{
  CLog::Log(LOGDEBUG, "SFTPFILE::Read: %li bytes requested", uiBufSize);
  if (m_read_session && m_sftp_handle)
  {
    if (m_eof)
      return 0;

    CSingleLock lock(m_lock);
    // request data from server in 32KB portions
    if (FillQueue())
      return 0;

    int cached = m_buf_end - m_buf;

    // maybe cache just matches request size
    if (cached == uiBufSize)
    {
      memcpy(lpBuf, m_buf, uiBufSize);
      m_buf_end = m_buf;
      CLog::Log(LOGDEBUG, "SFTPFile::Read: matching cache size");
      return uiBufSize;
    }

    // we have more data in cache than requested
    if (cached > uiBufSize)
    {
      memcpy(lpBuf, m_buf, uiBufSize);
      // check if remaining memory is more than requested data
      // if yes data would overlap on copy (requires memmove)
      if (uiBufSize >= (cached / 2))
        memcpy(m_buf, m_buf + uiBufSize, cached - uiBufSize);
      else
        memmove(m_buf, m_buf + uiBufSize, cached - uiBufSize);
      m_buf_end -= uiBufSize;
      CLog::Log(LOGDEBUG, "SFTPFile::Read: %i bytes in cache, %li requested",
                cached, uiBufSize);
      return uiBufSize;
    }

    char *position = static_cast<char*>(lpBuf);
    int64_t requested = uiBufSize;

    // if cached data is available first answer from cache
    if (cached > 0)
    {
      memcpy(lpBuf, m_buf, cached);
      m_buf_end = m_buf;
      position += cached;
      requested -= cached;
      CLog::Log(LOGDEBUG, "SFTPFile::Read: added %i bytes from cache", cached);
    }

    // we fill with full request blocks. If block size doesn't match
    // requested size we don't care, receiver has to deal with it (and ask
    // again), to prevent unnecessary cache usage
    int i;
    for (i = 0; i < (requested / REQUEST_SIZE); ++i)
    {
      int rc = m_read_session->Read(m_sftp_handle, m_queue,
                                    position, REQUEST_SIZE);
      if (rc < 0)
      {
        CLog::Log(LOGERROR, "SFTPFile: Failed to read %i", rc);
        return 0;
      }

      // if we get less than REQUEST_SIZE we're at EOF
      if (rc != REQUEST_SIZE)
      {
        CLog::Log(LOGINFO, "CSFTPFile::Read: At EOF");
        m_eof = true;
        return cached + rc + i * REQUEST_SIZE;
      }

      CLog::Log(LOGDEBUG, "SFTPFile::Read: added %i bytes from session",
                REQUEST_SIZE);
      position += REQUEST_SIZE;

      // fill the request queue every (m_queue.max() / 2) iterations
      if (((i + 1) % (QUEUE_COUNT / 2)) == 0)
      {
        CLog::Log(LOGDEBUG, "SFTPFile::Read: Requesting more data");
        if (FillQueue())
          return 0;
      }
    }

    // if there's data we don't use the cache but just return less data
    // than requested
    if (cached || i)
    {
      CLog::Log(LOGDEBUG, "SFTPFile::Read: returning %i bytes",
                cached + i * REQUEST_SIZE);
      return cached + i * REQUEST_SIZE;
    }

    CLog::Log(LOGDEBUG, "SFTPFile::Read: Reading into cache");
    // no data was in cache and less than a request size was requested
    int rc = m_session->Read(m_sftp_handle, m_queue,
                             m_buf, REQUEST_SIZE);
    if (rc < 0)
    {
      CLog::Log(LOGERROR, "SFTPFile: Failed to read %i", rc);
      return 0;
    }

    if (rc != REQUEST_SIZE)
    {
      CLog::Log(LOGINFO, "CSFTPFile::Read: At EOF");
      m_eof = true;
    }

    // check if we got less data than requested
    if (rc <= uiBufSize)
    {
      memcpy(lpBuf, m_buf, rc);
      m_buf_end = m_buf;
      return rc;
    }

    memcpy(lpBuf, m_buf, uiBufSize);
    // check if we can use memcpy or must use memmove
    if (uiBufSize * 2 < rc)
      memmove(m_buf, m_buf + uiBufSize, rc - uiBufSize);
    else
      memcpy(m_buf, m_buf + uiBufSize, rc - uiBufSize);

    m_buf_end = m_buf + rc - uiBufSize;

    CLog::Log(LOGDEBUG, "SFTPFile::Read: Returning %li bytes read into cache",
              uiBufSize);
    return uiBufSize;
  }
  else
    CLog::Log(LOGERROR, "SFTPFile: Can't read without a filehandle");

  return 0;
}

bool CSFTPFile::Exists(const CURL& url)
{
  CSFTPSessionPtr session =
    CSFTPSessionManager::GetInstance().CreateSession(url);
  if (session)
    return session->FileExists(url.GetFileName().c_str());
  else
  {
    CLog::Log(LOGERROR, "SFTPFile: Failed to create session to check exists for '%s'", url.GetFileName().c_str());
    return false;
  }
}

int CSFTPFile::Stat(const CURL& url, struct __stat64* buffer)
{
  CSFTPSessionPtr session =
    CSFTPSessionManager::GetInstance().CreateSession(url);
  if (session)
    return session->Stat(url.GetFileName().c_str(), buffer);
  else
  {
    CLog::Log(LOGERROR, "SFTPFile: Failed to create session to stat for '%s'", url.GetFileName().c_str());
    return -1;
  }
}

int CSFTPFile::Stat(struct __stat64* buffer)
{
  if (m_session)
    return m_session->Stat(m_file.c_str(), buffer);

  CLog::Log(LOGERROR, "SFTPFile: Can't stat without a session for '%s'", m_file.c_str());
  return -1;
}

int64_t CSFTPFile::GetLength()
{
  struct __stat64 buffer;
  if (Stat(&buffer) != 0)
    return 0;
  else
  {
    int64_t length = buffer.st_size;
    return length;
  }
}

/**
 * @warning if requests in the queue are shorter than expected (due to
 * EOF), this position is wrong.
 */
int64_t CSFTPFile::GetPosition()
{
  if (m_read_session && m_sftp_handle)
  {
    CSingleLock lock(m_lock);
    // this postion is after all the requests in the queue are handled
    int pos = m_read_session->GetPosition(m_sftp_handle);
    // subtract number of request to get a (hopefully) correct position
    pos -= REQUEST_SIZE * m_queue.size();
    return pos;
  }

  CLog::Log(LOGERROR, "SFTPFile: Can't get position without a filehandle for '%s'", m_file.c_str());
  return 0;
}

int CSFTPFile::IoControl(EIoControl request, void* param)
{
  if(request == IOCTRL_SEEK_POSSIBLE)
    return 1;

  return -1;
}

/**
 * @brief clear the buffer queue
 *
 * libssh_async_read_begin allocates memory. To prevent memory leaks it's
 * not possible to only delete the handles. This function dumps the data
 * into a buffer, which it throws away.
 *
 * Be aware, that the requests made already advanced the file position.
 * Dumping the queue and restarting at the same position will lead to
 * missing data.
 */
void
CSFTPFile::DumpQueue()
{
  if (!m_read_session || !m_sftp_handle)
    return;

  char buf[REQUEST_SIZE];
  while (! m_queue.empty())
  {
    int rc = m_read_session->Read(m_sftp_handle, m_queue, buf, REQUEST_SIZE);
    if (rc < 0)
      CLog::Log(LOGERROR, "CSFTPFile::DumpQueue: Read error while dumping %i",
                rc);
  }
}

/**
 * @brief fills the request queue
 *
 * this function fills the queue up to the maximum with requests until
 * m_eof is set, in which case it does nothing
 *
 * @return 0 if successfull (even on eof), < 0 if error
 */
int
CSFTPFile::FillQueue()
{
  if (m_eof)
    return 0;

  int rc = m_read_session->InitRead(m_sftp_handle, REQUEST_SIZE, m_queue);
  if (rc < 0)
  {
    CLog::Log(LOGERROR, "CSFTPFile::FillQueue: InitRead failed with %i", rc);
    return -1;
  }

  return 0;
}

#endif
