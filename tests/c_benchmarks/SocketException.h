//
// Created by jdanek on 7/8/21.
//

#ifndef QPID_DISPATCH_SOCKETEXCEPTION_H
#define QPID_DISPATCH_SOCKETEXCEPTION_H

#include <string>
class SocketException : public std::exception
{
   private:
    std::string mMessage;
   public:
    explicit SocketException(std::string message, bool captureErrno = false) noexcept;

    SocketException(const SocketException &e) noexcept;

    ~SocketException() noexcept override;
    const char *what() const noexcept override;
};

#endif  // QPID_DISPATCH_SOCKETEXCEPTION_H
