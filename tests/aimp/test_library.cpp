// The shell's library: folders, recent, favourites, persistence and the audio file scan.
#include <windows.h>

#include <string>

#include "../../aimp/core/library.h"
#include "../test_framework.h"

using namespace glass;

namespace {

std::wstring TempDir(const wchar_t* name) {
  wchar_t tmp[MAX_PATH];
  GetTempPathW(MAX_PATH, tmp);
  std::wstring d = std::wstring(tmp) + name + std::to_wstring(GetCurrentProcessId());
  CreateDirectoryW(d.c_str(), nullptr);
  return d;
}

void Touch(const std::wstring& path) {
  HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
}

}  // namespace

TEST(library_folders_dedupe_and_nest) {
  Library lib;
  CHECK(lib.AddFolder(L"D:\\Music\\Rock"));
  CHECK(!lib.AddFolder(L"d:/music/rock/"));            // same folder, other spelling
  CHECK(!lib.AddFolder(L"D:\\Music\\Rock\\Live"));     // inside an existing one
  CHECK(lib.AddFolder(L"D:\\Music\\Jazz"));
  CHECK(lib.AddFolder(L"D:\\Music"));                  // the parent replaces both children
  CHECK_EQ(lib.Folders().size(), (size_t)1);
  CHECK(lib.Folders()[0] == L"D:\\Music");
  CHECK(lib.RemoveFolder(L"d:\\music\\"));
  CHECK(lib.Folders().empty());
  CHECK(!lib.AddFolder(L""));
}

TEST(library_recent_newest_first_unique_and_capped) {
  Library lib;
  for (int i = 0; i < 600; i++) lib.Played(LibTrack{L"C:\\t" + std::to_wstring(i) + L".mp3", L"T", L"A", L"B", 1});
  CHECK_EQ(lib.Recent().size(), kMaxRecent);
  CHECK(lib.Recent()[0].path == L"C:\\t599.mp3");
  lib.Played(LibTrack{L"c:\\T300.MP3", L"again", L"", L"", 2});
  CHECK(lib.Recent()[0].title == L"again");
  int count = 0;
  for (const LibTrack& t : lib.Recent()) count += SamePath(t.path, L"C:\\t300.mp3") ? 1 : 0;
  CHECK_EQ(count, 1);
}

TEST(library_favorites_toggle) {
  Library lib;
  LibTrack t{L"D:\\a.flac", L"A", L"B", L"C", 200};
  CHECK(!lib.IsFavorite(t.path));
  lib.SetFavorite(t, true);
  lib.SetFavorite(t, true);
  CHECK(lib.IsFavorite(L"d:/A.FLAC"));
  CHECK_EQ(lib.Favorites().size(), (size_t)1);
  lib.SetFavorite(t, false);
  CHECK(!lib.IsFavorite(t.path));
}

TEST(library_save_load_round_trip) {
  Library lib;
  lib.AddFolder(L"D:\\Музыка\\Новое");
  lib.Played(LibTrack{L"D:\\a\tb.mp3", L"Заголовок\nс переводом", L"Исп\\олнитель", L"Альбом", 123.5});
  lib.SetFavorite(LibTrack{L"D:\\fav.flac", L"Fav", L"Art", L"", 60}, true);
  std::wstring file = TempDir(L"aglib_") + L"\\library.txt";
  CHECK(lib.Save(file));
  Library back;
  CHECK(back.Load(file));
  CHECK_EQ(back.Folders().size(), (size_t)1);
  CHECK(back.Folders()[0] == L"D:\\Музыка\\Новое");
  CHECK_EQ(back.Recent().size(), (size_t)1);
  CHECK(back.Recent()[0].path == L"D:\\a\tb.mp3");
  CHECK(back.Recent()[0].title == L"Заголовок\nс переводом");
  CHECK(back.Recent()[0].artist == L"Исп\\олнитель");
  CHECK_NEAR(back.Recent()[0].duration, 123.5, 1e-6);
  CHECK(back.IsFavorite(L"D:\\fav.flac"));
  CHECK(!back.Load(file + L".missing"));
  DeleteFileW(file.c_str());
}

TEST(library_audio_scan) {
  std::wstring d = TempDir(L"agscan_");
  CreateDirectoryW((d + L"\\sub").c_str(), nullptr);
  Touch(d + L"\\a.mp3");
  Touch(d + L"\\b.FLAC");
  Touch(d + L"\\cover.jpg");
  Touch(d + L"\\sub\\c.m4a");
  Touch(d + L"\\sub\\notes.txt");
  CHECK_EQ(CountAudioFiles(d), 3);
  // Files of a folder first, in natural order, then its subfolders.
  Touch(d + L"\\10 - ten.mp3");
  Touch(d + L"\\2 - two.mp3");
  std::vector<std::wstring> list = ListAudioFiles(d);
  CHECK_EQ(list.size(), (size_t)5);
  CHECK(list[0] == d + L"\\2 - two.mp3");
  CHECK(list[1] == d + L"\\10 - ten.mp3");
  CHECK(list[4] == d + L"\\sub\\c.m4a");
  CHECK(IsAudioFile(L"x.Opus"));
  CHECK(!IsAudioFile(L"x.mp3.txt"));
  CHECK(!IsAudioFile(L"noext"));
  CHECK(FolderName(L"D:\\Music\\Rock\\") == L"Rock");
  CHECK(FolderName(L"D:\\") == L"D:\\");
  for (const wchar_t* f : {L"\\a.mp3", L"\\b.FLAC", L"\\cover.jpg", L"\\sub\\c.m4a", L"\\sub\\notes.txt",
                           L"\\10 - ten.mp3", L"\\2 - two.mp3"})
    DeleteFileW((d + f).c_str());
  RemoveDirectoryW((d + L"\\sub").c_str());
  RemoveDirectoryW(d.c_str());
}
