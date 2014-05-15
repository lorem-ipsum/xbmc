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
#include "utils/StdString.h"

class CURL;
class CSFTPSessionManagerImpl;

class CSFTPSessionManager
{
public:
  static CSFTPSessionManager& GetInstance();

  CSFTPSessionPtr CreateSession(const CURL &url);
  CSFTPSessionPtr CreateSession(const CStdString &host,
                                unsigned int port,
                                const CStdString &username,
                                const CStdString &password);
  CSFTPSessionPtr CreateUniqueSession(const CURL &url);
  CSFTPSessionPtr CreateUniqueSession(const CStdString &host,
                                      unsigned int port,
                                      const CStdString &username,
                                      const CStdString &password);
  void ReturnUniqueSession(CSFTPSessionPtr);

  void ClearOutIdleSessions();
  void DisconnectAllSessions();
private:
  CSFTPSessionManager();
  CSFTPSessionManager(const CSFTPSessionManager&);
  CSFTPSessionManager& operator=(const CSFTPSessionManager&);
  ~CSFTPSessionManager();

  CSFTPSessionManagerImpl* pimpl;
};

#endif
