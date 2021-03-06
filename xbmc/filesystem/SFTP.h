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
#include <libssh/libssh.h>
#include <libssh/sftp.h>
#include <boost/shared_ptr.hpp>

#if LIBSSH_VERSION_INT < SSH_VERSION_INT(0,3,2)
#define ssh_session SSH_SESSION
#endif

#if LIBSSH_VERSION_INT < SSH_VERSION_INT(0,4,0)
#define sftp_file SFTP_FILE*
#define sftp_session SFTP_SESSION*
#define sftp_attributes SFTP_ATTRIBUTES*
#define sftp_dir SFTP_DIR*
#define ssh_session ssh_session*
#endif

//five secs timeout for SFTP
#define SFTP_TIMEOUT 5

class CSFTPSession;
class CSFTPSessionManager;
typedef boost::shared_ptr<CSFTPSession> CSFTPSessionPtr;

#endif
