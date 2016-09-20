#include "json_rpc_common.h"
#include "transientmap.h"

#include <QMetaMethod>
#include <QDebug>
#include <QJsonValue>

bool JsonRpcCommon::convertArgs(const QMetaMethod& meta_method,
                                const QVariantList& args,
                                QVariantList& converted_args)
{
    if (args.size() != meta_method.parameterCount()) {
        qDebug() << QString("wrong number of arguments to method %1 -- "
                          "expected %2 arguments, but got %3")
                  .arg(QString::fromUtf8(meta_method.methodSignature()))
                  .arg(meta_method.parameterCount())
                  .arg(args.size());
        return false;
    }

    for (int i = 0; i < meta_method.parameterCount(); i++) {
        const auto& arg = args.at(i);
        const auto param_type = meta_method.parameterType(i);

        if (param_type == QMetaType::UnknownType) {
          qDebug() << QString("Tried to invoke method %1 with unknown parameter type %2. Use Q_DECLARE_METATYPE to register.")
                      .arg(QString::fromUtf8(meta_method.name()))
                      .arg(QString::fromUtf8(meta_method.parameterTypes().at(i)));
          return false;
        }

        QVariant copy(arg);

        if (copy.userType() != param_type && param_type != QMetaType::QVariant) {
            if (copy.canConvert(param_type)) {
                if (!copy.convert(param_type)) {
                    // qDebug() << "cannot convert" << arg_type_name
                    //          << "to" << param_type_name;
                    return false;
                }
            } else if (copy.canConvert(qMetaTypeId<jcon::TransientMap>())) {
              if (!copy.convert(qMetaTypeId<jcon::TransientMap>()))
                return false;
              if (!copy.canConvert(param_type))
                return false;
              if (!copy.convert(param_type))
                return false;
            } else {
              return false;
            }
        }

        converted_args << copy;
    }
    return true;
}

bool JsonRpcCommon::convertArgs(const QMetaMethod& meta_method,
                                const QVariantMap& args,
                                QVariantList& converted_args)
{
    const QList<QByteArray> param_types = meta_method.parameterTypes();
    if (args.size() != param_types.size()) {
        // logError(QString("wrong number of arguments to method %1 -- "
        //                  "expected %2 arguments, but got %3")
        //          .arg(meta_method.methodSignature())
        //          .arg(meta_method.parameterCount())
        //          .arg(args.size()));
        return false;
    }

    for (int i = 0; i < param_types.size(); i++) {
        QByteArray param_name = meta_method.parameterNames().at(i);
        if (args.find(param_name) == args.end()) {
            // no arg with param name found
            return false;
        }
        const QVariant& arg = args.value(param_name);
        auto param_type = meta_method.parameterType(i);

        QVariant copy(arg);

        if (copy.type() != param_type) {
            if (copy.canConvert(param_type)) {
                if (!copy.convert(param_type)) {
                    // qDebug() << "cannot convert" << arg_type_name
                    //          << "to" << param_type_name;
                    return false;
                }
            }
        }

        converted_args << copy;
    }
    return true;
}


bool JsonRpcCommon::doCall(QObject* object,
                           const QMetaMethod& meta_method,
                           QVariantList& converted_args,
                           QVariant& return_value)
{
    QList<QGenericArgument> arguments;

    for (int i = 0; i < converted_args.size(); i++) {

        // Notice that we have to take a reference to the argument, else we'd be
        // pointing to a copy that will be destroyed when this loop exits.
        QVariant& argument = converted_args[i];

        // A const_cast is needed because calling data() would detach the
        // QVariant.
        QGenericArgument generic_argument(
            QMetaType::typeName(argument.userType()),
            const_cast<void*>(argument.constData())
        );

        arguments << generic_argument;
    }

    void* ptr = nullptr;
    auto metaType = meta_method.returnType();
    if (metaType != QMetaType::Void && metaType != QMetaType::UnknownType)
      ptr = QMetaType::create(metaType, nullptr);

    if (metaType == QMetaType::UnknownType)
      qDebug() << QString("Trying to call method %1::%2 with unknown return value type. Please register with Q_DECLARE_METATYPE!")
                  .arg(object->metaObject()->className())
                  .arg(QString::fromUtf8(meta_method.name()));

    QGenericReturnArgument return_argument(
        QMetaType::typeName(metaType),
        ptr
    );




    // perform the call
    bool ok = meta_method.invoke(
        object,
        Qt::DirectConnection,
        return_argument,
        arguments.value(0),
        arguments.value(1),
        arguments.value(2),
        arguments.value(3),
        arguments.value(4),
        arguments.value(5),
        arguments.value(6),
        arguments.value(7),
        arguments.value(8),
        arguments.value(9)
    );

    if (!ok) {
        qDebug() << "calling" << meta_method.methodSignature() << "failed.";
        return false;
    }

    QVariant return_argument_variant;

    if (metaType != QMetaType::Void && metaType != QMetaType::QVariant)
      return_argument_variant = QVariant(metaType, ptr);
    else if (metaType == QMetaType::QVariant) {
      return_argument_variant = *static_cast<QVariant*>(ptr);
      metaType = return_argument_variant.userType();
    }

    QMetaType::destruct(metaType, ptr);

    if (return_argument_variant.canConvert<QVariantMap>()) {
      return_argument_variant.convert(qMetaTypeId<QVariantMap>());
      return_value = std::move(return_argument_variant);
    } else if (return_argument_variant.canConvert<jcon::TransientMap>()) {
      return_argument_variant.convert(qMetaTypeId<jcon::TransientMap>());
      return_argument_variant.convert(qMetaTypeId<QVariantMap>());
      return_value = std::move(return_argument_variant);
    } else if (return_argument_variant.canConvert<QString>()) {
      return_argument_variant.convert(qMetaTypeId<QString>());
      return_value = std::move(return_argument_variant);
    }

    return true;
}

