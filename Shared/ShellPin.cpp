#include "ShellPin.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <QFileInfo>
#include <QDir>

namespace esp {

// 通过 IDispatch 调用 Shell.Application 的 FolderItemVerbs（与 C# 版行为一致）
bool toggleTaskbarPin(const QString &path, bool pin)
{
    const QFileInfo fi(path);
    if (!fi.exists())
        return false;

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    const CLSID clsidShell = {0x13709620, 0xC279, 0x11CE, {0xA4, 0x9E, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
    IDispatch *shellDisp = nullptr;
    if (FAILED(CoCreateInstance(clsidShell, nullptr, CLSCTX_INPROC_SERVER,
                                IID_IDispatch, reinterpret_cast<void **>(&shellDisp))) || !shellDisp)
        return false;

    bool done = false;
    DISPID dispid = 0;
    const OLECHAR *nameSpaceName = L"NameSpace";
    const OLECHAR *parseNameName = L"ParseName";
    const OLECHAR *verbsName = L"Verbs";
    const OLECHAR *nameName = L"Name";
    const OLECHAR *doItName = L"DoIt";

    if (FAILED(shellDisp->GetIDsOfNames(IID_NULL, const_cast<LPOLESTR *>(&nameSpaceName), 1,
                                        LOCALE_USER_DEFAULT, &dispid)))
        goto done2;

    {
        VARIANT varDir;
        VariantInit(&varDir);
        varDir.vt = VT_BSTR;
        const std::wstring dirW = QDir::toNativeSeparators(fi.absolutePath()).toStdWString();
        varDir.bstrVal = SysAllocString(dirW.c_str());

        DISPPARAMS dp = {&varDir, nullptr, 1, 0};
        VARIANT res;
        VariantInit(&res);
        if (SUCCEEDED(shellDisp->Invoke(dispid, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD,
                                        &dp, &res, nullptr, nullptr)) &&
            res.vt == VT_DISPATCH && res.pdispVal) {
            IDispatch *folder = res.pdispVal;
            DISPID parseId = 0;
            if (SUCCEEDED(folder->GetIDsOfNames(IID_NULL, const_cast<LPOLESTR *>(&parseNameName), 1,
                                                LOCALE_USER_DEFAULT, &parseId))) {
                VARIANT varFile;
                VariantInit(&varFile);
                varFile.vt = VT_BSTR;
                const std::wstring fileW = fi.fileName().toStdWString();
                varFile.bstrVal = SysAllocString(fileW.c_str());

                DISPPARAMS dp2 = {&varFile, nullptr, 1, 0};
                VARIANT res2;
                VariantInit(&res2);
                if (SUCCEEDED(folder->Invoke(parseId, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD,
                                             &dp2, &res2, nullptr, nullptr)) &&
                    res2.vt == VT_DISPATCH && res2.pdispVal) {
                    IDispatch *item = res2.pdispVal;
                    DISPID verbsId = 0;
                    if (SUCCEEDED(item->GetIDsOfNames(IID_NULL, const_cast<LPOLESTR *>(&verbsName), 1,
                                                      LOCALE_USER_DEFAULT, &verbsId))) {
                        DISPPARAMS dp3 = {nullptr, nullptr, 0, 0};
                        VARIANT res3;
                        VariantInit(&res3);
                        if (SUCCEEDED(item->Invoke(verbsId, IID_NULL, LOCALE_USER_DEFAULT,
                                                   DISPATCH_METHOD | DISPATCH_PROPERTYGET,
                                                   &dp3, &res3, nullptr, nullptr)) &&
                            res3.vt == VT_DISPATCH && res3.pdispVal) {
                            IDispatch *verbs = res3.pdispVal;
                            DISPID countId = 0;
                            const OLECHAR *countName = L"Count";
                            if (SUCCEEDED(verbs->GetIDsOfNames(IID_NULL, const_cast<LPOLESTR *>(&countName), 1,
                                                               LOCALE_USER_DEFAULT, &countId))) {
                                DISPPARAMS dpc = {nullptr, nullptr, 0, 0};
                                VARIANT vc;
                                VariantInit(&vc);
                                if (SUCCEEDED(verbs->Invoke(countId, IID_NULL, LOCALE_USER_DEFAULT,
                                                            DISPATCH_METHOD | DISPATCH_PROPERTYGET,
                                                            &dpc, &vc, nullptr, nullptr)) && vc.vt == VT_I4) {
                                    const int count = vc.intVal;
                                    for (int i = 0; i < count && !done; ++i) {
                                        DISPID itemId = 0;
                                        const OLECHAR *itemName = L"Item";
                                        if (FAILED(verbs->GetIDsOfNames(IID_NULL, const_cast<LPOLESTR *>(&itemName), 1,
                                                                        LOCALE_USER_DEFAULT, &itemId)))
                                            continue;
                                        VARIANT varg;
                                        VariantInit(&varg);
                                        varg.vt = VT_I4;
                                        varg.intVal = i;
                                        DISPPARAMS dpi = {&varg, nullptr, 1, 0};
                                        VARIANT vri;
                                        VariantInit(&vri);
                                        if (FAILED(verbs->Invoke(itemId, IID_NULL, LOCALE_USER_DEFAULT,
                                                                 DISPATCH_METHOD | DISPATCH_PROPERTYGET,
                                                                 &dpi, &vri, nullptr, nullptr)) ||
                                            vri.vt != VT_DISPATCH || !vri.pdispVal)
                                            continue;
                                        IDispatch *verb = vri.pdispVal;
                                        DISPID nameId = 0;
                                        if (FAILED(verb->GetIDsOfNames(IID_NULL, const_cast<LPOLESTR *>(&nameName), 1,
                                                                        LOCALE_USER_DEFAULT, &nameId))) {
                                            verb->Release();
                                            continue;
                                        }
                                        DISPPARAMS dpn = {nullptr, nullptr, 0, 0};
                                        VARIANT vn;
                                        VariantInit(&vn);
                                        bool gotName = SUCCEEDED(verb->Invoke(nameId, IID_NULL, LOCALE_USER_DEFAULT,
                                                                             DISPATCH_METHOD | DISPATCH_PROPERTYGET,
                                                                             &dpn, &vn, nullptr, nullptr)) && vn.vt == VT_BSTR;
                                        QString verbName = gotName ? QString::fromWCharArray(vn.bstrVal) : QString();
                                        if (gotName) VariantClear(&vn);

                                        const QStringList wanted = pin
                                            ? QStringList{QStringLiteral("固定到任务栏"), QStringLiteral("Pin to taskbar")}
                                            : QStringList{QStringLiteral("从任务栏取消固定"), QStringLiteral("Unpin from taskbar")};
                                        if (wanted.contains(verbName)) {
                                            DISPID doItId = 0;
                                            if (SUCCEEDED(verb->GetIDsOfNames(IID_NULL, const_cast<LPOLESTR *>(&doItName), 1,
                                                                                LOCALE_USER_DEFAULT, &doItId))) {
                                                DISPPARAMS dpd = {nullptr, nullptr, 0, 0};
                                                VARIANT vrd;
                                                VariantInit(&vrd);
                                                if (SUCCEEDED(verb->Invoke(doItId, IID_NULL, LOCALE_USER_DEFAULT,
                                                                           DISPATCH_METHOD, &dpd, &vrd, nullptr, nullptr)))
                                                    done = true;
                                            }
                                        }
                                        verb->Release();
                                    }
                                }
                            }
                            verbs->Release();
                        }
                    }
                    item->Release();
                }
                VariantClear(&res2);
                SysFreeString(varFile.bstrVal);
            }
            folder->Release();
        }
        VariantClear(&res);
        SysFreeString(varDir.bstrVal);
    }

done2:
    shellDisp->Release();
    return done;
}

} // namespace esp
