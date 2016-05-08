#ifndef JSON_RPC_SERIALIZATION_H
#define JSON_RPC_SERIALIZATION_H

#include <QJsonValue>
#include <QDebug>
#include <QVariant>

#include "transientmap.h"

namespace jcon {
  template <typename T>
  inline QVariant valueToJson(const T& x) {
    auto value = QVariant::fromValue(x);

    if (value.canConvert(qMetaTypeId<QVariantMap>()))
      value.convert(qMetaTypeId<QVariantMap>());
    else if (value.canConvert(qMetaTypeId<TransientMap>())) {
      value.convert(qMetaTypeId<TransientMap>());
      value.convert(qMetaTypeId<QVariantMap>());
    } else if (value.canConvert(qMetaTypeId<QString>()))
      value.convert(qMetaTypeId<QString>());

    return value;
  }

  template <>
  inline QVariant valueToJson<QVariant>(const QVariant& v) {
    return v;
  }
}

#endif // JSON_RPC_SERIALIZATION_H
