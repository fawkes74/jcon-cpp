#ifndef JSON_RPC_SERIALIZATION_H
#define JSON_RPC_SERIALIZATION_H

#include <QJsonValue>
#include <QDebug>
#include <QVariant>

namespace jcon {
  template <typename T>
  inline QVariant valueToJson(const T& x) {
    auto value = QVariant::fromValue(x);
    const auto type = qMetaTypeId<QVariantMap>();

    if (value.type() == QVariant::UserType) {
      if (value.canConvert(type))
        value.convert(type);
    }
    return value;
  }
}

#endif // JSON_RPC_SERIALIZATION_H
