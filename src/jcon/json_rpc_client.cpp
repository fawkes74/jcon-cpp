#include "json_rpc_client.h"
#include "json_rpc_file_logger.h"
#include "json_rpc_success.h"
#include "jcon_assert.h"
#include "string_util.h"

#include <QSignalSpy>
#include <QUuid>

#include <memory>

namespace jcon {

const QString JsonRpcClient::InvalidRequestId = "";

JsonRpcClient::JsonRpcClient(JsonRpcSocketPtr socket,
                             QObject* parent,
                             JsonRpcLoggerPtr logger)
    : QObject(parent)
    , m_logger(logger)
{
    if (!m_logger) {
        m_logger = std::make_shared<JsonRpcFileLogger>("client_log.txt");
    }

    m_endpoint = std::make_shared<JsonRpcEndpoint>(socket, m_logger, this);

    connect(m_endpoint.get(), &JsonRpcEndpoint::socketConnected,
            this, &JsonRpcClient::socketConnected);

    connect(m_endpoint.get(), &JsonRpcEndpoint::socketDisconnected,
            this, &JsonRpcClient::socketDisconnected);

    connect(m_endpoint.get(), &JsonRpcEndpoint::socketError,
            this, &JsonRpcClient::socketError);
}

JsonRpcClient::~JsonRpcClient()
{
    disconnectFromServer();
}

JsonRpcResultPtr
JsonRpcClient::waitForSyncCallbacks(const JsonRpcRequest* request)
{
    m_last_result = QVariant();
    m_last_error = JsonRpcError();

    connect(request, &JsonRpcRequest::result,
            this, &JsonRpcClient::syncCallResult);

    connect(request, &JsonRpcRequest::error,
            this, &JsonRpcClient::syncCallError);

    QSignalSpy res_spy(this, &JsonRpcClient::syncCallSucceeded);
    QSignalSpy err_spy(this, &JsonRpcClient::syncCallFailed);
    QTime timer;
    timer.start();
    while (res_spy.isEmpty() && err_spy.isEmpty() &&
           timer.elapsed() < CallTimeout) {
        QCoreApplication::processEvents();
    }
    if (!res_spy.isEmpty()) {
        return std::make_shared<JsonRpcSuccess>(m_last_result);
    } else if (!err_spy.isEmpty()) {
        return std::make_shared<JsonRpcError>(m_last_error);
    } else {
        return std::make_shared<JsonRpcError>(
            JsonRpcError::EC_InternalError,
            "RPC call timed out"
        );
    }
}

JsonRpcResultPtr JsonRpcClient::callExpandArgs(const QString& method,
                                               const QVariantList& params)
{
    JsonRpcRequestPtr req = callAsyncExpandArgs(method, params);
    return waitForSyncCallbacks(req.get());
}

JsonRpcRequestPtr JsonRpcClient::callAsyncExpandArgs(const QString& method,
                                                     const QVariantList& params)
{
    JsonRpcRequestPtr request;
    QJsonObject req_json_obj;
    std::tie(request, req_json_obj) = prepareCall(method);

    if (params.size() > 0) {
        req_json_obj["params"] = QJsonArray::fromVariantList(params);
    }

    m_logger->logInfo(getCallLogMessage(method, params));
    m_endpoint->send(QJsonDocument(req_json_obj));

    return request;
}

std::pair<JsonRpcRequestPtr, QJsonObject>
JsonRpcClient::prepareCall(const QString& method)
{
    JsonRpcRequestPtr request;
    RequestId id;
    std::tie(request, id) = createRequest();
    m_outstanding_requests[id] = request;
    QJsonObject req_json_obj = createRequestJsonObject(method, id);
    return std::make_pair(request, req_json_obj);
}

std::pair<JsonRpcRequestPtr, JsonRpcClient::RequestId>
JsonRpcClient::createRequest()
{
    auto id = createUuid();
    auto request = std::make_shared<JsonRpcRequest>(this, id);
    return std::make_pair(request, id);
}

JsonRpcClient::RequestId JsonRpcClient::createUuid()
{
    RequestId id = QUuid::createUuid().toString();
    int len = id.length();
    id = id.left(len - 1).right(len - 2);
    return id;
}

QJsonObject JsonRpcClient::createRequestJsonObject(const QString& method,
                                                   const QString& id)
{
    return QJsonObject {
        { "jsonrpc", "2.0" },
        { "method", method },
        { "id", id }
    };
}

bool JsonRpcClient::connectToServer(const QString& host, int port)
{
    if (!m_endpoint->connectToHost(host, port)) {
        return false;
    }

    connect(m_endpoint.get(), &JsonRpcEndpoint::jsonObjectReceived,
            this, &JsonRpcClient::jsonResponseReceived);

    return true;
}

void JsonRpcClient::disconnectFromServer()
{
    m_endpoint->disconnectFromHost();
    m_endpoint->disconnect(this);
}

bool JsonRpcClient::isConnected() const
{
    return m_endpoint->isConnected();
}

QHostAddress JsonRpcClient::clientAddress() const
{
    return m_endpoint->localAddress();
}

int JsonRpcClient::clientPort() const
{
    return m_endpoint->localPort();
}

QHostAddress JsonRpcClient::serverAddress() const
{
    return m_endpoint->peerAddress();
}

int JsonRpcClient::serverPort() const
{
    return m_endpoint->peerPort();
}

void JsonRpcClient::syncCallResult(const QVariant& result)
{
    m_last_result = result;
    emit syncCallSucceeded();
}

void JsonRpcClient::syncCallError(int code,
                                  const QString& message,
                                  const QVariant& data)
{
    m_last_error = JsonRpcError(code, message, data);
    emit syncCallFailed();
}

void JsonRpcClient::jsonResponseReceived(const QJsonObject& response)
{
    JCON_ASSERT(response["jsonrpc"].toString() == "2.0");

    if (response.value("jsonrpc").toString() != "2.0") {
        logError("invalid protocol tag");
        return;
    }

    if (response.value("error").isObject()) {
        int code;
        QString msg;
        QVariant data;
        getJsonErrorInfo(response, code, msg, data);
        logError(QString("(%1) - %2").arg(code).arg(msg));

        RequestId id = response.value("id").toString(InvalidRequestId);
        if (id != InvalidRequestId) {
            auto it = m_outstanding_requests.find(id);
            if (it == m_outstanding_requests.end()) {
                logError(QString("got error response for non-existing "
                                 "request: %1").arg(id));
                return;
            }
            emit it->second->error(code, msg, data);
            m_outstanding_requests.erase(it);
        }

        return;
    }

    if (response["result"].isUndefined()) {
        logError("result is undefined");
        return;
    }

    RequestId id = response.value("id").toString(InvalidRequestId);
    if (id == InvalidRequestId) {
        logError("response ID is undefined");
        return;
    }

    QVariant result = response.value("result").toVariant();

    auto it = m_outstanding_requests.find(id);
    if (it == m_outstanding_requests.end()) {
        logError(QString("got response to non-existing request: %1").arg(id));
        return;
    }

    const auto QVariantMapType = QMetaType::type("QVariantMap");
    if (result.type() == QVariantMapType) {
      const auto& map = result.value<QVariantMap>();

      if (map.contains("typeName") && map.contains("object") && map.value("object").type() == QVariantMapType ) {
        const auto& typeName = map.value("typeName", "").toString();
        const auto& metaType = QMetaType::type(typeName.toUtf8());

        if (metaType != QMetaType::UnknownType && QMetaType::hasRegisteredConverterFunction(QVariantMapType, metaType)) {
          const auto& objectMap = map.value("object").value<QVariantMap>();
          auto ptr = QMetaType::create(metaType);
          QMetaType::convert(&objectMap, QMetaType::type("QVariantMap"), ptr, metaType);

          QVariant resultObj(metaType, ptr);
          emit it->second->result(resultObj);
          QMetaType::destroy(metaType, ptr);
          m_outstanding_requests.erase(it);
          return;
        }
      }
    }

    emit it->second->result(result);
    m_outstanding_requests.erase(it);
}

void JsonRpcClient::getJsonErrorInfo(const QJsonObject& response,
                                     int& code,
                                     QString& message,
                                     QVariant& data)
{
    QJsonObject error = response["error"].toObject();
    code = error["code"].toInt();
    message = error["message"].toString("unknown error");
    data = error.value("data").toVariant();
}

QString JsonRpcClient::getCallLogMessage(const QString& method,
                                         const QVariantList& params)
{
    return QString("Calling RPC method: '%1' with arguments: %2")
        .arg(method).arg(variantListToString(params));
}

void JsonRpcClient::logError(const QString& msg)
{
    m_logger->logError("JSON RPC client error: " + msg);
}

}
