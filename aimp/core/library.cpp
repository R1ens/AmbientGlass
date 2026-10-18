#include "library.h"

#include <windows.h>

#include <algorithm>
#include <cwctype>

#include "common.h"

namespace glass {
namespace {

std::wstring Norm(std::wstring p) {
  for (wchar_t& c : p) c = c == L'/' ? L'\\' : (wchar_t)std::towlower(c);
  while (p.size() > 3 && p.back() == L'\\') p.pop_back();
  return p;
}

// Tabs and line breaks inside fields are escaped so every record stays on one line.
std::wstring Esc(const std::wstring& s) {
  std::wstring o;
  for (wchar_t c : s) {
    if (c == L'\\') o += L"\\\\";
    else if (c == L'\t') o += L"\\t";
    else if (c == L'\n') o += L"\\n";
    else if (c == L'\r') o += L"\\r";
    else o += c;
  }
  return o;
}

std::wstring Unesc(const std::wstring& s) {
  std::wstring o;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == L'\\' && i + 1 < s.size()) {
      wchar_t n = s[++i];
      o += n == L't' ? L'\t' : n == L'n' ? L'\n' : n == L'r' ? L'\r' : n;
    } else {
      o += s[i];
    }
  }
  return o;
}

std::vector<std::wstring> Split(const std::wstring& line) {
  std::vector<std::wstring> f;
  size_t i = 0;
  while (true) {
    size_t j = line.find(L'\t', i);
    f.push_back(Unesc(line.substr(i, j == std::wstring::npos ? std::wstring::npos : j - i)));
    if (j == std::wstring::npos) break;
    i = j + 1;
  }
  return f;
}

std::wstring TrackLine(wchar_t kind, const LibTrack& t) {
  wchar_t dur[32];
  swprintf_s(dur, L"%.3f", t.duration);
  return std::wstring(1, kind) + L"\t" + Esc(t.path) + L"\t" + Esc(t.title) + L"\t" + Esc(t.artist) + L"\t" +
         Esc(t.album) + L"\t" + dur + L"\n";
}

}  // namespace

bool SamePath(const std::wstring& a, const std::wstring& b) { return Norm(a) == Norm(b); }

std::wstring FolderName(const std::wstring& path) {
  std::wstring p = path;
  while (p.size() > 3 && (p.back() == L'\\' || p.back() == L'/')) p.pop_back();
  size_t s = p.find_last_of(L"\\/");
  return s == std::wstring::npos || s + 1 >= p.size() ? p : p.substr(s + 1);
}

bool IsAudioFile(const std::wstring& path) {
  static const wchar_t* kExt[] = {L".mp3", L".flac", L".m4a", L".aac", L".ogg", L".opus", L".wav", L".wma", L".ape",
                                  L".wv", L".alac", L".aiff", L".aif", L".dsf", L".dff", L".mpc", L".tta", L".tak",
                                  L".mka", L".webm", L".mp4", L".m4b", L".oga", L".spx"};
  size_t dot = path.find_last_of(L'.');
  if (dot == std::wstring::npos) return false;
  std::wstring e = path.substr(dot);
  for (wchar_t& c : e) c = (wchar_t)std::towlower(c);
  for (const wchar_t* k : kExt)
    if (e == k) return true;
  return false;
}

int CountAudioFiles(const std::wstring& root, const volatile bool* cancel) {
  int n = 0;
  std::vector<std::wstring> stack{root};
  while (!stack.empty()) {
    if (cancel && *cancel) break;
    std::wstring dir = stack.back();
    stack.pop_back();
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW((dir + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr,
                                FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) continue;
    do {
      std::wstring name = fd.cFileName;
      if (name == L"." || name == L"..") continue;
      if (fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) continue;
      if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) stack.push_back(dir + L"\\" + name);
      } else if (IsAudioFile(name)) {
        n++;
      }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
  }
  return n;
}

