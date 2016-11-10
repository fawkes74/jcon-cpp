#ifndef JSONRPCSERVER_H
#define JSONRPCSERVER_H

#include "jcon.h"
#include "json_rpc_logger.h"

#include <QAbstractSocket>

#include <memory>

#include "json_rpc_endpoint.h"
#include "json_rpc_common.h"

class QSignalSpy;

namespace jcon {

class JsonRpcSocket;

class JCON_API JsonRpcServer : public QObject, public JsonRpcCommon
{
    Q_OBJECT

  enum class PointerType {
    SharedPointer,
    RawPointer
  };

  struct UniversalPointer {
    PointerType type = PointerType::SharedPointer;
    std::shared_ptr<QObject> sharedPtr;
    QObject* rawPtr = nullptr;

    UniversalPointer(const std::shared_ptr<QObject>& ptr) :
      type(PointerType::SharedPointer),
      sharedPtr(ptr),
      rawPtr(nullptr)
    { }

    UniversalPointer(QObject* ptr) :
      type(PointerType::RawPointer),
      rawPtr(ptr)
    { }

    QObject* operator->() {
      if (type == PointerType::SharedPointer)
        return sharedPtr.get();
      else
        return rawPtr;
    }

    QObject* get() {
      if (type == PointerType::SharedPointer)
        return sharedPtr.get();
      else
        return rawPtr;
    }
  };

public:
    JsonRpcServer(QObject* parent = nullptr, JsonRpcLoggerPtr logger = nullptr);
    virtual ~JsonRpcServer();

    virtual void registerService(const std::shared_ptr<QObject>& service, const QString& domain = QString());
    virtual void registerService(QObject* service, const QString& domain = QString());

    virtual bool listen(int port) = 0;
    virtual void close() = 0;

protected:
    virtual JsonRpcEndpointPtr findClient(QObject* socket) = 0;

signals:
    /// Emitted when the RPC socket has an error.
    void socketError(QObject* socket, QAbstractSocket::SocketError error);

public slots:
    void jsonRequestReceived(const QJsonObject& request, QObject* socket);

protected slots:
    virtual void newConnection() = 0;
    virtual void clientDisconnected(QObject* client_socket) = 0;
    void serviceSignalEmitted();


protected:
    void logInfo(const QString& msg);
    void logError(const QString& msg);
    JsonRpcLoggerPtr log() { return m_logger; }

    QVariant registerSignal(JsonRpcEndpointPtr endpoint, UniversalPointer service, const QVariant& params);
    void handleDestroyedEndpoint();
    static inline QVariant signalResultObject(bool success, QString&& text) {
      return QVariantMap({{"resultCode", success}, {"resultText", text}}); }

private:
    static const QString InvalidRequestId;

    bool dispatch(JsonRpcEndpointPtr endpoint, const QString& complete_method_name,
                  const QVariant& params,
                  const QString& request_id,
                  QVariant& return_value);

    QJsonDocument createResponse(const QString& request_id,
                                 const QVariant& return_value,
                                 const QString& method_name);
    QJsonDocument createErrorResponse(const QString& request_id,
                                      int code,
                                      const QString& message);

    JsonRpcLoggerPtr m_logger;
    std::map<QString, UniversalPointer> m_services;
    std::vector<std::tuple<QObject*,int, JsonRpcEndpoint::WeakPtr, std::shared_ptr<QSignalSpy>>> m_signalspies;
};

}

#endif