// based on https://gist.github.com/andref/2838534.
bool JsonRpcCommon::invoke(QObject* object,
                         const QMetaMethod& meta_method,
                         const QVariantList& args,
                         QVariant& return_value)
{
    if (meta_method.parameterCount() != args.size())
      return false; // Maybe we have to call a different overload!

    QVariantList converted_args;
    if (!convertArgs(meta_method, args, converted_args)) {
        return false;
    }

    return_value = QVariant();

    return doCall(object, meta_method, converted_args, return_value);
}

bool JsonRpcCommon::invoke(QObject* object,
                         const QMetaMethod& meta_method,
                         const QVariantMap& args,
                         QVariant& return_value)
{
    return_value = QVariant();

    QVariantList converted_args;
    if (!convertArgs(meta_method, args, converted_args)) {
      qDebug() << QString("Could not convert arguments. Aborting call of %1.").arg(QString::fromLatin1(meta_method.name()));
      return false;
    }

    return doCall(object, meta_method, converted_args, return_value);
}

QVariant JsonRpcCommon::convertValue(const QJsonValue& parameter) const
{
  const auto parameterAsVariant = parameter.toVariant();
  const auto MapType = qMetaTypeId<QVariantMap>();
  if (parameterAsVariant.type() != MapType)
    return QVariant();

  const auto& map = parameterAsVariant.value<QVariantMap>();

  if (!map.contains(QStringLiteral("typename")) || !map.contains(QStringLiteral("value")))
    return QVariant();

  const auto& typeName = map.value(QStringLiteral("typename")).toString();
  const auto& metaType = QMetaType::type(typeName.toUtf8());

  if (metaType == QMetaType::UnknownType) {
    qDebug() << QString("Typename %1, given in JSON RPC result, is unknown to the client.").arg(typeName);
    return QVariant();
  }

  auto value = map.value(QStringLiteral("value"));

  if (value.canConvert(metaType)) {
    value.convert(metaType);
    return value;
  }

  value.convert(qMetaTypeId<jcon::TransientMap>());

  if (value.canConvert(metaType)) {
    value.convert(metaType);
    return value;
  }

  return QVariant();
}

std::tuple<bool,QJsonValue> JsonRpcCommon::convertValue(const QVariant& parameter) const
{
  const auto type = parameter.type();

  switch (type) {
  case QMetaType::Bool:
  case QMetaType::Int:
  case QMetaType::UInt:
  case QMetaType::LongLong:
  case QMetaType::ULongLong:
  case QMetaType::Float:
  case QMetaType::Double:
  case QMetaType::QString:
  case QMetaType::QStringList:
  case QMetaType::QVariantList:
  case QMetaType::QVariantMap:
  case QMetaType::QVariantHash:
    // These types are directly supported by QJsonValue,
    // no conversion here needed.
    return std::make_tuple(true, QJsonValue::fromVariant(parameter));
  default:
    if (parameter.canConvert<QVariantMap>())
      return std::make_tuple(true, QJsonValue::fromVariant(parameter.toMap()));
    else if (parameter.canConvert<QVariantList>())
      return std::make_tuple(true, QJsonValue::fromVariant(parameter.toList()));
    else if (parameter.canConvert<QString>())
      return std::make_tuple(true, QJsonValue(parameter.toString()));
    else  {
      qDebug() << QString("Type not detected. Could not convert parameter/return value! Current QVariant type is %1").arg(parameter.typeName());
      return std::make_tuple(true, QJsonValue());
    }
  }
}
