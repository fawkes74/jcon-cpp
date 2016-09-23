#ifndef JSONRPCCOMMON_H
#define JSONRPCCOMMON_H

#include <type_traits>

#include <QVariantList>
#include <QVariantMap>

class QMetaMethod;

class JsonRpcCommon
{

protected:
  bool convertArgs(const QMetaMethod& meta_method,
                   const QVariantList& args,
                   QVariantList& converted);

  bool convertArgs(const QMetaMethod& meta_method,
                   const QVariantMap& args,
                   QVariantList& converted);

  bool invoke(QObject* object,
            const QMetaMethod& meta_method,
            const QVariantList& args,
            QVariant& return_value);

  bool invoke(QObject* object,
            const QMetaMethod& meta_method,
            const QVariantMap& args,
            QVariant& return_value);

  bool doCall(QObject* object,
              const QMetaMethod& meta_method,
              QVariantList& converted_args,
              QVariant& return_value);

  QJsonValue convertValue(const QVariant& parameter) const;

};

Q_DECLARE_METATYPE(std::false_type)

#endif // JSONRPCCOMMON_H
