/*
 * Copyright (C) Lichuang
 *
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <strings.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "net_utility.h"
#include "log.h"
namespace {
static const uint32 kMaxTryConnecTime = 3;
static const uint32 kConnectFailSleepTime = 1;
};
namespace eventrpc {
int NetUtility::Connect(const NetAddress &address) {
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    VLOG_ERROR() << "create socket error: " << strerror(errno);
    return -1;
  }

  const struct sockaddr_in *addr = address.address();
  uint32 count = 0;
  while (count < kMaxTryConnecTime) {
    if (::connect(fd, (struct sockaddr *)(addr),
                  sizeof(*addr)) < 0) {
      ++count;
      sleep(count * kConnectFailSleepTime);
      VLOG_ERROR() << "connect to " << address.DebugString()
        << " fail, try again...";
    }
    break;
  }
  if (fd < 0) {
    ::close(fd);
    return -1;
  }
  if (!NetUtility::SetNonBlocking(fd)) {
    VLOG_ERROR() << "SetNonBlocking fail";
    ::close(fd);
    return -1;
  }

  return fd;
}

int NetUtility::Listen(const NetAddress &address) {
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    VLOG_ERROR() << "create socket error: " << strerror(errno);
    return -1;
  }

  int one = 1;
  if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
                   &one, sizeof(one)) < 0) {
    return -1;
  }
  if (!NetUtility::SetNonBlocking(fd)) {
    return -1;
  }

  const struct sockaddr_in *addr = address.address();
  if (::bind(fd, (struct sockaddr *)(addr), sizeof(*addr)) < 0) {
    VLOG_ERROR() << "bind socket to " << address.DebugString()
      << " error: " << strerror(errno);
    return -1;
  }

  if (::listen(fd, 10000) < 0) {
    VLOG_ERROR() << "listen socket to " << address.DebugString()
      << " error: " << strerror(errno);
    return -1;
  }

  return fd;
}

bool NetUtility::Accept(int listen_fd,
                        struct sockaddr_in *addr,
                        int *fd) {
  int accept_fd = 0, errno_copy = 0;
  socklen_t length = sizeof(*addr);

  do {
    accept_fd = ::accept(listen_fd,
                         (struct sockaddr *)(addr),
                         &length);
    if (accept_fd > 0) {
      break;
    }
    errno_copy = errno;
    if (accept_fd < 0) {
      if (errno_copy == EINTR) {
        continue;
      } else if (errno_copy == EAGAIN) {
        *fd = 0;
        return true;
      } else {
        VLOG_ERROR() << "Fail to accept, "
          << " error: "
          << strerror(errno_copy);
        return false;
      }
    }
  } while (true);

  if (!NetUtility::SetNonBlocking(accept_fd)) {
    close(accept_fd);
    return false;
  }

  *fd = accept_fd;
  return true;
}

bool NetUtility::Send(int fd, const void *buf, size_t count,
                      int *length) {
  int ret = 0, errno_copy = 0;
  *length = 0;
  do {
    ret = ::send(fd, ((char*)buf + (*length)),
                 count, MSG_NOSIGNAL | MSG_DONTWAIT);
    errno_copy = errno;
    if (ret > 0) {
      count -= ret;
      *length += ret;
      continue;
    }
    if (ret == 0) {
      return true;
    }
    if (errno_copy == EINTR) {
      continue;
    }
    if (errno_copy == EAGAIN || errno_copy == EWOULDBLOCK) {
      return true;
    }
    VLOG_ERROR() << "send error: " << strerror(errno_copy);
    return false;
  } while (ret > 0 && count > 0);

  return true;
}

bool NetUtility::Recv(int fd, void *buf, size_t count, int *length) {
  int ret = 0, errno_copy = 0;
  *length = 0;
  do {
    ret = ::recv(fd, (char*)buf + (*length), count, MSG_DONTWAIT);
    errno_copy = errno;
    if (ret == 0) {
      // socket has been closed
      VLOG_ERROR() << "recv error: " << strerror(errno_copy);
      return true;
    }
    if (ret > 0) {
      count -= ret;
      *length += ret;
      if (count == 0) {
        return true;
      }
      continue;
    }
    // if or not try again depends on the error code
    if (errno_copy == EINTR) {
      continue;
    }
    if (errno_copy == EAGAIN || errno_copy == EWOULDBLOCK) {
      return true;
    }
    VLOG_ERROR() << "recv error: " << strerror(errno_copy);
    return false;
  } while (ret > 0 && count > 0);

  return true;
}

bool NetUtility::SetNonBlocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1) {
    return false;
  }

  if (-1 == fcntl(fd, F_SETFL, flags | O_NONBLOCK)) {
    return false;
  }

  return true;
}
};
