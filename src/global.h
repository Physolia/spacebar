#include <QStringLiteral>
#include <QByteArray>
#include <TelepathyQt/SharedPtr>
#pragma once

class ChannelHandler;

constexpr auto APPLICATION_ID = "org.kde.spacebear";

#define SL QStringLiteral
#define BL QByteArrayLiteral

using ChannelHandlerPtr = Tp::SharedPtr<ChannelHandler>;
