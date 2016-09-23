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

        if (meta_method.parameterType(i) == QMetaType::QVariant) {
          QGenericArgument generic_argument("QVariant", &argument);
          arguments << generic_argument;
        } else {
          // A const_cast is needed because calling data() would detach the
          // QVariant.
          QGenericArgument generic_argument(
                QMetaType::typeName(argument.userType()),
                const_cast<void*>(argument.constData())
                );

          arguments << generic_argument;
        }
    }

    void* ptr = nullptr;
    auto returnMetaType = meta_method.returnType();
    if (returnMetaType != QMetaType::Void && returnMetaType != QMetaType::UnknownType)
      ptr = QMetaType::create(returnMetaType, nullptr);

    if (returnMetaType == QMetaType::UnknownType)
      qDebug() << QString("Trying to call method %1::%2 with unknown return value type. Please register with Q_DECLARE_METATYPE!")
                  .arg(object->metaObject()->className())
                  .arg(QString::fromUtf8(meta_method.name()));

    QGenericReturnArgument return_argument(
        QMetaType::typeName(returnMetaType),
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

    if (returnMetaType != QMetaType::Void && returnMetaType != QMetaType::QVariant)
      return_value = QVariant(returnMetaType, ptr);
    else if (returnMetaType == QMetaType::QVariant) {
      return_value = *reinterpret_cast<QVariant*>(ptr);
      returnMetaType = return_value.userType();
    } else if (returnMetaType == QMetaType::Void) {
      return_value = QVariant::fromValue(std::false_type());
    }

    QMetaType::destruct(returnMetaType, ptr);

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

QJsonValue JsonRpcCommon::convertValue(const QVariant& parameter) const
{
  const auto type = parameter.userType();

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
    return QJsonValue::fromVariant(parameter);
  }

  if (type == qMetaTypeId<std::false_type>())
    // In this case return value was void and we construct a QJsonValue which is of type Null.
    return QJsonValue();

  if (parameter.canConvert<QJsonValue>())
    return parameter.value<QJsonValue>();

  if (parameter.canConvert<QVariantMap>())
    return QJsonValue::fromVariant(parameter.toMap());

  if (parameter.canConvert<QVariantList>())
    return QJsonValue::fromVariant(parameter.toList());

  if (parameter.canConvert<QString>())
    return QJsonValue(parameter.toString());

  throw std::invalid_argument(QString("Could not convert given QVariant of type %1").arg(parameter.typeName()).toStdString());
}
