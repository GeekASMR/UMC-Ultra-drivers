/*
 * ClassFactory - COM 类工厂实现
 */

#include "ClassFactory.h"
#include "../driver/BehringerASIO.h"
#include "../utils/Logger.h"

#define LOG_MODULE "ClassFactory"

LONG ClassFactory::s_serverLocks = 0;

ClassFactory::ClassFactory()
    : m_refCount(1)
{
    LOG_DEBUG(LOG_MODULE, "ClassFactory created");
}

ClassFactory::~ClassFactory() {
    LOG_DEBUG(LOG_MODULE, "ClassFactory destroyed");
}

STDMETHODIMP ClassFactory::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;

    if (riid == IID_IUnknown || riid == IID_IClassFactory) {
        *ppv = static_cast<IClassFactory*>(this);
        AddRef();
        return S_OK;
    }

    *ppv = nullptr;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) ClassFactory::AddRef() {
    return InterlockedIncrement(&m_refCount);
}

STDMETHODIMP_(ULONG) ClassFactory::Release() {
    ULONG ref = InterlockedDecrement(&m_refCount);
    if (ref == 0) {
        delete this;
    }
    return ref;
}

STDMETHODIMP ClassFactory::CreateInstance(LPUNKNOWN pUnk, REFIID riid, void** ppv) {
    LOG_INFO(LOG_MODULE, "CreateInstance called");

    if (!ppv) return E_POINTER;
    *ppv = nullptr;

    // No aggregation support
    if (pUnk != nullptr) {
        return CLASS_E_NOAGGREGATION;
    }

    return BehringerASIO::CreateInstance(pUnk, riid, ppv);
}

STDMETHODIMP ClassFactory::LockServer(BOOL fLock) {
    if (fLock) {
        InterlockedIncrement(&s_serverLocks);
    } else {
        InterlockedDecrement(&s_serverLocks);
    }
    LOG_DEBUG(LOG_MODULE, "LockServer(%d), locks=%ld", fLock, s_serverLocks);
    return S_OK;
}
