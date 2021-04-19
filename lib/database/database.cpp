// SPDX-FileCopyrightText: 2020 Jonah Brüchert <jbb@kaidan.im>
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "database.h"

#include <QDebug>
#include <QDir>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>

#include <random>

#include "global.h"

enum Column {
    Id = 0,
    PhoneNumber,
    Text,
    DateTime,
    Read,
    DeliveryState,
    SentByMe
};

Database::Database(QObject *parent)
    : QObject(parent)
    , m_database(QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("messages")))
{
    const auto databaseLocation = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + SL("/spacebar");
    if (!QDir().mkpath(databaseLocation)) {
        qDebug() << "Could not create the database directory at" << databaseLocation;
    }

    m_database.setDatabaseName(databaseLocation + SL("/messages.sqlite"));
    const bool open = m_database.open();

    if (!open) {
        qWarning() << "Could not open call database" << m_database.lastError();
    }

    QSqlQuery createTable(m_database);
    createTable.exec(SL("CREATE TABLE IF NOT EXISTS Messages (id INTEGER, phoneNumber TEXT, text TEXT, time DATETIME, read BOOLEAN, delivered BOOLEAN, sentByMe BOOLEAN)"));

    QSqlQuery migrateV1(m_database);
    migrateV1.exec(SL("CREATE TABLE temp_table AS SELECT * FROM Messages"));
    migrateV1.exec(SL("DROP TABLE Messages"));
    migrateV1.exec(SL("CREATE TABLE IF NOT EXISTS Messages (id TEXT, phoneNumber TEXT, text TEXT, time DATETIME, read BOOLEAN, delivered INTEGER, sentByMe BOOLEAN)"));
    migrateV1.exec(SL("INSERT INTO Messages SELECT * FROM temp_table"));
    migrateV1.exec(SL("DROP TABLE temp_table"));
}

QVector<Message> Database::messagesForNumber(const QString &phoneNumber) const
{
    QVector<Message> messages;

    QSqlQuery fetch(m_database);
    fetch.prepare(SL("SELECT id, phoneNumber, text, time, read, delivered, sentByMe FROM Messages WHERE phoneNumber == :phoneNumber ORDER BY time DESC"));
    fetch.bindValue(SL(":phoneNumber"), phoneNumber);
    fetch.exec();

    while (fetch.next()) {
        Message message;
        message.id = fetch.value(Column::Id).toInt();
        message.phoneNumber = fetch.value(Column::PhoneNumber).toString();
        message.text = fetch.value(Column::Text).toString();
        message.datetime = QDateTime::fromMSecsSinceEpoch(fetch.value(Column::DateTime).value<quint64>());
        message.read = fetch.value(Column::Read).toBool();
        qDebug() << "delivery" << fetch.value(Column::DeliveryState);
        message.deliveryStatus = fetch.value(Column::DeliveryState).value<MessageState>();
        message.sentByMe = fetch.value(Column::SentByMe).toBool();

        messages.append(std::move(message));
    }

    return messages;
}

void Database::updateMessageDeliveryState(const QString &id, const MessageState state)
{
    qDebug() << "Mark as delivered" << id << state;
    QSqlQuery put(m_database);
    put.prepare(SL("UPDATE Messages SET delivered = :state WHERE id == :id"));
    put.bindValue(SL(":id"), id);
    put.bindValue(SL(":state"), state);
    put.exec();
}

void Database::markMessageRead(const int id)
{
    QSqlQuery put(m_database);
    put.prepare(SL("UPDATE Messages SET read = True WHERE id == :id AND NOT read = True"));
    put.bindValue(SL(":id"), id);
    put.exec();
}

