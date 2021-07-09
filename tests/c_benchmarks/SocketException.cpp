#include "SocketException.h"

#include <cerrno>
#include <cstring>
#include <string>

SocketException::SocketException(std::string message, bool captureErrno) noexcept : mMessage(std::move(message))
{
    if (captureErrno) {
        message.append(": ");
        message.append(strerror(errno));
    }
}

SocketException::SocketException(const SocketException &e) noexcept : mMessage(e.mMessage)
{
}

SocketException::~SocketException() noexcept = default;

const char *SocketException::what() const noexcept
{
    return mMessage.c_str();
}