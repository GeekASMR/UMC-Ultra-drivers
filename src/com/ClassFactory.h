/*
 * ClassFactory - COM 类工厂
 * 
 * 用于从 COM 子系统创建 BehringerASIO 驱动实例
 */

#pragma once

#include <windows.h>
#include <unknwn.h>

class ClassFactory : public IClassFactory {
public:
    ClassFactory();
    virtual ~ClassFactory();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IClassFactory
    STDMETHODIMP CreateInstance(LPUNKNOWN pUnk, REFIID riid, void** ppv) override;
    STDMETHODIMP LockServer(BOOL fLock) override;

    static LONG s_serverLocks;

private:
    volatile LONG m_refCount;
};
