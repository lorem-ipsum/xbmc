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

#pragma once

#include <deque>
#include "threads/CriticalSection.h"
#include "threads/SingleLock.h"

/**
 * Basic implementation of a synchronized queue
 *
 * It's designed to be compatible with the draft for a buffer queue
 * currently in evaluation for the c++ standard, found here:
 * http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2013/n3533.html
 *
 * Currently it uses a deque to make the implementation as easy as
 * possible. A complete implementation can be found in Googles
 * concurrency library found here:
 * http://code.google.com/p/google-concurrency-library/
 */
template <typename T>
class CBufferQueue
{
public:
  CBufferQueue(size_t max);

  T value_pop();
  void push(const T&);

  size_t size() const;
  bool empty() const;

  // not in standard proposal
  void resize(size_t new_max);

private:
  CBufferQueue();
  CBufferQueue(const CBufferQueue&);
  CBufferQueue& operator=(const CBufferQueue&);

  CCriticalSection m_lock;
  std::deque<T> m_deque;
  size_t m_max;
};

template <typename T>
inline
CBufferQueue::CBufferQueue(size_t max)
  : m_lock(),
    m_deque(),
    m_max(max)
{
}

template <typename T>
inline
T
CBufferQueue<T>::value_pop()
{
  CSingleLock lock(m_lock);
  T value = m_deque.front();
  m_deque.pop_front();
  return value;
}

template <typename T>
inline
void
CBufferQueue<T>::push(const T& value)
{
  CSingleLock lock(m_lock);
  m_deque.push_back(value);
}

template <typename T>
inline
size_t
CBufferQueue<T>::size() const
{
  return m_deque.size();
}

template <typename T>
inline
bool
CBufferQueue<T>::empty() const
{
  return m_deque.empty();
}

template <typename T>
inline
bool
CBufferQueue<T>::full() const
{
  return size() >= full;
}

template <typename T>
inline
void
CBufferQueue<T>::resize(size_t new_max)
{
  CSingleLock lock(m_lock);
  max = new_max;
}
