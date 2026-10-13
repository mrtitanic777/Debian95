/* Debian95 — COM runtime (freestanding C++). A component object model of our
 * own: IUnknown (QueryInterface/AddRef/Release), IClassFactory, a class registry
 * and a CoCreateInstance-style activation path. Kernel-hosted for now (a
 * user-mode C++ runtime + loader arrives with the win32 subsystem/shell). */
#include <stddef.h>
extern "C" {
#include <debian95/mm.h>
#include <debian95/rtl.h>
#include <debian95/com.h>
}

typedef i32 HRESULT;
#define S_OK            ((HRESULT)0)
#define E_NOINTERFACE   ((HRESULT)0x80004002)
#define E_FAIL          ((HRESULT)0x80004005)
#define E_NOTREG        ((HRESULT)0x80040154)

/* ---- freestanding C++ support: heap new/delete + pure-virtual stub ---- */
void *operator new(size_t n)        { return kmalloc(n); }
void *operator new[](size_t n)      { return kmalloc(n); }
void  operator delete(void *p) noexcept          { kfree(p); }
void  operator delete[](void *p) noexcept        { kfree(p); }
void  operator delete(void *p, size_t) noexcept  { kfree(p); }
void  operator delete[](void *p, size_t) noexcept{ kfree(p); }
extern "C" void __cxa_pure_virtual(void) { for (;;) { } }

/* ---- GUIDs ---- */
struct GUID { u32 d1; u16 d2; u16 d3; u8 d4[8]; };

static bool guid_eq(const GUID &a, const GUID &b) {
    const u8 *x = (const u8 *)&a, *y = (const u8 *)&b;
    for (unsigned i = 0; i < sizeof(GUID); i++)
        if (x[i] != y[i]) return false;
    return true;
}

static const GUID IID_IUnknown = { 0, 0, 0, {0,0,0,0,0,0,0,0} };
static const GUID IID_IHello   = { 0x1111, 0, 0, {'H','E','L','L','O',0,0,0} };
static const GUID CLSID_Hello  = { 0x2222, 0, 0, {'C','L','S','H','L','O',0,0} };

/* ---- interfaces ---- */
struct IUnknown {
    virtual HRESULT QueryInterface(const GUID &iid, void **ppv) = 0;
    virtual u32     AddRef() = 0;
    virtual u32     Release() = 0;
};

struct IHello : IUnknown {
    virtual HRESULT SayHello() = 0;
    virtual HRESULT Add(int a, int b, int *out) = 0;
};

struct IClassFactory : IUnknown {
    virtual HRESULT CreateInstance(const GUID &iid, void **ppv) = 0;
};

/* ---- a concrete component ---- */
class HelloComponent final : public IHello {
    u32 m_ref;
public:
    HelloComponent() : m_ref(1) { }

    HRESULT QueryInterface(const GUID &iid, void **ppv) override {
        if (guid_eq(iid, IID_IUnknown) || guid_eq(iid, IID_IHello)) {
            *ppv = static_cast<IHello *>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    u32 AddRef() override { return ++m_ref; }
    u32 Release() override {
        u32 r = --m_ref;
        if (r == 0) delete this;
        return r;
    }
    HRESULT SayHello() override {
        kprintf("    [COM] HelloComponent::SayHello -> Hello from a COM object!\n");
        return S_OK;
    }
    HRESULT Add(int a, int b, int *out) override {
        if (!out) return E_FAIL;
        *out = a + b;
        return S_OK;
    }
};

class HelloFactory final : public IClassFactory {
    u32 m_ref;
public:
    HelloFactory() : m_ref(1) { }
    HRESULT QueryInterface(const GUID &iid, void **ppv) override {
        if (guid_eq(iid, IID_IUnknown)) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    u32 AddRef() override { return ++m_ref; }
    u32 Release() override { u32 r = --m_ref; if (r == 0) delete this; return r; }
    HRESULT CreateInstance(const GUID &iid, void **ppv) override {
        HelloComponent *c = new HelloComponent();     /* ref = 1 */
        if (!c) return E_FAIL;
        HRESULT hr = c->QueryInterface(iid, ppv);     /* ref = 2 on success */
        c->Release();                                 /* ref = 1, held by *ppv */
        return hr;
    }
};

/* ---- tiny class registry + activation ---- */
struct Registration { GUID clsid; IClassFactory *factory; };
static Registration g_reg[8];
static int          g_nreg;

static HRESULT CoRegisterClass(const GUID &clsid, IClassFactory *f) {
    if (g_nreg >= 8) return E_FAIL;
    g_reg[g_nreg].clsid = clsid;
    g_reg[g_nreg].factory = f;
    g_nreg++;
    return S_OK;
}

static HRESULT CoCreateInstance(const GUID &clsid, const GUID &iid, void **ppv) {
    for (int i = 0; i < g_nreg; i++)
        if (guid_eq(g_reg[i].clsid, clsid))
            return g_reg[i].factory->CreateInstance(iid, ppv);
    *ppv = nullptr;
    return E_NOTREG;
}

/* ---- demo ---- */
extern "C" void com_demo(void) {
    kprintf("\n  P8 COM runtime (C++):\n");

    IClassFactory *factory = new HelloFactory();
    CoRegisterClass(CLSID_Hello, factory);
    kprintf("    registered CLSID_Hello; activating via CoCreateInstance...\n");

    void *pv = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_Hello, IID_IHello, &pv);
    if (hr != S_OK) { kprintf("    CoCreateInstance failed %x\n", (u32)hr); return; }

    IHello *hello = (IHello *)pv;
    hello->SayHello();

    int sum = 0;
    hello->Add(19, 23, &sum);
    kprintf("    [COM] IHello::Add(19, 23) = %d\n", sum);

    void *unk = nullptr;
    HRESULT q = hello->QueryInterface(IID_IUnknown, &unk);
    kprintf("    [COM] QueryInterface(IUnknown) -> %s\n", q == S_OK ? "ok" : "fail");
    if (q == S_OK) ((IUnknown *)unk)->Release();

    u32 rc = hello->Release();
    kprintf("    [COM] final Release refcount=%u (0 = component destroyed)\n", rc);

    factory->Release();
}
