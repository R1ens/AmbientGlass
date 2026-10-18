#pragma once
// Text the window shows that is computed, not translated: times, counts, list summaries.

#include <string>
#include <vector>

#include "model.h"

namespace glass {

// 0:05, 3:47, 1:02:03; negative and NaN show as 0:00.
std::wstring FormatTime(double seconds);
// "1 трек", "2 трека", "5 треков" (Russian plural forms).
std::wstring TrackCount(int n);
// "• 6 треков • 24:18" for the visible rows.
std::wstring ListSummary(const Model& m);
// "Исполнитель • Альбом", either may be missing.
std::wstring ArtistLine(const TrackInfo& t, const wchar_t* sep);
// "FLAC | 44.1 kHz | 16 bit | 867 kbps"; unknown parts are left out, empty when nothing is known.
std::wstring FormatLine(const TrackInfo& t);
// The format's short name: the file's extension in capitals (FLAC, MP3); for containers that may hold
// different codecs (M4A, MP4, MKA, WEBM) and for streams without an extension, the codec the player reports.
std::wstring FormatName(const std::wstring& path, const std::wstring& codec);

// Recomputes Model::visible from Model::query (all rows when the query is empty).
void ApplyQuery(Model& m);

}  // namespace glass
