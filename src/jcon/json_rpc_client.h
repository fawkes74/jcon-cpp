#ifndef JSONRPCCLIENT_H
#define JSONRPCCLIENT_H

#include "jcon.h"
#include "json_rpc_endpoint.h"
#include "json_rpc_error.h"
#include "json_rpc_logger.h"
#include "json_rpc_request.h"
#include "json_rpc_result.h"
#include "json_rpc_common.h"
#include "json_rpc_serialization.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>

#include <map>
#include <memory>
#include <utility>

namespace jcon {

class JsonRpcSocket;

class JCON_API JsonRpcClient : public QObject, public JsonRpcCommon
{
    Q_OBJECT

public:
    typedef QString RequestId;

    JsonRpcClient(JsonRpcSocketPtr socket,
                  QObject* parent = nullptr,
                  JsonRpcLoggerPtr logger = nullptr);
    virtual ~JsonRpcClient();

    bool connectToServer(const QString& host, int port);
    void connectToServerAsync(const QString &host, int port);

    void disconnectFromServer();

    bool isConnected() const;

    QHostAddress clientAddress() const {
      return m_endpoint->localAddress();
    }
    int clientPort() const;

    QHostAddress serverAddress() const {
      return m_endpoint->peerAddress();
    }
    int serverPort() const {
      return m_endpoint->peerPort();
    }

    template<typename... T>
    JsonRpcResultPtr call(const QString& method, T&&... params);

    template<typename... T>
    JsonRpcRequestPtr callAsync(const QString& method, T&&... params);

    /// Expand arguments in list before making the RPC call
    JsonRpcResultPtr callExpandArgs(const QString& method,
                                    const QVariantList& params);

    /// Expand arguments in list before making the RPC call
    JsonRpcRequestPtr callAsyncExpandArgs(const QString& method,
                                          const QVariantList& params);

    JsonRpcError lastError() const { return m_last_error; }

    void registerNotificationHandler(QObject* obj, const char* methodName, const QString& notificationName);

signals:
    /// Emitted when a connection has been made to the server.
    void socketConnected(QObject* socket);

    /// Emitted when connection to server is lost.
    void socketDisconnected(QObject* socket);

    /// Emitted when the RPC socket has an error.
    void socketError(QObject* socket, QAbstractSocket::SocketError error);

    void syncCallSucceeded();
    void syncCallFailed();

protected:
    void logError(const QString& msg);

private slots:
    void syncCallResult(const QVariant& result);
    void syncCallError(int code, const QString& message, const QVariant& data);
    void jsonResponseReceived(const QJsonObject& obj);
    void registerSignalHandler(const QString& name);

private:
    static const int CallTimeout = 5000;
    static const QString InvalidRequestId;

    static QString getCallLogMessage(const QString& method,
                                     const QVariantList& params);

    JsonRpcResultPtr waitForSyncCallbacks(const JsonRpcRequest* request);

    std::pair<JsonRpcRequestPtr, QJsonObject>
        prepareCall(const QString& method);

    std::pair<JsonRpcRequestPtr, RequestId> createRequest();
    static RequestId createUuid();
    QJsonObject createRequestJsonObject(const QString& method,
                                        const QString& id);

    void convertToQVariantList(QVariantList& result) { Q_UNUSED(result) }

    template<typename T>
    void convertToQVariantList(QVariantList& result, T&& x);

    template<typename T, typename... Ts>
    void convertToQVariantList(QVariantList& result, T&& head, Ts&&... tail);

    static void getJsonErrorInfo(const QJsonObject& response,
                                 int& code,
                                 QString& message,
                                 QVariant& data);

    void handleNotificationFromServer(const QJsonObject& notification);

    typedef std::map<RequestId, JsonRpcRequestPtr> RequestMap;

    JsonRpcLoggerPtr m_logger;
    JsonRpcEndpointPtr m_endpoint;
    RequestMap m_outstanding_requests;
    QVariant m_last_result;
    JsonRpcError m_last_error;

    QMultiHash<QString,QPair<QObject*,QMetaMethod> > m_registered_notification_handlers;
};

typedef std::shared_ptr<JsonRpcClient> JsonRpcClientPtr;

template<typename... T>
JsonRpcResultPtr JsonRpcClient::call(const QString& method, T&&... params)
{
    JsonRpcRequestPtr req = callAsync(method, std::forward<T>(params)...);
    return waitForSyncCallbacks(req.get());
}

template<typename... T>
JsonRpcRequestPtr JsonRpcClient::callAsync(const QString& method,
                                           T&&... params)
{
    JsonRpcRequestPtr request;
    QJsonObject req_json_obj;
    std::tie(request, req_json_obj) = prepareCall(method);

    QVariantList param_list;
    convertToQVariantList(param_list, std::forward<T>(params)...);
    req_json_obj["params"] = QJsonArray::fromVariantList(param_list);

    m_logger->logInfo(getCallLogMessage(method, param_list));
    m_endpoint->send(QJsonDocument(req_json_obj));

    return request;
}

template<typename T>
void JsonRpcClient::convertToQVariantList(QVariantList& result, T&& x)
{
  const auto value = valueToJson(x);
  result.push_front(value);
}

template<typename T, typename... Ts>
void JsonRpcClient::convertToQVariantList(QVariantList& result,
                                          T&& head, Ts&&... tail)
{
    convertToQVariantList(result, std::forward<Ts>(tail)...);

    const auto value = valueToJson(head);
    result.push_front(value);
}

}

#endif
