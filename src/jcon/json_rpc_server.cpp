#include "json_rpc_server.h"
#include "json_rpc_endpoint.h"
#include "json_rpc_error.h"
#include "json_rpc_file_logger.h"
#include "jcon_assert.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariant>
#include <QMetaMethod>
#include <QSignalSpy>

namespace jcon {

const QString JsonRpcServer::InvalidRequestId = QStringLiteral("");

JsonRpcServer::JsonRpcServer(QObject* parent, JsonRpcLoggerPtr logger)
    : QObject(parent)
    , m_logger(logger)
{
    if (!m_logger) {
        m_logger = std::make_shared<JsonRpcFileLogger>("server_log.txt");
    }
}

JsonRpcServer::~JsonRpcServer()
{
}

void JsonRpcServer::registerService(std::shared_ptr<QObject> service, const QString& domain)
{
  if (m_services.find(domain) != m_services.end()) {
    qDebug() << "Service or namespace already registered.";
    return;
  }

  if (domain.contains('/')) {
    qDebug() << "'/' is not a valid character in a namespace name! Cannot register...";
    return;
  }

  m_services.insert({domain, service});
}

void JsonRpcServer::jsonRequestReceived(const QJsonObject& request,
                                        QObject* socket)
{
    JCON_ASSERT(request.value("jsonrpc").toString() == "2.0");

    if (request.value("jsonrpc").toString() != "2.0") {
        logError("invalid protocol tag");
        return;
    }

    QString method_name = request.value("method").toString();
    if (method_name.isEmpty()) {
        logError("no method present in request");
    }

    QVariant params = request.value("params").toVariant();

    QString request_id = request.value("id").toString(InvalidRequestId);

    auto endpoint = findClient(socket);

    if (!endpoint) {
        logError("invalid client socket, cannot send response");
        return;
    }

    try {

      QVariant return_value;
      if (!dispatch(endpoint, method_name, params, request_id, return_value)) {
          auto msg = QString("method '%1' not found, check name and "
                             "parameter types ").arg(method_name);
          logError(msg);

          // send error response if request had valid ID
          if (request_id != InvalidRequestId) {
              QJsonDocument error =
                  createErrorResponse(request_id,
                                      JsonRpcError::EC_MethodNotFound,
                                      msg);
              endpoint->send(error);
          }
      } else {
          // send response if request had valid ID
          if (request_id != InvalidRequestId) {
              QJsonDocument response = createResponse(request_id,
                                                      return_value,
                                                      method_name);
              endpoint->send(response);
          }
      }
    } catch (const std::exception& e) {
      auto msg = QString("An exception occured. Message was: '%1'").arg(e.what());
      logError(msg);

      if (request_id != InvalidRequestId) {
          QJsonDocument error =
              createErrorResponse(request_id,
                                  JsonRpcError::EC_InternalError,
                                  msg);
          endpoint->send(error);
      }
    }
}



bool JsonRpcServer::dispatch(JsonRpcEndpointPtr endpoint, const QString& complete_method_name,
                             const QVariant& params,
                             const QString& request_id,
                             QVariant& return_value) {

      Q_UNUSED(request_id)

      const auto parts = complete_method_name.split('/');

      if (parts.size() > 2)
        return false; // Cannot handle method names with more than one slash inside. Expecting domain/method_name

      const auto domain = parts.size() == 2 ? parts.first() : QString();
      const auto method_name = parts.size() == 2 ? parts.at(1) : parts.first();

      // If no namespace is given, we will look for a service, registered with an empty namespace name.
      auto result = m_services.find(domain);

      if (result == m_services.end())
        return false; // Not found matching namespace

      auto service = result->second;

      if (method_name == "registerSignalHandler") {
          return_value = registerSignal(endpoint, service, params);
          return true;
      }


      const QMetaObject* meta_obj = service->metaObject();
      for (int i = 0; i < meta_obj->methodCount(); ++i) {
          auto meta_method = meta_obj->method(i);
          if (QString::fromUtf8(meta_method.name()) == method_name) {
              if (params.type() == QVariant::List ||
                  params.type() == QVariant::StringList) {
                  if (invoke(service.get(),
                            meta_method,
                            params.toList(),
                           return_value))
                  {
                  return true;
                  }
              } else if (params.type() == QVariant::Map) {
                  if (invoke(service.get(),
                             meta_method,
                             params.toMap(),
                            return_value))
                  {
                      return true;
                  }
              }
          }
      }

    return false;
}

QVariant JsonRpcServer::registerSignal(JsonRpcEndpointPtr endpoint, std::shared_ptr<QObject> service, const QVariant& params) {
  const auto& metaObject = service->metaObject();

  QString signalNameToLookFor;

  QVariantMap result;
  result.insert("resultCode", true);
  result.insert("resultText", "Success!");

  if (params.type() == QVariant::List || params.type() == QVariant::StringList) {
    const auto list = params.toList();

    if (list.isEmpty())
      return signalResultObject(false, "No signal name given.");

    signalNameToLookFor = list.first().toString();
  } else if (params.type() == QVariant::Map) {
    const auto map = params.toMap();

    if (map.isEmpty())
      return signalResultObject(false, "No signal name given.");

    signalNameToLookFor = (*map.constBegin()).toString();
  }

  if (signalNameToLookFor.isEmpty())
    return signalResultObject(false, "The parameter list is empty. No signal name given.");

  for (int currentMethodIndex = 0; currentMethodIndex < metaObject->methodCount(); currentMethodIndex++) {
    const auto& currentMethod = metaObject->method(currentMethodIndex);

    if (currentMethod.methodType() != QMetaMethod::Signal)
      continue;

    if (currentMethod.methodSignature() != signalNameToLookFor)
      continue;

    qDebug() << QString("Found signal %1 in service %2. Registering now if not already done...")
                .arg(signalNameToLookFor, service->objectName());

    bool signalSpyFound = false;

    for (auto element : m_signalspies) {
      int registeredMethodIndex;
      std::shared_ptr<QSignalSpy>  registeredSignalSpy;
      QObject* registeredService;
      std::tie(registeredService, registeredMethodIndex, std::ignore, registeredSignalSpy) = element;

      if (service.get() == registeredService && registeredMethodIndex == currentMethodIndex) {
        signalSpyFound = true;
        m_signalspies.push_back(std::make_tuple(service.get(), currentMethodIndex, JsonRpcEndpoint::WeakPtr(endpoint), registeredSignalSpy));
        break;
      }
    }

    if (!signalSpyFound) {
      const auto signalName = QByteArray("2").append(currentMethod.methodSignature());
      auto signalSpy = std::make_shared<QSignalSpy>(service.get(), signalName.constData());
      signalSpy->setParent(this);
      QObject::connect(service.get(), signalName, this, SLOT(serviceSignalEmitted()));
      m_signalspies.push_back(std::make_tuple(service.get(), currentMethodIndex, JsonRpcEndpoint::WeakPtr(endpoint), signalSpy));
    }

    QObject::connect(endpoint.get(), &QObject::destroyed, this, &JsonRpcServer::handleDestroyedEndpoint);

    return signalResultObject(true, "Signal found and registered.");
  }

  return signalResultObject(false, "Signal not found.");
}

void JsonRpcServer::handleDestroyedEndpoint() {

  auto it = m_signalspies.begin();
  while (it != m_signalspies.end()) {
    const auto currentEndpoint = std::get<2>(*it);

    if (currentEndpoint.expired()) {
      const auto currentSpy = std::get<3>(*it);      
      if (currentSpy.use_count() == 2) {
        // We have to check for use count == 2, since there a two shared pointers left,
        // one in m_signalspies and one here in currentSpy.
        auto sender = std::get<0>(*it);
        const auto signalIndex = std::get<1>(*it);
        const auto signature = "2" + sender->metaObject()->method(signalIndex).methodSignature();
        QObject::disconnect(sender, signature, this, SLOT(serviceSignalEmitted()));
      }
      it = m_signalspies.erase(it);
    } else {
      ++it;
    }
  }

}

void JsonRpcServer::serviceSignalEmitted() {
  if (sender() == nullptr)
    return;

  const auto signal = sender()->metaObject()->method(senderSignalIndex());
  QJsonDocument notificationDocument;

  for (auto element : m_signalspies) {
    int currentSignalIndex;
    QObject* currentSender;
    std::shared_ptr<QSignalSpy> currentSignalSpy;
    JsonRpcEndpoint::WeakPtr currentEndpoint;
    std::tie(currentSender, currentSignalIndex, currentEndpoint, currentSignalSpy) = element;

    if (currentEndpoint.expired()) {
      qDebug() << "There is an non existing endpoint in signal spy list. Probably a programming error...";
      continue;
    }

    JsonRpcEndpointPtr currentEndpointAccess(currentEndpoint);

    if (sender() == currentSender && senderSignalIndex() == currentSignalIndex) {
      if (notificationDocument.isNull()) {
        const auto parameters = currentSignalSpy->takeFirst();

        QJsonArray paramArray;
        for (int i = 0; i < parameters.count(); i++) {
          const auto& parameter = parameters.at(i);
          const auto parameterName = signal.parameterNames().at(i);
          const auto parameterType = signal.parameterTypes().at(i);

          try {
            paramArray.append(convertValue(parameter));

          } catch (const std::invalid_argument&) {
            qDebug() << QString("Could not encode parameter %1 of type %2 to a json representation. Cannot send signal...")
                        .arg(QString::fromUtf8(parameterName), QString::fromUtf8(parameterType));
            return;
          }
        }

        QString name;

        for(auto pair : m_services) {
          if (sender() == pair.second.get()) {
            name = pair.first;
            break;
          }
        }

        if (!name.isEmpty()) {
          name.append("/");
          name.append(signal.name().constData());
        } else {
          name = QString(signal.name().constData());
        }

        QJsonObject notificationObject {
          { "jsonrpc", "2.0" },
          { "method", std::move(name) },
          { "params", std::move(paramArray) }
        };

        notificationDocument = QJsonDocument(notificationObject);
        qDebug() << "Sending RPC notification for signal" << currentSignalSpy->signal();
      }


      currentEndpointAccess->send(notificationDocument);
    }
  }

  if (notificationDocument.isNull()) {
    qDebug() << "Slot triggered, but no signal spyed.";
  }
}


QJsonDocument JsonRpcServer::createResponse(const QString& request_id,
                                            const QVariant& return_value,
                                            const QString& method_name)
{

    QJsonObject res_json_obj {
        { "jsonrpc", "2.0" },
        { "id", request_id }
    };

    try {
      auto return_json_value = convertValue(return_value);
      res_json_obj.insert(QStringLiteral("result"), return_json_value);
      return QJsonDocument(res_json_obj);

    } catch (std::invalid_argument&) {
        auto msg =
            QString("method '%1' has unknown return type: %2")
            .arg(method_name)
            .arg(return_value.type());
        logError(msg);
        return createErrorResponse(request_id,
                                   JsonRpcError::EC_InvalidRequest,
                                   msg);
    }
}

QJsonDocument JsonRpcServer::createErrorResponse(const QString& request_id,
                                                 int code,
                                                 const QString& message)
{
    QJsonObject error_object {
        { "code", code },
        { "message", message }
    };

    QJsonObject res_json_obj {
        { "jsonrpc", "2.0" },
        { "error", error_object },
        { "id", request_id }
    };
    return QJsonDocument(res_json_obj);
}

void JsonRpcServer::logInfo(const QString& msg)
{
    m_logger->logInfo("JSON RPC server: " + msg);
}

void JsonRpcServer::logError(const QString& msg)
{
  m_logger->logError("JSON RPC server error: " + msg);
}



}
