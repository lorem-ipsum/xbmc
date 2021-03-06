#pragma once
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

#include "system.h"
#ifdef HAS_FILESYSTEM_SFTP
#include "SFTP.h"
#include "IFile.h"
#include "threads/CriticalSection.h"
#include "utils/BufferQueue.h"

// server request size
#define REQUEST_SIZE (32 * 1024)

namespace XFILE
{
  class CSFTPFile : public IFile
  {
  public:
    CSFTPFile();
    virtual ~CSFTPFile();
    virtual void Close();
    virtual int64_t Seek(int64_t iFilePosition, int iWhence = SEEK_SET);
    virtual unsigned int Read(void* lpBuf, int64_t uiBufSize);
    virtual bool Open(const CURL& url);
    virtual bool Exists(const CURL& url);
    virtual int Stat(const CURL& url, struct __stat64* buffer);
    virtual int Stat(struct __stat64* buffer);
    virtual int64_t GetLength();
    virtual int64_t GetPosition();
    virtual int     GetChunkSize() { return REQUEST_SIZE; }
    virtual int     IoControl(EIoControl request, void* param);

  private:
    void DumpQueue();
    int FillQueue();

    CStdString m_file;
    CSFTPSessionPtr m_session;
    CSFTPSessionPtr m_read_session;
    sftp_file m_sftp_handle;
    CBufferQueue<int> m_queue;
    char m_buf[REQUEST_SIZE];
    char *m_buf_end;
    CCriticalSection m_lock;
    bool m_eof;
  };
}
#endif
