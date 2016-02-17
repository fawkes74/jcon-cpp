#ifndef JSON_RPC_RESULT_H
#define JSON_RPC_RESULT_H

#include <QVariant>

#include "jcon.h"

namespace std {
    template<typename T>
    class shared_ptr;
}

namespace jcon {

class JCON_API JsonRpcResult
{
public:
    virtual ~JsonRpcResult() {}

    virtual bool isSuccess() const = 0;
    virtual QVariant result() const = 0;
    virtual QString toString() const = 0;
};

typedef std::shared_ptr<JsonRpcResult> JsonRpcResultPtr;

}

#endif