bool Library::AddFolder(const std::wstring& path) {
  std::wstring p = Norm(path);
  if (p.empty()) return false;
  for (const std::wstring& f : m_folders) {
    std::wstring nf = Norm(f);
    if (nf == p || (p.size() > nf.size() && p.compare(0, nf.size(), nf) == 0 && p[nf.size()] == L'\\')) return false;
  }
  // A new parent replaces the children it contains.
  m_folders.erase(std::remove_if(m_folders.begin(), m_folders.end(),
                                 [&](const std::wstring& f) {
                                   std::wstring nf = Norm(f);
                                   return nf.size() > p.size() && nf.compare(0, p.size(), p) == 0 && nf[p.size()] == L'\\';
                                 }),
                  m_folders.end());
  std::wstring keep = path;
  while (keep.size() > 3 && (keep.back() == L'\\' || keep.back() == L'/')) keep.pop_back();
  m_folders.push_back(keep);
  return true;
}

bool Library::RemoveFolder(const std::wstring& path) {
  auto it = std::find_if(m_folders.begin(), m_folders.end(), [&](const std::wstring& f) { return SamePath(f, path); });
  if (it == m_folders.end()) return false;
  m_folders.erase(it);
  return true;
}

void Library::Played(const LibTrack& t) {
  if (t.path.empty()) return;
  m_recent.erase(std::remove_if(m_recent.begin(), m_recent.end(), [&](const LibTrack& r) { return SamePath(r.path, t.path); }),
                 m_recent.end());
  m_recent.insert(m_recent.begin(), t);
  if (m_recent.size() > kMaxRecent) m_recent.resize(kMaxRecent);
}

bool Library::IsFavorite(const std::wstring& path) const {
  return std::any_of(m_favorites.begin(), m_favorites.end(), [&](const LibTrack& f) { return SamePath(f.path, path); });
}

void Library::SetFavorite(const LibTrack& t, bool on) {
  if (t.path.empty()) return;
  m_favorites.erase(
      std::remove_if(m_favorites.begin(), m_favorites.end(), [&](const LibTrack& f) { return SamePath(f.path, t.path); }),
      m_favorites.end());
  if (on) m_favorites.insert(m_favorites.begin(), t);
}

bool Library::Load(const std::wstring& file) {
  std::string data;
  if (!ReadAll(file, data)) return false;
  if (data.size() >= 3 && (unsigned char)data[0] == 0xEF) data.erase(0, 3);
  std::wstring text = FromUtf8(data);
  Library lib;
  size_t i = 0;
  while (i < text.size()) {
    size_t j = text.find(L'\n', i);
    std::wstring line = text.substr(i, j == std::wstring::npos ? std::wstring::npos : j - i);
    i = j == std::wstring::npos ? text.size() : j + 1;
    if (!line.empty() && line.back() == L'\r') line.pop_back();
    if (line.size() < 2 || line[1] != L'\t') continue;
    std::vector<std::wstring> f = Split(line.substr(2));
    if (line[0] == L'F' && !f.empty()) {
      lib.m_folders.push_back(f[0]);
    } else if ((line[0] == L'R' || line[0] == L'V') && f.size() >= 5) {
      LibTrack t{f[0], f[1], f[2], f[3], _wtof(f[4].c_str())};
      (line[0] == L'R' ? lib.m_recent : lib.m_favorites).push_back(t);
    }
  }
  if (lib.m_recent.size() > kMaxRecent) lib.m_recent.resize(kMaxRecent);
  *this = std::move(lib);
  return true;
}

bool Library::Save(const std::wstring& file) const {
  std::wstring text = L"# AimpGlass library: F folder, R recently played, V favourite\n";
  for (const std::wstring& f : m_folders) text += L"F\t" + Esc(f) + L"\n";
  for (const LibTrack& t : m_favorites) text += TrackLine(L'V', t);
  for (const LibTrack& t : m_recent) text += TrackLine(L'R', t);
  return WriteAll(file, ToUtf8(text));
}

}  // namespace glass
