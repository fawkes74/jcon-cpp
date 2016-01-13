#ifndef JSON_RPC_OBJECT_H
#define JSON_RPC_OBJECT_H

#include <QVariantMap>
#include <QObject>

class JsonRpcObject
{
  Q_GADGET

public:
  constexpr JsonRpcObject() {}
  virtual JsonRpcObject& operator= (const QVariantMap& other) = 0;
  virtual operator QVariantMap() const = 0;

};

#endif // JSON_RPC_OBJECT_H
