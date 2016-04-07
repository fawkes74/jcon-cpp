#ifndef TRANSIENTMAP_H
#define TRANSIENTMAP_H

#include <QVariant>

namespace jcon {

class TransientMap : public QVariantMap {

public:

  using QVariantMap::QVariantMap;
  TransientMap(const QVariantMap& other) : QVariantMap(other) {}
  TransientMap() : QVariantMap() {}

  QVariantMap toQVariantMap() const {
    return *this;
  }

  static void registerConverters() {
    QMetaType::registerConverter<TransientMap,QVariantMap>(&TransientMap::toQVariantMap);
    QMetaType::registerConverter<QVariantMap,TransientMap>();
  }

};

}

Q_DECLARE_METATYPE(jcon::TransientMap)



#endif // TRANSIENTMAP_H
