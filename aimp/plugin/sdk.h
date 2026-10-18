#pragma once
// The AIMP SDK as the plugin uses it: headers, and small helpers for strings, services and COM-like objects.

#include <windows.h>

#include <string>

#include "IUnknownImpl.h"
#include "apiAlbumArt.h"
#include "apiCore.h"
#include "apiDecoders.h"
#include "apiFileManager.h"
#include "apiMenu.h"
#include "apiActions.h"
#include "apiMessages.h"
#include "apiObjects.h"
#include "apiPlayer.h"
#include "apiPlaylists.h"
#include "apiPlugin.h"
#include "apiThreading.h"
#include "apiVisuals.h"

namespace aimp {

// A reference to an AIMP object, released automatically (the SDK interfaces have no __uuidof).
template <class T>
class Ref {
 public:
  Ref() = default;
  Ref(const Ref& o) : p(o.p) { if (p) p->AddRef(); }
  Ref& operator=(const Ref& o) {
    if (o.p) o.p->AddRef();
    Reset();
    p = o.p;
    return *this;
  }
  ~Ref() { Reset(); }
  void Reset() {
    if (p) p->Release();
    p = nullptr;
  }
  T** Put() {
    Reset();
    return &p;
  }
  void** PutVoid() { return reinterpret_cast<void**>(Put()); }
  T* Get() const { return p; }
  T* operator->() const { return p; }
  explicit operator bool() const { return p != nullptr; }

 private:
  T* p = nullptr;
};

inline IAIMPCore*& Core() {
  static IAIMPCore* core = nullptr;
  return core;
}

template <class T>
Ref<T> Service(const GUID& iid) {
  Ref<T> s;
  if (Core()) Core()->QueryInterface(iid, s.PutVoid());
  return s;
}

inline Ref<IAIMPString> MakeString(const std::wstring& text) {
  Ref<IAIMPString> s;
  if (Core() && SUCCEEDED(Core()->CreateObject(IID_IAIMPString, s.PutVoid())) && s)
    s->SetData(const_cast<wchar_t*>(text.c_str()), (INT32)text.size());
  return s;
}

inline std::wstring Str(IAIMPString* s) { return s ? std::wstring(s->GetData(), s->GetLength()) : std::wstring(); }

inline std::wstring PropString(IAIMPPropertyList* list, int id) {
  Ref<IAIMPString> s;
  if (list && SUCCEEDED(list->GetValueAsObject(id, IID_IAIMPString, s.PutVoid()))) return Str(s.Get());
  return {};
}

}  // namespace aimp