QVector<Chat> Database::chats() const
{
    QVector<Chat> chats;

    auto before = QTime::currentTime().msecsSinceStartOfDay();

    QSqlQuery fetch(m_database);
    fetch.exec(SL("SELECT DISTINCT phoneNumber FROM Messages"));

    while (fetch.next()) {
        Chat chat;
        chat.phoneNumber = fetch.value(0).toString();
        chat.unreadMessages = unreadMessagesForNumber(chat.phoneNumber);
        chat.lastMessage = lastMessageForNumber(chat.phoneNumber);
        chat.lastContacted = lastContactedForNumber(chat.phoneNumber);

        chats.append(chat);
    }

    auto after = QTime::currentTime().msecsSinceStartOfDay();
    qDebug() << "TOOK TIME" << after - before;

    return chats;
}

int Database::unreadMessagesForNumber(const QString &phoneNumber) const
{
    QSqlQuery fetch(m_database);
    fetch.prepare(SL("SELECT Count(*) FROM Messages WHERE phoneNumber == :phoneNumber AND read == False"));
    fetch.bindValue(SL(":phoneNumber"), phoneNumber);
    fetch.exec();

    fetch.first();
    return fetch.value(0).toInt();
}

QString Database::lastMessageForNumber(const QString &phoneNumber) const
{
    QSqlQuery fetch(m_database);
    fetch.prepare(SL("SELECT text FROM Messages WHERE phoneNumber == :phoneNumber ORDER BY time DESC LIMIT 1"));
    fetch.bindValue(SL(":phoneNumber"), phoneNumber);
    fetch.exec();

    fetch.first();
    return fetch.value(0).toString();
}

QDateTime Database::lastContactedForNumber(const QString &phoneNumber) const
{
    QSqlQuery fetch(m_database);
    fetch.prepare(SL("SELECT time FROM Messages WHERE phoneNumber == :phoneNumber ORDER BY time DESC LIMIT 1"));
    fetch.bindValue(SL(":phoneNumber"), phoneNumber);
    fetch.exec();

    fetch.first();
    return QDateTime::fromMSecsSinceEpoch(fetch.value(0).toInt());
}

void Database::markChatAsRead(const QString &phoneNumber)
{
    QSqlQuery update(m_database);
    update.prepare(SL("UPDATE Messages SET read = True WHERE phoneNumber = :phoneNumber AND NOT read == True"));
    update.bindValue(SL(":phoneNumber"), phoneNumber);
    update.exec();

    Q_EMIT messagesChanged(phoneNumber);
}

void Database::deleteChat(const QString &phoneNumber)
{
    QSqlQuery update(m_database);
    update.prepare(SL("DELETE FROM Messages WHERE phoneNumber = :phoneNumber"));
    update.bindValue(SL(":phoneNumber"), phoneNumber);
    update.exec();

    Q_EMIT messagesChanged(phoneNumber);
}

void Database::addMessage(const Message &message)
{
    auto before = QTime::currentTime().msecsSinceStartOfDay();
    QSqlQuery putCall(m_database);
    putCall.prepare(SL("INSERT INTO Messages (id, phoneNumber, text, time, read, delivered, sentByMe) VALUES (:id, :phoneNumber, :text, :time, :read, :delivered, :sentByMe)"));
    putCall.bindValue(SL(":id"), message.id);
    putCall.bindValue(SL(":phoneNumber"), message.phoneNumber);
    putCall.bindValue(SL(":text"), message.text);
    putCall.bindValue(SL(":time"), message.datetime.toMSecsSinceEpoch());
    putCall.bindValue(SL(":read"), message.read);
    putCall.bindValue(SL(":sentByMe"), message.sentByMe);
    putCall.bindValue(SL(":delivered"), message.deliveryStatus);
    putCall.exec();

    qDebug() << "WRITING TOOK TIME" << QTime::currentTime().msecsSinceStartOfDay() - before;
}

QString Database::generateRandomId()
{
    QString intermediateId = SL("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890");
    std::shuffle(intermediateId.begin(), intermediateId.end(), std::mt19937(std::random_device()()));
    intermediateId.truncate(10);

    return intermediateId;
}
