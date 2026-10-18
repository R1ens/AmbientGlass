#pragma once
// The shell's own library: music folders, recently played tracks and favourites, kept in library.txt.
// AIMP's playlists stay AIMP's; this adds what the sidebar needs on top of them.

#include <string>
#include <vector>

namespace glass {

struct LibTrack {
  std::wstring path, title, artist, album;
  double duration = 0;
};

struct FolderInfo {
  std::wstring path, name;
  int files = 0;  // audio files below it, counted by a scan; -1 until scanned
};

constexpr size_t kMaxRecent = 500;

class Library {
 public:
  const std::vector<std::wstring>& Folders() const { return m_folders; }
  const std::vector<LibTrack>& Recent() const { return m_recent; }  // newest first
  const std::vector<LibTrack>& Favorites() const { return m_favorites; }

  bool AddFolder(const std::wstring& path);  // false when it (or a parent of it) is already there
  bool RemoveFolder(const std::wstring& path);
  void Played(const LibTrack& t);            // moves it to the front of Recent, trims to kMaxRecent
  bool IsFavorite(const std::wstring& path) const;
  void SetFavorite(const LibTrack& t, bool on);

  bool Load(const std::wstring& file);
  bool Save(const std::wstring& file) const;

 private:
  std::vector<std::wstring> m_folders;
  std::vector<LibTrack> m_recent, m_favorites;
};

bool SamePath(const std::wstring& a, const std::wstring& b);  // case-insensitive, either slash, no trailing one
bool IsAudioFile(const std::wstring& path);                    // by extension
// Counts audio files under `root` (recursively, skipping hidden and system folders). Stops early when
// `cancel` becomes true.
int CountAudioFiles(const std::wstring& root, const volatile bool* cancel = nullptr);
std::wstring FolderName(const std::wstring& path);

}  // namespace glass
